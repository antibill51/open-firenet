// Host tests of firenet_web_guard.h: which requests the web API answers (issue #77). Run by test/build_and_test.sh.
#include "firenet_web_guard.h"
#include <cstdio>

using namespace firenet::webguard;
static int ok = 0, ko = 0;
#define CH(name, cond) do { if (cond) ok++; else { ko++; printf("FAIL %s\n", name); } } while (0)

int main() {
  // The bridge's own page: the Origin is the Host.
  CH("own page by address", requestAllowed("/api/controls", "192.168.1.93", "http://192.168.1.93"));
  CH("own page by mDNS name", requestAllowed("/api/controls", "open-firenet.local", "http://open-firenet.local"));
  CH("own page, case and default port", requestAllowed("/api/controls", "Open-Firenet.local:80", "http://open-firenet.local"));
  CH("own page in setup mode", requestAllowed("/api/wifi", "192.168.4.1", "http://192.168.4.1"));
  // Not browsers: no Origin.
  CH("Home Assistant by address", requestAllowed("/api/state", "192.168.1.93", ""));
  CH("script by short name", requestAllowed("/api/state", "open-firenet", ""));
  CH("mDNS name of another bridge", requestAllowed("/api/state", "stove-2.local", ""));
  CH("a router name is not assumed local", !requestAllowed("/api/state", "open-firenet.fritz.box", "") && !requestAllowed("/api/state", "stove.lan", "") &&
                                           !requestAllowed("/log", "open-firenet.home", ""));
  CH("a router name the owner added", requestAllowed("/api/state", "open-firenet.fritz.box", "", parseExtraHosts("open-firenet.fritz.box")) &&
                                      requestAllowed("/api/state", "stove.lan", "", parseExtraHosts("stove.lan")));
  {
    std::string r = refusalJson("Poele.MonDomaine.fr:80", "", "192.168.1.93");
    CH("refusal says the host check, the name and the address", r.find("\"refused\":\"host\"") != std::string::npos &&
       r.find("\"host\":\"poele.mondomaine.fr\"") != std::string::npos && r.find("\"ip\":\"192.168.1.93\"") != std::string::npos &&
       r.find("http://192.168.1.93") != std::string::npos);
    CH("refusal says the origin check", refusalJson("192.168.1.93", "https://example.com", "192.168.1.93").find("\"refused\":\"origin\"") != std::string::npos);
    CH("refusal cannot carry markup from the Host header", refusalJson("a\"<b>.example.com", "", "1.2.3.4").find('<') == std::string::npos);
  }
  // Names added by the owner.
  {
    auto extra = parseExtraHosts("MonDomaine.fr\n *.mabox.example , .station.box\nfr\ncom\nbad_name.fr\nmondomaine.fr");
    CH("entries are cleaned, a whole extension and a bad name are dropped, no duplicate",
       extra.size() == 3 && extra[0] == "mondomaine.fr" && extra[1] == "mabox.example" && extra[2] == "station.box");
    CH("added domain and names under it", requestAllowed("/api/state", "mondomaine.fr", "", extra) &&
       requestAllowed("/api/state", "poele.mondomaine.fr", "", extra) && requestAllowed("/api/state", "open-firenet.mabox.example:80", "", extra));
    CH("own page under an added domain", requestAllowed("/api/controls", "poele.mondomaine.fr", "http://poele.mondomaine.fr", extra));
    CH("a name that only looks like it", !requestAllowed("/api/state", "evilmondomaine.fr", "", extra) && !requestAllowed("/api/state", "mondomaine.fr.example.com", "", extra));
    CH("another website stays refused under an added domain", !requestAllowed("/api/controls", "poele.mondomaine.fr", "https://example.com", extra));
    CH("at most 8 entries", parseExtraHosts("a.b1 a.b2 a.b3 a.b4 a.b5 a.b6 a.b7 a.b8 a.b9 a.b10").size() == MAX_EXTRA_HOSTS);
    CH("round trip", parseExtraHosts(joinExtraHosts(extra)) == extra && parseExtraHosts("").empty());
  }
  CH("no Host header", requestAllowed("/api/state", "", ""));
  CH("IPv6 literal", requestAllowed("/api/state", "[fe80::1]:80", ""));
  // Another website open in a browser on the home network.
  CH("foreign page, simple POST", !requestAllowed("/api/controls", "192.168.1.93", "https://example.com"));
  CH("foreign page, GET with Origin", !requestAllowed("/api/state", "open-firenet.local", "http://example.com"));
  CH("foreign page on another port of the same host", !requestAllowed("/api/controls", "192.168.1.93", "http://192.168.1.93:8080"));
  CH("opaque origin", !requestAllowed("/api/controls", "192.168.1.93", "null"));
  CH("local page of another device", !requestAllowed("/api/controls", "192.168.1.93", "http://192.168.1.50"));
  // A public name made to point at the bridge (DNS rebinding): "same origin" for the browser.
  CH("public name, POST", !requestAllowed("/api/controls", "attacker.example.com", "http://attacker.example.com"));
  CH("public name, GET without Origin", !requestAllowed("/api/state", "attacker.example.com", ""));
  CH("public name dressed up", !requestAllowed("/log", "open-firenet.local.example.com", "") && !requestAllowed("/api/state", "192.168.1.93.example.com", ""));
  // Outside the API: the page and the captive-portal checks of phones.
  CH("page itself under any name", requestAllowed("/", "captive.apple.com", "") && requestAllowed("/generate_204", "connectivitycheck.gstatic.com", ""));
  CH("helpers", hostName("Open-Firenet.local:80") == "open-firenet.local" && isIpLiteral("10.0.0.7") && !isIpLiteral("10.0.0.7.nip.io") &&
                isLocalName("stove") && !isLocalName("example.com"));
  printf("web guard: %d ok, %d failures\n", ok, ko);
  return ko ? 1 : 0;
}
