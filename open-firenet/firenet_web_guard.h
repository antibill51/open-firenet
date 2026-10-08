// Who may call the web API (issue #77). The bridge is a local device: its API is for its own page, for Home
// Assistant, the installer and scripts on the home network, not for a page of another website that a browser on
// that network happens to have open. Two checks, both on request headers a web page cannot forge:
//  - Origin: a browser names the page a request comes from. It must be the bridge itself.
//  - Host: the name the request was addressed to. A public domain name made to point at the bridge's address
//    (DNS rebinding) turns a foreign page into a "same origin" one; such a name is refused.
// Clients that are not browsers send no Origin, and a Host that is the address or local name they dialled.
// No Arduino dependency: also built by test/web_guard_test.cpp.
#pragma once
#include <string>
#include <vector>

namespace firenet {
namespace webguard {

inline std::string lower(std::string s) {
  for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  return s;
}

// "Host" header value without its port. IPv6 literals ("[::1]:80") keep their brackets.
inline std::string hostName(const std::string& hostHeader) {
  std::string h = lower(hostHeader);
  if (!h.empty() && h[0] == '[') { size_t e = h.find(']'); return e == std::string::npos ? h : h.substr(0, e + 1); }
  size_t colon = h.rfind(':');
  return colon == std::string::npos ? h : h.substr(0, colon);
}

inline bool isIpLiteral(const std::string& name) {
  if (name.empty()) return false;
  if (name[0] == '[') return true;
  for (char c : name) if (!((c >= '0' && c <= '9') || c == '.')) return false;
  return true;
}

// Names nobody outside the home network can make point at the bridge: a name without a dot ("open-firenet"), and
// the mDNS name ("open-firenet.local"). Nothing else is assumed local: the suffixes home routers give (.lan,
// .home, .fritz.box...) differ from one to the next and some are also public domains, so the owner adds the one
// in use (see parseExtraHosts).
inline bool isLocalName(const std::string& name) {
  if (name.find('.') == std::string::npos) return true;
  static const std::string MDNS = ".local";
  return name.size() > MDNS.size() && name.compare(name.size() - MDNS.size(), MDNS.size(), MDNS) == 0;
}

// Domains the owner added in the bridge's settings: the name their router gives the bridge, or a domain of their
// own. Read from free text: one per line, or separated by commas or spaces. An entry is kept when it is
// made of letters, digits, dots and hyphens and has at least one dot inside: a whole extension ("fr", "com")
// would open the door again. A leading "*." or "." is dropped. At most MAX_EXTRA_HOSTS entries of 63 characters.
static const size_t MAX_EXTRA_HOSTS = 8;
inline std::vector<std::string> parseExtraHosts(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&]() {
    std::string e = lower(cur); cur.clear();
    if (e.compare(0, 2, "*.") == 0) e.erase(0, 2);
    while (!e.empty() && e[0] == '.') e.erase(0, 1);
    while (!e.empty() && e.back() == '.') e.pop_back();
    if (e.size() < 3 || e.size() > 63 || out.size() >= MAX_EXTRA_HOSTS) return;
    if (e.find('.') == std::string::npos || e.find("..") != std::string::npos) return;
    for (char c : e) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) return;
    for (const auto& known : out) if (known == e) return;
    out.push_back(e);
  };
  for (char c : text) {
    if (c == '\n' || c == '\r' || c == ',' || c == ';' || c == ' ' || c == '\t') flush();
    else cur += c;
  }
  flush();
  return out;
}
inline std::string joinExtraHosts(const std::vector<std::string>& hosts) {
  std::string out;
  for (const auto& h : hosts) { if (!out.empty()) out += '\n'; out += h; }
  return out;
}
// The name is an added domain, or ends with one ("stove.example.org" for the entry "example.org").
inline bool isExtraName(const std::string& name, const std::vector<std::string>& extra) {
  for (const auto& e : extra) {
    if (name == e) return true;
    if (name.size() > e.size() + 1 && name.compare(name.size() - e.size(), e.size(), e) == 0 && name[name.size() - e.size() - 1] == '.') return true;
  }
  return false;
}

// An address, a local name, a domain the owner added, or no Host at all (HTTP/1.0 clients; every browser sends one).
inline bool hostAllowed(const std::string& hostHeader, const std::vector<std::string>& extra = {}) {
  std::string name = hostName(hostHeader);
  return name.empty() || isIpLiteral(name) || isLocalName(name) || isExtraName(name, extra);
}

// No Origin (not a browser, or a plain same-origin GET), or the Origin of the bridge's own page: its host and
// port are then the ones of the Host header.
inline bool originAllowed(const std::string& origin, const std::string& hostHeader) {
  if (origin.empty()) return true;
  std::string o = lower(origin);
  size_t scheme = o.find("://");
  if (scheme == std::string::npos) return false;                         // "null": sandboxed page, file, redirect
  std::string authority = o.substr(scheme + 3);
  std::string host = lower(hostHeader);
  auto withoutDefaultPort = [](std::string a) {
    if (a.size() > 3 && a.compare(a.size() - 3, 3, ":80") == 0) a.resize(a.size() - 3);
    return a;
  };
  return !host.empty() && withoutDefaultPort(authority) == withoutDefaultPort(host);
}

// The paths that give data or act on the stove. The page itself and the captive-portal answers stay open: in
// setup mode phones ask for them under the names of their own connectivity checks.
inline bool guardedPath(const std::string& path) {
  return path.compare(0, 5, "/api/") == 0 || path == "/log";
}

inline bool requestAllowed(const std::string& path, const std::string& hostHeader, const std::string& origin,
                           const std::vector<std::string>& extra = {}) {
  if (!guardedPath(path)) return true;
  return hostAllowed(hostHeader, extra) && originAllowed(origin, hostHeader);
}

// Body of the 403, as JSON so that the bridge's page can explain it: which check refused ("host" or "origin"),
// the name that was refused, and the address under which the bridge always answers.
inline std::string refusalJson(const std::string& hostHeader, const std::string& origin, const std::string& ip,
                               const std::vector<std::string>& extra = {}) {
  bool byHost = !hostAllowed(hostHeader, extra);
  std::string name = hostName(hostHeader), safe;
  for (char c : name) if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '[' || c == ']' || c == ':') safe += c;
  std::string msg = byHost ? "The bridge does not answer under the name \\\"" + safe + "\\\". Open it by its IP address, http://" + ip +
                             ", then add this name in the Bridge tab, section Access."
                           : "This request comes from another web page, not from the bridge's own page.";
  return std::string("{\"ok\":false,\"refused\":\"") + (byHost ? "host" : "origin") + "\",\"host\":\"" + safe +
         "\",\"ip\":\"" + ip + "\",\"error\":\"" + msg + "\"}";
}

}  // namespace webguard
}  // namespace firenet
