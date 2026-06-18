#include "audio_streamer.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include <cstring>
#include <cmath>
#include <errno.h>
#include <fcntl.h>

namespace esphome {
namespace audio_streamer {

static const char *const TAG = "audio_streamer";

void AudioStreamer::setup() {
  ESP_LOGCONFIG(TAG, "Setting up audio_streamer...");
  if (mic_ != nullptr && !callback_registered_) {
    mic_->add_data_callback([this](const std::vector<uint8_t> &data) {
      this->on_audio_data_(data);
    });
    callback_registered_ = true;
  }
}

void AudioStreamer::loop() {}

void AudioStreamer::dump_config() {
  ESP_LOGCONFIG(TAG, "Audio Streamer:");
  ESP_LOGCONFIG(TAG, "  Host: %s:%u", host_.c_str(), port_);
  ESP_LOGCONFIG(TAG, "  Silence threshold (RMS): %u", silence_threshold_);
  ESP_LOGCONFIG(TAG, "  Silence duration: %u ms", silence_duration_ms_);
  ESP_LOGCONFIG(TAG, "  Min duration: %u ms", min_duration_ms_);
}

bool AudioStreamer::open_socket_() {
  struct hostent *he = gethostbyname(host_.c_str());
  if (he == nullptr) {
    ESP_LOGE(TAG, "DNS resolution failed for %s", host_.c_str());
    return false;
  }

  sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (sock_ < 0) {
    ESP_LOGE(TAG, "socket() failed: errno=%d", errno);
    return false;
  }

  // Non-blocking connect with select() timeout so we don't hang the main loop
  int flags = ::fcntl(sock_, F_GETFL, 0);
  ::fcntl(sock_, F_SETFL, flags | O_NONBLOCK);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  memcpy(&addr.sin_addr, he->h_addr, he->h_length);

  int ret = ::connect(sock_, (struct sockaddr *) &addr, sizeof(addr));
  if (ret < 0 && errno != EINPROGRESS) {
    ESP_LOGE(TAG, "connect() failed: errno=%d", errno);
    ::close(sock_);
    sock_ = -1;
    return false;
  }

  fd_set wfds;
  FD_ZERO(&wfds);
  FD_SET(sock_, &wfds);
  struct timeval tv;
  tv.tv_sec = 2;
  tv.tv_usec = 0;

  ret = ::select(sock_ + 1, nullptr, &wfds, nullptr, &tv);
  if (ret <= 0) {
    ESP_LOGE(TAG, "connect() to %s:%u timed out", host_.c_str(), port_);
    ::close(sock_);
    sock_ = -1;
    return false;
  }

  int so_error = 0;
  socklen_t len = sizeof(so_error);
  ::getsockopt(sock_, SOL_SOCKET, SO_ERROR, &so_error, &len);
  if (so_error != 0) {
    ESP_LOGE(TAG, "connect() to %s:%u failed: errno=%d", host_.c_str(), port_, so_error);
    ::close(sock_);
    sock_ = -1;
    return false;
  }

  // Back to blocking, but with a send timeout so we never hang on send()
  ::fcntl(sock_, F_SETFL, flags);

  int flag = 1;
  ::setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

  struct timeval send_tv;
  send_tv.tv_sec = 1;
  send_tv.tv_usec = 0;
  ::setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, &send_tv, sizeof(send_tv));

  // Protocol header: [1 byte name length][name bytes]
  uint8_t name_len = static_cast<uint8_t>(std::min(name_.size(), (size_t) 255));
  if (::send(sock_, &name_len, 1, 0) != 1) {
    ESP_LOGE(TAG, "Failed to send name length");
    ::close(sock_);
    sock_ = -1;
    return false;
  }
  if (name_len > 0 && ::send(sock_, name_.c_str(), name_len, 0) != name_len) {
    ESP_LOGE(TAG, "Failed to send name");
    ::close(sock_);
    sock_ = -1;
    return false;
  }

  ESP_LOGI(TAG, "Connected to %s:%u as '%s'", host_.c_str(), port_, name_.c_str());
  return true;
}

void AudioStreamer::close_socket_(bool error) {
  if (sock_ >= 0) {
    ::close(sock_);
    sock_ = -1;
  }
  bool was_active = active_;
  active_ = false;
  if (was_active) {
    if (error) {
      on_error_callbacks_.call();
    } else {
      on_end_callbacks_.call();
    }
  }
}

void AudioStreamer::start() {
  if (active_) {
    ESP_LOGW(TAG, "Already streaming, ignoring start()");
    return;
  }
  if (mic_ == nullptr) {
    ESP_LOGE(TAG, "No microphone configured");
    on_error_callbacks_.call();
    return;
  }
  if (!open_socket_()) {
    on_error_callbacks_.call();
    return;
  }
  start_ms_ = millis();
  last_voice_ms_ = start_ms_;
  decim_acc_ = 0;
  decim_count_ = 0;
  active_ = true;
  mic_->start();
  auto si = mic_->get_audio_stream_info();
  ESP_LOGD(TAG, "Mic stream info: rate=%u Hz, channels=%u, bits=%u",
           (unsigned) si.get_sample_rate(), (unsigned) si.get_channels(),
           (unsigned) si.get_bits_per_sample());
  on_start_callbacks_.call();
  ESP_LOGI(TAG, "Streaming started");
}

void AudioStreamer::stop() {
  if (!active_) return;
  mic_->stop();
  close_socket_(false);
  ESP_LOGI(TAG, "Streaming stopped (manual)");
}

void AudioStreamer::on_audio_data_(const std::vector<uint8_t> &data) {
  if (!active_ || sock_ < 0) return;

  // El mic del fork entrega ya 16 kHz / 32-bit / STEREO interleaved [L,R,L,R,...].
  // El server espera 16 kHz / 16-bit / MONO. Por cada frame tomamos el canal R
  // (índice 1, el mismo que usa micro_wake_word) y pasamos 32→16 bits (16 bits
  // altos; el shift aritmético conserva el signo). SIN decimar: el rate ya es 16k.
  size_t n_frames = data.size() / 8;  // 2 canales * 4 bytes
  if (n_frames == 0) return;
  const int32_t *src = reinterpret_cast<const int32_t *>(data.data());

  std::vector<int16_t> pcm16(n_frames);
  uint64_t sum_sq = 0;
  for (size_t i = 0; i < n_frames; i++) {
    int16_t s16 = static_cast<int16_t>(src[i * 2 + 1] >> 16);  // canal R, 32→16 bits
    pcm16[i] = s16;
    int32_t v = s16;
    sum_sq += static_cast<uint64_t>(v * v);
  }

  uint16_t rms = static_cast<uint16_t>(std::sqrt((double) sum_sq / (double) n_frames));

  uint32_t now = millis();
  if (rms >= silence_threshold_) {
    last_voice_ms_ = now;
  }

  // Send chunk (best-effort; handle partial sends in a loop)
  const uint8_t *p = reinterpret_cast<const uint8_t *>(pcm16.data());
  size_t remaining = pcm16.size() * sizeof(int16_t);
  while (remaining > 0) {
    int sent = ::send(sock_, p, remaining, 0);
    if (sent <= 0) {
      ESP_LOGE(TAG, "send() failed: errno=%d", errno);
      mic_->stop();
      close_socket_(true);
      return;
    }
    p += sent;
    remaining -= sent;
  }

  if (max_duration_ms_ > 0 && (now - start_ms_ >= max_duration_ms_)) {
    ESP_LOGI(TAG, "Max duration reached (%u ms), closing stream", max_duration_ms_);
    mic_->stop();
    close_socket_(false);
    return;
  }

  if ((now - start_ms_ >= min_duration_ms_) &&
      (now - last_voice_ms_ >= silence_duration_ms_)) {
    ESP_LOGI(TAG, "Silence detected (RMS=%u), closing stream", rms);
    mic_->stop();
    close_socket_(false);
  }
}

}  // namespace audio_streamer
}  // namespace esphome
