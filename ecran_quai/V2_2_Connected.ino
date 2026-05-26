#include <WiFi.h>
#include <esp_now.h>
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
#define FW_BUILD_LABEL "V2.2 Connected"

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

String AP_SSID = "PM3D-Display 0000";
String apPasswordDisplay = "";

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

// =======================================================
// Reception sensors PM3D comme Crossing Gate : ESP-NOW.
// =======================================================
static const uint32_t PM3D_MAGIC = 0x504D3344; // "PM3D"
static const uint8_t PKT_SENSOR = 1;

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint8_t type;
  uint8_t event; // 0=heartbeat, 1=on, 2=off
  uint8_t apMac[6];
  char suffix[5];
  uint32_t seq;
} Pm3dSensorPacket;


const int NB_SENSORS_MAX_DISPLAY = 2;
String macSensorDisplay[NB_SENSORS_MAX_DISPLAY] = {"", ""};
String ssidSensorDisplay[NB_SENSORS_MAX_DISPLAY] = {"", ""};
int sensorTrainChoiceDisplay[NB_SENSORS_MAX_DISPLAY] = {0, 1};
int sensorPositionChoiceDisplay[NB_SENSORS_MAX_DISPLAY] = {0, 1};
String sensorArrowSideDisplay = "right";
bool sensorArrowBlinkDisplay = true;
unsigned long sensorArrowBlinkSecDisplay = 1;
unsigned long sensorReturnNormalSecDisplay = 8;

// Parametres reels par detection S1 / S2.
// Par defaut : S1 fleche a gauche, S2 fleche a droite.
// Mode par defaut : automatique toutes les X secondes pendant X secondes.
// Mode sensor : l'affichage ne part que quand le sensor declenche.
bool sensorDetectionEnabledDisplay[NB_SENSORS_MAX_DISPLAY] = {true, true};
String sensorArrowSideBySlotDisplay[NB_SENSORS_MAX_DISPLAY] = {"left", "right"};
bool sensorArrowBlinkBySlotDisplay[NB_SENSORS_MAX_DISPLAY] = {true, true};
unsigned long sensorArrowBlinkDurationSecBySlotDisplay[NB_SENSORS_MAX_DISPLAY] = {3, 3};
unsigned long sensorReturnNormalSecBySlotDisplay[NB_SENSORS_MAX_DISPLAY] = {8, 8};
String sensorTriggerModeBySlotDisplay[NB_SENSORS_MAX_DISPLAY] = {"auto", "auto"};
unsigned long sensorAutoEverySecBySlotDisplay[NB_SENSORS_MAX_DISPLAY] = {30, 30};
unsigned long sensorAutoShowSecBySlotDisplay[NB_SENSORS_MAX_DISPLAY] = {8, 8};
unsigned long sensorNextAutoTriggerMsDisplay[NB_SENSORS_MAX_DISPLAY] = {0, 0};
int sensorDestinationTextSizeBySlotDisplay[NB_SENSORS_MAX_DISPLAY] = {1, 1};
bool sensorHeaderBlinkBySlotDisplay[NB_SENSORS_MAX_DISPLAY] = {true, true};
unsigned long sensorDetectedUntilMsDisplay[NB_SENSORS_MAX_DISPLAY] = {0, 0};
unsigned long sensorLastSeenMsDisplay[NB_SENSORS_MAX_DISPLAY] = {0, 0};
unsigned long sensorLastEspNowRawMsDisplay = 0;
unsigned long sensorLastEspNowValidMsDisplay = 0;
unsigned long sensorLastEspNowMatchedMsDisplay = 0;
uint32_t sensorEspNowRawCountDisplay = 0;
uint32_t sensorEspNowValidCountDisplay = 0;
uint32_t sensorEspNowMatchedCountDisplay = 0;
unsigned long lastEspNowChannelLockMsDisplay = 0;
const unsigned long SENSOR_CONNECTED_TIMEOUT_MS = 10000UL;
bool sensorKnownPresentDisplay[NB_SENSORS_MAX_DISPLAY] = {false, false};
unsigned long sensorLastWifiScanMsDisplay = 0;
unsigned long sensorLastRealTriggerMsDisplay[NB_SENSORS_MAX_DISPLAY] = {0, 0};
const unsigned long SENSOR_WIFI_SCAN_INTERVAL_MS = 2500UL;
const unsigned long SENSOR_REAL_TRIGGER_COOLDOWN_MS = 12000UL;
bool sensorI2CHeaderVisible = true;
unsigned long sensorI2CLastHeaderBlinkMs = 0;

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

struct ThèmeConfig {
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

ThèmeConfig theme = {
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
const char* defaultDestDE[5] = {"Bruessel", "Antwerpen", "Luettich", "Namur", "Mons"};
const char* defaultDestEN[5] = {"Brussels", "Antwerp", "Liege", "Namur", "Mons"};

String msgLine1FR = "PM3D.NET";
String msgLine2FR = "vous souhaite";
String msgLine3FR = "un bon voyage !";
String msgLine1NL = "PM3D.NET";
String msgLine2NL = "wenst u";
String msgLine3NL = "een goede reis !";
String msgLine1DE = "PM3D.NET";
String msgLine2DE = "wuenscht Ihnen";
String msgLine3DE = "eine gute Reise !";
String msgLine1EN = "PM3D.NET";
String msgLine2EN = "wishes you";
String msgLine3EN = "a pleasant trip !";
bool msgLine1CenterFR = true;
bool msgLine2CenterFR = true;
bool msgLine3CenterFR = true;
bool msgLine1CenterNL = true;
bool msgLine2CenterNL = true;
bool msgLine3CenterNL = true;
bool msgLine1CenterDE = true;
bool msgLine2CenterDE = true;
bool msgLine3CenterDE = true;
bool msgLine1CenterEN = true;
bool msgLine2CenterEN = true;
bool msgLine3CenterEN = true;
bool customMessageEditedFR = false;
bool customMessageEditedNL = false;
bool customMessageEditedDE = false;
bool customMessageEditedEN = false;
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

bool sensorI2CTestActive = false;
int sensorI2CTestSlot = 0;
unsigned long sensorI2CTestStartMs = 0;
bool sensorI2CArrowVisible = true;
unsigned long sensorI2CLastBlinkMs = 0;

String normalizeGithubRawUrl(String url);
void saveConfig();

String sanitizeHexCouleur(String value, const String& fallback);
void applyPresetThème(const String& presetName);
String makeThèmePage(const String& message);
String introPage();

String makeParamètresPage(const String& message);
String advancedWarningModalHtml();
String configMenuPage();
void handleConfigMenu();
void handleSaveSensorPositionDisplay();

String configMenuPage() {
  String html = "<!DOCTYPE html><html lang='fr'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>PM3D</title>";
  html += makeStyleBlock();
  html += "<script>function showAdvancedWarning(){document.getElementById('advModal').style.display='flex';return false;}function hideAdvancedWarning(){document.getElementById('advModal').style.display='none';}</script>";
  html += "</head><body><div class='container'>";
  html += "<div class='panel'><div class='topbar'><div><div class='brand'>PM3D - Configuration</div></div><a href='/main'><button type='button'>" + htmlEscape(t_back()) + "</button></a></div></div>";
  html += "<div class='panel'><div class='toolbar'>";
  html += "<a href='/settings'><button type='button'>" + htmlEscape(t_big_settings()) + "</button></a>";
  html += "<a href='/advanced' onclick='return showAdvancedWarning()'><button type='button' class='btn-secondary danger'>" + htmlEscape(currentLang == "FR" ? "Reglages avances" : (currentLang == "NL" ? "Geavanceerd" : (currentLang == "DE" ? "Erweitert" : "Advanced"))) + "</button></a>";
  html += "<a class='paletteBtn' href='/theme' title='" + htmlEscape(t_theme()) + "'>&#127912;</a>";
  html += "</div></div>";
  html += advancedWarningModalHtml();
  html += "</div></body></html>";
  return html;
}

void handleConfigMenu() { server.send(200, "text/html; charset=utf-8", configMenuPage()); }

void handleSaveSensorPositionDisplay() {
  for (int s = 0; s < NB_SENSORS_MAX_DISPLAY; s++) {
    if (server.hasArg("senTrain" + String(s))) {
      sensorTrainChoiceDisplay[s] = server.arg("senTrain" + String(s)).toInt();
      if (sensorTrainChoiceDisplay[s] < 0) sensorTrainChoiceDisplay[s] = 0;
      if (sensorTrainChoiceDisplay[s] >= MAX_TRAINS) sensorTrainChoiceDisplay[s] = MAX_TRAINS - 1;
    }
  }
  for (int p = 0; p < NB_SENSORS_MAX_DISPLAY; p++) {
    if (server.hasArg("senPos" + String(p))) {
      sensorPositionChoiceDisplay[p] = server.arg("senPos" + String(p)).toInt();
      if (sensorPositionChoiceDisplay[p] < 0) sensorPositionChoiceDisplay[p] = 0;
      if (sensorPositionChoiceDisplay[p] >= NB_SENSORS_MAX_DISPLAY) sensorPositionChoiceDisplay[p] = NB_SENSORS_MAX_DISPLAY - 1;
    }
  }
  saveConfig();
  server.send(200, "text/html; charset=utf-8", makeParamètresPage(t_saved()));
}


String advancedPage();
void handleIntro();
void handleAdvanced();
void handleSaveSensorsDisplay();
void handleClearSensorsDisplay();
String sensorParamsPageDisplay(const String& message);
void handleSensorParamsDisplay();
void handleSaveSensorParamsDisplay();
void handleTestSensorDisplay();
void startSensorI2CTest(int slot);
void afficherSensorI2CTest();
void maintainAutomaticSensorDisplay();
void maintainRegisteredSensorWifiDetection();
String sensorSearchPageDisplay(int slot);
void handleSearchSensorsDisplay();
void handleAddSensorDisplay();
void handleSensorTriggerDisplay();
void handleSensorPacket(const Pm3dSensorPacket &p);
void onEspNowReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len);
String macToStringDisplay(const uint8_t mac[6]);
bool parseMacDisplay(const String &value, uint8_t mac[6]);
bool sameMacDisplay(const uint8_t a[6], const uint8_t b[6]);
bool macMatchesVariantsDisplay(const uint8_t sender[6], const String &savedText);
bool suffixMatchesSavedSensorDisplay(const char suffix[5], const String &savedSsid, const String &savedMac);
void handleSensorStatusDisplay();
void handleThèmePage();
void handleParamètresPage();
void handleSaveThème();

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
    if (s == defaultDestFR[i] || s == defaultDestNL[i] || s == defaultDestDE[i] || s == defaultDestEN[i]) return true;
  }
  return false;
}

bool isSupportedLang(String lang) {
  lang.toUpperCase();
  return lang == "FR" || lang == "NL" || lang == "DE" || lang == "EN";
}


String translatedThemeNameDisplay(String name) {
  String key = name;
  key.toLowerCase();

  if (key == "blue" || key == "bleu") {
    if (currentLang == "NL") return "Blauw";
    if (currentLang == "DE") return "Blau";
    if (currentLang == "EN") return "Blue";
    return "Bleu";
  }
  if (key == "orange") {
    return "Orange";
  }
  if (key == "green" || key == "vert") {
    if (currentLang == "NL") return "Groen";
    if (currentLang == "DE") return "Gruen";
    if (currentLang == "EN") return "Green";
    return "Vert";
  }
  if (key == "yellow" || key == "jaune") {
    if (currentLang == "NL") return "Geel";
    if (currentLang == "DE") return "Gelb";
    if (currentLang == "EN") return "Yellow";
    return "Jaune";
  }
  if (key == "black" || key == "noir") {
    if (currentLang == "NL") return "Zwart";
    if (currentLang == "DE") return "Schwarz";
    if (currentLang == "EN") return "Black";
    return "Noir";
  }
  if (key == "purple" || key == "violet") {
    if (currentLang == "NL") return "Paars";
    if (currentLang == "DE") return "Violett";
    if (currentLang == "EN") return "Purple";
    return "Violet";
  }
  if (key == "pink" || key == "rose") {
    if (currentLang == "NL") return "Roze";
    if (currentLang == "DE") return "Rosa";
    if (currentLang == "EN") return "Pink";
    return "Rose";
  }
  if (key == "red" || key == "rouge") {
    if (currentLang == "NL") return "Rood";
    if (currentLang == "DE") return "Rot";
    if (currentLang == "EN") return "Red";
    return "Rouge";
  }
  if (key == "white" || key == "blanc") {
    if (currentLang == "NL") return "Wit";
    if (currentLang == "DE") return "Weiss";
    if (currentLang == "EN") return "White";
    return "Blanc";
  }
  if (key == "dark" || key == "sombre") {
    if (currentLang == "NL") return "Donker";
    if (currentLang == "DE") return "Dunkel";
    if (currentLang == "EN") return "Dark";
    return "Sombre";
  }
  if (key == "light" || key == "clair") {
    if (currentLang == "NL") return "Licht";
    if (currentLang == "DE") return "Hell";
    if (currentLang == "EN") return "Light";
    return "Clair";
  }
  if (key == "pm3d") return "PM3D";
  return name;
}

String translateDefaultDestination(const String& s, const String& targetLang) {
  for (int i = 0; i < 5; i++) {
    if (s == defaultDestFR[i] || s == defaultDestNL[i] || s == defaultDestDE[i] || s == defaultDestEN[i]) {
      if (targetLang == "NL") return String(defaultDestNL[i]);
      if (targetLang == "DE") return String(defaultDestDE[i]);
      if (targetLang == "EN") return String(defaultDestEN[i]);
      return String(defaultDestFR[i]);
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

void rTrains() {
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
  if (currentLang == "NL") {
    if (lineIndex == 0) return msgLine1NL;
    if (lineIndex == 1) return msgLine2NL;
    return msgLine3NL;
  }
  if (currentLang == "DE") {
    if (lineIndex == 0) return msgLine1DE;
    if (lineIndex == 1) return msgLine2DE;
    return msgLine3DE;
  }
  if (currentLang == "EN") {
    if (lineIndex == 0) return msgLine1EN;
    if (lineIndex == 1) return msgLine2EN;
    return msgLine3EN;
  }
  if (lineIndex == 0) return msgLine1FR;
  if (lineIndex == 1) return msgLine2FR;
  return msgLine3FR;
}

bool getMsgCenter(int lineIndex) {
  if (currentLang == "NL") {
    if (lineIndex == 0) return msgLine1CenterNL;
    if (lineIndex == 1) return msgLine2CenterNL;
    return msgLine3CenterNL;
  }
  if (currentLang == "DE") {
    if (lineIndex == 0) return msgLine1CenterDE;
    if (lineIndex == 1) return msgLine2CenterDE;
    return msgLine3CenterDE;
  }
  if (currentLang == "EN") {
    if (lineIndex == 0) return msgLine1CenterEN;
    if (lineIndex == 1) return msgLine2CenterEN;
    return msgLine3CenterEN;
  }
  if (lineIndex == 0) return msgLine1CenterFR;
  if (lineIndex == 1) return msgLine2CenterFR;
  return msgLine3CenterFR;
}


String sanitizeHexCouleur(String value, const String& fallback) {
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

void applyPresetThème(const String& presetName) {
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
    applyPresetThème("pm3d");
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
  prefs.putString("msg1DE", msgLine1DE);
  prefs.putString("msg2DE", msgLine2DE);
  prefs.putString("msg3DE", msgLine3DE);
  prefs.putString("msg1EN", msgLine1EN);
  prefs.putString("msg2EN", msgLine2EN);
  prefs.putString("msg3EN", msgLine3EN);
  prefs.putBool("msg1cFR", msgLine1CenterFR);
  prefs.putBool("msg2cFR", msgLine2CenterFR);
  prefs.putBool("msg3cFR", msgLine3CenterFR);
  prefs.putBool("msg1cNL", msgLine1CenterNL);
  prefs.putBool("msg2cNL", msgLine2CenterNL);
  prefs.putBool("msg3cNL", msgLine3CenterNL);
  prefs.putBool("msg1cDE", msgLine1CenterDE);
  prefs.putBool("msg2cDE", msgLine2CenterDE);
  prefs.putBool("msg3cDE", msgLine3CenterDE);
  prefs.putBool("msg1cEN", msgLine1CenterEN);
  prefs.putBool("msg2cEN", msgLine2CenterEN);
  prefs.putBool("msg3cEN", msgLine3CenterEN);
  prefs.putBool("msgEditFR", customMessageEditedFR);
  prefs.putBool("msgEditNL", customMessageEditedNL);
  prefs.putBool("msgEditDE", customMessageEditedDE);
  prefs.putBool("msgEditEN", customMessageEditedEN);
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
  prefs.putString("apPass", apPasswordDisplay);
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

  for (int i = 0; i < NB_SENSORS_MAX_DISPLAY; i++) {
    prefs.putString(("dSenMac" + String(i)).c_str(), macSensorDisplay[i]);
    prefs.putString(("dSenSsid" + String(i)).c_str(), ssidSensorDisplay[i]);
    prefs.putInt(("dSenTrain" + String(i)).c_str(), sensorTrainChoiceDisplay[i]);
    prefs.putInt(("dSenPos" + String(i)).c_str(), sensorPositionChoiceDisplay[i]);

    prefs.putBool(("sEn" + String(i)).c_str(), sensorDetectionEnabledDisplay[i]);
    prefs.putString(("sSide" + String(i)).c_str(), sensorArrowSideBySlotDisplay[i]);
    prefs.putBool(("sBlink" + String(i)).c_str(), sensorArrowBlinkBySlotDisplay[i]);
    prefs.putULong(("sBlinkDur" + String(i)).c_str(), sensorArrowBlinkDurationSecBySlotDisplay[i]);
    prefs.putULong(("sBack" + String(i)).c_str(), sensorReturnNormalSecBySlotDisplay[i]);
    prefs.putString(("sMode" + String(i)).c_str(), sensorTriggerModeBySlotDisplay[i]);
    prefs.putULong(("sEvery" + String(i)).c_str(), sensorAutoEverySecBySlotDisplay[i]);
    prefs.putULong(("sShow" + String(i)).c_str(), sensorAutoShowSecBySlotDisplay[i]);
    prefs.putInt(("sDestSize" + String(i)).c_str(), sensorDestinationTextSizeBySlotDisplay[i]);
    prefs.putBool(("sHeadBlink" + String(i)).c_str(), sensorHeaderBlinkBySlotDisplay[i]);
  }

  prefs.putString("senArrowSide", sensorArrowSideDisplay);
  prefs.putBool("senArrowBlink", sensorArrowBlinkDisplay);
  prefs.putULong("senBlinkSec", sensorArrowBlinkSecDisplay);
  prefs.putULong("senBackSec", sensorReturnNormalSecDisplay);

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
  if (!isSupportedLang(currentLang)) currentLang = "FR";

  msgLine1FR = prefs.getString("msg1FR", "PM3D.NET");
  msgLine2FR = prefs.getString("msg2FR", "vous souhaite");
  msgLine3FR = prefs.getString("msg3FR", "un bon voyage !");
  msgLine1NL = prefs.getString("msg1NL", "PM3D.NET");
  msgLine2NL = prefs.getString("msg2NL", "wenst u");
  msgLine3NL = prefs.getString("msg3NL", "een goede reis !");
  msgLine1DE = prefs.getString("msg1DE", "PM3D.NET");
  msgLine2DE = prefs.getString("msg2DE", "wuenscht Ihnen");
  msgLine3DE = prefs.getString("msg3DE", "eine gute Reise !");
  msgLine1EN = prefs.getString("msg1EN", "PM3D.NET");
  msgLine2EN = prefs.getString("msg2EN", "wishes you");
  msgLine3EN = prefs.getString("msg3EN", "a pleasant trip !");
  msgLine1CenterFR = prefs.getBool("msg1cFR", true);
  msgLine2CenterFR = prefs.getBool("msg2cFR", true);
  msgLine3CenterFR = prefs.getBool("msg3cFR", true);
  msgLine1CenterNL = prefs.getBool("msg1cNL", true);
  msgLine2CenterNL = prefs.getBool("msg2cNL", true);
  msgLine3CenterNL = prefs.getBool("msg3cNL", true);
  msgLine1CenterDE = prefs.getBool("msg1cDE", true);
  msgLine2CenterDE = prefs.getBool("msg2cDE", true);
  msgLine3CenterDE = prefs.getBool("msg3cDE", true);
  msgLine1CenterEN = prefs.getBool("msg1cEN", true);
  msgLine2CenterEN = prefs.getBool("msg2cEN", true);
  msgLine3CenterEN = prefs.getBool("msg3cEN", true);
  customMessageEditedFR = prefs.getBool("msgEditFR", false);
  customMessageEditedNL = prefs.getBool("msgEditNL", false);
  customMessageEditedDE = prefs.getBool("msgEditDE", false);
  customMessageEditedEN = prefs.getBool("msgEditEN", false);
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
  apPasswordDisplay = prefs.getString("apPass", "");
  if (apPasswordDisplay.length() > 0 && apPasswordDisplay.length() < 8) apPasswordDisplay = "";
  if (apPasswordDisplay.length() > 63) apPasswordDisplay = apPasswordDisplay.substring(0, 63);
  apIpSuffix = prefs.getInt("apSuffix", 1);
  if (apIpSuffix < 1) apIpSuffix = 1;
  if (apIpSuffix > 254) apIpSuffix = 254;
  otaManifestUrl = prefs.getString("otaManUrl", "https://raw.githubusercontent.com/PM3D-Wavre/pm3d-firmware/main/ecran_quai/manifest.json");
  otaBinUrl = normalizeGithubRawUrl(prefs.getString("otaBinUrl", ""));
  otaLastStatus = prefs.getString("otaStatus", "Aucune verification effectuee");
  otaLastVersion = prefs.getString("otaVer", "");

  for (int i = 0; i < NB_SENSORS_MAX_DISPLAY; i++) {
    macSensorDisplay[i] = prefs.getString(("dSenMac" + String(i)).c_str(), "");
    ssidSensorDisplay[i] = prefs.getString(("dSenSsid" + String(i)).c_str(), "");
    sensorTrainChoiceDisplay[i] = prefs.getInt(("dSenTrain" + String(i)).c_str(), i);
    if (sensorTrainChoiceDisplay[i] < 0) sensorTrainChoiceDisplay[i] = 0;
    if (sensorTrainChoiceDisplay[i] >= MAX_TRAINS) sensorTrainChoiceDisplay[i] = MAX_TRAINS - 1;
    sensorPositionChoiceDisplay[i] = prefs.getInt(("dSenPos" + String(i)).c_str(), i);
    if (sensorPositionChoiceDisplay[i] < 0) sensorPositionChoiceDisplay[i] = 0;
    if (sensorPositionChoiceDisplay[i] >= NB_SENSORS_MAX_DISPLAY) sensorPositionChoiceDisplay[i] = NB_SENSORS_MAX_DISPLAY - 1;

    sensorDetectionEnabledDisplay[i] = prefs.getBool(("sEn" + String(i)).c_str(), true);
    sensorArrowSideBySlotDisplay[i] = prefs.getString(("sSide" + String(i)).c_str(), i == 0 ? "left" : "right");
    if (sensorArrowSideBySlotDisplay[i] != "left" && sensorArrowSideBySlotDisplay[i] != "right") {
      sensorArrowSideBySlotDisplay[i] = (i == 0 ? "left" : "right");
    }

    sensorArrowBlinkBySlotDisplay[i] = prefs.getBool(("sBlink" + String(i)).c_str(), true);
    sensorArrowBlinkDurationSecBySlotDisplay[i] = prefs.getULong(("sBlinkDur" + String(i)).c_str(), 3);
    if (sensorArrowBlinkDurationSecBySlotDisplay[i] < 1) sensorArrowBlinkDurationSecBySlotDisplay[i] = 1;
    if (sensorArrowBlinkDurationSecBySlotDisplay[i] > 60) sensorArrowBlinkDurationSecBySlotDisplay[i] = 60;

    sensorReturnNormalSecBySlotDisplay[i] = prefs.getULong(("sBack" + String(i)).c_str(), 8);
    if (sensorReturnNormalSecBySlotDisplay[i] < 1) sensorReturnNormalSecBySlotDisplay[i] = 1;
    if (sensorReturnNormalSecBySlotDisplay[i] > 600) sensorReturnNormalSecBySlotDisplay[i] = 600;

    sensorTriggerModeBySlotDisplay[i] = prefs.getString(("sMode" + String(i)).c_str(), "auto");
    if (sensorTriggerModeBySlotDisplay[i] != "auto" && sensorTriggerModeBySlotDisplay[i] != "sensor") sensorTriggerModeBySlotDisplay[i] = "auto";

    sensorAutoEverySecBySlotDisplay[i] = prefs.getULong(("sEvery" + String(i)).c_str(), 30);
    if (sensorAutoEverySecBySlotDisplay[i] < 1) sensorAutoEverySecBySlotDisplay[i] = 1;
    if (sensorAutoEverySecBySlotDisplay[i] > 3600) sensorAutoEverySecBySlotDisplay[i] = 3600;

    sensorAutoShowSecBySlotDisplay[i] = prefs.getULong(("sShow" + String(i)).c_str(), 8);
    if (sensorAutoShowSecBySlotDisplay[i] < 1) sensorAutoShowSecBySlotDisplay[i] = 1;
    if (sensorAutoShowSecBySlotDisplay[i] > 600) sensorAutoShowSecBySlotDisplay[i] = 600;

    sensorDestinationTextSizeBySlotDisplay[i] = prefs.getInt(("sDestSize" + String(i)).c_str(), 1);
    if (sensorDestinationTextSizeBySlotDisplay[i] < 1) sensorDestinationTextSizeBySlotDisplay[i] = 1;
    if (sensorDestinationTextSizeBySlotDisplay[i] > 3) sensorDestinationTextSizeBySlotDisplay[i] = 3;

    sensorHeaderBlinkBySlotDisplay[i] = prefs.getBool(("sHeadBlink" + String(i)).c_str(), true);

    sensorNextAutoTriggerMsDisplay[i] = millis() + sensorAutoEverySecBySlotDisplay[i] * 1000UL;
  }

  sensorArrowSideDisplay = prefs.getString("senArrowSide", "left");
  if (sensorArrowSideDisplay != "left" && sensorArrowSideDisplay != "right") sensorArrowSideDisplay = "right";
  sensorArrowBlinkDisplay = prefs.getBool("senArrowBlink", true);
  sensorArrowBlinkSecDisplay = prefs.getULong("senBlinkSec", 1);
  if (sensorArrowBlinkSecDisplay < 1) sensorArrowBlinkSecDisplay = 1;
  if (sensorArrowBlinkSecDisplay > 60) sensorArrowBlinkSecDisplay = 60;
  sensorReturnNormalSecDisplay = prefs.getULong("senBackSec", 8);
  if (sensorReturnNormalSecDisplay < 1) sensorReturnNormalSecDisplay = 1;
  if (sensorReturnNormalSecDisplay > 600) sensorReturnNormalSecDisplay = 600;

  for (int i = 0; i < MAX_TRAINS; i++) {
    trains[i].heure = prefs.getString(("h" + String(i)).c_str(), "");
    trains[i].destination = prefs.getString(("d" + String(i)).c_str(), "");
    trains[i].voie = prefs.getString(("v" + String(i)).c_str(), "");
  }

  String themePreset = prefs.getString("thPreset", "pm3d");
  applyPresetThème(themePreset);
  theme.bodyBg1 = sanitizeHexCouleur(prefs.getString("thBg1", theme.bodyBg1), theme.bodyBg1);
  theme.bodyBg2 = sanitizeHexCouleur(prefs.getString("thBg2", theme.bodyBg2), theme.bodyBg2);
  theme.panelBg1 = sanitizeHexCouleur(prefs.getString("thPan1", theme.panelBg1), theme.panelBg1);
  theme.panelBg2 = sanitizeHexCouleur(prefs.getString("thPan2", theme.panelBg2), theme.panelBg2);
  theme.accent = sanitizeHexCouleur(prefs.getString("thAcc", theme.accent), theme.accent);
  theme.accentText = sanitizeHexCouleur(prefs.getString("thAccTxt", theme.accentText), theme.accentText);
  theme.text = sanitizeHexCouleur(prefs.getString("thText", theme.text), theme.text);
  theme.muted = sanitizeHexCouleur(prefs.getString("thMuted", theme.muted), theme.muted);
  theme.info = sanitizeHexCouleur(prefs.getString("thInfo", theme.info), theme.info);
  theme.warn = sanitizeHexCouleur(prefs.getString("thWarn", theme.warn), theme.warn);
  theme.inputBg = sanitizeHexCouleur(prefs.getString("thInput", theme.inputBg), theme.inputBg);
  prefs.end();

  rTrains();

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
    } else if (currentLang == "DE") {
      trains[0] = {"08:12", "Bruessel", "1"};
      trains[1] = {"08:19", "Antwerpen", "3"};
      trains[2] = {"08:27", "Luettich", "2"};
      trains[3] = {"08:34", "Namur", "5"};
      trains[4] = {"08:41", "Mons", "4"};
    } else if (currentLang == "EN") {
      trains[0] = {"08:12", "Brussels", "1"};
      trains[1] = {"08:19", "Antwerp", "3"};
      trains[2] = {"08:27", "Liege", "2"};
      trains[3] = {"08:34", "Namur", "5"};
      trains[4] = {"08:41", "Mons", "4"};
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

String trText(const String& fr, const String& nl, const String& de, const String& en) {
  if (currentLang == "NL") return nl;
  if (currentLang == "DE") return de;
  if (currentLang == "EN") return en;
  return fr;
}

String t_title()       { return trText("Tableau des départs", "Vertrekbord", "Abfahrtstafel", "Tableau des départs"); }
String t_small()       { return trText(". Les réglages avancés sont séparés.", "e interface. Geavanceerde instellingen staan apart.", "Kompakte Oberfläche. Erweiterte Einstellungen sind getrennt.", " interface. Advanced settings are separate."); }
String t_save()        { return trText("Enregistrer", "Opslaan", "Speichern", "Save"); }
String t_saved()       { return trText("Données enregistrées.", "Gegevens opgeslagen.", "Daten gespeichert.", "Data saved."); }
String t_train()       { return trText("Train ", "Trein ", "Zug ", "Train "); }
String t_hour()        { return trText("Heure", "Uur", "Zeit", "Time"); }
String t_dest()        { return trText("Destination", "Richting", "Ziel", "Destination"); }
String t_track_label() { return trText("Voie", "Spoor", "Gleis", "Track"); }
String t_connect()     { return trText("Connexion", "Verbinding", "Verbindung", "Connection"); }
String t_password()    { return trText("Mot de passe", "Wachtwoord", "Passwort", "Password"); }
String t_address()     { return trText("Adresse", "Adres", "Adresse", "Address"); }
String t_language()    { return trText("Langue", "Taal", "Sprache", "Langue"); }
String t_msg_title()   { return trText("Texte libre", "Vrij bericht", "Freitext", "Free text"); }
String t_msg_every()   { return trText("Afficher toutes les", "Tonen om de", "Anzeigen alle", "Show every"); }
String t_seconds()     { return trText("secondes", "seconden", "Sekunden", "seconds"); }
String t_duration()    { return trText("Duree", "Duur", "Dauer", "Duration"); }
String t_line_label(int n) { return trText("Ligne ", "Lijn ", "Zeile ", "Line ") + String(n); }
String t_center()      { return trText("Centrer", "Centreren", "Zentrieren", "Center"); }
String t_banner()      { return trText("Produit par PM3D - visitez pm3d.net", "Gemaakt door PM3D - bezoek pm3d.net", "Hergestellt von PM3D - pm3d.net", "Made by PM3D - visit pm3d.net"); }
String t_preview()     { return trText("Aperçu", "Voorbeeld", "Vorschau", "Preview"); }

String t_device_title() { return trText("Réglages écran", "Scherminstellingen", "Bildschirm-Einstellungen", "Screen settings"); }
String t_device_name()  { return trText("Nom de l ecran", "Naam van het scherm", "Name des Bildschirms", "Screen name"); }
String t_brightness()   { return trText("Luminosite ecran", "Helderheid scherm", "Bildschirmhelligkeit", "Screen brightness"); }
String t_brightness_saved() { return trText("Luminosite enregistree.", "Helderheid opgeslagen.", "Helligkeit gespeichert.", "Brightness saved."); }
String t_signal_num()   { return trText("Numero", "Nummer", "Nummer", "Number"); }
String t_saved_device() { return trText("Reglages ecran enregistres.", "Scherminstellingen opgeslagen.", "Bildschirm-Einstellungen gespeichert.", "Screen settings saved."); }
String t_big_settings() { return trText("Parametres", "Parameters", "Parameter", "Settings"); }
String t_lang_pack()    { return trText("Langue", "Taal", "Sprache", "Langue"); }

String t_theme()        { return trText("Thème", "Thema", "Design", "Thème"); }
String t_theme_title()  { return trText("Theme de l interface", "Interface-thema", "Oberflaechen-Design", "Interface theme"); }
String t_theme_saved()  { return trText("Theme enregistre.", "Thema opgeslagen.", "Design gespeichert.", "Theme saved."); }
String t_rgb_help()     { return trText("Code RGB au format #RRGGBB.", "RGB-code in formaat #RRGGBB.", "RGB-Code im Format #RRGGBB.", "RGB code in #RRGGBB format."); }
String t_back()         { return trText("Retour", "Terug", "Zurueck", "Back"); }
String t_apply()        { return trText("Appliquer", "Toepassen", "Anwenden", "Apply"); }
String t_custom_colors(){ return trText("Couleurs personnalisees", "Aangepaste kleuren", "Benutzerdefinierte Farben", "Custom colors"); }
String t_choose_theme() { return trText("Choisissez un thème de base", "Kies een basis-thema", "Basis-Design waehlen", "Choose a base theme"); }
String t_body_bg1()     { return trText("Fond 1", "Achtergrond 1", "Hintergrund 1", "Background 1"); }
String t_body_bg2()     { return trText("Fond 2", "Achtergrond 2", "Hintergrund 2", "Background 2"); }
String t_panel_bg1()    { return trText("Panneau 1", "Paneel 1", "Feld 1", "Panel 1"); }
String t_panel_bg2()    { return trText("Panneau 2", "Paneel 2", "Feld 2", "Panel 2"); }
String t_accent()       { return trText("Accent", "Accent", "Akzent", "Accent"); }
String t_accent_text()  { return trText("Texte accent", "Accenttekst", "Akzenttext", "Accent text"); }
String t_text_color()   { return trText("Texte principal", "Hoofdtekst", "Haupttext", "Main text"); }
String t_muted_color()  { return trText("Texte secondaire", "Secundaire tekst", "Sekundaertext", "Secondary text"); }
String t_info_color()   { return trText("Info", "Info", "Info", "Info"); }
String t_warn_color()   { return trText("Avertissement", "Waarschuwing", "Warnung", "Warning"); }
String t_input_bg()     { return trText("Fond des champs", "Veldachtergrond", "Feldhintergrund", "Input background"); }

String currentApIpString() {
  return String("192.168.4.") + String(apIpSuffix);
}

String buildApSsidFromMac() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  mac.toUpperCase();
  String suffix = "0000";
  if (mac.length() >= 4) {
    suffix = mac.substring(mac.length() - 4);
  }
  return String("PM3D-Display ") + suffix;
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

  int valueStart = colonPos + 1;
  while (valueStart < (int)json.length()) {
    char c = json.charAt(valueStart);
    if (c != ' ' && c != '\n' && c != '\r' && c != '\t') break;
    valueStart++;
  }
  if (valueStart >= (int)json.length()) return "";

  if (json.charAt(valueStart) == '"') {
    int firstQuote = valueStart;
    int secondQuote = firstQuote + 1;
    while (secondQuote < (int)json.length()) {
      if (json.charAt(secondQuote) == '"' && json.charAt(secondQuote - 1) != '\\') break;
      secondQuote++;
    }
    if (secondQuote >= (int)json.length()) return "";
    String value = json.substring(firstQuote + 1, secondQuote);
    value.replace("\\/", "/");
    value.replace("\\\"", "\"");
    return value;
  }

  int valueEnd = valueStart;
  while (valueEnd < (int)json.length()) {
    char c = json.charAt(valueEnd);
    if (c == ',' || c == '}' || c == ']') break;
    valueEnd++;
  }
  String value = json.substring(valueStart, valueEnd);
  value.trim();
  return value;
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
  url.replace(" ", "");

  String rawRefsPrefix = "https://github.com/PM3D-Wavre/pm3d-firmware/raw/refs/heads/main/";
  if (url.startsWith(rawRefsPrefix)) {
    url = "https://raw.githubusercontent.com/PM3D-Wavre/pm3d-firmware/main/" + url.substring(rawRefsPrefix.length());
  }

  String rawMainPrefix = "https://github.com/PM3D-Wavre/pm3d-firmware/raw/main/";
  if (url.startsWith(rawMainPrefix)) {
    url = "https://raw.githubusercontent.com/PM3D-Wavre/pm3d-firmware/main/" + url.substring(rawMainPrefix.length());
  }

  String blobMainPrefix = "https://github.com/PM3D-Wavre/pm3d-firmware/blob/main/";
  if (url.startsWith(blobMainPrefix)) {
    url = "https://raw.githubusercontent.com/PM3D-Wavre/pm3d-firmware/main/" + url.substring(blobMainPrefix.length());
  }

  String blobRefsPrefix = "https://github.com/PM3D-Wavre/pm3d-firmware/blob/refs/heads/main/";
  if (url.startsWith(blobRefsPrefix)) {
    url = "https://raw.githubusercontent.com/PM3D-Wavre/pm3d-firmware/main/" + url.substring(blobRefsPrefix.length());
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
      String latestLabel = extractJsonValue(latestBlock, "name");
      if (latestLabel.length() == 0) latestLabel = extractJsonValue(latestBlock, "version");
      String latestUrl = extractJsonValue(latestBlock, "url");
      if (latestUrl.length() == 0) latestUrl = extractJsonValue(latestBlock, "bin");
      addOtaVersion(latestLabel, latestUrl);
    }
  }

  int versionsPos = payload.indexOf("\"versions\"");
  int arrayStart = -1;
  int arrayEnd = -1;

  if (versionsPos >= 0) {
    arrayStart = payload.indexOf("[", versionsPos);
    arrayEnd = payload.indexOf("]", arrayStart);
  }

  if (arrayStart < 0 || arrayEnd <= arrayStart) {
    String trimmed = payload;
    trimmed.trim();
    if (trimmed.startsWith("[")) {
      arrayStart = payload.indexOf("[");
      arrayEnd = payload.lastIndexOf("]");
    }
  }

  if (arrayStart >= 0 && arrayEnd > arrayStart) {
    String arr = payload.substring(arrayStart + 1, arrayEnd);
    int pos = 0;
    while (true) {
      int objStart = arr.indexOf("{", pos);
      if (objStart < 0) break;
      int objEnd = arr.indexOf("}", objStart);
      if (objEnd < 0) break;
      String one = arr.substring(objStart, objEnd + 1);
      String label = extractJsonValue(one, "name");
      String ver = extractJsonValue(one, "version");
      String theme = extractJsonValue(one, "theme");
      if (label.length() == 0) label = ver;
      else if (ver.length() > 0) label += " - " + ver;
      if (theme.length() > 0) label += " (" + theme + ")";
      String url = extractJsonValue(one, "url");
      if (url.length() == 0) url = extractJsonValue(one, "bin");
      addOtaVersion(label, url);
      pos = objEnd + 1;
    }
  }

  String singleLabel = extractJsonValue(payload, "name");
  String singleVer = extractJsonValue(payload, "version");
  if (singleLabel.length() == 0) singleLabel = singleVer;
  else if (singleVer.length() > 0) singleLabel += " - " + singleVer;
  String singleUrl = extractJsonValue(payload, "url");
  if (singleUrl.length() == 0) singleUrl = extractJsonValue(payload, "bin");
  addOtaVersion(singleLabel, singleUrl);

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
  html += "<div style='margin-top:10px; display:flex; gap:6px; flex-wrap:wrap;'>";
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
  payload.trim();
  if (payload.length() > 0 && payload.charAt(0) == (char)0xEF) {
    payload = payload.substring(3);
    payload.trim();
  }

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
    String debugPayload = payload;
    debugPayload.replace("\n", " ");
    debugPayload.replace("\r", " ");
    debugPayload.trim();
    if (debugPayload.startsWith("<!DOCTYPE") || debugPayload.startsWith("<html") || debugPayload.indexOf("<html") >= 0) {
      otaLastStatus = "Manifeste invalide: URL GitHub HTML, utiliser raw.githubusercontent.com";
    } else if (debugPayload.length() == 0) {
      otaLastStatus = "Manifeste invalide: reponse vide";
    } else {
      if (debugPayload.length() > 90) debugPayload = debugPayload.substring(0, 90) + "...";
      otaLastStatus = "Manifeste invalide: JSON lu mais aucune URL firmware trouvee - " + debugPayload;
    }
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

void otaProgressSend(int percent, const String& phase) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  String js = "<script>setOtaProgress(" + String(percent) + ",";
  js += "'";
  String safe = phase;
  safe.replace("\\", "\\\\");
  safe.replace("'", "\\'");
  safe.replace("\n", " ");
  safe.replace("\r", " ");
  js += safe;
  js += "');</script>\n";
  server.sendContent(js);
}

void otaProgressPageStart(const String& title) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += makeStyleBlock();
  html += "<style>";
  html += ".otaOverlay{position:fixed;inset:0;background:rgba(0,0,0,.72);display:flex;align-items:center;justify-content:center;z-index:9999;padding:18px;}";
  html += ".otaBox{width:min(520px,94vw);border:1px solid rgba(255,255,255,.22);border-radius:24px;padding:24px;background:linear-gradient(145deg,rgba(18,28,48,.98),rgba(3,8,18,.98));box-shadow:0 20px 70px rgba(0,0,0,.55),inset 0 1px rgba(255,255,255,.18);}";
  html += ".otaTitle{font-size:22px;font-weight:800;margin-bottom:10px;}";
  html += ".otaText{font-size:14px;opacity:.9;margin-bottom:16px;}";
  html += ".otaBar{height:20px;border-radius:999px;background:rgba(255,255,255,.12);overflow:hidden;border:1px solid rgba(255,255,255,.18);}";
  html += ".otaFill{height:100%;width:0%;border-radius:999px;background:linear-gradient(90deg,#1e8bff,#5ee7ff,#b8f7ff);box-shadow:0 0 18px rgba(94,231,255,.7);transition:width .18s linear;}";
  html += ".otaPct{font-size:28px;font-weight:900;margin-top:14px;}";
  html += ".otaWarn{margin-top:14px;font-size:12px;opacity:.78;}";
  html += ".otaSuccess{display:none;margin-top:18px;padding:18px;border-radius:18px;background:rgba(0,180,100,.18);border:1px solid rgba(120,255,190,.45);}";
  html += ".otaSuccessTitle{font-size:24px;font-weight:900;margin-bottom:8px;color:#c9ffe2;}";
  html += ".otaSuccessText{font-size:16px;line-height:1.45;color:#fff;}";
  html += ".otaBtn{display:inline-block;margin-top:14px;padding:12px 16px;border-radius:14px;background:#5ee7ff;color:#06111f;text-decoration:none;font-weight:900;}";
  html += "</style></head><body><div class='otaOverlay'><div class='otaBox'>";
  html += "<div class='otaTitle'>" + htmlEscape(title) + "</div>";
  html += "<div id='otaPhase' class='otaText'>Preparation...</div>";
  html += "<div class='otaBar'><div id='otaFill' class='otaFill'></div></div>";
  html += "<div id='otaPct' class='otaPct'>0%</div>";
  html += "<div class='otaWarn'>Ne pas couper l’alimentation pendant la mise a jour.</div>";
  html += "<div id='otaSuccess' class='otaSuccess'><div class='otaSuccessTitle'>Installation effectuee</div><div class='otaSuccessText'>Veuillez vous reconnecter au Wi-Fi <b>" + htmlEscape(AP_SSID) + "</b>, puis relancer l’interface.<br>La page d’accueil va essayer de se rouvrir automatiquement.</div><a id='otaHomeBtn' class='otaBtn' href='http://" + currentApIpString() + "/'>Relancer l’interface</a></div>";
  html += "</div></div>";
  html += "<script>function setOtaProgress(p,t){document.getElementById('otaFill').style.width=p+'%';document.getElementById('otaPct').textContent=p+'%';document.getElementById('otaPhase').textContent=t;}function showOtaSuccess(){setOtaProgress(100,'Installation effectuee');document.getElementById('otaSuccess').style.display='block';document.querySelector('.otaWarn').textContent='Redemarrage du module en cours. Reconnectez-vous au Wi-Fi PM3D si le telephone ne le fait pas automatiquement.';setTimeout(function(){location.href='http://" + currentApIpString() + "/';},9000);}</script>\n";
  server.sendContent(html);
}

bool performOtaFromUrlWithProgress(const String& url, String &resultMessage) {
  if (!wifiStaConnected()) {
    resultMessage = "Pas de connexion WiFi Internet";
    otaProgressSend(0, resultMessage);
    return false;
  }
  if (url.length() == 0) {
    resultMessage = "URL du firmware vide";
    otaProgressSend(0, resultMessage);
    return false;
  }

  String finalUrl = normalizeGithubRawUrl(url);
  otaProgressSend(3, "Connexion au serveur firmware...");

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, finalUrl)) {
    resultMessage = "Ouverture firmware impossible";
    otaProgressSend(0, resultMessage);
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    resultMessage = "Erreur HTTP firmware: " + String(httpCode);
    otaProgressSend(0, resultMessage);
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  bool canBegin = Update.begin(contentLength > 0 ? (size_t)contentLength : UPDATE_SIZE_UNKNOWN);
  if (!canBegin) {
    resultMessage = "Update.begin a echoue";
    otaProgressSend(0, resultMessage);
    http.end();
    return false;
  }

  otaProgressSend(8, "Telechargement du firmware...");
  WiFiClient * stream = http.getStreamPtr();
  uint8_t buff[1024];
  size_t totalWritten = 0;
  unsigned long lastUi = 0;

  while (http.connected() && (contentLength <= 0 || totalWritten < (size_t)contentLength)) {
    size_t available = stream->available();
    if (available) {
      int readLen = stream->readBytes(buff, available > sizeof(buff) ? sizeof(buff) : available);
      if (readLen > 0) {
        size_t written = Update.write(buff, readLen);
        totalWritten += written;
        if (written != (size_t)readLen) {
          resultMessage = "Erreur ecriture OTA";
          otaProgressSend(0, resultMessage);
          Update.abort();
          http.end();
          return false;
        }
        if (millis() - lastUi > 180 || (contentLength > 0 && totalWritten >= (size_t)contentLength)) {
          int pct = 10;
          if (contentLength > 0) pct = 10 + (int)((totalWritten * 78UL) / (unsigned long)contentLength);
          otaProgressSend(pct, "Telechargement : " + String(totalWritten / 1024) + " Ko" + (contentLength > 0 ? String(" / ") + String(contentLength / 1024) + " Ko" : ""));
          lastUi = millis();
          delay(1);
        }
      }
    } else {
      delay(1);
    }
  }

  if (contentLength > 0 && totalWritten != (size_t)contentLength) {
    resultMessage = "Firmware incomplet";
    otaProgressSend(0, resultMessage);
    Update.abort();
    http.end();
    return false;
  }

  otaProgressSend(92, "Installation en cours...");
  if (!Update.end()) {
    resultMessage = "Echec finalisation OTA";
    otaProgressSend(0, resultMessage);
    http.end();
    return false;
  }

  if (!Update.isFinished()) {
    resultMessage = "OTA non terminee";
    otaProgressSend(0, resultMessage);
    http.end();
    return false;
  }

  http.end();
  otaProgressSend(100, "Mise a jour reussie. Redemarrage...");
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
  } else if (currentLang == "DE") {
    display.setCursor(X_HEURE, 1);
    display.print("Zeit");
    display.setCursor(X_DEST, 1);
    display.print("Ziel");
    display.setCursor(94, 1);
    display.print("Gleis");
  } else if (currentLang == "EN") {
    display.setCursor(X_HEURE, 1);
    display.print("Time");
    display.setCursor(X_DEST, 1);
    display.print("Destination");
    display.setCursor(X_V, 1);
    display.print("Tr");
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

  String txt = currentLang == "NL" ? "Vertrekbord" : (currentLang == "DE" ? "Abfahrtstafel" : (currentLang == "EN" ? "Departure board" : "Tableau des departs"));
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

  String nom = "V2.1 Connected";
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




String advancedWarningModalHtml() {
  String h;
  String title = (currentLang == "FR" ? "Attention - réglages avancés" : (currentLang == "NL" ? "Opgelet - geavanceerde instellingen" : (currentLang == "DE" ? "Achtung - erweiterte Einstellungen" : "Warning - advanced settings")));
  String txt = (currentLang == "FR" ? "Cette zone est réservée à la configuration technique de l’installation. Une modification incorrecte peut perturber le fonctionnement du Display, des sensors ou des accessoires associés." : (currentLang == "NL" ? "Deze zone is voor technische configuratie van de installatie. Een verkeerde wijziging kan de werking van het Display, sensoren of gekoppelde accessoires verstoren." : (currentLang == "DE" ? "Dieser Bereich ist für die technische Konfiguration der Anlage vorgesehen. Eine falsche Änderung kann den Betrieb des Displays, der Sensoren oder der verbundenen Zubehörteile stören." : "This area is reserved for technical configuration of the installation. An incorrect change may disturb the Display, sensors or linked accessories.")));
  String cancelTxt = (currentLang == "FR" ? "Annuler" : (currentLang == "NL" ? "Annuleren" : (currentLang == "DE" ? "Abbrechen" : "Cancel")));
  String okTxt = (currentLang == "FR" ? "Je comprends les risques - Continuer" : (currentLang == "NL" ? "Ik begrijp de risico’s - Doorgaan" : (currentLang == "DE" ? "Ich verstehe die Risiken - Weiter" : "I understand the risks - Continue")));
  h += "<div id='advModal' class='advModal'><div class='advModalBox'>";
  h += "<div class='advModalTitle'><span class='advIcon'>!</span>" + htmlEscape(title) + "</div>";
  h += "<div class='advModalText'>" + htmlEscape(txt) + "</div>";
  h += "<div class='advModalBtns'><button class='danger' type='button' onclick='hideAdvancedWarning()'>" + htmlEscape(cancelTxt) + "</button><button type='button' onclick=\"location.href='/advanced'\">" + htmlEscape(okTxt) + "</button></div>";
  h += "</div></div>";
  return h;
}

String introPage() {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>PM3D Display</title>";
  html += "<style>";
  html += "body{margin:0;min-height:100vh;overflow:auto;display:flex;align-items:flex-start;justify-content:center;padding-top:6px;box-sizing:border-box;font-family:Arial,Helvetica,sans-serif;color:white;background:radial-gradient(circle at 50% 15%,rgba(95,190,255,.28),rgba(3,12,28,.96) 42%,#01040A 100%);}";
  html += ".intro{width:min(92vw,540px);text-align:center;padding:16px 16px 18px;border-radius:28px;background:linear-gradient(180deg,rgba(14,42,78,.72),rgba(1,8,18,.84));border:1px solid rgba(180,230,255,.25);box-shadow:0 24px 70px rgba(0,0,0,.55),inset 0 1px 0 rgba(255,255,255,.18);}";
  html += ".logoZone{position:relative;display:inline-block;margin:0 auto;}";
  html += ".logo{width:180px;max-width:70%;animation:spinLogo 6s ease-in-out forwards, glowLogo 1.4s ease-in-out infinite alternate;filter:drop-shadow(0 18px 24px rgba(0,0,0,.55));}";
  html += ".gateStamp{position:absolute;left:50%;top:50%;z-index:3;opacity:0;transform:translate(-50%,-50%) rotate(-13deg) scale(.65);padding:8px 16px;border:4px solid rgba(255,70,70,.92);border-radius:10px;color:#ff5b5b;background:rgba(255,255,255,.04);font-size:24px;font-weight:1000;letter-spacing:2px;text-transform:uppercase;text-shadow:0 2px 0 rgba(0,0,0,.65);box-shadow:0 0 18px rgba(255,40,40,.35),inset 0 0 12px rgba(255,40,40,.18);animation:stampGate 6s ease-out forwards;}";
  html += "@keyframes stampGate{0%,49%{opacity:0;transform:translate(-50%,-50%) rotate(-13deg) scale(.65);}52%{opacity:1;transform:translate(-50%,-50%) rotate(-13deg) scale(1.18);}58%,100%{opacity:1;transform:translate(-50%,-50%) rotate(-13deg) scale(1);}}";
  html += "@keyframes spinLogo{0%{transform:scale(.72) rotate(-18deg);opacity:0;}18%{opacity:1;}70%{transform:scale(1.03) rotate(360deg);}100%{transform:scale(1) rotate(360deg);opacity:1;}}";
  html += "@keyframes glowLogo{from{filter:drop-shadow(0 12px 20px rgba(80,190,255,.25));}to{filter:drop-shadow(0 18px 30px rgba(190,255,255,.55));}}";
  html += ".title{font-size:27px;font-weight:900;margin:8px 0 5px;text-shadow:0 2px 10px #000;}";
  html += ".phrase{font-size:13px;color:#CDEFFF;line-height:1.35;margin-bottom:10px;}";
  html += ".bar{position:relative;height:20px;border-radius:999px;background:#020812;border:1px solid rgba(190,230,255,.35);overflow:hidden;box-shadow:inset 0 2px 8px rgba(0,0,0,.8);}";
  html += ".fill{height:100%;width:0%;border-radius:999px;background:linear-gradient(90deg,#74d9ff,#d8faff,#5fb7ff);animation:load 6s linear forwards;box-shadow:0 0 18px rgba(120,220,255,.9);}";
  html += ".fill:after{content:'';display:block;height:100%;background:repeating-linear-gradient(45deg,rgba(255,255,255,.38) 0 10px,transparent 10px 20px);animation:stripes .65s linear infinite;}";
  html += "@keyframes load{from{width:0%;}to{width:100%;}}@keyframes stripes{from{transform:translateX(-20px);}to{transform:translateX(20px);}}";
  html += ".pct{font-weight:900;font-size:18px;margin-top:12px;color:#E9FAFF;}";
  html += ".steps{height:18px;margin-top:10px;font-size:12px;color:#A9DDF7;}";
  html += ".pm3dTickerBox{margin:8px auto 12px;width:100%;height:34px;border-radius:18px;overflow:hidden;position:relative;background:linear-gradient(180deg,rgba(210,230,245,.16),rgba(20,38,55,.38) 45%,rgba(2,9,18,.72));border:1px solid rgba(185,225,255,.32);box-shadow:inset 0 1px 0 rgba(255,255,255,.22),inset 0 -16px 30px rgba(0,0,0,.28),0 0 22px rgba(80,185,255,.22);}";
  html += ".pm3dTickerBox:before{content:'';position:absolute;inset:0;background:linear-gradient(90deg,transparent,rgba(130,220,255,.18),transparent);animation:scanLight 2.2s linear infinite;z-index:2;pointer-events:none;}";
  html += ".pm3dTicker{position:absolute;white-space:nowrap;left:-70%;top:50%;transform:translateY(-50%);font-size:23px;font-weight:1000;letter-spacing:5px;text-transform:uppercase;color:#dff7ff;text-shadow:0 1px 0 #7fa9bf,0 2px 0 #31546a,0 5px 12px rgba(0,0,0,.75),0 0 18px rgba(95,210,255,.95);animation:tickerPm3d 6s linear infinite;}";
  html += "@keyframes tickerPm3d{0%{left:-75%;}100%{left:115%;}}@keyframes scanLight{0%{transform:translateX(-100%);}100%{transform:translateX(100%);}}";
  html += "</style>";
  html += "<script>";
  html += "function showAdvancedWarning(){document.getElementById(\'advModal\').style.display=\'flex\';return false;}function hideAdvancedWarning(){document.getElementById(\'advModal\').style.display=\'none\';}";
  html += "let p=0;const msgs=['Initialisation du systeme PM3D','Verification de l ecran','Chargement des departs','Chargement de l interface','Pret au depart'];";
  html += "function tick(){p=Math.min(100,Math.round((performance.now()-t0)/6000*100));document.getElementById('pct').innerText=p+'%';document.getElementById('steps').innerText=msgs[Math.min(msgs.length-1,Math.floor(p/25))];if(p<100)requestAnimationFrame(tick);else setTimeout(()=>{location.replace('/main');},350);}";
  html += "let t0;window.onload=()=>{t0=performance.now();tick();};";
  html += "</script></head><body>";
  html += "<div class='intro'>";
  html += "<div class='logoZone'>";
  html += "<img class='logo' src='data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAoQAAAJ1CAYAAABAeeHzAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAADsMAAA7DAcdvqGQAAJwrSURBVHhe7d0HnBTl/T/wL8IdHJ2DozcFpEiVjpQIigWNGk3URBPRxNj9xxKNJerPEk0ssXcsWLACRhBUUDgEpUgX0EPqgXQ4yh39P59n5znmlr27LTO7M/t83q9sbmYP7/ZmZ2c+833KVDhsESIiIiIy1jH2VyIiIiIyFAMhERERkeEYCImIiIgMx0BIREREZDgGQiIiIiLDMRASERERGY6BkIiIiMhwDIREREREhmMgJCIiIjIcAyERERGR4RgIiYiIiAzHQEhERERkOAZCIiIiIsMxEBIREREZjoGQiIiIyHAMhERERESGYyAkIiIiMhwDIREREZHhGAiJiIiIDMdASERERGQ4BkIiIiIiwzEQEhERERmOgZCIiIjIcAyERERERIZjICQiIiIyHAMhERERkeEYCImIiIgMx0BIREREZDgGQiIiIiLDMRASERERGY6BkIiIiMhwDIREREREhmMgJCIiIjIcAyERERGR4RgIiYiIiAzHQEhERERkOAZCIiIiIsMxEBIREREZjoGQiIiIyHAMhERERESGYyAkIiIiMhwDIREREZHhGAiJiIiIDMdASERERGQ4BkIiIiIiwzEQEhERERmOgZCIiIjIcAyERERERIZjICQiIiIyHAMhERERkeEYCImIiIgMx0BIREREZDgGQiIiIiLDMRASERERGY6BkIiIiMhwDIREREREhmMgJCIiIjIcAyERERGR4RgIiYiIiAzHQEhERERkOAZCIiIiIsMxEBIREREZjoGQiIiIyHAMhERERESGYyAkIiIiMhwDIREREZHhGAiJiIiIDMdASERERGQ4BkIiIiIiwzEQEhERERmOgZCIiIjIcAyERERERIZjICQiIiIyHAMhERERkeEYCImIiIgMx0BIREREZDgGQiIiIiLDMRASke+tO3DIXiIiIi8wEBKR7xVVOkby7WUiInIfAyER+dp+64H6YKH1WG09DloPIiJyFwMhEfnaXvsr7LMeqBQyFBIRuYuBkIh8rSCs/yBDIRGR+xgIicjXKlY6+jDFUEhE5C4GQiLyNWeTsRNDIRGRexgIici3ig4cUsGvNPje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'>";
  html += "<div class='gateStamp'>Display</div></div>";
  html += "<div class='title'>PM3D Display</div>";
  html += "<div class='phrase'>" + htmlEscape(currentLang == "FR" ? "Systeme autonome PM3D pour ecran de quai miniature" : (currentLang == "NL" ? "Autonoom PM3D-systeem voor miniatuurperrondisplay" : (currentLang == "DE" ? "Autonomes PM3D-System fuer miniature Bahnsteiganzeige" : "Standalone PM3D system for miniature platform display"))) + "<br>" + htmlEscape(currentLang == "FR" ? "Tableau des departs et messages libres" : (currentLang == "NL" ? "Vertrekbord en vrije berichten" : (currentLang == "DE" ? "Abfahrtstafel und freie Nachrichten" : "Departure board and free messages"))) + "</div>";
  html += "<div class='pm3dTickerBox'><div class='pm3dTicker'>PM3D.NET &nbsp; • &nbsp; PM3D.NET &nbsp; • &nbsp; PM3D.NET</div></div>";
  html += "<div class='bar'><div class='fill'></div></div>";
  html += "<div id='pct' class='pct'>0%</div>";
  html += "<div id='steps' class='steps'>Initialisation du systeme PM3D</div>";
  html += "</div></body></html>";
  return html;
}

String makeStyleBlock() {
  String html = "<style>";
  bool isPm3d = (theme.preset == "pm3d");
  html += "body{font-family:Arial,Helvetica,sans-serif;background:linear-gradient(180deg," + theme.bodyBg1 + " 0%," + theme.bodyBg2 + " 100%);color:" + theme.text + ";margin:0;padding:10px;}";
  if (isPm3d) html += "body{background-attachment:fixed;}";
  html += ".container{max-width:1080px;margin:0 auto;}";
  html += ".panel{background:linear-gradient(180deg," + theme.panelBg1 + " 0%," + theme.panelBg2 + " 100%);border:1px solid " + theme.accent + ";border-radius:10px;padding:10px;margin-bottom:10px;box-shadow:0 0 0 1px rgba(255,255,255,0.03) inset;}";
  if (isPm3d) html += ".panel{border-color:#7DD3FF;box-shadow:0 0 0 1px rgba(255,255,255,0.06) inset,0 10px 24px rgba(0,0,0,0.35),0 0 14px rgba(125,211,255,0.18);position:relative;overflow:hidden;}.panel:before{content:'';position:absolute;left:0;right:0;top:0;height:1px;background:linear-gradient(90deg,rgba(255,255,255,0.02),rgba(125,211,255,0.70),rgba(255,255,255,0.02));}";
  html += ".topbar{display:flex;justify-content:space-between;align-items:center;gap:12px;flex-wrap:wrap;}";
  html += ".brand{font-size:16px;font-weight:700;letter-spacing:0.5px;}";
  if (isPm3d) html += ".brand{letter-spacing:1.2px;text-transform:uppercase;text-shadow:0 0 10px rgba(125,211,255,0.28);}";
  html += ".sub{color:" + theme.muted + ";font-size:11px;margin-top:3px;}";
  html += ".badge{background:" + theme.accent + ";color:" + theme.accentText + ";border-radius:999px;padding:5px 10px;font-weight:700;display:inline-block;}";
  if (isPm3d) html += ".badge{background:linear-gradient(180deg,#9BE7FF 0%,#49C2FF 48%,#0A67B8 100%);color:#03101C;border:1px solid #AEEBFF;box-shadow:inset 0 1px 0 rgba(255,255,255,0.60),0 0 10px rgba(73,194,255,0.18);}";
  html += ".langbox{display:flex;gap:6px;flex-wrap:wrap;}";
  html += ".langbtn, button{padding:7px 10px;border:none;border-radius:12px;background:" + theme.accent + ";color:" + theme.accentText + ";font-weight:700;cursor:pointer;text-decoration:none;display:inline-block;}";
  if (isPm3d) html += ".langbtn, button{background:linear-gradient(180deg,#6FD9FF 0%,#33B8FF 42%,#0A74C8 51%,#07589C 100%);color:#03101C;border:1px solid #AEEBFF;box-shadow:inset 0 1px 0 rgba(255,255,255,0.70),inset 0 -10px 18px rgba(0,0,0,0.18),0 0 0 1px rgba(125,211,255,0.20),0 8px 18px rgba(0,0,0,0.30),0 0 12px rgba(125,211,255,0.12);text-shadow:0 1px 0 rgba(255,255,255,0.20);}button:hover,.langbtn:hover{filter:brightness(1.06);}button:active,.langbtn:active{transform:translateY(1px);box-shadow:inset 0 1px 0 rgba(255,255,255,0.50),inset 0 -6px 10px rgba(0,0,0,0.24),0 4px 10px rgba(0,0,0,0.26),0 0 10px rgba(125,211,255,0.18);}";
  html += ".langbtn.secondary,.btn-secondary{background:" + theme.panelBg1 + ";color:" + theme.text + ";border:1px solid " + theme.accent + ";}";
  if (isPm3d) html += ".langbtn.secondary,.btn-secondary{background:linear-gradient(180deg,#132E52 0%,#09172A 100%);color:#EAF8FF;border:1px solid #7DD3FF;box-shadow:inset 0 1px 0 rgba(255,255,255,0.08),0 0 10px rgba(125,211,255,0.10);}";
  html += ".info{color:" + theme.info + ";font-weight:700;margin-top:10px;}";
  html += ".warn{color:" + theme.warn + ";font-weight:700;margin-top:10px;}";
  html += ".grid{display:grid;grid-template-columns:90px 1fr 60px;gap:6px;}";
  html += ".grid2{display:grid;grid-template-columns:1fr 1fr;gap:8px;}";
  html += ".theme-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:8px;}";
  html += "label{display:block;font-size:11px;color:" + theme.muted + ";margin-bottom:4px;}";
  html += "input,select{width:100%;box-sizing:border-box;padding:7px 8px;border-radius:10px;border:1px solid " + theme.accent + ";background:" + theme.inputBg + ";color:" + theme.text + ";}";
  if (isPm3d) html += "input,select{border-color:#7DD3FF;box-shadow:inset 0 1px 0 rgba(255,255,255,0.05),0 0 10px rgba(125,211,255,0.06);background:linear-gradient(180deg,#061120 0%,#020A14 100%);}input:focus,select:focus{outline:none;box-shadow:0 0 0 2px rgba(125,211,255,0.18),0 0 16px rgba(125,211,255,0.12);}";
  html += ".preview{background:" + theme.inputBg + ";border:1px solid " + theme.accent + ";border-radius:8px;padding:6px;}";
  if (isPm3d) html += ".preview{border-color:#7DD3FF;box-shadow:inset 0 0 20px rgba(73,194,255,0.05),0 0 12px rgba(125,211,255,0.08);}";
  html += ".previewhead{display:flex;justify-content:space-between;font-weight:700;margin-bottom:8px;}";
  html += ".previewline{height:1px;background:" + theme.text + ";margin-bottom:6px;opacity:0.8;}";
  html += ".bandw{background:" + theme.accent + ";color:" + theme.accentText + ";padding:4px 6px;margin-bottom:2px;font-weight:700;}";
  if (isPm3d) html += ".bandw{background:linear-gradient(180deg,#7FE0FF 0%,#49C2FF 50%,#0A67B8 100%);color:#03101C;border:1px solid #AEEBFF;box-shadow:inset 0 1px 0 rgba(255,255,255,0.55),0 0 10px rgba(73,194,255,0.10);}";
  html += ".bandb{background:" + theme.panelBg1 + ";color:" + theme.text + ";border:1px solid " + theme.accent + ";padding:4px 6px;margin-bottom:2px;font-weight:700;}";
  if (isPm3d) html += ".bandb{border-color:#7DD3FF;background:linear-gradient(180deg,#112744 0%,#07101D 100%);}";
  html += "hr{border:none;border-top:1px solid " + theme.accent + ";margin:14px 0;}";
  if (isPm3d) html += "hr{border-top-color:#7DD3FF;box-shadow:0 0 8px rgba(125,211,255,0.10);}";
  html += ".small{color:" + theme.muted + ";font-size:11px;}";
  html += ".banner{font-size:10px;color:" + theme.info + ";margin-bottom:8px;}";
  html += "a{color:" + theme.muted + ";}";
  html += ".toolbar{display:flex;gap:6px;align-items:center;flex-wrap:wrap;}";
  html += ".departureRow{display:grid;grid-template-columns:30px 82px 1fr 58px;gap:6px;align-items:end;margin-bottom:7px;}";
  html += ".rowNo{height:32px;display:flex;align-items:center;justify-content:center;border-radius:8px;background:" + theme.accent + ";color:" + theme.accentText + ";font-weight:800;}";

  html += ".sensorCards{display:grid;grid-template-columns:repeat(2,1fr);gap:6px;text-align:center;}.sensorCard{border:1px solid rgba(143,212,255,.28);border-radius:10px;padding:5px;background:linear-gradient(180deg,rgba(7,24,43,.82),rgba(0,0,0,.24));box-shadow:inset 0 1px 0 rgba(255,255,255,.08);}.dot{width:18px;height:18px;border-radius:50%;display:inline-block;border:2px solid white;box-shadow:0 0 8px rgba(0,0,0,.8);}.red{background:#d80000;box-shadow:0 0 12px rgba(216,0,0,.75);}.green{background:#00c853;box-shadow:0 0 14px rgba(0,200,83,.85);}";
  html += ".sensorPlan{position:relative;border:1px solid rgba(143,212,255,.28);border-radius:16px;padding:12px;padding-top:42px;background:radial-gradient(circle at 50% 0%,rgba(143,212,255,.10),rgba(0,0,0,.20));margin-top:12px;}.gearBtn{position:absolute;top:8px;right:8px;width:30px;height:30px;border-radius:50%;display:flex;align-items:center;justify-content:center;text-decoration:none;background:linear-gradient(180deg,#e9f8ff,#7dd3ff);color:#06111F!important;font-size:16px;font-weight:900;border:1px solid #fff;box-shadow:0 0 14px rgba(125,211,255,.45);}.pnScene{position:relative;height:150px;margin:12px 0;border-radius:16px;background:linear-gradient(180deg,#243321,#172216);overflow:hidden;border:1px solid rgba(255,255,255,.12);}.railTrack{position:absolute;left:5%;right:5%;height:46px;z-index:3;top:52px;}.railTrack:before,.railTrack:after{content:'';position:absolute;left:0;right:0;height:4px;background:linear-gradient(180deg,#f0f5f8,#7e8a92);border-radius:3px;box-shadow:0 1px 3px rgba(0,0,0,.7);}.railTrack:before{top:12px;}.railTrack:after{top:30px;}.sleepers{position:absolute;left:0;right:0;top:5px;height:36px;background:repeating-linear-gradient(90deg,transparent 0 12px,#5a3d25 12px 17px,transparent 17px 29px);opacity:.9;filter:drop-shadow(0 1px 1px rgba(0,0,0,.8));}";
  html += ".displayMini{position:absolute;z-index:6;left:50%;top:17px;transform:translateX(-50%);width:138px;height:82px;border-radius:8px;background:linear-gradient(180deg,#06233f,#010711);border:3px solid #8fd4ff;box-shadow:0 0 18px rgba(90,190,255,.65),inset 0 1px 0 rgba(255,255,255,.25);padding:5px;box-sizing:border-box;text-align:center;}.displayMiniTitle{color:#dff7ff;font-size:13px;font-weight:900;letter-spacing:1px;margin-bottom:4px;}.displayTrainChoice{display:grid;grid-template-columns:52px 1fr;gap:3px;align-items:center;margin-bottom:3px;color:#8fd4ff;font-size:11px;font-weight:800;}.displayTrainChoice select{font-size:11px;padding:3px 4px;border-radius:6px;}.displayMini:after{content:'';position:absolute;left:52px;right:52px;bottom:-19px;height:19px;background:linear-gradient(180deg,#34424d,#101820);border-radius:0 0 5px 5px;}";
  html += ".sensorPoint{position:absolute;z-index:7;width:60px;background:rgba(6,17,31,.78);border:1px solid rgba(143,212,255,.35);border-radius:10px;padding:4px;box-shadow:0 4px 12px rgba(0,0,0,.45);}.sensorLeft{left:3%;}.sensorRight{right:3%;}.sensorMid{top:51px;}.sensorPoint label{text-align:center;margin:0 0 3px 0;color:#8fd4ff;font-size:10px;text-shadow:0 1px 2px #000;}.sensorPoint select{font-size:11px;padding:4px;background:#06111F;border-radius:6px;}.trackTitle{text-align:center;font-weight:900;color:#ffffff;margin:8px 0 4px;font-size:14px;}.legendPN{text-align:center;font-size:11px;color:#B9D1DE;margin-top:4px;line-height:1.35;}.sensorParamGrid{display:grid;grid-template-columns:repeat(2,1fr);gap:10px;margin-top:10px;}.sensorParamCard{border:1px solid rgba(143,212,255,.28);border-radius:14px;padding:10px;background:linear-gradient(180deg,rgba(7,24,43,.84),rgba(0,0,0,.26));}.sensorParamTitle{font-weight:900;color:#8fd4ff;margin-bottom:8px;text-align:center;}.sensorTestGrid{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin-top:8px;}.sensorTestBtn{font-size:12px;padding:6px 8px;border-radius:10px;}";

  html += ".advModal{position:fixed;inset:0;background:rgba(0,0,0,.72);z-index:9999;display:none;align-items:center;justify-content:center;padding:16px;}.advModalBox{width:min(480px,94vw);border-radius:18px;background:linear-gradient(145deg,rgba(80,0,0,.98),rgba(18,0,0,.98));border:3px solid #ff1d1d;box-shadow:0 0 30px rgba(255,0,0,.75),inset 0 1px 0 rgba(255,255,255,.18);padding:16px;color:#fff;}.advModalTitle{font-size:18px;font-weight:900;margin-bottom:10px;display:flex;align-items:center;gap:10px;}.advIcon{width:34px;height:34px;border-radius:50%;background:#ff1d1d;color:#fff;display:inline-flex;align-items:center;justify-content:center;font-weight:1000;border:2px solid #fff;box-shadow:0 0 14px rgba(255,0,0,.75);}.advModalText{font-size:13px;line-height:1.45;margin-bottom:14px;}.advModalBtns{display:flex;gap:8px;flex-wrap:wrap;justify-content:flex-end;}.danger{background:linear-gradient(180deg,#ff6b6b,#9b0000)!important;color:#fff!important;border:1px solid #ffb0b0!important;}";
  html += "</style>";
  return html;
}


String makeLanguePage() {
  String html = "<!DOCTYPE html><html lang='fr'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>PM3D</title>";
  html += makeStyleBlock();
  html += "<script>function showAdvancedWarning(){document.getElementById('advModal').style.display='flex';return false;}function hideAdvancedWarning(){document.getElementById('advModal').style.display='none';}</script>";
  html += "</head><body><div class='container'>";
  html += "<div class='panel'>";
  html += "<div class='banner'>Produit par PM3D - visitez pm3d.net<br>Gemaakt door PM3D - bezoek pm3d.net</div>";
  html += "<div class='topbar'><div><div class='brand'>PM3D</div><div class='sub'>" + htmlEscape(currentLang == "FR" ? "Choix de la langue" : (currentLang == "NL" ? "Taalkeuze" : (currentLang == "DE" ? "Sprache waehlen" : "Language selection"))) + "</div></div><div class='badge'>FR / NL / DE / EN</div></div>";
  html += "<hr><h2 style='margin:0 0 12px 0;'>" + htmlEscape(currentLang == "FR" ? "Choisissez votre langue" : (currentLang == "NL" ? "Kies uw taal" : (currentLang == "DE" ? "Waehlen Sie Ihre Sprache" : "Choose your language"))) + "</h2>";
  html += "<h2 style='margin:0 0 18px 0;'>Kies uw taal</h2>";
  html += "<div class='langbox'>";
  html += "<a href='/setlang?lang=FR'><button class='langbtn'>Français</button></a>";
  html += "<a href='/setlang?lang=NL'><button class='langbtn secondary'>Nederlands</button></a>";
  html += "<a href='/setlang?lang=DE'><button class='langbtn secondary'>Deutsch</button></a>";
  html += "<a href='/setlang?lang=EN'><button class='langbtn secondary'>English</button></a>";
  html += "</div></div></div></body></html>";
  return html;
}


String firmwareUpdateButtonHtml() {
  String html;
  html += "<div class='panel' style='text-align:center;'>";
  html += "<strong>" + htmlEscape(currentLang == "NL" ? "Onderhoud" : "Maintenance") + "</strong><hr>";
  html += "<div style='margin-top:12px;'><a href='/update'><button type='button'>" + htmlEscape(currentLang == "FR" ? "Mise a jour firmware" : (currentLang == "NL" ? "Firmware-update" : (currentLang == "DE" ? "Firmware-Aktualisierung" : "Firmware update"))) + "</button></a></div>";
  html += "</div>";
  return html;
}

String firmwareUpdatePageHtml(const String& message) {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += makeStyleBlock();
  html += "</head><body><div class='container'>";

  html += "<div class='topbar'><div><div class='brand'>PM3D - " + htmlEscape(currentLang == "FR" ? "Mise a jour firmware" : (currentLang == "NL" ? "Firmware-update" : (currentLang == "DE" ? "Firmware-Aktualisierung" : "Firmware update"))) + "</div></div>";
  html += "<div style='display:flex; gap:6px; align-items:center; flex-wrap:wrap;'><div class='badge'>" + htmlEscape(buildVersionLabel()) + "</div><a href='/main'><button type='button'>" + htmlEscape(currentLang == "FR" ? "Retour" : (currentLang == "NL" ? "Terug" : (currentLang == "DE" ? "Zurueck" : "Back"))) + "</button></a></div></div>";
  if (message.length() > 0) html += "<div class='info'>" + htmlEscape(message) + "</div>";

  html += "<div class='small'>" + htmlEscape(currentLang == "NL" ? "Verbind hier met een internet-wifi om later firmware-updates te kunnen uitvoeren." : "Connectez-vous ici a un Wi-Fi Internet pour pouvoir effectuer les futures mises a jour.") + "</div>";

  html += "<div class='panel'><strong>" + htmlEscape(currentLang == "FR" ? "Mise a jour automatique" : (currentLang == "NL" ? "Automatische update" : (currentLang == "DE" ? "Automatische Aktualisierung" : "Automatic update"))) + "</strong><hr>";
  html += "<div class='small'>" + htmlEscape(currentLang == "FR" ? "Version actuelle" : (currentLang == "NL" ? "Huidige versie" : (currentLang == "DE" ? "Aktuelle Version" : "Current version"))) + " : <b>" + htmlEscape(buildVersionLabel()) + "</b></div>";
  html += "<div class='small'>" + htmlEscape(currentLang == "NL" ? "Dernier firmware detecte" : "Derniere version detectee") + " : <b>" + htmlEscape(otaLastVersion.length() ? otaLastVersion : String("-")) + "</b></div>";
  html += "<div class='small'>" + htmlEscape(currentLang == "NL" ? "Beschikbare versies" : "Versions disponibles") + " : <b>" + htmlEscape(otaAvailableVersions.length() ? otaAvailableVersions : String(currentLang == "NL" ? "onbekend" : "inconnues")) + "</b></div>";
  html += "<div class='small'>" + htmlEscape(currentLang == "NL" ? "Resultaat" : "Resultat") + " : " + htmlEscape(otaLastStatus) + "</div>";
  html += "<div style='margin-top:12px; display:flex; gap:6px; flex-wrap:wrap;'><a href='/otacheck'><button type='button'>" + htmlEscape(currentLang == "NL" ? "Zoek naar update" : "Rechercher une mise a jour") + "</button></a></div>";
  html += otaButtonsHtml();
  html += "</div>";

  html += "<div class='panel'><strong>" + htmlEscape(currentLang == "NL" ? "Internet-wifi voor firmware-update" : "Wi-Fi Internet pour mise a jour") + "</strong><hr>";
  html += "<div class='small'>" + htmlEscape(currentLang == "NL" ? "Opgeslagen netwerk" : "Reseau enregistre") + " : <b>" + (otaSSID.length() ? htmlEscape(otaSSID) : String(currentLang == "NL" ? "geen" : "aucun")) + "</b></div>";
  html += "<div class='small'>" + htmlEscape(currentLang == "NL" ? "Status" : "Etat") + " : " + htmlEscape(otaWifiStatus) + "</div>";
  html += "<div style='margin-top:12px; display:flex; gap:6px; flex-wrap:wrap;'><a href='/otascan'><button type='button'>" + htmlEscape(currentLang == "NL" ? "Wifi zoeken" : "Rechercher un Wi-Fi") + "</button></a><a href='/clearotawifi'><button type='button'>" + htmlEscape(currentLang == "NL" ? "Opgeslagen wifi wissen" : "Effacer le Wi-Fi enregistre") + "</button></a><a href='/reboot'><button type='button'>" + htmlEscape(currentLang == "NL" ? "Herstarten" : "Redemarrer") + "</button></a></div>";
  html += "</div>";


  html += "</div></body></html>";
  return html;
}

String languagePackButtonsHtml() {
  String html = "";
  html += "<div class='toolbar'>";
  html += "<span class='small'>" + htmlEscape(t_lang_pack()) + " :</span>";
  html += "<a href='/setlang?lang=FR'><button type='button' class='" + String(currentLang == "FR" ? "" : "btn-secondary") + "'>FR</button></a>";
  html += "<a href='/setlang?lang=NL'><button type='button' class='" + String(currentLang == "NL" ? "" : "btn-secondary") + "'>NL</button></a>";
  html += "<a href='/setlang?lang=DE'><button type='button' class='" + String(currentLang == "DE" ? "" : "btn-secondary") + "'>DE</button></a>";
  html += "<a href='/setlang?lang=EN'><button type='button' class='" + String(currentLang == "EN" ? "" : "btn-secondary") + "'>EN</button></a>";
  html += "</div>";
  return html;
}

String makeMainPage(const String& message) {
  String html = "<!DOCTYPE html><html lang='fr'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>PM3D</title>";
  html += makeStyleBlock();
  html += "<script>function showAdvancedWarning(){document.getElementById(\'advModal\').style.display=\'flex\';return false;}function hideAdvancedWarning(){document.getElementById(\'advModal\').style.display=\'none\';}</script>";
  html += "</head><body><div class='container'>";

  html += "<div class='panel'>";
  html += "<div class='banner'>" + htmlEscape(t_banner()) + "</div>";
  html += "<div class='topbar'><div><div class='brand'>PM3D - " + htmlEscape(t_title()) + "</div><div class='sub'>" + htmlEscape(t_small()) + "</div></div>";
  html += "<div class='badge'>" + htmlEscape(currentLang) + "</div></div>";
  if (message.length() > 0) html += "<div class='info'>" + htmlEscape(message) + "</div>";
  if (!displayOk) html += "<div class='warn'>OLED non detecte. Verifie le cablage puis redemarre.</div>";
  html += "<hr>";
  html += "<div class='toolbar'><a href='/settings'><button type='button'>" + htmlEscape(t_big_settings()) + "</button></a><a href='/advanced' onclick='return showAdvancedWarning()'><button type='button' class='btn-secondary danger'>" + htmlEscape(currentLang == "FR" ? "Reglages avances" : (currentLang == "NL" ? "Geavanceerd" : (currentLang == "DE" ? "Erweitert" : "Advanced"))) + "</button></a><a class='paletteBtn' href='/theme' title='" + htmlEscape(t_theme()) + "'>&#127912;</a></div>";
  html += "</div>";

  html += "<form method='POST' action='/save'>";
  html += "<div class='panel'>";
  html += "<div class='topbar'><strong>" + htmlEscape(t_title()) + "</strong>" + languagePackButtonsHtml() + "</div><hr>";

  for (int i = 0; i < MAX_TRAINS; i++) {
    html += "<div class='departureRow'>";
    html += "<div class='rowNo'>" + String(i + 1) + "</div>";
    html += "<div><label>" + htmlEscape(t_hour()) + "</label><input name='h" + String(i) + "' maxlength='5' placeholder='08:12' value='" + htmlEscape(trains[i].heure) + "'></div>";
    html += "<div><label>" + htmlEscape(t_dest()) + "</label><input name='d" + String(i) + "' maxlength='18' placeholder='" + htmlEscape(currentLang == "NL" ? "Brussel" : (currentLang == "DE" ? "Bruessel" : (currentLang == "EN" ? "Brussels" : "Bruxelles"))) + "' value='" + htmlEscape(trains[i].destination) + "'></div>";
    html += "<div><label>" + htmlEscape(t_track_label()) + "</label><input name='v" + String(i) + "' maxlength='2' placeholder='1' value='" + htmlEscape(trains[i].voie) + "'></div>";
    html += "</div>";
  }

  html += "<div class='tableSettingsRow'><a class='tableGearBtn' href='/configmenu' title='Configuration'>&#9881;</a></div>";
  html += "<hr><button type='submit'>" + htmlEscape(t_save()) + "</button>";
  html += "</div></form>";

  html += advancedWarningModalHtml();
  html += "</div></body></html>";
  return html;
}

String makeParamètresPage(const String& message) {
  String html = "<!DOCTYPE html><html lang='fr'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>PM3D</title>";
  html += makeStyleBlock();
  html += "</head><body><div class='container'>";
  html += "<div class='panel'><div class='topbar'><div><div class='brand'>PM3D - " + htmlEscape(t_big_settings()) + "</div></div><a href='/main'><button type='button'>" + htmlEscape(t_back()) + "</button></a></div>";
  if (message.length() > 0) html += "<div class='info'>" + htmlEscape(message) + "</div>";
  html += "</div>";

  html += "<div class='panel'><div class='topbar'><strong>" + htmlEscape(currentLang == "FR" ? "Ajouter mdp WiFi" : (currentLang == "NL" ? "WiFi-wachtwoord toevoegen" : (currentLang == "DE" ? "WiFi-Passwort hinzufuegen" : "Add WiFi password"))) + "</strong><a class='paletteBtn' href='/theme' title='" + htmlEscape(t_theme()) + "'>&#127912;</a></div><hr>";
  html += "<form method='POST' action='/savedevice'>";
  html += "<div class='small' style='margin-bottom:10px;'>WiFi local : " + htmlEscape(String(AP_SSID)) + "</div>";
  html += "<label style='margin-top:10px'>" + htmlEscape(t_password()) + " WiFi local</label>";
  html += "<input name='appass' type='text' minlength='0' maxlength='63' value='" + htmlEscapeAttr(apPasswordDisplay) + "' placeholder='" + htmlEscape(currentLang == "FR" ? "Vide = reseau ouvert, ou 8 caracteres minimum" : (currentLang == "NL" ? "Leeg = open netwerk, of minimum 8 tekens" : (currentLang == "DE" ? "Leer = offenes Netzwerk, oder mindestens 8 Zeichen" : "Empty = open network, or minimum 8 characters"))) + "'>";
  html += "<div class='small'>" + htmlEscape(currentLang == "FR" ? "Laisser vide pour supprimer le mot de passe. Le nom du WiFi local et l'adresse ne se changent pas ici." : (currentLang == "NL" ? "Laat leeg om het wachtwoord te verwijderen. De lokale WiFi-naam en het adres worden hier niet gewijzigd." : (currentLang == "DE" ? "Leer lassen, um das Passwort zu entfernen. Der lokale WiFi-Name und die Adresse werden hier nicht geaendert." : "Leave empty to remove the password. The local WiFi name and address are not changed here."))) + "</div>";
  html += "<div style='margin-top:12px;'><button type='submit'>" + htmlEscape(currentLang == "NL" ? "Opslaan en herstarten" : (currentLang == "DE" ? "Speichern und neu starten" : (currentLang == "EN" ? "Save and restart" : "Enregistrer et redemarrer"))) + "</button></div></form></div>";

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
  html += "<div style='margin-top:12px;'><button type='submit'>" + htmlEscape(t_save()) + "</button></div></form></div>";

  html += "<div class='panel'><form method='POST' action='/savesensorpos'><div class='section'>" + htmlEscape(currentLang == "FR" ? "Position des sensors" : (currentLang == "NL" ? "Positie van de sensoren" : (currentLang == "DE" ? "Position der Sensoren" : "Sensor position"))) + "</div>";
  html += "<div class='sensorPlan'>";
  html += "<a class='gearBtn' href='/sensorparams' title='Settings'>&#9881;</a>";
  html += "<div class='small'>" + htmlEscape(currentLang == "FR" ? "Choisissez quel sensor est place a gauche ou a droite. Le choix de la ligne du tableau des departs 1 a 10 se fait directement sur le petit ecran." : (currentLang == "NL" ? "Kies welke sensor links of rechts staat. De keuze van vertrekregel 1 tot 10 gebeurt op het kleine scherm." : (currentLang == "DE" ? "Waehlen Sie, welcher Sensor links oder rechts steht. Die Wahl der Abfahrtszeile 1 bis 10 erfolgt direkt auf dem kleinen Bildschirm." : "Choose which sensor is on the left or right. Departure-board row choice 1 to 10 is set directly on the small screen."))) + "</div>";
  html += "<div class='trackTitle'>" + htmlEscape(currentLang == "FR" ? "Display au centre - Sensors autour de la voie" : (currentLang == "NL" ? "Display in het midden - sensoren rond het spoor" : (currentLang == "DE" ? "Display in der Mitte - Sensoren am Gleis" : "Display in the center - sensors around the track"))) + "</div>";
  html += "<div class='pnScene'>";
  html += "<div class='railTrack'><div class='sleepers'></div></div>";
  html += "<div class='displayMini'><div class='displayMiniTitle'>" + htmlEscape(currentLang == "FR" ? "Ecran" : (currentLang == "NL" ? "Scherm" : (currentLang == "DE" ? "Bildschirm" : "Screen"))) + "</div>";
  for (int s = 0; s < NB_SENSORS_MAX_DISPLAY; s++) {
    html += "<div class='displayTrainChoice'><span>" + htmlEscape(currentLang == "FR" ? "Ligne S" : (currentLang == "NL" ? "Regel S" : (currentLang == "DE" ? "Zeile S" : "Line S"))) + String(s + 1) + "</span><select name='senTrain" + String(s) + "'>";
    for (int t = 0; t < MAX_TRAINS; t++) {
      html += "<option value='" + String(t) + "'" + String(sensorTrainChoiceDisplay[s] == t ? " selected" : "") + ">" + String(t + 1) + "</option>";
    }
    html += "</select></div>";
  }
  html += "</div>";
  html += "<div class='sensorPoint sensorLeft sensorMid'><label>" + htmlEscape(currentLang == "FR" ? "Gauche" : (currentLang == "NL" ? "Links" : (currentLang == "DE" ? "Links" : "Left"))) + "</label><select name='senPos0'>";
  for (int s = 0; s < NB_SENSORS_MAX_DISPLAY; s++) {
    html += "<option value='" + String(s) + "'" + String(sensorPositionChoiceDisplay[0] == s ? " selected" : "") + ">S" + String(s + 1) + "</option>";
  }
  html += "</select></div>";
  html += "<div class='sensorPoint sensorRight sensorMid'><label>" + htmlEscape(currentLang == "FR" ? "Droite" : (currentLang == "NL" ? "Rechts" : (currentLang == "DE" ? "Rechts" : "Right"))) + "</label><select name='senPos1'>";
  for (int s = 0; s < NB_SENSORS_MAX_DISPLAY; s++) {
    html += "<option value='" + String(s) + "'" + String(sensorPositionChoiceDisplay[1] == s ? " selected" : "") + ">S" + String(s + 1) + "</option>";
  }
  html += "</select></div>";
  html += "</div>";
  html += "<div class='sensorCards' style='margin-top:8px;'>";
  for (int i = 0; i < NB_SENSORS_MAX_DISPLAY; i++) {
    bool sensorLiveDetectedDisplay = ((long)(millis() - sensorDetectedUntilMsDisplay[i]) < 0);
    String dotClass = sensorLiveDetectedDisplay ? "dot green" : "dot red";
    html += "<div class='sensorCard'><div>S" + String(i + 1) + "</div><span id='sd" + String(i) + "' class='" + dotClass + "'></span></div>";
  }
  html += "</div>";

  html += "<div class='legendPN'>" + htmlEscape(currentLang == "FR" ? "Quand un sensor detecte le train, le Display affichera la ligne choisie du tableau des departs, puis reviendra a l'affichage normal selon le temps regle dans les parametres." : (currentLang == "NL" ? "Wanneer een sensor de trein detecteert, toont het Display de gekozen regel van het vertrektableau en keert daarna terug naar normaal volgens de ingestelde tijd." : (currentLang == "DE" ? "Wenn ein Sensor den Zug erkennt, zeigt das Display die gewaehlte Zeile der Abfahrtstafel an und kehrt nach der eingestellten Zeit zur normalen Anzeige zurueck." : "When a sensor detects the train, the Display shows the selected departure-board row and then returns to normal after the configured time."))) + "</div>";
  html += "</div>";
  html += "<hr><button type='submit'>" + htmlEscape(t_save()) + "</button></form></div>";

  html += "<script>function updateSensorDots(){fetch('/sensorstatus').then(function(r){return r.json();}).then(function(j){for(var i=0;i<j.sensors.length;i++){var e=document.getElementById('sd'+i);if(e){e.className=j.sensors[i].active?'dot green':'dot red';}var c=document.getElementById('sc'+i);if(c){c.className=j.sensors[i].connected?'badge':'badge danger';c.textContent=j.sensors[i].connected?'OK':'NON OK';}}}).catch(function(){});}function testSensorReception(slot){var e=document.getElementById('sd'+slot);if(e){e.className='dot green';}fetch('/testsensor?slot='+slot+'&ajax=1').then(function(r){return r.json();}).then(function(j){updateSensorDots();}).catch(function(){updateSensorDots();});return false;}setInterval(updateSensorDots,500);</script>";
  html += "</div></body></html>";
  return html;
}


String advancedPage() {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Reglages avances</title>";
  html += makeStyleBlock();
  html += "<style>.advWarn{background:linear-gradient(180deg,rgba(110,0,0,.92),rgba(45,0,0,.94));border:3px solid #ff1d1d;border-radius:14px;padding:12px;font-weight:900;color:#fff;line-height:1.45;box-shadow:0 0 18px rgba(255,0,0,.65),inset 0 1px 0 rgba(255,255,255,.18);animation:warnPulse 1.05s infinite;}@keyframes warnPulse{0%,100%{border-color:#ff1d1d;box-shadow:0 0 10px rgba(255,0,0,.45),inset 0 1px 0 rgba(255,255,255,.18);}50%{border-color:#ffffff;box-shadow:0 0 26px rgba(255,0,0,.95),0 0 8px rgba(255,255,255,.75),inset 0 1px 0 rgba(255,255,255,.25);}}.advCard{margin-top:12px;background:linear-gradient(135deg,rgba(15,39,62,.95),rgba(44,50,58,.92));border:1px solid rgba(143,212,255,.35);border-radius:14px;padding:12px;box-shadow:inset 0 1px 0 rgba(255,255,255,.12),0 0 16px rgba(49,154,255,.18);}.advSection{font-size:13px;font-weight:900;color:" + theme.info + ";text-transform:uppercase;margin:4px 0 9px;text-align:left;letter-spacing:.4px;}.advGrid{display:grid;grid-template-columns:1fr 1fr;gap:8px;}.sensorLine{display:grid;grid-template-columns:60px 1fr 1fr 86px;gap:6px;align-items:end;margin-bottom:8px;}.cgSub{border:1px solid rgba(143,212,255,.25);border-radius:16px;padding:11px;background:linear-gradient(180deg,rgba(8,32,58,.72),rgba(0,0,0,.20));margin:11px 0;text-align:left;box-shadow:inset 0 1px 0 rgba(255,255,255,.08);}.cgSection{font-size:13px;font-weight:900;color:" + theme.info + ";text-transform:uppercase;margin:12px 0 7px;text-align:left;letter-spacing:.4px;border-top:1px solid rgba(143,212,255,.14);padding-top:9px;}.cgGrid{display:grid;grid-template-columns:1fr 110px;gap:7px 9px;align-items:center;}.cgHint{font-size:12px;color:" + theme.muted + ";margin:7px 0;line-height:1.45;}.cgBtn{display:block;text-align:center;text-decoration:none;border-radius:12px;background:" + theme.accent + ";color:" + theme.accentText + ";font-weight:800;padding:9px 10px;}.cgSearchResult{border:1px solid rgba(143,212,255,.25);border-radius:16px;padding:11px;background:linear-gradient(180deg,rgba(8,32,58,.72),rgba(0,0,0,.20));margin:11px 0;text-align:left;box-shadow:inset 0 1px 0 rgba(255,255,255,.08);}@media(max-width:620px){.advGrid,.sensorLine,.cgGrid{grid-template-columns:1fr;}}</style>";
  html += "</head><body><div class='container'>";
  html += "<div class='panel'><div class='topbar'><div><div class='brand'>PM3D - " + htmlEscape(currentLang == "FR" ? "Reglages avances" : (currentLang == "NL" ? "Geavanceerde instellingen" : (currentLang == "DE" ? "Erweiterte Einstellungen" : "Advanced settings"))) + "</div></div><a href='/main'><button type='button'>" + htmlEscape(t_back()) + "</button></a></div></div>";
  html += "<div class='panel'><div class='advWarn'><b>" + htmlEscape(currentLang == "FR" ? "Attention - reglages techniques" : (currentLang == "NL" ? "Opgelet - technische instellingen" : (currentLang == "DE" ? "Achtung - technische Einstellungen" : "Warning - technical settings"))) + "</b><br>" + htmlEscape(currentLang == "FR" ? "Cette zone permet d'associer les sensors au Display. Elle est destinée à l'installation initiale ou à une intervention de maintenance." : (currentLang == "NL" ? "In deze zone koppelt u sensoren aan het Display. Ze is bedoeld voor eerste installatie of onderhoud." : (currentLang == "DE" ? "In diesem Bereich werden Sensoren mit dem Display gekoppelt. Er ist für Erstinstallation oder Wartung vorgesehen." : "This area links sensors to the Display. It is intended for initial setup or maintenance."))) + "</div>";
  html += "<div class='advCard'><div class='advSection'>Sensors</div>";
  html += "<form method='POST' action='/savesensors'>";
  html += "<div class='cgSub'><div class='cgSection'>Sensors</div>";
  html += "<div class='cgHint'>" + htmlEscape(currentLang == "FR" ? "Recherche et association des sensors PM3D, presentation identique au Crossing Gate." : (currentLang == "NL" ? "PM3D-sensoren zoeken en koppelen, met dezelfde presentatie als Crossing Gate." : (currentLang == "DE" ? "PM3D-Sensoren suchen und verbinden, mit derselben Darstellung wie Crossing Gate." : "Search and pairing of PM3D sensors, using the same presentation as Crossing Gate."))) + "</div>";
  for (int i = 0; i < NB_SENSORS_MAX_DISPLAY; i++) {
    bool sensorConnectedDisplay = (sensorLastSeenMsDisplay[i] > 0 && millis() - sensorLastSeenMsDisplay[i] < SENSOR_CONNECTED_TIMEOUT_MS);
    String state = sensorConnectedDisplay ? "OK" : (currentLang == "FR" ? "NON OK" : (currentLang == "NL" ? "NIET OK" : (currentLang == "DE" ? "NICHT OK" : "NOT OK")));
    String badgeClass = sensorConnectedDisplay ? "badge" : "badge danger";
    html += "<label>Sensor " + String(i + 1) + " <span id='sc" + String(i) + "' class='" + badgeClass + "'>" + htmlEscape(state) + "</span></label>";
    html += "<div class='cgGrid'><input name='sen" + String(i) + "' maxlength='17' value='" + htmlEscape(macSensorDisplay[i]) + "' placeholder='AA:BB:CC:DD:EE:FF'>";
    html += "<input type='hidden' name='senssid" + String(i) + "' value='" + htmlEscape(ssidSensorDisplay[i]) + "'>";
    html += "<a class='cgBtn' href='/searchsensor?slot=" + String(i) + "'>" + htmlEscape(currentLang == "FR" ? "Rechercher" : (currentLang == "NL" ? "Zoeken" : (currentLang == "DE" ? "Suchen" : "Search"))) + "</a></div>";
    if (ssidSensorDisplay[i].length()) html += "<div class='cgHint'>SSID : " + htmlEscape(ssidSensorDisplay[i]) + "</div>";
  }
  html += "</div>";
  html += "<div class='cgSection'>" + htmlEscape(currentLang == "FR" ? "Reception declenchement sensors" : (currentLang == "NL" ? "Ontvangst sensortrigger" : (currentLang == "DE" ? "Sensor-Ausloesung empfangen" : "Sensor trigger reception"))) + "</div>";
  html += "<div class='cgHint'>" + htmlEscape(currentLang == "FR" ? "OK indique que le sensor est ajoute. Les voyants rouge/vert indiquent uniquement si un ordre de declenchement via sensor est recu." : (currentLang == "NL" ? "OK betekent dat de sensor is toegevoegd. De rood/groene lampjes tonen alleen of een sensor-triggeropdracht ontvangen wordt." : (currentLang == "DE" ? "OK bedeutet, dass der Sensor hinzugefuegt wurde. Die rot/gruenen Anzeigen zeigen nur, ob ein Sensor-Ausloesebefehl empfangen wird." : "OK means the sensor is added. The red/green indicators only show whether a sensor trigger command is received."))) + "</div>";
  html += "<div class='sensorCards'>";
  for (int i = 0; i < NB_SENSORS_MAX_DISPLAY; i++) {
    bool sensorLiveDetectedDisplay = ((long)(millis() - sensorDetectedUntilMsDisplay[i]) < 0);
    String dotClass = sensorLiveDetectedDisplay ? "dot green" : "dot red";
    html += "<div class='sensorCard'><div>S" + String(i + 1) + "</div><span id='sd" + String(i) + "' class='" + dotClass + "'></span></div>";
  }
  html += "</div>";

  html += "<div class='advGrid' style='margin-top:8px;'><button type='submit'>" + htmlEscape(t_save()) + "</button><a href='/clearsensors' onclick=\"return confirm('" + htmlEscape(currentLang == "FR" ? "Confirmer la suppression des sensors enregistres ?" : (currentLang == "NL" ? "Opgeslagen sensoren verwijderen?" : (currentLang == "DE" ? "Gespeicherte Sensoren loeschen?" : "Clear saved sensors?"))) + "');\"><button class='danger' type='button'>" + htmlEscape(currentLang == "FR" ? "Vider sensors" : (currentLang == "NL" ? "Sensoren wissen" : (currentLang == "DE" ? "Sensoren leeren" : "Clear sensors"))) + "</button></a></div>";
  html += "</form></div>";

  html += "<div class='advCard'><div class='advSection'>" + htmlEscape(currentLang == "FR" ? "Recherche mise a jour firmware" : (currentLang == "NL" ? "Firmware-update zoeken" : (currentLang == "DE" ? "Firmware-Update suchen" : "Firmware update search"))) + "</div>";
  html += "<div class='small'>" + htmlEscape(currentLang == "FR" ? "La mise a jour depuis GitHub reste geree par la page existante, sans changement de mecanique." : (currentLang == "NL" ? "De update via GitHub blijft beheerd door de bestaande pagina, zonder mechanische wijziging." : (currentLang == "DE" ? "Das Update ueber GitHub bleibt durch die bestehende Seite verwaltet, ohne Aenderung der Mechanik." : "GitHub update remains handled by the existing page, with no mechanical change."))) + "</div>";
  html += "<div class='advGrid' style='margin-top:8px;'><a href='/update'><button type='button'>" + htmlEscape(currentLang == "FR" ? "Mise a jour" : (currentLang == "NL" ? "Update" : (currentLang == "DE" ? "Aktualisierung" : "Update"))) + "</button></a></div>";
  html += "<script>function updateSensorDots(){fetch('/sensorstatus').then(function(r){return r.json();}).then(function(j){for(var i=0;i<j.sensors.length;i++){var e=document.getElementById('sd'+i);if(e){e.className=j.sensors[i].active?'dot green':'dot red';}var c=document.getElementById('sc'+i);if(c){c.className=j.sensors[i].connected?'badge':'badge danger';c.textContent=j.sensors[i].connected?'OK':'NON OK';}}}).catch(function(){});}function testSensorReception(slot){var e=document.getElementById('sd'+slot);if(e){e.className='dot green';}fetch('/testsensor?slot='+slot+'&ajax=1').then(function(r){return r.json();}).then(function(j){updateSensorDots();}).catch(function(){updateSensorDots();});return false;}setInterval(updateSensorDots,500);</script>";
  html += "</div></div></div></body></html>";
  return html;
}

void handleIntro() { server.send(200, "text/html; charset=utf-8", introPage()); }
void handleAdvanced() { server.send(200, "text/html; charset=utf-8", advancedPage()); }

void handleSaveSensorsDisplay() {
  for (int i = 0; i < NB_SENSORS_MAX_DISPLAY; i++) {
    if (server.hasArg("sen" + String(i))) macSensorDisplay[i] = server.arg("sen" + String(i));
    if (server.hasArg("senssid" + String(i))) ssidSensorDisplay[i] = server.arg("senssid" + String(i));
    if (server.hasArg("senTrain" + String(i))) {
      sensorTrainChoiceDisplay[i] = server.arg("senTrain" + String(i)).toInt();
      if (sensorTrainChoiceDisplay[i] < 0) sensorTrainChoiceDisplay[i] = 0;
      if (sensorTrainChoiceDisplay[i] >= MAX_TRAINS) sensorTrainChoiceDisplay[i] = MAX_TRAINS - 1;
    }
    if (server.hasArg("senPos" + String(i))) {
      sensorPositionChoiceDisplay[i] = server.arg("senPos" + String(i)).toInt();
      if (sensorPositionChoiceDisplay[i] < 0) sensorPositionChoiceDisplay[i] = 0;
      if (sensorPositionChoiceDisplay[i] >= NB_SENSORS_MAX_DISPLAY) sensorPositionChoiceDisplay[i] = NB_SENSORS_MAX_DISPLAY - 1;
    }
    macSensorDisplay[i].trim();
    ssidSensorDisplay[i].trim();

    if (macSensorDisplay[i].length() > 0) {
      sensorDetectionEnabledDisplay[i] = true;
      if (sensorTriggerModeBySlotDisplay[i] != "sensor") sensorTriggerModeBySlotDisplay[i] = "sensor";
      sensorNextAutoTriggerMsDisplay[i] = 0;
      sensorKnownPresentDisplay[i] = false;
      sensorLastSeenMsDisplay[i] = 0;
      sensorLastRealTriggerMsDisplay[i] = 0;
    }
  }
  saveConfig();
  server.sendHeader("Location", "/advanced");
  server.send(303, "text/plain", "");
}

void handleClearSensorsDisplay() {
  for (int i = 0; i < NB_SENSORS_MAX_DISPLAY; i++) {
    macSensorDisplay[i] = "";
    ssidSensorDisplay[i] = "";
    sensorTrainChoiceDisplay[i] = i;
    sensorPositionChoiceDisplay[i] = i;
    sensorDetectionEnabledDisplay[i] = true;
    sensorArrowSideBySlotDisplay[i] = (i == 0 ? "left" : "right");
    sensorArrowBlinkBySlotDisplay[i] = true;
    sensorArrowBlinkDurationSecBySlotDisplay[i] = 3;
    sensorReturnNormalSecBySlotDisplay[i] = 8;
    sensorTriggerModeBySlotDisplay[i] = "auto";
    sensorAutoEverySecBySlotDisplay[i] = 30;
    sensorAutoShowSecBySlotDisplay[i] = 8;
    sensorDestinationTextSizeBySlotDisplay[i] = 1;
    sensorHeaderBlinkBySlotDisplay[i] = true;
    sensorNextAutoTriggerMsDisplay[i] = 0;
    sensorKnownPresentDisplay[i] = false;
    sensorLastSeenMsDisplay[i] = 0;
    sensorLastRealTriggerMsDisplay[i] = 0;
    sensorDetectedUntilMsDisplay[i] = 0;
  }
  saveConfig();
  server.sendHeader("Location", "/advanced");
  server.send(303, "text/plain", "");
}


String sensorParamsPageDisplay(const String& message) {
  String html = "<!DOCTYPE html><html lang='fr'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>PM3D Sensor Params</title>";
  html += makeStyleBlock();
  html += "</head><body><div class='container'><div class='panel'>";
  html += "<div class='topbar'><div><div class='brand'>" + htmlEscape(currentLang == "FR" ? "Parametres sensor Display" : (currentLang == "NL" ? "Sensorinstellingen Display" : (currentLang == "DE" ? "Sensor-Einstellungen Display" : "Display sensor settings"))) + "</div><div class='sub'>" + htmlEscape(currentLang == "FR" ? "Reglage separe des detections S1 et S2" : (currentLang == "NL" ? "Afzonderlijke instelling van detecties S1 en S2" : (currentLang == "DE" ? "Separate Einstellung der Erkennungen S1 und S2" : "Separate settings for S1 and S2 detections"))) + "</div></div><a href='/main'><button type='button'>" + htmlEscape(t_back()) + "</button></a></div>";
  if (message.length()) html += "<div class='banner'>" + htmlEscape(message) + "</div>";

  html += "<form method='POST' action='/savesensorparams'>";
  html += "<div class='section'>" + htmlEscape(currentLang == "FR" ? "Parametres de detection" : (currentLang == "NL" ? "Detectieparameters" : (currentLang == "DE" ? "Erkennungsparameter" : "Detection settings"))) + "</div>";
  html += "<div class='sensorParamGrid'>";

  for (int i = 0; i < NB_SENSORS_MAX_DISPLAY; i++) {
    html += "<div class='sensorParamCard'>";
    html += "<div class='sensorParamTitle'>" + htmlEscape(currentLang == "FR" ? "Detection S" : (currentLang == "NL" ? "Detectie S" : (currentLang == "DE" ? "Erkennung S" : "Detection S"))) + String(i + 1) + "</div>";

    html += "<label>" + htmlEscape(currentLang == "FR" ? "Activation" : (currentLang == "NL" ? "Activatie" : (currentLang == "DE" ? "Aktivierung" : "Activation"))) + "</label>";
    html += "<select name='detEn" + String(i) + "'>";
    html += "<option value='1'" + String(sensorDetectionEnabledDisplay[i] ? " selected" : "") + ">" + htmlEscape(currentLang == "FR" ? "Oui" : (currentLang == "NL" ? "Ja" : (currentLang == "DE" ? "Ja" : "Yes"))) + "</option>";
    html += "<option value='0'" + String(!sensorDetectionEnabledDisplay[i] ? " selected" : "") + ">" + htmlEscape(currentLang == "FR" ? "Non" : (currentLang == "NL" ? "Nee" : (currentLang == "DE" ? "Nein" : "No"))) + "</option>";
    html += "</select>";

    html += "<label>" + htmlEscape(currentLang == "FR" ? "Mode de declenchement" : (currentLang == "NL" ? "Trigger-modus" : (currentLang == "DE" ? "Ausloesemodus" : "Trigger mode"))) + "</label>";
    html += "<select name='trigMode" + String(i) + "'>";
    html += "<option value='auto'" + String(sensorTriggerModeBySlotDisplay[i] == "auto" ? " selected" : "") + ">" + htmlEscape(currentLang == "FR" ? "Automatique" : (currentLang == "NL" ? "Automatisch" : (currentLang == "DE" ? "Automatisch" : "Automatic"))) + "</option>";
    html += "<option value='sensor'" + String(sensorTriggerModeBySlotDisplay[i] == "sensor" ? " selected" : "") + ">Sensor</option>";
    html += "</select>";

    html += "<div class='grid2'>";
    html += "<div><label>" + htmlEscape(currentLang == "FR" ? "Auto toutes les X sec" : (currentLang == "NL" ? "Auto elke X sec" : (currentLang == "DE" ? "Auto alle X Sek." : "Auto every X sec"))) + "</label><input type='number' name='autoEvery" + String(i) + "' min='1' max='3600' value='" + String(sensorAutoEverySecBySlotDisplay[i]) + "'></div>";
    html += "<div><label>" + htmlEscape(currentLang == "FR" ? "Pendant X sec" : (currentLang == "NL" ? "Gedurende X sec" : (currentLang == "DE" ? "Waehrend X Sek." : "During X sec"))) + "</label><input type='number' name='autoShow" + String(i) + "' min='1' max='600' value='" + String(sensorAutoShowSecBySlotDisplay[i]) + "'></div>";
    html += "</div>";

    html += "<label>" + htmlEscape(currentLang == "FR" ? "Cote de la fleche" : (currentLang == "NL" ? "Kant van de pijl" : (currentLang == "DE" ? "Pfeilseite" : "Arrow side"))) + "</label>";
    html += "<select name='arrowSide" + String(i) + "'>";
    html += "<option value='left'" + String(sensorArrowSideBySlotDisplay[i] == "left" ? " selected" : "") + ">" + htmlEscape(currentLang == "FR" ? "Gauche" : (currentLang == "NL" ? "Links" : (currentLang == "DE" ? "Links" : "Left"))) + "</option>";
    html += "<option value='right'" + String(sensorArrowSideBySlotDisplay[i] == "right" ? " selected" : "") + ">" + htmlEscape(currentLang == "FR" ? "Droite" : (currentLang == "NL" ? "Rechts" : (currentLang == "DE" ? "Rechts" : "Right"))) + "</option>";
    html += "</select>";

    html += "<label style='display:flex;gap:8px;align-items:center;color:" + theme.text + ";margin-top:8px;'><input style='width:auto;' type='checkbox' name='arrowBlink" + String(i) + "' value='1'" + String(sensorArrowBlinkBySlotDisplay[i] ? " checked" : "") + "> " + htmlEscape(currentLang == "FR" ? "Faire clignoter la fleche" : (currentLang == "NL" ? "Pijl laten knipperen" : (currentLang == "DE" ? "Pfeil blinken lassen" : "Blink the arrow"))) + "</label>";

    html += "<label style='display:flex;gap:8px;align-items:center;color:" + theme.text + ";margin-top:8px;'><input style='width:auto;' type='checkbox' name='headBlink" + String(i) + "' value='1'" + String(sensorHeaderBlinkBySlotDisplay[i] ? " checked" : "") + "> " + htmlEscape(currentLang == "FR" ? "Faire clignoter le texte Train en approche" : (currentLang == "NL" ? "Tekst Trein in aantocht laten knipperen" : (currentLang == "DE" ? "Text Zug naehert blinkend anzeigen" : "Blink Train approaching text"))) + "</label>";

    html += "<label>" + htmlEscape(currentLang == "FR" ? "Vitesse du clignotement fleche" : (currentLang == "NL" ? "Knippersnelheid pijl" : (currentLang == "DE" ? "Blinkgeschwindigkeit Pfeil" : "Arrow blink speed"))) + "</label>";
    html += "<input type='number' name='blinkSec" + String(i) + "' min='1' max='60' value='" + String(sensorArrowBlinkDurationSecBySlotDisplay[i]) + "'>";

    html += "<label>" + htmlEscape(currentLang == "FR" ? "Taille police destination" : (currentLang == "NL" ? "Lettergrootte bestemming" : (currentLang == "DE" ? "Schriftgroesse Ziel" : "Destination font size"))) + "</label>";
    html += "<select name='destSize" + String(i) + "' onchange='this.form.submit()'>";
    html += "<option value='1'" + String(sensorDestinationTextSizeBySlotDisplay[i] == 1 ? " selected" : "") + ">" + htmlEscape(currentLang == "FR" ? "1 - petite" : (currentLang == "NL" ? "1 - klein" : (currentLang == "DE" ? "1 - klein" : "1 - small"))) + "</option>";
    html += "<option value='2'" + String(sensorDestinationTextSizeBySlotDisplay[i] == 2 ? " selected" : "") + ">" + htmlEscape(currentLang == "FR" ? "2 - moyenne" : (currentLang == "NL" ? "2 - middel" : (currentLang == "DE" ? "2 - mittel" : "2 - medium"))) + "</option>";
    html += "<option value='3'" + String(sensorDestinationTextSizeBySlotDisplay[i] == 3 ? " selected" : "") + ">" + htmlEscape(currentLang == "FR" ? "3 - grande" : (currentLang == "NL" ? "3 - groot" : (currentLang == "DE" ? "3 - gross" : "3 - large"))) + "</option>";
    html += "</select>";

    html += "<label>" + htmlEscape(currentLang == "FR" ? "Temps d'affichage avant retour normal" : (currentLang == "NL" ? "Weergavetijd voor terugkeer" : (currentLang == "DE" ? "Anzeigezeit bis Rueckkehr" : "Display time before return"))) + "</label>";
    html += "<input type='number' name='backSec" + String(i) + "' min='1' max='600' value='" + String(sensorReturnNormalSecBySlotDisplay[i]) + "'>";

    html += "</div>";
  }

  html += "</div>";
  html += "<hr><button type='submit'>" + htmlEscape(t_save()) + "</button>";
  html += "</form></div></div></body></html>";
  return html;
}

void handleSensorParamsDisplay() {
  server.send(200, "text/html; charset=utf-8", sensorParamsPageDisplay(""));
}

void handleSaveSensorParamsDisplay() {
  for (int i = 0; i < NB_SENSORS_MAX_DISPLAY; i++) {
    if (server.hasArg("detEn" + String(i))) sensorDetectionEnabledDisplay[i] = (server.arg("detEn" + String(i)) == "1");

    if (server.hasArg("trigMode" + String(i))) {
      sensorTriggerModeBySlotDisplay[i] = server.arg("trigMode" + String(i));
      if (sensorTriggerModeBySlotDisplay[i] != "auto" && sensorTriggerModeBySlotDisplay[i] != "sensor") sensorTriggerModeBySlotDisplay[i] = "auto";
    }

    if (server.hasArg("autoEvery" + String(i))) sensorAutoEverySecBySlotDisplay[i] = server.arg("autoEvery" + String(i)).toInt();
    if (sensorAutoEverySecBySlotDisplay[i] < 1) sensorAutoEverySecBySlotDisplay[i] = 1;
    if (sensorAutoEverySecBySlotDisplay[i] > 3600) sensorAutoEverySecBySlotDisplay[i] = 3600;

    if (server.hasArg("autoShow" + String(i))) sensorAutoShowSecBySlotDisplay[i] = server.arg("autoShow" + String(i)).toInt();
    if (sensorAutoShowSecBySlotDisplay[i] < 1) sensorAutoShowSecBySlotDisplay[i] = 1;
    if (sensorAutoShowSecBySlotDisplay[i] > 600) sensorAutoShowSecBySlotDisplay[i] = 600;

    if (server.hasArg("arrowSide" + String(i))) {
      sensorArrowSideBySlotDisplay[i] = server.arg("arrowSide" + String(i));
      if (sensorArrowSideBySlotDisplay[i] != "left" && sensorArrowSideBySlotDisplay[i] != "right") sensorArrowSideBySlotDisplay[i] = (i == 0 ? "left" : "right");
    }

    sensorArrowBlinkBySlotDisplay[i] = server.hasArg("arrowBlink" + String(i));
    sensorHeaderBlinkBySlotDisplay[i] = server.hasArg("headBlink" + String(i));

    if (server.hasArg("destSize" + String(i))) sensorDestinationTextSizeBySlotDisplay[i] = server.arg("destSize" + String(i)).toInt();
    if (sensorDestinationTextSizeBySlotDisplay[i] < 1) sensorDestinationTextSizeBySlotDisplay[i] = 1;
    if (sensorDestinationTextSizeBySlotDisplay[i] > 3) sensorDestinationTextSizeBySlotDisplay[i] = 3;

    if (server.hasArg("blinkSec" + String(i))) sensorArrowBlinkDurationSecBySlotDisplay[i] = server.arg("blinkSec" + String(i)).toInt();
    if (sensorArrowBlinkDurationSecBySlotDisplay[i] < 1) sensorArrowBlinkDurationSecBySlotDisplay[i] = 1;
    if (sensorArrowBlinkDurationSecBySlotDisplay[i] > 60) sensorArrowBlinkDurationSecBySlotDisplay[i] = 60;

    if (server.hasArg("backSec" + String(i))) sensorReturnNormalSecBySlotDisplay[i] = server.arg("backSec" + String(i)).toInt();
    if (sensorReturnNormalSecBySlotDisplay[i] < 1) sensorReturnNormalSecBySlotDisplay[i] = 1;
    if (sensorReturnNormalSecBySlotDisplay[i] > 600) sensorReturnNormalSecBySlotDisplay[i] = 600;

    sensorNextAutoTriggerMsDisplay[i] = millis() + sensorAutoEverySecBySlotDisplay[i] * 1000UL;
  }

  // Compatibilite avec l'ancien rendu global.
  sensorArrowSideDisplay = sensorArrowSideBySlotDisplay[0];
  sensorArrowBlinkDisplay = sensorArrowBlinkBySlotDisplay[0];
  sensorArrowBlinkSecDisplay = sensorArrowBlinkDurationSecBySlotDisplay[0];
  sensorReturnNormalSecDisplay = sensorReturnNormalSecBySlotDisplay[0];

  saveConfig();
  server.send(200, "text/html; charset=utf-8", sensorParamsPageDisplay(t_saved()));
}


void startSensorI2CTest(int slot) {
  if (slot < 0) slot = 0;
  if (slot >= NB_SENSORS_MAX_DISPLAY) slot = NB_SENSORS_MAX_DISPLAY - 1;

  sensorI2CTestSlot = slot;
  sensorI2CTestActive = true;
  sensorI2CTestStartMs = millis();
  sensorI2CLastBlinkMs = 0;
  sensorI2CArrowVisible = true;
  sensorI2CLastHeaderBlinkMs = 0;
  sensorI2CHeaderVisible = true;

  sensorArrowSideDisplay = sensorArrowSideBySlotDisplay[slot];
  sensorArrowBlinkDisplay = sensorArrowBlinkBySlotDisplay[slot];
  sensorArrowBlinkSecDisplay = sensorArrowBlinkDurationSecBySlotDisplay[slot];
  sensorReturnNormalSecDisplay = sensorReturnNormalSecBySlotDisplay[slot];

  showingCustomMessage = false;
  afficherSensorI2CTest();
}

void afficherSensorI2CTest() {
  if (!displayOk) return;

  int slot = sensorI2CTestSlot;
  if (slot < 0) slot = 0;
  if (slot >= NB_SENSORS_MAX_DISPLAY) slot = NB_SENSORS_MAX_DISPLAY - 1;

  int ligne = sensorTrainChoiceDisplay[slot];
  if (ligne < 0) ligne = 0;
  if (ligne >= MAX_TRAINS) ligne = MAX_TRAINS - 1;

  TrainItem t = trains[ligne];

  unsigned long now = millis();

  // La fleche et "Train en approche" restent presents pendant TOUTE la duree
  // de l'affichage sensor. Si le clignotement est coche, ils clignotent ensemble
  // pendant toute cette duree, sans s'arreter apres X secondes.
  bool arrowBlinkActive = sensorArrowBlinkBySlotDisplay[slot];
  bool headerBlinkActive = sensorHeaderBlinkBySlotDisplay[slot];

  bool syncedBlinkActive = arrowBlinkActive || headerBlinkActive;
  if (syncedBlinkActive) {
    const unsigned long syncedBlinkIntervalMs = 350UL;
    if (now - sensorI2CLastBlinkMs >= syncedBlinkIntervalMs) {
      sensorI2CLastBlinkMs = now;
      bool newVisibleState = !sensorI2CArrowVisible;
      sensorI2CArrowVisible = newVisibleState;
      sensorI2CHeaderVisible = newVisibleState;
    }
  } else {
    sensorI2CArrowVisible = true;
    sensorI2CHeaderVisible = true;
  }

  // Si le clignotement d'un element n'est pas coche, cet element reste affiche fixe.
  if (!arrowBlinkActive) sensorI2CArrowVisible = true;
  if (!headerBlinkActive) sensorI2CHeaderVisible = true;

  bool arrowLeft = (sensorArrowSideBySlotDisplay[slot] == "left");

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.drawLine(0, 14, SCREEN_WIDTH - 1, 14, SSD1306_WHITE);
  display.drawLine(0, 48, SCREEN_WIDTH - 1, 48, SSD1306_WHITE);

  // Bandeau haut : "Train en approche", clignotant par defaut.
  display.setTextSize(1);
  if (sensorI2CHeaderVisible) {
    String title = currentLang == "FR" ? "Train en approche" : (currentLang == "NL" ? "Trein nadert" : (currentLang == "DE" ? "Zug naehert" : "Train approaching"));
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    int tx = (SCREEN_WIDTH - w) / 2;
    if (tx < 2) tx = 2;
    display.setCursor(tx, 3);
    display.print(title);
  }

  // Fleche de detection : rendu symetrique droite/gauche.
  if (sensorI2CArrowVisible) {
    if (arrowLeft) {
      display.fillTriangle(4, 31, 16, 22, 16, 40, SSD1306_WHITE);
      display.drawLine(16, 31, 32, 31, SSD1306_WHITE);
    } else {
      display.fillTriangle(124, 31, 112, 22, 112, 40, SSD1306_WHITE);
      display.drawLine(96, 31, 112, 31, SSD1306_WHITE);
    }
  }

  // Le rendu droit et gauche utilisent la meme logique :
  // la fleche occupe un cote, les infos commencent juste apres/avant.
  if (arrowLeft) {
    // Fleche a gauche : voie pres de la fleche, heure a droite.
    display.setTextSize(1);
    display.setCursor(34, 22);
    display.print("VOIE");

    display.setTextSize(2);
    display.setCursor(36, 32);
    display.print(fitText(t.voie, 2));

    display.setTextSize(2);
    display.setCursor(64, 25);
    display.print(fitText(t.heure, 5));
  } else {
    // Fleche a droite : heure a gauche, voie pres de la fleche.
    display.setTextSize(2);
    display.setCursor(4, 25);
    display.print(fitText(t.heure, 5));

    display.setTextSize(1);
    display.setCursor(76, 22);
    display.print("VOIE");

    display.setTextSize(2);
    display.setCursor(80, 32);
    display.print(fitText(t.voie, 2));
  }

  // Destination tout en bas avec taille plus fine : 1, 2 ou 3.
  int destSize = sensorDestinationTextSizeBySlotDisplay[slot];
  if (destSize < 1) destSize = 1;
  if (destSize > 3) destSize = 3;

  display.setTextSize(destSize);
  int maxChars = 20;
  if (destSize == 2) maxChars = 10;
  if (destSize == 3) maxChars = 7;

  String dest = fitText(t.destination, maxChars);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(dest, 0, 0, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - w) / 2;
  if (x < 2) x = 2;

  int y = 54;
  if (destSize == 2) y = 50;
  if (destSize == 3) y = 43;

  display.setCursor(x, y);
  display.print(dest);

  display.display();
}


void handleTestSensorDisplay() {
  int slot = 0;
  if (server.hasArg("slot")) slot = server.arg("slot").toInt();
  if (slot < 0) slot = 0;
  if (slot >= NB_SENSORS_MAX_DISPLAY) slot = NB_SENSORS_MAX_DISPLAY - 1;

  sensorDetectionEnabledDisplay[slot] = true;
  sensorDetectedUntilMsDisplay[slot] = millis() + 5000UL;

  if (server.hasArg("ajax")) {
    server.send(200, "application/json; charset=utf-8", "{\"ok\":true,\"test\":true,\"sensor\":" + String(slot + 1) + "}");
    return;
  }

  server.send(200, "text/html; charset=utf-8", makeParamètresPage(""));
}



void maintainAutomaticSensorDisplay() {
  if (!displayOk) return;
  if (sensorI2CTestActive) return;

  unsigned long now = millis();
  for (int i = 0; i < NB_SENSORS_MAX_DISPLAY; i++) {
    if (!sensorDetectionEnabledDisplay[i]) continue;
    if (sensorTriggerModeBySlotDisplay[i] != "auto") continue;

    if (sensorNextAutoTriggerMsDisplay[i] == 0) {
      sensorNextAutoTriggerMsDisplay[i] = now + sensorAutoEverySecBySlotDisplay[i] * 1000UL;
    }

    if ((long)(now - sensorNextAutoTriggerMsDisplay[i]) >= 0) {
      startSensorI2CTest(i);
      sensorNextAutoTriggerMsDisplay[i] = now + sensorAutoEverySecBySlotDisplay[i] * 1000UL;
      return;
    }
  }
}

void maintainRegisteredSensorWifiDetection() {
  // Desactive volontairement : le scan Wi-Fi automatique surcharge le C3
  // et bloque le defilement des gares.
  // Les sensors doivent declencher le Display par ordre HTTP :
  // /sensortrigger?sensor=1, /sensortrigger?mac=AA:BB:CC:DD:EE:FF, /sensor?...
}


String sensorSearchPageDisplay(int slot) {
  if (slot < 0 || slot >= NB_SENSORS_MAX_DISPLAY) slot = 0;

  WiFi.mode(WIFI_AP_STA);
  int n = WiFi.scanNetworks(false, true);

  String html = "<!DOCTYPE html><html lang='fr'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += makeStyleBlock();
  html += "<style>.cgSub{border:1px solid rgba(143,212,255,.25);border-radius:16px;padding:11px;background:linear-gradient(180deg,rgba(8,32,58,.72),rgba(0,0,0,.20));margin:11px 0;text-align:left;box-shadow:inset 0 1px 0 rgba(255,255,255,.08);}.cgHint{font-size:12px;color:" + theme.muted + ";margin:7px 0;line-height:1.45;}.cgBtn{display:block;text-align:center;text-decoration:none;border-radius:12px;background:" + theme.accent + ";color:" + theme.accentText + ";font-weight:800;padding:9px 10px;margin-top:8px;}</style>";
  html += "</head><body><div class='container'><div class='panel'>";
  html += "<div class='topbar'><div><div class='brand'>PM3D - " + htmlEscape(currentLang == "FR" ? "Recherche PM3D" : (currentLang == "NL" ? "PM3D zoeken" : (currentLang == "DE" ? "PM3D-Suche" : "PM3D search"))) + "</div><div class='sub'>" + htmlEscape(currentLang == "FR" ? "Affectation vers Sensor " : (currentLang == "NL" ? "Koppeling naar Sensor " : (currentLang == "DE" ? "Zuweisung zu Sensor " : "Pairing to Sensor "))) + String(slot + 1) + "</div></div><a href='/advanced'><button type='button'>" + htmlEscape(t_back()) + "</button></a></div>";

  bool found = false;
  if (n <= 0) {
    html += "<div class='cgHint'>" + htmlEscape(currentLang == "FR" ? "Aucun reseau detecte." : (currentLang == "NL" ? "Geen netwerk gedetecteerd." : (currentLang == "DE" ? "Kein Netzwerk erkannt." : "No network detected."))) + "</div>";
  } else {
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;
      String ssidUpper = ssid;
      ssidUpper.toUpperCase();
      if (!ssidUpper.startsWith("PM3D")) continue;

      found = true;
      String mac = WiFi.BSSIDstr(i);
      html += "<div class='cgSub'><b>" + htmlEscape(ssid) + "</b><div class='cgHint'>MAC " + htmlEscape(mac) + " / RSSI " + String(WiFi.RSSI(i)) + " dBm</div>";
      html += "<a class='cgBtn' href='/addsensor?slot=" + String(slot) + "&mac=" + urlEncode(mac) + "&ssid=" + urlEncode(ssid) + "'>" + htmlEscape(currentLang == "FR" ? "Ajouter" : (currentLang == "NL" ? "Toevoegen" : (currentLang == "DE" ? "Hinzufuegen" : "Add"))) + "</a></div>";
    }
  }

  if (!found) {
    html += "<div class='cgHint'>" + htmlEscape(currentLang == "FR" ? "Aucun accessoire PM3D detecte." : (currentLang == "NL" ? "Geen PM3D-accessoire gevonden." : (currentLang == "DE" ? "Kein PM3D-Zubehoer erkannt." : "No PM3D accessory detected."))) + "</div>";
  }

  html += "<div style='margin-top:12px;display:flex;gap:6px;flex-wrap:wrap;'><a href='/searchsensor?slot=" + String(slot) + "'><button type='button'>" + htmlEscape(currentLang == "FR" ? "Relancer la recherche" : (currentLang == "NL" ? "Zoeken opnieuw starten" : (currentLang == "DE" ? "Suche erneut starten" : "Search again"))) + "</button></a><a href='/advanced'><button type='button'>" + htmlEscape(currentLang == "FR" ? "Retour aux reglages avances" : (currentLang == "NL" ? "Terug naar geavanceerde instellingen" : (currentLang == "DE" ? "Zurueck zu erweiterten Einstellungen" : "Back to advanced settings"))) + "</button></a></div>";
  html += "</div></div></body></html>";
  WiFi.scanDelete();
  return html;
}

void handleSearchSensorsDisplay() {
  int slot = server.hasArg("slot") ? server.arg("slot").toInt() : 0;
  server.send(200, "text/html; charset=utf-8", sensorSearchPageDisplay(slot));
}

void handleAddSensorDisplay() {
  int slot = server.hasArg("slot") ? server.arg("slot").toInt() : 0;
  if (slot < 0 || slot >= NB_SENSORS_MAX_DISPLAY) slot = 0;

  if (server.hasArg("mac")) macSensorDisplay[slot] = urlDecode(server.arg("mac"));
  if (server.hasArg("ssid")) ssidSensorDisplay[slot] = urlDecode(server.arg("ssid"));
  macSensorDisplay[slot].trim();
  ssidSensorDisplay[slot].trim();

  // Quand un sensor est trouve puis ajoute, il doit etre directement utilisable.
  sensorDetectionEnabledDisplay[slot] = true;
  sensorTriggerModeBySlotDisplay[slot] = "sensor";
  sensorNextAutoTriggerMsDisplay[slot] = 0;
  sensorKnownPresentDisplay[slot] = false;
  sensorLastSeenMsDisplay[slot] = 0;
  sensorLastRealTriggerMsDisplay[slot] = 0;

  saveConfig();
  server.sendHeader("Location", "/advanced");
  server.send(303, "text/plain", "");
}





String macToStringDisplay(const uint8_t mac[6]) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

bool parseMacDisplay(const String &value, uint8_t mac[6]) {
  int v[6];
  if (sscanf(value.c_str(), "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
  for (int i = 0; i < 6; i++) {
    if (v[i] < 0 || v[i] > 255) return false;
    mac[i] = (uint8_t)v[i];
  }
  return true;
}

bool sameMacDisplay(const uint8_t a[6], const uint8_t b[6]) {
  for (int i = 0; i < 6; i++) if (a[i] != b[i]) return false;
  return true;
}

bool macMatchesVariantsDisplay(const uint8_t sender[6], const String &savedText) {
  uint8_t saved[6];
  if (!parseMacDisplay(savedText, saved)) return false;
  if (sameMacDisplay(sender, saved)) return true;

  uint8_t v[6];
  memcpy(v, saved, 6);
  v[5] = saved[5] + 1; if (sameMacDisplay(sender, v)) return true;
  v[5] = saved[5] - 1; if (sameMacDisplay(sender, v)) return true;
  v[5] = saved[5] + 2; if (sameMacDisplay(sender, v)) return true;
  v[5] = saved[5] - 2; if (sameMacDisplay(sender, v)) return true;
  v[5] = saved[5] + 3; if (sameMacDisplay(sender, v)) return true;
  v[5] = saved[5] - 3; if (sameMacDisplay(sender, v)) return true;
  return false;
}

bool suffixMatchesSavedSensorDisplay(const char suffix[5], const String &savedSsid, const String &savedMac) {
  String suf = String(suffix);
  suf.trim();
  suf.toUpperCase();

  String ss = savedSsid;
  ss.toUpperCase();

  if (suf.length() == 4 && ss.indexOf(suf) >= 0) return true;

  uint8_t mac[6];
  if (parseMacDisplay(savedMac, mac)) {
    char m[5];
    snprintf(m, sizeof(m), "%02X%02X", mac[4], mac[5]);
    if (suf == String(m)) return true;
  }

  return false;
}

void handleSensorPacket(const Pm3dSensorPacket &p) {
  int slot = -1;

  for (int i = 0; i < NB_SENSORS_MAX_DISPLAY; i++) {
    if (macMatchesVariantsDisplay(p.apMac, macSensorDisplay[i]) || suffixMatchesSavedSensorDisplay(p.suffix, ssidSensorDisplay[i], macSensorDisplay[i])) {
      slot = i;
      break;
    }
  }

  if (slot < 0) {
    Serial.print("Sensor ESP-NOW ignore : ");
    Serial.print(macToStringDisplay(p.apMac));
    Serial.print(" suffix=");
    Serial.println(p.suffix);
    return;
  }

  if (!sensorDetectionEnabledDisplay[slot]) return;

  sensorLastEspNowMatchedMsDisplay = millis();
  sensorEspNowMatchedCountDisplay++;

  // Paquet reconnu = sensor réellement connecté / communication OK.
  sensorLastSeenMsDisplay[slot] = millis();

  // Comme Crossing Gate : heartbeat = communication, pas action.
  // Le voyant rouge/vert et l'ecran sensor ne reagissent qu'au vrai ON.
  if (p.event == 0) return;
  if (p.event != 1) return;

  sensorDetectedUntilMsDisplay[slot] = millis() + 5000UL;
  startSensorI2CTest(slot);
}

void onEspNowReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  sensorLastEspNowRawMsDisplay = millis();
  sensorEspNowRawCountDisplay++;

  if (len < 5) return;

  uint32_t magic;
  memcpy(&magic, data, sizeof(magic));
  if (magic != PM3D_MAGIC) return;

  uint8_t type = data[4];

  if (type == PKT_SENSOR && len == sizeof(Pm3dSensorPacket)) {
    sensorLastEspNowValidMsDisplay = millis();
    sensorEspNowValidCountDisplay++;

    Pm3dSensorPacket p;
    memcpy(&p, data, sizeof(p));
    handleSensorPacket(p);
    return;
  }
}


void handleSensorStatusDisplay() {
  String json = "{\"ok\":true,\"sensors\":[";
  for (int i = 0; i < NB_SENSORS_MAX_DISPLAY; i++) {
    if (i > 0) json += ",";
    bool active = ((long)(millis() - sensorDetectedUntilMsDisplay[i]) < 0);
    bool connected = (sensorLastSeenMsDisplay[i] > 0 && millis() - sensorLastSeenMsDisplay[i] < SENSOR_CONNECTED_TIMEOUT_MS);
    json += "{\"slot\":" + String(i + 1);
    json += ",\"paired\":" + String(macSensorDisplay[i].length() > 0 ? "true" : "false");
    json += ",\"connected\":" + String(connected ? "true" : "false");
    json += ",\"enabled\":" + String(sensorDetectionEnabledDisplay[i] ? "true" : "false");
    json += ",\"active\":" + String(active ? "true" : "false");
    json += "}";
  }
  json += "],";
  json += "\"espnow_raw_count\":" + String(sensorEspNowRawCountDisplay);
  json += ",\"espnow_valid_count\":" + String(sensorEspNowValidCountDisplay);
  json += ",\"espnow_matched_count\":" + String(sensorEspNowMatchedCountDisplay);
  json += ",\"espnow_raw_recent\":" + String((sensorLastEspNowRawMsDisplay > 0 && millis() - sensorLastEspNowRawMsDisplay < SENSOR_CONNECTED_TIMEOUT_MS) ? "true" : "false");
  json += ",\"espnow_valid_recent\":" + String((sensorLastEspNowValidMsDisplay > 0 && millis() - sensorLastEspNowValidMsDisplay < SENSOR_CONNECTED_TIMEOUT_MS) ? "true" : "false");
  json += ",\"espnow_matched_recent\":" + String((sensorLastEspNowMatchedMsDisplay > 0 && millis() - sensorLastEspNowMatchedMsDisplay < SENSOR_CONNECTED_TIMEOUT_MS) ? "true" : "false");
  json += "}";
  server.send(200, "application/json; charset=utf-8", json);
}


void handleSensorTriggerDisplay() {
  int slot = -1;

  if (server.hasArg("slot")) slot = server.arg("slot").toInt();
  else if (server.hasArg("sensor")) slot = server.arg("sensor").toInt() - 1;
  else if (server.hasArg("s")) slot = server.arg("s").toInt() - 1;

  if (server.hasArg("mac")) {
    String mac = urlDecode(server.arg("mac"));
    mac.trim();
    mac.toUpperCase();
    for (int i = 0; i < NB_SENSORS_MAX_DISPLAY; i++) {
      String known = macSensorDisplay[i];
      known.trim();
      known.toUpperCase();
      if (known.length() > 0 && known == mac) {
        slot = i;
        break;
      }
    }
  }

  if (slot < 0 || slot >= NB_SENSORS_MAX_DISPLAY) {
    server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"sensor_unknown\"}");
    return;
  }

  if (!sensorDetectionEnabledDisplay[slot]) {
    server.send(403, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"sensor_disabled\"}");
    return;
  }

  sensorDetectedUntilMsDisplay[slot] = millis() + 5000UL;
  startSensorI2CTest(slot);
  server.send(200, "application/json; charset=utf-8", "{\"ok\":true,\"sensor\":" + String(slot + 1) + "}");
}



String makeThèmePage(const String& message) {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += makeStyleBlock();
  html += "</head><body><div class='container'>";

  html += "<div class='panel'><div class='topbar'><div><div class='brand'>PM3D - " + htmlEscape(t_theme_title()) + "</div>";
  html += "<div class='sub'>" + htmlEscape(t_rgb_help()) + "</div></div>";
  html += "<div class='toolbar'><div class='badge'>" + htmlEscape(translatedThemeNameDisplay(theme.preset)) + "</div><a href='/main'><button type='button'>" + htmlEscape(t_back()) + "</button></a></div></div>";
  if (message.length() > 0) html += "<div class='info'>" + htmlEscape(message) + "</div>";
  html += "</div>";

  html += "<div class='panel'><strong>" + htmlEscape(t_choose_theme()) + "</strong><hr>";
  html += "<div class='toolbar'>";
  html += "<a href='/savetheme?preset=vert'><button type='button'>" + htmlEscape(translatedThemeNameDisplay("green")) + "</button></a>";
  html += "<a href='/savetheme?preset=jaune'><button type='button'>" + htmlEscape(translatedThemeNameDisplay("yellow")) + "</button></a>";
  html += "<a href='/savetheme?preset=rouge'><button type='button'>" + htmlEscape(translatedThemeNameDisplay("red")) + "</button></a>";
  html += "<a href='/savetheme?preset=bleu'><button type='button'>" + htmlEscape(translatedThemeNameDisplay("blue")) + "</button></a>";
  html += "<a href='/savetheme?preset=orange'><button type='button'>" + htmlEscape(translatedThemeNameDisplay("orange")) + "</button></a>";
  html += "<a href='/savetheme?preset=violet'><button type='button'>" + htmlEscape(translatedThemeNameDisplay("purple")) + "</button></a>";
  html += "<a href='/savetheme?preset=rose'><button type='button'>" + htmlEscape(translatedThemeNameDisplay("pink")) + "</button></a>";
  html += "<a href='/savetheme?preset=pm3d'><button type='button'>" + htmlEscape(translatedThemeNameDisplay("pm3d")) + "</button></a>";
  html += "</div></div>";

  html += "<div class='panel'><strong>" + htmlEscape(t_brightness()) + "</strong><hr>";
  html += "<div class='toolbar'>";
  html += "<a href='/brightness?delta=-15'><button type='button' style='font-size:16px;min-width:38px;'>-</button></a>";
  html += "<div class='badge'>" + String(screenBrightness) + " / 255</div>";
  html += "<a href='/brightness?delta=15'><button type='button' style='font-size:16px;min-width:38px;'>+</button></a>";
  html += "</div>";
  html += "<div class='small' style='margin-top:10px;'>" + htmlEscape(currentLang == "FR" ? "0 = minimum, 255 = maximum" : (currentLang == "NL" ? "0 = minimum, 255 = maximum" : (currentLang == "DE" ? "0 = Minimum, 255 = Maximum" : "0 = minimum, 255 = maximum"))) + "</div>";
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
  html += "<div class='preview'><div class='previewhead'><span>12:04</span><span>" + htmlEscape(currentLang == "FR" ? "Voie 1" : (currentLang == "NL" ? "Spoor 1" : (currentLang == "DE" ? "Gleis 1" : "Track 1"))) + "</span></div><div class='previewline'></div>";
  html += "<div class='bandw'>" + htmlEscape(currentLang == "FR" ? "Bruxelles" : (currentLang == "NL" ? "Brussel" : (currentLang == "DE" ? "Bruessel" : "Brussels"))) + "</div>";
  html += "<div class='bandb'>PM3D.NET</div>";
  html += "<div class='small'>" + htmlEscape(t_rgb_help()) + "</div>";
  html += "</div></div>";

  html += "</div></body></html>";
  return html;
}

void handleRoot() {
  if (server.hasArg("home") || server.hasArg("nointro")) server.send(200, "text/html; charset=utf-8", makeLanguePage());
  else server.send(200, "text/html; charset=utf-8", introPage());
}


// ============================================================
// PM3D Box API - carte d'identite et commandes externes
// Ajout non intrusif : ne modifie pas l'interface existante.
// ============================================================
String pm3dJsonEscape(const String& s) {
  String out = s;
  out.replace("\\", "\\\\");
  out.replace("\"", "\\\"");
  out.replace("\n", "\\n");
  out.replace("\r", "");
  return out;
}

String pm3dMac() {
  String mac = WiFi.softAPmacAddress();
  if (!mac.length() || mac == "00:00:00:00:00:00") mac = WiFi.macAddress();
  return mac;
}

String pm3dDeviceSuffix() {
  String mac = pm3dMac();
  mac.replace(":", "");
  if (mac.length() >= 4) return mac.substring(mac.length() - 4);
  return String("0000");
}

String pm3dDisplayName() {
  if (screenCustomName.length()) return screenCustomName;
  if (AP_SSID.length()) return AP_SSID;
  return String("PM3D-Display ") + pm3dDeviceSuffix();
}

String pm3dBuildRowsJson() {
  String json = "[";
  for (int i = 0; i < MAX_TRAINS; i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"line\":" + String(i + 1);
    json += ",\"time\":\"" + pm3dJsonEscape(trains[i].heure) + "\"";
    json += ",\"destination\":\"" + pm3dJsonEscape(trains[i].destination) + "\"";
    json += ",\"track\":\"" + pm3dJsonEscape(trains[i].voie) + "\"";
    json += "}";
  }
  json += "]";
  return json;
}

String pm3dIdentityJson() {
  String json = "{";
  json += "\"brand\":\"PM3D\"";
  json += ",\"device_type\":\"display\"";
  json += ",\"product\":\"PM3D Display\"";
  json += ",\"model\":\"Ecran de quai OLED\"";
  json += ",\"name\":\"" + pm3dJsonEscape(pm3dDisplayName()) + "\"";
  json += ",\"ssid\":\"" + pm3dJsonEscape(AP_SSID) + "\"";
  json += ",\"mac\":\"" + pm3dJsonEscape(pm3dMac()) + "\"";
  json += ",\"mac_suffix\":\"" + pm3dJsonEscape(pm3dDeviceSuffix()) + "\"";
  json += ",\"ip\":\"" + pm3dJsonEscape(currentApIpString()) + "\"";
  json += ",\"firmware\":\"" + pm3dJsonEscape(String(FW_VERSION)) + "\"";
  json += ",\"build\":\"" + pm3dJsonEscape(String(FW_BUILD_LABEL)) + "\"";
  json += ",\"language\":\"" + pm3dJsonEscape(currentLang) + "\"";
  json += ",\"skin\":\"pm3d_display_quai_oled\"";
  json += ",\"editor_type\":\"display_table\"";
  json += ",\"capabilities\":{";
  json += "\"rows\":" + String(MAX_TRAINS);
  json += ",\"visible_rows\":" + String(VISIBLE_ROWS);
  json += ",\"train_number\":false";
  json += ",\"free_message\":true";
  json += ",\"themes\":true";
  json += ",\"brightness\":true";
  json += ",\"ota\":true";
  json += "}";
  json += ",\"ui\":{";
  json += "\"plan_icon\":\"pm3d_display_quai\"";
  json += ",\"skin\":\"pm3d_display_quai_oled\"";
  json += ",\"fields\":[\"time\",\"destination\",\"train\",\"track\"]";
  json += "}";
  json += ",\"api\":{";
  json += "\"identity\":\"/api/identity\"";
  json += ",\"status\":\"/api/status\"";
  json += ",\"control\":\"/api/control\"";
  json += ",\"rows\":\"/api/rows\"";
  json += "}";
  json += "}";
  return json;
}

String pm3dStatusJson() {
  String json = "{";
  json += "\"ok\":true";
  json += ",\"name\":\"" + pm3dJsonEscape(pm3dDisplayName()) + "\"";
  json += ",\"device_type\":\"display\"";
  json += ",\"firmware\":\"" + pm3dJsonEscape(String(FW_VERSION)) + "\"";
  json += ",\"ip\":\"" + pm3dJsonEscape(currentApIpString()) + "\"";
  json += ",\"display_ok\":" + String(displayOk ? "true" : "false");
  json += ",\"language\":\"" + pm3dJsonEscape(currentLang) + "\"";
  json += ",\"rows\":" + pm3dBuildRowsJson();
  json += "}";
  return json;
}

void handlePm3dIdentity() { server.send(200, "application/json; charset=utf-8", pm3dIdentityJson()); }
void handlePm3dStatus() { server.send(200, "application/json; charset=utf-8", pm3dStatusJson()); }
void handlePm3dRows() { server.send(200, "application/json; charset=utf-8", pm3dBuildRowsJson()); }

void handlePm3dControl() {
  String cmd = getParamSafe("cmd");
  if (cmd == "identity") { handlePm3dIdentity(); return; }
  if (cmd == "status") { handlePm3dStatus(); return; }
  if (cmd == "setRow") {
    int line = getParamSafe("line").toInt();
    if (line < 1 || line > MAX_TRAINS) { server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"line_out_of_range\"}"); return; }
    int idx = line - 1;
    if (server.hasArg("time")) trains[idx].heure = nettoyerTexte(server.arg("time"), 5);
    if (server.hasArg("dest")) trains[idx].destination = nettoyerTexte(server.arg("dest"), 18);
    if (server.hasArg("destination")) trains[idx].destination = nettoyerTexte(server.arg("destination"), 18);
    if (server.hasArg("track")) trains[idx].voie = nettoyerTexte(server.arg("track"), 3);
    if (server.hasArg("voie")) trains[idx].voie = nettoyerTexte(server.arg("voie"), 3);
    saveConfig();
    showingCustomMessage = false;
    inPause = true;
    pauseStartMs = millis();
    server.send(200, "application/json; charset=utf-8", "{\"ok\":true,\"cmd\":\"setRow\",\"line\":" + String(line) + "}");
    return;
  }
  if (cmd == "setLang") {
    String lang = getParamSafe("lang");
    lang.toUpperCase();
    if (isSupportedLang(lang)) { currentLang = lang; translateKnownDestinationsOnly(); saveConfig(); resetAnimation(); afficherListeFluide(); server.send(200, "application/json; charset=utf-8", "{\"ok\":true,\"cmd\":\"setLang\",\"lang\":\"" + pm3dJsonEscape(currentLang) + "\"}"); return; }
    server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"unsupported_language\"}"); return;
  }
  if (cmd == "sensor" || cmd == "triggerSensor" || cmd == "detect") {
    handleSensorTriggerDisplay();
    return;
  }
  server.send(400, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"unknown_cmd\"}");
}

void handleUpdatePage() { server.send(200, "text/html; charset=utf-8", firmwareUpdatePageHtml("")); }

void handleThèmePage() { server.send(200, "text/html; charset=utf-8", makeThèmePage("")); }
void handleParamètresPage() { server.send(200, "text/html; charset=utf-8", makeParamètresPage("")); }

void handleBrightness() {
  int delta = getParamSafe("delta").toInt();
  screenBrightness += delta;
  if (screenBrightness < 0) screenBrightness = 0;
  if (screenBrightness > 255) screenBrightness = 255;
  applyScreenBrightness();
  saveConfig();
  server.send(200, "text/html; charset=utf-8", makeThèmePage(t_brightness_saved()));
}

void handleSaveThème() {
  String preset = getParamSafe("preset");
  if (preset.length() > 0) applyPresetThème(preset);

  theme.bodyBg1 = sanitizeHexCouleur(getParamSafe("bodyBg1"), theme.bodyBg1);
  theme.bodyBg2 = sanitizeHexCouleur(getParamSafe("bodyBg2"), theme.bodyBg2);
  theme.panelBg1 = sanitizeHexCouleur(getParamSafe("panelBg1"), theme.panelBg1);
  theme.panelBg2 = sanitizeHexCouleur(getParamSafe("panelBg2"), theme.panelBg2);
  theme.accent = sanitizeHexCouleur(getParamSafe("accent"), theme.accent);
  theme.accentText = sanitizeHexCouleur(getParamSafe("accentText"), theme.accentText);
  theme.text = sanitizeHexCouleur(getParamSafe("text"), theme.text);
  theme.muted = sanitizeHexCouleur(getParamSafe("muted"), theme.muted);
  theme.info = sanitizeHexCouleur(getParamSafe("info"), theme.info);
  theme.warn = sanitizeHexCouleur(getParamSafe("warn"), theme.warn);
  theme.inputBg = sanitizeHexCouleur(getParamSafe("inputBg"), theme.inputBg);

  saveConfig();
  server.send(200, "text/html; charset=utf-8", makeThèmePage(t_theme_saved()));
}

void handleSetLang() {
  String lang = getParamSafe("lang");
  lang.toUpperCase();
  if (isSupportedLang(lang)) {
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
    if (!customMessageEditedDE) {
      msgLine1DE = "PM3D.NET"; msgLine2DE = "wuenscht Ihnen"; msgLine3DE = "eine gute Reise !";
      msgLine1CenterDE = true; msgLine2CenterDE = true; msgLine3CenterDE = true;
    }
    if (!customMessageEditedEN) {
      msgLine1EN = "PM3D.NET"; msgLine2EN = "wishes you"; msgLine3EN = "a pleasant trip !";
      msgLine1CenterEN = true; msgLine2CenterEN = true; msgLine3CenterEN = true;
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
  } else if (currentLang == "DE") {
    msgLine1DE = line1; msgLine2DE = line2; msgLine3DE = line3;
    msgLine1CenterDE = c1; msgLine2CenterDE = c2; msgLine3CenterDE = c3; customMessageEditedDE = true;
  } else if (currentLang == "EN") {
    msgLine1EN = line1; msgLine2EN = line2; msgLine3EN = line3;
    msgLine1CenterEN = c1; msgLine2CenterEN = c2; msgLine3CenterEN = c3; customMessageEditedEN = true;
  } else {
    msgLine1FR = line1; msgLine2FR = line2; msgLine3FR = line3;
    msgLine1CenterFR = c1; msgLine2CenterFR = c2; msgLine3CenterFR = c3; customMessageEditedFR = true;
  }
  saveConfig();
  resetAnimation();
  server.send(200, "text/html; charset=utf-8", makeParamètresPage(t_saved()));
}

void handleSave() {
  for (int i = 0; i < MAX_TRAINS; i++) {
    trains[i].heure = nettoyerTexte(getParamSafe("h" + String(i)), 5);
    trains[i].destination = nettoyerTexte(getParamSafe("d" + String(i)), 18);
    trains[i].voie = nettoyerTexte(getParamSafe("v" + String(i)), 2);
  }
  for (int i = 0; i < NB_SENSORS_MAX_DISPLAY; i++) {
    if (server.hasArg("senTrain" + String(i))) {
      sensorTrainChoiceDisplay[i] = server.arg("senTrain" + String(i)).toInt();
      if (sensorTrainChoiceDisplay[i] < 0) sensorTrainChoiceDisplay[i] = 0;
      if (sensorTrainChoiceDisplay[i] >= MAX_TRAINS) sensorTrainChoiceDisplay[i] = MAX_TRAINS - 1;
    }
    if (server.hasArg("senPos" + String(i))) {
      sensorPositionChoiceDisplay[i] = server.arg("senPos" + String(i)).toInt();
      if (sensorPositionChoiceDisplay[i] < 0) sensorPositionChoiceDisplay[i] = 0;
      if (sensorPositionChoiceDisplay[i] >= NB_SENSORS_MAX_DISPLAY) sensorPositionChoiceDisplay[i] = NB_SENSORS_MAX_DISPLAY - 1;
    }
  }
  rTrains();
  saveConfig();
  resetAnimation();
  afficherListeFluide();
  server.send(200, "text/html; charset=utf-8", makeMainPage(t_saved()));
}



void handleSaveDevice() {
  apPasswordDisplay = getParamSafe("appass");
  apPasswordDisplay.trim();
  if (apPasswordDisplay.length() > 0 && apPasswordDisplay.length() < 8) apPasswordDisplay = "";
  if (apPasswordDisplay.length() > 63) apPasswordDisplay = apPasswordDisplay.substring(0, 63);

  saveConfig();
  server.send(200, "text/html; charset=utf-8", makeParamètresPage(t_saved_device() + " - " + (currentLang == "NL" ? "Herstart..." : (currentLang == "DE" ? "Neustart..." : (currentLang == "EN" ? "Restart..." : "Redemarrage...")))));
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
  html += "<div style='margin-top:12px; display:flex; gap:6px; flex-wrap:wrap;'><button type='submit'>Enregistrer et connecter</button><a href='/update'><button type='button'>Annuler</button></a></div>";
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

  otaProgressPageStart("Mise a jour firmware PM3D");
  otaProgressSend(1, "Preparation de la mise a jour...");

  markPendingOtaInstall(selectedLabel, finalUrl);

  String result;
  bool ok = performOtaFromUrlWithProgress(finalUrl, result);
  otaLastStatus = result;
  if (ok) {
    otaLastVersion = selectedLabel;
    saveConfig();
  } else {
    clearPendingOtaState(false);
    saveConfig();
  }

  if (ok) {
    server.sendContent("<script>showOtaSuccess();</script></body></html>");
    delay(2200);
    ESP.restart();
    return;
  }

  server.sendContent("<script>document.querySelector('.otaWarn').textContent='Echec de la mise a jour. Retour possible avec le bouton du navigateur.';</script></body></html>");
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
  otaWifiConnected = false;
  otaWifiStatus = "Connexion Internet inactive - OTA uniquement sur demande";
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
  AP_SSID = buildApSsidFromMac();
  IPAddress local_ip(192, 168, 4, apIpSuffix);
  IPAddress gateway(192, 168, 4, apIpSuffix);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_ip, gateway, subnet);

  bool apOk = false;
  if (apPasswordDisplay.length() >= 8) {
    apOk = WiFi.softAP(AP_SSID.c_str(), apPasswordDisplay.c_str(), 1, 0, 2);
  } else {
    apOk = WiFi.softAP(AP_SSID.c_str(), nullptr, 1, 0, 2);
  }
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  if (apOk) {
    esp_wifi_set_max_tx_power(72);
    esp_wifi_set_inactive_time(WIFI_IF_AP, 600);
    Serial.println("Point d'acces demarre");
    Serial.print("SSID : "); Serial.println(AP_SSID);
    Serial.print("IP AP : "); Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Erreur demarrage point d'acces");
  }

  WiFi.mode(WIFI_AP_STA);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onEspNowReceive);
    Serial.println("ESP-NOW sensors actif canal 1");
  } else {
    Serial.println("Erreur init ESP-NOW sensors");
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/main", HTTP_GET, handleMain);
  server.on("/intro", HTTP_GET, handleIntro);
  server.on("/advanced", HTTP_GET, handleAdvanced);
  server.on("/savesensors", HTTP_POST, handleSaveSensorsDisplay);
  server.on("/clearsensors", HTTP_GET, handleClearSensorsDisplay);
  server.on("/sensorparams", HTTP_GET, handleSensorParamsDisplay);
  server.on("/savesensorparams", HTTP_POST, handleSaveSensorParamsDisplay);
  server.on("/testsensor", HTTP_GET, handleTestSensorDisplay);
  server.on("/sensortrigger", HTTP_GET, handleSensorTriggerDisplay);
  server.on("/sensor", HTTP_GET, handleSensorTriggerDisplay);
  server.on("/sensorstatus", HTTP_GET, handleSensorStatusDisplay);
  server.on("/searchsensor", HTTP_GET, handleSearchSensorsDisplay);
  server.on("/addsensor", HTTP_GET, handleAddSensorDisplay);
  server.on("/theme", HTTP_GET, handleThèmePage);
  server.on("/configmenu", HTTP_GET, handleConfigMenu);
  server.on("/settings", HTTP_GET, handleParamètresPage);
  server.on("/savesensorpos", HTTP_POST, handleSaveSensorPositionDisplay);
  server.on("/brightness", HTTP_GET, handleBrightness);
  server.on("/savetheme", HTTP_GET, handleSaveThème);
  server.on("/savetheme", HTTP_POST, handleSaveThème);
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
  server.on("/api/identity", HTTP_GET, handlePm3dIdentity);
  server.on("/api/status", HTTP_GET, handlePm3dStatus);
  server.on("/api/rows", HTTP_GET, handlePm3dRows);
  server.on("/api/control", HTTP_GET, handlePm3dControl);
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

  if (millis() - lastEspNowChannelLockMsDisplay >= 2000UL) {
    lastEspNowChannelLockMsDisplay = millis();
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  }

  if (otaValidationDeferred && millis() - bootStartMs >= 5000) {
    finalizePendingOtaSuccessIfNeeded();
    otaValidationDeferred = false;
  }

  if (!displayOk) { delay(20); return; }

  unsigned long now = millis();

  if (sensorI2CTestActive) {
    int slot = sensorI2CTestSlot;
    if (slot < 0) slot = 0;
    if (slot >= NB_SENSORS_MAX_DISPLAY) slot = NB_SENSORS_MAX_DISPLAY - 1;
    unsigned long durationSec = sensorReturnNormalSecBySlotDisplay[slot];
    if (sensorTriggerModeBySlotDisplay[slot] == "auto") durationSec = sensorAutoShowSecBySlotDisplay[slot];
    unsigned long durationMs = durationSec * 1000UL;
    if (durationMs < 1000UL) durationMs = 1000UL;

    if (now - sensorI2CTestStartMs < durationMs) {
      afficherSensorI2CTest();
      delay(20);
      return;
    }

    sensorI2CTestActive = false;
    resetAnimation();
    afficherListeFluide();
  }

  maintainAutomaticSensorDisplay();
  if (sensorI2CTestActive) {
    delay(20);
    return;
  }

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
