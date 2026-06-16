/*
 * ============================================================
 *  config_portal.h — ESP32 WiFi + Server Config (Captive Portal)
 * ============================================================
 *
 *  HOW IT WORKS
 *  ------------
 *  First boot (or after reset): ESP32 opens its own WiFi AP named
 *  "Agriflow-Setup". Join it from your phone/computer, a sign-in
 *  page pops up (or browse to http://192.168.4.1), fill in your home
 *  WiFi + the PC's IP that runs the dashboard, then Save. ESP32 reboots
 *  and connects automatically. The settings are stored in NVS, so every
 *  subsequent boot reconnects silently — no code edits, no re-flash.
 *
 *  RESET TO RECONFIGURE
 *  --------------------
 *  Hold the BOOT button (GPIO 0) for ~3 seconds while powered on, then
 *  release. NVS is wiped and the portal opens again.
 *
 *  SHARED BY ALL .ino SKETCHES — no external libraries required
 *  (WebServer.h, DNSServer.h, Preferences.h ship with arduino-esp32).
 * ============================================================
 */

#ifndef CONFIG_PORTAL_H
#define CONFIG_PORTAL_H

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

// ── Defaults ─────────────────────────────────
#define CP_AP_SSID        "Agriflow-Setup"
#define CP_AP_PASS        "setup123"
#define CP_NS             "sprinkler"   // NVS namespace
#define CP_RESET_PIN      0             // GPIO0 = BOOT button on most boards
#define CP_RESET_HOLD_MS  3000          // hold 3s to wipe config
#define CP_AP_IP_3        4             // → AP IP 192.168.4.1
#define CP_SERVER_URL     "https://agriflow-mvt7.onrender.com/api/sensor"  // ← Embedded server URL

// ── Stored config ────────────────────────────
struct DeviceConfig {
  String wifiSsid;
  String wifiPass;
  String serverUrl;  // Full URL (e.g., "https://agriflow-mvt7.onrender.com/api/sensor")
};

// Globals used by the portal loop (declared here, defined below).
static WebServer   cpServer(80);
static DNSServer   cpDns;
static Preferences cpPrefs;
static bool        cpConfiguredThisBoot = false; // set true after a /save

// ── HTML helpers ──────────────────────────────
// Escape special chars so SSIDs like "Tom's WiFi" or "A&B" don't break HTML.
static String cpHtmlEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:   out += c;        break;
    }
  }
  return out;
}

// ── HTML page ────────────────────────────────
// Built fresh each request so the WiFi scan list is current.
static String cpBuildPage() {
  // Scan with async=false so results are available immediately.
  // Delete any previous scan first to avoid memory leak.
  WiFi.scanDelete();
  int n = WiFi.scanNetworks(false, false);  // async=false, show_hidden=false

  String opts = "";
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;
    // Escape both the value attribute and display text
    String escaped = cpHtmlEscape(s);
    opts += "<option value=\"" + escaped + "\">" + escaped
            + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
  }

  String html =
    String("<!DOCTYPE html><html><head>")
    + "<meta charset='utf-8'>"
    + "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    + "<title>Agriflow Setup</title>"
    + "<style>"
    + "  *{box-sizing:border-box;margin:0;padding:0}"
    + "  body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;"
    + "       background:linear-gradient(135deg,#0b0f1e,#1a1f3a);color:#ddeeff;"
    + "       min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}"
    + "  .card{background:rgba(11,15,30,.9);border:1px solid rgba(0,210,255,.2);"
    + "        border-radius:22px;padding:32px;max-width:420px;width:100%;"
    + "        box-shadow:0 20px 60px rgba(0,0,0,.5)}"
    + "  h1{font-size:1.4rem;margin-bottom:6px;display:flex;align-items:center;gap:8px}"
    + "  p.sub{color:#4a6070;font-size:.82rem;margin-bottom:24px}"
    + "  label{display:block;font-size:.72rem;text-transform:uppercase;letter-spacing:.1em;"
    + "        color:#4a6070;font-weight:700;margin:18px 0 8px}"
    + "  input,select{width:100%;padding:13px 15px;border-radius:12px;border:1px solid rgba(255,255,255,.1);"
    + "        background:rgba(255,255,255,.04);color:#ddeeff;font-size:1rem;outline:none;"
    + "        transition:border .2s}"
    + "  input:focus,select:focus{border-color:#00d2ff}"
    + "  .row{display:flex;gap:10px}"
    + "  .row .ip{flex:3}.row .port{flex:1}"
    + "  button{margin-top:26px;width:100%;padding:15px;border:none;border-radius:12px;"
    + "        background:linear-gradient(135deg,#2ed573,#00d2ff);color:#fff;font-size:1.05rem;"
    + "        font-weight:800;cursor:pointer;transition:transform .15s}"
    + "  button:active{transform:scale(.98)}"
    + "  .hint{font-size:.72rem;color:#4a6070;margin-top:8px}"
    + "</style></head><body>"
    + "<form class='card' action='/save' method='POST'>"
    + "<h1>&#127793; Agriflow</h1>"
    + "<p class='sub'>Set up your device — saved on the ESP32, no code editing needed.</p>"

    + "<label>&#127760; Home WiFi Network</label>"
    + "<select name='ssid' required>"
    + (opts.length() ? opts : String("<option value=''>— No networks found —</option>"))
    + "</select>"

    + "<label>&#128273; WiFi Password</label>"
    + "<input name='pass' type='password' placeholder='your WiFi password'>"

    + "<!-- Server URL is pre-configured — no need to enter -->"
    + "<input type='hidden' name='ip' value='" CP_SERVER_URL "'>"
    + "<input type='hidden' name='port' value='443'>"

    + "<button type='submit'>&#128190; Save &amp; Connect</button>"
    + "</form></body></html>";

  WiFi.scanDelete(); // free scan memory
  return html;
}

// ── Validation ───────────────────────────────
// Accept both IP addresses (192.168.1.53) and domain names (server.onrender.com)
static bool cpValidIp(const String &s) {
  if (s.length() == 0 || s.length() > 255) return false;

  // Check if it's a domain (contains letters)
  bool hasLetters = false;
  bool hasDigits = false;
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-') hasLetters = true;
    if (c >= '0' && c <= '9') hasDigits = true;
  }

  // If contains letters → it's a domain → accept it
  if (hasLetters) return true;

  // Otherwise validate as IP address
  int parts = 0, dots = 0, val = 0; bool any = false;
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '.') {
      if (!any) return false;
      if (val > 255) return false;
      parts++; dots++; val = 0; any = false;
      if (dots > 3) return false;
    } else if (c >= '0' && c <= '9') {
      val = val * 10 + (c - '0'); any = true;
      if (val > 255) return false;
    } else {
      return false;
    }
  }
  return dots == 3 && any && val <= 255 && parts == 3;
}

// ── HTTP handlers ────────────────────────────
static void cpHandleRoot() {
  cpServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  cpServer.send(200, "text/html", cpBuildPage());
}

static void cpHandleSave() {
  String ssid = cpServer.arg("ssid");
  String pass = cpServer.arg("pass");

  // Fallback: some cores need arg("plain") for POST body parsing
  if (ssid.length() == 0 && cpServer.hasArg("plain")) {
    String body = cpServer.arg("plain");
    auto extract = [&](const String &key) -> String {
      String search = key + "=";
      int idx = body.indexOf(search);
      if (idx < 0) return "";
      int start = idx + search.length();
      int end = body.indexOf('&', start);
      if (end < 0) end = body.length();
      String val = body.substring(start, end);
      val.replace("+", " ");
      val.replace("%20", " ");
      return val;
    };
    ssid = extract("ssid");
    pass = extract("pass");
  }

  if (ssid.length() == 0) {
    cpServer.send(400, "text/html",
      "<!DOCTYPE html><html><head><meta charset='utf-8'></head><body style='font-family:sans-serif;background:#0b0f1e;color:#ddeeff;display:flex;align-items:center;justify-content:center;min-height:100vh'><div style='text-align:center;padding:40px'>"
      "<h2 style='color:#ff4757'>&#10060; Missing WiFi Network</h2>"
      "<p style='color:#4a6070;margin-top:12px'>Go back and select a network from the dropdown.</p>"
      "<p style='margin-top:20px'><a href='/' style='color:#00d2ff'>&#8592; Back to Setup</a></p>"
      "</div></body></html>");
    return;
  }

  // Persist to NVS — WiFi credentials only (server URL is pre-configured)
  cpPrefs.begin(CP_NS, false);
  cpPrefs.putString("ssid", ssid);
  cpPrefs.putString("pass", pass);
  cpPrefs.putString("url", CP_SERVER_URL);
  cpPrefs.putBool("set", true);
  cpPrefs.end();

  cpConfiguredThisBoot = true;

  String ok =
    String("<!DOCTYPE html><html><head><meta charset='utf-8'>")
    + "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    + "<style>body{font-family:-apple-system,sans-serif;background:#0b0f1e;color:#ddeeff;"
    + "display:flex;align-items:center;justify-content:center;min-height:100vh;margin:0}"
    + ".c{text-align:center;padding:40px}h1{font-size:2rem}.s{color:#2ed573;font-size:4rem}</style>"
    + "</head><body><div class='c'>"
    + "<div class='s'>&#9989;</div><h1>Saved!</h1>"
    + "<p>Rebooting and connecting to <b>" + cpHtmlEscape(ssid) + "</b>&hellip;</p>"
    + "<p style='color:#4a6070;margin-top:12px;font-size:.85rem'>"
    + "If it does not restart in ~3s, power-cycle it.</p>"
    + "</div></body></html>";
  cpServer.send(200, "text/html", ok);
}

// Captive-portal probe endpoints per OS — any other path → config page.
// Android: /generate_204 expects non-204 to trigger sign-in popup.
static void cpHandle204() {
  cpServer.sendHeader("Location", "http://192.168.4.1/", true);
  cpServer.send(302, "text/plain", "");
}
// iOS/macOS: non-200 / non-empty body triggers the popup.
static void cpHandleApple() {
  cpServer.send(200, "text/html",
    "<HTML><HEAD><TITLE>Success</TITLE></HEAD>"
    "<BODY><center><h2>Success</h2></center></BODY></HTML>");
}
// Windows NCSI: any response other than the expected plain text triggers sign-in.
static void cpHandleNcsi() {
  cpServer.send(200, "text/html", cpBuildPage());
}
// Fallback catch-all — serve the config page.
static void cpHandleAny() {
  cpHandleRoot();
}

// ── Public API ───────────────────────────────

// Load saved config from NVS. Returns false if never configured.
static bool loadConfig(DeviceConfig &cfg) {
  cpPrefs.begin(CP_NS, true);
  bool set = cpPrefs.getBool("set", false);
  if (set) {
    cfg.wifiSsid  = cpPrefs.getString("ssid", "");
    cfg.wifiPass  = cpPrefs.getString("pass", "");
    cfg.serverUrl = cpPrefs.getString("url",  CP_SERVER_URL);
  }
  cpPrefs.end();
  return set && cfg.wifiSsid.length() > 0;
}

// Wipe NVS so the portal reopens on next boot.
static void clearConfig() {
  cpPrefs.begin(CP_NS, false);
  cpPrefs.clear();
  cpPrefs.end();
  Serial.println("[CONFIG] Cleared — portal will open next boot.");
}

// Open the AP + captive portal. Blocks until the user submits the form,
// then returns (caller should reboot).
static void startConfigPortal() {
  Serial.println();
  Serial.println("══════════════════════════════════════════════════════");
  Serial.println("  CONFIG MODE — No WiFi saved yet");
  Serial.println("══════════════════════════════════════════════════════");
  Serial.println("  1. Connect your phone/computer to WiFi:");
  Serial.printf ("     SSID: %s   Pass: %s\n", CP_AP_SSID, CP_AP_PASS);
  Serial.println("  2. A setup page should pop up automatically.");
  Serial.println("     (or open http://192.168.4.1 in a browser)");
  Serial.println("══════════════════════════════════════════════════════");
  Serial.println();

  WiFi.mode(WIFI_AP);
  IPAddress apIP(192, 168, CP_AP_IP_3, 1);
  IPAddress mask(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, apIP, mask);
  WiFi.softAP(CP_AP_SSID, CP_AP_PASS);
  delay(100);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  // DNS wildcard — hijack every hostname so devices land on our page.
  // Some ESP32 core versions need the gateway IP, others work with any IP.
  cpDns.start(53, "*", apIP);

  // Routes: config page + save + captive probes + catch-all.
  cpServer.on("/",                    HTTP_GET,  cpHandleRoot);
  cpServer.on("/save",                HTTP_POST, cpHandleSave);

  // Android captive portal probes
  cpServer.on("/generate_204",                   cpHandle204);
  cpServer.on("/gen_204",                        cpHandle204);
  cpServer.on("/connectivitycheck/gstatic_204",  cpHandle204);

  // iOS/macOS captive portal probes
  cpServer.on("/hotspot-detect.html",            cpHandleApple);
  cpServer.on("/library/test/success.html",      cpHandleApple);

  // Windows NCSI probes
  cpServer.on("/ncsi.txt",                       cpHandleNcsi);
  cpServer.on("/connecttest.txt",                cpHandleNcsi);
  cpServer.on("/redirect",                       cpHandleNcsi);

  // Chrome/Edge connectivity check
  cpServer.on("/success.txt",                    cpHandle204);

  // Catch-all — serve the config page
  cpServer.onNotFound(cpHandleAny);

  cpServer.begin();
  Serial.println("[PORTAL] Waiting for configuration...");

  // Spin until the user saves a config.
  while (!cpConfiguredThisBoot) {
    cpDns.processNextRequest();
    cpServer.handleClient();
    delay(2);
  }

  // Give the response a moment to flush before the caller reboots.
  unsigned long t0 = millis();
  while (millis() - t0 < 1500) {
    cpDns.processNextRequest();
    cpServer.handleClient();
    delay(2);
  }
  cpServer.stop();
  cpDns.stop();
  WiFi.softAPdisconnect(true);
  Serial.println("[PORTAL] Config saved. Rebooting...");
}

// Build the sensor endpoint URL from saved config.
// Auto-detect: if serverIp contains letters (domain) → HTTPS, else HTTP.
static String buildServerUrl(const DeviceConfig &cfg) {
  // Server URL is pre-configured and stored in NVS
  return cfg.serverUrl;
}

// Poll the reset button. Returns true if held long enough → caller wipes
// config and reboots. Call once per loop() iteration.
static bool checkResetButton(unsigned long &pressedSince) {
  if (digitalRead(CP_RESET_PIN) == LOW) {
    if (pressedSince == 0) pressedSince = millis();
    if (millis() - pressedSince >= CP_RESET_HOLD_MS) {
      return true; // held long enough
    }
  } else {
    pressedSince = 0;
  }
  return false;
}

#endif // CONFIG_PORTAL_H
