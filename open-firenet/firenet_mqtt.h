// firenet_mqtt.h — minimal MQTT 3.1.1 client (QoS 0 only), header-only and free of Arduino types so it is tested
// on the host (test/mqtt_test.cpp). The firmware plugs a socket-backed transport into it. Nothing here blocks.
//
// Supported: CONNECT (user / password, last will), PUBLISH QoS 0 (retain), SUBSCRIBE QoS 0, incoming PUBLISH,
// PINGREQ / PINGRESP keep-alive, DISCONNECT. Not supported on purpose: QoS 1 / 2, TLS, MQTT 5.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace firenet {

// Byte stream to the broker, already connected by the caller. None of these calls may block.
struct MqttTransport {
  virtual bool connected() = 0;
  virtual int read(uint8_t* buf, size_t n) = 0;           // bytes read, 0 when nothing is waiting
  virtual size_t write(const uint8_t* buf, size_t n) = 0;  // bytes written; anything short of n drops the session
  virtual void stop() = 0;
  virtual ~MqttTransport() {}
};

namespace mqtt {

inline void putLen(std::string& out, size_t n) {  // "remaining length", 1 to 4 bytes
  do { uint8_t b = n % 128; n /= 128; if (n) b |= 0x80; out += (char)b; } while (n);
}
inline void putStr(std::string& out, const std::string& s) {
  out += (char)(s.size() >> 8); out += (char)(s.size() & 0xff); out += s;
}
inline std::string packet(uint8_t header, const std::string& body) {
  std::string p(1, (char)header); putLen(p, body.size()); p += body; return p;
}

inline std::string connectPacket(const std::string& clientId, const std::string& user, const std::string& pass,
                                 const std::string& willTopic, const std::string& willPayload, bool willRetain,
                                 uint16_t keepAliveS) {
  std::string b;
  putStr(b, "MQTT"); b += (char)0x04;                    // protocol name and level 4 = 3.1.1
  uint8_t flags = 0x02;                                  // clean session
  if (!willTopic.empty()) flags |= 0x04 | (willRetain ? 0x20 : 0);
  if (!user.empty()) flags |= 0x80;
  if (!user.empty() && !pass.empty()) flags |= 0x40;     // a password is only allowed with a user name
  b += (char)flags; b += (char)(keepAliveS >> 8); b += (char)(keepAliveS & 0xff);
  putStr(b, clientId);
  if (!willTopic.empty()) { putStr(b, willTopic); putStr(b, willPayload); }
  if (!user.empty()) putStr(b, user);
  if (!user.empty() && !pass.empty()) putStr(b, pass);
  return packet(0x10, b);
}
inline std::string publishPacket(const std::string& topic, const std::string& payload, bool retain) {
  std::string b; putStr(b, topic); b += payload;
  return packet(0x30 | (retain ? 0x01 : 0), b);
}
inline std::string subscribePacket(uint16_t packetId, const std::string& topic) {
  std::string b; b += (char)(packetId >> 8); b += (char)(packetId & 0xff); putStr(b, topic); b += (char)0x00;
  return packet(0x82, b);
}

// Incremental parser: feed() bytes as they arrive, a complete packet is delivered through the callback.
// Packets larger than maxSize are skipped (their bytes are consumed and dropped).
class Parser {
public:
  using PacketFn = std::function<void(uint8_t header, const std::string& body)>;
  Parser(size_t maxSize, PacketFn fn) : max_(maxSize), fn_(fn) {}
  void reset() { state_ = 0; body_.clear(); }
  void feed(uint8_t b) {
    switch (state_) {
      case 0: header_ = b; len_ = 0; shift_ = 0; body_.clear(); state_ = 1; break;
      case 1:
        len_ += (size_t)(b & 0x7f) << shift_; shift_ += 7;
        if (b & 0x80) { if (shift_ > 21) reset(); break; }   // more than 4 length bytes: malformed
        left_ = len_; skip_ = len_ > max_;
        if (left_ == 0) { fn_(header_, body_); state_ = 0; } else state_ = 2;
        break;
      case 2:
        if (!skip_) body_ += (char)b;
        if (--left_ == 0) { if (!skip_) fn_(header_, body_); state_ = 0; body_.clear(); }
        break;
    }
  }
private:
  size_t max_; PacketFn fn_;
  int state_ = 0; uint8_t header_ = 0; size_t len_ = 0, left_ = 0; int shift_ = 0; bool skip_ = false;
  std::string body_;
};

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

}  // namespace mqtt

// Session over an already connected transport: begin() sends CONNECT, loop() does the rest. Nothing blocks.
class MqttClient {
public:
  // retained: the broker replayed a stored message on subscription (not a live one).
  using MessageFn = std::function<void(const std::string& topic, const std::string& payload, bool retained)>;
  MqttClient(MqttTransport& t, std::function<uint32_t()> now)
    : t_(t), now_(now), parser_(MAX_PACKET, [this](uint8_t h, const std::string& b) { onPacket(h, b); }) {}

  static constexpr size_t MAX_PACKET = 2048;
  static constexpr uint32_t CONNACK_TIMEOUT_MS = 5000;

  void setClientId(const std::string& id) { clientId_ = id; }
  void setCredentials(const std::string& user, const std::string& pass) { user_ = user; pass_ = pass; }
  void setWill(const std::string& topic, const std::string& payload, bool retain) {
    willTopic_ = topic; willPayload_ = payload; willRetain_ = retain;
  }
  void setKeepAlive(uint16_t seconds) { keepAliveS_ = seconds; }
  void onMessage(MessageFn fn) { onMessage_ = fn; }
  void onConnect(std::function<void()> fn) { onConnect_ = fn; }   // called once the broker accepted the session

  // 0 = accepted; 1..5 = CONNACK refusal code (4 = bad user / password, 5 = not authorised);
  // -2 = no answer from the broker; -3 = connection lost; -4 = never started.
  int lastError() const { return lastError_; }
  bool connected() { return state_ == READY && t_.connected(); }
  bool busy() const { return state_ != IDLE; }                   // a session is open or being opened

  // The transport must be connected. The answer (CONNACK) is handled by loop().
  bool begin() {
    parser_.reset(); state_ = WAIT_CONNACK; started_ = lastIn_ = now_(); pingPending_ = false;
    return send(mqtt::connectPacket(clientId_, user_, pass_, willTopic_, willPayload_, willRetain_, keepAliveS_));
  }
  bool publish(const std::string& topic, const std::string& payload, bool retain = false) {
    return connected() && send(mqtt::publishPacket(topic, payload, retain));
  }
  bool subscribe(const std::string& topic) {
    if (++packetId_ == 0) packetId_ = 1;
    return connected() && send(mqtt::subscribePacket(packetId_, topic));
  }

  // Call regularly: delivers incoming messages and keeps the session alive. Returns false when no session is open.
  bool loop() {
    if (state_ == IDLE) return false;
    if (!t_.connected()) { drop(-3); return false; }
    uint8_t buf[128];
    for (int n; state_ != IDLE && (n = t_.read(buf, sizeof buf)) > 0;) {
      lastIn_ = now_();
      for (int i = 0; i < n && state_ != IDLE; i++) parser_.feed(buf[i]);
    }
    if (state_ == IDLE) return false;
    uint32_t now = now_();
    if (state_ == WAIT_CONNACK) {
      if ((now - started_) >= CONNACK_TIMEOUT_MS) { drop(-2); return false; }
      return true;
    }
    // Keep-alive on what is received, not on what is sent: a broker that went silent is detected even while the
    // bridge keeps publishing.
    uint32_t half = (uint32_t)keepAliveS_ * 500;
    if (half) {
      if (pingPending_ && (now - pingSent_) >= half) { drop(-3); return false; }
      if (!pingPending_ && (now - lastIn_) >= half) {
        static const uint8_t ping[2] = {0xC0, 0x00};
        if (t_.write(ping, 2) != 2) { drop(-3); return false; }
        pingSent_ = now; pingPending_ = true;
      }
    }
    return true;
  }

  void disconnect() {
    if (state_ == READY && t_.connected()) { static const uint8_t d[2] = {0xE0, 0x00}; t_.write(d, 2); }
    t_.stop(); state_ = IDLE;
  }

private:
  enum State { IDLE, WAIT_CONNACK, READY };
  bool send(const std::string& p) {
    if (t_.write((const uint8_t*)p.data(), p.size()) != p.size()) { drop(-3); return false; }
    return true;
  }
  void drop(int error) { t_.stop(); state_ = IDLE; lastError_ = error; }
  void onPacket(uint8_t header, const std::string& body) {
    switch (header >> 4) {
      case 2: {                                                                      // CONNACK
        if (state_ != WAIT_CONNACK) break;
        int code = body.size() >= 2 ? (uint8_t)body[1] : 255;
        if (code != 0) { drop(code); break; }
        state_ = READY; lastError_ = 0;
        if (onConnect_) onConnect_();
        break;
      }
      case 13: pingPending_ = false; break;                                          // PINGRESP
      case 3: {                                                                      // PUBLISH
        if (body.size() < 2) break;
        size_t tl = ((uint8_t)body[0] << 8) | (uint8_t)body[1];
        size_t pos = 2 + tl;
        if (pos > body.size()) break;
        if ((header >> 1) & 0x03) pos += 2;                                          // QoS > 0 carries a packet id
        if (pos > body.size()) break;
        if (onMessage_) onMessage_(body.substr(2, tl), body.substr(pos), header & 0x01);
        break;
      }
      default: break;                                                                // SUBACK etc.: nothing to do
    }
  }

  MqttTransport& t_;
  std::function<uint32_t()> now_;
  mqtt::Parser parser_;
  MessageFn onMessage_;
  std::function<void()> onConnect_;
  std::string clientId_ = "open-firenet", user_, pass_, willTopic_, willPayload_;
  uint16_t keepAliveS_ = 30, packetId_ = 0;
  bool willRetain_ = false, pingPending_ = false;
  State state_ = IDLE;
  int lastError_ = -4;
  uint32_t started_ = 0, lastIn_ = 0, pingSent_ = 0;
};

}  // namespace firenet
