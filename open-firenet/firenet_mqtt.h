// firenet_mqtt.h -- Arduino-free helpers of the MQTT feature, tested on the host (test/mqtt_test.cpp). The MQTT
// client itself is Espressif's esp-mqtt, shipped with the ESP32 core (see the MQTT section of open-firenet.ino).
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace firenet {
namespace mqtt {

// Flattens {"section":{"key":value,...},...} into ("section/key", value) pairs: one MQTT topic per value. String
// values lose their quotes. Top-level scalars and deeper levels are ignored.
inline void flattenSections(const std::string& json, std::vector<std::pair<std::string, std::string>>& out) {
  auto ws = [&](size_t& p) { while (p < json.size() && (json[p] == ' ' || json[p] == '\n' || json[p] == '\t' || json[p] == '\r')) p++; };
  auto str = [&](size_t& p, std::string& v) {             // p on the opening quote; leaves p after the closing one
    size_t e = json.find('"', p + 1); if (e == std::string::npos) return false;
    v = json.substr(p + 1, e - p - 1); p = e + 1; return true;
  };
  std::string section;
  int depth = 0;
  size_t p = 0;
  while (p < json.size()) {
    char c = json[p];
    if (c == '{') { depth++; p++; continue; }
    if (c == '}') { depth--; if (depth == 1) section.clear(); p++; continue; }
    if (c != '"') { p++; continue; }
    std::string key, val;
    if (!str(p, key)) return;
    ws(p);
    if (p >= json.size() || json[p] != ':') continue;
    p++; ws(p);
    if (p >= json.size()) return;
    if (json[p] == '{') { if (depth == 1) section = key; continue; }
    if (json[p] == '"') { if (!str(p, val)) return; }
    else { size_t e = json.find_first_of(",}", p); if (e == std::string::npos) e = json.size(); val = json.substr(p, e - p); p = e; }
    if (depth == 2 && !section.empty()) out.push_back({section + "/" + key, val});
  }
}

// Name/value pairs commanded by a message: the JSON or "k=v;" payload of <base>/set, or the single value of
// <base>/set/<name>. Empty for any other topic. The split of the payload is left to the caller (firenet_api.h).
inline bool commandTopic(const std::string& base, const std::string& topic, std::string& name) {
  const std::string set = base + "/set";
  if (topic == set) { name.clear(); return true; }
  if (topic.size() > set.size() + 1 && topic.compare(0, set.size() + 1, set + "/") == 0) { name = topic.substr(set.size() + 1); return true; }
  return false;
}

}  // namespace mqtt
}  // namespace firenet
