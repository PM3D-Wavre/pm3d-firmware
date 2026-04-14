
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <Update.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

WebServer server(80);
Preferences prefs;

static const char* DEFAULT_PASSWORD = "12345678";
static const char* FW_VERSION = "1.0.0";
static const char* OTA_MANIFEST_URL = "https://raw.githubusercontent.com/PM3D-Wavre/pm3d-firmware/main/signal-sncb/firmware_manifest.txt";
static const uint8_t OTA_MAX_VERSIONS = 8;
static const uint8_t MAX_TOTAL_SIGNALS = 10;   // 1 maitre + 9 esclaves
static const uint8_t MAX_SLAVES = 9;

// ================= MODES =================
enum DeviceMode {
  DEVICE_SETUP = 0,
  DEVICE_STANDALONE = 1,
  DEVICE_MASTER = 2,
  DEVICE_SLAVE = 3
};

enum RunMode {
  RUN_OFF = 0,
  RUN_MANUAL = 1,
  RUN_AUTO = 2,
  RUN_PROGRAM = 3,
  RUN_PAIRING = 4
};

enum UiLang {
  LANG_FR = 0,
  LANG_NL = 1
};

DeviceMode deviceMode = DEVICE_STANDALONE;
RunMode runMode = RUN_AUTO;
UiLang uiLang = LANG_FR;

// ================= CONFIG =================
int pinLedVert = 3;
int pinLedRouge = 2;
int pinLedJauneBas = 5;
int pinLedJauneD = 4;
bool isCounterVoie = false;

uint8_t signalNumber = 1;
uint8_t masterNumber = 1;
String apName = "Signal_SNCB_01";
String setupAPName = "Signal_Setup_01";
String masterSSID = "";
String masterPassword = DEFAULT_PASSWORD;
String localIpString = "";
bool configuredOnce = false;
String otaSSID = "";
String otaPassword = "";
bool otaWifiConnected = false;
String otaWifiStatus = "";

struct OtaVersionItem {
  String version;
  String url;
};

OtaVersionItem otaVersions[OTA_MAX_VERSIONS];
uint8_t otaVersionCount = 0;
String otaLatestVersion = "";
String otaLatestUrl = "";
String otaLastCheckMessage = "";

// ================= ETATS =================
bool vert = false;
bool rouge = true;
bool jauneB = false;
bool jauneD = false;

// ================= CLIGNOTEMENT =================
enum BlinkEffect {
  BLINK_EFFECT_HARD = 0,
  BLINK_EFFECT_FADE = 1,
  BLINK_EFFECT_STOP = 2
};

bool blinkEnabled = false;
unsigned long blinkOnMs = 500UL;
unsigned long blinkOffMs = 500UL;
bool blinkPhaseOn = true;
unsigned long blinkLastToggleMs = 0;
unsigned long blinkLastRefreshMs = 0;
unsigned long blinkStopMs = 300UL;
BlinkEffect blinkEffect = BLINK_EFFECT_HARD;

bool blinkUserModified = false;

void applyBlinkDefaultsForTrackType(bool counterVoie) {
  if (counterVoie) {
    blinkEnabled = true;
    blinkOnMs = 600UL;
    blinkOffMs = 900UL;
    blinkStopMs = 200UL;
    blinkEffect = BLINK_EFFECT_FADE;
  } else {
    blinkEnabled = false;
    blinkOnMs = 600UL;
    blinkOffMs = 900UL;
    blinkStopMs = 200UL;
    blinkEffect = BLINK_EFFECT_FADE;
  }
  blinkPhaseOn = true;
  blinkLastToggleMs = 0;
  blinkLastRefreshMs = 0;
}

// ================= PROGRAMMES =================
const uint8_t autoStepCount = 4;
String autoAspects[autoStepCount] = {"rouge", "vert", "rouge", "2j"};
unsigned long autoDurations[autoStepCount] = {15000UL, 15000UL, 15000UL, 15000UL};

const uint8_t progStepCount = 10;
String progAspects[progStepCount] = {
  "rouge", "vert", "2j", "off", "off",
  "off", "off", "off", "off", "off"
};
unsigned long progDurations[progStepCount] = {
  5UL, 5UL, 5UL, 1UL, 1UL,
  1UL, 1UL, 1UL, 1UL, 1UL
};

unsigned long timerSequence = 0;
uint8_t stepSequence = 0;
unsigned long lastHeartbeatMs = 0;
unsigned long lastReconnectTry = 0;
unsigned long lastRegistryClean = 0;

// ================= REGISTRE ESCLAVES =================
struct SlaveInfo {
  bool active;
  uint8_t num;
  String name;
  String ip;
  String aspect;
  String setupSsid;
  bool counterVoie;
  unsigned long lastSeen;
};

SlaveInfo slaves[MAX_SLAVES];

struct ScenarioRule {
  bool active;
  uint8_t triggerNum;
  String triggerAspect;
  uint8_t targetNum;
  String targetAspect;
};

static const uint8_t MAX_SCENARIO_RULES = 12;
ScenarioRule scenarioRules[MAX_SCENARIO_RULES];

// ================= OUTILS =================
String twoDigits(uint8_t n) {
  if (n < 10) return "0" + String(n);
  return String(n);
}

String urlEncode(const String &str) {
  String encoded = "";
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < str.length(); i++) {
    unsigned char c = (unsigned char)str.charAt(i);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += (char)c;
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
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
    } else if (c == '+') {
      decoded += ' ';
    } else {
      decoded += c;
    }
  }
  return decoded;
}

String deviceModeName(uint8_t mode) {
  switch (mode) {
    case DEVICE_SETUP: return "SETUP";
    case DEVICE_STANDALONE: return "STANDALONE";
    case DEVICE_MASTER: return "MASTER";
    case DEVICE_SLAVE: return "CLIENT";
    default: return "UNKNOWN";
  }
}

String runModeName(uint8_t mode) {
  switch (mode) {
    case RUN_OFF: return "OFF";
    case RUN_MANUAL: return "MANUEL";
    case RUN_AUTO: return "AUTO";
    case RUN_PROGRAM: return "PROGRAMME";
    case RUN_PAIRING: return "APPAREILLAGE";
    default: return "INCONNU";
  }
}

String selectedIf(bool cond) {
  return cond ? " selected" : "";
}

String tr(const String& fr, const String& nl) {
  return (uiLang == LANG_NL) ? nl : fr;
}

String trRoleMaster() { return tr("Master", "Master"); }
String trRoleSlave() { return tr("Client", "Client"); }
String trRoleSolo() { return tr("Standalone", "Standalone"); }
String trRailway() { return "SNCB_NMBS"; }

bool isValidOutputPin(int p) {
  return p >= 0 && p <= 21;
}

String buildStandaloneName(uint8_t n) {
  return trRailway() + " " + trRoleSolo() + " " + String(n);
}

String buildMasterName(uint8_t n) {
  return trRailway() + " " + trRoleMaster() + " " + String(n);
}

String buildSlaveName(uint8_t n) {
  return trRailway() + " " + trRoleSlave() + " " + String(n);
}

String buildSetupName(uint8_t n) {
  return "Signal_Setup_" + twoDigits(n);
}

String buildSetupIpString() {
  return "192.168.4.1";
}

String buildStandaloneIpString(uint8_t n) {
  return "192.168.100." + String(100 + n);
}

String buildSlaveIpString(uint8_t n) {
  return "192.168.100." + String(20 + n);
}

String buildMasterIpString() {
  return "192.168.100.10";
}

String buildStandaloneApiHost(uint8_t n) {
  return buildStandaloneIpString(n);
}

String getCurrentAspectName() {
  if (rouge && !vert && !jauneB && !jauneD) return tr("ROUGE", "ROOD");
  if (!rouge && vert && !jauneB && !jauneD) return tr("VERT", "GROEN");
  if (!rouge && !vert && jauneB && jauneD) return tr("2 JAUNES", "2 GELEN");
  if (!rouge && vert && !jauneB && jauneD) return tr("VERT + JAUNE DEPORTE", "GROEN + VERPLAATST GEEL");
  if (!rouge && !vert && !jauneB && !jauneD) return "OFF";
  if (!rouge && !vert && jauneB && !jauneD) return tr("JAUNE BAS", "ONDER GEEL");
  if (!rouge && !vert && !jauneB && jauneD) return tr("JAUNE DEPORTE", "VERPLAATST GEEL");
  return tr("PERSONNALISE", "AANGEPAST");
}

String currentDeviceLabel() {
  if (deviceMode == DEVICE_MASTER) return buildMasterName(signalNumber);
  if (deviceMode == DEVICE_SLAVE) return buildSlaveName(signalNumber);
  return buildStandaloneName(signalNumber);
}

void clearSlaveRegistry() {
  for (uint8_t i = 0; i < MAX_SLAVES; i++) {
    slaves[i].active = false;
    slaves[i].num = 0;
    slaves[i].name = "";
    slaves[i].ip = "";
    slaves[i].aspect = "";
    slaves[i].lastSeen = 0;
  }
  for (uint8_t i = 0; i < MAX_SCENARIO_RULES; i++) {
    scenarioRules[i].active = false;
    scenarioRules[i].triggerNum = 0;
    scenarioRules[i].triggerAspect = "";
    scenarioRules[i].targetNum = 0;
    scenarioRules[i].targetAspect = "";
  }
}

int findSlaveIndexByNum(uint8_t num) {
  for (uint8_t i = 0; i < MAX_SLAVES; i++) {
    if (slaves[i].active && slaves[i].num == num) return i;
  }
  return -1;
}

int firstFreeSlaveIndex() {
  for (uint8_t i = 0; i < MAX_SLAVES; i++) {
    if (!slaves[i].active) return i;
  }
  return -1;
}

int findSlaveIndexByIp(const String& ip) {
  for (uint8_t i = 0; i < MAX_SLAVES; i++) {
    if (slaves[i].active && slaves[i].ip == ip) return i;
  }
  return -1;
}

int findSlaveIndexBySetupSsid(const String& setupSsid) {
  for (uint8_t i = 0; i < MAX_SLAVES; i++) {
    if (slaves[i].active && slaves[i].setupSsid == setupSsid) return i;
  }
  return -1;
}

uint8_t firstAvailableSlaveNumber() {
  for (uint8_t n = 1; n <= 99; n++) {
    if (n == signalNumber) continue;
    if (findSlaveIndexByNum(n) < 0) return n;
  }
  return 1;
}

String aspectCodeToLabel(String a) {
  a.toLowerCase();
  if (a == "rouge") return "ROUGE";
  if (a == "vert") return "VERT";
  if (a == "jb") return "JAUNE BAS";
  if (a == "jd") return "JAUNE DEPORTE";
  if (a == "vjd") return "VERT + JAUNE DEPORTE";
  if (a == "2j") return "2 JAUNES";
  return "OFF";
}

String aspectLabelUi(String a) {
  String b = a;
  b.trim();
  String up = b;
  up.toUpperCase();
  String low = b;
  low.toLowerCase();

  if (up == "ROUGE" || low == "rouge") return tr("Rouge", "Rood");
  if (up == "VERT" || low == "vert") return tr("Vert", "Groen");
  if (up == "JAUNE BAS" || low == "jb") return tr("Jaune bas", "Onder geel");
  if (up == "JAUNE DEPORTE" || low == "jd") return tr("Jaune deporte", "Verplaatst geel");
  if (up == "VERT + JAUNE DEPORTE" || low == "vjd") return tr("Vert + jaune deporte", "Groen + verplaatst geel");
  if (up == "2 JAUNES" || up == "DOUBLE JAUNE" || up == "2J" || low == "2j") return tr("2 jaunes", "2 gelen");
  if (up == "PERSONNALISE") return tr("Personnalise", "Aangepast");
  if (up == "OFF") return "OFF";
  return htmlEscape(a);
}

String aspectLabelToCode(String a) {
  a.toUpperCase();
  if (a == "ROUGE") return "rouge";
  if (a == "VERT") return "vert";
  if (a == "JAUNE BAS") return "jb";
  if (a == "JAUNE DEPORTE") return "jd";
  if (a == "VERT + JAUNE DEPORTE" || a == "VJD") return "vjd";
  if (a == "2 JAUNES" || a == "DOUBLE JAUNE" || a == "2J") return "2j";
  return "off";
}

void refreshNames() {
  if (deviceMode == DEVICE_MASTER) {
    apName = buildMasterName(signalNumber);
  } else if (deviceMode == DEVICE_SLAVE) {
    apName = buildSlaveName(signalNumber);
  } else {
    apName = buildStandaloneName(signalNumber);
  }
  setupAPName = buildSetupName(signalNumber);

  if (deviceMode == DEVICE_MASTER) {
    localIpString = "192.168.100.10";
  } else if (deviceMode == DEVICE_STANDALONE) {
    localIpString = buildStandaloneIpString(signalNumber);
  } else if (deviceMode == DEVICE_SLAVE) {
    localIpString = WiFi.localIP().toString();
    if (localIpString == "0.0.0.0") localIpString = buildSlaveIpString(signalNumber) + " (en attente)";
  } else {
    localIpString = "192.168.4.1";
  }

  if (masterSSID.length() == 0) {
    masterSSID = buildMasterName(masterNumber);
  }
}

void loadConfig() {
  prefs.begin("signalcfg", true);

  signalNumber = prefs.getUChar("num", 1);
  if (signalNumber < 1 || signalNumber > 99) signalNumber = 1;

  masterNumber = prefs.getUChar("mnum", 1);
  if (masterNumber < 1 || masterNumber > 99) masterNumber = 1;

  uint8_t dm = prefs.getUChar("dmode", DEVICE_STANDALONE);
  if (dm > DEVICE_SLAVE) dm = DEVICE_STANDALONE;
  deviceMode = (DeviceMode)dm;

  configuredOnce = prefs.getBool("cfg", false);

  uint8_t lang = prefs.getUChar("lang", LANG_FR);
  if (lang > LANG_NL) lang = LANG_FR;
  uiLang = (UiLang)lang;

  pinLedVert = prefs.getInt("pinVert", 3);
  pinLedRouge = prefs.getInt("pinRouge", 2);
  pinLedJauneBas = prefs.getInt("pinJBas", 5);
  pinLedJauneD = prefs.getInt("pinJD", 4);
  isCounterVoie = prefs.getBool("ctrvoie", false);

  masterSSID = prefs.getString("mssid", buildMasterName(masterNumber));
  masterPassword = prefs.getString("mpass", DEFAULT_PASSWORD);
  otaSSID = prefs.getString("otassid", "");
  otaPassword = prefs.getString("otapass", "");

  blinkUserModified = prefs.getBool("blinkUsr", false);
  if (blinkUserModified) {
    blinkEnabled = prefs.getBool("blinkEn", false);
    blinkOnMs = prefs.getULong("blinkOn", 600UL);
    blinkOffMs = prefs.getULong("blinkOff", 900UL);
    uint8_t be = prefs.getUChar("blinkFx", BLINK_EFFECT_FADE);
    if (be > BLINK_EFFECT_STOP) be = BLINK_EFFECT_FADE;
    blinkEffect = (BlinkEffect)be;
    blinkStopMs = prefs.getULong("blinkStop", 200UL);
  } else {
    applyBlinkDefaultsForTrackType(isCounterVoie);
  }
  if (blinkOnMs < 50UL) blinkOnMs = 50UL;
  if (blinkOnMs > 5000UL) blinkOnMs = 5000UL;
  if (blinkOffMs < 50UL) blinkOffMs = 50UL;
  if (blinkOffMs > 5000UL) blinkOffMs = 5000UL;
  if (blinkStopMs > 5000UL) blinkStopMs = 5000UL;

  for (uint8_t i = 0; i < progStepCount; i++) {
    String aKey = "pa" + String(i);
    String dKey = "pd" + String(i);
    progAspects[i] = prefs.getString(aKey.c_str(), progAspects[i]);
    progDurations[i] = prefs.getULong(dKey.c_str(), progDurations[i]);
    if (progDurations[i] < 1) progDurations[i] = 1;
    if (progDurations[i] > 999) progDurations[i] = 999;
  }

  prefs.end();
  refreshNames();
}

void clearMasterSlaveData() {
  masterNumber = 1;
  masterSSID = "";
  masterPassword = DEFAULT_PASSWORD;
}

void saveCoreConfig() {
  prefs.begin("signalcfg", false);
  prefs.putUChar("num", signalNumber);
  prefs.putUChar("mnum", masterNumber);
  prefs.putUChar("dmode", (uint8_t)deviceMode);
  prefs.putBool("cfg", true);
  prefs.putUChar("lang", (uint8_t)uiLang);
  prefs.putString("mssid", masterSSID);
  prefs.putString("mpass", masterPassword);
  prefs.putString("otassid", otaSSID);
  prefs.putString("otapass", otaPassword);
  prefs.end();
  configuredOnce = true;
}

void saveUiLang() {
  prefs.begin("signalcfg", false);
  prefs.putUChar("lang", (uint8_t)uiLang);
  prefs.end();
}

void saveOtaWifiConfig(const String& ssid, const String& pass) {
  otaSSID = ssid;
  otaPassword = pass;
  prefs.begin("signalcfg", false);
  prefs.putString("otassid", otaSSID);
  prefs.putString("otapass", otaPassword);
  prefs.end();
}

void clearOtaWifiConfig() {
  otaSSID = "";
  otaPassword = "";
  otaWifiConnected = false;
  otaWifiStatus = "";
  prefs.begin("signalcfg", false);
  prefs.remove("otassid");
  prefs.remove("otapass");
  prefs.end();
}

void saveBlinkConfig(bool enabled, unsigned long onMs, unsigned long offMs, unsigned long stopMs, BlinkEffect effect) {
  if (onMs < 50UL) onMs = 50UL;
  if (onMs > 5000UL) onMs = 5000UL;
  if (offMs < 50UL) offMs = 50UL;
  if (offMs > 5000UL) offMs = 5000UL;
  if (stopMs > 5000UL) stopMs = 5000UL;

  prefs.begin("signalcfg", false);
  prefs.putBool("blinkEn", enabled);
  prefs.putULong("blinkOn", onMs);
  prefs.putULong("blinkOff", offMs);
  prefs.putULong("blinkStop", stopMs);
  prefs.putUChar("blinkFx", (uint8_t)effect);
  prefs.putBool("blinkUsr", true);
  prefs.end();

  blinkUserModified = true;
  blinkEnabled = enabled;
  blinkOnMs = onMs;
  blinkOffMs = offMs;
  blinkStopMs = stopMs;
  blinkEffect = effect;
  blinkPhaseOn = true;
  blinkLastToggleMs = millis();
  blinkLastRefreshMs = 0;
}

void saveLedPins(int newVert, int newRouge, int newJauneBas, int newJauneD, bool newCounterVoie) {
  if (!isValidOutputPin(newVert)) newVert = 3;
  if (!isValidOutputPin(newRouge)) newRouge = 2;
  if (!isValidOutputPin(newJauneBas)) newJauneBas = 5;
  if (!isValidOutputPin(newJauneD)) newJauneD = 4;

  prefs.begin("signalcfg", false);
  prefs.putInt("pinVert", newVert);
  prefs.putInt("pinRouge", newRouge);
  prefs.putInt("pinJBas", newJauneBas);
  prefs.putInt("pinJD", newJauneD);
  prefs.putBool("ctrvoie", newCounterVoie);
  prefs.end();

  pinLedVert = newVert;
  pinLedRouge = newRouge;
  pinLedJauneBas = newJauneBas;
  pinLedJauneD = newJauneD;
  isCounterVoie = newCounterVoie;

  if (!blinkUserModified) {
    applyBlinkDefaultsForTrackType(isCounterVoie);
    prefs.begin("signalcfg", false);
    prefs.putBool("blinkEn", blinkEnabled);
    prefs.putULong("blinkOn", blinkOnMs);
    prefs.putULong("blinkOff", blinkOffMs);
    prefs.putULong("blinkStop", blinkStopMs);
    prefs.putUChar("blinkFx", (uint8_t)blinkEffect);
    prefs.putBool("blinkUsr", false);
    prefs.end();
  }

  resetBlinkCycle();
  applyLeds();
}

void saveProgramToPrefs() {
  prefs.begin("signalcfg", false);
  for (uint8_t i = 0; i < progStepCount; i++) {
    String aKey = "pa" + String(i);
    String dKey = "pd" + String(i);
    prefs.putString(aKey.c_str(), progAspects[i]);
    prefs.putULong(dKey.c_str(), progDurations[i]);
  }
  prefs.end();
}

void reconfigureLedPins() {
  pinMode(pinLedVert, OUTPUT);
  pinMode(pinLedRouge, OUTPUT);
  pinMode(pinLedJauneBas, OUTPUT);
  pinMode(pinLedJauneD, OUTPUT);
}

uint8_t computeBlinkBrightness() {
  if (!blinkEnabled) return 255;

  if (blinkEffect == BLINK_EFFECT_HARD) {
    return blinkPhaseOn ? 255 : 0;
  }

  if (blinkEffect == BLINK_EFFECT_STOP) {
    return 0;
  }

  unsigned long now = millis();
  unsigned long elapsed = now - blinkLastToggleMs;

  if (!blinkPhaseOn && elapsed >= blinkOffMs) {
    return 0;
  }

  unsigned long duration = blinkPhaseOn ? blinkOnMs : blinkOffMs;
  if (duration < 1UL) duration = 1UL;
  if (elapsed > duration) elapsed = duration;

  if (blinkPhaseOn) {
    return (uint8_t)((elapsed * 255UL) / duration);
  }
  return (uint8_t)(255UL - ((elapsed * 255UL) / duration));
}

void writeLedLevel(int pin, bool isOn, uint8_t level) {
  uint8_t out = isOn ? level : 0;
  analogWrite(pin, out);
}

void applyLeds() {
  uint8_t level = computeBlinkBrightness();
  writeLedLevel(pinLedVert, vert, level);
  writeLedLevel(pinLedRouge, rouge, level);
  writeLedLevel(pinLedJauneBas, jauneB, level);
  writeLedLevel(pinLedJauneD, jauneD, level);
}

void resetBlinkCycle() {
  blinkPhaseOn = true;
  blinkLastToggleMs = millis();
  blinkLastRefreshMs = 0;
}

void updateBlinkState() {
  if (!blinkEnabled) return;

  unsigned long now = millis();
  unsigned long target = blinkPhaseOn ? blinkOnMs : blinkOffMs;

  if (blinkEffect == BLINK_EFFECT_STOP) {
    if (blinkLastRefreshMs == 0 || now - blinkLastRefreshMs >= 60UL) {
      blinkLastRefreshMs = now;
      applyLeds();
    }
    return;
  }

  if (blinkEffect == BLINK_EFFECT_FADE) {
    if (blinkPhaseOn) {
      if (now - blinkLastToggleMs >= target) {
        blinkPhaseOn = false;
        blinkLastToggleMs = now;
        applyLeds();
      }
    } else {
      unsigned long totalLow = blinkOffMs + blinkStopMs;
      if (now - blinkLastToggleMs >= totalLow) {
        blinkPhaseOn = true;
        blinkLastToggleMs = now;
        applyLeds();
      }
    }

    if (blinkLastRefreshMs == 0 || now - blinkLastRefreshMs >= 15UL) {
      blinkLastRefreshMs = now;
      applyLeds();
    }
    return;
  }

  if (now - blinkLastToggleMs >= target) {
    blinkPhaseOn = !blinkPhaseOn;
    blinkLastToggleMs = now;
    applyLeds();
  }
}

void setAll(bool r, bool v, bool jb, bool jd) {
  rouge = r;
  vert = v;
  jauneB = jb;
  jauneD = jd;
  resetBlinkCycle();
  applyLeds();
}

void applyAspect(const String& a) {
  if (a == "rouge") setAll(true, false, false, false);
  else if (a == "vert") setAll(false, true, false, false);
  else if (a == "2j") setAll(false, false, true, true);
  else if (a == "vjd") setAll(false, true, false, true);
  else if (a == "jb") setAll(false, false, true, false);
  else if (a == "jd") setAll(false, false, false, true);
  else setAll(false, false, false, false);
}

String blinkStateLabel() {
  return blinkEnabled ? tr("Clignotement actif", "Knipperen actief") : tr("Clignotement inactif", "Knipperen uit");
}

String blinkButtonColor() {
  return blinkEnabled ? "#22c55e" : "#d62f2f";
}

String blinkEffectLabel() {
  if (blinkEffect == BLINK_EFFECT_FADE) return tr("Effet fondu", "Fade-effect");
  if (blinkEffect == BLINK_EFFECT_STOP) return tr("Arret", "Stop");
  return tr("Clignotement franc", "Hard knipperen");
}

String blinkEffectOption(const String& value) {
  if (value == "fade") return blinkEffect == BLINK_EFFECT_FADE ? " selected" : "";
  if (value == "stop") return blinkEffect == BLINK_EFFECT_STOP ? " selected" : "";
  return blinkEffect == BLINK_EFFECT_HARD ? " selected" : "";
}

void toggleBlinkEnabled() {
  saveBlinkConfig(!blinkEnabled, blinkOnMs, blinkOffMs, blinkStopMs, blinkEffect);
  applyLeds();
}

void toggleRougeOnly() { rouge = !rouge; resetBlinkCycle(); applyLeds(); }
void toggleVertOnly() { vert = !vert; resetBlinkCycle(); applyLeds(); }
void toggleJauneBasOnly() { jauneB = !jauneB; resetBlinkCycle(); applyLeds(); }
void toggleJauneDeporteOnly() { jauneD = !jauneD; resetBlinkCycle(); applyLeds(); }
void toggleDoubleJaune() {
  bool ns = !(jauneB && jauneD);
  jauneB = ns;
  jauneD = ns;
  resetBlinkCycle();
  applyLeds();
}

void startAutoMode() {
  runMode = RUN_AUTO;
  stepSequence = 0;
  timerSequence = millis();
  applyAspect(autoAspects[0]);
}

void startProgramMode() {
  runMode = RUN_PROGRAM;
  stepSequence = 0;
  timerSequence = millis();
  applyAspect(progAspects[0]);
}

void startManualMode() { runMode = RUN_MANUAL; }
void startOffMode() { runMode = RUN_OFF; applyAspect("off"); }
void startPairingMode() { runMode = RUN_PAIRING; }

void runAutoSequence() {
  if (millis() - timerSequence < autoDurations[stepSequence]) return;
  timerSequence = millis();
  stepSequence++;
  if (stepSequence >= autoStepCount) stepSequence = 0;
  applyAspect(autoAspects[stepSequence]);
}

void runProgramSequence() {
  if (millis() - timerSequence < (progDurations[stepSequence] * 1000UL)) return;
  timerSequence = millis();
  stepSequence++;
  if (stepSequence >= progStepCount) stepSequence = 0;
  applyAspect(progAspects[stepSequence]);
}

// ================= WIFI =================

void optimizeWiFiStability() {
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  #ifdef WIFI_PS_NONE
  esp_wifi_set_ps(WIFI_PS_NONE);
  #endif
  #ifdef WIFI_POWER_19_5dBm
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  #endif
}

void startStandaloneAP() {
  optimizeWiFiStability();
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(false, false);
  WiFi.softAPdisconnect(true);
  delay(150);

  IPAddress local(192, 168, 100, 100 + signalNumber);
  IPAddress gateway(192, 168, 100, 100 + signalNumber);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local, gateway, subnet);
  WiFi.softAP(apName.c_str(), DEFAULT_PASSWORD);
  localIpString = WiFi.softAPIP().toString();

  otaWifiConnected = false;
  if (otaSSID.length() > 0) {
    IPAddress zero(0,0,0,0);
    WiFi.config(zero, zero, zero);
    WiFi.begin(otaSSID.c_str(), otaPassword.c_str());
    unsigned long startTry = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTry < 10000UL) {
      delay(250);
    }
    otaWifiConnected = (WiFi.status() == WL_CONNECTED);
    otaWifiStatus = otaWifiConnected ? (String("OK - ") + WiFi.localIP().toString()) : tr("Connexion OTA impossible", "OTA-verbinding mislukt");
  } else {
    otaWifiStatus = tr("Wi-Fi Internet non configure", "Internet-wifi niet ingesteld");
  }
}

void startMasterAP() {
  optimizeWiFiStability();
  WiFi.mode(WIFI_AP);
  WiFi.softAPdisconnect(true);
  delay(150);

  IPAddress local(192, 168, 100, 10);
  IPAddress gateway(192, 168, 100, 10);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local, gateway, subnet);
  WiFi.softAP(apName.c_str(), masterPassword.c_str(), 1, 0, MAX_TOTAL_SIGNALS);
  localIpString = WiFi.softAPIP().toString();
}

bool connectToMasterSTA(bool useStaticIp, uint8_t ipNum, unsigned long timeoutMs) {
  optimizeWiFiStability();
  WiFi.disconnect(true, true);
  delay(250);

  if (useStaticIp) {
    IPAddress staLocal(192, 168, 100, 20 + ipNum);
    IPAddress staGateway(192, 168, 100, 10);
    IPAddress staSubnet(255, 255, 255, 0);
    if (!WiFi.config(staLocal, staGateway, staSubnet)) {
      Serial.println("Echec config IP statique esclave");
    }
  } else {
    IPAddress zero(0,0,0,0);
    WiFi.config(zero, zero, zero);
  }

  WiFi.begin(masterSSID.c_str(), masterPassword.c_str(), 1);

  Serial.println("Connexion au maitre...");
  Serial.print("SSID maitre : ");
  Serial.println(masterSSID);

  unsigned long startTry = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTry < timeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

void connectSlaveToMaster() {
  optimizeWiFiStability();
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPdisconnect(true);
  delay(100);

  IPAddress apLocal(192, 168, 4, 1);
  IPAddress apGateway(192, 168, 4, 1);
  IPAddress apSubnet(255, 255, 255, 0);
  WiFi.softAPConfig(apLocal, apGateway, apSubnet);
  WiFi.softAP(setupAPName.c_str(), DEFAULT_PASSWORD, 1, 0, 1);

  // 1) Connexion souple au maitre pour obtenir un numero libre
  bool connected = connectToMasterSTA(false, signalNumber, 15000UL);
  if (!connected) {
    localIpString = buildSlaveIpString(signalNumber) + " (non connecte)";
    Serial.println("Connexion maitre echouee");
    Serial.print("IP attendue : ");
    Serial.println(buildSlaveIpString(signalNumber));
    return;
  }

  Serial.println("Connecte au maitre (phase 1)");
  Serial.print("IP provisoire : ");
  Serial.println(WiFi.localIP());

  uint8_t assignedNum = requestFreeSlaveNumberFromMaster();
  if (assignedNum >= 1 && assignedNum <= 99 && assignedNum != signalNumber) {
    signalNumber = assignedNum;
    refreshNames();
    saveCoreConfig();
    WiFi.softAPdisconnect(true);
    delay(100);
    WiFi.softAPConfig(apLocal, apGateway, apSubnet);
    WiFi.softAP(setupAPName.c_str(), DEFAULT_PASSWORD, 1, 0, 1);
    Serial.print("Numero attribue par le maitre : ");
    Serial.println(signalNumber);
  }

  // 2) Reconnexion finale avec IP statique conforme au numero attribue
  connected = connectToMasterSTA(true, signalNumber, 12000UL);
  if (connected) {
    localIpString = WiFi.localIP().toString();
    Serial.println("Connecte au maitre");
    Serial.print("IP esclave : ");
    Serial.println(localIpString);
    sendRegisterToMaster();
  } else {
    localIpString = buildSlaveIpString(signalNumber) + " (non connecte)";
    Serial.println("Connexion maitre finale echouee");
    Serial.print("IP attendue : ");
    Serial.println(buildSlaveIpString(signalNumber));
  }
}

void beginWiFiForCurrentMode() {
  refreshNames();

  if (deviceMode == DEVICE_MASTER) startMasterAP();
  else if (deviceMode == DEVICE_SLAVE) connectSlaveToMaster();
  else startStandaloneAP();
}

String htmlEscape(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  return s;
}

bool sendHttpGet(String host, String path) {
  WiFiClient client;

  Serial.print("HTTP GET -> ");
  Serial.print(host);
  Serial.println(path);

  if (!client.connect(host.c_str(), 80)) {
    Serial.println("ECHEC connect()");
    return false;
  }

  client.print("GET ");
  client.print(path);
  client.print(" HTTP/1.1\r\nHost: ");
  client.print(host);
  client.print("\r\nConnection: close\r\n\r\n");

  String response = "";
  unsigned long t0 = millis();

  while (millis() - t0 < 2000UL) {
    while (client.available()) {
      char c = client.read();
      response += c;
      t0 = millis();
    }
    if (!client.connected() && !client.available()) break;
    delay(1);
  }

  client.stop();

  Serial.println("REPONSE HTTP:");
  Serial.println(response);

  return response.indexOf("200 OK") >= 0;
}

String sendHttpGetReadBody(String host, String path) {
  WiFiClient client;
  if (!client.connect(host.c_str(), 80)) return "";
  client.print(String("GET ") + path + " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n");
  String response = "";
  unsigned long t0 = millis();
  while (millis() - t0 < 2000UL) {
    while (client.available()) {
      response += char(client.read());
      t0 = millis();
    }
    if (!client.connected() && !client.available()) break;
    delay(1);
  }
  client.stop();
  int bodyPos = response.indexOf("\r\n\r\n");
  if (bodyPos >= 0) return response.substring(bodyPos + 4);
  return response;
}

uint8_t requestFreeSlaveNumberFromMaster() {
  if (WiFi.status() != WL_CONNECTED) return signalNumber;
  String body = sendHttpGetReadBody(buildMasterIpString(), "/api/nextnum");
  body.trim();
  int n = body.toInt();
  if (n < 1 || n > 99) return signalNumber;
  return (uint8_t)n;
}

void broadcastStateToSlaves() {
  if (deviceMode != DEVICE_MASTER) return;

  String path = "/set?r=" + String(rouge ? 1 : 0);
  path += "&v=" + String(vert ? 1 : 0);
  path += "&jb=" + String(jauneB ? 1 : 0);
  path += "&jd=" + String(jauneD ? 1 : 0);

  for (uint8_t i = 0; i < MAX_SLAVES; i++) {
    if (!slaves[i].active) continue;
    sendHttpGet(slaves[i].ip, path);
  }
}

void sendStateToSlave(uint8_t slaveNum) {
  if (deviceMode != DEVICE_MASTER) return;
  int idx = findSlaveIndexByNum(slaveNum);
  if (idx < 0) return;

  String path = "/set?r=" + String(rouge ? 1 : 0);
  path += "&v=" + String(vert ? 1 : 0);
  path += "&jb=" + String(jauneB ? 1 : 0);
  path += "&jd=" + String(jauneD ? 1 : 0);

  sendHttpGet(slaves[idx].ip, path);
}

void sendAspectToSlave(uint8_t slaveNum, const String& aspect) {
  if (deviceMode != DEVICE_MASTER) return;
  int idx = findSlaveIndexByNum(slaveNum);
  if (idx < 0) return;

  String path = "/aspect?a=" + aspect;
  sendHttpGet(slaves[idx].ip, path);
}

void sendRegisterToMaster() {
  if (deviceMode != DEVICE_SLAVE) return;
  if (WiFi.status() != WL_CONNECTED) return;
  String host = "192.168.100.10";

  String path = "/api/register?num=" + String(signalNumber);
  path += "&name=" + urlEncode(currentDeviceLabel());
  path += "&ip=" + urlEncode(WiFi.localIP().toString());
  path += "&aspect=" + urlEncode(getCurrentAspectName());
  path += "&setupssid=" + urlEncode(setupAPName);
  path += "&side=" + String(isCounterVoie ? "contre" : "voie");

  Serial.print("REGISTER -> ");
  Serial.println(path);

  bool ok = sendHttpGet(host, path);
  Serial.print("REGISTER RESULTAT : ");
  Serial.println(ok ? "OK" : "ECHEC");
}

void sendHeartbeatToMaster() {
  if (deviceMode != DEVICE_SLAVE) return;
  if (WiFi.status() != WL_CONNECTED) return;
  String host = "192.168.100.10";

  String path = "/api/heartbeat?num=" + String(signalNumber);
  path += "&ip=" + urlEncode(WiFi.localIP().toString());
  path += "&aspect=" + urlEncode(getCurrentAspectName());
  path += "&setupssid=" + urlEncode(setupAPName);
  path += "&side=" + String(isCounterVoie ? "contre" : "voie");

  bool ok = sendHttpGet(host, path);
  Serial.print("HEARTBEAT RESULTAT : ");
  Serial.println(ok ? "OK" : "ECHEC");
}

void ensureSlaveConnected() {
  if (deviceMode != DEVICE_SLAVE) return;

  if (WiFi.status() == WL_CONNECTED) {
    localIpString = WiFi.localIP().toString();
    return;
  }

  if (millis() - lastReconnectTry < 5000UL) return;
  lastReconnectTry = millis();

  Serial.println("Reconnexion au maitre...");
  bool connected = connectToMasterSTA(true, signalNumber, 8000UL);

  if (connected) {
    localIpString = WiFi.localIP().toString();
    Serial.print("Reconnexion OK, IP : ");
    Serial.println(localIpString);
    sendRegisterToMaster();
  } else {
    localIpString = buildSlaveIpString(signalNumber) + " (non connecte)";
    Serial.println("Reconnexion echouee");
  }
}

void registerOrUpdateSlave(uint8_t num, const String& name, const String& ip, const String& aspect, const String& setupSsid, bool counterVoie) {
  if (ip.length() == 0 && setupSsid.length() == 0) return;

  int idx = -1;
  if (setupSsid.length()) idx = findSlaveIndexBySetupSsid(setupSsid);
  if (idx < 0 && ip.length()) idx = findSlaveIndexByIp(ip);
  if (idx < 0 && num > 0 && num != signalNumber) idx = findSlaveIndexByNum(num);
  if (idx < 0) idx = firstFreeSlaveIndex();
  if (idx < 0) return;

  uint8_t effectiveNum = num;
  if (effectiveNum == 0 || effectiveNum == signalNumber) effectiveNum = firstAvailableSlaveNumber();
  int conflictIdx = findSlaveIndexByNum(effectiveNum);
  if (conflictIdx >= 0 && conflictIdx != idx) effectiveNum = firstAvailableSlaveNumber();

  slaves[idx].active = true;
  slaves[idx].num = effectiveNum;
  slaves[idx].name = buildSlaveName(effectiveNum);
  if (name.length() && name.indexOf("Esclave") < 0) slaves[idx].name = name;
  slaves[idx].ip = ip;
  slaves[idx].aspect = aspect;
  slaves[idx].setupSsid = setupSsid.length() ? setupSsid : buildSetupName(effectiveNum);
  slaves[idx].counterVoie = counterVoie;
  slaves[idx].lastSeen = millis();
}

void clearSlaveEntry(uint8_t idx) {
  if (idx >= MAX_SLAVES) return;
  slaves[idx].active = false;
  slaves[idx].num = 0;
  slaves[idx].name = "";
  slaves[idx].ip = "";
  slaves[idx].aspect = "";
  slaves[idx].setupSsid = "";
  slaves[idx].counterVoie = false;
  slaves[idx].lastSeen = 0;
}

void cleanStaleSlaves() {
  if (deviceMode != DEVICE_MASTER) return;
  if (millis() - lastRegistryClean < 3000UL) return;
  lastRegistryClean = millis();

  for (uint8_t i = 0; i < MAX_SLAVES; i++) {
    if (!slaves[i].active) continue;
    if (millis() - slaves[i].lastSeen > 12000UL) {
      clearSlaveEntry(i);
    }
  }
}

// ================= SVG =================
int mirrorCoord(int x, int width, bool mirror) {
  if (!mirror) return x;
  return width - x;
}

String signalOrientationLabel() {
  return isCounterVoie ? tr("Contre-voie", "Tegenspoor") : tr("Voie", "Spoor");
}

String drawSignalSVG(bool clickable) {
  String svg;
  String cl = clickable ? " style='cursor:pointer'" : "";
  bool mirror = isCounterVoie;

  int xMain = mirrorCoord(95, 420, mirror);
  int xDeporte = mirrorCoord(245, 420, mirror);

  // Les 4 LED principales restent strictement a leur place.
  // Les deux mini-combinaisons se placent dans la colonne du jaune deporte.
  int miniGap = 17;
  int xComboVert = mirrorCoord(245 - miniGap, 420, mirror);
  int xComboJaune = mirrorCoord(245 + miniGap, 420, mirror);
  int xDouble1 = mirrorCoord(245 - miniGap, 420, mirror);
  int xDouble2 = mirrorCoord(245 + miniGap, 420, mirror);

  svg += "<svg viewBox='0 0 420 340' aria-label='" + tr("Signal SNCB_NMBS simplifie", "Vereenvoudigd signaal SNCB_NMBS") + "'>";

  svg += "<g" + cl + (clickable ? " onclick=\"tapLed('vert')\"" : "") + ">";
  svg += "<circle cx='" + String(xMain) + "' cy='75' r='30' fill='#dffcff'/>";
  svg += "<circle cx='" + String(xMain) + "' cy='75' r='23' fill='#145a22' stroke='#0d0f12' stroke-width='3'/>";
  svg += "<circle cx='" + String(xMain) + "' cy='75' r='18' fill='" + String(vert ? "#2ef06f" : "#145a22") + "'/>";
  svg += "<circle cx='" + String(xMain - 9) + "' cy='66' r='5.5' fill='#ffffff' opacity='0.72'/>";
  svg += "</g>";

  svg += "<g" + cl + (clickable ? " onclick=\"tapLed('rouge')\"" : "") + ">";
  svg += "<circle cx='" + String(xMain) + "' cy='160' r='32' fill='#ffe7ef'/>";
  svg += "<circle cx='" + String(xMain) + "' cy='160' r='24' fill='#6a1010' stroke='#0d0f12' stroke-width='3'/>";
  svg += "<circle cx='" + String(xMain) + "' cy='160' r='19' fill='" + String(rouge ? "#ff4b4b" : "#6a1010") + "'/>";
  svg += "<circle cx='" + String(xMain - 10) + "' cy='150' r='5.5' fill='#ffffff' opacity='0.72'/>";
  svg += "</g>";

  svg += "<g" + cl + (clickable ? " onclick=\"tapLed('jaunebas')\"" : "") + ">";
  svg += "<circle cx='" + String(xMain) + "' cy='255' r='30' fill='#f3ffd9'/>";
  svg += "<circle cx='" + String(xMain) + "' cy='255' r='23' fill='#6a5a00' stroke='#0d0f12' stroke-width='3'/>";
  svg += "<circle cx='" + String(xMain) + "' cy='255' r='18' fill='" + String(jauneB ? "#ffe34a" : "#6a5a00") + "'/>";
  svg += "<circle cx='" + String(xMain - 9) + "' cy='246' r='5.5' fill='#ffffff' opacity='0.72'/>";
  svg += "</g>";

  svg += "<g" + cl + (clickable ? " onclick=\"tapLed('jauned')\"" : "") + ">";
  svg += "<circle cx='" + String(xDeporte) + "' cy='75' r='30' fill='#f3ffd9'/>";
  svg += "<circle cx='" + String(xDeporte) + "' cy='75' r='23' fill='#6a5a00' stroke='#0d0f12' stroke-width='3'/>";
  svg += "<circle cx='" + String(xDeporte) + "' cy='75' r='18' fill='" + String(jauneD ? "#ffe34a" : "#6a5a00") + "'/>";
  svg += "<circle cx='" + String(xDeporte - 9) + "' cy='66' r='5.5' fill='#ffffff' opacity='0.72'/>";
  svg += "</g>";

  svg += "<g" + cl + (clickable ? " onclick=\"tapLed('vertjauned')\"" : "") + ">";
  svg += "<circle cx='" + String(xComboVert) + "' cy='177' r='15' fill='#dffcff'/>";
  svg += "<circle cx='" + String(xComboVert) + "' cy='177' r='11' fill='#145a22' stroke='#0d0f12' stroke-width='2'/>";
  svg += "<circle cx='" + String(xComboVert) + "' cy='177' r='8' fill='" + String((vert && jauneD && !rouge && !jauneB) ? "#2ef06f" : "#145a22") + "'/>";
  svg += "<circle cx='" + String(xComboVert - 4) + "' cy='173' r='2.5' fill='#ffffff' opacity='0.72'/>";

  svg += "<circle cx='" + String(xComboJaune) + "' cy='177' r='15' fill='#f3ffd9'/>";
  svg += "<circle cx='" + String(xComboJaune) + "' cy='177' r='11' fill='#6a5a00' stroke='#0d0f12' stroke-width='2'/>";
  svg += "<circle cx='" + String(xComboJaune) + "' cy='177' r='8' fill='" + String((vert && jauneD && !rouge && !jauneB) ? "#ffe34a" : "#6a5a00") + "'/>";
  svg += "<circle cx='" + String(xComboJaune - 4) + "' cy='173' r='2.5' fill='#ffffff' opacity='0.72'/>";
  svg += "</g>";

  svg += "<g" + cl + (clickable ? " onclick=\"tapLed('doublejaune')\"" : "") + ">";
  svg += "<circle cx='" + String(xDouble1) + "' cy='270' r='15' fill='#f3ffd9'/>";
  svg += "<circle cx='" + String(xDouble1) + "' cy='270' r='11' fill='#6a5a00' stroke='#0d0f12' stroke-width='2'/>";
  svg += "<circle cx='" + String(xDouble1) + "' cy='270' r='8' fill='" + String((jauneD && jauneB) ? "#ffe34a" : "#6a5a00") + "'/>";
  svg += "<circle cx='" + String(xDouble1 - 4) + "' cy='266' r='2.5' fill='#ffffff' opacity='0.72'/>";

  svg += "<circle cx='" + String(xDouble2) + "' cy='255' r='15' fill='#f3ffd9'/>";
  svg += "<circle cx='" + String(xDouble2) + "' cy='255' r='11' fill='#6a5a00' stroke='#0d0f12' stroke-width='2'/>";
  svg += "<circle cx='" + String(xDouble2) + "' cy='255' r='8' fill='" + String((jauneD && jauneB) ? "#ffe34a" : "#6a5a00") + "'/>";
  svg += "<circle cx='" + String(xDouble2 - 4) + "' cy='251' r='2.5' fill='#ffffff' opacity='0.72'/>";
  svg += "</g>";

  svg += "</svg>";
  return svg;
}

String commonStyle() {
  String css;
  css += "body{background:#0b1d3a;color:white;text-align:center;font-family:Arial;margin:0;padding:10px}";
  css += ".wrap{max-width:1040px;margin:0 auto}";
  css += ".card{background:#10203a;border:1px solid rgba(96,165,250,.18);border-radius:20px;padding:14px;margin-bottom:12px}";
  css += ".topTitle{font-size:24px;font-weight:800;margin-bottom:8px}";
  css += ".small{color:#cfe6ff;font-size:15px;font-weight:700;margin-bottom:6px}";
  css += ".topLink a,.nav a{color:#8ec5ff;font-size:16px;font-weight:700;text-decoration:none}";
  css += ".mainTitle{font-size:28px;font-weight:800;margin:8px 0}";
  css += ".badge{display:block;padding:10px;border-radius:14px;background:#0e1d34;margin-top:8px}";
  css += ".sectionTitle{font-size:20px;font-weight:800;margin:0 0 12px 0}";
  css += ".btnGrid{display:grid;grid-template-columns:1fr 1fr;gap:10px}";
  css += ".btnGridSingle{display:grid;grid-template-columns:1fr;gap:10px}";
  css += ".triGrid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px}";
  css += "button,.fakeBtn{width:100%;min-height:52px;padding:10px;border:none;border-radius:14px;background:linear-gradient(180deg,#2f6fed,#1f56c0);color:#fff;font-size:15px;font-weight:800}";
  css += "svg{width:95%;max-width:420px}";
  css += ".signalTools{display:flex;justify-content:center;align-items:center;gap:10px;flex-wrap:wrap;margin-top:10px}";
  css += ".iconBtn{width:58px;height:58px;min-height:58px;border-radius:16px;display:flex;align-items:center;justify-content:center;padding:0;border:1px solid rgba(255,255,255,.18);box-shadow:0 6px 18px rgba(0,0,0,.25)}";
  css += ".iconBtn svg{width:30px;height:30px}";
  css += ".metalBtn{background:linear-gradient(180deg,#d9dde2,#8d98a8);color:#1d2633}";
  css += ".blinkInfo{font-size:13px;color:#cfe6ff;margin-top:8px}";
  css += ".progHeader,.progGrid{display:grid;grid-template-columns:62px 1fr 74px;gap:8px;align-items:center;margin:8px 0}";
  css += ".progHeader{font-size:12px;color:#cfe6ff;font-weight:700}";
  css += "select,input{width:100%;padding:10px;border-radius:12px;border:1px solid rgba(96,165,250,.18);background:#081321;color:#fff;font-size:14px;box-sizing:border-box}";
  css += ".hint,.code,.footerInfo,.tiny{font-size:13px;color:#cfe6ff}";
  css += ".code{background:#08111d;border:1px solid rgba(255,255,255,.06);padding:10px;border-radius:12px;margin:8px 0;text-align:left}";
  css += ".row2{display:grid;grid-template-columns:1fr 1fr;gap:10px}";
  css += ".row3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px}";
  css += ".tableWrap{overflow:auto}";
  css += "table{width:100%;border-collapse:collapse}";
  css += "th,td{border-bottom:1px solid rgba(255,255,255,.08);padding:8px;text-align:center;font-size:14px}";
  css += ".statusOn{color:#73ff9d;font-weight:800}";
  css += ".statusOff{color:#ff8c8c;font-weight:800}";
  css += ".nav{margin:10px 0 16px 0}";
  css += ".multiGrid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px;align-items:start}";
  css += ".multiCard svg{max-width:220px;width:100%;height:auto}";
  css += ".clickHint{font-size:12px;color:#8ec5ff;font-weight:700;margin-top:6px}";
  css += "@media (max-width:900px){.multiGrid{grid-template-columns:repeat(2,minmax(0,1fr));}}";
  css += "@media (max-width:640px){.multiGrid{grid-template-columns:1fr;}}";
  return css;
}

String topHeaderCard() {
  String html;
  html += "<div class='card'>";
  html += "<div class='topTitle'>" + tr("Produit par PM3D", "Geproduceerd door PM3D") + "</div>";
  html += "<div class='small'>" + tr("Mode d'emploi disponible sur le site", "Handleiding beschikbaar op de website") + " - V" + String(FW_VERSION) + "</div>";
  html += "</div>";
  return html;
}

String languageBar() {
  String html;
  html += "<div class='card nav'>";
  html += "<span style='margin-right:10px;font-weight:800'>" + tr("Langue", "Taal") + "</span>";
  html += "<a href='/setlang?lang=fr'>FR</a>";
  html += " &nbsp;|&nbsp; ";
  html += "<a href='/setlang?lang=nl'>NL</a>";
  html += "</div>";
  return html;
}

String navBar() {
  String html;
  html += "<div class='card nav'>";
  html += "<a href='/'>" + tr("Accueil", "Start") + "</a>";
  if (deviceMode == DEVICE_MASTER) {
    html += " &nbsp;|&nbsp; <a href='/multi'>" + tr("Controle multiple", "Meervoudige bediening") + "</a>";
  }
  if (deviceMode == DEVICE_SLAVE) {
    html += " &nbsp;|&nbsp; <a href='/setup'>" + tr("Choisir le mode Server", "Servermodus kiezen") + "</a>";
  }
  html += "</div>";
  return html;
}

String setupSectionHtml() {
  String html;
  html += "<div class='card'><div class='sectionTitle'>" + tr("Configuration du signal", "Configuratie van het sein") + "</div>";
  html += "<div class='row3'>";
  html += "<div><div class='tiny'>" + tr("Numero du signal", "Nummer van het signaal") + "</div><input id='signalNum' type='number' min='1' max='99' value='" + String(signalNumber) + "'></div>";
  html += "<div><div class='tiny'>" + tr("Mode", "Modus") + "</div>";
  html += "<input type='hidden' id='deviceMode' value='" + String(deviceMode == DEVICE_MASTER ? "master" : (deviceMode == DEVICE_SLAVE ? "slave" : "solo")) + "'>";
  html += "<div class='btnGrid'>";
  html += "<button type='button' id='btnModeSolo' onclick=\"selectSetupMode('solo')\">" + tr("Standalone", "Standalone") + "</button>";
  html += "<button type='button' id='btnModeMaster' onclick=\"selectSetupMode('master')\">" + tr("Master", "Master") + "</button>";
  html += "<button type='button' id='btnModeSlave' onclick=\"selectSetupMode('slave')\">" + tr("Client", "Client") + "</button>";
  html += "</div></div>";
  html += "<div><div class='tiny'>" + tr("Numero du Server", "Servernummer") + "</div><input id='masterNum' type='number' min='1' max='99' value='" + String(masterNumber) + "'></div>";
  html += "</div>";

  html += "<div class='row2' style='margin-top:10px'>";
  html += "<div><div class='tiny'>" + tr("SSID Server", "Server-SSID") + "</div><input id='masterSSID' value='" + htmlEscape(masterSSID) + "'></div>";
  html += "<div><div class='tiny'>" + tr("Mot de passe Server", "Server-wachtwoord") + "</div><input id='masterPass' value='" + htmlEscape(masterPassword) + "'></div>";
  html += "</div>";

  html += "<div class='btnGrid' style='margin-top:10px'>";
  html += "<button onclick='saveSetup()'>" + tr("Sauvegarder le mode", "Modus opslaan") + "</button>";
  html += "<button onclick='scanMasters()'>" + tr("Scanner les Masters Wi-Fi", "Wi-Fi Masters scannen") + "</button>";
  html += "</div>";

  html += "<div id='scanResults' class='code'>" + tr("Aucun scan lance pour le moment", "Nog geen scan gestart") + "</div>";

  html += "<div class='footerInfo'>" + tr("Par defaut le signal reste en <b>", "Standaard blijft het sein in <b>") + buildStandaloneName(signalNumber) + "</b>. " + tr("Le nom <b>SNCB_NMBS Standalone X</b> reste actif tant que vous ne passez pas explicitement en Master ou en Client.", "De naam <b>SNCB_NMBS Standalone X</b> blijft actief zolang u niet expliciet naar Master of Client overschakelt.") + "</div>";
  html += "</div>";
  return html;
}

String ledConfigSectionHtml() {
  String html;
  html += "<div class='card'><div class='sectionTitle'>" + tr("Reglage LED", "LED-instelling") + "</div>";
  html += "<div class='tiny'>" + tr("Attribuez la bonne sortie a la bonne LED sans recompiler, puis choisissez si le signal est pose en voie ou en contre-voie.", "Wijs zonder hercompileren de juiste uitgang aan de juiste LED toe en kies daarna of het sein op spoor of op tegenspoor staat.") + "</div>";
  html += "<div class='row2' style='margin-top:10px'>";
  html += "<div><div class='tiny'>" + tr("LED verte", "Groene LED") + "</div><input id='pinVert' type='number' min='0' max='21' value='" + String(pinLedVert) + "'></div>";
  html += "<div><div class='tiny'>" + tr("LED rouge", "Rode LED") + "</div><input id='pinRouge' type='number' min='0' max='21' value='" + String(pinLedRouge) + "'></div>";
  html += "</div>";
  html += "<div class='row2' style='margin-top:10px'>";
  html += "<div><div class='tiny'>" + tr("Jaune bas", "Onder geel") + "</div><input id='pinJBas' type='number' min='0' max='21' value='" + String(pinLedJauneBas) + "'></div>";
  html += "<div><div class='tiny'>" + tr("Jaune deporte / double jaune", "Verplaatst geel / dubbel geel") + "</div><input id='pinJD' type='number' min='0' max='21' value='" + String(pinLedJauneD) + "'></div>";
  html += "</div>";
  html += "<div class='row2' style='margin-top:10px'>";
  html += "<div><div class='tiny'>" + tr("Sens du signal", "Richting van het sein") + "</div><select id='sigSide'><option value='voie'" + selectedIf(!isCounterVoie) + ">" + tr("Voie", "Spoor") + "</option><option value='contre'" + selectedIf(isCounterVoie) + ">" + tr("Contre-voie", "Tegenspoor") + "</option></select></div>";
  html += "<div><div class='tiny'>" + tr("Effet sur le dessin", "Effect op de tekening") + "</div><div class='code' style='margin-top:0'>" + tr("Le dessin passe automatiquement en miroir quand vous choisissez <b>Contre-voie</b>.", "De tekening wordt automatisch gespiegeld wanneer u <b>Tegenspoor</b> kiest.") + "</div></div>";
  html += "</div>";
  html += "<div class='btnGridSingle' style='margin-top:10px'><button onclick='saveLedConfig()'>" + tr("Sauvegarder le reglage LED", "LED-instelling opslaan") + "</button></div>";
  html += "<div class='code'>" + tr("Sens", "Richting") + " = " + signalOrientationLabel() + "<br>" + tr("Vert", "Groen") + " = GPIO " + String(pinLedVert) + "<br>" + tr("Rouge", "Rood") + " = GPIO " + String(pinLedRouge) + "<br>" + tr("Jaune bas", "Onder geel") + " = GPIO " + String(pinLedJauneBas) + "<br>" + tr("Jaune deporte", "Verplaatst geel") + " = GPIO " + String(pinLedJauneD) + "</div>";
  html += "</div>";
  return html;
}

String programSectionHtml() {
  String html;
  html += "<div class='card'><div class='sectionTitle'>" + tr("Creer son propre programme", "Eigen programma maken") + "</div>";
  html += "<div class='progHeader'><div>" + tr("Etape", "Stap") + "</div><div>" + tr("Aspect", "Seinbeeld") + "</div><div>" + tr("Duree (sec)", "Duur (sec)") + "</div></div>";
  for (uint8_t i = 0; i < progStepCount; i++) {
    html += "<div class='progGrid'><div>" + tr("Etape", "Stap") + " " + String(i + 1) + "</div>";
    html += "<div><select id='a" + String(i) + "'>";
    html += "<option value='off'" + selectedIf(progAspects[i] == "off") + ">" + tr("Non utilise", "Niet gebruikt") + "</option>";
    html += "<option value='rouge'" + selectedIf(progAspects[i] == "rouge") + ">" + tr("Rouge", "Rood") + "</option>";
    html += "<option value='vert'" + selectedIf(progAspects[i] == "vert") + ">" + tr("Vert", "Groen") + "</option>";
    html += "<option value='jb'" + selectedIf(progAspects[i] == "jb") + ">" + tr("Jaune bas", "Onder geel") + "</option>";
    html += "<option value='jd'" + selectedIf(progAspects[i] == "jd") + ">" + tr("Jaune deporte", "Verplaatst geel") + "</option>";
    html += "<option value='vjd'" + selectedIf(progAspects[i] == "vjd") + ">" + tr("Vert + jaune deporte", "Groen + verplaatst geel") + "</option>";
    html += "<option value='2j'" + selectedIf(progAspects[i] == "2j") + ">" + tr("2 jaunes", "2 gelen") + "</option>";
    html += "</select></div>";
    html += "<div><input id='d" + String(i) + "' type='number' min='1' max='999' value='" + String(progDurations[i]) + "'></div></div>";
  }
  html += "<div class='btnGridSingle'><button onclick='saveProgram()'>" + tr("Sauvegarder le programme", "Programma opslaan") + "</button><button onclick=\"go('/mode?m=prog')\">" + tr("Lancer ce programme", "Dit programma starten") + "</button><button onclick=\"go('/mode?m=auto')\">" + tr("Revenir au programme automatique", "Terug naar automatisch programma") + "</button></div>";
  html += "</div>";
  return html;
}

String blinkIconSvg() {
  String svg;
  svg += "<svg viewBox='0 0 64 64' aria-hidden='true'>";
  svg += "<path d='M24 18c0-5 4-9 8-9s8 4 8 9v4H24z' fill='white'/>";
  svg += "<rect x='22' y='22' width='20' height='18' rx='5' fill='white'/>";
  svg += "<rect x='28' y='40' width='8' height='9' rx='2' fill='white'/>";
  svg += "<rect x='22' y='49' width='20' height='4' rx='2' fill='white'/>";
  svg += "<path d='M14 24l-5-3' stroke='white' stroke-width='4' stroke-linecap='round'/>";
  svg += "<path d='M50 24l5-3' stroke='white' stroke-width='4' stroke-linecap='round'/>";
  svg += "<path d='M12 34H6' stroke='white' stroke-width='4' stroke-linecap='round'/>";
  svg += "<path d='M52 34h6' stroke='white' stroke-width='4' stroke-linecap='round'/>";
  svg += "</svg>";
  return svg;
}

String gearIconSvg() {
  String svg;
  svg += "<svg viewBox='0 0 64 64' aria-hidden='true'>";
  svg += "<path d='M36 7l2 6c2 0 4 1 6 2l5-3 6 10-5 4c1 2 1 4 1 6l6 2-3 12-6-1c-1 2-3 3-5 4l-1 6H30l-2-6c-2-1-4-2-5-4l-6 1-3-12 6-2c0-2 0-4 1-6l-5-4 6-10 5 3c2-1 4-2 6-2l2-6z' fill='#5d6675'/>";
  svg += "<circle cx='32' cy='32' r='10' fill='#f4f6f8'/><circle cx='32' cy='32' r='4' fill='#7b8593'/>";
  svg += "</svg>";
  return svg;
}

String blinkToolsHtml() {
  String html;
  html += "<div class='signalTools'>";
  html += "<button class='iconBtn' title='" + blinkStateLabel() + "' style='background:" + blinkButtonColor() + "' onclick=\"go('/blink/toggle')\">" + blinkIconSvg() + "</button>";
  html += "<button class='iconBtn metalBtn' title='" + tr("Regler le clignotement", "Knipperen instellen") + "' onclick=\"location.href='/blink'\">" + gearIconSvg() + "</button>";
  html += "</div>";
  html += "<div class='blinkInfo'>" + blinkStateLabel() + " - " + blinkEffectLabel() + " - " + tr("ON", "AAN") + " " + String(blinkOnMs) + " ms / " + tr("OFF", "UIT") + " " + String(blinkOffMs) + " ms / " + tr("ARRET", "STOP") + " " + String(blinkStopMs) + " ms</div>";
  return html;
}

String blinkPageHtml() {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>" + tr("Parametres clignotement", "Knipperinstellingen") + "</title>";
  html += "<style>" + commonStyle() + "</style></head><body><div class='wrap'>";
  html += topHeaderCard();
  html += languageBar();
  html += navBar();
  html += "<div class='card'><div class='sectionTitle'>" + tr("Parametres du clignotement", "Parameters van het knipperen") + "</div>";
  html += "<div class='footerInfo'>" + tr("Le bouton gyrophare vert active le clignotement du feu. Rouge = clignotement coupe. Les durees ci-dessous s'appliquent a l'aspect actuellement affiche.", "De groene zwaailichtknop activeert het knipperen van het sein. Rood = knipperen uit. De tijden hieronder gelden voor het momenteel getoonde seinbeeld.") + "</div>";
  html += "<div class='row3' style='margin-top:12px'>";
  html += "<div><div class='tiny'>" + tr("Temps allume / montee (ms)", "AAN-tijd / stijgen (ms)") + "</div><input id='blinkOn' type='number' min='50' max='5000' value='" + String(blinkOnMs) + "'></div>";
  html += "<div><div class='tiny'>" + tr("Temps eteint / descente (ms)", "UIT-tijd / dalen (ms)") + "</div><input id='blinkOff' type='number' min='50' max='5000' value='" + String(blinkOffMs) + "'></div>";
  html += "<div><div class='tiny'>" + tr("Temps d'arret bas (ms)", "Lage stop-tijd (ms)") + "</div><input id='blinkStop' type='number' min='0' max='5000' value='" + String(blinkStopMs) + "'></div>";
  html += "</div>";
  html += "<div style='margin-top:12px'><div class='tiny'>" + tr("Style visuel", "Visuele stijl") + "</div><select id='blinkFx'><option value='hard'" + blinkEffectOption("hard") + ">" + tr("Clignotement franc", "Hard knipperen") + "</option><option value='fade'" + blinkEffectOption("fade") + ">" + tr("Effet fondu", "Fade-effect") + "</option><option value='stop'" + blinkEffectOption("stop") + ">" + tr("Arret", "Stop") + "</option></select></div>";
  html += "<div class='btnGrid' style='margin-top:12px'>";
  html += "<button onclick='saveBlinkConfigPage()'>" + tr("Sauvegarder", "Opslaan") + "</button>";
  html += "<button onclick=\"location.href='/'\">" + tr("Retour accueil", "Terug naar start") + "</button>";
  html += "</div>";
  html += "<div class='code'>" + tr("Etat actuel", "Huidige status") + " : <b>" + blinkStateLabel() + "</b><br>" + tr("Style actuel", "Huidige stijl") + " : <b>" + blinkEffectLabel() + "</b><br>" + tr("En mode fondu, le temps d'arret bas ajoute une pause LED eteinte avant la remontee.", "In fade-modus voegt de lage stop-tijd een pauze toe terwijl de LED uit blijft voordat ze weer opkomt.") + "<br>" + tr("Bouton gyrophare vert = actif, rouge = inactif.", "Groene zwaailichtknop = actief, rode = inactief.") + "</div>";
  html += "</div>";
  html += "<script>";
  html += "function saveBlinkConfigPage(){var q='/blink/save?on='+encodeURIComponent(document.getElementById('blinkOn').value)+'&off='+encodeURIComponent(document.getElementById('blinkOff').value)+'&stop='+encodeURIComponent(document.getElementById('blinkStop').value)+'&fx='+encodeURIComponent(document.getElementById('blinkFx').value);fetch(q).then(r=>r.text()).then(t=>alert(t)).then(()=>setTimeout(()=>location.reload(),180)).catch(()=>alert('" + tr("Erreur", "Fout") + "'));}";
  html += "</script>";
  html += "</div></body></html>";
  return html;
}

String controlSectionHtml() {
  String html;
  html += "<div class='card'><div class='sectionTitle'>" + tr("Panneau de controle", "Bedieningspaneel") + "</div>";
  html += drawSignalSVG(true);
  html += blinkToolsHtml();
  html += "</div>";

  html += "<div class='card'><div class='sectionTitle'>" + tr("Choisir le fonctionnement", "Werking kiezen") + "</div>";
  html += "<div class='btnGrid'>";
  html += "<button onclick=\"go('/mode?m=off')\">" + tr("Tout eteint", "Alles uit") + "</button>";
  html += "<button onclick=\"go('/mode?m=manuel')\">" + tr("Mode manuel", "Handmatige modus") + "</button>";
  html += "<button onclick=\"go('/mode?m=auto')\">" + tr("Mode automatique", "Automatische modus") + "</button>";
  html += "<button onclick=\"go('/mode?m=prog')\">" + tr("Programme perso", "Eigen programma") + "</button>";
  html += "<button onclick=\"go('/mode?m=app')\">" + tr("Appareillage", "Koppeling") + "</button>";
  html += "<button onclick='location.reload()'>" + tr("Rafraichir", "Vernieuwen") + "</button>";
  html += "</div></div>";
  return html;
}

String statusCardHtml() {
  String html;
  html += "<div class='card'><div class='mainTitle'>" + currentDeviceLabel() + "</div>";
  html += "<span class='badge'>" + tr("Mode appareil :", "Apparaatmodus :") + " <b>" + deviceModeName(deviceMode) + "</b></span>";
  html += "<span class='badge'>" + tr("Mode de marche :", "Bedrijfsmodus :") + " <b>" + runModeName(runMode) + "</b></span>";
  html += "<span class='badge'>" + tr("Aspect :", "Seinbeeld :") + " <b>" + aspectLabelUi(getCurrentAspectName()) + "</b></span>";
  html += "<span class='badge'>" + tr("Sens :", "Richting :") + " <b>" + signalOrientationLabel() + "</b></span>";

  if (deviceMode == DEVICE_MASTER || deviceMode == DEVICE_STANDALONE) {
    html += "<span class='badge'>Wi-Fi : <b>" + apName + "</b></span>";
    html += "<span class='badge'>" + tr("Adresse IP :", "IP-adres :") + " <b>" + localIpString + "</b></span>";
  } else if (deviceMode == DEVICE_SLAVE) {
    html += "<span class='badge'>" + tr("Server cible :", "Doelserver :") + " <b>" + htmlEscape(masterSSID) + "</b></span>";
    html += "<span class='badge'>" + tr("IP locale Client :", "Lokaal Client-IP :") + " <b>" + localIpString + "</b></span>";
    html += "<span class='badge'>" + tr("AP de secours :", "Nood-AP :") + " <b>" + setupAPName + "</b></span>";
  }

  html += "</div>";
  return html;
}



String htmlEscapeAttr(const String& s) {
  String out = htmlEscape(s);
  out.replace("'", "&#39;");
  return out;
}

String trimCopy(String s) {
  s.trim();
  return s;
}

bool sameVersionLabel(String a, String b) {
  a.trim();
  b.trim();
  a.toUpperCase();
  b.toUpperCase();
  return a == b;
}

String normalizeFirmwareUrl(String url) {
  url.trim();
  const String prefix = "https://github.com/PM3D-Wavre/pm3d-firmware/raw/refs/heads/main/";
  if (url.startsWith(prefix)) {
    url = "https://raw.githubusercontent.com/PM3D-Wavre/pm3d-firmware/main/" + url.substring(prefix.length());
  }
  return url;
}

void addOtaVersionItem(const String& versionIn, const String& urlIn) {
  String version = trimCopy(versionIn);
  String url = normalizeFirmwareUrl(urlIn);
  if (version.length() == 0 || url.length() == 0) return;

  for (uint8_t i = 0; i < otaVersionCount; i++) {
    if (sameVersionLabel(otaVersions[i].version, version) || otaVersions[i].url == url) {
      otaVersions[i].version = version;
      otaVersions[i].url = url;
      return;
    }
  }

  if (otaVersionCount < OTA_MAX_VERSIONS) {
    otaVersions[otaVersionCount].version = version;
    otaVersions[otaVersionCount].url = url;
    otaVersionCount++;
  }
}

String extractTokenAfter(const String& body, const String& key) {
  int start = body.indexOf(key);
  if (start < 0) return "";
  start += key.length();
  while (start < body.length() && (body.charAt(start) == ' ' || body.charAt(start) == '\t' || body.charAt(start) == '"')) start++;
  int end = start;
  while (end < body.length()) {
    char c = body.charAt(end);
    if (c == '\r' || c == '\n' || c == ' ' || c == '\t' || c == ',' || c == ']') break;
    end++;
  }
  String out = body.substring(start, end);
  out.trim();
  return out;
}

void parseJsonStyleOtaEntries(const String& body) {
  int pos = 0;
  while (true) {
    int vKey = body.indexOf("\"version\"", pos);
    if (vKey < 0) break;
    int vColon = body.indexOf(':', vKey);
    if (vColon < 0) break;
    int vQ1 = body.indexOf('"', vColon + 1);
    if (vQ1 < 0) break;
    int vQ2 = body.indexOf('"', vQ1 + 1);
    if (vQ2 < 0) break;
    String version = body.substring(vQ1 + 1, vQ2);

    int uKey = body.indexOf("\"url\"", vQ2);
    if (uKey < 0) break;
    int uColon = body.indexOf(':', uKey);
    if (uColon < 0) break;
    int uQ1 = body.indexOf('"', uColon + 1);
    if (uQ1 < 0) break;
    int uQ2 = body.indexOf('"', uQ1 + 1);
    if (uQ2 < 0) break;
    String url = body.substring(uQ1 + 1, uQ2);

    addOtaVersionItem(version, url);
    pos = uQ2 + 1;
  }
}

void resetOtaManifestState() {
  otaLatestVersion = "";
  otaLatestUrl = "";
  otaVersionCount = 0;
  otaLastCheckMessage = "";
  for (uint8_t i = 0; i < OTA_MAX_VERSIONS; i++) {
    otaVersions[i].version = "";
    otaVersions[i].url = "";
  }
}

bool ensureOtaWifiConnection(unsigned long timeoutMs = 12000UL) {
  if (deviceMode != DEVICE_STANDALONE) {
    otaWifiStatus = tr("Fonction reservee au mode Standalone", "Functie alleen voor Standalone-modus");
    otaWifiConnected = false;
    return false;
  }
  if (otaSSID.length() == 0) {
    otaWifiStatus = tr("Aucun Wi-Fi Internet enregistre", "Geen internet-wifi opgeslagen");
    otaWifiConnected = false;
    return false;
  }
  if (WiFi.status() == WL_CONNECTED) {
    otaWifiConnected = true;
    otaWifiStatus = String("OK - ") + WiFi.localIP().toString();
    return true;
  }
  WiFi.mode(WIFI_AP_STA);
  IPAddress zero(0,0,0,0);
  WiFi.config(zero, zero, zero);
  WiFi.begin(otaSSID.c_str(), otaPassword.c_str());
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(250);
  }
  otaWifiConnected = (WiFi.status() == WL_CONNECTED);
  otaWifiStatus = otaWifiConnected ? (String("OK - ") + WiFi.localIP().toString()) : tr("Connexion OTA impossible", "OTA-verbinding mislukt");
  return otaWifiConnected;
}

bool fetchOtaManifest() {
  resetOtaManifestState();
  if (!ensureOtaWifiConnection()) {
    otaLastCheckMessage = otaWifiStatus;
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, OTA_MANIFEST_URL)) {
    otaLastCheckMessage = tr("Manifest OTA inaccessible", "OTA-manifest niet bereikbaar");
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    otaLastCheckMessage = tr("Erreur de lecture du manifest", "Fout bij lezen manifest") + " HTTP " + String(code);
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  parseJsonStyleOtaEntries(body);

  otaLatestVersion = trimCopy(extractTokenAfter(body, "latest="));
  otaLatestUrl = normalizeFirmwareUrl(extractTokenAfter(body, "latest_url="));

  int pos = 0;
  while (pos < body.length()) {
    int next = body.indexOf('\n', pos);
    if (next < 0) next = body.length();
    String line = body.substring(pos, next);
    line.trim();
    pos = next + 1;
    if (line.length() == 0 || line.startsWith("#")) continue;

    if ((line.startsWith("v") || line.startsWith("prev")) && line.indexOf('=') > 0) {
      int eq = line.indexOf('=');
      String rhs = line.substring(eq + 1);
      int pipe = rhs.indexOf('|');
      if (pipe > 0) addOtaVersionItem(rhs.substring(0, pipe), rhs.substring(pipe + 1));
    }
  }

  if (otaLatestVersion.length() == 0 && otaVersionCount > 0) {
    otaLatestVersion = otaVersions[0].version;
    otaLatestUrl = otaVersions[0].url;
  }

  if (otaLatestVersion.length() && otaLatestUrl.length()) {
    bool latestAlreadyListed = false;
    for (uint8_t i = 0; i < otaVersionCount; i++) {
      if (sameVersionLabel(otaVersions[i].version, otaLatestVersion) || otaVersions[i].url == otaLatestUrl) {
        latestAlreadyListed = true;
        break;
      }
    }
    if (!latestAlreadyListed && otaVersionCount < OTA_MAX_VERSIONS) {
      for (int i = otaVersionCount; i > 0; --i) otaVersions[i] = otaVersions[i - 1];
      otaVersions[0].version = otaLatestVersion;
      otaVersions[0].url = otaLatestUrl;
      otaVersionCount++;
    }
  }

  if (otaVersionCount == 0 && (otaLatestVersion.length() == 0 || otaLatestUrl.length() == 0)) {
    otaLastCheckMessage = tr("Manifest incomplet", "Manifest onvolledig");
    return false;
  }

  if (otaLatestVersion.length() == 0 && otaVersionCount > 0) otaLatestVersion = otaVersions[0].version;
  if (otaLatestUrl.length() == 0 && otaVersionCount > 0) otaLatestUrl = otaVersions[0].url;

  otaLastCheckMessage = sameVersionLabel(otaLatestVersion, String(FW_VERSION))
    ? tr("Le signal est deja a jour", "Het sein is al up-to-date")
    : tr("Versions disponibles chargees", "Beschikbare versies geladen") + String(" : ") + String(otaVersionCount);
  return true;
}


bool installFirmwareFromUrl(const String& url, String &message) {
  if (url.length() == 0) {
    message = tr("URL firmware vide", "Firmware-URL leeg");
    return false;
  }
  if (!ensureOtaWifiConnection()) {
    message = otaWifiStatus;
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, url)) {
    message = tr("Connexion firmware impossible", "Firmwareverbinding mislukt");
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    message = tr("Telechargement impossible", "Download mislukt") + " HTTP " + String(code);
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  WiFiClient *stream = http.getStreamPtr();
  bool canBegin = (contentLength > 0) ? Update.begin(contentLength) : Update.begin(UPDATE_SIZE_UNKNOWN);
  if (!canBegin) {
    Update.printError(Serial);
    message = tr("Initialisation OTA impossible", "OTA-initialisatie mislukt");
    http.end();
    return false;
  }

  size_t written = Update.writeStream(*stream);
  bool ok = Update.end(true);
  http.end();

  if (!ok) {
    Update.printError(Serial);
    message = tr("Installation OTA echouee", "OTA-installatie mislukt");
    return false;
  }

  message = tr("Mise a jour terminee. Le signal redemarre.", "Update voltooid. Het sein herstart.");
  Serial.printf("OTA OK, %u octets\n", (unsigned)written);
  return true;
}

String otaVersionListHtml() {
  String html;
  if (otaVersionCount == 0) {
    html += "<div class='footerInfo'>" + tr("Aucune version chargee pour le moment", "Nog geen versies geladen") + "</div>";
    return html;
  }
  html += "<div class='code'>";
  html += "<b>" + tr("Toutes les versions disponibles", "Alle beschikbare versies") + "</b><br>";
  for (uint8_t i = 0; i < otaVersionCount; i++) {
    html += "<div style='margin-top:8px'><b>" + htmlEscape(otaVersions[i].version) + "</b>";
    if (sameVersionLabel(otaVersions[i].version, String(FW_VERSION))) html += " - " + tr("version actuelle", "huidige versie");
    html += " &nbsp; <a href='/otainstall?slot=" + String(i) + "'>" + tr("Installer", "Installeren") + "</a></div>";
  }
  html += "</div>";
  return html;
}

String firmwareUpdateButtonHtml() {
  if (deviceMode != DEVICE_STANDALONE) return "";
  String html;
  html += "<div class='card' style='text-align:center;margin-top:14px'>";
  html += "<div class='sectionTitle'>" + tr("Maintenance", "Onderhoud") + "</div>";
  html += "<button style='margin-top:12px' onclick=\"location.href='/update'\">" + tr("Mise a jour firmware", "Firmware-update") + "</button>";
  html += "</div>";
  return html;
}

String firmwareUpdatePageHtml() {
  fetchOtaManifest();
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>" + tr("Mise a jour firmware", "Firmware-update") + "</title>";
  html += "<style>" + commonStyle() + ".updBox{max-width:860px;margin:0 auto}.warn{background:#3a2b15;border:1px solid rgba(255,196,92,.35);color:#ffe0a3;padding:12px;border-radius:12px;margin-top:12px}.ok{background:#17361f;border:1px solid rgba(102,255,153,.25);color:#c7ffd9;padding:10px;border-radius:12px;margin-top:12px}.prog{width:100%;height:18px}.tinyCenter{text-align:center;font-size:13px;opacity:.85;margin-top:8px}.lineBtn{display:grid;grid-template-columns:1fr 1fr;gap:10px}.miniBtn{display:inline-block;padding:10px 14px;border-radius:12px;background:linear-gradient(180deg,#2f6fed,#1f56c0);color:#fff;font-weight:800;text-decoration:none}</style>";
  html += "</head><body><div class='wrap'>";
  html += topHeaderCard();
  html += languageBar();
  html += navBar();
  html += "<div class='card updBox'><div class='mainTitle'>" + tr("Mise a jour firmware", "Firmware-update") + " V" + String(FW_VERSION) + "</div>";
  html += "<div class='warn'>" + tr("Les reglages du client restent conserves. N'utilisez que des firmwares prevus pour ce modele.", "De klantinstellingen blijven behouden. Gebruik alleen firmware voor dit model.") + "</div>";

  html += "<div class='sectionTitle' style='margin-top:16px'>" + tr("Wi-Fi Internet pour OTA", "Internet-wifi voor OTA") + "</div>";
  html += "<div class='code'>";
  html += tr("Reseau enregistre :", "Opgeslagen netwerk :") + " <b>" + (otaSSID.length() ? htmlEscape(otaSSID) : tr("aucun", "geen")) + "</b><br>";
  html += tr("Etat :", "Status :") + " <b>" + htmlEscape(otaWifiStatus.length() ? otaWifiStatus : tr("non teste", "niet getest")) + "</b>";
  html += "</div>";
  html += "<div class='lineBtn' style='margin-top:10px'><a class='miniBtn' href='/otascan'>" + tr("Rechercher un Wi-Fi", "Wifi zoeken") + "</a><a class='miniBtn' href='/clearotawifi'>" + tr("Effacer le Wi-Fi OTA", "OTA-wifi wissen") + "</a></div>";

  html += "<div class='sectionTitle' style='margin-top:16px'>" + tr("Mise a jour automatique", "Automatische update") + "</div>";
  html += "<div class='code'>";
  html += tr("Version actuelle :", "Huidige versie :") + " <b>" + String(FW_VERSION) + "</b><br>";
  html += tr("Derniere version :", "Laatste versie :") + " <b>" + (otaLatestVersion.length() ? htmlEscape(otaLatestVersion) : tr("inconnue", "onbekend")) + "</b><br>";
  html += tr("Resultat :", "Resultaat :") + " <b>" + htmlEscape(otaLastCheckMessage.length() ? otaLastCheckMessage : tr("Aucun test", "Geen test")) + "</b>";
  html += "</div>";
  html += "<div class='lineBtn' style='margin-top:10px'><a class='miniBtn' href='/otacheck'>" + tr("Rechercher une mise a jour", "Zoek naar update") + "</a><a class='miniBtn' href='/otainstall?slot=latest'>" + tr("Installer la derniere version", "Installeer laatste versie") + "</a></div>";
  html += "<div style='margin-top:12px'>" + otaVersionListHtml() + "</div>";

  html += "<div class='sectionTitle' style='margin-top:16px'>" + tr("Mise a jour manuelle", "Handmatige update") + "</div>";
  html += "<div style='margin-top:14px'><input id='fwfile' type='file' accept='.bin,application/octet-stream'></div>";
  html += "<div class='lineBtn' style='margin-top:14px'><button onclick='startUpdate()'>" + tr("Charger un fichier .bin", "Laad een .bin-bestand") + "</button><button onclick=\"location.href='/'\">" + tr("Retour", "Terug") + "</button></div>";
  html += "<div style='margin-top:14px'><progress id='prog' class='prog' value='0' max='100'></progress><div id='pct' class='tinyCenter'>0%</div></div>";
  html += "<div id='status' class='code' style='margin-top:12px'>" + tr("En attente d'un fichier .bin", "Wachten op een .bin-bestand") + "</div>";
  html += "</div>";
  html += "<script>";
  html += "function startUpdate(){var f=document.getElementById('fwfile').files[0];if(!f){alert('" + tr("Selectionnez un fichier .bin", "Selecteer een .bin-bestand") + "');return;}var fd=new FormData();fd.append('update',f);var xhr=new XMLHttpRequest();xhr.open('POST','/update',true);xhr.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round((e.loaded/e.total)*100);document.getElementById('prog').value=p;document.getElementById('pct').textContent=p+'%';document.getElementById('status').innerHTML='" + tr("Envoi du firmware en cours...", "Firmware wordt verzonden...") + "';}};xhr.onload=function(){if(xhr.status===200){document.getElementById('prog').value=100;document.getElementById('pct').textContent='100%';document.getElementById('status').innerHTML='<div class=\"ok\">'+xhr.responseText+'</div>';}else{document.getElementById('status').textContent='" + tr("Echec de la mise a jour", "Update mislukt") + " : '+xhr.responseText;}};xhr.onerror=function(){document.getElementById('status').textContent='" + tr("Erreur reseau pendant la mise a jour", "Netwerkfout tijdens de update") + "';};xhr.send(fd);}";
  html += "</script>";
  html += "</div></body></html>";
  return html;
}

String mainPageHtml() {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>" + currentDeviceLabel() + "</title>";
  html += "<style>" + commonStyle() + "</style></head><body><div class='wrap'>";
  html += topHeaderCard();
  html += languageBar();
  html += navBar();
  html += statusCardHtml();
  html += controlSectionHtml();

  if (deviceMode == DEVICE_MASTER || deviceMode == DEVICE_STANDALONE) {
    html += programSectionHtml();
  }

  html += setupSectionHtml();
  html += ledConfigSectionHtml();
  html += firmwareUpdateButtonHtml();

  html += "<script>";
  html += "function go(url){fetch(url).then(()=>setTimeout(()=>location.reload(),200)).catch(()=>alert('" + tr("Erreur", "Fout") + "'));}";
  html += "function tapLed(target){fetch('/tap?target='+encodeURIComponent(target)).then(()=>setTimeout(()=>location.reload(),150)).catch(()=>alert('" + tr("Erreur", "Fout") + "'));}";
  html += "function saveProgram(){var q='/saveprog?';for(var i=0;i<10;i++){if(i>0)q+='&';q+='a'+(i+1)+'='+encodeURIComponent(document.getElementById('a'+i).value)+'&d'+(i+1)+'='+encodeURIComponent(document.getElementById('d'+i).value);}fetch(q).then(()=>alert('" + tr("Programme sauvegarde", "Programma opgeslagen") + "')).catch(()=>alert('" + tr("Erreur", "Fout") + "'));}";
  html += "function saveLedConfig(){var q='/saveled?pv='+encodeURIComponent(document.getElementById('pinVert').value)+'&pr='+encodeURIComponent(document.getElementById('pinRouge').value)+'&pj='+encodeURIComponent(document.getElementById('pinJBas').value)+'&pd='+encodeURIComponent(document.getElementById('pinJD').value)+'&side='+encodeURIComponent(document.getElementById('sigSide').value);fetch(q).then(()=>alert('" + tr("Reglage LED sauvegarde", "LED-instelling opgeslagen") + "')).then(()=>setTimeout(()=>location.reload(),200)).catch(()=>alert('" + tr("Erreur", "Fout") + "'));}";
  html += "function selectSetupMode(mode){document.getElementById('deviceMode').value=mode;['solo','master','slave'].forEach(function(m){var el=document.getElementById('btnMode'+m.charAt(0).toUpperCase()+m.slice(1)); if(el){el.style.outline=(m===mode)?'3px solid #7cc8ff':'none'; el.style.background=(m===mode)?'#1f4f83':'#1a1f2b';}});if(mode!=='slave'){document.getElementById('masterSSID').value='';document.getElementById('masterPass').value='12345678';document.getElementById('masterNum').value='1';}}function saveSetup(){var mode=document.getElementById('deviceMode').value;var modeToSend=(mode==='solo')?'solo':mode;var q='/savesetup?n='+encodeURIComponent(document.getElementById('signalNum').value)+'&mode='+encodeURIComponent(modeToSend)+'&mnum='+encodeURIComponent(document.getElementById('masterNum').value)+'&mssid='+encodeURIComponent(document.getElementById('masterSSID').value)+'&mpass='+encodeURIComponent(document.getElementById('masterPass').value);fetch(q).then(r=>r.text()).then(t=>alert(t)).then(()=>setTimeout(()=>location.reload(),1200)).catch(()=>alert('" + tr("Erreur", "Fout") + "'));}";
  html += "function scanMasters(){fetch('/scan').then(r=>r.text()).then(t=>{document.getElementById('scanResults').innerHTML=t;}).catch(()=>alert('" + tr("Erreur de scan", "Scanfout") + "'));}";
  html += "function saveBlinkConfigPage(){var q='/blink/save?on='+encodeURIComponent(document.getElementById('blinkOn').value)+'&off='+encodeURIComponent(document.getElementById('blinkOff').value)+'&stop='+encodeURIComponent(document.getElementById('blinkStop').value)+'&fx='+encodeURIComponent(document.getElementById('blinkFx').value);fetch(q).then(r=>r.text()).then(t=>alert(t)).then(()=>setTimeout(()=>location.reload(),180)).catch(()=>alert('" + tr("Erreur", "Fout") + "'));}";
  html += "window.addEventListener('load',function(){selectSetupMode(document.getElementById('deviceMode').value||'solo');});";
  html += "</script>";
  html += "</div></body></html>";
  return html;
}


String miniSignalSvgForAspect(const String& aspect) {
  bool r = false, v = false, jb = false, jd = false;
  String a = aspect;
  a.toUpperCase();
  if (a == "ROUGE") r = true;
  else if (a == "VERT") v = true;
  else if (a == "2 JAUNES" || a == "DOUBLE JAUNE" || a == "2J") { jb = true; jd = true; }
  else if (a == "VERT + JAUNE DEPORTE" || a == "VJD") { v = true; jd = true; }
  else if (a == "JAUNE BAS") jb = true;
  else if (a == "JAUNE DEPORTE") jd = true;

  String svg;
  svg += "<svg viewBox='0 0 210 180' style='max-width:180px;width:100%;height:auto'>";
  svg += "<circle cx='55' cy='40' r='16' fill='#dffcff'/><circle cx='55' cy='40' r='12' fill='#145a22' stroke='#0d0f12' stroke-width='2'/><circle cx='55' cy='40' r='9' fill='" + String(v ? "#2ef06f" : "#145a22") + "'/>";
  svg += "<circle cx='55' cy='90' r='17' fill='#ffe7ef'/><circle cx='55' cy='90' r='12' fill='#6a1010' stroke='#0d0f12' stroke-width='2'/><circle cx='55' cy='90' r='9' fill='" + String(r ? "#ff4b4b" : "#6a1010") + "'/>";
  svg += "<circle cx='55' cy='145' r='16' fill='#f3ffd9'/><circle cx='55' cy='145' r='12' fill='#6a5a00' stroke='#0d0f12' stroke-width='2'/><circle cx='55' cy='145' r='9' fill='" + String(jb ? "#ffe34a" : "#6a5a00") + "'/>";
  svg += "<circle cx='145' cy='40' r='16' fill='#f3ffd9'/><circle cx='145' cy='40' r='12' fill='#6a5a00' stroke='#0d0f12' stroke-width='2'/><circle cx='145' cy='40' r='9' fill='" + String(jd ? "#ffe34a" : "#6a5a00") + "'/>";
  svg += "<circle cx='133' cy='96' r='11' fill='#dffcff'/><circle cx='133' cy='96' r='8' fill='#145a22' stroke='#0d0f12' stroke-width='2'/><circle cx='133' cy='96' r='6' fill='" + String((v && jd && !r && !jb) ? "#2ef06f" : "#145a22") + "'/>";
  svg += "<circle cx='157' cy='96' r='11' fill='#f3ffd9'/><circle cx='157' cy='96' r='8' fill='#6a5a00' stroke='#0d0f12' stroke-width='2'/><circle cx='157' cy='96' r='6' fill='" + String((v && jd && !r && !jb) ? "#ffe34a" : "#6a5a00") + "'/>";
  svg += "<circle cx='133' cy='150' r='11' fill='#f3ffd9'/><circle cx='133' cy='150' r='8' fill='#6a5a00' stroke='#0d0f12' stroke-width='2'/><circle cx='133' cy='150' r='6' fill='" + String((jb && jd) ? "#ffe34a" : "#6a5a00") + "'/>";
  svg += "<circle cx='157' cy='139' r='11' fill='#f3ffd9'/><circle cx='157' cy='139' r='8' fill='#6a5a00' stroke='#0d0f12' stroke-width='2'/><circle cx='157' cy='139' r='6' fill='" + String((jb && jd) ? "#ffe34a" : "#6a5a00") + "'/>";
  svg += "</svg>";
  return svg;
}

String miniSignalSvgInteractive(const String& aspect, uint8_t num, bool mirror) {
  bool r = false, v = false, jb = false, jd = false;
  String a = aspect;
  a.toUpperCase();
  if (a == "ROUGE") r = true;
  else if (a == "VERT") v = true;
  else if (a == "2 JAUNES" || a == "DOUBLE JAUNE" || a == "2J") { jb = true; jd = true; }
  else if (a == "VERT + JAUNE DEPORTE" || a == "VJD") { v = true; jd = true; }
  else if (a == "JAUNE BAS") jb = true;
  else if (a == "JAUNE DEPORTE") jd = true;

  int xMain = mirrorCoord(55, 210, mirror);
  int xDeporte = mirrorCoord(145, 210, mirror);
  int xMiniLeft = mirrorCoord(133, 210, mirror);
  int xMiniRight = mirrorCoord(157, 210, mirror);

  String base = "/multi/cmd?num=" + String(num) + "&a=";
  String svg;
  svg += "<svg viewBox='0 0 210 180' aria-label='Signal cliquable'>";
  svg += "<g style='cursor:pointer' onclick=\"go('" + base + "vert')\">";
  svg += "<circle cx='" + String(xMain) + "' cy='40' r='16' fill='#dffcff'/><circle cx='" + String(xMain) + "' cy='40' r='12' fill='#145a22' stroke='#0d0f12' stroke-width='2'/><circle cx='" + String(xMain) + "' cy='40' r='9' fill='" + String(v ? "#2ef06f" : "#145a22") + "'/>";
  svg += "</g>";
  svg += "<g style='cursor:pointer' onclick=\"go('" + base + "rouge')\">";
  svg += "<circle cx='" + String(xMain) + "' cy='90' r='17' fill='#ffe7ef'/><circle cx='" + String(xMain) + "' cy='90' r='12' fill='#6a1010' stroke='#0d0f12' stroke-width='2'/><circle cx='" + String(xMain) + "' cy='90' r='9' fill='" + String(r ? "#ff4b4b" : "#6a1010") + "'/>";
  svg += "</g>";
  svg += "<g style='cursor:pointer' onclick=\"go('" + base + "jb')\">";
  svg += "<circle cx='" + String(xMain) + "' cy='145' r='16' fill='#f3ffd9'/><circle cx='" + String(xMain) + "' cy='145' r='12' fill='#6a5a00' stroke='#0d0f12' stroke-width='2'/><circle cx='" + String(xMain) + "' cy='145' r='9' fill='" + String(jb ? "#ffe34a" : "#6a5a00") + "'/>";
  svg += "</g>";
  svg += "<g style='cursor:pointer' onclick=\"go('" + base + "jd')\">";
  svg += "<circle cx='" + String(xDeporte) + "' cy='40' r='16' fill='#f3ffd9'/><circle cx='" + String(xDeporte) + "' cy='40' r='12' fill='#6a5a00' stroke='#0d0f12' stroke-width='2'/><circle cx='" + String(xDeporte) + "' cy='40' r='9' fill='" + String(jd ? "#ffe34a" : "#6a5a00") + "'/>";
  svg += "</g>";
  svg += "<g style='cursor:pointer' onclick=\"go('" + base + "vjd')\">";
  svg += "<circle cx='" + String(xMiniLeft) + "' cy='96' r='11' fill='#dffcff'/><circle cx='" + String(xMiniLeft) + "' cy='96' r='8' fill='#145a22' stroke='#0d0f12' stroke-width='2'/><circle cx='" + String(xMiniLeft) + "' cy='96' r='6' fill='" + String((v && jd && !r && !jb) ? "#2ef06f" : "#145a22") + "'/>";
  svg += "<circle cx='" + String(xMiniRight) + "' cy='96' r='11' fill='#f3ffd9'/><circle cx='" + String(xMiniRight) + "' cy='96' r='8' fill='#6a5a00' stroke='#0d0f12' stroke-width='2'/><circle cx='" + String(xMiniRight) + "' cy='96' r='6' fill='" + String((v && jd && !r && !jb) ? "#ffe34a" : "#6a5a00") + "'/>";
  svg += "</g>";
  svg += "<g style='cursor:pointer' onclick=\"go('" + base + "2j')\">";
  svg += "<circle cx='" + String(xMiniLeft) + "' cy='150' r='11' fill='#f3ffd9'/><circle cx='" + String(xMiniLeft) + "' cy='150' r='8' fill='#6a5a00' stroke='#0d0f12' stroke-width='2'/><circle cx='" + String(xMiniLeft) + "' cy='150' r='6' fill='" + String((jb && jd) ? "#ffe34a" : "#6a5a00") + "'/>";
  svg += "<circle cx='" + String(xMiniRight) + "' cy='139' r='11' fill='#f3ffd9'/><circle cx='" + String(xMiniRight) + "' cy='139' r='8' fill='#6a5a00' stroke='#0d0f12' stroke-width='2'/><circle cx='" + String(xMiniRight) + "' cy='139' r='6' fill='" + String((jb && jd) ? "#ffe34a" : "#6a5a00") + "'/>";
  svg += "</g>";
  svg += "<text x='105' y='174' text-anchor='middle' font-family='Arial' font-size='11' fill='#8ec5ff'>" + tr("Tape une LED pour commander", "Tik op een LED om te bedienen") + "</text>";
  svg += "</svg>";
  return svg;
}

String multiSignalCard(const String& title, const String& aspect, const String& ip, const String& setupSsid, bool isMaster, uint8_t num, bool mirror) {
  String html;
  html += "<div class='card multiCard' style='text-align:center'>";
  html += "<div class='sectionTitle'>" + htmlEscape(title) + "</div>";
  html += "<div class='badge'>" + String(isMaster ? trRoleMaster() : trRoleSlave()) + "</div>";
  html += "<div style='margin:10px auto;max-width:220px'>" + miniSignalSvgInteractive(aspect, num, mirror) + "</div>";
  html += "<div class='clickHint'>" + tr("Les LED du dessin servent de boutons.", "De leds van de tekening werken als knoppen.") + "</div>";
  html += "<div><b>" + tr("Etat", "Status") + " :</b> " + aspectLabelUi(aspect) + "</div>";
  html += "<div><b>" + tr("Numero", "Nummer") + " :</b> " + String(num) + "</div>";
  html += "<div><b>" + tr("IP reseau Master", "IP Master-netwerk") + " :</b> " + htmlEscape(ip) + "</div>";
  if (!isMaster) {
    html += "<div><b>" + tr("Wi-Fi reglages", "Instellingen-Wi-Fi") + " :</b> " + htmlEscape(setupSsid.length() ? setupSsid : buildSetupName(num)) + "</div>";
    html += "<div><b>" + tr("Page locale", "Lokale pagina") + " :</b> " + buildSetupIpString() + "</div>";
    html += "<form action='/multi/assign' method='get' style='margin-top:10px'>";
    html += "<input type='hidden' name='ip' value='" + htmlEscape(ip) + "'>";
    html += "<div class='tiny'>" + tr("Attribuer le numero depuis le Master", "Nummer toewijzen vanaf de Master") + "</div>";
    html += "<div class='row2'><input type='number' min='1' max='99' name='newnum' value='" + String(num) + "'><button type='submit'>" + tr("Attribuer", "Toewijzen") + "</button><a href='/multi/eject?num=" + String(num) + "' style='display:inline-block;text-decoration:none'><button type='button' style='background:#7a1f1f'>" + tr("Ejecter", "Verwijderen") + "</button></a></div>";
    html += "</form>";
    html += "<div class='footerInfo'>" + tr("Pour modifier ses reglages propres, connectez votre GSM au Wi-Fi <b>", "Om de eigen instellingen te wijzigen, verbind uw gsm met de Wi-Fi <b>") + htmlEscape(setupSsid.length() ? setupSsid : buildSetupName(num)) + "</b>, " + tr("puis ouvrez <b>", "open daarna <b>") + buildSetupIpString() + "</b>.</div>";
  }
  html += "</div>";
  return html;
}

String multiConfigSectionHtml() {
  String html;
  html += "<div class='card'><div class='sectionTitle'>" + tr("Configuration du mode multiple", "Configuratie van meervoudige modus") + "</div>";
  html += "<div class='row3'>";
  html += "<div><div class='tiny'>" + tr("Signal Master", "Master-signaal") + "</div><div class='code'>" + currentDeviceLabel() + "</div></div>";
  html += "<div><div class='tiny'>" + tr("IP du Master", "IP van de Master") + "</div><div class='code'>" + localIpString + "</div></div>";
  html += "<div><div class='tiny'>" + tr("Capacite maximum", "Maximale capaciteit") + "</div><div class='code'>" + tr("1 Master + 9 Clients", "1 Master + 9 Clients") + "</div></div>";
  html += "</div>";
  html += "<div class='btnGrid' style='margin-top:10px'>";
  html += "<button onclick=\"location.href='/setup'\">" + tr("Configurer ce Master", "Deze Master configureren") + "</button>";
  html += "<button onclick=\"location.href='/multi'\">" + tr("Rafraichir la page multiple", "Meervoudige pagina vernieuwen") + "</button>";
  html += "</div>";
  html += "<div class='footerInfo'>" + tr("Ce menu remet un acces rapide a la configuration du mode multiple sans toucher aux liaisons deja en place.", "Dit menu geeft opnieuw snelle toegang tot de configuratie van de meervoudige modus zonder bestaande koppelingen te wijzigen.") + "</div>";
  html += "</div>";
  return html;
}

String multiPageHtml() {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>" + tr("Controle multiple", "Meervoudige bediening") + "</title><style>" + commonStyle() + "</style></head><body><div class='wrap'>";
  html += topHeaderCard();
  html += languageBar();
  html += navBar();

  html += "<div class='card'><div class='mainTitle'>" + tr("Controle multiple", "Meervoudige bediening") + "</div>";
  html += "<span class='badge'>" + tr("Capacite maximum :", "Maximale capaciteit :") + " <b>" + tr("1 Master + 9 Clients", "1 Master + 9 Clients") + "</b></span>";
  html += "<span class='badge'>" + tr("Feux visibles :", "Zichtbare seinen :") + " <b>";
  uint8_t cnt = 1;
  for (uint8_t i = 0; i < MAX_SLAVES; i++) if (slaves[i].active) cnt++;
  html += String(cnt) + " / 10";
  html += "</b></span>";
  html += "<div class='footerInfo'>" + tr("Les dessins ci-dessous representent les feux actuellement connectes au Master. Utilise le bouton Rafraichir pour actualiser sans perdre une saisie en cours.", "De tekeningen hieronder tonen de signalen die momenteel met de Master verbonden zijn. Gebruik de knop Vernieuwen om te actualiseren zonder lopende invoer te verliezen.") + "</div>";
  html += "</div>";

  html += multiConfigSectionHtml();
  html += scenarioSectionHtml();

  html += "<div class='multiGrid'>";
  html += multiSignalCard(currentDeviceLabel(), getCurrentAspectName(), localIpString, "", true, signalNumber, isCounterVoie);
  for (uint8_t i = 0; i < MAX_SLAVES; i++) {
    if (!slaves[i].active) continue;
    html += multiSignalCard(slaves[i].name.length() ? slaves[i].name : buildStandaloneName(slaves[i].num),
                            slaves[i].aspect.length() ? slaves[i].aspect : "OFF",
                            slaves[i].ip,
                            slaves[i].setupSsid,
                            false,
                            slaves[i].num,
                            slaves[i].counterVoie);
  }
  html += "</div>";

  html += "<div class='card'><div class='sectionTitle'>" + tr("Liste rapide", "Snelle lijst") + "</div><div class='tableWrap'><table>";
  html += "<tr><th>" + tr("Signal", "Signaal") + "</th><th>" + tr("Type", "Type") + "</th><th>" + tr("IP reseau Master", "IP Master-netwerk") + "</th><th>" + tr("Wi-Fi reglages", "Instellingen-Wi-Fi") + "</th><th>" + tr("Etat", "Status") + "</th></tr>";
  html += "<tr><td>" + currentDeviceLabel() + "</td><td>" + tr("Master", "Master") + "</td><td>" + localIpString + "</td><td>-</td><td>" + aspectLabelUi(getCurrentAspectName()) + "</td></tr>";
  for (uint8_t i = 0; i < MAX_SLAVES; i++) {
    if (!slaves[i].active) continue;
    html += "<tr><td>" + htmlEscape(slaves[i].name) + "</td><td>" + tr("Client", "Client") + "</td><td>" + htmlEscape(slaves[i].ip) + "</td><td>" + htmlEscape(slaves[i].setupSsid) + "</td><td>" + htmlEscape(slaves[i].aspect) + "</td></tr>";
  }
  html += "</table></div>";
  html += "<div class='footerInfo'>" + tr("Pour regler un Client sur sa propre interface, connectez-vous a son Wi-Fi de reglages puis ouvrez ", "Om een Client via zijn eigen interface in te stellen, verbindt u met zijn instellingen-Wi-Fi en opent u ") + buildSetupIpString() + ".</div>";
  html += "</div>";

  html += "<script>function go(url){fetch(url).then(()=>setTimeout(()=>location.reload(),250)).catch(()=>alert('" + tr("Erreur", "Fout") + "'));}</script>";
  html += "</div></body></html>";
  return html;
}

String setupPageHtml() {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>" + tr("Choisir le mode Server", "Servermodus kiezen") + "</title><style>" + commonStyle() + "</style></head><body><div class='wrap'>";
  html += topHeaderCard();
  html += languageBar();
  html += navBar();
  html += statusCardHtml();
  html += setupSectionHtml();
  html += ledConfigSectionHtml();
  html += "<script>";
  html += "function saveLedConfig(){var q='/saveled?pv='+encodeURIComponent(document.getElementById('pinVert').value)+'&pr='+encodeURIComponent(document.getElementById('pinRouge').value)+'&pj='+encodeURIComponent(document.getElementById('pinJBas').value)+'&pd='+encodeURIComponent(document.getElementById('pinJD').value)+'&side='+encodeURIComponent(document.getElementById('sigSide').value);fetch(q).then(()=>alert('" + tr("Reglage LED sauvegarde", "LED-instelling opgeslagen") + "')).then(()=>setTimeout(()=>location.reload(),200)).catch(()=>alert('" + tr("Erreur", "Fout") + "'));}";
  html += "function selectSetupMode(mode){document.getElementById('deviceMode').value=mode;['solo','master','slave'].forEach(function(m){var el=document.getElementById('btnMode'+m.charAt(0).toUpperCase()+m.slice(1)); if(el){el.style.outline=(m===mode)?'3px solid #7cc8ff':'none'; el.style.background=(m===mode)?'#1f4f83':'#1a1f2b';}});if(mode!=='slave'){document.getElementById('masterSSID').value='';document.getElementById('masterPass').value='12345678';document.getElementById('masterNum').value='1';}}function saveSetup(){var mode=document.getElementById('deviceMode').value;var modeToSend=(mode==='solo')?'solo':mode;var q='/savesetup?n='+encodeURIComponent(document.getElementById('signalNum').value)+'&mode='+encodeURIComponent(modeToSend)+'&mnum='+encodeURIComponent(document.getElementById('masterNum').value)+'&mssid='+encodeURIComponent(document.getElementById('masterSSID').value)+'&mpass='+encodeURIComponent(document.getElementById('masterPass').value);fetch(q).then(r=>r.text()).then(t=>alert(t)).then(()=>setTimeout(()=>location.reload(),1200)).catch(()=>alert('" + tr("Erreur", "Fout") + "'));}";
  html += "function scanMasters(){fetch('/scan').then(r=>r.text()).then(t=>{document.getElementById('scanResults').innerHTML=t;}).catch(()=>alert('" + tr("Erreur de scan", "Scanfout") + "'));}";
  html += "window.addEventListener('load',function(){selectSetupMode(document.getElementById('deviceMode').value||'solo');});";
  html += "</script>";
  html += "</div></body></html>";
  return html;
}

// ================= HTTP HANDLERS =================
void handleRoot() {
  server.send(200, "text/html", mainPageHtml());
}

void handleSetupPage() {
  server.send(200, "text/html", setupPageHtml());
}

void handleBlinkPage() {
  server.send(200, "text/html", blinkPageHtml());
}


void handleOtaScan() {
  if (deviceMode != DEVICE_STANDALONE) {
    server.send(403, "text/plain", tr("Page reservee au mode Standalone", "Pagina alleen voor Standalone-modus"));
    return;
  }
  WiFi.mode(WIFI_AP_STA);
  int n = WiFi.scanNetworks(false, true);
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><style>" + commonStyle() + "</style></head><body><div class='wrap'>";
  html += topHeaderCard();
  html += languageBar();
  html += navBar();
  html += "<div class='card'><div class='mainTitle'>" + tr("Choix du Wi-Fi Internet", "Keuze van internet-wifi") + "</div>";
  if (n <= 0) {
    html += "<div class='code'>" + tr("Aucun reseau detecte", "Geen netwerk gevonden") + "</div>";
  } else {
    html += "<div class='code'>";
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;
      html += "<div style='margin:8px 0'><b>" + htmlEscape(ssid) + "</b> &nbsp; <a href='/selectotawifi?ssid=" + urlEncode(ssid) + "'>" + tr("Selectionner", "Kiezen") + "</a></div>";
    }
    html += "</div>";
  }
  html += "<div style='margin-top:12px'><a class='miniBtn' href='/update'>" + tr("Retour", "Terug") + "</a></div></div></div></body></html>";
  WiFi.scanDelete();
  server.send(200, "text/html", html);
}

void handleSelectOtaWifi() {
  if (deviceMode != DEVICE_STANDALONE) {
    server.send(403, "text/plain", tr("Page reservee au mode Standalone", "Pagina alleen voor Standalone-modus"));
    return;
  }
  String ssid = urlDecode(server.arg("ssid"));
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><style>" + commonStyle() + "</style></head><body><div class='wrap'>";
  html += topHeaderCard();
  html += languageBar();
  html += navBar();
  html += "<div class='card'><div class='mainTitle'>" + tr("Enregistrer le Wi-Fi OTA", "OTA-wifi opslaan") + "</div>";
  html += "<div class='code'><b>" + htmlEscape(ssid) + "</b></div>";
  html += "<form action='/saveotawifi' method='get'>";
  html += "<input type='hidden' name='ssid' value='" + htmlEscapeAttr(ssid) + "'>";
  html += "<div class='tiny' style='margin:10px 0 6px'>" + tr("Mot de passe", "Wachtwoord") + "</div>";
  html += "<input type='text' name='pass' value=''>";
  html += "<div class='lineBtn' style='margin-top:12px'><button type='submit'>" + tr("Enregistrer et connecter", "Opslaan en verbinden") + "</button><button type='button' onclick=\"location.href='/update'\">" + tr("Annuler", "Annuleren") + "</button></div>";
  html += "</form></div></div></body></html>";
  server.send(200, "text/html", html);
}

void handleSaveOtaWifi() {
  if (deviceMode != DEVICE_STANDALONE) {
    server.send(403, "text/plain", tr("Page reservee au mode Standalone", "Pagina alleen voor Standalone-modus"));
    return;
  }
  String ssid = urlDecode(server.arg("ssid"));
  String pass = server.arg("pass");
  saveOtaWifiConfig(ssid, pass);
  ensureOtaWifiConnection();
  server.send(200, "text/html", "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><style>" + commonStyle() + "</style></head><body><div class='wrap'><div class='card'><div class='mainTitle'>" + tr("Wi-Fi OTA enregistre", "OTA-wifi opgeslagen") + "</div><div class='code'><b>" + htmlEscape(otaSSID) + "</b><br>" + htmlEscape(otaWifiStatus) + "</div><div style='margin-top:12px'><a class='miniBtn' href='/update'>" + tr("Retour a la mise a jour", "Terug naar update") + "</a></div></div></div></body></html>");
}

void handleClearOtaWifi() {
  clearOtaWifiConfig();
  WiFi.disconnect(false, false);
  server.sendHeader("Location", "/update");
  server.send(302, "text/plain", "");
}

void handleOtaCheck() {
  fetchOtaManifest();
  server.send(200, "text/html", firmwareUpdatePageHtml());
}

void handleOtaInstall() {
  if (deviceMode != DEVICE_STANDALONE) {
    server.send(403, "text/plain", tr("Page reservee au mode Standalone", "Pagina alleen voor Standalone-modus"));
    return;
  }
  fetchOtaManifest();
  String slot = server.arg("slot");
  String url = "";
  if (slot == "latest") {
    url = otaLatestUrl;
  } else {
    int idx = slot.toInt();
    if (idx >= 0 && idx < otaVersionCount) url = otaVersions[idx].url;
  }
  String msg;
  bool ok = installFirmwareFromUrl(url, msg);
  if (!ok) {
    server.send(500, "text/plain", msg);
    return;
  }
  server.send(200, "text/plain", msg);
  delay(400);
  ESP.restart();
}

void handleFirmwareUpdatePage() {
  if (deviceMode != DEVICE_STANDALONE) {
    server.send(403, "text/plain", tr("Page reservee au mode Standalone", "Pagina alleen voor Standalone-modus"));
    return;
  }
  server.send(200, "text/html", firmwareUpdatePageHtml());
}

void handleFirmwareUpdateUpload() {
  if (deviceMode != DEVICE_STANDALONE) return;

  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("Debut MAJ firmware : %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("MAJ OK : %u octets\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    Serial.println("MAJ firmware annulee");
  }
}

void handleFirmwareUpdateResult() {
  if (deviceMode != DEVICE_STANDALONE) {
    server.send(403, "text/plain", tr("Page reservee au mode Standalone", "Pagina alleen voor Standalone-modus"));
    return;
  }

  if (Update.hasError()) {
    server.send(500, "text/plain", tr("Echec de la mise a jour firmware.", "Firmware-update mislukt."));
    return;
  }

  server.send(200, "text/plain", tr("Mise a jour terminee. Le signal redemarre.", "Update voltooid. Het sein herstart."));
  delay(400);
  ESP.restart();
}

void handleBlinkToggle() {
  toggleBlinkEnabled();
  if (deviceMode == DEVICE_MASTER) {
    broadcastStateToSlaves();
  }
  server.send(200, "text/plain", blinkStateLabel());
}

void handleBlinkSave() {
  unsigned long onMs = server.arg("on").toInt();
  unsigned long offMs = server.arg("off").toInt();
  unsigned long stopMs = server.arg("stop").toInt();
  String fx = server.arg("fx");
  if (onMs < 50UL) onMs = 50UL;
  if (offMs < 50UL) offMs = 50UL;
  if (onMs > 5000UL) onMs = 5000UL;
  if (offMs > 5000UL) offMs = 5000UL;
  if (stopMs > 5000UL) stopMs = 5000UL;

  BlinkEffect effect = blinkEffect;
  if (fx == "fade") effect = BLINK_EFFECT_FADE;
  else if (fx == "stop") effect = BLINK_EFFECT_STOP;
  else if (fx == "hard") effect = BLINK_EFFECT_HARD;

  saveBlinkConfig(blinkEnabled, onMs, offMs, stopMs, effect);
  applyLeds();
  server.send(200, "text/plain", tr("Parametres du clignotement sauvegardes", "Knipperinstellingen opgeslagen"));
}

void handleMultiPage() {
  if (deviceMode != DEVICE_MASTER) {
    server.send(403, "text/plain", tr("Page reservee au Master", "Pagina alleen voor de Master"));
    return;
  }
  server.send(200, "text/html", multiPageHtml());
}

void handleMode() {
  String m = server.arg("m");
  if (m == "off") startOffMode();
  else if (m == "manuel") startManualMode();
  else if (m == "auto") startAutoMode();
  else if (m == "prog") startProgramMode();
  else if (m == "app") startPairingMode();

  if (deviceMode == DEVICE_MASTER) {
    broadcastStateToSlaves();
  }
  server.send(200, "text/plain", "OK");
}

void handleTap() {
  if (runMode == RUN_AUTO || runMode == RUN_PROGRAM) startManualMode();

  String t = server.arg("target");
  if (t == "rouge") applyAspect("rouge");
  else if (t == "vert") applyAspect("vert");
  else if (t == "jauned") applyAspect("jd");
  else if (t == "vertjauned") applyAspect("vjd");
  else if (t == "jaunebas") applyAspect("jb");
  else if (t == "doublejaune") applyAspect("2j");
  else if (t == "off") applyAspect("off");

  if (deviceMode == DEVICE_MASTER) broadcastStateToSlaves();
  server.send(200, "text/plain", "OK");
}

void handleSaveProg() {
  for (uint8_t i = 0; i < progStepCount; i++) {
    String a = server.arg("a" + String(i + 1));
    String d = server.arg("d" + String(i + 1));
    if (a.length() > 0) progAspects[i] = a;
    unsigned long sec = d.toInt();
    if (sec < 1) sec = 1;
    if (sec > 999) sec = 999;
    progDurations[i] = sec;
  }
  saveProgramToPrefs();
  server.send(200, "text/plain", tr("Programme sauvegarde", "Programma opgeslagen"));
}


String scenarioSectionHtml() {
  String html;
  html += "<div class='card'><div class='sectionTitle'>" + tr("Creation de scenario", "Creatie van scenario") + "</div>";
  html += "<div class='footerInfo'>" + tr("Exemple : si le signal 1 passe au vert, le 2 peut passer au jaune et le 3 au rouge. Cette page ne se rafraichit plus toute seule pour eviter de perdre la saisie.", "Voorbeeld: als sein 1 groen wordt, kan sein 2 geel worden en sein 3 rood. Deze pagina wordt niet automatisch vernieuwd om verlies van invoer te voorkomen.") + "</div>";
  html += "<form action='/scenario/add' method='get'>";
  html += "<div class='row2'>";
  html += "<div><div class='tiny'>" + tr("Si signal", "Als sein") + "</div><input type='number' min='1' max='99' name='triggerNum' value='" + String(signalNumber) + "'></div>";
  html += "<div><div class='tiny'>" + tr("Aspect declencheur", "Activerend seinbeeld") + "</div><select name='triggerAspect'><option value='ROUGE'>" + tr("Rouge", "Rood") + "</option><option value='VERT'>" + tr("Vert", "Groen") + "</option><option value='JAUNE BAS'>" + tr("Jaune bas", "Onder geel") + "</option><option value='JAUNE DEPORTE'>" + tr("Jaune deporte", "Verplaatst geel") + "</option><option value='VERT + JAUNE DEPORTE'>" + tr("Vert + jaune deporte", "Groen + verplaatst geel") + "</option><option value='2 JAUNES'>" + tr("2 jaunes", "2 gelen") + "</option><option value='OFF'>Off</option></select></div>";
  html += "</div>";
  html += "<div class='row2' style='margin-top:10px'>";
  html += "<div><div class='tiny'>" + tr("Alors signal", "Dan sein") + "</div><input type='number' min='1' max='99' name='targetNum' value='2'></div>";
  html += "<div><div class='tiny'>" + tr("Passe a", "Ga naar") + "</div><select name='targetAspect'><option value='ROUGE'>" + tr("Rouge", "Rood") + "</option><option value='VERT'>" + tr("Vert", "Groen") + "</option><option value='JAUNE BAS'>" + tr("Jaune bas", "Onder geel") + "</option><option value='JAUNE DEPORTE'>" + tr("Jaune deporte", "Verplaatst geel") + "</option><option value='VERT + JAUNE DEPORTE'>" + tr("Vert + jaune deporte", "Groen + verplaatst geel") + "</option><option value='2 JAUNES'>" + tr("2 jaunes", "2 gelen") + "</option><option value='OFF'>Off</option></select></div>";
  html += "</div>";
  html += "<div style='margin-top:10px'><button type='submit'>" + tr("Ajouter la regle", "Regel toevoegen") + "</button></div>";
  html += "</form>";
  html += "<div style='margin-top:12px'>";
  bool hasRule = false;
  for (uint8_t i = 0; i < MAX_SCENARIO_RULES; i++) {
    if (!scenarioRules[i].active) continue;
    hasRule = true;
    html += "<div class='code'>" + tr("Regle", "Regel") + " " + String(i + 1) + " : " + tr("si signal", "als sein") + " " + String(scenarioRules[i].triggerNum) + " = " + aspectLabelUi(scenarioRules[i].triggerAspect) + ", " + tr("alors signal", "dan sein") + " " + String(scenarioRules[i].targetNum) + " = " + aspectLabelUi(scenarioRules[i].targetAspect) + " <a style='color:#8ec5ff' href='/scenario/del?i=" + String(i) + "'>" + tr("Supprimer", "Verwijderen") + "</a></div>";
  }
  if (!hasRule) html += "<div class='footerInfo'>" + tr("Aucune regle pour le moment.", "Geen regels momenteel.") + "</div>";
  html += "</div></div>";
  return html;
}

void applyScenarioRules(uint8_t triggerNum, const String& triggerAspectLabel) {
  if (deviceMode != DEVICE_MASTER) return;
  for (uint8_t i = 0; i < MAX_SCENARIO_RULES; i++) {
    if (!scenarioRules[i].active) continue;
    if (scenarioRules[i].triggerNum != triggerNum) continue;
    if (scenarioRules[i].triggerAspect != triggerAspectLabel) continue;
    String targetCode = aspectLabelToCode(scenarioRules[i].targetAspect);
    if (scenarioRules[i].targetNum == signalNumber) {
      applyAspect(targetCode);
    } else {
      sendAspectToSlave(scenarioRules[i].targetNum, targetCode);
      int idx = findSlaveIndexByNum(scenarioRules[i].targetNum);
      if (idx >= 0) slaves[idx].aspect = scenarioRules[i].targetAspect;
    }
  }
}

void handleScenarioAdd() {
  if (deviceMode != DEVICE_MASTER) { server.send(403, "text/plain", tr("Reserve au Master", "Alleen voor de Master")); return; }
  uint8_t triggerNum = (uint8_t)server.arg("triggerNum").toInt();
  uint8_t targetNum = (uint8_t)server.arg("targetNum").toInt();
  String triggerAspect = server.arg("triggerAspect");
  String targetAspect = server.arg("targetAspect");
  int idx = -1;
  for (uint8_t i = 0; i < MAX_SCENARIO_RULES; i++) if (!scenarioRules[i].active) { idx = i; break; }
  if (idx < 0) { server.sendHeader("Location", "/multi"); server.send(302, "text/plain", "Max"); return; }
  scenarioRules[idx].active = true;
  scenarioRules[idx].triggerNum = triggerNum;
  scenarioRules[idx].triggerAspect = triggerAspect;
  scenarioRules[idx].targetNum = targetNum;
  scenarioRules[idx].targetAspect = targetAspect;
  server.sendHeader("Location", "/multi");
  server.send(302, "text/plain", "OK");
}

void handleScenarioDel() {
  int idx = server.arg("i").toInt();
  if (idx >= 0 && idx < MAX_SCENARIO_RULES) scenarioRules[idx].active = false;
  server.sendHeader("Location", "/multi");
  server.send(302, "text/plain", "OK");
}

void handleMultiAssign() {
  if (deviceMode != DEVICE_MASTER) { server.send(403, "text/plain", tr("Reserve au Master", "Alleen voor de Master")); return; }
  String ip = server.arg("ip");
  uint8_t newNum = (uint8_t)server.arg("newnum").toInt();
  if (newNum < 1) newNum = 1;
  if (newNum > 99) newNum = 99;
  int idx = findSlaveIndexByIp(ip);
  if (idx < 0) { server.send(404, "text/plain", "Esclave introuvable"); return; }
  String path = "/savesetup?n=" + String(newNum) + "&mode=slave&mnum=" + String(signalNumber) + "&mssid=" + urlEncode(apName) + "&mpass=" + urlEncode(masterPassword);
  sendHttpGet(ip, path);
  slaves[idx].num = newNum;
  slaves[idx].name = buildSlaveName(newNum);
  server.sendHeader("Location", "/multi");
  server.send(302, "text/plain", "OK");
}

void handleMultiEject() {
  if (deviceMode != DEVICE_MASTER) {
    server.send(403, "text/plain", tr("Reserve au Master", "Alleen voor de Master"));
    return;
  }

  uint8_t num = (uint8_t)server.arg("num").toInt();
  int idx = findSlaveIndexByNum(num);
  if (idx < 0) {
    server.send(404, "text/plain", "Esclave introuvable");
    return;
  }

  String ip = slaves[idx].ip;
  bool ok = sendHttpGet(ip, "/eject");
  if (ok) {
    clearSlaveEntry(idx);
  }

  server.sendHeader("Location", "/multi");
  server.send(302, "text/plain", ok ? "OK" : "ECHEC");
}

void handleEjectSelf() {
  if (deviceMode != DEVICE_SLAVE) {
    server.send(403, "text/plain", tr("Reserve a un Client", "Alleen voor een Client"));
    return;
  }

  clearMasterSlaveData();
  deviceMode = DEVICE_STANDALONE;
  refreshNames();
  saveCoreConfig();

  server.send(200, "text/html",
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>" + tr("Retour en mode Standalone", "Terug naar Standalone-modus") + "</title></head>"
    "<body style='font-family:Arial;background:#111;color:#eee;padding:18px'>"
    "<h3>" + tr("Le signal repasse en mode Standalone", "Het sein schakelt terug naar Standalone-modus") + "</h3>"
    "<p>" + tr("Le Wi-Fi va redemarrer en <b>", "De wifi start opnieuw op als <b>") + htmlEscape(apName) + "</b>.</p>"
    "</body></html>"
  );

  delay(300);
  beginWiFiForCurrentMode();
}

void handleSaveLed() {
  int newVert = server.arg("pv").toInt();
  int newRouge = server.arg("pr").toInt();
  int newJauneBas = server.arg("pj").toInt();
  int newJauneD = server.arg("pd").toInt();
  bool counter = server.arg("side") == "contre";
  saveLedPins(newVert, newRouge, newJauneBas, newJauneD, counter);
  reconfigureLedPins();
  applyLeds();
  server.send(200, "text/plain", "Reglage LED sauvegarde");
}

void handleSet() {
  rouge = server.arg("r") == "1";
  vert = server.arg("v") == "1";
  jauneB = server.arg("jb") == "1";
  jauneD = server.arg("jd") == "1";
  applyLeds();
  server.send(200, "text/plain", "OK");
}

void handleAspect() {
  String a = server.arg("a");
  applyAspect(a);
  runMode = RUN_MANUAL;
  server.send(200, "text/plain", "OK");
}

void handleSaveSetup() {
  uint8_t newNum = (uint8_t)server.arg("n").toInt();
  if (newNum < 1) newNum = 1;
  if (newNum > 99) newNum = 99;

  uint8_t newMasterNum = (uint8_t)server.arg("mnum").toInt();
  if (newMasterNum < 1) newMasterNum = 1;
  if (newMasterNum > 99) newMasterNum = 99;

  String mode = server.arg("mode");
  DeviceMode newMode = DEVICE_STANDALONE;
  if (mode == "master") newMode = DEVICE_MASTER;
  else if (mode == "slave") newMode = DEVICE_SLAVE;
  else newMode = DEVICE_STANDALONE;

  signalNumber = newNum;
  deviceMode = newMode;

  if (deviceMode == DEVICE_SLAVE) {
    masterNumber = newMasterNum;
    masterSSID = server.arg("mssid");
    masterPassword = server.arg("mpass");
    if (masterPassword.length() < 8) masterPassword = DEFAULT_PASSWORD;
    if (masterSSID.length() == 0) masterSSID = buildMasterName(masterNumber);
  } else {
    clearMasterSlaveData();
    if (deviceMode == DEVICE_MASTER) {
      masterPassword = server.arg("mpass");
      if (masterPassword.length() < 8) masterPassword = DEFAULT_PASSWORD;
    }
  }

  refreshNames();
  saveCoreConfig();

  WiFi.disconnect(true, true);
  delay(250);

  server.send(200, "text/plain", tr("Configuration sauvegardee. Le signal redemarre son Wi-Fi.", "Configuratie opgeslagen. Het sein herstart zijn wifi."));
  delay(300);
  beginWiFiForCurrentMode();
  if (deviceMode == DEVICE_SLAVE) {
    delay(500);
    sendRegisterToMaster();
  }
}

String lowerAscii(String s) {
  s.toLowerCase();
  return s;
}

bool isMasterSsidCandidate(const String &ssid) {
  String l = lowerAscii(ssid);
  return l.startsWith("sncb_nmbs master ") || l.startsWith("sncb_nmbs server ") || l.startsWith("signal_master_") ||
         l.startsWith("signal sncb maitre ") || l.startsWith("sein nmbs hoofd ") ||
         (l.indexOf("sncb_nmbs") >= 0 && (l.indexOf("master") >= 0 || l.indexOf("server") >= 0)) ||
         (l.indexOf("signal") >= 0 && (l.indexOf("maitre") >= 0 || l.indexOf("master") >= 0)) ||
         (l.indexOf("sein") >= 0 && l.indexOf("hoofd") >= 0);
}

int extractMasterNumberFromSsid(const String &ssid) {
  int i = ssid.length() - 1;
  while (i >= 0 && isDigit(ssid[i])) i--;
  String num = ssid.substring(i + 1);
  int n = num.toInt();
  if (n < 1 || n > 99) return 1;
  return n;
}

void handleSelectMaster() {
  String ssid = urlDecode(server.arg("ssid"));
  int num = server.arg("num").toInt();
  if (ssid.length() == 0) {
    server.send(400, "text/plain", tr("SSID Server manquant", "Server-SSID ontbreekt"));
    return;
  }

  deviceMode = DEVICE_SLAVE;
  masterSSID = ssid;
  masterPassword = DEFAULT_PASSWORD;
  if (num > 0 && num < 100) masterNumber = (uint8_t)num;

  refreshNames();
  saveCoreConfig();

  String nextSetup = setupAPName;

  server.send(200, "text/html",
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>" + tr("Passage en mode Client", "Overschakelen naar Client-modus") + "</title></head>"
    "<body style='font-family:Arial;background:#111;color:#eee;padding:18px'>"
    "<h3>" + tr("Passage en mode Client en cours", "Overschakelen naar Client-modus bezig") + "</h3>"
    "<p>" + tr("Le signal applique maintenant la connexion au Master.", "Het signaal gebruikt nu de verbinding met de Master.") + "</p>"
    "<p>" + tr("Pour ouvrir ensuite ses reglages locaux, connectez-vous au Wi-Fi :", "Verbind daarna met deze wifi om de lokale instellingen te openen:") + "</p>"
    "<p><b>" + htmlEscape(nextSetup) + "</b></p>"
    "<p>" + tr("Adresse locale : <b>192.168.4.1</b>", "Lokaal adres: <b>192.168.4.1</b>") + "</p>"
    "</body></html>"
  );

  delay(250);
  beginWiFiForCurrentMode();
}

void handleScan() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(false, false);
  delay(120);
  int n = WiFi.scanNetworks(false, true);
  String out = "";
  String debugOthers = "";
  if (n <= 0) {
    out = tr("Aucun reseau detecte", "Geen netwerk gevonden");
  } else {
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      bool candidate = isMasterSsidCandidate(ssid);
      int ch = WiFi.channel(i);
      int rssi = WiFi.RSSI(i);
      if (candidate) {
        int num = extractMasterNumberFromSsid(ssid);
        out += "<div style='margin:6px 0;padding:8px;border:1px solid rgba(255,255,255,.08);border-radius:10px'>";
        out += "<b>" + htmlEscape(ssid) + "</b>";
        out += " <span style='opacity:.75'>" + tr("canal ", "kanaal ") + String(ch) + " | RSSI " + String(rssi) + " dBm</span>";
        out += " <a href='/selectmaster?ssid=" + urlEncode(ssid) + "&num=" + String(num) + "'>" + tr("Selectionner", "Kiezen") + "</a>";
        out += "</div>";
      } else {
        debugOthers += "<div style='margin:4px 0;opacity:.75'>" + tr("Vu", "Gezien") + " : <b>" + htmlEscape(ssid) + "</b> <span style='opacity:.65'>" + tr("(canal ", "(kanaal ") + String(ch) + ", RSSI " + String(rssi) + " dBm)</span></div>";
      }
    }
    if (out.length() == 0) out = tr("Aucun Master detecte.", "Geen Master gevonden.") + "<hr><div style='margin-bottom:6px'><b>" + tr("Tous les reseaux trouves :", "Alle gevonden netwerken:") + "</b></div>" + debugOthers;
  }
  WiFi.scanDelete();
  server.send(200, "text/html", out);
}

void handleApiNextNum() {
  if (deviceMode != DEVICE_MASTER) {
    server.send(403, "text/plain", "0");
    return;
  }
  server.send(200, "text/plain", String(firstAvailableSlaveNumber()));
}

void handleApiRegister() {
  if (deviceMode != DEVICE_MASTER) {
    server.send(403, "text/plain", "NON");
    return;
  }

  uint8_t num = (uint8_t)server.arg("num").toInt();
  String name = urlDecode(server.arg("name"));
  String ip = urlDecode(server.arg("ip"));
  String aspect = urlDecode(server.arg("aspect"));
  String setupSsid = urlDecode(server.arg("setupssid"));
  bool counterVoie = server.arg("side") == "contre";

  Serial.println("=== REGISTER ESCLAVE ===");
  Serial.print("Num: "); Serial.println(num);
  Serial.print("Nom: "); Serial.println(name);
  Serial.print("IP: "); Serial.println(ip);
  Serial.print("Aspect: "); Serial.println(aspect);
  Serial.print("Setup SSID: "); Serial.println(setupSsid);
  Serial.print("Sens: "); Serial.println(counterVoie ? "Contre-voie" : "Voie");

  registerOrUpdateSlave(num, name, ip, aspect, setupSsid, counterVoie);
  server.send(200, "text/plain", "REGISTERED");
}

void handleApiHeartbeat() {
  if (deviceMode != DEVICE_MASTER) {
    server.send(403, "text/plain", "NON");
    return;
  }

  uint8_t num = (uint8_t)server.arg("num").toInt();
  String ip = urlDecode(server.arg("ip"));
  String aspect = urlDecode(server.arg("aspect"));
  String setupSsid = urlDecode(server.arg("setupssid"));
  bool counterVoie = server.arg("side") == "contre";
  registerOrUpdateSlave(num, buildSlaveName(num), ip, aspect, setupSsid, counterVoie);
  server.send(200, "text/plain", "ALIVE");
}

void handleMultiCmd() {
  if (deviceMode != DEVICE_MASTER) {
    server.send(403, "text/plain", tr("Page reservee au Master", "Pagina alleen voor de Master"));
    return;
  }

  String a = server.arg("a");
  uint8_t num = (uint8_t)server.arg("num").toInt();

  if (num == signalNumber) {
    applyAspect(a);
    applyScenarioRules(num, aspectCodeToLabel(a));
    server.send(200, "text/plain", "OK");
    return;
  }

  sendAspectToSlave(num, a);
  int idx = findSlaveIndexByNum(num);
  if (idx >= 0) {
    slaves[idx].aspect = aspectCodeToLabel(a);
  }
  applyScenarioRules(num, aspectCodeToLabel(a));
  server.send(200, "text/plain", "OK");
}

void handleSetLang() {
  String lang = server.arg("lang");
  uiLang = (lang == "nl") ? LANG_NL : LANG_FR;
  saveUiLang();
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "OK");
}

void registerRoutes() {
  server.on("/", handleRoot);
  server.on("/setup", handleSetupPage);
  server.on("/multi", handleMultiPage);
  server.on("/setlang", handleSetLang);
  server.on("/blink", handleBlinkPage);
  server.on("/blink/toggle", handleBlinkToggle);
  server.on("/blink/save", handleBlinkSave);
  server.on("/update", HTTP_GET, handleFirmwareUpdatePage);
  server.on("/update", HTTP_POST, handleFirmwareUpdateResult, handleFirmwareUpdateUpload);
  server.on("/otascan", handleOtaScan);
  server.on("/selectotawifi", handleSelectOtaWifi);
  server.on("/saveotawifi", handleSaveOtaWifi);
  server.on("/clearotawifi", handleClearOtaWifi);
  server.on("/otacheck", handleOtaCheck);
  server.on("/otainstall", handleOtaInstall);

  server.on("/mode", handleMode);
  server.on("/tap", handleTap);
  server.on("/saveprog", handleSaveProg);
  server.on("/saveled", handleSaveLed);
  server.on("/savesetup", handleSaveSetup);
  server.on("/scan", handleScan);
  server.on("/selectmaster", handleSelectMaster);

  server.on("/set", handleSet);
  server.on("/aspect", handleAspect);

  server.on("/api/nextnum", handleApiNextNum);
  server.on("/api/register", handleApiRegister);
  server.on("/api/heartbeat", handleApiHeartbeat);
  server.on("/multi/cmd", handleMultiCmd);
  server.on("/multi/assign", handleMultiAssign);
  server.on("/multi/eject", handleMultiEject);
  server.on("/eject", handleEjectSelf);
  server.on("/scenario/add", handleScenarioAdd);
  server.on("/scenario/del", handleScenarioDel);
}

// ================= SETUP / LOOP =================
void setup() {
  Serial.begin(115200);
  delay(150);

  loadConfig();
  reconfigureLedPins();
  clearSlaveRegistry();

  beginWiFiForCurrentMode();
  setAll(true, false, false, false);
  startAutoMode();

  registerRoutes();
  server.begin();

  Serial.println();
  Serial.println("==============================");
  Serial.println(currentDeviceLabel());
  Serial.print("Mode appareil : ");
  Serial.println(deviceModeName(deviceMode));
  Serial.print("Wi-Fi principal : ");
  Serial.println(apName);
  Serial.print("IP principale : ");
  Serial.println(localIpString);
  if (deviceMode == DEVICE_SLAVE) {
    Serial.print("Master : ");
    Serial.println(masterSSID);
    Serial.print("Mot de passe maitre : ");
    Serial.println(masterPassword);
    Serial.print("AP secours : ");
    Serial.println(setupAPName);
  }
  Serial.println("==============================");

  if (deviceMode == DEVICE_SLAVE) {
    delay(500);
    sendRegisterToMaster();
  }
}

void loop() {
  server.handleClient();
  updateBlinkState();

  if (runMode == RUN_AUTO) runAutoSequence();
  else if (runMode == RUN_PROGRAM) runProgramSequence();

  if (deviceMode == DEVICE_MASTER) {
    cleanStaleSlaves();
  }

  if (deviceMode == DEVICE_SLAVE) {
    ensureSlaveConnected();

    if (WiFi.status() == WL_CONNECTED) {
      if (millis() - lastHeartbeatMs > 3000UL) {
        lastHeartbeatMs = millis();
        sendHeartbeatToMaster();
      }
    }
  }
}
