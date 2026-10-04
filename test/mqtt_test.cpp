// Host tests of the MQTT helpers (open-firenet/firenet_mqtt.h).
#include "firenet_mqtt.h"
#include <iostream>
#include <map>
using namespace firenet;

static int ok = 0, ko = 0;
static void CH(const char* n, bool c) { if (c) ok++; else { ko++; std::cout << "ECHEC " << n << "\n"; } }

int main() {
  // --- flattening of the state JSON into one topic per value
  {
    std::vector<std::pair<std::string, std::string>> f;
    mqtt::flattenSections("{\"stove\":{\"state\":\"heating\",\"state_code\":4,\"model\":null,\"state_label\":\"Grate Cleaning\"},"
                          "\"sensors\":{\"room_temperature\":21.5,\"deep\":{\"x\":1},\"last\":-3},\"top\":7,\"controls\":{\"on\":true}}", f);
    std::map<std::string, std::string> m(f.begin(), f.end());
    CH("flatten: string without quotes", m["stove/state"] == "heating");
    CH("flatten: number, null, boolean, negative", m["stove/state_code"] == "4" && m["stove/model"] == "null" && m["controls/on"] == "true" && m["sensors/last"] == "-3");
    CH("flatten: string with a space", m["stove/state_label"] == "Grate Cleaning");
    CH("flatten: float", m["sensors/room_temperature"] == "21.5");
    CH("flatten: deeper levels and top-level scalars ignored", f.size() == 7 && !m.count("sensors/x") && !m.count("deep/x") && !m.count("top"));
  }

  // --- command topics
  {
    std::string name = "x";
    CH("command: <base>/set carries a whole payload", mqtt::commandTopic("openfirenet", "openfirenet/set", name) && name.empty());
    CH("command: <base>/set/<name>", mqtt::commandTopic("openfirenet", "openfirenet/set/target_temperature", name) && name == "target_temperature");
    CH("command: base with a slash", mqtt::commandTopic("home/stove", "home/stove/set/on", name) && name == "on");
    CH("command: a state topic is not a command", !mqtt::commandTopic("openfirenet", "openfirenet/state", name));
    CH("command: <base>/set/ without a name is not a command", !mqtt::commandTopic("openfirenet", "openfirenet/set/", name));
    CH("command: another base is not a command", !mqtt::commandTopic("openfirenet", "openfirenet2/set/on", name));
    CH("command: <base>/settings is not a command", !mqtt::commandTopic("openfirenet", "openfirenet/settings", name));
  }

  std::cout << "mqtt: " << ok << " ok, " << ko << " failed\n";
  return ko ? 1 : 0;
}
