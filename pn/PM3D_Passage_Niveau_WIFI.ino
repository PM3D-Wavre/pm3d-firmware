#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>

WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;
Preferences prefs;

// OTA / Wi-Fi local pour mises a jour firmware GitHub
static const char* FW_BIN_URL = "https://raw.githubusercontent.com/PM3D-Wavre/pm3d-firmware/refs/heads/main/pn/PM3D_Passage_Niveau_WIFI.ino.bin";
static const char* FW_MANIFEST_URL = "https://raw.githubusercontent.com/PM3D-Wavre/pm3d-firmware/refs/heads/main/pn/manifest.json";
String otaStaSsid = "";
String otaStaPassword = "";
String otaLastStatus = "Wi-Fi local non connecte";
String currentLang = "FR";

String L(const char* fr, const char* nl, const char* de, const char* en) {
  if (currentLang == "NL") return String(nl);
  if (currentLang == "DE") return String(de);
  if (currentLang == "EN") return String(en);
  return String(fr);
}

String otaWifiStatusText();
String urlEncode(const String &value);

// =======================================================
// PM3D Passage a niveau - base propre ESP-NOW
// Wi-Fi AP uniquement pour interface web.
// Sensors + signaux secondaires uniquement en ESP-NOW.
// =======================================================

static const uint32_t PM3D_MAGIC = 0x504D3344; // "PM3D"
static const uint8_t PKT_SENSOR = 1;
static const uint8_t PKT_STATE  = 2;
static const uint8_t PKT_ACK    = 3;

String apSsid = "";
String wifiApName = "";
String wifiApPassword = "";
IPAddress apIp(192,168,4,1);
IPAddress apGateway(192,168,4,1);
IPAddress apSubnet(255,255,255,0);

// GPIO
int gpioRougeGauche = 2;
int gpioRougeDroite = 3;
int gpioJaunePN = 4;

// Luminosite 0-100
int luminositeLed = 55;

// Etats
bool modeAutomatique = true;
bool modeSecondaire = false;
unsigned long dernierOrdrePrincipalMs = 0;
const unsigned long DELAI_RETOUR_AUTONOME_MS = 12000UL;

bool rougeGauche = false;
bool rougeDroite = false;
bool jaunePN = false;

// Test temporaire des GPIO LED depuis la page Reglages.
// Independants du programme principal et coupes a chaque validation.
bool ledTestTempState[3] = {false, false, false};
int ledTestTempPin[3] = {-1, -1, -1};

bool clignotementRougeActif = false;
bool alternanceRouges = true;
unsigned long tempsRougeAllumageMs = 400;
unsigned long tempsRougeExtinctionMs = 100;
unsigned long tempsRougePauseMs = 0;

bool clignotementJauneActif = false;
unsigned long tempsJauneAllumageMs = 700;
unsigned long tempsJauneExtinctionMs = 300;
unsigned long tempsJaunePauseMs = 1200;

bool phaseRougeAllumee = true;
bool phaseRougeAlternance = true;
unsigned long dernierChangementRouge = 0;

bool phaseJauneAllumee = true;
unsigned long dernierChangementJaune = 0;

unsigned long latenceExecutionMs = 0;

// Accessoires
String macSecondaire[4] = {"","","",""};
String ssidSecondaire[4] = {"","","",""};

String macSensor[4] = {"","","",""};
String ssidSensor[4] = {"","","",""};

// Ordre client : quel sensor physique correspond a S1/S2/S3/S4 dans la logique PN.
// Exemple : ordreSensorClient[0] = 2 signifie que S1 logique utilise le sensor physique 3.
int ordreSensorClient[4] = {0, 1, 2, 3};

String modeSensor[4][2] = {
  {"rien","rien"},
  {"rien","rien"},
  {"rien","rien"},
  {"rien","rien"}
};

String actionSensor[4][2] = {
  {"rouge","rouge"},
  {"rouge","rouge"},
  {"rouge","rouge"},
  {"rouge","rouge"}
};

bool configDoubleVoie = false; // false = simple voie, true = double voie

// Cantons :
// Canton 0 = S1 <-> S2
// Canton 1 = S3 <-> S4
bool cantonActif[2] = {false, false};
int cantonEntree[2] = {-1, -1};
int cantonSortie[2] = {-1, -1};
bool cantonSortieVue[2] = {false, false};

bool etatToggleSensor[4][2] = {
  {false,false},
  {false,false},
  {false,false},
  {false,false}
};

unsigned long dernierVuSensorMs[4] = {0,0,0,0};
unsigned long dernierDetectSensorMs[4] = {0,0,0,0};
unsigned long dernierOkSecondaireMs[4] = {0,0,0,0};

String debugDernierSensorTexte = "Aucun paquet recu";
String debugDernierSensorMac = "-";
String debugDernierSensorSuffix = "-";
String debugDernierSensorEvent = "-";
String debugDernierSensorResultat = "En attente";
String debugDerniereAction = "-";
unsigned long debugDernierSensorMs = 0;

uint8_t dernierPrincipalMac[6] = {0,0,0,0,0,0};
bool dernierPrincipalValide = false;
unsigned long dernierHeartbeatSecondaireMs = 0;
const unsigned long INTERVALLE_HEARTBEAT_SECONDAIRE_MS = 3000UL;

unsigned long dernierMaintienAccessoiresMs = 0;
const unsigned long INTERVALLE_MAINTIEN_MS = 2000UL;
const unsigned long SYNC_DEMARRAGE_MS = 0UL;

// Surveillance legere des accessoires deja associes.
// IMPORTANT : pas de scan Wi-Fi automatique en boucle ici, car cela bloque l'interface.
// On maintient seulement les peers ESP-NOW et on envoie un heartbeat/ping leger.
// Les scans restent uniquement manuels via la page de recherche.
unsigned long dernierPingAccessoiresMs = 0;
unsigned long derniereTentativePeerAccessoiresMs = 0;
const unsigned long INTERVALLE_PING_ACCESSOIRES_MS = 5000UL;
const unsigned long INTERVALLE_TENTATIVE_PEER_MS = 30000UL;
const unsigned long DELAI_OK_SECONDAIRE_MS = 12000UL;
const unsigned long DELAI_OK_SENSOR_MS = 20000UL;
const uint8_t ECHECS_AVANT_RECONNEXION = 3;
uint8_t echecsSecondaire[4] = {0,0,0,0};
uint8_t echecsSensor[4] = {0,0,0,0};

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint8_t type;
  uint8_t event; // 0=heartbeat, 1=on, 2=off
  uint8_t apMac[6];
  char suffix[5];
  uint32_t seq;
} Pm3dSensorPacket;

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint8_t type;
  bool rougeGauche;
  bool rougeDroite;
  bool jaunePN;
  bool clignotementRougeActif;
  bool clignotementJauneActif;
  bool alternanceRouges;
  bool phaseRougeAllumee;
  bool phaseRougeAlternance;
  bool phaseJauneAllumee;
  int luminositeLed;
  unsigned long latenceExecutionMs;
  unsigned long syncDelayMs;
  unsigned long tempsRougeAllumageMs;
  unsigned long tempsRougeExtinctionMs;
  unsigned long tempsRougePauseMs;
  unsigned long tempsJauneAllumageMs;
  unsigned long tempsJauneExtinctionMs;
  unsigned long tempsJaunePauseMs;
  uint32_t seq;
} Pm3dStatePacket;

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint8_t type;
  uint8_t apMac[6];
  uint32_t seq;
} Pm3dAckPacket;

// Prototype manuel place apres les typedefs : evite que le preprocesseur Arduino
// genere un prototype avant la declaration de Pm3dSensorPacket.
void handleSensorPacket(const Pm3dSensorPacket &p);
void sendStateToSecondaries();

uint32_t seqState = 0;
uint32_t seqHeartbeat = 0;

// =======================================================
// Helpers
// =======================================================

String macToString(const uint8_t mac[6]) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

bool parseMac(const String &text, uint8_t mac[6]) {
  int v[6];
  if (sscanf(text.c_str(), "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
  for (int i=0;i<6;i++) {
    if (v[i] < 0 || v[i] > 255) return false;
    mac[i] = (uint8_t)v[i];
  }
  return true;
}

bool sameMac(const uint8_t a[6], const uint8_t b[6]) {
  for (int i=0;i<6;i++) if (a[i] != b[i]) return false;
  return true;
}

bool macMatchesVariants(const uint8_t sender[6], const String &savedText) {
  uint8_t saved[6];
  if (!parseMac(savedText, saved)) return false;
  if (sameMac(sender, saved)) return true;

  uint8_t v[6];
  memcpy(v, saved, 6);
  v[5] = saved[5] + 1; if (sameMac(sender, v)) return true;
  v[5] = saved[5] - 1; if (sameMac(sender, v)) return true;
  v[5] = saved[5] + 2; if (sameMac(sender, v)) return true;
  v[5] = saved[5] - 2; if (sameMac(sender, v)) return true;
  v[5] = saved[5] + 3; if (sameMac(sender, v)) return true;
  v[5] = saved[5] - 3; if (sameMac(sender, v)) return true;
  return false;
}

bool suffixMatchesSavedSensor(const char suffix[5], const String &savedSsid, const String &savedMac) {
  String suf = String(suffix);
  suf.trim();
  suf.toUpperCase();

  String ss = savedSsid;
  ss.toUpperCase();

  if (suf.length() == 4 && ss.indexOf(suf) >= 0) return true;

  uint8_t mac[6];
  if (parseMac(savedMac, mac)) {
    char m[5];
    snprintf(m, sizeof(m), "%02X%02X", mac[4], mac[5]);
    if (suf == String(m)) return true;
  }

  return false;
}

String htmlEscape(String s) {
  s.replace("&","&amp;");
  s.replace("\"","&quot;");
  s.replace("<","&lt;");
  s.replace(">","&gt;");
  return s;
}

String checked(bool v) { return v ? " checked" : ""; }
String selectedOption(const String &v, const String &cur) { return v == cur ? " selected" : ""; }

String macValue(const String &v) { return v; }

bool recent(unsigned long t, unsigned long durationMs) {
  return t > 0 && millis() - t < durationMs;
}

String badge(bool ok) {
  if (ok) return "<span class='okBadge'>OK</span>";
  return "<span class='nokBadge'>NOK</span>";
}

String normalizeMode(String v) {
  if (v == "onoff" || v == "presence" || v == "absence") return v;
  return "rien";
}

String normalizeAction(String v) {
  if (v == "jaune") return "jaune";
  return "rouge";
}

String buildApSsid() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
  return String("PM3D-signal ") + String(suffix);
}

// =======================================================
// GPIO / LED
// =======================================================

bool isValidGpio(int pin) {
  if (pin < 0) return true;
  if (pin > 48) return false;
  return true;
}

unsigned long sanitizeMs(unsigned long v, unsigned long def) {
  if (v > 60000UL) return def;
  return v;
}

void setupOneOutput(int pin) {
  if (pin < 0) return;
  pinMode(pin, OUTPUT);
  ledcAttach(pin, 5000, 8);
  ledcWrite(pin, 0);
}

void writeOneLed(int pin, bool state) {
  if (pin < 0) return;
  int duty = 0;
  if (state && luminositeLed > 0) duty = map(luminositeLed, 1, 100, 3, 255);
  ledcWrite(pin, duty);
}

void writeTestLedDirect(int pin, bool state) {
  if (pin < 0) return;
  pinMode(pin, OUTPUT);
  ledcAttach(pin, 5000, 8);
  int duty = 0;
  if (state && luminositeLed > 0) duty = map(luminositeLed, 1, 100, 3, 255);
  ledcWrite(pin, duty);
}

void stopLedGpioTests() {
  for (int i = 0; i < 3; i++) {
    if (ledTestTempPin[i] >= 0) writeTestLedDirect(ledTestTempPin[i], false);
    ledTestTempState[i] = false;
    ledTestTempPin[i] = -1;
  }
}

bool ledGpioTestActive() {
  for (int i = 0; i < 3; i++) {
    if (ledTestTempPin[i] >= 0) return true;
  }
  return false;
}

void applyLedOutputs() {
  // Pendant le test des GPIO LED, le programme principal ne reprend pas la main
  // sur les sorties. Le test reste donc totalement independant jusqu'a validation.
  if (ledGpioTestActive()) {
    for (int i = 0; i < 3; i++) {
      if (ledTestTempPin[i] >= 0) writeTestLedDirect(ledTestTempPin[i], ledTestTempState[i]);
    }
    return;
  }
  bool rg = rougeGauche;
  bool rd = rougeDroite;
  bool ja = jaunePN;

  if (clignotementRougeActif) {
    if (!(rougeGauche || rougeDroite)) {
      rg = false; rd = false;
    } else if (alternanceRouges) {
      if (rougeGauche && rougeDroite) {
        rg = phaseRougeAlternance && phaseRougeAllumee;
        rd = !phaseRougeAlternance && phaseRougeAllumee;
      } else if (rougeGauche) {
        rg = phaseRougeAllumee;
        rd = false;
      } else {
        rg = false;
        rd = phaseRougeAllumee;
      }
    } else {
      rg = rougeGauche && phaseRougeAllumee;
      rd = rougeDroite && phaseRougeAllumee;
    }
  }

  if (clignotementJauneActif) {
    ja = jaunePN && phaseJauneAllumee;
  }

  writeOneLed(gpioRougeGauche, rg);
  writeOneLed(gpioRougeDroite, rd);
  writeOneLed(gpioJaunePN, ja);
}

void updateBlinkEngine() {
  unsigned long now = millis();

  if (!clignotementRougeActif) {
    phaseRougeAllumee = true;
  } else {
    unsigned long d = phaseRougeAllumee ? tempsRougeAllumageMs : (tempsRougeExtinctionMs + tempsRougePauseMs);
    if (d < 50) d = 50;
    if (now - dernierChangementRouge >= d) {
      dernierChangementRouge = now;
      phaseRougeAllumee = !phaseRougeAllumee;
      if (alternanceRouges && phaseRougeAllumee) phaseRougeAlternance = !phaseRougeAlternance;
      applyLedOutputs();
      if (!modeSecondaire) sendStateToSecondaries();
    }
  }

  if (!clignotementJauneActif) {
    phaseJauneAllumee = true;
  } else {
    unsigned long d = phaseJauneAllumee ? tempsJauneAllumageMs : (tempsJauneExtinctionMs + tempsJaunePauseMs);
    if (d < 50) d = 50;
    if (now - dernierChangementJaune >= d) {
      dernierChangementJaune = now;
      phaseJauneAllumee = !phaseJauneAllumee;
      applyLedOutputs();
      if (!modeSecondaire) sendStateToSecondaries();
    }
  }
}

// =======================================================
// Preferences
// =======================================================

void loadConfig() {
  prefs.begin("pm3d-pn", true);

  gpioRougeGauche = prefs.getInt("rg", 2);
  gpioRougeDroite = prefs.getInt("rd", 3);
  gpioJaunePN = prefs.getInt("ja", 4);

  modeAutomatique = prefs.getBool("auto", true);
  wifiApName = prefs.getString("apname", "");
  wifiApPassword = prefs.getString("appass", "");
  otaStaSsid = prefs.getString("otassid", "");
  otaStaPassword = prefs.getString("otapass", "");
  currentLang = prefs.getString("lang", "FR");
  currentLang.toUpperCase();
  if (currentLang != "FR" && currentLang != "NL" && currentLang != "DE" && currentLang != "EN") currentLang = "FR";
  configDoubleVoie = prefs.getBool("double", false);
  luminositeLed = prefs.getInt("lum", 55);
  if (luminositeLed < 0 || luminositeLed > 100) luminositeLed = 55;

  latenceExecutionMs = sanitizeMs(prefs.getULong("latms", 0), 0);

  for (int i=0;i<4;i++) {
    macSecondaire[i] = prefs.getString(("msec" + String(i)).c_str(), "");
    ssidSecondaire[i] = prefs.getString(("ssec" + String(i)).c_str(), "");

    macSensor[i] = prefs.getString(("msen" + String(i)).c_str(), "");
    ssidSensor[i] = prefs.getString(("ssen" + String(i)).c_str(), "");
    ordreSensorClient[i] = prefs.getInt(("ordsen" + String(i)).c_str(), i);
    if (ordreSensorClient[i] < 0 || ordreSensorClient[i] > 3) ordreSensorClient[i] = i;

    modeSensor[i][0] = prefs.getString(("moa" + String(i)).c_str(), "rien");
    actionSensor[i][0] = prefs.getString(("aca" + String(i)).c_str(), "rouge");
    modeSensor[i][1] = prefs.getString(("mob" + String(i)).c_str(), "rien");
    actionSensor[i][1] = prefs.getString(("acb" + String(i)).c_str(), "rouge");
  }

  clignotementRougeActif = prefs.getBool("blinkR", false);
  alternanceRouges = prefs.getBool("altR", true);
  tempsRougeAllumageMs = sanitizeMs(prefs.getULong("ronms", 400), 400);
  tempsRougeExtinctionMs = sanitizeMs(prefs.getULong("roffms", 100), 100);
  tempsRougePauseMs = sanitizeMs(prefs.getULong("rpaus", 0), 0);

  clignotementJauneActif = prefs.getBool("blinkJ", false);
  tempsJauneAllumageMs = sanitizeMs(prefs.getULong("jonms", 700), 700);
  tempsJauneExtinctionMs = sanitizeMs(prefs.getULong("joffms", 300), 300);
  tempsJaunePauseMs = sanitizeMs(prefs.getULong("jpaus", 1200), 1200);

  prefs.end();

  if (!isValidGpio(gpioRougeGauche)) gpioRougeGauche = 2;
  if (!isValidGpio(gpioRougeDroite)) gpioRougeDroite = 3;
  if (!isValidGpio(gpioJaunePN)) gpioJaunePN = 4;

  for (int i=0;i<4;i++) {
    modeSensor[i][0] = normalizeMode(modeSensor[i][0]);
    modeSensor[i][1] = normalizeMode(modeSensor[i][1]);
    actionSensor[i][0] = normalizeAction(actionSensor[i][0]);
    actionSensor[i][1] = normalizeAction(actionSensor[i][1]);
  }
}

void saveConfig() {
  prefs.begin("pm3d-pn", false);

  prefs.putInt("rg", gpioRougeGauche);
  prefs.putInt("rd", gpioRougeDroite);
  prefs.putInt("ja", gpioJaunePN);

  prefs.putBool("auto", modeAutomatique);
  prefs.putString("apname", wifiApName);
  prefs.putString("appass", wifiApPassword);
  prefs.putString("otassid", otaStaSsid);
  prefs.putString("otapass", otaStaPassword);
  prefs.putString("lang", currentLang);
  prefs.putBool("double", configDoubleVoie);
  prefs.putInt("lum", luminositeLed);
  prefs.putULong("latms", latenceExecutionMs);

  for (int i=0;i<4;i++) {
    prefs.putString(("msec" + String(i)).c_str(), macSecondaire[i]);
    prefs.putString(("ssec" + String(i)).c_str(), ssidSecondaire[i]);

    prefs.putString(("msen" + String(i)).c_str(), macSensor[i]);
    prefs.putString(("ssen" + String(i)).c_str(), ssidSensor[i]);
    prefs.putInt(("ordsen" + String(i)).c_str(), ordreSensorClient[i]);

    prefs.putString(("moa" + String(i)).c_str(), modeSensor[i][0]);
    prefs.putString(("aca" + String(i)).c_str(), actionSensor[i][0]);
    prefs.putString(("mob" + String(i)).c_str(), modeSensor[i][1]);
    prefs.putString(("acb" + String(i)).c_str(), actionSensor[i][1]);
  }

  prefs.putBool("blinkR", clignotementRougeActif);
  prefs.putBool("altR", alternanceRouges);
  prefs.putULong("ronms", tempsRougeAllumageMs);
  prefs.putULong("roffms", tempsRougeExtinctionMs);
  prefs.putULong("rpaus", tempsRougePauseMs);

  prefs.putBool("blinkJ", clignotementJauneActif);
  prefs.putULong("jonms", tempsJauneAllumageMs);
  prefs.putULong("joffms", tempsJauneExtinctionMs);
  prefs.putULong("jpaus", tempsJaunePauseMs);

  prefs.end();
}

// =======================================================
// ESP-NOW peers / send
// =======================================================

void addPeerArray(const uint8_t mac[6]) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 1;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
}

void addPeerVariants(const String &macText) {
  uint8_t mac[6];
  if (!parseMac(macText, mac)) return;
  uint8_t v[6];

  memcpy(v, mac, 6); addPeerArray(v);
  memcpy(v, mac, 6); v[5] = mac[5] + 1; addPeerArray(v);
  memcpy(v, mac, 6); v[5] = mac[5] - 1; addPeerArray(v);
  memcpy(v, mac, 6); v[5] = mac[5] + 2; addPeerArray(v);
  memcpy(v, mac, 6); v[5] = mac[5] - 2; addPeerArray(v);
}

void reconnectSavedAccessories() {
  for (int i=0;i<4;i++) {
    addPeerVariants(macSecondaire[i]);
    addPeerVariants(macSensor[i]);
  }
}

// Scan automatique supprime volontairement : les scans Wi-Fi restent manuels dans les pages de recherche.

void sendStateToOne(const String &macText) {
  uint8_t mac[6];
  if (!parseMac(macText, mac)) return;

  Pm3dStatePacket p;
  p.magic = PM3D_MAGIC;
  p.type = PKT_STATE;
  p.rougeGauche = rougeGauche;
  p.rougeDroite = rougeDroite;
  p.jaunePN = jaunePN;
  p.clignotementRougeActif = clignotementRougeActif;
  p.clignotementJauneActif = clignotementJauneActif;
  p.alternanceRouges = clignotementRougeActif ? true : alternanceRouges;
  p.phaseRougeAllumee = phaseRougeAllumee;
  p.phaseRougeAlternance = phaseRougeAlternance;
  p.phaseJauneAllumee = phaseJauneAllumee;
  p.luminositeLed = luminositeLed;
  p.latenceExecutionMs = latenceExecutionMs;
  p.syncDelayMs = SYNC_DEMARRAGE_MS;
  p.tempsRougeAllumageMs = tempsRougeAllumageMs;
  p.tempsRougeExtinctionMs = tempsRougeExtinctionMs;
  p.tempsRougePauseMs = tempsRougePauseMs;
  p.tempsJauneAllumageMs = tempsJauneAllumageMs;
  p.tempsJauneExtinctionMs = tempsJauneExtinctionMs;
  p.tempsJaunePauseMs = tempsJaunePauseMs;
  p.seq = ++seqState;

  addPeerVariants(macText);

  uint8_t v[6];
  memcpy(v, mac, 6); esp_now_send(v, (uint8_t*)&p, sizeof(p));
  memcpy(v, mac, 6); v[5] = mac[5] + 1; esp_now_send(v, (uint8_t*)&p, sizeof(p));
  memcpy(v, mac, 6); v[5] = mac[5] - 1; esp_now_send(v, (uint8_t*)&p, sizeof(p));
  memcpy(v, mac, 6); v[5] = mac[5] + 2; esp_now_send(v, (uint8_t*)&p, sizeof(p));
  memcpy(v, mac, 6); v[5] = mac[5] - 2; esp_now_send(v, (uint8_t*)&p, sizeof(p));
}

void sendStateToSecondaries() {
  for (int i=0;i<4;i++) sendStateToOne(macSecondaire[i]);
}

void sendAckToPrincipal(const uint8_t *destMac) {
  if (destMac == nullptr) return;

  Pm3dAckPacket ack;
  ack.magic = PM3D_MAGIC;
  ack.type = PKT_ACK;
  esp_read_mac(ack.apMac, ESP_MAC_WIFI_SOFTAP);
  ack.seq = ++seqHeartbeat;

  addPeerArray(destMac);
  esp_now_send(destMac, (uint8_t*)&ack, sizeof(ack));
}

void updateOkSecondaryFromMac(const uint8_t mac[6]) {
  for (int i=0;i<4;i++) {
    if (macMatchesVariants(mac, macSecondaire[i])) {
      dernierOkSecondaireMs[i] = millis();
      echecsSecondaire[i] = 0;
    }
  }
}

void updateOkSensorFromMac(const uint8_t mac[6]) {
  for (int i=0;i<4;i++) {
    if (macMatchesVariants(mac, macSensor[i])) {
      dernierVuSensorMs[i] = millis();
      echecsSensor[i] = 0;
    }
  }
}

// =======================================================
// Sensor actions
// =======================================================


void applyLocalAfterSyncDelay() {
  if (SYNC_DEMARRAGE_MS > 0) delay(SYNC_DEMARRAGE_MS);
  dernierChangementRouge = millis();
  dernierChangementJaune = millis();
  applyLedOutputs();
}


void lancerJauneClignotantAuto() {
  debugDerniereAction = "Auto : jaune clignotant";
  resetCantons();

  rougeGauche = false;
  rougeDroite = false;
  jaunePN = true;

  clignotementRougeActif = false;
  clignotementJauneActif = true;

  phaseRougeAllumee = true;
  phaseRougeAlternance = true;
  phaseJauneAllumee = true;

  dernierChangementRouge = millis();
  dernierChangementJaune = millis();

  sendStateToSecondaries();
  applyLocalAfterSyncDelay();
}

void lancerRougeClignotantAuto() {
  debugDerniereAction = "Auto : rouge clignotant";

  rougeGauche = true;
  rougeDroite = true;
  jaunePN = false;

  clignotementRougeActif = true;
  clignotementJauneActif = false;
  alternanceRouges = true;

  phaseRougeAllumee = true;
  phaseRougeAlternance = true;
  phaseJauneAllumee = true;

  dernierChangementRouge = millis();
  dernierChangementJaune = millis();

  sendStateToSecondaries();
  applyLocalAfterSyncDelay();
}

void eteindreSignalParSensor() {
  debugDerniereAction = "Arret / extinction par sensor";
  rougeGauche = false;
  rougeDroite = false;
  jaunePN = false;
  clignotementRougeActif = false;
  clignotementJauneActif = false;
  sendStateToSecondaries();
  applyLocalAfterSyncDelay();
}

void lancerActionSensor(const String &action) {
  debugDerniereAction = "Action sensor : " + action;

  if (action == "jaune") {
    rougeGauche = false;
    rougeDroite = false;
    jaunePN = true;

    clignotementRougeActif = false;
    clignotementJauneActif = true;

    phaseRougeAllumee = true;
    phaseRougeAlternance = true;
    phaseJauneAllumee = true;

    dernierChangementRouge = millis();
    dernierChangementJaune = millis();
  } else {
    // ROUGE CLIGNOTANT SENSOR =
    // 2 rouges actives + alternance gauche/droite
    // avec les reglages avances :
    // tempsRougeAllumageMs / tempsRougeExtinctionMs / tempsRougePauseMs.
    rougeGauche = true;
    rougeDroite = true;
    jaunePN = false;

    clignotementRougeActif = true;
    clignotementJauneActif = false;
    alternanceRouges = true;

    phaseRougeAllumee = true;
    phaseRougeAlternance = true;
    phaseJauneAllumee = true;

    dernierChangementRouge = millis();
    dernierChangementJaune = millis();
  }

  sendStateToSecondaries();
  applyLocalAfterSyncDelay();
}

void appliquerConditionSensor(int idx, int cond, const String &eventText) {
  String ev = eventText;
  ev.toLowerCase();

  String mode = modeSensor[idx][cond];
  String action = actionSensor[idx][cond];

  if (mode == "rien") return;

  bool isOn = (ev.indexOf("on") >= 0 || ev == "1" || ev.indexOf("presence") >= 0 || ev.indexOf("detect") >= 0 || ev.indexOf("trigger") >= 0);
  bool isOff = (ev.indexOf("off") >= 0 || ev == "0" || ev.indexOf("absence") >= 0 || ev.indexOf("clear") >= 0 || ev.indexOf("stop") >= 0);

  if (mode == "onoff") {
    if (!isOn) return;
    etatToggleSensor[idx][cond] = !etatToggleSensor[idx][cond];
    if (etatToggleSensor[idx][cond]) lancerActionSensor(action);
    else eteindreSignalParSensor();
    return;
  }

  if (mode == "presence") {
    if (isOn) lancerActionSensor(action);
    else if (isOff) eteindreSignalParSensor();
    return;
  }

  if (mode == "absence") {
    if (isOff) lancerActionSensor(action);
    else if (isOn) eteindreSignalParSensor();
    return;
  }
}

int logiqueDepuisPhysiqueSensor(int physiqueIdx) {
  for (int logique = 0; logique < 4; logique++) {
    if (ordreSensorClient[logique] == physiqueIdx) return logique;
  }
  return physiqueIdx;
}

int cantonPourSensor(int idx) {
  if (idx == 0 || idx == 1) return 0;
  if (idx == 2 || idx == 3) return 1;
  return -1;
}

int sensorOpposeDansCanton(int idx) {
  if (idx == 0) return 1;
  if (idx == 1) return 0;
  if (idx == 2) return 3;
  if (idx == 3) return 2;
  return -1;
}

bool aucunCantonActif() {
  return !cantonActif[0] && !cantonActif[1];
}

void resetCantons() {
  for (int i = 0; i < 2; i++) {
    cantonActif[i] = false;
    cantonEntree[i] = -1;
    cantonSortie[i] = -1;
    cantonSortieVue[i] = false;
  }
}

void handleSensorPacket(const Pm3dSensorPacket &p) {
  debugDernierSensorMs = millis();
  debugDernierSensorMac = macToString(p.apMac);
  debugDernierSensorSuffix = String(p.suffix);
  debugDernierSensorEvent = (p.event == 1) ? "ON" : ((p.event == 2) ? "OFF" : "HEARTBEAT");
  debugDerniereAction = "-";
  debugDernierSensorTexte = "Paquet ESP-NOW recu";

  int idx = -1;
  for (int i=0;i<4;i++) {
    if (macMatchesVariants(p.apMac, macSensor[i]) || suffixMatchesSavedSensor(p.suffix, ssidSensor[i], macSensor[i])) {
      idx = i;
      break;
    }
  }

  if (idx < 0) {
    debugDernierSensorResultat = "IGNORE - aucun sensor configure ne correspond";
    Serial.print("Sensor ignore : ");
    Serial.print(macToString(p.apMac));
    Serial.print(" suffix=");
    Serial.println(p.suffix);
    return;
  }

  int idxPhysique = idx;
  idx = logiqueDepuisPhysiqueSensor(idxPhysique);

  int maxSensors = configDoubleVoie ? 4 : 2;
  if (idx >= maxSensors) {
    debugDernierSensorResultat = "IGNORE - sensor hors configuration";
    debugDerniereAction = configDoubleVoie ? "Double voie" : "Simple voie : S3/S4 ignores";
    return;
  }

  dernierVuSensorMs[idx] = millis();
  debugDernierSensorResultat = "Reconnu physique S" + String(idxPhysique + 1) + " -> logique S" + String(idx + 1);

  if (p.event == 0) {
    debugDerniereAction = "Heartbeat uniquement - pas d'action";
    return;
  }

  if (p.event == 1) dernierDetectSensorMs[idx] = millis();

  if (!modeAutomatique) {
    debugDerniereAction = "Mode manuel : sensor recu sans scenario auto";
    return;
  }

  // Logique par canton :
  // Simple voie : seul le canton S1<->S2 est utilise.
  // Double voie : deux cantons independants S1<->S2 et S3<->S4.
  // Un canton est ouvert par le premier sensor touche.
  // Il se referme uniquement quand le sensor oppose a detecte puis s'est libere.
  int canton = cantonPourSensor(idx);
  int sortieOpposee = sensorOpposeDansCanton(idx);

  if (canton < 0 || sortieOpposee < 0) return;

  if (!configDoubleVoie && canton > 0) {
    debugDerniereAction = "Simple voie : S3/S4 ignores";
    return;
  }

  if (p.event == 1) {
    if (!cantonActif[canton]) {
      cantonActif[canton] = true;
      cantonEntree[canton] = idx;
      cantonSortie[canton] = sortieOpposee;
      cantonSortieVue[canton] = false;

      debugDerniereAction = String(configDoubleVoie ? "Double voie" : "Simple voie") +
                            " : canton " + String(canton + 1) +
                            " ouvert par S" + String(idx + 1) +
                            " -> sortie S" + String(sortieOpposee + 1) +
                            " -> rouge";
      lancerRougeClignotantAuto();
      return;
    }

    if (idx == cantonSortie[canton]) {
      cantonSortieVue[canton] = true;
      debugDerniereAction = "Canton " + String(canton + 1) +
                            " : sortie S" + String(idx + 1) +
                            " detectee -> maintien rouge";
      return;
    }

    debugDerniereAction = "Canton " + String(canton + 1) + " deja actif -> maintien rouge";
    return;
  }

  if (p.event == 2) {
    if (cantonActif[canton] && idx == cantonSortie[canton] && cantonSortieVue[canton]) {
      cantonActif[canton] = false;
      cantonEntree[canton] = -1;
      cantonSortie[canton] = -1;
      cantonSortieVue[canton] = false;

      if (aucunCantonActif()) {
        debugDerniereAction = "Tous les cantons sont refermes -> retour jaune";
        lancerJauneClignotantAuto();
      } else {
        debugDerniereAction = "Canton " + String(canton + 1) +
                              " referme, autre canton actif -> maintien rouge";
      }
      return;
    }

    debugDerniereAction = "S" + String(idx + 1) + " libre mais canton non refermable";
    return;
  }
}

// =======================================================
// ESP-NOW receive/callbacks
// =======================================================

void onEspNowSend(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS && tx_info != nullptr) {
    updateOkSecondaryFromMac(tx_info->des_addr);
  }
}

void onEspNowReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len < 5) return;

  uint32_t magic;
  memcpy(&magic, data, sizeof(magic));
  if (magic != PM3D_MAGIC) return;

  uint8_t type = data[4];

  if (type == PKT_SENSOR && len == sizeof(Pm3dSensorPacket)) {
    Pm3dSensorPacket p;
    memcpy(&p, data, sizeof(p));
    handleSensorPacket(p);
    return;
  }

  if (type == PKT_ACK && len == sizeof(Pm3dAckPacket)) {
    Pm3dAckPacket p;
    memcpy(&p, data, sizeof(p));
    updateOkSecondaryFromMac(p.apMac);
    return;
  }

  if (type == PKT_STATE && len == sizeof(Pm3dStatePacket)) {
    Pm3dStatePacket p;
    memcpy(&p, data, sizeof(p));

    modeSecondaire = true;
    dernierOrdrePrincipalMs = millis();

    if (info != nullptr) {
      memcpy(dernierPrincipalMac, info->src_addr, 6);
      dernierPrincipalValide = true;
      addPeerArray(dernierPrincipalMac);
    }

    unsigned long delaiTotal = p.syncDelayMs;
    if (p.latenceExecutionMs > 0 && p.latenceExecutionMs <= 5000UL) delaiTotal += p.latenceExecutionMs;
    if (delaiTotal > 0 && delaiTotal <= 6000UL) delay(delaiTotal);

    rougeGauche = p.rougeGauche;
    rougeDroite = p.rougeDroite;
    jaunePN = p.jaunePN;
    clignotementRougeActif = p.clignotementRougeActif;
    clignotementJauneActif = p.clignotementJauneActif;
    alternanceRouges = p.clignotementRougeActif ? true : p.alternanceRouges;
    phaseRougeAllumee = p.phaseRougeAllumee;
    phaseRougeAlternance = p.phaseRougeAlternance;
    phaseJauneAllumee = p.phaseJauneAllumee;
    luminositeLed = p.luminositeLed;
    // latenceExecutionMs reste un réglage du maître, elle ne devient pas le réglage local du secondaire.

    tempsRougeAllumageMs = p.tempsRougeAllumageMs;
    tempsRougeExtinctionMs = p.tempsRougeExtinctionMs;
    tempsRougePauseMs = p.tempsRougePauseMs;
    tempsJauneAllumageMs = p.tempsJauneAllumageMs;
    tempsJauneExtinctionMs = p.tempsJauneExtinctionMs;
    tempsJaunePauseMs = p.tempsJaunePauseMs;

    dernierChangementRouge = millis();
    dernierChangementJaune = millis();

    applyLedOutputs();

    if (info != nullptr) sendAckToPrincipal(info->src_addr);
    return;
  }
}

void updateRoleSecondaire() {
  if (!modeSecondaire) return;

  if (millis() - dernierOrdrePrincipalMs > DELAI_RETOUR_AUTONOME_MS) {
    modeSecondaire = false;
    dernierPrincipalValide = false;
    resetCantons();
    if (modeAutomatique) lancerJauneClignotantAuto();
    Serial.println("Retour autonome : principal perdu.");
  }
}

void updateHeartbeatSecondaire() {
  if (!modeSecondaire || !dernierPrincipalValide) return;
  if (millis() - dernierHeartbeatSecondaireMs < INTERVALLE_HEARTBEAT_SECONDAIRE_MS) return;

  dernierHeartbeatSecondaireMs = millis();
  sendAckToPrincipal(dernierPrincipalMac);
}

void updateMaintienAccessoires() {
  if (modeSecondaire) return;

  unsigned long now = millis();

  // Maintien leger des peers ESP-NOW : aucune recherche Wi-Fi ici.
  // addPeerVariants est rapide et non bloquant, contrairement a WiFi.scanNetworks().
  if (now - dernierMaintienAccessoiresMs >= INTERVALLE_MAINTIEN_MS) {
    dernierMaintienAccessoiresMs = now;
    reconnectSavedAccessories();
  }

  // Ping/heartbeat leger : on renvoie ponctuellement l'etat aux secondaires.
  // Les ACK recus remettent echecsSecondaire[] a zero via updateOkSecondaryFromMac().
  if (now - dernierPingAccessoiresMs < INTERVALLE_PING_ACCESSOIRES_MS) return;
  dernierPingAccessoiresMs = now;

  bool tentativePeerAutorisee = (now - derniereTentativePeerAccessoiresMs >= INTERVALLE_TENTATIVE_PEER_MS);
  bool peerRelance = false;

  for (int i = 0; i < 4; i++) {
    if (macSecondaire[i].length() > 0) {
      bool secondaireOk = recent(dernierOkSecondaireMs[i], DELAI_OK_SECONDAIRE_MS);
      if (!secondaireOk) {
        if (echecsSecondaire[i] < 250) echecsSecondaire[i]++;

        // Ping leger : un paquet state sert aussi de heartbeat pour le secondaire.
        sendStateToOne(macSecondaire[i]);

        // Apres plusieurs echecs, on relance juste les peers ESP-NOW, mais sans scan Wi-Fi.
        if (echecsSecondaire[i] >= ECHECS_AVANT_RECONNEXION && tentativePeerAutorisee) {
          addPeerVariants(macSecondaire[i]);
          peerRelance = true;
        }
      }
    }

    if (macSensor[i].length() > 0) {
      bool sensorOk = recent(dernierVuSensorMs[i], DELAI_OK_SENSOR_MS);
      if (!sensorOk) {
        if (echecsSensor[i] < 250) echecsSensor[i]++;

        // Les sensors parlent d'eux-memes par heartbeat/event. On ne lance pas de scan ici.
        // On garde seulement les peers connus prets si le sensor utilise ESP-NOW bidirectionnel.
        if (echecsSensor[i] >= ECHECS_AVANT_RECONNEXION && tentativePeerAutorisee) {
          addPeerVariants(macSensor[i]);
          peerRelance = true;
        }
      }
    }
  }

  if (peerRelance) {
    derniereTentativePeerAccessoiresMs = now;
    Serial.println("Maintien accessoires : relance peers ESP-NOW sans scan Wi-Fi.");
  }
}

// =======================================================
// HTML
// =======================================================

String ledSvg() {
  String svg;
  svg += "<svg viewBox='0 0 340 280'>";
  svg += "<rect x='38' y='34' width='264' height='210' rx='22' fill='#050607' stroke='#ffffff' stroke-width='4'/>";
  svg += "<rect x='55' y='51' width='230' height='176' rx='12' fill='#0b0d10' opacity='.95'/>";

  svg += "<a href='/toggle?led=rg'><g>";
  svg += "<circle cx='120' cy='110' r='25' fill='#ffe7ef'/>";
  svg += "<circle cx='120' cy='110' r='18' fill='#5f0c0c' stroke='#0d0f12' stroke-width='3'/>";
  svg += "<circle cx='120' cy='110' r='14' fill='" + String(rougeGauche ? "#ff3030" : "#5f0c0c") + "'/>";
  svg += "</g></a>";

  svg += "<a href='/toggle?led=rd'><g>";
  svg += "<circle cx='220' cy='110' r='25' fill='#ffe7ef'/>";
  svg += "<circle cx='220' cy='110' r='18' fill='#5f0c0c' stroke='#0d0f12' stroke-width='3'/>";
  svg += "<circle cx='220' cy='110' r='14' fill='" + String(rougeDroite ? "#ff3030" : "#5f0c0c") + "'/>";
  svg += "</g></a>";

  svg += "<a href='/toggle?led=ja'><g>";
  svg += "<circle cx='170' cy='190' r='25' fill='#f3ffd9'/>";
  svg += "<circle cx='170' cy='190' r='18' fill='#6a5a00' stroke='#0d0f12' stroke-width='3'/>";
  svg += "<circle cx='170' cy='190' r='14' fill='" + String(jaunePN ? "#ffe34a" : "#6a5a00") + "'/>";
  svg += "</g></a>";

  svg += "</svg>";
  return svg;
}

String cssCommon() {
  String css;
  css += "body{margin:0;padding:10px;text-align:center;font-family:Arial,Helvetica,sans-serif;color:#F6FBFF;background:radial-gradient(circle at 50% -10%,rgba(80,170,230,.24),rgba(4,14,30,.55) 38%,#01040A 78%),linear-gradient(180deg,#06162B,#020711);background-attachment:fixed;}";
  css += "body:before{content:'';position:fixed;inset:0;pointer-events:none;background:linear-gradient(rgba(255,255,255,.035) 1px,transparent 1px),linear-gradient(90deg,rgba(255,255,255,.025) 1px,transparent 1px);background-size:28px 28px;opacity:.28;}";
  css += ".wrap{max-width:760px;margin:0 auto;position:relative;z-index:1;}";
  css += ".card{background:linear-gradient(180deg,rgba(19,54,90,.96),rgba(7,22,42,.98));border:1px solid rgba(164,220,255,.22);border-radius:22px;padding:13px;margin-bottom:11px;box-shadow:0 18px 44px rgba(0,0,0,.42),inset 0 1px 0 rgba(255,255,255,.14),inset 0 -1px 0 rgba(0,0,0,.45);}";
  css += ".title{font-size:22px;font-weight:900;margin:6px 0 12px;letter-spacing:.2px;text-shadow:0 2px 8px rgba(0,0,0,.55);}";
  css += ".btn{display:block;position:relative;overflow:hidden;border:1px solid rgba(230,246,255,.34);border-radius:14px;padding:12px;background:linear-gradient(180deg,#d7edf8 0%,#69b7e6 12%,#1b78b0 45%,#082f56 100%);color:white;font-size:15px;font-weight:900;text-decoration:none;margin:8px 0;box-shadow:inset 0 1px 0 rgba(255,255,255,.70),inset 0 10px 14px rgba(255,255,255,.10),inset 0 -10px 18px rgba(0,0,0,.42),0 8px 18px rgba(0,0,0,.30);text-shadow:0 1px 3px rgba(0,0,0,.65);}";
  css += ".btn:before{content:'';position:absolute;left:-20%;right:-20%;top:0;height:42%;background:linear-gradient(180deg,rgba(255,255,255,.42),rgba(255,255,255,.08),transparent);pointer-events:none;}";
  css += ".btn:after{content:'';position:absolute;inset:1px;border-radius:13px;border-top:1px solid rgba(255,255,255,.45);pointer-events:none;}";
  css += ".btn:active{transform:translateY(1px);filter:brightness(.95);}";
  css += ".danger{background:linear-gradient(180deg,#ffb1b1 0%,#c93535 42%,#6d1018 100%);}.selectedMode{color:#07111c !important;border:1px solid rgba(255,255,255,.78) !important;box-shadow:0 0 0 2px rgba(255,255,255,.75) inset,inset 0 1px 0 rgba(255,255,255,.95),inset 0 -12px 20px rgba(0,0,0,.32),0 0 24px rgba(210,225,235,.38),0 10px 24px rgba(0,0,0,.38) !important;background:linear-gradient(180deg,#ffffff 0%,#cfd8de 18%,#8b969d 50%,#e5edf2 100%) !important;text-shadow:0 1px 0 rgba(255,255,255,.55) !important;}";
  css += ".okBadge{display:inline-block;margin-left:6px;padding:3px 8px;border-radius:10px;background:linear-gradient(180deg,#55f08b,#00a846);color:#001b09;font-weight:900;font-size:11px;box-shadow:0 0 10px rgba(0,200,83,.4);}";
  css += ".nokBadge{display:inline-block;margin-left:6px;padding:3px 8px;border-radius:10px;background:linear-gradient(180deg,#ff5a5a,#b40000);color:white;font-weight:900;font-size:11px;box-shadow:0 0 10px rgba(216,0,0,.4);}";
  css += "input,select{width:100%;box-sizing:border-box;border-radius:12px;border:1px solid rgba(164,220,255,.28);background:linear-gradient(180deg,#06111F,#02070E);color:white;padding:9px;font-size:15px;font-weight:800;box-shadow:inset 0 1px 0 rgba(255,255,255,.08);}";
  css += "label{font-weight:800;color:#D9ECF7;font-size:14px;text-align:left;display:block;margin-top:8px;}";
  css += ".grid{display:grid;grid-template-columns:1fr 110px;gap:7px 9px;align-items:center;}";
  css += ".hint{font-size:12px;color:#C2DDEC;margin:7px 0;line-height:1.45;}";
  css += ".back{position:absolute;left:0;top:0;width:40px;height:40px;border-radius:13px;background:linear-gradient(180deg,#5fbdf2,#0b3b67);color:white;text-decoration:none;display:flex;align-items:center;justify-content:center;font-weight:900;box-shadow:inset 0 1px 0 rgba(255,255,255,.38),0 8px 18px rgba(0,0,0,.35);}";
  css += ".sub{border:1px solid rgba(143,212,255,.25);border-radius:16px;padding:11px;background:linear-gradient(180deg,rgba(8,32,58,.72),rgba(0,0,0,.20));margin:11px 0;text-align:left;box-shadow:inset 0 1px 0 rgba(255,255,255,.08);}";
  css += ".section{font-size:13px;font-weight:900;color:#8fd4ff;text-transform:uppercase;margin:12px 0 7px;text-align:left;letter-spacing:.4px;border-top:1px solid rgba(143,212,255,.14);padding-top:9px;}";
  css += ".sensorCards{display:grid;grid-template-columns:1fr;gap:12px;}";
  css += ".sensorCard{border:1px solid rgba(143,212,255,.28);border-radius:15px;padding:12px;background:linear-gradient(180deg,rgba(7,24,43,.82),rgba(0,0,0,.24));box-shadow:inset 0 1px 0 rgba(255,255,255,.08);}";
  css += ".sensorCardTitle{text-align:center;font-size:16px;font-weight:900;margin-bottom:8px;}";
  css += ".cond{font-size:12px;font-weight:900;color:#8fd4ff;margin:8px 0 4px;text-transform:uppercase;}";
  css += ".dot{width:28px;height:28px;border-radius:50%;display:inline-block;border:3px solid white;box-shadow:0 0 12px rgba(0,0,0,.8);}";
  css += ".red{background:#d80000;box-shadow:0 0 18px rgba(216,0,0,.75);}.green{background:#00c853;box-shadow:0 0 20px rgba(0,200,83,.85);}";
  css += "svg{width:95%;max-width:420px;height:auto;filter:drop-shadow(0 16px 26px rgba(0,0,0,.50));}";
  css += ".sensorPlan{border:1px solid rgba(143,212,255,.28);border-radius:16px;padding:12px;background:radial-gradient(circle at 50% 0%,rgba(143,212,255,.10),rgba(0,0,0,.20));}";
  css += ".pnScene{position:relative;height:150px;margin:12px 0;border-radius:16px;background:linear-gradient(180deg,#243321,#172216);overflow:hidden;border:1px solid rgba(255,255,255,.12);}";
  css += ".roadPN{position:absolute;left:43%;top:-10%;width:14%;height:120%;background:linear-gradient(90deg,#1b1f24,#343b43,#1b1f24);border-left:2px solid #e8edf2;border-right:2px solid #e8edf2;z-index:2;box-shadow:0 0 18px rgba(0,0,0,.45);}";
  css += ".roadPN:before{content:'';position:absolute;left:48%;top:0;width:2px;height:100%;background:repeating-linear-gradient(180deg,#f5f5f5 0 12px,transparent 12px 24px);opacity:.85;}";
  css += ".railTrack{position:absolute;left:5%;right:5%;height:46px;z-index:3;}";
  css += ".trackA{top:50px;}.trackB{top:92px;}.simpleMode .trackA{top:28px;}.doubleMode .trackA{top:50px;}.doubleMode .trackB{top:92px;}";
  css += ".railTrack:before,.railTrack:after{content:'';position:absolute;left:0;right:0;height:4px;background:linear-gradient(180deg,#f0f5f8,#7e8a92);border-radius:3px;box-shadow:0 1px 3px rgba(0,0,0,.7);}";
  css += ".railTrack:before{top:12px;}.railTrack:after{top:30px;}";
  css += ".sleepers{position:absolute;left:0;right:0;top:5px;height:36px;background:repeating-linear-gradient(90deg,transparent 0 12px,#5a3d25 12px 17px,transparent 17px 29px);opacity:.9;filter:drop-shadow(0 1px 1px rgba(0,0,0,.8));}";
  css += ".pnPlate{position:absolute;left:40%;top:0;width:20%;height:100%;background:rgba(25,28,31,.38);border-left:1px solid rgba(255,255,255,.25);border-right:1px solid rgba(255,255,255,.25);z-index:1;}";
  css += ".sensorPoint{position:absolute;z-index:5;width:120px;background:rgba(6,17,31,.78);border:1px solid rgba(143,212,255,.35);border-radius:12px;padding:5px;box-shadow:0 4px 12px rgba(0,0,0,.45);}";
  css += ".sensorLeft{left:2%;}.sensorRight{right:2%;}.sensorTop{top:8px;}.sensorMid{top:47px;}.sensorLow{top:90px;}";
  css += "";
  css += "";
  css += ".sensorPoint label{text-align:center;margin:0 0 4px 0;color:#8fd4ff;font-size:13px;text-shadow:0 1px 2px #000;}";
  css += ".sensorPoint select{font-size:13px;padding:7px;background:#06111F;}";
  css += ".miniBarrier{position:absolute;z-index:6;width:42px;height:5px;background:repeating-linear-gradient(90deg,#fff 0 10px,#d71920 10px 18px);border:1px solid #901016;border-radius:5px;box-shadow:0 1px 4px #000;}";
  css += ".miniBarrierL{left:40%;top:48%;transform:rotate(-18deg);}.miniBarrierR{right:40%;top:48%;transform:rotate(198deg);}";
  css += ".trackTitle{text-align:center;font-weight:900;color:#ffffff;margin:8px 0 4px;font-size:14px;}";
  css += ".legendPN{text-align:center;font-size:11px;color:#B9D1DE;margin-top:4px;}";
  css += ".langFrame{display:flex;align-items:center;gap:6px;margin:0 0 10px;padding:8px;border-radius:16px;background:linear-gradient(180deg,rgba(215,235,248,.14),rgba(5,18,35,.76));border:1px solid rgba(160,220,255,.28);box-shadow:inset 0 1px 0 rgba(255,255,255,.16),0 8px 18px rgba(0,0,0,.25);}.langTitle{font-size:12px;font-weight:900;color:#bfeaff;margin-right:auto;text-transform:uppercase;letter-spacing:.5px;}.langBtn{min-width:42px;text-align:center;text-decoration:none;color:white;border-radius:11px;padding:8px 7px;font-size:13px;font-weight:900;border:1px solid rgba(255,255,255,.20);background:linear-gradient(180deg,#446579,#122842);box-shadow:inset 0 1px 0 rgba(255,255,255,.18);}.activeLang{background:linear-gradient(180deg,#ffffff,#a9dfff 45%,#1672af)!important;color:#03111d!important;box-shadow:0 0 16px rgba(100,210,255,.55),inset 0 1px 0 rgba(255,255,255,.9)!important;}";
  css += ".advModal{position:fixed;inset:0;z-index:99;background:rgba(0,0,0,.72);display:flex;align-items:center;justify-content:center;padding:18px;}.advBox{width:min(92vw,520px);border-radius:22px;padding:18px;background:linear-gradient(180deg,rgba(80,0,0,.97),rgba(18,0,0,.98));border:3px solid #ff3030;color:#fff;box-shadow:0 0 34px rgba(255,0,0,.7),inset 0 1px 0 rgba(255,255,255,.18);animation:advPulse 1s infinite;}.advTitle{font-size:21px;font-weight:1000;margin-bottom:10px;text-shadow:0 2px 8px #000;}.advText{font-size:14px;line-height:1.55;color:#ffecec;font-weight:800;}.advBtns{display:grid;grid-template-columns:1fr;gap:8px;margin-top:14px;}@keyframes advPulse{0%,100%{border-color:#ff3030;box-shadow:0 0 18px rgba(255,0,0,.45),inset 0 1px 0 rgba(255,255,255,.18);}50%{border-color:#fff;box-shadow:0 0 38px rgba(255,0,0,.95),0 0 10px rgba(255,255,255,.65),inset 0 1px 0 rgba(255,255,255,.25);}}";
  return css;
}


String langButtonClass(const String &code) {
  return currentLang == code ? "langBtn activeLang" : "langBtn";
}

String languageScript() {
  String js;
  js += "<script>\n";
  js += "const PM3D_LANG='%LANG%';\n";
  js += "const PM3D_TR={\n";
  js += "NL:{\"PM3D Passage a niveau\":\"PM3D Overweg\",\"PM3D Passage à niveau\":\"PM3D Overweg\",\"Utilisation simple voie\":\"Gebruik enkelspoor\",\"Utilisation double voie\":\"Gebruik dubbelspoor\",\"Configuration des capteurs\":\"Configuratie sensoren\",\"Mode automatique par capteurs\":\"Automatische modus via sensoren\",\"Mode manuel\":\"Handmatige modus\",\"Configuration\":\"Configuratie\",\"Luminosite des LED\":\"Helderheid leds\",\"Valeur :\":\"Waarde:\",\"Nom du Wi-Fi\":\"Wi-Fi-naam\",\"Mot de passe Wi-Fi\":\"Wi-Fi-wachtwoord\",\"Enregistrer\":\"Opslaan\",\"Reglages avances\":\"Geavanceerde instellingen\",\"Réglages avancés\":\"Geavanceerde instellingen\",\"Recherche mise à jour firmware\":\"Firmware-update zoeken\",\"Rechercher les reseaux Wi-Fi locaux\":\"Lokale Wi-Fi-netwerken zoeken\",\"Nom du Wi-Fi local\":\"Naam lokaal Wi-Fi\",\"Mot de passe du Wi-Fi local\":\"Wachtwoord lokaal Wi-Fi\",\"Enregistrer et se connecter\":\"Opslaan en verbinden\",\"Effacer le Wi-Fi enregistre\":\"Opgeslagen Wi-Fi wissen\",\"Rechercher les mises a jour disponibles\":\"Beschikbare updates zoeken\",\"Installer cette version\":\"Deze versie installeren\",\"Retour\":\"Terug\",\"Retour aux reglages avances\":\"Terug naar geavanceerde instellingen\",\"Attention - réglages techniques\":\"Opgelet - technische instellingen\",\"Attention - réglages avancés\":\"Opgelet - geavanceerde instellingen\",\"Annuler\":\"Annuleren\",\"Je comprends les risques - Continuer\":\"Ik begrijp de risico’s - Doorgaan\",\"Sensor 1 ou Sensor 2 detecte un train : passage au rouge clignotant.\":\"Sensor 1 of sensor 2 detecteert een trein: rood knipperen wordt actief.\",\"Cette zone permet d'associer les capteurs et les signaux secondaires. Elle est destinée à l'installation initiale ou à une intervention de maintenance.\":\"In deze zone koppelt u sensoren en secundaire seinen. Ze is bedoeld voor eerste installatie of onderhoud.\",\"Etat Wi-Fi local :\":\"Status lokale Wi-Fi:\",\"Mot de passe\":\"Wachtwoord\",\"Se connecter au Wi-Fi enregistre\":\"Verbinden met opgeslagen Wi-Fi\",\"Effacer le Wi-Fi local enregistre\":\"Opgeslagen lokale Wi-Fi wissen\",\"Une fois le Wi-Fi local connecte, ouvrez la liste des editions disponibles puis choisissez le firmware a installer.\":\"Wanneer de lokale Wi-Fi verbonden is, opent u de lijst met beschikbare edities en kiest u de firmware om te installeren.\",\"Option de secours\":\"Noodoptie\",\"Lien direct :\":\"Directe link:\",\"Installer le firmware direct\":\"Directe firmware installeren\",\"Secondaire\":\"Secundair\",\"Sensors\":\"Sensoren\",\"Rechercher\":\"Zoeken\",\"Recherche Wi-Fi local\":\"Lokale Wi-Fi zoeken\",\"Mises a jour firmware\":\"Firmware-updates\",\"Version :\":\"Versie:\",\"Theme :\":\"Thema:\"},\n";
  js += "DE:{\"PM3D Passage a niveau\":\"PM3D Bahnübergang\",\"PM3D Passage à niveau\":\"PM3D Bahnübergang\",\"Utilisation simple voie\":\"Einspuriger Betrieb\",\"Utilisation double voie\":\"Zweispuriger Betrieb\",\"Configuration des capteurs\":\"Sensor-Konfiguration\",\"Mode automatique par capteurs\":\"Automatikmodus über Sensoren\",\"Mode manuel\":\"Manueller Modus\",\"Configuration\":\"Konfiguration\",\"Luminosite des LED\":\"LED-Helligkeit\",\"Valeur :\":\"Wert:\",\"Nom du Wi-Fi\":\"WLAN-Name\",\"Mot de passe Wi-Fi\":\"WLAN-Passwort\",\"Enregistrer\":\"Speichern\",\"Reglages avances\":\"Erweiterte Einstellungen\",\"Réglages avancés\":\"Erweiterte Einstellungen\",\"Recherche mise à jour firmware\":\"Firmware-Update suchen\",\"Rechercher les reseaux Wi-Fi locaux\":\"Lokale WLAN-Netze suchen\",\"Nom du Wi-Fi local\":\"Name des lokalen WLANs\",\"Mot de passe du Wi-Fi local\":\"Passwort des lokalen WLANs\",\"Enregistrer et se connecter\":\"Speichern und verbinden\",\"Effacer le Wi-Fi enregistre\":\"Gespeichertes WLAN löschen\",\"Rechercher les mises a jour disponibles\":\"Verfügbare Updates suchen\",\"Installer cette version\":\"Diese Version installieren\",\"Retour\":\"Zurück\",\"Retour aux reglages avances\":\"Zurück zu erweiterten Einstellungen\",\"Attention - réglages techniques\":\"Achtung - technische Einstellungen\",\"Attention - réglages avancés\":\"Achtung - erweiterte Einstellungen\",\"Annuler\":\"Abbrechen\",\"Je comprends les risques - Continuer\":\"Ich verstehe die Risiken - Weiter\",\"Sensor 1 ou Sensor 2 detecte un train : passage au rouge clignotant.\":\"Sensor 1 oder Sensor 2 erkennt einen Zug: rotes Blinken wird aktiviert.\",\"Cette zone permet d'associer les capteurs et les signaux secondaires. Elle est destinée à l'installation initiale ou à une intervention de maintenance.\":\"In diesem Bereich werden Sensoren und sekundäre Signale gekoppelt. Er ist für Erstinstallation oder Wartung vorgesehen.\",\"Etat Wi-Fi local :\":\"Status lokales WLAN:\",\"Mot de passe\":\"Passwort\",\"Se connecter au Wi-Fi enregistre\":\"Mit gespeichertem WLAN verbinden\",\"Effacer le Wi-Fi local enregistre\":\"Gespeichertes WLAN löschen\",\"Une fois le Wi-Fi local connecte, ouvrez la liste des editions disponibles puis choisissez le firmware a installer.\":\"Sobald das lokale WLAN verbunden ist, öffnen Sie die verfügbaren Editionen und wählen die zu installierende Firmware.\",\"Option de secours\":\"Notfalloption\",\"Lien direct :\":\"Direktlink:\",\"Installer le firmware direct\":\"Direkte Firmware installieren\",\"Secondaire\":\"Sekundär\",\"Sensors\":\"Sensoren\",\"Rechercher\":\"Suchen\",\"Recherche Wi-Fi local\":\"Lokales WLAN suchen\",\"Mises a jour firmware\":\"Firmware-Updates\",\"Version :\":\"Version:\",\"Theme :\":\"Thema:\"},\n";
  js += "EN:{\"PM3D Passage a niveau\":\"PM3D Level Crossing\",\"PM3D Passage à niveau\":\"PM3D Level Crossing\",\"Utilisation simple voie\":\"Single-track use\",\"Utilisation double voie\":\"Double-track use\",\"Configuration des capteurs\":\"Sensor configuration\",\"Mode automatique par capteurs\":\"Automatic sensor mode\",\"Mode manuel\":\"Manual mode\",\"Configuration\":\"Configuration\",\"Luminosite des LED\":\"LED brightness\",\"Valeur :\":\"Value:\",\"Nom du Wi-Fi\":\"Wi-Fi name\",\"Mot de passe Wi-Fi\":\"Wi-Fi password\",\"Enregistrer\":\"Save\",\"Reglages avances\":\"Advanced settings\",\"Réglages avancés\":\"Advanced settings\",\"Recherche mise à jour firmware\":\"Firmware update search\",\"Rechercher les reseaux Wi-Fi locaux\":\"Scan local Wi-Fi networks\",\"Nom du Wi-Fi local\":\"Local Wi-Fi name\",\"Mot de passe du Wi-Fi local\":\"Local Wi-Fi password\",\"Enregistrer et se connecter\":\"Save and connect\",\"Effacer le Wi-Fi enregistre\":\"Clear saved Wi-Fi\",\"Rechercher les mises a jour disponibles\":\"Search available updates\",\"Installer cette version\":\"Install this version\",\"Retour\":\"Back\",\"Retour aux reglages avances\":\"Back to advanced settings\",\"Attention - réglages techniques\":\"Warning - technical settings\",\"Attention - réglages avancés\":\"Warning - advanced settings\",\"Annuler\":\"Cancel\",\"Je comprends les risques - Continuer\":\"I understand the risks - Continue\",\"Sensor 1 ou Sensor 2 detecte un train : passage au rouge clignotant.\":\"Sensor 1 or sensor 2 detects a train: flashing red becomes active.\",\"Cette zone permet d'associer les capteurs et les signaux secondaires. Elle est destinée à l'installation initiale ou à une intervention de maintenance.\":\"This area links sensors and secondary signals. It is intended for initial setup or maintenance.\",\"Etat Wi-Fi local :\":\"Local Wi-Fi status:\",\"Mot de passe\":\"Password\",\"Se connecter au Wi-Fi enregistre\":\"Connect to saved Wi-Fi\",\"Effacer le Wi-Fi local enregistre\":\"Clear saved local Wi-Fi\",\"Une fois le Wi-Fi local connecte, ouvrez la liste des editions disponibles puis choisissez le firmware a installer.\":\"Once local Wi-Fi is connected, open the available editions list and choose the firmware to install.\",\"Option de secours\":\"Fallback option\",\"Lien direct :\":\"Direct link:\",\"Installer le firmware direct\":\"Install direct firmware\",\"Secondaire\":\"Secondary\",\"Sensors\":\"Sensors\",\"Rechercher\":\"Search\",\"Recherche Wi-Fi local\":\"Local Wi-Fi search\",\"Mises a jour firmware\":\"Firmware updates\",\"Version :\":\"Version:\",\"Theme :\":\"Theme:\"}\n";
  js += "};\n";
  js += "function pm3dApplyLang(){if(PM3D_LANG==='FR')return;const d=PM3D_TR[PM3D_LANG]||{};function tr(x){let t=x.trim();return d[t]?x.replace(t,d[t]):x;}function walk(n){if(n.nodeType===3){n.nodeValue=tr(n.nodeValue);return;}if(n&&n.nodeName==='INPUT'&&n.placeholder)n.placeholder=tr(n.placeholder);if(!n||!n.childNodes||['SCRIPT','STYLE','TEXTAREA'].includes(n.nodeName))return;for(const c of Array.from(n.childNodes))walk(c);}walk(document.body);}window.addEventListener('load',pm3dApplyLang);\n";
  js += "</script>\n";
  js.replace("%LANG%", currentLang);
  return js;
}

String langBarHtml() {
  String h;
  String back = server.uri();
  if (back.length() == 0 || back.indexOf("/intro") >= 0) back = "/?home=1";
  String backArg = "&back=" + urlEncode(back);
  h += "<div class='langFrame'><span class='langTitle'>Interface</span>";
  h += "<a class='" + langButtonClass("FR") + "' href='/setlang?lang=FR" + backArg + "'>FR</a>";
  h += "<a class='" + langButtonClass("NL") + "' href='/setlang?lang=NL" + backArg + "'>NL</a>";
  h += "<a class='" + langButtonClass("DE") + "' href='/setlang?lang=DE" + backArg + "'>DE</a>";
  h += "<a class='" + langButtonClass("EN") + "' href='/setlang?lang=EN" + backArg + "'>EN</a>";
  h += "</div>";
  h += languageScript();
  return h;
}

String advancedWarningModalHtml() {
  String h;
  h += "<div id='advModal' class='advModal' style='display:none'><div class='advBox'>";
  h += "<div class='advTitle'>⚠️ " + L("Attention - réglages avancés", "Opgelet - geavanceerde instellingen", "Achtung - erweiterte Einstellungen", "Warning - advanced settings") + "</div>";
  h += "<div class='advText'>" + L("Cette zone est réservée à la configuration technique de l’installation. Une modification incorrecte peut perturber le fonctionnement du passage à niveau, des capteurs ou des signaux associés.", "Deze zone is voor technische configuratie van de installatie. Een verkeerde wijziging kan de werking van de overweg, sensoren of gekoppelde seinen verstoren.", "Dieser Bereich ist für die technische Konfiguration der Anlage vorgesehen. Eine falsche Änderung kann den Betrieb des Bahnübergangs, der Sensoren oder der verbundenen Signale stören.", "This area is reserved for technical configuration of the installation. An incorrect change may disturb the level crossing, sensors or linked signals.") + "</div>";
  h += "<div class='advBtns'><button class='btn danger' type='button' onclick='hideAdvancedWarning()'>" + L("Annuler", "Annuleren", "Abbrechen", "Cancel") + "</button><button class='btn' type='button' onclick=\"location.href='/advanced'\">" + L("Je comprends les risques - Continuer", "Ik begrijp de risico’s - Doorgaan", "Ich verstehe die Risiken - Weiter", "I understand the risks - Continue") + "</button></div>";
  h += "</div></div>";
  return h;
}

String introPage() {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>PM3D Passage a niveau</title>";
  html += "<style>";
  html += "body{margin:0;min-height:100vh;overflow:auto;display:flex;align-items:flex-start;justify-content:center;padding-top:10px;box-sizing:border-box;font-family:Arial,Helvetica,sans-serif;color:white;background:radial-gradient(circle at 50% 15%,rgba(95,190,255,.28),rgba(3,12,28,.96) 42%,#01040A 100%);}";
  html += ".intro{width:min(92vw,520px);text-align:center;padding:14px;border-radius:28px;background:linear-gradient(180deg,rgba(14,42,78,.72),rgba(1,8,18,.84));border:1px solid rgba(180,230,255,.25);box-shadow:0 24px 70px rgba(0,0,0,.55),inset 0 1px 0 rgba(255,255,255,.18);}";
  html += ".logo{width:180px;max-width:70%;animation:spinLogo 6s ease-in-out forwards, glowLogo 1.4s ease-in-out infinite alternate;filter:drop-shadow(0 18px 24px rgba(0,0,0,.55));}";
  html += "@keyframes spinLogo{0%{transform:scale(.72) rotate(-18deg);opacity:0;}18%{opacity:1;}70%{transform:scale(1.03) rotate(360deg);}100%{transform:scale(1) rotate(360deg);opacity:1;}}";
  html += "@keyframes glowLogo{from{filter:drop-shadow(0 12px 20px rgba(80,190,255,.25));}to{filter:drop-shadow(0 18px 30px rgba(190,255,255,.55));}}";
  html += ".title{font-size:25px;font-weight:900;margin:10px 0 5px;text-shadow:0 2px 10px #000;}";
  html += ".phrase{font-size:13px;color:#CDEFFF;line-height:1.35;margin-bottom:12px;}";
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
  html += "if(sessionStorage.getItem('pm3dIntroDone')==='1'){location.replace('/?home=1');}else{sessionStorage.setItem('pm3dIntroDone','1');}let p=0;const msgs=['Initialisation du systeme PM3D','Verification des sensors','Synchronisation des signaux','Chargement de l interface','Pret au depart'];";
  html += "function tick(){p=Math.min(100,Math.round((performance.now()-t0)/6000*100));document.getElementById('pct').innerText=p+'%';document.getElementById('steps').innerText=msgs[Math.min(msgs.length-1,Math.floor(p/25))];if(p<100)requestAnimationFrame(tick);else setTimeout(()=>{location.replace('/?home=1');},350);}";
  html += "let t0;window.onload=()=>{t0=performance.now();tick();};";
  html += "</script></head><body>";
  html += "<div class='intro'>";
  html += "<img class='logo' src='data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAoQAAAJ1CAYAAABAeeHzAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAADsMAAA7DAcdvqGQAAJwrSURBVHhe7d0HnBTl/T/wL8IdHJ2DozcFpEiVjpQIigWNGk3URBPRxNj9xxKNJerPEk0ssXcsWLACRhBUUDgEpUgX0EPqgXQ4yh39P59n5znmlr27LTO7M/t83q9sbmYP7/ZmZ2c+833KVDhsESIiIiIy1jH2VyIiIiIyFAMhERERkeEYCImIiIgMx0BIREREZDgGQiIiIiLDMRASERERGY6BkIiIiMhwDIREREREhmMgJCIiIjIcAyERERGR4RgIiYiIiAzHQEhERERkOAZCIiIiIsMxEBIREREZjoGQiIiIyHAMhERERESGYyAkIiIiMhwDIREREZHhGAiJiIiIDMdASERERGQ4BkIiIiIiwzEQEhERERmOgZCIiIjIcAyERERERIZjICQiIiIyHAMhERERkeEYCImIiIgMx0BIREREZDgGQiIiIiLDMRASERERGY6BkIiIiMhwDIREREREhmMgJCIiIjIcAyERERGR4RgIiYiIiAzHQEhERERkOAZCIiIiIsMxEBIREREZjoGQiIiIyHAMhERERESGYyAkIiIiMhwDIREREZHhGAiJiIiIDMdASERERGQ4BkIiIiIiwzEQEhERERmOgZCIiIjIcAyERERERIZjICQiIiIyHAMhERERkeEYCImIiIgMx0BIREREZDgGQiIiIiLDMRASERERGY6BkIiIiMhwDIREREREhmMgJCIiIjIcAyERERGR4RgIiYiIiAzHQEhERERkOAZCIiIiIsMxEBIREREZjoGQiIiIyHAMhERERESGYyAkIiIiMhwDIREREZHhGAiJiIiIDMdASERERGQ4BkIiIiIiwzEQEhERERmOgZCIiIjIcAyERERERIZjICQiIiIyHAMhERERkeEYCImIiIgMx0BIREREZDgGQiIiIiLDMRASke+tO3DIXiIiIi8wEBKR7xVVOkby7WUiInIfAyER+dp+64H6YKH1WG09DloPIiJyFwMhEfnaXvsr7LMeqBQyFBIRuYuBkIh8rSCs/yBDIRGR+xgIicjXKlY6+jDFUEhE5C4GQiLyNWeTsRNDIRGRexgIici3ig4cUsGvNPje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'>";
  html += "<div class='title'>PM3D Passage a niveau</div>";
  html += "<div class='phrase'>Systeme autonome de signalisation ferroviaire miniature<br>Configuration intelligente simple voie et double voie</div>";
  html += "<div class='pm3dTickerBox'><div class='pm3dTicker'>PM3D.NET &nbsp; • &nbsp; PM3D.NET &nbsp; • &nbsp; PM3D.NET</div></div>";
  html += "<div class='bar'><div class='fill'></div></div>";
  html += "<div id='pct' class='pct'>0%</div>";
  html += "<div id='steps' class='steps'>Initialisation du systeme PM3D</div>";
  html += "</div></body></html>";
  return html;
}

String rootPage() {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>PM3D Passage a niveau</title><style>" + cssCommon();
  html += ".role{border-radius:12px;padding:8px;margin-bottom:8px;font-weight:900;}";
  html += ".primary{background:#12669a}.secondary{background:#5c24a8}";
  html += ".auto{background:#0d6d32}.manual{background:#8a3f00}";
  html += "</style></head><body><div class='wrap'>" + langBarHtml() + "";
  html += "<div class='card'><div class='title'>PM3D Passage a niveau</div>";
  html += "<a class='btn " + String(!configDoubleVoie ? "selectedMode" : "") + "' href='/setvoie?mode=simple'>Utilisation simple voie</a>";
  html += "<a class='btn " + String(configDoubleVoie ? "selectedMode" : "") + "' href='/setvoie?mode=double'>Utilisation double voie</a>";
  html += "<a class='btn' href='/config'>Configuration des capteurs</a>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<div class='section'>" + L("Mode de commande", "Bedieningsmodus", "Steuerungsmodus", "Control mode") + "</div>";
  html += "<div class='hint'>" + L("Choisissez le fonctionnement manuel ou le fonctionnement automatique par capteurs.", "Kies handmatige bediening of automatische werking via sensoren.", "Wählen Sie manuellen Betrieb oder Automatikbetrieb über Sensoren.", "Choose manual operation or automatic sensor operation.") + "</div>";
  html += "<a class='btn " + String(!modeAutomatique ? "selectedMode" : "") + "' href='/setcontrol?mode=manual'>" + L("Mode manuel", "Handmatige modus", "Manueller Modus", "Manual mode") + "</a>";
  html += "<a class='btn " + String(modeAutomatique ? "selectedMode" : "") + "' href='/setcontrol?mode=sensor'>" + L("Mode par capteur", "Modus via sensor", "Sensormodus", "Sensor mode") + "</a>";
  html += "</div>";

  html += "<div class='card'>" + ledSvg() + "</div>";
  html += "</div></body></html>";
  return html;
}

String configPage() {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Configuration</title><style>" + cssCommon() + "</style>";
  html += "<script>";
  html += "function showAdvancedWarning(){document.getElementById('advModal').style.display='flex';return false;}function hideAdvancedWarning(){document.getElementById('advModal').style.display='none';}";
  html += "function setLum(v){document.getElementById('lumVal').innerText=v+'%';fetch('/setlum?lum='+v);}";
  html += "function upd(){fetch('/sensorstatus').then(r=>r.json()).then(d=>{for(let i=0;i<4;i++){let e=document.getElementById('sd'+i);if(e)e.className='dot '+(d['s'+i]?'green':'red');}});}setInterval(upd,400);window.addEventListener('load',upd);";
  html += "</script></head><body><div class='wrap'>" + langBarHtml() + "<a class='back' href='/'>←</a>";
  html += "<div class='card'><div class='title'>" + L("Configuration", "Configuratie", "Konfiguration", "Configuration") + "</div>";

  html += "<div class='sub'><div class='section'>" + L("Luminosite des LED", "Helderheid leds", "LED-Helligkeit", "LED brightness") + "</div>";
  html += "<input type='range' min='0' max='100' value='" + String(luminositeLed) + "' oninput='setLum(this.value)'>";
  html += "<div class='hint'>" + L("Valeur :", "Waarde:", "Wert:", "Value:") + " <span id='lumVal'>" + String(luminositeLed) + "%</span></div></div>";

  html += "<div class='sub'><div class='section'>" + L("Reseau Wi-Fi de l'appareil", "Wi-Fi-netwerk van het toestel", "WLAN-Netz des Geräts", "Device Wi-Fi network") + "</div>";
  html += "<form action='/savewifi'>";
  html += "<div class='hint'>" + L("Nom Wi-Fi visible par le client. Le mot de passe est optionnel. S'il est utilise, il doit contenir au moins 8 caracteres.", "Wi-Fi-naam zichtbaar voor de klant. Het wachtwoord is optioneel. Als het wordt gebruikt, moet het minstens 8 tekens bevatten.", "Für den Kunden sichtbarer WLAN-Name. Das Passwort ist optional. Wenn es verwendet wird, muss es mindestens 8 Zeichen enthalten.", "Wi-Fi name visible to the customer. The password is optional. If used, it must contain at least 8 characters.") + "</div>";
  html += "<label>" + L("Nom du Wi-Fi", "Wi-Fi-naam", "WLAN-Name", "Wi-Fi name") + "</label><input name='ssid' maxlength='31' value='" + htmlEscape(apSsid) + "'>";
  html += "<label>" + L("Mot de passe Wi-Fi", "Wi-Fi-wachtwoord", "WLAN-Passwort", "Wi-Fi password") + "</label><input name='pass' type='password' maxlength='63' value='" + htmlEscape(wifiApPassword) + "' placeholder='" + L("vide = acces sans mot de passe", "leeg = toegang zonder wachtwoord", "leer = Zugang ohne Passwort", "empty = access without password") + "'>";
  html += "<button class='btn' type='submit'>" + L("Enregistrer Wi-Fi", "Wi-Fi opslaan", "WLAN speichern", "Save Wi-Fi") + "</button>";
  html += "</form></div>";

  html += "<div class='sub'><div class='section'>" + L("Retard signal secondaire", "Vertraging secundair sein", "Verzögerung sekundäres Signal", "Secondary signal delay") + "</div>";
  html += "<form action='/savesensorconfig'><div class='hint'>" + L("Permet d'ajouter un petit decalage d'execution pour aligner visuellement un signal secondaire avec le signal principal. Laissez 0 si tout est deja synchronise.", "Voegt een kleine uitvoeringsvertraging toe om een secundair sein visueel met het hoofdsein uit te lijnen. Laat 0 staan als alles al synchroon is.", "Fügt eine kleine Ausführungsverzögerung hinzu, um ein sekundäres Signal optisch mit dem Hauptsignal abzugleichen. Lassen Sie 0, wenn bereits alles synchron ist.", "Adds a small execution delay to visually align a secondary signal with the main signal. Leave 0 if everything is already synchronized.") + "</div><div class='grid'><label>" + L("Retard (ms)", "Vertraging (ms)", "Verzögerung (ms)", "Delay (ms)") + "</label><input name='latms' type='number' min='0' max='5000' value='" + String(latenceExecutionMs) + "'></div>";

  html += "<div class='section'>" + L("Test reception sensors", "Ontvangsttest sensoren", "Empfangstest Sensoren", "Sensor reception test") + "</div>";
  html += "<div class='hint'>" + L("Ces voyants permettent de verifier en direct si chaque sensor detecte correctement un train ou votre main. Rouge = rien detecte, vert = detection active.", "Deze lampjes tonen live of elke sensor correct een trein of uw hand detecteert. Rood = niets gedetecteerd, groen = actieve detectie.", "Diese Anzeigen prüfen live, ob jeder Sensor einen Zug oder Ihre Hand korrekt erkennt. Rot = nichts erkannt, grün = aktive Erkennung.", "These indicators show live whether each sensor correctly detects a train or your hand. Red = nothing detected, green = active detection.") + "</div>";
  html += "<div style='display:grid;grid-template-columns:repeat(" + String(configDoubleVoie ? 4 : 2) + ",1fr);gap:8px;text-align:center'>";
  for (int i=0;i<(configDoubleVoie ? 4 : 2);i++) {
    html += "<div class='sensorCard'><div>S" + String(i+1) + "</div><span id='sd" + String(i) + "' class='dot red'></span></div>";
  }
  html += "</div>";

  html += "<div class='section'>" + L("Position des sensors", "Positie van de sensoren", "Position der Sensoren", "Sensor position") + "</div>";
  html += "<div class='sensorPlan'>";
  html += "<div class='hint'>" + L("Choisissez quel sensor installe correspond a chaque position autour du passage a niveau.", "Kies welke geïnstalleerde sensor overeenkomt met elke positie rond de overweg.", "Wählen Sie, welcher installierte Sensor welcher Position am Bahnübergang entspricht.", "Choose which installed sensor matches each position around the level crossing.") + "</div>";

  if (!configDoubleVoie) {
    html += "<div class='trackTitle'>" + L("Simple voie", "Enkelspoor", "Einspurig", "Single track") + "</div>";
    html += "<div class='pnScene simpleMode'>";
    html += "<div class='pnPlate'></div><div class='roadPN'></div>";
    html += "<div class='railTrack trackA'><div class='sleepers'></div></div>";
    html += "<div class='miniBarrier miniBarrierL'></div><div class='miniBarrier miniBarrierR'></div>";
    html += "<div class='sensorPoint sensorLeft sensorMid'><label>" + L("Capteur 1", "Sensor 1", "Sensor 1", "Sensor 1") + "</label><select name='ord0'>";
    for (int phys = 0; phys < 2; phys++) {
      html += "<option value=\'" + String(phys) + "\'" + String(ordreSensorClient[0] == phys ? " selected" : "") + ">S" + String(phys + 1) + "</option>";
    }
    html += "</select></div>";
    html += "<div class='sensorPoint sensorRight sensorMid'><label>" + L("Capteur 2", "Sensor 2", "Sensor 2", "Sensor 2") + "</label><select name='ord1'>";
    for (int phys = 0; phys < 2; phys++) {
      html += "<option value=\'" + String(phys) + "\'" + String(ordreSensorClient[1] == phys ? " selected" : "") + ">S" + String(phys + 1) + "</option>";
    }
    html += "</select></div>";
    html += "</div>";
    html += "<div class='legendPN'>" + L("Le passage a niveau est au centre. S1 et S2 forment le canton simple voie.", "De overweg staat in het midden. S1 en S2 vormen het enkelspoorblok.", "Der Bahnübergang befindet sich in der Mitte. S1 und S2 bilden den einspurigen Block.", "The level crossing is in the center. S1 and S2 form the single-track block.") + "</div>";
  } else {
    html += "<div class='trackTitle'>" + L("Double voie", "Dubbelspoor", "Zweispurig", "Double track") + "</div>";
    html += "<div class='pnScene doubleMode'>";
    html += "<div class='pnPlate'></div><div class='roadPN'></div>";
    html += "<div class='railTrack trackA'><div class='sleepers'></div></div>";
    html += "<div class='railTrack trackB'><div class='sleepers'></div></div>";
    html += "<div class='miniBarrier miniBarrierL'></div><div class='miniBarrier miniBarrierR'></div>";

    html += "<div class='sensorPoint sensorLeft sensorTop'><label>" + L("Capteur 1", "Sensor 1", "Sensor 1", "Sensor 1") + "</label><select name='ord0'>";
    for (int phys = 0; phys < 4; phys++) {
      html += "<option value=\'" + String(phys) + "\'" + String(ordreSensorClient[0] == phys ? " selected" : "") + ">S" + String(phys + 1) + "</option>";
    }
    html += "</select></div>";

    html += "<div class='sensorPoint sensorRight sensorTop'><label>" + L("Capteur 2", "Sensor 2", "Sensor 2", "Sensor 2") + "</label><select name='ord1'>";
    for (int phys = 0; phys < 4; phys++) {
      html += "<option value=\'" + String(phys) + "\'" + String(ordreSensorClient[1] == phys ? " selected" : "") + ">S" + String(phys + 1) + "</option>";
    }
    html += "</select></div>";

    html += "<div class='sensorPoint sensorLeft sensorLow'><label>" + L("Capteur 3", "Sensor 3", "Sensor 3", "Sensor 3") + "</label><select name='ord2'>";
    for (int phys = 0; phys < 4; phys++) {
      html += "<option value=\'" + String(phys) + "\'" + String(ordreSensorClient[2] == phys ? " selected" : "") + ">S" + String(phys + 1) + "</option>";
    }
    html += "</select></div>";

    html += "<div class='sensorPoint sensorRight sensorLow'><label>" + L("Capteur 4", "Sensor 4", "Sensor 4", "Sensor 4") + "</label><select name='ord3'>";
    for (int phys = 0; phys < 4; phys++) {
      html += "<option value=\'" + String(phys) + "\'" + String(ordreSensorClient[3] == phys ? " selected" : "") + ">S" + String(phys + 1) + "</option>";
    }
    html += "</select></div>";

    html += "</div>";
    html += "<div class='legendPN'>" + L("Canton haut : S1/S2. Canton bas : S3/S4. Retour jaune uniquement quand les deux cantons sont refermes.", "Bovenste blok: S1/S2. Onderste blok: S3/S4. Terug naar geel alleen wanneer beide blokken gesloten zijn.", "Oberer Block: S1/S2. Unterer Block: S3/S4. Rückkehr zu Gelb nur, wenn beide Blöcke geschlossen sind.", "Upper block: S1/S2. Lower block: S3/S4. Return to yellow only when both blocks are closed.") + "</div>";
  }

  html += "</div>";

  html += "<div class='section'>" + L("Activite des sensors", "Activiteit van de sensoren", "Sensoraktivität", "Sensor activity") + "</div>";
  html += "<div class='sensorCard'>";
  html += "<div class='hint'><b>" + L("Fonctionnement automatique du passage a niveau", "Automatische werking van de overweg", "Automatischer Betrieb des Bahnübergangs", "Automatic level crossing operation") + "</b></div>";
  html += "<div class='hint'><b>" + L("Utilisation simple voie :", "Gebruik enkelspoor:", "Einspuriger Betrieb:", "Single-track use:") + "</b><br>";
  html += "- " + L("Sensor 1 ou Sensor 2 detecte un train : passage au rouge clignotant.", "Sensor 1 of sensor 2 detecteert een trein: rood knipperen wordt actief.", "Sensor 1 oder Sensor 2 erkennt einen Zug: rotes Blinken wird aktiviert.", "Sensor 1 or sensor 2 detects a train: flashing red becomes active.") + "<br>";
  html += "- " + L("L'autre sensor devient le sensor de sortie.", "De andere sensor wordt de uitgangssensor.", "Der andere Sensor wird zum Ausgangssensor.", "The other sensor becomes the exit sensor.") + "<br>";
  html += "- " + L("Quand ce sensor de sortie a detecte le train puis ne detecte plus rien : retour au jaune clignotant.", "Wanneer deze uitgangssensor de trein heeft gedetecteerd en daarna niets meer detecteert: terug naar knipperend geel.", "Wenn dieser Ausgangssensor den Zug erkannt hat und danach nichts mehr erkennt: Rückkehr zu blinkendem Gelb.", "When this exit sensor has detected the train and then detects nothing anymore: return to flashing yellow.") + "</div>";
  html += "<div class='hint'><b>" + L("Utilisation double voie :", "Gebruik dubbelspoor:", "Zweispuriger Betrieb:", "Double-track use:") + "</b><br>";
  html += "- " + L("S1 et S2 forment un premier canton.", "S1 en S2 vormen een eerste blok.", "S1 und S2 bilden einen ersten Block.", "S1 and S2 form a first block.") + "<br>";
  html += "- " + L("S3 et S4 forment un second canton parallele.", "S3 en S4 vormen een tweede parallel blok.", "S3 und S4 bilden einen zweiten parallelen Block.", "S3 and S4 form a second parallel block.") + "<br>";
  html += "- " + L("Un canton ouvert par un sensor reste actif jusqu'a detection puis liberation du sensor oppose.", "Een blok dat door een sensor wordt geopend, blijft actief tot detectie en daarna vrijgave door de tegenoverliggende sensor.", "Ein durch einen Sensor geöffneter Block bleibt aktiv bis zur Erkennung und anschließenden Freigabe durch den gegenüberliegenden Sensor.", "A block opened by a sensor remains active until detection and then release by the opposite sensor.") + "<br>";
  html += "- " + L("Le retour au jaune clignotant se fait uniquement quand les deux cantons sont refermes.", "Terug naar knipperend geel gebeurt alleen wanneer beide blokken gesloten zijn.", "Die Rückkehr zu blinkendem Gelb erfolgt nur, wenn beide Blöcke geschlossen sind.", "Return to flashing yellow only happens when both blocks are closed.") + "</div>";
  html += "<div class='hint'><b>" + L("Important :", "Belangrijk:", "Wichtig:", "Important:") + "</b><br>";
  html += L("Les conditions manuelles des sensors ont ete supprimees. Le fonctionnement est fixe, automatique et securise. L'ordre S1/S2/S3/S4 peut etre modifie ci-dessus sans toucher aux adresses MAC.", "De handmatige voorwaarden van de sensoren zijn verwijderd. De werking is vast, automatisch en veilig. De volgorde S1/S2/S3/S4 kan hierboven worden gewijzigd zonder de MAC-adressen aan te raken.", "Die manuellen Bedingungen der Sensoren wurden entfernt. Der Betrieb ist fest, automatisch und sicher. Die Reihenfolge S1/S2/S3/S4 kann oben geändert werden, ohne die MAC-Adressen zu ändern.", "Manual sensor conditions have been removed. Operation is fixed, automatic and safe. The S1/S2/S3/S4 order can be changed above without touching MAC addresses.") + "</div>";
  html += "</div><button class='btn' type='submit'>" + L("Enregistrer", "Opslaan", "Speichern", "Save") + "</button></form></div>";

  html += advancedWarningModalHtml();
  html += "<a class='btn danger' href='/advanced' onclick='return showAdvancedWarning()'>" + L("Reglages avances", "Geavanceerde instellingen", "Erweiterte Einstellungen", "Advanced settings") + "</a>";
  html += "</div></div></body></html>";
  return html;
}

String advancedPage() {
  int maxSecondairesAdvanced = 3;
  int maxSensorsAdvanced = configDoubleVoie ? 4 : 2;
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Avance</title><style>" + cssCommon() + ".warn{background:linear-gradient(180deg,rgba(110,0,0,.92),rgba(45,0,0,.94));border:3px solid #ff1d1d;border-radius:14px;padding:12px;font-weight:900;color:#fff;line-height:1.45;box-shadow:0 0 18px rgba(255,0,0,.65),inset 0 1px 0 rgba(255,255,255,.18);animation:warnPulse 1.05s infinite;}@keyframes warnPulse{0%,100%{border-color:#ff1d1d;box-shadow:0 0 10px rgba(255,0,0,.45),inset 0 1px 0 rgba(255,255,255,.18);}50%{border-color:#ffffff;box-shadow:0 0 26px rgba(255,0,0,.95),0 0 8px rgba(255,255,255,.75),inset 0 1px 0 rgba(255,255,255,.25);}}.debugBox{border:1px solid rgba(143,212,255,.28);border-radius:12px;padding:10px;background:rgba(0,0,0,.22);line-height:1.7;font-size:13px;} .fwBox{margin-top:12px;background:linear-gradient(135deg,rgba(15,39,62,.95),rgba(44,50,58,.92));border:1px solid rgba(143,212,255,.35);border-radius:14px;padding:12px;box-shadow:inset 0 1px 0 rgba(255,255,255,.12),0 0 16px rgba(49,154,255,.18);} .fwUrl{font-size:11px;line-height:1.35;word-break:break-all;color:#cfefff;background:rgba(0,0,0,.28);border:1px solid rgba(255,255,255,.12);border-radius:10px;padding:8px;margin:8px 0;} .fwStatus{font-size:12px;color:#d7f3ff;line-height:1.45;margin-top:8px;} .otaState{padding:9px;border-radius:12px;background:rgba(0,0,0,.25);border:1px solid rgba(255,255,255,.14);font-size:13px;line-height:1.45;margin:8px 0;} .miniBtn{font-size:13px;padding:9px 10px;} .ledLine{display:grid;grid-template-columns:1fr 92px 44px;gap:8px;align-items:center;margin-bottom:8px;} .ledTestDot{width:28px;height:28px;border-radius:50%;border:2px solid rgba(255,255,255,.6);background:#303844;color:white;font-weight:900;box-shadow:inset 0 1px 0 rgba(255,255,255,.25);cursor:pointer;} .ledTestDot.on{background:#ff2525;box-shadow:0 0 12px rgba(255,37,37,.8),inset 0 1px 0 rgba(255,255,255,.25);} .ledValidate{margin-top:10px;}</style><script>function updDbg(){fetch('/debugsensor').then(r=>r.json()).then(d=>{document.getElementById('dbgTexte').innerText=d.texte;document.getElementById('dbgAge').innerText=d.age;document.getElementById('dbgMac').innerText=d.mac;document.getElementById('dbgSuffix').innerText=d.suffix;document.getElementById('dbgEvent').innerText=d.event;document.getElementById('dbgResultat').innerText=d.resultat;document.getElementById('dbgAction').innerText=d.action;});}setInterval(updDbg,500);window.addEventListener('load',updDbg);function ledTest(id,inputId,btn){var p=document.getElementById(inputId).value;fetch('/ledtest?id='+id+'&pin='+encodeURIComponent(p)).then(r=>r.text()).then(t=>{if(t==='ON'){btn.classList.add('on');btn.innerText='●';}else{btn.classList.remove('on');btn.innerText='○';}});}</script></head>";
  html += "<body><div class='wrap'>" + langBarHtml() + "<a class='back' href='/config'>←</a><div class='card'><div class='title'>" + L("Reglages avances", "Geavanceerde instellingen", "Erweiterte Einstellungen", "Advanced settings") + "</div><div class='warn'><b>" + L("Attention - réglages techniques", "Opgelet - technische instellingen", "Achtung - technische Einstellungen", "Warning - technical settings") + "</b><br>" + L("Cette zone permet d'associer les capteurs et les signaux secondaires. Elle est destinée à l'installation initiale ou à une intervention de maintenance.", "In deze zone koppelt u sensoren en secundaire seinen. Ze is bedoeld voor eerste installatie of onderhoud.", "In diesem Bereich werden Sensoren und sekundäre Signale gekoppelt. Er ist für Erstinstallation oder Wartung vorgesehen.", "This area links sensors and secondary signals. It is intended for initial setup or maintenance.") + "</div>";

  html += "<div class='fwBox'><div class='section'>" + L("Recherche mise à jour firmware", "Firmware-update zoeken", "Firmware-Update suchen", "Firmware update search") + "</div>";
  html += "<div class='hint'>" + L("La mise a jour depuis GitHub necessite une connexion temporaire a votre Wi-Fi local. Le Wi-Fi PM3D reste actif pour revenir a l'interface.", "De update via GitHub vereist tijdelijk verbinding met uw lokale Wi-Fi. De PM3D-Wi-Fi blijft actief om terug te keren naar de interface.", "Das Update über GitHub benötigt vorübergehend eine Verbindung mit Ihrem lokalen WLAN. Das PM3D-WLAN bleibt aktiv, um zur Oberfläche zurückzukehren.", "The GitHub update temporarily requires a connection to your local Wi-Fi. The PM3D Wi-Fi remains active so you can return to the interface.") + "</div>";
  html += "<div class='fwUrl'><b>" + L("Catalogue des mises a jour :", "Updatecatalogus:", "Update-Katalog:", "Update catalogue:") + "</b><br>" + String(FW_MANIFEST_URL) + "</div>";
  html += "<div class='otaState'><b>" + L("Etat Wi-Fi local :", "Status lokale Wi-Fi:", "Status lokales WLAN:", "Local Wi-Fi status:") + "</b><br>" + htmlEscape(otaWifiStatusText()) + "</div>";
  html += "<a class='btn miniBtn' href='/otascan'>" + L("Rechercher les reseaux Wi-Fi locaux", "Lokale Wi-Fi-netwerken zoeken", "Lokale WLAN-Netze suchen", "Scan local Wi-Fi networks") + "</a>";
  html += "<form action='/otasavewifi' method='get'>";
  html += "<label>" + L("Nom du Wi-Fi local", "Naam lokaal Wi-Fi", "Name des lokalen WLANs", "Local Wi-Fi name") + "</label><input name='ssid' maxlength='32' value='" + htmlEscape(otaStaSsid) + "' placeholder='" + L("Nom de votre box", "Naam van uw router", "Name Ihres Routers", "Your router name") + "'>";
  html += "<label>" + L("Mot de passe", "Wachtwoord", "Passwort", "Password") + "</label><input name='pass' type='password' maxlength='63' value='" + htmlEscape(otaStaPassword) + "' placeholder='" + L("mot de passe Wi-Fi", "Wi-Fi-wachtwoord", "WLAN-Passwort", "Wi-Fi password") + "'>";
  html += "<button class='btn miniBtn' type='submit'>" + L("Enregistrer et se connecter", "Opslaan en verbinden", "Speichern und verbinden", "Save and connect") + "</button></form>";
  html += "<a class='btn miniBtn' href='/otaconnect'>" + L("Se connecter au Wi-Fi enregistre", "Verbinden met opgeslagen Wi-Fi", "Mit gespeichertem WLAN verbinden", "Connect to saved Wi-Fi") + "</a>";
  html += "<a class='btn danger miniBtn' href='/otaclearwifi'>" + L("Effacer le Wi-Fi local enregistre", "Opgeslagen lokale Wi-Fi wissen", "Gespeichertes WLAN löschen", "Clear saved local Wi-Fi") + "</a>";
  html += "<div class='fwStatus'>" + L("Une fois le Wi-Fi local connecte, ouvrez la liste des editions disponibles puis choisissez le firmware a installer.", "Wanneer de lokale Wi-Fi verbonden is, opent u de lijst met beschikbare edities en kiest u de firmware om te installeren.", "Sobald das lokale WLAN verbunden ist, öffnen Sie die verfügbaren Editionen und wählen die zu installierende Firmware.", "Once local Wi-Fi is connected, open the available editions list and choose the firmware to install.") + "</div>";
  html += "<a class='btn miniBtn' href='/otamanifest'>" + L("Rechercher les mises a jour disponibles", "Beschikbare updates zoeken", "Verfügbare Updates suchen", "Search available updates") + "</a>";
  html += "<details style='margin-top:10px;'><summary style='cursor:pointer;color:#cfefff;font-weight:800;'>" + L("Option de secours", "Noodoptie", "Notfalloption", "Fallback option") + "</summary>";
  html += "<div class='fwUrl'><b>" + L("Lien direct :", "Directe link:", "Direktlink:", "Direct link:") + "</b><br>" + String(FW_BIN_URL) + "</div>";
  html += "<a class='btn miniBtn' href='/otaupdate'>" + L("Installer le firmware direct", "Directe firmware installeren", "Direkte Firmware installieren", "Install direct firmware") + "</a></details>";
  html += "</div>";

  html += "<form action='/saveadvanced'>";
  html += "<div class='sub'><div class='section'>" + L("Réglage GPIO LEDs", "GPIO-ledinstellingen", "GPIO-LED-Einstellungen", "GPIO LED settings") + "</div>";
  html += "<div class='hint'>" + L("Le rond permet de tester la LED pendant le réglage. Le test est indépendant du programme et sera coupé à la validation.", "Met het bolletje test u de led tijdens het instellen. De test staat los van het programma en wordt uitgeschakeld bij bevestigen.", "Mit dem Kreis testen Sie die LED während der Einstellung. Der Test ist unabhängig vom Programm und wird beim Bestätigen ausgeschaltet.", "The round button tests the LED while setting it. The test is independent from the program and is disabled when validated.") + "</div>";
  html += "<div class='ledLine'><label>" + L("Rouge gauche", "Rood links", "Rot links", "Left red") + "</label><input id='gpioRg' name='rg' type='number' value='" + String(gpioRougeGauche) + "'><button type='button' class='ledTestDot' onclick=\"ledTest(0,'gpioRg',this)\">○</button></div>";
  html += "<div class='ledLine'><label>" + L("Rouge droite", "Rood rechts", "Rot rechts", "Right red") + "</label><input id='gpioRd' name='rd' type='number' value='" + String(gpioRougeDroite) + "'><button type='button' class='ledTestDot' onclick=\"ledTest(1,'gpioRd',this)\">○</button></div>";
  html += "<div class='ledLine'><label>" + L("Jaune PN", "Geel overweg", "Gelb Bahnübergang", "Crossing yellow") + "</label><input id='gpioJa' name='ja' type='number' value='" + String(gpioJaunePN) + "'><button type='button' class='ledTestDot' onclick=\"ledTest(2,'gpioJa',this)\">○</button></div>";
  html += "<button class='btn ledValidate' type='submit'>" + L("Valider les GPIO LED", "GPIO-leds bevestigen", "GPIO-LEDs bestätigen", "Validate LED GPIOs") + "</button></div>";

  html += "<div class='sub'><div class='section'>" + L("Signaux secondaires", "Secundaire seinen", "Sekundäre Signale", "Secondary signals") + "</div>";
  for (int i=0;i<maxSecondairesAdvanced;i++) {
    html += "<label>" + L("Secondaire", "Secundair", "Sekundär", "Secondary") + " " + String(i+1) + " " + badge(recent(dernierOkSecondaireMs[i], 8000UL)) + "</label>";
    html += "<div class='grid'><input name='sec" + String(i) + "' value='" + htmlEscape(macSecondaire[i]) + "' placeholder='AA:BB:CC:DD:EE:FF'>";
    html += "<a class='btn' href='/search?target=sec" + String(i) + "'>" + L("Rechercher", "Zoeken", "Suchen", "Search") + "</a></div>";
  }
  html += "</div>";

  html += "<div class='sub'><div class='section'>" + L("Sensors", "Sensoren", "Sensoren", "Sensors") + "</div>";
  for (int i=0;i<maxSensorsAdvanced;i++) {
    html += "<label>" + L("Sensor", "Sensor", "Sensor", "Sensor") + " " + String(i+1) + " " + badge(recent(dernierVuSensorMs[i], 8000UL)) + "</label>";
    html += "<div class='grid'><input name='sen" + String(i) + "' value='" + htmlEscape(macSensor[i]) + "' placeholder='AA:BB:CC:DD:EE:FF'>";
    html += "<a class='btn' href='/search?target=sen" + String(i) + "'>" + L("Rechercher", "Zoeken", "Suchen", "Search") + "</a></div>";
  }
  html += "</div>";

  html += "<div class='sub'><div class='section'>" + L("Clignotement rouge", "Rood knipperen", "Rotes Blinken", "Red flashing") + "</div><div class='grid'>";
  html += "<label>" + L("Rouge ON ms", "Rood AAN ms", "Rot EIN ms", "Red ON ms") + "</label><input name='ron' value='" + String(tempsRougeAllumageMs) + "'>";
  html += "<label>" + L("Rouge OFF ms", "Rood UIT ms", "Rot AUS ms", "Red OFF ms") + "</label><input name='roff' value='" + String(tempsRougeExtinctionMs) + "'>";
  html += "<label>" + L("Rouge pause ms", "Rood pauze ms", "Rot Pause ms", "Red pause ms") + "</label><input name='rpaus' value='" + String(tempsRougePauseMs) + "'>";
  html += "</div><label><input type='checkbox' name='altR' value='1'" + checked(alternanceRouges) + "> " + L("Alternance rouges", "Rood afwisselen", "Rote Wechselblinken", "Alternating reds") + "</label></div>";

  html += "<div class='sub'><div class='section'>" + L("Clignotement jaune", "Geel knipperen", "Gelbes Blinken", "Yellow flashing") + "</div><div class='grid'>";
  html += "<label>" + L("Jaune ON ms", "Geel AAN ms", "Gelb EIN ms", "Yellow ON ms") + "</label><input name='jon' value='" + String(tempsJauneAllumageMs) + "'>";
  html += "<label>" + L("Jaune OFF ms", "Geel UIT ms", "Gelb AUS ms", "Yellow OFF ms") + "</label><input name='joff' value='" + String(tempsJauneExtinctionMs) + "'>";
  html += "<label>" + L("Jaune pause ms", "Geel pauze ms", "Gelb Pause ms", "Yellow pause ms") + "</label><input name='jpaus' value='" + String(tempsJaunePauseMs) + "'>";
  html += "</div></div>";

  html += "<div class='sub'><div class='section'>" + L("Debug réception sensors", "Debug ontvangst sensoren", "Debug Sensor-Empfang", "Sensor reception debug") + "</div>";
  html += "<div class='hint'>" + L("Ce panneau montre en temps réel si le signal principal reçoit quelque chose des sensors.", "Dit paneel toont live of het hoofdsein iets van de sensoren ontvangt.", "Dieses Feld zeigt in Echtzeit, ob das Hauptsignal etwas von den Sensoren empfängt.", "This panel shows in real time whether the main signal receives anything from the sensors.") + "</div>";
  html += "<div class='debugBox'>";
  html += "<div>" + L("Réception :", "Ontvangst:", "Empfang:", "Reception:") + " <span id='dbgTexte'>-</span></div>";
  html += "<div>" + L("Âge :", "Leeftijd:", "Alter:", "Age:") + " <span id='dbgAge'>-</span> ms</div>";
  html += "<div>" + L("MAC reçue :", "Ontvangen MAC:", "Empfangene MAC:", "Received MAC:") + " <span id='dbgMac'>-</span></div>";
  html += "<div><b>Suffix :</b> <span id='dbgSuffix'>-</span></div>";
  html += "<div>" + L("Événement :", "Gebeurtenis:", "Ereignis:", "Event:") + " <span id='dbgEvent'>-</span></div>";
  html += "<div>" + L("Interprétation :", "Interpretatie:", "Interpretation:", "Interpretation:") + " <span id='dbgResultat'>-</span></div>";
  html += "<div>" + L("Action :", "Actie:", "Aktion:", "Action:") + " <span id='dbgAction'>-</span></div>";
  html += "</div></div>";
  html += "<button class='btn' type='submit'>" + L("Enregistrer", "Opslaan", "Speichern", "Save") + "</button></form>";
  html += "</div></div></body></html>";
  return html;
}

String searchPage(const String &target) {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>" + L("Recherche PM3D", "PM3D zoeken", "PM3D-Suche", "PM3D search") + "</title><style>" + cssCommon() + "</style></head><body><div class='wrap'>" + langBarHtml() + "<a class='back' href='/advanced'>←</a><div class='card'><div class='title'>" + L("Recherche PM3D", "PM3D zoeken", "PM3D-Suche", "PM3D search") + "</div>";

  int count = WiFi.scanNetworks(false, true);
  bool found = false;

  for (int i=0;i<count;i++) {
    String ssid = WiFi.SSID(i);
    bool ok = false;
    if (target.startsWith("sec") && ssid.startsWith("PM3D-signal")) ok = true;
    if (target.startsWith("sen") && ssid.startsWith("PM3D-Sensor")) ok = true;

    if (ok) {
      found = true;
      String mac = WiFi.BSSIDstr(i);
      html += "<div class='sub'><b>" + htmlEscape(ssid) + "</b><div class='hint'>MAC " + mac + " / RSSI " + String(WiFi.RSSI(i)) + "</div>";
      html += "<a class='btn' href='/add?target=" + target + "&mac=" + mac + "&ssid=" + ssid + "'>" + L("Ajouter", "Toevoegen", "Hinzufügen", "Add") + "</a></div>";
    }
  }

  if (!found) html += "<div class='hint'>" + L("Aucun accessoire PM3D détecté.", "Geen PM3D-accessoire gevonden.", "Kein PM3D-Zubehör erkannt.", "No PM3D accessory detected.") + "</div>";
  WiFi.scanDelete();

  html += "</div></div></body></html>";
  return html;
}

// =======================================================
// HTTP Handlers
// =======================================================

String argText(const String &n) {
  if (!server.hasArg(n)) return "";
  String v = server.arg(n);
  v.trim();
  return v;
}

void addNoCacheHeaders() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate, max-age=0");
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
}

void sendCaptiveRedirect() {
  addNoCacheHeaders();
  server.sendHeader("Location", "http://192.168.4.1/intro", true);
  server.send(302, "text/plain", "");
}

void handleIntro() {
  addNoCacheHeaders();
  server.send(200, "text/html", introPage());
}

void handleCaptivePortal() {
  // Important : on renvoie une vraie page HTML PM3D pour forcer la detection du portail captif.
  handleIntro();
}

void handleCaptivePortalPage() {
  handleIntro();
}

void handleRoot() {
  addNoCacheHeaders();
  if (server.hasArg("home") || server.hasArg("nointro")) server.send(200, "text/html", rootPage());
  else server.send(200, "text/html", introPage());
}
void handleConfig() { server.send(200, "text/html", configPage()); }
void handleAdvanced() { server.send(200, "text/html", advancedPage()); }


void handleSetLang() {
  String l = argText("lang");
  l.toUpperCase();
  if (l != "FR" && l != "NL" && l != "DE" && l != "EN") l = "FR";
  currentLang = l;
  saveConfig();
  String back = server.hasArg("back") ? argText("back") : server.header("Referer");
  if (back.length() == 0 || back.indexOf("/intro") >= 0 || back.indexOf("/setlang") >= 0) back = "/?home=1";
  server.sendHeader("Location", back, true);
  server.send(302, "text/plain", "");
}

void handleSaveWifi() {
  wifiApName = argText("ssid");
  wifiApPassword = argText("pass");

  if (wifiApName.length() == 0) wifiApName = buildApSsid();

  if (wifiApPassword.length() > 0 && wifiApPassword.length() < 8) {
    server.send(400, "text/plain", "Mot de passe Wi-Fi trop court : minimum 8 caracteres, ou laissez vide.");
    return;
  }

  saveConfig();

  server.sendHeader("Location", "/config", true);
  server.send(302, "text/plain", "");
}

void handleSetVoie() {
  String mode = argText("mode");

  configDoubleVoie = (mode == "double");

  resetCantons();

  saveConfig();

  if (modeAutomatique) {
    lancerJauneClignotantAuto();
  }

  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleToggleMode() {
  if (!modeSecondaire) {
    modeAutomatique = !modeAutomatique;
    if (modeAutomatique) {
      resetCantons();
      lancerJauneClignotantAuto();
    }
    saveConfig();
  }
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleSetControlMode() {
  if (!modeSecondaire) {
    String mode = argText("mode");
    if (mode == "manual") {
      modeAutomatique = false;
      clignotementRougeActif = false;
      alternanceRouges = false;
      saveConfig();
      applyLedOutputs();
    } else if (mode == "sensor") {
      modeAutomatique = true;
      resetCantons();
      lancerJauneClignotantAuto();
      saveConfig();
    }
  }
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleToggle() {
  if (modeSecondaire || modeAutomatique) {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
    return;
  }

  String led = argText("led");
  if (led == "rg") rougeGauche = !rougeGauche;
  else if (led == "rd") rougeDroite = !rougeDroite;
  else if (led == "ja") jaunePN = !jaunePN;

  // Mode manuel :
  // si au moins une rouge est active -> clignotement alterné des DEUX rouges
  if (rougeGauche || rougeDroite) {
    clignotementRougeActif = true;
    alternanceRouges = true;
    rougeGauche = true;
    rougeDroite = true;
  } else {
    clignotementRougeActif = false;
  }
  sendStateToSecondaries();

  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleSetLum() {
  if (server.hasArg("lum")) luminositeLed = server.arg("lum").toInt();
  if (luminositeLed < 0) luminositeLed = 0;
  if (luminositeLed > 100) luminositeLed = 100;

  saveConfig();
  applyLedOutputs();
  sendStateToSecondaries();
  server.send(200, "text/plain", "OK");
}

void handleSaveSensorConfig() {
  if (server.hasArg("latms")) latenceExecutionMs = sanitizeMs(server.arg("latms").toInt(), 0);
  if (latenceExecutionMs > 5000UL) latenceExecutionMs = 0;

  int maxSensorsSave = configDoubleVoie ? 4 : 2;
  bool dejaUtilise[4] = {false, false, false, false};

  for (int logique = 0; logique < maxSensorsSave; logique++) {
    String argName = "ord" + String(logique);
    int phys = ordreSensorClient[logique];

    if (server.hasArg(argName)) {
      phys = server.arg(argName).toInt();
    }

    if (phys < 0 || phys >= maxSensorsSave || dejaUtilise[phys]) {
      phys = logique;
    }

    ordreSensorClient[logique] = phys;
    dejaUtilise[phys] = true;
  }

  if (!configDoubleVoie) {
    ordreSensorClient[2] = 2;
    ordreSensorClient[3] = 3;
  }

  saveConfig();
  sendStateToSecondaries();

  server.sendHeader("Location", "/config", true);
  server.send(302, "text/plain", "");
}


String otaWifiStatusText() {
  String txt;
  if (WiFi.status() == WL_CONNECTED) {
    txt += L("Connecte a ", "Verbonden met ", "Verbunden mit ", "Connected to ");
    txt += WiFi.SSID();
    txt += "<br>" + L("IP locale : ", "Lokaal IP: ", "Lokale IP: ", "Local IP: ");
    txt += WiFi.localIP().toString();
    txt += "<br>" + L("Signal : ", "Signaal: ", "Signal: ", "Signal: ");
    txt += String(WiFi.RSSI());
    txt += " dBm";
  } else if (otaStaSsid.length() > 0) {
    txt += L("Non connecte. Reseau enregistre : ", "Niet verbonden. Opgeslagen netwerk: ", "Nicht verbunden. Gespeichertes Netzwerk: ", "Not connected. Saved network: ");
    txt += otaStaSsid;
  } else {
    txt += L("Non connecte. Aucun Wi-Fi local enregistre.", "Niet verbonden. Geen lokale Wi-Fi opgeslagen.", "Nicht verbunden. Kein lokales WLAN gespeichert.", "Not connected. No local Wi-Fi saved.");
  }
  if (otaLastStatus.length() > 0) {
    txt += "<br>" + L("Dernier message : ", "Laatste bericht: ", "Letzte Meldung: ", "Last message: ");
    txt += otaLastStatus;
  }
  return txt;
}

bool connectOtaWifi(uint32_t timeoutMs = 12000UL) {
  otaStaSsid.trim();
  otaStaPassword.trim();
  if (otaStaSsid.length() == 0) {
    otaLastStatus = "Aucun Wi-Fi local renseigne";
    return false;
  }
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(otaStaSsid.c_str(), otaStaPassword.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) delay(250);
  if (WiFi.status() == WL_CONNECTED) {
    otaLastStatus = "Connexion Wi-Fi locale reussie";
    return true;
  }
  otaLastStatus = "Connexion Wi-Fi locale echouee";
  return false;
}

String urlEncode(const String &value) {
  String encoded = "";
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < value.length(); i++) {
    char c = value.charAt(i);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else if (c == ' ') {
      encoded += "%20";
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

String jsonValueFromObject(const String &obj, const String &key) {
  String marker = "\"" + key + "\"";
  int k = obj.indexOf(marker);
  if (k < 0) return "";
  int colon = obj.indexOf(':', k + marker.length());
  if (colon < 0) return "";
  int q1 = obj.indexOf('"', colon + 1);
  if (q1 < 0) return "";
  String out = "";
  bool esc = false;
  for (int i = q1 + 1; i < obj.length(); i++) {
    char c = obj.charAt(i);
    if (esc) { out += c; esc = false; }
    else if (c == '\\') esc = true;
    else if (c == '"') break;
    else out += c;
  }
  return out;
}

String firmwareListHtmlFromManifest(const String &manifest) {
  String list = "";
  int pos = 0;
  int count = 0;
  while (true) {
    int a = manifest.indexOf('{', pos);
    if (a < 0) break;
    int b = manifest.indexOf('}', a + 1);
    if (b < 0) break;
    String obj = manifest.substring(a, b + 1);
    String name = jsonValueFromObject(obj, "name");
    String version = jsonValueFromObject(obj, "version");
    String theme = jsonValueFromObject(obj, "theme");
    String notes = jsonValueFromObject(obj, "notes");
    String url = jsonValueFromObject(obj, "url");
    if (name.length() == 0 && version.length() == 0) name = "Firmware PM3D";
    if (url.endsWith(".bin") || url.indexOf(".bin?") >= 0) {
      count++;
      list += "<div class='fwItem'>";
      list += "<div class='fwName'>" + htmlEscape(name) + "</div>";
      if (version.length() > 0) list += "<div class='fwMeta'><b>" + L("Version :", "Versie:", "Version:", "Version:") + "</b> " + htmlEscape(version) + "</div>";
      if (theme.length() > 0) list += "<div class='fwMeta'><b>" + L("Theme :", "Thema:", "Thema:", "Theme:") + "</b> " + htmlEscape(theme) + "</div>";
      if (notes.length() > 0) list += "<div class='hint'>" + htmlEscape(notes) + "</div>";
      list += "<div class='fwUrl'>" + htmlEscape(url) + "</div>";
      list += "<a class='btn miniBtn' href='/otainstall?url=" + urlEncode(url) + "'>" + L("Installer cette version", "Deze versie installeren", "Diese Version installieren", "Install this version") + "</a>";
      list += "</div>";
    }
    pos = b + 1;
  }
  if (count == 0) list += "<div class='hint'>" + L("Aucun firmware .bin valide trouve dans le manifeste. Verifiez le fichier manifest.json sur GitHub.", "Geen geldige .bin-firmware gevonden in het manifest. Controleer manifest.json op GitHub.", "Keine gültige .bin-Firmware im Manifest gefunden. Prüfen Sie manifest.json auf GitHub.", "No valid .bin firmware found in the manifest. Check manifest.json on GitHub.") + "</div>";
  return list;
}

String otaScanPage() {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Wi-Fi local OTA</title><style>" + cssCommon() + ".net{display:block;text-decoration:none;color:white;border:1px solid rgba(143,212,255,.3);border-radius:12px;padding:10px;margin:8px 0;background:rgba(0,0,0,.22);}.netForm{border:1px solid rgba(143,212,255,.3);border-radius:12px;padding:10px;margin:8px 0;background:rgba(0,0,0,.22);}.rssi{font-size:12px;color:#cfefff;margin-bottom:8px;}.wifiPassLine{display:grid;grid-template-columns:1fr;gap:8px;margin-top:8px;}</style></head><body><div class='wrap'>" + langBarHtml() + "<a class='back' href='/advanced'>←</a><div class='card'><div class='title'>" + L("Recherche Wi-Fi local", "Lokale Wi-Fi zoeken", "Lokales WLAN suchen", "Local Wi-Fi search") + "</div>";
  html += "<div class='hint'>" + L("Choisissez le Wi-Fi de votre box pour permettre au passage a niveau de telecharger le firmware depuis GitHub.", "Kies de Wi-Fi van uw router zodat de overweg de firmware van GitHub kan downloaden.", "Wählen Sie das WLAN Ihres Routers, damit der Bahnübergang die Firmware von GitHub herunterladen kann.", "Choose your router Wi-Fi so the level crossing can download firmware from GitHub.") + "</div>";
  int n = WiFi.scanNetworks(false, true);
  if (n <= 0) html += "<div class='hint'>" + L("Aucun reseau trouve. Verifiez que la box est proche puis relancez la recherche.", "Geen netwerk gevonden. Controleer of de router dichtbij staat en zoek opnieuw.", "Kein Netzwerk gefunden. Prüfen Sie, ob der Router in der Nähe ist, und suchen Sie erneut.", "No network found. Check that the router is nearby and search again.") + "</div>";
  else {
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;
      bool openNet = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
      html += "<form class='netForm' action='/otasavewifi' method='get'>";
      html += "<input type='hidden' name='ssid' value='" + htmlEscape(ssid) + "'>";
      html += "<b>" + htmlEscape(ssid) + "</b><br><div class='rssi'>" + L("Signal : ", "Signaal: ", "Signal: ", "Signal: ") + String(WiFi.RSSI(i)) + " dBm";
      html += openNet ? " - " + L("ouvert", "open", "offen", "open") : " - " + L("securise", "beveiligd", "gesichert", "secured");
      html += "</div>";
      if (!openNet) {
        html += "<div class='wifiPassLine'><input name='pass' type='password' maxlength='63' placeholder='" + L("Mot de passe Wi-Fi", "Wi-Fi-wachtwoord", "WLAN-Passwort", "Wi-Fi password") + "'>";
        html += "<button class='btn miniBtn' type='submit'>" + L("Se connecter a ce Wi-Fi", "Met deze Wi-Fi verbinden", "Mit diesem WLAN verbinden", "Connect to this Wi-Fi") + "</button></div>";
      } else {
        html += "<input type='hidden' name='pass' value=''>";
        html += "<button class='btn miniBtn' type='submit'>" + L("Se connecter a ce Wi-Fi", "Met deze Wi-Fi verbinden", "Mit diesem WLAN verbinden", "Connect to this Wi-Fi") + "</button>";
      }
      html += "</form>";
    }
  }
  html += "<a class='btn' href='/otascan'>" + L("Relancer la recherche", "Zoeken opnieuw starten", "Suche erneut starten", "Search again") + "</a>";
  html += "</div></div></body></html>";
  return html;
}

void handleOtaScan() { server.send(200, "text/html", otaScanPage()); }

void handleOtaSaveWifi() {
  if (server.hasArg("ssid")) otaStaSsid = argText("ssid");
  if (server.hasArg("pass")) otaStaPassword = argText("pass");
  saveConfig();
  connectOtaWifi();
  server.sendHeader("Location", "/advanced", true);
  server.send(302, "text/plain", "");
}

void handleOtaConnect() {
  connectOtaWifi();
  server.sendHeader("Location", "/advanced", true);
  server.send(302, "text/plain", "");
}

void handleOtaClearWifi() {
  WiFi.disconnect(false, false);
  otaStaSsid = "";
  otaStaPassword = "";
  otaLastStatus = "Identifiants Wi-Fi local effaces";
  saveConfig();
  server.sendHeader("Location", "/advanced", true);
  server.send(302, "text/plain", "");
}

void runOtaUpdateUrl(const String &firmwareUrl) {
  server.send(200, "text/html", "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><style>" + cssCommon() + "</style></head><body><div class='wrap'>" + langBarHtml() + "<div class='card'><div class='title'>" + L("Mise a jour en cours", "Update bezig", "Update läuft", "Update in progress") + "</div><div class='hint'>" + L("Telechargement du firmware PM3D depuis GitHub. Ne coupez pas l alimentation. L appareil redemarrera automatiquement si la mise a jour reussit.", "PM3D-firmware wordt van GitHub gedownload. Schakel de voeding niet uit. Het toestel herstart automatisch als de update slaagt.", "PM3D-Firmware wird von GitHub heruntergeladen. Schalten Sie die Stromversorgung nicht aus. Das Gerät startet automatisch neu, wenn das Update erfolgreich ist.", "Downloading PM3D firmware from GitHub. Do not cut power. The device will restart automatically if the update succeeds.") + "</div></div></div></body></html>");
  delay(800);
  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.rebootOnUpdate(true);
  t_httpUpdate_return ret = httpUpdate.update(client, firmwareUrl);
  if (ret == HTTP_UPDATE_FAILED) otaLastStatus = "Echec OTA : " + String(httpUpdate.getLastError()) + " - " + httpUpdate.getLastErrorString();
  else if (ret == HTTP_UPDATE_NO_UPDATES) otaLastStatus = "Aucune mise a jour disponible";
  else if (ret == HTTP_UPDATE_OK) otaLastStatus = "Mise a jour reussie, redemarrage";
}

void handleOtaManifest() {
  if (WiFi.status() != WL_CONNECTED) {
    if (!connectOtaWifi()) {
      server.send(200, "text/html", "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><style>" + cssCommon() + "</style></head><body><div class='wrap'>" + langBarHtml() + "<div class='card'><div class='title'>" + L("Recherche impossible", "Zoeken onmogelijk", "Suche nicht möglich", "Search impossible") + "</div><div class='hint'>" + L("Le Wi-Fi local n'est pas connecte. Retournez dans Reglages avances, choisissez votre reseau et enregistrez le mot de passe.", "De lokale Wi-Fi is niet verbonden. Ga terug naar geavanceerde instellingen, kies uw netwerk en sla het wachtwoord op.", "Das lokale WLAN ist nicht verbunden. Gehen Sie zurück zu den erweiterten Einstellungen, wählen Sie Ihr Netzwerk und speichern Sie das Passwort.", "Local Wi-Fi is not connected. Go back to advanced settings, choose your network and save the password.") + "</div><a class='btn' href='/advanced'>" + L("Retour", "Terug", "Zurück", "Back") + "</a></div></div></body></html>");
      return;
    }
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(12000);
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Mises a jour PM3D</title><style>" + cssCommon() + ".fwItem{margin:12px 0;padding:14px;border-radius:16px;background:linear-gradient(180deg,rgba(12,38,65,.92),rgba(2,10,20,.90));border:1px solid rgba(143,212,255,.38);box-shadow:0 8px 20px rgba(0,0,0,.32),inset 0 1px 0 rgba(255,255,255,.12);} .fwName{font-weight:900;font-size:18px;margin-bottom:8px;color:#fff;text-shadow:0 2px 6px rgba(0,0,0,.55);}.fwMeta{font-size:13px;margin:4px 0;color:#d9f2ff;} .fwUrl{font-size:11px;line-height:1.35;word-break:break-all;color:#cfefff;background:rgba(0,0,0,.28);border:1px solid rgba(255,255,255,.12);border-radius:10px;padding:8px;margin:8px 0;} .miniBtn{font-size:13px;padding:9px 10px;}</style></head><body><div class='wrap'>" + langBarHtml() + "<a class='back' href='/advanced'>←</a><div class='card'><div class='title'>" + L("Mises a jour firmware", "Firmware-updates", "Firmware-Updates", "Firmware updates") + "</div>";
  html += "<div class='hint'>Liste chargee depuis le catalogue PM3D GitHub.</div>";
  html += "<div class='fwUrl'>" + String(FW_MANIFEST_URL) + "</div>";
  if (!http.begin(client, FW_MANIFEST_URL)) {
    html += "<div class='hint'>Impossible de preparer la connexion au manifeste.</div>";
  } else {
    int code = http.GET();
    if (code != HTTP_CODE_OK) html += "<div class='hint'>Impossible de lire le manifeste. Code HTTP : " + String(code) + "</div>";
    else html += firmwareListHtmlFromManifest(http.getString());
    http.end();
  }
  html += "<a class='btn' href='/advanced'>Retour aux reglages avances</a>";
  html += "</div></div></body></html>";
  server.send(200, "text/html", html);
}

void handleOtaInstall() {
  String firmwareUrl = argText("url");
  firmwareUrl.trim();
  if (!firmwareUrl.startsWith("https://") || firmwareUrl.indexOf(".bin") < 0) {
    server.send(200, "text/html", "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><style>" + cssCommon() + "</style></head><body><div class='wrap'>" + langBarHtml() + "<div class='card'><div class='title'>" + L("Lien firmware refuse", "Firmwarelink geweigerd", "Firmware-Link abgelehnt", "Firmware link refused") + "</div><div class='hint'>" + L("Le lien choisi n'est pas un fichier .bin HTTPS valide.", "De gekozen link is geen geldig HTTPS .bin-bestand.", "Der gewählte Link ist keine gültige HTTPS-.bin-Datei.", "The selected link is not a valid HTTPS .bin file.") + "</div><a class='btn' href='/otamanifest'>" + L("Retour", "Terug", "Zurück", "Back") + "</a></div></div></body></html>");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (!connectOtaWifi()) {
      server.send(200, "text/html", "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><style>" + cssCommon() + "</style></head><body><div class='wrap'>" + langBarHtml() + "<div class='card'><div class='title'>" + L("Mise a jour impossible", "Update onmogelijk", "Update nicht möglich", "Update impossible") + "</div><div class='hint'>" + L("Le Wi-Fi local n'est pas connecte. Retournez dans Reglages avances, choisissez votre reseau et enregistrez le mot de passe.", "De lokale Wi-Fi is niet verbonden. Ga terug naar geavanceerde instellingen, kies uw netwerk en sla het wachtwoord op.", "Das lokale WLAN ist nicht verbunden. Gehen Sie zurück zu den erweiterten Einstellungen, wählen Sie Ihr Netzwerk und speichern Sie das Passwort.", "Local Wi-Fi is not connected. Go back to advanced settings, choose your network and save the password.") + "</div><a class='btn' href='/advanced'>" + L("Retour", "Terug", "Zurück", "Back") + "</a></div></div></body></html>");
      return;
    }
  }
  runOtaUpdateUrl(firmwareUrl);
}

void handleOtaUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    if (!connectOtaWifi()) {
      server.send(200, "text/html", "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><style>" + cssCommon() + "</style></head><body><div class='wrap'>" + langBarHtml() + "<div class='card'><div class='title'>" + L("Mise a jour impossible", "Update onmogelijk", "Update nicht möglich", "Update impossible") + "</div><div class='hint'>" + L("Le Wi-Fi local n'est pas connecte. Retournez dans Reglages avances, choisissez votre reseau et enregistrez le mot de passe.", "De lokale Wi-Fi is niet verbonden. Ga terug naar geavanceerde instellingen, kies uw netwerk en sla het wachtwoord op.", "Das lokale WLAN ist nicht verbunden. Gehen Sie zurück zu den erweiterten Einstellungen, wählen Sie Ihr Netzwerk und speichern Sie das Passwort.", "Local Wi-Fi is not connected. Go back to advanced settings, choose your network and save the password.") + "</div><a class='btn' href='/advanced'>" + L("Retour", "Terug", "Zurück", "Back") + "</a></div></div></body></html>");
      return;
    }
  }
  runOtaUpdateUrl(String(FW_BIN_URL));
}

void handleSaveAdvanced() {
  stopLedGpioTests();
  if (server.hasArg("rg")) gpioRougeGauche = server.arg("rg").toInt();
  if (server.hasArg("rd")) gpioRougeDroite = server.arg("rd").toInt();
  if (server.hasArg("ja")) gpioJaunePN = server.arg("ja").toInt();

  for (int i=0;i<3;i++) {
    macSecondaire[i] = argText("sec" + String(i));
  }
  for (int i=0;i<4;i++) {
    macSensor[i] = argText("sen" + String(i));
  }

  tempsRougeAllumageMs = sanitizeMs(argText("ron").toInt(), 400);
  tempsRougeExtinctionMs = sanitizeMs(argText("roff").toInt(), 100);
  tempsRougePauseMs = sanitizeMs(argText("rpaus").toInt(), 0);
  alternanceRouges = server.hasArg("altR");

  tempsJauneAllumageMs = sanitizeMs(argText("jon").toInt(), 700);
  tempsJauneExtinctionMs = sanitizeMs(argText("joff").toInt(), 300);
  tempsJaunePauseMs = sanitizeMs(argText("jpaus").toInt(), 1200);

  saveConfig();

  setupOneOutput(gpioRougeGauche);
  setupOneOutput(gpioRougeDroite);
  setupOneOutput(gpioJaunePN);
  reconnectSavedAccessories();
  applyLedOutputs();
  sendStateToSecondaries();

  server.sendHeader("Location", "/advanced", true);
  server.send(302, "text/plain", "");
}

void handleLedTest() {
  int id = server.hasArg("id") ? server.arg("id").toInt() : -1;
  int pin = server.hasArg("pin") ? server.arg("pin").toInt() : -1;

  if (id < 0 || id > 2 || !isValidGpio(pin)) {
    server.send(400, "text/plain", "ERR");
    return;
  }

  if (ledTestTempPin[id] >= 0 && ledTestTempPin[id] != pin) {
    writeTestLedDirect(ledTestTempPin[id], false);
  }

  // Si le meme GPIO est deja teste sur une autre ligne, on coupe l'ancien test
  // pour eviter deux boutons qui se disputent la meme sortie.
  for (int i = 0; i < 3; i++) {
    if (i != id && ledTestTempPin[i] == pin) {
      ledTestTempState[i] = false;
      ledTestTempPin[i] = -1;
    }
  }

  ledTestTempPin[id] = pin;
  ledTestTempState[id] = !ledTestTempState[id];
  writeTestLedDirect(pin, ledTestTempState[id]);
  server.send(200, "text/plain", ledTestTempState[id] ? "ON" : "OFF");
}

void handleSearch() {
  String target = argText("target");
  server.send(200, "text/html", searchPage(target));
}

void handleAdd() {
  String target = argText("target");
  String mac = argText("mac");
  String ssid = argText("ssid");

  if (target.startsWith("sec")) {
    int i = target.substring(3).toInt();
    if (i >= 0 && i < 4) {
      macSecondaire[i] = mac;
      ssidSecondaire[i] = ssid;
    }
  } else if (target.startsWith("sen")) {
    int i = target.substring(3).toInt();
    if (i >= 0 && i < 4) {
      macSensor[i] = mac;
      ssidSensor[i] = ssid;
    }
  }

  saveConfig();
  reconnectSavedAccessories();
  sendStateToSecondaries();

  server.sendHeader("Location", "/advanced", true);
  server.send(302, "text/plain", "");
}

void handleSensorStatus() {
  String json = "{";
  for (int i=0;i<4;i++) {
    if (i > 0) json += ",";
    json += "\"s" + String(i) + "\":" + String(recent(dernierDetectSensorMs[i], 1200UL) ? "true" : "false");
  }
  json += "}";
  server.send(200, "application/json", json);
}

void handleNotFound() {
  sendCaptiveRedirect();
}

// =======================================================
// Setup/Loop
// =======================================================

void setup() {
  Serial.begin(115200);
  delay(200);

  loadConfig();

  setupOneOutput(gpioRougeGauche);
  setupOneOutput(gpioRougeDroite);
  setupOneOutput(gpioJaunePN);

  if (modeAutomatique) {
    rougeGauche = false;
    rougeDroite = false;
    jaunePN = true;
    clignotementRougeActif = false;
    clignotementJauneActif = true;
    phaseJauneAllumee = true;
    dernierChangementJaune = millis();
  }

  applyLedOutputs();

  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  delay(150);
  WiFi.mode(WIFI_AP_STA);
  delay(100);

  apSsid = wifiApName;
  apSsid.trim();
  if (apSsid.length() == 0) apSsid = buildApSsid();

  WiFi.softAPConfig(apIp, apGateway, apSubnet);
  bool ok;
  if (wifiApPassword.length() >= 8) ok = WiFi.softAP(apSsid.c_str(), wifiApPassword.c_str(), 1);
  else ok = WiFi.softAP(apSsid.c_str(), nullptr, 1);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  dnsServer.start(DNS_PORT, "*", apIp);

  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onEspNowReceive);
    esp_now_register_send_cb(onEspNowSend);
    reconnectSavedAccessories();
    sendStateToSecondaries();
  }

  server.on("/", handleRoot);
  server.on("/intro", handleIntro);
  server.on("/generate_204", handleCaptivePortal);          // Android
  server.on("/gen_204", handleCaptivePortal);               // Android alternative
  server.on("/hotspot-detect.html", handleCaptivePortal);   // iOS/macOS
  server.on("/library/test/success.html", handleCaptivePortal);
  server.on("/canonical.html", handleCaptivePortal);        // Android / ChromeOS
  server.on("/fwlink", handleCaptivePortal);                // Windows
  server.on("/ncsi.txt", handleCaptivePortal);              // Windows
  server.on("/connecttest.txt", handleCaptivePortal);       // Windows
  server.on("/redirect", handleCaptivePortal);
  server.on("/mobile/status.php", handleCaptivePortal);     // Android / Samsung
  server.on("/kindle-wifi/wifistub.html", handleCaptivePortal);
  server.on("/success.txt", handleCaptivePortalPage);
  server.on("/success.html", handleCaptivePortal);
  server.on("/connectivity-check.html", handleCaptivePortal);
  server.on("/check_network_status.txt", handleCaptivePortal);
  server.on("/gstatic/generate_204", handleCaptivePortal);
  server.on("/generate204", handleCaptivePortal);
  server.on("/wpad.dat", handleCaptivePortal);
  server.on("/config", handleConfig);
  server.on("/setlang", handleSetLang);
  server.on("/advanced", handleAdvanced);
  server.on("/otascan", handleOtaScan);
  server.on("/otasavewifi", handleOtaSaveWifi);
  server.on("/otaconnect", handleOtaConnect);
  server.on("/otaclearwifi", handleOtaClearWifi);
  server.on("/otamanifest", handleOtaManifest);
  server.on("/otainstall", handleOtaInstall);
  server.on("/otaupdate", handleOtaUpdate);
  server.on("/savewifi", handleSaveWifi);
  server.on("/setvoie", handleSetVoie);
  server.on("/togglemode", handleToggleMode);
  server.on("/setcontrol", handleSetControlMode);
  server.on("/toggle", handleToggle);
  server.on("/setlum", handleSetLum);
  server.on("/savesensorconfig", handleSaveSensorConfig);
  server.on("/ledtest", handleLedTest);
  server.on("/saveadvanced", handleSaveAdvanced);
  server.on("/search", handleSearch);
  server.on("/add", handleAdd);
  server.on("/sensorstatus", handleSensorStatus);
  server.on("/debugsensor", [](){
    String json = "{";
    json += "\"age\":" + String(debugDernierSensorMs > 0 ? millis() - debugDernierSensorMs : 999999);
    json += ",\"texte\":\"" + htmlEscape(debugDernierSensorTexte) + "\"";
    json += ",\"mac\":\"" + htmlEscape(debugDernierSensorMac) + "\"";
    json += ",\"suffix\":\"" + htmlEscape(debugDernierSensorSuffix) + "\"";
    json += ",\"event\":\"" + htmlEscape(debugDernierSensorEvent) + "\"";
    json += ",\"resultat\":\"" + htmlEscape(debugDernierSensorResultat) + "\"";
    json += ",\"action\":\"" + htmlEscape(debugDerniereAction) + "\"";
    json += "}";
    server.send(200, "application/json", json);
  });
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println();
  Serial.println("==============================");
  Serial.println("PM3D Passage a niveau ESP-NOW");
  Serial.print("SSID : "); Serial.println(apSsid);
  Serial.print("IP   : "); Serial.println(WiFi.softAPIP());
  Serial.print("AP OK: "); Serial.println(ok ? "oui" : "non");
  Serial.println("==============================");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  updateBlinkEngine();
  updateRoleSecondaire();
  updateHeartbeatSecondaire();
  updateMaintienAccessoires();
}
