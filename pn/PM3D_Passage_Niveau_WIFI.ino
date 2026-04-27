#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>

WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;
Preferences prefs;

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

void applyLedOutputs() {
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
    if (macMatchesVariants(mac, macSecondaire[i])) dernierOkSecondaireMs[i] = millis();
  }
}

void updateOkSensorFromMac(const uint8_t mac[6]) {
  for (int i=0;i<4;i++) {
    if (macMatchesVariants(mac, macSensor[i])) dernierVuSensorMs[i] = millis();
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

  if (millis() - dernierMaintienAccessoiresMs < INTERVALLE_MAINTIEN_MS) return;
  dernierMaintienAccessoiresMs = millis();

  reconnectSavedAccessories();

  bool needSync = false;
  for (int i=0;i<4;i++) {
    if (macSecondaire[i].length() > 0 && !recent(dernierOkSecondaireMs[i], 8000UL)) {
      needSync = true;
    }
  }
  if (needSync) sendStateToSecondaries();
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
  css += "body{margin:0;padding:10px;text-align:center;font-family:Arial,Helvetica,sans-serif;color:#F6FBFF;background:radial-gradient(circle at 50% -10%,rgba(92,190,255,.34),rgba(9,26,48,.62) 34%,#01040A 78%),linear-gradient(135deg,#0b1828 0%,#121c28 22%,#06162B 46%,#020711 100%);background-attachment:fixed;}";
  css += "body:before{content:'';position:fixed;inset:0;pointer-events:none;background:linear-gradient(rgba(160,215,255,.045) 1px,transparent 1px),linear-gradient(90deg,rgba(220,235,245,.035) 1px,transparent 1px),radial-gradient(circle at 18% 12%,rgba(80,190,255,.16),transparent 28%),radial-gradient(circle at 88% 2%,rgba(210,225,235,.12),transparent 24%);background-size:28px 28px,28px 28px,100% 100%,100% 100%;opacity:.42;}";
  css += ".wrap{max-width:760px;margin:0 auto;position:relative;z-index:1;}";
  css += ".card{background:linear-gradient(145deg,rgba(56,76,92,.96) 0%,rgba(18,54,88,.97) 28%,rgba(7,22,42,.98) 72%,rgba(2,8,18,.99) 100%);border:1px solid rgba(190,230,255,.30);border-radius:22px;padding:13px;margin-bottom:11px;box-shadow:0 18px 44px rgba(0,0,0,.46),0 0 24px rgba(80,180,255,.10),inset 0 1px 0 rgba(255,255,255,.22),inset 0 -1px 0 rgba(0,0,0,.55);}";
  css += ".title{font-size:22px;font-weight:900;margin:6px 0 12px;letter-spacing:.2px;text-shadow:0 2px 8px rgba(0,0,0,.65),0 0 16px rgba(100,205,255,.32);}";
  css += ".btn{display:block;position:relative;overflow:hidden;border:1px solid rgba(235,248,255,.42);border-radius:14px;padding:12px;background:linear-gradient(180deg,#f1fbff 0%,#9fd2ea 10%,#516a7b 23%,#1b78b0 48%,#082f56 100%);color:white;font-size:15px;font-weight:900;text-decoration:none;margin:8px 0;box-shadow:inset 0 1px 0 rgba(255,255,255,.82),inset 0 10px 14px rgba(255,255,255,.13),inset 0 -10px 18px rgba(0,0,0,.48),0 0 16px rgba(80,185,255,.15),0 8px 18px rgba(0,0,0,.32);text-shadow:0 1px 3px rgba(0,0,0,.70);}";
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
  return css;
}

String introPage() {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>PM3D Passage a niveau</title>";
  html += "<style>";
  html += "body{margin:0;height:100vh;overflow:hidden;display:flex;align-items:center;justify-content:center;font-family:Arial,Helvetica,sans-serif;color:white;background:radial-gradient(circle at 50% 12%,rgba(120,215,255,.34),rgba(18,45,72,.82) 32%,rgba(3,12,28,.96) 58%,#01040A 100%);}";
  html += ".intro{width:min(92vw,520px);text-align:center;padding:24px;border-radius:28px;background:linear-gradient(145deg,rgba(76,92,104,.80),rgba(14,42,78,.76) 34%,rgba(1,8,18,.88));border:1px solid rgba(190,235,255,.34);box-shadow:0 24px 70px rgba(0,0,0,.60),0 0 36px rgba(80,190,255,.16),inset 0 1px 0 rgba(255,255,255,.24),inset 0 -1px 0 rgba(0,0,0,.55);}";
  html += ".logo{width:230px;max-width:70%;animation:spinLogo 6s ease-in-out forwards, glowLogo 1.4s ease-in-out infinite alternate;filter:drop-shadow(0 18px 24px rgba(0,0,0,.55));}";
  html += "@keyframes spinLogo{0%{transform:scale(.72) rotate(-18deg);opacity:0;}10%{opacity:1;}56%{transform:scale(1.04) rotate(720deg);opacity:1;}64%{transform:scale(1) rotate(720deg) translateX(0);}70%{transform:scale(1) rotate(720deg) translateX(18px);}76%{transform:scale(1) rotate(720deg) translateX(-18px);}82%{transform:scale(1) rotate(720deg) translateX(14px);}88%{transform:scale(1) rotate(720deg) translateX(-14px);}94%{transform:scale(1) rotate(720deg) translateX(9px);}100%{transform:scale(1) rotate(720deg) translateX(0);opacity:1;}}";
  html += "@keyframes glowLogo{from{filter:drop-shadow(0 12px 20px rgba(80,190,255,.25));}to{filter:drop-shadow(0 18px 30px rgba(190,255,255,.55));}}";
  html += ".title{font-size:27px;font-weight:900;margin:18px 0 6px;text-shadow:0 2px 10px #000;}";
  html += ".phrase{font-size:14px;color:#CDEFFF;line-height:1.45;margin-bottom:22px;}";
  html += ".bar{position:relative;height:20px;border-radius:999px;background:#020812;border:1px solid rgba(190,230,255,.35);overflow:hidden;box-shadow:inset 0 2px 8px rgba(0,0,0,.8);}";
  html += ".fill{height:100%;width:0%;border-radius:999px;background:linear-gradient(90deg,#74d9ff,#d8faff,#5fb7ff);animation:load 6s linear forwards;box-shadow:0 0 18px rgba(120,220,255,.9);}";
  html += ".fill:after{content:'';display:block;height:100%;background:repeating-linear-gradient(45deg,rgba(255,255,255,.38) 0 10px,transparent 10px 20px);animation:stripes .65s linear infinite;}";
  html += "@keyframes load{from{width:0%;}to{width:100%;}}@keyframes stripes{from{transform:translateX(-20px);}to{transform:translateX(20px);}}";
  html += ".pct{font-weight:900;font-size:18px;margin-top:12px;color:#E9FAFF;}";
  html += ".steps{height:18px;margin-top:10px;font-size:12px;color:#A9DDF7;}";
  html += "</style>";
  html += "<script>";
  html += "let p=0;const msgs=['Initialisation du systeme PM3D','Verification des sensors','Synchronisation des signaux','Chargement de l interface','Pret au depart'];";
  html += "function tick(){p=Math.min(100,Math.round((performance.now()-t0)/6000*100));document.getElementById('pct').innerText=p+'%';document.getElementById('steps').innerText=msgs[Math.min(msgs.length-1,Math.floor(p/25))];if(p<100)requestAnimationFrame(tick);else setTimeout(()=>{try{sessionStorage.setItem('pm3dIntroDone','1');}catch(e){}location.replace('/?nointro=1');},350);}";
  html += "let t0;window.onload=()=>{t0=performance.now();tick();};";
  html += "</script></head><body>";
  html += "<div class='intro'>";
  html += "<img class='logo' src='data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAoQAAAJ1CAYAAABAeeHzAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAADsMAAA7DAcdvqGQAAJwrSURBVHhe7d0HnBTl/T/wL8IdHJ2DozcFpEiVjpQIigWNGk3URBPRxNj9xxKNJerPEk0ssXcsWLACRhBUUDgEpUgX0EPqgXQ4yh39P59n5znmlr27LTO7M/t83q9sbmYP7/ZmZ2c+833KVDhsESIiIiIy1jH2VyIiIiIyFAMhERERkeEYCImIiIgMx0BIREREZDgGQiIiIiLDMRASERERGY6BkIiIiMhwDIREREREhmMgJCIiIjIcAyERERGR4RgIiYiIiAzHQEhERERkOAZCIiIiIsMxEBIREREZjoGQiIiIyHAMhERERESGYyAkIiIiMhwDIREREZHhGAiJiIiIDMdASERERGQ4BkIiIiIiwzEQEhERERmOgZCIiIjIcAyERERERIZjICQiIiIyHAMhERERkeEYCImIiIgMx0BIREREZDgGQiIiIiLDMRASERERGY6BkIiIiMhwDIREREREhmMgJCIiIjIcAyERERGR4RgIiYiIiAzHQEhERERkOAZCIiIiIsMxEBIREREZjoGQiIiIyHAMhERERESGYyAkIiIiMhwDIREREZHhGAiJiIiIDMdASERERGQ4BkIiIiIiwzEQEhERERmOgZCIiIjIcAyERERERIZjICQiIiIyHAMhERERkeEYCImIiIgMx0BIREREZDgGQiIiIiLDMRASERERGY6BkIiIiMhwDIREREREhmMgJCIiIjIcAyERERGR4RgIiYiIiAzHQEhERERkOAZCIiIiIsMxEBIREREZjoGQiIiIyHAMhERERESGYyAkIiIiMhwDIREREZHhGAiJiIiIDMdASERERGQ4BkIiIiIiwzEQEhERERmOgZCIiIjIcAyERERERIZjICQiIiIyHAMhERERkeEYCImIiIgMx0BIREREZDgGQiIiIiLDMRASke+tO3DIXiIiIi8wEBKR7xVVOkby7WUiInIfAyER+dp+64H6YKH1WG09DloPIiJyFwMhEfnaXvsr7LMeqBQyFBIRuYuBkIh8rSCs/yBDIRGR+xgIicjXKlY6+jDFUEhE5C4GQiLyNWeTsRNDIRGRexgIici3ig4cUsGvNPje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'>";
  html += "<div class='title'>PM3D Passage a niveau</div>";
  html += "<div class='phrase'>Systeme autonome de signalisation ferroviaire miniature<br>Configuration intelligente simple voie et double voie</div>";
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
  html += "</style><script>try{const q=new URLSearchParams(location.search);if(!q.has('nointro')&&sessionStorage.getItem('pm3dIntroDone')!=='1'){location.replace('/intro');}}catch(e){}</script></head><body><div class='wrap'>";
  html += "<div class='card'><div class='title'>PM3D Passage a niveau</div>";
  html += "<a class='btn " + String(!configDoubleVoie ? "selectedMode" : "") + "' href='/setvoie?mode=simple'>Utilisation simple voie</a>";
  html += "<a class='btn " + String(configDoubleVoie ? "selectedMode" : "") + "' href='/setvoie?mode=double'>Utilisation double voie</a>";
  html += "<a class='btn' href='/config'>Configuration des capteurs</a>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<a class='btn " + String(modeAutomatique ? "selectedMode" : "") + "' href='/togglemode'>";
  html += modeAutomatique ? "Mode automatique par capteurs" : "Mode manuel";
  html += "</a>";
  html += "</div>";

  html += "<div class='card'>" + ledSvg() + "</div>";
  html += "</div></body></html>";
  return html;
}

String configPage() {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Configuration</title><style>" + cssCommon() + ".modalMask{position:fixed;inset:0;background:rgba(0,0,0,.72);display:none;align-items:center;justify-content:center;z-index:999;padding:18px;backdrop-filter:blur(4px);}.modalBox{max-width:440px;width:100%;border-radius:22px;padding:20px;text-align:center;background:linear-gradient(145deg,rgba(65,0,0,.96),rgba(18,2,6,.98));border:3px solid #ff1d1d;box-shadow:0 0 34px rgba(255,0,0,.72),0 20px 60px rgba(0,0,0,.75),inset 0 1px 0 rgba(255,255,255,.18);animation:advPulse 1s infinite;color:white;}.modalTitle{font-size:23px;font-weight:900;margin-bottom:10px;text-transform:uppercase;}.modalText{font-size:15px;line-height:1.55;color:#fff;margin-bottom:16px;}.modalActions{display:grid;grid-template-columns:1fr;gap:9px;}@keyframes advPulse{0%,100%{border-color:#ff1d1d;box-shadow:0 0 18px rgba(255,0,0,.55),0 20px 60px rgba(0,0,0,.75),inset 0 1px 0 rgba(255,255,255,.18);}50%{border-color:#fff;box-shadow:0 0 38px rgba(255,0,0,.95),0 0 12px rgba(255,255,255,.75),0 20px 60px rgba(0,0,0,.75),inset 0 1px 0 rgba(255,255,255,.24);}}" + "</style>";
  html += "<script>";
  html += "function setLum(v){document.getElementById('lumVal').innerText=v+'%';fetch('/setlum?lum='+v);}";
  html += "function openAdvancedWarning(){document.getElementById('advWarn').style.display='flex';}function closeAdvancedWarning(){document.getElementById('advWarn').style.display='none';}function goAdvanced(){location.href='/advanced';}";
  html += "function upd(){fetch('/sensorstatus').then(r=>r.json()).then(d=>{for(let i=0;i<4;i++){let e=document.getElementById('sd'+i);if(e)e.className='dot '+(d['s'+i]?'green':'red');}});}setInterval(upd,400);window.addEventListener('load',upd);";
  html += "</script></head><body><div class='wrap'><a class='back' href='/'>←</a>";
  html += "<div class='card'><div class='title'>Configuration</div>";

  html += "<div class='sub'><div class='section'>Luminosite des LED</div>";
  html += "<input type='range' min='0' max='100' value='" + String(luminositeLed) + "' oninput='setLum(this.value)'>";
  html += "<div class='hint'>Valeur : <span id='lumVal'>" + String(luminositeLed) + "%</span></div></div>";

  html += "<div class='sub'><div class='section'>Reseau Wi-Fi de l'appareil</div>";
  html += "<form action='/savewifi'>";
  html += "<div class='hint'>Nom Wi-Fi visible par le client. Le mot de passe est optionnel. S'il est utilise, il doit contenir au moins 8 caracteres.</div>";
  html += "<label>Nom du Wi-Fi</label><input name='ssid' maxlength='31' value='" + htmlEscape(apSsid) + "'>";
  html += "<label>Mot de passe Wi-Fi</label><input name='pass' type='password' maxlength='63' value='" + htmlEscape(wifiApPassword) + "' placeholder='vide = acces sans mot de passe'>";
  html += "<button class='btn' type='submit'>Enregistrer Wi-Fi</button>";
  html += "</form></div>";

  html += "<div class='sub'><div class='section'>Retard signal secondaire</div>";
  html += "<form action='/savesensorconfig'><div class='hint'>Permet d'ajouter un petit decalage d'execution pour aligner visuellement un signal secondaire avec le signal principal. Laissez 0 si tout est deja synchronise.</div><div class='grid'><label>Retard (ms)</label><input name='latms' type='number' min='0' max='5000' value='" + String(latenceExecutionMs) + "'></div>";

  html += "<div class='section'>Test reception sensors</div>";
  html += "<div class='hint'>Ces voyants permettent de verifier en direct si chaque sensor detecte correctement un train ou votre main. Rouge = rien detecte, vert = detection active.</div>";
  html += "<div style='display:grid;grid-template-columns:repeat(" + String(configDoubleVoie ? 4 : 2) + ",1fr);gap:8px;text-align:center'>";
  for (int i=0;i<(configDoubleVoie ? 4 : 2);i++) {
    html += "<div class='sensorCard'><div>S" + String(i+1) + "</div><span id='sd" + String(i) + "' class='dot red'></span></div>";
  }
  html += "</div>";

  html += "<div class='section'>Position des sensors</div>";
  html += "<div class='sensorPlan'>";
  html += "<div class='hint'>Choisissez quel sensor installe correspond a chaque position autour du passage a niveau.</div>";

  if (!configDoubleVoie) {
    html += "<div class='trackTitle'>Simple voie</div>";
    html += "<div class='pnScene simpleMode'>";
    html += "<div class='pnPlate'></div><div class='roadPN'></div>";
    html += "<div class='railTrack trackA'><div class='sleepers'></div></div>";
    html += "<div class='miniBarrier miniBarrierL'></div><div class='miniBarrier miniBarrierR'></div>";
    html += "<div class='sensorPoint sensorLeft sensorMid'><label>S1 - entree/sortie</label><select name='ord0'>";
    for (int phys = 0; phys < 2; phys++) {
      html += "<option value='" + String(phys) + "'" + String(ordreSensorClient[0] == phys ? " selected" : "") + ">Sensor " + String(phys + 1) + "</option>";
    }
    html += "</select></div>";
    html += "<div class='sensorPoint sensorRight sensorMid'><label>S2 - entree/sortie</label><select name='ord1'>";
    for (int phys = 0; phys < 2; phys++) {
      html += "<option value='" + String(phys) + "'" + String(ordreSensorClient[1] == phys ? " selected" : "") + ">Sensor " + String(phys + 1) + "</option>";
    }
    html += "</select></div>";
    html += "</div>";
    html += "<div class='legendPN'>Le passage a niveau est au centre. S1 et S2 forment le canton simple voie.</div>";
  } else {
    html += "<div class='trackTitle'>Double voie</div>";
    html += "<div class='pnScene doubleMode'>";
    html += "<div class='pnPlate'></div><div class='roadPN'></div>";
    html += "<div class='railTrack trackA'><div class='sleepers'></div></div>";
    html += "<div class='railTrack trackB'><div class='sleepers'></div></div>";
    html += "<div class='miniBarrier miniBarrierL'></div><div class='miniBarrier miniBarrierR'></div>";

    html += "<div class='sensorPoint sensorLeft sensorTop'><label>S1</label><select name='ord0'>";
    for (int phys = 0; phys < 4; phys++) {
      html += "<option value='" + String(phys) + "'" + String(ordreSensorClient[0] == phys ? " selected" : "") + ">Sensor " + String(phys + 1) + "</option>";
    }
    html += "</select></div>";

    html += "<div class='sensorPoint sensorRight sensorTop'><label>S2</label><select name='ord1'>";
    for (int phys = 0; phys < 4; phys++) {
      html += "<option value='" + String(phys) + "'" + String(ordreSensorClient[1] == phys ? " selected" : "") + ">Sensor " + String(phys + 1) + "</option>";
    }
    html += "</select></div>";

    html += "<div class='sensorPoint sensorLeft sensorLow'><label>S3</label><select name='ord2'>";
    for (int phys = 0; phys < 4; phys++) {
      html += "<option value='" + String(phys) + "'" + String(ordreSensorClient[2] == phys ? " selected" : "") + ">Sensor " + String(phys + 1) + "</option>";
    }
    html += "</select></div>";

    html += "<div class='sensorPoint sensorRight sensorLow'><label>S4</label><select name='ord3'>";
    for (int phys = 0; phys < 4; phys++) {
      html += "<option value='" + String(phys) + "'" + String(ordreSensorClient[3] == phys ? " selected" : "") + ">Sensor " + String(phys + 1) + "</option>";
    }
    html += "</select></div>";

    html += "</div>";
    html += "<div class='legendPN'>Canton haut : S1/S2. Canton bas : S3/S4. Retour jaune uniquement quand les deux cantons sont refermes.</div>";
  }

  html += "</div>";

  html += "<div class='section'>Activite des sensors</div>";
  html += "<div class='sensorCard'>";
  html += "<div class='hint'><b>Fonctionnement automatique du passage a niveau</b></div>";
  html += "<div class='hint'><b>Utilisation simple voie :</b><br>";
  html += "- Sensor 1 ou Sensor 2 detecte un train : passage au rouge clignotant.<br>";
  html += "- L'autre sensor devient le sensor de sortie.<br>";
  html += "- Quand ce sensor de sortie a detecte le train puis ne detecte plus rien : retour au jaune clignotant.</div>";
  html += "<div class='hint'><b>Utilisation double voie :</b><br>";
  html += "- S1 et S2 forment un premier canton.<br>";
  html += "- S3 et S4 forment un second canton parallele.<br>";
  html += "- Un canton ouvert par un sensor reste actif jusqu'a detection puis liberation du sensor oppose.<br>";
  html += "- Le retour au jaune clignotant se fait uniquement quand les deux cantons sont refermes.</div>";
  html += "<div class='hint'><b>Important :</b><br>";
  html += "Les conditions manuelles des sensors ont ete supprimees. Le fonctionnement est fixe, automatique et securise. L'ordre S1/S2/S3/S4 peut etre modifie ci-dessus sans toucher aux adresses MAC.</div>";
  html += "</div><button class='btn' type='submit'>Enregistrer</button></form></div>";

  html += "<a class='btn danger' href='#' onclick='openAdvancedWarning();return false;'>Reglages avances</a>";
  html += "</div></div>";
  html += "<div id='advWarn' class='modalMask'><div class='modalBox'><div class='modalTitle'>Attention</div><div class='modalText'><b>Zone de configuration avancee PM3D</b><br>Toute modification effectuee ici peut perturber le fonctionnement de l'installation, des capteurs ou des signaux associes.<br><br>Continuez uniquement si vous savez exactement ce que vous modifiez.</div><div class='modalActions'><button class='btn danger' onclick='closeAdvancedWarning()'>Annuler et revenir en securite</button><button class='btn' onclick='goAdvanced()'>Je comprends les risques - Continuer</button></div></div></div>";
  html += "</body></html>";
  return html;
}

String advancedPage() {
  int maxSecondairesAdvanced = configDoubleVoie ? 4 : 2;
  int maxSensorsAdvanced = configDoubleVoie ? 4 : 2;
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Avance</title><style>" + cssCommon() + ".warn{background:linear-gradient(180deg,rgba(110,0,0,.92),rgba(45,0,0,.94));border:3px solid #ff1d1d;border-radius:14px;padding:12px;font-weight:900;color:#fff;line-height:1.45;box-shadow:0 0 18px rgba(255,0,0,.65),inset 0 1px 0 rgba(255,255,255,.18);animation:warnPulse 1.05s infinite;}@keyframes warnPulse{0%,100%{border-color:#ff1d1d;box-shadow:0 0 10px rgba(255,0,0,.45),inset 0 1px 0 rgba(255,255,255,.18);}50%{border-color:#ffffff;box-shadow:0 0 26px rgba(255,0,0,.95),0 0 8px rgba(255,255,255,.75),inset 0 1px 0 rgba(255,255,255,.25);}}.debugBox{border:1px solid rgba(143,212,255,.28);border-radius:12px;padding:10px;background:rgba(0,0,0,.22);line-height:1.7;font-size:13px;}</style><script>function updDbg(){fetch('/debugsensor').then(r=>r.json()).then(d=>{document.getElementById('dbgTexte').innerText=d.texte;document.getElementById('dbgAge').innerText=d.age;document.getElementById('dbgMac').innerText=d.mac;document.getElementById('dbgSuffix').innerText=d.suffix;document.getElementById('dbgEvent').innerText=d.event;document.getElementById('dbgResultat').innerText=d.resultat;document.getElementById('dbgAction').innerText=d.action;});}setInterval(updDbg,500);window.addEventListener('load',updDbg);</script></head>";
  html += "<body><div class='wrap'><a class='back' href='/config'>←</a><div class='card'><div class='title'>Reglages avances</div><div class='warn'><b>Attention - réglages techniques</b><br>Cette zone permet d'associer les capteurs et les signaux secondaires. Elle est destinée à l'installation initiale ou à une intervention de maintenance.</div>";

  html += "<form action='/saveadvanced'>";
  html += "<div class='sub'><div class='section'>GPIO LED</div><div class='grid'>";
  html += "<label>Rouge gauche</label><input name='rg' type='number' value='" + String(gpioRougeGauche) + "'>";
  html += "<label>Rouge droite</label><input name='rd' type='number' value='" + String(gpioRougeDroite) + "'>";
  html += "<label>Jaune PN</label><input name='ja' type='number' value='" + String(gpioJaunePN) + "'>";
  html += "</div></div>";

  html += "<div class='sub'><div class='section'>Signaux secondaires</div>";
  for (int i=0;i<maxSecondairesAdvanced;i++) {
    html += "<label>Secondaire " + String(i+1) + " " + badge(recent(dernierOkSecondaireMs[i], 8000UL)) + "</label>";
    html += "<div class='grid'><input name='sec" + String(i) + "' value='" + htmlEscape(macSecondaire[i]) + "' placeholder='AA:BB:CC:DD:EE:FF'>";
    html += "<a class='btn' href='/search?target=sec" + String(i) + "'>Rechercher</a></div>";
  }
  html += "</div>";

  html += "<div class='sub'><div class='section'>Sensors</div>";
  for (int i=0;i<maxSensorsAdvanced;i++) {
    html += "<label>Sensor " + String(i+1) + " " + badge(recent(dernierVuSensorMs[i], 8000UL)) + "</label>";
    html += "<div class='grid'><input name='sen" + String(i) + "' value='" + htmlEscape(macSensor[i]) + "' placeholder='AA:BB:CC:DD:EE:FF'>";
    html += "<a class='btn' href='/search?target=sen" + String(i) + "'>Rechercher</a></div>";
  }
  html += "</div>";

  html += "<div class='sub'><div class='section'>Clignotement rouge</div><div class='grid'>";
  html += "<label>Rouge ON ms</label><input name='ron' value='" + String(tempsRougeAllumageMs) + "'>";
  html += "<label>Rouge OFF ms</label><input name='roff' value='" + String(tempsRougeExtinctionMs) + "'>";
  html += "<label>Rouge pause ms</label><input name='rpaus' value='" + String(tempsRougePauseMs) + "'>";
  html += "</div><label><input type='checkbox' name='altR' value='1'" + checked(alternanceRouges) + "> Alternance rouges</label></div>";

  html += "<div class='sub'><div class='section'>Clignotement jaune</div><div class='grid'>";
  html += "<label>Jaune ON ms</label><input name='jon' value='" + String(tempsJauneAllumageMs) + "'>";
  html += "<label>Jaune OFF ms</label><input name='joff' value='" + String(tempsJauneExtinctionMs) + "'>";
  html += "<label>Jaune pause ms</label><input name='jpaus' value='" + String(tempsJaunePauseMs) + "'>";
  html += "</div></div>";

  html += "<div class='sub'><div class='section'>Debug reception sensors</div>";
  html += "<div class='hint'>Ce panneau montre en temps reel si le signal principal recoit quelque chose des sensors.</div>";
  html += "<div class='debugBox'>";
  html += "<div><b>Reception :</b> <span id='dbgTexte'>-</span></div>";
  html += "<div><b>Age :</b> <span id='dbgAge'>-</span> ms</div>";
  html += "<div><b>MAC recue :</b> <span id='dbgMac'>-</span></div>";
  html += "<div><b>Suffix :</b> <span id='dbgSuffix'>-</span></div>";
  html += "<div><b>Evenement :</b> <span id='dbgEvent'>-</span></div>";
  html += "<div><b>Interpretation :</b> <span id='dbgResultat'>-</span></div>";
  html += "<div><b>Action :</b> <span id='dbgAction'>-</span></div>";
  html += "</div></div>";
  html += "<button class='btn' type='submit'>Enregistrer</button></form>";
  html += "</div></div></body></html>";
  return html;
}

String searchPage(const String &target) {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Recherche</title><style>" + cssCommon() + "</style></head><body><div class='wrap'><a class='back' href='/advanced'>←</a><div class='card'><div class='title'>Recherche PM3D</div>";

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
      html += "<a class='btn' href='/add?target=" + target + "&mac=" + mac + "&ssid=" + ssid + "'>Ajouter</a></div>";
    }
  }

  if (!found) html += "<div class='hint'>Aucun accessoire PM3D detecte.</div>";
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
  // Important : pour que le telephone considere le Wi-Fi comme portail captif,
  // on ne renvoie pas le code attendu par Android/iOS/Windows.
  // On renvoie directement une vraie page HTML PM3D.
  handleIntro();
}

void handleCaptivePortalPage() {
  handleIntro();
}

void handleRoot() {
  addNoCacheHeaders();
  server.send(200, "text/html", rootPage());
}
void handleConfig() { server.send(200, "text/html", configPage()); }
void handleAdvanced() { server.send(200, "text/html", advancedPage()); }

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

void handleSaveAdvanced() {
  if (server.hasArg("rg")) gpioRougeGauche = server.arg("rg").toInt();
  if (server.hasArg("rd")) gpioRougeDroite = server.arg("rd").toInt();
  if (server.hasArg("ja")) gpioJaunePN = server.arg("ja").toInt();

  for (int i=0;i<4;i++) {
    macSecondaire[i] = argText("sec" + String(i));
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
  // DNS captif : tout nom de domaine tape ou teste par le telephone renvoie vers 192.168.4.1
  dnsServer.setTTL(0);
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
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
  server.on("/favicon.ico", [](){ server.send(204, "text/plain", ""); });
  server.on("/config", handleConfig);
  server.on("/advanced", handleAdvanced);
  server.on("/savewifi", handleSaveWifi);
  server.on("/setvoie", handleSetVoie);
  server.on("/togglemode", handleToggleMode);
  server.on("/toggle", handleToggle);
  server.on("/setlum", handleSetLum);
  server.on("/savesensorconfig", handleSaveSensorConfig);
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
