// Host tests of the MQTT client (open-firenet/firenet_mqtt.h): packet encoding checked byte by byte against the
// MQTT 3.1.1 specification, the session state machine against a scripted broker, and the JSON flattening.
// With a broker address as arguments (host port [user pass]) it also runs a round trip against a real broker.
#include "firenet_mqtt.h"
#include <iostream>
#include <deque>
#include <map>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <chrono>
#include <thread>
using namespace firenet;

static int ok = 0, ko = 0;
static void CH(const char* n, bool c) { if (c) ok++; else { ko++; std::cout << "ECHEC " << n << "\n"; } }

struct FakeTransport : MqttTransport {
  bool up = true; std::string sent; std::deque<uint8_t> in; size_t writeLimit = SIZE_MAX;
  bool connected() override { return up; }
  int read(uint8_t* b, size_t n) override { size_t k = 0; while (k < n && !in.empty()) { b[k++] = in.front(); in.pop_front(); } return (int)k; }
  size_t write(const uint8_t* b, size_t n) override { if (n > writeLimit) return 0; sent.append((const char*)b, n); return n; }
  void stop() override { up = false; }
  void push(const std::string& s) { for (char c : s) in.push_back((uint8_t)c); }
};

struct SockTransport : MqttTransport {
  int fd = -1;
  bool open(const char* host, const char* port) {
    addrinfo hints{}, *res = nullptr; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) return false;
    fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    bool okc = fd >= 0 && ::connect(fd, res->ai_addr, res->ai_addrlen) == 0;
    freeaddrinfo(res);
    if (okc) fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); else stop();
    return okc;
  }
  bool connected() override { return fd >= 0; }
  int read(uint8_t* b, size_t n) override { ssize_t r = ::recv(fd, b, n, 0); if (r == 0) { stop(); return 0; } return r < 0 ? 0 : (int)r; }
  size_t write(const uint8_t* b, size_t n) override { ssize_t r = ::send(fd, b, n, MSG_NOSIGNAL); return r < 0 ? 0 : (size_t)r; }
  void stop() override { if (fd >= 0) ::close(fd); fd = -1; }
};

static uint32_t g_now = 0;
static uint32_t realNow() { return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

static int realBroker(int argc, char** argv) {
  const std::string base = "openfirenet-test-" + std::to_string(getpid());
  SockTransport ta, tb;
  MqttClient a(ta, realNow), b(tb, realNow);
  a.setClientId(base + "-a"); b.setClientId(base + "-b");
  if (argc > 4) { a.setCredentials(argv[3], argv[4]); b.setCredentials(argv[3], argv[4]); }
  a.setWill(base + "/availability", "offline", true);
  std::vector<std::pair<std::string, std::string>> got;
  b.onMessage([&](const std::string& t, const std::string& p, bool) { got.push_back({t, p}); });
  b.onConnect([&] { b.subscribe(base + "/#"); });
  CH("real: TCP a", ta.open(argv[1], argv[2])); CH("real: TCP b", tb.open(argv[1], argv[2]));
  a.begin(); b.begin();
  auto spin = [&](int ms) { for (uint32_t s = realNow(); realNow() - s < (uint32_t)ms;) { a.loop(); b.loop(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); } };
  spin(500);
  CH("real: a accepted", a.connected() && a.lastError() == 0); CH("real: b accepted", b.connected());
  a.publish(base + "/availability", "online", true);
  a.publish(base + "/state", std::string(1500, 'x'));
  a.publish(base + "/sensors/room_temperature", "21.5", true);
  spin(500);
  CH("real: three messages delivered", got.size() == 3);
  CH("real: payload of 1500 bytes intact", got.size() == 3 && got[1].second == std::string(1500, 'x'));
  CH("real: topic and value", got.size() == 3 && got[2].first == base + "/sensors/room_temperature" && got[2].second == "21.5");
  ::shutdown(ta.fd, SHUT_RDWR); ::close(ta.fd); ta.fd = -1;      // a dies without DISCONNECT: the broker sends its will
  spin(700);
  CH("real: last will delivered", got.size() == 4 && got[3].first == base + "/availability" && got[3].second == "offline");
  // clear the retained test messages
  SockTransport tc; MqttClient c(tc, realNow); c.setClientId(base + "-c"); if (argc > 4) c.setCredentials(argv[3], argv[4]);
  tc.open(argv[1], argv[2]); c.begin();
  for (uint32_t s = realNow(); realNow() - s < 300;) c.loop();
  c.publish(base + "/availability", "", true); c.publish(base + "/sensors/room_temperature", "", true);
  for (uint32_t s = realNow(); realNow() - s < 200;) c.loop();
  c.disconnect(); b.disconnect();
  if (argc > 4) {
    SockTransport td; MqttClient d(td, realNow); d.setClientId(base + "-d"); d.setCredentials(argv[3], "wrong-password");
    td.open(argv[1], argv[2]); d.begin();
    for (uint32_t s = realNow(); realNow() - s < 500;) d.loop();
    CH("real: wrong password refused with code 4 or 5", !d.connected() && (d.lastError() == 4 || d.lastError() == 5));
  }
  std::cout << "mqtt real broker: " << ok << " ok, " << ko << " failed\n";
  return ko ? 1 : 0;
}

int main(int argc, char** argv) {
  if (argc > 2) return realBroker(argc, argv);

  // --- encoding (MQTT 3.1.1, sections 2.2.3 and 3.x)
  { std::string s; mqtt::putLen(s, 0); CH("len 0", s == std::string("\x00", 1)); }
  { std::string s; mqtt::putLen(s, 127); CH("len 127", s == "\x7f"); }
  { std::string s; mqtt::putLen(s, 128); CH("len 128", s == std::string("\x80\x01", 2)); }
  { std::string s; mqtt::putLen(s, 16383); CH("len 16383", s == "\xff\x7f"); }
  { std::string s; mqtt::putLen(s, 16384); CH("len 16384", s == std::string("\x80\x80\x01", 3)); }
  CH("CONNECT without user or will",
     mqtt::connectPacket("id", "", "", "", "", false, 30) == std::string("\x10\x0e\x00\x04MQTT\x04\x02\x00\x1e\x00\x02id", 16));
  CH("CONNECT with user, password and retained will",
     mqtt::connectPacket("id", "u", "p", "t", "off", true, 30) ==
       std::string("\x10\x1c\x00\x04MQTT\x04\xe6\x00\x1e\x00\x02id\x00\x01t\x00\x03off\x00\x01u\x00\x01p", 30));
  CH("CONNECT: a password without a user is not sent",
     mqtt::connectPacket("id", "", "p", "", "", false, 30)[9] == 0x02);
  CH("PUBLISH", mqtt::publishPacket("a/b", "hi", false) == std::string("\x30\x07\x00\x03" "a/bhi", 9));
  CH("PUBLISH retained", mqtt::publishPacket("a/b", "hi", true)[0] == 0x31);
  CH("PUBLISH of 200 bytes uses a two-byte length", mqtt::publishPacket("t", std::string(200, 'x'), false).substr(0, 3) == std::string("\x30\xcb\x01", 3));
  CH("SUBSCRIBE", mqtt::subscribePacket(1, "a/#") == std::string("\x82\x08\x00\x01\x00\x03" "a/#\x00", 10));

  // --- session
  {
    FakeTransport t; MqttClient c(t, [] { return g_now; });
    std::vector<std::pair<std::string, std::string>> got; int connects = 0, retainedSeen = 0;
    c.onMessage([&](const std::string& tp, const std::string& p, bool r) { got.push_back({tp, p}); retainedSeen += r; });
    c.onConnect([&] { connects++; c.subscribe("base/set/#"); });
    CH("not connected before begin", !c.connected() && !c.busy() && c.lastError() == -4);
    CH("publish refused before the session is accepted", !c.publish("x", "y"));
    g_now = 1000; c.begin();
    CH("begin sends CONNECT", t.sent.size() > 2 && t.sent[0] == 0x10);
    CH("busy while waiting for CONNACK, not connected yet", c.busy() && !c.connected());
    t.push(std::string("\x20\x02\x00\x00", 4)); c.loop();
    CH("CONNACK 0: connected, onConnect called once", c.connected() && connects == 1 && c.lastError() == 0);
    CH("subscribe sent from onConnect", t.sent.find(std::string("\x82\x0f\x00\x01\x00\x0a" "base/set/#\x00", 17)) != std::string::npos);
    t.sent.clear();
    CH("publish goes out", c.publish("base/state", "{}", true) && t.sent == mqtt::publishPacket("base/state", "{}", true));
    // incoming messages, split across reads and two in a row
    std::string in = mqtt::publishPacket("base/set/on", "true", false) + mqtt::publishPacket("base/set", "{\"mode\":\"auto\"}", false);
    t.push(in.substr(0, 5)); c.loop(); CH("partial packet: nothing delivered", got.empty());
    t.push(in.substr(5)); c.loop();
    CH("two messages delivered", got.size() == 2 && got[0].first == "base/set/on" && got[0].second == "true" && got[1].second == "{\"mode\":\"auto\"}");
    t.push(std::string("\x32\x08\x00\x01t\x00\x07" "abc", 10)); c.loop();
    CH("QoS 1 message: packet id skipped", got.size() == 3 && got[2].first == "t" && got[2].second == "abc");
    t.push(std::string("\x30\x80\x20", 3) + std::string(4096, 'z') + mqtt::publishPacket("after", "big", false)); c.loop();
    CH("oversized packet skipped, next one delivered", got.size() == 4 && got[3].first == "after");
    CH("live messages are not flagged retained", retainedSeen == 0);
    t.push(mqtt::publishPacket("base/set/on", "true", true)); c.loop();
    CH("replayed message flagged retained", got.size() == 5 && retainedSeen == 1);
    got.pop_back();
    t.push(std::string("\x30\x03\x00\x09x", 5)); c.loop();
    CH("topic length beyond the packet: ignored", got.size() == 4 && c.connected());
    // keep-alive of 30 s, driven by what is received
    t.sent.clear(); g_now += 14000; c.loop(); CH("no ping before 15 s of silence", t.sent.empty());
    c.publish("base/x", "1"); t.sent.clear();
    g_now += 1500; c.loop(); CH("ping after 15 s without anything received, even while publishing", t.sent == std::string("\xc0\x00", 2));
    t.push(std::string("\xd0\x00", 2)); c.loop(); CH("PINGRESP keeps the session", c.connected());
    t.sent.clear(); g_now += 15000; c.loop(); CH("next ping", t.sent == std::string("\xc0\x00", 2));
    g_now += 15000; c.loop(); CH("no PINGRESP: session dropped", !c.connected() && !c.busy() && c.lastError() == -3 && !t.up);
  }
  { FakeTransport t; MqttClient c(t, [] { return g_now; }); c.begin(); t.push(std::string("\x20\x02\x00\x04", 4)); c.loop();
    CH("CONNACK 4 (bad credentials): refused", !c.connected() && !c.busy() && c.lastError() == 4 && !t.up); }
  { FakeTransport t; MqttClient c(t, [] { return g_now; }); c.begin(); g_now += 4000; CH("still waiting at 4 s", c.loop());
    g_now += 1500; c.loop(); CH("no CONNACK after 5 s: error -2", !c.busy() && c.lastError() == -2); }
  { FakeTransport t; MqttClient c(t, [] { return g_now; }); c.begin(); t.push(std::string("\x20\x02\x00\x00", 4)); c.loop();
    t.up = false; c.loop(); CH("transport closed by the broker: session lost", !c.connected() && c.lastError() == -3); }
  { FakeTransport t; MqttClient c(t, [] { return g_now; }); c.begin(); t.push(std::string("\x20\x02\x00\x00", 4)); c.loop();
    t.writeLimit = 10; CH("write that does not fit: publish fails and the session is dropped", !c.publish("topic", std::string(100, 'x')) && !c.busy()); }
  { FakeTransport t; MqttClient c(t, [] { return g_now; }); c.begin(); t.push(std::string("\x20\x02\x00\x00", 4)); c.loop();
    t.sent.clear(); c.disconnect(); CH("disconnect sends DISCONNECT and closes", t.sent == std::string("\xe0\x00", 2) && !t.up && !c.busy()); }

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

  std::cout << "mqtt: " << ok << " ok, " << ko << " failed\n";
  return ko ? 1 : 0;
}
