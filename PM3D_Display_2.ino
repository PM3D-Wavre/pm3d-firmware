#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>


#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <Update.h>
#include <pgmspace.h>
#include "G:/PM3D/arduino/Passerelle ESP-32S3/_tmp_display2_arduino_libs/GFX_Library_for_Arduino/examples/HelloWorldGfxfont/FreeSansBold10pt7b.h"
extern "C" {
#include "G:/PM3D/arduino/libraries/QRCode/src/qrcode.c"
}

void drawRetroRowsOnly(bool animated);
void drawSncbTft2010PhotoFix();
void drawSncbRailTime();
void drawSncb2023Photo();
void drawSncb2023PhotoRef();
void drawSncf2012Photo();
void drawSncfValenceSideRows();
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
void handleQrTest();
void handleQrStop();
void handleDemoStart();
void handleDemoStop();
String bootWifiQrPayload();
void makeBootQrMatrix(const String &payload, bool modules[29][29]);
void updateDemoMode(unsigned long now);
void drawDemoCountryFlag(const String &country);

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

// QR Wi-Fi valide repris du sketch STIB qui scanne correctement.
// Contenu encode: WIFI:T:nopass;S:PM3D Display;;
#define STARTUP_QR_SIZE 29
#define STARTUP_QR_BPR 4
const uint8_t STARTUP_QR[STARTUP_QR_SIZE][STARTUP_QR_BPR] PROGMEM = {
  {0xFE, 0x37, 0x7B, 0xF8},
  {0x82, 0x07, 0xA2, 0x08},
  {0xBA, 0xB5, 0x0A, 0xE8},
  {0xBA, 0xBB, 0x42, 0xE8},
  {0xBA, 0xC1, 0x8A, 0xE8},
  {0x82, 0xF8, 0x62, 0x08},
  {0xFE, 0xAA, 0xAB, 0xF8},
  {0x00, 0xDA, 0x80, 0x00},
  {0xBE, 0x40, 0xC3, 0xE0},
  {0x9C, 0x77, 0x13, 0xB0},
  {0xFE, 0x0F, 0xA5, 0x40},
  {0x90, 0xDD, 0x2A, 0x98},
  {0x17, 0xA3, 0x45, 0x60},
  {0xD0, 0x41, 0xEB, 0xB0},
  {0xF7, 0x70, 0x0F, 0xA0},
  {0x1C, 0x52, 0xB2, 0xC0},
  {0x8F, 0x40, 0xC8, 0x58},
  {0xCC, 0x77, 0x31, 0xD0},
  {0x93, 0xAF, 0xAC, 0x80},
  {0x81, 0xD5, 0x0A, 0x88},
  {0xA2, 0x2B, 0x5F, 0xE0},
  {0x00, 0x99, 0xC8, 0xA0},
  {0xFE, 0x58, 0x2A, 0xA0},
  {0x82, 0xFA, 0x98, 0xD8},
  {0xBA, 0xF8, 0x6F, 0xA8},
  {0xBA, 0xEF, 0x45, 0x60},
  {0xBA, 0xD1, 0xE3, 0xB0},
  {0x82, 0x18, 0x17, 0x50},
  {0xFE, 0xDC, 0xF7, 0x20}
};

bool startupQrModule(int x, int y) {
  if (x < 0 || y < 0 || x >= STARTUP_QR_SIZE || y >= STARTUP_QR_SIZE) return false;
  uint8_t b = pgm_read_byte(&STARTUP_QR[y][x >> 3]);
  return b & (0x80 >> (x & 7));
}

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
DNSServer dnsServer;
Preferences prefs;
String apSSID;
String apPass = "";
String uiTheme = "blue";
String displayProfile = "es_barcelona_grid";
String currentLang = "FR";
String localWifiSsid = "";
String localWifiPass = "";

String transilienFrame = "orange";
String transilienAffluence = "Moyenne";
String transilienDirection = "Direction Meaux";
String transilienStops = "Chelles, Vaires-Torcy, Lagny-Thorigny, Esbly, Meaux";
String transilienInfoTitle = "Infos Travaux";
String transilienInfoText = "Travaux a Villeneuve le Roi, l'arret est supprime. Bus de remplacement entre Ablon et Choisy le Roi. Plus d'informations: Transilien.com";
String swissInfoMessage = "Horaire modifie. Consultez l'affichage en gare avant votre voyage.";
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


bool rowIsFilled(int idx);
int filledRowCount();
int visibleRowIndex(int pos);
int scrollWindowRows();
void getHardwareMac(uint8_t mac[6]);
String macSuffix();
IPAddress stableApIPFromMac();
String apUrl();
String htmlEscape(String s);
String jsonEscape(String s);
String urlEncode(String s);
String cutText(String s, int maxLen);
String marqueeWindow(const String &text, int maxChars, int speedDiv);
void resetRetroDisplay();
String retroBuildVisibleText(int visibleRow, int idx, int maxLen);
int profileMaxVisibleRows(const String &profile);
int profileDefaultVisibleRows(const String &profile);
int autoFontForRows(const String &profile, int visibleRows);
void normalizeSettings();
void normalizeQrSettings();
void loadDefaults();
void loadConfig();
void saveConfig();
String profileTuneSuffix(const String &profile);
int tunedFontSize();
String fontUiValue(int level);
int parseFontLevel(String raw);
int fineTextBaseSize(int level);
int fineTextWeight(int level);
void fineTextAt(int x, int y, uint16_t color, const String &txt, int level);
int colTune(int col);
int sx(int x);
int sxCol(int x, int col);
int sy(int y);
void smallText(int x, int y, uint16_t color, const String &txt);
void fixedSmallText(int x, int y, uint16_t color, const String &txt);
void mediumText(int x, int y, uint16_t color, const String &txt);
void colSmallText(int col, int x, int y, uint16_t color, const String &txt);
void colMediumText(int col, int x, int y, uint16_t color, const String &txt);
void colSmallTextLevel(int col, int x, int y, uint16_t color, const String &txt, int level);
void scrollingCellText(int col, int x, int y, int w, int h, uint8_t size, uint16_t fg, uint16_t bg, const String &txt, int speedDiv);
void clippedCellText(int col, int x, int y, int w, int h, uint8_t size, uint16_t fg, uint16_t bg, const String &txt);
void drawDbLogo(int x, int y, int w, int h);
void drawDbLogo2010(int x, int y, int w, int h);
void drawOebbLogo(int x, int y, uint16_t color, uint16_t bg);
String oebbCompanyForRow(const Row &r);
String oebbCleanInfo(const String &info);
String delayText(const Row &r);
String oebbTrackText(const Row &r);
void drawRailCompanyBox(int x, int y, int w, int h, const String &company, uint16_t bg);
void drawOebbStatusDot(int x, int y, const Row &r, uint16_t bg);
void pseudoItalicText(int x, int y, uint8_t size, uint16_t fg, uint16_t bg, const String &txt);
int getListTop();
int getListHeight();
int getRowHeight();
bool useBigText();
String tftFrameTitle();
String tftFramePlace();
void drawModernFrame();
void drawModernRowsOnly();
void drawOldFrame();
void drawOldRowsOnly();
void drawRetroFrame();
char flapRandomChar(int visibleRow, int charPos);
void drawRetroDestinationCell(int visibleRow, const String &textToDraw);
void retroAnimateOneStep();
void drawSncfModernFrame();
void drawSncfModernRowsOnly();
String marqueeSlice(const String &text, int maxChars, int speedDiv);
void drawSncfOldFrame();
void drawSncfOldRowsOnly();
void drawSncfIdfCrtRows();
void drawFlipCellsText(int x, int y, int cells, const String &txt);
void drawSncfDepartGrandesLignesRows();
void drawSncfArriveesGrandesLignesRows();
void drawSncf2009LedRows();
void drawIenaJuvisyRows();
void drawMontparnasse2010Rows();
void drawMontparnasseTftRows();
void drawTgv1Rows();
void drawTgv2Rows();
void drawSncf1990FlipFlapRows();
bool isItalyProfile();
void drawItalyFrame();
void drawItalyRowsOnly();
bool isRetroProfile();
void drawRowsClassic(int top, int height, uint16_t a, uint16_t b, uint16_t fg, uint16_t hi, uint16_t grid, int layout);
void drawBelgianGridRows();
void drawBelgianOldRows();
void drawSncbDetailListRows();
void drawDetailBoardRows();
void drawTransilien2016Rows();
void drawTransilienLinePRows();
void drawRerALineRows();
void drawRerD8090Rows();
void drawSncfFirstScreenRows();
void drawSncfOldLedRows();
void drawSncfArrivalsGreenRows();
String dbClockText();
void drawDbClockBox();
void drawDbUberCells();
void drawDbLargeBlueRows();
void drawDbIntercityMarquee(uint16_t panel, uint16_t line);
void drawDbIntercityRows();
uint8_t db2010ClockBits(char c, int row);
void drawDb2010ClockGlyph(int x, int y, char c, uint16_t color);
void drawDb2010Clock(int x, int y, const String &txt, uint16_t color);
uint8_t tinyDbBits(char c, int row);
void tinyDbText(int x, int y, uint16_t color, uint16_t bg, const String &txt, int maxChars);
void tinyDbCellText(int x, int y, int w, uint16_t color, uint16_t bg, const String &txt);
void oebbFitText(int x, int y, int w, int h, uint16_t color, uint16_t bg, const String &txt);
void drawDb2010BoardRows();
void drawDb2022BoardRows();
void drawBvgRows();
String pm3dDynamicClock();
void directText(int x, int y, uint8_t size, uint16_t color, const String &txt);
uint8_t badenTinyGlyph(char c, int row);
void drawBadenTinyText(int x, int y, uint16_t color, const char *txt);
uint8_t badenHeaderGlyph(char c, int row);
void drawBadenHeaderText(int x, int y, uint16_t color, const String &txt);
void drawBadenBadenHeader();
void drawBadenBadenRows();
void drawNsLightModernRows();
void drawNs2010PhotoRows();
void drawNsDarkModernRows();
void drawNsBlueClassicRows();
void drawUkModernRows();
void drawSwissCffBlueRows();
void drawSwissBernArrivalRows();
void drawSwissRomandieRows();
void drawDbModernPhotoRows();
void drawDbClockPhotoRows();
void drawUkSplitFlapPhotoRows();
void drawOebbGreenPhotoRows();
void drawOebbBluePhotoRows();
void drawOebbWhiteArrivalRows();
void drawOebbBlueDenseRows();
void drawOebbTealArrivalRows();
void drawZurichFernverkehrRows();
void drawJapanGreenLedRows();
void drawTokyoGreyRows();
void drawTokyoNaritaRows();
void drawSaintLazareRows();
void drawStockholmRows();
void drawMavPhotoRows(bool arrivals);
void drawBarcelonaRodaliesGridRows();
void drawPolandBlueRows(bool arrivals);
void drawLosAngelesRows();
void drawUsAmtrakBlackRows();
void drawBarcelonaAdifRows();
void drawSpainAdifDeparturesRows();
void drawSpainRodaliesDeparturesRows();
void drawNaplesAmberRows();
void drawIndiaRows();
void drawSheffieldRows();
void drawNzBritomartRows();
void drawProfileFrame();
void drawProfileRowsOnly();
void drawFrame();
void drawRowsOnly();
void drawScreenFull();
String themeAccent1();
String themeBodyBg();
String themeCardBg();
String themeSoftPanelBg();
String themeButtonBgFor(const String &theme);
String themeButtonBg();
String cssCommon();
String currentRequestTarget();
String langButton(const String &code);
String langBarHtml();
String pageStart(const String &title);
String pageEnd();
String logoImg(const String &classes);
String logoHeader(const String &title, const String &subtitle);
String advancedWarningModalHtml();
String trText(const String &fr, const String &nl, const String &de, const String &en);
String modeButton(int mode, const String &label);
String modeName();
void applyProfileToScreenMode();
String styleButton(const String &profile, const String &label, const String &meta);
void toggleFavoriteProfile(const String &profile);
String styleMetaText(const String &profile);
String defaultOrderForCountry(const String &country);
bool orderHasProfile(const String &order, const String &profile);
String cleanCountryOrder(const String &country, const String &saved);
String getCountryOrder(const String &country);
void setCountryOrder(const String &country, const String &order);
String buildOrder(String items[], int count);
void moveProfileInCountry(const String &country, const String &profile, const String &dir);
String styleEntry(const String &country, const String &profile);
void activateCountryIfNeeded(const String &country);
String introPage();
String mainPage();
String configMenuPage();
bool profileBlocksCopy(const String &profile);
String settingsPage();
String themeChoice(const String &id, const String &label);
String themesPage();
String previewClassForProfile(const String &profile);
String previewTitleForProfile(const String &profile);
String advancedPage();
String countriesPage();
String countryPage();
String wifiScanPage();
String updatePage();
String packCountryItem(const String &country);
String packManagerPage();
void handleOtaDone();
void handleOtaUpload();
void handleOnlineOta();
void handleIntro();
void handleMain();
void handleConfigMenu();
void handleSettingsPage();
void handleThemes();
void handleAdvanced();
void handleCountries();
void handleCountry();
void handleWifiScan();
void handleUpdates();
void handlePackManager();
void handleCaptivePortal();
void handleFavoriteProfile();
void handleMoveProfile();
String safeBackTarget(const String &rawBack);
void handleSetLang();
void handleSetBright();
void handleSetTheme();
void clearRowsFrom(int startIndex);
void loadProfileDefaults(const String &profile);
void applyBelgium2023PresetOnce();
void applyDb2010PresetOnce();
void handleSetProfile();
void handleSaveWifi();
void handlePing();
void handlePreviewData();
void handleLiveDisplay();
void handleSave();
void pm3dSendHtml(int code, const String &html);
void drawBootQrBlock(int x, int y, int scale);
void drawBootInfo(const IPAddress &ip);
void setup();
void loop();
void drawSncbBlueRecent();
void showAdvancedSettingsPopup();
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

int scrollWindowRows() {
  if (displayProfile == "de_db_intercity") return min(nbVisible, 3);
  if (displayProfile == "fr_sncf_valence_side") return min(nbVisible, 7);
  return nbVisible;
}

bool fullRedrawNeeded = true;
bool rowsOnlyPass = false;
unsigned long lastScroll = 0;
unsigned long lastRetro = 0;
unsigned long lastDbClock = 0;
const unsigned long SCROLL_DELAY = 2600;
const unsigned long RETRO_DELAY  = 55;
int scrollDelayMs = 2600;
int screenBrightness = 255;
int startupQrScale = 4;
int startupQrBorder = 1;
int startupQrBrightness = 4;
int startupQrStyle = 3;
int startupQrFgId = 0;
int startupQrBgId = 1;
int startupQrBodyStyle = 0;
int startupQrMarkerStyle = 0;
int startupQrContentMode = 0;
bool qrTestMode = false;
bool qrPresetMigrationNeeded = false;
bool demoMode = false;
bool demoShowingFlag = true;
int demoCountryIndex = 0;
int demoStyleIndex = 0;
unsigned long demoLastSwitch = 0;
String demoSavedProfile = "";
int displayFontSize = 1;  // Police TFT fine : UI 1.0..3.8 -> niveaux internes 1..12
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
bool otaReady = false;
const char* ONLINE_FIRMWARE_URL = "https://raw.githubusercontent.com/PM3D-Wavre/pm3d-firmware/main/PM3D_Display_2_latest.bin";

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
  // IP volontairement FIXE pour que lÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢interface soit toujours accessible.
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

String jsonEscape(String s) {
  s.replace("\\", "\\\\");
  s.replace("\"", "\\\"");
  s.replace("\r", "\\r");
  s.replace("\n", "\\n");
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
  if (profile == "fr_sncf_idf_crt") return 8;
  if (profile == "fr_sncf_1990_flipflap") return 5;
  if (profile == "fr_sncf_depart_grandes_lignes") return 6;
  if (profile == "fr_sncf_arrivees_grandes_lignes") return 6;
  if (profile == "fr_sncf_2009_led") return 6;
  if (profile == "fr_iena_juvisy") return 6;
  if (profile == "fr_montparnasse_2010") return 7;
  if (profile == "fr_montparnasse_tft") return 8;
  if (profile == "fr_tgv_1") return 9;
  if (profile == "fr_tgv_2") return 5;
  if (profile == "fr_sncf_valence_side") return 7;
  if (profile == "fr_rer_d_8090") return 16;
  if (profile == "fr_sncf_first") return 5;
  if (profile == "fr_sncf_old_led") return 6;
  if (profile == "fr_sncf_arrivals") return 7;
  if (profile == "fr_sncf_2012") return 5;
  if (profile == "fr_rer_a") return 8;
  if (profile == "fr_saint_lazare") return 10;
  if (profile == "fr_transilien_p") return 8;
  if (profile == "fr_transilien_2016") return 8;
  if (profile == "be_sncb_detail_list") return 8;
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
  if (profile == "ch_zurich_fern") return 10;
  if (profile == "ch_bern_arrival") return 8;
  if (profile == "ch_sbb_romandie") return 6;
  if (profile == "ch_sbb_blue") return 8;
  if (profile == "uk_modern") return 4;
  if (profile == "uk_splitflap") return 6;
  if (profile == "uk_sheffield") return 4;
  if (profile == "at_oebb_white") return 5;
  if (profile == "at_oebb_blue") return 7;
  if (profile == "at_oebb_green") return 7;
  if (profile == "at_oebb_dense") return 7;
  if (profile == "at_oebb_teal") return 7;
  if (profile == "nl_ns_light") return 5;
  if (profile == "nl_ns_2010_photo") return 5;
  if (profile == "nl_ns_dark") return 6;
  if (profile == "nl_ns_blue") return 6;
  if (profile == "it_naples_amber") return 6;
  if (profile == "it_fs_blue") return 6;
  if (profile == "hu_mav_arrivals") return 7;
  if (profile == "hu_mav_departures") return 7;
  if (profile == "se_stockholm") return 5;
  if (profile == "jp_jr_led") return 5;
  if (profile == "jp_tokyo_grey") return 3;
  if (profile == "jp_tokyo_narita") return 3;
  if (profile == "es_barcelona_grid") return 6;
  if (profile == "es_adif_departures") return 6;
  if (profile == "es_rodalies_departures") return 6;
  if (profile == "es_barcelona_adif") return 6;
  if (profile == "pl_pkp_departures") return 5;
  if (profile == "us_la_metro") return 5;
  if (profile == "us_amtrak_black") return 6;
  if (profile == "in_indian_railways") return 6;

  return 10;
}

int profileDefaultVisibleRows(const String &profile) {
  if (profile == "be_sncb_modern") return 6;
  if (profile == "be_sncb_detail") return 5;
  if (profile == "fr_montparnasse_tft") return 8;
  if (profile == "fr_sncf_2012") return 5;
  if (profile == "fr_sncf_1990_flipflap") return 5;
  if (profile == "fr_sncf_depart_grandes_lignes" || profile == "fr_sncf_arrivees_grandes_lignes") return 6;
  if (profile == "fr_iena_juvisy") return 6;
  if (profile == "fr_sncf_valence_side") return 7;
  if (profile == "fr_rer_d_8090") return 16;
  if (profile == "pl_pkp_departures") return 5;
  if (profile == "jp_tokyo_narita") return 3;
  if (profile == "hu_mav_arrivals" || profile == "hu_mav_departures") return 7;
  if (profile == "se_stockholm") return 5;
  if (profile == "ch_sbb_romandie") return 6;
  if (profile == "nl_ns_light" || profile == "nl_ns_2010_photo") return 5;
  if (profile == "us_la_metro") return 5;
  if (profile == "us_amtrak_black") return 6;
  if (profile == "de_db_large_blue") return 6;
  if (profile == "de_db_intercity") return 3;
  if (profile == "de_db_2010_2015") return 5;
  if (profile == "de_db_2022") return 4;
  return profileMaxVisibleRows(profile);
}

int autoFontForRows(const String &profile, int visibleRows) {
  if (profile == "fr_transilien" || profile == "fr_sncf_idf_crt" || profile == "fr_sncf_1990_flipflap" || profile == "fr_sncf_depart_grandes_lignes" || profile == "fr_sncf_arrivees_grandes_lignes" || profile == "fr_sncf_2009_led" || profile == "fr_iena_juvisy" || profile == "fr_montparnasse_2010" || profile == "fr_montparnasse_tft" || profile == "fr_tgv_1" || profile == "fr_tgv_2" || profile == "fr_sncf_valence_side" || profile == "fr_rer_d_8090" || profile == "uk_modern" || profile == "jp_jr_led" || profile == "jp_tokyo_grey" || profile == "jp_tokyo_narita") return 1;
  if (profile == "de_db_intercity" || profile == "fr_rer_a") return 1;
  if (profile == "ch_sbb_romandie" || profile == "ch_sbb_blue") return 1;
  if (visibleRows <= 2) return 5;
  if (visibleRows <= 3) return 4;
  if (visibleRows <= 6) return 2;
  return 1;
}

void normalizeSettings() {
  int maxRows = profileMaxVisibleRows(displayProfile);
  if (nbVisible < 1) nbVisible = 1;
  if (nbVisible > maxRows) nbVisible = maxRows;
  if (scrollDelayMs < 500) scrollDelayMs = 500;
  if (scrollDelayMs > 15000) scrollDelayMs = 15000;
  if (screenBrightness < 20) screenBrightness = 20;
  if (screenBrightness > 255) screenBrightness = 255;
  if (displayFontSize < 1) displayFontSize = 1;
  if (displayFontSize > 12) displayFontSize = 12;
  badenSteig.trim();
  if (badenSteig.length() == 0) badenSteig = "1a";
  if (badenSteig.length() > 6) badenSteig = badenSteig.substring(0, 6);
  dbIntercityMessage.trim();
  if (dbIntercityMessage.length() == 0) dbIntercityMessage = "Bitte beachten Sie die Anzeige am Bahnsteig";
  if (dbIntercityMessage.length() > 96) dbIntercityMessage = dbIntercityMessage.substring(0, 96);
  swissInfoMessage.trim();
  if (swissInfoMessage.length() == 0) swissInfoMessage = "Horaire modifie. Consultez l'affichage en gare avant votre voyage.";
  if (swissInfoMessage.length() > 140) swissInfoMessage = swissInfoMessage.substring(0, 140);
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
  startupQrScale = prefs.getInt("qrScale", 4);
  startupQrBorder = prefs.getInt("qrBorder", 1);
  startupQrBrightness = prefs.getInt("qrBright", 10);
  startupQrStyle = prefs.getInt("qrStyle", 0);
  startupQrFgId = prefs.getInt("qrFg", 0);
  startupQrBgId = prefs.getInt("qrBg", 1);
  startupQrBodyStyle = prefs.getInt("qrBody", 0);
  startupQrMarkerStyle = prefs.getInt("qrMark", 0);
  startupQrContentMode = prefs.getInt("qrType", 0);
  qrPresetMigrationNeeded = !prefs.getBool("qrPreset2", false);
  bool franceOrderMigrationNeeded = !prefs.getBool("ordFr2026a", false);
  if (qrPresetMigrationNeeded) {
    startupQrScale = 4;
    startupQrBrightness = 4;
    startupQrStyle = 3;
    startupQrBorder = 1;
    startupQrBodyStyle = 0;
    startupQrMarkerStyle = 0;
    startupQrContentMode = 0;
  }
  displayFontSize = prefs.getInt("font", 1);
  tftOffsetX1 = prefs.getInt("ox1", 52);
  tftOffsetY1 = prefs.getInt("oy1", 40);
  tftOffsetX2 = prefs.getInt("ox2", 53);
  tftOffsetY2 = prefs.getInt("oy2", 40);
  tftPanelW = prefs.getInt("pw", 135);
  tftPanelH = prefs.getInt("ph", 240);
  screenMode = prefs.getInt("mode", 0);
  uiTheme = prefs.getString("theme", "blue");
  displayProfile = prefs.getString("profile", "es_barcelona_grid");
  currentLang = prefs.getString("lang", "FR");
  userRowsEdited = prefs.getBool("rowsEdited", false);
  demoMode = prefs.getBool("demo", true);
  localWifiSsid = prefs.getString("staSsid", "");
  localWifiPass = prefs.getString("staPass", "");

  apPass = prefs.getString("apPass", "");
  transilienFrame = prefs.getString("trFrame", "orange");
  transilienAffluence = prefs.getString("trAff", "Moyenne");
  transilienDirection = prefs.getString("trDir", "Direction Meaux");
  transilienStops = prefs.getString("trStops", "Chelles, Vaires-Torcy, Lagny-Thorigny, Esbly, Meaux");
  transilienInfoTitle = prefs.getString("trInfoT", "Infos Travaux");
  transilienInfoText = prefs.getString("trInfoX", "Travaux a Villeneuve le Roi, l'arret est supprime. Bus de remplacement entre Ablon et Choisy le Roi. Plus d'informations: Transilien.com");
  swissInfoMessage = prefs.getString("swissMsg", "Horaire modifie. Consultez l'affichage en gare avant votre voyage.");
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
    normalizeQrSettings();
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
  if (franceOrderMigrationNeeded) {
    orderFr = defaultOrderForCountry("fr");
    prefs.begin("pm3ddisp", false);
    prefs.putString("ordFr", orderFr);
    prefs.putBool("ordFr2026a", true);
    prefs.end();
  }
  normalizeSettings();
  normalizeQrSettings();
  normalizeTftOffsets();
  if (qrPresetMigrationNeeded) {
    prefs.begin("pm3ddisp", false);
    prefs.putInt("qrScale", startupQrScale);
    prefs.putInt("qrBorder", startupQrBorder);
    prefs.putInt("qrBright", startupQrBrightness);
    prefs.putInt("qrStyle", startupQrStyle);
    prefs.putInt("qrFg", startupQrFgId);
    prefs.putInt("qrBg", startupQrBgId);
    prefs.putInt("qrBody", startupQrBodyStyle);
    prefs.putInt("qrMark", startupQrMarkerStyle);
    prefs.putInt("qrType", startupQrContentMode);
    prefs.putBool("qrPreset2", true);
    prefs.end();
    qrPresetMigrationNeeded = false;
  }
  loadStyleTune(displayProfile);
}

void saveConfig() {
  normalizeSettings();
  normalizeQrSettings();
  prefs.begin("pm3ddisp", false);

  prefs.putBool("saved", true);
  prefs.putInt("nb", nbVisible);
  prefs.putInt("scrollMs", scrollDelayMs);
  prefs.putInt("bright", screenBrightness);
  prefs.putInt("qrScale", startupQrScale);
  prefs.putInt("qrBorder", startupQrBorder);
  prefs.putInt("qrBright", startupQrBrightness);
  prefs.putInt("qrStyle", startupQrStyle);
  prefs.putInt("qrFg", startupQrFgId);
  prefs.putInt("qrBg", startupQrBgId);
  prefs.putInt("qrBody", startupQrBodyStyle);
  prefs.putInt("qrMark", startupQrMarkerStyle);
  prefs.putInt("qrType", startupQrContentMode);
  prefs.putBool("qrPreset2", true);
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
  prefs.putBool("demo", demoMode);
  prefs.putString("staSsid", localWifiSsid);
  prefs.putString("staPass", localWifiPass);

  prefs.putString("apPass", apPass);
  prefs.putString("trFrame", transilienFrame);
  prefs.putString("trAff", transilienAffluence);
  prefs.putString("trDir", transilienDirection);
  prefs.putString("trStops", transilienStops);
  prefs.putString("trInfoT", transilienInfoTitle);
  prefs.putString("trInfoX", transilienInfoText);
  prefs.putString("swissMsg", swissInfoMessage);
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

void normalizeQrSettings() {
  if (startupQrScale < 1) startupQrScale = 1;
  startupQrBorder = constrain(startupQrBorder, 0, 8);
  startupQrBrightness = constrain(startupQrBrightness, 1, 10);
  startupQrStyle = constrain(startupQrStyle, 0, 12);
  startupQrFgId = constrain(startupQrFgId, 0, 7);
  startupQrBgId = constrain(startupQrBgId, 0, 7);
  startupQrBodyStyle = constrain(startupQrBodyStyle, 0, 11);
  startupQrMarkerStyle = constrain(startupQrMarkerStyle, 0, 5);
  startupQrContentMode = constrain(startupQrContentMode, 0, 1);
  if (startupQrFgId == startupQrBgId && startupQrStyle == 4) {
    startupQrFgId = 0;
    startupQrBgId = 1;
  }
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

String fontUiValue(int level) {
  float v = 1.0f + (constrain(level, 1, 12) - 1) * 0.25f;
  char buf[8];
  dtostrf(v, 0, 1, buf);
  return String(buf);
}

int parseFontLevel(String raw) {
  raw.trim();
  raw.replace(',', '.');
  if (raw.indexOf('.') >= 0) {
    int level = 1 + (int)round((raw.toFloat() - 1.0f) * 4.0f);
    return constrain(level, 1, 12);
  }
  return constrain(raw.toInt(), 1, 12);
}

int fineTextBaseSize(int level) {
  if (level >= 11) return 4;
  if (level >= 9) return 3;
  if (level >= 5) return 2;
  return 1;
}

int fineTextWeight(int level) {
  if (level == 2 || level == 6 || level == 9 || level == 11) return 1;
  if (level == 3 || level == 7 || level == 10 || level == 12) return 2;
  if (level == 4 || level == 8) return 3;
  return 0;
}

void fineTextAt(int x, int y, uint16_t color, const String &txt, int level) {
  int size = fineTextBaseSize(level);
  int weight = fineTextWeight(level);
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(sx(x), sy(y));
  gfx->print(txt);
  if (weight >= 1) {
    gfx->setCursor(sx(x + 1), sy(y));
    gfx->print(txt);
  }
  if (weight >= 2) {
    gfx->setCursor(sx(x), sy(y + 1));
    gfx->print(txt);
  }
  if (weight >= 3) {
    gfx->setCursor(sx(x + 1), sy(y + 1));
    gfx->print(txt);
  }
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
  styleFontSize = constrain(styleFontSize, 0, 12);
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
  fineTextAt(x, y, color, txt, tunedFontSize());
}

void fixedSmallText(int x, int y, uint16_t color, const String &txt) {
  gfx->setTextSize(1);
  gfx->setTextColor(color);
  gfx->setCursor(sx(x), sy(y));
  gfx->print(txt);
}

void mediumText(int x, int y, uint16_t color, const String &txt) {
  gfx->setTextSize(fineTextBaseSize(tunedFontSize()) + 1);
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

void colSmallTextLevel(int col, int x, int y, uint16_t color, const String &txt, int level) {
  fineTextAt(sxCol(x, col), y, color, txt, constrain(level, 1, 12));
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
  if (!rowsOnlyPass) gfx->fillScreen(C_BLACK);

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
  if (!rowsOnlyPass) gfx->fillScreen(C_BLACK);
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
  if (!rowsOnlyPass) gfx->fillScreen(C_BLACK);
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
  if (!rowsOnlyPass) gfx->fillScreen(C_BLACK);
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
  const uint16_t blueB = 0x0218;
  const uint16_t line = 0x03BF;
  const uint16_t yellow = C_SNCB_YELLOW;

  if (!rowsOnlyPass) gfx->fillScreen(C_BLACK);
  gfx->fillRect(0, 0, w, h, 0x0012);
  gfx->fillRect(0, 0, w, 20, 0x2945);
  gfx->setTextSize(2);
  gfx->setTextColor(C_WHITE, 0x2945);
  gfx->setCursor(8, 3);
  gfx->print("Departs");
  gfx->setTextSize(1);
  gfx->setCursor(93, 7);
  gfx->print("Departures - Partenze");

  int rowsToDraw = constrain(nbVisible, 1, 7);
  int rowTop = 27;
  int rowH = max(13, (112 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    uint16_t bg = (i % 2 == 0) ? blueA : blueB;
    gfx->fillRect(5, y, w - 10, rowH - 1, bg);
    gfx->drawFastHLine(5, y + rowH - 1, w - 10, line);

    String train = rows[idx].typeTrain.length() ? rows[idx].typeTrain : "TER";
    String number = "";
    int sp = train.indexOf(' ');
    if (sp > 0) {
      number = train.substring(sp + 1);
      train = train.substring(0, sp);
    }
    train.toUpperCase();
    smallText(8, y + 2, C_WHITE, cutText(train, 5));
    if (number.length()) smallText(8, y + 9, 0xD6FF, cutText(number, 6));

    String status = "a l'heure";
    if (rows[idx].info.startsWith("retard") || rows[idx].info.startsWith("+")) status = rows[idx].info;
    smallText(49, y + 4, yellow, cutText(rows[idx].heure, 5));
    smallText(82, y + 2, C_WHITE, cutText(rows[idx].destination, 20));

    String stops = rows[idx].info;
    if (!stops.length() || stops == "a l'heure" || stops.startsWith("retard") || stops.startsWith("+")) {
      if (rows[idx].destination.indexOf("Lyon") >= 0) stops = "Chatillon  *  Villars les Dombes  *  St-Andre de Corcy";
      else if (rows[idx].destination.indexOf("Paris") >= 0) stops = "Bellegarde  *  Lyon Part-Dieu  *  Le Creusot TGV";
      else if (rows[idx].destination.indexOf("Geneve") >= 0) stops = "Bourg-en-Bresse  *  Bellegarde  *  Geneve";
      else stops = "Arrets desservis  *  correspondances  *  voie " + rows[idx].voie;
    }
    smallText(82, y + 9, 0xD6FF, marqueeSlice(stops, 22, 280));

    if (rows[idx].voie.length()) {
      gfx->drawRoundRect(w - 20, y + 2, 16, rowH - 4, 2, C_WHITE);
      smallText(w - 15, y + 5, C_WHITE, cutText(rows[idx].voie, 1));
    }
  }
  smallText(8, 124, C_WHITE, "train n");
  smallText(58, 124, C_WHITE, "heure");
  smallText(107, 124, C_WHITE, "destination");
  smallText(217, 124, C_WHITE, "voie");
}

void drawSncfValenceSideRows() {
  const uint16_t paper = 0xEF5D;
  const uint16_t paper2 = 0xD69A;
  const uint16_t blue = 0x039F;
  const uint16_t blue2 = 0x025F;
  const uint16_t ink = 0x2945;
  const uint16_t pale = 0xD6FF;
  const uint16_t dot = 0xE7FF;
  gfx->fillRect(0, 0, 240, 135, 0x39E7);
  gfx->fillRect(5, 8, 82, 120, paper);
  gfx->fillRect(87, 8, 148, 120, blue);
  gfx->drawRect(5, 8, 230, 120, 0x9CD3);
  gfx->fillRect(5, 8, 82, 7, paper2);
  fixedSmallText(10, 11, 0x7B8E, "SNCF");
  gfx->setTextSize(2);
  gfx->setTextColor(ink, paper);
  gfx->setCursor(sx(13), sy(30));
  gfx->print("12h43");
  gfx->setTextSize(1);
  fixedSmallText(55, 36, 0x6B4D, "a l'heure");
  gfx->drawFastHLine(12, 51, 68, 0xB596);
  gfx->setTextColor(ink, paper);
  gfx->setCursor(sx(13), sy(58));
  gfx->print("Valence Ville");
  fixedSmallText(13, 70, 0x5269, "Via Grenoble");
  fixedSmallText(13, 83, 0x5269, "TER 17524");

  int rowsToDraw = min(8, scrollWindowRows());
  int rowTop = 13;
  int rowH = max(13, (120 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    uint16_t rb = (i % 2) ? blue2 : blue;
    gfx->fillRect(90, y, 141, rowH - 1, rb);
    gfx->drawFastHLine(90, y + rowH - 1, 141, 0x24DF);
    int cy = y + rowH / 2;
    gfx->fillCircle(96, cy, 2, dot);
    String txt = rows[idx].destination.length() ? rows[idx].destination : rows[idx].info;
    gfx->setTextSize(1);
    gfx->setTextColor(C_WHITE, rb);
    gfx->setCursor(sx(102), sy(y + max(2, (rowH - 8) / 2)));
    gfx->print(cutText(txt, 20));
  }
  gfx->fillRect(210, 119, 23, 9, 0x0012);
  fixedSmallText(212, 121, C_WHITE, "12:22");
  gfx->fillRect(82, 16, 3, 104, 0xB596);
  for (int yy = 43; yy < 94; yy += 9) gfx->drawPixel(83, yy, pale);
}

// =====================================================
// MODE 4 : ANCIEN ECRAN SNCF
// =====================================================
void drawSncfOldFrame() {
  if (!rowsOnlyPass) gfx->fillScreen(C_BLACK);
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

void drawSncfIdfCrtRows() {
  const uint16_t bg = 0x001F;
  const uint16_t blueA = 0x02DF;
  const uint16_t blueB = 0x01B7;
  const uint16_t yellow = C_YELLOW;
  const uint16_t white = C_WHITE;
  gfx->fillRect(0, 0, 240, 135, C_BLACK);
  gfx->fillRect(5, 7, 230, 122, bg);
  gfx->drawRect(5, 7, 230, 122, yellow);
  fixedSmallText(10, 12, yellow, "Nom");
  fixedSmallText(47, 12, yellow, "Destination");
  fixedSmallText(137, 12, yellow, "Heure");
  fixedSmallText(209, 12, yellow, "17");
  fixedSmallText(224, 12, yellow, "G");
  gfx->drawFastHLine(7, 24, 226, yellow);
  gfx->drawFastVLine(42, 9, 117, yellow);
  gfx->drawFastVLine(132, 9, 117, yellow);
  gfx->drawFastVLine(206, 9, 117, yellow);
  gfx->drawFastVLine(221, 9, 117, yellow);

  int rowsToDraw = constrain(nbVisible, 1, 8);
  int rowTop = 28;
  int rowH = max(11, (126 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    gfx->fillRect(7, y, 226, rowH - 1, (i % 2) ? blueA : blueB);
    fixedSmallText(10, ty, white, cutText(rows[idx].typeTrain, 4));
    fixedSmallText(47, ty, white, cutText(rows[idx].destination, 13));
    fixedSmallText(137, ty, white, cutText(rows[idx].heure, 5));
    fixedSmallText(211, ty, white, cutText(rows[idx].voie, 1));
    fixedSmallText(225, ty, white, cutText(rows[idx].info, 1));
  }
}

void drawFlipCellsText(int x, int y, int cells, const String &txt) {
  const uint16_t cellBg = 0x2104;
  const uint16_t edge = 0x4208;
  const uint16_t yellow = 0xFEE0;
  String s = txt;
  s.toUpperCase();
  for (int i = 0; i < cells; i++) {
    int cx = x + i * 9;
    gfx->fillRect(cx, y, 8, 10, cellBg);
    gfx->drawRect(cx, y, 8, 10, edge);
    gfx->drawFastHLine(cx + 1, y + 5, 6, edge);
    char ch = i < (int)s.length() ? s[i] : ' ';
    if (ch != ' ') {
      gfx->setTextSize(1);
      gfx->setTextColor(yellow, cellBg);
      gfx->setCursor(sx(cx + 1), sy(y + 1));
      gfx->print(ch);
    }
  }
}

void drawSncfDepartGrandesLignesRows() {
  const uint16_t screen = 0x0016;
  const uint16_t blueA = 0x029F;
  const uint16_t blueB = 0x0178;
  const uint16_t soft = 0xBDF7;
  const uint16_t dim = 0x7BEF;
  gfx->fillRect(0, 0, 240, 135, screen);
  fixedSmallText(10, 7, soft, "Departs Grandes Lignes");
  fixedSmallText(166, 7, dim, "15:24");

  gfx->fillRect(4, 20, 232, 102, 0x012F);
  int rowsToDraw = constrain(nbVisible, 1, 8);
  int rowTop = 22;
  int rowH = max(10, (122 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    gfx->fillRect(4, y, 232, rowH - 1, (i % 2) ? blueB : blueA);
    gfx->fillRect(8, ty - 1, 24, 8, 0xE7FF);
    fixedSmallText(11, ty, 0x03DF, rows[idx].typeTrain.length() ? cutText(rows[idx].typeTrain, 3) : "SNC");
    fixedSmallText(39, ty, soft, rows[idx].info.length() ? cutText(rows[idx].info, 8) : "a l'heure");
    fixedSmallText(90, ty, C_YELLOW, cutText(rows[idx].heure, 5));
    fixedSmallText(126, ty, C_WHITE, cutText(rows[idx].destination, 14));
    fixedSmallText(220, ty, C_WHITE, cutText(rows[idx].voie, 2));
  }

  fixedSmallText(10, 125, dim, "train");
  fixedSmallText(89, 125, dim, "heure");
  fixedSmallText(126, 125, dim, "destination");
  fixedSmallText(220, 125, dim, "voie");
}
void drawSncfArriveesGrandesLignesRows() {
  const uint16_t outer = 0x2104;
  const uint16_t screen = 0x0024;
  const uint16_t greenA = 0x04C7;
  const uint16_t greenB = 0x0365;
  const uint16_t greenDeep = 0x01C3;
  const uint16_t soft = 0xBDF7;
  const uint16_t dim = 0x7BEF;
  gfx->fillRect(0, 0, 240, 135, outer);
  gfx->fillRect(8, 8, 224, 119, screen);
  gfx->drawRect(8, 8, 224, 119, 0x39E7);
  fixedSmallText(18, 12, soft, "Arrivees Grandes Lignes");
  fixedSmallText(18, 21, dim, "Mainline arrivals - Llegadas Grandes Lineas");

  gfx->fillRect(18, 31, 204, 82, greenDeep);
  int rowsToDraw = constrain(nbVisible, 1, 8);
  int rowTop = 33;
  int rowH = max(9, (112 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    gfx->fillRect(19, y, 202, rowH - 1, (i % 2) ? greenB : greenA);
    gfx->fillRect(23, ty - 1, 19, 8, 0xE7FF);
    fixedSmallText(25, ty, 0x03A0, rows[idx].typeTrain.length() ? cutText(rows[idx].typeTrain, 3) : "SNC");
    fixedSmallText(46, ty, soft, rows[idx].info.length() ? cutText(rows[idx].info, 9) : "a l'heure");
    fixedSmallText(88, ty, C_YELLOW, cutText(rows[idx].heure, 5));
    fixedSmallText(123, ty, C_WHITE, cutText(rows[idx].destination, 13));
    fixedSmallText(221, ty, C_WHITE, cutText(rows[idx].voie, 2));
  }

  gfx->fillRect(211, 35, 15, 16, 0x029F);
  gfx->drawRect(211, 35, 15, 16, 0x7DFF);
  fixedSmallText(214, 39, C_WHITE, "18");
  gfx->fillRect(202, 113, 24, 10, 0x027F);
  fixedSmallText(205, 115, C_WHITE, "15 24");
  fixedSmallText(20, 120, dim, "train n");
  fixedSmallText(72, 120, dim, "heure");
  fixedSmallText(122, 120, dim, "provenance");
  fixedSmallText(220, 120, dim, "voie");
}
void drawSncf2009LedRows() {
  const uint16_t bg = C_BLACK;
  const uint16_t amber = 0xFD20;
  const uint16_t amberDim = 0xC340;
  const uint16_t line = 0x7A20;
  gfx->fillRect(0, 0, 240, 135, bg);
  gfx->drawRect(4, 4, 232, 127, 0x2104);
  fixedSmallText(185, 11, amber, "15:29");
  gfx->fillRect(8, 32, 224, 12, amber);
  fixedSmallText(10, 35, bg, "Nom");
  fixedSmallText(50, 35, bg, "Destination");
  fixedSmallText(143, 35, bg, "Heure");
  fixedSmallText(211, 35, bg, "Voie");

  int rowsToDraw = constrain(nbVisible, 1, 6);
  int rowTop = 48;
  int rowH = max(10, (101 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    if (i > 0) gfx->drawFastHLine(8, y - 1, 224, 0x2104);
    fixedSmallText(10, ty, amber, cutText(rows[idx].typeTrain, 4));
    fixedSmallText(50, ty, amber, cutText(rows[idx].destination, 13));
    String status = rows[idx].info.length() ? rows[idx].info : rows[idx].heure;
    fixedSmallText(143, ty, amber, cutText(status, 10));
    fixedSmallText(214, ty, amber, cutText(rows[idx].voie, 2));
  }

  gfx->fillRect(8, 106, 224, 14, amber);
  fixedSmallText(14, 110, bg, "train ne circule apres St Denis. Bus");
  gfx->drawFastHLine(8, 123, 224, line);
  fixedSmallText(10, 124, amberDim, "SNCF 2009 - quai depart");
}
void drawIenaJuvisyRows() {
  const uint16_t page = 0xD71C;
  const uint16_t head = 0xE71C;
  const uint16_t purple = 0x21B2;
  const uint16_t blue = 0x7D5F;
  const uint16_t blue2 = 0x6CDA;
  const uint16_t ink = C_WHITE;
  const uint16_t dark = 0x2945;
  gfx->fillRect(0, 0, 240, 135, page);
  gfx->fillRect(4, 5, 232, 14, head);
  gfx->fillRoundRect(7, 7, 29, 10, 3, 0x9CF3);
  fixedSmallText(10, 9, C_WHITE, "17:52");

  const int cols = 3;
  const int colW = 74;
  const int x0 = 6;
  int rowsToDraw = constrain(nbVisible, 1, 15);
  int perCol = (rowsToDraw + cols - 1) / cols;
  int rowTop = 30;
  int rowH = max(10, (126 - rowTop) / perCol);
  for (int c = 0; c < cols; c++) {
    int x = x0 + c * 78;
    if (c == 0) fixedSmallText(42, 21, dark, "Prochains trains");
    if (c == 2) fixedSmallText(172, 21, 0x5AEB, "Suivants");
    for (int r = 0; r < perCol; r++) {
      int item = c * perCol + r;
      if (item >= rowsToDraw) continue;
      int idx = visibleRowIndex(scrollOffset + item);
      int y = rowTop + r * rowH;
      int ty = y + max(1, (rowH - 8) / 2);
      gfx->fillRect(x, y, 32, rowH - 1, purple);
      gfx->fillRect(x + 32, y, 39, rowH - 1, (r % 2) ? blue2 : blue);
      gfx->fillRect(x + 71, y, 6, rowH - 1, 0xBDF7);
      if (r == 0) {
        gfx->fillRect(x, y, 8, rowH - 1, 0x3192);
        fixedSmallText(x + 2, ty, C_WHITE, String((char)('A' + c)));
      }
      String juvDest = rows[idx].destination;
      juvDest.replace("-", " ");
      fixedSmallText(x + 10, ty, ink, cutText(juvDest, 7));
      fixedSmallText(x + 38, ty, 0xEFFF, cutText(rows[idx].typeTrain, 3));
      fixedSmallText(x + 55, ty, ink, cutText(rows[idx].heure, 4));
      fixedSmallText(x + 72, ty, dark, cutText(rows[idx].voie, 2));
    }
  }
}
void drawMontparnasse2010Rows() {
  const uint16_t bg = 0x0000;
  const uint16_t panel = 0x0841;
  const uint16_t text = 0xCE59;
  const uint16_t dim = 0x6B4D;
  const uint16_t green = 0x3C66;
  gfx->fillRect(0, 0, 240, 135, bg);
  gfx->fillRect(3, 5, 234, 124, panel);
  gfx->drawRect(3, 5, 234, 124, 0x2104);
  fixedSmallText(10, 11, dim, "train");
  fixedSmallText(64, 11, dim, "destination");
  fixedSmallText(181, 11, dim, "depart");
  fixedSmallText(218, 11, dim, "voie");
  gfx->drawFastHLine(8, 23, 224, 0x18C3);

  int rowsToDraw = constrain(nbVisible, 1, 7);
  int rowTop = 27;
  int rowH = max(12, (119 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(0, (rowH - 8) / 2);
    uint16_t c = (i % 2) ? text : 0xBDF7;
    fixedSmallText(10, ty, c, cutText(rows[idx].typeTrain, 8));
    fixedSmallText(64, ty, c, marqueeSlice(rows[idx].destination, 18, 330));
    fixedSmallText(181, ty, c, cutText(rows[idx].heure, 5));
    fixedSmallText(220, ty, c, cutText(rows[idx].voie, 2));
  }
  gfx->drawFastHLine(8, 117, 224, green);
  fixedSmallText(63, 121, green, "quais indisponibles - verifiez voie");
}
void drawMontparnasseTftRows() {
  const uint16_t wall = 0x39E7;
  const uint16_t screen = 0x0010;
  const uint16_t rowA = 0x0278;
  const uint16_t rowB = 0x001F;
  const uint16_t header = 0x0008;
  const uint16_t yellow = 0xFFE0;
  const uint16_t pale = 0xBDF7;
  gfx->fillRect(0, 0, 240, 135, wall);
  gfx->fillRect(8, 23, 224, 105, screen);
  gfx->drawRect(8, 23, 224, 105, 0x18C3);
  gfx->setTextSize(1);
  gfx->setTextColor(C_WHITE, wall);
  gfx->setCursor(sx(18), sy(5));
  gfx->print("Departs Ile-de-France");
  fixedSmallText(18, 15, C_WHITE, "Suburban departures");

  const int cols = 2;
  const int colW = 108;
  int rowsToDraw = constrain(nbVisible, 1, 10);
  int perCol = (rowsToDraw + cols - 1) / cols;
  int rowTop = 39;
  int rowH = max(16, (126 - rowTop) / perCol);
  for (int c = 0; c < cols; c++) {
    int x = 11 + c * 111;
    gfx->fillRect(x, 27, colW, 10, header);
    fixedSmallText(x + 2, 29, pale, "Destination");
    fixedSmallText(x + 68, 29, pale, "Heure");
    for (int r = 0; r < perCol; r++) {
      int item = c * perCol + r;
      if (item >= rowsToDraw) continue;
      int idx = visibleRowIndex(scrollOffset + item);
      int y = rowTop + r * rowH;
      int ty = y + 2;
      gfx->fillRect(x, y, colW, rowH - 1, (r % 2) ? rowB : rowA);
      gfx->drawFastHLine(x, y + rowH - 1, colW, 0x039F);
      if (rows[idx].destination == "Chartres" || rows[idx].destination == "Dreux" || rows[idx].destination == "Maintenon") {
        gfx->fillRect(x + 1, y + 2, 3, rowH - 4, yellow);
      }
      fixedSmallText(x + 6, ty, C_WHITE, cutText(rows[idx].destination, 10));
      fixedSmallText(x + 68, ty, yellow, cutText(rows[idx].heure, 5));
      fixedSmallText(x + 68, ty + 8, pale, cutText(rows[idx].info, 5));
      if (rows[idx].typeTrain.length()) {
        gfx->fillRect(x + 96, y + 3, 9, 9, 0x0010);
        fixedSmallText(x + 98, y + 5, C_WHITE, cutText(rows[idx].typeTrain, 1));
      }
    }
  }
}
void drawTgv1Rows() {
  const uint16_t bg = 0x0008;
  const uint16_t blueA = 0x02BF;
  const uint16_t blueB = 0x001F;
  const uint16_t logoBg = 0xE7FF;
  const uint16_t logoText = 0x025F;
  const uint16_t pale = 0xBDF7;
  gfx->fillRect(0, 0, 240, 135, bg);
  gfx->fillRect(3, 3, 234, 129, 0x0018);
  gfx->fillRect(220, 6, 15, 112, 0x04BF);
  gfx->setTextSize(3);
  gfx->setTextColor(0x039F, 0x04BF);
  gfx->setCursor(sx(219), sy(29));
  gfx->print("d");
  gfx->setCursor(sx(219), sy(53));
  gfx->print("e");
  gfx->setCursor(sx(219), sy(77));
  gfx->print("p");
  gfx->setTextSize(1);
  gfx->setTextColor(0x039F, 0x04BF);
  gfx->setCursor(sx(222), sy(103));
  gfx->print("art");
  int rowsToDraw = constrain(nbVisible, 1, 9);
  int rowTop = 6;
  int rowH = max(13, (124 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    uint16_t rb = (i % 2) ? blueB : blueA;
    gfx->fillRect(5, y, 212, rowH - 1, rb);
    gfx->fillRect(8, ty - 2, 22, 10, logoBg);
    fixedSmallText(10, ty, logoText, "SNCF");
    fixedSmallText(34, ty - 2, pale, cutText(rows[idx].typeTrain, 3));
    if (rowH >= 14) fixedSmallText(34, ty + 5, pale, cutText(rows[idx].info.startsWith("retard") ? rows[idx].info : rows[idx].retard, 5));
    fixedSmallText(66, ty, 0xFFE0, cutText(rows[idx].heure, 5));
    fixedSmallText(107, ty, C_WHITE, cutText(rows[idx].destination, 15));
    if (rows[idx].voie.length()) {
      gfx->fillRoundRect(198, ty - 4, 15, 14, 3, 0x001F);
      gfx->drawRoundRect(198, ty - 4, 15, 14, 3, C_WHITE);
      fixedSmallText(203, ty, C_WHITE, cutText(rows[idx].voie, 1));
    }
  }
  fixedSmallText(190, 124, C_WHITE, pm3dDynamicClock());
}
void drawTgv2Rows() {
  const uint16_t bg = 0x0008;
  const uint16_t blueA = 0x0278;
  const uint16_t blueB = 0x0130;
  const uint16_t logoBg = 0xE7FF;
  const uint16_t logoText = 0x025F;
  const uint16_t pale = 0xBDF7;
  const uint16_t bigBlue = 0x047F;
  gfx->fillRect(0, 0, 240, 135, bg);
  gfx->fillRect(3, 6, 234, 123, 0x0018);
  int rowsToDraw = constrain(nbVisible, 1, 5);
  int rowTop = 10;
  int rowH = max(22, (126 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    uint16_t rb = (i % 2) ? blueB : blueA;
    gfx->fillRect(5, y, 230, rowH - 1, rb);
    gfx->fillCircle(16, y + 8, 4, 0x7BEF);
    gfx->fillRect(31, y + 3, 21, 8, logoBg);
    fixedSmallText(33, y + 4, logoText, rows[idx].typeTrain.length() ? cutText(rows[idx].typeTrain, 3) : "SNC");
    fixedSmallText(58, y + 4, 0xFFE0, cutText(rows[idx].heure, 5));
    fixedSmallText(96, y + 4, C_WHITE, cutText(rows[idx].destination, 17));
    if (rows[idx].voie.length()) {
      gfx->drawRect(221, y + 4, 14, 12, 0xBDF7);
      fixedSmallText(225, y + 7, C_WHITE, cutText(rows[idx].voie, 1));
    }
    String sub = rows[idx].info.startsWith("retard") ? rows[idx].info : "";
    if (sub.length()) fixedSmallText(58, y + 14, pale, cutText(sub, 12));
    else fixedSmallText(96, y + 14, pale, cutText(rows[idx].destination, 19));
  }
}
String flipFlapPreviewText(String target, int width, int row, int phaseMs) {
  target = cutText(target, width);
  while (target.length() < width) target += " ";
  if (phaseMs >= 900) return target;
  const char* wheel = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  int bucket = phaseMs / 85;
  int settled = map(phaseMs, 0, 900, -1, width);
  String out = target;
  for (int p = 0; p < out.length(); p++) {
    char c = out.charAt(p);
    if (c == ' ' || p <= settled) continue;
    out.setCharAt(p, wheel[(p * 7 + row * 11 + bucket * 3) % 36]);
  }
  return out;
}

void drawSncf1990FlipFlapRows() {
  const uint16_t bg = 0x18E3;
  const uint16_t header = 0x0000;
  const uint16_t ink = 0xFEE0;
  const uint16_t line = 0x6B4D;
  unsigned long now = millis();
  int filled = max(1, filledRowCount());
  int cycle = (now / 3000UL) % filled;
  int phase = now % 3000UL;
  gfx->fillRect(0, 0, 240, 135, bg);
  gfx->fillRect(5, 5, 230, 124, header);
  gfx->drawRect(5, 5, 230, 124, line);
  gfx->setTextSize(1);
  gfx->setTextColor(ink, header);
  gfx->setCursor(sx(10), sy(11));
  gfx->print("SNCF  DEPART");
  fixedSmallText(10, 25, ink, "Heure");
  fixedSmallText(65, 25, ink, "Destination");

  int rowsToDraw = constrain(nbVisible, 1, 7);
  int rowTop = 38;
  int rowH = max(12, (125 - rowTop) / rowsToDraw);
  int lift = phase > 2480 ? map(phase, 2480, 2999, 0, rowH) : 0;
  for (int i = -1; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(cycle + i + 1);
    int y = rowTop + i * rowH - lift;
    if (y < rowTop - rowH || y > 130) continue;
    gfx->drawFastHLine(8, y + rowH - 1, 224, line);
    String h = rows[idx].heure;
    h.replace(":", "h");
    h.replace(".", "h");
    if (h.indexOf('h') < 0 && h.length() >= 4) h = h.substring(0, 2) + "h" + h.substring(2);
    String hh = h.length() >= 2 ? h.substring(0, 2) : h;
    String mm = h.length() >= 5 ? h.substring(3, 5) : "";
    drawFlipCellsText(10, y + 1, 2, flipFlapPreviewText(hh, 2, i + 1, phase));
    fixedSmallText(32, y + 4, ink, "h");
    drawFlipCellsText(42, y + 1, 2, flipFlapPreviewText(mm, 2, i + 2, phase));
    drawFlipCellsText(70, y + 1, 17, flipFlapPreviewText(rows[idx].destination, 17, i + 3, phase));
  }
}

bool isItalyProfile() {
  return displayProfile.startsWith("it_");
}

void drawItalyFrame() {
  if (!rowsOnlyPass) gfx->fillScreen(C_BLACK);
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
  int perCol = max(1, (totalToDraw + 2) / 3);
  int rowH = max(10, 104 / perCol);
  for (int p = 0; p < 3; p++) {
    int x = 5 + p * w;
    gfx->drawRect(x, 22, w - 2, 106, C_GRID);
    for (int r = 0; r < perCol; r++) {
      int linear = p * perCol + r;
      if (linear >= totalToDraw) continue;
      int idx = visibleRowIndex(scrollOffset + linear);
      int y = 24 + r * rowH;
      int ty = y + max(1, (rowH - 8) / 2);
      gfx->fillRect(x + 1, y, w - 4, rowH - 2, (r % 2) ? C_BLUE_ROW1 : C_BLUE_ROW2);
      colSmallText(0, x + 3, ty, C_WHITE, rows[idx].heure);
      colSmallText(2, x + 29, ty, C_SNCB_YELLOW, cutText(rows[idx].destination, 6));
      colSmallText(4, x + w - 18, ty, C_WHITE, cutText(rows[idx].voie, 1));
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

  int rowsToDraw = constrain(nbVisible, 1, 8);
  int rowTop = 22;
  int rh = max(10, (128 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = 22 + i * rh;
    int ty = y + max(1, (rh - 8) / 2);
    uint16_t bg = (i % 2 == 0) ? 0x0861 : 0x10A2;
    gfx->fillRect(4, y, 232, rh - 1, bg);
    gfx->drawFastHLine(4, y + rh - 1, 232, 0x4A49);
    gfx->drawFastVLine(38, y, rh - 1, 0x4A49);
    gfx->drawFastVLine(154, y, rh - 1, 0x4A49);
    gfx->drawFastVLine(194, y, rh - 1, 0x4A49);
    gfx->drawFastVLine(221, y, rh - 1, 0x4A49);
    colSmallText(0, 8, ty, C_SNCB_YELLOW, rows[idx].heure);
    colSmallText(2, 45, ty, C_SNCB_YELLOW, cutText(rows[idx].destination, 17));
    colSmallText(3, 160, ty, C_SNCB_YELLOW, cutText(rows[idx].typeTrain, 4));
    colSmallText(4, 201, ty, C_SNCB_YELLOW, cutText(rows[idx].voie, 2));
    if (rows[idx].info.length()) colSmallText(1, 225, ty, C_SNCB_YELLOW, cutText(rows[idx].info, 2));
  }
}

void drawSncbDetailListRows() {
  gfx->fillRect(3, 3, 234, 129, 0x1A30);
  gfx->fillRect(3, 3, 234, 16, C_BLUE_TOP);
  smallText(8, 8, C_WHITE, "17:56");
  smallText(108, 8, C_WHITE, "Depart");
  gfx->drawCircle(225, 10, 6, C_WHITE);
  smallText(222, 7, C_WHITE, "B");
  int rowsToDraw = constrain(nbVisible, 1, 8);
  int y = 21;
  int rowH = (129 - y - 4) / rowsToDraw;
  if (rowH < 12) rowH = 12;
  bool compact = rowsToDraw > 4;
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int h = rowH;
    if (i > 0) gfx->drawFastHLine(6, y - 2, 228, 0xBDF7);
    smallText(7, y + 1, C_WHITE, cutText(rows[idx].heure, 5));
    if (rows[idx].info.startsWith("+")) {
      int alertY = y + (compact ? 7 : 11);
      gfx->fillRect(5, alertY, 24, 8, C_RED);
      gfx->fillTriangle(29, alertY, 35, alertY + 4, 29, alertY + 8, C_RED);
      smallText(10, alertY + 1, C_WHITE, cutText(rows[idx].info, 4));
    }
    if (compact) colSmallText(2, 44, y + 1, C_SNCB_YELLOW, cutText(rows[idx].destination, 21));
    else colMediumText(2, 44, y, C_SNCB_YELLOW, cutText(rows[idx].destination, 15));
    if (!compact) smallText(44, y + 13, C_WHITE, cutText(rows[idx].info.startsWith("+") ? rows[idx].info.substring(4) : rows[idx].info, 42));
    smallText(210, y + 2, C_WHITE, cutText(rows[idx].typeTrain, 3));
    bool voieBox = (!compact && i == 2);
    gfx->fillRect(229, y + 2, 8, 8, voieBox ? C_WHITE : 0x1A30);
    smallText(231, y + 3, voieBox ? C_BLUE_DARK : C_WHITE, cutText(rows[idx].voie, 1));
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
  int totalRows = constrain(nbVisible, 1, 16);
  int perCol = (totalRows + 1) / 2;
  int rowTop = 35;
  int rowH = max(9, (113 - rowTop) / perCol);
  for (int col = 0; col < 2; col++) {
    int x = 8 + col * 116;
    gfx->fillRect(x, 24, colW, 10, blueHeader);
    smallText(x + 2, 26, C_WHITE, "Prochains trains au depart");
    for (int r = 0; r < perCol; r++) {
      int item = col * perCol + r;
      if (item >= totalRows) continue;
      int idx = visibleRowIndex(scrollOffset + item);
      int y = rowTop + r * rowH;
      int ty = y + max(1, (rowH - 8) / 2);
      uint16_t bg = (r % 2) ? blueB : blueA;
      gfx->fillRect(x, y, colW, rowH - 1, bg);
      if (rows[idx].voie.length()) {
        gfx->fillRect(x + 78, ty - 1, 10, 8, 0x4A9F);
        smallText(x + 81, ty, C_WHITE, cutText(rows[idx].voie, 1));
      }
      smallText(x + 2, ty, C_WHITE, cutText(rows[idx].destination, 15));
      smallText(x + 91, ty - 1, C_YELLOW, cutText(rows[idx].heure, 5));
      if (rowH >= 10) smallText(x + 91, ty + 4, C_WHITE, cutText(rows[idx].info, 5));
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
  int totalRows = constrain(nbVisible, 1, 16);
  int perCol = (totalRows + 1) / 2;
  int rowTop = 8;
  int rowH = max(11, (119 - rowTop) / perCol);
  for (int col = 0; col < 2; col++) {
    int x = 5 + col * 118;
    for (int r = 0; r < perCol; r++) {
      int item = col * perCol + r;
      if (item >= totalRows) continue;
      int idx = visibleRowIndex(scrollOffset + item);
      int y = rowTop + r * rowH;
      int ty = y + max(1, (rowH - 10) / 2);
      uint16_t bg = (r % 2) ? blueA : blueB;
      gfx->fillRect(x, y, colW, rowH - 1, bg);
      String dest = rows[idx].destination;
      if (dest.length()) {
        smallText(x + 2, ty + 1, C_YELLOW, dest.substring(0, 1));
        smallText(x + 9, ty + 1, C_WHITE, cutText(dest.substring(1), 13));
      }
      if (rows[idx].heure.length()) {
        smallText(x + 68, ty - 1, C_YELLOW, cutText(rows[idx].heure, 6));
        if (rowH >= 13) smallText(x + 72, ty + 6, C_WHITE, cutText(rows[idx].typeTrain, 5));
        gfx->fillRect(x + 96, ty, 15, 10, cellBlue);
        gfx->drawRect(x + 96, ty, 15, 10, 0x8E7F);
        smallText(x + 101, ty + 2, C_WHITE, cutText(rows[idx].voie, 2));
      } else if (rows[idx].info.length()) {
        smallText(x + 67, ty - 1, C_YELLOW, "train a");
        if (rowH >= 13) smallText(x + 67, ty + 5, C_YELLOW, "l'approche");
        gfx->fillRect(x + 96, ty, 15, 10, cellBlue);
        gfx->drawRect(x + 96, ty, 15, 10, 0x8E7F);
        smallText(x + 101, ty + 2, C_WHITE, cutText(rows[idx].voie, 2));
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

void drawRerD8090Rows() {
  const uint16_t bg = 0x2945;
  const uint16_t dark = 0x2104;
  const uint16_t line = 0x6B4D;
  const uint16_t dim = 0xB596;
  const uint16_t lampOff = 0x4A49;
  const uint16_t lampOn = 0xEFFF;
  const uint16_t rerGreen = 0x03E0;
  gfx->fillRect(0, 0, 240, 135, 0x18E3);
  gfx->fillRect(3, 3, 234, 129, bg);
  gfx->drawRect(5, 5, 230, 125, line);
  gfx->fillRect(8, 8, 224, 25, dark);
  gfx->drawRect(8, 8, 224, 25, dim);
  gfx->fillCircle(23, 20, 11, rerGreen);
  mediumText(17, 12, C_WHITE, "D");
  fixedSmallText(42, 12, C_WHITE, "RER D");
  fixedSmallText(42, 22, dim, "GARE DE LYON  VOIE A");
  fixedSmallText(177, 12, dim, "TRAIN LONG");
  fixedSmallText(199, 22, C_YELLOW, "032");

  gfx->fillRect(14, 42, 34, 75, dark);
  gfx->drawRect(14, 42, 34, 75, dim);
  fixedSmallText(21, 47, dim, "VOIE");
  mediumText(25, 59, C_WHITE, "A");
  fixedSmallText(18, 82, dim, "Direction");
  fixedSmallText(20, 94, C_YELLOW, "Melun");

  int lineX = 68;
  gfx->drawFastVLine(lineX, 44, 77, rerGreen);
  gfx->drawFastVLine(lineX + 1, 44, 77, rerGreen);
  int rowsToDraw = constrain(nbVisible, 1, 8);
  int rowTop = 43;
  int rowH = max(9, (121 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    uint16_t lc = (i == 0 || (i + scrollOffset) % 3 == 0) ? lampOn : lampOff;
    gfx->fillCircle(lineX, y + rowH / 2, 4, lc);
    gfx->drawCircle(lineX, y + rowH / 2, 4, rerGreen);
    gfx->fillRect(82, y, 143, rowH - 1, (i % 2) ? 0x2104 : 0x3186);
    gfx->drawFastHLine(82, y + rowH - 1, 143, line);
    fixedSmallText(86, ty, C_WHITE, cutText(rows[idx].destination, 17));
    fixedSmallText(190, ty, C_YELLOW, cutText(rows[idx].heure, 5));
  }
  gfx->fillRect(14, 122, 211, 7, dark);
  fixedSmallText(18, 122, dim, "Ce train dessert uniquement les gares allumees");
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
  int rowsToDraw = constrain(nbVisible, 1, 5);
  int rowTop = 43;
  int rowH = max(11, (94 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    gfx->fillRect(10, y, 213, rowH - 2, blue);
    smallText(12, ty, C_WHITE, cutText(rows[idx].typeTrain, 4));
    smallText(50, ty, C_WHITE, cutText(rows[idx].destination, 13));
    smallText(151, ty, C_WHITE, cutText(rows[idx].heure, 5));
    smallText(224, ty, C_WHITE, cutText(rows[idx].voie, 5));
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
  int rowsToDraw = constrain(nbVisible, 1, 6);
  int rowTop = 18;
  int rowH = max(9, (77 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
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
  int rowsToDraw = constrain(nbVisible, 1, 7);
  int rowTop = 36;
  int rowH = max(9, (107 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(0, (rowH - 8) / 2);
    gfx->fillRect(26, y, 183, rowH - 1, greens[i % 2]);
    gfx->fillRect(29, ty, 18, 7, C_WHITE);
    smallText(31, ty + 1, 0x0344, cutText(rows[idx].typeTrain, 3));
    smallText(50, ty, C_WHITE, cutText(rows[idx].info, 6));
    smallText(76, ty, C_YELLOW, cutText(rows[idx].heure, 5));
    smallText(111, ty, C_WHITE, cutText(rows[idx].destination, 15));
    if (rows[idx].voie.length()) {
      gfx->fillRect(197, y, 12, rowH - 1, 0x0B5F);
      smallText(201, ty, C_WHITE, cutText(rows[idx].voie, 1));
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
  if (!rowsOnlyPass) gfx->fillScreen(bgNight);
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
  int totalRows = constrain(nbVisible, 1, 10);
  int pairCount = (totalRows + 1) / 2;
  int rowTop = 44;
  int pairH = max(16, (132 - rowTop) / pairCount);
  for (int i = 0; i < pairCount; i++) {
    int idx = visibleRowIndex(scrollOffset + i * 2);
    int idx2 = visibleRowIndex(scrollOffset + i * 2 + 1);
    int y = rowTop + i * pairH;
    int row1Y = y + max(1, (pairH / 2 - 8) / 2);
    int row2Y = y + pairH / 2 + max(1, (pairH / 2 - 8) / 2);
    gfx->fillRect(0, y, 240, pairH - 1, rowBlue);
    gfx->drawFastHLine(0, y + pairH - 1, 240, gridDark);
    gfx->drawFastVLine(40, y, pairH - 1, gridDark);
    gfx->drawFastVLine(78, y, pairH - 1, gridDark);
    gfx->drawFastVLine(122, y, pairH - 1, gridDark);
    gfx->drawFastVLine(206, y, pairH - 1, gridDark);

    colSmallText(0, 5, row1Y, C_WHITE, cutText(rows[idx].heure, 5));
    gfx->fillRect(43, row1Y + 1, 27, 6, C_WHITE);
    tinyDbText(46, row1Y + 2, 0x4A5F, C_WHITE, rows[idx].typeTrain, 6);
    colSmallText(1, 81, row1Y, skyText, marqueeSlice(rows[idx].info, 6, 320));
    colSmallText(2, 127, row1Y, C_WHITE, cutText(rows[idx].destination, 12));
    String voie1 = cutText(rows[idx].voie, 4);
    int voieX1 = 206 + max(0, (34 - (int)voie1.length() * 6) / 2);
    colSmallText(4, voieX1, row1Y, C_WHITE, voie1);

    if (i * 2 + 1 < totalRows) {
      colSmallText(0, 5, row2Y, C_WHITE, cutText(rows[idx2].heure, 5));
      gfx->fillRect(43, row2Y + 1, 27, 6, C_WHITE);
      tinyDbText(46, row2Y + 2, 0x4A5F, C_WHITE, rows[idx2].typeTrain, 6);
      colSmallText(1, 81, row2Y, skyText, marqueeSlice(rows[idx2].info, 6, 320));
      colSmallText(2, 127, row2Y, C_WHITE, cutText(rows[idx2].destination, 12));
      String voie2 = cutText(rows[idx2].voie, 4);
      int voieX2 = 206 + max(0, (34 - (int)voie2.length() * 6) / 2);
      colSmallText(4, voieX2, row2Y, C_WHITE, voie2);
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
  if (rowsOnlyPass) {
    gfx->fillRect(sx(8), sy(99), 225, 31, panel);
    int rowsDrawn = min(nbVisible, 3);
    for (int r = 0; r < rowsDrawn; r++) {
      int idx = visibleRowIndex(scrollOffset + r);
      int y = 100 + r * 10;
      smallText(12, y + 1, pale, cutText(rows[idx].heure, 5));
      smallText(54, y, pale, cutText(rows[idx].typeTrain, 7));
      smallText(96, y, C_WHITE, cutText(rows[idx].destination, 20));
      smallText(218, y, C_WHITE, cutText(rows[idx].voie, 2));
    }
    return;
  }
  int clockMinutes = (dbIntercityClockHour * 60 + dbIntercityClockMinute + (int)(millis() / 60000UL)) % 1440;
  int mainIdx = visibleRowIndex(scrollOffset);
  int bestDelta = 1440;
  if (!rowsOnlyPass && scrollOffset == 0) {
    for (int i = 0; i < MAX_ROWS; i++) {
      if (!rowIsFilled(i) || rows[i].heure.length() < 4) continue;
      int hh = rows[i].heure.substring(0, 2).toInt();
      int mm = rows[i].heure.substring(3, 5).toInt();
      int delta = (hh * 60 + mm - clockMinutes + 1440) % 1440;
      if (delta < bestDelta) { bestDelta = delta; mainIdx = i; }
    }
  }
  if (!rowsOnlyPass) {
    gfx->fillScreen(bg);
    gfx->fillRect(0, 0, 240, 132, panel);
    gfx->drawRect(0, 0, 240, 132, line);
    drawDbIntercityMarquee(panel, line);
  }
  gfx->fillRect(sx(8), sy(22), 225, 55, panel);
  gfx->fillRect(sx(8), sy(99), 225, 31, panel);
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
    int idx = visibleRowIndex(scrollOffset + r);
    int y = 100 + r * 10;
    smallText(12, y + 1, pale, cutText(rows[idx].heure, 5));
    smallText(54, y, pale, cutText(rows[idx].typeTrain, 7));
    smallText(96, y, C_WHITE, cutText(rows[idx].destination, 20));
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
  int rowsToDraw = constrain(nbVisible, 1, MAX_ROWS);
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
    clippedCellText(2, 38, y + 1, 62, rowH - 3, rowTextSize, C_WHITE, dbBlue, rows[idx].typeTrain + " " + rows[idx].destination);
    String via = rows[idx].info;
    int sep = via.indexOf("|");
    if (sep >= 0) {
      via = via.substring(0, sep);
      via.trim();
    }
    scrollingCellText(1, 104, y + 1, 101, rowH - 3, rowTextSize, C_WHITE, dbBlue, via, 360);
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
  int rowsToDraw = constrain(nbVisible, 1, MAX_ROWS);
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
  gfx->fillRoundRect(194, 11, 36, 14, 3, swBlue);
  fixedSmallText(198, 15, C_WHITE, clock);
}

void drawBadenBadenRows() {
  uint16_t blueA = 0x357F;
  uint16_t blueB = 0x253F;
  uint16_t line = 0xBDF7;
  gfx->fillRect(3, 3, 234, 129, blueA);
  drawBadenBadenHeader();

  int rowsToDraw = constrain(nbVisible, 1, 6);
  int rowTop = 32;
  int rowH = max(12, (129 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(2, (rowH - 8) / 2);
    gfx->fillRect(4, y, 232, rowH - 1, (i % 2) ? blueB : blueA);
    gfx->drawFastHLine(4, y + rowH - 1, 232, line);
    smallText(10, ty, C_WHITE, cutText(rows[idx].typeTrain, 5));
    smallText(54, ty, C_WHITE, cutText(rows[idx].destination, 23));
    smallText(197, ty, C_WHITE, cutText(rows[idx].heure, 6));
  }
}
void drawNsLightModernRows() {
  uint16_t blue = 0x21B5;
  uint16_t blue2 = 0x31F7;
  uint16_t paleA = 0xE73C;
  uint16_t paleB = 0xCE79;
  uint16_t grid = 0xAD75;
  uint16_t ink = 0x18E3;
  gfx->fillScreen(0x0841);
  gfx->fillRect(3, 3, 234, 129, 0x0841);
  gfx->fillRect(4, 6, 232, 18, blue);
  gfx->fillRect(4, 24, 232, 84, paleA);
  fixedSmallText(9, 11, C_WHITE, "09:56");
  fixedSmallText(55, 11, C_WHITE, "Vertrek van de treinen");
  gfx->fillRect(4, 27, 232, 11, 0xD69A);
  smallText(8, 30, ink, "Vertrek");
  smallText(43, 30, ink, "Naar");
  smallText(122, 30, ink, "Trein");
  smallText(202, 30, ink, "Spoor");
  int rowsToDraw = constrain(nbVisible, 1, 5);
  int rowTop = 39;
  int rowH = max(10, (108 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    gfx->fillRect(4, y, 232, rowH - 1, (i % 2) ? paleB : paleA);
    gfx->drawFastHLine(4, y + rowH - 1, 232, grid);
    smallText(8, ty, ink, cutText(rows[idx].heure, 5));
    smallText(43, ty, ink, cutText(rows[idx].destination, 12));
    smallText(122, ty, ink, cutText(rows[idx].typeTrain, 9));
    gfx->fillRect(202, ty - 1, 11, 9, C_WHITE);
    gfx->drawRect(202, ty - 1, 11, 9, grid);
    smallText(206, ty, ink, cutText(rows[idx].voie, 2));
    smallText(216, ty - 1, ink, cutText(rows[idx].typeTrain, 3));
  }
  gfx->fillRect(4, 109, 232, 21, 0xE73C);
  gfx->drawFastHLine(4, 109, 232, grid);
  gfx->fillCircle(15, 119, 8, blue);
  smallText(12, 115, C_WHITE, "i");
  smallText(29, 112, ink, cutText(transilienInfoTitle, 20));
  smallText(29, 121, ink, cutText(transilienInfoText, 38));
  smallText(219, 116, ink, "2/2");
}

void drawNs2010PhotoRows() {
  const uint16_t frame = 0x001F;
  const uint16_t page = 0xD71C;
  const uint16_t head = 0xCE79;
  const uint16_t rowA = 0xD69A;
  const uint16_t rowB = 0xC5F7;
  const uint16_t info = 0xFF60;
  const uint16_t ink = 0x0841;
  const uint16_t blue = 0x039F;
  gfx->fillScreen(frame);
  gfx->fillRect(0, 0, 240, 135, frame);
  gfx->fillRect(14, 10, 212, 113, page);
  gfx->drawRect(14, 10, 212, 113, 0x39E7);
  gfx->fillRect(17, 13, 206, 11, head);
  fixedSmallText(19, 15, ink, "Vertrek");
  fixedSmallText(50, 15, ink, "Naar");
  fixedSmallText(148, 15, ink, "Spoor");
  fixedSmallText(181, 15, ink, "Trein");
  gfx->fillRoundRect(205, 13, 17, 9, 2, 0x0010);
  fixedSmallText(209, 15, C_WHITE, "13:17");

  int rowsToDraw = constrain(nbVisible, 1, 5);
  int rowTop = 27;
  int rowH = max(10, (91 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    gfx->fillRect(17, y, 206, rowH - 1, (i % 2) ? rowB : rowA);
    fixedSmallText(20, ty, blue, cutText(rows[idx].heure, 5));
    fixedSmallText(50, ty - 1, ink, cutText(rows[idx].destination, 15));
    if (rowH >= 13) fixedSmallText(50, ty + 6, ink, cutText(rows[idx].info, 18));
    gfx->fillRect(148, ty - 2, 15, 10, C_WHITE);
    gfx->drawRect(148, ty - 2, 15, 10, blue);
    fixedSmallText(153, ty, ink, cutText(rows[idx].voie, 2));
    fixedSmallText(177, ty, ink, cutText(rows[idx].typeTrain, 11));
  }
  gfx->fillRect(17, 94, 206, 25, info);
  fixedSmallText(20, 98, ink, "Eindhoven - Heerlen");
  fixedSmallText(20, 107, ink, "... Er rijden stopbussen tussen Eindhoven, Geldrop, Heeze");
  fixedSmallText(208, 98, ink, "4/5");
}
void drawNsDarkModernRows() {
  uint16_t bg = 0x0011;
  uint16_t head = 0x001F;
  uint16_t rowA = 0x0137;
  uint16_t rowB = 0x00B2;
  uint16_t yellow = 0xFFE0;
  gfx->fillScreen(bg);
  gfx->fillRect(3, 3, 234, 129, bg);
  gfx->fillRect(4, 4, 232, 18, head);
  mediumText(8, 8, yellow, "Vertrektijden");
  smallText(167, 9, C_WHITE, "NS");
  smallText(8, 27, C_WHITE, "Tijd");
  smallText(43, 27, C_WHITE, "Naar");
  smallText(146, 27, C_WHITE, "Trein");
  smallText(205, 27, C_WHITE, "Spoor");
  int rowsToDraw = constrain(nbVisible, 1, 6);
  int rowTop = 38;
  int rowH = max(10, (114 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    uint16_t rowBg = (i % 2) ? rowB : rowA;
    gfx->fillRect(5, y, 230, rowH - 1, rowBg);
    int ty = y + max(1, (rowH - 8) / 2);
    colSmallText(0, 8, ty, yellow, cutText(rows[idx].heure, 5));
    colSmallText(2, 43, ty, C_WHITE, cutText(rows[idx].destination, 16));
    colSmallText(3, 146, ty, C_WHITE, cutText(rows[idx].typeTrain, 8));
    gfx->fillRect(205, ty - 1, 24, 10, C_WHITE);
    colSmallText(4, 211, ty + 1, C_BLACK, cutText(rows[idx].voie, 3));
  }
  gfx->fillRect(5, 115, 230, 14, yellow);
  smallText(8, 118, C_BLACK, cutText(transilienInfoTitle, 22));
  smallText(8, 126, C_BLACK, cutText(transilienInfoText, 37));
}

void drawNsBlueClassicRows() {
  const uint16_t bg = C_BLUE_DARK;
  const uint16_t head = 0x001F;
  const uint16_t rowA = 0x001F;
  const uint16_t rowB = 0x085F;
  const uint16_t yellow = 0xDE80;
  const uint16_t grid = C_GRID;
  if (!rowsOnlyPass) {
    gfx->fillScreen(bg);
    gfx->fillRect(3, 3, 234, 129, bg);
    fixedSmallText(8, 8, C_WHITE, "NS Vertrektijden");
    fixedSmallText(8, 25, C_WHITE, "Tijd");
    fixedSmallText(43, 25, C_WHITE, "Naar");
    fixedSmallText(146, 25, C_WHITE, "Trein");
    fixedSmallText(205, 25, C_WHITE, "Spoor");
  }
  int rowsToDraw = constrain(nbVisible, 1, 6);
  int rowTop = 36;
  int rowH = max(10, (116 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    uint16_t rb = (i % 2) ? rowB : rowA;
    gfx->fillRect(5, y, 230, rowH - 1, rb);
    gfx->drawFastHLine(5, y + rowH - 1, 230, grid);
    fixedSmallText(8, ty, yellow, cutText(rows[idx].heure, 5));
    fixedSmallText(43, ty, C_WHITE, cutText(rows[idx].destination, 16));
    fixedSmallText(146, ty, C_WHITE, cutText(rows[idx].typeTrain, 8));
    gfx->fillRect(205, ty - 1, 24, 10, C_WHITE);
    gfx->drawRect(205, ty - 1, 24, 10, grid);
    fixedSmallText(211, ty + 1, C_BLACK, cutText(rows[idx].voie, 3));
  }
  gfx->fillRect(5, 118, 230, 11, yellow);
  fixedSmallText(8, 120, C_BLACK, cutText(transilienInfoText.length() ? transilienInfoText : String("Let op: gewijzigde dienstregeling"), 36));
}
void drawUkModernRows() {
  gfx->fillRect(4, 25, 232, 106, C_BLACK);
  int cols = constrain(nbVisible, 1, 4);
  int colW = 232 / cols;
  for (int c = 0; c < cols; c++) {
    int x = 5 + c * colW;
    int idx = visibleRowIndex(scrollOffset + c);
    int w = colW - 2;
    gfx->fillRect(x, 26, w, 25, c < 2 ? 0x3666 : 0x255F);
    colSmallText(0, x + 3, 29, C_WHITE, rows[idx].heure);
    colSmallText(2, x + 3, 40, C_WHITE, cutText(rows[idx].destination, 7));
    gfx->drawRect(x, 52, w, 76, C_GREY);
    for (int r = 0; r < 5; r++) smallText(x + 4, 56 + r * 12, C_WHITE, cutText(rows[visibleRowIndex(scrollOffset + c + r)].destination, max(5, w / 6 - 1)));
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
  int rowsToDraw = constrain(nbVisible, 1, 8);
  int rowTop = 22;
  int rowH = max(9, (113 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    uint16_t bg = (i % 2 == 0) ? 0x001F : 0x085F;
    gfx->fillRect(4, y, 232, rowH - 1, bg);
    gfx->drawFastHLine(4, y + rowH - 1, 232, C_GRID);
    if (rows[idx].typeTrain.length()) {
      gfx->fillRect(6, ty - 1, 24, 8, (rows[idx].typeTrain.startsWith("IR") || rows[idx].typeTrain.startsWith("IC")) ? C_RED : C_BLUE_ROW2);
      colSmallText(3, 8, ty, C_WHITE, cutText(rows[idx].typeTrain, 3));
    }
    colSmallText(0, 41, ty, C_WHITE, rows[idx].heure);
    colSmallText(2, 72, ty, C_WHITE, cutText(rows[idx].destination, 15));
    colSmallText(4, 180, ty, C_WHITE, cutText(rows[idx].voie, 2));
    if (rows[idx].info.length()) colSmallText(1, 205, ty, C_WHITE, cutText(rows[idx].info, 4));
  }
  gfx->fillRect(4, 114, 232, 18, C_RED);
  String msg = swissInfoMessage.length() ? swissInfoMessage : String("Info CFF/SBB: verifiez l'horaire avant votre voyage");
  String shown = msg.length() > 37 ? marqueeWindow(msg + "   ", 37, 4) : msg;
  fixedSmallText(7, 120, C_WHITE, cutText(shown, 37));
}

void drawSwissBernArrivalRows() {
  uint16_t paper = 0xEF7D;
  uint16_t table = 0xE71C;
  uint16_t grid = 0x9CD3;
  uint16_t ink = C_BLACK;
  gfx->fillRect(0, 0, 240, 135, table);
  gfx->drawRect(1, 2, 238, 131, ink);
  gfx->fillRect(8, 7, 17, 18, 0xD6BA);
  gfx->drawRect(8, 7, 17, 18, ink);
  gfx->drawFastHLine(11, 17, 11, ink);
  gfx->drawFastVLine(14, 10, 11, ink);
  gfx->drawFastVLine(20, 10, 11, ink);
  gfx->fillRect(12, 10, 9, 5, ink);
  gfx->fillCircle(14, 22, 2, ink);
  gfx->fillCircle(20, 22, 2, ink);
  fixedSmallText(35, 9, ink, "Ankunft");
  fixedSmallText(92, 9, ink, "Arrivee");
  fixedSmallText(148, 9, ink, "Arrivo");
  gfx->fillRect(2, 27, 236, 104, paper);
  gfx->drawRect(2, 27, 236, 104, ink);
  gfx->fillRect(3, 28, 234, 12, 0xD6BA);
  fixedSmallText(48, 31, ink, "Von");
  fixedSmallText(166, 31, ink, "Gleis");
  fixedSmallText(203, 31, ink, "Hinw.");
  gfx->drawFastVLine(38, 28, 102, grid);
  gfx->drawFastVLine(163, 28, 102, grid);
  gfx->drawFastVLine(201, 28, 102, grid);
  int rowsToDraw = constrain(nbVisible, 1, 8);
  int rowTop = 42;
  int rowH = max(10, (130 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    gfx->drawFastHLine(3, y + rowH - 1, 234, grid);
    String train = rows[idx].typeTrain;
    uint16_t tag = train.startsWith("IC") || train.startsWith("IR") || train == "RE" ? C_RED : (train.startsWith("S") ? 0x02DF : C_WHITE);
    if (train.length()) {
      gfx->fillRect(7, ty - 1, 21, 8, tag);
      fixedSmallText(9, ty, tag == C_WHITE ? ink : C_WHITE, cutText(train, 3));
    }
    fixedSmallText(41, ty, ink, cutText(rows[idx].heure, 5));
    fixedSmallText(72, ty, ink, cutText(rows[idx].destination, 16));
    fixedSmallText(169, ty, ink, cutText(rows[idx].voie, 5));
    if (rows[idx].info.length()) fixedSmallText(204, ty, ink, cutText(rows[idx].info, 5));
  }
}

void drawSwissRomandieRows() {
  gfx->fillRect(3, 3, 234, 129, 0xFDA0);
  gfx->drawRect(9, 12, 25, 18, C_BLACK);
  fixedSmallText(12, 16, C_BLACK, "SBB");
  fixedSmallText(42, 18, C_BLACK, "Abfahrt");
  fixedSmallText(94, 18, C_BLACK, "Depart");
  fixedSmallText(142, 18, C_BLACK, "Partenza");
  gfx->fillRect(9, 38, 222, 72, 0x001F);
  gfx->drawRect(9, 38, 222, 72, C_BLACK);
  fixedSmallText(62, 40, C_WHITE, "Destination");
  fixedSmallText(166, 40, C_WHITE, "Voie");
  fixedSmallText(194, 40, C_WHITE, "Rem.");
  int rowsToDraw = constrain(nbVisible, 1, 8);
  int rowTop = 48;
  int rowH = max(7, (109 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(0, (rowH - 7) / 2);
    gfx->drawFastHLine(10, y + rowH - 1, 220, 0x7BFF);
    uint16_t tagBg = rows[idx].typeTrain.startsWith("T") ? C_RED : (rows[idx].typeTrain.startsWith("R") ? C_WHITE : C_YELLOW);
    gfx->fillRect(12, ty, 17, 7, tagBg);
    fixedSmallText(13, ty, tagBg == C_WHITE ? C_BLACK : C_WHITE, cutText(rows[idx].typeTrain, 3));
    fixedSmallText(34, ty, C_WHITE, cutText(rows[idx].heure, 5));
    fixedSmallText(67, ty, C_WHITE, cutText(rows[idx].destination, 16));
    fixedSmallText(171, ty, C_WHITE, cutText(rows[idx].voie, 2));
    if (rows[idx].info.length()) fixedSmallText(189, ty, C_WHITE, cutText(rows[idx].info, 7));
  }
  gfx->fillRect(9, 110, 222, 18, C_RED);
  String msg = swissInfoMessage.length() ? swissInfoMessage : String("Horaire modifie. Consultez l'affichage en gare avant votre voyage.");
  String shown = msg.length() > 35 ? marqueeWindow(msg + "   ", 35, 4) : msg;
  fixedSmallText(12, 116, C_WHITE, cutText(shown, 35));
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
  int maxRows = constrain(nbVisible, 1, 7);
  int rowTop = 37;
  int rowH = max(10, (129 - rowTop) / maxRows);
  for (int i = 0; i < maxRows; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    gfx->fillRect(5, y, 230, rowH - 1, (i % 2) ? 0x02DF : 0x037F);
    colSmallText(0, 8, ty, C_WHITE, rows[idx].heure);
    colSmallText(3, 46, ty, C_WHITE, cutText(rows[idx].typeTrain, 4));
    colSmallText(2, 87, ty, C_WHITE, cutText(rows[idx].destination, 15));
    gfx->fillRect(205, ty - 1, 26, 10, C_WHITE);
    colSmallText(4, 213, ty + 1, C_BLACK, cutText(rows[idx].voie, 2));
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
  int maxRows = constrain(nbVisible, 1, 7);
  int rowTop = 41;
  int rowH = max(10, (129 - rowTop) / maxRows);
  for (int i = 0; i < maxRows; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    gfx->fillRect(5, y, 230, rowH - 1, (i % 2) ? 0x081F : 0x001F);
    gfx->drawFastHLine(5, y + rowH - 1, 230, 0x4A7F);
    colSmallText(0, 8, ty, C_WHITE, rows[idx].heure);
    colSmallText(3, 44, ty, C_WHITE, cutText(rows[idx].typeTrain, 4));
    colSmallText(2, 82, ty, C_WHITE, cutText(rows[idx].destination, 17));
    colSmallText(4, 213, ty, C_YELLOW, cutText(rows[idx].voie, 2));
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
  int rowsToDraw = constrain(nbVisible, 1, 6);
  int rowTop = 36;
  int rowH = max(12, (128 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    gfx->fillRect(6, y, 226, rowH - 2, C_BLACK);
    gfx->drawRect(6, y, 34, rowH - 2, C_GREY);
    gfx->drawRect(42, y, 124, rowH - 2, C_GREY);
    gfx->drawRect(168, y, 30, rowH - 2, C_GREY);
    gfx->drawRect(200, y, 32, rowH - 2, C_GREY);
    gfx->drawFastHLine(7, y + (rowH - 2) / 2, 32, 0x4208);
    gfx->drawFastHLine(43, y + (rowH - 2) / 2, 122, 0x4208);
    colSmallText(0, 9, ty, C_WHITE, rows[idx].heure);
    colSmallText(2, 47, ty, C_WHITE, cutText(rows[idx].destination, 16));
    colSmallText(4, 176, ty, C_WHITE, cutText(rows[idx].voie, 2));
    smallText(203, ty, C_WHITE, rows[idx].info.length() ? cutText(rows[idx].info, 4) : "ON");
  }
}


void drawOebbGreenPhotoRows() {
  gfx->fillRect(3, 3, 234, 129, 0x0140);
  gfx->fillRect(4, 4, 232, 21, 0x0220);
  smallText(12, 9, C_YELLOW, "Ankunft");
  pseudoItalicText(76, 9, 1, C_YELLOW, 0x0220, "Arrival");
  smallText(145, 9, C_WHITE, "11:13:56");
  drawOebbLogo(202, 7, C_WHITE, 0x0220);
  tinyDbCellText(203, 16, 22, C_WHITE, 0x0220, "INFRA");
  smallText(14, 27, C_WHITE, "Zeit");
  smallText(43, 27, C_WHITE, "Erw.");
  smallText(91, 27, C_WHITE, "Zug");
  smallText(119, 27, C_WHITE, "Von");
  smallText(205, 27, C_YELLOW, "Bstg");
  int rowsToDraw = constrain(nbVisible, 1, MAX_ROWS);
  int rowTop = 37;
  int rowH = max(5, (129 - rowTop) / max(1, rowsToDraw));
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
}

void drawOebbBluePhotoRows() {
  gfx->fillRect(3, 3, 234, 129, 0x0016);
  gfx->fillRect(5, 5, 230, 20, 0x001F);
  smallText(14, 9, C_YELLOW, "Abfahrt");
  pseudoItalicText(75, 9, 1, C_YELLOW, 0x001F, "Departure");
  smallText(145, 9, C_WHITE, "11:13:56");
  drawOebbLogo(202, 7, C_WHITE, 0x001F);
  tinyDbCellText(203, 16, 22, C_WHITE, 0x001F, "INFRA");
  smallText(14, 27, C_WHITE, "Zeit");
  smallText(43, 27, C_WHITE, "Erw.");
  smallText(91, 27, C_WHITE, "Zug");
  smallText(119, 27, C_WHITE, "Nach");
  smallText(207, 27, C_YELLOW, "Bstg");
  int rowsToDraw = constrain(nbVisible, 1, MAX_ROWS);
  int rowTop = 37;
  int rowH = max(5, (129 - rowTop) / max(1, rowsToDraw));
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
  int rowH = max(6, (129 - rowTop) / max(1, rowsToDraw));
  bool bigRows = rowH >= 17;
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(0, (rowH - 8) / 2);
    gfx->fillRect(5, y, 230, rowH - 1, (i % 2) ? 0x02DF : 0x021F);
    smallText(8, ty, C_WHITE, rows[idx].heure);
    smallText(49, ty, C_WHITE, cutText(rows[idx].typeTrain, 8));
    smallText(101, ty, C_WHITE, cutText(rows[idx].destination, bigRows ? 14 : 16));
    smallText(211, ty, C_WHITE, cutText(rows[idx].voie, 2));
  }
}

void drawOebbBlueDenseRows() {
  gfx->fillRect(3, 3, 234, 129, 0x0016);
  gfx->fillRect(5, 5, 230, 20, 0x001F);
  smallText(14, 10, C_YELLOW, "Abfahrt");
  smallText(72, 10, C_YELLOW, "Departure");
  smallText(145, 10, C_WHITE, "15:08:52");
  drawOebbLogo(202, 8, C_WHITE, 0x001F);
  tinyDbCellText(203, 16, 22, C_WHITE, 0x001F, "INFRA");
  smallText(8, 27, C_WHITE, "Zeit");
  smallText(39, 27, C_WHITE, "Zug");
  smallText(72, 27, C_WHITE, "Nach");
  smallText(142, 27, C_WHITE, "Via");
  smallText(205, 27, C_WHITE, "Bstg");
  int rowsToDraw = constrain(nbVisible, 1, MAX_ROWS);
  int rowTop = 37;
  int rowH = max(5, (129 - rowTop) / max(1, rowsToDraw));
  bool bigRows = rowH >= 16;
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(0, (rowH - 8) / 2);
    gfx->fillRect(5, y, 229, rowH - 1, (i % 2) ? 0x045F : 0x0195);
    if (bigRows) smallText(8, ty, C_WHITE, cutText(rows[idx].heure, 5)); else tinyDbCellText(8, ty + 1, 24, C_WHITE, (i % 2) ? 0x045F : 0x0195, rows[idx].heure);
    smallText(39, ty, C_WHITE, cutText(rows[idx].typeTrain, 5));
    if (bigRows) colMediumText(2, 72, y + 1, C_WHITE, cutText(rows[idx].destination, 8)); else smallText(72, ty, C_WHITE, cutText(rows[idx].destination, 11));
    smallText(142, ty, 0xDFF7, cutText(rows[idx].info, 11));
    smallText(207, ty, C_WHITE, cutText(rows[idx].voie, 4));
  }
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
  int rowH = max(6, (129 - rowTop) / max(1, rowsToDraw));
  bool bigRows = rowH >= 16;
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(0, (rowH - 8) / 2);
    gfx->fillRect(5, y, 230, rowH - 1, (i % 2) ? 0x02A9 : 0x0187);
    smallText(8, ty, C_WHITE, cutText(rows[idx].heure, 5));
    smallText(42, ty, C_WHITE, cutText(rows[idx].typeTrain, 7));
    smallText(96, ty, C_WHITE, cutText(rows[idx].destination, bigRows ? 15 : 16));
    smallText(210, ty, C_WHITE, cutText(rows[idx].voie, 3));
  }
}

void drawZurichFernverkehrRows() {
  gfx->fillRect(3, 3, 234, 129, C_BLUE_DARK);
  gfx->fillRect(5, 7, 226, 13, C_RED);
  smallText(12, 10, C_WHITE, "Fernverkehr");
  int rowsToDraw = constrain(nbVisible, 1, 10);
  int rowTop = 23;
  int rowH = max(10, (127 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    gfx->drawFastHLine(5, y + rowH - 2, 226, 0x7BFF);
    smallText(8, ty, C_RED, cutText(rows[idx].typeTrain, 5));
    smallText(40, ty, C_WHITE, cutText(rows[idx].heure, 5));
    smallText(74, ty, C_WHITE, cutText(rows[idx].destination, 20));
    smallText(208, ty, C_YELLOW, cutText(rows[idx].voie, 2));
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
  int rowsToDraw = constrain(nbVisible, 1, 5);
  int rowTop = 38;
  int rowH = max(13, (116 - rowTop) / rowsToDraw);
  bool bigRows = rowH >= 17;
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 10) / 2);
    uint16_t trainColor = rows[idx].typeTrain.startsWith("HIKARI") ? C_RED : (rows[idx].typeTrain.startsWith("KODAMA") ? 0x041F : C_AMBER);
    fixedSmallText(9, ty, trainColor, cutText(rows[idx].typeTrain, 10));
    fixedSmallText(72, ty + 1, C_WHITE, cutText(rows[idx].heure, 5));
    fixedSmallText(116, ty + 1, C_WHITE, cutText(rows[idx].destination, 14));
    fixedSmallText(190, ty + 1, C_WHITE, cutText(rows[idx].info, 8));
  }
  smallText(50, 121, C_WHITE, "[Car information]  Track 23");
}

void drawTokyoGreyRows() {
  gfx->fillRect(3, 3, 234, 129, 0x8410);
  gfx->fillRect(4, 4, 232, 18, 0x39E7);
  smallText(12, 8, C_WHITE, "Next Departure");
  fixedSmallText(132, 9, C_WHITE, "Track 23");
  smallText(206, 8, C_WHITE, "JR");
  smallText(9, 25, 0xD69A, "Train");
  smallText(56, 25, 0xD69A, "No.");
  smallText(91, 25, 0xD69A, "Time");
  smallText(132, 25, 0xD69A, "Destination");
  smallText(205, 25, 0xD69A, "Cars");
  int rowsToDraw = constrain(nbVisible, 1, 3);
  int rowTop = 38;
  int rowH = max(22, (116 - rowTop) / rowsToDraw);
  bool bigRows = rowH >= 26;
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(2, (rowH - 16) / 2);
    gfx->fillRect(7, y, 226, rowH - 4, (i % 2) ? 0x8C51 : 0x6B4D);
    gfx->drawFastVLine(7, y, rowH - 4, C_YELLOW);
    fixedSmallText(13, y + 4, 0xF7DE, cutText(rows[idx].typeTrain, 10));
    fixedSmallText(84, ty + 3, C_WHITE, cutText(rows[idx].heure, 5));
    fixedSmallText(130, ty + 3, C_WHITE, cutText(rows[idx].destination, 13));
    fixedSmallText(205, y + 4, C_WHITE, cutText(rows[idx].info, 7));
    fixedSmallText(13, y + rowH - 12, 0xE71C, cutText(rows[idx].voie, 18));
  }
  smallText(35, 120, 0xE71C, "for Karuizawa and Nagano");
}

void drawTokyoNaritaRows() {
  const uint16_t bg = C_BLACK;
  const uint16_t rowBg = 0x0008;
  const uint16_t orange = 0xFC60;
  const uint16_t blue = 0x05BF;
  const uint16_t red = 0xF945;
  const uint16_t grey = 0x4A49;
  gfx->fillRect(0, 0, 240, 135, bg);
  gfx->fillRect(3, 3, 234, 129, rowBg);
  int rowsToDraw = constrain(nbVisible, 1, 3);
  int rowH = 40;
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = 6 + i * rowH;
    uint16_t accent = rows[idx].typeTrain == "Skyliner" ? blue : red;
    gfx->drawFastHLine(4, y, 232, orange);
    gfx->drawFastHLine(4, y + rowH - 2, 232, orange);
    gfx->fillRoundRect(13, y + 7, 48, 22, 5, accent);
    fixedSmallText(18, y + 12, C_WHITE, cutText(rows[idx].typeTrain, 8));
    fixedSmallText(69, y + 9, C_WHITE, cutText(rows[idx].heure, 5));
    fixedSmallText(103, y + 9, C_WHITE, "via");
    fixedSmallText(123, y + 5, C_WHITE, cutText(rows[idx].destination, 10));
    fixedSmallText(123, y + 16, C_WHITE, "Narita A.");
    gfx->fillRoundRect(172, y + 7, 44, 23, 3, 0x1082);
    fixedSmallText(176, y + 10, C_WHITE, "Term.2");
    fixedSmallText(194, y + 19, C_WHITE, cutText(rows[idx].info, 5));
    if (rows[idx].voie.length()) {
      gfx->fillRoundRect(221, y + 6, 16, 16, 8, C_WHITE);
      fixedSmallText(225, y + 10, 0x041F, cutText(rows[idx].voie, 2));
    }
  }
}
void drawSaintLazareRows() {
  gfx->fillRect(3, 3, 234, 129, 0x001F);
  mediumText(10, 8, C_WHITE, "Departs Ile-de-France");
  int totalRows = constrain(nbVisible, 1, 16);
  int perCol = (totalRows + 1) / 2;
  int rowTop = 32;
  int rowH = max(9, (129 - rowTop) / perCol);
  for (int col = 0; col < 2; col++) {
    int x = 8 + col * 116;
    for (int r = 0; r < perCol; r++) {
      int item = col * perCol + r;
      if (item >= totalRows) continue;
      int idx = visibleRowIndex(scrollOffset + item);
      int y = rowTop + r * rowH;
      int ty = y + max(1, (rowH - 8) / 2);
      gfx->fillRect(x, y, 110, rowH - 1, (r % 2) ? 0x039F : 0x04BF);
      smallText(x + 2, ty, C_WHITE, cutText(rows[idx].destination, 15));
      smallText(x + 67, ty, C_YELLOW, cutText(rows[idx].heure, 5));
      smallText(x + 92, ty, C_WHITE, cutText(rows[idx].voie, 2));
    }
  }
}

void drawStockholmRows() {
  const uint16_t bg = C_BLACK;
  const uint16_t amber = 0xFDE0;
  const uint16_t blue = 0x0195;
  const uint16_t blue2 = 0x0110;
  const uint16_t green = 0x07E0;
  gfx->fillRect(0, 0, 240, 135, bg);
  fixedSmallText(8, 6, C_WHITE, "Avgaende");
  fixedSmallText(78, 6, amber, "Departures");
  fixedSmallText(8, 22, amber, "Tid");
  fixedSmallText(42, 22, amber, "Destination");
  fixedSmallText(132, 22, amber, "Spar");
  fixedSmallText(170, 22, amber, "Tag");
  int rowsToDraw = constrain(nbVisible, 1, 5);
  int rowTop = 32;
  int rowH = max(9, (128 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(0, (rowH - 8) / 2);
    gfx->fillRect(4, y, 232, rowH - 1, (i % 2) ? blue2 : blue);
    fixedSmallText(8, ty, green, cutText(rows[idx].heure, 5));
    fixedSmallText(42, ty, amber, cutText(rows[idx].destination, 13));
    fixedSmallText(132, ty, C_WHITE, cutText(rows[idx].voie, 3));
    fixedSmallText(170, ty, amber, cutText(rows[idx].typeTrain, 10));
  }
}

void drawMavPhotoRows(bool arrivals) {
  uint16_t bg = arrivals ? 0x0186 : 0x0258;
  uint16_t head = arrivals ? 0x04A8 : 0x041F;
  uint16_t rowA = arrivals ? 0x04A6 : 0x03BF;
  uint16_t rowB = arrivals ? 0x0365 : 0x02DF;
  gfx->fillRect(3, 3, 234, 129, bg);
  gfx->fillRect(4, 4, 232, 19, head);
  fixedSmallText(8, 8, C_WHITE, arrivals ? "12:18" : "12:18:47");
  fixedSmallText(76, 8, C_WHITE, arrivals ? "ERKEZO VONATOK" : "INDULO VONATOK");
  fixedSmallText(184, 8, C_WHITE, arrivals ? "Arrivals" : "Departures");
  fixedSmallText(8, 26, C_WHITE, "Time");
  fixedSmallText(38, 26, C_WHITE, "Train");
  fixedSmallText(72, 26, C_WHITE, arrivals ? "From" : "To");
  fixedSmallText(154, 26, C_WHITE, arrivals ? "Via" : "Through");
  fixedSmallText(219, 26, C_WHITE, "Tr.");
  int rowsToDraw = constrain(nbVisible, 1, 8);
  int rowTop = 35;
  int rowH = max(7, (120 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(0, (rowH - 7) / 2);
    gfx->fillRect(5, y, 228, rowH - 1, (i % 2) ? rowB : rowA);
    smallText(8, ty, C_WHITE, cutText(rows[idx].heure, 5));
    smallText(38, ty, C_WHITE, cutText(rows[idx].typeTrain, 4));
    smallText(72, ty, C_WHITE, cutText(rows[idx].destination, 12));
    smallText(154, ty, C_WHITE, cutText(rows[idx].info, 11));
    smallText(221, ty, C_WHITE, cutText(rows[idx].voie, 2));
  }
  gfx->fillRect(5, 121, 228, 8, arrivals ? 0x04A8 : 0x041F);
  smallText(8, 122, C_WHITE, arrivals ? "Budapest arrivals - MAV START" : "Budapest departures - MAV START");
}

void drawBarcelonaRodaliesGridRows() {
  const uint16_t bg = 0x10A2;
  const uint16_t header = 0x0320;
  const uint16_t cyan = 0x07FF;
  const uint16_t amber = 0xFDA0;
  gfx->fillRect(0, 0, 240, 135, C_BLACK);
  gfx->fillRect(4, 5, 232, 124, bg);
  gfx->fillRect(5, 6, 230, 18, header);
  fixedSmallText(9, 10, C_WHITE, "Salidas");
  fixedSmallText(62, 10, C_WHITE, "Sortides");
  fixedSmallText(130, 10, C_WHITE, "Departures");
  fixedSmallText(8, 28, C_WHITE, "Hora");
  fixedSmallText(42, 28, C_WHITE, "Destino");
  fixedSmallText(154, 28, C_WHITE, "Tren");
  fixedSmallText(190, 28, C_WHITE, "Via");
  int rowsToDraw = constrain(nbVisible, 1, 7);
  int rowTop = 40;
  int rowH = max(11, (116 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    gfx->drawFastHLine(6, y + rowH - 1, 226, 0x4A69);
    fixedSmallText(9, ty, cyan, cutText(rows[idx].heure, 5));
    fixedSmallText(42, ty, cyan, cutText(rows[idx].destination, 18));
    fixedSmallText(154, ty, cyan, cutText(rows[idx].typeTrain, 6));
    fixedSmallText(190, ty, cyan, cutText(rows[idx].voie, 3));
  }
  gfx->fillRect(5, 118, 230, 11, C_BLACK);
  fixedSmallText(10, 120, amber, "AVANT Y LARGA DISTANCIA VENTANILLAS");
}

void drawPolandBlueRows(bool arrivals) {
  const uint16_t bg = 0x1082;
  const uint16_t head = 0x2104;
  const uint16_t rowA = 0x281F;
  const uint16_t rowB = 0x2017;
  const uint16_t violet = 0x681F;
  const uint16_t amber = 0xFDE0;
  const uint16_t pale = 0xBDF7;
  gfx->fillRect(0, 0, 240, 135, bg);
  gfx->fillRect(3, 3, 234, 129, head);
  fixedSmallText(8, 8, amber, "Odjazdy");
  fixedSmallText(72, 11, amber, "Departures");
  gfx->fillCircle(122, 13, 12, 0xC618);
  gfx->drawCircle(122, 13, 12, C_BLACK);
  gfx->drawLine(122, 13, 122, 4, C_BLACK);
  gfx->drawLine(122, 13, 130, 15, C_BLACK);
  fixedSmallText(7, 24, pale, "Czas");
  fixedSmallText(42, 24, pale, "Pociag");
  fixedSmallText(92, 24, pale, "Do");
  fixedSmallText(178, 24, pale, "Peron");
  fixedSmallText(207, 24, pale, "Uwagi");
  int rowsToDraw = constrain(nbVisible, 1, 5);
  int rowTop = 34;
  int rowH = max(11, (127 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    gfx->fillRect(5, y, 230, rowH - 1, (i % 2) ? rowB : rowA);
    gfx->drawFastHLine(5, y + rowH - 1, 230, violet);
    fixedSmallText(8, ty, C_WHITE, cutText(rows[idx].heure, 5));
    fixedSmallText(42, ty, pale, cutText(rows[idx].info, 8));
    fixedSmallText(92, ty, C_WHITE, cutText(rows[idx].destination, 13));
    fixedSmallText(178, ty, C_WHITE, cutText(rows[idx].voie, 3));
    String uwagi = rows[idx].retard.length() ? rows[idx].retard : rows[idx].typeTrain;
    fixedSmallText(207, ty, pale, cutText(uwagi, 5));
  }
}

void drawLosAngelesRows() {
  const uint16_t head = 0x001F;
  const uint16_t rowA = 0x047F;
  const uint16_t rowB = 0x029F;
  const uint16_t grid = 0x0010;
  gfx->fillRect(0, 0, 240, 135, 0x0008);
  gfx->fillRect(4, 4, 232, 124, rowA);
  gfx->fillRect(4, 4, 232, 18, head);
  fixedSmallText(94, 9, C_WHITE, "AMTRAK");
  gfx->fillRect(4, 23, 232, 10, 0xBDF7);
  fixedSmallText(12, 25, 0x2945, "Time");
  fixedSmallText(46, 25, 0x2945, "No.");
  fixedSmallText(76, 25, 0x2945, "Train");
  fixedSmallText(146, 25, 0x2945, "To");
  fixedSmallText(194, 25, 0x2945, "Status");
  int rowsToDraw = constrain(nbVisible, 1, 7);
  int rowTop = 36;
  int rowH = max(12, (114 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    gfx->fillRect(5, y, 230, rowH - 1, (i % 2) ? rowB : rowA);
    gfx->drawFastHLine(5, y + rowH - 1, 230, grid);
    fixedSmallText(12, ty, C_WHITE, cutText(rows[idx].heure, 5));
    fixedSmallText(46, ty, C_WHITE, cutText(rows[idx].info, 4));
    fixedSmallText(76, ty, C_WHITE, cutText(rows[idx].typeTrain, 11));
    fixedSmallText(146, ty, C_WHITE, cutText(rows[idx].destination, 8));
    fixedSmallText(194, ty, C_WHITE, cutText(rows[idx].voie, 7));
  }
  gfx->fillRect(4, 116, 232, 12, 0x39E7);
  fixedSmallText(78, 119, C_WHITE, "MOYNIHAN TRAIN HALL");
}

void drawUsAmtrakBlackRows() {
  const uint16_t bg = C_BLACK;
  const uint16_t white = C_WHITE;
  const uint16_t grey = 0x2945;
  const uint16_t red = C_RED;
  gfx->fillRect(0, 0, 240, 135, bg);
  fixedSmallText(78, 4, white, "DEPARTURES");
  fixedSmallText(4, 18, white, "Time");
  fixedSmallText(38, 18, white, "No.");
  fixedSmallText(68, 18, white, "Train");
  fixedSmallText(127, 18, white, "To");
  fixedSmallText(176, 18, white, "Status");
  fixedSmallText(224, 18, white, "Tr");
  int rowsToDraw = constrain(nbVisible, 1, 5);
  int rowTop = 30;
  int rowH = max(10, (115 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    gfx->fillRect(5, y, 230, rowH - 1, (i % 2) ? 0x0000 : 0x1082);
    fixedSmallText(5, ty, white, cutText(rows[idx].heure, 6));
    fixedSmallText(39, ty, white, cutText(rows[idx].info, 4));
    fixedSmallText(68, ty, white, cutText(rows[idx].typeTrain, 9));
    fixedSmallText(127, ty, white, cutText(rows[idx].destination, 8));
    fixedSmallText(176, ty, white, cutText(rows[idx].voie, 8));
    fixedSmallText(224, ty, white, cutText(rows[idx].retard, 3));
    gfx->drawFastHLine(5, y + rowH - 1, 230, grey);
  }
  gfx->fillRect(0, 118, 240, 10, 0x2000);
  fixedSmallText(4, 120, red, "EWR - designates Newark Intl Airport stop");
}
void drawBarcelonaAdifRows() {
  gfx->fillRect(3, 3, 234, 129, 0xD69A);
  gfx->fillRect(4, 4, 232, 18, 0xE71C);
  fixedSmallText(8, 9, 0x6B4D, "10:48");
  fixedSmallText(48, 9, C_BLACK, "Sortides/Salidas");
  fixedSmallText(158, 9, 0x6B4D, "DEP.");
  smallText(8, 25, 0x6B4D, "Hora");
  smallText(52, 25, 0x6B4D, "Desti");
  smallText(136, 25, 0x6B4D, "Tren");
  smallText(205, 25, 0x6B4D, "Via");
  int rowsToDraw = constrain(nbVisible, 1, 8);
  int rowTop = 36;
  int rowH = max(11, (126 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    gfx->fillRect(5, y, 229, rowH - 1, (i % 2) ? 0xC638 : 0xEF7D);
    gfx->drawFastHLine(5, y + rowH - 1, 229, 0xB596);
    smallText(8, ty, 0x4A49, cutText(rows[idx].heure, 5));
    smallText(52, ty, C_BLACK, cutText(rows[idx].destination, 13));
    smallText(138, ty, C_RED, cutText(rows[idx].typeTrain, 5));
    smallText(205, ty, C_BLACK, cutText(rows[idx].voie, 5));
  }
}

void drawSpainAdifDeparturesRows() {
  const uint16_t paper = 0xE73C;
  const uint16_t paperAlt = 0xEF7D;
  const uint16_t green = 0x03E0;
  const uint16_t greenDark = 0x01C0;
  const uint16_t ink = 0x4A49;
  const uint16_t grid = 0xB596;
  const uint16_t renfe = 0x980C;
  const uint16_t orange = 0xFBE0;
  if (!rowsOnlyPass) {
    gfx->fillRect(0, 0, 240, 135, C_BLACK);
    gfx->fillRect(4, 4, 232, 125, paper);
    gfx->fillRect(5, 5, 34, 124, greenDark);
    gfx->fillRect(39, 5, 197, 22, 0xF7DE);
    mediumText(8, 9, C_WHITE, "10 01");
    mediumText(45, 8, ink, "Salidas");
    smallText(132, 11, ink, "DEPARTURES");
    smallText(206, 9, greenDark, "adif");
    smallText(8, 31, greenDark, "Hora");
    smallText(45, 31, greenDark, "Destino");
    smallText(117, 31, greenDark, "Oper");
    smallText(153, 31, greenDark, "Num");
    smallText(184, 31, greenDark, "Via");
    smallText(210, 31, greenDark, "Obs");
  }

  int rowsToDraw = constrain(nbVisible, 1, 8);
  int rowTop = 44;
  int rowH = max(10, (129 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    uint16_t rowBg = (i % 2) ? paperAlt : paper;
    gfx->fillRect(5, y, 34, rowH - 1, greenDark);
    gfx->fillRect(40, y, 196, rowH - 1, rowBg);
    gfx->drawFastHLine(40, y + rowH - 1, 196, grid);
    smallText(8, ty, C_WHITE, cutText(rows[idx].heure, 5));
    smallText(44, ty, ink, cutText(rows[idx].destination, 10));
    String oper = rows[idx].typeTrain.length() ? rows[idx].typeTrain : String("RENFE");
    smallText(116, ty, renfe, cutText(oper, 5));
    smallText(151, ty, ink, cutText(rows[idx].info, 5));
    smallText(184, ty, ink, cutText(rows[idx].voie, 2));
    String obs = rows[idx].retard.length() ? rows[idx].retard : "";
    uint16_t obsColor = obs == "SAL" ? C_RED : orange;
    smallText(209, ty, obsColor, cutText(obs, 4));
  }
}

void drawSpainRodaliesDeparturesRows() {
  const uint16_t panel = 0x2104;
  const uint16_t header = 0x0320;
  const uint16_t amber = 0xFD20;
  const uint16_t line = 0x7BEF;
  gfx->fillRect(0, 0, 240, 135, C_BLACK);
  gfx->fillRect(4, 6, 232, 122, panel);
  gfx->fillRect(5, 7, 230, 18, header);
  fixedSmallText(10, 11, C_WHITE, "Salidas");
  fixedSmallText(72, 11, C_WHITE, "Departures");
  fixedSmallText(8, 30, C_WHITE, "Hora");
  fixedSmallText(43, 30, C_WHITE, "Destino");
  fixedSmallText(145, 30, C_WHITE, "Tren");
  fixedSmallText(188, 30, C_WHITE, "Via");
  int rowsToDraw = constrain(nbVisible, 1, 6);
  int rowTop = 42;
  int rowH = max(12, (116 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    gfx->drawFastHLine(7, y + rowH - 1, 226, line);
    fixedSmallText(9, ty, amber, cutText(rows[idx].heure, 5));
    fixedSmallText(43, ty, amber, cutText(rows[idx].destination, 15));
    fixedSmallText(145, ty, amber, cutText(rows[idx].typeTrain, 8));
    fixedSmallText(190, ty, amber, cutText(rows[idx].voie, 2));
  }
  gfx->fillRect(5, 118, 230, 10, C_BLACK);
  fixedSmallText(18, 120, amber, "CERCANIAS C1 - C2 - C3");
}

void drawNaplesAmberRows() {
  gfx->fillRect(3, 3, 234, 129, 0x1082);
  gfx->fillCircle(27, 42, 24, C_BLACK);
  gfx->drawCircle(27, 42, 25, C_WHITE);
  gfx->drawCircle(27, 42, 23, 0xC618);
  for (int a = 0; a < 360; a += 30) {
    float rad = a * 0.0174533f;
    int x1 = 27 + (int)(cos(rad) * 19);
    int y1 = 42 + (int)(sin(rad) * 19);
    int x2 = 27 + (int)(cos(rad) * 22);
    int y2 = 42 + (int)(sin(rad) * 22);
    gfx->drawLine(x1, y1, x2, y2, C_WHITE);
  }
  gfx->drawLine(27, 42, 17, 35, C_WHITE);
  gfx->drawLine(27, 42, 43, 51, C_RED);
  gfx->fillCircle(27, 42, 2, C_WHITE);
  smallText(8, 8, C_WHITE, "13.10.2024");
  smallText(72, 8, C_WHITE, "PARTENZE");
  int rowsToDraw = constrain(nbVisible, 1, 6);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
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
  int rowsToDraw = constrain(nbVisible, 1, 6);
  int rowTop = 30;
  int rowH = max(10, (129 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    gfx->drawFastHLine(5, y + rowH - 1, 230, 0x4208);
    smallText(8, ty, C_AMBER, cutText(rows[idx].heure, 5));
    smallText(43, ty, C_GREEN, cutText(rows[idx].typeTrain, 6));
    smallText(91, ty, C_AMBER, cutText(rows[idx].destination, 14));
    smallText(202, ty, C_WHITE, cutText(rows[idx].voie, 3));
  }
}

void drawSheffieldRows() {
  gfx->fillRect(3, 3, 234, 129, 0x39E7);
  int rowsToDraw = constrain(nbVisible, 1, 4);
  int rowTop = 8;
  int rowH = max(23, (126 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    gfx->fillRect(5, y, 230, rowH - 4, 0x3186);
    gfx->drawFastHLine(5, y + rowH - 4, 230, 0xA514);
    fixedSmallText(8, y + 4, C_AMBER, cutText(rows[idx].heure, 5));
    fixedSmallText(54, y + 4, C_AMBER, cutText(rows[idx].destination, 14));
    fixedSmallText(151, y + 4, C_AMBER, cutText(rows[idx].voie, 11));
    fixedSmallText(205, y + 4, C_AMBER, cutText(rows[idx].typeTrain.length() ? rows[idx].typeTrain : String("On time"), 7));
    fixedSmallText(8, y + 14, 0xD5A0, cutText(rows[idx].info, 30));
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
  int rowsToDraw = constrain(nbVisible, 1, 7);
  int rowTop = 19;
  int rowH = max(10, (109 - rowTop) / rowsToDraw);
  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = rowTop + i * rowH;
    int ty = y + max(1, (rowH - 8) / 2);
    String tag = rows[idx].typeTrain;
    uint16_t tagColor = (tag == "WEST") ? 0x07E0 : ((tag == "STH") ? C_RED : C_YELLOW);
    uint16_t tagText = (tag == "STH") ? C_WHITE : bg;
    gfx->fillRect(4, y, leftW, rowH - 1, (i % 2) ? rowB : rowA);
    gfx->drawFastHLine(4, y + rowH - 1, leftW, line);
    gfx->fillRoundRect(7, ty, 26, 9, 2, tagColor);
    smallText(11, ty + 2, tagText, cutText(tag, 4));
    smallText(41, ty, C_WHITE, cutText(rows[idx].destination, 9));
    smallText(82, ty + 3, 0xD6FF, cutText(rows[idx].info, 12));
    if (rows[idx].info.indexOf("Panmure") >= 0 || rows[idx].destination == "Papakura") smallText(137, ty + 2, C_YELLOW, "x");
    smallText(122, ty, C_WHITE, cutText(rows[idx].voie, 2));
    smallText(146, ty, C_WHITE, cutText(rows[idx].heure, 5));
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
  if (!rowsOnlyPass) gfx->fillScreen(C_BLACK);
  if (isRetroProfile()) { drawRetroFrame(); return; }
  if (displayProfile == "ch_sbb_romandie") { drawSwissRomandieRows(); return; }
  if (displayProfile == "ch_zurich_fern") { drawZurichFernverkehrRows(); return; }
  if (displayProfile == "ch_bern_arrival") { drawSwissBernArrivalRows(); return; }
  if (displayProfile == "ch_sbb_blue") { drawSwissCffBlueRows(); return; }
  if (displayProfile == "be_sncb_detail_list") { drawSncb2023PhotoRef(); return; }
  if (displayProfile == "be_sncb_modern") { drawSncb2023Photo(); return; }
  if (displayProfile == "be_sncb_grid") { drawSncbTft2010PhotoFix(); return; }
  if (displayProfile == "be_sncb_detail") { drawSncbRailTime(); return; }
  if (displayProfile == "fr_sncf_first") { drawSncfFirstScreenRows(); return; }
  if (displayProfile == "fr_sncf_old_led") { drawSncfOldLedRows(); return; }
  if (displayProfile == "fr_sncf_arrivals") { drawSncfArrivalsGreenRows(); return; }
  if (displayProfile == "fr_sncf_2012") { drawSncf2012Photo(); return; }
  if (displayProfile == "fr_sncf_valence_side") { drawSncfValenceSideRows(); return; }
  if (displayProfile == "fr_sncf_idf_crt") { drawSncfIdfCrtRows(); return; }
  if (displayProfile == "fr_sncf_1990_flipflap") { drawSncf1990FlipFlapRows(); return; }
  if (displayProfile == "fr_sncf_depart_grandes_lignes") { drawSncfDepartGrandesLignesRows(); return; }
  if (displayProfile == "fr_sncf_arrivees_grandes_lignes") { drawSncfArriveesGrandesLignesRows(); return; }
  if (displayProfile == "fr_sncf_2009_led") { drawSncf2009LedRows(); return; }
  if (displayProfile == "fr_iena_juvisy") { drawIenaJuvisyRows(); return; }
  if (displayProfile == "fr_montparnasse_2010") { drawMontparnasse2010Rows(); return; }
  if (displayProfile == "fr_montparnasse_tft") { drawMontparnasseTftRows(); return; }
  if (displayProfile == "fr_tgv_1") { drawTgv1Rows(); return; }
  if (displayProfile == "fr_tgv_2") { drawTgv2Rows(); return; }
  if (displayProfile == "fr_rer_a") { drawRerALineRows(); return; }
  if (displayProfile == "fr_rer_d_8090") { drawRerD8090Rows(); return; }
  if (displayProfile == "fr_saint_lazare") { drawSaintLazareRows(); return; }
  if (displayProfile == "fr_transilien_p") { drawTransilienLinePRows(); return; }
  if (displayProfile == "fr_transilien_2016") { drawTransilien2016Rows(); return; }
  if (displayProfile == "fr_rer_90") { gfx->fillRect(3,3,234,129,0x0300); gfx->fillRect(4,4,232,18,0x14A3); smallText(8,8,C_WHITE,"SNCF arrivee / depart"); drawRowsClassic(24,103,0x24C6,0x1383,C_WHITE,C_YELLOW,0x2E8A,2); return; }
  if (displayProfile == "fr_rer_orange") { gfx->fillRect(3,3,234,129,C_BLUE_DARK); gfx->fillRect(4,4,232,19,0x0B34); smallText(8,8,C_WHITE,"train n   heure   provenance"); smallText(200,8,C_WHITE,"voie"); drawRowsClassic(25,102,0x1383,0x0B34,C_WHITE,C_YELLOW,C_GRID,3); return; }
  if (displayProfile == "fr_transilien") { gfx->fillRect(3,3,234,129,0xC618); gfx->fillRect(4,4,232,17,C_WHITE); smallText(88,8,C_BLACK,"Prochains Trains"); drawDetailBoardRows(); return; }
  if (displayProfile == "es_adif_departures") { drawSpainAdifDeparturesRows(); return; }
  if (displayProfile == "es_rodalies_departures") { drawSpainRodaliesDeparturesRows(); return; }
  if (displayProfile == "es_barcelona_grid") { drawBarcelonaRodaliesGridRows(); return; }
  if (displayProfile == "es_barcelona_adif") { drawBarcelonaAdifRows(); return; }
  if (displayProfile == "pl_pkp_departures") { drawPolandBlueRows(false); return; }
  if (displayProfile == "us_la_metro") { drawLosAngelesRows(); return; }
  if (displayProfile == "us_amtrak_black") { drawUsAmtrakBlackRows(); return; }
  if (displayProfile == "it_naples_amber") { drawNaplesAmberRows(); return; }
  if (displayProfile == "in_indian_railways") { drawIndiaRows(); return; }
  if (displayProfile == "uk_sheffield") { drawSheffieldRows(); return; }
  
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
  if (displayProfile == "jp_tokyo_narita") { drawTokyoNaritaRows(); return; }
  if (displayProfile == "at_oebb_white") { drawOebbWhiteArrivalRows(); return; }
  if (displayProfile == "at_oebb_dense") { drawOebbBlueDenseRows(); return; }
  if (displayProfile == "at_oebb_teal") { drawOebbTealArrivalRows(); return; }
  if (displayProfile == "at_oebb_green") { drawOebbGreenPhotoRows(); return; }
  if (displayProfile == "at_oebb_blue") { drawOebbBluePhotoRows(); return; }
  if (displayProfile == "nl_ns_light") { drawNsLightModernRows(); return; }
  if (displayProfile == "nl_ns_2010_photo") { drawNs2010PhotoRows(); return; }
  if (displayProfile == "nl_ns_dark") { drawNsDarkModernRows(); return; }
  if (displayProfile == "nl_ns_blue") { drawNsBlueClassicRows(); return; }
  if (displayProfile == "fr_sncf_white") { gfx->fillRect(3,3,234,129,C_WHITE); gfx->fillRect(4,25,232,104,0xDF1F); smallText(8,7,C_BLACK,tftFramePlace()); smallText(196,7,C_BLACK,"10:30"); drawRowsClassic(28,99,0xDF1F,C_WHITE,C_BLACK,0x001F,C_GREY,0); return; }
  if (displayProfile == "fr_sncf_green" || displayProfile == "at_oebb_green") { gfx->fillRect(3,3,234,129,0x0300); smallText(8,8,C_WHITE,tftFrameTitle()); drawRowsClassic(23,104,0x24C6,0x1383,C_WHITE,C_YELLOW,0x2E8A,2); return; }
  if (displayProfile == "fr_led_nice") { gfx->fillRect(3,3,234,129,0x001F); gfx->fillRect(4,4,232,16,0x025F); smallText(8,7,C_WHITE,"SNCF Departures"); drawRowsClassic(21,108,0x001F,0x085F,C_WHITE,C_YELLOW,C_GRID,0); return; }
  if (displayProfile == "es_renfe" || displayProfile == "es_alsa") { gfx->fillRect(3,3,234,129,C_BLACK); smallText(8,7,C_AMBER,displayProfile == "es_alsa" ? "ALSA SALIDAS" : tftFrameTitle()); drawRowsClassic(21,108,C_BLACK,0x1082,C_WHITE,C_AMBER,C_GREY,1); return; }
  if (displayProfile == "de_db_orange" || displayProfile == "uk_led_amber" || displayProfile == "fr_tgv_amber" || displayProfile == "es_renfe_split" || displayProfile == "it_fs_amber") { gfx->fillRect(3,3,234,129,C_BLACK); gfx->fillRect(4,4,232,16,0x2104); smallText(8,8,C_AMBER,displayProfile == "es_renfe_split" ? "Salidas / Departures" : tftFrameTitle()); drawRowsClassic(22,103,C_BLACK,0x1082,C_AMBER,C_AMBER,0x4208,1); return; }
  if (displayProfile == "ch_sbb_yellow") { gfx->fillRect(3,3,234,129,0xFD20); mediumText(36,9,C_BLACK,"Abfahrt Depart"); drawRowsClassic(35,72,0x001F,0x181F,C_WHITE,C_WHITE,C_WHITE,0); gfx->fillRect(4,108,232,20,C_RED); fixedSmallText(8,115,C_WHITE,cutText(swissInfoMessage.length() > 37 ? marqueeWindow(swissInfoMessage + "   ", 37, 4) : swissInfoMessage,37)); return; }
  if (displayProfile == "hu_mav_arrivals") { drawMavPhotoRows(true); return; }
  if (displayProfile == "hu_mav_departures") { drawMavPhotoRows(false); return; }
  if (displayProfile == "hu_mav_old") { gfx->fillRect(3,3,234,129,C_BLUE_DARK); fixedSmallText(92,8,C_WHITE,"09 : 23"); fixedSmallText(8,24,C_WHITE,"MAV Indulo vonatok / Departure"); drawRowsClassic(39,86,0x0861,0x10A2,C_WHITE,0xB7FF,C_GRID,0); return; }
  if (displayProfile == "hu_mav_blue") { gfx->fillRect(3,3,234,129,0x03BF); fixedSmallText(8,7,C_WHITE,"MAV  INDULO VONATOK"); drawRowsClassic(23,104,0x05FF,0x037F,C_WHITE,C_WHITE,C_GRID,0); return; }
  if (displayProfile == "hu_mav_green") { gfx->fillRect(3,3,234,129,0x03E0); fixedSmallText(8,7,C_WHITE,"MAV  ERKEZO VONATOK"); drawRowsClassic(23,104,0x05E0,0x0380,C_WHITE,C_WHITE,C_GRID,0); return; }
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
  if (displayProfile == "be_sncb_detail_list") { drawSncb2023PhotoRef(); return; }
  if (displayProfile == "be_sncb_modern") { drawSncb2023Photo(); return; }
  if (displayProfile == "be_sncb_grid") { drawSncbTft2010PhotoFix(); return; }
  if (displayProfile == "be_sncb_detail") { drawSncbRailTime(); return; }
  if (displayProfile == "fr_sncf_first") { drawSncfFirstScreenRows(); return; }
  if (displayProfile == "fr_sncf_old_led") { drawSncfOldLedRows(); return; }
  if (displayProfile == "fr_sncf_arrivals") { drawSncfArrivalsGreenRows(); return; }
  if (displayProfile == "fr_sncf_2012") { drawSncf2012Photo(); return; }
  if (displayProfile == "fr_sncf_valence_side") { drawSncfValenceSideRows(); return; }
  if (displayProfile == "fr_sncf_idf_crt") { drawSncfIdfCrtRows(); return; }
  if (displayProfile == "fr_sncf_1990_flipflap") { drawSncf1990FlipFlapRows(); return; }
  if (displayProfile == "fr_sncf_depart_grandes_lignes") { drawSncfDepartGrandesLignesRows(); return; }
  if (displayProfile == "fr_sncf_arrivees_grandes_lignes") { drawSncfArriveesGrandesLignesRows(); return; }
  if (displayProfile == "fr_sncf_2009_led") { drawSncf2009LedRows(); return; }
  if (displayProfile == "fr_iena_juvisy") { drawIenaJuvisyRows(); return; }
  if (displayProfile == "fr_montparnasse_2010") { drawMontparnasse2010Rows(); return; }
  if (displayProfile == "fr_montparnasse_tft") { drawMontparnasseTftRows(); return; }
  if (displayProfile == "fr_tgv_1") { drawTgv1Rows(); return; }
  if (displayProfile == "fr_tgv_2") { drawTgv2Rows(); return; }
  if (displayProfile == "fr_rer_a") { drawRerALineRows(); return; }
  if (displayProfile == "fr_rer_d_8090") { drawRerD8090Rows(); return; }
  if (displayProfile == "fr_saint_lazare") { drawSaintLazareRows(); return; }
  if (displayProfile == "fr_transilien_p") { drawTransilienLinePRows(); return; }
  if (displayProfile == "fr_transilien_2016") { drawTransilien2016Rows(); return; }
  if (displayProfile == "fr_rer_90") { drawRowsClassic(24,103,0x24C6,0x1383,C_WHITE,C_YELLOW,0x2E8A,2); return; }
  if (displayProfile == "fr_rer_orange") { drawRowsClassic(25,102,0x1383,0x0B34,C_WHITE,C_YELLOW,C_GRID,3); return; }
  if (displayProfile == "fr_transilien") { drawDetailBoardRows(); return; }
  if (displayProfile == "es_adif_departures") { drawSpainAdifDeparturesRows(); return; }
  if (displayProfile == "es_rodalies_departures") { drawSpainRodaliesDeparturesRows(); return; }
  if (displayProfile == "es_barcelona_grid") { drawBarcelonaRodaliesGridRows(); return; }
  if (displayProfile == "es_barcelona_adif") { drawBarcelonaAdifRows(); return; }
  if (displayProfile == "pl_pkp_departures") { drawPolandBlueRows(false); return; }
  if (displayProfile == "us_la_metro") { drawLosAngelesRows(); return; }
  if (displayProfile == "us_amtrak_black") { drawUsAmtrakBlackRows(); return; }
  if (displayProfile == "it_naples_amber") { drawNaplesAmberRows(); return; }
  if (displayProfile == "in_indian_railways") { drawIndiaRows(); return; }
  if (displayProfile == "uk_sheffield") { drawSheffieldRows(); return; }
  
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
  if (displayProfile == "jp_tokyo_narita") { drawTokyoNaritaRows(); return; }
  if (displayProfile == "at_oebb_white") { drawOebbWhiteArrivalRows(); return; }
  if (displayProfile == "at_oebb_dense") { drawOebbBlueDenseRows(); return; }
  if (displayProfile == "at_oebb_teal") { drawOebbTealArrivalRows(); return; }
  if (displayProfile == "at_oebb_green") { drawOebbGreenPhotoRows(); return; }
  if (displayProfile == "at_oebb_blue") { drawOebbBluePhotoRows(); return; }
  if (displayProfile == "nl_ns_light") { drawNsLightModernRows(); return; }
  if (displayProfile == "nl_ns_2010_photo") { drawNs2010PhotoRows(); return; }
  if (displayProfile == "nl_ns_dark") { drawNsDarkModernRows(); return; }
  if (displayProfile == "nl_ns_blue") { drawNsBlueClassicRows(); return; }
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
static const char PM3D_LOGO_B64[] PROGMEM = R"PM3DLOGO(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)PM3DLOGO";

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
  css += ".advCompact .card{padding:9px}.advCompact .advNotice,.advCompact .hint,.advCompact .sub{padding:7px;margin:5px 0;font-size:11px;line-height:1.25}.advCompact .section{margin:8px 0 5px;font-size:12px}.advCompact .advCards{grid-template-columns:1fr 1fr;gap:5px}.advCompact .advCard{padding:7px;border-radius:8px}.advCompact .advCardTitle{font-size:11px}.advCompact .advCardText{font-size:10px;line-height:1.25}.advCompact .settingsGrid{gap:5px}.advCompact label{font-size:11px}.advCompact select,.advCompact input{padding:7px 6px;font-size:12px;border-radius:8px}.advCompact .stepCtl{padding:6px 6px;border-radius:8px;gap:4px}.advCompact .stepCtl span{font-size:10px}.advCompact .stepBox{grid-template-columns:26px 34px 26px;gap:3px}.advCompact .stepBox button{height:26px;padding:0;border-radius:7px;font-size:15px}.advCompact .stepBox b{height:26px;border-radius:7px;font-size:12px}.advCompact .compactActions,.advCompact .advActions{gap:5px}.advCompact .btn,.advCompact button{padding:8px 9px;font-size:11px;border-radius:9px}";
  css += ".updBox{padding:11px}.fwBox{margin:8px 0;padding:10px;border-radius:12px;background:rgba(0,0,0,.18);border:1px solid rgba(255,255,255,.12)}.fwBox input[type=file]{width:100%;box-sizing:border-box;font-size:12px;padding:8px;border-radius:9px;background:rgba(0,0,0,.22);border:1px solid rgba(255,255,255,.16);color:white}.updBox .advCards{grid-template-columns:1fr;gap:6px}.updBox .advCard{padding:8px;border-radius:10px}.updBox .section{margin:4px 0 7px;font-size:13px}.updBox .hint{font-size:11px;line-height:1.3;padding:7px;margin:6px 0}";
  css += ".pm3dLogo{width:56px;height:56px;border-radius:16px;border:1px solid rgba(230,246,255,.34);background:" + themeButtonBg() + ";display:flex;align-items:center;justify-content:center;font-weight:1000;color:white;text-shadow:0 2px 6px #000;box-shadow:0 10px 22px rgba(0,0,0,.38),inset 0 1px 0 rgba(255,255,255,.65);}.pm3dLogoImg{width:58px;height:58px;object-fit:contain;border-radius:15px;filter:drop-shadow(0 12px 18px rgba(0,0,0,.48));}.introLogo{width:132px;height:132px;margin:0 auto 12px;}.homeHero{padding:22px 12px 16px}.mainActions{display:grid;grid-template-columns:1fr;gap:8px}.grid2{display:grid;grid-template-columns:1fr 1fr;gap:8px}.modeGrid{display:grid;grid-template-columns:repeat(3,1fr);gap:6px}.row{display:grid;grid-template-columns:48px 44px 38px 1fr 45px 34px;gap:5px;align-items:center;margin:5px 0}.head{color:#F0F8FF;font-size:11px;display:grid;grid-template-columns:48px 44px 38px 1fr 45px 34px;gap:5px;margin-top:4px;text-align:left}.row input{padding:7px;font-size:13px}.countryGrid{display:grid;grid-template-columns:repeat(3,1fr);gap:7px}.swatches{display:grid;grid-template-columns:repeat(3,1fr);gap:7px}.settingsGrid{display:grid;grid-template-columns:1fr 1fr;gap:8px}.settingsLine{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;align-items:end}.settingsLineWide{grid-template-columns:repeat(5,minmax(0,1fr));gap:6px}.styleGrid{display:grid;grid-template-columns:1fr 1fr;gap:5px}.styleBtn{text-align:left;min-height:34px;padding:7px 8px;margin:0;font-size:11px;line-height:1.1}.styleItem{position:relative;border:1px solid rgba(255,255,255,.10);border-radius:10px;padding:3px;background:rgba(0,0,0,.08)}.styleActions{position:absolute;right:7px;top:6px;display:flex;gap:4px;align-items:center}.miniMove{width:26px;height:26px;margin:0;padding:0;border-radius:8px;font-size:13px;line-height:26px;display:flex;align-items:center;justify-content:center}";
  css += ".trainList{display:grid;gap:5px}.trainCard{display:grid;grid-template-columns:42px 1fr;gap:5px 6px;border:1px solid rgba(255,255,255,.12);border-radius:10px;background:rgba(0,0,0,.16);padding:6px;text-align:left}.trainTop{grid-row:1/3;display:flex;align-items:center;justify-content:center;flex-direction:column;gap:1px;min-width:0}.trainTop .tiny{display:none}.trainNum{font-size:10px;font-weight:1000;color:" + themeAccent1() + ";text-transform:uppercase;line-height:1;text-align:center}.trainMini{display:grid;grid-template-columns:54px 54px 54px 48px;gap:4px;min-width:0}.trainWide{display:grid;grid-template-columns:minmax(0,1.2fr) minmax(0,1fr);gap:4px;min-width:0}.fieldLbl{display:block;margin:0;font-size:8px;text-transform:uppercase;color:#AFCDE0;font-weight:1000;letter-spacing:0;line-height:1}.fieldLbl input{margin-top:2px;padding:5px 5px;font-size:12px;border-radius:8px;height:28px}.fieldDestination{grid-column:auto}.previewRow{display:grid;grid-template-columns:45px 1fr 52px 34px;gap:6px;align-items:center;border-bottom:1px solid rgba(255,255,255,.10);padding:5px 0;text-align:left;font-size:12px}.previewHead{color:#AFCDE0;font-weight:900;text-transform:uppercase;font-size:10px}";
  css += ".pm3dStatus{display:grid;grid-template-columns:1fr 1fr;gap:7px;margin:10px 0}.statusTile{border:1px solid rgba(255,255,255,.14);border-radius:14px;background:rgba(0,0,0,.18);padding:9px;text-align:left}.statusLabel{font-size:10px;text-transform:uppercase;color:" + themeAccent1() + ";font-weight:900}.statusValue{font-size:13px;font-weight:900;margin-top:3px}.hint{font-size:12px;line-height:1.35;color:#D8ECF8;text-align:left;margin:7px 0}.compactActions{display:grid;grid-template-columns:1fr 1fr;gap:7px}.compactActions .btn,.compactActions button{margin:0;min-height:44px}";
  css += ".advModal{position:fixed;inset:0;z-index:200;display:none;align-items:center;justify-content:center;padding:18px;background:rgba(0,4,10,.78);backdrop-filter:blur(4px)}.advBox{width:min(92vw,430px);border-radius:20px;border:1px solid rgba(255,215,120,.62);background:linear-gradient(180deg,rgba(28,22,12,.98),rgba(6,10,18,.98));box-shadow:0 24px 70px rgba(0,0,0,.62),inset 0 1px 0 rgba(255,255,255,.18);padding:16px;text-align:left}.advTitle{display:flex;align-items:center;gap:10px;font-size:18px;font-weight:1000;color:#fff1bd;margin-bottom:10px}.advIcon{width:34px;height:34px;min-width:34px;border-radius:50%;display:flex;align-items:center;justify-content:center;background:linear-gradient(180deg,#ffe68b,#d98a00 58%,#6d3100);color:#2a1500;border:1px solid rgba(255,255,255,.58);box-shadow:0 0 18px rgba(255,170,0,.45);font-weight:1000}.advText{font-size:13px;line-height:1.45;color:#f5e7cf;margin:8px 0 14px}.advBtns{display:grid;grid-template-columns:1fr;gap:8px}.advBtns button{margin:0;min-height:44px}.advNotice{border:1px solid rgba(255,213,93,.48);background:linear-gradient(180deg,rgba(112,72,0,.30),rgba(0,0,0,.18));border-radius:14px;padding:10px 11px;color:#fff0c6;font-size:13px;line-height:1.4;text-align:left}.advCards{display:grid;grid-template-columns:1fr 1fr;gap:8px}.advCard{border:1px solid rgba(255,255,255,.14);border-radius:14px;background:rgba(0,0,0,.18);padding:10px;text-align:left}.advCardTitle{font-size:11px;text-transform:uppercase;color:" + themeAccent1() + ";font-weight:1000;margin-bottom:5px}.advCardText{font-size:13px;font-weight:850;color:#e8f5ff;line-height:1.3}.advActions{display:grid;grid-template-columns:1fr 1fr;gap:8px}.advActions .btn,.advActions button{margin:0}.dangerText{color:#ffd1d1;font-size:12px;line-height:1.35;margin-top:6px}";
  css += ".screenPreview{margin-top:10px}.tftMock{width:min(100%,360px);aspect-ratio:240/135;margin:8px auto;border-radius:12px;padding:6px;background:#05080d;border:2px solid rgba(255,255,255,.18);box-shadow:inset 0 0 0 1px rgba(255,255,255,.08),0 14px 34px rgba(0,0,0,.42);overflow:hidden;text-align:left}.tftHead{height:19%;display:flex;align-items:center;gap:6px;padding:0 6px;font-size:11px;font-weight:1000;white-space:nowrap;overflow:hidden}.tftClock{margin-left:auto}.tftRows{height:81%;display:grid;gap:1px}.tftLine{display:grid;grid-template-columns:38px 1fr 42px 28px;gap:4px;align-items:center;padding:0 5px;font-size:10px;font-weight:900;min-height:0;overflow:hidden}.tftLine span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.tftDest{font-size:11px}.tftInfo{opacity:.82}.prevSncb{background:#061841}.prevSncb .tftHead{background:#043a87;color:#fff}.prevSncb .tftLine:nth-child(odd){background:#123c86}.prevSncb .tftLine:nth-child(even){background:#0d2e68}.prevSncb .tftTime,.prevSncb .tftTrack{color:#ffe95a}.prevSncf{background:#051240}.prevSncf .tftHead{background:#0050b8;color:#fff}.prevSncf .tftLine:nth-child(odd){background:#073f92}.prevSncf .tftLine:nth-child(even){background:#032f70}.prevSncf .tftTime{color:#ffd84a}.prevTransilien{background:#ddd;color:#111}.prevTransilien .tftHead{background:#fff;color:#111}.prevTransilien .tftLine:nth-child(odd){background:#f2f2f2}.prevTransilien .tftLine:nth-child(even){background:#e4e8ee}.prevTransilien .tftInfo{color:#e46f00}.prevDb{background:#00082f}.prevDb .tftHead{background:#001a78;color:#fff}.prevDb .tftLine:nth-child(odd){background:#002d9a}.prevDb .tftLine:nth-child(even){background:#001d66}.prevDb .tftTime,.prevDb .tftTrack{color:#ffd84a}.prevSbb{background:#f5be00;color:#111}.prevSbb .tftHead{background:#f5be00;color:#111}.prevSbb .tftRows{background:#001f86}.prevSbb .tftLine{color:#fff}.prevSbb .tftLine:nth-child(odd){background:#002f9f}.prevSbb .tftLine:nth-child(even){background:#001d70}.prevOebb{background:#00145c}.prevOebb .tftHead{background:#002080;color:#fff}.prevOebb .tftLine:nth-child(odd){background:#034aa3}.prevOebb .tftLine:nth-child(even){background:#013276}.prevOebb .tftTime,.prevOebb .tftTrack{color:#ffe95a}.prevAmber{background:#080808;color:#ffc13b}.prevAmber .tftHead{background:#1c1608;color:#ffc13b}.prevAmber .tftLine:nth-child(odd){background:#121212}.prevAmber .tftLine:nth-child(even){background:#1f1b12}.prevLight{background:#e8eef2;color:#111}.prevLight .tftHead{background:#d7e4ea;color:#111}.prevLight .tftLine:nth-child(odd){background:#fff}.prevLight .tftLine:nth-child(even){background:#dfe8ed}.prevGeneric{background:#071932}.prevGeneric .tftHead{background:#0b477c;color:#fff}.prevGeneric .tftLine:nth-child(odd){background:#104b83}.prevGeneric .tftLine:nth-child(even){background:#0b3765}";
  css += "@media(max-width:560px){body{padding:6px}.langBar{display:flex!important;flex-direction:row!important;flex-wrap:nowrap!important}.langBar .btn,.langBtn{min-width:34px;padding:7px 7px;font-size:12px}.title{font-size:19px}.grid2{grid-template-columns:1fr}.swatches{grid-template-columns:repeat(3,1fr)}.countryGrid{grid-template-columns:1fr 1fr}.modeGrid{grid-template-columns:1fr 1fr}.settingsLineWide{grid-template-columns:1fr 1fr}.trainCard{grid-template-columns:38px 1fr;padding:5px;border-radius:9px}.trainMini{grid-template-columns:repeat(4,minmax(0,1fr));gap:3px}.trainWide{grid-template-columns:1.15fr .85fr;gap:3px}.fieldLbl{font-size:7px}.fieldLbl input{font-size:11px;padding:4px 4px;height:25px;border-radius:7px}.previewRow{grid-template-columns:42px 1fr 44px 28px}.pm3dLogo{width:48px;height:48px}.pm3dLogoImg{width:50px;height:50px}.introLogo{width:118px;height:118px}.back{width:36px;height:36px}.headerActions{position:static;justify-content:center;margin-top:8px}.brandRow{flex-direction:column}.brandText{text-align:center}.advCards,.advActions{grid-template-columns:1fr}}";
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
  h += "<a class='btn langBtn paletteTop' href='#' onclick='showAdvancedWarning();return false' title='" + trKey("settings") + "'>&#9881;</a>";
  h += "</div>";
  return h;
}
String pageStart(const String &title) {
  String h;
  h += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<title>PM3D - " + title + "</title><style>" + cssCommon() + "</style>";
  h += R"PM3DSLIDER(<style>
/* PM3D sliders longs et progressifs */
.rangeBig, input[type=range]{
  width: min(92vw, 760px) !important;
  height: 34px !important;
  touch-action: pan-y;
}
input[type=range]::-webkit-slider-thumb{
  width: 34px !important;
  height: 34px !important;
}
.pm3d-settings-line{
  display:flex;
  align-items:center;
  gap:10px;
  flex-wrap:nowrap;
}
.pm3d-small-input{
  width:54px !important;
  min-width:54px !important;
  max-width:54px !important;
  text-align:center;
}
.pm3d-sec::after{content:" sec"; margin-left:4px;}
</style>)PM3DSLIDER";
  h += R"PM3DCSS(<style>
button,.btn,a.btn,.styleBtn,.countryBtn{
  touch-action:manipulation!important;
  -webkit-tap-highlight-color:transparent!important;
  cursor:pointer!important;
}
button:active,.btn:active,a.btn:active,.styleBtn:active,.countryBtn:active{
  transform:scale(.97)!important;
  filter:brightness(1.25)!important;
}
.langBtn{min-width:38px!important;padding:7px 9px!important;font-weight:1000}
.countryGrid{grid-template-columns:repeat(3,1fr)!important;gap:5px!important}
.countryBtn{min-height:34px!important;padding:6px 4px!important;gap:3px!important;font-size:10px!important;line-height:1.05!important;justify-content:center!important;text-align:center!important;flex-direction:column!important}
.countryBtn .flag{min-width:22px!important;width:22px!important;height:18px!important;font-size:15px!important;border-radius:6px!important}
.stadtwerke,.stadtwerkeLogo,.swbbLogo,.brandStadtwerke{font-size:.78em!important;letter-spacing:-.5px!important}
.bahnhofTitle,.bahnTitle,.stationTitle{font-size:.94em!important}
</style>)PM3DCSS";
  h += "<script>function pmCur(){return location.pathname+location.search;}function pmFallback(){var p=location.pathname;if(p=='/main')return'/countries';if(p=='/countries')return'/main';if(p=='/country')return'/countries';if(p=='/stylecfg'){var q=location.search.match(/[?&]back=([^&]+)/);return q?'/country?c='+decodeURIComponent(q[1]):'/countries';}if(p=='/settings')return'/config';if(p=='/advanced')return'/config';if(p=='/wifiscan'||p=='/updates')return'/advanced';if(p=='/themes')return'/config';if(p=='/config')return'/main';return'/countries';}function pmBack(){var s=[];try{s=JSON.parse(sessionStorage.getItem('pmStack')||'[]');}catch(e){}var c=pmCur();while(s.length&&s[s.length-1]==c)s.pop();var d=s.pop();try{sessionStorage.setItem('pmStack',JSON.stringify(s));}catch(e){}location.href=d||pmFallback();return false;}function pmRemember(){var c=pmCur();var s=[];try{s=JSON.parse(sessionStorage.getItem('pmStack')||'[]');}catch(e){}if(!s.length||s[s.length-1]!=c){s.push(c);if(s.length>20)s.shift();try{sessionStorage.setItem('pmStack',JSON.stringify(s));}catch(e){}}}</script>";
  h += "</head><body onload='pmRemember()'><a class='back' href='/countries' title='" + trKey("back") + "' onclick='return pmBack()'>&lt;</a><div class='wrap'>" + langBarHtml();
  return h;
}

String pageEnd() {
  String h;
  h += advancedWarningModalHtml();
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
  h += "<div class='advTitle'><span class='advIcon'><span>!</span></span>" + trKey("advancedWarningTitle") + "</div>";
  h += "<div class='advText'>" + trKey("advancedWarningText") + "</div>";
  h += "<div class='advBtns'><button class='danger' type='button' onclick='hideAdvancedWarning()'>" + trKey("cancel") + "</button><button type='button' onclick=\"location.href='/advanced'\">" + trKey("understandContinue") + "</button></div>";
  h += "</div></div><script>function showAdvancedWarning(){var m=document.getElementById('advModal');if(m)m.style.display='flex';return false}function hideAdvancedWarning(){var m=document.getElementById('advModal');if(m)m.style.display='none';return false}</script>";
  return h;
}


String trText(const String &fr, const String &nl, const String &de, const String &en) {
  if (currentLang == "NL") return nl;
  if (currentLang == "DE") return de;
  if (currentLang == "EN") return en;
  return fr;
}

String trKey(const String &key) {
  if (key == "home") return trText("Accueil", "Start", "Start", "Home");
  if (key == "countries") return trText("Pays", "Landen", "Laender", "Countries");
  if (key == "styles") return trText("Styles", "Stijlen", "Stile", "Styles");
  if (key == "chooseFamily") return trText("Choisir une famille d'affichage", "Kies een schermfamilie", "Anzeigefamilie waehlen", "Choose a display family");
  if (key == "chooseStyle") return trText("Epoque, style ou type d'ecran", "Periode, stijl of schermtype", "Epoche, Stil oder Bildschirmtyp", "Era, style or screen type");
  if (key == "settings") return trText("Configuration", "Configuratie", "Konfiguration", "Configuration");
  if (key == "appName") return trText("PM3D Ecran Quai", "PM3D Perronscherm", "PM3D Bahnsteiganzeige", "PM3D Platform Board");
  if (key == "introSubtitle") return trText("Tableau des trains miniature", "Miniatuur treintabel", "Miniatur-Zugtafel", "Miniature train board");
  if (key == "trainTable") return trText("Tableau des trains", "Treintabel", "Zugtafel", "Train board");
  if (key == "advanced") return trText("Reglages avances", "Geavanceerd", "Erweitert", "Advanced");
  if (key == "themes") return trText("Themes", "Thema's", "Designs", "Themes");
  if (key == "wifiSearch") return trText("Recherche Wi-Fi", "Wi-Fi zoeken", "Wi-Fi suchen", "Wi-Fi search");
  if (key == "update") return trText("Mise a jour", "Update", "Aktualisierung", "Update");
  if (key == "backHome") return trText("Retour accueil", "Terug naar start", "Zurueck zum Start", "Back home");
  if (key == "save") return trText("Enregistrer et afficher", "Opslaan en tonen", "Speichern und anzeigen", "Save and display");
  if (key == "favorite") return trText("Favori", "Favoriet", "Favorit", "Favorite");
  if (key == "noFavorite") return trText("Aucun favori pour le moment. Ouvre un pays et touche l'etoile d'un style pour l'ajouter ici.", "Nog geen favorieten. Open een land en tik op de ster van een stijl om hem hier toe te voegen.", "Noch keine Favoriten. Oeffne ein Land und tippe auf den Stern eines Stils.", "No favorites yet. Open a country and tap the star on a style to add it here.");
  if (key == "screenEdit") return trText("Modifier l'ecran", "Scherm wijzigen", "Bildschirm bearbeiten", "Edit screen");
  if (key == "currentStyle") return trText("Style actif", "Actieve stijl", "Aktiver Stil", "Active style");
  if (key == "changeCountry") return trText("Changer de pays ou de style", "Land of stijl wijzigen", "Land oder Stil aendern", "Change country or style");
  if (key == "copyTable") return trText("Copier le contenu d'un autre tableau", "Inhoud van een ander bord kopieren", "Inhalt einer anderen Tafel kopieren", "Copy another board content");
  if (key == "source") return trText("Source", "Bron", "Quelle", "Source");
  if (key == "copy") return trText("Copier", "Kopieren", "Kopieren", "Copy");
  if (key == "intercityOptions") return trText("Options Intercity", "Intercity-opties", "Intercity-Optionen", "Intercity options");
  if (key == "topMessage") return trText("Message du bandeau", "Bericht bovenaan", "Meldung oben", "Top message");
  if (key == "clockStart") return trText("Heure programmee", "Geprogrammeerde tijd", "Programmierte Uhrzeit", "Programmed time");
  if (key == "addTrain") return trText("Ajouter un train", "Trein toevoegen", "Zug hinzufuegen", "Add train");
  if (key == "memoryWarn") return trText("Attention : chaque train supplementaire consomme de la memoire. Trop d'elements peuvent empecher une future mise a jour OTA.", "Let op: elke extra trein gebruikt geheugen. Te veel elementen kunnen een toekomstige OTA-update blokkeren.", "Achtung: Jeder zusaetzliche Zug verbraucht Speicher. Zu viele Elemente koennen ein spaeteres OTA-Update verhindern.", "Warning: each extra train uses memory. Too many items may block a future OTA update.");
  if (key == "noSpace") return trText("Plus de place disponible.", "Geen ruimte meer.", "Kein Platz mehr.", "No space left.");
  if (key == "memoryManageTitle") return trText("Mise a jour", "Update", "Aktualisierung", "Update");
  if (key == "memoryNoSpaceLong") return trText("Installation de train supplementaire impossible : la limite de lignes de ce tableau est atteinte.", "Extra trein installeren is onmogelijk: de regellimiet van dit bord is bereikt.", "Zusaetzlichen Zug installieren nicht moeglich: Die Zeilengrenze dieser Tafel ist erreicht.", "Extra train installation is impossible: this board reached its row limit.");
  if (key == "memoryOtaRisk") return trText("Attention OTA : trop de fonctions embarquees peuvent reduire la marge de mise a jour. Masquer un pays dans l'interface ne libere pas la flash du firmware actuel.", "OTA-waarschuwing: te veel ingebouwde functies kunnen de updateruimte verkleinen. Een land verbergen in de interface maakt geen flash vrij in de huidige firmware.", "OTA-Hinweis: Zu viele eingebaute Funktionen koennen den Update-Spielraum verkleinern. Ein Land in der Oberflaeche auszublenden gibt im aktuellen Firmware-Image keinen Flash frei.", "OTA warning: too many built-in functions can reduce update margin. Hiding a country in the UI does not free flash in the current firmware.");
  if (key == "manageCountries") return trText("Gerer les pays", "Landen beheren", "Laender verwalten", "Manage countries");
  if (key == "lightFirmware") return trText("Firmware allege", "Lichte firmware", "Leichte Firmware", "Light firmware");
  if (key == "keepCountries") return trText("Pays a conserver", "Landen behouden", "Laender behalten", "Countries to keep");
  if (key == "close") return trText("Fermer", "Sluiten", "Schliessen", "Close");
  if (key == "transilienOptions") return trText("Options Transilien", "Transilien-opties", "Transilien-Optionen", "Transilien options");
  if (key == "frame") return trText("Cadre", "Kader", "Rahmen", "Frame");
  if (key == "white") return trText("Blanc", "Wit", "Weiss", "White");
  if (key == "crowding") return trText("Affluence", "Drukte", "Auslastung", "Crowding");
  if (key == "direction") return trText("Direction", "Richting", "Richtung", "Direction");
  if (key == "servedStops") return trText("Gares desservies", "Bedienende stations", "Bediente Halte", "Served stops");
  if (key == "orangeBoxTitle") return trText("Titre case orange", "Titel oranje vak", "Titel orangenes Feld", "Orange box title");
  if (key == "orangeBoxText") return trText("Texte case orange", "Tekst oranje vak", "Text orangenes Feld", "Orange box text");
  if (key == "sncbGridCount") return trText("Cases SNCB ancien", "SNCB oude vakken", "SNCB alte Felder", "Old SNCB boxes");
  if (key == "lineCount") return trText("Nombre de lignes", "Aantal regels", "Zeilenanzahl", "Number of lines");
  if (key == "departures") return trText("Departs", "Vertrekken", "Abfahrten", "Departures");
  if (key == "time") return trText("Heure", "Tijd", "Zeit", "Time");
  if (key == "info") return trText("Info", "Info", "Info", "Info");
  if (key == "destination") return trText("Destination", "Bestemming", "Ziel", "Destination");
  if (key == "train") return trText("Train", "Trein", "Zug", "Train");
  if (key == "platform") return trText("Voie", "Spoor", "Gleis", "Platform");
  if (key == "displaySettings") return trText("Reglages affichage", "Scherminstellingen", "Anzeigeeinstellungen", "Display settings");
  if (key == "visibleRows") return trText("Lignes", "Zichtbare regels", "Sichtbare Zeilen", "Visible rows");
  if (key == "fontSizeBoard") return trText("Police", "Lettergrootte bord", "Schriftgroesse Tafel", "Board font size");
  if (key == "smallMediumLarge") return trText("1 petit / 2 moyen / 3 grand", "1 klein / 2 middel / 3 groot", "1 klein / 2 mittel / 3 gross", "1 small / 2 medium / 3 large");
  if (key == "scrollDelay") return trText("Delai defilement ms", "Scrolltijd ms", "Scrollzeit ms", "Scroll delay ms");
  if (key == "brightness") return trText("Luminosite directe", "Helderheid direct", "Helligkeit direkt", "Direct brightness");
  if (key == "saveSettings") return trText("Enregistrer reglages", "Instellingen opslaan", "Einstellungen speichern", "Save settings");
  if (key == "connection") return trText("Connexion", "Verbinding", "Verbindung", "Connection");
  if (key == "password") return trText("Mot de passe", "Wachtwoord", "Passwort", "Password");
  if (key == "none") return trText("aucun", "geen", "keins", "none");
  if (key == "browserAddress") return trText("Adresse navigateur", "Browseradres", "Browseradresse", "Browser address");
  if (key == "scrollDelayShort") return trText("Delai defilement", "Scrolltijd", "Scrollzeit", "Scroll delay");
  if (key == "brightnessShort") return trText("Luminosite", "Helderheid", "Helligkeit", "Brightness");
  if (key == "screenCalibration") return trText("Calage ecran ST7789", "ST7789 schermuitlijning", "ST7789 Bildschirmabgleich", "ST7789 screen alignment");
  if (key == "saveScreenCalibration") return trText("Enregistrer calage ecran", "Schermuitlijning opslaan", "Bildschirmabgleich speichern", "Save screen alignment");
  if (key == "defaultCalibrationNote") return trText("Defaut : X1 52, Y1 40, X2 53, Y2 40. Redemarre apres modification du calage.", "Standaard: X1 52, Y1 40, X2 53, Y2 40. Herstart na wijziging van de uitlijning.", "Standard: X1 52, Y1 40, X2 53, Y2 40. Neustart nach Aenderung des Abgleichs.", "Default: X1 52, Y1 40, X2 53, Y2 40. Reboots after alignment change.");
  if (key == "styleSettings") return trText("Reglages style", "Stijlinstellingen", "Stileinstellungen", "Style settings");
  if (key == "styleGear") return trText("Reglages du style", "Stijl instellen", "Stil einstellen", "Style settings");
  if (key == "fontAndGlobalPosition") return trText("Police et position globale", "Lettertype en globale positie", "Schrift und Gesamtposition", "Font and global position");
  if (key == "columns") return trText("Colonnes", "Kolommen", "Spalten", "Columns");
  if (key == "fontGlobal") return trText("Police 0=globale", "Lettertype 0=globaal", "Schrift 0=global", "Font 0=global");
  if (key == "positionX") return trText("Position X", "Positie X", "Position X", "Position X");
  if (key == "positionY") return trText("Position Y", "Positie Y", "Position Y", "Position Y");
  if (key == "timeX") return trText("Heure X", "Tijd X", "Zeit X", "Time X");
  if (key == "infoX") return trText("Info X", "Info X", "Info X", "Info X");
  if (key == "destinationX") return trText("Destination X", "Bestemming X", "Ziel X", "Destination X");
  if (key == "trainX") return trText("Train X", "Trein X", "Zug X", "Train X");
  if (key == "platformX") return trText("Voie X", "Spoor X", "Gleis X", "Platform X");
  if (key == "saveStyle") return trText("Enregistrer ce style", "Deze stijl opslaan", "Diesen Stil speichern", "Save this style");
  if (key == "backStyles") return trText("Retour styles", "Terug naar stijlen", "Zurueck zu Stilen", "Back to styles");
  if (key == "localUpdateConnection") return trText("Connexion locale pour mise a jour", "Lokale verbinding voor update", "Lokale Verbindung fuer Update", "Local connection for update");
  if (key == "savedWifi") return trText("Wi-Fi enregistre", "Opgeslagen Wi-Fi", "Gespeichertes Wi-Fi", "Saved Wi-Fi");
  if (key == "state") return trText("Etat", "Status", "Status", "State");
  if (key == "connected") return trText("connecte", "verbonden", "verbunden", "connected");
  if (key == "disconnected") return trText("non connecte", "niet verbonden", "nicht verbunden", "disconnected");
  if (key == "noNetwork") return trText("Aucun reseau trouve.", "Geen netwerk gevonden.", "Kein Netzwerk gefunden.", "No network found.");
  if (key == "saveAndConnect") return trText("Enregistrer et connecter", "Opslaan en verbinden", "Speichern und verbinden", "Save and connect");
  if (key == "firmware") return trText("Firmware PM3D Ecran Quai", "Firmware PM3D perronscherm", "Firmware PM3D Bahnsteiganzeige", "PM3D platform board firmware");
  if (key == "pm3dWifi") return trText("Wi-Fi PM3D", "PM3D Wi-Fi", "PM3D Wi-Fi", "PM3D Wi-Fi");
  if (key == "localWifi") return trText("Wi-Fi local", "Lokale Wi-Fi", "Lokales Wi-Fi", "Local Wi-Fi");
  if (key == "localSsid") return trText("SSID local", "Lokale SSID", "Lokale SSID", "Local SSID");
  if (key == "firmwareFile") return trText("Firmware .bin", "Firmware .bin", "Firmware .bin", "Firmware .bin");
  if (key == "sendAndUpdate") return trText("Envoyer et mettre a jour", "Verzenden en updaten", "Senden und aktualisieren", "Send and update");
  if (key == "searchLocalWifi") return trText("Rechercher un Wi-Fi local", "Lokale Wi-Fi zoeken", "Lokales Wi-Fi suchen", "Search local Wi-Fi");
  if (key == "updateNote") return trText("La mise a jour peut se faire depuis le Wi-Fi PM3D ou depuis le Wi-Fi local si le module y est connecte. Ne coupe pas l'alimentation pendant l'envoi.", "De update kan via PM3D Wi-Fi of via lokale Wi-Fi als de module verbonden is. Schakel de voeding niet uit tijdens het verzenden.", "Das Update kann ueber PM3D Wi-Fi oder lokales Wi-Fi erfolgen, wenn das Modul verbunden ist. Strom waehrend des Sendens nicht ausschalten.", "The update can use PM3D Wi-Fi or local Wi-Fi if the module is connected. Do not cut power while sending.");
  if (key == "updateDone") return trText("Mise a jour terminee", "Update voltooid", "Update abgeschlossen", "Update complete");
  if (key == "updateError") return trText("Erreur OTA", "OTA-fout", "OTA-Fehler", "OTA error");
  if (key == "restarting") return trText("Redemarrage en cours", "Herstart bezig", "Neustart laeuft", "Restarting");
  if (key == "firmwareNotInstalled") return trText("Firmware non installe", "Firmware niet geinstalleerd", "Firmware nicht installiert", "Firmware not installed");
  if (key == "updateOkMsg") return trText("Le firmware a ete envoye et valide. L'ecran redemarre maintenant.", "De firmware is verzonden en gevalideerd. Het scherm herstart nu.", "Die Firmware wurde gesendet und validiert. Der Bildschirm startet jetzt neu.", "The firmware was sent and validated. The screen is restarting now.");
  if (key == "updateFailMsg") return trText("La mise a jour a echoue. Verifie que le fichier est bien un firmware .bin pour cet ESP32-C3.", "De update is mislukt. Controleer dat het bestand een .bin firmware voor deze ESP32-C3 is.", "Das Update ist fehlgeschlagen. Pruefe, ob die Datei eine .bin Firmware fuer diesen ESP32-C3 ist.", "The update failed. Check that the file is a .bin firmware for this ESP32-C3.");
  if (key == "backUpdate") return trText("Retour mise a jour", "Terug naar update", "Zurueck zum Update", "Back to update");
  if (key == "advancedWarningTitle") return trText("Attention - reglages techniques", "Let op - technische instellingen", "Achtung - technische Einstellungen", "Warning - technical settings");
  if (key == "advancedWarningText") return trText("Cette zone modifie le Wi-Fi PM3D, l'OTA et le calage ST7789. Une mauvaise valeur peut rendre l'ecran illisible ou le module difficile a joindre. Elle est destinee a l'installation initiale ou a la maintenance.", "Deze zone wijzigt PM3D Wi-Fi, OTA en ST7789-uitlijning. Een verkeerde waarde kan het scherm onleesbaar maken of de module moeilijk bereikbaar maken. Ze is bedoeld voor eerste installatie of onderhoud.", "Dieser Bereich aendert PM3D-Wi-Fi, OTA und ST7789-Abgleich. Ein falscher Wert kann die Anzeige unlesbar machen oder das Modul schwer erreichbar machen. Er ist fuer Erstinstallation oder Wartung gedacht.", "This area changes PM3D Wi-Fi, OTA and ST7789 alignment. A wrong value can make the screen unreadable or the module hard to reach. It is intended for initial setup or maintenance.");
  if (key == "cancel") return trText("Annuler", "Annuleren", "Abbrechen", "Cancel");
  if (key == "understandContinue") return trText("Je comprends les risques - Continuer", "Ik begrijp de risico's - Doorgaan", "Ich verstehe die Risiken - Weiter", "I understand the risks - Continue");
  if (key == "themeBlue") return trText("Bleu PM3D", "PM3D blauw", "PM3D Blau", "PM3D blue");
  if (key == "themeOrange") return trText("Orange", "Oranje", "Orange", "Orange");
  if (key == "themeYellow") return trText("Jaune", "Geel", "Gelb", "Yellow");
  if (key == "themeGreen") return trText("Vert", "Groen", "Gruen", "Green");
  if (key == "themeBlack") return trText("Noir", "Zwart", "Schwarz", "Black");
  if (key == "themePurple") return trText("Violet", "Paars", "Violett", "Purple");
  if (key == "themeRed") return trText("Rouge", "Rood", "Rot", "Red");
  if (key == "themeCyan") return trText("Cyan", "Cyaan", "Cyan", "Cyan");
  if (key == "themeIce") return trText("Glace", "IJs", "Eis", "Ice");
  if (key == "themePink") return trText("Rose", "Roze", "Rosa", "Pink");
  if (key == "saveTheme") return trText("Enregistrer le theme", "Thema opslaan", "Design speichern", "Save theme");
  if (key == "back") return trText("Retour", "Terug", "Zurueck", "Back");
  if (key == "themeInterface") return trText("Theme interface web", "Webthema", "Webdesign", "Web theme");
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
  if (country == "fr") return "&#127467;&#127479;";
  if (country == "be") return "&#127463;&#127466;";
  if (country == "de") return "&#127465;&#127466;";
  if (country == "ch") return "&#127464;&#127469;";
  if (country == "uk") return "&#127468;&#127463;";
  if (country == "se") return "&#127480;&#127466;";
  if (country == "jp") return "&#127471;&#127477;";
  if (country == "se") return "&#127480;&#127466;";
  if (country == "jp") return "&#127471;&#127477;";
  if (country == "at") return "&#127462;&#127481;";
  if (country == "nl") return "&#127475;&#127473;";
  if (country == "it") return "&#127470;&#127481;";
  if (country == "hu") return "&#127469;&#127482;";
  if (country == "es") return "&#127466;&#127480;";
  if (country == "pl") return "&#127477;&#127473;";
  if (country == "us") return "&#127482;&#127480;";
  if (country == "in") return "&#127470;&#127475;";
  if (country == "nz") return "&#127475;&#127487;";
  return "&#127757;";
}

String countryName(const String &country) {
  if (country == "fr") return trText("France", "Frankrijk", "Frankreich", "France");
  if (country == "be") return trText("Belgique", "Belgie", "Belgien", "Belgium");
  if (country == "de") return trText("Allemagne", "Duitsland", "Deutschland", "Germany");
  if (country == "ch") return trText("Suisse", "Zwitserland", "Schweiz", "Switzerland");
  if (country == "uk") return trText("Royaume-Uni", "Verenigd Koninkrijk", "Vereinigtes Koenigreich", "United Kingdom");
  if (country == "se") return trText("Suede", "Zweden", "Schweden", "Sweden");
  if (country == "jp") return trText("Japon", "Japan", "Japan", "Japan");
  if (country == "se") return trText("Suede", "Zweden", "Schweden", "Sweden");
  if (country == "jp") return trText("Japon", "Japan", "Japan", "Japan");
  if (country == "at") return trText("Autriche", "Oostenrijk", "Oesterreich", "Austria");
  if (country == "nl") return trText("Pays-Bas", "Nederland", "Niederlande", "Netherlands");
  if (country == "it") return trText("Italie", "Italie", "Italien", "Italy");
  if (country == "hu") return trText("Hongrie", "Hongarije", "Ungarn", "Hungary");
  if (country == "es") return trText("Espagne", "Spanje", "Spanien", "Spain");
  if (country == "pl") return trText("Pologne", "Polen", "Polen", "Poland");
  if (country == "us") return trText("Etats-Unis", "Verenigde Staten", "USA", "United States");
  if (country == "in") return trText("Inde", "India", "Indien", "India");
  if (country == "nz") return trText("Nouvelle-Zelande", "Nieuw-Zeeland", "Neuseeland", "New Zealand");
  return trText("Espagne", "Spanje", "Spanien", "Spain");
}

String profileLabel() {
  if (displayProfile == "fr_sncf_first") return "France - Premier ecran SNCF bleu";
  if (displayProfile == "fr_sncf_old_led") return "France - Vieil ecran LED SNCF";
  if (displayProfile == "fr_sncf_arrivals") return "France - Arrivees SNCF vert";
  if (displayProfile == "fr_sncf_2012") return "France - SNCF grandes gares 2012";
  if (displayProfile == "fr_sncf_valence_side") return "France - SNCF Valence panneau quai 2020s";
  if (displayProfile == "fr_rer_a") return "France - RER A schema de ligne";
  if (displayProfile == "fr_rer_d_8090") return "France - RER D panneau voie 1980-1990";
  if (displayProfile == "fr_transilien_p") return "France - Transilien ligne P";
  if (displayProfile == "fr_transilien_2016") return "France - Transilien departs Ile-de-France 2016";
  if (displayProfile == "fr_rer_90") return "France - SNCF vert COTEP annees 1990/2000";
  if (displayProfile == "fr_rer_orange") return "France - SNCF ecran quai bleu/vert annees 2000";
  if (displayProfile == "fr_transilien") return "France - SNCF/Transilien quai 2019+";
  if (displayProfile == "fr_sncf_green") return "France - SNCF vert arrivees/departs annees 1990/2000";
  if (displayProfile == "fr_sncf_white") return "France - SNCF TER blanc/bleu recent";
  if (displayProfile == "fr_sncf_idf_crt") return "France - SNCF IDF CRT 1990s";
  if (displayProfile == "fr_sncf_1990_flipflap") return "France - SNCF palettes 1990s";
  if (displayProfile == "fr_sncf_depart_grandes_lignes") return "France - SNCF departs Grandes Lignes 2000s";
  if (displayProfile == "fr_sncf_arrivees_grandes_lignes") return "France - SNCF arrivees Grandes Lignes 2000s";
  if (displayProfile == "fr_sncf_2009_led") return "France - SNCF LED orange 2009";
  if (displayProfile == "fr_iena_juvisy") return "France - Juvisy 2010s";
  if (displayProfile == "fr_montparnasse_2010") return "France - Montparnasse 2010";
  if (displayProfile == "fr_montparnasse_tft") return "France - Montparnasse TFT 2010s";
  if (displayProfile == "fr_tgv_1") return "France - TGV bleu 2010s";
  if (displayProfile == "fr_tgv_2") return "France - TGV voie carree 2010s";
  if (displayProfile == "fr_splitflap") return "France - SNCF grand tableau a palettes ancien";
  if (displayProfile == "fr_tgv_amber") return "France - SNCF/TGV tableau ambre ancien";
  if (displayProfile == "fr_led_nice") return "France - SNCF grand panneau bleu multi-colonnes";
  if (displayProfile == "fr_saint_lazare") return "France - Gare Saint-Lazare";
  if (displayProfile == "be_sncb_detail_list") return "Belgique - SNCB/NMBS 2023 detail";
  if (displayProfile == "be_sncb_modern") return "Belgique - SNCB/NMBS 2023 moderne";
  if (displayProfile == "be_sncb_grid") return "Belgique - SNCB/NMBS 2020s grille";
  if (displayProfile == "be_sncb_detail") return "Belgique - SNCB/NMBS ancien jaune";
  if (displayProfile == "de_db_large_blue") return "Allemagne - DB grand tableau bleu photo";
  if (displayProfile == "de_db_orange") return "Allemagne - DB LCD noir/ambre";
  if (displayProfile == "de_db_blue") return "Allemagne - DB tableau bleu avec horloge 2000s/2010s";
  if (displayProfile == "de_db_2022") return "Allemagne - DB bleu bilingue 2022";
  if (displayProfile == "de_baden_baden") return "Allemagne 2025 - Baden-Baden Stadtwerke";
  if (displayProfile == "de_bvg_blue") return "Allemagne - BVG bus bleu";
  if (displayProfile == "de_db_modern") return "Allemagne - DB bleu moderne Abfahrt/Departure 2020s";
  if (displayProfile == "ch_sbb_romandie") return "Suisse - CFF Abfahrt Depart Partenza";
  if (displayProfile == "ch_zurich_fern") return "Suisse - Zurich HB Fernverkehr";
  if (displayProfile == "ch_bern_arrival") return "Suisse - Berne arrivees";
  if (displayProfile == "ch_sbb_splitflap") return "Suisse - CFF/SBB a palettes";
  if (displayProfile == "ch_sbb_blue") return "Suisse - CFF/SBB bleu photo";
  if (displayProfile == "ch_sbb_yellow") return "Suisse - CFF jaune/bleu";
  if (displayProfile == "uk_led_amber") return "Royaume-Uni - Departures LED ambre";
  if (displayProfile == "uk_modern") return "Royaume-Uni - grand depart moderne";
  if (displayProfile == "uk_sheffield") return "Royaume-Uni - Sheffield Train Station";
  if (displayProfile == "at_oebb_green") return "Autriche - OBB vert arrivees photo";
  if (displayProfile == "at_oebb_blue") return "Autriche - OBB bleu departs photo";
  if (displayProfile == "at_oebb_white") return "Autriche - OBB Ankunft blanc/bleu";
  if (displayProfile == "at_oebb_dense") return "Autriche - OBB depart dense bleu";
  if (displayProfile == "at_oebb_teal") return "Autriche - OBB Ankunft vert petrole";
  if (displayProfile == "se_stockholm") return "Suede - Stockholm departures";
  if (displayProfile == "jp_jr_led") return "Japon - Tokyo Shinkansen noir";
  if (displayProfile == "jp_tokyo_grey") return "Japon - Tokyo Shinkansen gris";
  if (displayProfile == "hu_mav_arrivals") return "Hongrie - MAV Erkezo vonatok";
  if (displayProfile == "hu_mav_departures") return "Hongrie - MAV Indulo vonatok";
  if (displayProfile == "nl_ns_dark") return "Pays-Bas - NS dark mode";
  if (displayProfile == "nl_ns_light") return "Pays-Bas - NS clair moderne";
  if (displayProfile == "it_trenitalia") return "Italie - Trenitalia partenze";
  if (displayProfile == "it_fs_blue") return "Italie - FS bleu arrivee/depart";
  if (displayProfile == "it_naples_amber") return "Italie - Naples noir/ambre";
  if (displayProfile == "es_adif_departures") return "Espagne - Adif salidas grande gare";
  if (displayProfile == "es_rodalies_departures") return "Espagne - Rodalies Adif blanc/violet";
  if (displayProfile == "es_barcelona_grid") return "Espagne - Barcelone Rodalies grille";
  if (displayProfile == "es_barcelona_adif") return "Espagne - Barcelone Adif blanc";
  if (displayProfile == "pl_pkp_departures") return "Pologne - PKP Departures";
  if (displayProfile == "us_la_metro") return "Etats-Unis - Amtrak Moynihan";
  if (displayProfile == "us_amtrak_black") return "Etats-Unis - Amtrak departures noir";
  if (displayProfile == "in_indian_railways") return "Inde - Indian Railways";

  if (displayProfile == "es_renfe") return "Espagne - Renfe/Adif";
  if (displayProfile == "es_renfe_split") return "Espagne - Renfe ancien panneau";
  if (displayProfile == "es_alsa") return "Espagne - ALSA autocars";
  return "Style a definir";
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
  return "es";
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
  if (profile == "fr_sncf_first") return "SNCF 1980s";
  if (profile == "fr_sncf_old_led") return "SNCF 1990s";
  if (profile == "fr_sncf_arrivals") return "SNCF 2000s";
  if (profile == "fr_sncf_2012") return "SNCF grandes gares 2012";
  if (profile == "fr_sncf_valence_side") return "SNCF quai Valence 2020s";
  if (profile == "fr_rer_a") return "RATP/SNCF 2010s";
  if (profile == "fr_rer_d_8090") return "RER D 1980-1990";
  if (profile == "fr_transilien_p") return "Transilien 2010s";
  if (profile == "fr_transilien_2016") return "Transilien 2016";
  if (profile == "fr_rer_90") return "SNCF 1990s";
  if (profile == "fr_rer_orange") return "SNCF 2000s";
  if (profile == "fr_transilien") return "Transilien 2019+";
  if (profile == "fr_sncf_green") return "SNCF 1990s";
  if (profile == "fr_sncf_white") return "SNCF/TER 2020s";
  if (profile == "fr_sncf_idf_crt") return "SNCF IDF CRT 1990s";
  if (profile == "fr_sncf_1990_flipflap") return "SNCF palettes 1990s";
  if (profile == "fr_sncf_depart_grandes_lignes") return "SNCF departs GL 2000s";
  if (profile == "fr_sncf_arrivees_grandes_lignes") return "SNCF arrivees GL 2000s";
  if (profile == "fr_sncf_2009_led") return "SNCF LED 2009";
  if (profile == "fr_iena_juvisy") return "Juvisy 2010s";
  if (profile == "fr_montparnasse_2010") return "Montparnasse 2010";
  if (profile == "fr_montparnasse_tft") return "Montparnasse TFT 2010s";
  if (profile == "fr_tgv_1") return "TGV bleu 2010s";
  if (profile == "fr_tgv_2") return "TGV voie carree 2010s";
  if (profile == "fr_splitflap") return "SNCF 1970s";
  if (profile == "fr_tgv_amber") return "SNCF/TGV 1990s";
  if (profile == "fr_led_nice") return "SNCF 2000s";
  if (profile == "fr_saint_lazare") return "SNCF 2010s";
  if (profile == "be_sncb_detail_list") return "SNCB/NMBS 2023 detail";
  if (profile == "be_sncb_modern") return "SNCB/NMBS 2023 moderne";
  if (profile == "be_sncb_grid") return "SNCB/NMBS 2020s grille";
  if (profile == "be_sncb_detail") return "SNCB/NMBS 1990s";
  if (profile == "de_db_large_blue") return "DB 2010s";
  if (profile == "de_db_intercity") return "DB 2004";
  if (profile == "de_db_2010_2015") return "DB 2010-2015";
  if (profile == "de_db_2022") return "DB 2022";
  if (profile == "de_db_orange") return "DB 1990s";
  if (profile == "de_db_blue") return "DB 2000s";
  if (profile == "de_baden_baden") return "Stadtwerke 2025";
  if (profile == "de_bvg_blue") return "BVG 2010s";
  if (profile == "de_db_modern") return "DB 2020s";
  if (profile == "ch_sbb_romandie") return "CFF/SBB 2010s";
  if (profile == "ch_zurich_fern") return "SBB 2020s";
  if (profile == "ch_bern_arrival") return "SBB 2020s";
  if (profile == "ch_sbb_splitflap") return "SBB/CFF 1970s";
  if (profile == "ch_sbb_blue") return "CFF/SBB 2010s";
  if (profile == "ch_sbb_yellow") return "CFF/SBB 2000s";
  if (profile == "uk_led_amber") return "National Rail 1990s";
  if (profile == "uk_modern") return "National Rail 2020s";
  if (profile == "uk_sheffield") return "National Rail 2010s";
  if (profile == "at_oebb_green") return "OBB 2010s";
  if (profile == "at_oebb_blue") return "OBB 2010s";
  if (profile == "at_oebb_white") return "OBB 2020s";
  if (profile == "at_oebb_dense") return "OBB 2010s";
  if (profile == "at_oebb_teal") return "OBB 2020s";
  if (profile == "se_stockholm") return "SL/SJ 2020s";
  if (profile == "jp_jr_led") return "JR 2010s";
  if (profile == "jp_tokyo_grey") return "JR 2020s";
  if (profile == "jp_tokyo_narita") return "Tokyo Narita";
  if (profile == "hu_mav_arrivals") return "MAV 2020s";
  if (profile == "hu_mav_departures") return "MAV 2020s";
  if (profile == "nl_ns_dark") return "NS 2020s";
  if (profile == "nl_ns_light") return "NS 2020s";
  if (profile == "nl_ns_2010_photo") return "NS 2010s";
  if (profile == "it_trenitalia") return "Trenitalia 2020s";
  if (profile == "it_fs_blue") return "FS/Trenitalia 2010s";
  if (profile == "it_fs_amber") return "FS 1990s";
  if (profile == "it_naples_amber") return "FS 2000s";
  if (profile == "es_barcelona_grid") return "Rodalies Barcelone 2010s";
  if (profile == "es_adif_departures") return "Adif Madrid 2020s";
  if (profile == "es_rodalies_departures") return "Adif/Rodalies Seville 2010s";
  if (profile == "es_barcelona_adif") return "Adif Barcelone clair 2020s";
  if (profile == "pl_pkp_departures") return "PKP 2020s";
  if (profile == "us_la_metro") return "Amtrak 2020s";
  if (profile == "us_amtrak_black") return "Amtrak 2000s";
  if (profile == "in_indian_railways") return "Indian Railways 2010s";

  if (profile == "nl_ns_blue") return "NS 1990s/2000s";
  if (profile == "uk_splitflap") return "British Rail 1970s";
  if (profile == "hu_mav_old") return "MAV 1990s/2000s";
  if (profile == "hu_mav_blue") return "MAV 2010s";
  if (profile == "hu_mav_green") return "MAV 2010s";
  if (profile == "es_renfe") return "Renfe/Adif 2020s";
  if (profile == "es_renfe_split") return "Renfe 1990s";
  if (profile == "es_alsa") return "ALSA 2020s";
  return "Style PM3D";
}

String styleMetaText(const String &profile) {
  if (profile == "fr_sncf_first") return "Bleu ancien, colonnes Nom Destination Heure";
  if (profile == "fr_sncf_old_led") return "Noir et ambre, trains retardes/supprimes";
  if (profile == "fr_sncf_arrivals") return "Ecran vert arrivees, TGV/TER, messages terminus";
  if (profile == "fr_sncf_2012") return "Grand ecran bleu, logo train, heure jaune";
  if (profile == "fr_sncf_valence_side") return "Ecran quai bleu/blanc Valence Ville, arrets a droite";
  if (profile == "fr_rer_a") return "Schema de ligne RER A, destination Saint-Germain-en-Laye";
  if (profile == "fr_rer_d_8090") return "Panneau voie A a lampes, RER D Gare de Lyon";
  if (profile == "fr_transilien_p") return "Ligne P, deux colonnes bleues, prochains trains";
  if (profile == "fr_transilien_2016") return "Departs Ile-de-France, deux colonnes bleues, bloc orange";
  if (profile == "fr_rer_90") return "Fond vert, arrivee/depart, voie a droite";
  if (profile == "fr_rer_orange") return "Petit ecran de quai bleu/vert, train n, voie";
  if (profile == "fr_transilien") return "Ecran quai Transilien/SNCF moderne";
  if (profile == "fr_sncf_green") return "Tableau vert SNCF/COTEP arrivees/departs";
  if (profile == "fr_sncf_white") return "Ecran TER recent clair, bleu/blanc";
  if (profile == "fr_sncf_idf_crt") return "CRT Ile-de-France bleu, Nom Destination Heure, quai a droite";
  if (profile == "fr_sncf_1990_flipflap") return "Grand tableau gris a palettes, lettres jaunes";
  if (profile == "fr_sncf_depart_grandes_lignes") return "Departs Grandes Lignes, bandes bleues SNCF, mot departs vertical";
  if (profile == "fr_sncf_arrivees_grandes_lignes") return "Arrivees Grandes Lignes, bandes vertes SNCF, mot arrivees vertical";
  if (profile == "fr_sncf_2009_led") return "Panneau LED orange SNCF 2009, Nom Destination Heure Voie";
  if (profile == "fr_iena_juvisy") return "Grand ecran Juvisy, 3 colonnes Prochain Train et Train Suivant";
  if (profile == "fr_montparnasse_2010") return "Panneau sombre Montparnasse 2010, destination depart voie";
  if (profile == "fr_montparnasse_tft") return "Grand TFT Montparnasse, deux colonnes bleues Ile-de-France";
  if (profile == "fr_tgv_1") return "Panneau TGV bleu, departs verticaux, destinations grandes lignes";
  if (profile == "fr_tgv_2") return "Panneau TGV bleu zoome, voie carree et details via";
  if (profile == "fr_splitflap") return "Ancien tableau mecanique a palettes";
  if (profile == "fr_tgv_amber") return "TGV/TER ancien, brun/noir, lettres ambre";
  if (profile == "fr_led_nice") return "Grand tableau bleu avec destinations en colonnes";
  if (profile == "fr_saint_lazare") return "Grand tableau Ile-de-France Saint-Lazare";
  if (profile == "be_sncb_detail_list") return "Liste SNCB detaillee, arrets, retard rouge, voie a droite";
  if (profile == "be_sncb_modern") return trText("Liste bleue recente, titre Depart, logo B", "Recente blauwe lijst, titel Vertrek, B-logo", "Neue blaue Liste, Titel Abfahrt, B-Logo", "Recent blue list, Departures title, B logo");
  if (profile == "be_sncb_grid") return trText("Grand panneau bleu a 3 colonnes", "Groot blauw bord met 3 kolommen", "Grosse blaue Tafel mit 3 Spalten", "Large blue 3-column board");
  if (profile == "be_sncb_detail") return trText("Ancien tableau noir/gris, texte jaune", "Oud zwart/grijs bord, gele tekst", "Alte schwarz/graue Tafel, gelber Text", "Old black/grey board, yellow text");
  if (profile == "de_db_large_blue") return "Tableau DB bleu type annees 2010: Abfahrt, via, destination, voie";
  if (profile == "de_db_intercity") return "Intercity DB avec message, gare, plan de quai et lignes de correspondance";
  if (profile == "de_db_2010_2015") return "Tableau DB bleu/jaune programmable, Zeit/Nach/Uber/Gleis";
  if (profile == "de_db_2022") return "Tableau DB bleu recent bilingue Abfahrt/Departure, train, via, voie";
  if (profile == "de_db_orange") return "Abfahrt / Departure / Depart, ICE/RE/RB";
  if (profile == "de_db_blue") return "Ancien/moyen bleu DB, horloge, voie a droite";
  if (profile == "de_baden_baden") return "Stadtwerke Baden-Baden, quai Steig 1a, lignes 201/5";
  if (profile == "de_bvg_blue") return "Berlin, ligne 247, minutes";
  if (profile == "de_db_modern") return "Abfahrt / Departure, DB, ICE/RE/RB, voie large";
  if (profile == "ch_sbb_romandie") return "Comme la photo: jaune, tableau bleu, bandeau rouge";
  if (profile == "ch_zurich_fern") return "Zurich HB, Fernverkehr, bandeau rouge";
  if (profile == "ch_bern_arrival") return "Berne: Ankunft/Arrivee/Arrivo, tableau clair arrivees";
  if (profile == "ch_sbb_splitflap") return "Abfahrt / Depart / Partenza a palettes";
  if (profile == "ch_sbb_blue") return "Comme la photo: bleu, lignes CFF, voie/remarque, bandeau rouge";
  if (profile == "ch_sbb_yellow") return "Suisse romande, jaune/bleu, remarques rouges";
  if (profile == "uk_led_amber") return "Departures, texte jaune/orange";
  if (profile == "uk_modern") return "Cartes vert/bleu, boarding, calling at";
  if (profile == "uk_sheffield") return "Grandes lignes grises, ambre, platform et On time";
  if (profile == "at_oebb_green") return "Comme photo: Ankunft/Arrivals, fond vert, voie a droite";
  if (profile == "at_oebb_blue") return "Comme photo: Abfahrt/Departures, fond bleu, lignes blanches";
  if (profile == "at_oebb_white") return "Ankunft Arrival blanc/bleu";
  if (profile == "at_oebb_dense") return "Departures bleu dense";
  if (profile == "at_oebb_teal") return "Ankunft Arrival vert petrole";
  if (profile == "se_stockholm") return "Departures cont. Stockholm";
  if (profile == "jp_jr_led") return "Photo 3: tableau noir Tokaido/Sanyo";
  if (profile == "jp_tokyo_grey") return "Photo 4: panneau gris Next Departure";
  if (profile == "hu_mav_arrivals") return "Budapest arrivals MAV photo";
  if (profile == "hu_mav_departures") return "Budapest departures MAV photo";
  if (profile == "nl_ns_dark") return "Bleu nuit moderne, texte blanc/jaune";
  if (profile == "nl_ns_light") return "NS clair, quais, Intercity/Sprinter";
  if (profile == "it_trenitalia") return "Partenze/Arrivi, binario, ritardo";
  if (profile == "it_fs_blue") return "FS Trenitalia bleu, arrivo et partenza";
  if (profile == "it_fs_amber") return "Fond noir, texte ambre/orange, tableau italien ancien";
  if (profile == "it_naples_amber") return "Photo Naples: horloge analogique, colonnes orange";
  if (profile == "es_adif_departures") return "Madrid Atocha Adif blanc/vert, style 2020s";
  if (profile == "es_rodalies_departures") return "Seville Adif noir/orange, style 2010s";
  if (profile == "es_barcelona_grid") return "Rodalies Barcelone grille verte, style 2010s";
  if (profile == "es_barcelona_adif") return "Barcelone Adif clair, liste compacte 2020s";
  if (profile == "pl_pkp_departures") return "Photo: Odjazdy violet, horloge centrale";
  if (profile == "us_la_metro") return "Amtrak bleu Moynihan Train Hall";
  if (profile == "us_amtrak_black") return "Amtrak noir, DEPARTURES blanc, bande rouge EWR";
  if (profile == "in_indian_railways") return "Tableau Indian Railways noir, texte ambre/vert";

  if (profile == "nl_ns_blue") return "Ancien style NS bleu, texte clair, depart/voie";
  if (profile == "uk_splitflap") return "Noir/blanc, horaires et destinations a palettes";
  if (profile == "hu_mav_old") return "Grand panneau bleu fonce avec enorme horloge";
  if (profile == "hu_mav_blue") return "Indulo vonatok, lignes bleues modernes, voie a droite";
  if (profile == "hu_mav_green") return "Erkezo vonatok, arrivals, theme vert MAV";
  if (profile == "es_renfe") return "Panel salida/llegada moderne";
  if (profile == "es_renfe_split") return "Renfe ancien, fond beige, texte ambre";
  if (profile == "es_alsa") return "Autocars ALSA, depart/arrivee magenta";
  return "Base libre a completer";
}

String defaultOrderForCountry(const String &country) {
  // Bibliotheque remplie uniquement avec les styles donnes par Benoit.
  if (country == "be") return "be_sncb_detail_list,be_sncb_modern,be_sncb_grid,be_sncb_detail";
  if (country == "fr") return "fr_rer_d_8090,fr_sncf_idf_crt,fr_sncf_1990_flipflap,fr_sncf_depart_grandes_lignes,fr_sncf_arrivees_grandes_lignes,fr_sncf_2009_led,fr_montparnasse_2010,fr_sncf_2012,fr_iena_juvisy,fr_montparnasse_tft,fr_tgv_1,fr_tgv_2,fr_sncf_valence_side";
  if (country == "de") return "de_db_intercity,de_db_2010_2015,de_db_2022,de_db_large_blue,de_baden_baden";
  if (country == "it") return "it_naples_amber,it_fs_blue,it_fs_amber,it_trenitalia";
  if (country == "nl") return "nl_ns_2010_photo,nl_ns_light,nl_ns_blue,nl_ns_dark";
  if (country == "uk") return "uk_sheffield,uk_splitflap,uk_led_amber,uk_modern";
  if (country == "hu") return "hu_mav_arrivals,hu_mav_departures";
  if (country == "ch") return "ch_sbb_romandie,ch_zurich_fern,ch_bern_arrival";
  if (country == "at") return "at_oebb_blue,at_oebb_green,at_oebb_white,at_oebb_dense,at_oebb_teal";
  if (country == "se") return "se_stockholm";
  if (country == "jp") return "jp_jr_led,jp_tokyo_grey,jp_tokyo_narita";
  if (country == "es") return "es_barcelona_grid,es_adif_departures,es_barcelona_adif,es_rodalies_departures";
  if (country == "pl") return "pl_pkp_departures";
  if (country == "us") return "us_la_metro,us_amtrak_black";
  if (country == "in") return "in_indian_railways";
  if (country == "nz") return "";
  return "";
}

bool orderHasProfile(const String &order, const String &profile) {
  return ("," + order + ",").indexOf("," + profile + ",") >= 0;
}

String cleanCountryOrder(const String &country, const String &saved) {
  if (country == "fav") {
    return "";
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
  if (country == "fr") order = orderFr;
  else if (country == "be") order = orderBe;
  else if (country == "de") order = orderDe;
  else if (country == "ch") order = orderCh;
  else if (country == "uk") order = orderUk;
  else if (country == "at") order = orderAt;
  else if (country == "nl") order = orderNl;
  else if (country == "it") order = orderIt;
  else if (country == "es") order = orderEs;
  else if (country == "pl" || country == "us" || country == "in") order = orderOther;
  else order = orderOther;
  order = cleanCountryOrder(country, order);
  return order;
}

void setCountryOrder(const String &country, const String &order) {
  if (country == "fr") orderFr = order;
  else if (country == "be") orderBe = order;
  else if (country == "de") orderDe = order;
  else if (country == "ch") orderCh = order;
  else if (country == "uk") orderUk = order;
  else if (country == "at") orderAt = order;
  else if (country == "nl") orderNl = order;
  else if (country == "it") orderIt = order;
  else if (country == "es") orderEs = order;
  else if (country == "pl" || country == "us" || country == "in") orderOther = order;
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
  String items[24];
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
  String h;
  h += "<div class='styleItem'>";
  h += "<form class='styleForm' method='POST' action='/setprofile'><input type='hidden' name='profile' value='" + profile + "'><input type='hidden' name='back' value='" + country + "'><button class='" + cls + "' type='submit'>" + styleTitle(profile) + "</button></form>";
  h += "</div>";
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
    if (profileCountry(profile) != country) continue;
    h += styleEntry(country, profile);
  }
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
  html += ".logo{display:block;width:180px;max-width:70%;height:auto;margin:0 auto;animation:spinLogo 6s ease-in-out forwards, glowLogo 1.4s ease-in-out infinite alternate;filter:drop-shadow(0 18px 24px rgba(0,0,0,.55));}";
  html += ".gateStamp{position:absolute;left:50%;top:50%;z-index:3;opacity:0;transform:translate(-50%,-50%) rotate(-13deg) scale(.65);padding:8px 16px;border:4px solid rgba(255,70,70,.92);border-radius:10px;color:#ff5b5b;background:rgba(255,255,255,.04);font-size:24px;font-weight:1000;letter-spacing:2px;text-transform:uppercase;text-shadow:0 2px 0 rgba(0,0,0,.65);box-shadow:0 0 18px rgba(255,40,40,.35),inset 0 0 12px rgba(255,40,40,.18);animation:stampGate 6s ease-out forwards;}";
  html += ".gateTwo{position:absolute;left:50%;top:calc(50% + 30px);z-index:4;opacity:0;transform:translate(-50%,-50%) rotate(-13deg) scale(.35);color:#ff1f1f;font-size:34px;font-weight:1000;line-height:1;text-shadow:0 2px 0 rgba(0,0,0,.75),0 0 15px rgba(255,0,0,.75);animation:stampTwo 6s ease-out forwards;}";
  html += "@keyframes stampGate{0%,49%{opacity:0;transform:translate(-50%,-50%) rotate(-13deg) scale(.65);}52%{opacity:1;transform:translate(-50%,-50%) rotate(-13deg) scale(1.18);}58%,100%{opacity:1;transform:translate(-50%,-50%) rotate(-13deg) scale(1);}}";
  html += "@keyframes stampTwo{0%,84%{opacity:0;transform:translate(-50%,-50%) rotate(-13deg) scale(.35);}88%{opacity:1;transform:translate(-50%,-50%) rotate(-13deg) scale(1.35);}94%,100%{opacity:1;transform:translate(-50%,-50%) rotate(-13deg) scale(1);}}";
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
  html += ".pm3dTickerBox:before{content:\'\';position:absolute;inset:0;background:linear-gradient(90deg,transparent,rgba(130,220,255,.18),transparent);animation:scanLight 2.2s linear infinite;z-index:2;pointer-events:none;}";
  html += ".pm3dTicker{position:absolute;white-space:nowrap;left:-70%;top:50%;transform:translateY(-50%);font-size:23px;font-weight:1000;letter-spacing:5px;text-transform:uppercase;color:#dff7ff;text-shadow:0 1px 0 #7fa9bf,0 2px 0 #31546a,0 5px 12px rgba(0,0,0,.75),0 0 18px rgba(95,210,255,.95);animation:tickerPm3d 6s linear infinite;}";
  html += "@keyframes tickerPm3d{0%{left:-75%;}100%{left:115%;}}@keyframes scanLight{0%{transform:translateX(-100%);}100%{transform:translateX(100%);}}";
  html += "</style>";
  html += "<script>";
  html += "function showAdvancedWarning(){document.getElementById(\'advModal\').style.display=\'flex\';return false;}function hideAdvancedWarning(){document.getElementById(\'advModal\').style.display=\'none\';}";
  html += "let p=0;const msgs=['Initialisation du systeme PM3D','Verification de l ecran','Chargement des departs','Chargement de l interface','Pret au depart'];";
  html += "function tick(){p=Math.min(100,Math.round((performance.now()-t0)/6000*100));document.getElementById('pct').innerText=p+'%';document.getElementById('steps').innerText=msgs[Math.min(msgs.length-1,Math.floor(p/25))];if(p<100)requestAnimationFrame(tick);else setTimeout(()=>{location.replace('/countries');},350);}";
  html += "let t0;window.onload=()=>{t0=performance.now();tick();};";
  html += "</script></head><body>";
  html += "<div class='intro'>";
  html += "<div class='logoZone'>";  html += "<img class='logo' src='data:image/png;base64,";
  html += FPSTR(PM3D_LOGO_B64);
  html += "'>";
html += "<div class='gateStamp'>Display</div><div class='gateTwo'>2</div></div>";
  html += "<div class='title'>PM3D Display</div>";
  html += "<div class='phrase'>" + htmlEscape(currentLang == "FR" ? "Systeme autonome PM3D pour ecran de quai miniature" : (currentLang == "NL" ? "Autonoom PM3D-systeem voor miniatuurperrondisplay" : (currentLang == "DE" ? "Autonomes PM3D-System fuer miniature Bahnsteiganzeige" : "Standalone PM3D system for miniature platform display"))) + "<br>" + htmlEscape(currentLang == "FR" ? "Tableau des departs et messages libres" : (currentLang == "NL" ? "Vertrekbord en vrije berichten" : (currentLang == "DE" ? "Abfahrtstafel und freie Nachrichten" : "Departure board and free messages"))) + "</div>";
  html += "<div class='pm3dTickerBox'><div class='pm3dTicker'>PM3D.NET &nbsp; &bull; &nbsp; PM3D.NET &nbsp; &bull; &nbsp; PM3D.NET</div></div>";
  html += "<div class='bar'><div class='fill'></div></div>";
  html += "<div id='pct' class='pct'>0%</div>";
  html += "<div id='steps' class='steps'>Initialisation du systeme PM3D</div>";
  html += "</div></body></html>";
  return html;
}

String mainPage() {
  String h = pageStart(trKey("home"));
  h += "<div class='card homeHero'>" + logoHeader(trKey("appName"), trKey("trainTable")) + "";
  h += "<div class='pm3dStatus'><div class='statusTile'><div class='statusLabel'>" + trKey("currentStyle") + "</div><div class='statusValue'>" + profileLabel() + "</div></div>";
  h += "<div class='statusTile'><div class='statusLabel'>" + trKey("lineCount") + "</div><div class='statusValue'>" + String(nbVisible) + " / " + String(profileMaxVisibleRows(displayProfile)) + "</div></div></div>";
  h += "<div class='hint'>" + trText("Choisis un style, ajuste les lignes lisibles, puis modifie les trains. PM3D garde les reglages apres redemarrage et OTA.", "Kies een stijl, stel leesbare regels in en wijzig daarna de treinen. PM3D bewaart de instellingen na herstart en OTA.", "Waehle einen Stil, stelle lesbare Zeilen ein und bearbeite dann die Zuege. PM3D behaelt die Einstellungen nach Neustart und OTA.", "Choose a style, tune readable rows, then edit trains. PM3D keeps settings after restart and OTA.") + "</div>";
  h += "<div class='compactActions'><a class='btn' href='/settings'>" + trKey("screenEdit") + "</a><a class='btn' href='/countries'>" + trKey("countries") + "</a><a class='btn' href='/updates'>" + trKey("update") + "</a><a class='btn' href='/config'>" + trKey("settings") + "</a></div>";
  h += "</div>" + pageEnd();
  return h;
}

String configMenuPage() {
  String h = pageStart(trKey("settings"));
  h += "<div class='card'>" + logoHeader(trKey("settings"), "PM3D Ecran Quai") + "";
  h += "<div class='hint'>" + trText("Les actions courantes sont devant. Les reglages avances restent proteges pour eviter un mauvais calage ecran.", "De dagelijkse acties staan vooraan. Geavanceerde instellingen blijven beschermd om verkeerde schermuitlijning te vermijden.", "Die haeufigen Aktionen stehen vorne. Erweiterte Einstellungen bleiben geschuetzt, um falschen Bildschirmabgleich zu vermeiden.", "Common actions are first. Advanced settings stay protected to avoid bad screen alignment.") + "</div>";
  h += "<div class='compactActions'><a class='btn' href='/settings'>" + trKey("screenEdit") + "</a><a class='btn' href='/countries'>" + trKey("countries") + "</a><a class='btn' href='/updates'>" + trKey("update") + "</a><button type='button' onclick='showAdvancedWarning()'>" + trKey("advanced") + "</button></div>";
  h += "<a class='btn' href='/main'>" + trKey("backHome") + "</a>";
  h += "</div>" + pageEnd();
  return h;
}

String profileSelectOptions(const String &selected) {
  const char* profiles[] = {"fr_rer_d_8090","fr_sncf_idf_crt","fr_sncf_1990_flipflap","fr_sncf_depart_grandes_lignes","fr_sncf_arrivees_grandes_lignes","fr_sncf_2009_led","fr_iena_juvisy","fr_montparnasse_2010","fr_montparnasse_tft","fr_tgv_1","fr_tgv_2","fr_sncf_2012","fr_sncf_valence_side","be_sncb_detail_list","be_sncb_modern","be_sncb_detail","de_db_2010_2015","de_baden_baden","ch_sbb_romandie","ch_zurich_fern","ch_bern_arrival","uk_sheffield","uk_modern","at_oebb_blue","at_oebb_green","at_oebb_white","at_oebb_dense","at_oebb_teal","nl_ns_2010_photo","nl_ns_light","nl_ns_blue","nl_ns_dark","it_naples_amber","it_fs_blue","it_fs_amber","hu_mav_arrivals","hu_mav_departures","se_stockholm","jp_jr_led","jp_tokyo_grey","jp_tokyo_narita","es_adif_departures","es_rodalies_departures","es_barcelona_grid","es_barcelona_adif","pl_pkp_departures","us_la_metro","us_amtrak_black","in_indian_railways"};
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
  h += "<div class='pm3dStatus'><div class='statusTile'><div class='statusLabel'>" + trKey("currentStyle") + "</div><div class='statusValue'>" + profileLabel() + "</div></div>";
  h += "<div class='statusTile'><div class='statusLabel'>" + trKey("visibleRows") + "</div><div class='statusValue'>" + String(nbVisible) + " / " + String(profileMaxVisibleRows(displayProfile)) + "</div></div></div>";
  h += "<div class='compactActions'><a class='btn' href='/countries'>" + trKey("changeCountry") + "</a><a class='btn' href='/stylecfg?p=" + displayProfile + "&back=" + profileCountry(displayProfile) + "'>" + trKey("styleSettings") + "</a></div>";
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
  if (displayProfile == "ch_sbb_romandie" || displayProfile == "ch_sbb_blue" || displayProfile == "ch_sbb_yellow") {
    h += "<div class='section'>Message CFF/SBB</div><div class='settingsGrid'>";
    h += "<label>Bandeau rouge<input name='swissMsg' value='" + htmlEscape(swissInfoMessage) + "' maxlength='140'></label>";
    h += "<div class='hint'>Maximum 140 caracteres. Si le texte est trop long pour le cadre rouge, il defile automatiquement.</div></div>";
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
    h += "<div id='pmTrainLimitModal' class='advModal'><div class='advBox'><div class='advTitle'><span class='advIcon'><span>!</span></span>" + trKey("memoryManageTitle") + "</div>";
    h += "<div class='advText'><b>" + trKey("memoryNoSpaceLong") + "</b><br><br>" + trKey("memoryOtaRisk") + "</div>";
    h += "<div class='advBtns'><button type='button' onclick='pmCloseTrainLimit()'>" + trKey("close") + "</button><button type='button' onclick=\"location.href='/updates'\">" + trKey("update") + "</button><button type='button' onclick=\"location.href='/packmanager'\">" + trKey("manageCountries") + "</button></div></div></div>";
    h += "<script>function pmCloseTrainLimit(){var m=document.getElementById('pmTrainLimitModal');if(m)m.style.display='none';}function pmShowTrainLimit(){var m=document.getElementById('pmTrainLimitModal');if(m)m.style.display='flex';else alert('" + trKey("memoryNoSpaceLong") + "');}function pmAddTrain(){var warn=\"" + trKey("memoryWarn") + "\\n\\n" + trKey("memoryOtaRisk") + "\";var n=document.querySelector('input[name=nb]');var max=n?parseInt(n.max||'7'):7;var current=n?parseInt(n.value||'3'):3;var row=null;var idx=-1;for(var k=0;k<max;k++){var c=document.getElementById('row'+k);if(c&&(c.style.display=='none'||getComputedStyle(c).display=='none')){row=c;idx=k;break;}}if(!row){pmShowTrainLimit();return;}if(!confirm(warn)){return;}row.style.display='grid';var xs=row.querySelectorAll('input');for(var i=0;i<xs.length;i++){xs[i].value='';}if(n){n.value=Math.max(current,idx+1);}row.scrollIntoView({behavior:'smooth',block:'center'});}</script>";
  }
  h += "<div class='section'>" + trKey("displaySettings") + "</div>";
  h += "<div class='settingsLine settingsLineWide'>";
  if (displayProfile == "de_baden_baden") {
    h += "<label>Voie / Steig<input name='badenSt' value='" + htmlEscape(badenSteig) + "' maxlength='6'></label>";
  }
  int maxVisibleForStyle = profileMaxVisibleRows(displayProfile);
  h += "<label>" + trKey("visibleRows") + "<input id='pmNb' name='nb' type='number' min='1' max='" + String(maxVisibleForStyle) + "' value='" + String(nbVisible) + "' onchange='pmLiveRows(true)' oninput='pmLiveRows(true)'></label>";
  h += "<label>" + trKey("fontSizeBoard") + "<input id='pmFont' name='font' type='number' min='1' max='3.8' step='0.1' value='" + fontUiValue(displayFontSize) + "' onchange='pmLiveRows(false)' oninput='pmLiveRows(false)'></label>";
  h += "<label>" + trKey("scrollDelay") + "<input id='pmScroll' name='scrollMs' type='number' min='500' max='15000' step='100' value='" + String(scrollDelayMs) + "' onchange='pmLiveRows(false)' oninput='pmLiveRows(false)'></label>";
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
  h += "<div class='hint' id='pmLiveState'>" + trText("Ajustement instantane actif : lignes et police changent directement sur le TFT.", "Live-aanpassing actief: regels en lettertype wijzigen direct op het TFT.", "Live-Anpassung aktiv: Zeilen und Schrift wechseln direkt auf dem TFT.", "Live tuning active: rows and font update directly on the TFT.") + "</div>";
  h += "<script>var pmLiveTimer=0;function pmAutoFont(n){n=parseInt(n||'0');if(n<=2)return '2.0';if(n<=3)return '1.8';if(n<=6)return '1.3';return '1.0';}function pmLiveRows(autoFont){clearTimeout(pmLiveTimer);pmLiveTimer=setTimeout(function(){var nb=document.getElementById('pmNb');var ft=document.getElementById('pmFont');var st=document.getElementById('pmLiveState');if(!nb||!ft)return;if(autoFont){ft.value=pmAutoFont(nb.value);}var bright=document.querySelector('select[name=bright]');var sc=document.getElementById('pmScroll');var url='/live?nb='+encodeURIComponent(nb.value)+'&font='+encodeURIComponent(ft.value);if(bright)url+='&bright='+encodeURIComponent(bright.value);if(sc)url+='&scrollMs='+encodeURIComponent(sc.value);fetch(url,{cache:'no-store'}).then(function(r){return r.text();}).then(function(t){if(st)st.innerText=t;}).catch(function(){if(st)st.innerText='Live indisponible';});},180);}document.addEventListener('change',function(e){if(e.target&&e.target.name=='bright')pmLiveRows(false);});</script>";
  h += "<button type='submit'>" + trKey("saveSettings") + "</button></div>";
  h += "<div class='card'><div class='section'>" + trKey("departures") + "</div><div class='trainList'>";
  int editableRows = displayProfile == "de_db_intercity" ? profileMaxVisibleRows(displayProfile) : MAX_ROWS;
  for (int i = 0; i < editableRows; i++) {
    String extraStyle = (displayProfile == "de_db_intercity" && i >= nbVisible) ? " style='display:none'" : "";
    h += "<div class='trainCard' id='row" + String(i) + "'" + extraStyle + ">";
    h += "<div class='trainTop'><div class='trainNum'>Train " + String(i + 1) + "</div><div class='tiny'>" + styleTitle(displayProfile) + "</div></div>";
    h += "<div class='trainMini'>";
    h += "<label class='fieldLbl'>" + trKey("time") + "<input name='h" + String(i) + "' value='" + htmlEscape(rows[i].heure) + "' placeholder='08:12'></label>";
    h += "<label class='fieldLbl'>Retard<input name='r" + String(i) + "' value='" + htmlEscape(rows[i].retard) + "' placeholder='+5'></label>";
    h += "<label class='fieldLbl'>" + trKey("train") + "<input name='t" + String(i) + "' value='" + htmlEscape(rows[i].typeTrain) + "' placeholder='IC'></label>";
    h += "<label class='fieldLbl'>" + trKey("platform") + "<input name='v" + String(i) + "' value='" + htmlEscape(rows[i].voie) + "' placeholder='4'></label>";
    h += "</div><div class='trainWide'>";
    h += "<label class='fieldLbl fieldDestination'>" + trKey("destination") + "<input name='d" + String(i) + "' value='" + htmlEscape(rows[i].destination) + "' placeholder='" + trKey("destination") + "'></label>";
    h += "<label class='fieldLbl fieldDestination'>" + trKey("info") + " / Via<input name='i" + String(i) + "' value='" + htmlEscape(rows[i].info) + "' placeholder='via ...'></label>";
    h += "</div>";
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

String previewClassForProfile(const String &profile) {
  if (profile.startsWith("be_")) return "prevSncb";
  if (profile == "fr_transilien" || profile == "fr_transilien_2016" || profile == "fr_transilien_p" || profile == "fr_rer_a") return "prevTransilien";
  if (profile.startsWith("fr_")) return "prevSncf";
  if (profile.startsWith("de_")) return "prevDb";
  if (profile.startsWith("ch_")) return "prevSbb";
  if (profile.startsWith("at_")) return "prevOebb";
  if (profile.indexOf("amber") >= 0 || profile.indexOf("split") >= 0 || profile == "uk_led_amber") return "prevAmber";
  if (profile == "nl_ns_light" || profile == "es_barcelona_adif" || profile == "ch_bern_arrival") return "prevLight";
  return "prevGeneric";
}

String previewTitleForProfile(const String &profile) {
  if (profile.startsWith("be_")) return "SNCB NMBS";
  if (profile.startsWith("fr_")) return "SNCF / Transilien";
  if (profile.startsWith("de_")) return "DB Abfahrt";
  if (profile.startsWith("ch_")) return "SBB CFF FFS";
  if (profile.startsWith("at_")) return "OBB Abfahrt";
  if (profile.startsWith("nl_")) return "NS Vertrektijden";
  if (profile.startsWith("uk_")) return "DEPARTURES";
  if (profile.startsWith("it_")) return "PARTENZE";
  if (profile.startsWith("es_")) return "Salidas";
  return styleTitle(profile);
}

String qrColorOptions(int selected) {
  const char* labels[] = {"Noir","Blanc","Cyan","Jaune","Vert","Bleu","Ambre","Gris"};
  String h;
  for (int i = 0; i < 8; i++) {
    h += "<option value='" + String(i) + "'";
    if (i == selected) h += " selected";
    h += ">" + String(labels[i]) + "</option>";
  }
  return h;
}

String qrBodyStyleOptions(int selected) {
  const char* labels[] = {"Carres pleins","Petits carres","Ronds","Petits points","Losanges","Barres horizontales","Barres verticales","Croix","Pixels alternes","Fin net","Point fin net","Trait extra fin"};
  String h;
  for (int i = 0; i < 12; i++) {
    h += "<option value='" + String(i) + "'";
    if (i == selected) h += " selected";
    h += ">" + String(labels[i]) + "</option>";
  }
  return h;
}

String qrMarkerStyleOptions(int selected) {
  const char* labels[] = {"Carres classiques","Coins arrondis","Ronds","Losanges","Contour fin","Bloc plein"};
  String h;
  for (int i = 0; i < 6; i++) {
    h += "<option value='" + String(i) + "'";
    if (i == selected) h += " selected";
    h += ">" + String(labels[i]) + "</option>";
  }
  return h;
}

String advancedPage() {
  String h = pageStart(trKey("advanced"));
  h += "<div class='advCompact'><div class='card'>" + logoHeader(trKey("advanced"), "PM3D") + "";
  h += "<div class='advNotice'><b>" + trKey("advancedWarningTitle") + "</b><br>" + trKey("advancedWarningText") + "</div>";
  h += "<div class='section'>" + trKey("connection") + "</div><div class='advCards'>";
  h += "<div class='advCard'><div class='advCardTitle'>Wi-Fi PM3D</div><div class='advCardText'>SSID : " + htmlEscape(apSSID) + "<br>" + trKey("password") + " : " + String(apPass.length() >= 8 ? trText("defini", "ingesteld", "gesetzt", "set") : trKey("none")) + "</div></div>";
  h += "<div class='advCard'><div class='advCardTitle'>" + trKey("browserAddress") + "</div><div class='advCardText'>" + apUrl() + "</div></div>";
  h += "<div class='advCard'><div class='advCardTitle'>" + trKey("currentStyle") + "</div><div class='advCardText'>" + profileLabel() + "<br>" + trKey("lineCount") + " : " + String(nbVisible) + "</div></div>";
  h += "<div class='advCard'><div class='advCardTitle'>" + trKey("displaySettings") + "</div><div class='advCardText'>" + trKey("scrollDelayShort") + " : " + String(scrollDelayMs) + " ms<br>" + trKey("brightnessShort") + " : " + String(screenBrightness) + "</div></div>";
  h += "</div>";
  h += "<form method='POST' action='/saveadvanced'>";
  h += "<div class='section'>" + trText("Securite Wi-Fi PM3D", "PM3D Wi-Fi beveiliging", "PM3D-Wi-Fi Sicherheit", "PM3D Wi-Fi security") + "</div>";
  h += "<div class='sub'><b>SSID :</b> " + htmlEscape(apSSID) + "<div class='hint'>" + trText("L'identifiant Wi-Fi reste fixe et ne peut pas etre modifie ici. Seul le mot de passe peut etre ajoute ou modifie.", "De Wi-Fi-naam blijft vast en kan hier niet worden gewijzigd. Alleen het wachtwoord kan worden toegevoegd of gewijzigd.", "Der WLAN-Name bleibt fest und kann hier nicht geaendert werden. Nur das Passwort kann hinzugefuegt oder geaendert werden.", "The Wi-Fi name stays fixed and cannot be changed here. Only the password can be added or changed.") + "</div>";
  h += "<label>" + trKey("password") + "<input name='apPass' type='password' minlength='8' maxlength='63' placeholder='8 a 63 caracteres'></label><div class='dangerText'>" + trText("Laisse vide pour garder le mot de passe actuel. Si tu le changes, reconnecte-toi ensuite au Wi-Fi PM3D avec ce nouveau mot de passe.", "Laat leeg om het huidige wachtwoord te behouden. Als je het wijzigt, verbind daarna opnieuw met PM3D Wi-Fi met dit nieuwe wachtwoord.", "Leer lassen, um das aktuelle Passwort zu behalten. Wenn du es aenderst, verbinde dich danach erneut mit dem PM3D-Wi-Fi.", "Leave empty to keep the current password. If you change it, reconnect to PM3D Wi-Fi with the new password.") + "</div></div>";
  h += "<details><summary class='section'>" + trKey("screenCalibration") + "</summary><div class='hint'>" + trText("Ces valeurs deplacent la zone visible du ST7789. Modifie par petits pas, puis enregistre.", "Deze waarden verplaatsen het zichtbare ST7789-gebied. Wijzig in kleine stappen en sla daarna op.", "Diese Werte verschieben den sichtbaren ST7789-Bereich. In kleinen Schritten aendern und dann speichern.", "These values move the visible ST7789 area. Change them in small steps, then save.") + "</div><div class='settingsGrid'>";
  h += stepperControl("X1", "ox1", tftOffsetX1, 0, 120, 1);
  h += stepperControl("Y1", "oy1", tftOffsetY1, 0, 120, 1);
  h += stepperControl("X2", "ox2", tftOffsetX2, 0, 120, 1);
  h += stepperControl("Y2", "oy2", tftOffsetY2, 0, 120, 1);
  h += stepperControl("Largeur", "pw", tftPanelW, 120, 240, 1);
  h += stepperControl("Hauteur", "ph", tftPanelH, 120, 260, 1);
  h += "</div></details>";
  h += "<div class='compactActions'><button type='submit'>" + trKey("saveScreenCalibration") + "</button></div><div class='tiny'>" + trKey("defaultCalibrationNote") + "</div></form>" + stepperScript();
  h += "<div class='section'>" + trText("Maintenance", "Onderhoud", "Wartung", "Maintenance") + "</div>";
  h += "<div class='advActions'><a class='btn' href='/wifiscan'>" + trKey("wifiSearch") + "</a><a class='btn' href='/updates'>" + trKey("update") + "</a><a class='btn' href='/settings'>" + trKey("screenEdit") + "</a><a class='btn' href='/config'>" + trKey("settings") + "</a></div>";
  h += "<div class='section'>Mode demo</div><div class='hint'>Affiche le drapeau du pays 3 secondes puis chaque ecran disponible 5 secondes. Se coupe automatiquement quand tu choisis un ecran manuellement.</div>";
  h += "<div class='compactActions'><a class='btn' href='/demostart'>Activer demo</a><a class='btn' href='/demostop'>Desactiver demo</a></div>";
  h += "</div></div>" + pageEnd();
  return h;
}

String countriesPage() {
  String h = pageStart(trKey("countries"));
  h += "<div class='card'>" + logoHeader(trKey("countries"), trKey("chooseFamily")) + "";
  h += "<div class='countryGrid'>";
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
  h += countryButton("pl");
  h += countryButton("se");
  h += countryButton("us");
  h += countryButton("uk");
  h += "</div></div>" + pageEnd();
  return h;
}

String tablePreviewHtml(const String &profile, const String &country) {
  String h;
  String cls = previewClassForProfile(profile);
  h += "<div class='card tablePreview compactPreview'><div class='previewTitle'>" + styleTitle(profile) + "<a class='btn miniMove tableGear' title='" + trKey("styleGear") + "' href='/stylecfg?p=" + profile + "&back=" + country + "'>&#9881;</a></div>";
  h += "<div class='screenPreview'><div id='pmMirrorPreview' class='tftMock " + cls + "'>";
  h += "<div class='tftHead'><span id='pmMirrorTitle'>" + htmlEscape(previewTitleForProfile(profile)) + "</span><span id='pmMirrorClock' class='tftClock'>" + htmlEscape(rows[visibleRowIndex(scrollOffset)].heure.length() ? rows[visibleRowIndex(scrollOffset)].heure : String("12:00")) + "</span></div>";
  h += "<div id='pmMirrorRows' class='tftRows'>";
  int shown = min(nbVisible, 8);
  for (int i = 0; i < shown; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    h += "<div class='tftLine'><span class='tftTime'>" + htmlEscape(rows[idx].heure) + "</span><span class='tftDest'>" + htmlEscape(rows[idx].destination) + "</span><span class='tftInfo'>" + htmlEscape(rows[idx].typeTrain) + "</span><span class='tftTrack'>" + htmlEscape(rows[idx].voie) + "</span></div>";
  }
  h += "</div></div></div>";
  h += "<script>var pmMirrorTimer=0;function pmEsc(s){return String(s||'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/\"/g,'&quot;');}function pmMirrorTick(){fetch('/previewdata',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){var title=document.getElementById('pmMirrorTitle');var clock=document.getElementById('pmMirrorClock');var box=document.getElementById('pmMirrorRows');if(title)title.textContent=d.title||'';if(clock)clock.textContent=d.clock||'';if(box){var html='';for(var i=0;i<d.rows.length;i++){var row=d.rows[i];html+='<div class=\"tftLine\"><span class=\"tftTime\">'+pmEsc(row.h)+'</span><span class=\"tftDest\">'+pmEsc(row.d)+'</span><span class=\"tftInfo\">'+pmEsc(row.t)+'</span><span class=\"tftTrack\">'+pmEsc(row.v)+'</span></div>';}box.innerHTML=html;}var delay=Math.max(350,Math.min(parseInt(d.delay||1000),1200));clearTimeout(pmMirrorTimer);pmMirrorTimer=setTimeout(pmMirrorTick,delay);}).catch(function(){pmMirrorTimer=setTimeout(pmMirrorTick,1500);});}pmMirrorTick();</script>";
  h += "<a class='btn' href='/settings'>" + trKey("screenEdit") + "</a></div>";
  return h;
}
String countryPage() {
  String country = server.hasArg("c") ? server.arg("c") : "fr";
  if (country == "fav" || country == "other") country = "fr";
  String h = pageStart(countryName(country));
  h += "<div class='card'>" + logoHeader(countryFlag(country) + " " + countryName(country), trKey("chooseStyle")) + "";
  h += "<div class='styleGrid'>" + countryStyles(country) + "</div>";
  h += "</div>";
  if (profileCountry(displayProfile) == country) h += tablePreviewHtml(displayProfile, country);
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
  h += stepperControl(trKey("fontGlobal"), "sf", styleFontSize, 0, 12, 1);
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
  h += "<div class='card updBox'>" + logoHeader(trKey("update"), "Display 2") + "";
  h += "<div class='advCards'>";
  h += "<div class='advCard'><div class='advCardTitle'>PM3D AP</div><div class='advCardText'>" + htmlEscape(apSSID) + "<br>" + apUrl() + "</div></div>";
  h += "<div class='advCard'><div class='advCardTitle'>" + trKey("localWifi") + "</div><div class='advCardText'>" + String(WiFi.status() == WL_CONNECTED ? trKey("connected") + String(" - ") + WiFi.localIP().toString() : trKey("disconnected")) + "<br>" + (localWifiSsid.length() ? htmlEscape(localWifiSsid) : trKey("none")) + "</div></div>";
  h += "<div class='advCard'><div class='advCardTitle'>Firmware</div><div class='advCardText'>" + String(ESP.getSketchSize() / 1024) + " Ko<br>OTA " + String(ESP.getFreeSketchSpace() / 1024) + " Ko</div></div>";
  h += "</div>";
  h += "<div class='fwBox'><div class='section'>Mise a jour</div>";
  h += "<div class='hint'>Connecte l'ecran au Wi-Fi de la box, puis clique sur Mise a jour. L'ecran telecharge automatiquement le dernier firmware PM3D.</div>";
  h += "<div class='compactActions'><a class='btn' href='/wifiscan'>Rechercher les reseaux Wi-Fi locaux</a></div>";
  h += "<button id='onlineBtn' type='button'>" + trKey("update") + "</button><div class='bar' style='height:18px;margin-top:10px'><div id='onlineBar' class='fill' style='animation:none;width:0%'></div></div><div id='onlineProg' class='hint'>Pret</div></div>";
  h += "<details class='fwBox'><summary class='section'>Fichier .bin manuel</summary><form id='otaForm' method='POST' action='/ota' enctype='multipart/form-data'><input id='otaFile' name='firmware' type='file' accept='.bin,application/octet-stream' required><button id='otaBtn' type='submit'>" + trKey("update") + "</button><div class='bar' style='height:18px;margin-top:10px'><div id='otaBar' class='fill' style='animation:none;width:0%'></div></div><div id='otaProg' class='hint'>0%</div></form></details>";
  h += "<div class='sub tiny'>" + trKey("updateNote") + "</div>";
  h += "<script>var ob=document.getElementById('onlineBtn');if(ob){ob.onclick=function(){var p=document.getElementById('onlineProg'),b=document.getElementById('onlineBar');ob.disabled=true;var pct=4;if(p)p.innerHTML='Connexion...';if(b)b.style.width='4%';var t=setInterval(function(){pct=Math.min(92,pct+3);if(b)b.style.width=pct+'%';if(p)p.innerHTML=pct<25?'Connexion...':(pct<55?'Telechargement...':(pct<85?'Installation...':'Finalisation...'));},650);fetch('/onlineota',{method:'POST',cache:'no-store'}).then(function(r){return r.text().then(function(x){return {ok:r.ok,body:x};});}).then(function(res){clearInterval(t);if(b)b.style.width=res.ok?'100%':'0%';if(p)p.innerHTML=res.ok?'Mise a jour terminee, redemarrage...':'Erreur mise a jour';document.open();document.write(res.body);document.close();}).catch(function(){clearInterval(t);ob.disabled=false;if(b)b.style.width='0%';if(p)p.innerHTML='Erreur reseau';});};}var f=document.getElementById('otaForm');if(f){f.addEventListener('submit',function(e){e.preventDefault();var file=document.getElementById('otaFile');if(!file||!file.files.length)return;var p=document.getElementById('otaProg'),b=document.getElementById('otaBar'),btn=document.getElementById('otaBtn');var data=new FormData(f);var x=new XMLHttpRequest();btn.disabled=true;if(p)p.innerHTML='Mise a jour : 0%';x.upload.onprogress=function(ev){if(ev.lengthComputable){var pct=Math.round(ev.loaded*100/ev.total);if(b)b.style.width=pct+'%';if(p)p.innerHTML='Mise a jour : '+pct+'%';}};x.onreadystatechange=function(){if(x.readyState==4){if(p)p.innerHTML=x.status>=200&&x.status<300?'Mise a jour terminee, redemarrage...':'Erreur mise a jour';if(x.responseText)document.open(),document.write(x.responseText),document.close();}};x.open('POST','/ota',true);x.send(data);});}</script>";
  h += "</div>" + pageEnd();
  return h;
}
String packCountryItem(const String &country) {
  return "<label class='sub tiny' style='display:block;margin:6px 0'><input type='checkbox' class='pmPackCountry' value='" + country + "' checked onchange='pmPackRefresh()'> " + countryFlag(country) + " " + countryName(country) + "</label>";
}

String packManagerPage() {
  String h = pageStart(trKey("memoryManageTitle"));
  h += "<div class='card'>" + logoHeader(trKey("memoryManageTitle"), trKey("lightFirmware")) + "";
  h += "<div class='advNotice'><b>" + trKey("memoryOtaRisk") + "</b><br>";
  h += trText("Pour vraiment gagner de la place OTA, il faudra installer une variante de firmware compilee avec moins de pays. Cette page sert a choisir ce qu'on garde.", "Om echt OTA-ruimte te winnen moet een firmwarevariant met minder landen worden geinstalleerd. Deze pagina kiest wat behouden blijft.", "Um wirklich OTA-Platz zu gewinnen, muss eine Firmware-Variante mit weniger Laendern installiert werden. Diese Seite waehlt aus, was erhalten bleibt.", "To really gain OTA space, install a firmware variant compiled with fewer countries. This page chooses what to keep.");
  h += "</div>";
  h += "<div class='advCards'>";
  h += "<div class='advCard'><div class='advCardTitle'>Firmware</div><div class='advCardText'>" + String(ESP.getSketchSize() / 1024) + " Ko</div></div>";
  h += "<div class='advCard'><div class='advCardTitle'>Marge OTA</div><div class='advCardText'>" + String(ESP.getFreeSketchSpace() / 1024) + " Ko</div></div>";
  h += "<div class='advCard'><div class='advCardTitle'>Flash</div><div class='advCardText'>" + String(ESP.getFlashChipSize() / 1024) + " Ko</div></div>";
  h += "<div class='advCard'><div class='advCardTitle'>RAM libre</div><div class='advCardText'>" + String(ESP.getFreeHeap() / 1024) + " Ko</div></div>";
  h += "</div>";
  h += "<div class='section'>" + trKey("keepCountries") + "</div>";
  h += "<div class='settingsGrid'>";
  const char* countries[] = {"fr","be","de","ch","at","nl","it","uk","es","pl","hu","se","jp","us","in"};
  for (unsigned int i = 0; i < sizeof(countries) / sizeof(countries[0]); i++) h += packCountryItem(countries[i]);
  h += "</div>";
  h += "<div class='sub tiny' id='pmPackResult'></div>";
  h += "<div class='hint'>" + trText("Les pays decoches seront a retirer dans une prochaine compilation allegee. Sur ce firmware-ci, ils restent physiquement presents.", "Uitgevinkte landen worden verwijderd in een volgende lichte compilatie. In deze firmware blijven ze fysiek aanwezig.", "Abgewaehlte Laender werden in einer naechsten leichten Kompilierung entfernt. In dieser Firmware bleiben sie physisch vorhanden.", "Unchecked countries will be removed in a future light build. In this firmware they are still physically present.") + "</div>";
  h += "<div class='compactActions'><a class='btn' href='/updates'>" + trKey("update") + "</a><a class='btn' href='/countries'>" + trKey("countries") + "</a><a class='btn' href='/settings'>" + trKey("screenEdit") + "</a></div>";
  h += "</div><script>function pmPackRefresh(){var xs=document.querySelectorAll('.pmPackCountry');var keep=[];for(var i=0;i<xs.length;i++){if(xs[i].checked)keep.push(xs[i].value.toUpperCase());}var r=document.getElementById('pmPackResult');if(r)r.innerHTML='<b>" + trKey("keepCountries") + " :</b> '+keep.join(', ')+'<br><b>" + trKey("lightFirmware") + " :</b> '+keep.length+' pays';}pmPackRefresh();</script>" + pageEnd();
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
    otaReady = Update.begin(UPDATE_SIZE_UNKNOWN);
    if (!otaReady) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (otaReady && Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
      otaReady = false;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!otaReady || !Update.end(true)) {
      Update.printError(Serial);
    } else {
      Serial.printf("OTA OK: %u bytes\n", upload.totalSize);
    }
    otaReady = false;
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    otaReady = false;
    Update.abort();
    Serial.println("OTA aborted");
  }
}

void handleOnlineOta() {
  if (WiFi.status() != WL_CONNECTED) {
    String h = pageStart(trKey("updateError"));
    h += "<div class='card'>" + logoHeader(trKey("updateError"), trKey("localWifi")) + "<div class='sub tiny'>Connecte d'abord l'ecran au Wi-Fi de la box.</div><a class='btn' href='/wifiscan'>Rechercher les reseaux Wi-Fi locaux</a><a class='btn' href='/updates'>" + trKey("backUpdate") + "</a></div>" + pageEnd();
    server.send(409, "text/html", h);
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, ONLINE_FIRMWARE_URL)) {
    server.send(500, "text/html", pageStart(trKey("updateError")) + "<div class='card'>" + logoHeader(trKey("updateError"), "PM3D") + "<div class='sub tiny'>URL firmware impossible a ouvrir.</div><a class='btn' href='/updates'>" + trKey("backUpdate") + "</a></div>" + pageEnd());
    return;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    String msg = "Telechargement impossible. Code HTTP " + String(code);
    http.end();
    server.send(502, "text/html", pageStart(trKey("updateError")) + "<div class='card'>" + logoHeader(trKey("updateError"), "PM3D") + "<div class='sub tiny'>" + msg + "</div><a class='btn' href='/updates'>" + trKey("backUpdate") + "</a></div>" + pageEnd());
    return;
  }

  int len = http.getSize();
  if (len > 0 && (uint32_t)len > ESP.getFreeSketchSpace()) {
    http.end();
    server.send(507, "text/html", pageStart(trKey("updateError")) + "<div class='card'>" + logoHeader(trKey("updateError"), "OTA") + "<div class='sub tiny'>Firmware trop grand pour l'espace OTA disponible.</div><a class='btn' href='/updates'>" + trKey("backUpdate") + "</a></div>" + pageEnd());
    return;
  }

  if (!Update.begin(len > 0 ? len : UPDATE_SIZE_UNKNOWN)) {
    Update.printError(Serial);
    http.end();
    server.send(500, "text/html", pageStart(trKey("updateError")) + "<div class='card'>" + logoHeader(trKey("updateError"), "OTA") + "<div class='sub tiny'>Impossible de demarrer l'installation.</div><a class='btn' href='/updates'>" + trKey("backUpdate") + "</a></div>" + pageEnd());
    return;
  }

  WiFiClient *stream = http.getStreamPtr();
  size_t written = Update.writeStream(*stream);
  bool ok = (len <= 0 || written == (size_t)len) && Update.end(true);
  if (!ok) {
    Update.printError(Serial);
    Update.abort();
  }
  http.end();

  String h = pageStart(ok ? trKey("updateDone") : trKey("updateError"));
  h += "<div class='card'>" + logoHeader(ok ? trKey("updateDone") : trKey("updateError"), ok ? trKey("restarting") : trKey("firmwareNotInstalled")) + "";
  if (ok) h += "<div class='sub tiny'>" + trKey("updateOkMsg") + "</div><script>setTimeout(function(){location.href='/intro'},9000)</script>";
  else h += "<div class='sub tiny'>" + trKey("updateFailMsg") + "</div><a class='btn' href='/updates'>" + trKey("backUpdate") + "</a>";
  h += "</div>" + pageEnd();
  server.send(ok ? 200 : 500, "text/html", h);
  if (ok) {
    delay(600);
    ESP.restart();
  }
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
void handlePackManager() { server.send(200, "text/html", packManagerPage()); }
void handleCaptivePortal() {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Location", apUrl() + "/intro", true);
  server.send(302, "text/plain", "PM3D Display");
}

void handleStyleConfig() { server.send(200, "text/html", styleConfigPage()); }

void handleSaveStyleConfig() {
  String profile = server.hasArg("profile") ? server.arg("profile") : displayProfile;
  String back = server.hasArg("back") ? server.arg("back") : profileCountry(profile);
  if (server.hasArg("sf")) styleFontSize = constrain(server.arg("sf").toInt(), 0, 12);
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
  bool apPasswordChanged = false;
  if (server.hasArg("apPass")) {
    String nextPass = server.arg("apPass");
    nextPass.trim();
    if (nextPass.length() >= 8 && nextPass != apPass) {
      apPass = nextPass;
      apPasswordChanged = true;
    }
  }
  if (server.hasArg("ox1")) tftOffsetX1 = server.arg("ox1").toInt();
  if (server.hasArg("oy1")) tftOffsetY1 = server.arg("oy1").toInt();
  if (server.hasArg("ox2")) tftOffsetX2 = server.arg("ox2").toInt();
  if (server.hasArg("oy2")) tftOffsetY2 = server.arg("oy2").toInt();
  if (server.hasArg("pw")) tftPanelW = server.arg("pw").toInt();
  if (server.hasArg("ph")) tftPanelH = server.arg("ph").toInt();
  normalizeTftOffsets();
  saveConfig();
  if (apPasswordChanged) {
    WiFi.softAPdisconnect(false);
    delay(150);
    IPAddress apIP = stableApIPFromMac();
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(apSSID.c_str(), apPass.c_str());
  }
  server.sendHeader("Location", "/advanced");
  server.send(303);
}

void handleQrTest() {
  qrTestMode = true;
  drawConfiguredStartupQr();
  server.sendHeader("Location", "/advanced");
  server.send(303);
}

void handleQrStop() {
  qrTestMode = false;
  analogWrite(TFT_BL, screenBrightness);
  fullRedrawNeeded = true;
  server.sendHeader("Location", "/advanced");
  server.send(303);
}

void handleDemoStart() {
  demoMode = true;
  qrTestMode = false;
  demoShowingFlag = true;
  demoCountryIndex = 0;
  demoStyleIndex = 0;
  demoLastSwitch = 0;
  saveConfig();
  updateDemoMode(millis());
  server.sendHeader("Location", "/advanced");
  server.send(303);
}

void handleDemoStop() {
  demoMode = false;
  demoLastSwitch = 0;
  saveConfig();
  analogWrite(TFT_BL, screenBrightness);
  fullRedrawNeeded = true;
  server.sendHeader("Location", "/advanced");
  server.send(303);
}

void handleFavoriteProfile() {
  String profile = server.hasArg("p") ? server.arg("p") : "";
  String back = server.hasArg("back") ? server.arg("back") : profileCountry(profile);
  if (back == "fav" || back == "other") back = "fr";
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
  } else if (profile == "fr_sncf_idf_crt") {
    setRow(0,"17.32","B","Chelles","CILE","8");
    setRow(1,"17.33","E","Paris Est","PUMA","8");
    setRow(2,"17.39","B","Chelles","CILE","8");
    setRow(3,"17.46","E","Paris Est","PUMA","8");
    setRow(4,"17.54","B","Chelles","CILE","8");
    setRow(5,"18.00","E","Paris Est","PUMA","8");
    setRow(6,"18.08","B","Chelles","CILE","8");
    setRow(7,"18.15","E","Paris Est","PUMA","8");
    setRow(8,"18.22","B","Chelles","CILE","8");
    setRow(9,"18.30","E","Paris Est","PUMA","8");
    clearRowsFrom(10);
  } else if (profile == "fr_sncf_1990_flipflap") {
    setRow(0,"14:01","","Persan Beaumont","TER","");
    setRow(1,"14:43","","London St Pancras","Eurostar","");
    setRow(2,"14:46","","Lille Flandres","TER","");
    setRow(3,"14:49","","Creil Rieux Pont","TER","");
    setRow(4,"14:52","","Orry la Ville","RER","");
    setRow(5,"15:09","","Ashford London St P.","Eurostar","");
    setRow(6,"15:13","","Ashford London St P.","Eurostar","");
    setRow(7,"15:22","","Chambly Meru","TER","");
    setRow(8,"15:31","","Compiegne","TER","");
    clearRowsFrom(9);
  } else if (profile == "fr_sncf_depart_grandes_lignes") {
    setRow(0,"16h39","","Chartres","SNC","3");
    setRow(1,"16h53","","Brest","iOU","2");
    setRow(2,"16h53","","Quimper","iOU","2");
    setRow(3,"16h57","","Poitiers","iOU","6");
    setRow(4,"17h05","","Le Mans","SNC","4");
    setRow(5,"17h06","","Bordeaux Saint-Jean","iOU","7");
    setRow(6,"17h06","","Toulouse Matabiau","iOU","8");
    setRow(7,"17h09","","Chartres","SNC","3");
    setRow(8,"17h12","","Rennes","iOU","1");
    clearRowsFrom(9);
  } else if (profile == "fr_sncf_arrivees_grandes_lignes") {
    setRow(0,"13h53","retard +3 mn","Le Mans","SNC","18");
    setRow(1,"14h50","retard +5 mn","Chartres","SNC","18");
    setRow(2,"15h02","retard 25 mn","Brest","SNC","18");
    setRow(3,"15h53","a l'heure","Le Mans","SNC","18");
    setRow(4,"15h53","a l'heure","Bordeaux Saint-Jean","iOU","18");
    setRow(5,"15h53","a l'heure","Bordeaux Saint-Jean","iOU","18");
    setRow(6,"16h12","a l'heure","Nantes","iOU","18");
    setRow(7,"16h20","a l'heure","Rennes","iOU","18");
    setRow(8,"16h35","a l'heure","La Rochelle","iOU","18");
    clearRowsFrom(9);
  } else if (profile == "fr_sncf_2009_led") {
    setRow(0,"15.34","","Gare de Lyon","DUCA","2B");
    setRow(1,"","retarde","Gare de Lyon","DUCA","2B");
    setRow(2,"","retarde","Melun","ZUCO","1B");
    setRow(3,"","supprime","Gare de Lyon","DUCA","2B");
    setRow(4,"","retarde","Melun","ZUCO","1B");
    setRow(5,"15.50","","Gare de Lyon","DUCA","2B");
    setRow(6,"16.02","","Malesherbes","BIPE","1A");
    setRow(7,"16.09","retarde","Corbeil Essonnes","ROVO","2A");
    clearRowsFrom(8);
  } else if (profile == "fr_iena_juvisy") {
    setRow(0,"9 min","","Ablon","LICK","40");
    setRow(1,"12 min","","Arpajon","DYVI","44");
    setRow(2,"9 min","","Athis Mons","LICK","40");
    setRow(3,"5 min","","Ballancourt","BOUO","51");
    setRow(4,"8 min","","Bibl. F.Mit.","SARA","41");
    setRow(5,"5 min","","Bretigny","ELBO","43");
    setRow(6,"12 min","","Breuillet B.","DYVI","44");
    setRow(7,"5 min","","Buno Gironv.","BOUO","51");
    setRow(8,"3 min","","Chamarande","ELBO","43");
    setRow(9,"19:14","","Chantilly G.","SOPE","50");
    setRow(10,"12 min","","Dourdan","DYVI","44");
    setRow(11,"22 min","","Epinay/Orge","BHLI","45");
    setRow(12,"3 min","","Etampes","ELBO","43");
    setRow(13,"5 min","","Evry Val S.","BOUO","51");
    setRow(14,"7 min","","Goussainv.","LOPE","50");
    clearRowsFrom(15);
  } else if (profile == "fr_montparnasse_2010") {
    setRow(0,"15:00","8837","Nantes","TGV 1/2 CL","8");
    setRow(1,"15:05","8027","Rennes","TGV 1/2 CL","4");
    setRow(2,"15:15","3435","Granville","EXPRESS 1/2CL","27");
    setRow(3,"15:15","8441","Bordeaux","TGV","");
    setRow(4,"15:33","862529","Le Mans via Chartres","TER-CENTRE","");
    setRow(5,"15:35","8343","Tours","TGV 1/2 CL","");
    setRow(6,"15:50","8183","Hendaye","TGV 2e CL","");
    clearRowsFrom(7);
  } else if (profile == "fr_montparnasse_tft") {
    setRow(0,"11h34","01h15","Amilly Ouerray","13h04","01h14");
    setRow(1,"07h43","13mn","Bellevue","07h58","13mn");
    setRow(2,"08h40","42mn","Beynes","09h43","49mn");
    setRow(3,"07h49","01h06","Chartres","08h19","58mn");
    setRow(4,"08h40","01h05","Dreux","09h20","47mn");
    setRow(5,"07h49","39mn","Epernon","08h19","41mn");
    setRow(6,"08h10","27mn","Fontenay le Fleury","08h40","27mn");
    setRow(7,"08h40","38mn","Garancieres la Queue","09h40","38mn");
    setRow(8,"09h34","34mn","Gazeran","11h34","34mn");
    setRow(9,"08h19","01h22","La Ferte Bernard","10h34","01h20");
    setRow(10,"08h19","50mn","Le Mans","10h34","02h12");
    setRow(11,"10h34","01h40","Le Theil la Rouge","15h34","02h03");
    setRow(12,"07h49","45mn","Maintenon","08h19","47mn");
    setRow(13,"08h40","01h10","Mantes la Jolie","09h43","01h17");
    clearRowsFrom(14);
  } else if (profile == "fr_tgv_1") {
    setRow(0,"17h51","a l'heure","Bruxelles Midi","SNC","");
    setRow(1,"17h57","a l'heure","Carpentras","SNC","");
    setRow(2,"18h18","a l'heure","Paris Lyon","iOU","");
    setRow(3,"18h20","a l'heure","Marseille St Charles","iOU","");
    setRow(4,"18h28","a l'heure","Lyon Perrache","SNC","");
    setRow(5,"18h52","a l'heure","Marseille St Charles","SNC","");
    setRow(6,"18h56","a l'heure","Tourcoing","iOU","");
    setRow(7,"18h57","a l'heure","Carpentras","SNC","");
    setRow(8,"19h10","retard 05 min","Miramas","SNC","");
    setRow(9,"19h11","retard 05 min","Nice Ville","SNC","");
    clearRowsFrom(10);
  } else if (profile == "fr_tgv_2") {
    setRow(0,"17h18","a l'heure","Marseille St Charles","SNC","3");
    setRow(1,"17h01","retard 30 min","Marseille St Charles","SNC","3");
    setRow(2,"17h21","a l'heure","Aeroport CDG 2","SNC","4");
    setRow(3,"17h40","a l'heure","Marseille St Charles","SNC","");
    setRow(4,"17h42","a l'heure","Paris Lyon","iOU","");
    setRow(5,"17h52","a l'heure","Lyon Perrache","SNC","3");
    clearRowsFrom(6);
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
  } else if (profile == "fr_sncf_valence_side") {
    setRow(0,"12h43","Aix-les-Bains le Revard","Aix-les-Bains le Revard","TER","A");
    setRow(1,"12h43","Chambery-Challes-Eaux","Chambery-Challes-Eaux","TER","A");
    setRow(2,"12h43","Montmelian","Montmelian","TER","A");
    setRow(3,"12h43","Pontcharra sur Breda","Pontcharra sur Breda","TER","A");
    setRow(4,"12h43","Grenoble Univ.-Gieres","Grenoble Univ.-Gieres","TER","A");
    setRow(5,"12h43","Grenoble","Grenoble","TER","A");
    setRow(6,"12h43","Moirans","Moirans","TER","A");
    setRow(7,"12h43","Tullins-Fures","Tullins-Fures","TER","A");
    setRow(8,"12h43","Saint-Marcellin","Saint-Marcellin","TER","A");
    setRow(9,"12h43","Valence TGV Rhone-Alpes","Valence TGV Rhone-Alpes","TER","A");
    setRow(10,"12h43","Valence Ville","Valence Ville","TER","A");
    clearRowsFrom(11);
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
    setRow(0,"08:12","+7","Ostende via Gand-Saint-Pierre","IC","9");
    setRow(1,"08:17","","Anvers-Central via Malines","IC","5");
    setRow(2,"08:21","","Liege-Guillemins via Louvain","IC","3");
    setRow(3,"08:26","Supprime","Louvain-la-Neuve","S8","--");
    setRow(4,"08:31","+13","Namur via Ottignies","IC","2");
    setRow(5,"08:36","","Charleroi-Central via Fleurus","IC","11");
    setRow(6,"08:41","","Tournai via Ath","IC","7");
    setRow(7,"08:47","+5","Courtrai via Bruxelles-Midi","IC","4");
    setRow(8,"08:52","","Brussels Airport-Zaventem","IC","6");
    setRow(9,"08:58","","Dinant via Gembloux, Namur","IC","5");
    clearRowsFrom(10);
  } else if (profile == "be_sncb_detail") {
    setRow(0,"12:02","","Ostende","IC","6");
    setRow(1,"12:07","","Namur","IC","5");
    setRow(2,"12:12","","Mons","IC","3");
    setRow(3,"12:18","","Louvain","S2","4");
    setRow(4,"12:24","","Bruxelles","S10","2");
    setRow(5,"12:31","","Liege-G.","IC","7");
    setRow(6,"12:36","","Gand-Saint-Pierre","IC","4");
    setRow(7,"12:42","","Anvers-Central","IC","2");
    clearRowsFrom(8);
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
  } else if (profile == "fr_rer_d_8090") {
    setRow(0,"","", "VILLENEUVE TRIAGE", "RER D", "A");
    setRow(1,"","", "GARE DE LYON", "RER D", "A");
    setRow(2,"","", "CHATELET-LES HALLES", "RER D", "A");
    setRow(3,"","", "STADE DE FRANCE", "RER D", "A");
    setRow(4,"","", "JUVISY", "RER D", "A");
    setRow(5,"","", "MELUN", "RER D", "A");
    clearRowsFrom(6);
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
    setRow(6,"18:39","Bochum Hbf, Dortmund Hbf, Hamm(Westf), Bielefeld Hbf","Hannover Hbf","IC 2045","7");
    clearRowsFrom(7);
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
    setRow(11,"09:15","Olten - Luzern","Luzern","IR 70","6");
    setRow(12,"09:18","Winterthur - Wil SG","St. Gallen","IC 1","34");
    clearRowsFrom(13);
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
  } else if (profile == "nl_ns_2010_photo") {
    setRow(0,"13:19","via Sloterdijk, Haarlem, Leiden C.","Den Haag Centraal","Intercity","2");
    setRow(1,"13:19","Does not stop at Zaandam","Enkhuizen","Intercity","8");
    setRow(2,"13:19","via Duivendrecht, Bijlmer ArenA, Woerden","Rotterdam Centraal","Sprinter","5b");
    setRow(3,"13:22","via Schiphol Airport","Rotterdam Centraal","Intercity direct","10");
    setRow(4,"13:23","via Sloterdijk, Zaandam, Zaanse Schans","Uitgeest","Sprinter","7a");
    setRow(5,"13:28","via Gouda","Utrecht Centraal","Intercity","4");
    clearRowsFrom(6);
  } else if (profile == "nl_ns_blue") {
    setRow(0,"10:27","","Alkmaar","IC","10");
    setRow(1,"10:31","","Amersfoort","IC","8a");
    setRow(2,"10:34","","Uitgeest","SPR","13a");
    setRow(3,"10:35","","Frankfurt","ICE","2b");
    setRow(4,"10:37","","Lelystad","IC","10a");
    setRow(5,"10:38","","Maastricht","IC","5b");
    setRow(6,"10:41","","Den Haag Centraal","IC","7");
    setRow(7,"10:44","","Rotterdam Centraal","SPR","4");
    setRow(8,"10:48","","Nijmegen","IC","11");
    setRow(9,"10:52","","Utrecht Centraal","SPR","3");
    clearRowsFrom(10);
    transilienInfoText = "Let op: gewijzigde dienstregeling";
  } else if (profile.startsWith("nl_")) {
    setRow(0,"10:27","","Alkmaar","IC","10");
    setRow(1,"10:31","","Amersfoort","IC","8a");
    setRow(2,"10:34 /5","","Uitgeest","SPR","13a");
    setRow(3,"10:35","","Frankfurt","ICE","2b");
    setRow(4,"10:37","","Lelystad","IC","10a");
    setRow(5,"10:38","","Maastricht","IC","5b");
    clearRowsFrom(6);
  } else if (profile == "es_adif_departures") {
    setRow(0,"10:15","02100","Sevilla-Santa Justa","AVE","7","SAL");
    setRow(1,"10:20","08100","Puertollano","Avant","14","PB");
    setRow(2,"10:25","08302","Toledo","Avant","14","PB");
    setRow(3,"10:30","05032","Albacete Los Llanos","AVE","7","PB");
    setRow(4,"10:40","03103","Barcelona Sants","AVE","5","P1");
    setRow(5,"11:00","05100","Valencia J.Sorolla","AVE","9","P1");
    setRow(6,"11:30","02110","Sevilla-Santa Justa","AVE","6","P1");
    setRow(7,"11:35","03113","Barcelona Sants","AVE","4","P1");
    setRow(8,"11:40","00605","Pamplona","Alvia","8","P1");
    setRow(9,"11:45","04121","Madrid Chamartin","AVE","2","P1");
    setRow(10,"11:50","06210","Malaga Maria Zambrano","AVE","10","PB");
    clearRowsFrom(11);
  } else if (profile == "es_rodalies_departures") {
    setRow(0,"16:08","C1","Utrera","Cercanias","2");
    setRow(1,"16:15","C2","Benacazon","Cercanias","17");
    setRow(2,"16:18","C4","Madrona P.M.","Cercanias","3");
    setRow(3,"16:22","C1","Lebrija","Cercanias","2");
    setRow(4,"16:27","C5","Jardines de Hercules","Cercanias","4");
    setRow(5,"16:33","C3","Cazalla-Constantina","Cercanias","8");
    setRow(6,"16:41","C1","Dos Hermanas","Cercanias","2");
    setRow(7,"16:48","C5","Benacazon","Cercanias","17");
    clearRowsFrom(8);
  } else if (profile == "es_barcelona_grid") {
    setRow(0,"13:00","3132", "Madrid Pta. Atocha AVE", "AVE", "1");
    setRow(1,"14:00","3142", "Madrid Pta. Atocha AVE", "AVE", "1");
    setRow(2,"14:10","632", "Valladolid-Salamanca", "Alvia", "6");
    setRow(3,"14:30","34632", "Lleida-Pirineus", "Avant", "11");
    setRow(4,"14:30","1341", "Valencia J.Sorolla", "Euromed", "6");
    setRow(5,"14:50","165", "Figueres-Vilafant", "Talgo", "11");
    setRow(6,"15:00","3134", "Lorca", "Alvia", "4");
    setRow(7,"15:10","3120", "Alicante-Terminal", "AVE", "7");
    setRow(8,"15:20","3490", "Zaragoza-Delicias", "AVLO", "2");
    setRow(9,"15:35","3128", "Granada", "AVE", "8");
    setRow(10,"15:45","3402", "Castellon", "Euromed", "5");
    setRow(11,"16:00","3510", "Malaga M.Zambrano", "AVE", "9");
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
    setRow(0,"17:57","Os 77630", "Golancz", "KW", "3", "PKM3");
    setRow(1,"18:02","OsP 77138", "Kalisz", "KW", "2", "CALLSIA");
    setRow(2,"18:10","R 77998", "Ostrow Wielkopolski", "PR", "1", "PKM4");
    setRow(3,"18:11","Os 77426", "Gniezno", "KW", "3", "PKM1");
    setRow(4,"18:13","Os 77386", "Grodzisk Wielkopolski", "KW", "10", "PKM3");
    setRow(5,"18:19","Os 77548", "Konin", "KW", "5", "PKM2");
    setRow(6,"18:33","IC 84100", "Katowice", "IC", "11", "GWAREK");
    setRow(7,"18:38","IC 75108", "Gdynia Glowna", "IC", "2", "STOCZNIOWIEC");
    setRow(8,"18:42","Os 78203", "Krzyz", "KW", "10", "PKM4");
    setRow(9,"18:44","IC 65104", "Olsztyn Glowny", "IC", "3", "JEZIORAK");
    setRow(10,"18:45","R 76940", "Wroclaw Glowny", "PR", "7", "");
    setRow(11,"18:51","TLK 81108", "Lodz Kaliska", "IC", "10", "WLOKNIARZ");
    clearRowsFrom(12);
  } else if (profile == "us_la_metro") {
    setRow(0,"6:05","135", "Washington", "Northeast Regional", "Standby");
    setRow(1,"7:00","168", "Boston", "Northeast Regional", "On Time");
    setRow(2,"7:01","57", "Washington", "Vermonter", "On Time");
    setRow(3,"7:15","241", "Albany-Rensselaer", "Empire Service", "On Time");
    setRow(4,"8:00","146", "Springfield", "Northeast Regional", "On Time");
    setRow(5,"9:05","167", "Washington", "Northeast Regional", "On Time");
    setRow(6,"9:15","259", "Albany-Rensselaer", "Empire Service", "On Time");
    clearRowsFrom(7);
  } else if (profile == "us_amtrak_black") {
    setRow(0,"10:00A","2153", "WASHING.", "ACELA EXP", "ALLBOARD", "12E");
    setRow(1,"10:03A","2154", "BOSTON", "ACELA EXP", "BOARDING", "11W");
    setRow(2,"10:05A","3833", "TRENTON", "NE CORR", "BOARDING", "4");
    setRow(3,"10:12A","6621", "DOVER", "MIDTOWN", "ON TIME", "");
    setRow(4,"10:20A","172", "BOSTON", "ACELA REG", "ON TIME", "");
    setRow(5,"10:35A","95", "N. NEWS", "ACELA REG", "ON TIME", "");
    setRow(6,"10:38A","3835", "TRENTON", "NE CORR", "ON TIME", "");
    setRow(7,"10:44A","3235", "BAYHEAD", "NJ COAST", "ON TIME", "");
    clearRowsFrom(8);
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
    setRow(0,"17:31","106", "Sodertalje S Strangnas Eskilstuna", "Malartag", "4");
    setRow(1,"17:34","10", "Hamburg Berlin Hbf", "SJ EuroNight", "");
    setRow(2,"17:37","13", "Marsta Knivsta Uppsala", "UL Regional", "");
    setRow(3,"17:40","13", "Hallsberg Norrkoping Linkoping", "SJ InterCity", "");
    setRow(4,"17:42","13", "Sodertalje S Nykoping Norrkoping", "Malartag", "");
    setRow(5,"17:44","13", "Balsta Enkoping Vasteras", "SJ Regional", "");
    setRow(6,"17:49","13", "Katrineholm Skovde Goteborg", "VR Snabbtag", "");
    setRow(7,"17:52","14", "Sodertalje S Katrineholm Hallsberg", "Malartag", "");
    setRow(8,"17:55","13", "Eskilstuna Arboga Orebro S", "Malartag", "");
    setRow(9,"18:00","10", "Lulea Umea C Riksgransen", "Vy Nattag", "");
    setRow(10,"18:09","13", "Katrineholm Karlstad Oslo", "SJ Snabbtag", "");
    setRow(11,"18:11","10", "Arlanda C Knivsta Uppsala", "Malartag", "");
    clearRowsFrom(12);
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
  } else if (profile == "jp_tokyo_narita") {
    setRow(0,"8:04","9:26","Narita Airport","Express","");
    setRow(1,"8:13","8:57","Narita Airport","Skyliner","11");
    setRow(2,"8:25","9:37","Narita Airport","Express","");
    setRow(3,"8:39","9:21","Narita Airport","Skyliner","13");
    setRow(4,"8:44","9:48","Narita Airport","Express","");
    clearRowsFrom(5);
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

void applyBelgium2023PresetOnce() {
  if (displayProfile != "be_sncb_modern") return;
  prefs.begin("pm3ddisp", true);
  bool done = prefs.getBool("be23v3", false);
  prefs.end();
  if (done) return;

  loadProfileDefaults("be_sncb_modern");
  userRowsEdited = true;
  nbVisible = 6;
  saveConfig();
  prefs.begin("pm3ddisp", false);
  prefs.putBool("be23v3", true);
  prefs.end();
}

void applyDb2010PresetOnce() {
  if (displayProfile != "de_db_2010_2015") return;
  prefs.begin("pm3ddisp", true);
  bool done = prefs.getBool("db10v5", false);
  prefs.end();
  if (done) return;

  loadProfileDefaults("de_db_2010_2015");
  userRowsEdited = true;
  nbVisible = 5;
  saveConfig();
  prefs.begin("pm3ddisp", false);
  prefs.putBool("db10v5", true);
  prefs.end();
}

String demoCountryAt(int index) {
  const char* countries[] = {"de","at","be","ch","es","fr","hu","in","it","jp","nl","pl","se","us","uk"};
  int count = sizeof(countries) / sizeof(countries[0]);
  return String(countries[index % count]);
}

int demoCountryCount() {
  return 15;
}

void drawDemoFlagGraphic(const String &country, int x, int y, int w, int h) {
  uint16_t red = 0xF800, blue = 0x001F, darkBlue = 0x0010, white = C_WHITE;
  uint16_t green = 0x07E0, yellow = 0xFFE0, orange = 0xFD20, black = C_BLACK;
  gfx->fillRect(x, y, w, h, white);
  if (country == "fr") {
    gfx->fillRect(x, y, w / 3, h, blue);
    gfx->fillRect(x + w / 3, y, w / 3, h, white);
    gfx->fillRect(x + (w * 2) / 3, y, w - (w * 2) / 3, h, red);
  } else if (country == "be") {
    gfx->fillRect(x, y, w / 3, h, black);
    gfx->fillRect(x + w / 3, y, w / 3, h, yellow);
    gfx->fillRect(x + (w * 2) / 3, y, w - (w * 2) / 3, h, red);
  } else if (country == "de") {
    gfx->fillRect(x, y, w, h / 3, black);
    gfx->fillRect(x, y + h / 3, w, h / 3, red);
    gfx->fillRect(x, y + (h * 2) / 3, w, h - (h * 2) / 3, yellow);
  } else if (country == "es") {
    gfx->fillRect(x, y, w, h / 4, red);
    gfx->fillRect(x, y + h / 4, w, h / 2, yellow);
    gfx->fillRect(x, y + (h * 3) / 4, w, h / 4, red);
  } else if (country == "it") {
    gfx->fillRect(x, y, w / 3, h, green);
    gfx->fillRect(x + w / 3, y, w / 3, h, white);
    gfx->fillRect(x + (w * 2) / 3, y, w - (w * 2) / 3, h, red);
  } else if (country == "nl") {
    gfx->fillRect(x, y, w, h / 3, red);
    gfx->fillRect(x, y + h / 3, w, h / 3, white);
    gfx->fillRect(x, y + (h * 2) / 3, w, h - (h * 2) / 3, blue);
  } else if (country == "ch") {
    gfx->fillRect(x, y, w, h, C_BLACK);
    int s = min(w, h);
    int fx = x + (w - s) / 2;
    int fy = y + (h - s) / 2;
    gfx->fillRect(fx, fy, s, s, red);
    int bar = max(6, s / 5);
    int longBar = (s * 3) / 5;
    gfx->fillRect(fx + s / 2 - bar / 2, fy + (s - longBar) / 2, bar, longBar, white);
    gfx->fillRect(fx + (s - longBar) / 2, fy + s / 2 - bar / 2, longBar, bar, white);
    gfx->drawRect(fx, fy, s, s, C_WHITE);
  } else if (country == "at") {
    gfx->fillRect(x, y, w, h / 3, red);
    gfx->fillRect(x, y + h / 3, w, h / 3, white);
    gfx->fillRect(x, y + (h * 2) / 3, w, h - (h * 2) / 3, red);
  } else if (country == "jp") {
    gfx->fillRect(x, y, w, h, white);
    gfx->fillCircle(x + w / 2, y + h / 2, min(w, h) / 4, red);
  } else if (country == "se") {
    gfx->fillRect(x, y, w, h, blue);
    gfx->fillRect(x + w / 3, y, 8, h, yellow);
    gfx->fillRect(x, y + h / 2 - 4, w, 8, yellow);
  } else if (country == "pl") {
    gfx->fillRect(x, y, w, h / 2, white);
    gfx->fillRect(x, y + h / 2, w, h / 2, red);
  } else if (country == "hu") {
    gfx->fillRect(x, y, w, h / 3, red);
    gfx->fillRect(x, y + h / 3, w, h / 3, white);
    gfx->fillRect(x, y + (h * 2) / 3, w, h - (h * 2) / 3, green);
  } else if (country == "in") {
    gfx->fillRect(x, y, w, h / 3, orange);
    gfx->fillRect(x, y + h / 3, w, h / 3, white);
    gfx->fillRect(x, y + (h * 2) / 3, w, h - (h * 2) / 3, green);
    gfx->fillCircle(x + w / 2, y + h / 2, min(w, h) / 9, blue);
  } else if (country == "us") {
    int stripeH = max(1, h / 13);
    for (int i = 0; i < 13; i++) {
      int sy = y + i * stripeH;
      int sh = (i == 12) ? (y + h - sy) : stripeH;
      gfx->fillRect(x, sy, w, max(1, sh), (i % 2 == 0) ? red : white);
    }
    int cantonW = (w * 2) / 5;
    int cantonH = stripeH * 7;
    gfx->fillRect(x, y, cantonW, cantonH, darkBlue);
    int starCols = 6;
    int starRows = 5;
    int stepX = max(1, (cantonW - 8) / max(1, starCols - 1));
    int stepY = max(1, (cantonH - 6) / max(1, starRows - 1));
    bool bigStars = (cantonW >= 46 && cantonH >= 20);
    for (int r = 0; r < starRows; r++) {
      for (int c = 0; c < starCols; c++) {
        int sx = x + 4 + c * stepX;
        int sy = y + 3 + r * stepY;
        gfx->drawPixel(sx, sy, white);
        if (bigStars) {
          gfx->drawPixel(sx - 1, sy, white);
          gfx->drawPixel(sx + 1, sy, white);
          gfx->drawPixel(sx, sy - 1, white);
          gfx->drawPixel(sx, sy + 1, white);
        }
      }
    }
  } else if (country == "uk") {
    gfx->fillRect(x, y, w, h, darkBlue);
    for (int d = -1; d <= 1; d++) {
      gfx->drawLine(x, y + d, x + w / 2, y + h / 2 + d, white);
      gfx->drawLine(x + w - 1, y + d, x + w / 2, y + h / 2 + d, white);
      gfx->drawLine(x, y + h - 1 + d, x + w / 2, y + h / 2 + d, white);
      gfx->drawLine(x + w - 1, y + h - 1 + d, x + w / 2, y + h / 2 + d, white);
    }
    gfx->drawLine(x, y, x + w / 2, y + h / 2, red);
    gfx->drawLine(x + w - 1, y, x + w / 2, y + h / 2, red);
    gfx->drawLine(x, y + h - 1, x + w / 2, y + h / 2, red);
    gfx->drawLine(x + w - 1, y + h - 1, x + w / 2, y + h / 2, red);
    gfx->fillRect(x + w / 2 - 5, y, 10, h, white);
    gfx->fillRect(x, y + h / 2 - 5, w, 10, white);
    gfx->fillRect(x + w / 2 - 3, y, 6, h, red);
    gfx->fillRect(x, y + h / 2 - 3, w, 6, red);
  } else {
    gfx->fillRect(x, y, w, h, darkBlue);
  }
  gfx->drawRect(x, y, w, h, C_WHITE);
}

void drawDemoCountryFlag(const String &country) {
  gfx->fillScreen(C_BLACK);
  int flagW = 96;
  int flagH = 58;
  drawDemoFlagGraphic(country, (gfx->width() - flagW) / 2, 8, flagW, flagH);

  String name = countryName(country);
  gfx->setTextColor(C_WHITE, C_BLACK);
  gfx->setTextSize(2);
  int nameW = name.length() * 12;
  gfx->setCursor(max(0, (gfx->width() - nameW) / 2), 75);
  gfx->print(name);
  gfx->setTextSize(1);
  gfx->setCursor(max(0, (gfx->width() - 54) / 2), 112);
  gfx->print("MODE DEMO");
}

void demoApplyCurrentStep(bool flagScreen) {
  String country = demoCountryAt(demoCountryIndex);
  String order = getCountryOrder(country);
  int count = profileCount(order);
  if (count <= 0) {
    demoCountryIndex = (demoCountryIndex + 1) % demoCountryCount();
    demoStyleIndex = 0;
    drawDemoCountryFlag(demoCountryAt(demoCountryIndex));
    return;
  }
  if (flagScreen) {
    drawDemoCountryFlag(country);
    return;
  }
  String profile = profileAt(order, demoStyleIndex % count);
  if (!profile.length()) return;
  displayProfile = profile;
  applyProfileToScreenMode();
  loadProfileDefaults(displayProfile);
  nbVisible = profileDefaultVisibleRows(displayProfile);
  normalizeSettings();
  loadStyleTune(displayProfile);
  scrollOffset = 0;
  retroAnimating = false;
  drawScreenFull();
}

void updateDemoMode(unsigned long now) {
  if (!demoMode) return;
  unsigned long duration = demoShowingFlag ? 3000UL : 5000UL;
  if (demoLastSwitch != 0 && now - demoLastSwitch < duration) return;
  if (demoLastSwitch == 0) {
    demoShowingFlag = true;
  } else if (demoShowingFlag) {
    demoShowingFlag = false;
  } else {
    String country = demoCountryAt(demoCountryIndex);
    int count = max(1, profileCount(getCountryOrder(country)));
    demoStyleIndex++;
    if (demoStyleIndex >= count) {
      demoStyleIndex = 0;
      demoCountryIndex = (demoCountryIndex + 1) % demoCountryCount();
      demoShowingFlag = true;
    }
  }
  demoLastSwitch = now;
  demoApplyCurrentStep(demoShowingFlag);
}

void handleSetProfile() {
  if (server.hasArg("profile")) displayProfile = server.arg("profile");
  demoMode = false;
  demoLastSwitch = 0;
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

void handlePreviewData() {
  int shown = min(nbVisible, 8);
  String json = "{";
  json += "\"profile\":\"" + jsonEscape(displayProfile) + "\",";
  json += "\"title\":\"" + jsonEscape(previewTitleForProfile(displayProfile)) + "\",";
  json += "\"offset\":" + String(scrollOffset) + ",";
  json += "\"visible\":" + String(nbVisible) + ",";
  json += "\"delay\":" + String(scrollDelayMs) + ",";
  int clockIdx = visibleRowIndex(scrollOffset);
  json += "\"clock\":\"" + jsonEscape(rows[clockIdx].heure.length() ? rows[clockIdx].heure : String("12:00")) + "\",";
  json += "\"rows\":[";
  for (int i = 0; i < shown; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    if (i) json += ",";
    json += "{";
    json += "\"h\":\"" + jsonEscape(rows[idx].heure) + "\",";
    json += "\"d\":\"" + jsonEscape(rows[idx].destination) + "\",";
    json += "\"t\":\"" + jsonEscape(rows[idx].typeTrain) + "\",";
    json += "\"v\":\"" + jsonEscape(rows[idx].voie) + "\",";
    json += "\"i\":\"" + jsonEscape(rows[idx].info) + "\"";
    json += "}";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleLiveDisplay() {
  if (server.hasArg("nb")) nbVisible = server.arg("nb").toInt();
  nbVisible = constrain(nbVisible, 1, profileMaxVisibleRows(displayProfile));
  if (server.hasArg("font")) displayFontSize = parseFontLevel(server.arg("font"));
  else displayFontSize = autoFontForRows(displayProfile, nbVisible);
  displayFontSize = constrain(displayFontSize, 1, 12);
  if (server.hasArg("bright")) screenBrightness = map(constrain(server.arg("bright").toInt(), 1, 10), 1, 10, 25, 255);
  if (server.hasArg("scrollMs")) scrollDelayMs = server.arg("scrollMs").toInt();
  normalizeSettings();
  if (screenMode == 2) resetRetroDisplay();
  analogWrite(TFT_BL, screenBrightness);
  drawScreenFull();
  server.send(200, "text/plain", "Live TFT : " + String(nbVisible) + " lignes / police " + fontUiValue(displayFontSize) + " / defilement " + String(scrollDelayMs) + " ms");
}

void handleSave() {
  if (server.hasArg("nb")) nbVisible = server.arg("nb").toInt();
  nbVisible = constrain(nbVisible, 1, profileMaxVisibleRows(displayProfile));
  if (server.hasArg("scrollMs")) scrollDelayMs = server.arg("scrollMs").toInt();
  if (server.hasArg("bright")) screenBrightness = map(constrain(server.arg("bright").toInt(), 1, 10), 1, 10, 25, 255);
  if (server.hasArg("font")) displayFontSize = parseFontLevel(server.arg("font"));
  if (server.hasArg("theme")) uiTheme = server.arg("theme");
  if (server.hasArg("mode")) screenMode = server.arg("mode").toInt();
  if (server.hasArg("trFrame")) transilienFrame = server.arg("trFrame");
  if (server.hasArg("trAff")) transilienAffluence = server.arg("trAff");
  if (server.hasArg("trDir")) transilienDirection = server.arg("trDir");
  if (server.hasArg("trStops")) transilienStops = server.arg("trStops");
  if (server.hasArg("trInfoTitle")) transilienInfoTitle = server.arg("trInfoTitle");
  if (server.hasArg("trInfoText")) transilienInfoText = server.arg("trInfoText");
  if (server.hasArg("swissMsg")) swissInfoMessage = server.arg("swissMsg");
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

void drawSncb2023PhotoRef() {
  int w = gfx->width();
  int h = gfx->height();
  const uint16_t headerBlue = 0x04BF;
  const uint16_t rowBlue = 0x0216;
  const uint16_t white = C_WHITE;
  const uint16_t yellow = C_SNCB_YELLOW;
  const uint16_t red = 0xF986;
  const uint16_t pale = 0xEFFF;
  const uint16_t blueText = 0x0216;

  int headerH = 17;
  int rowsToDraw = constrain(nbVisible, 1, min(10, MAX_ROWS));
  int rowH = (h - headerH) / rowsToDraw;
  if (rowH < 10) rowH = 10;

  if (!rowsOnlyPass) {
    gfx->fillScreen(rowBlue);
    gfx->fillRect(0, 0, w, headerH, headerBlue);
    fixedSmallText(3, 5, white, rows[0].heure.length() ? rows[0].heure : "17:10");
    fixedSmallText((w / 2) - 17, 5, white, "Depart");
    gfx->drawCircle(w - 10, 8, 6, white);
    gfx->drawCircle(w - 10, 8, 5, white);
    fixedSmallText(w - 13, 5, white, "B");
  }

  for (int i = 0; i < rowsToDraw; i++) {
    int idx = visibleRowIndex(scrollOffset + i);
    int y = headerH + i * rowH;
    int textY = y + max(1, (rowH - 8) / 2);
    gfx->fillRect(0, y, w, rowH, rowBlue);
    gfx->drawFastHLine(0, y + rowH - 1, w, white);

    String info = rows[idx].info;
    String dest = rows[idx].destination;
    String train = rows[idx].typeTrain;
    String voie = rows[idx].voie;
    int viaPos = dest.indexOf(" via ");
    String mainDest = viaPos >= 0 ? dest.substring(0, viaPos) : dest;
    bool cancelled = info == "Supprime" || info == "SUPPRIME" || info == "Supprimee" || info.indexOf("Supprime") >= 0;
    bool atPlatform = info == "A quai" || info == "a quai";

    fixedSmallText(3, textY, white, cutText(rows[idx].heure, 5));

    int destX = 62;
    if (cancelled || info == "Limite") {
      int boxW = cancelled ? 48 : 38;
      gfx->fillRect(34, y + 2, boxW, rowH - 4, red);
      fixedSmallText(38, textY, white, cancelled ? "Suppr." : "Limite");
      destX = 88;
    } else if (atPlatform) {
      fixedSmallText(37, textY, white, "A quai");
      destX = 78;
    } else if (info.startsWith("+")) {
      int delayMin = 0;
      for (int c = 1; c < info.length(); c++) {
        if (info[c] >= '0' && info[c] <= '9') delayMin = delayMin * 10 + (info[c] - '0');
        else if (delayMin > 0) break;
      }
      String delayBadge = delayMin > 0 ? ("+" + String(delayMin) + "'") : cutText(info, 3);
      gfx->fillRect(34, y + 2, 22, rowH - 4, red);
      gfx->fillTriangle(56, y + 2, 63, y + rowH / 2, 56, y + rowH - 2, red);
      fixedSmallText(37, textY, white, cutText(delayBadge, 3));
      String newTime = rows[idx].retard;
      if (!newTime.length() && rows[idx].heure.length() >= 5) {
        int hh = rows[idx].heure.substring(0, 2).toInt();
        int mm = rows[idx].heure.substring(3, 5).toInt();
        int total = (hh * 60 + mm + delayMin) % (24 * 60);
        char buf[6];
        snprintf(buf, sizeof(buf), "%02d:%02d", total / 60, total % 60);
        newTime = String(buf);
      }
      if (newTime.length()) {
        gfx->fillRect(64, y + 2, 31, rowH - 4, pale);
        fixedSmallText(67, textY, blueText, cutText(newTime, 5));
        destX = 100;
      } else {
        destX = 70;
      }
    } else if (info.length()) {
      fixedSmallText(36, textY, white, cutText(info, 8));
      destX = 88;
    }

    int trainX = w - 31;
    int voieX = w - 10;
    int availableChars = max(5, (trainX - destX - 2) / 6);
    fixedSmallText(destX, textY, yellow, cutText(mainDest, availableChars));

    fixedSmallText(trainX, textY, white, cutText(train, 3));
    fixedSmallText(voieX, textY, white, cutText(voie, 2));
  }
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

  if (!rowsOnlyPass) {
    gfx->fillScreen(rowBlue);
    gfx->fillRect(0, 0, w, 17, headerBlue);
    gfx->fillRect(0, 17, w, h - 17, rowBlue);
    smallText(7, 6, C_WHITE, rows[0].heure.length() ? rows[0].heure : "08:57");
    smallText((w / 2) - 18, 6, C_WHITE, "Depart");
    gfx->drawCircle(w - 14, 8, 6, C_WHITE);
    smallText(w - 17, 5, C_WHITE, "B");
  }

  int top = 18;
  int maxRows = constrain(nbVisible, 1, min(11, MAX_ROWS));
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
    bool cancelled = info == "Supprime" || info == "SUPPRIME" || info == "SupprimÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â©";

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

    destX += 4;
    int yellowLevel = max(1, tunedFontSize() - 1);
    colSmallTextLevel(2, destX, y + 1, C_SNCB_YELLOW, cutText(dest, destX > 92 ? 18 : 23), yellowLevel);
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

  if (!rowsOnlyPass) gfx->fillScreen(bg);
  gfx->setTextSize(1);
  gfx->setTextColor(white, bg);
  smallText(5, 6, white, "18 05");
  smallText(67, 6, white, "Depart");

  int count = nbVisible;
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
    {"11:40", "", "Bruxelles Alost", "S", "4"},
    {"11:47", "", "Bruxelles Termonde", "S", "2"},
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
      gfx->fillRect(x + 3, y + max(3, rowH - 9), colW - 8, 7, pale);
      smallText(x + 5, y + max(4, rowH - 8), blue, cutText(info, 12));
    } else {
      smallText(x + 3, y + max(3, rowH - 9), white, cutText(String(r.dest), 13));
    }
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

  // SNCB / RailTime ancien jaune : pas de lignes tracÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â©es, uniquement alternance de bandes.
  if (!rowsOnlyPass) gfx->fillScreen(0x0000);

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
  int rows = max(1, min(nbVisible, 8));
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
    int idx = visibleRowIndex(scrollOffset + i);
    int y = headerH + i * rowH;
    uint16_t bg = (i % 2 == 0) ? bandA : bandB;

    // IMPORTANT : pas de drawFastHLine ici. L'effet vient seulement des bandes alternÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â©es.
    gfx->fillRect(0, y, w, rowH, bg);

    gfx->setTextColor(yellow, bg);
    int ty = y + max(1, (rowH - 8 * max(1, pm3dFont)) / 2);
    gfx->setCursor(4, ty);
    gfx->print(cutText(::rows[idx].heure, 5));

    gfx->setCursor(40, ty);
    gfx->print(cutText(::rows[idx].destination, 20));

    gfx->setCursor(w - 60, ty);
    gfx->print(cutText(::rows[idx].typeTrain, 4));

    gfx->setCursor(w - 28, ty);
    gfx->print(cutText(::rows[idx].voie, 3));
  }

  gfx->fillRect(0, h - footerH, w, footerH, black);
  gfx->setTextSize(1);
  gfx->setTextColor(0x07E0, black);
  gfx->setCursor((w / 2) - 28, h - footerH + 2);
  gfx->print("SNCB NMBS");
}


uint16_t qrColorFromId(int id) {
  switch (id) {
    case 1: return C_WHITE;
    case 2: return C_CYAN;
    case 3: return C_YELLOW;
    case 4: return C_GREEN;
    case 5: return C_BLUE_TOP;
    case 6: return C_AMBER;
    case 7: return C_GREY;
    default: return C_BLACK;
  }
}

uint16_t startupQrFgColor() {
  if (startupQrStyle == 1) return C_WHITE;
  if (startupQrStyle == 2) return C_BLACK;
  if (startupQrStyle == 3) return C_AMBER;
  if (startupQrStyle == 4) return qrColorFromId(startupQrFgId);
  if (startupQrStyle == 5) return C_GREEN;
  if (startupQrStyle == 6) return C_CYAN;
  if (startupQrStyle == 7) return C_BLACK;
  if (startupQrStyle == 8) return C_BLUE_TOP;
  if (startupQrStyle == 9) return C_RED;
  if (startupQrStyle == 10) return C_YELLOW;
  if (startupQrStyle == 11) return C_AMBER;
  if (startupQrStyle == 12) return C_WHITE;
  return C_BLACK;
}

uint16_t startupQrBgColor() {
  if (startupQrStyle == 1) return C_BLACK;
  if (startupQrStyle == 2) return C_CYAN;
  if (startupQrStyle == 3) return C_BLACK;
  if (startupQrStyle == 4) return qrColorFromId(startupQrBgId);
  if (startupQrStyle == 5) return C_BLACK;
  if (startupQrStyle == 6) return C_BLACK;
  if (startupQrStyle == 7) return C_AMBER;
  if (startupQrStyle == 8) return C_WHITE;
  if (startupQrStyle == 9) return C_BLACK;
  if (startupQrStyle == 10) return C_BLUE_DARK;
  if (startupQrStyle == 11) return C_BLACK;
  if (startupQrStyle == 12) return C_BLACK;
  return C_WHITE;
}

uint16_t startupQrModuleColor(int x, int y) {
  if (startupQrStyle == 11) return ((x + y) & 1) ? C_WHITE : C_AMBER;
  return startupQrFgColor();
}

bool qrIsFinderCell(int x, int y) {
  return (x < 7 && y < 7) || (x >= 22 && y < 7) || (x < 7 && y >= 22);
}

void drawQrDiamond(int cx, int cy, int r, uint16_t color) {
  gfx->fillTriangle(cx, cy - r, cx + r, cy, cx, cy + r, color);
  gfx->fillTriangle(cx, cy - r, cx - r, cy, cx, cy + r, color);
}

void drawQrModuleShape(int x, int y, int s, uint16_t color, int mx, int my) {
  int pad = 0;
  switch (startupQrBodyStyle) {
    case 1:
      pad = max(1, s / 5);
      gfx->fillRect(x + pad, y + pad, max(1, s - pad * 2), max(1, s - pad * 2), color);
      break;
    case 2:
      gfx->fillCircle(x + s / 2, y + s / 2, max(1, s / 2), color);
      break;
    case 3:
      gfx->fillCircle(x + s / 2, y + s / 2, max(1, s / 3), color);
      break;
    case 4:
      drawQrDiamond(x + s / 2, y + s / 2, max(1, s / 2), color);
      break;
    case 5:
      pad = max(1, s / 4);
      gfx->fillRoundRect(x, y + pad, s, max(1, s - pad * 2), max(1, s / 3), color);
      break;
    case 6:
      pad = max(1, s / 4);
      gfx->fillRoundRect(x + pad, y, max(1, s - pad * 2), s, max(1, s / 3), color);
      break;
    case 7:
      pad = max(1, s / 3);
      gfx->fillRect(x + pad, y, max(1, s - pad * 2), s, color);
      gfx->fillRect(x, y + pad, s, max(1, s - pad * 2), color);
      break;
    case 8:
      pad = ((mx + my) & 1) ? max(1, s / 4) : 0;
      gfx->fillRect(x + pad, y + pad, max(1, s - pad * 2), max(1, s - pad * 2), color);
      break;
    case 9:
      pad = max(1, s / 3);
      gfx->fillRect(x + pad, y + pad, max(1, s - pad * 2), max(1, s - pad * 2), color);
      break;
    case 10:
      gfx->fillCircle(x + s / 2, y + s / 2, max(1, s / 4), color);
      break;
    case 11:
      pad = max(1, s / 2 - 1);
      gfx->fillRect(x + pad, y + pad, max(1, s - pad * 2), max(1, s - pad * 2), color);
      break;
    default:
      gfx->fillRect(x, y, s, s, color);
      break;
  }
}

void drawQrFinderShape(int x, int y, int s, uint16_t fg, uint16_t bg) {
  int w = 7 * s;
  int ring = max(1, s);
  int inner = 3 * s;
  int innerX = x + 2 * s;
  int innerY = y + 2 * s;
  int r = max(1, w / 2);
  switch (startupQrMarkerStyle) {
    case 1:
      gfx->fillRoundRect(x, y, w, w, max(1, s * 2), fg);
      gfx->fillRoundRect(x + ring, y + ring, w - ring * 2, w - ring * 2, max(1, s), bg);
      gfx->fillRoundRect(innerX, innerY, inner, inner, max(1, s), fg);
      break;
    case 2:
      gfx->fillCircle(x + w / 2, y + w / 2, r, fg);
      gfx->fillCircle(x + w / 2, y + w / 2, max(1, r - ring), bg);
      gfx->fillCircle(x + w / 2, y + w / 2, max(1, inner / 2), fg);
      break;
    case 3:
      drawQrDiamond(x + w / 2, y + w / 2, r, fg);
      drawQrDiamond(x + w / 2, y + w / 2, max(1, r - ring), bg);
      drawQrDiamond(x + w / 2, y + w / 2, max(1, inner / 2), fg);
      break;
    case 4:
      gfx->fillRect(x, y, w, ring, fg);
      gfx->fillRect(x, y + w - ring, w, ring, fg);
      gfx->fillRect(x, y, ring, w, fg);
      gfx->fillRect(x + w - ring, y, ring, w, fg);
      gfx->fillRect(innerX, innerY, inner, inner, fg);
      break;
    case 5:
      gfx->fillRect(x, y, w, w, fg);
      gfx->fillRect(x + ring, y + ring, w - ring * 2, w - ring * 2, bg);
      gfx->fillRect(innerX, innerY, inner, inner, fg);
      break;
    default:
      gfx->fillRect(x, y, w, w, fg);
      gfx->fillRect(x + ring, y + ring, w - ring * 2, w - ring * 2, bg);
      gfx->fillRect(innerX, innerY, inner, inner, fg);
      break;
  }
}

uint8_t qrGfMul(uint8_t x, uint8_t y) {
  uint16_t z = 0;
  for (int i = 7; i >= 0; i--) {
    z = (uint16_t)((z << 1) ^ (((z >> 7) & 1) ? 0x11D : 0));
    if ((y >> i) & 1) z ^= x;
  }
  return (uint8_t)z;
}

void qrAppendBits(uint8_t *bits, int &len, uint16_t val, int count) {
  for (int i = count - 1; i >= 0; i--) bits[len++] = (val >> i) & 1;
}

void qrRsGenerator(int deg, uint8_t *gen) {
  uint8_t tmp[16];
  memset(gen, 0, deg + 1);
  gen[0] = 1;
  int glen = 1;
  uint8_t root = 1;
  for (int i = 0; i < deg; i++) {
    memset(tmp, 0, sizeof(tmp));
    for (int j = 0; j < glen; j++) {
      tmp[j] ^= qrGfMul(gen[j], root);
      tmp[j + 1] ^= gen[j];
    }
    glen++;
    for (int j = 0; j < glen; j++) gen[j] = tmp[j];
    root = qrGfMul(root, 2);
  }
}

void qrRsRemainder(const uint8_t *data, int dataLen, const uint8_t *gen, int deg, uint8_t *ecc) {
  memset(ecc, 0, deg);
  for (int i = 0; i < dataLen; i++) {
    uint8_t factor = data[i] ^ ecc[0];
    for (int j = 0; j < deg - 1; j++) ecc[j] = ecc[j + 1];
    ecc[deg - 1] = 0;
    for (int j = 0; j < deg; j++) ecc[j] ^= qrGfMul(gen[j], factor);
  }
}

String wifiQrEscape(String s) {
  s.replace("\\", "\\\\");
  s.replace(";", "\\;");
  s.replace(",", "\\,");
  s.replace(":", "\\:");
  return s;
}

String bootWifiQrPayload() {
  if (startupQrContentMode == 1) return apUrl() + "/intro";
  String ssid = wifiQrEscape(apSSID);
  if (apPass.length() >= 8) return "WIFI:T:WPA;S:" + ssid + ";P:" + wifiQrEscape(apPass) + ";;";
  return "WIFI:T:nopass;S:" + ssid + ";;";
}

void qrSet(bool modules[29][29], bool func[29][29], int x, int y, bool v, bool f = true) {
  if (x < 0 || y < 0 || x >= 29 || y >= 29) return;
  modules[y][x] = v;
  if (f) func[y][x] = true;
}

void qrFinder(bool modules[29][29], bool func[29][29], int x, int y) {
  for (int dy = -1; dy <= 7; dy++) {
    for (int dx = -1; dx <= 7; dx++) {
      int xx = x + dx, yy = y + dy;
      if (xx < 0 || yy < 0 || xx >= 29 || yy >= 29) continue;
      bool on = dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6 &&
                (dx == 0 || dx == 6 || dy == 0 || dy == 6 || (dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4));
      qrSet(modules, func, xx, yy, on, true);
    }
  }
}

uint16_t qrFormatBits(uint8_t eccBits, uint8_t mask) {
  uint16_t data = (eccBits << 3) | mask;
  uint16_t rem = data;
  for (int i = 0; i < 10; i++) rem = (uint16_t)((rem << 1) ^ (((rem >> 9) & 1) ? 0x537 : 0));
  return (uint16_t)(((data << 10) | rem) ^ 0x5412);
}

void makeBootQrMatrix(const String &payload, bool modules[29][29]) {
  const int size = 29;
  const int dataCodewords = 55;
  const int eccLen = 15;
  bool func[29][29];
  memset(modules, 0, sizeof(bool) * 29 * 29);
  memset(func, 0, sizeof(bool) * 29 * 29);

  uint8_t bits[440];
  int bitLen = 0;
  qrAppendBits(bits, bitLen, 0x4, 4);
  int textLen = min((int)payload.length(), 53);
  qrAppendBits(bits, bitLen, textLen, 8);
  for (int i = 0; i < textLen; i++) qrAppendBits(bits, bitLen, (uint8_t)payload[i], 8);
  int cap = dataCodewords * 8;
  int term = min(4, cap - bitLen);
  qrAppendBits(bits, bitLen, 0, term);
  while (bitLen % 8) bits[bitLen++] = 0;

  uint8_t data[55];
  int dataLen = 0;
  for (int i = 0; i < bitLen; i += 8) {
    uint8_t b = 0;
    for (int j = 0; j < 8; j++) b = (uint8_t)((b << 1) | bits[i + j]);
    data[dataLen++] = b;
  }
  for (int pad = 0; dataLen < dataCodewords; pad ^= 1) data[dataLen++] = pad ? 0x11 : 0xEC;

  uint8_t gen[16], ecc[15], codewords[70];
  qrRsGenerator(eccLen, gen);
  qrRsRemainder(data, dataCodewords, gen, eccLen, ecc);
  memcpy(codewords, data, dataCodewords);
  memcpy(codewords + dataCodewords, ecc, eccLen);

  qrFinder(modules, func, 0, 0);
  qrFinder(modules, func, size - 7, 0);
  qrFinder(modules, func, 0, size - 7);
  for (int i = 0; i < size; i++) {
    if (!func[6][i]) qrSet(modules, func, i, 6, (i % 2) == 0, true);
    if (!func[i][6]) qrSet(modules, func, 6, i, (i % 2) == 0, true);
  }
  for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) qrSet(modules, func, 22 + dx, 22 + dy, max(abs(dx), abs(dy)) != 1, true);
  qrSet(modules, func, 8, size - 8, true, true);
  for (int i = 0; i < 9; i++) if (i != 6) { func[8][i] = true; func[i][8] = true; }
  for (int i = 0; i < 8; i++) { func[8][size - 1 - i] = true; func[size - 1 - i][8] = true; }

  uint8_t allBits[560];
  int allLen = 0;
  for (int i = 0; i < 70; i++) qrAppendBits(allBits, allLen, codewords[i], 8);
  int bitIndex = 0;
  bool upward = true;
  for (int right = size - 1; right >= 1; right -= 2) {
    if (right == 6) right--;
    for (int vert = 0; vert < size; vert++) {
      int y = upward ? size - 1 - vert : vert;
      for (int j = 0; j < 2; j++) {
        int x = right - j;
        if (func[y][x]) continue;
        bool bit = bitIndex < allLen ? allBits[bitIndex++] : false;
        bool invert = ((x + y) % 2) == 0;
        modules[y][x] = bit ^ invert;
      }
    }
    upward = !upward;
  }

  uint16_t fmt = qrFormatBits(1, 0);
  for (int i = 0; i <= 5; i++) qrSet(modules, func, 8, i, (fmt >> i) & 1, true);
  qrSet(modules, func, 8, 7, (fmt >> 6) & 1, true);
  qrSet(modules, func, 8, 8, (fmt >> 7) & 1, true);
  qrSet(modules, func, 7, 8, (fmt >> 8) & 1, true);
  for (int i = 9; i < 15; i++) qrSet(modules, func, 14 - i, 8, (fmt >> i) & 1, true);
  for (int i = 0; i < 8; i++) qrSet(modules, func, size - 1 - i, 8, (fmt >> i) & 1, true);
  for (int i = 8; i < 15; i++) qrSet(modules, func, 8, size - 15 + i, (fmt >> i) & 1, true);
  qrSet(modules, func, 8, size - 8, true, true);
}

bool makeBootQrMatrixReliable(const String &payload, bool modules[29][29]) {
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(3)];
  int8_t ok = qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, payload.c_str());
  if (ok != 0 || qrcode.size != STARTUP_QR_SIZE) {
    makeBootQrMatrix(payload, modules);
    return false;
  }
  for (uint8_t yy = 0; yy < qrcode.size; yy++) {
    for (uint8_t xx = 0; xx < qrcode.size; xx++) {
      modules[yy][xx] = qrcode_getModule(&qrcode, xx, yy);
    }
  }
  return true;
}

void drawBootQrBlock(int x, int y, int scale) {
  normalizeQrSettings();
  const int s = scale;
  const int border = startupQrBorder;
  int qrPixels = STARTUP_QR_SIZE * s;
  uint16_t bg = startupQrBgColor();
  bool modules[29][29];
  makeBootQrMatrixReliable(bootWifiQrPayload(), modules);
  gfx->fillRect(x - border, y - border, qrPixels + border * 2, qrPixels + border * 2, bg);
  drawQrFinderShape(x, y, s, startupQrModuleColor(0, 0), bg);
  drawQrFinderShape(x + 22 * s, y, s, startupQrModuleColor(22, 0), bg);
  drawQrFinderShape(x, y + 22 * s, s, startupQrModuleColor(0, 22), bg);
  for (int yy = 0; yy < STARTUP_QR_SIZE; yy++) {
    for (int xx = 0; xx < STARTUP_QR_SIZE; xx++) {
      if (qrIsFinderCell(xx, yy)) continue;
      if (modules[yy][xx]) drawQrModuleShape(x + xx * s, y + yy * s, s, startupQrModuleColor(xx, yy), xx, yy);
    }
  }
}

void drawConfiguredStartupQr() {
  normalizeQrSettings();
  int bootBrightness = map(startupQrBrightness, 1, 10, 25, 255);
  analogWrite(TFT_BL, constrain(bootBrightness, 20, 255));
  uint16_t bg = startupQrStyle == 1 ? C_BLACK : startupQrBgColor();
  gfx->fillScreen(bg);
  int qrPixels = STARTUP_QR_SIZE * startupQrScale;
  drawBootQrBlock((gfx->width() - qrPixels) / 2, (gfx->height() - qrPixels) / 2, startupQrScale);
}

void bootStroke(int x, int y, int w, int h, uint16_t color) {
  gfx->fillRoundRect(x, y, w, h, 2, color);
}

void drawBootLogoGlyph(char c, int x, int y, int s, uint16_t color) {
  int t = s;
  int w = s * 5;
  int h = s * 7;
  if (c == 'P') {
    bootStroke(x, y, t, h, color);
    bootStroke(x, y, w - t, t, color);
    bootStroke(x, y + h / 2 - t / 2, w - t, t, color);
    bootStroke(x + w - t, y, t, h / 2, color);
  } else if (c == 'M') {
    bootStroke(x, y, t, h, color);
    bootStroke(x + w - t, y, t, h, color);
    for (int i = 0; i < s * 3; i++) {
      gfx->drawLine(x + t + i, y + i, x + t + i, y + i + t, color);
      gfx->drawLine(x + w - t - i, y + i, x + w - t - i, y + i + t, color);
    }
  } else if (c == '3') {
    bootStroke(x, y, w, t, color);
    bootStroke(x + w - t, y, t, h, color);
    bootStroke(x + s, y + h / 2 - t / 2, w - s, t, color);
    bootStroke(x, y + h - t, w, t, color);
  } else if (c == 'D') {
    bootStroke(x, y, t, h, color);
    bootStroke(x, y, w - t, t, color);
    bootStroke(x, y + h - t, w - t, t, color);
    bootStroke(x + w - t, y + t, t, h - t * 2, color);
  }
}

void drawBootWelcomeScreen() {
  uint16_t bg = 0x0000;
  uint16_t accent = 0x0679;
  uint16_t muted = 0x8410;
  gfx->fillScreen(bg);
  gfx->fillRect(0, 0, 240, 36, 0x0008);
  gfx->drawFastHLine(0, 36, 240, 0x01DF);

  gfx->setFont(&FreeSansBold10pt7b);
  gfx->setTextSize(1);
  gfx->setTextColor(C_WHITE);
  gfx->setCursor(sx(59), sy(55));
  gfx->print("PM3D.NET");
  gfx->setCursor(sx(59), sy(78));
  gfx->print("Display 2");
  gfx->setFont(NULL);
  gfx->setTextSize(1);
  gfx->setTextColor(muted, bg);
  gfx->setCursor(sx(50), sy(94));
  gfx->print("Ecran de quai miniature");

  gfx->fillRect(54, 116, 132, 3, 0x2104);
  gfx->fillRect(54, 116, 84, 3, accent);
}

void drawBootInfo(const IPAddress &ip) {
  analogWrite(TFT_BL, screenBrightness);
  gfx->setTextWrap(false);
  drawBootWelcomeScreen();
  unsigned long phaseStart = millis();
  while (millis() - phaseStart < 3000UL) {
    dnsServer.processNextRequest();
    server.handleClient();
    delay(10);
    yield();
  }

  gfx->fillScreen(0x0000);
  gfx->fillRect(0, 0, 240, 34, 0x0008);
  gfx->drawFastHLine(0, 34, 240, 0x01DF);
  gfx->setFont(&FreeSansBold10pt7b);
  gfx->setTextSize(1);
  gfx->setTextColor(C_WHITE);
  gfx->setCursor(sx(58), sy(30));
  gfx->print("Connexion");
  gfx->setFont(NULL);
  gfx->setTextSize(1);
  gfx->setTextColor(0x8410, 0x0000);
  gfx->setCursor(sx(28), sy(49));
  gfx->print("Wi-Fi");
  gfx->setTextColor(C_WHITE, 0x0000);
  gfx->setCursor(sx(28), sy(62));
  gfx->print(cutText(apSSID, 28));
  gfx->setTextColor(0x8410, 0x0000);
  gfx->setCursor(sx(28), sy(81));
  gfx->print("Adresse");
  gfx->setTextColor(C_WHITE, 0x0000);
  gfx->setCursor(sx(28), sy(94));
  gfx->print("http://192.168.4.1");

  unsigned long loadStart = millis();
  while (millis() - loadStart < 5000UL) {
    int pct = min(100, (int)((millis() - loadStart) / 50UL));
    int barW = map(pct, 0, 100, 0, 154);
    gfx->fillRect(28, 116, 154, 3, 0x2104);
    gfx->fillRect(28, 116, barW, 3, 0x0679);
    gfx->fillRect(190, 111, 28, 12, 0x0000);
    gfx->setTextColor(0x8410, 0x0000);
    gfx->setCursor(sx(190), sy(113));
    gfx->print(String(pct) + "%");
    dnsServer.processNextRequest();
    server.handleClient();
    delay(50);
    yield();
  }
  analogWrite(TFT_BL, screenBrightness);
}
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  loadDefaults();
  loadConfig();
  prefs.begin("pm3ddisp", false);
  if (!prefs.getBool("demoApPwd1", false)) {
    demoMode = true;
    prefs.putBool("demo", true);
    prefs.putBool("demoApPwd1", true);
  }
  prefs.end();
  applyBelgium2023PresetOnce();
  applyDb2010PresetOnce();
  normalizeTftOffsets();

  gfx = new Arduino_ST7789(bus, TFT_RST, 1, true, tftPanelW, tftPanelH, tftOffsetX1, tftOffsetY1, tftOffsetX2, tftOffsetY2);
  analogWrite(TFT_BL, screenBrightness);
  gfx->begin();

  apSSID = "PM3D-Display-" + macSuffix();

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  IPAddress apIP = stableApIPFromMac();
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  bool apOk = apPass.length() >= 8
    ? WiFi.softAP(apSSID.c_str(), apPass.c_str())
    : WiFi.softAP(apSSID.c_str());
  Serial.print("AP SSID: "); Serial.println(apSSID);
  Serial.print("AP IP stable: "); Serial.println(WiFi.softAPIP());
  dnsServer.start(53, "*", apIP);

  if (localWifiSsid.length()) {
    WiFi.begin(localWifiSsid.c_str(), localWifiPass.c_str());
    Serial.print("Connexion WiFi local : ");
    Serial.println(localWifiSsid);
  }

    server.on("/", handleIntro);
  server.on("/intro", handleIntro);
  server.on("/setlang", handleSetLang);
  server.on("/main", handleMain);
  server.on("/config", handleConfigMenu);
  server.on("/settings", handleSettingsPage);
  server.on("/countries", handleCountries);
  server.on("/country", handleCountry);
  server.on("/moveprofile", handleMoveProfile);
  server.on("/setprofile", HTTP_POST, handleSetProfile);
  server.on("/themes", handleThemes);
  server.on("/settheme", handleSetTheme);
  server.on("/setbright", handleSetBright);
  server.on("/advanced", handleAdvanced);
  server.on("/saveadvanced", HTTP_POST, handleSaveAdvanced);
  server.on("/demostart", handleDemoStart);
  server.on("/demostop", handleDemoStop);
  server.on("/stylecfg", handleStyleConfig);
  server.on("/savestylecfg", HTTP_POST, handleSaveStyleConfig);
  server.on("/wifiscan", handleWifiScan);
  server.on("/savewifi", HTTP_POST, handleSaveWifi);
  server.on("/updates", handleUpdates);
  server.on("/onlineota", HTTP_POST, handleOnlineOta);

  server.on("/packmanager", handlePackManager);
  server.on("/ota", HTTP_POST, handleOtaDone, handleOtaUpload);
  server.on("/live", handleLiveDisplay);
  server.on("/previewdata", handlePreviewData);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/generate_204", handleCaptivePortal);
  server.on("/gen_204", handleCaptivePortal);
  server.on("/hotspot-detect.html", handleCaptivePortal);
  server.on("/library/test/success.html", handleCaptivePortal);
  server.on("/ncsi.txt", handleCaptivePortal);
  server.on("/connecttest.txt", handleCaptivePortal);
  server.onNotFound(handleCaptivePortal);
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

  if (demoMode) {
    demoLastSwitch = 0;
    updateDemoMode(millis());
  } else {
    drawScreenFull();
  }
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  unsigned long now = millis();

  if (demoMode) {
    updateDemoMode(now);
    delay(10);
    yield();
    return;
  }

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

  if (displayProfile == "de_db_2010_2015" && now - lastDbClock >= 360UL) {
    lastDbClock = now;
    drawRowsOnly();
  }

  if (displayProfile == "fr_sncf_1990_flipflap" && now - lastRetro >= 80UL) {
    lastRetro = now;
    drawSncf1990FlipFlapRows();
  }

  int scrollableRows = filledRowCount();
  int visibleRows = max(1, scrollWindowRows());
  if (scrollableRows <= visibleRows && scrollOffset != 0) {
    scrollOffset = 0;
    if (!retroAnimating) drawScreenFull();
  }
  if (scrollableRows > visibleRows && !retroAnimating && displayProfile != "fr_sncf_1990_flipflap" && now - lastScroll >= (unsigned long)scrollDelayMs) {
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

  // SNCB bleu rÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â©cent fidÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¨le photo
  if (!rowsOnlyPass) gfx->fillScreen(0x01D7);

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

    // retard/supprimÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â©
    String delay = rows[i][1];
    if(delay.length()>0){
      uint16_t boxColor = red;
      int bw = (delay=="Supprime") ? 52 : 28;
      gfx->fillRect(34,y+1,bw,rowH-3,boxColor);
      gfx->setTextColor(white, boxColor);
      gfx->setCursor(36,y+3);
      gfx->print(delay);
    }

    // heure corrigÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â©e
    gfx->setTextColor(white, blueBg);
    if(String(rows[i][2]).length()>0){
      gfx->setCursor(68,y+3);
      gfx->print(rows[i][2]);
    }

    // destination jaune
    gfx->setTextColor(yellow, blueBg);
    gfx->setCursor(104,y+3);
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




