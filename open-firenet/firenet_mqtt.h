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

// ---- Home Assistant MQTT discovery (issue #54)
// One retained configuration message per entity, under homeassistant/<component>/<device id>/<key>/config. The
// entities read the per-value topics (<base>/sensors/..., <base>/controls/...) and command <base>/set/<name>.
struct FeatureFlags {
  bool has_eco_mode = false;
  bool has_air_flaps = false;
  bool has_log_runtime = false;
  bool has_multiair1 = false;
  bool has_multiair2 = false;

  bool operator==(const FeatureFlags& o) const {
    return has_eco_mode == o.has_eco_mode &&
           has_air_flaps == o.has_air_flaps &&
           has_log_runtime == o.has_log_runtime &&
           has_multiair1 == o.has_multiair1 &&
           has_multiair2 == o.has_multiair2;
  }
  bool operator!=(const FeatureFlags& o) const { return !(*this == o); }
};

// ---- Home Assistant MQTT discovery (issue #54)
// One retained configuration message per entity, under homeassistant/<component>/<device id>/<key>/config. The
// entities read the per-value topics (<base>/sensors/..., <base>/controls/...) and command <base>/set/<name>.
struct DiscoveryDevice {
  std::string id;        // unique and stable, e.g. "openfirenet_45eac8"
  std::string base;      // base topic
  std::string model;     // stove model name, empty while unknown
  std::string version;   // bridge firmware version
  std::string url;       // configuration URL, e.g. "http://192.168.1.50"
  FeatureFlags features{};
};

enum FeatureReq {
  FEAT_NONE,
  FEAT_ECO_MODE,
  FEAT_AIR_FLAPS,
  FEAT_LOG_RUNTIME,
  FEAT_MULTIAIR1,
  FEAT_MULTIAIR2
};

namespace detail {
struct Entity { const char* component; const char* key; const char* name; const char* config; FeatureReq feat; };

// The values published as "null" (unknown) are turned into "None", which Home Assistant reads as "unknown".
#define OF_NULLABLE "\"value_template\":\"{{ None if value == 'null' else value }}\","
#define OF_BOOL "\"payload_on\":\"true\",\"payload_off\":\"false\","
#define OF_DIAG "\"entity_category\":\"diagnostic\","

// "~" is the base topic (Home Assistant expands it in every topic of the message).
static const Entity ENTITIES[] = {
  {"climate", "stove", nullptr,
   "\"modes\":[\"off\",\"heat\"],"
   "\"mode_state_topic\":\"~/controls/on\",\"mode_state_template\":\"{{ 'heat' if value == 'true' else 'off' }}\","
   "\"mode_command_topic\":\"~/set/on\",\"mode_command_template\":\"{{ 'true' if value == 'heat' else 'false' }}\","
   "\"temperature_state_topic\":\"~/controls/target_temperature\","
   "\"temperature_state_template\":\"{{ None if value == 'null' or value == '0' or value == '0.0' else value }}\","
   "\"temperature_command_topic\":\"~/set/target_temperature\","
   "\"current_temperature_topic\":\"~/sensors/room_temperature\","
   "\"current_temperature_template\":\"{{ None if value == 'null' else value }}\","
   "\"preset_modes\":[\"manual\",\"auto\",\"comfort\"],"
   "\"preset_mode_state_topic\":\"~/controls/mode\",\"preset_mode_command_topic\":\"~/set/mode\","
   "\"action_topic\":\"~/state\","
   "\"action_template\":\"{% if not value_json.controls.on %}off{% elif value_json.stove.is_burning %}heating{% else %}idle{% endif %}\","
   "\"min_temp\":14,\"max_temp\":28,\"temp_step\":1,\"temperature_unit\":\"C\","
   "\"json_attributes_topic\":\"~/state\","
   "\"json_attributes_template\":\"{{ {'entity_picture': value_json.stove.status_picture} | tojson }}\",",
   FEAT_NONE},

  {"number", "power_percent", "Heating Power",
   "\"state_topic\":\"~/controls/power_percent\",\"command_topic\":\"~/set/power_percent\","
   "\"min\":30,\"max\":100,\"step\":5,\"unit_of_measurement\":\"%\",\"icon\":\"mdi:fire\","
   "\"entity_category\":\"config\",",
   FEAT_NONE},
  {"number", "setback_temperature", "Setback Temperature",
   "\"state_topic\":\"~/controls/setback_temperature\",\"command_topic\":\"~/set/setback_temperature\","
   "\"min\":10,\"max\":25,\"step\":0.5,\"unit_of_measurement\":\"°C\",\"device_class\":\"temperature\",\"entity_category\":\"config\",",
   FEAT_NONE},
  {"number", "frost_protection_temperature", "Frost Protection Temperature",
   "\"state_topic\":\"~/controls/frost_protection_temperature\",\"command_topic\":\"~/set/frost_protection_temperature\","
   "\"min\":4,\"max\":10,\"step\":1,\"unit_of_measurement\":\"°C\",\"device_class\":\"temperature\",\"entity_category\":\"config\",",
   FEAT_NONE},
  {"number", "room_temperature_offset", "Room Temperature Offset",
   "\"state_topic\":\"~/controls/room_temperature_offset\",\"command_topic\":\"~/set/room_temperature_offset\","
   "\"min\":-4,\"max\":4,\"step\":0.1,\"unit_of_measurement\":\"°C\",\"device_class\":\"temperature\",\"entity_category\":\"config\",",
   FEAT_NONE},
  {"number", "room_power_request", "Room Power Request",
   "\"state_topic\":\"~/controls/room_power_request\",\"command_topic\":\"~/set/room_power_request\","
   "\"min\":1,\"max\":4,\"step\":1,\"icon\":\"mdi:speedometer\",\"entity_category\":\"config\",",
   FEAT_NONE},
  {"number", "convection_fan1_level", "Convection Fan 1 Level",
   "\"state_topic\":\"~/controls/convection_fan1_level\",\"command_topic\":\"~/set/convection_fan1_level\","
   "\"min\":0,\"max\":5,\"step\":1,\"icon\":\"mdi:fan\",\"entity_category\":\"config\",",
   FEAT_MULTIAIR1},
  {"number", "convection_fan1_area", "Convection Fan 1 Area",
   "\"state_topic\":\"~/controls/convection_fan1_area\",\"command_topic\":\"~/set/convection_fan1_area\","
   "\"min\":-30,\"max\":30,\"step\":1,\"unit_of_measurement\":\"%\",\"icon\":\"mdi:arrow-split-horizontal\",\"entity_category\":\"config\",",
   FEAT_MULTIAIR1},
  {"number", "convection_fan2_level", "Convection Fan 2 Level",
   "\"state_topic\":\"~/controls/convection_fan2_level\",\"command_topic\":\"~/set/convection_fan2_level\","
   "\"min\":0,\"max\":5,\"step\":1,\"icon\":\"mdi:fan\",\"entity_category\":\"config\",",
   FEAT_MULTIAIR2},
  {"number", "convection_fan2_area", "Convection Fan 2 Area",
   "\"state_topic\":\"~/controls/convection_fan2_area\",\"command_topic\":\"~/set/convection_fan2_area\","
   "\"min\":-30,\"max\":30,\"step\":1,\"unit_of_measurement\":\"%\",\"icon\":\"mdi:arrow-split-horizontal\",\"entity_category\":\"config\",",
   FEAT_MULTIAIR2},

  {"switch", "on_off", "On Off",
   "\"state_topic\":\"~/controls/on\",\"command_topic\":\"~/set/on\"," OF_BOOL
   "\"icon\":\"mdi:power\",",
   FEAT_NONE},
  {"switch", "heating_schedule", "Heating Schedule",
   "\"state_topic\":\"~/controls/heating_times_active\",\"command_topic\":\"~/set/heating_times_active\"," OF_BOOL
   "\"icon\":\"mdi:calendar-clock\",",
   FEAT_NONE},
  {"switch", "frost_protection", "Frost Protection",
   "\"state_topic\":\"~/controls/frost_protection_active\",\"command_topic\":\"~/set/frost_protection_active\"," OF_BOOL
   "\"icon\":\"mdi:snowflake\",",
   FEAT_NONE},
  {"switch", "eco_mode", "Eco Mode",
   "\"state_topic\":\"~/controls/eco_mode\",\"command_topic\":\"~/set/eco_mode\"," OF_BOOL "\"icon\":\"mdi:leaf\",",
   FEAT_ECO_MODE},
  {"switch", "convection_fan1", "Convection Fan 1",
   "\"state_topic\":\"~/controls/convection_fan1_active\",\"command_topic\":\"~/set/convection_fan1_active\"," OF_BOOL
   "\"icon\":\"mdi:fan\",",
   FEAT_MULTIAIR1},
  {"switch", "convection_fan2", "Convection Fan 2",
   "\"state_topic\":\"~/controls/convection_fan2_active\",\"command_topic\":\"~/set/convection_fan2_active\"," OF_BOOL
   "\"icon\":\"mdi:fan\",",
   FEAT_MULTIAIR2},

  {"sensor", "room_temperature", "Room Temperature",
   "\"state_topic\":\"~/sensors/room_temperature\"," OF_NULLABLE
   "\"unit_of_measurement\":\"°C\",\"device_class\":\"temperature\",\"state_class\":\"measurement\",",
   FEAT_NONE},
  {"sensor", "combustion_temperature", "Combustion Chamber Temperature",
   "\"state_topic\":\"~/sensors/combustion_temperature\","
   "\"unit_of_measurement\":\"°C\",\"device_class\":\"temperature\",\"state_class\":\"measurement\",",
   FEAT_NONE},
  {"sensor", "board_temperature", "Board Temperature",
   "\"state_topic\":\"~/sensors/board_temperature\","
   "\"unit_of_measurement\":\"°C\",\"device_class\":\"temperature\",\"state_class\":\"measurement\"," OF_DIAG,
   FEAT_NONE},
  {"sensor", "pellets_total_kg", "Pellets Consumed",
   "\"state_topic\":\"~/sensors/pellets_total_kg\","
   "\"unit_of_measurement\":\"kg\",\"device_class\":\"weight\",\"state_class\":\"total_increasing\"," OF_DIAG,
   FEAT_NONE},
  {"sensor", "pellet_hours", "Pellet Operating Hours",
   "\"state_topic\":\"~/sensors/pellet_hours\","
   "\"unit_of_measurement\":\"h\",\"device_class\":\"duration\",\"state_class\":\"total_increasing\"," OF_DIAG,
   FEAT_NONE},
  {"sensor", "pellet_hours_logs", "Logs Operating Hours",
   "\"state_topic\":\"~/sensors/log_runtime_hours\","
   "\"unit_of_measurement\":\"h\",\"device_class\":\"duration\",\"state_class\":\"total_increasing\"," OF_DIAG,
   FEAT_LOG_RUNTIME},
  {"sensor", "service_countdown_kg", "Service Countdown",
   "\"state_topic\":\"~/sensors/service_countdown_kg\",\"unit_of_measurement\":\"kg\",\"device_class\":\"weight\"," OF_DIAG,
   FEAT_NONE},
  {"sensor", "fan_speed_rpm", "Flue Draft Fan Speed",
   "\"state_topic\":\"~/sensors/fan_speed_rpm\",\"unit_of_measurement\":\"rpm\",\"state_class\":\"measurement\",\"icon\":\"mdi:fan\"," OF_DIAG,
   FEAT_NONE},
  {"sensor", "auger_speed_rpm", "Pellet Auger Speed",
   "\"state_topic\":\"~/sensors/auger_speed_rpm\",\"unit_of_measurement\":\"rpm\",\"state_class\":\"measurement\",\"icon\":\"mdi:screw-lag\"," OF_DIAG,
   FEAT_NONE},
  {"sensor", "air_flaps_percent", "Air Flaps",
   "\"state_topic\":\"~/sensors/air_flaps_percent\"," OF_NULLABLE
   "\"unit_of_measurement\":\"%\",\"state_class\":\"measurement\"," OF_DIAG,
   FEAT_AIR_FLAPS},
  {"sensor", "air_flaps_target_percent", "Air Flaps Target",
   "\"state_topic\":\"~/sensors/air_flaps_target_percent\"," OF_NULLABLE
   "\"unit_of_measurement\":\"%\",\"state_class\":\"measurement\"," OF_DIAG,
   FEAT_AIR_FLAPS},
  {"sensor", "stove_state", "Stove State",
   "\"state_topic\":\"~/stove/state\",\"device_class\":\"enum\","
   "\"options\":[\"off\",\"standby\",\"ignition\",\"flame_start\",\"heating\",\"cleaning\",\"burn_off\",\"splitlog\",\"unknown\"],"
   "\"icon\":\"mdi:fireplace\",",
   FEAT_NONE},
  {"sensor", "stove_status", "Stove Status",
   "\"state_topic\":\"~/stove/status_text\",\"icon\":\"mdi:information-outline\",",
   FEAT_NONE},
  {"sensor", "status_picture", "Stove Status Picture",
   "\"state_topic\":\"~/stove/status_picture\",\"icon\":\"mdi:image\",\"enabled_by_default\":false,",
   FEAT_NONE},
  {"sensor", "main_state", "Main State Code",
   "\"state_topic\":\"~/stove/state_code\",\"icon\":\"mdi:information-outline\"," OF_DIAG,
   FEAT_NONE},
  {"sensor", "sub_state", "Sub State Code",
   "\"state_topic\":\"~/stove/sub_state\",\"icon\":\"mdi:information-outline\"," OF_DIAG,
   FEAT_NONE},
  {"sensor", "error_code", "Error Code", "\"state_topic\":\"~/stove/error_code\"," OF_DIAG,
   FEAT_NONE},
  {"sensor", "error_sub", "Error Sub-code", "\"state_topic\":\"~/stove/error_sub\"," OF_DIAG,
   FEAT_NONE},
  {"sensor", "warning_code", "Warning Code", "\"state_topic\":\"~/stove/warning_code\"," OF_DIAG,
   FEAT_NONE},
  {"sensor", "wifi_strength", "Wi-Fi Signal Strength",
   "\"state_topic\":\"~/sensors/wifi_rssi\",\"unit_of_measurement\":\"dBm\",\"device_class\":\"signal_strength\",\"state_class\":\"measurement\"," OF_DIAG,
   FEAT_NONE},

  {"binary_sensor", "burning", "Combustion Active",
   "\"state_topic\":\"~/stove/is_burning\"," OF_BOOL "\"icon\":\"mdi:fire\",",
   FEAT_NONE},
  {"binary_sensor", "problem", "Stove Error",
   "\"state_topic\":\"~/stove/has_error\"," OF_BOOL "\"device_class\":\"problem\",",
   FEAT_NONE},
  {"binary_sensor", "door", "Door",
   "\"state_topic\":\"~/sensors/door_open\"," OF_BOOL "\"device_class\":\"door\"," OF_DIAG,
   FEAT_NONE},
  {"binary_sensor", "pellet_lid", "Pellet Lid",
   "\"state_topic\":\"~/sensors/hopper_open\"," OF_BOOL "\"device_class\":\"opening\"," OF_DIAG,
   FEAT_NONE},
  {"binary_sensor", "grid_contact", "Grid Contact Problem",
   "\"state_topic\":\"~/sensors/grid_problem\"," OF_BOOL "\"device_class\":\"problem\"," OF_DIAG,
   FEAT_NONE},
  {"binary_sensor", "external_request", "External Request",
   "\"state_topic\":\"~/sensors/external_request\"," OF_BOOL "\"device_class\":\"running\"," OF_DIAG,
   FEAT_NONE},
  {"binary_sensor", "connected", "Connected",
   "\"state_topic\":\"~/device/connected\"," OF_BOOL "\"device_class\":\"connectivity\"," OF_DIAG,
   FEAT_NONE},
};
#undef OF_NULLABLE
#undef OF_BOOL
#undef OF_DIAG

inline std::string jsonEscape(const std::string& in) {
  std::string out;
  for (char c : in) {
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if ((unsigned char)c >= 0x20) out += c;
  }
  return out;
}
}  // namespace detail

inline size_t discoveryCount() { return sizeof(detail::ENTITIES) / sizeof(detail::ENTITIES[0]); }

// Topic and configuration message of entity i. With remove = true the message is empty, which deletes the entity
// from Home Assistant. Returns false past the last entity.
inline bool discoveryEntity(size_t i, const DiscoveryDevice& dev, bool remove, std::string& topic, std::string& payload) {
  if (i >= discoveryCount()) return false;
  const detail::Entity& e = detail::ENTITIES[i];
  topic = std::string("homeassistant/") + e.component + "/" + dev.id + "/" + e.key + "/config";
  payload.clear();
  if (remove) return true;

  bool supported = true;
  switch (e.feat) {
    case FEAT_ECO_MODE:     supported = dev.features.has_eco_mode; break;
    case FEAT_AIR_FLAPS:    supported = dev.features.has_air_flaps; break;
    case FEAT_LOG_RUNTIME:  supported = dev.features.has_log_runtime; break;
    case FEAT_MULTIAIR1:    supported = dev.features.has_multiair1; break;
    case FEAT_MULTIAIR2:    supported = dev.features.has_multiair2; break;
    case FEAT_NONE: default: break;
  }
  if (!supported) return true;  // empty payload removes or masks the entity in HA

  const bool linkEntity = std::string(e.key) == "connected";
  payload = "{\"~\":\"" + detail::jsonEscape(dev.base) + "\",";
  payload += e.name ? "\"name\":\"" + std::string(e.name) + "\"," : std::string("\"name\":null,");
  payload += "\"unique_id\":\"" + dev.id + "_" + e.key + "\",";
  payload += e.config;
  // Available while the bridge is online and, except for the link sensor itself, while the stove is linked.
  payload += "\"availability\":[{\"topic\":\"~/availability\"}";
  if (!linkEntity) payload += ",{\"topic\":\"~/device/connected\",\"payload_available\":\"true\",\"payload_not_available\":\"false\"}";
  payload += "],\"availability_mode\":\"all\",";
  // The device is described in every message: Home Assistant may read them in any order.
  payload += "\"device\":{\"identifiers\":[\"" + dev.id + "\"],\"name\":\"Open Firenet\",\"manufacturer\":\"RIKA\",\"model\":\"" +
             detail::jsonEscape(dev.model.empty() ? "Pellet stove" : dev.model) + "\"";
  if (!dev.version.empty()) payload += ",\"sw_version\":\"" + detail::jsonEscape(dev.version) + "\"";
  if (!dev.url.empty()) payload += ",\"configuration_url\":\"" + detail::jsonEscape(dev.url) + "\"";
  payload += "}}";
  return true;
}

}  // namespace mqtt
}  // namespace firenet
