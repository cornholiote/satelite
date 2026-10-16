#pragma once
// ESP-IDF runtime IPv4 configuration for Ambrosio STA.
// Experimental: requires validation on a test satellite before OTA rollout.
#include <string>
#include <cstring>
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs.h"
#include "lwip/ip4_addr.h"
#include "lwip/dns.h"
#include "esphome/core/log.h"

namespace ambrosio_network {
static constexpr const char *TAG = "ambrosio_net";
static constexpr const char *NAMESPACE = "ambnet";

inline bool parse(const std::string &text, uint32_t &out) {
  ip4_addr_t addr{};
  if (!ip4addr_aton(text.c_str(), &addr)) return false;
  out = addr.addr;
  return true;
}
inline bool valid(const std::string &ip, const std::string &mask,
                  const std::string &gateway, const std::string &dns) {
  uint32_t a, m, g, d;
  if (!parse(ip, a) || !parse(mask, m) || !parse(gateway, g) || !parse(dns, d)) return false;
  const uint32_t bits = ntohl(m);
  const uint32_t inv = ~bits;
  if (!bits || !inv || (inv & (inv + 1))) return false;
  if ((a & m) != (g & m) || a == g) return false;
  const uint32_t host = ntohl(a) & inv;
  return host != 0 && host != inv;
}
inline std::string read(const char *key, const char *fallback = "") {
  nvs_handle_t handle;
  if (nvs_open(NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return fallback;
  size_t size = 0;
  std::string value = fallback;
  if (nvs_get_str(handle, key, nullptr, &size) == ESP_OK && size > 0 && size < 100) {
    std::string buffer(size, '\0');
    if (nvs_get_str(handle, key, buffer.data(), &size) == ESP_OK) {
      buffer.resize(size - 1);
      value = buffer;
    }
  }
  nvs_close(handle);
  return value;
}
inline bool write(const char *key, const std::string &value) {
  nvs_handle_t handle;
  if (nvs_open(NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
  const esp_err_t result = nvs_set_str(handle, key, value.c_str());
  const esp_err_t commit = result == ESP_OK ? nvs_commit(handle) : result;
  nvs_close(handle);
  return commit == ESP_OK;
}
inline bool dhcp_ready() {
  auto *iface = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (!iface) return false;
  esp_netif_dhcp_status_t status{};
  esp_netif_ip_info_t info{};
  if (esp_netif_dhcpc_get_status(iface, &status) != ESP_OK ||
      esp_netif_get_ip_info(iface, &info) != ESP_OK) return false;
  return status == ESP_NETIF_DHCP_STARTED && info.ip.addr != 0 &&
         info.netmask.addr != 0 && info.gw.addr != 0;
}
inline void log_dhcp_state() {
  auto *iface = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (!iface) return;
  esp_netif_dhcp_status_t status{};
  esp_netif_ip_info_t info{};
  const auto sr = esp_netif_dhcpc_get_status(iface, &status);
  const auto ir = esp_netif_get_ip_info(iface, &info);
  ESP_LOGI(TAG, "DHCP state: status=%d status_err=%s ip_err=%s ip=" IPSTR " gateway=" IPSTR,
           static_cast<int>(status), esp_err_to_name(sr), esp_err_to_name(ir),
           IP2STR(&info.ip), IP2STR(&info.gw));
}
inline bool apply(const std::string &mode, const std::string &ip,
                  const std::string &mask, const std::string &gateway,
                  const std::string &dns) {
  auto *iface = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (!iface) { ESP_LOGE(TAG, "STA interface unavailable"); return false; }
  if (mode == "dhcp") {
    const auto stopped = esp_netif_dhcpc_stop(iface);
    ESP_LOGI(TAG, "DHCP stop result: %s", esp_err_to_name(stopped));
    const auto result = esp_netif_dhcpc_start(iface);
    ESP_LOGI(TAG, "DHCP start result: %s", esp_err_to_name(result));
    log_dhcp_state();
    return result == ESP_OK || result == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED;
  }
  if (mode != "static" || !valid(ip, mask, gateway, dns)) return false;
  esp_netif_ip_info_t info{};
  uint32_t addr;
  parse(ip, addr); info.ip.addr = addr;
  parse(mask, addr); info.netmask.addr = addr;
  parse(gateway, addr); info.gw.addr = addr;
  esp_netif_dhcpc_stop(iface);
  if (esp_netif_set_ip_info(iface, &info) != ESP_OK) return false;
  esp_netif_dns_info_t resolver{};
  resolver.ip.type = ESP_IPADDR_TYPE_V4;
  parse(dns, addr); resolver.ip.u_addr.ip4.addr = addr;
  return esp_netif_set_dns_info(iface, ESP_NETIF_DNS_MAIN, &resolver) == ESP_OK;
}
inline void restore_confirmed();
inline bool stage(const std::string &mode, const std::string &ip,
                  const std::string &mask, const std::string &gateway,
                  const std::string &dns) {
  if (mode != "dhcp" && mode != "static") return false;
  if (mode == "static" && !valid(ip, mask, gateway, dns)) return false;
  // Persist candidate as pending BEFORE changing the interface.
  // Any unexpected reboot while pending returns to the last confirmed config.
  if (!write("pending", "1")) return false;
  if (!apply(mode, ip, mask, gateway, dns)) {
    write("pending", "0");
    return false;
  }
  if (!write("candidate", mode + "|" + ip + "|" + mask + "|" + gateway + "|" + dns)) {
    restore_confirmed();
    write("pending", "0");
    return false;
  }
  // Keep the ESPHome-managed Wi-Fi association intact; changing IPv4 must not
  // trigger a competing reconnect cycle. The pending rollback remains active.
  return true;
}
inline void restore_confirmed() {
  auto config = read("confirmed", "dhcp||||");
  ESP_LOGI(TAG, "Restoring confirmed network configuration: %s", config.c_str());
  const auto first = config.find('|');
  if (first == std::string::npos) return;
  std::string fields[5];
  size_t start = 0;
  for (int i = 0; i < 4; ++i) {
    const auto pos = config.find('|', start);
    if (pos == std::string::npos) return;
    fields[i] = config.substr(start, pos - start);
    start = pos + 1;
  }
  fields[4] = config.substr(start);
  if (!apply(fields[0], fields[1], fields[2], fields[3], fields[4])) {
    ESP_LOGW(TAG, "Stored network failed, restoring DHCP");
    apply("dhcp", "", "", "", "");
  }
}
inline void confirm() {
  if (read("pending") != "1") {
    ESP_LOGW(TAG, "Confirm skipped: pending flag not set in NVS");
    return;
  }
  const auto candidate = read("candidate");
  if (candidate.empty()) {
    ESP_LOGE(TAG, "Confirm failed: candidate missing in NVS");
    return;
  }
  if (!write("confirmed", candidate)) {
    ESP_LOGE(TAG, "Confirm failed: could not persist confirmed network in NVS");
    return;
  }
  if (!write("pending", "0")) {
    ESP_LOGE(TAG, "Confirmed network saved but pending flag could not be cleared");
    return;
  }
  ESP_LOGI(TAG, "Confirmed network persisted in NVS: %s", read("confirmed").c_str());
}
}  // namespace ambrosio_network
