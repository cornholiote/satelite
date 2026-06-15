#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/helpers.h"
#include "esphome/components/microphone/microphone.h"

#include <string>
#include <vector>

namespace esphome {
namespace audio_streamer {

class AudioStreamer : public Component {
 public:
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_microphone(microphone::Microphone *mic) { mic_ = mic; }
  void set_host(const std::string &host) { host_ = host; }
  void set_port(uint16_t port) { port_ = port; }
  void set_silence_threshold(uint16_t t) { silence_threshold_ = t; }
  void set_silence_duration(uint32_t ms) { silence_duration_ms_ = ms; }
  void set_min_duration(uint32_t ms) { min_duration_ms_ = ms; }
  void set_max_duration(uint32_t ms) { max_duration_ms_ = ms; }
  void set_name(const std::string &name) { name_ = name; }

  void start();
  void stop();

  void add_on_start_callback(std::function<void()> &&cb) { on_start_callbacks_.add(std::move(cb)); }
  void add_on_end_callback(std::function<void()> &&cb) { on_end_callbacks_.add(std::move(cb)); }
  void add_on_error_callback(std::function<void()> &&cb) { on_error_callbacks_.add(std::move(cb)); }

 protected:
  void on_audio_data_(const std::vector<uint8_t> &data);
  bool open_socket_();
  void close_socket_(bool error);

  microphone::Microphone *mic_{nullptr};
  std::string host_;
  std::string name_;
  uint16_t port_{0};
  uint16_t silence_threshold_{300};
  uint32_t silence_duration_ms_{1500};
  uint32_t min_duration_ms_{500};
  uint32_t max_duration_ms_{0};  // 0 = sin límite

  int sock_{-1};
  bool active_{false};
  bool callback_registered_{false};
  uint32_t last_voice_ms_{0};
  uint32_t start_ms_{0};

  CallbackManager<void()> on_start_callbacks_;
  CallbackManager<void()> on_end_callbacks_;
  CallbackManager<void()> on_error_callbacks_;
};

template<typename... Ts> class StartAction : public Action<Ts...> {
 public:
  explicit StartAction(AudioStreamer *parent) : parent_(parent) {}
  void play(const Ts &...) override { this->parent_->start(); }
 protected:
  AudioStreamer *parent_;
};

template<typename... Ts> class StopAction : public Action<Ts...> {
 public:
  explicit StopAction(AudioStreamer *parent) : parent_(parent) {}
  void play(const Ts &...) override { this->parent_->stop(); }
 protected:
  AudioStreamer *parent_;
};

class StartTrigger : public Trigger<> {
 public:
  explicit StartTrigger(AudioStreamer *parent) {
    parent->add_on_start_callback([this]() { this->trigger(); });
  }
};

class EndTrigger : public Trigger<> {
 public:
  explicit EndTrigger(AudioStreamer *parent) {
    parent->add_on_end_callback([this]() { this->trigger(); });
  }
};

class ErrorTrigger : public Trigger<> {
 public:
  explicit ErrorTrigger(AudioStreamer *parent) {
    parent->add_on_error_callback([this]() { this->trigger(); });
  }
};

}  // namespace audio_streamer
}  // namespace esphome
