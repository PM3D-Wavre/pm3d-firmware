#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>

// =======================================================
// PM3D Sensor ESP-NOW + page test locale
// DO capteur sur GPIO 2
// Wi-Fi AP local : http://192.168.4.1
// =======================================================

static const uint32_t PM3D_MAGIC = 0x504D3344;
static const uint8_t PKT_SENSOR = 1;

const int SENSOR_PIN = 2;
const unsigned long DEBOUNCE_MS = 80;
const unsigned long HEARTBEAT_MS = 3000UL;

IPAddress apIp(192, 168, 4, 1);
IPAddress apGateway(192, 168, 4, 1);
IPAddress apSubnet(255, 255, 255, 0);

WebServer server(80);

String apSsid = "";
uint8_t apMac[6] = {0,0,0,0,0,0};

int idleRawState = LOW;

bool lastRawDetected = false;
bool stableDetected = false;
unsigned long lastChangeMs = 0;
unsigned long lastHeartbeatMs = 0;
uint32_t seq = 0;

uint8_t broadcastAddress[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint8_t type;
  uint8_t event; // 0 heartbeat, 1 on, 2 off
  uint8_t apMac[6];
  char suffix[5];
  uint32_t seq;
} Pm3dSensorPacket;

String macToString(const uint8_t mac[6]) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

String buildSsid() {
  esp_read_mac(apMac, ESP_MAC_WIFI_SOFTAP);
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%02X%02X", apMac[4], apMac[5]);
  return String("PM3D-Sensor ") + String(suffix);
}

bool readDetected() {
  int raw = digitalRead(SENSOR_PIN);
  return raw != idleRawState;
}

void sendEvent(uint8_t event) {
  Pm3dSensorPacket p;
  p.magic = PM3D_MAGIC;
  p.type = PKT_SENSOR;
  p.event = event;
  memcpy(p.apMac, apMac, 6);
  snprintf(p.suffix, sizeof(p.suffix), "%02X%02X", apMac[4], apMac[5]);
  p.seq = ++seq;

  esp_now_send(broadcastAddress, (uint8_t*)&p, sizeof(p));
  delay(8);
  esp_now_send(broadcastAddress, (uint8_t*)&p, sizeof(p));
  delay(8);
  esp_now_send(broadcastAddress, (uint8_t*)&p, sizeof(p));

  Serial.print("ESP-NOW event=");
  Serial.print(event);
  Serial.print(" MAC=");
  Serial.print(macToString(apMac));
  Serial.print(" suffix=");
  Serial.println(p.suffix);
}

void setupEspNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("Erreur ESP-NOW");
    return;
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastAddress, 6);
  peer.channel = 1;
  peer.encrypt = false;

  if (!esp_now_is_peer_exist(broadcastAddress)) {
    esp_now_add_peer(&peer);
  }
}

String sensorPageHtml() {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>PM3D Sensor</title>";
  html += "<style>";
  html += "body{margin:0;padding:12px;font-family:Arial,Helvetica,sans-serif;text-align:center;color:#F6FBFF;background:linear-gradient(180deg,#06111F,#02060C);}";
  html += ".card{max-width:420px;margin:20px auto;padding:18px;border-radius:20px;background:linear-gradient(180deg,#102946,#07182B);border:1px solid rgba(255,255,255,.16);box-shadow:0 18px 44px rgba(0,0,0,.35);}";
  html += ".title{font-size:24px;font-weight:900;margin-bottom:6px;}";
  html += ".sub{font-size:13px;color:#B9D1DE;margin-bottom:18px;}";
  html += ".dot{width:90px;height:90px;border-radius:50%;margin:18px auto;border:5px solid rgba(255,255,255,.75);}";
  html += ".red{background:#d80000;box-shadow:0 0 24px rgba(216,0,0,.85);}";
  html += ".green{background:#00c853;box-shadow:0 0 28px rgba(0,200,83,.9);}";
  html += ".state{font-size:22px;font-weight:900;margin:10px 0;}";
  html += ".info{font-size:13px;color:#D9ECF7;text-align:left;background:rgba(0,0,0,.20);border-radius:14px;padding:12px;margin-top:16px;line-height:1.7;}";
  html += ".hint{font-size:12px;color:#B9D1DE;margin-top:14px;}";
  html += "</style>";
  html += "<script>";
  html += "function upd(){fetch('/status').then(r=>r.json()).then(d=>{";
  html += "document.getElementById('dot').className='dot '+(d.detected?'green':'red');";
  html += "document.getElementById('state').innerText=d.detected?'CAPTE':'NE CAPTE PAS';";
  html += "document.getElementById('raw').innerText=d.raw;";
  html += "document.getElementById('idle').innerText=d.idle;";
  html += "}).catch(e=>{});}";
  html += "setInterval(upd,300);window.addEventListener('load',upd);";
  html += "</script></head><body>";
  html += "<div class='card'>";
  html += "<div class='title'>PM3D Sensor</div>";
  html += "<div class='sub'>Test local du capteur</div>";
  html += "<div id='dot' class='dot red'></div>";
  html += "<div id='state' class='state'>...</div>";
  html += "<div class='info'>";
  html += "<b>SSID :</b> " + apSsid + "<br>";
  html += "<b>IP :</b> 192.168.4.1<br>";
  html += "<b>MAC :</b> " + macToString(apMac) + "<br>";
  html += "<b>GPIO DO :</b> " + String(SENSOR_PIN) + "<br>";
  html += "<b>Etat brut GPIO :</b> <span id='raw'>-</span><br>";
  html += "<b>Etat repos calibre :</b> <span id='idle'>-</span>";
  html += "</div>";
  html += "<div class='hint'>Passez la main devant le capteur : le rond doit devenir vert.</div>";
  html += "</div></body></html>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html", sensorPageHtml());
}

void handleStatus() {
  int raw = digitalRead(SENSOR_PIN);
  bool detected = readDetected();

  String json = "{";
  json += "\"detected\":" + String(detected ? "true" : "false");
  json += ",\"raw\":\"" + String(raw == HIGH ? "HIGH" : "LOW") + "\"";
  json += ",\"idle\":\"" + String(idleRawState == HIGH ? "HIGH" : "LOW") + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleNotFound() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void setupWifi() {
  apSsid = buildSsid();

  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  delay(150);

  WiFi.mode(WIFI_AP_STA);
  delay(100);

  WiFi.softAPConfig(apIp, apGateway, apSubnet);
  WiFi.softAP(apSsid.c_str(), nullptr, 1, false);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
}

void updateSensor() {
  bool detected = readDetected();

  if (detected != lastRawDetected) {
    lastRawDetected = detected;
    lastChangeMs = millis();
  }

  if (millis() - lastChangeMs >= DEBOUNCE_MS && detected != stableDetected) {
    stableDetected = detected;

    Serial.print("Detection capteur = ");
    Serial.println(stableDetected ? "ON" : "OFF");

    sendEvent(stableDetected ? 1 : 2);
  }
}

void updateHeartbeat() {
  if (millis() - lastHeartbeatMs >= HEARTBEAT_MS) {
    lastHeartbeatMs = millis();
    sendEvent(0);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(SENSOR_PIN, INPUT);

  setupWifi();
  setupEspNow();

  delay(300);
  idleRawState = digitalRead(SENSOR_PIN);

  stableDetected = readDetected();
  lastRawDetected = stableDetected;
  lastChangeMs = millis();

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println();
  Serial.println("==============================");
  Serial.println("PM3D Sensor ESP-NOW + TEST WEB");
  Serial.print("SSID : "); Serial.println(apSsid);
  Serial.print("IP   : "); Serial.println(WiFi.softAPIP());
  Serial.print("MAC  : "); Serial.println(macToString(apMac));
  Serial.print("GPIO : "); Serial.println(SENSOR_PIN);
  Serial.print("Repos: "); Serial.println(idleRawState == HIGH ? "HIGH" : "LOW");
  Serial.println("Page : http://192.168.4.1/");
  Serial.println("==============================");

  sendEvent(stableDetected ? 1 : 2);
}

void loop() {
  server.handleClient();
  updateSensor();
  updateHeartbeat();
}
