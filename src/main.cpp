/*
 * ╔═══════════════════════════════════════╗
 * ║      C O M M U N I T Y  H U B         ║
 * ║        Local Bulletin Board           ║
 * ╚═══════════════════════════════════════╝
 *
 * Hardware : ESP32-C3
 * Storage  : Internal Flash via LittleFS
 *
 * Libraries (Arduino Library Manager):
 *   - ArduinoJson   by Benoit Blanchon
 *
 * Board setting: ESP32-C3 Dev Module
 * Partition scheme: Default 4MB with spiffs (1.2MB APP / 1.5MB SPIFFS)
 * Uses the built-in WebServer (no extra libs needed).
 */

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Update.h>
#include "FS.h"
#include "LittleFS.h"
#include <ArduinoJson.h>
#include <time.h>
#include <math.h>

// ===================== CONFIG ===================== //
int led = LED_BUILTIN;

namespace Config
{
  //========= Defaults — overridable at runtime via admin panel ========//

  const char *LOCALITY_NAME = "mssg ina bottle";
  const char *BOARD_ICON = "";
  const char *BOARD_TAGLINE = "Take what you need • Share what you can";
  // const char *BOARD_RULES = "Be local • Be kind • No spam";
  const char *BOARD_RULES = "";
  const char *BOARD_FOOTER = "Powered locally — no internet required";

  const char *ADMIN_KEY = "change_me"; // Please definintely do - either here or in the admin panel.

  const int LED_PIN = 4;

  const int LED_DAY_BRIGHTNESS = 80;
  const int LED_NIGHT_BRIGHTNESS = 20;
  const int NIGHT_START_HOUR = 20;
  const int DAY_START_HOUR = 7;

  //======= Settings that are NOT in the Admin Panel ==========//

  // Access Point settings
  // If you want to customize the AP info, this is the place to do it.
  // SSID is what neighbours see in their WiFi list.
  // Leave AP_PASS empty ("") for an open network.
  const char *AP_SSID = "love injection";
  const char *AP_PASS = ""; // "" = open network
  const int AP_CHANNEL = 6;
  const int AP_MAX_CONN = 20;

  // Default message expiration time
  const int DEFAULT_EXPIRY_HOURS = 72;

  // !!!! DO NOT CHANGE THESE !!!!
  // The MAX_MSGS amount is not arbitrary. The heap for the array needs to be sized accordingly.
  // And why would you even need to change it? 200 messages is an absurd amount anyhow.
  const int MAX_MSGS = 200;
  const char *STORAGE_FILE = "/msgs.json";
  const char *TIME_FILE = "/time.json";
  const char *LEDCFG_FILE = "/led.json";

}

// ===================== RUNTIME IDENTITY =====================
// These shadow the Config defaults and can be changed via the admin panel.
// Persisted to /identity.json on the SD card.

String id_name = Config::LOCALITY_NAME;
String id_icon = Config::BOARD_ICON;
String id_tagline = Config::BOARD_TAGLINE;
String id_rules = Config::BOARD_RULES;
String id_footer = Config::BOARD_FOOTER;

void saveIdentityConfig()
{
  DynamicJsonDocument doc(1024);
  doc["name"] = id_name;
  doc["icon"] = id_icon;
  doc["tagline"] = id_tagline;
  doc["rules"] = id_rules;
  doc["footer"] = id_footer;

  File tmp = LittleFS.open("/id.tmp", FILE_WRITE);
  if (!tmp)
    return;
  serializeJson(doc, tmp);
  tmp.close();
  LittleFS.remove("/identity.json");
  LittleFS.rename("/id.tmp", "/identity.json");
}

void loadIdentityConfig()
{
  if (!LittleFS.exists("/identity.json"))
    return;
  File f = LittleFS.open("/identity.json");
  if (!f)
    return;
  DynamicJsonDocument doc(1024);
  if (!deserializeJson(doc, f))
  {
    if (doc["name"].as<String>().length())
      id_name = doc["name"].as<String>();
    if (doc["icon"].as<String>().length())
      id_icon = doc["icon"].as<String>();
    if (doc["tagline"].as<String>().length())
      id_tagline = doc["tagline"].as<String>();
    if (doc["rules"].as<String>().length())
      id_rules = doc["rules"].as<String>();
    if (doc["footer"].as<String>().length())
      id_footer = doc["footer"].as<String>();
  }
  f.close();
}

// ===================== RUNTIME ADMIN KEY =====================
// Shadows Config::ADMIN_KEY. Persisted to /adminkey.json.
// Config::ADMIN_KEY is the run-time fallback if the file is absent.

// String adminKey    = Config::ADMIN_KEY;
String adminKey = "pretzeldog";
String sessionToken = "";           // set on successful auth, cleared on reboot
unsigned long tokenIssuedAt = 0;    // millis() when token was generated
#define TOKEN_LIFETIME_MS 1800000UL // 30 minutes

String generateToken()
{
  String token = "";
  for (int i = 0; i < 4; i++)
  {
    uint32_t r = esp_random();
    char chunk[9];
    snprintf(chunk, sizeof(chunk), "%08x", r);
    token += chunk;
  }
  tokenIssuedAt = millis();
  return token;
}

void saveAdminKey()
{
  DynamicJsonDocument doc(128);
  doc["key"] = adminKey;
  File tmp = LittleFS.open("/adminkey.tmp", FILE_WRITE);
  if (!tmp)
    return;
  serializeJson(doc, tmp);
  tmp.close();
  LittleFS.remove("/adminkey.json");
  LittleFS.rename("/adminkey.tmp", "/adminkey.json");
}

void loadAdminKey()
{
  if (!LittleFS.exists("/adminkey.json"))
    return;
  File f = LittleFS.open("/adminkey.json");
  if (!f)
    return;
  DynamicJsonDocument doc(128);
  if (!deserializeJson(doc, f))
  {
    String k = doc["key"] | "";
    if (k.length())
      adminKey = k;
  }
  f.close();
}

// ===================== RUNTIME LED SETTINGS =====================
// These start from Config defaults but can be changed via the admin panel and are persisted to /led.json on the SD card.

int led_day_brightness = Config::LED_DAY_BRIGHTNESS;
int led_night_brightness = Config::LED_NIGHT_BRIGHTNESS;
int led_night_start = Config::NIGHT_START_HOUR;
int led_day_start = Config::DAY_START_HOUR;
int led_pin = Config::LED_PIN;
bool led_enabled = true;
bool led_pulse_enabled = true;    // sine-wave pulsing on/off
bool led_activity_enabled = true; // faster pulse on recent post activity

void saveLedConfig()
{
  DynamicJsonDocument doc(512);
  doc["day_br"] = led_day_brightness;
  doc["night_br"] = led_night_brightness;
  doc["night_st"] = led_night_start;
  doc["day_st"] = led_day_start;
  doc["pin"] = led_pin;
  doc["enabled"] = led_enabled;
  doc["pulse"] = led_pulse_enabled;
  doc["activity"] = led_activity_enabled;

  File tmp = LittleFS.open("/led.tmp", FILE_WRITE);
  if (!tmp)
    return;
  serializeJson(doc, tmp);
  tmp.close();
  LittleFS.remove(Config::LEDCFG_FILE);
  LittleFS.rename("/led.tmp", Config::LEDCFG_FILE);
}
void loadLedConfig()
{
  if (!LittleFS.exists(Config::LEDCFG_FILE))
    return;
  File f = LittleFS.open(Config::LEDCFG_FILE);
  if (!f)
    return;
  DynamicJsonDocument doc(512);
  if (!deserializeJson(doc, f))
  {
    led_day_brightness = doc["day_br"] | Config::LED_DAY_BRIGHTNESS;
    led_night_brightness = doc["night_br"] | Config::LED_NIGHT_BRIGHTNESS;
    led_night_start = doc["night_st"] | Config::NIGHT_START_HOUR;
    led_day_start = doc["day_st"] | Config::DAY_START_HOUR;
    led_pin = doc["pin"] | Config::LED_PIN;
    led_enabled = doc["enabled"] | true;
    led_pulse_enabled = doc["pulse"] | true;
    led_activity_enabled = doc["activity"] | true;
  }
  f.close();
}

// ===================== TIME =====================
unsigned long baseEpoch = 0; // We use UNIX time in this house, son.
unsigned long baseMillis = 0;
unsigned long lastTimeSave = 0;

unsigned long nowSecs()
{
  return baseEpoch + (millis() - baseMillis) / 1000;
}

bool setTimeFromString(String t)
{
  if (t.length() != 13)
    return false;
  struct tm tm;
  memset(&tm, 0, sizeof(tm));
  tm.tm_mday = t.substring(0, 2).toInt();
  tm.tm_mon = t.substring(2, 4).toInt() - 1;
  tm.tm_year = t.substring(4, 8).toInt() - 1900;
  tm.tm_hour = t.substring(9, 11).toInt();
  tm.tm_min = t.substring(11, 13).toInt();
  time_t epoch = mktime(&tm);
  if (epoch <= 0)
    return false;
  baseEpoch = epoch;
  baseMillis = millis();
  return true;
}

void saveTime()
{ // "I save more time with this one lifehack than any other way! Like and subscribe for more hastag relateable content."
  DynamicJsonDocument doc(256);
  doc["epoch"] = nowSecs();
  File tmp = LittleFS.open("/time.tmp", FILE_WRITE);
  if (!tmp)
    return;
  serializeJson(doc, tmp);
  tmp.close();
  LittleFS.remove(Config::TIME_FILE);
  LittleFS.rename("/time.tmp", Config::TIME_FILE);
}

void loadTime()
{
  if (!LittleFS.exists(Config::TIME_FILE))
    return;
  File f = LittleFS.open(Config::TIME_FILE);
  if (!f)
    return;
  DynamicJsonDocument doc(256);
  if (!deserializeJson(doc, f))
  {
    baseEpoch = doc["epoch"];
    baseMillis = millis();
  }
  f.close();
}

int currentHour()
{
  time_t t = nowSecs();
  struct tm *tm = localtime(&t);
  return tm ? tm->tm_hour : 12;
}

// ===================== UPTIME =====================
unsigned long bootMillis = 0;

String formatUptime()
{
  unsigned long secs = (millis() - bootMillis) / 1000;
  unsigned long days = secs / 86400;
  secs %= 86400;
  unsigned long hours = secs / 3600;
  secs %= 3600;
  unsigned long mins = secs / 60;
  char buf[32];
  if (days > 0)
    snprintf(buf, sizeof(buf), "↑ %lud %luh %lum", days, hours, mins);
  else if (hours > 0)
    snprintf(buf, sizeof(buf), "↑ %luh %lum", hours, mins);
  else
    snprintf(buf, sizeof(buf), "↑ %lum", mins);
  return String(buf);
}

// ===================== MESSAGES =====================
struct Message
{
  uint16_t id;
  String author;
  String type;
  String text;
  unsigned long expires;
};

Message msgs[Config::MAX_MSGS];
int msgCount = 0;
uint16_t nextMsgId = 1;
unsigned long lastPostTime = 0;

bool msgsDirty = false; // Ooh you're so dirty.
unsigned long lastMsgDirtyTime = 0;

void saveMessages()
{
  DynamicJsonDocument doc(81920); // ~80KB; sized for 200 worst-case messages
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < msgCount; i++)
  {
    JsonObject o = arr.createNestedObject();
    o["id"] = msgs[i].id;
    o["author"] = msgs[i].author;
    o["type"] = msgs[i].type;
    o["text"] = msgs[i].text;
    o["expires"] = msgs[i].expires;
  }
  File tmp = LittleFS.open("/msgs.tmp", FILE_WRITE);
  if (!tmp)
    return;
  serializeJson(doc, tmp);
  tmp.close();
  LittleFS.remove(Config::STORAGE_FILE);
  LittleFS.rename("/msgs.tmp", Config::STORAGE_FILE);
}

void loadMessages()
{
  if (!LittleFS.exists(Config::STORAGE_FILE))
    return;
  File f = LittleFS.open(Config::STORAGE_FILE);
  if (!f)
    return;
  DynamicJsonDocument doc(81920); // ~80KB; sized for 200 worst-case messages
  if (deserializeJson(doc, f))
  {
    f.close();
    return;
  }
  JsonArray arr = doc.as<JsonArray>();
  msgCount = 0;
  for (JsonObject o : arr)
  {
    if (msgCount >= Config::MAX_MSGS)
      break;
    msgs[msgCount].id = o["id"] | nextMsgId;
    msgs[msgCount].author = (const char *)o["author"];
    msgs[msgCount].type = (const char *)o["type"];
    msgs[msgCount].text = (const char *)o["text"];
    msgs[msgCount].expires = o["expires"];
    if (msgs[msgCount].id >= nextMsgId)
      nextMsgId = msgs[msgCount].id + 1;
    msgCount++;
  }
  f.close();
}

void addMessage(String author, String type, String text, int expiryHours)
{
  if (msgCount >= Config::MAX_MSGS)
  {
    // Find the oldest expired post and evict it
    unsigned long now = nowSecs();
    int evict = -1;
    unsigned long oldest = ULONG_MAX;
    for (int i = 0; i < msgCount; i++)
    {
      if (msgs[i].expires <= now && msgs[i].expires < oldest)
      {
        oldest = msgs[i].expires;
        evict = i;
      }
    }
    if (evict < 0)
      return; // No expired posts — board is genuinely full
    // Shift everything above the evicted slot down one
    for (int i = evict; i < msgCount - 1; i++)
      msgs[i] = msgs[i + 1];
    msgCount--;
  }
  msgs[msgCount].id = nextMsgId++;
  msgs[msgCount].author = author;
  msgs[msgCount].type = type;
  msgs[msgCount].text = text;
  msgs[msgCount].expires = nowSecs() + expiryHours * 3600;
  msgCount++;
  lastPostTime = nowSecs();
  if (!msgsDirty)
  {
    msgsDirty = true;
    lastMsgDirtyTime = millis();
  }
}

// ===================== LED =====================
void updateLED()
{
  digitalWrite(led, HIGH); // turn the LED on (HIGH is the voltage level)
  delay(100);              // wait for a half second
  digitalWrite(led, LOW);  // turn the LED off by making the voltage LOW
  delay(3000);
}

// ===================== HTML: MAIN BOARD =====================
// Imported from frontend.html

// Escapes a string for safe injection into a JS double-quoted string literal.
String jsEscape(const String &s)
{
  String out;
  out.reserve(s.length());
  for (unsigned int i = 0; i < s.length(); i++)
  {
    char c = s.charAt(i);
    if (c == '"')
      out += "\\\"";
    else if (c == '\\')
      out += "\\\\";
    else if (c == '\n')
      out += "\\n";
    else if (c == '\r')
      out += "\\r";
    else
      out += c;
  }
  return out;
}

// ===================== HTML: ADMIN PANEL =====================
// The admin key is NEVER sent to the browser.
// The gate POSTs the key to /admin/auth which returns a session token.
// All subsequent admin calls use token= not key=.

// String buildAdminPage()
// {
//   String page = F(R"rawliteral(

// )rawliteral");

//   return page;
// }

String buildAdminPage()
{
  // 1. Open the admin.html file from LittleFS
  File file = LittleFS.open("/admin.html", "r");
  if (!file)
  {
    return "<h1>Error: admin.html not found in LittleFS</h1>";
  }

  // 2. Read the entire file into a String
  String html = file.readString();
  file.close();

  // 3. Build the dynamic JavaScript string (the part you wanted to keep in C++)
  String jsData = "let SESSION_TOKEN = '';\n";
  jsData += "window.addEventListener('DOMContentLoaded', () => {\n";
  jsData += "  document.getElementById('idName').value    = \"" + jsEscape(id_name) + "\";\n";
  jsData += "  document.getElementById('idIcon').value    = \"" + jsEscape(id_icon) + "\";\n";
  jsData += "  document.getElementById('idTagline').value = \"" + jsEscape(id_tagline) + "\";\n";
  jsData += "  document.getElementById('idRules').value   = \"" + jsEscape(id_rules) + "\";\n";
  jsData += "  document.getElementById('idFooter').value  = \"" + jsEscape(id_footer) + "\";\n";
  jsData += "});\n";

  // 4. Replace the placeholder in the HTML with the dynamic JS
  html.replace("<!-- JS_INJECTION -->", jsData);

  return html;
}

// ===================== WEB SERVER =====================
DNSServer dnsServer;
WebServer server(80);

bool checkKey()
{ // WOTS DA PASSWARD?
  if (sessionToken.length() == 0)
    return false;
  if (millis() - tokenIssuedAt > TOKEN_LIFETIME_MS)
    return false;
  if (!server.hasArg("token"))
    return false;
  return server.arg("token") == sessionToken;
}

// Strip angle brackets and trim whitespace to prevent HTML injection.
// Applied to all user-supplied text before storage.
// Because users are hostile, whether they mean to be or not.
String sanitize(const String &s, int maxLen)
{
  String out;
  out.reserve(s.length());
  for (unsigned int i = 0; i < s.length(); i++)
  {
    char c = s.charAt(i);
    if (c != '<' && c != '>')
      out += c;
  }
  out.trim();
  if ((int)out.length() > maxLen)
    out = out.substring(0, maxLen);
  return out;
}

// Accept only the four known post types; fall back to "Notice".
String validateType(const String &t)
{
  if (t == "Notice" || t == "Offer" || t == "Need" || t == "Event")
    return t;
  return "Notice";
}

void handleRoot()
{
  File file = LittleFS.open("/frontend.html", "r");
  if (!file)
  {
    server.send(404, "text/plain", "File not found");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

void handleAdmin()
{
  File file = LittleFS.open("/admin.html", "r");
  if (!file)
  {
    server.send(404, "text/plain", "admin.html not found");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

void handleAdminAuth()
{
  // Key submitted via POST body as JSON: {"key":"..."}
  // Never echoed back — only a token is returned on success.
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, server.arg("plain")))
  {
    server.send(400, "text/plain", "bad request");
    return;
  }
  String submitted = doc["key"] | "";
  if (submitted == adminKey)
  {
    sessionToken = generateToken();
    server.send(200, "text/plain", sessionToken);
  }
  else
  {
    server.send(403, "text/plain", "forbidden");
  }
}

void handleInfo()
{
  DynamicJsonDocument doc(512);
  doc["name"] = id_name;
  doc["icon"] = id_icon;
  doc["tagline"] = id_tagline;
  doc["rules"] = id_rules;
  doc["footer"] = id_footer;
  doc["uptime"] = formatUptime();
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleMessages()
{
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.to<JsonArray>();
  unsigned long now = nowSecs();
  for (int i = 0; i < msgCount; i++)
  {
    if (msgs[i].expires < now)
      continue;
    JsonObject o = arr.createNestedObject();
    o["id"] = msgs[i].id;
    o["author"] = msgs[i].author;
    o["type"] = msgs[i].type;
    o["text"] = msgs[i].text;
    o["expires"] = msgs[i].expires;
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handlePost()
{
  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, server.arg("plain")))
  {
    server.send(400, "text/plain", "bad json");
    return;
  }
  String author = sanitize(doc["author"] | "neighbor", 24); // Won't you be my neighbor?
  String type = validateType(doc["type"] | "Notice");
  String text = sanitize(doc["text"] | "", 300);
  int expiry = doc["expiry"] | Config::DEFAULT_EXPIRY_HOURS;

  if (author.isEmpty())
    author = "neighbor";
  if (text.isEmpty())
  {
    server.send(400, "text/plain", "empty message");
    return;
  }

  addMessage(author, type, text, expiry);
  server.send(200, "text/plain", "ok");
}

// ── Admin handlers ────────────────────────────────────────────────────────────
void handleGetConfig()
{
  String json = "{";
  json += "\"name\":\"" + jsEscape(id_name) + "\",";
  json += "\"icon\":\"" + jsEscape(id_icon) + "\",";
  json += "\"tagline\":\"" + jsEscape(id_tagline) + "\",";
  json += "\"rules\":\"" + jsEscape(id_rules) + "\",";
  json += "\"footer\":\"" + jsEscape(id_footer) + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleAdminIdentityGet()
{
  if (!checkKey())
  {
    server.send(403, "text/plain", "forbidden");
    return;
  }
  DynamicJsonDocument doc(1024);
  doc["name"] = id_name;
  doc["icon"] = id_icon;
  doc["tagline"] = id_tagline;
  doc["rules"] = id_rules;
  doc["footer"] = id_footer;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleAdminIdentitySet()
{
  if (!checkKey())
  {
    server.send(403, "text/plain", "forbidden");
    return;
  }
  if (server.hasArg("name") && server.arg("name").length())
    id_name = sanitize(server.arg("name"), 48);
  if (server.hasArg("icon") && server.arg("icon").length())
    id_icon = sanitize(server.arg("icon"), 8);
  if (server.hasArg("tagline"))
    id_tagline = sanitize(server.arg("tagline"), 100);
  if (server.hasArg("rules"))
    id_rules = sanitize(server.arg("rules"), 100);
  if (server.hasArg("footer"))
    id_footer = sanitize(server.arg("footer"), 100);
  saveIdentityConfig();
  server.send(200, "text/plain", "identity saved");
}

void handleAdminTime()
{
  if (!checkKey())
  {
    server.send(403, "text/plain", "forbidden");
    return;
  }
  if (!setTimeFromString(server.arg("time")))
  {
    server.send(400, "text/plain", "bad format — use DDMMYYYY-HHMM");
    return;
  }
  saveTime();
  server.send(200, "text/plain", "time set");
}
// bored
void handleAdminLedGet()
{
  if (!checkKey())
  {
    server.send(403, "text/plain", "forbidden");
    return;
  }
  DynamicJsonDocument doc(512);
  doc["day_br"] = led_day_brightness;
  doc["night_br"] = led_night_brightness;
  doc["day_st"] = led_day_start;
  doc["night_st"] = led_night_start;
  doc["pin"] = led_pin;
  doc["enabled"] = led_enabled;
  doc["pulse"] = led_pulse_enabled;
  doc["activity"] = led_activity_enabled;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}
// so bored
void handleAdminLedSet()
{
  if (!checkKey())
  {
    server.send(403, "text/plain", "forbidden");
    return;
  }
  if (server.hasArg("day_br"))
    led_day_brightness = constrain(server.arg("day_br").toInt(), 0, 100);
  if (server.hasArg("night_br"))
    led_night_brightness = constrain(server.arg("night_br").toInt(), 0, 100);
  if (server.hasArg("day_st"))
    led_day_start = constrain(server.arg("day_st").toInt(), 0, 23);
  if (server.hasArg("night_st"))
    led_night_start = constrain(server.arg("night_st").toInt(), 0, 23);
  if (server.hasArg("pin"))
  {
    int newPin = constrain(server.arg("pin").toInt(), 0, 48);
    if (newPin != led_pin)
    {
      analogWrite(led_pin, 0);
      pinMode(led_pin, INPUT);
      led_pin = newPin;
      pinMode(led_pin, OUTPUT);
    }
  }
  if (server.hasArg("enabled"))
    led_enabled = server.arg("enabled") == "1";
  if (server.hasArg("pulse"))
    led_pulse_enabled = server.arg("pulse") == "1";
  if (server.hasArg("activity"))
    led_activity_enabled = server.arg("activity") == "1";
  saveLedConfig();
  server.send(200, "text/plain", "LED settings saved");
}

void handleAdminBackup()
{ // C'mon shawty, back that data up!
  if (!checkKey())
  {
    server.send(403, "text/plain", "forbidden");
    return;
  }
  if (msgsDirty)
  {
    saveMessages();
    msgsDirty = false;
  }
  File f = LittleFS.open(Config::STORAGE_FILE);
  if (!f)
  {
    server.send(500, "text/plain", "no file");
    return;
  }
  String out = f.readString();
  f.close();
  server.send(200, "application/json", out);
}

void handleAdminRestore()
{
  if (!checkKey())
  {
    server.send(403, "text/plain", "forbidden");
    return;
  }
  DynamicJsonDocument doc(16384);
  if (deserializeJson(doc, server.arg("plain")))
  {
    server.send(400, "text/plain", "bad json");
    return;
  }
  msgCount = 0;
  for (JsonObject o : doc.as<JsonArray>())
  {
    if (msgCount >= Config::MAX_MSGS)
      break;
    msgs[msgCount].author = (const char *)o["author"];
    msgs[msgCount].type = (const char *)o["type"];
    msgs[msgCount].text = (const char *)o["text"];
    msgs[msgCount].expires = o["expires"];
    msgCount++;
  }
  saveMessages();
  server.send(200, "text/plain", "restored " + String(msgCount) + " messages");
}

void handleAdminSetKey()
{
  if (!checkKey())
  {
    server.send(403, "text/plain", "forbidden");
    return;
  }
  String newKey = server.arg("newkey");
  newKey.trim();
  if (newKey.length() < 4)
  {
    server.send(400, "text/plain", "key must be at least 4 characters"); // Ugh, size queen
    return;
  }
  adminKey = newKey;
  saveAdminKey();
  server.send(200, "text/plain", "key updated — page will reload");
}

void handleAdminOTA()
{
  if (!checkKey())
  {
    server.send(403, "text/plain", "forbidden");
    return;
  }
  server.send(200, "text/plain", Update.hasError() ? "UPDATE FAILED" : "UPDATE OK — rebooting");
  delay(500);
  ESP.restart();
}

void handleAdminOTAUpload()
{
  if (!checkKey())
    return;
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START)
  {
    Serial.printf("[OTA] Starting: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN))
    {
      Update.printError(Serial);
    }
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
    {
      Update.printError(Serial);
    }
    Serial.printf("[OTA] Written %u bytes\n", upload.currentSize);
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    if (Update.end(true))
    {
      Serial.printf("[OTA] Success: %u bytes total\n", upload.totalSize);
    }
    else
    {
      Update.printError(Serial);
    }
  }
}

void handleAdminFlush()
{
  if (!checkKey())
  {
    server.send(403, "text/plain", "forbidden");
    return;
  }
  saveMessages();
  msgsDirty = false;
  saveTime();
  server.send(200, "text/plain", "flushed");
}

void handleAdminDeletePost()
{
  if (!checkKey())
  {
    server.send(403, "text/plain", "forbidden");
    return;
  }
  if (!server.hasArg("id"))
  {
    server.send(400, "text/plain", "missing id");
    return;
  }
  uint16_t targetId = (uint16_t)server.arg("id").toInt();
  for (int i = 0; i < msgCount; i++)
  {
    if (msgs[i].id == targetId)
    {
      // Shift remaining messages down to fill the gap
      for (int j = i; j < msgCount - 1; j++)
        msgs[j] = msgs[j + 1];
      msgCount--;
      if (!msgsDirty)
      {
        msgsDirty = true;
        lastMsgDirtyTime = millis();
      }
      server.send(200, "text/plain", "deleted");
      return;
    }
  }
  server.send(404, "text/plain", "not found");
}

void handleAdminClear()
{
  if (!checkKey())
  {
    server.send(403, "text/plain", "forbidden");
    return;
  }
  msgCount = 0;
  saveMessages();
  server.send(200, "text/plain", "cleared");
}

// ===================== SETUP =====================
void setup()
{
  pinMode(led, OUTPUT);

  bootMillis = millis();
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("╔══════════════════════════════╗");
  Serial.println("║      C O M M U N I T Y       ║");
  Serial.println("║            H U B             ║");
  Serial.println("╚══════════════════════════════╝");

  pinMode(led_pin, OUTPUT);

  // ── WiFi Access Point ──
  WiFi.mode(WIFI_AP);
  WiFi.softAP(Config::AP_SSID, Config::AP_PASS[0] ? Config::AP_PASS : nullptr,
              Config::AP_CHANNEL, 0, Config::AP_MAX_CONN);
  delay(200); // softAP needs a moment to settle

  IPAddress apIP = WiFi.softAPIP();
  Serial.println("✓ Access Point started.");
  Serial.print("  SSID : ");
  Serial.println(Config::AP_SSID);
  Serial.print("  IP   : ");
  Serial.println(apIP);
  Serial.print("  Pass : ");
  Serial.println(Config::AP_PASS[0] ? Config::AP_PASS : "(open)");
  Serial.print("  Admin: http://");
  Serial.print(apIP);
  Serial.println("/admin");

  // ── DNS — redirect every hostname to us ──
  dnsServer.start(53, "*", apIP);

  // ── LittleFS ──
  if (!LittleFS.begin(true))
  {
    Serial.println("⚠  LittleFS init failed — running without persistence."); // if this fails, we got problems.
  }
  else
  {
    Serial.println("✓ LittleFS mounted.");
    loadTime();
    loadLedConfig();
    loadAdminKey();
    loadIdentityConfig();
    loadMessages();
    Serial.printf("  Loaded %d message(s).\n", msgCount);

    // Sanity check: Just check if the file exists
    if (!LittleFS.exists("/frontend.html"))
    {
      Serial.println("⚠ ERROR: /frontend.html is missing from LittleFS!");
    }
    else
    {
      Serial.println("✓ frontend.html found.");
    }
  }

  // ── Routes ──
  server.on("/", handleRoot);
  server.on("/admin", handleAdmin);
  server.on("/admin/config", handleGetConfig);
  server.on("/admin/auth", HTTP_POST, handleAdminAuth);
  server.on("/info", handleInfo);
  server.on("/messages", handleMessages);
  server.on("/post", HTTP_POST, handlePost);
  server.on("/api/status", HTTP_GET, []()
            {
    unsigned long now = nowSecs();
    bool hasExpired = false;
    for (int i = 0; i < msgCount; i++) {
      if (msgs[i].expires <= now) { hasExpired = true; break; }
    }
    bool boardFull = (msgCount >= Config::MAX_MSGS) && !hasExpired;
    DynamicJsonDocument doc(64);
    doc["full"] = boardFull;
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out); });

  // ── Captive portal detection endpoints ──────────────────────────────────────
  // DNS resolves ALL hostnames to our IP, so only the path matters.
  // We redirect each known probe URL to "/" to trigger the portal popup.
  // Even with all this, doesn't always work. Samsung devices are especially persnickety.

  // Apple (iOS / macOS)
  server.on("/hotspot-detect.html", []()
            { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/library/test/success.html", []()
            { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/success.html", []()
            { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/captive.apple.com", []()
            { server.sendHeader("Location", "/"); server.send(302); });

  // Android / Google
  server.on("/generate_204", []()
            { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/gen_204", []()
            { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/connectivitycheck", []()
            { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/connectivity-check", []()
            { server.sendHeader("Location", "/"); server.send(302); });

  // Windows (NCSI + connecttest)
  server.on("/connecttest.txt", []()
            { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/ncsi.txt", []()
            { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/redirect", []()
            { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/fwlink/", []()
            { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/fwlink", []()
            { server.sendHeader("Location", "/"); server.send(302); });

  // Firefox browser
  server.on("/success.txt", []()
            { server.sendHeader("Location", "/"); server.send(302); });
  server.on("/canonical.html", []()
            { server.sendHeader("Location", "/"); server.send(302); });

  server.on("/admin/identity/get", handleAdminIdentityGet);
  server.on("/admin/identity/set", handleAdminIdentitySet);
  server.on("/admin/time", handleAdminTime);
  server.on("/admin/led/get", handleAdminLedGet);
  server.on("/admin/led/set", handleAdminLedSet);
  server.on("/admin/backup", handleAdminBackup);
  server.on("/admin/restore", HTTP_POST, handleAdminRestore);
  server.on("/admin/setkey", handleAdminSetKey);
  server.on("/admin/ota", HTTP_POST, handleAdminOTA, handleAdminOTAUpload);
  server.on("/admin/flush", handleAdminFlush);
  server.on("/admin/clear", handleAdminClear);
  server.on("/admin/delete/post", handleAdminDeletePost);

  // Serve related files for the admin page
  server.on("/admin.js", []()
            { 
              File file = LittleFS.open("/admin.js", "r");
              server.streamFile(file, "application/javascript"); });
  server.on("/admin.css", []()
            { 
              File file = LittleFS.open("/admin.css", "r");
              server.streamFile(file, "text/css"); });

  // Serve related files for frontend
  server.on("/frontend.js", []()
            { 
              File file = LittleFS.open("/frontend.js", "r");
              server.streamFile(file, "application/javascript"); });
  server.on("/frontend.css", []()
            { 
              File file = LittleFS.open("/frontend.css", "r");
              server.streamFile(file, "text/css"); });

  // Catch-all: redirect everything else to the board (required for captive portal)
  server.onNotFound([]()
                    { server.sendHeader("Location", "/"); server.send(302); });

  server.begin();
  Serial.println("✓ HTTP server started.\n");
}

// ===================== LOOP =====================
void loop()
{
  dnsServer.processNextRequest();
  server.handleClient();
  updateLED();

  unsigned long now = millis();

  if (msgsDirty && (now - lastMsgDirtyTime) >= 60000)
  {
    saveMessages();
    msgsDirty = false;
  }

  if (now - lastTimeSave > 1800000)
  {
    saveTime();
    lastTimeSave = now;
  }
}