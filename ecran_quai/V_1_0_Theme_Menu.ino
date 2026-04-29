#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include "esp_wifi.h"
#include "esp_ota_ops.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

#define DEFAULT_SDA_PIN 4
#define DEFAULT_SCL_PIN 3
#define EXPECTED_OLED_SDA_PIN 3
#define EXPECTED_OLED_SCL_PIN 4

#define FW_VERSION "1.0"
#define FW_BUILD_LABEL "v1.0"

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const char* AP_SSID = "SNCB-NMBS Display";
const char* AP_PASSWORD = "12345678";

WebServer server(80);
Preferences prefs;

const int MAX_TRAINS = 10;
const int VISIBLE_ROWS = 3;

const int HEADER_HEIGHT = 10;
const int SEPARATOR_Y = 10;
const int GAP_AFTER_SEPARATOR = 3;
const int FOOTER_HEIGHT = 12;
const int FOOTER_Y = SCREEN_HEIGHT - FOOTER_HEIGHT;
const int LIST_TOP_Y = SEPARATOR_Y + 1 + GAP_AFTER_SEPARATOR;
const int ROW_HEIGHT = 11;

const int X_HEURE = 1;
const int X_DEST = 36;
const int X_V = 118;

String currentLang = "FR";

int oledSdaPin = DEFAULT_SDA_PIN;
int oledSclPin = DEFAULT_SCL_PIN;
bool displayOk = false;
int screenBrightness = 255;
unsigned long bootStartMs = 0;
bool otaValidationDeferred = false;

String otaSSID = "";
String otaPassword = "";
String otaManifestUrl = "https://raw.githubusercontent.com/PM3D-Wavre/pm3d-firmware/main/ecran_quai/manifest.json";
String otaBinUrl = "";
String otaLastStatus = "Aucune verification effectuee";
String otaLastVersion = "";
bool otaWifiConnected = false;
String otaWifiStatus = "Aucune connexion Internet";
unsigned long lastOtaWifiRetryMs = 0;
const unsigned long OTA_WIFI_RETRY_MS = 10000UL;
String otaAvailableVersions = "";
const int MAX_OTA_VERSIONS = 12;
String otaVersionLabels[MAX_OTA_VERSIONS];
String otaVersionUrls[MAX_OTA_VERSIONS];
int otaVersionCount = 0;
String installedVersionLabel = FW_BUILD_LABEL;
String otaPendingInstallLabel = "";
String otaPendingInstallUrl = "";
bool otaInstallInProgress = false;
String screenCustomName = "";
int apIpSuffix = 1;

struct ThemeConfig {
  String preset;
  String bodyBg1;
  String bodyBg2;
  String panelBg1;
  String panelBg2;
  String accent;
  String accentText;
  String text;
  String muted;
  String info;
  String warn;
  String inputBg;
};

ThemeConfig theme = {
  "vert",
  "#062b1a",
  "#02150d",
  "#0a3320",
  "#041c12",
  "#00c853",
  "#03180f",
  "#ecfff4",
  "#9fe7c0",
  "#5af19b",
  "#fff0d8",
  "#02150d"
};

struct TrainItem {
  String heure;
  String destination;
  String voie;
};

TrainItem trains[MAX_TRAINS];

const char* defaultDestFR[5] = {"Bruxelles", "Anvers", "Liege", "Namur", "Mons"};
const char* defaultDestNL[5] = {"Brussel", "Antwerpen", "Luik", "Namen", "Bergen"};

String msgLine1FR = "PM3D.NET";
String msgLine2FR = "vous souhaite";
String msgLine3FR = "un bon voyage !";
String msgLine1NL = "PM3D.NET";
String msgLine2NL = "wenst u";
String msgLine3NL = "een goede reis !";
bool msgLine1CenterFR = true;
bool msgLine2CenterFR = true;
bool msgLine3CenterFR = true;
bool msgLine1CenterNL = true;
bool msgLine2CenterNL = true;
bool msgLine3CenterNL = true;
bool customMessageEditedFR = false;
bool customMessageEditedNL = false;
unsigned long customMessageEverySec = 30;
unsigned long customMessageShowSec = 6;

int baseIndex = 0;
int scrollPixelOffset = 0;
unsigned long lastAnimMs = 0;
unsigned long pauseStartMs = 0;
bool inPause = true;

const unsigned long PAUSE_MS = 5000;
const unsigned long FRAME_MS = 30;
const int PIXELS_PER_FRAME = 1;

unsigned long lastCustomMessageCycle = 0;
unsigned long customMessageShowStart = 0;
bool showingCustomMessage = false;

bool footerBlinkState = false;
unsigned long lastFooterBlinkMs = 0;
const unsigned long FOOTER_BLINK_MS = 500;

String normalizeGithubRawUrl(String url);
void saveConfig();

String sanitizeHexColor(String value, const String& fallback);
void applyPresetTheme(const String& presetName);
String makeThemePage(const String& message);
void handleThemePage();
void handleSaveTheme();

String htmlEscape(const String& s) {
  String out = s;
  out.replace("&", "&amp;");
  out.replace("\"", "&quot;");
  out.replace("<", "&lt;");
  out.replace(">", "&gt;");
  return out;
}


String htmlEscapeAttr(const String& s) {
  return htmlEscape(s);
}

String urlEncode(const String &str) {
  String encoded = "";
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < str.length(); i++) {
    unsigned char c = (unsigned char)str.charAt(i);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') encoded += (char)c;
    else { encoded += '%'; encoded += hex[(c >> 4) & 0x0F]; encoded += hex[c & 0x0F]; }
  }
  return encoded;
}

String urlDecode(const String &str) {
  String decoded = "";
  char temp[] = "0x00";
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (c == '%') {
      if (i + 2 < str.length()) {
        temp[2] = str.charAt(i + 1);
        temp[3] = str.charAt(i + 2);
        decoded += (char)strtol(temp, NULL, 16);
        i += 2;
      }
    } else if (c == '+') decoded += ' ';
    else decoded += c;
  }
  return decoded;
}

String getParamSafe(const String& name) {
  if (server.hasArg(name)) return server.arg(name);
  return "";
}

String nettoyerTexte(String s, int maxLen) {
  s.trim();
  s.replace("\r", "");
  s.replace("\n", "");
  while (s.indexOf("  ") >= 0) s.replace("  ", " ");
  if ((int)s.length() > maxLen) s = s.substring(0, maxLen);
  return s;
}

String fitText(String txt, int maxChars) {
  if ((int)txt.length() > maxChars) return txt.substring(0, maxChars);
  return txt;
}

bool isKnownDefaultDestination(const String& s) {
  for (int i = 0; i < 5; i++) {
    if (s == defaultDestFR[i] || s == defaultDestNL[i]) return true;
  }
  return false;
}

String translateDefaultDestination(const String& s, const String& targetLang) {
  for (int i = 0; i < 5; i++) {
    if (s == defaultDestFR[i] || s == defaultDestNL[i]) {
      return targetLang == "NL" ? String(defaultDestNL[i]) : String(defaultDestFR[i]);
    }
  }
  return s;
}

void translateKnownDestinationsOnly() {
  for (int i = 0; i < MAX_TRAINS; i++) {
    if (isKnownDefaultDestination(trains[i].destination)) {
      trains[i].destination = translateDefaultDestination(trains[i].destination, currentLang);
    }
  }
}

int countValidTrains() {
  int count = 0;
  for (int i = 0; i < MAX_TRAINS; i++) {
    if (trains[i].heure.length() > 0 || trains[i].destination.length() > 0 || trains[i].voie.length() > 0) {
      count++;
    }
  }
  return count;
}

void compacterTrains() {
  TrainItem temp[MAX_TRAINS];
  int pos = 0;

  for (int i = 0; i < MAX_TRAINS; i++) {
    if (trains[i].heure.length() > 0 || trains[i].destination.length() > 0 || trains[i].voie.length() > 0) {
      temp[pos++] = trains[i];
    }
  }

  for (int i = pos; i < MAX_TRAINS; i++) {
    temp[i].heure = "";
    temp[i].destination = "";
    temp[i].voie = "";
  }

  for (int i = 0; i < MAX_TRAINS; i++) {
    trains[i] = temp[i];
  }
}

int wrappedTrainIndex(int logicalIndex, int total) {
  if (total <= 0) return 0;
  while (logicalIndex >= total) logicalIndex -= total;
  while (logicalIndex < 0) logicalIndex += total;
  return logicalIndex;
}

String getMsgLine(int lineIndex) {
  bool nl = (currentLang == "NL");
  if (lineIndex == 0) return nl ? msgLine1NL : msgLine1FR;
  if (lineIndex == 1) return nl ? msgLine2NL : msgLine2FR;
  return nl ? msgLine3NL : msgLine3FR;
}

bool getMsgCenter(int lineIndex) {
  bool nl = (currentLang == "NL");
  if (lineIndex == 0) return nl ? msgLine1CenterNL : msgLine1CenterFR;
  if (lineIndex == 1) return nl ? msgLine2CenterNL : msgLine2CenterFR;
  return nl ? msgLine3CenterNL : msgLine3CenterFR;
}


String sanitizeHexColor(String value, const String& fallback) {
  value.trim();
  if (value.length() == 0) return fallback;
  if (!value.startsWith("#")) value = "#" + value;
  if (value.length() != 7) return fallback;
  for (int i = 1; i < 7; i++) {
    char c = value.charAt(i);
    bool ok = (c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F');
    if (!ok) return fallback;
  }
  value.toUpperCase();
  return value;
}

void applyPresetTheme(const String& presetName) {
  String p = presetName;
  p.toLowerCase();

  if (p == "vert" || p == "green") {
    theme.preset = "vert";
    theme.bodyBg1 = "#062B1A"; theme.bodyBg2 = "#02150D";
    theme.panelBg1 = "#0A3320"; theme.panelBg2 = "#041C12";
    theme.accent = "#00C853"; theme.accentText = "#03180F";
    theme.text = "#ECFFF4"; theme.muted = "#9FE7C0";
    theme.info = "#5AF19B"; theme.warn = "#FFF0D8"; theme.inputBg = "#02150D";
  } else if (p == "jaune" || p == "yellow") {
    theme.preset = "jaune";
    theme.bodyBg1 = "#2D2400"; theme.bodyBg2 = "#161100";
    theme.panelBg1 = "#3A2F00"; theme.panelBg2 = "#1D1700";
    theme.accent = "#FFD400"; theme.accentText = "#241D00";
    theme.text = "#FFFBE6"; theme.muted = "#FFE27A";
    theme.info = "#FFD95A"; theme.warn = "#FFF0D8"; theme.inputBg = "#161100";
  } else if (p == "rouge" || p == "red") {
    theme.preset = "rouge";
    theme.bodyBg1 = "#2B0808"; theme.bodyBg2 = "#140303";
    theme.panelBg1 = "#3A0D0D"; theme.panelBg2 = "#1A0505";
    theme.accent = "#FF3B30"; theme.accentText = "#220403";
    theme.text = "#FFECEC"; theme.muted = "#FFB0AA";
    theme.info = "#FF7D73"; theme.warn = "#FFF0D8"; theme.inputBg = "#140303";
  } else if (p == "bleu" || p == "blue") {
    theme.preset = "bleu";
    theme.bodyBg1 = "#071B36"; theme.bodyBg2 = "#030C18";
    theme.panelBg1 = "#0B2347"; theme.panelBg2 = "#061327";
    theme.accent = "#49C2FF"; theme.accentText = "#03101C";
    theme.text = "#EEF5FF"; theme.muted = "#A9C9FF";
    theme.info = "#71ACFF"; theme.warn = "#FFF0D8"; theme.inputBg = "#030C18";

} else if (p == "pm3d") {
  theme.preset = "pm3d";
  theme.bodyBg1 = "#071B36"; theme.bodyBg2 = "#030C18";
  theme.panelBg1 = "#0B2347"; theme.panelBg2 = "#061327";
  theme.accent = "#49C2FF"; theme.accentText = "#03101C";
  theme.text = "#F3FAFF"; theme.muted = "#9FD9FF";
  theme.info = "#7DD3FF"; theme.warn = "#FFF0D8"; theme.inputBg = "#020A14";
  } else if (p == "orange") {
    theme.preset = "orange";
    theme.bodyBg1 = "#2A1200"; theme.bodyBg2 = "#140A00";
    theme.panelBg1 = "#2B1400"; theme.panelBg2 = "#160B00";
    theme.accent = "#FF8C1A"; theme.accentText = "#2B1400";
    theme.text = "#FFF4E8"; theme.muted = "#FFD6A3";
    theme.info = "#FFB457"; theme.warn = "#FFF0D8"; theme.inputBg = "#140A00";
  } else if (p == "violet" || p == "purple") {
    theme.preset = "violet";
    theme.bodyBg1 = "#1E1038"; theme.bodyBg2 = "#0B0617";
    theme.panelBg1 = "#28144A"; theme.panelBg2 = "#140A25";
    theme.accent = "#A855F7"; theme.accentText = "#14081F";
    theme.text = "#F7EEFF"; theme.muted = "#D5B2FF";
    theme.info = "#C88CFF"; theme.warn = "#FFF0D8"; theme.inputBg = "#0B0617";
  } else if (p == "rose" || p == "pink") {
    theme.preset = "rose";
    theme.bodyBg1 = "#341121"; theme.bodyBg2 = "#17060E";
    theme.panelBg1 = "#45162B"; theme.panelBg2 = "#220A15";
    theme.accent = "#FF4FA0"; theme.accentText = "#220713";
    theme.text = "#FFF0F7"; theme.muted = "#FFB8D6";
    theme.info = "#FF88BF"; theme.warn = "#FFF0D8"; theme.inputBg = "#17060E";
  } else {
    applyPresetTheme("pm3d");
  }
}

void saveConfig() {
  prefs.begin("horaires", false);
  prefs.putString("lang", currentLang);
  prefs.putString("msg1FR", msgLine1FR);
  prefs.putString("msg2FR", msgLine2FR);
  prefs.putString("msg3FR", msgLine3FR);
  prefs.putString("msg1NL", msgLine1NL);
  prefs.putString("msg2NL", msgLine2NL);
  prefs.putString("msg3NL", msgLine3NL);
  prefs.putBool("msg1cFR", msgLine1CenterFR);
  prefs.putBool("msg2cFR", msgLine2CenterFR);
  prefs.putBool("msg3cFR", msgLine3CenterFR);
  prefs.putBool("msg1cNL", msgLine1CenterNL);
  prefs.putBool("msg2cNL", msgLine2CenterNL);
  prefs.putBool("msg3cNL", msgLine3CenterNL);
  prefs.putBool("msgEditFR", customMessageEditedFR);
  prefs.putBool("msgEditNL", customMessageEditedNL);
  prefs.putULong("msgEvery", customMessageEverySec);
  prefs.putULong("msgShow", customMessageShowSec);

  prefs.putInt("oledSDA", oledSdaPin);
  prefs.putInt("oledSCL", oledSclPin);
  prefs.putInt("oledBright", screenBrightness);
  prefs.putString("otassid", otaSSID);
  prefs.putString("otapass", otaPassword);
  prefs.putString("otaManUrl", otaManifestUrl);
  prefs.putString("otaBinUrl", otaBinUrl);
  prefs.putString("otaStatus", otaLastStatus);
  prefs.putString("otaVer", otaLastVersion);
  prefs.putString("otawstat", otaWifiStatus);
  prefs.putString("installedVer", installedVersionLabel);
  prefs.putString("otaPendLbl", otaPendingInstallLabel);
  prefs.putString("otaPendUrl", otaPendingInstallUrl);
  prefs.putBool("otaInProg", otaInstallInProgress);
  prefs.putString("scrName", screenCustomName);
  prefs.putInt("apSuffix", apIpSuffix);
  prefs.putString("thPreset", theme.preset);
  prefs.putString("thBg1", theme.bodyBg1);
  prefs.putString("thBg2", theme.bodyBg2);
  prefs.putString("thPan1", theme.panelBg1);
  prefs.putString("thPan2", theme.panelBg2);
  prefs.putString("thAcc", theme.accent);
  prefs.putString("thAccTxt", theme.accentText);
  prefs.putString("thText", theme.text);
  prefs.putString("thMuted", theme.muted);
  prefs.putString("thInfo", theme.info);
  prefs.putString("thWarn", theme.warn);
  prefs.putString("thInput", theme.inputBg);

  for (int i = 0; i < MAX_TRAINS; i++) {
    prefs.putString(("h" + String(i)).c_str(), trains[i].heure);
    prefs.putString(("d" + String(i)).c_str(), trains[i].destination);
    prefs.putString(("v" + String(i)).c_str(), trains[i].voie);
  }
  prefs.end();
}

void loadConfig() {
  prefs.begin("horaires", true);
  currentLang = prefs.getString("lang", "FR");

  msgLine1FR = prefs.getString("msg1FR", "PM3D.NET");
  msgLine2FR = prefs.getString("msg2FR", "vous souhaite");
  msgLine3FR = prefs.getString("msg3FR", "un bon voyage !");
  msgLine1NL = prefs.getString("msg1NL", "PM3D.NET");
  msgLine2NL = prefs.getString("msg2NL", "wenst u");
  msgLine3NL = prefs.getString("msg3NL", "een goede reis !");
  msgLine1CenterFR = prefs.getBool("msg1cFR", true);
  msgLine2CenterFR = prefs.getBool("msg2cFR", true);
  msgLine3CenterFR = prefs.getBool("msg3cFR", true);
  msgLine1CenterNL = prefs.getBool("msg1cNL", true);
  msgLine2CenterNL = prefs.getBool("msg2cNL", true);
  msgLine3CenterNL = prefs.getBool("msg3cNL", true);
  customMessageEditedFR = prefs.getBool("msgEditFR", false);
  customMessageEditedNL = prefs.getBool("msgEditNL", false);
  customMessageEverySec = prefs.getULong("msgEvery", 30);
  customMessageShowSec = prefs.getULong("msgShow", 6);
  if (customMessageShowSec < 1) customMessageShowSec = 6;

  if (msgLine1FR.length() == 0 && msgLine2FR.length() == 0 && msgLine3FR.length() == 0) {
    msgLine1FR = "PM3D.NET"; msgLine2FR = "vous souhaite"; msgLine3FR = "un bon voyage !";
    customMessageEditedFR = false;
  }
  if (msgLine1NL.length() == 0 && msgLine2NL.length() == 0 && msgLine3NL.length() == 0) {
    msgLine1NL = "PM3D.NET"; msgLine2NL = "wenst u"; msgLine3NL = "een goede reis !";
    customMessageEditedNL = false;
  }
  if (msgLine1FR.length() == 0 && msgLine2FR.length() == 0 && msgLine3FR.length() == 0) {
    msgLine1FR = "PM3D.NET"; msgLine2FR = "vous souhaite"; msgLine3FR = "un bon voyage !";
    customMessageEditedFR = false;
  }
  if (msgLine1NL.length() == 0 && msgLine2NL.length() == 0 && msgLine3NL.length() == 0) {
    msgLine1NL = "PM3D.NET"; msgLine2NL = "wenst u"; msgLine3NL = "een goede reis !";
    customMessageEditedNL = false;
  }
  if (msgLine1FR == "PM3D.NET" && msgLine2FR == "vous souhaite" && msgLine3FR == "un bon voyage !") {
    msgLine1CenterFR = true; msgLine2CenterFR = true; msgLine3CenterFR = true;
  }
  if (msgLine1NL == "PM3D.NET" && msgLine2NL == "wenst u" && msgLine3NL == "een goede reis !") {
    msgLine1CenterNL = true; msgLine2CenterNL = true; msgLine3CenterNL = true;
  }

  oledSdaPin = prefs.getInt("oledSDA", DEFAULT_SDA_PIN);
  oledSclPin = prefs.getInt("oledSCL", DEFAULT_SCL_PIN);
  screenBrightness = prefs.getInt("oledBright", 255);
  if (screenBrightness < 0) screenBrightness = 0;
  if (screenBrightness > 255) screenBrightness = 255;
  otaSSID = prefs.getString("otassid", "");
  otaPassword = prefs.getString("otapass", "");
  otaWifiStatus = prefs.getString("otawstat", "Aucune connexion Internet");
  installedVersionLabel = prefs.getString("installedVer", FW_BUILD_LABEL);
  otaPendingInstallLabel = prefs.getString("otaPendLbl", "");
  otaPendingInstallUrl = prefs.getString("otaPendUrl", "");
  otaInstallInProgress = prefs.getBool("otaInProg", false);
  screenCustomName = prefs.getString("scrName", "");
  apIpSuffix = prefs.getInt("apSuffix", 1);
  if (apIpSuffix < 1) apIpSuffix = 1;
  if (apIpSuffix > 254) apIpSuffix = 254;
  otaManifestUrl = prefs.getString("otaManUrl", "https://raw.githubusercontent.com/PM3D-Wavre/pm3d-firmware/main/ecran_quai/manifest.json");
  otaBinUrl = normalizeGithubRawUrl(prefs.getString("otaBinUrl", ""));
  otaLastStatus = prefs.getString("otaStatus", "Aucune verification effectuee");
  otaLastVersion = prefs.getString("otaVer", "");

  for (int i = 0; i < MAX_TRAINS; i++) {
    trains[i].heure = prefs.getString(("h" + String(i)).c_str(), "");
    trains[i].destination = prefs.getString(("d" + String(i)).c_str(), "");
    trains[i].voie = prefs.getString(("v" + String(i)).c_str(), "");
  }

  String themePreset = prefs.getString("thPreset", "pm3d");
  applyPresetTheme(themePreset);
  theme.bodyBg1 = sanitizeHexColor(prefs.getString("thBg1", theme.bodyBg1), theme.bodyBg1);
  theme.bodyBg2 = sanitizeHexColor(prefs.getString("thBg2", theme.bodyBg2), theme.bodyBg2);
  theme.panelBg1 = sanitizeHexColor(prefs.getString("thPan1", theme.panelBg1), theme.panelBg1);
  theme.panelBg2 = sanitizeHexColor(prefs.getString("thPan2", theme.panelBg2), theme.panelBg2);
  theme.accent = sanitizeHexColor(prefs.getString("thAcc", theme.accent), theme.accent);
  theme.accentText = sanitizeHexColor(prefs.getString("thAccTxt", theme.accentText), theme.accentText);
  theme.text = sanitizeHexColor(prefs.getString("thText", theme.text), theme.text);
  theme.muted = sanitizeHexColor(prefs.getString("thMuted", theme.muted), theme.muted);
  theme.info = sanitizeHexColor(prefs.getString("thInfo", theme.info), theme.info);
  theme.warn = sanitizeHexColor(prefs.getString("thWarn", theme.warn), theme.warn);
  theme.inputBg = sanitizeHexColor(prefs.getString("thInput", theme.inputBg), theme.inputBg);
  prefs.end();

  compacterTrains();

  if (msgLine1FR == "PM3D.NET" && msgLine2FR == "vous souhaite" && msgLine3FR == "un bon voyage !") {
    msgLine1CenterFR = true; msgLine2CenterFR = true; msgLine3CenterFR = true;
  }
  if (msgLine1NL == "PM3D.NET" && msgLine2NL == "wenst u" && msgLine3NL == "een goede reis !") {
    msgLine1CenterNL = true; msgLine2CenterNL = true; msgLine3CenterNL = true;
  }

  if (countValidTrains() == 0) {
    if (currentLang == "NL") {
      trains[0] = {"08:12", "Brussel", "1"};
      trains[1] = {"08:19", "Antwerpen", "3"};
      trains[2] = {"08:27", "Luik", "2"};
      trains[3] = {"08:34", "Namen", "5"};
      trains[4] = {"08:41", "Bergen", "4"};
    } else {
      trains[0] = {"08:12", "Bruxelles", "1"};
      trains[1] = {"08:19", "Anvers", "3"};
      trains[2] = {"08:27", "Liege", "2"};
      trains[3] = {"08:34", "Namur", "5"};
      trains[4] = {"08:41", "Mons", "4"};
    }
    saveConfig();
  }
}

String t_title()       { return currentLang == "NL" ? "Vertrekbord" : "Tableau des départs"; }
String t_small()       { return currentLang == "NL" ? "3 vaste banden. Alleen de tekst schuift omhoog. Berichtenscherm instelbaar." : "3 bandeaux fixes. Seul le texte défile vers le haut. Écran message configurable."; }
String t_save()        { return currentLang == "NL" ? "Opslaan" : "Enregistrer"; }
String t_saved()       { return currentLang == "NL" ? "Gegevens opgeslagen." : "Données enregistrées."; }
String t_train()       { return currentLang == "NL" ? "Trein " : "Train "; }
String t_hour()        { return currentLang == "NL" ? "Uur" : "Heure"; }
String t_dest()        { return currentLang == "NL" ? "Richting" : "Destination"; }
String t_track_label() { return currentLang == "NL" ? "Spoor" : "Voie"; }
String t_connect()     { return currentLang == "NL" ? "Verbinding" : "Connexion"; }
String t_password()    { return currentLang == "NL" ? "Wachtwoord" : "Mot de passe"; }
String t_address()     { return currentLang == "NL" ? "Adres" : "Adresse"; }
String t_language()    { return currentLang == "NL" ? "Taal" : "Langue"; }
String t_msg_title()   { return currentLang == "NL" ? "Vrij bericht" : "Texte libre"; }
String t_msg_every()   { return currentLang == "NL" ? "Tonen om de" : "Afficher toutes les"; }
String t_seconds()     { return currentLang == "NL" ? "seconden" : "secondes"; }
String t_duration()    { return currentLang == "NL" ? "Duur" : "Duree"; }
String t_line_label(int n) { return currentLang == "NL" ? "Lijn " + String(n) : "Ligne " + String(n); }
String t_center()      { return currentLang == "NL" ? "Centreren" : "Centrer"; }
String t_banner()      { return currentLang == "NL" ? "Gemaakt door PM3D - bezoek pm3d.net" : "Produit par PM3D - visitez pm3d.net"; }
String t_preview()     { return currentLang == "NL" ? "Voorbeeld" : "Aperçu"; }

String t_device_title() { return currentLang == "NL" ? "Scherminstellingen" : "Réglages écran"; }
String t_device_name()  { return currentLang == "NL" ? "Naam van het scherm" : "Nom de l ecran"; }
String t_brightness()   { return currentLang == "NL" ? "Helderheid scherm" : "Luminosite ecran"; }
String t_brightness_saved() { return currentLang == "NL" ? "Helderheid opgeslagen." : "Luminosite enregistree."; }
String t_signal_num()   { return currentLang == "NL" ? "Nummer" : "Numero"; }
String t_saved_device() { return currentLang == "NL" ? "Scherminstellingen opgeslagen." : "Reglages ecran enregistres."; }

String t_theme()        { return currentLang == "NL" ? "Thema" : "Thème"; }
String t_theme_title()  { return currentLang == "NL" ? "Interface-thema" : "Thème de l interface"; }
String t_theme_saved()  { return currentLang == "NL" ? "Thema opgeslagen." : "Thème enregistré."; }
String t_rgb_help()     { return currentLang == "NL" ? "RGB-code in formaat #RRGGBB." : "Code RGB au format #RRGGBB."; }
String t_back()         { return currentLang == "NL" ? "Terug" : "Retour"; }
String t_apply()        { return currentLang == "NL" ? "Toepassen" : "Appliquer"; }
String t_custom_colors(){ return currentLang == "NL" ? "Aangepaste kleuren" : "Couleurs personnalisées"; }
String t_choose_theme() { return currentLang == "NL" ? "Kies een basis-thema" : "Choisissez un thème de base"; }
String t_body_bg1()     { return currentLang == "NL" ? "Fond 1" : "Fond 1"; }
String t_body_bg2()     { return currentLang == "NL" ? "Fond 2" : "Fond 2"; }
String t_panel_bg1()    { return currentLang == "NL" ? "Panneau 1" : "Panneau 1"; }
String t_panel_bg2()    { return currentLang == "NL" ? "Panneau 2" : "Panneau 2"; }
String t_accent()       { return currentLang == "NL" ? "Accent" : "Accent"; }
String t_accent_text()  { return currentLang == "NL" ? "Texte accent" : "Texte accent"; }
String t_text_color()   { return currentLang == "NL" ? "Texte principal" : "Texte principal"; }
String t_muted_color()  { return currentLang == "NL" ? "Texte secondaire" : "Texte secondaire"; }
String t_info_color()   { return currentLang == "NL" ? "Info" : "Info"; }
String t_warn_color()   { return currentLang == "NL" ? "Avertissement" : "Avertissement"; }
String t_input_bg()     { return currentLang == "NL" ? "Fond champs" : "Fond des champs"; }

String currentApIpString() {
  return String("192.168.4.") + String(apIpSuffix);
}

String currentScreenDisplayName() {
  return screenCustomName.length() ? screenCustomName : String("Ecran de quai");
}

void applyScreenBrightness() {
  if (!displayOk) return;
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command((uint8_t)screenBrightness);
}


String buildVersionLabel() {
  return String(FW_BUILD_LABEL);
}

void clearPendingOtaState(bool saveNow) {
  otaPendingInstallLabel = "";
  otaPendingInstallUrl = "";
  otaInstallInProgress = false;
  if (saveNow) saveConfig();
}

void markPendingOtaInstall(const String& label, const String& url) {
  otaPendingInstallLabel = label;
  otaPendingInstallUrl = normalizeGithubRawUrl(url);
  otaInstallInProgress = true;
  saveConfig();
}

void finalizeBootVersionState() {
  bool changed = false;

  if (installedVersionLabel.length() == 0) {
    installedVersionLabel = buildVersionLabel();
    changed = true;
  }

  if (!otaInstallInProgress && installedVersionLabel != buildVersionLabel()) {
    installedVersionLabel = buildVersionLabel();
    changed = true;
  }

  if (!otaInstallInProgress && otaPendingInstallLabel.length() > 0) {
    clearPendingOtaState(false);
    changed = true;
  }

  if (otaManifestUrl.length() == 0) {
    otaManifestUrl = "https://raw.githubusercontent.com/PM3D-Wavre/pm3d-firmware/main/ecran_quai/manifest.json";
    changed = true;
  }

  otaManifestUrl = normalizeGithubRawUrl(otaManifestUrl);
  otaBinUrl = normalizeGithubRawUrl(otaBinUrl);

  if (changed) saveConfig();
}


void resetAnimation() {
  baseIndex = 0;
  scrollPixelOffset = 0;
  inPause = true;
  pauseStartMs = millis();
  lastAnimMs = millis();
  lastCustomMessageCycle = millis();
  customMessageShowStart = 0;
  showingCustomMessage = false;
}


bool probeI2CDeviceOnPins(int sdaPin, int sclPin, uint8_t address) {
  Wire.end();
  delay(15);
  Wire.begin(sdaPin, sclPin);
  delay(30);
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool isRollbackVerificationPending() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) return false;
  esp_ota_img_states_t otaState = ESP_OTA_IMG_UNDEFINED;
  if (esp_ota_get_state_partition(running, &otaState) != ESP_OK) return false;
  return otaState == ESP_OTA_IMG_PENDING_VERIFY;
}

void validateCurrentFirmwareIfNeeded() {
  if (isRollbackVerificationPending()) {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
      Serial.println("OTA validee : rollback annule");
    } else {
      Serial.printf("Erreur validation OTA: %d\n", (int)err);
    }
  }
}

bool initializeDisplayWithAutoI2CDetection() {
  bool expectedOk = probeI2CDeviceOnPins(EXPECTED_OLED_SDA_PIN, EXPECTED_OLED_SCL_PIN, SCREEN_ADDRESS);
  bool inverseOk = false;

  if (expectedOk) {
    oledSdaPin = EXPECTED_OLED_SDA_PIN;
    oledSclPin = EXPECTED_OLED_SCL_PIN;
    Serial.printf("OLED detecte avec cablage attendu SDA=%d SCL=%d\n", oledSdaPin, oledSclPin);
  } else {
    inverseOk = probeI2CDeviceOnPins(EXPECTED_OLED_SCL_PIN, EXPECTED_OLED_SDA_PIN, SCREEN_ADDRESS);
    if (inverseOk) {
      oledSdaPin = EXPECTED_OLED_SCL_PIN;
      oledSclPin = EXPECTED_OLED_SDA_PIN;
      Serial.printf("OLED detecte avec cablage inverse SDA=%d SCL=%d\n", oledSdaPin, oledSclPin);
      otaLastStatus = "OLED detecte avec cablage inverse - adaptation automatique active";
    } else {
      oledSdaPin = EXPECTED_OLED_SDA_PIN;
      oledSclPin = EXPECTED_OLED_SCL_PIN;
      Serial.println("OLED non detecte ni en cablage attendu ni en cablage inverse");
      otaLastStatus = "OLED non detecte";
    }
  }

  Wire.end();
  delay(15);
  Wire.begin(oledSdaPin, oledSclPin);
  delay(30);
  displayOk = display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  if (displayOk) {
    display.setRotation(2);
    applyScreenBrightness();
  }
  return true;
}

void finalizePendingOtaSuccessIfNeeded() {
  if (!otaInstallInProgress || otaPendingInstallLabel.length() == 0) return;
  validateCurrentFirmwareIfNeeded();
  installedVersionLabel = otaPendingInstallLabel;
  otaLastVersion = otaPendingInstallLabel;
  otaLastStatus = "Mise a jour installee : " + otaPendingInstallLabel;
  clearPendingOtaState(false);
  saveConfig();
}

bool wifiStaConnected() {
  return WiFi.status() == WL_CONNECTED;
}

String wifiStaStatusText() {
  if (wifiStaConnected()) {
    return "Connecte a " + WiFi.SSID() + " - IP " + WiFi.localIP().toString();
  }
  if (otaSSID.length() == 0) return "Wi-Fi Internet non configure";
  return "Wi-Fi Internet non connecte";
}

void ensureOtaWifiConnection(bool waitForConnection) {
  if (otaSSID.length() == 0) {
    otaWifiConnected = false;
    otaWifiStatus = "Wi-Fi Internet non configure";
    otaLastStatus = otaWifiStatus;
    return;
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  if (WiFi.status() == WL_CONNECTED) {
    otaWifiConnected = true;
    otaWifiStatus = String("OK - ") + WiFi.localIP().toString();
    otaLastStatus = otaWifiStatus;
    return;
  }

  WiFi.disconnect(false, false);
  delay(150);
  WiFi.begin(otaSSID.c_str(), otaPassword.c_str());
  lastOtaWifiRetryMs = millis();

  if (!waitForConnection) {
    otaWifiConnected = false;
    otaWifiStatus = "Connexion Internet en cours...";
    otaLastStatus = otaWifiStatus;
    return;
  }

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  otaWifiConnected = (WiFi.status() == WL_CONNECTED);
  otaWifiStatus = otaWifiConnected ? (String("OK - ") + WiFi.localIP().toString()) : "Connexion Internet impossible";
  otaLastStatus = otaWifiStatus;

  if (otaWifiConnected) {
    Serial.print("WiFi Internet connecte: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Echec connexion WiFi Internet");
  }
}

void maintainOtaWifiConnection() {
  if (otaSSID.length() == 0) {
    otaWifiConnected = false;
    otaWifiStatus = "Wi-Fi Internet non configure";
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    otaWifiConnected = true;
    otaWifiStatus = String("OK - ") + WiFi.localIP().toString();
    return;
  }

  otaWifiConnected = false;
  otaWifiStatus = "Reconnexion Internet...";
  if (millis() - lastOtaWifiRetryMs < OTA_WIFI_RETRY_MS) return;

  Serial.println("Reconnexion Wi-Fi Internet...");
  ensureOtaWifiConnection(false);
}

String extractJsonValue(String json, const String& key) {
  String pattern = "\"" + key + "\"";
  int keyPos = json.indexOf(pattern);
  if (keyPos < 0) return "";
  int colonPos = json.indexOf(':', keyPos + pattern.length());
  if (colonPos < 0) return "";
  int firstQuote = json.indexOf('"', colonPos + 1);
  if (firstQuote < 0) return "";
  int secondQuote = json.indexOf('"', firstQuote + 1);
  if (secondQuote < 0) return "";
  return json.substring(firstQuote + 1, secondQuote);
}


void clearOtaVersions() {
  otaVersionCount = 0;
  for (int i = 0; i < MAX_OTA_VERSIONS; i++) {
    otaVersionLabels[i] = "";
    otaVersionUrls[i] = "";
  }
}

String normalizeGithubRawUrl(String url) {
  url.trim();
  String oldPrefix = "https://github.com/PM3D-Wavre/pm3d-firmware/raw/refs/heads/main/";
  if (url.startsWith(oldPrefix)) {
    url = "https://raw.githubusercontent.com/PM3D-Wavre/pm3d-firmware/main/" + url.substring(oldPrefix.length());
  }
  return url;
}

void addOtaVersion(const String& labelIn, const String& urlIn) {
  String label = labelIn;
  String url = normalizeGithubRawUrl(urlIn);
  label.trim();
  url.trim();
  if (label.length() == 0 || url.length() == 0) return;

  for (int i = 0; i < otaVersionCount; i++) {
    if (otaVersionLabels[i] == label || otaVersionUrls[i] == url) return;
  }

  if (otaVersionCount < MAX_OTA_VERSIONS) {
    otaVersionLabels[otaVersionCount] = label;
    otaVersionUrls[otaVersionCount] = url;
    otaVersionCount++;
  }
}

void parseManifestVersions(const String& payload) {
  clearOtaVersions();

  int latestPos = payload.indexOf("\"latest\"");
  if (latestPos >= 0) {
    int latestEnd = payload.indexOf("}", latestPos);
    if (latestEnd > latestPos) {
      String latestBlock = payload.substring(latestPos, latestEnd + 1);
      addOtaVersion(extractJsonValue(latestBlock, "version"), extractJsonValue(latestBlock, "url"));
    }
  }

  int versionsPos = payload.indexOf("\"versions\"");
  if (versionsPos < 0) return;
  int arrayStart = payload.indexOf("[", versionsPos);
  int arrayEnd = payload.indexOf("]", arrayStart);
  if (arrayStart < 0 || arrayEnd <= arrayStart) return;

  String arr = payload.substring(arrayStart + 1, arrayEnd);
  int pos = 0;
  while (true) {
    int objStart = arr.indexOf("{", pos);
    if (objStart < 0) break;
    int objEnd = arr.indexOf("}", objStart);
    if (objEnd < 0) break;
    String one = arr.substring(objStart, objEnd + 1);
    String ver = extractJsonValue(one, "version");
    String url = extractJsonValue(one, "url");
    if (url.length() == 0) url = extractJsonValue(one, "bin");
    addOtaVersion(ver, url);
    pos = objEnd + 1;
  }

  otaAvailableVersions = "";
  for (int i = 0; i < otaVersionCount; i++) {
    if (otaAvailableVersions.length()) otaAvailableVersions += " | ";
    otaAvailableVersions += otaVersionLabels[i];
  }
}

String otaButtonsHtml() {
  String html = "";
  if (otaVersionCount <= 0) return html;
  html += "<div style='margin-top:12px;'><strong>" + htmlEscape(currentLang == "NL" ? "Beschikbare versies" : "Versions disponibles") + "</strong></div>";
  html += "<div style='margin-top:10px; display:flex; gap:10px; flex-wrap:wrap;'>";
  for (int i = 0; i < otaVersionCount; i++) {
    html += "<a href='/startota?slot=" + String(i) + "'><button type='button'>" + htmlEscape(otaVersionLabels[i]) + "</button></a>";
  }
  html += "</div>";
  return html;
}

bool fetchManifest(String &version, String &bin, String &notes) {
  if (!wifiStaConnected()) {
    otaLastStatus = "Pas de connexion WiFi Internet";
    saveConfig();
    return false;
  }
  if (otaManifestUrl.length() == 0) {
    otaLastStatus = "URL manifeste vide";
    saveConfig();
    return false;
  }

  clearOtaVersions();

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String manifestUrl = normalizeGithubRawUrl(otaManifestUrl);
  if (!http.begin(client, manifestUrl)) {
    otaLastStatus = "Ouverture manifeste impossible";
    saveConfig();
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    otaLastStatus = "Erreur HTTP manifeste: " + String(code);
    http.end();
    saveConfig();
    return false;
  }

  String payload = http.getString();
  http.end();

  version = "";
  bin = "";
  notes = "";

  int latestPos = payload.indexOf("\"latest\"");
  if (latestPos >= 0) {
    int latestEnd = payload.indexOf("}", latestPos);
    if (latestEnd > latestPos) {
      String latestBlock = payload.substring(latestPos, latestEnd + 1);
      version = extractJsonValue(latestBlock, "version");
      bin = extractJsonValue(latestBlock, "url");
      if (bin.length() == 0) bin = extractJsonValue(latestBlock, "bin");
      notes = extractJsonValue(latestBlock, "notes");
    }
  }

  parseManifestVersions(payload);

  if (version.length() == 0 && otaVersionCount > 0) version = otaVersionLabels[0];
  if (bin.length() == 0 && otaVersionCount > 0) bin = otaVersionUrls[0];

  bin = normalizeGithubRawUrl(bin);

  if (version.length() == 0 || bin.length() == 0) {
    otaLastStatus = "Manifeste invalide";
    saveConfig();
    return false;
  }

  otaLastVersion = version;
  if (otaAvailableVersions.length() == 0) otaAvailableVersions = version;
  otaLastStatus = "Derniere version trouvee: " + version;
  saveConfig();
  return true;
}

bool performOtaFromUrl(const String& url, String &resultMessage) {
  if (!wifiStaConnected()) {
    resultMessage = "Pas de connexion WiFi Internet";
    return false;
  }
  if (url.length() == 0) {
    resultMessage = "URL du firmware vide";
    return false;
  }

  String finalUrl = normalizeGithubRawUrl(url);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, finalUrl)) {
    resultMessage = "Ouverture firmware impossible";
    return false;
  }

  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    resultMessage = "Erreur HTTP firmware: " + String(httpCode);
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  bool canBegin = Update.begin(contentLength > 0 ? (size_t)contentLength : UPDATE_SIZE_UNKNOWN);
  if (!canBegin) {
    resultMessage = "Update.begin a echoue";
    http.end();
    return false;
  }

  WiFiClient * stream = http.getStreamPtr();
  size_t written = Update.writeStream(*stream);

  if (contentLength > 0 && written != (size_t)contentLength) {
    resultMessage = "Firmware incomplet";
    Update.abort();
    http.end();
    return false;
  }

  if (!Update.end()) {
    resultMessage = "Echec finalisation OTA";
    http.end();
    return false;
  }

  if (!Update.isFinished()) {
    resultMessage = "OTA non terminee";
    http.end();
    return false;
  }

  http.end();
  resultMessage = "Mise a jour reussie. Redemarrage...";
  return true;
}

void drawHeader() {
  if (!displayOk) return;
  display.fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, SSD1306_BLACK);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (currentLang == "NL") {
    display.setCursor(X_HEURE, 1);
    display.print("Uur");
    display.setCursor(X_DEST, 1);
    display.print("Richting");
    display.setCursor(94, 1);
    display.print("Spoor");
  } else {
    display.setCursor(X_HEURE, 1);
    display.print("Heure");
    display.setCursor(X_DEST, 1);
    display.print("Destination");
    display.setCursor(X_V, 1);
    display.print("V");
  }

  display.drawLine(0, SEPARATOR_Y, SCREEN_WIDTH - 1, SEPARATOR_Y, SSD1306_WHITE);
}

void drawBandBackgrounds() {
  if (!displayOk) return;
  for (int row = 0; row < VISIBLE_ROWS; row++) {
    int y = LIST_TOP_Y + row * ROW_HEIGHT;
    int h = ROW_HEIGHT;

    if (y + h > FOOTER_Y) h = FOOTER_Y - y;
    if (h <= 0) continue;

    bool whiteBand = (row % 2 == 0);
    uint16_t bg = whiteBand ? SSD1306_WHITE : SSD1306_BLACK;
    display.fillRect(0, y, SCREEN_WIDTH, h, bg);
  }
}

void drawFooterNormal() {
  if (!displayOk) return;
  display.fillRect(0, FOOTER_Y, SCREEN_WIDTH, FOOTER_HEIGHT, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);

  String txt = currentLang == "NL" ? "Vertrekbord" : "Tableau des departs";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - (int)w) / 2;
  int y = FOOTER_Y + 2;
  display.setCursor(x, y);
  display.print(txt);
}

void drawFooterInfo() {
  if (!displayOk) return;
  unsigned long now = millis();
  if (now - lastFooterBlinkMs >= FOOTER_BLINK_MS) {
    lastFooterBlinkMs = now;
    footerBlinkState = !footerBlinkState;
  }

  if (footerBlinkState) {
    display.fillRect(0, FOOTER_Y, SCREEN_WIDTH, FOOTER_HEIGHT, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else {
    display.fillRect(0, FOOTER_Y, SCREEN_WIDTH, FOOTER_HEIGHT, SSD1306_BLACK);
    display.setTextColor(SSD1306_WHITE);
  }

  display.setTextSize(1);
  String txt = "INFO";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - (int)w) / 2;
  int y = FOOTER_Y + 2;
  display.setCursor(x, y);
  display.print(txt);
}

void drawTrainTextAtY(int y, const TrainItem& t, bool onWhiteBand) {
  if (!displayOk) return;
  if (y < LIST_TOP_Y || y + ROW_HEIGHT > FOOTER_Y) return;

  display.setTextSize(1);
  display.setTextColor(onWhiteBand ? SSD1306_BLACK : SSD1306_WHITE);

  String heure = fitText(t.heure, 5);
  String destination = fitText(t.destination, currentLang == "NL" ? 10 : 11);
  String voie = fitText(t.voie, 2);

  display.setCursor(X_HEURE, y + 1);
  display.print(heure);
  display.setCursor(X_DEST, y + 1);
  display.print(destination);
  display.setCursor(X_V, y + 1);
  display.print(voie);
}

void afficherListeFluide() {
  if (!displayOk) return;
  display.clearDisplay();
  drawHeader();
  drawBandBackgrounds();

  int total = countValidTrains();
  if (total > 0) {
    for (int logicalRow = 0; logicalRow < VISIBLE_ROWS + 1; logicalRow++) {
      int idx = wrappedTrainIndex(baseIndex + logicalRow, total);
      int y = LIST_TOP_Y + logicalRow * ROW_HEIGHT - scrollPixelOffset;
      int bandRow = (y - LIST_TOP_Y + (ROW_HEIGHT / 2)) / ROW_HEIGHT;
      bool onWhiteBand = (bandRow % 2 == 0);
      drawTrainTextAtY(y, trains[idx], onWhiteBand);
    }
  }

  display.fillRect(0, 0, SCREEN_WIDTH, LIST_TOP_Y, SSD1306_BLACK);
  drawHeader();
  drawFooterNormal();
  display.display();
}

void drawMessageLineInBand(int bandIndex, const String& txt, bool centered) {
  if (!displayOk) return;
  int y = LIST_TOP_Y + bandIndex * ROW_HEIGHT;
  bool whiteBand = (bandIndex % 2 == 0);

  display.setTextSize(1);
  display.setTextColor(whiteBand ? SSD1306_BLACK : SSD1306_WHITE);

  String line = fitText(txt, 21);
  int x = 2;
  if (centered) {
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(line, 0, 0, &x1, &y1, &w, &h);
    x = (SCREEN_WIDTH - (int)w) / 2;
    if (x < 2) x = 2;
  }

  display.setCursor(x, y + 1);
  display.print(line);
}

void afficherMessageLibre() {
  if (!displayOk) return;
  display.clearDisplay();
  drawHeader();

  display.fillRect(0, LIST_TOP_Y, SCREEN_WIDTH, ROW_HEIGHT, SSD1306_WHITE);
  display.fillRect(0, LIST_TOP_Y + ROW_HEIGHT, SCREEN_WIDTH, ROW_HEIGHT, SSD1306_BLACK);
  display.fillRect(0, LIST_TOP_Y + (ROW_HEIGHT * 2), SCREEN_WIDTH, ROW_HEIGHT, SSD1306_WHITE);

  drawMessageLineInBand(0, getMsgLine(0), getMsgCenter(0));
  drawMessageLineInBand(1, getMsgLine(1), getMsgCenter(1));
  drawMessageLineInBand(2, getMsgLine(2), getMsgCenter(2));

  drawFooterInfo();
  display.display();
}


void afficherEcranDemarrage() {
  if (!displayOk) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(20, 2);
  display.print("PM3D HORAIRES");

  display.setCursor(0, 16);
  display.print("WiFi : ");
  display.print(AP_SSID);

  display.setCursor(0, 28);
  display.print("IP : ");
  display.print(currentApIpString());

  display.setCursor(0, 40);
  display.print("SDA:");
  display.print(oledSdaPin);
  display.print(" SCL:");
  display.print(oledSclPin);

  display.setCursor(0, 52);
  display.print("FW ");
  display.print(installedVersionLabel);
  display.display();
}

void afficherNomEtAdresseAuDemarrage() {
  if (!displayOk) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  String nom = currentScreenDisplayName();
  String ip = currentApIpString();

  display.setCursor(0, 8);
  display.print(nom);

  display.setCursor(0, 28);
  display.print("IP : ");
  display.print(ip);

  display.setCursor(0, 48);
  display.print("Connexion WiFi");
  display.display();
}



String makeStyleBlock() {
  String html = "<style>";
  bool isPm3d = (theme.preset == "pm3d");
  html += "body{font-family:Arial,Helvetica,sans-serif;background:linear-gradient(180deg," + theme.bodyBg1 + " 0%," + theme.bodyBg2 + " 100%);color:" + theme.text + ";margin:0;padding:18px;}";
  if (isPm3d) html += "body{background-attachment:fixed;}";
  html += ".container{max-width:1080px;margin:0 auto;}";
  html += ".panel{background:linear-gradient(180deg," + theme.panelBg1 + " 0%," + theme.panelBg2 + " 100%);border:1px solid " + theme.accent + ";border-radius:14px;padding:16px;margin-bottom:16px;box-shadow:0 0 0 1px rgba(255,255,255,0.03) inset;}";
  if (isPm3d) html += ".panel{border-color:#7DD3FF;box-shadow:0 0 0 1px rgba(255,255,255,0.06) inset,0 10px 24px rgba(0,0,0,0.35),0 0 14px rgba(125,211,255,0.18);position:relative;overflow:hidden;}.panel:before{content:'';position:absolute;left:0;right:0;top:0;height:1px;background:linear-gradient(90deg,rgba(255,255,255,0.02),rgba(125,211,255,0.70),rgba(255,255,255,0.02));}";
  html += ".topbar{display:flex;justify-content:space-between;align-items:center;gap:12px;flex-wrap:wrap;}";
  html += ".brand{font-size:22px;font-weight:700;letter-spacing:0.5px;}";
  if (isPm3d) html += ".brand{letter-spacing:1.2px;text-transform:uppercase;text-shadow:0 0 10px rgba(125,211,255,0.28);}";
  html += ".sub{color:" + theme.muted + ";font-size:13px;margin-top:6px;}";
  html += ".badge{background:" + theme.accent + ";color:" + theme.accentText + ";border-radius:999px;padding:8px 14px;font-weight:700;display:inline-block;}";
  if (isPm3d) html += ".badge{background:linear-gradient(180deg,#9BE7FF 0%,#49C2FF 48%,#0A67B8 100%);color:#03101C;border:1px solid #AEEBFF;box-shadow:inset 0 1px 0 rgba(255,255,255,0.60),0 0 10px rgba(73,194,255,0.18);}";
  html += ".langbox{display:flex;gap:10px;flex-wrap:wrap;}";
  html += ".langbtn, button{padding:12px 18px;border:none;border-radius:12px;background:" + theme.accent + ";color:" + theme.accentText + ";font-weight:700;cursor:pointer;text-decoration:none;display:inline-block;}";
  if (isPm3d) html += ".langbtn, button{background:linear-gradient(180deg,#6FD9FF 0%,#33B8FF 42%,#0A74C8 51%,#07589C 100%);color:#03101C;border:1px solid #AEEBFF;box-shadow:inset 0 1px 0 rgba(255,255,255,0.70),inset 0 -10px 18px rgba(0,0,0,0.18),0 0 0 1px rgba(125,211,255,0.20),0 8px 18px rgba(0,0,0,0.30),0 0 12px rgba(125,211,255,0.12);text-shadow:0 1px 0 rgba(255,255,255,0.20);}button:hover,.langbtn:hover{filter:brightness(1.06);}button:active,.langbtn:active{transform:translateY(1px);box-shadow:inset 0 1px 0 rgba(255,255,255,0.50),inset 0 -6px 10px rgba(0,0,0,0.24),0 4px 10px rgba(0,0,0,0.26),0 0 10px rgba(125,211,255,0.18);}";
  html += ".langbtn.secondary,.btn-secondary{background:" + theme.panelBg1 + ";color:" + theme.text + ";border:1px solid " + theme.accent + ";}";
  if (isPm3d) html += ".langbtn.secondary,.btn-secondary{background:linear-gradient(180deg,#132E52 0%,#09172A 100%);color:#EAF8FF;border:1px solid #7DD3FF;box-shadow:inset 0 1px 0 rgba(255,255,255,0.08),0 0 10px rgba(125,211,255,0.10);}";
  html += ".info{color:" + theme.info + ";font-weight:700;margin-top:10px;}";
  html += ".warn{color:" + theme.warn + ";font-weight:700;margin-top:10px;}";
  html += ".grid{display:grid;grid-template-columns:120px 1fr 90px;gap:10px;}";
  html += ".grid2{display:grid;grid-template-columns:1fr 1fr;gap:12px;}";
  html += ".theme-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px;}";
  html += "label{display:block;font-size:13px;color:" + theme.muted + ";margin-bottom:4px;}";
  html += "input,select{width:100%;box-sizing:border-box;padding:11px 10px;border-radius:10px;border:1px solid " + theme.accent + ";background:" + theme.inputBg + ";color:" + theme.text + ";}";
  if (isPm3d) html += "input,select{border-color:#7DD3FF;box-shadow:inset 0 1px 0 rgba(255,255,255,0.05),0 0 10px rgba(125,211,255,0.06);background:linear-gradient(180deg,#061120 0%,#020A14 100%);}input:focus,select:focus{outline:none;box-shadow:0 0 0 2px rgba(125,211,255,0.18),0 0 16px rgba(125,211,255,0.12);}";
  html += ".preview{background:" + theme.inputBg + ";border:1px solid " + theme.accent + ";border-radius:12px;padding:10px;}";
  if (isPm3d) html += ".preview{border-color:#7DD3FF;box-shadow:inset 0 0 20px rgba(73,194,255,0.05),0 0 12px rgba(125,211,255,0.08);}";
  html += ".previewhead{display:flex;justify-content:space-between;font-weight:700;margin-bottom:8px;}";
  html += ".previewline{height:1px;background:" + theme.text + ";margin-bottom:6px;opacity:0.8;}";
  html += ".bandw{background:" + theme.accent + ";color:" + theme.accentText + ";padding:6px 8px;margin-bottom:4px;font-weight:700;}";
  if (isPm3d) html += ".bandw{background:linear-gradient(180deg,#7FE0FF 0%,#49C2FF 50%,#0A67B8 100%);color:#03101C;border:1px solid #AEEBFF;box-shadow:inset 0 1px 0 rgba(255,255,255,0.55),0 0 10px rgba(73,194,255,0.10);}";
  html += ".bandb{background:" + theme.panelBg1 + ";color:" + theme.text + ";border:1px solid " + theme.accent + ";padding:6px 8px;margin-bottom:4px;font-weight:700;}";
  if (isPm3d) html += ".bandb{border-color:#7DD3FF;background:linear-gradient(180deg,#112744 0%,#07101D 100%);}";
  html += "hr{border:none;border-top:1px solid " + theme.accent + ";margin:14px 0;}";
  if (isPm3d) html += "hr{border-top-color:#7DD3FF;box-shadow:0 0 8px rgba(125,211,255,0.10);}";
  html += ".small{color:" + theme.muted + ";font-size:13px;}";
  html += ".banner{font-size:12px;color:" + theme.info + ";margin-bottom:8px;}";
  html += "a{color:" + theme.muted + ";}";
  html += ".toolbar{display:flex;gap:10px;align-items:center;flex-wrap:wrap;}";
  html += "</style>";
  return html;
}


String makeLanguagePage() {
  String html = "<!DOCTYPE html><html lang='fr'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>PM3D</title>";
  html += makeStyleBlock();
  html += "</head><body><div class='container'>";
  html += "<div class='panel'>";
  html += "<div class='banner'>Produit par PM3D - visitez pm3d.net<br>Gemaakt door PM3D - bezoek pm3d.net</div>";
  html += "<div class='topbar'><div><div class='brand'>PM3D</div><div class='sub'>Choix de la langue / Taalkeuze</div></div><div class='badge'>FR / NL</div></div>";
  html += "<hr><h2 style='margin:0 0 12px 0;'>Choisissez votre langue</h2>";
  html += "<h2 style='margin:0 0 18px 0;'>Kies uw taal</h2>";
  html += "<div class='langbox'>";
  html += "<a href='/setlang?lang=FR'><button class='langbtn'>Français</button></a>";
  html += "<a href='/setlang?lang=NL'><button class='langbtn secondary'>Nederlands</button></a>";
  html += "</div></div></div></body></html>";
  return html;
}


String firmwareUpdateButtonHtml() {
  String html;
  html += "<div class='panel' style='text-align:center;'>";
  html += "<strong>" + htmlEscape(currentLang == "NL" ? "Onderhoud" : "Maintenance") + "</strong><hr>";
  html += "<div style='margin-top:12px;'><a href='/update'><button type='button'>" + htmlEscape(currentLang == "NL" ? "Firmware-update" : "Mise a jour firmware") + "</button></a></div>";
  html += "</div>";
  return html;
}

String firmwareUpdatePageHtml(const String& message) {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += makeStyleBlock();
  html += "</head><body><div class='container'>";

  html += "<div class='topbar'><div><div class='brand'>PM3D - " + htmlEscape(currentLang == "NL" ? "Firmware-update" : "Mise a jour firmware") + "</div></div>";
  html += "<div style='display:flex; gap:10px; align-items:center; flex-wrap:wrap;'><div class='badge'>" + htmlEscape(buildVersionLabel()) + "</div><a href='/main'><button type='button'>" + htmlEscape(currentLang == "NL" ? "Terug" : "Retour") + "</button></a></div></div>";
  if (message.length() > 0) html += "<div class='info'>" + htmlEscape(message) + "</div>";

  html += "<div class='small'>" + htmlEscape(currentLang == "NL" ? "Verbind hier met een internet-wifi om later firmware-updates te kunnen uitvoeren." : "Connectez-vous ici a un Wi-Fi Internet pour pouvoir effectuer les futures mises a jour.") + "</div>";

  html += "<div class='panel'><strong>" + htmlEscape(currentLang == "NL" ? "Automatische update" : "Mise a jour automatique") + "</strong><hr>";
  html += "<div class='small'>" + htmlEscape(currentLang == "NL" ? "Huidige versie" : "Version actuelle") + " : <b>" + htmlEscape(buildVersionLabel()) + "</b></div>";
  html += "<div class='small'>" + htmlEscape(currentLang == "NL" ? "Dernier firmware detecte" : "Derniere version detectee") + " : <b>" + htmlEscape(otaLastVersion.length() ? otaLastVersion : String("-")) + "</b></div>";
  html += "<div class='small'>" + htmlEscape(currentLang == "NL" ? "Beschikbare versies" : "Versions disponibles") + " : <b>" + htmlEscape(otaAvailableVersions.length() ? otaAvailableVersions : String(currentLang == "NL" ? "onbekend" : "inconnues")) + "</b></div>";
  html += "<div class='small'>" + htmlEscape(currentLang == "NL" ? "Resultaat" : "Resultat") + " : " + htmlEscape(otaLastStatus) + "</div>";
  html += "<div style='margin-top:12px; display:flex; gap:10px; flex-wrap:wrap;'><a href='/otacheck'><button type='button'>" + htmlEscape(currentLang == "NL" ? "Zoek naar update" : "Rechercher une mise a jour") + "</button></a></div>";
  html += otaButtonsHtml();
  html += "</div>";

  html += "<div class='panel'><strong>" + htmlEscape(currentLang == "NL" ? "Internet-wifi voor firmware-update" : "Wi-Fi Internet pour mise a jour") + "</strong><hr>";
  html += "<div class='small'>" + htmlEscape(currentLang == "NL" ? "Opgeslagen netwerk" : "Reseau enregistre") + " : <b>" + (otaSSID.length() ? htmlEscape(otaSSID) : String(currentLang == "NL" ? "geen" : "aucun")) + "</b></div>";
  html += "<div class='small'>" + htmlEscape(currentLang == "NL" ? "Status" : "Etat") + " : " + htmlEscape(otaWifiStatus) + "</div>";
  html += "<div style='margin-top:12px; display:flex; gap:10px; flex-wrap:wrap;'><a href='/otascan'><button type='button'>" + htmlEscape(currentLang == "NL" ? "Wifi zoeken" : "Rechercher un Wi-Fi") + "</button></a><a href='/clearotawifi'><button type='button'>" + htmlEscape(currentLang == "NL" ? "Opgeslagen wifi wissen" : "Effacer le Wi-Fi enregistre") + "</button></a><a href='/reboot'><button type='button'>" + htmlEscape(currentLang == "NL" ? "Herstarten" : "Redemarrer") + "</button></a></div>";
  html += "</div>";


  html += "</div></body></html>";
  return html;
}

String makeMainPage(const String& message) {
  String html = "<!DOCTYPE html><html lang='fr'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>PM3D</title>";
  html += makeStyleBlock();
  html += "</head><body><div class='container'>";

  html += "<div class='panel'>";
  html += "<div class='banner'>" + htmlEscape(t_banner()) + "</div>";
  html += "<div class='topbar'><div><div class='brand'>PM3D - " + htmlEscape(t_title()) + "</div><div class='sub'>" + htmlEscape(t_small()) + "</div></div>";
  html += "<div class='badge'>" + htmlEscape(currentLang) + "</div></div>";
  if (message.length() > 0) html += "<div class='info'>" + htmlEscape(message) + "</div>";
  if (!displayOk) html += "<div class='warn'>OLED non detecte. Verifie le cablage puis redemarre.</div>";
  html += "<hr>";
  html += "<div class='small'>" + htmlEscape(t_language()) + " : <a href='/setlang?lang=FR'>FR</a> | <a href='/setlang?lang=NL'>NL</a></div>";
  html += "</div>";


  html += "<div class='panel'><div class='topbar'><strong>" + htmlEscape(t_device_title()) + "</strong><a href='/theme'><button type='button'>" + htmlEscape(t_theme()) + "</button></a></div><hr>";
  html += "<form method='POST' action='/savedevice'>";
  html += "<div class='grid2'>";
  html += "<div><label>" + htmlEscape(t_device_name()) + "</label><input name='scrname' maxlength='24' placeholder='" + htmlEscape(currentLang == "NL" ? "Perron 1" : "Quai 1") + "' value='" + htmlEscape(screenCustomName) + "'></div>";
  html += "<div><label>" + htmlEscape(t_address()) + "</label><div style='display:flex;align-items:center;gap:8px;'><span class='small' style='min-width:74px;color:#ffd6a3;'>192.168.4.</span><input name='ipsuffix' type='number' min='2' max='254' value='" + String(apIpSuffix) + "'></div></div>";
  html += "</div>";
  html += "<div class='small' style='margin-top:10px;'>" + htmlEscape(currentLang == "NL" ? "Het scherm toont deze naam en dit adres opnieuw bij het opstarten." : "L’écran réaffichera ce nom et cette adresse au redémarrage.") + "</div>";
  html += "<hr>";
  html += "<div class='small'>WiFi local : " + htmlEscape(String(AP_SSID)) + "</div>";
  html += "<div class='small'>" + htmlEscape(t_password()) + " : " + htmlEscape(String(AP_PASSWORD)) + "</div>";
  html += "<div class='small'>" + htmlEscape(t_address()) + " : http://" + currentApIpString() + "</div>";
  html += "<div style='margin-top:12px;'><button type='submit'>" + htmlEscape(currentLang == "NL" ? "Opslaan en herstarten" : "Enregistrer et redemarrer") + "</button></div></form></div>";

  html += "<div class='panel'><strong>" + htmlEscape(t_msg_title()) + "</strong><hr>";
  html += "<form method='POST' action='/savemsg'>";
  for (int i = 0; i < 3; i++) {
    String lineValue = htmlEscape(getMsgLine(i));
    bool centered = getMsgCenter(i);
    html += "<div style='margin-bottom:12px;'>";
    html += "<label>" + htmlEscape(t_line_label(i + 1)) + " (" + htmlEscape(currentLang) + ")</label>";
    html += "<input name='msg" + String(i + 1) + "' maxlength='21' value='" + lineValue + "'>";
    html += "<label style='margin-top:6px; display:block;'><input type='checkbox' name='c" + String(i + 1) + "'";
    if (centered) html += " checked";
    html += "> " + htmlEscape(t_center()) + "</label></div>";
  }
  html += "<div class='grid'>";
  html += "<div><label>" + htmlEscape(t_msg_every()) + "</label><input name='every' type='number' min='30' max='600' value='" + String(customMessageEverySec) + "'></div>";
  html += "<div><label>" + htmlEscape(t_duration()) + "</label><input name='duration' type='number' min='1' max='60' value='" + String(customMessageShowSec) + "'></div>";
  html += "<div></div></div>";
  html += "<div class='small' style='margin-top:10px;'>" + htmlEscape(t_msg_every()) + " " + String(customMessageEverySec) + " " + htmlEscape(t_seconds()) + " - " + htmlEscape(t_duration()) + " : " + String(customMessageShowSec) + " " + htmlEscape(t_seconds()) + "</div>";
  html += "<div style='margin-top:12px;'><button type='submit'>" + htmlEscape(currentLang == "NL" ? "Opslaan en herstarten" : "Enregistrer et redemarrer") + "</button></div></form></div>";

  html += "<form method='POST' action='/save'>";
  for (int i = 0; i < MAX_TRAINS; i++) {
    html += "<div class='panel'><strong>" + htmlEscape(t_train() + String(i + 1)) + "</strong><hr><div class='grid'>";
    html += "<div><label>" + htmlEscape(t_hour()) + "</label><input name='h" + String(i) + "' maxlength='5' placeholder='08:12' value='" + htmlEscape(trains[i].heure) + "'></div>";
    html += "<div><label>" + htmlEscape(t_dest()) + "</label><input name='d" + String(i) + "' maxlength='18' placeholder='" + htmlEscape(currentLang == "NL" ? "Brussel" : "Bruxelles") + "' value='" + htmlEscape(trains[i].destination) + "'></div>";
    html += "<div><label>" + htmlEscape(t_track_label()) + "</label><input name='v" + String(i) + "' maxlength='2' placeholder='1' value='" + htmlEscape(trains[i].voie) + "'></div>";
    html += "</div></div>";
  }
  html += "<div class='panel'><button type='submit'>" + htmlEscape(t_save()) + "</button></div></form>";



  html += firmwareUpdateButtonHtml();

  html += "</div></body></html>";
  return html;
}


String makeThemePage(const String& message) {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += makeStyleBlock();
  html += "</head><body><div class='container'>";

  html += "<div class='panel'><div class='topbar'><div><div class='brand'>PM3D - " + htmlEscape(t_theme_title()) + "</div>";
  html += "<div class='sub'>" + htmlEscape(t_rgb_help()) + "</div></div>";
  html += "<div class='toolbar'><div class='badge'>" + htmlEscape(theme.preset) + "</div><a href='/main'><button type='button'>" + htmlEscape(t_back()) + "</button></a></div></div>";
  if (message.length() > 0) html += "<div class='info'>" + htmlEscape(message) + "</div>";
  html += "</div>";

  html += "<div class='panel'><strong>" + htmlEscape(t_choose_theme()) + "</strong><hr>";
  html += "<div class='toolbar'>";
  html += "<a href='/savetheme?preset=vert'><button type='button'>Vert</button></a>";
  html += "<a href='/savetheme?preset=jaune'><button type='button'>Jaune</button></a>";
  html += "<a href='/savetheme?preset=rouge'><button type='button'>Rouge</button></a>";
  html += "<a href='/savetheme?preset=bleu'><button type='button'>Bleu</button></a>";
  html += "<a href='/savetheme?preset=orange'><button type='button'>Orange</button></a>";
  html += "<a href='/savetheme?preset=violet'><button type='button'>Violet</button></a>";
  html += "<a href='/savetheme?preset=rose'><button type='button'>Rose</button></a>";
  html += "<a href='/savetheme?preset=pm3d'><button type='button'>PM3D</button></a>";
  html += "</div></div>";

  html += "<div class='panel'><strong>" + htmlEscape(t_brightness()) + "</strong><hr>";
  html += "<div class='toolbar'>";
  html += "<a href='/brightness?delta=-15'><button type='button' style='font-size:22px;min-width:56px;'>-</button></a>";
  html += "<div class='badge'>" + String(screenBrightness) + " / 255</div>";
  html += "<a href='/brightness?delta=15'><button type='button' style='font-size:22px;min-width:56px;'>+</button></a>";
  html += "</div>";
  html += "<div class='small' style='margin-top:10px;'>0 = min, 255 = max</div>";
  html += "</div>";

  html += "<div class='panel'><strong>" + htmlEscape(t_custom_colors()) + "</strong><hr>";
  html += "<form method='POST' action='/savetheme'>";
  html += "<div class='theme-grid'>";
  html += "<div><label>" + htmlEscape(t_body_bg1()) + "</label><input name='bodyBg1' value='" + htmlEscape(theme.bodyBg1) + "'></div>";
  html += "<div><label>" + htmlEscape(t_body_bg2()) + "</label><input name='bodyBg2' value='" + htmlEscape(theme.bodyBg2) + "'></div>";
  html += "<div><label>" + htmlEscape(t_panel_bg1()) + "</label><input name='panelBg1' value='" + htmlEscape(theme.panelBg1) + "'></div>";
  html += "<div><label>" + htmlEscape(t_panel_bg2()) + "</label><input name='panelBg2' value='" + htmlEscape(theme.panelBg2) + "'></div>";
  html += "<div><label>" + htmlEscape(t_accent()) + "</label><input name='accent' value='" + htmlEscape(theme.accent) + "'></div>";
  html += "<div><label>" + htmlEscape(t_accent_text()) + "</label><input name='accentText' value='" + htmlEscape(theme.accentText) + "'></div>";
  html += "<div><label>" + htmlEscape(t_text_color()) + "</label><input name='text' value='" + htmlEscape(theme.text) + "'></div>";
  html += "<div><label>" + htmlEscape(t_muted_color()) + "</label><input name='muted' value='" + htmlEscape(theme.muted) + "'></div>";
  html += "<div><label>" + htmlEscape(t_info_color()) + "</label><input name='info' value='" + htmlEscape(theme.info) + "'></div>";
  html += "<div><label>" + htmlEscape(t_warn_color()) + "</label><input name='warn' value='" + htmlEscape(theme.warn) + "'></div>";
  html += "<div><label>" + htmlEscape(t_input_bg()) + "</label><input name='inputBg' value='" + htmlEscape(theme.inputBg) + "'></div>";
  html += "</div>";
  html += "<input type='hidden' name='preset' value='" + htmlEscape(theme.preset) + "'>";
  html += "<div style='margin-top:12px;' class='toolbar'><button type='submit'>" + htmlEscape(t_apply()) + "</button><a href='/main'><button type='button'>" + htmlEscape(t_back()) + "</button></a></div>";
  html += "</form></div>";

  html += "<div class='panel'><strong>" + htmlEscape(t_preview()) + "</strong><hr>";
  html += "<div class='preview'><div class='previewhead'><span>12:04</span><span>" + htmlEscape(currentLang == "NL" ? "Spoor 1" : "Voie 1") + "</span></div><div class='previewline'></div>";
  html += "<div class='bandw'>" + htmlEscape(currentLang == "NL" ? "Brussel" : "Bruxelles") + "</div>";
  html += "<div class='bandb'>PM3D.NET</div>";
  html += "<div class='small'>" + htmlEscape(t_rgb_help()) + "</div>";
  html += "</div></div>";

  html += "</div></body></html>";
  return html;
}

void handleRoot() { server.send(200, "text/html; charset=utf-8", makeLanguagePage()); }

void handleUpdatePage() { server.send(200, "text/html; charset=utf-8", firmwareUpdatePageHtml("")); }

void handleThemePage() { server.send(200, "text/html; charset=utf-8", makeThemePage("")); }

void handleBrightness() {
  int delta = getParamSafe("delta").toInt();
  screenBrightness += delta;
  if (screenBrightness < 0) screenBrightness = 0;
  if (screenBrightness > 255) screenBrightness = 255;
  applyScreenBrightness();
  saveConfig();
  server.send(200, "text/html; charset=utf-8", makeThemePage(t_brightness_saved()));
}

void handleSaveTheme() {
  String preset = getParamSafe("preset");
  if (preset.length() > 0) applyPresetTheme(preset);

  theme.bodyBg1 = sanitizeHexColor(getParamSafe("bodyBg1"), theme.bodyBg1);
  theme.bodyBg2 = sanitizeHexColor(getParamSafe("bodyBg2"), theme.bodyBg2);
  theme.panelBg1 = sanitizeHexColor(getParamSafe("panelBg1"), theme.panelBg1);
  theme.panelBg2 = sanitizeHexColor(getParamSafe("panelBg2"), theme.panelBg2);
  theme.accent = sanitizeHexColor(getParamSafe("accent"), theme.accent);
  theme.accentText = sanitizeHexColor(getParamSafe("accentText"), theme.accentText);
  theme.text = sanitizeHexColor(getParamSafe("text"), theme.text);
  theme.muted = sanitizeHexColor(getParamSafe("muted"), theme.muted);
  theme.info = sanitizeHexColor(getParamSafe("info"), theme.info);
  theme.warn = sanitizeHexColor(getParamSafe("warn"), theme.warn);
  theme.inputBg = sanitizeHexColor(getParamSafe("inputBg"), theme.inputBg);

  saveConfig();
  server.send(200, "text/html; charset=utf-8", makeThemePage(t_theme_saved()));
}

void handleSetLang() {
  String lang = getParamSafe("lang");
  lang.toUpperCase();
  if (lang == "FR" || lang == "NL") {
    currentLang = lang;
    translateKnownDestinationsOnly();
    if (!customMessageEditedFR) {
      msgLine1FR = "PM3D.NET"; msgLine2FR = "vous souhaite"; msgLine3FR = "un bon voyage !";
      msgLine1CenterFR = true; msgLine2CenterFR = true; msgLine3CenterFR = true;
    }
    if (!customMessageEditedNL) {
      msgLine1NL = "PM3D.NET"; msgLine2NL = "wenst u"; msgLine3NL = "een goede reis !";
      msgLine1CenterNL = true; msgLine2CenterNL = true; msgLine3CenterNL = true;
    }
    saveConfig();
    resetAnimation();
    afficherListeFluide();
  }
  server.send(200, "text/html; charset=utf-8", makeMainPage(""));
}

void handleMain() { server.send(200, "text/html; charset=utf-8", makeMainPage("")); }

void handleSaveMsg() {
  String line1 = nettoyerTexte(getParamSafe("msg1"), 21);
  String line2 = nettoyerTexte(getParamSafe("msg2"), 21);
  String line3 = nettoyerTexte(getParamSafe("msg3"), 21);
  unsigned long sec = getParamSafe("every").toInt(); if (sec < 30) sec = 30; if (sec > 600) sec = 600; customMessageEverySec = sec;
  unsigned long durationSec = getParamSafe("duration").toInt(); if (durationSec < 1) durationSec = 1; if (durationSec > 60) durationSec = 60; customMessageShowSec = durationSec;
  bool c1 = server.hasArg("c1"); bool c2 = server.hasArg("c2"); bool c3 = server.hasArg("c3");

  if (currentLang == "NL") {
    msgLine1NL = line1; msgLine2NL = line2; msgLine3NL = line3;
    msgLine1CenterNL = c1; msgLine2CenterNL = c2; msgLine3CenterNL = c3; customMessageEditedNL = true;
  } else {
    msgLine1FR = line1; msgLine2FR = line2; msgLine3FR = line3;
    msgLine1CenterFR = c1; msgLine2CenterFR = c2; msgLine3CenterFR = c3; customMessageEditedFR = true;
  }
  saveConfig();
  resetAnimation();
  server.send(200, "text/html; charset=utf-8", makeMainPage(t_saved()));
}

void handleSave() {
  for (int i = 0; i < MAX_TRAINS; i++) {
    trains[i].heure = nettoyerTexte(getParamSafe("h" + String(i)), 5);
    trains[i].destination = nettoyerTexte(getParamSafe("d" + String(i)), 18);
    trains[i].voie = nettoyerTexte(getParamSafe("v" + String(i)), 2);
  }
  compacterTrains();
  saveConfig();
  resetAnimation();
  afficherListeFluide();
  server.send(200, "text/html; charset=utf-8", makeMainPage(t_saved()));
}



void handleSaveDevice() {
  screenCustomName = nettoyerTexte(getParamSafe("scrname"), 24);
  int suffix = getParamSafe("ipsuffix").toInt();
  if (suffix < 2) suffix = 2;
  if (suffix > 254) suffix = 254;
  apIpSuffix = suffix;
  saveConfig();
  server.send(200, "text/html; charset=utf-8", makeMainPage(t_saved_device() + " - " + (currentLang == "NL" ? "Herstart..." : "Redemarrage...")));
  delay(1500);
  ESP.restart();
}

void handleSaveOta() {
  otaManifestUrl = nettoyerTexte(getParamSafe("manifest"), 220);
  otaBinUrl = normalizeGithubRawUrl(nettoyerTexte(getParamSafe("binurl"), 220));
  saveConfig();
  ensureOtaWifiConnection(true);
  saveConfig();
  server.send(200, "text/html; charset=utf-8", firmwareUpdatePageHtml("Parametres OTA enregistres."));
}


void handleOtaScan() {
  WiFi.mode(WIFI_AP_STA);
  int n = WiFi.scanNetworks(false, true);
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += makeStyleBlock();
  html += "</head><body><div class='container'><div class='panel'><div class='brand'>Choix du Wi-Fi Internet</div><hr>";
  if (n <= 0) html += "<div class='small'>Aucun reseau detecte</div>";
  else {
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;
      html += "<div style='margin:8px 0'><b>" + htmlEscape(ssid) + "</b> &nbsp; <a href='/selectotawifi?ssid=" + urlEncode(ssid) + "'>Selectionner</a></div>";
    }
  }
  html += "<div style='margin-top:12px'><a href='/update'><button type='button'>Retour</button></a></div></div></div></body></html>";
  WiFi.scanDelete();
  server.send(200, "text/html; charset=utf-8", html);
}

void handleSelectOtaWifi() {
  String ssid = urlDecode(server.arg("ssid"));
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += makeStyleBlock();
  html += "</head><body><div class='container'><div class='panel'><div class='brand'>Enregistrer le Wi-Fi Internet</div><hr>";
  html += "<div class='small'><b>" + htmlEscape(ssid) + "</b></div>";
  html += "<form action='/saveotawifi' method='get'>";
  html += "<input type='hidden' name='ssid' value='" + htmlEscapeAttr(ssid) + "'>";
  html += "<label style='margin-top:10px'>Mot de passe</label>";
  html += "<input type='text' name='pass' value=''>";
  html += "<div style='margin-top:12px; display:flex; gap:10px; flex-wrap:wrap;'><button type='submit'>Enregistrer et connecter</button><a href='/update'><button type='button'>Annuler</button></a></div>";
  html += "</form></div></div></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleSaveOtaWifi() {
  otaSSID = urlDecode(server.arg("ssid"));
  otaPassword = server.arg("pass");
  saveConfig();
  ensureOtaWifiConnection(true);
  saveConfig();
  server.send(200, "text/html; charset=utf-8", firmwareUpdatePageHtml("Wi-Fi Internet enregistre."));
}

void handleClearOtaWifi() {
  otaSSID = "";
  otaPassword = "";
  otaWifiConnected = false;
  otaWifiStatus = "Wi-Fi Internet efface";
  saveConfig();
  WiFi.disconnect(false, false);
  server.sendHeader("Location", "/update");
  server.send(302, "text/plain", "");
}

void handleCheckOta() {
  ensureOtaWifiConnection(true);
  String version, bin, notes;
  String msg;
  if (otaManifestUrl.length() > 0 && fetchManifest(version, bin, notes)) {
    msg = "Version detectee: " + version;
    if (version == buildVersionLabel()) msg += " (deja installee)";
    otaBinUrl = bin;
    saveConfig();
  } else {
    otaAvailableVersions = "";
    clearOtaVersions();
    otaBinUrl = "";
    saveConfig();
    msg = otaLastStatus;
  }
  server.send(200, "text/html; charset=utf-8", firmwareUpdatePageHtml(msg));
}

void handleStartOta() {
  ensureOtaWifiConnection(true);

  String slotStr = getParamSafe("slot");
  int slot = slotStr.length() ? slotStr.toInt() : -1;

  String version, bin, notes;
  String finalUrl = "";
  String selectedLabel = "";

  if (slot >= 0 && slot < otaVersionCount) {
    finalUrl = otaVersionUrls[slot];
    selectedLabel = otaVersionLabels[slot];
  } else if (otaManifestUrl.length() > 0 && fetchManifest(version, bin, notes)) {
    finalUrl = bin;
    selectedLabel = version;
  }

  finalUrl = normalizeGithubRawUrl(finalUrl);

  if (finalUrl.length() == 0 || selectedLabel.length() == 0) {
    otaLastStatus = "Aucune version OTA valide disponible";
    saveConfig();
    server.send(200, "text/html; charset=utf-8", firmwareUpdatePageHtml(otaLastStatus));
    return;
  }

  markPendingOtaInstall(selectedLabel, finalUrl);

  String result;
  bool ok = performOtaFromUrl(finalUrl, result);
  otaLastStatus = result;
  if (ok) {
    otaLastVersion = selectedLabel;
    saveConfig();
  } else {
    clearPendingOtaState(false);
    saveConfig();
  }

  if (ok) {
    server.send(200, "text/html; charset=utf-8", firmwareUpdatePageHtml(result));
    delay(1500);
    ESP.restart();
    return;
  }

  server.send(200, "text/html; charset=utf-8", firmwareUpdatePageHtml(result));
}

void handleReboot() {
  server.send(200, "text/html; charset=utf-8", firmwareUpdatePageHtml("Redemarrage en cours..."));
  delay(800);
  ESP.restart();
}

void handleNotFound() { server.send(404, "text/plain; charset=utf-8", "Page non trouvee"); }

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Demarrage PM3D Horaires OLED OTA");

  loadConfig();
  bool rollbackPending = isRollbackVerificationPending();
  bootStartMs = millis();

  if (otaInstallInProgress && !rollbackPending) {
    if (otaPendingInstallLabel.length() > 0 && otaPendingInstallLabel != buildVersionLabel()) {
      otaLastStatus = "Mise a jour annulee : retour a la version precedente";
      otaLastVersion = installedVersionLabel;
    }
    clearPendingOtaState(false);
    saveConfig();
  }

  finalizeBootVersionState();

  initializeDisplayWithAutoI2CDetection();
  if (displayOk) {
    saveConfig();
  } else {
    Serial.println("OLED non detecte a l'adresse 0x3C - poursuite du boot sans ecran");
    Serial.print("Pins testes au demarrage - SDA=");
    Serial.print(oledSdaPin);
    Serial.print(" SCL=");
    Serial.println(oledSclPin);
    otaLastStatus = "OLED non detecte - fonctionnement sans ecran";
    saveConfig();
  }

  otaValidationDeferred = rollbackPending;

  WiFi.mode(WIFI_AP_STA);
  IPAddress local_ip(192, 168, 4, apIpSuffix);
  IPAddress gateway(192, 168, 4, apIpSuffix);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_ip, gateway, subnet);

  bool apOk = WiFi.softAP(AP_SSID, AP_PASSWORD, 6, 0, 2);
  if (apOk) {
    esp_wifi_set_max_tx_power(72);
    esp_wifi_set_inactive_time(WIFI_IF_AP, 600);
    Serial.println("Point d'acces demarre");
    Serial.print("SSID : "); Serial.println(AP_SSID);
    Serial.print("IP AP : "); Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Erreur demarrage point d'acces");
  }

  ensureOtaWifiConnection(true);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/main", HTTP_GET, handleMain);
  server.on("/theme", HTTP_GET, handleThemePage);
  server.on("/brightness", HTTP_GET, handleBrightness);
  server.on("/savetheme", HTTP_GET, handleSaveTheme);
  server.on("/savetheme", HTTP_POST, handleSaveTheme);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/setlang", HTTP_GET, handleSetLang);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/savemsg", HTTP_POST, handleSaveMsg);
  server.on("/savedevice", HTTP_POST, handleSaveDevice);
  server.on("/saveota", HTTP_POST, handleSaveOta);
  server.on("/otascan", HTTP_GET, handleOtaScan);
  server.on("/selectotawifi", HTTP_GET, handleSelectOtaWifi);
  server.on("/saveotawifi", HTTP_GET, handleSaveOtaWifi);
  server.on("/clearotawifi", HTTP_GET, handleClearOtaWifi);
  server.on("/otacheck", HTTP_GET, handleCheckOta);
  server.on("/startota", HTTP_GET, handleStartOta);
  server.on("/reboot", HTTP_GET, handleReboot);
  server.onNotFound(handleNotFound);
  server.begin();

  afficherEcranDemarrage();
  delay(2200);
  if (screenCustomName.length() || apIpSuffix != 1) {
    afficherNomEtAdresseAuDemarrage();
    delay(10000);
  }
  resetAnimation();
  saveConfig();
  if (displayOk) afficherListeFluide();
}

void loop() {
  server.handleClient();
  maintainOtaWifiConnection();

  if (otaValidationDeferred && millis() - bootStartMs >= 5000) {
    finalizePendingOtaSuccessIfNeeded();
    otaValidationDeferred = false;
  }

  if (!displayOk) { delay(20); return; }

  unsigned long now = millis();

  if (!showingCustomMessage && customMessageEverySec >= 30) {
    if (now - lastCustomMessageCycle >= customMessageEverySec * 1000UL) {
      showingCustomMessage = true;
      customMessageShowStart = now;
      afficherMessageLibre();
      return;
    }
  }

  if (showingCustomMessage) {
    if (now - customMessageShowStart < customMessageShowSec * 1000UL) {
      afficherMessageLibre();
      return;
    } else {
      showingCustomMessage = false;
      lastCustomMessageCycle = now;
      afficherListeFluide();
    }
  }

  int total = countValidTrains();
  if (total <= 0) {
    afficherListeFluide();
    delay(200);
    return;
  }

  if (total <= VISIBLE_ROWS) {
    static unsigned long lastRefresh = 0;
    if (now - lastRefresh >= 600) {
      lastRefresh = now;
      afficherListeFluide();
    }
    return;
  }

  if (inPause) {
    if (now - pauseStartMs >= PAUSE_MS) {
      inPause = false;
      lastAnimMs = now;
    }
  } else {
    if (now - lastAnimMs >= FRAME_MS) {
      lastAnimMs = now;
      scrollPixelOffset += PIXELS_PER_FRAME;
      if (scrollPixelOffset >= ROW_HEIGHT) {
        scrollPixelOffset = 0;
        baseIndex = wrappedTrainIndex(baseIndex + 1, total);
        inPause = true;
        pauseStartMs = now;
      }
    }
  }

  afficherListeFluide();
}
