#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#define FW_VERSION "1.2"
#define FW_BUILD_LABEL "D2"

void drawRetroRowsOnly(bool animated);
void drawSncbTft2010PhotoFix();
void drawSncbRailTime();
void drawSncb2023Photo();
void drawSncf2012Photo();
void normalizeTftOffsets();
void loadStyleTune(const String &profile);
void saveStyleTune(const String &profile);
String trKey(const String &key);
String profileCountry(const String &profile);
String countryFlag(const String &country);
String countryName(const String &country);
String profileLabel();
String styleTitle(const String &profile);
String profileAt(const String &order, int wanted);
String profileSelectOptions(const String &selected);
bool isFavoriteProfile(const String &profile);
String countryButton(const String &country);
int profileCount(const String &order);
String countryStyles(const String &country);
String tablePreviewHtml(const String &profile, const String &country);
String styleConfigPage();
void handleStyleConfig();
void handleSaveStyleConfig();
void handleSaveAdvanced();
String stepperScript();
String stepperControl(const String &label, const String &name, int value, int minVal, int maxVal, int step);
void drawAdvancedSettingsPopup();

// =====================================================
// PM3D DISPLAY - ESP32-C3 + ST7789 1.14" 135x240
// Anti-scintillement + nombre de lignes dynamique
// Base affichage conservee - bibliotheque web vide, avec styles Europe complets ajoutes manuellement
//
// C3 <-> ST7789: SCK=GPIO4, SDA/MOSI=GPIO3, DC=GPIO20, RST=GPIO10, CS=GPIO6, BLK=GPIO5
// =====================================================

// ====== COULEURS RGB565 ======
#define C_BLACK       0x0000
#define C_WHITE       0xFFFF
#define C_YELLOW      0xFFE0
#define C_SNCB_YELLOW 0xFEE0
#define C_BLUE_TOP    0x035F
#define C_BLUE_DARK   0x010B
#define C_BLUE_ROW1   0x1A30
#define C_BLUE_ROW2   0x2A95
#define C_GRID        0x8C71
#define C_CYAN        0x07FF
#define C_RED         0xF800
#define C_GREEN       0x07E0
#define C_GREY        0x8410
#define C_AMBER       0xFFE0
#define C_FLAP_BG     0x1082
#define C_FLAP_CELL   0x3186

// ====== TFT ======
#define TFT_SCK   4
#define TFT_MOSI  3
#define TFT_DC    20
#define TFT_RST   10
#define TFT_CS    6
#define TFT_BL    5

Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, -1);
Arduino_GFX *gfx = nullptr;

// ====== WIFI / WEB ======
WebServer server(80);
Preferences prefs;
String apSSID;
String apPass = "";
String uiTheme = "blue";
String displayProfile = "fr_sncf_2012";
String currentLang = "FR";
String localWifiSsid = "";
String localWifiPass = "";
String transilienFrame = "orange";
String transilienAffluence = "Moyenne";
String transilienDirection = "Direction Meaux";
String transilienStops = "Chelles, Vaires-Torcy, Lagny-Thorigny, Esbly, Meaux";
String transilienInfoTitle = "Infos Travaux";
String transilienInfoText = "Travaux a Villeneuve le Roi, l'arret est supprime. Bus de remplacement entre Ablon et Choisy le Roi. Plus d'informations: Transilien.com";
int sncbGridCount = 15;
String dbIntercityMessage = "Bitte beachten Sie die Anzeige am Bahnsteig";
int dbIntercityClockHour = 14;
int dbIntercityClockMinute = 43;
String db2010Title = "Abfahrt Bochum Hbf";
int db2010TitleFontSize = 2;
String favoriteProfiles = "";
String orderFr = "";
String orderBe = "";
String orderDe = "";
String orderCh = "";
String orderUk = "";
String orderAt = "";
String orderNl = "";
String orderIt = "";
String orderEs = "";
String orderOther = "";
bool userRowsEdited = false;  // true si le tableau a ete modifie manuellement
// ====== DATA ======
#define MAX_ROWS 16

struct Row {
  String heure;
  String retard;
  String info;
  String destination;
  String typeTrain;
  String voie;
};

Row rows[MAX_ROWS];


bool rowIsFilled(int idx) {
  if (idx < 0 || idx >= MAX_ROWS) return false;
  return rows[idx].heure.length() || rows[idx].retard.length() || rows[idx].info.length() || rows[idx].destination.length() || rows[idx].typeTrain.length() || rows[idx].voie.length();
}

int filledRowCount() {
  int n = 0;
  for (int i = 0; i < MAX_ROWS; i++) if (rowIsFilled(i)) n++;
  return n;
}

int visibleRowIndex(int pos) {
  int total = filledRowCount();
  if (total <= 0) return pos % MAX_ROWS;
  int wanted = pos % total;
  for (int i = 0; i < MAX_ROWS; i++) {
    if (!rowIsFilled(i)) continue;
    if (wanted == 0) return i;
    wanted--;
  }
  return 0;
}

int nbVisible = 8;
int screenMode = 0;    // 0 SNCB nouveau, 1 SNCB ancien, 2 retro aeroport, 3 SNCF nouveau, 4 SNCF ancien
int scrollOffset = 0;

bool fullRedrawNeeded = true;
bool rowsOnlyPass = false;
unsigned long lastScroll = 0;
unsigned long lastRetro = 0;
unsigned long lastDbClock = 0;
const unsigned long SCROLL_DELAY = 2600;
const unsigned long RETRO_DELAY  = 55;
int scrollDelayMs = 2600;
int screenBrightness = 255;
int displayFontSize = 1;  // Police des textes du tableau TFT : 1=petit, 2=moyen, 3=grand
String badenSteig = "1a";
int tftOffsetX1 = 52;
int tftOffsetY1 = 40;
int tftOffsetX2 = 53;
int tftOffsetY2 = 40;
int tftPanelW = 135;
int tftPanelH = 240;
int styleFontSize = 0;    // 0 = taille globale
int styleMoveX = 0;
int styleMoveY = 0;
int styleTimeX = 0;
int styleInfoX = 0;
int styleDestX = 0;
int styleTrainX = 0;
int styleVoieX = 0;

bool retroAnimating = false;
int retroLine = 0;
int retroCharIndex = 0;
int retroSpinStep = 0;
String retroDisplayed[MAX_ROWS];

// ====== UTILS ======
void getHardwareMac(uint8_t mac[6]) {
  // MAC eFuse materielle de l'ESP32 : stable, non aleatoire, conservee apres redemarrage/OTA.
  // On n'utilise PAS WiFi.macAddress(), car elle peut dependre du mode Wi-Fi AP/STA.
  uint64_t efuse = ESP.getEfuseMac();
  mac[0] = (uint8_t)(efuse >> 40);
  mac[1] = (uint8_t)(efuse >> 32);
  mac[2] = (uint8_t)(efuse >> 24);
  mac[3] = (uint8_t)(efuse >> 16);
  mac[4] = (uint8_t)(efuse >> 8);
  mac[5] = (uint8_t)(efuse);
}

String macSuffix() {
  uint8_t mac[6];
  getHardwareMac(mac);
  char b[5];
  snprintf(b, sizeof(b), "%02X%02X", mac[4], mac[5]);
  return String(b);
}

IPAddress stableApIPFromMac() {
  // IP volontairement FIXE pour que lÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢interface soit toujours accessible.
  // Le suffixe unique reste uniquement dans le SSID : PM3D-Display-XXXX,
  // avec XXXX = fin du vrai MAC materiel.
  return IPAddress(192, 168, 4, 1);
}

String apUrl() {
  IPAddress ip = WiFi.softAPIP();
  if (ip[0] == 0) ip = stableApIPFromMac();
  return String("http://") + ip.toString();
}

String htmlEscape(String s) {
  s.replace("&", "&amp;");
  s.replace("\"", "&quot;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  return s;
}

String urlEncode(String s) {
  String out;
  const char *hex = "0123456789ABCDEF";
  for (int i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

String cutText(String s, int maxLen) {
  if ((int)s.length() <= maxLen) return s;
  if (maxLen <= 1) return s.substring(0, maxLen);
  return s.substring(0, maxLen - 1) + ".";
}

String marqueeWindow(const String &text, int maxChars, int speedDiv) {
  if ((int)text.length() <= maxChars) return text;
  String padded = text + "   ";
  int start = (millis() / speedDiv) % padded.length();
  String out;
  for (int i = 0; i < maxChars; i++) out += padded[(start + i) % padded.length()];
  return out;
}

void resetRetroDisplay() {
  for (int i = 0; i < MAX_ROWS; i++) {
    retroDisplayed[i] = "";
  }
  retroLine = 0;
  retroCharIndex = 0;
  retroSpinStep = 0;
  retroAnimating = true;

  if (screenMode == 2) {
    drawRetroRowsOnly(true);
  }
}

String retroBuildVisibleText(int visibleRow, int idx, int maxLen) {
  String target = cutText(rows[idx].destination, maxLen);

  if (visibleRow < retroLine) {
    return target;
  }

  if (visibleRow > retroLine) {
    return "";
  }

  String out = "";
  for (int i = 0; i < retroCharIndex && i < (int)target.length(); i++) {
    out += target[i];
  }
  return out;
}

int profileMaxVisibleRows(const String &profile) {
  if (profile == "fr_transilien") return 3;
  if (profile == "fr_sncf_first") return 5;
  if (profile == "fr_sncf_old_led") return 6;
  if (profile == "fr_sncf_arrivals") return 7;
  if (profile == "fr_sncf_2012") return 8;
  if (profile == "fr_rer_a") return 8;
  if (profile == "fr_saint_lazare") return 10;
  if (profile == "fr_transilien_p") return 8;
  if (profile == "fr_transilien_2016") return 8;
  if (profile == "be_sncb_detail_list") return 4;
  if (profile == "be_sncb_grid") return 12;
  if (profile == "be_sncb_modern") return 8;
  if (profile == "be_sncb_detail") return 6;
  if (profile == "de_db_large_blue") return 10;
  if (profile == "de_db_intercity") return MAX_ROWS;
  if (profile == "de_db_2010_2015") return MAX_ROWS;
  if (profile == "de_db_2022") return MAX_ROWS;
  if (profile == "de_db_modern") return 7;
  if (profile == "de_db_blue") return 7;
  if (profile == "de_baden_baden") return 6;
  if (profile == "de_bvg_blue") return 8;
  if (profile == "ch_zurich_fern") return 11;
  if (profile == "ch_bern_arrival") return 12;
  if (profile == "ch_sbb_romandie") return 10;
  if (profile == "ch_sbb_blue") return 10;
  if (profile == "uk_modern") return 4;
  if (profile == "uk_splitflap") return 6;
  if (profile == "uk_sheffield") return 7;
  if (profile == "at_oebb_white") return 5;
  if (profile == "at_oebb_blue") return 9;
  if (profile == "at_oebb_green") return 9;
  if (profile == "at_oebb_dense") return 9;
  if (profile == "at_oebb_teal") return 7;
  if (profile == "nl_ns_light") return 6;
  if (profile == "it_naples_amber") return 8;
  if (profile == "it_fs_blue") return 8;
  if (profile == "hu_mav_arrivals") return 8;
  if (profile == "hu_mav_departures") return 8;
  if (profile == "se_stockholm") return 8;
  if (profile == "jp_jr_led") return 8;
  if (profile == "jp_tokyo_grey") return 8;
  if (profile == "es_barcelona_grid") return 8;
  if (profile == "es_barcelona_adif") return 7;
  if (profile == "pl_pkp_departures") return 7;
  if (profile == "pl_pkp_arrivals") return 7;
  if (profile == "us_la_metro") return 8;
  if (profile == "in_indian_railways") return 8;
  if (profile == "nz_britomart") return 7;
  return 10;
}

int profileDefaultVisibleRows(const String &profile) {
  if (profile == "de_db_intercity") return 3;
  if (profile == "de_db_2010_2015") return 5;
  if (profile == "de_db_2022") return 4;
  return profileMaxVisibleRows(profile);
}

void normalizeSettings() {
  int maxRows = profileMaxVisibleRows(displayProfile);
  if (nbVisible < 3) nbVisible = 3;
  if (nbVisible > maxRows) nbVisible = maxRows;
  if (scrollDelayMs < 500) scrollDelayMs = 500;
  if (scrollDelayMs > 15000) scrollDelayMs = 15000;
  if (screenBrightness < 20) screenBrightness = 20;
  if (screenBrightness > 255) screenBrightness = 255;
  if (displayFontSize < 1) displayFontSize = 1;
  if (displayFontSize > 3) displayFontSize = 3;
  badenSteig.trim();
  if (badenSteig.length() == 0) badenSteig = "1a";
  if (badenSteig.length() > 6) badenSteig = badenSteig.substring(0, 6);
  dbIntercityMessage.trim();
  if (dbIntercityMessage.length() == 0) dbIntercityMessage = "Bitte beachten Sie die Anzeige am Bahnsteig";
  if (dbIntercityMessage.length() > 96) dbIntercityMessage = dbIntercityMessage.substring(0, 96);
  dbIntercityClockHour = constrain(dbIntercityClockHour, 0, 23);
  dbIntercityClockMinute = constrain(dbIntercityClockMinute, 0, 59);
  db2010Title.trim();
  if (db2010Title.length() == 0) db2010Title = "Abfahrt Bochum Hbf";
  if (db2010Title.length() > 36) db2010Title = db2010Title.substring(0, 36);
  db2010TitleFontSize = constrain(db2010TitleFontSize, 1, 3);
  if (screenMode < 0 || screenMode > 4) screenMode = 0;
}

void loadDefaults() {
  rows[0]  = {"11:24", "+7", "Courtrai via Bruxelles, Tournai", "IC", "4"};
  rows[1]  = {"11:37", "", "Saint-Nicolas via Termonde, Lokeren", "IC", "3"};
  rows[2]  = {"11:40", "", "Alost via Bruxelles, Jette", "S10", "4"};
  rows[3]  = {"11:47", "", "Termonde via Bruxelles-Ouest", "S10", "2"};
  rows[4]  = {"11:58", "", "Termonde", "S3", "3"};
  rows[5]  = {"12:02", "", "Zottegem via Denderleeuw", "S3", "4"};
  rows[6]  = {"12:13", "", "Malines via Vilvorde", "S4", "2"};
  rows[7]  = {"12:16", "", "Alost via Liedekerke", "S10", "1"};
  rows[8]  = {"12:21", "", "Braine-le-Comte", "IC", "3"};
  rows[9]  = {"12:24", "", "Brussels Airport-Zaventem", "S19", "4"};
  rows[10] = {"12:31", "", "Anvers-Central", "IC", "6"};
  rows[11] = {"12:35", "", "Liege-Guillemins", "IC", "5"};
  rows[12] = {"12:40", "", "Knokke via Gand", "IC", "4"};
  rows[13] = {"12:44", "", "Quievrain via Mons", "IC", "3"};
  rows[14] = {"12:52", "", "Namur via Ottignies", "IC", "2"};
  rows[15] = {"12:58", "", "Louvain", "S2", "5"};
}

void loadConfig() {
  prefs.begin("pm3ddisp", true);

  nbVisible = prefs.getInt("nb", 8);
  scrollDelayMs = prefs.getInt("scrollMs", 2600);
  screenBrightness = prefs.getInt("bright", 255);
  displayFontSize = prefs.getInt("font", 1);
  tftOffsetX1 = prefs.getInt("ox1", 52);
  tftOffsetY1 = prefs.getInt("oy1", 40);
  tftOffsetX2 = prefs.getInt("ox2", 53);
  tftOffsetY2 = prefs.getInt("oy2", 40);
  tftPanelW = prefs.getInt("pw", 135);
  tftPanelH = prefs.getInt("ph", 240);
  screenMode = prefs.getInt("mode", 0);
  uiTheme = prefs.getString("theme", "blue");
  displayProfile = prefs.getString("profile", "fr_sncf_2012");
  currentLang = prefs.getString("lang", "FR");
  userRowsEdited = prefs.getBool("rowsEdited", false);
  localWifiSsid = prefs.getString("staSsid", "");
  localWifiPass = prefs.getString("staPass", "");
  apPass = prefs.getString("apPass", "");
  transilienFrame = prefs.getString("trFrame", "orange");
  transilienAffluence = prefs.getString("trAff", "Moyenne");
  transilienDirection = prefs.getString("trDir", "Direction Meaux");
  transilienStops = prefs.getString("trStops", "Chelles, Vaires-Torcy, Lagny-Thorigny, Esbly, Meaux");
  transilienInfoTitle = prefs.getString("trInfoT", "Infos Travaux");
  transilienInfoText = prefs.getString("trInfoX", "Travaux a Villeneuve le Roi, l'arret est supprime. Bus de remplacement entre Ablon et Choisy le Roi. Plus d'informations: Transilien.com");
  sncbGridCount = prefs.getInt("sncbGrid", 15);
  dbIntercityMessage = prefs.getString("dbicMsg", "Bitte beachten Sie die Anzeige am Bahnsteig");
  dbIntercityClockHour = prefs.getInt("dbicH", 14);
  dbIntercityClockMinute = prefs.getInt("dbicM", 43);
  db2010Title = prefs.getString("db10Title", "Abfahrt Bochum Hbf");
  db2010TitleFontSize = prefs.getInt("db10Font", 2);
  badenSteig = prefs.getString("badenSt", "1a");
  favoriteProfiles = prefs.getString("favProf", "");
  orderFr = prefs.getString("ordFr", "");
  orderBe = prefs.getString("ordBe", "");
  orderDe = prefs.getString("ordDe", "");
  orderCh = prefs.getString("ordCh", "");
  orderUk = prefs.getString("ordUk", "");
  orderAt = prefs.getString("ordAt", "");
  orderNl = prefs.getString("ordNl", "");
  orderIt = prefs.getString("ordIt", "");
  orderEs = prefs.getString("ordEs", "");
  orderOther = prefs.getString("ordOther", "");
  bool saved = prefs.getBool("saved", false);

  if (!saved) {
    prefs.end();
    loadDefaults();
    normalizeSettings();
    return;
  }

  for (int i = 0; i < MAX_ROWS; i++) {
    rows[i].heure       = prefs.getString(("h" + String(i)).c_str(), "");
    rows[i].retard      = prefs.getString(("r" + String(i)).c_str(), "");
    rows[i].info        = prefs.getString(("i" + String(i)).c_str(), "");
    rows[i].destination = prefs.getString(("d" + String(i)).c_str(), "");
    rows[i].typeTrain   = prefs.getString(("t" + String(i)).c_str(), "");
    rows[i].voie        = prefs.getString(("v" + String(i)).c_str(), "");
  }

  prefs.end();
  normalizeSettings();
  normalizeTftOffsets();
  loadStyleTune(displayProfile);
}

void saveConfig() {
  normalizeSettings();
  prefs.begin("pm3ddisp", false);

  prefs.putBool("saved", true);
  prefs.putInt("nb", nbVisible);
  prefs.putInt("scrollMs", scrollDelayMs);
  prefs.putInt("bright", screenBrightness);
  prefs.putInt("font", displayFontSize);
  prefs.putInt("ox1", tftOffsetX1);
  prefs.putInt("oy1", tftOffsetY1);
  prefs.putInt("ox2", tftOffsetX2);
  prefs.putInt("oy2", tftOffsetY2);
  prefs.putInt("pw", tftPanelW);
  prefs.putInt("ph", tftPanelH);
  prefs.putInt("mode", screenMode);
  prefs.putString("theme", uiTheme);
  prefs.putString("profile", displayProfile);
  prefs.putString("lang", currentLang);
  prefs.putBool("rowsEdited", userRowsEdited);
  prefs.putString("staSsid", localWifiSsid);
  prefs.putString("staPass", localWifiPass);
  prefs.putString("trFrame", transilienFrame);
  prefs.putString("trAff", transilienAffluence);
  prefs.putString("trDir", transilienDirection);
  prefs.putString("trStops", transilienStops);
  prefs.putString("trInfoT", transilienInfoTitle);
  prefs.putString("trInfoX", transilienInfoText);
  prefs.putInt("sncbGrid", sncbGridCount);
  prefs.putString("dbicMsg", dbIntercityMessage);
  prefs.putInt("dbicH", dbIntercityClockHour);
  prefs.putInt("dbicM", dbIntercityClockMinute);
  prefs.putString("db10Title", db2010Title);
  prefs.putInt("db10Font", db2010TitleFontSize);
  prefs.putString("badenSt", badenSteig);
  prefs.putString("favProf", favoriteProfiles);
  prefs.putString("ordFr", orderFr);
  prefs.putString("ordBe", orderBe);
  prefs.putString("ordDe", orderDe);
  prefs.putString("ordCh", orderCh);
  prefs.putString("ordUk", orderUk);
  prefs.putString("ordAt", orderAt);
  prefs.putString("ordNl", orderNl);
  prefs.putString("ordIt", orderIt);
  prefs.putString("ordEs", orderEs);
  prefs.putString("ordOther", orderOther);
  for (int i = 0; i < MAX_ROWS; i++) {
    prefs.putString(("h" + String(i)).c_str(), rows[i].heure);
    prefs.putString(("r" + String(i)).c_str(), rows[i].retard);
    prefs.putString(("i" + String(i)).c_str(), rows[i].info);
    prefs.putString(("d" + String(i)).c_str(), rows[i].destination);
    prefs.putString(("t" + String(i)).c_str(), rows[i].typeTrain);
    prefs.putString(("v" + String(i)).c_str(), rows[i].voie);
  }

  prefs.end();
}

void normalizeTftOffsets() {
  tftOffsetX1 = constrain(tftOffsetX1, 0, 120);
  tftOffsetY1 = constrain(tftOffsetY1, 0, 120);
  tftOffsetX2 = constrain(tftOffsetX2, 0, 120);
  tftOffsetY2 = constrain(tftOffsetY2, 0, 120);
  tftPanelW = constrain(tftPanelW, 120, 240);
  tftPanelH = constrain(tftPanelH, 120, 260);
}

String profileTuneSuffix(const String &profile) {
  uint16_t h = 21661;
  for (int i = 0; i < profile.length(); i++) h = (uint16_t)((h ^ profile[i]) * 16719);
  String s = String(h, HEX);
  while (s.length() < 4) s = "0" + s;
  return s;
}

int tunedFontSize() {
  return styleFontSize > 0 ? styleFontSize : displayFontSize;
}

int colTune(int col) {
  if (col == 0) return styleTimeX;
  if (col == 1) return styleInfoX;
  if (col == 2) return styleDestX;
  if (col == 3) return styleTrainX;
  if (col == 4) return styleVoieX;
  return 0;
}

int sx(int x) { return x + styleMoveX; }
int sxCol(int x, int col) { return x + colTune(col); }
int sy(int y) { return y + styleMoveY; }

void loadStyleTune(const String &profile) {
  String k = profileTuneSuffix(profile);
  prefs.begin("pm3ddisp", true);
  styleFontSize = prefs.getInt(("sf" + k).c_str(), 0);
  styleMoveX = prefs.getInt(("sx" + k).c_str(), 0);
  styleMoveY = prefs.getInt(("sy" + k).c_str(), 0);
  styleTimeX = prefs.getInt(("st" + k).c_str(), 0);
  styleInfoX = prefs.getInt(("si" + k).c_str(), 0);
  styleDestX = prefs.getInt(("sd" + k).c_str(), 0);
  styleTrainX = prefs.getInt(("sr" + k).c_str(), 0);
  styleVoieX = prefs.getInt(("sv" + k).c_str(), 0);
  prefs.end();
}

void saveStyleTune(const String &profile) {
  String k = profileTuneSuffix(profile);
  prefs.begin("pm3ddisp", false);
  prefs.putInt(("sf" + k).c_str(), styleFontSize);
  prefs.putInt(("sx" + k).c_str(), styleMoveX);
  prefs.putInt(("sy" + k).c_str(), styleMoveY);
  prefs.putInt(("st" + k).c_str(), styleTimeX);
  prefs.putInt(("si" + k).c_str(), styleInfoX);
  prefs.putInt(("sd" + k).c_str(), styleDestX);
  prefs.putInt(("sr" + k).c_str(), styleTrainX);
  prefs.putInt(("sv" + k).c_str(), styleVoieX);
  prefs.end();
}
void smallText(int x, int y, uint16_t color, const String &txt) {
  gfx->setTextSize(tunedFontSize());
  gfx->setTextColor(color);
  gfx->setCursor(sx(x), sy(y));
  gfx->print(txt);
}

void mediumText(int x, int y, uint16_t color, const String &txt) {
  gfx->setTextSize(tunedFontSize() + 1);
  gfx->setTextColor(color);
  gfx->setCursor(sx(x), sy(y));
  gfx->print(txt);
}

void colSmallText(int col, int x, int y, uint16_t color, const String &txt) {
  smallText(sxCol(x, col), y, color, txt);
}

void colMediumText(int col, int x, int y, uint16_t color, const String &txt) {
  mediumText(sxCol(x, col), y, color, txt);
}

void scrollingCellText(int col, int x, int y, int w, int h, uint8_t size, uint16_t fg, uint16_t bg, const String &txt, int speedDiv) {
  int charW = max(1, 6 * (int)size);
  int maxChars = max(1, (w - 2) / charW);
  String out = txt.length() > maxChars ? marqueeWindow(txt, maxChars, speedDiv) : txt;
  gfx->fillRect(sxCol(x, col), sy(y), w, h, bg);
  gfx->setTextWrap(false);
  gfx->setTextSize(size);
  gfx->setTextColor(fg, bg);
  gfx->setCursor(sxCol(x + 1, col), sy(y + max(0, (h - 8 * (int)size) / 2)));
  gfx->print(out);
}

void clippedCellText(int col, int x, int y, int w, int h, uint8_t size, uint16_t fg, uint16_t bg, const String &txt) {
  int charW = max(1, 6 * (int)size);
  int maxChars = max(1, (w - 2) / charW);
  String out = cutText(txt, maxChars);
  gfx->fillRect(sxCol(x, col), sy(y), w, h, bg);
  gfx->setTextWrap(false);
  gfx->setTextSize(size);
  gfx->setTextColor(fg, bg);
  gfx->setCursor(sxCol(x + 1, col), sy(y + max(0, (h - 8 * (int)size) / 2)));
  gfx->print(out);
}

void drawDbLogo(int x, int y, int w, int h) {
  gfx->fillRect(x, y, w, h, C_WHITE);
  gfx->drawRect(x, y, w, h, C_RED);
  gfx->setTextWrap(false);
  gfx->setTextSize(1);
  gfx->setTextColor(C_RED, C_WHITE);
  int tx = x + max(2, (w - 12) / 2);
  int ty = y + max(1, (h - 8) / 2);
  gfx->setCursor(sx(tx), sy(ty));
  gfx->print("DB");
}

void drawDbLogo2010(int x, int y, int w, int h) {
  drawDbLogo(x, y, w, h);
}

void drawOebbLogo(int x, int y, uint16_t color, uint16_t bg) {
  gfx->fillRect(sx(x), sy(y), 25, 9, bg);
  gfx->drawCircle(sx(x + 4), sy(y + 4), 4, color);
  gfx->drawCircle(sx(x + 4), sy(y + 4), 3, color);
  gfx->drawLine(sx(x + 1), sy(y + 1), sx(x + 4), sy(y + 1), bg);
  gfx->drawLine(sx(x + 1), sy(y + 2), sx(x + 3), sy(y + 2), bg);
  gfx->drawLine(sx(x + 3), sy(y + 3), sx(x + 7), sy(y), color);
  gfx->drawPixel(sx(x + 4), sy(y + 2), color);
  gfx->setTextWrap(false);
  gfx->setTextSize(1);
  gfx->setTextColor(color, bg);
  gfx->setCursor(sx(x + 10), sy(y + 1));
  gfx->print("BB");
}

String oebbCompanyForRow(const Row &r) {
  String tag = r.info;
  int sep = tag.indexOf("|");
  if (sep > 0) {
    tag = tag.substring(0, sep);
    tag.trim();
    tag.toUpperCase();
    if (tag == "SLB" || tag == "GKB") return tag;
  }
  String probe = r.typeTrain + " " + r.destination + " " + r.info;
  probe.toUpperCase();
  if (probe.indexOf("KOEFLACH") >= 0 || probe.indexOf("WIES") >= 0) return "GKB";
  if (probe.indexOf("SALZBURG") >= 0 && (probe.indexOf("S1") >= 0 || probe.indexOf("S11") >= 0)) return "SLB";
  return "OEBB";
}

String oebbCleanInfo(const String &info) {
  int sep = info.indexOf("|");
  if (sep > 0) return info.substring(sep + 1);
  return info;
}

String delayText(const Row &r) {
  String out = r.retard;
  out.trim();
  return out;
}

String oebbTrackText(const Row &r) {
  String v = r.voie;
  v.trim();
  v.toUpperCase();
  if (v == "BUS") return "";
  String out = "";
  for (int i = 0; i < v.length(); i++) {
    char c = v[i];
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || c == '-') out += c;
  }
  return out;
}

void drawRailCompanyBox(int x, int y, int w, int h, const String &company, uint16_t bg) {
  gfx->fillRect(sx(x), sy(y), w, h, C_WHITE);
  String c = company;
  c.toUpperCase();
  if (c == "SLB") {
    gfx->setTextSize(1);
    gfx->setTextColor(0x001F, C_WHITE);
    gfx->setCursor(sx(x + max(1, (w - 18) / 2)), sy(y + 1));
    gfx->print("SL");
    gfx->setTextColor(C_RED, C_WHITE);
    gfx->print("B");
  } else if (c == "GKB") {
    gfx->fillRect(sx(x + 1), sy(y + 1), w - 2, h - 2, 0xE7E0);
    gfx->drawCircle(sx(x + 5), sy(y + h / 2), min(4, h / 2), 0x05C0);
    gfx->drawLine(sx(x + 2), sy(y + h - 2), sx(x + 9), sy(y + 2), 0x001F);
    gfx->setTextSize(1);
    gfx->setTextColor(0x05C0, 0xE7E0);
    gfx->setCursor(sx(x + 11), sy(y + 1));
    gfx->print("G");
  } else {
    drawOebbLogo(x + max(0, (w - 25) / 2), y, C_RED, C_WHITE);
  }
}

void drawOebbStatusDot(int x, int y, const Row &r, uint16_t bg) {
  String probe = r.voie + " " + r.info;
  probe.toUpperCase();
  uint16_t col = C_WHITE;
  if (probe.indexOf("BUS") >= 0 || probe.indexOf("BAU") >= 0 || probe.indexOf("ERSATZ") >= 0) col = C_YELLOW;
  else if (r.info.length() > 0) col = 0x07E0;
  gfx->fillCircle(sx(x), sy(y), 3, col);
  gfx->drawCircle(sx(x), sy(y), 3, bg == C_WHITE ? 0x2945 : C_WHITE);
}

void pseudoItalicText(int x, int y, uint8_t size, uint16_t fg, uint16_t bg, const String &txt) {
  gfx->setTextWrap(false);
  gfx->setTextSize(size);
  gfx->setTextColor(fg, bg);
  int step = 6 * size;
  for (int i = 0; i < (int)txt.length(); i++) {
    gfx->setCursor(sx(x + i * step + (i % 2)), sy(y));
    gfx->print(txt[i]);
  }
}

int getListTop() {
  if (screenMode == 0) return 34;
  if (screenMode == 1) return 21;
  if (screenMode == 2) return 25;
  if (screenMode == 3) return 32;
  return 22;
}

int getListHeight() {
  if (screenMode == 0) return 89;
  if (screenMode == 1) return 102;
  if (screenMode == 2) return 99;
  if (screenMode == 3) return 91;
  return 101;
}

int getRowHeight() {
  int h = getListHeight() / nbVisible;
  if (h < 5) h = 5;
  return h;
}

bool useBigText() {
  return getRowHeight() >= 16;
}
String tftFrameTitle() {
  if (displayProfile.startsWith("fr_")) return "SNCF DEPART";
  if (displayProfile.startsWith("be_")) return "SNCB NMBS";
  if (displayProfile.startsWith("de_")) return "DB ABFAHRT";
  if (displayProfile.startsWith("ch_")) return "CFF SBB CFF";
  if (displayProfile.startsWith("uk_")) return "DEPARTURES";
  if (displayProfile.startsWith("at_")) return "OBB ABFAHRT";
  if (displayProfile.startsWith("nl_")) return "NS VERTREK";
  if (displayProfile.startsWith("it_")) return "FS PARTENZE";
  if (displayProfile.startsWith("es_")) return "RENFE SALIDAS";
  return "PM3D DEPART";
}

String tftFramePlace() {
  if (displayProfile.startsWith("fr_")) return "Paris-Est";
  if (displayProfile.startsWith("be_")) return "Bruxelles-Central";
  if (displayProfile.startsWith("de_")) return "Berlin Hbf";
  if (displayProfile.startsWith("ch_")) return "Zurich HB";
  if (displayProfile.startsWith("uk_")) return "London";
  if (displayProfile.startsWith("at_")) return "Wien Hbf";
  if (displayProfile.startsWith("nl_")) return "Amsterdam C.";
  if (displayProfile.startsWith("it_")) return "Roma Termini";
  if (displayProfile.startsWith("es_")) return "Madrid Atocha";
  return "Gare";
}

// =====================================================
// =====================================================
void drawModernFrame() {
  gfx->fillScreen(C_BLACK);

  gfx->fillRect(3, 3, 234, 129, C_BLUE_DARK);
  gfx->drawRect(3, 3, 234, 129, C_GRID);

  gfx->fillRect(4, 4, 232, 17, C_BLUE_TOP);
  gfx->drawFastHLine(4, 21, 232, C_GRID);

  smallText(8, 9, C_WHITE, tftFrameTitle());
  smallText(86, 9, C_WHITE, tftFramePlace());

  gfx->drawCircle(224, 12, 6, C_WHITE);
  smallText(221, 9, C_WHITE, "B");

  gfx->fillRect(4, 22, 232, 11, C_BLUE_DARK);
  smallText(8, 25, C_WHITE, "Heure");
  smallText(47, 25, C_WHITE, "Destination");
  smallText(184, 25, C_WHITE, "Tr");
  smallText(220, 25, C_WHITE, "V");
  gfx->drawFastHLine(4, 33, 232, C_GRID);

  gfx->fillRect(4, 124, 232, 8, C_BLACK);
  smallText(8, 125, C_CYAN, "PM3D.NET");
}

void drawModernRowsOnly() {
  int listTop = getListTop();
  int listHeight = getListHeight();
  int rowHeight = getRowHeight();
  bool big = useBigText();

  gfx->fillRect(4, listTop, 232, listHeight, C_BLUE_DARK);

  for (int i = 0; i < nbVisible; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = listTop + i * rowHeight;
    uint16_t bg = (i % 2 == 0) ? C_BLUE_ROW1 : C_BLUE_ROW2;

    gfx->fillRect(4, y, 232, rowHeight, bg);
    gfx->drawFastHLine(4, y + rowHeight - 1, 232, C_GRID);

    if (big) {
      colSmallText(0, 8, y + 1, C_WHITE, rows[idx].heure);
      colMediumText(2, 47, y + 1, C_SNCB_YELLOW, cutText(rows[idx].destination, 10));
      colSmallText(3, 184, y + 2, C_WHITE, cutText(rows[idx].typeTrain, 3));
      colMediumText(4, 215, y + 1, C_WHITE, cutText(rows[idx].voie, 2));
    } else {
      colSmallText(0, 8, y + 2, C_WHITE, rows[idx].heure);

      int destX = 47;
      if (rows[idx].info.length() > 0) {
        gfx->fillRect(35, y + 1, 18, 8, C_RED);
        colSmallText(1, 37, y + 2, C_WHITE, cutText(rows[idx].info, 3));
        destX = 57;
      }

      int maxDest = (nbVisible <= 6) ? 21 : (nbVisible <= 8 ? 19 : 17);
      colSmallText(2, destX, y + 2, C_SNCB_YELLOW, cutText(rows[idx].destination, maxDest));
      colSmallText(3, 184, y + 2, C_WHITE, cutText(rows[idx].typeTrain, 3));
      colSmallText(4, 222, y + 2, C_WHITE, cutText(rows[idx].voie, 2));
    }
  }
}

// =====================================================
// MODE 1 : ANCIEN ECRAN SNCB
// =====================================================
void drawOldFrame() {
  gfx->fillScreen(C_BLACK);
  gfx->drawRect(2, 2, 236, 131, C_GREY);

  smallText(8, 6, C_WHITE, tftFrameTitle());
  smallText(48, 6, C_WHITE, "DESTINATION");
  smallText(180, 6, C_WHITE, "TYPE");
  smallText(215, 6, C_WHITE, "V");
  gfx->drawFastHLine(4, 18, 232, C_GREY);

  gfx->fillRect(4, 124, 232, 8, C_BLACK);
  smallText(8, 125, C_WHITE, "VERTREK / DEPART");
  smallText(172, 125, C_WHITE, "PM3D");
}

void drawOldRowsOnly() {
  int listTop = getListTop();
  int listHeight = getListHeight();
  int rowHeight = getRowHeight();
  bool big = useBigText();

  gfx->fillRect(4, listTop, 232, listHeight, C_BLACK);

  for (int i = 0; i < nbVisible; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = listTop + i * rowHeight;

    gfx->fillRect(4, y, 232, rowHeight, C_FLAP_BG);
    gfx->drawFastHLine(4, y + rowHeight - 1, 232, C_FLAP_CELL);

    for (int x = 44; x < 172; x += 8) {
      gfx->drawFastVLine(x, y, rowHeight, C_FLAP_CELL);
    }

    if (big) {
      colSmallText(0, 8, y + 2, C_AMBER, rows[idx].heure);
      colMediumText(2, 48, y + 1, C_AMBER, cutText(rows[idx].destination, 10));
      colSmallText(3, 180, y + 2, C_AMBER, cutText(rows[idx].typeTrain, 3));
      colMediumText(4, 216, y + 1, C_AMBER, cutText(rows[idx].voie, 2));
    } else {
      colSmallText(0, 8, y + 2, C_AMBER, rows[idx].heure);
      colSmallText(2, 48, y + 2, C_AMBER, cutText(rows[idx].destination, nbVisible <= 7 ? 20 : 18));
      colSmallText(3, 180, y + 2, C_AMBER, cutText(rows[idx].typeTrain, 3));
      colSmallText(4, 220, y + 2, C_AMBER, cutText(rows[idx].voie, 2));
    }
  }
}

// =====================================================
// =====================================================
void drawRetroFrame() {
  gfx->fillScreen(C_BLACK);
  gfx->drawRect(2, 2, 236, 131, C_AMBER);

  smallText(8, 6, C_AMBER, tftFrameTitle());
  smallText(105, 6, C_AMBER, tftFramePlace());
  gfx->drawFastHLine(4, 20, 232, C_AMBER);

  gfx->fillRect(4, 124, 232, 8, C_BLACK);
  smallText(8, 125, C_AMBER, "LETTRES QUI TOURNENT");
}

void drawRetroRowsOnly(bool animated) {
  int listTop = getListTop();
  int listHeight = getListHeight();
  int rowHeight = getRowHeight();
  bool big = useBigText();

  // On efface uniquement la zone des lignes une seule fois,
  gfx->fillRect(4, listTop, 232, listHeight, C_BLACK);

  for (int i = 0; i < nbVisible; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = listTop + i * rowHeight;

    gfx->fillRect(4, y, 232, rowHeight, C_FLAP_BG);
    gfx->drawFastHLine(4, y + rowHeight - 1, 232, C_FLAP_CELL);

    int maxDest = big ? 10 : (nbVisible <= 7 ? 20 : 18);
    String dest;

    if (animated) {
      dest = retroBuildVisibleText(i, idx, maxDest);
    } else {
      dest = cutText(rows[idx].destination, maxDest);
    }

    if (big) {
      colSmallText(0, 8, y + 2, C_AMBER, rows[idx].heure);
      mediumText(48, y + 1, C_AMBER, dest);
      colSmallText(3, 180, y + 2, C_AMBER, rows[idx].typeTrain);
      mediumText(216, y + 1, C_AMBER, rows[idx].voie);
    } else {
      colSmallText(0, 8, y + 2, C_AMBER, rows[idx].heure);
      smallText(48, y + 2, C_AMBER, dest);
      colSmallText(3, 180, y + 2, C_AMBER, rows[idx].typeTrain);
      smallText(220, y + 2, C_AMBER, rows[idx].voie);
    }
  }
}

char flapRandomChar(int visibleRow, int charPos) {
  const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -";
  int n = sizeof(table) - 1;
  return table[(millis() / 37 + visibleRow * 11 + charPos * 5) % n];
}

void drawRetroDestinationCell(int visibleRow, const String &textToDraw) {
  int listTop = getListTop();
  int rowHeight = getRowHeight();
  bool big = useBigText();

  int y = listTop + visibleRow * rowHeight;
  int x = 48;
  int w = 128;

  gfx->fillRect(x, y + 1, w, rowHeight - 2, C_FLAP_BG);

  // Traits verticaux type ancien tableau.
  for (int cx = x; cx < x + w; cx += 8) {
    gfx->drawFastVLine(cx, y, rowHeight, C_FLAP_CELL);
  }

  if (big) {
    mediumText(x, y + 1, C_AMBER, textToDraw);
  } else {
    smallText(x, y + 2, C_AMBER, textToDraw);
  }
}

void retroAnimateOneStep() {
  int maxDest = useBigText() ? 10 : (nbVisible <= 7 ? 20 : 18);
  retroSpinStep++;

  for (int row = 0; row < nbVisible; row++) {
    int idx = visibleRowIndex(scrollOffset + row);
    String target = cutText(rows[idx].destination, maxDest);
    String out = "";

    for (int i = 0; i < maxDest; i++) {
      if (i >= (int)target.length()) {
        out += ' ';
      } else if (retroSpinStep < 18) {
        out += flapRandomChar(row, i);
      } else {
        out += target[i];
      }
    }

    drawRetroDestinationCell(row, out);
  }

  if (retroSpinStep >= 18) {
    retroAnimating = false;
    drawRetroRowsOnly(false);
  }
}
// =====================================================
// MODE 3 : NOUVEL ECRAN SNCF
// =====================================================
void drawSncfModernFrame() {
  gfx->fillScreen(C_BLACK);
  gfx->fillRect(3, 3, 234, 129, 0x0012);
  gfx->drawRect(3, 3, 234, 129, C_GRID);
  gfx->fillRect(4, 4, 232, 18, C_RED);
  smallText(8, 9, C_WHITE, tftFrameTitle());
  smallText(125, 9, C_WHITE, tftFramePlace());
  gfx->fillRect(4, 23, 232, 9, 0x001F);
  smallText(8, 24, C_WHITE, "Heure");
  smallText(48, 24, C_WHITE, "Destination");
  smallText(184, 24, C_WHITE, "Train");
  smallText(222, 24, C_WHITE, "V");
  gfx->drawFastHLine(4, 33, 232, C_WHITE);
  gfx->fillRect(4, 124, 232, 8, C_BLACK);
  smallText(8, 125, C_WHITE, "PM3D.NET");
}

void drawSncfModernRowsOnly() {
  int listTop = getListTop();
  int listHeight = getListHeight();
  int rowHeight = getRowHeight();
  bool big = useBigText();
  gfx->fillRect(4, listTop, 232, listHeight, C_BLACK);
  for (int i = 0; i < nbVisible; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = listTop + i * rowHeight;
    uint16_t bg = (i % 2 == 0) ? C_WHITE : 0xE71C;
    gfx->fillRect(4, y, 232, rowHeight, bg);
    gfx->drawFastHLine(4, y + rowHeight - 1, 232, C_GREY);
    if (big) {
      colSmallText(0, 8, y + 1, C_BLACK, rows[idx].heure);
      colMediumText(2, 47, y + 1, C_BLACK, cutText(rows[idx].destination, 10));
      colSmallText(3, 184, y + 2, C_BLACK, cutText(rows[idx].typeTrain, 3));
      colMediumText(4, 215, y + 1, C_BLACK, cutText(rows[idx].voie, 2));
    } else {
      colSmallText(0, 8, y + 2, C_BLACK, rows[idx].heure);
      colSmallText(2, 48, y + 2, C_BLACK, cutText(rows[idx].destination, nbVisible <= 7 ? 20 : 18));
      colSmallText(3, 184, y + 2, C_BLACK, cutText(rows[idx].typeTrain, 3));
      colSmallText(4, 222, y + 2, C_BLACK, cutText(rows[idx].voie, 2));
    }
  }
}

String marqueeSlice(const String &text, int maxChars, int speedDiv) {
  if ((int)text.length() <= maxChars) return text;
  String padded = text + "   ";
  int span = padded.length();
  int start = (millis() / speedDiv) % span;
  String out;
  for (int i = 0; i < maxChars; i++) out += padded[(start + i) % span];
  return out;
}

void drawSncf2012Photo() {
  int w = gfx->width();
  int h = gfx->height();
  const uint16_t blueA = 0x045F;
  const uint16_t blueB = 0x001F;
  const uint16_t blueSide = 0x035F;
  const uint16_t line = 0x03BF;
  const uint16_t yellow = C_SNCB_YELLOW;

  gfx->fillScreen(C_BLACK);
  int rowsToDraw = min(nbVisible, 8);
  if (rowsToDraw < 5) rowsToDraw = 5;
  int rowH = h / rowsToDraw;
  if (rowH < 15) rowH = 15;

  int marquee = (millis() / 260) % 40;
  (void)marquee;
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = i * rowH;
    uint16_t bg = (i % 2 == 0) ? blueA : blueB;
    gfx->fillRect(0, y, w, rowH - 1, bg);
    gfx->drawFastHLine(0, y + rowH - 1, w, line);

    String train = rows[idx].typeTrain.length() ? rows[idx].typeTrain : "TER";
    String number = "";
    int sp = train.indexOf(' ');
    if (sp > 0) {
      number = train.substring(sp + 1);
      train = train.substring(0, sp);
    }
    train.toUpperCase();
    if (train == "TER") {
      gfx->setTextSize(2);
      gfx->setTextColor(C_WHITE, bg);
      gfx->setCursor(4, y + 2);
      gfx->print("ter");
      gfx->setTextSize(1);
    } else {
      smallText(4, y + 5, C_WHITE, cutText(train, 7));
    }
    if (number.length()) smallText(44, y + 4, C_WHITE, cutText(number, 6));

    String status = "a l'heure";
    if (rows[idx].info.startsWith("retard") || rows[idx].info.startsWith("+")) status = rows[idx].info;
    colSmallText(1, 84, y + 5, yellow, cutText(status, 10));
    colMediumText(2, 118, y + 1, C_WHITE, cutText(rows[idx].destination, 12));

    String stops = rows[idx].info;
    if (!stops.length() || stops == "a l'heure" || stops.startsWith("retard") || stops.startsWith("+")) {
      if (rows[idx].destination.indexOf("Lyon") >= 0) stops = "Chatillon  *  Villars les Dombes  *  St-Andre de Corcy";
      else if (rows[idx].destination.indexOf("Paris") >= 0) stops = "Bellegarde  *  Lyon Part-Dieu  *  Le Creusot TGV";
      else if (rows[idx].destination.indexOf("Geneve") >= 0) stops = "Bourg-en-Bresse  *  Bellegarde  *  Geneve";
      else stops = "Arrets desservis  *  correspondances  *  voie " + rows[idx].voie;
    }
    smallText(84, y + rowH - 8, C_WHITE, marqueeSlice(stops, 23, 260));

    if (rows[idx].voie.length()) {
      gfx->drawRoundRect(w - 18, y + 2, 15, min(rowH - 4, 18), 3, C_WHITE);
      colMediumText(4, w - 14, y + 4, C_WHITE, cutText(rows[idx].voie, 1));
    }
  }
  smallText(w - 18, h / 2 - 24, 0x3A7F, "d");
  smallText(w - 18, h / 2 - 14, 0x3A7F, "e");
  smallText(w - 18, h / 2 - 4, 0x3A7F, "p");
  smallText(w - 18, h / 2 + 6, 0x3A7F, "a");
  smallText(w - 18, h / 2 + 16, 0x3A7F, "r");
  smallText(w - 18, h / 2 + 26, 0x3A7F, "t");
}

// =====================================================
// MODE 4 : ANCIEN ECRAN SNCF
// =====================================================
void drawSncfOldFrame() {
  gfx->fillScreen(C_BLACK);
  gfx->drawRect(2, 2, 236, 131, C_WHITE);
  gfx->fillRect(4, 4, 232, 15, 0x7800);
  smallText(8, 8, C_WHITE, tftFrameTitle());
  gfx->drawFastHLine(4, 21, 232, C_GREY);
  smallText(8, 23, C_WHITE, "H");
  smallText(48, 23, C_WHITE, "DESTINATION");
  smallText(180, 23, C_WHITE, "TR");
  smallText(220, 23, C_WHITE, "V");
  gfx->fillRect(4, 124, 232, 8, C_BLACK);
  smallText(8, 125, C_WHITE, "PM3D.NET");
}

void drawSncfOldRowsOnly() {
  int listTop = getListTop();
  int listHeight = getListHeight();
  int rowHeight = getRowHeight();
  bool big = useBigText();
  gfx->fillRect(4, listTop, 232, listHeight, C_BLACK);
  for (int i = 0; i < nbVisible; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = listTop + i * rowHeight;
    gfx->fillRect(4, y, 232, rowHeight, C_BLACK);
    gfx->drawFastHLine(4, y + rowHeight - 1, 232, C_GREY);
    for (int x = 44; x < 172; x += 8) gfx->drawFastVLine(x, y, rowHeight, 0x39E7);
    if (big) {
      colSmallText(0, 8, y + 2, C_WHITE, rows[idx].heure);
      colMediumText(2, 48, y + 1, C_WHITE, cutText(rows[idx].destination, 10));
      colSmallText(3, 180, y + 2, C_WHITE, cutText(rows[idx].typeTrain, 3));
      colMediumText(4, 216, y + 1, C_WHITE, cutText(rows[idx].voie, 2));
    } else {
      colSmallText(0, 8, y + 2, C_WHITE, rows[idx].heure);
      colSmallText(2, 48, y + 2, C_WHITE, cutText(rows[idx].destination, nbVisible <= 7 ? 20 : 18));
      colSmallText(3, 180, y + 2, C_WHITE, cutText(rows[idx].typeTrain, 3));
      colSmallText(4, 220, y + 2, C_WHITE, cutText(rows[idx].voie, 2));
    }
  }
}

bool isItalyProfile() {
  return displayProfile.startsWith("it_");
}

void drawItalyFrame() {
  gfx->fillScreen(C_BLACK);
  gfx->fillRect(3, 3, 234, 129, C_WHITE);
  gfx->drawRect(3, 3, 234, 129, C_GRID);
  gfx->fillRect(4, 4, 232, 18, 0x001F);
  smallText(8, 8, C_WHITE, displayProfile == "it_fs_blue" ? "TRENI IN ARRIVO" : "PARTENZE");
  smallText(124, 8, C_WHITE, "TRENITALIA / FS");
  gfx->fillRect(4, 23, 232, 9, 0x021F);
  smallText(8, 24, C_WHITE, "Ora");
  smallText(45, 24, C_WHITE, "Destinazione");
  smallText(181, 24, C_WHITE, "Treno");
  smallText(220, 24, C_WHITE, "Bin");
  gfx->drawFastHLine(4, 33, 232, C_WHITE);
  gfx->fillRect(4, 124, 232, 8, C_WHITE);
  smallText(8, 125, C_RED, "DIVIETO DI SALITA E DISCESA DAI TRENI IN MOVIMENTO");
}

void drawItalyRowsOnly() {
  int listTop = getListTop();
  int listHeight = getListHeight();
  int rowHeight = getRowHeight();
  bool big = useBigText();
  gfx->fillRect(4, listTop, 232, listHeight, C_BLUE_DARK);

  for (int i = 0; i < nbVisible; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = listTop + i * rowHeight;
    uint16_t bg = (i % 2 == 0) ? 0x04BF : 0x031F;
    gfx->fillRect(4, y, 232, rowHeight, bg);
    gfx->drawFastHLine(4, y + rowHeight - 1, 232, C_WHITE);

    uint16_t destColor = (i % 2 == 0) ? C_YELLOW : C_WHITE;
    if (big) {
      colSmallText(0, 8, y + 1, C_WHITE, rows[idx].heure);
      colMediumText(2, 45, y + 1, destColor, cutText(rows[idx].destination, 11));
      colSmallText(3, 181, y + 2, C_WHITE, cutText(rows[idx].typeTrain, 4));
      colMediumText(4, 218, y + 1, C_WHITE, cutText(rows[idx].voie, 2));
    } else {
      colSmallText(0, 8, y + 2, C_WHITE, rows[idx].heure);
      colSmallText(2, 45, y + 2, destColor, cutText(rows[idx].destination, nbVisible <= 7 ? 20 : 18));
      colSmallText(3, 181, y + 2, C_WHITE, cutText(rows[idx].typeTrain, 4));
      colSmallText(4, 222, y + 2, C_WHITE, cutText(rows[idx].voie, 2));
    }
  }
}
bool isRetroProfile() {
  return displayProfile == "fr_splitflap" || displayProfile == "ch_sbb_splitflap";
}

void drawRowsClassic(int top, int height, uint16_t a, uint16_t b, uint16_t fg, uint16_t hi, uint16_t grid, int layout) {
  int rowsToDraw = constrain(nbVisible, 1, MAX_ROWS);
  int rh = max(5, height / max(1, rowsToDraw));
  bool big = rh >= 16;
  gfx->fillRect(4, top, 232, height, a);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = top + i * rh;
    uint16_t bg = (i % 2 == 0) ? a : b;
    gfx->fillRect(4, y, 232, rh - 1, bg);
    gfx->drawFastHLine(4, y + rh - 1, 232, grid);
    int ty = y + max(0, (rh - 8) / 2);
    if (layout == 1) {
      colSmallText(0, 8, ty, hi, rows[idx].heure);
      if (big) colMediumText(2, 48, y + 1, hi, cutText(rows[idx].destination, 9));
      else colSmallText(2, 48, ty, hi, cutText(rows[idx].destination, 19));
      colSmallText(3, 182, ty, hi, cutText(rows[idx].typeTrain, 4));
      colSmallText(4, 220, ty, hi, cutText(rows[idx].voie, 2));
    } else if (layout == 2) {
      colSmallText(0, 8, ty, fg, rows[idx].heure);
      if (big) colMediumText(2, 48, y + 1, hi, cutText(rows[idx].destination, 9));
      else colSmallText(2, 48, ty, hi, cutText(rows[idx].destination, 18));
      colSmallText(3, 178, ty, fg, cutText(rows[idx].typeTrain, 4));
      if (big) colMediumText(4, 218, y + 1, fg, cutText(rows[idx].voie, 2));
      else colSmallText(4, 222, ty, fg, cutText(rows[idx].voie, 2));
    } else if (layout == 3) {
      colSmallText(0, 8, ty, fg, rows[idx].heure);
      if (rows[idx].info.length()) { gfx->fillRect(34, y + 1, 25, max(3, rh - 3), C_RED); smallText(38, ty, C_WHITE, rows[idx].info); }
      colSmallText(2, 64, ty, hi, cutText(rows[idx].destination, 18));
      colSmallText(3, 198, ty, fg, cutText(rows[idx].typeTrain, 3));
      if (big) colMediumText(4, 222, y, fg, cutText(rows[idx].voie, 2));
      else colSmallText(4, 222, ty, fg, cutText(rows[idx].voie, 2));
    } else {
      colSmallText(0, 8, ty, fg, rows[idx].heure);
      if (big) colMediumText(2, 48, y + 1, hi, cutText(rows[idx].destination, 9));
      else colSmallText(2, 48, ty, hi, cutText(rows[idx].destination, 18));
      colSmallText(3, 184, ty, fg, cutText(rows[idx].typeTrain, 4));
      colSmallText(4, 222, ty, fg, cutText(rows[idx].voie, 2));
    }
  }
}

void drawBelgianGridRows() {
  gfx->fillRect(4, 22, 232, 108, C_BLUE_DARK);
  int w = 77;
  int totalToDraw = min(nbVisible, 18);
  for (int p = 0; p < 3; p++) {
    int x = 5 + p * w;
    gfx->drawRect(x, 22, w - 2, 106, C_GRID);
    for (int r = 0; r < 6; r++) {
      int linear = p * 6 + r;
      if (linear >= totalToDraw) continue;
      int idx = visibleRowIndex(scrollOffset + linear);
      int y = 24 + r * 17;
      gfx->fillRect(x + 1, y, w - 4, 15, (r % 2) ? C_BLUE_ROW1 : C_BLUE_ROW2);
      colSmallText(0, x + 3, y + 2, C_WHITE, rows[idx].heure);
      colSmallText(2, x + 29, y + 2, C_SNCB_YELLOW, cutText(rows[idx].destination, 6));
      colSmallText(4, x + w - 18, y + 2, C_WHITE, cutText(rows[idx].voie, 1));
    }
  }
}

void drawBelgianOldRows() {
  // Style inspire des anciens grands tableaux SNCB/NMBS a texte jaune.
  gfx->fillRect(3, 3, 234, 129, C_BLACK);
  gfx->fillRect(4, 4, 232, 15, 0x39E7);
  smallText(9, 8, C_WHITE, "HEURE");
  smallText(64, 8, C_WHITE, "DESTINATION");
  smallText(157, 8, C_WHITE, "NATURE");
  smallText(196, 8, C_WHITE, "VOIE");
  smallText(222, 8, C_WHITE, "REM");
  gfx->drawFastHLine(4, 20, 232, 0x7BEF);

  int rh = 13;
  for (int i = 0; i < min(nbVisible, 8); i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = 22 + i * rh;
    uint16_t bg = (i % 2 == 0) ? 0x0861 : 0x10A2;
    gfx->fillRect(4, y, 232, rh - 1, bg);
    gfx->drawFastHLine(4, y + rh - 1, 232, 0x4A49);
    gfx->drawFastVLine(38, y, rh - 1, 0x4A49);
    gfx->drawFastVLine(154, y, rh - 1, 0x4A49);
    gfx->drawFastVLine(194, y, rh - 1, 0x4A49);
    gfx->drawFastVLine(221, y, rh - 1, 0x4A49);
    colSmallText(0, 8, y + 2, C_SNCB_YELLOW, rows[idx].heure);
    colSmallText(2, 45, y + 2, C_SNCB_YELLOW, cutText(rows[idx].destination, 17));
    colSmallText(3, 160, y + 2, C_SNCB_YELLOW, cutText(rows[idx].typeTrain, 4));
    colSmallText(4, 201, y + 2, C_SNCB_YELLOW, cutText(rows[idx].voie, 2));
    if (rows[idx].info.length()) colSmallText(1, 225, y + 2, C_SNCB_YELLOW, cutText(rows[idx].info, 2));
  }
}

void drawSncbDetailListRows() {
  gfx->fillRect(3, 3, 234, 129, 0x1A30);
  gfx->fillRect(3, 3, 234, 16, C_BLUE_TOP);
  smallText(8, 8, C_WHITE, "17:56");
  smallText(108, 8, C_WHITE, "Depart");
  gfx->drawCircle(225, 10, 6, C_WHITE);
  smallText(222, 7, C_WHITE, "B");
  int heights[4] = {29, 30, 29, 29};
  int y = 21;
  for (int i = 0; i < 4; i++) {
    int idx = visibleRowIndex(i);
    int h = heights[i];
    if (i > 0) gfx->drawFastHLine(6, y - 2, 228, 0xBDF7);
    smallText(7, y + 1, C_WHITE, cutText(rows[idx].heure, 5));
    if (rows[idx].info.startsWith("+")) {
      gfx->fillRect(7, y + 12, 22, 8, C_RED);
      gfx->fillTriangle(29, y + 12, 36, y + 16, 29, y + 20, C_RED);
      smallText(13, y + 13, C_WHITE, cutText(rows[idx].info, 4));
    }
    colMediumText(2, 34, y, C_SNCB_YELLOW, cutText(rows[idx].destination, 18));
    smallText(34, y + 13, C_WHITE, cutText(rows[idx].info.startsWith("+") ? rows[idx].info.substring(4) : rows[idx].info, 46));
    smallText(210, y + 2, C_WHITE, cutText(rows[idx].typeTrain, 3));
    gfx->fillRect(229, y + 2, 8, 8, i == 2 ? C_WHITE : 0x1A30);
    smallText(231, y + 3, i == 2 ? C_BLUE_DARK : C_WHITE, cutText(rows[idx].voie, 1));
    y += h;
  }
}

void drawDetailBoardRows() {
  uint16_t frameColor = 0xFA20;
  if (transilienFrame == "white") frameColor = C_WHITE;
  else if (transilienFrame == "blue") frameColor = C_BLUE_TOP;
  gfx->fillRect(4, 22, 64, 106, frameColor);
  gfx->fillRect(69, 22, 167, 58, C_BLUE_DARK);
  gfx->fillRect(69, 82, 167, 22, C_BLUE_DARK);
  gfx->fillRect(69, 106, 167, 22, C_BLUE_DARK);
  gfx->drawRect(69, 22, 167, 58, 0x5B3F);
  gfx->drawRect(69, 82, 167, 22, 0x5B3F);
  gfx->drawRect(69, 106, 167, 22, 0x5B3F);
  gfx->fillRect(161, 22, 75, 9, 0xFA20);
  smallText(165, 24, C_WHITE, cutText(transilienAffluence, 15));

  smallText(12, 33, transilienFrame == "white" ? C_BLACK : C_WHITE, cutText(transilienInfoTitle, 10));
  String infoTxt = transilienInfoText;
  int pos = 0;
  for (int i = 0; i < 8 && pos < infoTxt.length(); i++) {
    while (pos < infoTxt.length() && infoTxt.charAt(pos) == ' ') pos++;
    String part = infoTxt.substring(pos, min(pos + 11, (int)infoTxt.length()));
    int lastSpace = part.lastIndexOf(' ');
    if (lastSpace > 4 && pos + 11 < infoTxt.length()) part = part.substring(0, lastSpace);
    smallText(9, 49 + i * 9, transilienFrame == "white" ? C_BLACK : C_WHITE, part);
    pos += part.length();
  }

  int idx = visibleRowIndex(scrollOffset);
  String dir = transilienDirection.length() ? transilienDirection : rows[idx].destination;
  gfx->drawCircle(82, 35, 8, C_YELLOW);
  smallText(79, 32, C_YELLOW, "C");
  colMediumText(2, 94, 27, C_WHITE, cutText(dir, 12));
  smallText(205, 33, C_WHITE, cutText(rows[idx].heure, 8));
  smallText(76, 46, C_WHITE, cutText(rows[idx].typeTrain + " dessert:", 20));
  smallText(76, 58, C_WHITE, "Page 2/4");
  int start = 0;
  for (int i = 0; i < 4; i++) {
    gfx->fillCircle(132, 58 + i * 7, 2, C_WHITE);
    gfx->drawFastVLine(132, 60 + i * 7, 5, C_WHITE);
    int comma = transilienStops.indexOf(',', start);
    String stop = comma >= 0 ? transilienStops.substring(start, comma) : transilienStops.substring(start);
    stop.trim();
    if (!stop.length()) stop = rows[visibleRowIndex(scrollOffset + i)].destination;
    uint16_t stopColor = stop.indexOf("supprime") >= 0 ? 0xFBE0 : C_WHITE;
    smallText(141, 55 + i * 7, stopColor, cutText(stop, 13));
    if (comma < 0) start = transilienStops.length(); else start = comma + 1;
  }
  for (int b = 0; b < 2; b++) {
    int r = visibleRowIndex(scrollOffset + b + 1);
    int y = b == 0 ? 82 : 106;
    gfx->drawCircle(81, y + 10, 7, C_YELLOW);
    smallText(78, y + 7, C_YELLOW, "C");
    colMediumText(1, 94, y + 4, C_WHITE, cutText(rows[r].destination, 17));
    smallText(207, y + 6, C_WHITE, cutText(rows[r].heure, 6));
    smallText(94, y + 15, 0xBDF7, cutText(rows[r].typeTrain + " " + rows[r].info, 24));
  }
}

void drawTransilien2016Rows() {
  gfx->fillRect(3, 3, 234, 129, 0x2104);
  gfx->fillRect(4, 4, 232, 17, 0x2104);
  smallText(12, 8, C_WHITE, "Departs Ile-de-France");
  uint16_t blueA = 0x0274;
  uint16_t blueB = 0x038F;
  uint16_t blueHeader = 0x01B1;
  uint16_t orange = 0xFBE0;
  int colW = 112;
  int rowH = 10;
  for (int col = 0; col < 2; col++) {
    int x = 8 + col * 116;
    gfx->fillRect(x, 24, colW, 10, blueHeader);
    smallText(x + 2, 26, C_WHITE, "Prochains trains au depart");
    for (int r = 0; r < 8; r++) {
      int idx = col * 8 + r;
      int y = 35 + r * rowH;
      uint16_t bg = (r % 2) ? blueB : blueA;
      gfx->fillRect(x, y, colW, rowH - 1, bg);
      if (rows[idx].voie.length()) {
        gfx->fillRect(x + 78, y + 1, 10, 8, 0x4A9F);
        smallText(x + 81, y + 2, C_WHITE, cutText(rows[idx].voie, 1));
      }
      smallText(x + 2, y + 2, C_WHITE, cutText(rows[idx].destination, 15));
      smallText(x + 91, y + 1, C_YELLOW, cutText(rows[idx].heure, 5));
      smallText(x + 91, y + 6, C_WHITE, cutText(rows[idx].info, 5));
    }
  }
  gfx->fillRect(124, 115, 96, 13, orange);
  smallText(128, 119, C_BLACK, cutText(transilienInfoText, 18));
  gfx->fillRect(220, 115, 16, 13, 0x2945);
  smallText(223, 119, C_WHITE, "08");
  smallText(7, 126, C_WHITE, "heure");
  smallText(45, 126, C_WHITE, "destination");
  smallText(207, 126, C_WHITE, "voie");
  smallText(194, 116, C_WHITE, "12:26");
}

void drawTransilienLinePRows() {
  gfx->fillRect(3, 3, 234, 129, 0x02B4);
  uint16_t blueA = 0x02D6;
  uint16_t blueB = 0x1298;
  uint16_t cellBlue = 0x34BF;
  int colW = 113;
  int rowH = 14;
  for (int col = 0; col < 2; col++) {
    int x = 5 + col * 118;
    for (int r = 0; r < 8; r++) {
      int idx = col * 8 + r;
      int y = 8 + r * rowH;
      uint16_t bg = (r % 2) ? blueA : blueB;
      gfx->fillRect(x, y, colW, rowH - 1, bg);
      String dest = rows[idx].destination;
      if (dest.length()) {
        smallText(x + 2, y + 3, C_YELLOW, dest.substring(0, 1));
        smallText(x + 9, y + 3, C_WHITE, cutText(dest.substring(1), 13));
      }
      if (rows[idx].heure.length()) {
        smallText(x + 68, y + 1, C_YELLOW, cutText(rows[idx].heure, 6));
        smallText(x + 72, y + 8, C_WHITE, cutText(rows[idx].typeTrain, 5));
        gfx->fillRect(x + 96, y + 2, 15, 10, cellBlue);
        gfx->drawRect(x + 96, y + 2, 15, 10, 0x8E7F);
        smallText(x + 101, y + 4, C_WHITE, cutText(rows[idx].voie, 2));
      } else if (rows[idx].info.length()) {
        smallText(x + 67, y + 1, C_YELLOW, "train a");
        smallText(x + 67, y + 7, C_YELLOW, "l'approche");
        gfx->fillRect(x + 96, y + 2, 15, 10, cellBlue);
        gfx->drawRect(x + 96, y + 2, 15, 10, 0x8E7F);
        smallText(x + 101, y + 4, C_WHITE, cutText(rows[idx].voie, 2));
      }
    }
  }
  gfx->fillRect(204, 120, 31, 11, 0x0332);
  smallText(209, 123, C_WHITE, "ligne P");
}

void drawRerALineRows() {
  gfx->fillRect(3, 3, 234, 129, C_BLACK);
  gfx->fillRect(4, 6, 228, 116, 0xEF7D);
  gfx->drawFastVLine(118, 6, 116, C_BLACK);
  gfx->fillCircle(132, 28, 18, 0xF9C7);
  mediumText(124, 16, C_RED, "A");
  smallText(151, 18, 0x02B4, "Terminus");
  colMediumText(2, 151, 25, 0x02B4, "St-Germain-en-Laye");
  gfx->fillRect(198, 16, 12, 14, C_BLACK);
  smallText(202, 20, C_YELLOW, "5");
  smallText(219, 9, C_YELLOW, "07:06");

  int lx[9] = {30, 42, 54, 66, 78, 90, 102, 118, 132};
  int ly[9] = {88, 84, 80, 76, 72, 68, 64, 60, 56};
  for (int i = 0; i < 8; i++) {
    gfx->drawLine(lx[i], ly[i], lx[i + 1], ly[i + 1], C_RED);
    gfx->drawLine(lx[i], ly[i] + 1, lx[i + 1], ly[i + 1] + 1, C_RED);
  }
  const char* west[] = {"St-Germain", "Le Vesinet", "Le Pecq", "Vesinet-C.", "Chatou", "Rueil", "Nanterre", "Nanterre-U."};
  for (int i = 0; i < 8; i++) {
    gfx->fillCircle(lx[i], ly[i], 3, C_WHITE);
    smallText(lx[i] - 15, ly[i] - 15, 0x02B4, west[i]);
  }
  int cx[7] = {132, 146, 160, 174, 188, 202, 216};
  const char* center[] = {"Nanterre-P", "La Defense", "Ch. Gaulle", "Auber", "Chatelet", "Gare de Lyon", ""};
  for (int i = 0; i < 6; i++) {
    gfx->drawFastHLine(cx[i], 56, cx[i + 1] - cx[i], C_RED);
    gfx->drawFastHLine(cx[i], 57, cx[i + 1] - cx[i], C_RED);
    gfx->fillCircle(cx[i], 56, 3, C_WHITE);
    smallText(cx[i] - 10, 39, 0x02B4, center[i]);
  }
  gfx->fillCircle(202, 56, 5, 0x02B4);
  smallText(199, 53, C_WHITE, "S");
  smallText(144, 36, C_BLACK, "ZEMA");
  gfx->fillRect(153, 32, 20, 6, 0xD6BA);
  smallText(157, 32, C_BLACK, "ZEMA");
}

void drawSncfFirstScreenRows() {
  gfx->fillRect(3, 3, 234, 129, 0x0013);
  gfx->fillRoundRect(6, 7, 228, 120, 6, 0x001F);
  uint16_t line = 0xE7E0;
  uint16_t blue = 0x001F;
  uint16_t yellow = C_SNCB_YELLOW;
  smallText(10, 27, yellow, "Nom");
  smallText(49, 27, yellow, "Destination");
  smallText(151, 27, yellow, "Heure");
  smallText(197, 14, yellow, "19:50");
  smallText(220, 31, yellow, "Train");
  gfx->drawFastVLine(44, 24, 80, line);
  gfx->drawFastVLine(148, 24, 80, line);
  gfx->drawFastVLine(223, 24, 80, line);
  gfx->drawFastHLine(10, 40, 214, line);
  for (int i = 0; i < 5; i++) {
    int idx = visibleRowIndex(i);
    int y = 43 + i * 13;
    gfx->fillRect(10, y, 213, 11, blue);
    smallText(12, y + 2, C_WHITE, cutText(rows[idx].typeTrain, 4));
    smallText(50, y + 2, C_WHITE, cutText(rows[idx].destination, 13));
    smallText(151, y + 2, C_WHITE, cutText(rows[idx].heure, 5));
    smallText(224, y + 2, C_WHITE, cutText(rows[idx].voie, 5));
  }
  smallText(101, 95, C_WHITE, "Train terminus");
  smallText(45, 112, yellow, "Attention, le train de 19.08 - voie 3 - est remplace");
  smallText(63, 121, yellow, "par le train ZYCHO.");
  smallText(52, 128, yellow, "Veuillez consulter les affichages de desserte.");
}

void drawSncfOldLedRows() {
  gfx->fillRect(3, 3, 234, 129, C_BLACK);
  uint16_t amber = 0xFEA0;
  uint16_t dimAmber = 0xD5A0;
  gfx->drawFastHLine(24, 13, 190, amber);
  gfx->drawFastHLine(24, 78, 190, amber);
  for (int i = 0; i < 6; i++) {
    int idx = visibleRowIndex(i);
    int y = 18 + i * 10;
    smallText(25, y, amber, cutText(rows[idx].typeTrain, 4));
    smallText(55, y, amber, cutText(rows[idx].destination, 12));
    smallText(153, y, amber, cutText(rows[idx].heure, 8));
    smallText(204, y, amber, cutText(rows[idx].voie, 2));
  }
  smallText(24, 84, dimAmber, "L'interconnexion est suspendue jusqu'a");
  smallText(24, 94, dimAmber, "nouvel ordre suite a un important");
  smallText(24, 104, dimAmber, "degagement de fumee a Chatelet. Un");
  smallText(24, 114, dimAmber, "retour a la normale est prevu a 21h00.");
  gfx->drawFastHLine(176, 122, 40, amber);
  smallText(184, 124, amber, "19 56");
}

void drawSncfArrivalsGreenRows() {
  gfx->fillRect(3, 3, 234, 129, 0x5AE5);
  gfx->fillRect(23, 32, 190, 76, 0x0A85);
  gfx->drawRect(23, 32, 190, 76, 0x9EB0);
  smallText(18, 8, 0xDFF7, "Arrivees");
  uint16_t greens[2] = {0x0344, 0x0BC6};
  for (int i = 0; i < 7; i++) {
    int idx = visibleRowIndex(i);
    int y = 36 + i * 10;
    gfx->fillRect(26, y, 183, 9, greens[i % 2]);
    gfx->fillRect(29, y + 1, 18, 7, C_WHITE);
    smallText(31, y + 2, 0x0344, cutText(rows[idx].typeTrain, 3));
    smallText(50, y + 1, C_WHITE, cutText(rows[idx].info, 6));
    smallText(76, y + 1, C_YELLOW, cutText(rows[idx].heure, 5));
    smallText(111, y + 1, C_WHITE, cutText(rows[idx].destination, 15));
    if (rows[idx].voie.length()) {
      gfx->fillRect(197, y, 12, 9, 0x0B5F);
      smallText(201, y + 1, C_WHITE, cutText(rows[idx].voie, 1));
    }
  }
  smallText(50, 49, C_WHITE, "Train Terminus - Ne prend pas de voy.");
  smallText(50, 59, C_WHITE, "Train Terminus - Ne prend pas de voy.");
  smallText(26, 112, C_WHITE, "TRANSPORT ALTERNATIF. AVANT DE VOUS REN.");
  gfx->fillRect(177, 113, 32, 11, 0x7BEF);
  smallText(181, 116, C_BLACK, "14:00");
}

String dbClockText() {
  unsigned long seconds = millis() / 1000UL;
  int totalMinutes = 12 * 60 + 48 + (seconds / 60);
  int hh = (totalMinutes / 60) % 24;
  int mm = totalMinutes % 60;
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
  return String(buf);
}

void drawDbClockBox() {
  gfx->fillRect(186, 8, 32, 13, 0x2A9F);
  gfx->drawRect(186, 8, 32, 13, 0x635F);
  smallText(189, 11, C_WHITE, dbClockText());
  drawDbLogo(221, 8, 16, 13);
}

void drawDbUberCells() {
  const uint16_t rowBlue = 0x029F;
  const uint16_t gridDark = 0x000A;
  const uint16_t skyText = 0xBFFF;
  int pairCount = min((nbVisible + 1) / 2, 5);
  for (int i = 0; i < pairCount; i++) {
    int idx = visibleRowIndex(scrollOffset + i * 2);
    int idx2 = visibleRowIndex(scrollOffset + i * 2 + 1);
    int y = 44 + i * 17;
    gfx->fillRect(79, y + 1, 42, 15, rowBlue);
    gfx->drawFastVLine(78, y, 16, gridDark);
    gfx->drawFastVLine(122, y, 16, gridDark);
    smallText(81, y + 1, skyText, marqueeSlice(rows[idx].info, 6, 320));
    smallText(81, y + 9, skyText, marqueeSlice(rows[idx2].info, 6, 320));
  }
}

void drawDbLargeBlueRows() {
  const uint16_t bgNight = 0x080B;
  const uint16_t headBlue = 0x1811;
  const uint16_t rowBlue = 0x029F;
  const uint16_t gridDark = 0x000A;
  const uint16_t skyText = 0xBFFF;
  gfx->fillScreen(bgNight);
  gfx->fillRect(0, 0, 240, 42, headBlue);
  gfx->drawFastHLine(0, 42, 240, gridDark);
  smallText(8, 9, C_WHITE, "Abfahrt");
  smallText(58, 10, C_WHITE, "Departure / Depart");
  smallText(10, 31, C_WHITE, "Zeit");
  smallText(42, 31, C_WHITE, "Time");
  smallText(73, 31, C_WHITE, "Uber");
  smallText(126, 31, C_WHITE, "Ziel Dest.");
  smallText(208, 31, C_WHITE, "Gleis");
  drawDbClockBox();
  for (int i = 0; i < 5; i++) {
    int idx = visibleRowIndex(scrollOffset + i * 2);
    int idx2 = visibleRowIndex(scrollOffset + i * 2 + 1);
    int y = 44 + i * 17;
    gfx->fillRect(0, y, 240, 16, rowBlue);
    gfx->drawFastHLine(0, y + 16, 240, gridDark);
    gfx->drawFastVLine(40, y, 16, gridDark);
    gfx->drawFastVLine(78, y, 16, gridDark);
    gfx->drawFastVLine(122, y, 16, gridDark);
    gfx->drawFastVLine(206, y, 16, gridDark);

    colSmallText(0, 5, y + 1, C_WHITE, cutText(rows[idx].heure, 5));
    gfx->fillRect(43, y + 2, 33, 6, C_WHITE);
    colSmallText(3, 46, y + 1, 0x4A5F, cutText(rows[idx].typeTrain, 7));
    colSmallText(1, 81, y + 1, skyText, marqueeSlice(rows[idx].info, 6, 320));
    colSmallText(2, 127, y + 1, C_WHITE, cutText(rows[idx].destination, 12));
    String voie1 = cutText(rows[idx].voie, 4);
    int voieX1 = 206 + max(0, (34 - (int)voie1.length() * 6) / 2);
    colSmallText(4, voieX1, y + 1, C_WHITE, voie1);

    if (i * 2 + 1 < nbVisible) {
      colSmallText(0, 5, y + 9, C_WHITE, cutText(rows[idx2].heure, 5));
      gfx->fillRect(43, y + 10, 33, 6, C_WHITE);
      colSmallText(3, 46, y + 9, 0x4A5F, cutText(rows[idx2].typeTrain, 7));
      colSmallText(1, 81, y + 9, skyText, marqueeSlice(rows[idx2].info, 6, 320));
      colSmallText(2, 127, y + 9, C_WHITE, cutText(rows[idx2].destination, 12));
      String voie2 = cutText(rows[idx2].voie, 4);
      int voieX2 = 206 + max(0, (34 - (int)voie2.length() * 6) / 2);
      colSmallText(4, voieX2, y + 9, C_WHITE, voie2);
    }
  }
}

void drawDbIntercityMarquee(uint16_t panel, uint16_t line) {
  const int x = 54;
  const int y = 4;
  const int w = 184;
  const int h = 11;
  String msg = dbIntercityMessage + "   +++   ";
  int msgPx = max(1, (int)msg.length() * 6);
  int span = w + msgPx + 18;
  int shift = (millis() / 75UL) % span;
  int textX = x + w - shift;
  if (textX < x) {
    int hiddenPx = x - textX;
    int skip = min((int)msg.length(), hiddenPx / 6);
    msg = msg.substring(skip);
    textX = x - (hiddenPx % 6);
  }

  gfx->fillRect(sx(x), sy(y), w, h, C_WHITE);
  if (textX < x + w) {
    gfx->setTextWrap(false);
    gfx->setTextSize(1);
    gfx->setTextColor(0x2945, C_WHITE);
    gfx->setCursor(sx(textX), sy(y + 2));
    gfx->print(msg);
  }
  gfx->fillRect(sx(0), sy(y), x, h, panel);
  gfx->fillRect(sx(x + w), sy(y), 240 - (x + w), h, panel);
  gfx->drawRect(0, 0, 240, 132, line);
}

void drawDbIntercityRows() {
  const uint16_t bg = 0x1811;
  const uint16_t panel = 0x2817;
  const uint16_t strip = 0xCFE0;
  const uint16_t line = 0x8CB2;
  const uint16_t pale = 0xDEFB;
  int clockMinutes = (dbIntercityClockHour * 60 + dbIntercityClockMinute + (int)(millis() / 60000UL)) % 1440;
  int mainIdx = 0;
  int bestDelta = 1440;
  for (int i = 0; i < MAX_ROWS; i++) {
    if (!rowIsFilled(i) || rows[i].heure.length() < 4) continue;
    int hh = rows[i].heure.substring(0, 2).toInt();
    int mm = rows[i].heure.substring(3, 5).toInt();
    int delta = (hh * 60 + mm - clockMinutes + 1440) % 1440;
    if (delta < bestDelta) { bestDelta = delta; mainIdx = i; }
  }
  if (!rowsOnlyPass) {
    gfx->fillScreen(bg);
    gfx->fillRect(0, 0, 240, 132, panel);
    gfx->drawRect(0, 0, 240, 132, line);
    drawDbIntercityMarquee(panel, line);
  }
  String mainTrainNo = rows[mainIdx].typeTrain.length() ? rows[mainIdx].typeTrain : "ICE";
  smallText(9, 26, pale, cutText(mainTrainNo, 8));
  gfx->drawRect(9, 55, 43, 18, line);
  smallText(16, 60, C_WHITE, rows[mainIdx].heure.length() ? rows[mainIdx].heure : "14:53");
  smallText(64, 24, 0xD6F7, rows[mainIdx].info.length() ? cutText(rows[mainIdx].info, 22) : "Duisburg Hbf");
  mediumText(64, 39, C_WHITE, rows[mainIdx].destination.length() ? cutText(rows[mainIdx].destination, 12) : "Essen Hbf");
  gfx->drawFastHLine(8, 77, 224, line);
  gfx->drawFastHLine(16, 92, 207, line);
  gfx->setTextSize(1);
  gfx->setTextWrap(false);
  const char* coachLabels[8] = {"29", "28", "27", "25", "23", "21", "31", "32"};
  for (int c = 0; c < 8; c++) {
    int x = 24 + c * 25;
    bool endCar = (c == 0 || c == 7);
    if (c == 0) {
      gfx->fillTriangle(x - 12, 88, x - 8, 83, x - 8, 93, 0xD6F7);
      gfx->fillRect(x - 8, 83, 22, 10, 0xD6F7);
      gfx->drawLine(x - 12, 88, x - 8, 83, 0xD6F7);
      gfx->drawLine(x - 12, 88, x - 8, 93, 0xD6F7);
      gfx->drawRect(x - 8, 83, 22, 10, 0xD6F7);
    } else if (c == 7) {
      gfx->fillRect(x - 8, 83, 18, 10, 0xD6F7);
      gfx->fillTriangle(x + 10, 83, x + 14, 88, x + 10, 93, 0xD6F7);
      gfx->drawRect(x - 8, 83, 18, 10, 0xD6F7);
      gfx->drawLine(x + 10, 83, x + 14, 88, 0xD6F7);
      gfx->drawLine(x + 14, 88, x + 10, 93, 0xD6F7);
    } else {
      gfx->fillRect(x - 8, 83, 22, 10, panel);
      gfx->drawRect(x - 8, 83, 22, 10, 0xD6F7);
    }
    gfx->drawFastVLine(x + 13, 84, 8, 0xD6F7);
    gfx->setTextColor(endCar ? 0x1811 : 0xD6F7, endCar ? 0xD6F7 : panel);
    gfx->setCursor(sx(x - 4), sy(85));
    gfx->print(coachLabels[c]);
  }
  smallText(10, 84, C_WHITE, "<-");
  int rowsDrawn = min(nbVisible, 3);
  for (int r = 0; r < rowsDrawn; r++) {
    int idx = visibleRowIndex(mainIdx + r);
    int y = 100 + r * 10;
    smallText(12, y + 1, pale, cutText(rows[idx].heure, 5));
    smallText(61, y, pale, cutText(rows[idx].typeTrain, 7));
    smallText(105, y, C_WHITE, cutText(rows[idx].destination, 19));
    smallText(218, y, C_WHITE, cutText(rows[idx].voie, 2));
  }
}

uint8_t db2010ClockBits(char c, int row) {
  switch (c) {
    case '0': { const uint8_t g[5] = {0b111,0b101,0b101,0b101,0b111}; return g[row]; }
    case '1': { const uint8_t g[5] = {0b010,0b110,0b010,0b010,0b111}; return g[row]; }
    case '2': { const uint8_t g[5] = {0b111,0b001,0b111,0b100,0b111}; return g[row]; }
    case '3': { const uint8_t g[5] = {0b111,0b001,0b111,0b001,0b111}; return g[row]; }
    case '4': { const uint8_t g[5] = {0b101,0b101,0b111,0b001,0b001}; return g[row]; }
    case '5': { const uint8_t g[5] = {0b111,0b100,0b111,0b001,0b111}; return g[row]; }
    case '6': { const uint8_t g[5] = {0b111,0b100,0b111,0b101,0b111}; return g[row]; }
    case '7': { const uint8_t g[5] = {0b111,0b001,0b010,0b010,0b010}; return g[row]; }
    case '8': { const uint8_t g[5] = {0b111,0b101,0b111,0b101,0b111}; return g[row]; }
    case '9': { const uint8_t g[5] = {0b111,0b101,0b111,0b001,0b111}; return g[row]; }
  }
  return 0;
}

void drawDb2010ClockGlyph(int x, int y, char c, uint16_t color) {
  if (c == ':') {
    gfx->fillRect(sx(x + 1), sy(y + 3), 2, 2, color);
    gfx->fillRect(sx(x + 1), sy(y + 9), 2, 2, color);
    return;
  }
  for (int row = 0; row < 5; row++) {
    uint8_t bits = db2010ClockBits(c, row);
    for (int col = 0; col < 3; col++) {
      if (bits & (1 << (2 - col))) gfx->fillRect(sx(x + col * 3), sy(y + row * 3), 2, 2, color);
    }
  }
}

void drawDb2010Clock(int x, int y, const String &txt, uint16_t color) {
  int cx = x;
  for (int i = 0; i < (int)txt.length(); i++) {
    char c = txt[i];
    drawDb2010ClockGlyph(cx, y, c, color);
    cx += (c == ':') ? 5 : 10;
  }
}

uint8_t tinyDbBits(char c, int row) {
  if (c >= '0' && c <= '9') return db2010ClockBits(c, row);
  switch (c) {
    case 'A': { const uint8_t g[5] = {0b010,0b101,0b111,0b101,0b101}; return g[row]; }
    case 'B': { const uint8_t g[5] = {0b110,0b101,0b110,0b101,0b110}; return g[row]; }
    case 'C': { const uint8_t g[5] = {0b111,0b100,0b100,0b100,0b111}; return g[row]; }
    case 'D': { const uint8_t g[5] = {0b110,0b101,0b101,0b101,0b110}; return g[row]; }
    case 'E': { const uint8_t g[5] = {0b111,0b100,0b110,0b100,0b111}; return g[row]; }
    case 'F': { const uint8_t g[5] = {0b111,0b100,0b110,0b100,0b100}; return g[row]; }
    case 'G': { const uint8_t g[5] = {0b111,0b100,0b101,0b101,0b111}; return g[row]; }
    case 'H': { const uint8_t g[5] = {0b101,0b101,0b111,0b101,0b101}; return g[row]; }
    case 'I': { const uint8_t g[5] = {0b111,0b010,0b010,0b010,0b111}; return g[row]; }
    case 'J': { const uint8_t g[5] = {0b001,0b001,0b001,0b101,0b111}; return g[row]; }
    case 'K': { const uint8_t g[5] = {0b101,0b101,0b110,0b101,0b101}; return g[row]; }
    case 'L': { const uint8_t g[5] = {0b100,0b100,0b100,0b100,0b111}; return g[row]; }
    case 'M': { const uint8_t g[5] = {0b101,0b111,0b111,0b101,0b101}; return g[row]; }
    case 'N': { const uint8_t g[5] = {0b101,0b111,0b111,0b111,0b101}; return g[row]; }
    case 'O': { const uint8_t g[5] = {0b111,0b101,0b101,0b101,0b111}; return g[row]; }
    case 'P': { const uint8_t g[5] = {0b110,0b101,0b110,0b100,0b100}; return g[row]; }
    case 'Q': { const uint8_t g[5] = {0b111,0b101,0b101,0b111,0b001}; return g[row]; }
    case 'R': { const uint8_t g[5] = {0b110,0b101,0b110,0b101,0b101}; return g[row]; }
    case 'S': { const uint8_t g[5] = {0b111,0b100,0b111,0b001,0b111}; return g[row]; }
    case 'T': { const uint8_t g[5] = {0b111,0b010,0b010,0b010,0b010}; return g[row]; }
    case 'U': { const uint8_t g[5] = {0b101,0b101,0b101,0b101,0b111}; return g[row]; }
    case 'V': { const uint8_t g[5] = {0b101,0b101,0b101,0b101,0b010}; return g[row]; }
    case 'W': { const uint8_t g[5] = {0b101,0b101,0b111,0b111,0b101}; return g[row]; }
    case 'X': { const uint8_t g[5] = {0b101,0b101,0b010,0b101,0b101}; return g[row]; }
    case 'Y': { const uint8_t g[5] = {0b101,0b101,0b010,0b010,0b010}; return g[row]; }
    case 'Z': { const uint8_t g[5] = {0b111,0b001,0b010,0b100,0b111}; return g[row]; }
    case '-': { const uint8_t g[5] = {0b000,0b000,0b111,0b000,0b000}; return g[row]; }
    case '/': { const uint8_t g[5] = {0b001,0b001,0b010,0b100,0b100}; return g[row]; }
    case '.': { const uint8_t g[5] = {0b000,0b000,0b000,0b000,0b010}; return g[row]; }
  }
  return 0;
}

void tinyDbText(int x, int y, uint16_t color, uint16_t bg, const String &txt, int maxChars) {
  gfx->fillRect(sx(x), sy(y), maxChars * 4, 6, bg);
  String out = cutText(txt, maxChars);
  for (int i = 0; i < (int)out.length(); i++) {
    char c = out[i];
    if (c >= 'a' && c <= 'z') c -= 32;
    for (int row = 0; row < 5; row++) {
      uint8_t bits = tinyDbBits(c, row);
      for (int col = 0; col < 3; col++) {
        if (bits & (1 << (2 - col))) gfx->drawPixel(sx(x + i * 4 + col), sy(y + row), color);
      }
    }
  }
}

void tinyDbCellText(int x, int y, int w, uint16_t color, uint16_t bg, const String &txt) {
  int maxChars = max(1, w / 4);
  tinyDbText(x, y, color, bg, txt, maxChars);
}

void oebbFitText(int x, int y, int w, int h, uint16_t color, uint16_t bg, const String &txt) {
  gfx->fillRect(sx(x), sy(y), w, h, bg);
  String s = txt;
  int smallMax = max(1, (w - 1) / 6);
  if ((int)s.length() <= smallMax || h < 13) {
    gfx->setTextWrap(false);
    gfx->setTextSize(1);
    gfx->setTextColor(color, bg);
    gfx->setCursor(sx(x), sy(y + max(0, (h - 8) / 2)));
    gfx->print(cutText(s, smallMax));
    return;
  }
  int medMax = max(1, (w - 1) / 12);
  gfx->setTextWrap(false);
  gfx->setTextSize(2);
  gfx->setTextColor(color, bg);
  gfx->setCursor(sx(x), sy(y + max(0, (h - 16) / 2)));
  gfx->print(cutText(s, medMax));
}

void drawDb2010BoardRows() {
  const uint16_t dbBlue = 0x0011;
  const uint16_t yellow = C_YELLOW;
  const uint16_t timeBg = C_WHITE;
  const uint16_t grid = 0x4A7F;
  if (!rowsOnlyPass) {
    gfx->fillScreen(dbBlue);
    gfx->fillRect(0, 0, 240, 30, dbBlue);
    drawDb2010Clock(3, 8, pm3dDynamicClock(), C_WHITE);
    int titleSize = constrain(db2010TitleFontSize, 1, 3);
    int titleY = titleSize >= 3 ? 1 : (titleSize == 2 ? 5 : 10);
    int titleMaxChars = max(4, 150 / (6 * titleSize));
    gfx->setTextSize(titleSize);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(sx(68), sy(titleY));
    gfx->print(cutText(db2010Title, titleMaxChars));
    drawDbLogo2010(221, 4, 17, 16);
    gfx->fillRect(0, 30, 240, 18, yellow);
    smallText(2, 35, C_BLACK, "Zeit");
    smallText(46, 35, C_BLACK, "Nach");
    smallText(104, 35, C_BLACK, "Uber");
    smallText(210, 35, C_BLACK, "Gleis");
  }
  int rowsToDraw = constrain(nbVisible, 3, MAX_ROWS);
  int rowH = max(5, (132 - 49) / max(1, rowsToDraw));
  int rowTextSize = rowsToDraw <= 3 ? 2 : 1;
  int timeW = 34;
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = 49 + i * rowH;
    gfx->fillRect(0, y, timeW, rowH - 1, timeBg);
    gfx->drawFastHLine(0, y, timeW, grid);
    gfx->drawFastHLine(0, y + rowH - 1, timeW, grid);
    gfx->drawFastVLine(0, y, rowH, grid);
    gfx->drawFastVLine(timeW - 1, y, rowH, grid);
    gfx->drawFastHLine(0, y + rowH - 1, 240, grid);
    gfx->drawFastVLine(timeW, y, rowH - 1, grid);
    gfx->setTextWrap(false);
    gfx->setTextSize(1);
    gfx->setTextColor(dbBlue, timeBg);
    gfx->setCursor(sxCol(1, 0), sy(y + (rowH >= 10 ? 3 : 0)));
    gfx->print(cutText(rows[idx].heure, 5));
    if (rowH >= 9) tinyDbText(1, y + rowH - 7, dbBlue, timeBg, rows[idx].typeTrain, 8);
    clippedCellText(2, 46, y + 1, 56, rowH - 3, rowTextSize, C_WHITE, dbBlue, rows[idx].destination);
    String via = rows[idx].info;
    int sep = via.indexOf("|");
    if (sep >= 0) {
      via = via.substring(0, sep);
      via.trim();
    }
    clippedCellText(1, 104, y + 1, 101, rowH - 3, rowTextSize, C_WHITE, dbBlue, via);
    gfx->fillRect(207, y + 1, 4, rowH - 3, dbBlue);
    gfx->setTextSize(rowTextSize);
    gfx->setTextColor(C_WHITE, dbBlue);
    gfx->setCursor(sxCol(224, 4), sy(y + (rowTextSize == 2 ? 3 : 4)));
    gfx->print(cutText(rows[idx].voie, 2));
  }
}

void drawDb2022BoardRows() {
  const uint16_t bg = 0x000C;
  const uint16_t panel = 0x0818;
  const uint16_t line = 0x8CBF;
  const uint16_t greyLine = C_WHITE;
  const uint16_t pale = C_WHITE;
  const uint16_t soft = C_WHITE;
  if (!rowsOnlyPass) {
    gfx->fillScreen(bg);
    gfx->fillRect(0, 0, 240, 132, bg);
    gfx->fillRect(0, 0, 240, 132, panel);
    gfx->setTextSize(1);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(sx(14), sy(9));
    gfx->print("Abfahrt");
    pseudoItalicText(78, 9, 1, C_WHITE, panel, "Departure");
    drawDbLogo2010(217, 6, 18, 15);
    gfx->fillRect(27, 24, 178, 2, greyLine);
    gfx->setTextWrap(false);
    gfx->fillRect(7, 30, 226, 7, panel);
    tinyDbCellText(7, 30, 48, soft, panel, "ZUG/TRAIN");
    tinyDbCellText(64, 30, 48, soft, panel, "UBER/VIA");
    tinyDbCellText(204, 30, 28, soft, panel, "GLEIS");
    gfx->drawFastHLine(7, 38, 226, C_WHITE);
  }
  int rowsToDraw = constrain(nbVisible, 3, MAX_ROWS);
  int top = 40;
  int rowH = max(5, (132 - top) / max(1, rowsToDraw));
  int rowTextSize = 1;
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = top + i * rowH;
    gfx->fillRect(0, y, 240, rowH - 1, panel);
    gfx->drawFastHLine(7, y + rowH - 1, 226, line);
    int cellH = max(4, min(8, rowH - 1));
    gfx->fillRect(sx(8), sy(y + 1), 46, cellH, panel);
    gfx->fillRect(sx(64), sy(y + 1), 135, cellH, panel);
    tinyDbCellText(8, y + 2, 46, pale, panel, rows[idx].typeTrain);
    tinyDbCellText(64, y + 2, 135, pale, panel, rows[idx].info);
    if (rowH >= 10) {
      clippedCellText(5, 8, y + rowH - 9, 46, 8, 1, pale, panel, rows[idx].heure);
      clippedCellText(5, 64, y + rowH - 9, 135, 8, 1, pale, panel, rows[idx].destination);
      clippedCellText(5, 207, y + rowH - 9, 27, 8, 1, pale, panel, rows[idx].voie);
    }
  }
}

void drawBvgRows() {
  gfx->fillRect(4, 20, 232, 111, 0x001F);
  for (int i = 0; i < nbVisible; i++) {
    int y = 22 + i * 14;
    gfx->fillRect(5, y, 230, 12, (i % 2) ? 0x101F : 0x181F);
    gfx->drawFastHLine(5, y + 12, 230, C_CYAN);
    smallText(10, y + 2, C_WHITE, i < 9 ? String((i + 1) * 5) + " min" : rows[visibleRowIndex(scrollOffset + i)].heure);
    smallText(64, y + 2, C_WHITE, "247");
    smallText(104, y + 2, C_WHITE, cutText(rows[visibleRowIndex(scrollOffset + i)].destination, 19));
  }
}


String pm3dDynamicClock() {
  int hh = ((__TIME__[0] - '0') * 10) + (__TIME__[1] - '0');
  int mm = ((__TIME__[3] - '0') * 10) + (__TIME__[4] - '0');
  int ss = ((__TIME__[6] - '0') * 10) + (__TIME__[7] - '0');
  unsigned long total = (unsigned long)hh * 3600UL + (unsigned long)mm * 60UL + (unsigned long)ss + millis() / 1000UL;
  total %= 86400UL;
  hh = total / 3600UL;
  mm = (total % 3600UL) / 60UL;
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
  return String(buf);
}

void directText(int x, int y, uint8_t size, uint16_t color, const String &txt) {
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(sx(x), sy(y));
  gfx->print(txt);
}
uint8_t badenTinyGlyph(char c, int row) {
  switch (c) {
    case 'A': { const uint8_t g[5] = {0b010,0b101,0b111,0b101,0b101}; return g[row]; }
    case 'D': { const uint8_t g[5] = {0b110,0b101,0b101,0b101,0b110}; return g[row]; }
    case 'E': { const uint8_t g[5] = {0b111,0b100,0b110,0b100,0b111}; return g[row]; }
    case 'K': { const uint8_t g[5] = {0b101,0b101,0b110,0b101,0b101}; return g[row]; }
    case 'R': { const uint8_t g[5] = {0b110,0b101,0b110,0b101,0b101}; return g[row]; }
    case 'S': { const uint8_t g[5] = {0b111,0b100,0b111,0b001,0b111}; return g[row]; }
    case 'T': { const uint8_t g[5] = {0b111,0b010,0b010,0b010,0b010}; return g[row]; }
    case 'W': { const uint8_t g[5] = {0b101,0b101,0b101,0b111,0b101}; return g[row]; }
    default: return 0;
  }
}

void drawBadenTinyText(int x, int y, uint16_t color, const char *txt) {
  int cx = x;
  for (int i = 0; txt[i]; i++) {
    char c = txt[i];
    if (c == ' ') { cx += 2; continue; }
    for (int r = 0; r < 5; r++) {
      uint8_t bits = badenTinyGlyph(c, r);
      for (int col = 0; col < 3; col++) {
        if (bits & (1 << (2 - col))) gfx->drawPixel(sx(cx + col), sy(y + r), color);
      }
    }
    cx += 4;
  }
}
uint8_t badenHeaderGlyph(char c, int row) {
  switch (c) {
    case ' ': { const uint8_t g[7] = {0,0,0,0,0,0,0}; return g[row]; }
    case ':': { const uint8_t g[7] = {0,0b010,0b010,0,0b010,0b010,0}; return g[row]; }
    case '0': { const uint8_t g[7] = {0b01110,0b10001,0b10011,0b10101,0b11001,0b10001,0b01110}; return g[row]; }
    case '1': { const uint8_t g[7] = {0b00100,0b01100,0b00100,0b00100,0b00100,0b00100,0b01110}; return g[row]; }
    case '2': { const uint8_t g[7] = {0b01110,0b10001,0b00001,0b00010,0b00100,0b01000,0b11111}; return g[row]; }
    case '3': { const uint8_t g[7] = {0b11110,0b00001,0b00001,0b01110,0b00001,0b00001,0b11110}; return g[row]; }
    case '4': { const uint8_t g[7] = {0b00010,0b00110,0b01010,0b10010,0b11111,0b00010,0b00010}; return g[row]; }
    case '5': { const uint8_t g[7] = {0b11111,0b10000,0b10000,0b11110,0b00001,0b00001,0b11110}; return g[row]; }
    case '6': { const uint8_t g[7] = {0b01110,0b10000,0b10000,0b11110,0b10001,0b10001,0b01110}; return g[row]; }
    case '7': { const uint8_t g[7] = {0b11111,0b00001,0b00010,0b00100,0b01000,0b01000,0b01000}; return g[row]; }
    case '8': { const uint8_t g[7] = {0b01110,0b10001,0b10001,0b01110,0b10001,0b10001,0b01110}; return g[row]; }
    case '9': { const uint8_t g[7] = {0b01110,0b10001,0b10001,0b01111,0b00001,0b00001,0b01110}; return g[row]; }
    case 'B': { const uint8_t g[7] = {0b11110,0b10001,0b10001,0b11110,0b10001,0b10001,0b11110}; return g[row]; }
    case 'S': { const uint8_t g[7] = {0b01111,0b10000,0b10000,0b01110,0b00001,0b00001,0b11110}; return g[row]; }
    case 'a': { const uint8_t g[7] = {0,0,0b01110,0b00001,0b01111,0b10001,0b01111}; return g[row]; }
    case 'e': { const uint8_t g[7] = {0,0,0b01110,0b10001,0b11111,0b10000,0b01110}; return g[row]; }
    case 'f': { const uint8_t g[7] = {0b00110,0b01000,0b01000,0b11100,0b01000,0b01000,0b01000}; return g[row]; }
    case 'g': { const uint8_t g[7] = {0,0,0b01111,0b10001,0b01111,0b00001,0b01110}; return g[row]; }
    case 'h': { const uint8_t g[7] = {0b10000,0b10000,0b10110,0b11001,0b10001,0b10001,0b10001}; return g[row]; }
    case 'i': { const uint8_t g[7] = {0b00100,0,0b01100,0b00100,0b00100,0b00100,0b01110}; return g[row]; }
    case 'n': { const uint8_t g[7] = {0,0,0b10110,0b11001,0b10001,0b10001,0b10001}; return g[row]; }
    case 'o': { const uint8_t g[7] = {0,0,0b01110,0b10001,0b10001,0b10001,0b01110}; return g[row]; }
    case 't': { const uint8_t g[7] = {0b01000,0b01000,0b11100,0b01000,0b01000,0b01001,0b00110}; return g[row]; }
    default: return 0;
  }
}

void drawBadenHeaderText(int x, int y, uint16_t color, const String &txt) {
  int cx = x;
  for (int i = 0; i < txt.length(); i++) {
    char c = txt[i];
    int charW = (c == ' ' || c == ':') ? 3 : 5;
    for (int r = 0; r < 7; r++) {
      uint8_t bits = badenHeaderGlyph(c, r);
      for (int col = 0; col < 5; col++) {
        if (bits & (1 << (4 - col))) {
          int px = cx + col;
          int py = y + r + (r / 2);
          gfx->drawPixel(sx(px), sy(py), color);
          gfx->drawPixel(sx(px + 1), sy(py), color);
          gfx->drawPixel(sx(px), sy(py + 1), color);
        }
      }
    }
    cx += charW + 2;
  }
}

void drawBadenBadenHeader() {
  uint16_t swGreen = 0x04A6;
  uint16_t swBlue = 0x053F;
  uint16_t paleLine = 0xBDF7;

  gfx->fillRect(4, 6, 232, 25, C_WHITE);
  gfx->drawFastHLine(4, 31, 232, paleLine);

  // Logo horizontal discret, comme la photo: plus de texte Baden-Baden sous le logo.
  gfx->drawLine(23, 11, 31, 8, swGreen);
  gfx->drawLine(22, 16, 30, 12, swGreen);
  gfx->drawLine(21, 21, 29, 17, swGreen);
  gfx->drawLine(20, 26, 28, 22, swGreen);
  drawBadenTinyText(5, 16, swBlue, "STADTWERKE");

  String badenTitle = "Bahnhof Steig " + badenSteig;
  drawBadenHeaderText(60, 13, swBlue, badenTitle);
  String clock = pm3dDynamicClock();
  drawBadenHeaderText(199, 13, swBlue, clock);
}

void drawBadenBadenRows() {
  uint16_t blueA = 0x357F;
  uint16_t blueB = 0x253F;
  uint16_t line = 0xBDF7;
  gfx->fillRect(3, 3, 234, 129, blueA);
  drawBadenBadenHeader();

  for (int i = 0; i < 6; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = 32 + i * 16;
    gfx->fillRect(4, y, 232, 15, (i % 2) ? blueB : blueA);
    gfx->drawFastHLine(4, y + 15, 232, line);
    smallText(10, y + 4, C_WHITE, cutText(rows[idx].typeTrain, 5));
    smallText(54, y + 4, C_WHITE, cutText(rows[idx].destination, 23));
    smallText(197, y + 4, C_WHITE, cutText(rows[idx].heure, 6));
  }
}
void drawNsLightModernRows() {
  uint16_t blue = 0x21B5;
  uint16_t blue2 = 0x31F7;
  uint16_t paleA = 0xE73C;
  uint16_t paleB = 0xCE79;
  uint16_t grid = 0xAD75;
  uint16_t ink = 0x18E3;
  gfx->fillRect(3, 3, 234, 129, 0x0841);
  gfx->fillRect(4, 6, 232, 18, blue);
  gfx->fillRect(4, 24, 232, 84, paleA);
  mediumText(8, 10, C_WHITE, "09:56");
  mediumText(53, 10, C_WHITE, "Vertrek van de treinen");
  gfx->fillRect(4, 27, 232, 11, 0xD69A);
  smallText(8, 30, ink, "Vertrek");
  smallText(43, 30, ink, "Naar");
  smallText(112, 30, ink, "Opmerking");
  smallText(202, 30, ink, "Spoor");
  for (int i = 0; i < 6; i++) {
    int idx = visibleRowIndex(i);
    int y = 39 + i * 11;
    gfx->fillRect(4, y, 232, 10, (i % 2) ? paleB : paleA);
    gfx->drawFastHLine(4, y + 10, 232, grid);
    smallText(8, y + 2, ink, cutText(rows[idx].heure, 5));
    smallText(43, y + 2, ink, cutText(rows[idx].destination, 13));
    smallText(112, y + 2, ink, cutText(rows[idx].info, 17));
    gfx->fillRect(202, y + 1, 11, 9, C_WHITE);
    gfx->drawRect(202, y + 1, 11, 9, grid);
    smallText(206, y + 2, ink, cutText(rows[idx].voie, 2));
    smallText(216, y + 1, ink, cutText(rows[idx].typeTrain, 8));
  }
  gfx->fillRect(4, 109, 232, 21, 0xE73C);
  gfx->drawFastHLine(4, 109, 232, grid);
  gfx->fillCircle(15, 119, 8, blue);
  smallText(12, 115, C_WHITE, "i");
  smallText(29, 112, ink, cutText(transilienInfoTitle, 20));
  smallText(29, 121, ink, cutText(transilienInfoText, 38));
  smallText(219, 116, ink, "2/2");
}

void drawUkModernRows() {
  gfx->fillRect(4, 25, 232, 106, C_BLACK);
  for (int c = 0; c < 4; c++) {
    int x = 5 + c * 58;
    int idx = visibleRowIndex(scrollOffset + c);
    gfx->fillRect(x, 26, 56, 25, c < 2 ? 0x3666 : 0x255F);
    colSmallText(0, x + 3, 29, C_WHITE, rows[idx].heure);
    colSmallText(2, x + 3, 40, C_WHITE, cutText(rows[idx].destination, 7));
    gfx->drawRect(x, 52, 56, 76, C_GREY);
    for (int r = 0; r < 5; r++) smallText(x + 4, 56 + r * 12, C_WHITE, cutText(rows[visibleRowIndex(idx + r)].destination, 8));
  }
}


void drawSwissCffBlueRows() {
  gfx->fillRect(3, 3, 234, 129, C_BLUE_DARK);
  gfx->fillRect(4, 4, 232, 15, C_BLUE_TOP);
  smallText(8, 7, C_WHITE, "CFF SBB FFS");
  smallText(72, 7, C_WHITE, "Destination");
  smallText(174, 7, C_WHITE, "Voie");
  smallText(205, 7, C_WHITE, "Rem.");
  gfx->drawFastHLine(4, 20, 232, C_GRID);
  gfx->drawFastVLine(38, 20, 92, C_GRID);
  gfx->drawFastVLine(170, 20, 92, C_GRID);
  gfx->drawFastVLine(200, 20, 92, C_GRID);
  for (int i = 0; i < min(nbVisible, 9); i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = 22 + i * 10;
    uint16_t bg = (i % 2 == 0) ? 0x001F : 0x085F;
    gfx->fillRect(4, y, 232, 9, bg);
    gfx->drawFastHLine(4, y + 9, 232, C_GRID);
    if (rows[idx].typeTrain.length()) {
      gfx->fillRect(6, y + 1, 24, 8, (rows[idx].typeTrain.startsWith("IR") || rows[idx].typeTrain.startsWith("IC")) ? C_RED : C_BLUE_ROW2);
      colSmallText(3, 8, y + 2, C_WHITE, cutText(rows[idx].typeTrain, 3));
    }
    colSmallText(0, 41, y + 2, C_WHITE, rows[idx].heure);
    colSmallText(2, 72, y + 2, C_WHITE, cutText(rows[idx].destination, 15));
    colSmallText(4, 180, y + 2, C_WHITE, cutText(rows[idx].voie, 2));
    if (rows[idx].info.length()) colSmallText(1, 205, y + 2, C_WHITE, cutText(rows[idx].info, 4));
  }
  gfx->fillRect(4, 114, 232, 18, C_RED);
  smallText(7, 117, C_WHITE, "Info CFF: verifiez l'horaire avant votre voyage");
  smallText(7, 126, C_WHITE, "www.cff.ch");
}

void drawSwissBernArrivalRows() {
  uint16_t paper = 0xEF7D;
  uint16_t table = 0xE71C;
  uint16_t grid = 0x9CD3;
  uint16_t ink = C_BLACK;
  gfx->fillRect(3, 3, 234, 129, paper);
  gfx->drawRect(3, 3, 234, 129, 0x39E7);
  gfx->fillRect(37, 14, 18, 22, 0xD6BA);
  gfx->drawRect(37, 14, 18, 22, ink);
  gfx->drawFastHLine(40, 25, 12, ink);
  gfx->drawFastVLine(43, 18, 12, ink);
  gfx->drawFastVLine(50, 18, 12, ink);
  gfx->fillRect(41, 18, 10, 5, ink);
  gfx->fillCircle(43, 32, 2, ink);
  gfx->fillCircle(50, 32, 2, ink);
  mediumText(64, 20, ink, "Ankunft");
  mediumText(119, 20, ink, "Arrivee");
  mediumText(177, 20, ink, "Arrivo");
  gfx->fillRect(37, 44, 189, 75, table);
  gfx->drawRect(37, 44, 189, 75, ink);
  gfx->fillRect(38, 45, 187, 10, 0xD6BA);
  smallText(78, 47, ink, "Von");
  smallText(172, 47, ink, "Gleis");
  smallText(195, 47, ink, "Hinweis");
  gfx->drawFastVLine(69, 45, 73, grid);
  gfx->drawFastVLine(169, 45, 73, grid);
  gfx->drawFastVLine(194, 45, 73, grid);
  for (int i = 0; i < 12; i++) {
    int idx = visibleRowIndex(i);
    int y = 56 + i * 5;
    gfx->drawFastHLine(38, y + 4, 187, grid);
    String train = rows[idx].typeTrain;
    uint16_t tag = train.startsWith("IC") || train.startsWith("IR") || train == "RE" ? C_RED : (train.startsWith("S") ? 0x02DF : C_WHITE);
    if (train.length()) {
      gfx->fillRect(39, y, 16, 5, tag);
      smallText(40, y - 1, tag == C_WHITE ? ink : C_WHITE, cutText(train, 3));
    }
    smallText(56, y - 1, ink, cutText(rows[idx].heure, 5));
    smallText(78, y - 1, ink, cutText(rows[idx].destination, 20));
    smallText(174, y - 1, ink, cutText(rows[idx].voie, 4));
    if (rows[idx].info.length()) smallText(197, y - 1, ink, cutText(rows[idx].info, 9));
  }
}

void drawSwissRomandieRows() {
  gfx->fillRect(3, 3, 234, 129, 0xFDA0);
  gfx->drawRect(9, 12, 18, 18, C_BLACK);
  smallText(13, 16, C_BLACK, "SBB");
  mediumText(34, 16, C_BLACK, "Abfahrt");
  mediumText(91, 16, C_BLACK, "Depart");
  mediumText(151, 16, C_BLACK, "Partenza");
  gfx->fillRect(9, 38, 222, 72, 0x001F);
  gfx->drawRect(9, 38, 222, 72, C_BLACK);
  smallText(62, 40, C_WHITE, "Destination");
  smallText(166, 40, C_WHITE, "Voie");
  smallText(189, 40, C_WHITE, "Remarque");
  for (int i = 0; i < 10; i++) {
    int idx = visibleRowIndex(i);
    int y = 48 + i * 6;
    gfx->drawFastHLine(10, y + 5, 220, 0x7BFF);
    uint16_t tagBg = rows[idx].typeTrain.startsWith("T") ? C_RED : (rows[idx].typeTrain.startsWith("R") ? C_WHITE : C_YELLOW);
    gfx->fillRect(12, y, 17, 5, tagBg);
    smallText(13, y - 1, tagBg == C_WHITE ? C_BLACK : C_WHITE, cutText(rows[idx].typeTrain, 3));
    smallText(34, y - 1, C_WHITE, cutText(rows[idx].heure, 5));
    smallText(67, y - 1, C_WHITE, cutText(rows[idx].destination, 19));
    smallText(171, y - 1, C_WHITE, cutText(rows[idx].voie, 2));
    if (rows[idx].info.length()) smallText(189, y - 1, C_WHITE, cutText(rows[idx].info, 10));
  }
  gfx->fillRect(9, 110, 222, 18, C_RED);
  smallText(12, 113, C_WHITE, "Suisse Romande : Horaire d'ete. Voyageurs Bern");
  smallText(12, 122, C_WHITE, "et au-dela voyagent via Neuchatel-Bienne.");
}

void drawDbModernPhotoRows() {
  // Allemagne photo 1 : grand tableau DB bleu clair Abfahrt/Departure.
  gfx->fillRect(3, 3, 234, 129, 0x043F);
  gfx->fillRect(4, 4, 232, 17, 0x04BF);
  smallText(8, 8, C_WHITE, "Abfahrt / Departure");
  smallText(205, 8, C_WHITE, "DB");
  gfx->fillRect(5, 23, 230, 12, 0x025F);
  smallText(8, 26, C_WHITE, "Time");
  smallText(46, 26, C_WHITE, "Train");
  smallText(87, 26, C_WHITE, "Destination");
  smallText(205, 26, C_WHITE, "Pl.");
  int maxRows = min(nbVisible, 7);
  for (int i = 0; i < maxRows; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = 37 + i * 13;
    gfx->fillRect(5, y, 230, 12, (i % 2) ? 0x02DF : 0x037F);
    colSmallText(0, 8, y + 2, C_WHITE, rows[idx].heure);
    colSmallText(3, 46, y + 2, C_WHITE, cutText(rows[idx].typeTrain, 4));
    colSmallText(2, 87, y + 2, C_WHITE, cutText(rows[idx].destination, 15));
    gfx->fillRect(205, y + 1, 26, 10, C_WHITE);
    colSmallText(4, 213, y + 3, C_BLACK, cutText(rows[idx].voie, 2));
  }
}

void drawDbClockPhotoRows() {
  // Allemagne photo 2 : tableau bleu fonce DB avec grande horloge.
  gfx->fillRect(3, 3, 234, 129, 0x0015);
  gfx->fillRect(4, 4, 232, 22, 0x011F);
  smallText(8, 8, C_WHITE, "DB");
  mediumText(148, 6, C_WHITE, "12:24");
  smallText(8, 29, C_WHITE, "Zeit");
  smallText(44, 29, C_WHITE, "Zug");
  smallText(82, 29, C_WHITE, "Richtung");
  smallText(207, 29, C_WHITE, "Gl.");
  int maxRows = min(nbVisible, 7);
  for (int i = 0; i < maxRows; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = 41 + i * 12;
    gfx->fillRect(5, y, 230, 11, (i % 2) ? 0x081F : 0x001F);
    gfx->drawFastHLine(5, y + 11, 230, 0x4A7F);
    colSmallText(0, 8, y + 1, C_WHITE, rows[idx].heure);
    colSmallText(3, 44, y + 1, C_WHITE, cutText(rows[idx].typeTrain, 4));
    colSmallText(2, 82, y + 1, C_WHITE, cutText(rows[idx].destination, 17));
    colSmallText(4, 213, y + 1, C_YELLOW, cutText(rows[idx].voie, 2));
  }
}

void drawUkSplitFlapPhotoRows() {
  gfx->fillRect(3, 3, 234, 129, C_BLACK);
  gfx->fillRect(4, 4, 232, 15, C_WHITE);
  smallText(8, 8, C_BLACK, "British Rail  DEPARTURES");
  gfx->drawFastHLine(4, 21, 232, C_GREY);
  smallText(8, 24, C_WHITE, "TIME");
  smallText(48, 24, C_WHITE, "DESTINATION");
  smallText(170, 24, C_WHITE, "PLAT");
  smallText(205, 24, C_WHITE, "STATUS");
  for (int i = 0; i < min(nbVisible, 6); i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = 36 + i * 15;
    gfx->fillRect(6, y, 226, 12, C_BLACK);
    gfx->drawRect(6, y, 34, 12, C_GREY);
    gfx->drawRect(42, y, 124, 12, C_GREY);
    gfx->drawRect(168, y, 30, 12, C_GREY);
    gfx->drawRect(200, y, 32, 12, C_GREY);
    gfx->drawFastHLine(7, y + 6, 32, 0x4208);
    gfx->drawFastHLine(43, y + 6, 122, 0x4208);
    colSmallText(0, 9, y + 2, C_WHITE, rows[idx].heure);
    colSmallText(2, 47, y + 2, C_WHITE, cutText(rows[idx].destination, 16));
    colSmallText(4, 176, y + 2, C_WHITE, cutText(rows[idx].voie, 2));
    smallText(203, y + 2, C_WHITE, rows[idx].info.length() ? cutText(rows[idx].info, 4) : "ON");
  }
}


void drawOebbGreenPhotoRows() {
  gfx->fillRect(3, 3, 234, 129, 0x0140);
  gfx->fillRect(4, 4, 232, 21, 0x0220);
  smallText(12, 9, C_YELLOW, "Ankunft");
  pseudoItalicText(76, 9, 1, C_YELLOW, 0x0220, "Arrival");
  smallText(145, 9, C_WHITE, "11:13:56");
  drawOebbLogo(207, 7, C_WHITE, 0x0220);
  tinyDbCellText(207, 16, 25, C_WHITE, 0x0220, "INFRA");
  smallText(14, 27, C_WHITE, "Zeit");
  smallText(43, 27, C_WHITE, "Erw.");
  smallText(91, 27, C_WHITE, "Zug");
  smallText(119, 27, C_WHITE, "Von");
  smallText(205, 27, C_YELLOW, "Bstg");
  int rowsToDraw = constrain(nbVisible, 1, MAX_ROWS);
  int rowTop = 37;
  int rowH = max(5, (119 - rowTop) / max(1, rowsToDraw));
  bool bigRows = rowH >= 16;
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    uint16_t rowBg = (i % 2) ? 0x0260 : 0x0380;
    int ty = y + max(0, (rowH - 8) / 2);
    gfx->fillRect(5, y, 229, rowH - 1, rowBg);
    drawOebbStatusDot(10, y + rowH / 2, rows[idx], rowBg);
    if (bigRows) smallText(15, ty, C_WHITE, cutText(rows[idx].heure, 5)); else tinyDbCellText(15, ty + 1, 24, C_WHITE, rowBg, rows[idx].heure);
    tinyDbCellText(43, ty + 1, 20, C_WHITE, rowBg, delayText(rows[idx]));
    drawRailCompanyBox(64, y + max(1, (rowH - 7) / 2), 22, 7, oebbCompanyForRow(rows[idx]), rowBg);
    oebbFitText(91, y + 1, 24, rowH - 2, C_WHITE, rowBg, rows[idx].typeTrain);
    oebbFitText(119, y + 1, 56, rowH - 2, C_WHITE, rowBg, rows[idx].destination);
    oebbFitText(178, y + 1, 27, rowH - 2, 0xDFF7, rowBg, oebbCleanInfo(rows[idx].info));
    smallText(209, ty, C_WHITE, cutText(oebbTrackText(rows[idx]), 4));
  }
  gfx->fillRect(5, 119, 230, 10, 0xCE79);
  smallText(8, 120, C_BLACK, "08.04 - 10.04: Bauarbeiten. Zuege verkehren");
  smallText(8, 127, C_BLACK, "zwischen Graz Hbf und Frohnleiten eingeschraenkt.");
}

void drawOebbBluePhotoRows() {
  gfx->fillRect(3, 3, 234, 129, 0x0016);
  gfx->fillRect(5, 5, 230, 20, 0x001F);
  smallText(14, 9, C_YELLOW, "Abfahrt");
  pseudoItalicText(75, 9, 1, C_YELLOW, 0x001F, "Departure");
  smallText(145, 9, C_WHITE, "11:13:56");
  drawOebbLogo(207, 7, C_WHITE, 0x001F);
  tinyDbCellText(207, 16, 25, C_WHITE, 0x001F, "INFRA");
  smallText(14, 27, C_WHITE, "Zeit");
  smallText(43, 27, C_WHITE, "Erw.");
  smallText(91, 27, C_WHITE, "Zug");
  smallText(119, 27, C_WHITE, "Nach");
  smallText(207, 27, C_YELLOW, "Bstg");
  int rowsToDraw = constrain(nbVisible, 1, MAX_ROWS);
  int rowTop = 37;
  int rowH = max(5, (119 - rowTop) / max(1, rowsToDraw));
  bool bigRows = rowH >= 16;
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    uint16_t rowBg = (i % 2) ? 0x0195 : 0x045F;
    int ty = y + max(0, (rowH - 8) / 2);
    gfx->fillRect(5, y, 229, rowH - 1, rowBg);
    drawOebbStatusDot(10, y + rowH / 2, rows[idx], rowBg);
    if (bigRows) smallText(15, ty, C_WHITE, cutText(rows[idx].heure, 5)); else tinyDbCellText(15, ty + 1, 24, C_WHITE, rowBg, rows[idx].heure);
    tinyDbCellText(43, ty + 1, 20, C_WHITE, rowBg, delayText(rows[idx]));
    drawRailCompanyBox(64, y + max(1, (rowH - 7) / 2), 22, 7, oebbCompanyForRow(rows[idx]), rowBg);
    oebbFitText(91, y + 1, 24, rowH - 2, C_WHITE, rowBg, rows[idx].typeTrain);
    oebbFitText(119, y + 1, 86, rowH - 2, C_WHITE, rowBg, rows[idx].destination);
    smallText(209, ty, C_WHITE, cutText(oebbTrackText(rows[idx]), 4));
  }
  gfx->fillRect(5, 119, 230, 10, 0xDEFB);
  smallText(8, 120, C_BLACK, "Bitte Busfahrplaene beachten!");
  smallText(8, 127, C_BLACK, "Information: oebb.at | SCOTTY mobil");
}

void drawOebbWhiteArrivalRows() {
  gfx->fillRect(0, 0, 240, 135, C_WHITE);
  gfx->fillRect(3, 3, 234, 126, 0x045F);
  gfx->fillRect(3, 3, 234, 27, C_WHITE);
  gfx->drawRect(3, 3, 234, 126, 0x045F);
  smallText(10, 10, C_BLACK, "Ankunft");
  smallText(66, 10, C_BLACK, "Arrival");
  smallText(157, 10, 0x2945, "15:19");
  drawOebbLogo(209, 10, C_RED, C_WHITE);
  smallText(8, 33, C_WHITE, "Zeit");
  smallText(51, 33, C_WHITE, "Zug");
  smallText(105, 33, C_WHITE, "Ziel");
  smallText(207, 33, C_WHITE, "Gl.");
  int rowsToDraw = constrain(nbVisible, 1, MAX_ROWS);
  int rowTop = 43;
  int rowH = max(6, (116 - rowTop) / max(1, rowsToDraw));
  bool bigRows = rowH >= 17;
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(i);
    int y = rowTop + i * rowH;
    int ty = y + max(0, (rowH - 8) / 2);
    gfx->fillRect(5, y, 230, rowH - 1, (i % 2) ? 0x02DF : 0x021F);
    smallText(8, ty, C_WHITE, rows[idx].heure);
    smallText(49, ty, C_WHITE, cutText(rows[idx].typeTrain, 8));
    if (bigRows) colMediumText(2, 101, y + 1, C_WHITE, cutText(rows[idx].destination, 9)); else smallText(101, ty, C_WHITE, cutText(rows[idx].destination, 16));
    smallText(211, ty, C_WHITE, cutText(rows[idx].voie, 2));
  }
  gfx->fillRect(4, 116, 232, 13, 0xFEE0);
  smallText(8, 119, C_BLACK, "Einzelne Regionalzuege zwischen Wien und Linz");
}

void drawOebbBlueDenseRows() {
  gfx->fillRect(3, 3, 234, 129, 0x0016);
  gfx->fillRect(5, 5, 230, 20, 0x001F);
  smallText(14, 10, C_YELLOW, "Abfahrt");
  smallText(72, 10, C_YELLOW, "Departure");
  smallText(145, 10, C_WHITE, "15:08:52");
  drawOebbLogo(210, 8, C_WHITE, 0x001F);
  smallText(210, 15, C_WHITE, "infra");
  smallText(8, 27, C_WHITE, "Zeit");
  smallText(39, 27, C_WHITE, "Zug");
  smallText(72, 27, C_WHITE, "Nach");
  smallText(142, 27, C_WHITE, "Via");
  smallText(205, 27, C_WHITE, "Bstg");
  int rowsToDraw = constrain(nbVisible, 1, MAX_ROWS);
  int rowTop = 37;
  int rowH = max(5, (119 - rowTop) / max(1, rowsToDraw));
  bool bigRows = rowH >= 16;
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(i);
    int y = rowTop + i * rowH;
    int ty = y + max(0, (rowH - 8) / 2);
    gfx->fillRect(5, y, 229, rowH - 1, (i % 2) ? 0x045F : 0x0195);
    if (bigRows) smallText(8, ty, C_WHITE, cutText(rows[idx].heure, 5)); else tinyDbCellText(8, ty + 1, 24, C_WHITE, (i % 2) ? 0x045F : 0x0195, rows[idx].heure);
    smallText(39, ty, C_WHITE, cutText(rows[idx].typeTrain, 5));
    if (bigRows) colMediumText(2, 72, y + 1, C_WHITE, cutText(rows[idx].destination, 8)); else smallText(72, ty, C_WHITE, cutText(rows[idx].destination, 11));
    smallText(142, ty, 0xDFF7, cutText(rows[idx].info, 11));
    smallText(207, ty, C_WHITE, cutText(rows[idx].voie, 4));
  }
  gfx->fillRect(5, 119, 230, 10, 0xFEE0);
  smallText(8, 120, C_BLACK, "08-11.08: Schienenersatzverkehr und Umleitung");
  smallText(8, 127, C_BLACK, "Fernverkehr ueber Salzburg und Woergl.");
}

void drawOebbTealArrivalRows() {
  gfx->fillRect(3, 3, 234, 129, 0x0148);
  gfx->fillRect(4, 4, 232, 18, 0x018B);
  smallText(10, 8, C_WHITE, "Ankunft       Arrival");
  smallText(165, 10, C_WHITE, "09:05");
  drawOebbLogo(207, 8, C_WHITE, 0x018B);
  smallText(8, 24, C_WHITE, "Zeit");
  smallText(42, 24, C_WHITE, "Zug");
  smallText(96, 24, C_WHITE, "Von");
  smallText(205, 24, C_WHITE, "Gl.");
  int rowsToDraw = constrain(nbVisible, 1, MAX_ROWS);
  int rowTop = 34;
  int rowH = max(6, (120 - rowTop) / max(1, rowsToDraw));
  bool bigRows = rowH >= 16;
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(i);
    int y = rowTop + i * rowH;
    int ty = y + max(0, (rowH - 8) / 2);
    gfx->fillRect(5, y, 230, rowH - 1, (i % 2) ? 0x02A9 : 0x0187);
    smallText(8, ty, C_WHITE, cutText(rows[idx].heure, 5));
    smallText(42, ty, C_WHITE, cutText(rows[idx].typeTrain, 7));
    if (bigRows) colMediumText(2, 96, y + 1, C_WHITE, cutText(rows[idx].destination, 9)); else smallText(96, ty, C_WHITE, cutText(rows[idx].destination, 16));
    smallText(210, ty, C_WHITE, cutText(rows[idx].voie, 3));
  }
}

void drawZurichFernverkehrRows() {
  gfx->fillRect(3, 3, 234, 129, C_BLUE_DARK);
  gfx->fillRect(5, 7, 226, 13, C_RED);
  smallText(12, 10, C_WHITE, "Fernverkehr");
  for (int i = 0; i < 11; i++) {
    int idx = visibleRowIndex(i);
    int y = 23 + i * 9;
    gfx->drawFastHLine(5, y + 8, 226, 0x7BFF);
    smallText(8, y + 1, C_RED, cutText(rows[idx].typeTrain, 5));
    smallText(38, y + 1, C_WHITE, cutText(rows[idx].heure, 5));
    smallText(72, y + 1, C_WHITE, cutText(rows[idx].destination, 21));
    smallText(206, y + 1, C_YELLOW, cutText(rows[idx].voie, 2));
  }
}

void drawJapanGreenLedRows() {
  gfx->fillRect(3, 3, 234, 129, C_BLACK);
  gfx->fillRect(4, 5, 232, 20, 0x18E3);
  smallText(18, 8, C_WHITE, "Tokaido, Sanyo Shinkansen Departures");
  smallText(10, 26, 0xBDF7, "Train");
  smallText(74, 26, 0xBDF7, "Time");
  smallText(116, 26, 0xBDF7, "Destination");
  smallText(194, 26, 0xBDF7, "Car");
  for (int i = 0; i < 5; i++) {
    int idx = visibleRowIndex(i);
    int y = 38 + i * 17;
    uint16_t trainColor = rows[idx].typeTrain.startsWith("HIKARI") ? C_RED : (rows[idx].typeTrain.startsWith("KODAMA") ? 0x041F : C_AMBER);
    smallText(9, y, trainColor, cutText(rows[idx].typeTrain, 10));
    mediumText(72, y - 2, C_WHITE, cutText(rows[idx].heure, 5));
    mediumText(116, y - 2, C_WHITE, cutText(rows[idx].destination, 10));
    smallText(190, y, C_WHITE, cutText(rows[idx].info, 8));
  }
  smallText(50, 121, C_WHITE, "[Car information]  Track 23");
}

void drawTokyoGreyRows() {
  gfx->fillRect(3, 3, 234, 129, 0x8410);
  gfx->fillRect(4, 4, 232, 18, 0x39E7);
  smallText(12, 8, C_WHITE, "Next Departure");
  mediumText(120, 5, C_WHITE, "Track 23");
  smallText(206, 8, C_WHITE, "JR");
  smallText(9, 25, 0xD69A, "Train");
  smallText(56, 25, 0xD69A, "No.");
  smallText(91, 25, 0xD69A, "Time");
  smallText(132, 25, 0xD69A, "Destination");
  smallText(205, 25, 0xD69A, "Cars");
  for (int i = 0; i < 3; i++) {
    int idx = visibleRowIndex(i);
    int y = 38 + i * 28;
    gfx->fillRect(7, y, 226, 24, (i % 2) ? 0x8C51 : 0x6B4D);
    gfx->drawFastVLine(7, y, 24, C_YELLOW);
    smallText(13, y + 3, 0xF7DE, cutText(rows[idx].typeTrain, 10));
    mediumText(84, y + 1, C_WHITE, cutText(rows[idx].heure, 5));
    mediumText(130, y + 1, C_WHITE, cutText(rows[idx].destination, 10));
    smallText(205, y + 3, C_WHITE, cutText(rows[idx].info, 7));
    smallText(13, y + 15, 0xE71C, cutText(rows[idx].voie, 18));
  }
  smallText(35, 120, 0xE71C, "for Karuizawa and Nagano");
}

void drawSaintLazareRows() {
  gfx->fillRect(3, 3, 234, 129, 0x001F);
  mediumText(10, 8, C_WHITE, "Departs Ile-de-France");
  for (int col = 0; col < 2; col++) {
    int x = 8 + col * 116;
    for (int r = 0; r < 8; r++) {
      int idx = col * 8 + r;
      int y = 32 + r * 11;
      gfx->fillRect(x, y, 110, 10, (r % 2) ? 0x039F : 0x04BF);
      smallText(x + 2, y + 2, C_WHITE, cutText(rows[idx].destination, 15));
      smallText(x + 67, y + 1, C_YELLOW, cutText(rows[idx].heure, 5));
      smallText(x + 92, y + 1, C_WHITE, cutText(rows[idx].voie, 2));
    }
  }
}

void drawStockholmRows() {
  gfx->fillRect(3, 3, 234, 129, C_BLACK);
  smallText(8, 8, C_WHITE, "Avgaende forts.  Departures cont.");
  for (int i = 0; i < 8; i++) {
    int idx = visibleRowIndex(i);
    int y = 24 + i * 12;
    gfx->fillRect(5, y, 230, 10, (i % 2) ? 0x0320 : 0x0220);
    smallText(8, y + 2, C_YELLOW, cutText(rows[idx].heure, 5));
    smallText(44, y + 2, C_YELLOW, cutText(rows[idx].destination, 15));
    smallText(158, y + 2, C_WHITE, cutText(rows[idx].typeTrain, 5));
    smallText(205, y + 2, C_WHITE, cutText(rows[idx].voie, 3));
  }
}

void drawMavPhotoRows(bool arrivals) {
  uint16_t bg = arrivals ? 0x0186 : 0x0258;
  uint16_t head = arrivals ? 0x04A8 : 0x041F;
  uint16_t rowA = arrivals ? 0x04A6 : 0x03BF;
  uint16_t rowB = arrivals ? 0x0365 : 0x02DF;
  gfx->fillRect(3, 3, 234, 129, bg);
  gfx->fillRect(4, 4, 232, 19, head);
  smallText(8, 8, C_WHITE, arrivals ? "12:18" : "12:18:47");
  mediumText(73, 6, C_WHITE, arrivals ? "ERKEZO VONATOK" : "INDULO VONATOK");
  smallText(188, 8, C_WHITE, arrivals ? "Arrivals" : "Departures");
  smallText(8, 26, C_WHITE, "Time");
  smallText(38, 26, C_WHITE, "Train");
  smallText(72, 26, C_WHITE, arrivals ? "From" : "To");
  smallText(154, 26, C_WHITE, arrivals ? "Via" : "Through");
  smallText(219, 26, C_WHITE, "Tr.");
  for (int i = 0; i < 11; i++) {
    int idx = visibleRowIndex(i);
    int y = 35 + i * 8;
    gfx->fillRect(5, y, 228, 7, (i % 2) ? rowB : rowA);
    smallText(8, y, C_WHITE, cutText(rows[idx].heure, 5));
    smallText(38, y, C_WHITE, cutText(rows[idx].typeTrain, 4));
    smallText(72, y, C_WHITE, cutText(rows[idx].destination, 12));
    smallText(154, y, C_WHITE, cutText(rows[idx].info, 11));
    smallText(221, y, C_WHITE, cutText(rows[idx].voie, 2));
  }
  gfx->fillRect(5, 121, 228, 8, arrivals ? 0x04A8 : 0x041F);
  smallText(8, 122, C_WHITE, arrivals ? "Budapest arrivals - MAV START" : "Budapest departures - MAV START");
}

void drawBarcelonaRodaliesGridRows() {
  gfx->fillRect(3, 3, 234, 129, 0xDEFB);
  gfx->fillRect(4, 4, 232, 15, 0x04A8);
  smallText(9, 8, C_WHITE, "18:25");
  smallText(54, 8, C_WHITE, "Primer Tren");
  smallText(119, 8, C_WHITE, "Segon Tren");
  smallText(190, 8, C_WHITE, "Rodalies");
  for (int c = 0; c < 2; c++) {
    int x = 5 + c * 116;
    for (int r = 0; r < 8; r++) {
      int idx = visibleRowIndex(c * 8 + r);
      int y = 22 + r * 13;
      gfx->fillRect(x, y, 114, 12, (r % 2) ? 0xE71C : C_WHITE);
      smallText(x + 2, y + 2, C_BLACK, cutText(rows[idx].destination, 11));
      gfx->fillRect(x + 52, y + 2, 12, 8, 0x05C9);
      smallText(x + 55, y + 3, C_WHITE, cutText(rows[idx].typeTrain, 2));
      smallText(x + 70, y + 2, C_BLACK, cutText(rows[idx].heure, 5));
      smallText(x + 101, y + 2, C_BLACK, cutText(rows[idx].voie, 2));
    }
  }
}

void drawPolandBlueRows(bool arrivals) {
  gfx->fillRect(3, 3, 234, 129, 0x0010);
  gfx->fillRect(4, 4, 232, 22, 0x0841);
  mediumText(8, 8, arrivals ? 0xA5B2 : C_AMBER, arrivals ? "Przyjazdy" : "Odjazdy");
  smallText(79, 11, 0xBDF7, arrivals ? "Arrivals" : "Departures");
  gfx->fillCircle(170, 15, 11, 0xC618);
  gfx->drawCircle(170, 15, 11, C_BLACK);
  smallText(9, 28, C_WHITE, "Czas");
  smallText(42, 28, C_WHITE, "Pociag");
  smallText(82, 28, C_WHITE, arrivals ? "Z" : "Do");
  smallText(196, 28, C_WHITE, "Peron");
  for (int i = 0; i < 8; i++) {
    int idx = visibleRowIndex(i);
    int y = 39 + i * 11;
    gfx->fillRect(5, y, 230, 10, (i % 2) ? 0x02DF : 0x039F);
    smallText(8, y + 1, C_WHITE, cutText(rows[idx].heure, 5));
    smallText(42, y + 1, 0xDFF7, cutText(rows[idx].typeTrain, 7));
    smallText(82, y + 1, C_WHITE, cutText(rows[idx].destination, 17));
    smallText(198, y + 1, C_WHITE, cutText(rows[idx].voie, 3));
  }
}

void drawLosAngelesRows() {
  gfx->fillRect(3, 3, 234, 129, C_BLACK);
  gfx->fillRect(5, 5, 230, 22, 0x05FF);
  mediumText(50, 10, C_BLACK, "TRAIN INFORMATION");
  smallText(10, 34, C_AMBER, "Train");
  smallText(65, 34, C_AMBER, "Line");
  smallText(119, 34, C_AMBER, "Destination");
  smallText(190, 34, C_AMBER, "Time");
  for (int i = 0; i < 4; i++) {
    int idx = visibleRowIndex(i);
    int y = 47 + i * 18;
    gfx->fillRect(6, y, 228, 14, (i % 2) ? 0x1082 : C_BLACK);
    smallText(9, y + 4, 0xBDF7, cutText(rows[idx].typeTrain, 8));
    smallText(64, y + 4, C_WHITE, cutText(rows[idx].info, 8));
    smallText(119, y + 4, C_WHITE, cutText(rows[idx].destination, 12));
    smallText(188, y + 4, C_WHITE, cutText(rows[idx].heure, 7));
  }
}

void drawBarcelonaAdifRows() {
  gfx->fillRect(3, 3, 234, 129, 0xD69A);
  gfx->fillRect(4, 4, 232, 18, 0xE71C);
  mediumText(8, 8, 0x6B4D, "10:48 /5");
  smallText(50, 8, C_BLACK, "Sortides | Salidas | Departures");
  smallText(8, 25, 0x6B4D, "Hora");
  smallText(52, 25, 0x6B4D, "Desti");
  smallText(141, 25, 0x6B4D, "Tren");
  smallText(206, 25, 0x6B4D, "Via");
  for (int i = 0; i < 8; i++) {
    int idx = visibleRowIndex(i);
    int y = 38 + i * 11;
    gfx->fillRect(5, y, 229, 10, (i % 2) ? 0xC638 : 0xEF7D);
    smallText(8, y + 1, 0x4A49, cutText(rows[idx].heure, 5));
    smallText(52, y + 1, C_BLACK, cutText(rows[idx].destination, 18));
    smallText(144, y + 1, C_RED, cutText(rows[idx].typeTrain, 3));
    smallText(206, y + 1, C_BLACK, cutText(rows[idx].voie, 5));
  }
}

void drawNaplesAmberRows() {
  gfx->fillRect(3, 3, 234, 129, 0x1082);
  gfx->drawCircle(27, 42, 25, C_WHITE);
  smallText(8, 8, C_WHITE, "13.10.2024");
  smallText(72, 8, C_WHITE, "PARTENZE");
  for (int i = 0; i < 8; i++) {
    int idx = visibleRowIndex(i);
    int y = 24 + i * 11;
    smallText(62, y, C_AMBER, cutText(rows[idx].typeTrain, 5));
    smallText(98, y, C_AMBER, cutText(rows[idx].destination, 10));
    smallText(167, y, C_AMBER, cutText(rows[idx].heure, 5));
    smallText(205, y, C_AMBER, cutText(rows[idx].voie, 3));
  }
  smallText(7, 101, C_AMBER, "ATTENZIONE: verificare sempre il binario");
  smallText(7, 112, C_AMBER, "sugli schermi in stazione.");
}

void drawIndiaRows() {
  gfx->fillRect(3, 3, 234, 129, C_BLACK);
  gfx->fillRect(5, 5, 230, 18, 0x2104);
  smallText(9, 9, C_AMBER, "INDIAN RAILWAYS");
  smallText(125, 9, C_WHITE, "Departures");
  for (int i = 0; i < 8; i++) {
    int idx = visibleRowIndex(i);
    int y = 30 + i * 11;
    gfx->drawFastHLine(5, y + 10, 230, 0x4208);
    smallText(8, y + 1, C_AMBER, cutText(rows[idx].heure, 5));
    smallText(43, y + 1, C_GREEN, cutText(rows[idx].typeTrain, 6));
    smallText(91, y + 1, C_AMBER, cutText(rows[idx].destination, 14));
    smallText(202, y + 1, C_WHITE, cutText(rows[idx].voie, 3));
  }
}

void drawSheffieldRows() {
  gfx->fillRect(3, 3, 234, 129, 0x39E7);
  for (int i = 0; i < 7; i++) {
    int idx = visibleRowIndex(i);
    int y = 6 + i * 18;
    gfx->fillRect(5, y, 230, 15, 0x3186);
    gfx->drawFastHLine(5, y + 15, 230, 0xA514);
    mediumText(8, y + 1, C_AMBER, cutText(rows[idx].heure, 5));
    smallText(54, y + 2, C_AMBER, cutText(rows[idx].destination, 14));
    smallText(151, y + 2, C_AMBER, cutText(rows[idx].voie, 11));
    smallText(205, y + 2, C_AMBER, "On time");
    smallText(8, y + 11, 0xD5A0, cutText(rows[idx].info, 30));
  }
}

void drawNzBritomartRows() {
  uint16_t bg = 0x00B5;
  uint16_t header = 0x037F;
  uint16_t rowA = 0x0198;
  uint16_t rowB = 0x0110;
  uint16_t line = 0x34DF;
  int leftW = 158;
  int rightX = 164;
  gfx->fillRect(3, 3, 234, 129, bg);
  gfx->fillRect(4, 4, leftW, 13, header);
  smallText(8, 8, C_WHITE, "Line");
  smallText(41, 8, C_WHITE, "Destination");
  smallText(116, 8, C_WHITE, "Platform");
  smallText(147, 8, C_WHITE, "Due");
  for (int i = 0; i < 7; i++) {
    int idx = visibleRowIndex(i);
    int y = 19 + i * 14;
    String tag = rows[idx].typeTrain;
    uint16_t tagColor = (tag == "WEST") ? 0x07E0 : ((tag == "STH") ? C_RED : C_YELLOW);
    uint16_t tagText = (tag == "STH") ? C_WHITE : bg;
    gfx->fillRect(4, y, leftW, 13, (i % 2) ? rowB : rowA);
    gfx->drawFastHLine(4, y + 13, leftW, line);
    gfx->fillRoundRect(7, y + 2, 26, 9, 2, tagColor);
    smallText(11, y + 4, tagText, cutText(tag, 4));
    smallText(41, y + 2, C_WHITE, cutText(rows[idx].destination, 9));
    smallText(82, y + 5, 0xD6FF, cutText(rows[idx].info, 12));
    if (rows[idx].info.indexOf("Panmure") >= 0 || rows[idx].destination == "Papakura") smallText(137, y + 4, C_YELLOW, "x");
    mediumText(122, y + 1, C_WHITE, cutText(rows[idx].voie, 2));
    mediumText(146, y + 1, C_WHITE, cutText(rows[idx].heure, 5));
  }
  gfx->fillRect(4, 119, leftW, 10, header);
  smallText(8, 122, C_YELLOW, "x");
  smallText(17, 122, C_WHITE, "AirportLink Bus connection at Puhinui");
  gfx->fillRect(rightX, 4, 72, 45, rowB);
  gfx->drawRect(rightX, 4, 72, 45, line);
  gfx->fillCircle(rightX + 8, 12, 4, C_YELLOW);
  smallText(rightX + 6, 10, bg, "i");
  smallText(rightX + 18, 9, C_WHITE, "Britomart");
  smallText(rightX + 18, 17, C_WHITE, "Train Station");
  mediumText(rightX + 19, 30, C_WHITE, "12:30");
  smallText(rightX + 55, 35, C_WHITE, "57");
  gfx->fillRect(rightX, 51, 72, 78, 0x07FF);
  gfx->fillCircle(rightX + 36, 66, 9, 0xF5B6);
  gfx->fillRoundRect(rightX + 25, 76, 24, 30, 4, 0xF800);
  gfx->drawLine(rightX + 25, 85, rightX + 13, 98, 0xF5B6);
  gfx->drawLine(rightX + 49, 85, rightX + 61, 98, 0xF5B6);
  gfx->drawLine(rightX + 28, 106, rightX + 20, 126, C_BLUE_DARK);
  gfx->drawLine(rightX + 46, 106, rightX + 55, 126, C_BLUE_DARK);
  smallText(rightX + 6, 112, bg, "Service Centre");
  smallText(rightX + 6, 120, bg, "Auckland AT");
}

void drawProfileFrame() {
  gfx->fillScreen(C_BLACK);
  if (isRetroProfile()) { drawRetroFrame(); return; }
  if (displayProfile == "ch_sbb_romandie") { drawSwissRomandieRows(); return; }
  if (displayProfile == "ch_zurich_fern") { drawZurichFernverkehrRows(); return; }
  if (displayProfile == "ch_bern_arrival") { drawSwissBernArrivalRows(); return; }
  if (displayProfile == "ch_sbb_blue") { drawSwissCffBlueRows(); return; }
  if (displayProfile == "be_sncb_detail_list") { drawSncbDetailListRows(); return; }
  if (displayProfile == "be_sncb_modern") { drawSncb2023Photo(); return; }
  if (displayProfile == "be_sncb_grid") { drawSncbTft2010PhotoFix(); return; }
  if (displayProfile == "be_sncb_detail") { drawSncbRailTime(); return; }
  if (displayProfile == "fr_sncf_first") { drawSncfFirstScreenRows(); return; }
  if (displayProfile == "fr_sncf_old_led") { drawSncfOldLedRows(); return; }
  if (displayProfile == "fr_sncf_arrivals") { drawSncfArrivalsGreenRows(); return; }
  if (displayProfile == "fr_sncf_2012") { drawSncf2012Photo(); return; }
  if (displayProfile == "fr_rer_a") { drawRerALineRows(); return; }
  if (displayProfile == "fr_saint_lazare") { drawSaintLazareRows(); return; }
  if (displayProfile == "fr_transilien_p") { drawTransilienLinePRows(); return; }
  if (displayProfile == "fr_transilien_2016") { drawTransilien2016Rows(); return; }
  if (displayProfile == "fr_rer_90") { gfx->fillRect(3,3,234,129,0x0300); gfx->fillRect(4,4,232,18,0x14A3); smallText(8,8,C_WHITE,"SNCF arrivee / depart"); drawRowsClassic(24,103,0x24C6,0x1383,C_WHITE,C_YELLOW,0x2E8A,2); return; }
  if (displayProfile == "fr_rer_orange") { gfx->fillRect(3,3,234,129,C_BLUE_DARK); gfx->fillRect(4,4,232,19,0x0B34); smallText(8,8,C_WHITE,"train n   heure   provenance"); smallText(200,8,C_WHITE,"voie"); drawRowsClassic(25,102,0x1383,0x0B34,C_WHITE,C_YELLOW,C_GRID,3); return; }
  if (displayProfile == "fr_transilien") { gfx->fillRect(3,3,234,129,0xC618); gfx->fillRect(4,4,232,17,C_WHITE); smallText(88,8,C_BLACK,"Prochains Trains"); drawDetailBoardRows(); return; }
  if (displayProfile == "es_barcelona_grid") { drawBarcelonaRodaliesGridRows(); return; }
  if (displayProfile == "es_barcelona_adif") { drawBarcelonaAdifRows(); return; }
  if (displayProfile == "pl_pkp_departures") { drawPolandBlueRows(false); return; }
  if (displayProfile == "pl_pkp_arrivals") { drawPolandBlueRows(true); return; }
  if (displayProfile == "us_la_metro") { drawLosAngelesRows(); return; }
  if (displayProfile == "it_naples_amber") { drawNaplesAmberRows(); return; }
  if (displayProfile == "in_indian_railways") { drawIndiaRows(); return; }
  if (displayProfile == "uk_sheffield") { drawSheffieldRows(); return; }
  if (displayProfile == "nz_britomart") { drawNzBritomartRows(); return; }
  if (displayProfile == "de_db_large_blue") { drawDbLargeBlueRows(); return; }
  if (displayProfile == "de_db_intercity") { drawDbIntercityRows(); return; }
  if (displayProfile == "de_db_2010_2015") { drawDb2010BoardRows(); return; }
  if (displayProfile == "de_db_2022") { drawDb2022BoardRows(); return; }
  if (displayProfile == "de_baden_baden") { drawBadenBadenRows(); return; }
  if (displayProfile == "de_bvg_blue") { gfx->fillRect(3,3,234,129,0x001F); smallText(8,8,C_WHITE,"Berlin BVG"); smallText(196,8,C_WHITE,"Abfahrt"); drawBvgRows(); return; }
  if (displayProfile == "de_db_modern") { drawDbModernPhotoRows(); return; }
  if (displayProfile == "de_db_blue") { drawDbClockPhotoRows(); return; }
  if (displayProfile == "uk_modern") { gfx->fillRect(3,3,234,129,C_BLACK); smallText(8,8,C_WHITE,"DEPARTURES"); drawUkModernRows(); return; }
  if (displayProfile == "uk_splitflap") { drawUkSplitFlapPhotoRows(); return; }
  if (displayProfile == "se_stockholm") { drawStockholmRows(); return; }
  if (displayProfile == "jp_jr_led") { drawJapanGreenLedRows(); return; }
  if (displayProfile == "jp_tokyo_grey") { drawTokyoGreyRows(); return; }
  if (displayProfile == "at_oebb_white") { drawOebbWhiteArrivalRows(); return; }
  if (displayProfile == "at_oebb_dense") { drawOebbBlueDenseRows(); return; }
  if (displayProfile == "at_oebb_teal") { drawOebbTealArrivalRows(); return; }
  if (displayProfile == "at_oebb_green") { drawOebbGreenPhotoRows(); return; }
  if (displayProfile == "at_oebb_blue") { drawOebbBluePhotoRows(); return; }
  if (displayProfile == "nl_ns_light") { drawNsLightModernRows(); return; }
  if (displayProfile == "nl_ns_blue") { gfx->fillRect(3,3,234,129,C_BLUE_DARK); smallText(8,8,C_WHITE,"NS Vertrektijden"); drawRowsClassic(24,103,0x001F,0x085F,C_WHITE,C_YELLOW,C_GRID,0); return; }
  if (displayProfile == "nl_ns_blue") { drawRowsClassic(24,103,0x001F,0x085F,C_WHITE,C_YELLOW,C_GRID,0); return; }
  if (displayProfile == "fr_sncf_white") { gfx->fillRect(3,3,234,129,C_WHITE); gfx->fillRect(4,25,232,104,0xDF1F); smallText(8,7,C_BLACK,tftFramePlace()); smallText(196,7,C_BLACK,"10:30"); drawRowsClassic(28,99,0xDF1F,C_WHITE,C_BLACK,0x001F,C_GREY,0); return; }
  if (displayProfile == "fr_sncf_green" || displayProfile == "at_oebb_green") { gfx->fillRect(3,3,234,129,0x0300); smallText(8,8,C_WHITE,tftFrameTitle()); drawRowsClassic(23,104,0x24C6,0x1383,C_WHITE,C_YELLOW,0x2E8A,2); return; }
  if (displayProfile == "fr_led_nice") { gfx->fillRect(3,3,234,129,0x001F); gfx->fillRect(4,4,232,16,0x025F); smallText(8,7,C_WHITE,"SNCF Departures"); drawRowsClassic(21,108,0x001F,0x085F,C_WHITE,C_YELLOW,C_GRID,0); return; }
  if (displayProfile == "es_renfe" || displayProfile == "es_alsa") { gfx->fillRect(3,3,234,129,C_BLACK); smallText(8,7,C_AMBER,displayProfile == "es_alsa" ? "ALSA SALIDAS" : tftFrameTitle()); drawRowsClassic(21,108,C_BLACK,0x1082,C_WHITE,C_AMBER,C_GREY,1); return; }
  if (displayProfile == "de_db_orange" || displayProfile == "uk_led_amber" || displayProfile == "fr_tgv_amber" || displayProfile == "es_renfe_split" || displayProfile == "it_fs_amber") { gfx->fillRect(3,3,234,129,C_BLACK); gfx->fillRect(4,4,232,16,0x2104); smallText(8,8,C_AMBER,displayProfile == "es_renfe_split" ? "Salidas / Departures" : tftFrameTitle()); drawRowsClassic(22,103,C_BLACK,0x1082,C_AMBER,C_AMBER,0x4208,1); return; }
  if (displayProfile == "ch_sbb_yellow") { gfx->fillRect(3,3,234,129,0xFD20); mediumText(36,9,C_BLACK,"Abfahrt Depart"); drawRowsClassic(35,72,0x001F,0x181F,C_WHITE,C_WHITE,C_WHITE,0); gfx->fillRect(4,108,232,20,C_RED); smallText(8,113,C_WHITE,"Suisse Romande: horaire d'ete"); return; }
  if (displayProfile == "hu_mav_arrivals") { drawMavPhotoRows(true); return; }
  if (displayProfile == "hu_mav_departures") { drawMavPhotoRows(false); return; }
  if (displayProfile == "hu_mav_old") { gfx->fillRect(3,3,234,129,C_BLUE_DARK); mediumText(70,6,C_WHITE,"09 : 23"); smallText(8,24,C_WHITE,"MAV Indulo vonatok / Departure"); drawRowsClassic(39,86,0x0861,0x10A2,C_WHITE,0xB7FF,C_GRID,0); return; }
  if (displayProfile == "hu_mav_blue") { gfx->fillRect(3,3,234,129,0x03BF); smallText(8,7,C_WHITE,"MAV  INDULO VONATOK"); drawRowsClassic(23,104,0x05FF,0x037F,C_WHITE,C_WHITE,C_GRID,0); return; }
  if (displayProfile == "hu_mav_green") { gfx->fillRect(3,3,234,129,0x03E0); smallText(8,7,C_WHITE,"MAV  ERKEZO VONATOK"); drawRowsClassic(23,104,0x05E0,0x0380,C_WHITE,C_WHITE,C_GRID,0); return; }
  if (displayProfile == "it_trenitalia" || displayProfile == "it_fs_blue") { drawItalyFrame(); return; }
  gfx->fillRect(3,3,234,129,C_BLUE_DARK);
  gfx->fillRect(4,4,232,18,(displayProfile == "de_db_blue" || displayProfile == "de_db_modern") ? 0x2945 : C_BLUE_TOP);
  smallText(8,8,C_WHITE,tftFrameTitle());
  smallText(160,8,C_WHITE,tftFramePlace());
  drawRowsClassic(25,104,(displayProfile == "be_sncb_modern") ? C_BLUE_DARK : C_BLUE_ROW1,(displayProfile == "de_db_modern") ? 0x1276 : C_BLUE_ROW2,C_WHITE,(displayProfile == "be_sncb_modern") ? C_SNCB_YELLOW : C_WHITE,C_GRID,(displayProfile == "be_sncb_modern") ? 3 : 0);
}

void drawProfileRowsOnly() {
  if (isRetroProfile()) { drawRetroRowsOnly(false); return; }
  if (displayProfile == "ch_sbb_romandie") { drawSwissRomandieRows(); return; }
  if (displayProfile == "ch_zurich_fern") { drawZurichFernverkehrRows(); return; }
  if (displayProfile == "ch_bern_arrival") { drawSwissBernArrivalRows(); return; }
  if (displayProfile == "ch_sbb_blue") { drawSwissCffBlueRows(); return; }
  if (displayProfile == "be_sncb_detail_list") { drawSncbDetailListRows(); return; }
  if (displayProfile == "be_sncb_modern") { drawSncb2023Photo(); return; }
  if (displayProfile == "be_sncb_grid") { drawSncbTft2010PhotoFix(); return; }
  if (displayProfile == "be_sncb_detail") { drawSncbRailTime(); return; }
  if (displayProfile == "fr_sncf_first") { drawSncfFirstScreenRows(); return; }
  if (displayProfile == "fr_sncf_old_led") { drawSncfOldLedRows(); return; }
  if (displayProfile == "fr_sncf_arrivals") { drawSncfArrivalsGreenRows(); return; }
  if (displayProfile == "fr_sncf_2012") { drawSncf2012Photo(); return; }
  if (displayProfile == "fr_rer_a") { drawRerALineRows(); return; }
  if (displayProfile == "fr_saint_lazare") { drawSaintLazareRows(); return; }
  if (displayProfile == "fr_transilien_p") { drawTransilienLinePRows(); return; }
  if (displayProfile == "fr_transilien_2016") { drawTransilien2016Rows(); return; }
  if (displayProfile == "fr_rer_90") { drawRowsClassic(24,103,0x24C6,0x1383,C_WHITE,C_YELLOW,0x2E8A,2); return; }
  if (displayProfile == "fr_rer_orange") { drawRowsClassic(25,102,0x1383,0x0B34,C_WHITE,C_YELLOW,C_GRID,3); return; }
  if (displayProfile == "fr_transilien") { drawDetailBoardRows(); return; }
  if (displayProfile == "es_barcelona_grid") { drawBarcelonaRodaliesGridRows(); return; }
  if (displayProfile == "es_barcelona_adif") { drawBarcelonaAdifRows(); return; }
  if (displayProfile == "pl_pkp_departures") { drawPolandBlueRows(false); return; }
  if (displayProfile == "pl_pkp_arrivals") { drawPolandBlueRows(true); return; }
  if (displayProfile == "us_la_metro") { drawLosAngelesRows(); return; }
  if (displayProfile == "it_naples_amber") { drawNaplesAmberRows(); return; }
  if (displayProfile == "in_indian_railways") { drawIndiaRows(); return; }
  if (displayProfile == "uk_sheffield") { drawSheffieldRows(); return; }
  if (displayProfile == "nz_britomart") { drawNzBritomartRows(); return; }
  if (displayProfile == "de_db_large_blue") { drawDbLargeBlueRows(); return; }
  if (displayProfile == "de_db_intercity") { drawDbIntercityRows(); return; }
  if (displayProfile == "de_db_2010_2015") { drawDb2010BoardRows(); return; }
  if (displayProfile == "de_db_2022") { drawDb2022BoardRows(); return; }
  if (displayProfile == "de_baden_baden") { drawBadenBadenRows(); return; }
  if (displayProfile == "de_bvg_blue") { drawBvgRows(); return; }
  if (displayProfile == "de_db_modern") { drawDbModernPhotoRows(); return; }
  if (displayProfile == "de_db_blue") { drawDbClockPhotoRows(); return; }
  if (displayProfile == "uk_modern") { drawUkModernRows(); return; }
  if (displayProfile == "uk_splitflap") { drawUkSplitFlapPhotoRows(); return; }
  if (displayProfile == "se_stockholm") { drawStockholmRows(); return; }
  if (displayProfile == "jp_jr_led") { drawJapanGreenLedRows(); return; }
  if (displayProfile == "jp_tokyo_grey") { drawTokyoGreyRows(); return; }
  if (displayProfile == "at_oebb_white") { drawOebbWhiteArrivalRows(); return; }
  if (displayProfile == "at_oebb_dense") { drawOebbBlueDenseRows(); return; }
  if (displayProfile == "at_oebb_teal") { drawOebbTealArrivalRows(); return; }
  if (displayProfile == "at_oebb_green") { drawOebbGreenPhotoRows(); return; }
  if (displayProfile == "at_oebb_blue") { drawOebbBluePhotoRows(); return; }
  if (displayProfile == "nl_ns_light") { drawNsLightModernRows(); return; }
  if (displayProfile == "nl_ns_blue") { gfx->fillRect(3,3,234,129,C_BLUE_DARK); smallText(8,8,C_WHITE,"NS Vertrektijden"); drawRowsClassic(24,103,0x001F,0x085F,C_WHITE,C_YELLOW,C_GRID,0); return; }
  if (displayProfile == "nl_ns_blue") { drawRowsClassic(24,103,0x001F,0x085F,C_WHITE,C_YELLOW,C_GRID,0); return; }
  if (displayProfile == "fr_sncf_white") { drawRowsClassic(28,99,0xDF1F,C_WHITE,C_BLACK,0x001F,C_GREY,0); return; }
  if (displayProfile == "fr_sncf_green" || displayProfile == "at_oebb_green") { drawRowsClassic(23,104,0x24C6,0x1383,C_WHITE,C_YELLOW,0x2E8A,2); return; }
  if (displayProfile == "fr_led_nice") { drawRowsClassic(21,108,0x001F,0x085F,C_WHITE,C_YELLOW,C_GRID,0); return; }
  if (displayProfile == "es_renfe" || displayProfile == "es_alsa") { drawRowsClassic(21,108,C_BLACK,0x1082,C_WHITE,C_AMBER,C_GREY,1); return; }
  if (displayProfile == "de_db_orange" || displayProfile == "uk_led_amber" || displayProfile == "fr_tgv_amber" || displayProfile == "es_renfe_split" || displayProfile == "it_fs_amber") { drawRowsClassic(22,103,C_BLACK,0x1082,C_AMBER,C_AMBER,0x4208,1); return; }
  if (displayProfile == "ch_sbb_yellow") { drawRowsClassic(35,72,0x001F,0x181F,C_WHITE,C_WHITE,C_WHITE,0); return; }
  if (displayProfile == "hu_mav_arrivals") { drawMavPhotoRows(true); return; }
  if (displayProfile == "hu_mav_departures") { drawMavPhotoRows(false); return; }
  if (displayProfile == "hu_mav_old") { drawRowsClassic(39,86,0x0861,0x10A2,C_WHITE,0xB7FF,C_GRID,0); return; }
  if (displayProfile == "hu_mav_blue") { drawRowsClassic(23,104,0x05FF,0x037F,C_WHITE,C_WHITE,C_GRID,0); return; }
  if (displayProfile == "hu_mav_green") { drawRowsClassic(23,104,0x05E0,0x0380,C_WHITE,C_WHITE,C_GRID,0); return; }
  if (displayProfile == "it_trenitalia" || displayProfile == "it_fs_blue") { drawItalyRowsOnly(); return; }
  drawRowsClassic(25,104,(displayProfile == "be_sncb_modern") ? C_BLUE_DARK : C_BLUE_ROW1,(displayProfile == "de_db_modern") ? 0x1276 : C_BLUE_ROW2,C_WHITE,(displayProfile == "be_sncb_modern") ? C_SNCB_YELLOW : C_WHITE,C_GRID,(displayProfile == "be_sncb_modern") ? 3 : 0);
}

void drawFrame() {
  drawProfileFrame();
}

void drawRowsOnly() {
  rowsOnlyPass = true;
  drawProfileRowsOnly();
  rowsOnlyPass = false;
}

void drawScreenFull() {
  rowsOnlyPass = false;
  drawFrame();
  drawProfileRowsOnly();
}

// ====== WEB ======
static const char PM3D_LOGO_B64[] PROGMEM = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNgYGD4DwABBAEAghLfcAAAAABJRU5ErkJggg==";

String themeAccent1() {
  if (uiTheme == "orange") return "#ff9a2f";
  if (uiTheme == "yellow") return "#ffd84a";
  if (uiTheme == "green") return "#41f08a";
  if (uiTheme == "black") return "#cfd8e3";
  if (uiTheme == "purple") return "#b276ff";
  if (uiTheme == "red") return "#ff4b4b";
  if (uiTheme == "cyan") return "#31f3ff";
  if (uiTheme == "ice") return "#ffffff";
  if (uiTheme == "pink") return "#ff6bd6";
  return "#5fbdf2";
}

String themeBodyBg() {
  if (uiTheme == "orange") return "radial-gradient(circle at 50% -10%,rgba(255,165,55,.30),rgba(45,16,0,.60) 38%,#090300 78%),linear-gradient(180deg,#2d1300,#080200)";
  if (uiTheme == "yellow") return "radial-gradient(circle at 50% -10%,rgba(255,220,70,.28),rgba(50,42,0,.62) 38%,#080700 78%),linear-gradient(180deg,#332600,#090700)";
  if (uiTheme == "green") return "radial-gradient(circle at 50% -10%,rgba(70,255,150,.24),rgba(0,40,22,.60) 38%,#000804 78%),linear-gradient(180deg,#002b18,#000805)";
  if (uiTheme == "black") return "radial-gradient(circle at 50% -10%,rgba(210,225,240,.18),rgba(18,24,33,.68) 38%,#020305 78%),linear-gradient(180deg,#151b24,#020305)";
  if (uiTheme == "purple") return "radial-gradient(circle at 50% -10%,rgba(180,110,255,.28),rgba(32,8,58,.62) 38%,#07010d 78%),linear-gradient(180deg,#21103e,#07010d)";
  if (uiTheme == "red") return "radial-gradient(circle at 50% -10%,rgba(255,55,55,.30),rgba(58,5,5,.66) 38%,#0b0000 78%),linear-gradient(180deg,#3a0505,#090000)";
  if (uiTheme == "cyan") return "radial-gradient(circle at 50% -10%,rgba(45,245,255,.28),rgba(0,45,58,.62) 38%,#00090d 78%),linear-gradient(180deg,#00313d,#00080b)";
  if (uiTheme == "ice") return "radial-gradient(circle at 50% -10%,rgba(255,255,255,.32),rgba(65,85,110,.58) 38%,#05080d 78%),linear-gradient(180deg,#eef5ff,#1b2635 45%,#04070c)";
  if (uiTheme == "pink") return "radial-gradient(circle at 50% -10%,rgba(255,105,215,.30),rgba(65,5,52,.62) 38%,#0b0109 78%),linear-gradient(180deg,#3b0732,#080107)";
  return "radial-gradient(circle at 50% -10%,rgba(80,170,230,.24),rgba(4,14,30,.55) 38%,#01040A 78%),linear-gradient(180deg,#06162B,#020711)";
}

String themeCardBg() {
  if (uiTheme == "orange") return "linear-gradient(180deg,rgba(95,45,8,.96),rgba(35,13,0,.98))";
  if (uiTheme == "yellow") return "linear-gradient(180deg,rgba(92,72,8,.96),rgba(31,25,0,.98))";
  if (uiTheme == "green") return "linear-gradient(180deg,rgba(8,74,42,.96),rgba(0,28,16,.98))";
  if (uiTheme == "black") return "linear-gradient(180deg,rgba(42,49,59,.96),rgba(10,13,18,.98))";
  if (uiTheme == "purple") return "linear-gradient(180deg,rgba(64,27,111,.96),rgba(20,5,39,.98))";
  if (uiTheme == "red") return "linear-gradient(180deg,rgba(104,18,18,.96),rgba(36,4,4,.98))";
  if (uiTheme == "cyan") return "linear-gradient(180deg,rgba(8,75,86,.96),rgba(0,26,34,.98))";
  if (uiTheme == "ice") return "linear-gradient(180deg,rgba(72,88,112,.96),rgba(17,24,36,.98))";
  if (uiTheme == "pink") return "linear-gradient(180deg,rgba(96,16,78,.96),rgba(35,4,29,.98))";
  return "linear-gradient(180deg,rgba(19,54,90,.96),rgba(7,22,42,.98))";
}

String themeSoftPanelBg() {
  if (uiTheme == "orange") return "linear-gradient(180deg,rgba(78,33,4,.72),rgba(0,0,0,.24))";
  if (uiTheme == "yellow") return "linear-gradient(180deg,rgba(70,56,4,.72),rgba(0,0,0,.24))";
  if (uiTheme == "green") return "linear-gradient(180deg,rgba(4,58,32,.72),rgba(0,0,0,.24))";
  if (uiTheme == "black") return "linear-gradient(180deg,rgba(36,43,52,.76),rgba(0,0,0,.26))";
  if (uiTheme == "purple") return "linear-gradient(180deg,rgba(48,18,86,.74),rgba(0,0,0,.24))";
  if (uiTheme == "red") return "linear-gradient(180deg,rgba(78,12,12,.74),rgba(0,0,0,.24))";
  if (uiTheme == "cyan") return "linear-gradient(180deg,rgba(4,60,70,.74),rgba(0,0,0,.24))";
  if (uiTheme == "ice") return "linear-gradient(180deg,rgba(68,82,104,.74),rgba(0,0,0,.24))";
  if (uiTheme == "pink") return "linear-gradient(180deg,rgba(72,12,58,.74),rgba(0,0,0,.24))";
  return "linear-gradient(180deg,rgba(8,32,58,.72),rgba(0,0,0,.20))";
}

String themeButtonBgFor(const String &theme) {
  if (theme == "orange") return "linear-gradient(135deg,#fff1d2 0%,#ff9c38 45%,#4c1800 100%)";
  if (theme == "yellow") return "linear-gradient(135deg,#fffbe0 0%,#ffd84a 45%,#3d3100 100%)";
  if (theme == "green") return "linear-gradient(135deg,#d9ffe8 0%,#31d978 45%,#00351d 100%)";
  if (theme == "black") return "linear-gradient(135deg,#f4f7fb 0%,#657080 45%,#06080d 100%)";
  if (theme == "purple") return "linear-gradient(135deg,#f0ddff 0%,#b276ff 45%,#240642 100%)";
  if (theme == "red") return "linear-gradient(135deg,#ffe1e1 0%,#ff4b4b 45%,#400606 100%)";
  if (theme == "cyan") return "linear-gradient(135deg,#e3fcff 0%,#31f3ff 45%,#003440 100%)";
  if (theme == "ice") return "linear-gradient(135deg,#ffffff 0%,#b9d7ff 45%,#1d314c 100%)";
  if (theme == "pink") return "linear-gradient(135deg,#ffe2f7 0%,#ff6bd6 45%,#3a062e 100%)";
  return "linear-gradient(135deg,#e8f7ff 0%,#58b8f0 45%,#082f56 100%)";
}

String themeButtonBg() {
  return themeButtonBgFor(uiTheme);
}

String cssCommon() {
  String css;
  css += "*{box-sizing:border-box}body{margin:0;padding:10px;text-align:center;font-family:Arial,Helvetica,sans-serif;color:#F6FBFF;background:" + themeBodyBg() + ";background-attachment:fixed;}";
  css += "body:before{content:'';position:fixed;inset:0;pointer-events:none;background:linear-gradient(rgba(255,255,255,.035) 1px,transparent 1px),linear-gradient(90deg,rgba(255,255,255,.025) 1px,transparent 1px);background-size:28px 28px;opacity:.28;}";
  css += ".wrap{max-width:760px;margin:0 auto;position:relative;z-index:1;}.card{background:" + themeCardBg() + ";border:1px solid rgba(164,220,255,.22);border-radius:22px;padding:13px;margin-bottom:11px;box-shadow:0 18px 44px rgba(0,0,0,.42),inset 0 1px 0 rgba(255,255,255,.14),inset 0 -1px 0 rgba(0,0,0,.45);}";
  css += ".title{font-size:22px;font-weight:900;margin:6px 0 12px;text-shadow:0 2px 8px rgba(0,0,0,.55);}.subtitle{font-size:12px;color:#C2DDEC;line-height:1.45;margin:5px 0 10px;}";
  css += ".btn,button{display:block;width:100%;position:relative;overflow:hidden;border:1px solid rgba(230,246,255,.34);border-radius:14px;padding:12px;background:" + themeButtonBg() + ";color:white;font-size:15px;font-weight:900;text-decoration:none;margin:8px 0;box-shadow:inset 0 1px 0 rgba(255,255,255,.70),inset 0 10px 14px rgba(255,255,255,.10),inset 0 -10px 18px rgba(0,0,0,.42),0 8px 18px rgba(0,0,0,.30);text-shadow:0 1px 3px rgba(0,0,0,.65);}";
  css += ".btn:before,button:before{content:'';position:absolute;left:-20%;right:-20%;top:0;height:42%;background:linear-gradient(180deg,rgba(255,255,255,.42),rgba(255,255,255,.08),transparent);pointer-events:none;}.btn:active,button:active{transform:translateY(1px);filter:brightness(.95);}";
  css += ".selectedMode{color:#07111c!important;border:1px solid rgba(255,255,255,.78)!important;box-shadow:0 0 0 2px rgba(255,255,255,.75) inset,inset 0 1px 0 rgba(255,255,255,.95),inset 0 -12px 20px rgba(0,0,0,.32),0 0 24px rgba(210,225,235,.38),0 10px 24px rgba(0,0,0,.38)!important;background:linear-gradient(180deg,#ffffff 0%,#cfd8de 18%,#8b969d 50%,#e5edf2 100%)!important;text-shadow:0 1px 0 rgba(255,255,255,.55)!important;}";
  css += ".danger{background:linear-gradient(180deg,#ffb1b1 0%,#c93535 42%,#6d1018 100%);}label{font-weight:800;color:#D9ECF7;font-size:14px;text-align:left;display:block;margin-top:8px;}";
  css += "input,select{width:100%;box-sizing:border-box;border-radius:12px;border:1px solid rgba(164,220,255,.28);background:linear-gradient(180deg,#06111F,#02070E);color:white;padding:9px;font-size:15px;font-weight:800;box-shadow:inset 0 1px 0 rgba(255,255,255,.08);}.smallInput{width:78px;display:inline-block;text-align:center;}";
  css += ".langBar{display:flex;flex-direction:row;flex-wrap:nowrap;gap:6px;justify-content:center;align-items:center;margin:0 0 10px}.langBar .btn,.langBtn{width:auto;min-width:42px;margin:0;padding:7px 10px;border-radius:10px;font-size:12px;line-height:1;text-align:center;white-space:nowrap}.langOn{box-shadow:0 0 0 2px rgba(255,255,255,.85) inset!important;background:" + themeButtonBg() + "!important;color:white!important;border-color:" + themeAccent1() + "!important;}";
  css += ".section{font-size:13px;font-weight:900;color:" + themeAccent1() + ";text-transform:uppercase;margin:12px 0 7px;text-align:left;letter-spacing:.4px;border-top:1px solid " + themeAccent1() + ";padding-top:9px;}.sub{border:1px solid " + themeAccent1() + ";border-radius:16px;padding:11px;background:" + themeSoftPanelBg() + ";margin:11px 0;text-align:left;box-shadow:inset 0 1px 0 rgba(255,255,255,.08);}";
  css += ".back{position:fixed;left:10px;top:10px;z-index:50;width:42px;height:42px;border-radius:13px;background:" + themeButtonBg() + ";color:white;text-decoration:none;display:flex;align-items:center;justify-content:center;font-weight:900;box-shadow:inset 0 1px 0 rgba(255,255,255,.38),0 8px 18px rgba(0,0,0,.35);}.brandRow{display:flex;align-items:center;justify-content:center;gap:10px;margin:0 0 10px;position:relative;}.brandText{text-align:left}.headerActions{position:absolute;right:0;top:4px;display:flex;gap:7px}.iconBtn{width:40px;height:40px;border-radius:13px;display:flex;align-items:center;justify-content:center;text-decoration:none;color:white;font-size:21px;border:1px solid rgba(230,246,255,.34);background:" + themeButtonBg() + ";box-shadow:inset 0 1px 0 rgba(255,255,255,.55),0 8px 18px rgba(0,0,0,.30);}";
  css += ".back{appearance:none;-webkit-appearance:none;border:0;margin:0;padding:0;cursor:pointer;}";
  css += ".stepCtl{display:flex;align-items:center;justify-content:space-between;gap:8px;padding:9px 8px;border-radius:12px;background:rgba(255,255,255,.08);border:1px solid rgba(255,255,255,.14);font-weight:900}.stepCtl span{font-size:12px;text-align:left}.stepBox{display:grid;grid-template-columns:34px 44px 34px;gap:5px;align-items:center}.stepBox button{margin:0;padding:7px 0;border-radius:10px;font-size:18px;line-height:1}.stepBox b{display:flex;align-items:center;justify-content:center;height:32px;border-radius:9px;background:rgba(0,0,0,.24);font-size:14px}";
  css += ".pm3dLogo{width:56px;height:56px;border-radius:16px;border:1px solid rgba(230,246,255,.34);background:" + themeButtonBg() + ";display:flex;align-items:center;justify-content:center;font-weight:1000;color:white;text-shadow:0 2px 6px #000;box-shadow:0 10px 22px rgba(0,0,0,.38),inset 0 1px 0 rgba(255,255,255,.65);}.pm3dLogoImg{width:58px;height:58px;object-fit:contain;border-radius:15px;filter:drop-shadow(0 12px 18px rgba(0,0,0,.48));}.introLogo{width:132px;height:132px;margin:0 auto 12px;}.homeHero{padding:22px 12px 16px}.mainActions{display:grid;grid-template-columns:1fr;gap:8px}.grid2{display:grid;grid-template-columns:1fr 1fr;gap:8px}.modeGrid{display:grid;grid-template-columns:repeat(3,1fr);gap:6px}.row{display:grid;grid-template-columns:48px 44px 38px 1fr 45px 34px;gap:5px;align-items:center;margin:5px 0}.head{color:#F0F8FF;font-size:11px;display:grid;grid-template-columns:48px 44px 38px 1fr 45px 34px;gap:5px;margin-top:4px;text-align:left}.row input{padding:7px;font-size:13px}.countryGrid{display:grid;grid-template-columns:repeat(3,1fr);gap:7px}.swatches{display:grid;grid-template-columns:repeat(3,1fr);gap:7px}.settingsGrid{display:grid;grid-template-columns:1fr 1fr;gap:8px}.settingsLine{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;align-items:end}.settingsLineWide{grid-template-columns:repeat(5,minmax(0,1fr));gap:6px}.styleGrid{display:grid;grid-template-columns:1fr 1fr;gap:5px}.styleBtn{text-align:left;min-height:34px;padding:7px 38px 7px 8px;margin:0;font-size:11px;line-height:1.1}.styleItem{position:relative;border:1px solid rgba(255,255,255,.10);border-radius:10px;padding:3px;background:rgba(0,0,0,.08)}.styleActions{position:absolute;right:7px;top:6px;display:flex;gap:4px;align-items:center}.miniMove{width:26px;height:26px;margin:0;padding:0;border-radius:8px;font-size:13px;line-height:26px;display:flex;align-items:center;justify-content:center}";
  css += "@media(max-width:560px){body{padding:8px}.langBar{display:flex!important;flex-direction:row!important;flex-wrap:nowrap!important}.langBar .btn,.langBtn{min-width:34px;padding:7px 7px;font-size:12px}.title{font-size:19px}.grid2{grid-template-columns:1fr}.swatches{grid-template-columns:repeat(3,1fr)}.countryGrid{grid-template-columns:1fr 1fr}.modeGrid{grid-template-columns:1fr 1fr}.row,.head{grid-template-columns:36px 38px 32px 1fr 34px 26px}.row input{padding:6px 4px;font-size:11px}.pm3dLogo{width:48px;height:48px}.pm3dLogoImg{width:50px;height:50px}.introLogo{width:118px;height:118px}.back{width:36px;height:36px}.headerActions{position:static;justify-content:center;margin-top:8px}.brandRow{flex-direction:column}.brandText{text-align:center}}";
  return css;
}

String currentRequestTarget() {
  String target = server.uri();
  int kept = 0;
  for (int i = 0; i < server.args(); i++) {
    String name = server.argName(i);
    if (name == "lang" || name == "back") continue;
    target += kept == 0 ? "?" : "&";
    target += urlEncode(name) + "=" + urlEncode(server.arg(i));
    kept++;
  }
  if (!target.length()) target = "/main";
  return target;
}

String langButton(const String &code) {
  String cls = (currentLang == code) ? "langBtn langOn" : "langBtn";
  String h = "<a class='btn " + cls + "' href='/setlang?lang=" + code + "&back=" + urlEncode(currentRequestTarget()) + "' ";
  h += "onclick=\"this.href='/setlang?lang=" + code + "&back='+encodeURIComponent(location.pathname+location.search);return true;\">";
  h += code + "</a>";
  return h;
}

String langBarHtml() {
  String h;
  h += "<div class='langBar'>";
  h += langButton("FR");
  h += langButton("NL");
  h += langButton("DE");
  h += langButton("EN");
  h += "<a class='btn langBtn paletteTop' href='/themes' title='" + trKey("themeInterface") + "'>&#127912;</a>";
  h += "<a class='btn langBtn paletteTop' href='/advanced' title='" + trKey("settings") + "'>&#9881;</a>";
  h += "</div>";
  return h;
}
String pageStart(const String &title) {
  String h;
  h += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<title>" + title + "</title>";
  h += "<style>body{margin:0;padding:14px;font-family:Arial,Helvetica,sans-serif;background:#06111F;color:white}.wrap{max-width:760px;margin:auto}.card{border:1px solid #3f8fc4;border-radius:10px;background:#102946;padding:14px;margin:10px 0}.btn,button{display:inline-block;margin:4px;padding:9px 12px;border:0;border-radius:8px;background:#5fbdf2;color:#00192d;font-weight:800;text-decoration:none}input,select{width:100%;box-sizing:border-box;margin:4px 0 9px;padding:8px;border-radius:7px;border:1px solid #5fbdf2;background:#06111F;color:white}.sub,.tiny,.subtitle{color:#cdefff}.title{font-size:21px;font-weight:900}.grid,.countryGrid,.styleGrid,.rowGrid{display:grid;gap:8px}.brandRow{display:flex;gap:10px;align-items:center}.pm3dLogoImg{width:30px;height:30px}.back{position:fixed;left:8px;top:8px;color:white;text-decoration:none;font-size:24px}.advModal{display:none}</style>";
  h += "</head><body><a class='back' href='/countries'>&lt;</a><div class='wrap'>";
  return h;
}

String pageEnd() {
  String h;
  h += "</div></body></html>";
  return h;
}

String logoImg(const String &classes) {
  String h;
  h += "<img class='pm3dLogoImg " + classes + "' alt='PM3D' src='data:image/png;base64,";
  h += FPSTR(PM3D_LOGO_B64);
  h += "'>";
  return h;
}
String logoHeader(const String &title, const String &subtitle) {
  String h;
  h += "<div class='brandRow'>" + logoImg("") + "<div class='brandText'><div class='title'>" + title + "</div><div class='subtitle'>" + subtitle + "</div></div></div>";
  return h;
}

String advancedWarningModalHtml() {
  String h;
  h += "<div id='advModal' class='advModal'><div class='advBox'>";
  h += "<div class='advTitle'>" + trKey("advancedWarningTitle") + "</div>";
  h += "<div class='advText'>" + trKey("advancedWarningText") + "</div>";
  h += "<div class='advBtns'><button class='danger' type='button' onclick='hideAdvancedWarning()'>" + trKey("cancel") + "</button><button type='button' onclick=\"location.href='/advanced'\">" + trKey("understandContinue") + "</button></div>";
  h += "</div></div><script>function showAdvancedWarning(){document.getElementById('advModal').style.display='flex'}function hideAdvancedWarning(){document.getElementById('advModal').style.display='none'}</script>";
  return h;
}


String trText(const String &fr, const String &nl, const String &de, const String &en) {
  if (currentLang == "NL") return nl;
  if (currentLang == "DE") return de;
  if (currentLang == "EN") return en;
  return fr;
}

String trKey(const String &key) {
  if (key == "home") return String("Accueil");
  if (key == "countries") return String("Pays");
  if (key == "styles") return String("Styles");
  if (key == "chooseFamily") return String("Choisir une famille d'affichage");
  if (key == "chooseStyle") return String("Epoque, style ou type d'ecran");
  if (key == "settings") return String("Configuration");
  if (key == "appName") return String("PM3D Ecran Quai");
  if (key == "introSubtitle") return String("Tableau des trains miniature");
  if (key == "trainTable") return String("Tableau des trains");
  if (key == "advanced") return String("Reglages avances");
  if (key == "themes") return String("Themes");
  if (key == "wifiSearch") return String("Recherche Wi-Fi");
  if (key == "update") return String("Mise a jour OTA");
  if (key == "backHome") return String("Retour accueil");
  if (key == "save") return String("Enregistrer et afficher");
  if (key == "favorite") return String("Favori");
  if (key == "noFavorite") return String("Aucun favori pour le moment. Ouvre un pays et touche l'etoile d'un style pour l'ajouter ici.");
  if (key == "screenEdit") return String("Modifier l'ecran");
  if (key == "currentStyle") return String("Style actif");
  if (key == "changeCountry") return String("Changer de pays ou de style");
  if (key == "copyTable") return String("Copier le contenu d'un autre tableau");
  if (key == "source") return String("Source");
  if (key == "copy") return String("Copier");
  if (key == "intercityOptions") return String("Options Intercity");
  if (key == "topMessage") return String("Message du bandeau");
  if (key == "clockStart") return String("Heure programmee");
  if (key == "addTrain") return String("Ajouter un train");
  if (key == "memoryWarn") return String("Attention : chaque train supplementaire consomme de la memoire. Trop d'elements peuvent empecher une future mise a jour OTA.");
  if (key == "noSpace") return String("Plus de place disponible.");
  if (key == "transilienOptions") return String("Options Transilien");
  if (key == "frame") return String("Cadre");
  if (key == "white") return String("Blanc");
  if (key == "crowding") return String("Affluence");
  if (key == "direction") return String("Direction");
  if (key == "servedStops") return String("Gares desservies");
  if (key == "orangeBoxTitle") return String("Titre case orange");
  if (key == "orangeBoxText") return String("Texte case orange");
  if (key == "sncbGridCount") return String("Cases SNCB ancien");
  if (key == "lineCount") return String("Nombre de lignes");
  if (key == "departures") return String("Departs");
  if (key == "time") return String("Heure");
  if (key == "info") return String("Info");
  if (key == "destination") return String("Destination");
  if (key == "train") return String("Train");
  if (key == "platform") return String("Voie");
  if (key == "displaySettings") return String("Reglages affichage");
  if (key == "visibleRows") return String("Lignes");
  if (key == "fontSizeBoard") return String("Police");
  if (key == "smallMediumLarge") return String("1 petit / 2 moyen / 3 grand");
  if (key == "scrollDelay") return String("Delai defilement ms");
  if (key == "brightness") return String("Luminosite directe");
  if (key == "saveSettings") return String("Enregistrer reglages");
  if (key == "connection") return String("Connexion");
  if (key == "password") return String("Mot de passe");
  if (key == "none") return String("aucun");
  if (key == "browserAddress") return String("Adresse navigateur");
  if (key == "scrollDelayShort") return String("Delai defilement");
  if (key == "brightnessShort") return String("Luminosite");
  if (key == "screenCalibration") return String("Calage ecran ST7789");
  if (key == "saveScreenCalibration") return String("Enregistrer calage ecran");
  if (key == "defaultCalibrationNote") return String("Defaut : X1 52, Y1 40, X2 53, Y2 40. Redemarre apres modification du calage.");
  if (key == "styleSettings") return String("Reglages style");
  if (key == "styleGear") return String("Reglages du style");
  if (key == "fontAndGlobalPosition") return String("Police et position globale");
  if (key == "columns") return String("Colonnes");
  if (key == "fontGlobal") return String("Police 0=globale");
  if (key == "positionX") return String("Position X");
  if (key == "positionY") return String("Position Y");
  if (key == "timeX") return String("Heure X");
  if (key == "infoX") return String("Info X");
  if (key == "destinationX") return String("Destination X");
  if (key == "trainX") return String("Train X");
  if (key == "platformX") return String("Voie X");
  if (key == "saveStyle") return String("Enregistrer ce style");
  if (key == "backStyles") return String("Retour styles");
  if (key == "localUpdateConnection") return String("Connexion locale pour mise a jour");
  if (key == "savedWifi") return String("Wi-Fi enregistre");
  if (key == "state") return String("Etat");
  if (key == "connected") return String("connecte");
  if (key == "disconnected") return String("non connecte");
  if (key == "noNetwork") return String("Aucun reseau trouve.");
  if (key == "saveAndConnect") return String("Enregistrer et connecter");
  if (key == "firmware") return String("Firmware PM3D Ecran Quai");
  if (key == "pm3dWifi") return String("Wi-Fi PM3D");
  if (key == "localWifi") return String("Wi-Fi local");
  if (key == "localSsid") return String("SSID local");
  if (key == "firmwareFile") return String("Firmware .bin");
  if (key == "sendAndUpdate") return String("Envoyer et mettre a jour");
  if (key == "searchLocalWifi") return String("Rechercher un Wi-Fi local");
  if (key == "updateNote") return String("La mise a jour peut se faire depuis le Wi-Fi PM3D ou depuis le Wi-Fi local si le module y est connecte. Ne coupe pas l'alimentation pendant l'envoi.");
  if (key == "updateDone") return String("Mise a jour terminee");
  if (key == "updateError") return String("Erreur OTA");
  if (key == "restarting") return String("Redemarrage en cours");
  if (key == "firmwareNotInstalled") return String("Firmware non installe");
  if (key == "updateOkMsg") return String("Le firmware a ete envoye et valide. L'ecran redemarre maintenant.");
  if (key == "updateFailMsg") return String("La mise a jour a echoue. Verifie que le fichier est bien un firmware .bin pour cet ESP32-C3.");
  if (key == "backUpdate") return String("Retour mise a jour");
  if (key == "advancedWarningTitle") return String("Attention - reglages avances");
  if (key == "advancedWarningText") return String("Cette zone est reservee a la configuration technique de l'ecran. Une mauvaise valeur peut empecher l'affichage correct ou rendre le module difficile a joindre.");
  if (key == "cancel") return String("Annuler");
  if (key == "understandContinue") return String("Je comprends - Continuer");
  if (key == "themeBlue") return String("Bleu PM3D");
  if (key == "themeOrange") return String("Orange");
  if (key == "themeYellow") return String("Jaune");
  if (key == "themeGreen") return String("Vert");
  if (key == "themeBlack") return String("Noir");
  if (key == "themePurple") return String("Violet");
  if (key == "themeRed") return String("Rouge");
  if (key == "themeCyan") return String("Cyan");
  if (key == "themeIce") return String("Glace");
  if (key == "themePink") return String("Rose");
  if (key == "saveTheme") return String("Enregistrer le theme");
  if (key == "back") return String("Retour");
  if (key == "themeInterface") return String("Theme interface web");
  return key;
}
String modeButton(int mode, const String &label) {
  String cls = (screenMode == mode) ? "selectedMode" : "";
  return "<button class='" + cls + "' name='mode' value='" + String(mode) + "'>" + label + "</button>";
}

String modeName() {
  if (isItalyProfile()) return "Ecran italien FS / Trenitalia";
  if (screenMode == 1) return "Ancien ecran SNCB";
  if (screenMode == 2) return "Retro aeroport";
  if (screenMode == 3) return "Nouvel ecran SNCF";
  if (screenMode == 4) return "Ancien ecran SNCF";
  return "Nouvel ecran SNCB";
}

String countryFlag(const String &country) {
  return country == "fav" ? String("*") : String("");
}

String countryName(const String &country) {
  if (country == "fav") return String("Favoris");
  return country.length() ? country : String("other");
}

String profileLabel() {
  return displayProfile.length() ? displayProfile : String("PM3D Display");
}

void applyProfileToScreenMode() {
  if (displayProfile == "fr_splitflap" || displayProfile == "fr_tgv_amber" || displayProfile == "ch_sbb_splitflap" || displayProfile == "es_renfe_split") screenMode = 2;
  else if (displayProfile == "fr_rer_a" || displayProfile == "fr_rer_90" || displayProfile == "fr_rer_orange" || displayProfile == "fr_sncf_green" || displayProfile == "fr_sncf_white" || displayProfile == "fr_led_nice") screenMode = 3;
  else if (displayProfile == "be_sncb_detail_list" || displayProfile == "be_sncb_modern" || displayProfile == "be_sncb_grid" || displayProfile == "be_sncb_detail") screenMode = 0;
  else if (displayProfile == "de_db_large_blue" || displayProfile == "de_db_intercity" || displayProfile == "de_db_2010_2015" || displayProfile == "de_db_2022" || displayProfile == "de_db_blue" || displayProfile == "de_db_orange" || displayProfile == "de_db_modern") screenMode = 4;
  else if (displayProfile.startsWith("it_")) screenMode = 3;
  else if (displayProfile.startsWith("hu_")) screenMode = 3;
  else screenMode = 3;
  normalizeSettings();
}

String countryButton(const String &country) {
  String href = "/country?c=" + country;
  return "<a class='btn countryBtn' href='" + href + "'><span class='flag'>" + countryFlag(country) + "</span><span>" + countryName(country) + "</span></a>";
}

String styleButton(const String &profile, const String &label, const String &meta) {
  String cls = (displayProfile == profile) ? "styleBtn selectedMode" : "styleBtn";
  String back = profileCountry(profile);
  return "<form method='POST' action='/setprofile'><input type='hidden' name='profile' value='" + profile + "'><input type='hidden' name='back' value='" + back + "'><button class='" + cls + "' type='submit'>" + label + "<span class='styleMeta'>" + meta + "</span></button></form>";
}

String profileCountry(const String &profile) {
  if (profile.startsWith("fr_")) return "fr";
  if (profile.startsWith("be_")) return "be";
  if (profile.startsWith("de_")) return "de";
  if (profile.startsWith("ch_")) return "ch";
  if (profile.startsWith("uk_")) return "uk";
  if (profile.startsWith("pl_")) return "pl";
  if (profile.startsWith("us_")) return "us";
  if (profile.startsWith("in_")) return "in";
  if (profile.startsWith("nz_")) return "nz";
  if (profile.startsWith("se_")) return "se";
  if (profile.startsWith("jp_")) return "jp";
  if (profile.startsWith("se_")) return "se";
  if (profile.startsWith("jp_")) return "jp";
  if (profile.startsWith("at_")) return "at";
  if (profile.startsWith("nl_")) return "nl";
  if (profile.startsWith("it_")) return "it";
  if (profile.startsWith("hu_")) return "hu";
  if (profile.startsWith("es_")) return "es";
  return "other";
}

bool isFavoriteProfile(const String &profile) {
  return ("," + favoriteProfiles + ",").indexOf("," + profile + ",") >= 0;
}

void toggleFavoriteProfile(const String &profile) {
  String token = "," + profile + ",";
  String list = "," + favoriteProfiles + ",";
  int pos = list.indexOf(token);
  if (pos >= 0) {
    list.remove(pos, token.length() - 1);
    if (list.startsWith(",")) list.remove(0, 1);
    if (list.endsWith(",")) list.remove(list.length() - 1);
    favoriteProfiles = list;
  } else {
    if (favoriteProfiles.length()) favoriteProfiles += ",";
    favoriteProfiles += profile;
  }
}
String styleTitle(const String &profile) {
  return profile.length() ? profile : String("style");
}

String defaultOrderForCountry(const String &country) {
  if (country == "be") return "be_sncb_detail_list";
  if (country == "fr") return "fr_sncf_2012";
  if (country == "de") return "de_db_2022";
  if (country == "nl") return "nl_ns_light";
  if (country == "uk") return "uk_sheffield";
  if (country == "it") return "it_trenitalia";
  if (country == "es") return "es_barcelona_grid";
  return "";
}

bool orderHasProfile(const String &order, const String &profile) {
  return ("," + order + ",").indexOf("," + profile + ",") >= 0;
}

String cleanCountryOrder(const String &country, const String &saved) {
  if (country == "fav") {
    String out;
    int savedCount = profileCount(saved);
    for (int i = 0; i < savedCount; i++) {
      String profile = profileAt(saved, i);
      if (!profile.length()) continue;
      if (orderHasProfile(out, profile)) continue;
      if (out.length()) out += ",";
      out += profile;
    }
    return out;
  }
  String defaults = defaultOrderForCountry(country);
  String out;
  int savedCount = profileCount(saved);
  for (int i = 0; i < savedCount; i++) {
    String profile = profileAt(saved, i);
    if (!profile.length()) continue;
    if (profileCountry(profile) != country) continue;
    if (!orderHasProfile(defaults, profile)) continue;
    if (orderHasProfile(out, profile)) continue;
    if (out.length()) out += ",";
    out += profile;
  }
  int defaultCount = profileCount(defaults);
  for (int i = 0; i < defaultCount; i++) {
    String profile = profileAt(defaults, i);
    if (!profile.length()) continue;
    if (orderHasProfile(out, profile)) continue;
    if (out.length()) out += ",";
    out += profile;
  }
  return out;
}
String getCountryOrder(const String &country) {
  String order;
  if (country == "fav") order = favoriteProfiles;
  else if (country == "fr") order = orderFr;
  else if (country == "be") order = orderBe;
  else if (country == "de") order = orderDe;
  else if (country == "ch") order = orderCh;
  else if (country == "uk") order = orderUk;
  else if (country == "at") order = orderAt;
  else if (country == "nl") order = orderNl;
  else if (country == "it") order = orderIt;
  else if (country == "es") order = orderEs;
  else if (country == "pl" || country == "us" || country == "in" || country == "nz") order = orderOther;
  else order = orderOther;
  order = cleanCountryOrder(country, order);
  return order;
}

void setCountryOrder(const String &country, const String &order) {
  if (country == "fav") favoriteProfiles = order;
  else if (country == "fr") orderFr = order;
  else if (country == "be") orderBe = order;
  else if (country == "de") orderDe = order;
  else if (country == "ch") orderCh = order;
  else if (country == "uk") orderUk = order;
  else if (country == "at") orderAt = order;
  else if (country == "nl") orderNl = order;
  else if (country == "it") orderIt = order;
  else if (country == "es") orderEs = order;
  else if (country == "pl" || country == "us" || country == "in" || country == "nz") orderOther = order;
  else orderOther = order;
}

String profileAt(const String &order, int wanted) {
  int start = 0;
  int index = 0;
  while (start <= order.length()) {
    int comma = order.indexOf(',', start);
    if (comma < 0) comma = order.length();
    if (index == wanted) return order.substring(start, comma);
    start = comma + 1;
    index++;
  }
  return "";
}

int profileCount(const String &order) {
  int count = 1;
  for (int i = 0; i < order.length(); i++) if (order.charAt(i) == ',') count++;
  if (order.length() == 0) return 0;
  return count;
}

String buildOrder(String items[], int count) {
  String out;
  for (int i = 0; i < count; i++) {
    if (i) out += ",";
    out += items[i];
  }
  return out;
}

void moveProfileInCountry(const String &country, const String &profile, const String &dir) {
  String order = getCountryOrder(country);
  int count = profileCount(order);
  String items[10];
  for (int i = 0; i < count && i < 10; i++) items[i] = profileAt(order, i);
  for (int i = 0; i < count; i++) {
    if (items[i] == profile) {
      int j = i;
      if (dir == "up" && i > 0) j = i - 1;
      if (dir == "down" && i < count - 1) j = i + 1;
      if (j != i) {
        String tmp = items[i];
        items[i] = items[j];
        items[j] = tmp;
      }
      break;
    }
  }
  setCountryOrder(country, buildOrder(items, count));
}

String styleEntry(const String &country, const String &profile) {
  String cls = (displayProfile == profile) ? "styleBtn selectedMode" : "styleBtn";
  String homeCountry = profileCountry(profile);
  String h;
  h += "<div class='styleItem'>";
  h += "<form class='styleForm' method='POST' action='/setprofile'><input type='hidden' name='profile' value='" + profile + "'><input type='hidden' name='back' value='" + country + "'><button class='" + cls + "' type='submit'>" + styleTitle(profile) + "</button></form>";
  h += "<div class='styleActions'>";
  h += "<a class='btn miniMove' title='" + trKey("favorite") + "' href='/favoriteprofile?back=" + country + "&p=" + profile + "'>" + String(isFavoriteProfile(profile) ? "&#9733;" : "&#9734;") + "</a>";
  if (country == "fav") h += "<a class='btn miniMove countryChip' href='/country?c=" + homeCountry + "'>" + countryName(homeCountry) + "</a>";
  h += "</div></div>";
  return h;
}

void activateCountryIfNeeded(const String &country) {
  (void)country;
  // Ne plus activer automatiquement un style quand on entre dans un pays.
  // Les pages pays doivent rester vides jusqu'a ce que de nouveaux styles soient ajoutes.
}

String countryStyles(const String &country) {
  String h;
  String order = getCountryOrder(country);
  int count = profileCount(order);
  for (int i = 0; i < count; i++) {
    String profile = profileAt(order, i);
    if (!profile.length()) continue;
    if (country != "fav" && profileCountry(profile) != country) continue;
    h += styleEntry(country, profile);
  }
  return h;
}
String introPage() {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='2;url=/countries'><title>PM3D Display</title>";
  html += "<style>body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;font-family:Arial,Helvetica,sans-serif;background:#06111F;color:white}.box{width:min(92vw,420px);text-align:center;padding:22px;border:1px solid #5fbdf2;border-radius:16px;background:#102946}.title{font-size:24px;font-weight:900}.sub{margin-top:10px;color:#cdefff}</style>";
  html += "</head><body><div class='box'><div class='title'>Display 2</div><div class='sub'>" + trKey("firmware") + "<br>" + String(FW_VERSION) + "</div></div></body></html>";
  return html;
}

String mainPage() {
  String h = pageStart(trKey("home"));
  h += "<div class='card homeHero'>" + logoHeader(trKey("appName"), trKey("trainTable")) + "";
  h += "<div class='mainActions'><a class='btn' href='/countries'>" + trKey("countries") + "</a><a class='btn' href='/config'>" + trKey("settings") + "</a></div>";
  h += "</div>" + advancedWarningModalHtml() + pageEnd();
  return h;
}

String configMenuPage() {
  String h = pageStart(trKey("settings"));
  h += "<div class='card'>" + logoHeader(trKey("settings"), "PM3D Ecran Quai") + "";
  h += "<a class='btn' href='/countries'>" + trKey("countries") + "</a>";
  h += "<button type='button' onclick='showAdvancedWarning()'>" + trKey("advanced") + "</button>";
  h += "";
  h += "<a class='btn' href='/main'>" + trKey("backHome") + "</a>";
  h += "</div>" + advancedWarningModalHtml() + pageEnd();
  return h;
}

String profileSelectOptions(const String &selected) {
  const char* profiles[] = {"be_sncb_detail_list","be_sncb_modern","be_sncb_grid","be_sncb_detail","de_db_intercity","de_db_2010_2015","de_db_large_blue","de_baden_baden","ch_sbb_romandie","ch_zurich_fern","ch_bern_arrival","uk_sheffield","uk_modern","uk_splitflap","at_oebb_blue","at_oebb_green","at_oebb_white","at_oebb_dense","at_oebb_teal","nl_ns_light","nl_ns_blue","it_naples_amber","it_fs_blue","it_fs_amber","hu_mav_arrivals","hu_mav_departures","se_stockholm","jp_jr_led","jp_tokyo_grey","es_barcelona_grid","es_barcelona_adif","pl_pkp_departures","pl_pkp_arrivals","us_la_metro","in_indian_railways","nz_britomart"};
  String h;
  for (unsigned int i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
    String p = profiles[i];
    h += "<option value='" + p + "'" + String(p == selected ? " selected" : "") + ">" + styleTitle(p) + "</option>";
  }
  return h;
}

bool profileBlocksCopy(const String &profile) {
  return profile == "de_db_intercity";
}

String settingsPage() {
  String h = pageStart(trKey("screenEdit"));
  h += "<form method='POST' action='/save'>";
  h += "<div class='card'>" + logoHeader(trKey("screenEdit"), "PM3D Ecran Quai") + "";
  h += "<div class='currentStyle' style='padding:6px;margin:4px 0;font-size:12px'>" + profileLabel() + "</div>";
  if (!profileBlocksCopy(displayProfile)) {
    h += "<details><summary class='section' style='margin:5px 0;padding-top:5px'>" + trKey("copyTable") + "</summary><div class='settingsGrid'><label>" + trKey("source") + "<select name='copySource'>" + profileSelectOptions(displayProfile) + "</select></label><label>&nbsp;<button type='submit' name='doCopy' value='1'>" + trKey("copy") + "</button></label></div></details>";
  }
  if (displayProfile == "fr_transilien" || displayProfile == "fr_transilien_2016") {
    h += "<div class='section'>" + trKey("transilienOptions") + "</div><div class='settingsGrid'>";
    h += "<label>" + trKey("frame") + "<select name='trFrame'><option value='orange'" + String(transilienFrame == "orange" ? " selected" : "") + ">" + trKey("themeOrange") + "</option><option value='white'" + String(transilienFrame == "white" ? " selected" : "") + ">" + trKey("white") + "</option><option value='blue'" + String(transilienFrame == "blue" ? " selected" : "") + ">" + trKey("themeBlue") + "</option></select></label>";
    h += "<label>" + trKey("crowding") + "<input name='trAff' value='" + htmlEscape(transilienAffluence) + "'></label>";
    h += "<label>" + trKey("direction") + "<input name='trDir' value='" + htmlEscape(transilienDirection) + "'></label>";
    h += "<label>" + trKey("servedStops") + "<input name='trStops' value='" + htmlEscape(transilienStops) + "'></label>";
    h += "<label>" + trKey("orangeBoxTitle") + "<input name='trInfoTitle' value='" + htmlEscape(transilienInfoTitle) + "'></label>";
    h += "<label>" + trKey("orangeBoxText") + "<input name='trInfoText' value='" + htmlEscape(transilienInfoText) + "'></label></div>";
  }
  if (displayProfile == "nl_ns_light") {
    h += "<div class='section'>Message NS</div><div class='settingsGrid'>";
    h += "<label>Titre du message<input name='trInfoTitle' value='" + htmlEscape(transilienInfoTitle) + "'></label>";
    h += "<label>Texte du message<input name='trInfoText' value='" + htmlEscape(transilienInfoText) + "'></label></div>";
  }
  if (displayProfile == "be_sncb_grid") {
    h += "<div class='section'>" + styleTitle(displayProfile) + "</div><div class='settingsGrid'>";
    h += stepperControl(trKey("sncbGridCount"), "sncbGrid", sncbGridCount, 6, 18, 3);
    h += "</div>";
  }
  if (displayProfile == "de_db_2010_2015") {
    h += "<div class='section'>DB 2010-2015</div><div class='settingsGrid'>";
    h += "<label>Titre gare<input name='db10Title' value='" + htmlEscape(db2010Title) + "' maxlength='36'></label>";
    h += "<label>Police titre<input name='db10Font' type='number' min='1' max='3' value='" + String(db2010TitleFontSize) + "'></label>";
    h += "</div>";
  }
  if (displayProfile == "de_db_intercity") {
    char dbTime[6];
    snprintf(dbTime, sizeof(dbTime), "%02d:%02d", dbIntercityClockHour, dbIntercityClockMinute);
    h += "<div class='section'>" + trKey("intercityOptions") + "</div><div class='settingsGrid'>";
    h += "<label>" + trKey("topMessage") + "<input name='dbicMsg' value='" + htmlEscape(dbIntercityMessage) + "' maxlength='96'></label>";
    h += "<label>" + trKey("clockStart") + "<input name='dbicTime' type='time' value='" + String(dbTime) + "'></label>";
    h += "</div>";
    h += "<button type='button' onclick=\"pmAddTrain()\">+ " + trKey("addTrain") + "</button>";
    h += "<script>function pmAddTrain(){var warn=\"" + trKey("memoryWarn") + "\";var nospace=\"" + trKey("noSpace") + "\";var n=document.querySelector('input[name=nb]');var max=n?parseInt(n.max||'7'):7;var current=n?parseInt(n.value||'3'):3;var row=null;var idx=-1;for(var k=0;k<max;k++){var c=document.getElementById('row'+k);if(c&&(c.style.display=='none'||getComputedStyle(c).display=='none')){row=c;idx=k;break;}}if(!row){alert(nospace);return;}if(!confirm(warn)){return;}row.style.display='grid';var xs=row.querySelectorAll('input');for(var i=0;i<xs.length;i++){xs[i].value='';}if(n){n.value=Math.max(current,idx+1);}row.scrollIntoView({behavior:'smooth',block:'center'});}</script>";
  }
  h += "<div class='section'>" + trKey("displaySettings") + "</div>";
  h += "<div class='settingsLine settingsLineWide'>";
  if (displayProfile == "de_baden_baden") {
    h += "<label>Voie / Steig<input name='badenSt' value='" + htmlEscape(badenSteig) + "' maxlength='6'></label>";
  }
  int maxVisibleForStyle = profileMaxVisibleRows(displayProfile);
  h += "<label>" + trKey("visibleRows") + "<input name='nb' type='number' min='3' max='" + String(maxVisibleForStyle) + "' value='" + String(nbVisible) + "'></label>";
  h += "<label>" + trKey("fontSizeBoard") + "<input name='font' type='number' min='1' max='3' value='" + String(displayFontSize) + "'></label>";
  h += "<label>" + trKey("scrollDelay") + "<input name='scrollMs' type='number' min='500' max='15000' step='100' value='" + String(scrollDelayMs) + "'></label>";
  h += "<label>" + trKey("brightness") + "<select name='bright'>";
  int pmBrightLevel = map(screenBrightness, 0, 255, 1, 10);
  if (pmBrightLevel < 1) pmBrightLevel = 1;
  if (pmBrightLevel > 10) pmBrightLevel = 10;
  for (int b = 1; b <= 10; b++) {
    h += "<option value='" + String(b) + "'";
    if (b == pmBrightLevel) h += " selected";
    h += ">" + String(b) + "</option>";
  }
  h += "</select></label>";
  h += "</div>";
  h += "<button type='submit'>" + trKey("saveSettings") + "</button></div>";
  h += "<div class='card'><div class='section'>" + trKey("departures") + "</div><div class='head'><div>" + trKey("time") + "</div><div>Retard</div><div>" + trKey("info") + "</div><div>" + trKey("destination") + "</div><div>" + trKey("train") + "</div><div>" + trKey("platform") + "</div></div>";
  int editableRows = displayProfile == "de_db_intercity" ? profileMaxVisibleRows(displayProfile) : MAX_ROWS;
  for (int i = 0; i < editableRows; i++) {
    String extraStyle = (displayProfile == "de_db_intercity" && i >= nbVisible) ? " style='display:none'" : "";
    h += "<div class='row' id='row" + String(i) + "'" + extraStyle + ">";
    h += "<input name='h" + String(i) + "' value='" + htmlEscape(rows[i].heure) + "'>";
    h += "<input name='r" + String(i) + "' value='" + htmlEscape(rows[i].retard) + "'>";
    h += "<input name='i" + String(i) + "' value='" + htmlEscape(rows[i].info) + "'>";
    h += "<input name='d" + String(i) + "' value='" + htmlEscape(rows[i].destination) + "'>";
    h += "<input name='t" + String(i) + "' value='" + htmlEscape(rows[i].typeTrain) + "'>";
    h += "<input name='v" + String(i) + "' value='" + htmlEscape(rows[i].voie) + "'>";
    h += "</div>";
  }
  h += "<button type='submit'>" + trKey("save") + "</button></div></form>" + stepperScript() + pageEnd();
  return h;
}

String themeChoice(const String &id, const String &label) {
  String cls = (uiTheme == id) ? "themePickOn" : "";
  String style = "background:" + themeButtonBgFor(id) + "!important";
  return "<a class='btn themeChoice " + cls + "' style='" + style + "' href='/settheme?theme=" + id + "'>" + label + "</a>";
}

String themesPage() {
  String h = pageStart(trKey("themes"));
  h += "<div class='card'>" + logoHeader(trKey("themes"), trKey("themeInterface")) + "";
  h += "<div class='swatches'>";
  h += themeChoice("blue", trKey("themeBlue"));
  h += themeChoice("orange", trKey("themeOrange"));
  h += themeChoice("yellow", trKey("themeYellow"));
  h += themeChoice("green", trKey("themeGreen"));
  h += themeChoice("black", trKey("themeBlack"));
  h += themeChoice("purple", trKey("themePurple"));
  h += themeChoice("red", trKey("themeRed"));
  h += themeChoice("cyan", trKey("themeCyan"));
  h += themeChoice("ice", trKey("themeIce"));
  h += themeChoice("pink", trKey("themePink"));
  h += "</div></div>" + pageEnd();
  return h;
}

String advancedPage() {
  String h = pageStart(trKey("advanced"));
  h += "<div class='card'>" + logoHeader(trKey("advanced"), "PM3D") + "";
  h += "<div class='section'>" + trKey("connection") + "</div><div class='sub tiny'><b>SSID :</b> " + htmlEscape(apSSID) + "<br><b>" + trKey("password") + " :</b> " + trKey("none") + "<br><b>" + trKey("browserAddress") + " :</b> " + apUrl() + "</div>";
  h += "<div class='section'>" + trKey("currentStyle") + "</div><div class='sub tiny'><b>" + trKey("currentStyle") + " :</b> " + profileLabel() + "<br><b>" + trKey("lineCount") + " :</b> " + String(nbVisible) + "<br><b>" + trKey("scrollDelayShort") + " :</b> " + String(scrollDelayMs) + " ms<br><b>" + trKey("brightnessShort") + " :</b> " + String(screenBrightness) + "</div>";
  h += "<form method='POST' action='/saveadvanced'><div class='section'>" + trKey("password") + " Wi-Fi PM3D</div><label>" + trKey("password") + "<input name='apPass' type='password' minlength='8' maxlength='63' placeholder='8 a 63 caracteres'></label><div class='tiny'>SSID : " + htmlEscape(apSSID) + "</div><div class='section'>" + trKey("screenCalibration") + "</div><div class='settingsGrid'>";
  h += stepperControl("X1", "ox1", tftOffsetX1, 0, 120, 1);
  h += stepperControl("Y1", "oy1", tftOffsetY1, 0, 120, 1);
  h += stepperControl("X2", "ox2", tftOffsetX2, 0, 120, 1);
  h += stepperControl("Y2", "oy2", tftOffsetY2, 0, 120, 1);
  h += stepperControl("Largeur", "pw", tftPanelW, 120, 240, 1);
  h += stepperControl("Hauteur", "ph", tftPanelH, 120, 260, 1);
  h += "</div><button type='submit'>" + trKey("saveScreenCalibration") + "</button><div class='tiny'>" + trKey("defaultCalibrationNote") + "</div></form>" + stepperScript();
  h += "<a class='btn' href='/wifiscan'>" + trKey("wifiSearch") + "</a><a class='btn' href='/updates'>" + trKey("update") + "</a>";
  h += "</div>" + pageEnd();
  return h;
}

String countriesPage() {
  String h = pageStart(trKey("countries"));
  h += "<div class='card'>" + logoHeader(trKey("countries"), trKey("chooseFamily")) + "";
  h += "<div class='countryGrid'>";
  h += countryButton("fav");
  h += countryButton("de");
  h += countryButton("at");
  h += countryButton("be");
  h += countryButton("ch");
  h += countryButton("es");
  h += countryButton("fr");
  h += countryButton("hu");
  h += countryButton("in");
  h += countryButton("it");
  h += countryButton("jp");
  h += countryButton("nl");
  h += countryButton("nz");
  h += countryButton("pl");
  h += countryButton("se");
  h += countryButton("us");
  h += countryButton("uk");
  h += countryButton("other");
  h += "</div></div>" + pageEnd();
  return h;
}

String tablePreviewHtml(const String &profile, const String &country) {
  String h;
  h += "<div class='card tablePreview compactPreview'><div class='previewTitle'>" + styleTitle(profile) + "<a class='btn miniMove tableGear' title='" + trKey("styleGear") + "' href='/stylecfg?p=" + profile + "&back=" + country + "'>&#9881;</a></div>";
  h += "<div class='head'><div>" + trKey("time") + "</div><div>" + trKey("info") + "</div><div>" + trKey("destination") + "</div><div>" + trKey("train") + "</div><div>" + trKey("platform") + "</div></div>";
  for (int i = 0; i < min(nbVisible, 8); i++) {
    int idx = visibleRowIndex(i);
    h += "<div class='row previewRow'><div>" + htmlEscape(rows[idx].heure) + "</div><div>" + htmlEscape(rows[idx].info) + "</div><div>" + htmlEscape(rows[idx].destination) + "</div><div>" + htmlEscape(rows[idx].typeTrain) + "</div><div>" + htmlEscape(rows[idx].voie) + "</div></div>";
  }
  h += "<a class='btn' href='/settings'>" + trKey("screenEdit") + "</a></div>";
  return h;
}
String countryPage() {
  String country = server.hasArg("c") ? server.arg("c") : "fr";
  String h = pageStart(countryName(country));
  h += "<div class='card'>" + logoHeader(countryFlag(country) + " " + countryName(country), trKey("chooseStyle")) + "";
  h += "<div class='styleGrid'>" + countryStyles(country) + "</div>";
  h += "</div>";
  if (profileCountry(displayProfile) == country || country == "fav") h += tablePreviewHtml(displayProfile, country);
  h += pageEnd();
  return h;
}

String stepperScript() {
  return "<script>function pmStep(id,d,min,max){var e=document.getElementById(id);var v=parseInt(e.value||0)+d;if(v<min)v=min;if(v>max)v=max;e.value=v;document.getElementById(id+'Val').innerText=v;}</script>";
}

String stepperControl(const String &label, const String &name, int value, int minVal, int maxVal, int step) {
  String h = "<div class='stepCtl'><span>" + label + "</span><div class='stepBox'>";
  h += "<button type='button' onclick=\"pmStep('" + name + "',-" + String(step) + "," + String(minVal) + "," + String(maxVal) + ")\">-</button>";
  h += "<b id='" + name + "Val'>" + String(value) + "</b><input id='" + name + "' name='" + name + "' type='hidden' value='" + String(value) + "'>";
  h += "<button type='button' onclick=\"pmStep('" + name + "'," + String(step) + "," + String(minVal) + "," + String(maxVal) + ")\">+</button>";
  h += "</div></div>";
  return h;
}

String styleConfigPage() {
  String profile = server.hasArg("p") ? server.arg("p") : displayProfile;
  String back = server.hasArg("back") ? server.arg("back") : profileCountry(profile);
  loadStyleTune(profile);
  String h = pageStart(trKey("styleSettings"));
  h += "<form method='POST' action='/savestylecfg'><input type='hidden' name='profile' value='" + profile + "'><input type='hidden' name='back' value='" + back + "'>";
  h += "<div class='card'>" + logoHeader(trKey("styleSettings"), styleTitle(profile)) + "";
  h += "<div class='section'>" + trKey("fontAndGlobalPosition") + "</div><div class='settingsGrid'>";
  h += stepperControl(trKey("fontGlobal"), "sf", styleFontSize, 0, 3, 1);
  h += stepperControl(trKey("positionX"), "sx", styleMoveX, -80, 80, 1);
  h += stepperControl(trKey("positionY"), "sy", styleMoveY, -80, 80, 1);
  h += "</div><div class='section'>" + trKey("columns") + "</div><div class='settingsGrid'>";
  h += stepperControl(trKey("timeX"), "st", styleTimeX, -80, 80, 1);
  h += stepperControl(trKey("infoX"), "si", styleInfoX, -80, 80, 1);
  h += stepperControl(trKey("destinationX"), "sd", styleDestX, -80, 80, 1);
  h += stepperControl(trKey("trainX"), "sr", styleTrainX, -80, 80, 1);
  h += stepperControl(trKey("platformX"), "sv", styleVoieX, -80, 80, 1);
  h += "</div><button type='submit'>" + trKey("saveStyle") + "</button>";
  h += "<a class='btn' href='/country?c=" + back + "'>" + trKey("backStyles") + "</a></div></form>" + stepperScript() + pageEnd();
  if (profile != displayProfile) loadStyleTune(displayProfile);
  return h;
}
String wifiScanPage() {
  int n = WiFi.scanNetworks();
  String h = pageStart(trKey("wifiSearch"));
  h += "<div class='card'>" + logoHeader(trKey("wifiSearch"), trKey("localUpdateConnection")) + "";
  h += "<div class='sub tiny'><b>" + trKey("savedWifi") + " :</b> " + (localWifiSsid.length() ? htmlEscape(localWifiSsid) : trKey("none")) + "<br><b>" + trKey("state") + " :</b> " + String(WiFi.status() == WL_CONNECTED ? trKey("connected") : trKey("disconnected")) + "</div>";
  if (n <= 0) h += "<div class='sub tiny'>" + trKey("noNetwork") + "</div>";
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    h += "<form method='POST' action='/savewifi'><div class='sub tiny'><b>" + htmlEscape(ssid) + "</b> <span class='pill'>" + String(WiFi.RSSI(i)) + " dBm</span><input type='hidden' name='ssid' value='" + htmlEscape(ssid) + "'><label>" + trKey("password") + "</label><input name='pass' type='password'><button type='submit'>" + trKey("saveAndConnect") + "</button></div></form>";
  }
  WiFi.scanDelete();
  h += "</div>" + pageEnd();
  return h;
}

String updatePage() {
  String h = pageStart(trKey("update"));
  h += "<div class='card'>" + logoHeader(trKey("update"), trKey("firmware")) + "";
  h += "<div class='sub tiny'><b>" + trKey("pm3dWifi") + " :</b> " + htmlEscape(apSSID) + "<br><b>" + trKey("browserAddress") + " :</b> " + apUrl() + "<br><b>" + trKey("localWifi") + " :</b> " + String(WiFi.status() == WL_CONNECTED ? trKey("connected") : trKey("disconnected")) + "<br><b>" + trKey("localSsid") + " :</b> " + (localWifiSsid.length() ? htmlEscape(localWifiSsid) : trKey("none")) + "</div>";
  h += "<form method='POST' action='/ota' enctype='multipart/form-data'><div class='sub tiny'><label>" + trKey("firmwareFile") + "</label><input name='firmware' type='file' accept='.bin,application/octet-stream' required><button type='submit'>" + trKey("sendAndUpdate") + "</button></div></form>";
  h += "<a class='btn' href='/wifiscan'>" + trKey("searchLocalWifi") + "</a>";
  h += "<div class='sub tiny'>" + trKey("updateNote") + "</div>";
  h += "</div>" + pageEnd();
  return h;
}

void handleOtaDone() {
  bool ok = !Update.hasError();
  String h = pageStart(ok ? trKey("updateDone") : trKey("updateError"));
  h += "<div class='card'>" + logoHeader(ok ? trKey("updateDone") : trKey("updateError"), ok ? trKey("restarting") : trKey("firmwareNotInstalled")) + "";
  if (ok) {
    h += "<div class='sub tiny'>" + trKey("updateOkMsg") + "</div><script>setTimeout(function(){location.href='/intro'},9000)</script>";
  } else {
    h += "<div class='sub tiny'>" + trKey("updateFailMsg") + "</div><a class='btn' href='/updates'>" + trKey("backUpdate") + "</a>";
  }
  h += "</div>" + pageEnd();
  server.send(ok ? 200 : 500, "text/html", h);
  if (ok) {
    delay(600);
    ESP.restart();
  }
}

void handleOtaUpload() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("OTA start: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) {
      Update.printError(Serial);
    } else {
      Serial.printf("OTA OK: %u bytes\n", upload.totalSize);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    Serial.println("OTA aborted");
  }
}

String normalizeFirmwareUrl(String url) {
  url.trim();
  url.replace("https://github.com/PM3D-Wavre/pm3d-firmware/raw/refs/heads/main/",
              "https://raw.githubusercontent.com/PM3D-Wavre/pm3d-firmware/refs/heads/main/");
  url.replace("https://github.com/PM3D-Wavre/pm3d-firmware/raw/main/",
              "https://raw.githubusercontent.com/PM3D-Wavre/pm3d-firmware/main/");
  url.replace("https://github.com/PM3D-Wavre/pm3d-firmware/blob/refs/heads/main/",
              "https://raw.githubusercontent.com/PM3D-Wavre/pm3d-firmware/refs/heads/main/");
  url.replace("https://github.com/PM3D-Wavre/pm3d-firmware/blob/main/",
              "https://raw.githubusercontent.com/PM3D-Wavre/pm3d-firmware/main/");
  return url;
}

bool performDirectOtaFromUrl(const String &rawUrl, String &message) {
  String url = normalizeFirmwareUrl(rawUrl);
  if (!(url.startsWith("https://") || url.startsWith("http://")) || !url.endsWith(".bin")) {
    message = "Lien firmware refuse";
    return false;
  }

  HTTPClient http;
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  bool okBegin = false;
  if (url.startsWith("https://")) {
    secureClient.setInsecure();
    okBegin = http.begin(secureClient, url);
  } else {
    okBegin = http.begin(plainClient, url);
  }
  if (!okBegin) {
    message = "Connexion firmware impossible";
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    message = String("Erreur HTTP ") + String(code);
    http.end();
    return false;
  }

  int len = http.getSize();
  if (!Update.begin(len > 0 ? (size_t)len : UPDATE_SIZE_UNKNOWN)) {
    message = String("Espace OTA insuffisant: ") + Update.errorString();
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  size_t written = Update.writeStream(*stream);
  bool ok = Update.end(true);
  http.end();

  if (!ok) {
    message = String("Finalisation OTA impossible: ") + Update.errorString();
    return false;
  }
  if (len > 0 && written != (size_t)len) {
    message = "Firmware incomplet";
    return false;
  }
  message = "Mise a jour OTA directe terminee";
  return true;
}

void handleApiUpdate() {
  String url = server.hasArg("url") ? server.arg("url") : (server.hasArg("u") ? server.arg("u") : "");
  String version = server.hasArg("version") ? server.arg("version") : FW_VERSION;
  url = normalizeFirmwareUrl(url);
  if (!url.length()) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing_url\"}");
    return;
  }
  server.send(202, "application/json", "{\"ok\":true,\"accepted\":true,\"version\":\"" + version + "\"}");
  delay(250);
  String msg;
  if (performDirectOtaFromUrl(url, msg)) {
    Serial.println(msg);
    delay(700);
    ESP.restart();
  } else {
    Serial.println(msg);
  }
}

void handleApiRegister() {
  String json = "{";
  json += "\"brand\":\"PM3D\",";
  json += "\"device_type\":\"display\",";
  json += "\"model\":\"ST7789_114_RGB\",";
  json += "\"firmware\":\"" + String(FW_VERSION) + "\",";
  json += "\"build\":\"" + String(FW_BUILD_LABEL) + "\",";
  json += "\"ota\":true";
  json += "}";
  server.send(200, "application/json", json);
}

void handleIntro() { server.send(200, "text/html", introPage()); }
void handleMain() { server.send(200, "text/html", mainPage()); }
void handleConfigMenu() { server.send(200, "text/html", configMenuPage()); }
void handleSettingsPage() { server.send(200, "text/html", settingsPage()); }
void handleThemes() { server.send(200, "text/html", themesPage()); }
void handleAdvanced() { server.send(200, "text/html", advancedPage()); }
void handleCountries() { server.send(200, "text/html", countriesPage()); }
void handleCountry() {
  String country = server.hasArg("c") ? server.arg("c") : profileCountry(displayProfile);
  activateCountryIfNeeded(country);
  server.send(200, "text/html", countryPage());
}
void handleWifiScan() { server.send(200, "text/html", wifiScanPage()); }
void handleUpdates() { server.send(200, "text/html", updatePage()); }

void handleStyleConfig() { server.send(200, "text/html", styleConfigPage()); }

void handleSaveStyleConfig() {
  String profile = server.hasArg("profile") ? server.arg("profile") : displayProfile;
  String back = server.hasArg("back") ? server.arg("back") : profileCountry(profile);
  if (server.hasArg("sf")) styleFontSize = constrain(server.arg("sf").toInt(), 0, 3);
  if (server.hasArg("sx")) styleMoveX = constrain(server.arg("sx").toInt(), -80, 80);
  if (server.hasArg("sy")) styleMoveY = constrain(server.arg("sy").toInt(), -80, 80);
  if (server.hasArg("st")) styleTimeX = constrain(server.arg("st").toInt(), -80, 80);
  if (server.hasArg("si")) styleInfoX = constrain(server.arg("si").toInt(), -80, 80);
  if (server.hasArg("sd")) styleDestX = constrain(server.arg("sd").toInt(), -80, 80);
  if (server.hasArg("sr")) styleTrainX = constrain(server.arg("sr").toInt(), -80, 80);
  if (server.hasArg("sv")) styleVoieX = constrain(server.arg("sv").toInt(), -80, 80);
  saveStyleTune(profile);
  if (profile == displayProfile) fullRedrawNeeded = true;
  else loadStyleTune(displayProfile);
  server.sendHeader("Location", "/country?c=" + back);
  server.send(303);
}

void handleSaveAdvanced() {
  if (server.hasArg("ox1")) tftOffsetX1 = server.arg("ox1").toInt();
  if (server.hasArg("oy1")) tftOffsetY1 = server.arg("oy1").toInt();
  if (server.hasArg("ox2")) tftOffsetX2 = server.arg("ox2").toInt();
  if (server.hasArg("oy2")) tftOffsetY2 = server.arg("oy2").toInt();
  if (server.hasArg("pw")) tftPanelW = server.arg("pw").toInt();
  if (server.hasArg("ph")) tftPanelH = server.arg("ph").toInt();
  normalizeTftOffsets();
  saveConfig();
  server.sendHeader("Location", "/advanced");
  server.send(303);
}
void handleFavoriteProfile() {
  String profile = server.hasArg("p") ? server.arg("p") : "";
  String back = server.hasArg("back") ? server.arg("back") : profileCountry(profile);
  if (profile.length()) toggleFavoriteProfile(profile);
  saveConfig();
  server.sendHeader("Location", "/country?c=" + back);
  server.send(303);
}
void handleMoveProfile() {
  String country = server.hasArg("c") ? server.arg("c") : "fr";
  String profile = server.hasArg("p") ? server.arg("p") : "";
  String dir = server.hasArg("dir") ? server.arg("dir") : "up";
  moveProfileInCountry(country, profile, dir);
  saveConfig();
  server.sendHeader("Location", "/country?c=" + country);
  server.send(303);
}
String safeBackTarget(const String &rawBack) {
  String back = rawBack.length() ? rawBack : String("/main");
  if (!back.startsWith("/")) back = "/main";
  if (back.startsWith("//")) back = "/main";
  if (back.indexOf("://") >= 0) back = "/main";
  return back;
}

void handleSetLang() {
  String requested = server.hasArg("lang") ? server.arg("lang") : currentLang;
  requested.toUpperCase();
  if (requested == "FR" || requested == "NL" || requested == "DE" || requested == "EN") currentLang = requested;
  saveConfig();
  String back = safeBackTarget(server.hasArg("back") ? server.arg("back") : String("/main"));
  server.sendHeader("Location", back);
  server.send(303);
}
void handleSetBright() {
  if (server.hasArg("bright")) {
    screenBrightness = map(constrain(server.arg("bright").toInt(), 1, 10), 1, 10, 25, 255);
    if (screenBrightness < 20) screenBrightness = 20;
    if (screenBrightness > 255) screenBrightness = 255;
    prefs.putInt("bright", screenBrightness);
    analogWrite(TFT_BL, screenBrightness);
  }
  server.send(200, "text/plain", String(screenBrightness));
}

void handleSetTheme() {
  if (server.hasArg("theme")) uiTheme = server.arg("theme");
  uiTheme.toLowerCase();
  if (uiTheme != "blue" && uiTheme != "orange" && uiTheme != "yellow" && uiTheme != "green" && uiTheme != "black" && uiTheme != "purple" && uiTheme != "red" && uiTheme != "cyan" && uiTheme != "ice" && uiTheme != "pink") uiTheme = "blue";
  saveConfig();
  server.sendHeader("Location", "/themes");
  server.send(303);
}


void setRow(int i, const String &heure, const String &info, const String &destination, const String &typeTrain, const String &voie, const String &retard = "") {
  if (i < 0 || i >= MAX_ROWS) return;
  rows[i].heure = heure;
  rows[i].retard = retard;
  rows[i].info = info;
  rows[i].destination = destination;
  rows[i].typeTrain = typeTrain;
  rows[i].voie = voie;
}

void clearRowsFrom(int startIndex) {
  for (int i = startIndex; i < MAX_ROWS; i++) setRow(i, "", "", "", "", "");
}

void loadProfileDefaults(const String &profile) {
  if (profile == "fr_sncf_first") {
    setRow(0,"19.08","","Combs MELUN","ZYCK","Long");
    setRow(1,"19.23","","Combs MELUN","ZYCK","Long");
    setRow(2,"20.16","","Corbeil","ROVA","Long");
    setRow(3,"20.26","","Malesherbes","BIPE","Court");
    setRow(4,"","Train terminus","","DIPA","Long");
    clearRowsFrom(5);
    transilienInfoTitle = "Attention";
    transilienInfoText = "Attention, le train de 19.08 - voie 3 - est remplace par le train ZYCHO. Veuillez consulter les affichages de desserte.";
  } else if (profile == "fr_sncf_old_led") {
    setRow(0,"19:52","","Goussainville","FACA","2B");
    setRow(1,"retarde","","Orry la Ville","LOVA","2B");
    setRow(2,"retarde","","Goussainville","FACA","2B");
    setRow(3,"20:05","","Orry la Ville","LOVA","2B");
    setRow(4,"20:15","","Paris Lyon (RER A)","FACA","2B");
    setRow(5,"supprime","","Paris Lyon (RER A)","DOVA","2B");
    clearRowsFrom(6);
    transilienInfoTitle = "Info";
    transilienInfoText = "L'interconnexion est suspendue jusqu'a nouvel ordre suite a un important degagement de fumee a Chatelet. Un retour a la normale est prevu a 21h00.";
  } else if (profile == "fr_sncf_arrivals") {
    setRow(0,"14h06","9813","Bruxelles","TGV","C");
    setRow(1,"14h12","5113","Lille Europe","TGV","E");
    setRow(2,"14h15","6212","Beziers","TGV","A");
    setRow(3,"13h48","6811","Lyon Part Dieu","TGV","");
    setRow(4,"14h36","6609","Paris Lyon","TGV","");
    setRow(5,"14h36","6061","Paris Lyon","TGV","");
    setRow(6,"14h46","876419","Nimes","TER","");
    clearRowsFrom(7);
    transilienInfoTitle = "Arrivees";
    transilienInfoText = "TRANSPORT ALTERNATIF. AVANT DE VOUS RENDRE EN GARE, CONSULTEZ LES AFFICHAGES.";
  } else if (profile == "fr_rer_a") {
    setRow(0,"07:06","ZEMA","St-Germain-en-Laye","A","5");
    setRow(1,"","","Saint-Germain-en-Laye","A","");
    setRow(2,"","","Le Vesinet-Le Pecq","A","");
    setRow(3,"","","Le Vesinet-Centre","A","");
    setRow(4,"","","Chatou-Croissy","A","");
    setRow(5,"","","Rueil-Malmaison","A","");
    setRow(6,"","","Nanterre-Ville","A","");
    setRow(7,"","","Nanterre-Universite","A","");
    clearRowsFrom(8);
  } else if (profile == "fr_saint_lazare") {
    setRow(0,"16h21","32mn","Acheres Ville","J","6");
    setRow(1,"16h41","29mn","Andresy","J","");
    setRow(2,"16h27","11mn","Argenteuil","J","9");
    setRow(3,"16h16","supprime","Asnieres-sur-Seine","L","");
    setRow(4,"16h44","39mn","Aubergenville Elisabeth","J","");
    setRow(5,"16h16","09mn","Becon les Bruyeres","L","6");
    setRow(6,"16h30","08mn","Bois-Colombes","J","");
    setRow(7,"16h44","41mn","Boissy-l'Aillerie","J","");
    setRow(8,"16h55","56mn","Bonnieres","J","");
    setRow(9,"16h22","32mn","Bougival","L","");
    setRow(10,"16h30","49mn","Breval","J","17");
    setRow(11,"16h31","56mn","Bueil","J","17");
    setRow(12,"16h21","49mn","Cergy le Haut","A","6");
    setRow(13,"16h21","41mn","Cergy Prefecture","A","6");
    setRow(14,"16h21","45mn","Chanteloup-les-Vignes","J","");
    setRow(15,"16h44","55mn","Chars","J","");
  } else if (profile == "fr_transilien_p") {
    setRow(0,"09h36","","Meaux","CITU","2");
    setRow(1,"09h41","","Chateau Thierry","PICI","3");
    setRow(2,"09h46","","Coulommiers","CITU","4");
    setRow(3,"09h51","","Provins","PIBU","5");
    setRow(4,"09h56","","La Ferte Milon","FIMO","6");
    setRow(5,"10h01","","Chelles Gournay","PICI","7");
    setRow(6,"10h06","","Tournan","PITU","8");
    setRow(7,"10h11","","Esbly","CITU","9");
    setRow(8,"","approche","Paris Est","PICI","2");
    setRow(9,"09h36","","Lagny Thorigny","VICK","2");
    setRow(10,"","approche","Vaires Torcy","PICI","2");
    setRow(11,"09h41","","Longueville","PIBU","1");
    setRow(12,"09h46","","Verneuil l'Etang","PIBU","1");
    setRow(13,"","approche","Nangis","PIBU","2");
    setRow(14,"09h51","","Gretz Armainvilliers","PITU","1");
    setRow(15,"09h56","","Mortcerf","CITU","2");
    transilienFrame = "blue";
    transilienAffluence = "";
    transilienDirection = "Transilien ligne P";
    transilienStops = "Paris Est, Chelles Gournay, Vaires Torcy, Lagny Thorigny, Esbly, Meaux, Chateau Thierry, Coulommiers, Provins, La Ferte Milon";
    transilienInfoTitle = "Ligne P";
    transilienInfoText = "Prochains trains";
  } else if (profile == "fr_transilien_2016") {
    setRow(0,"12h27","14min","Osny","J","1");
    setRow(1,"12h51","39min","Poissy","J","");
    setRow(2,"12h31","24min","Pont Cardinet","L","7");
    setRow(3,"12h27","39min","Pontoise","H","9");
    setRow(4,"12h35","43min","Puteaux","L","4");
    setRow(5,"16h51","40min","Rosny sur Seine","J","");
    setRow(6,"12h29","14min","Saint-Cloud","L","1");
    setRow(7,"12h32","40min","Saint-Nom la Breteche","L","");
    setRow(8,"13h27","10min","Trie Chateau","J","");
    setRow(9,"13h27","45min","Triel sur Seine","J","");
    setRow(10,"13h27","12min","Us","J","");
    setRow(11,"13h27","12min","Val d'Argenteuil","J","9");
    setRow(12,"12h32","23min","Vaucresson","L","");
    setRow(13,"12h42","49min","Vaux sur Seine","J","");
    setRow(14,"12h51","28min","Versailles Rive Droite","L","1");
    setRow(15,"12h29","24min","Viroflay Rive Droite","L","1");
    transilienFrame = "orange";
    transilienAffluence = "";
    transilienDirection = "Departs Ile-de-France";
    transilienStops = "Osny, Poissy, Pont Cardinet, Pontoise, Puteaux, Rosny sur Seine, Saint-Cloud, Saint-Nom la Breteche";
    transilienInfoTitle = "Info";
    transilienInfoText = "";
  } else if (profile == "fr_transilien") {
    setRow(0,"imminent","Page 2/4","Versailles Chateau via Paris","VICK","F");
    setRow(1,"8 min","Se situe entre St-Quentin en Y et Saint-Cyr","Paris puis > St-Martin d'Etampes","ELBH","F");
    setRow(2,"18 min","","Versailles Chateau via Paris","VICK","F");
    clearRowsFrom(3);
    transilienFrame = "orange";
    transilienAffluence = "Attention, desserte modifiee";
    transilienDirection = "Versailles Chateau via Paris";
    transilienStops = "Ablon, Villeneuve le Roi supprime, Choisy le Roi, Biblioth. F. Mitterrand, Paris Austerlitz, St-Michel Notre Dame, Musee d'Orsay, Invalides, Pont de l'Alma";
    transilienInfoTitle = "Infos Travaux";
    transilienInfoText = "Travaux a Villeneuve le Roi, l'arret est supprime. Bus de remplacement entre Ablon et Choisy le Roi. Plus d'informations: Transilien.com";
  } else if (profile == "fr_sncf_2012") {
    setRow(0,"17h43","Chatillon * Villars les Dombes * St-Andre de Corcy","Lyon Perrache","ter 886441","A");
    setRow(1,"17h57","Bellegarde * Lyon Part-Dieu * Le Creusot TGV","Paris Gare de Lyon","TGV 4243","4");
    setRow(2,"18h04","Bourg-en-Bresse * Bellegarde * Geneve","Geneve Cornavin","TGV Lyria 9777","");
    setRow(3,"18h05","Pont-de-Veyle * Macon Loche TGV","Macon Ville","ter 49622","");
    setRow(4,"18h10","Villars les Dombes * Marlieux-Chatillon","St-Andre de Corcy","ter 49607","");
    setRow(5,"18h19","Bourg-en-Bresse * Poligny * Arbois","Lons-le-Saunier","ter 895812","");
    setRow(6,"18h23","Villars les Dombes * Bourg-en-Bresse","Oyonnax","ter 886409","");
    setRow(7,"18h28","Meximieux * Amberieu * Culoz","Amberieu-en-Bugey","ter 882125","");
    clearRowsFrom(8);
  } else if (profile == "be_sncb_detail_list") {
    setRow(0,"18:12","Ce train s'arrete a : Delta, Etterbeek, Brux-Luxemb.","Bruxelles-Luxembourg","S7","2");
    setRow(1,"18:20","+ 7 Ce train s'arrete a : Delta, Etterbeek, Brux-Luxemb, Brux-Schuman, Boeckstael, Jette, Berchem-St-A, Gijbaarden, Dilbeek, St-M-Bodegem, Ternat, Essene-Lomb, Denderleeuw, Erembodegem, Alost.","Alost","S4","2");
    setRow(2,"18:41","Ce train s'arrete a : Meiser, Evere, Bordet, Vilvorde, Eppegem, Hofstade, Muizen, Malines.","Malines","S4","2");
    setRow(3,"18:48","Ce train s'arrete a : Meiser, Evere, Bordet, Haren, Vilvorde, Eppegem, Weerde, Malines.","Malines","S7","1");
    clearRowsFrom(4);
  } else if (profile == "be_sncb_grid") {
    setRow(0,"10:28","","Nivelles","S1","21");
    setRow(1,"10:29","","Knokke","IC","9");
    setRow(2,"10:30","","Louvain","S2","17");
    setRow(3,"10:33","","Turnhout","IC","16");
    setRow(4,"10:34 /5","","Luxembourg","IC","11");
    setRow(5,"10:38","","Courtrai","IC","17");
    setRow(6,"10:41","","Anvers-C.","IC","5");
    setRow(7,"10:44","","Namur","IC","12");
    setRow(8,"10:47","","Ostende","IC","6");
    setRow(9,"10:50","","Charleroi","IC","18");
    setRow(10,"10:53","","Gand-St-P","IC","10");
    setRow(11,"10:56","","Ottignies","S8","14");
    clearRowsFrom(12);
  } else if (profile == "be_sncb_modern") {
    setRow(0,"08:12","+27","Louvain","IC","9");
    setRow(1,"08:14","Supprime","Louvain-la-Neuve","S8","--");
    setRow(2,"08:31","+13","Louvain-la-Neuve","S81","5");
    setRow(3,"08:41","","Wavre via Bgs-Walibi","S61","10");
    setRow(4,"08:43","+8","Bruxelles-Midi via Brux-Luxemb, Brux-Schuman","IC","2");
    setRow(5,"08:47","","Bruxelles-Midi via Etterbeek, Brux-Luxemb,...","S8","1");
    setRow(6,"08:49","","Liege-Saint-Lambert via Gembloux, Namur,...","IC","3");
    setRow(7,"08:52","Supprime","Louvain-la-Neuve","S8","--");
    setRow(8,"08:52","+13","Charleroi-Central via Fleurus","IC","11");
    setRow(9,"08:55","","Louvain","S20","6");
    setRow(10,"08:58","+7","Brussels Airport via Etterbeek, Brux-Schuman, Bordet","IC","2");
    setRow(11,"09:02","","Dinant via Gembloux, Namur","IC","5");
    clearRowsFrom(12);
  } else if (profile == "be_sncb_detail") {
    setRow(0,"12:02","","Ostende","IC","6");
    setRow(1,"12:07","","Namur","IC","5");
    setRow(2,"12:12","","Mons","IC","3");
    setRow(3,"12:18","","Louvain","S2","4");
    setRow(4,"12:24","","Bruxelles","S10","2");
    setRow(5,"12:31","","Liege-G.","IC","7");
    clearRowsFrom(6);
  } else if (profile == "fr_led_nice") {
    setRow(0,"12:02","","Paris-Est","TGV","3");
    setRow(1,"12:08","","Lyon Part-D.","TGV","5");
    setRow(2,"12:16","","Marseille","TGV","7");
    setRow(3,"12:22","","Bordeaux","TGV","2");
    setRow(4,"12:29","","Lille Flandres","TER","9");
    setRow(5,"12:35","","Strasbourg","ICE","4");
    setRow(6,"12:42","","Nantes","TGV","6");
    setRow(7,"12:51","","Rennes","TGV","8");
    clearRowsFrom(12);
  } else if (profile == "fr_rer_90" || profile == "fr_sncf_green") {
    setRow(0,"18h24","","Rothau","TER","9");
    setRow(1,"18h29","","Molsheim","TER","8");
    setRow(2,"18h35","","Paris-Est","TGV","");
    setRow(3,"18h45","","Selestat","TER","");
    setRow(4,"18h51","","Bale","TER","6");
    setRow(5,"18h55","","St-Die","TER","");
    clearRowsFrom(6);
  } else if (profile.startsWith("fr_")) {
    setRow(0,"07:54","","Paris-Est","TGV","3");
    setRow(1,"08:03","","Nancy","TER","8");
    setRow(2,"08:12","","Metz","TER","5");
    setRow(3,"08:18","","Mulhouse","TER","2");
    setRow(4,"08:26","","Lyon","TGV","7");
    setRow(5,"08:34","","Dijon","TER","4");
    clearRowsFrom(6);
  } else if (profile == "de_baden_baden") {
    // Lignes pre-remplies inspirees du reseau reel Baden-Baden / KVV 2025.
    setRow(0,"3 Min","", "Stadtmitte - Leopoldsplatz", "201/5", "");
    setRow(1,"5 Min","", "Oberbeuern - Lichtental", "201/5", "");
    setRow(2,"7 Min","", "Baden-Baden Bahnhof", "203/5", "");
    setRow(3,"11 Min","", "Cite - Oos", "205/5", "");
    setRow(4,"14:27","", "Sandweier - Rastatt", "212/5", "");
    setRow(5,"14:35","", "Neuweier - Steinbach", "216/5", "");
    setRow(6,"14:44","", "Iffezheim - Wintersdorf", "218/5", "");
    setRow(7,"14:52","", "Kuppenheim", "243/5", "");
    setRow(8,"15:02","", "Loffenau - Bad Herrenalb", "244/5", "");
    setRow(9,"15:12","", "Buehl", "245/5", "");
    setRow(10,"15:26","", "Sinzheim - Huegelsheim", "285/5", "");
    setRow(11,"15:39","", "Baden-Baden Bahnhof", "262/5", "");
    setRow(12,"15:47","", "Merkurwald", "204/5", "");
    setRow(13,"16:04","", "Augustaplatz - Lichtental", "207/5", "");
    setRow(14,"16:21","", "Gaggenau", "214/5", "");
    setRow(15,"16:34","", "Stadtmitte - Leopoldsplatz", "X45/5", "");
  } else if (profile == "de_db_intercity") {
    dbIntercityMessage = "Bitte beachten Sie die Anzeige am Bahnsteig";
    dbIntercityClockHour = 14;
    dbIntercityClockMinute = 43;
    setRow(0,"14:53","Duisburg Hbf","Essen Hbf","ICE 124","2");
    setRow(1,"20:00","Duisburg - Duesseldorf - Koeln - Frankfurt Flughafen - Mannheim - Karlsruhe","Karlsruhe Hbf","ICE 205","2");
    setRow(2,"20:00","Bochum - Dortmund - Muenster - Osnabrueck - Bremen","Hamburg-Altona","ICE 104","6");
    setRow(3,"20:21","Bochum - Dortmund - Hamm - Bielefeld - Hannover - Wolfsburg","Berlin Ostbahnhof","ICE 947","6");
    setRow(4,"20:39","Duisburg - Duesseldorf Flughafen - Duesseldorf - Koeln","Koeln Hbf","ICE 544","1");
    setRow(5,"20:59","Bochum - Dortmund - Muenster - Osnabrueck - Bremen","Hamburg-Altona","ICE 514","6");
    setRow(6,"21:00","Duisburg - Duesseldorf - Koeln - Frankfurt Flughafen - Mannheim","Stuttgart Hbf","ICE 617","2");
    clearRowsFrom(7);
  } else if (profile == "de_db_2010_2015") {
    setRow(0,"17:49","Mainz Hbf, Mannheim Hbf, Heidelberg Hbf","Stuttgart Hbf","IC 2213","3");
    setRow(1,"18:21","Witten Hbf, Wetter(Ruhr), Hagen-Vorhalle","Hagen Hbf","ABR 5677","6");
    setRow(2,"18:24","Koeln Messe/Deutz Gl.11-12, Essen Hbf, Duisburg Hbf","Koeln/Bonn","ICE 546","3");
    setRow(3,"18:26","","Dortmund Hbf","RE 10025","5");
    setRow(4,"18:29","Koeln Messe/Deutz, Siegburg/Bonn, Montabaur, Limburg Sued, Frankfurt","Muenchen Hbf","ICE 821","4");
    setRow(5,"18:33","Essen Hbf, Duisburg Hbf, Duesseldorf Hbf","Aachen Hbf","RE 10128","2");
    clearRowsFrom(6);
  } else if (profile == "de_db_2022") {
    setRow(0,"12:39","Luebstorf - Schwerin Hbf - SN Mitte - SN Goerries - SN Sued - Holthusen","Ludwigslust","RB 17 / 13161","");
    setRow(1,"13:00","Grevesmuehlen - Grieben - Schoenberg - Herrnburg - Luebeck St Juergen","Luebeck Hbf","RE 4 / 93354","3");
    setRow(2,"13:03","Luebstorf","Schwerin Hbf","RB 18 / 13183","4 A");
    setRow(3,"13:04","Ventschow - Blankenberg - Buetzow - Guestrow - Teterow - Malchin","Neubrandenburg","RE 4 / 93361","3");
    setRow(4,"13:17","Moeldentin - Dorf Mecklenburg","Wismar","RB 17 / 13162","1 B-C");
    setRow(5,"13:39","Schwerin Hbf - Ludwigslust - Wittenberge - Nauen - Berlin Hbf","Berlin Ostkreuz","RE 2 / 63982","2");
    clearRowsFrom(6);
  } else if (profile == "de_db_large_blue" || profile.startsWith("de_")) {
    setRow(0,"21:19","Pasing - Geltendorf - Kaufering","Lindau Hbf","ALX 84146","26");
    setRow(1,"21:24","Freising - Moosburg - Landshut Hbf","Passau Hbf","RE 4090","24");
    setRow(2,"21:29","Markt Schwaben - Dorfen Bf","Simbach (Inn)","RB 27077","14");
    setRow(3,"21:29","Dachau Bf - Petershausen","Nurnberg Hbf","RB 59108","21");
    setRow(4,"21:32","Pasing - Tutzing - Weilheim/Obb","Mittenwald","RB 59471","28");
    setRow(5,"21:44","Freising - Moosburg - Landshut Hbf","Regensburg Hbf","RE 4868","24");
    setRow(6,"21:44","Grafing Bf - Rosenheim - Traunstein","Salzburg Hbf","M 79045","11");
    setRow(7,"22:01","Pasing - Mering - Augsburg","Donauworth","RE 57050","16");
    setRow(8,"22:05","Pasing - Mering - Augsburg","Ulm Hbf","RE 57246","16");
    setRow(9,"22:05","Holzkirchen - Miesbach - Schliersee","Bayrischzell","BOB 86841","34");
    clearRowsFrom(10);
  } else if (profile == "ch_zurich_fern") {
    setRow(0,"08:52","33","Zurich Flughafen","IR 36","33");
    setRow(1,"08:53","Brugg","Zurich HB","IR 35","33");
    setRow(2,"08:57","Arth-Goldau","Bellinzona","EC 3","15");
    setRow(3,"08:59","Basel SBB","Geneve-Aeroport","IC 3","13");
    setRow(4,"09:02","Bern Thun Spiez Visp Brig","Brig","RE","31");
    setRow(5,"09:03","Zurich - Winterthur - Rorschach","St. Gallen","IC 5","23");
    setRow(6,"09:05","Olten Brugg Aarau","Bern","IR 16","16");
    setRow(7,"09:07","Sargans","Chur","IC 3","8");
    setRow(8,"09:08","Konstanz","Konstanz","IR 75","17");
    setRow(9,"09:10","Delsberg Delemont","Basel SBB","IR 36","32");
    setRow(10,"09:12","Thalwil Waedenswil","Landquart Chur","IR 35","10");
    clearRowsFrom(11);
  } else if (profile == "ch_bern_arrival") {
    setRow(0,"09.28","ca. 9 Min spaeter","St. Gallen Winterthur Flughafen Zurich HB","IC","5");
    setRow(1,"09.40","","Laupen Niederwangen Buempliz Sued","S2","2");
    setRow(2,"09.40","","Langnau Burgdorf Zollikofen Wankdorf","S4","6");
    setRow(3,"09.40","","Bruennen Westside Buempliz Nord","S51","12 C");
    setRow(4,"09.42","","Belp Kehrsatz Weissenbuehl Europaplatz","S31","12 A");
    setRow(5,"09.43","","Thun Guemligen Ostermundigen Wankdorf","S1","1 CD");
    setRow(6,"09.44","","Fribourg/Freiburg Buempliz Nord","S1","7");
    setRow(7,"09.44","","Muenchenbuchsee Zollikofen Wankdorf","S31","4");
    setRow(8,"09.47","","Biel/Bienne Lyss","RE","9");
    setRow(9,"09.48","","Jegensdorf Zollikofen Worblaufen","S8","23");
    setRow(10,"09.48","","Langnau Konolfingen Guemligen Wankdorf","S2","1 AB");
    setRow(11,"09.48","","Thun Seftigen Thurnen Toffen Belp","S44","5");
    setRow(12,"09.51","","Bulle/Palezieux Romont Fribourg/Freiburg","RE","12 C");
    setRow(13,"09.52","","Interlaken Ost Interlaken West Spiez Thun","IC","13 C");
    setRow(14,"09.52","","Neuchatel/Murten/Morat Buempliz Nord","S5","13");
    clearRowsFrom(15);
  } else if (profile == "ch_sbb_romandie" || profile.startsWith("ch_")) {
    setRow(0,"14.38","Retard env. 11 min.","Bourg-en-B. Paris-Gare de Lyon","TGV","8");
    setRow(1,"14.42","","Lausanne","IC 1","4");
    setRow(2,"14.44","","Vernier Meyrin Zimeysa La Plaine","R","5");
    setRow(3,"14.47","","Geneve-Aeroport","IC 5","2");
    setRow(4,"14.49","","Coppet Lausanne Fribourg/Freiburg","RE","6");
    setRow(5,"14.50","","Geneve-Aeroport","IR 90","3");
    setRow(6,"14.50","","Bellegarde Chambery Grenoble Valence","TER","7");
    setRow(7,"14.59","","Lancy-Pont-Rouge","R","2");
    setRow(8,"15.00","","Nyon Morges Lausanne","IR 15","6");
    setRow(9,"15.01","","Secheron Chambesy Coppet","R","1");
    setRow(10,"15.03","","Geneve-Aeroport","IR 15","3");
    setRow(11,"15.12","","Lausanne Vevey Montreux Sion Brig","IR 90","6");
    clearRowsFrom(12);
  } else if (profile == "uk_sheffield") {
    setRow(0,"13:44","Calling at: The Bus Station","Alfreton","EMR","Bus Service");
    setRow(1,"13:48","Calling at: Worksop and Retford","Retford","Northern","Platform 3A");
    setRow(2,"13:55","Calling at: East Midlands Parkway","Derby","EMR","Bus Service");
    setRow(3,"14:01","Darnall, Woodhouse, Kiveton Bridge","Retford","Northern","Platform 1B");
    setRow(4,"14:04","Chesterfield only. Replacement bus","Chesterfield","EMR","Bus Service");
    setRow(5,"14:08","Dronfield, Barnetby, Grimsby Town","Cleethorpes","TPE","Platform 5B");
    setRow(6,"14:12","Stockport, Manchester Piccadilly","Manchester Airport","TPE","Platform 6A");
    clearRowsFrom(7);
  } else if (profile.startsWith("uk_")) {
    setRow(0,"13:39","","Charing X","SE","9");
    setRow(1,"13:40","","Maidstone E.","SE","6");
    setRow(2,"14:32","","St Pancras","EMR","6");
    setRow(3,"14:34","","Matlock","EMR","4B");
    setRow(4,"11:30","","Glasgow","Avanti","6");
    setRow(5,"11:39","","Milton K.","LNWR","8");
    clearRowsFrom(6);
  } else if (profile == "at_oebb_white") {
    setRow(0,"15:22","west","Salzburg Hbf","IC 548","3");
    setRow(1,"15:30","RJ 165","Munchen/Salzburg","OBB","6");
    setRow(2,"15:44","R 2031","St.Polten Hbf","OBB","10");
    setRow(3,"16:00","REX 1629","St.Valentin","OBB","4");
    setRow(4,"16:18","S 80","Wien Hbf","OBB","2");
    clearRowsFrom(5);
  } else if (profile == "at_oebb_dense") {
    setRow(0,"15:09","Bruck/Mur", "Schwarzach-St.Veit", "S3", "2");
    setRow(1,"15:09","Baden - Moedling", "Wr.Neustadt Hbf", "RJ 2316", "9 A-C");
    setRow(2,"15:10","Bruck - Kapfenberg", "Praha hl.n.", "RJ", "5 A-B");
    setRow(3,"15:11","Wien Mitte - Rennweg", "Wr.Neustadt Hbf", "S60", "9");
    setRow(4,"15:11","Floridsdorf - Handelskai", "Moedling", "S2", "10 A-C");
    setRow(5,"15:12","Untertullnerbach", "Flughafen Wien", "RJX", "2");
    setRow(6,"15:12","Meidling - Huetteldorf", "Gaenserndorf", "S1", "12 C-E");
    setRow(7,"15:15","Tullnerbach-Pressbaum", "Vulkangrossdorf", "REX", "12 C-E");
    setRow(8,"15:15","St.Poelten - Melk", "Pamhagen", "S2", "2");
    setRow(9,"15:15","Wiener Neustadt", "Mistelbach", "S2", "2");
    clearRowsFrom(10);
  } else if (profile == "at_oebb_blue") {
    setRow(0,"11:10","via Semmering", "Wien Hbf", "EC 158", "BUS", "+5");
    setRow(1,"11:25","SLB|Railjet nach Wien", "Salzburg Hbf", "EC 216", "8", "");
    setRow(2,"11:43","", "Bruck a.d.Mur", "S1", "6");
    setRow(3,"12:06","GKB|Zug nach Fehring", "Koeflach", "S61", "2 C");
    setRow(4,"12:07","GKB", "Wies-Eibiswald", "S6", "BUS", "+12");
    setRow(5,"12:10","", "Szentgotthard", "S3", "4 D-E");
    setRow(6,"12:10","ueber Maribor", "Praha hl.n.", "RJ 78", "BUS");
    setRow(7,"12:36","", "Spielfeld-Strass", "S5", "BUS");
    setRow(8,"12:43","", "Linz Hbf", "IC 506", "BUS");
    clearRowsFrom(9);
  } else if (profile == "at_oebb_green") {
    setRow(0,"11:09","", "Zagreb", "EC 158", "BUS", "+4");
    setRow(1,"11:18","", "Bruck a.d.Mur", "S1", "5 D");
    setRow(2,"11:51","ueber Fehring", "Spielfeld-Strass", "S5", "3 D");
    setRow(3,"11:53","", "Szentgotthard", "S3", "BUS", "+8");
    setRow(4,"11:54","", "Brno hl.n", "RJ 77", "8");
    setRow(5,"12:00","GKB", "Koeflach", "S7", "BUS");
    setRow(6,"12:14","", "Salzburg Hbf", "IC 513", "5 D");
    setRow(7,"12:18","", "Bruck a.d.Mur", "S1", "1 F");
    setRow(8,"12:50","Bauarbeiten", "Fehring", "S5", "BUS");
    clearRowsFrom(9);
  } else if (profile == "at_oebb_teal" || profile.startsWith("at_")) {
    setRow(0,"08:59","09:03","Wr.Neustadt Hbf","R 2316","2");
    setRow(1,"09:02","","Graz Hbf","RJ 72","9 C-F");
    setRow(2,"09:05","","Salzburg Hbf","RJ 543","10 A-C");
    setRow(3,"09:06","","Wien Praterstern","west 956","1");
    setRow(4,"09:07","","Deutschkreutz","REX 7615","12 B");
    setRow(5,"09:14","","Bratislava-Petrzalka","REX 7608","7 A-B");
    setRow(6,"09:18","","Budapest Keleti","RJX 162","8 A-E");
    clearRowsFrom(7);
  } else if (profile == "nl_ns_light") {
    setRow(0,"10:03","Leiden C, Schiphol Airport, Almere C.", "Leeuwarden", "Intercity", "10");
    setRow(1,"10:05","Den Haag HS, Moerwijk, Delft, Rotterdam C.", "Dordrecht", "Sprinter", "3");
    setRow(2,"10:09","Leiden C, Schiphol Airport x, Sloterdijk", "Hoorn Kersenboogerd", "Sprinter", "8");
    setRow(3,"10:13","10:25 IC Amersfoort C. spoor 5 verder in Utrecht C.", "Tiel", "Sprinter", "6");
    setRow(4,"10:18","Den Haag HS, Delft, Rotterdam C., Breda", "Eindhoven Centraal", "Intercity", "2");
    setRow(5,"10:22","Laan v NOI, Mariahoeve, Leiden C.", "Haarlem", "Sprinter", "7");
    setRow(6,"10:24","Den Haag HS, Moerwijk, Delft, Rotterdam C.", "Dordrecht", "Sprinter", "1");
    clearRowsFrom(7);
    transilienInfoTitle = "Van/naar Apeldoorn";
    transilienInfoText = "Werkzaamheden: Gebruik het vervangend vervoer. U kunt ook omreizen via Utrecht Centraal.";
  } else if (profile.startsWith("nl_")) {
    setRow(0,"10:27","","Alkmaar","IC","10");
    setRow(1,"10:31","","Amersfoort","IC","8a");
    setRow(2,"10:34 /5","","Uitgeest","SPR","13a");
    setRow(3,"10:35","","Frankfurt","ICE","2b");
    setRow(4,"10:37","","Lelystad","IC","10a");
    setRow(5,"10:38","","Maastricht","IC","5b");
    clearRowsFrom(6);
  } else if (profile == "es_barcelona_grid") {
    setRow(0,"18:36","", "Montcada-Ripollet", "R8", "8");
    setRow(1,"18:26","", "Montgat", "R1", "8");
    setRow(2,"18:30","", "Montgat Nord", "R1", "8");
    setRow(3,"18:30","", "Montmelo", "R2", "14");
    setRow(4,"20:33","", "Mora la Nova", "R15", "15");
    setRow(5,"18:26","", "Ocata", "R1", "8");
    setRow(6,"18:30","", "Palautordera", "R2", "14");
    setRow(7,"18:36","", "Parets del Valles", "R3", "8");
    setRow(8,"18:33","", "Riba-roja d'Ebre", "R15", "12");
    setRow(9,"18:57","", "Ribes de Freser", "R3", "8");
    setRow(10,"19:16","", "Ripoll", "R3", "8");
    setRow(11,"20:33","", "Riudecanyes-Botarell", "R15", "12");
    clearRowsFrom(12);
  } else if (profile == "es_barcelona_adif") {
    setRow(0,"11:07","Rodalies", "L'Hospitalet Llobregat", "R1", "Vies 7-8");
    setRow(1,"11:00","Rodalies", "Sant Vicenc de Calders", "R4", "Vilafranca");
    setRow(2,"11:10","Rodalies", "L'Hospitalet Llobregat", "R1", "Vies 7-8");
    setRow(3,"11:07","Rodalies", "L'Hospitalet Llobregat", "R4", "Vies 7-8");
    setRow(4,"11:19","Rodalies", "L'Hospitalet Llobregat", "R1", "Vies 7-8");
    setRow(5,"11:22","Rodalies", "Vilafranca del Penedes", "R4", "Vies 7-8");
    setRow(6,"11:39","Rodalies", "L'Hospitalet Llobregat", "R3", "Vies 7-8");
    setRow(7,"11:36","Rodalies", "L'Hospitalet Llobregat", "R1", "Vies 7-8");
    clearRowsFrom(8);
  } else if (profile == "pl_pkp_departures") {
    setRow(0,"10:11","PR", "Zielona Gora Glowna", "R 79603", "7");
    setRow(1,"10:13","KW", "Leszno", "Os 77520", "2");
    setRow(2,"10:16","KW", "Lodz Kaliska", "OsP 71120", "1");
    setRow(3,"10:21","KW", "Mogilno", "Os 77528", "1");
    setRow(4,"10:30","IC", "Berlin Hauptbahnhof", "57001/230", "5");
    setRow(5,"10:31","IC", "Szczecin Glowny", "IC 1805", "3");
    setRow(6,"10:35","IC", "Krakow Glowny", "IC 8322", "9");
    setRow(7,"10:38","IC", "Przemysl Glowny", "IC 8312", "10");
    clearRowsFrom(8);
  } else if (profile == "pl_pkp_arrivals") {
    setRow(0,"10:07","KW", "Leszno", "Os 77505", "1");
    setRow(1,"10:09","KW", "Krzyz", "Os 87810", "11");
    setRow(2,"10:15","IC", "Warszawa Wschodnia", "IC 1703", "3");
    setRow(3,"10:19","IC", "Ustka", "IC 8312", "10");
    setRow(4,"10:19","IC", "Warszawa Wschodnia", "EC 1805", "3");
    setRow(5,"10:20","KW", "Gdynia Glowna", "IC 5700", "5");
    setRow(6,"10:24","IC", "Szczecin Glowny", "IC 8322", "9");
    setRow(7,"10:25","PR", "Katowice", "TLK 47101", "3");
    clearRowsFrom(8);
  } else if (profile == "us_la_metro") {
    setRow(0,"04:32PM","91/PV Line", "South Perris", "Metrolink 725", "110");
    setRow(1,"04:38PM","RVS Line", "Downtown Riverside", "Metrolink 412", "801");
    setRow(2,"04:59PM","AV Line", "Lancaster", "Metrolink 221", "38");
    setRow(3,"05:10PM","Pacific Surfliner", "San Diego", "Amtrak 774", "12");
    clearRowsFrom(4);
  } else if (profile == "in_indian_railways") {
    setRow(0,"13:10","EXP", "New Delhi", "12951", "2");
    setRow(1,"13:25","SF", "Mumbai CSMT", "12009", "5");
    setRow(2,"13:40","MAIL", "Howrah", "12302", "8");
    setRow(3,"13:55","EXP", "Chennai Central", "12616", "4");
    setRow(4,"14:05","MEMU", "Ghaziabad", "64411", "1");
    setRow(5,"14:20","SF", "Ahmedabad", "12916", "6");
    setRow(6,"14:35","EXP", "Jaipur", "12985", "7");
    setRow(7,"14:50","LOCAL", "Agra Cantt", "64954", "3");
    clearRowsFrom(8);
  } else if (profile == "nz_britomart") {
    setRow(0,"7","via Panmure", "Manukau", "EAST", "1");
    setRow(1,"13","via Newmarket", "Swanson", "WEST", "2");
    setRow(2,"12:52","via Newmarket", "Papakura", "STH", "4");
    setRow(3,"12:58","via Panmure", "Manukau", "EAST", "1");
    setRow(4,"13:04","via Newmarket", "Swanson", "WEST", "3");
    setRow(5,"13:12","via Newmarket", "Papakura", "STH", "4");
    setRow(6,"13:18","via Panmure", "Manukau", "EAST", "1");
    clearRowsFrom(7);
  } else if (profile == "it_naples_amber") {
    setRow(0,"13:05","FA", "Roma Termini", "9628", "8");
    setRow(1,"13:12","IC", "Salerno", "552", "5");
    setRow(2,"13:20","R", "Caserta", "21430", "3");
    setRow(3,"13:25","AV", "Milano Centrale", "9514", "10");
    setRow(4,"13:38","R", "Sapri", "21072", "2");
    setRow(5,"13:44","IC", "Reggio Calabria", "727", "7");
    setRow(6,"13:50","R", "Pozzuoli", "21110", "1");
    setRow(7,"13:56","FA", "Venezia S. Lucia", "9422", "9");
    clearRowsFrom(8);
  } else if (profile.startsWith("it_")) {
    setRow(0,"15:05","","Roma Termini","FR","8");
    setRow(1,"15:15","","Milano C.","FR","11");
    setRow(2,"15:25","","Napoli C.","IC","6");
    setRow(3,"15:35","","Firenze SMN","RV","4");
    setRow(4,"15:45","","Venezia SL","FR","9");
    setRow(5,"15:55","","Torino PN","IC","3");
    clearRowsFrom(6);
  } else if (profile == "se_stockholm") {
    setRow(0,"12:13","","Uppsala C","SJ","5");
    setRow(1,"12:19","","Vasteras Central","SJ","10");
    setRow(2,"12:24","","Linkoping C","SJ","12");
    setRow(3,"12:31","","Goteborg C","SJ","18");
    setRow(4,"12:41","","Hallsberg","SJ","8");
    setRow(5,"12:50","","Norrkoping C","SJ","11");
    setRow(6,"13:01","","Malmo C","SJ","17");
    setRow(7,"13:05","","Umea Central","SJ","6");
    clearRowsFrom(8);
  } else if (profile == "jp_jr_led") {
    setRow(0,"10:30","Car 1-3","Hakata","NOZOMI 25","18");
    setRow(1,"10:33","Car 1-5","Shin-Osaka","HIKARI 509","15");
    setRow(2,"10:40 /5","Car 1-3","Shin-Osaka","NOZOMI 323","16");
    setRow(3,"10:50","Car 1-3","Hiroshima","NOZOMI 107","18");
    setRow(4,"10:56","Car 1-5","Shin-Osaka","KODAMA 649","14");
    clearRowsFrom(5);
  } else if (profile == "jp_tokyo_grey") {
    setRow(0,"12:36","10 cars","Morioka","Yamabiko 51","via Ueno, Sendai, Furukawa");
    setRow(1,"13:24","12 cars","Kanazawa","Hakutaka 565","via Nagano, Toyama");
    setRow(2,"13:40","10 cars","Niigata","Toki 323","for Karuizawa and Nagano");
    clearRowsFrom(3);
  } else if (profile == "hu_mav_departures") {
    setRow(0,"12:21","Pilisszentivan","Esztergom","Z72","1");
    setRow(1,"12:23","Kelenfold - Kecskemet","Budapest-Keleti","IC","13");
    setRow(2,"12:28","Cegled - Szolnok","Nyiregyhaza","IC","11");
    setRow(3,"12:35","Szekesfehervar","Veszprem","G","9");
    setRow(4,"12:45","Dunakeszi - God","Vac","S70","2");
    setRow(5,"12:51","Pilisszentivan","Esztergom","Z72","14");
    setRow(6,"12:53","Kecskemet - Kiskunfelegyhaza","Szeged","IC","12");
    setRow(7,"13:00","Veresegyhaz","Vac","S71","4");
    setRow(8,"13:03","Ferihegy - Monor","Cegled","Z50","17");
    setRow(9,"13:08","Vac","Szob","Z70","3");
    setRow(10,"13:11","Gyomro - Dabas","Lajosmizse","S21","6");
    setRow(11,"13:15","Dunakeszi - God","Vac","S70","2");
    clearRowsFrom(12);
  } else if (profile == "hu_mav_arrivals") {
    setRow(0,"12:20","Brno hl.n.","Praha hl.n. [Prag]","EC","1");
    setRow(1,"12:22","Gyor - Kelenfold","Monor","S50","8");
    setRow(2,"12:37","Szolnok","Nyiregyhaza","IC","11");
    setRow(3,"12:39","Pilisvorosvar","Esztergom","Z72","2");
    setRow(4,"12:44","God - Dunakeszi","Vac","S70","4");
    setRow(5,"12:47","Dabas - Gyal","Lajosmizse","S21","6");
    setRow(6,"12:54","Vac","Szob","Z70","3");
    setRow(7,"12:57","Monor - Ferihegy","Cegled","Z50","17");
    setRow(8,"12:59","Veresegyhaz","Vac","S71","5");
    setRow(9,"13:07","Kiskunfelegyhaza","Szeged","IC","12");
    setRow(10,"13:09","Piliscsaba","Esztergom","Z72","1");
    setRow(11,"13:14","God - Dunakeszi","Vac","S70","2");
    clearRowsFrom(12);
  } else if (profile == "hu_mav_arrivals" || profile == "hu_mav_departures" || profile.startsWith("hu_")) {
    setRow(0,"16:28","","Praha hl.n. Bratislava hl.st.","EC","5");
    setRow(1,"16:30","","Szolnok","Z50","7");
    setRow(2,"16:35","","Vac","S70","3");
    setRow(3,"16:40","","Debrecen","IC","8");
    setRow(4,"16:45","","Szeged","IC","10");
    setRow(5,"16:50","","Veszprem","S","12");
    setRow(6,"16:55","","Miskolc","IC","6");
    setRow(7,"17:00","","Gyor","IC","4");
    setRow(8,"17:05","","Pecs","IC","11");
    setRow(9,"17:10","","Kobanya-Kispest","S50","2");
    clearRowsFrom(10);
  } else {
    setRow(0,"10:00","","Destination","IC","1");
    setRow(1,"10:10","","Ville 2","RE","2");
    setRow(2,"10:20","","Ville 3","S","3");
    clearRowsFrom(3);
  }
}
void handleSetProfile() {
  if (server.hasArg("profile")) displayProfile = server.arg("profile");
  String backCountry = server.hasArg("back") ? server.arg("back") : profileCountry(displayProfile);
  applyProfileToScreenMode();
  loadProfileDefaults(displayProfile);
  nbVisible = profileDefaultVisibleRows(displayProfile);
  normalizeSettings();
  saveConfig();
  loadStyleTune(displayProfile);
  fullRedrawNeeded = true;
  // Rester dans le pays pour pouvoir tester/changer plusieurs styles sans revenir en arriere.
  if (backCountry.length()) {
    server.sendHeader("Location", "/country?c=" + backCountry);
  } else {
    server.sendHeader("Location", "/countries");
  }
  server.send(303);
}

void handleSaveWifi() {
  if (server.hasArg("ssid")) localWifiSsid = server.arg("ssid");
  if (server.hasArg("pass")) localWifiPass = server.arg("pass");
  saveConfig();
  if (localWifiSsid.length()) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(localWifiSsid.c_str(), localWifiPass.c_str());
  }
  server.sendHeader("Location", "/updates");
  server.send(303);
}
void handlePing() {
  server.send(200, "text/plain", "OK PM3D Display");
}

void handleSave() {
  if (server.hasArg("nb")) nbVisible = server.arg("nb").toInt();
  nbVisible = constrain(nbVisible, 3, profileMaxVisibleRows(displayProfile));
  if (server.hasArg("scrollMs")) scrollDelayMs = server.arg("scrollMs").toInt();
  if (server.hasArg("bright")) screenBrightness = map(constrain(server.arg("bright").toInt(), 1, 10), 1, 10, 25, 255);
  if (server.hasArg("font")) displayFontSize = server.arg("font").toInt();
  if (server.hasArg("theme")) uiTheme = server.arg("theme");
  if (server.hasArg("mode")) screenMode = server.arg("mode").toInt();
  if (server.hasArg("trFrame")) transilienFrame = server.arg("trFrame");
  if (server.hasArg("trAff")) transilienAffluence = server.arg("trAff");
  if (server.hasArg("trDir")) transilienDirection = server.arg("trDir");
  if (server.hasArg("trStops")) transilienStops = server.arg("trStops");
  if (server.hasArg("trInfoTitle")) transilienInfoTitle = server.arg("trInfoTitle");
  if (server.hasArg("trInfoText")) transilienInfoText = server.arg("trInfoText");
  if (server.hasArg("sncbGrid")) sncbGridCount = server.arg("sncbGrid").toInt();
  if (server.hasArg("dbicMsg")) dbIntercityMessage = server.arg("dbicMsg");
  if (server.hasArg("db10Title")) db2010Title = server.arg("db10Title");
  if (server.hasArg("db10Font")) db2010TitleFontSize = server.arg("db10Font").toInt();
  if (server.hasArg("dbicTime")) {
    String dbTime = server.arg("dbicTime");
    if (dbTime.length() >= 5) {
      dbIntercityClockHour = dbTime.substring(0, 2).toInt();
      dbIntercityClockMinute = dbTime.substring(3, 5).toInt();
    }
  }
  if (server.hasArg("badenSt")) badenSteig = server.arg("badenSt");
  if (sncbGridCount < 6) sncbGridCount = 6;
  if (sncbGridCount > 18) sncbGridCount = 18;
  sncbGridCount = ((sncbGridCount + 2) / 3) * 3;
  normalizeSettings();
  bool copiedTable = server.hasArg("doCopy") && server.hasArg("copySource") && !profileBlocksCopy(displayProfile);
  if (copiedTable) {
    loadProfileDefaults(server.arg("copySource"));
    userRowsEdited = true;
  } else {
    userRowsEdited = true;
    for (int i = 0; i < MAX_ROWS; i++) {
      if (server.hasArg("h" + String(i))) rows[i].heure = server.arg("h" + String(i));
      if (server.hasArg("r" + String(i))) rows[i].retard = server.arg("r" + String(i));
      if (server.hasArg("i" + String(i))) rows[i].info = server.arg("i" + String(i));
      if (server.hasArg("d" + String(i))) rows[i].destination = server.arg("d" + String(i));
      if (server.hasArg("t" + String(i))) rows[i].typeTrain = server.arg("t" + String(i));
      if (server.hasArg("v" + String(i))) rows[i].voie = server.arg("v" + String(i));
    }
  }

  saveConfig();

  if (screenMode == 2) {
    resetRetroDisplay();
  }

  analogWrite(TFT_BL, screenBrightness);
  fullRedrawNeeded = true;
  server.sendHeader("Location", "/settings");
  server.send(303);
}

void pm3dSendHtml(int code, const String &html) {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Connection", "close");
  server.send(code, "text/html; charset=utf-8", html);
}


// Rendu SNCB TFT gris/bleu 2010s - conforme photo : colonnes verticales, lignes courtes, alertes rouges.
void drawSncb2023Photo() {
  int w = gfx->width();
  int h = gfx->height();
  uint16_t headerBlue = 0x04BF;
  uint16_t rowBlue = 0x0278;
  uint16_t blueDark = 0x0194;
  uint16_t grid = C_WHITE;
  uint16_t delayRed = 0xF986;
  uint16_t delayPale = 0xEF9F;

  gfx->fillScreen(rowBlue);
  gfx->fillRect(0, 0, w, 17, headerBlue);
  gfx->fillRect(0, 17, w, h - 17, rowBlue);
  smallText(7, 6, C_WHITE, rows[0].heure.length() ? rows[0].heure : "08:57");
  smallText((w / 2) - 18, 6, C_WHITE, "Depart");
  gfx->drawCircle(w - 14, 8, 6, C_WHITE);
  smallText(w - 17, 5, C_WHITE, "B");

  int top = 18;
  int maxRows = min(11, MAX_ROWS);
  int rh = (h - top - 2) / maxRows;
  if (rh < 10) rh = 10;
  for (int i = 0; i < maxRows; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = top + i * rh;
    gfx->fillRect(0, y, w, rh, rowBlue);
    gfx->drawFastHLine(0, y + rh - 1, w, grid);

    String info = rows[idx].info;
    String dest = rows[idx].destination;
    String train = rows[idx].typeTrain;
    String voie = rows[idx].voie;
    bool cancelled = info == "Supprime" || info == "SUPPRIME" || info == "SupprimÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â©";

    colSmallText(0, 5, y + 1, C_WHITE, rows[idx].heure);
    int destX = 60;
    if (cancelled) {
      gfx->fillRect(39, y + 1, 48, rh - 2, delayRed);
      smallText(43, y + 2, C_WHITE, "Supprime");
      destX = 92;
    } else if (info.length()) {
      gfx->fillRect(38, y + 1, 25, rh - 2, delayRed);
      smallText(43, y + 2, C_WHITE, cutText(info, 4));
      gfx->fillRect(66, y + 1, 31, rh - 2, delayPale);
      smallText(69, y + 2, blueDark, "09:05");
      destX = 101;
    }

    colSmallText(2, destX, y + 1, C_SNCB_YELLOW, cutText(dest, destX > 90 ? 20 : 24));
    colSmallText(3, w - 43, y + 1, C_WHITE, cutText(train, 3));
    if (voie == "--" || voie == "") colSmallText(4, w - 15, y + 1, C_WHITE, "--");
    else {
      if (voie.toInt() == 9) gfx->fillRect(w - 23, y + 1, 17, rh - 2, delayPale);
      colSmallText(4, w - 18, y + 1, voie.toInt() == 9 ? blueDark : C_WHITE, cutText(voie, 2));
    }
  }
}
void drawSncbTft2010PhotoFix() {
  int w = gfx->width();
  int h = gfx->height();
  uint16_t bg = C_BLACK;
  uint16_t blue = 0x0278;
  uint16_t blue2 = 0x039F;
  uint16_t red = 0xE986;
  uint16_t pale = 0xBFFF;
  uint16_t white = 0xFFFF;
  uint16_t line = 0x5D9F;

  gfx->fillScreen(bg);
  gfx->setTextSize(1);
  gfx->setTextColor(white, bg);
  smallText(5, 6, white, "18 05");
  smallText(67, 6, white, "Depart");

  int count = sncbGridCount;
  if (count < 6) count = 6;
  if (count > 18) count = 18;
  count = ((count + 2) / 3) * 3;
  int perCol = max(2, count / 3);
  int gap = 2;
  int colW = (w - gap * 2) / 3;
  int top = 19;
  int rowH = (h - top - 2) / perCol;
  if (rowH < 13) rowH = 13;

  struct MiniRow { const char* t; const char* info; const char* dest; const char* tr; const char* voie; };
  const MiniRow items[] = {
    {"11:40", "en approche", "Bruxelles Alost", "S", "4"},
    {"11:47", "en approche", "Bruxelles Termonde", "S", "2"},
    {"11:47", "", "Denderleeuw Alost", "S", "1"},
    {"11:58", "", "Termonde", "S", "3"},
    {"12:02", "", "Bruxelles Zottegem", "S", "4"},
    {"12:13", "", "Bruxelles-Schuman Malines", "S", "1"},
    {"12:16", "", "Liedekerke Alost", "S", "1"},
    {"12:21", "", "Termonde", "S", "3"},
    {"12:24", "", "Bruxelles Courtrai", "IC", "4"},
    {"12:37", "", "Termonde Saint-Nicolas", "IC", "3"},
    {"12:40", "", "Bruxelles Alost", "S", "4"},
    {"12:47", "", "Bruxelles Termonde", "S", "2"},
    {"12:47", "", "Denderleeuw Alost", "S", "1"},
    {"12:58", "", "Termonde", "S", "3"},
    {"13:02", "", "Bruxelles Zottegem", "S", "4"},
    {"13:13", "", "Bruxelles-Schuman Malines", "S", "2"},
    {"13:16", "", "Liedekerke Alost", "S", "1"},
    {"13:21", "", "Termonde", "S", "3"}
  };

  auto drawMini = [&](int x, int y, const MiniRow &r, bool alt) {
    uint16_t cell = alt ? blue2 : blue;
    gfx->fillRect(x, y, colW, rowH - 1, cell);
    gfx->drawFastHLine(x, y + rowH - 1, colW, line);
    gfx->drawFastVLine(x + colW - 1, y, rowH - 1, line);
    smallText(x + 3, y + 2, white, r.t);
    String info = String(r.info);
    if (info.length()) {
      gfx->fillRect(x + 25, y + 2, 42, 7, pale);
      smallText(x + 27, y + 3, blue, cutText(info, 10));
    }
    smallText(x + 3, y + rowH - 7, white, cutText(String(r.dest), 13));
    smallText(x + colW - 22, y + 3, white, r.tr);
    smallText(x + colW - 9, y + 3, white, r.voie);
  };

  for (int c = 0; c < 3; c++) {
    int x = c * (colW + gap);
    for (int r = 0; r < perCol; r++) {
      int i = c * perCol + r;
      if (i >= count) continue;
      drawMini(x, top + r * rowH, items[i], r % 2);
    }
  }
  gfx->fillRect(0, h - 3, w, 3, bg);
}

void drawSncbRailTime() {
  int pm3dRows = 12;
  int pm3dFont = 1;

  // SNCB / RailTime ancien jaune : pas de lignes tracÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â©es, uniquement alternance de bandes.
  gfx->fillScreen(0x0000);

  int w = gfx->width();
  int h = gfx->height();

  const uint16_t blue   = 0x203F;
  const uint16_t bandA  = 0x2104;
  const uint16_t bandB  = 0x3186;
  const uint16_t yellow = 0xFFE0;
  const uint16_t white  = 0xFFFF;
  const uint16_t black  = 0x0000;

  int headerH = max(18, h / 9);
  int footerH = max(12, h / 14);
  int usableH = h - headerH - footerH;
  int rows = max(1, min(pm3dRows, 8));
  int rowH = usableH / rows;

  gfx->fillRect(0, 0, w, headerH, blue);
  gfx->setTextSize(1);
  gfx->setTextColor(white, blue);
  gfx->setCursor(8, 5);
  gfx->print("07:10");
  gfx->setCursor(w / 3, 5);
  gfx->print("VERTREK-DEPART");

  const char* rowsData[][4] = {
    {"7:12", "NAMUR - DINANT", "IC", "***"},
    {"",     "NAMEN - DINANT", "",   ""},
    {"7:12", "TOURNAI - MOUSCRON", "IC", "10"},
    {"",     "DOORNIK - MOESKROEN", "", ""},
    {"7:13", "BRUXELLES-MIDI", "L", "***"},
    {"",     "BRUSSEL-ZUID", "", ""},
    {"7:13", "JETTE - AALST", "L", "1"},
    {"7:17", "BRAINE-LE-COMTE", "L", "10"}
  };

  gfx->setTextSize(max(1, pm3dFont));
  for (int i = 0; i < rows; i++) {
    int y = headerH + i * rowH;
    uint16_t bg = (i % 2 == 0) ? bandA : bandB;

    // IMPORTANT : pas de drawFastHLine ici. L'effet vient seulement des bandes alternÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â©es.
    gfx->fillRect(0, y, w, rowH, bg);

    gfx->setTextColor(yellow, bg);
    int ty = y + max(1, (rowH - 8 * max(1, pm3dFont)) / 2);
    gfx->setCursor(4, ty);
    gfx->print(rowsData[i][0]);

    gfx->setCursor(40, ty);
    gfx->print(rowsData[i][1]);

    gfx->setCursor(w - 60, ty);
    gfx->print(rowsData[i][2]);

    gfx->setCursor(w - 28, ty);
    gfx->print(rowsData[i][3]);
  }

  gfx->fillRect(0, h - footerH, w, footerH, black);
  gfx->setTextSize(1);
  gfx->setTextColor(0x07E0, black);
  gfx->setCursor((w / 2) - 22, h - footerH + 2);
  gfx->print("RailTime");
}


void drawBootInfo(const IPAddress &ip) {
  gfx->fillScreen(C_BLACK);
  mediumText(64, 35, C_CYAN, "PM3D.NET");
  smallText(72, 62, C_WHITE, "Ecran de quai");
  smallText(58, 84, C_WHITE, String("IP : ") + ip.toString());

  unsigned long bootStart = millis();
  while (millis() - bootStart < 5000UL) {
    server.handleClient();
    delay(10);
    yield();
  }
}
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  loadDefaults();
  loadConfig();
  normalizeTftOffsets();
  gfx = new Arduino_ST7789(bus, TFT_RST, 1, true, tftPanelW, tftPanelH, tftOffsetX1, tftOffsetY1, tftOffsetX2, tftOffsetY2);
  analogWrite(TFT_BL, screenBrightness);
  gfx->begin();

  apSSID = "PM3D-Display-" + macSuffix();

  WiFi.mode(WIFI_AP_STA);
  IPAddress apIP = stableApIPFromMac();
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  bool apOk = apPass.length() >= 8 ? WiFi.softAP(apSSID.c_str(), apPass.c_str()) : WiFi.softAP(apSSID.c_str());
  Serial.print("AP SSID: "); Serial.println(apSSID);
  Serial.print("AP IP stable: "); Serial.println(WiFi.softAPIP());

    server.on("/", handleIntro);
  server.on("/intro", handleIntro);
  server.on("/setlang", handleSetLang);
  server.on("/main", handleMain);
  server.on("/config", handleConfigMenu);
  server.on("/settings", handleSettingsPage);
  server.on("/countries", handleCountries);
  server.on("/country", handleCountry);
  server.on("/moveprofile", handleMoveProfile);
  server.on("/favoriteprofile", handleFavoriteProfile);
  server.on("/setprofile", HTTP_POST, handleSetProfile);
  server.on("/themes", handleThemes);
  server.on("/settheme", handleSetTheme);
  server.on("/setbright", handleSetBright);
  server.on("/advanced", handleAdvanced);
  server.on("/saveadvanced", HTTP_POST, handleSaveAdvanced);
  server.on("/stylecfg", handleStyleConfig);
  server.on("/savestylecfg", HTTP_POST, handleSaveStyleConfig);
  server.on("/wifiscan", handleWifiScan);
  server.on("/savewifi", HTTP_POST, handleSaveWifi);
  server.on("/updates", handleUpdates);
  server.on("/ota", HTTP_POST, handleOtaDone, handleOtaUpload);
  server.on("/api/register", HTTP_GET, handleApiRegister);
  server.on("/api/update", HTTP_GET, handleApiUpdate);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  drawBootInfo(WiFi.softAPIP());

  Serial.println();
  Serial.println("PM3D Display");
  Serial.print("WiFi AP : ");
  Serial.println(apOk ? "OK" : "ERREUR");
  Serial.print("SSID : ");
  Serial.println(apSSID);
  Serial.println(apPass.length() >= 8 ? "Mot de passe : defini" : "Mot de passe : aucun");
  Serial.print("IP : ");
  Serial.println(WiFi.softAPIP());
  Serial.println("Test navigateur : http://192.168.4.1/ping");

  drawScreenFull();
}

void loop() {
  server.handleClient();

  unsigned long now = millis();

  if (fullRedrawNeeded) {
    fullRedrawNeeded = false;
    drawScreenFull();
  }

  if (screenMode == 2 && retroAnimating && now - lastRetro >= RETRO_DELAY) {
    lastRetro = now;
    retroAnimateOneStep();
  }

  if (displayProfile == "de_db_intercity" && now - lastDbClock >= 75UL) {
    lastDbClock = now;
    drawDbIntercityMarquee(0x2817, 0x8CB2);
  }

  if (displayProfile == "de_db_large_blue" && now - lastDbClock >= 1000UL) {
    lastDbClock = now;
    drawDbClockBox();
    drawDbUberCells();
  }

  if (displayProfile == "de_baden_baden" && now - lastDbClock >= 1000UL) {
    lastDbClock = now;
    drawBadenBadenHeader();
  }
  int scrollableRows = filledRowCount();
  if (scrollableRows > nbVisible && !retroAnimating && now - lastScroll >= (unsigned long)scrollDelayMs) {
    lastScroll = now;
    scrollOffset = (scrollOffset + 1) % scrollableRows;

    if (screenMode == 2) {
      resetRetroDisplay();
    } else {
      drawRowsOnly();
    }
  }
}










void drawSncbBlueRecent() {
  int pm3dRows = 12;

  // SNCB bleu rÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â©cent fidÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¨le photo
  gfx->fillScreen(0x01D7);

  int w = gfx->width();
  int h = gfx->height();

  uint16_t blueBg = 0x01D7;
  uint16_t blueDark = 0x00F3;
  uint16_t white = 0xFFFF;
  uint16_t yellow = 0xFFE0;
  uint16_t red = 0xE145;
  uint16_t gray = 0xFFFF;

  int headerH = 18;
  int rowH = max(12, min(18, (h-headerH)/max(6, pm3dRows)));

  // Header
  gfx->fillRect(0,0,w,headerH,blueDark);
  gfx->setTextColor(white, blueDark);
  gfx->setTextSize(1);

  gfx->setCursor(4,5);
  gfx->print("08:37");

  gfx->setCursor((w/2)-18,5);
  gfx->print("Depart");

  gfx->drawCircle(w-10,8,6,white);
  gfx->setCursor(w-12,4);
  gfx->print("B");

  const char* rows[][6] = {
    {"08:12","+27","08:39","Louvain","IC","9"},
    {"08:14","Supprime","","Louvain-la-Neuve","S8","--"},
    {"08:31","+13","08:44","Louvain-la-Neuve","S81","5"},
    {"08:41","","","Wavre via Bgs-Walibi","S61","10"},
    {"08:43","+8","08:51","Bruxelles-Midi","IC","2"},
    {"08:47","","","Bruxelles-Midi","S8","1"},
    {"08:49","","","Liege-Saint-Lambert","IC","3"},
    {"08:52","Supprime","","Louvain-la-Neuve","S8","--"},
    {"08:53","+13","09:05","Charleroi-Central","IC","11"},
    {"08:58","+7","09:05","Brussels Airport","IC","2"},
    {"09:02","","","Dinant","IC","5"},
    {"09:05","","","Namur via Gembloux","L","3"}
  };

  int maxRows = min((int)(sizeof(rows)/sizeof(rows[0])), max(1, pm3dRows));

  for(int i=0;i<maxRows;i++) {
    int y = headerH + i*rowH;

    gfx->fillRect(0,y,w,rowH-1,blueBg);
    gfx->drawFastHLine(0,y+rowH-1,w,white);

    // heure gauche
    gfx->setTextColor(white, blueBg);
    gfx->setCursor(2,y+3);
    gfx->print(rows[i][0]);

    // retard/supprimÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â©
    String delay = rows[i][1];
    if(delay.length()>0){
      uint16_t boxColor = red;
      int bw = (delay=="Supprime") ? 52 : 28;
      gfx->fillRect(34,y+1,bw,rowH-3,boxColor);
      gfx->setTextColor(white, boxColor);
      gfx->setCursor(36,y+3);
      gfx->print(delay);
    }

    // heure corrigÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â©e
    gfx->setTextColor(white, blueBg);
    if(String(rows[i][2]).length()>0){
      gfx->setCursor(68,y+3);
      gfx->print(rows[i][2]);
    }

    // destination jaune
    gfx->setTextColor(yellow, blueBg);
    gfx->setCursor(102,y+3);
    String dest = rows[i][3];
    if(dest.length()>18) dest = dest.substring(0,18);
    gfx->print(dest);

    // type train
    gfx->setTextColor(white, blueBg);
    gfx->setCursor(w-34,y+3);
    gfx->print(rows[i][4]);

    // voie
    gfx->setCursor(w-12,y+3);
    gfx->print(rows[i][5]);
  }
}





void showAdvancedSettingsPopup() {
  drawAdvancedSettingsPopup();
}

void drawAdvancedSettingsPopup() {
}
