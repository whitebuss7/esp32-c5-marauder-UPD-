// ============================================================
// Marauder C5 - ESP32-C5 + NRF24L01 + ILI9341 + XPT2046
// v2 - WiFi Beacon, BLE Beacon, Deauth Detect, DOS Log
// ============================================================

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <RF24.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// ============================================================
// PINS
// ============================================================
#define SPI_SCK   23
#define SPI_MISO  25
#define SPI_MOSI  24

#define TFT_CS     4
#define TFT_DC     2
#define TFT_RST    3
#define TFT_LED    5
#define TOUCH_CS   6

#define NRF_CE     9
#define NRF_CSN    10

// ============================================================
// TOUCH CALIBRATİON
// ============================================================
#define TS_MINX 200
#define TS_MAXX 3800
#define TS_MINY 200
#define TS_MAXY 3800
#define TS_INVERT_X true
#define TS_INVERT_Y true
#define TOUCH_DEBOUNCE_MS 25

#define SCREEN_W 320
#define SCREEN_H 240

// ============================================================
// COLORS
// ============================================================
#define C_BG      ILI9341_BLACK
#define C_FG      ILI9341_WHITE
#define C_ACCENT  0x07E0
#define C_WARN    ILI9341_ORANGE
#define C_ERR     ILI9341_RED
#define C_DIM     0x4208
#define C_BAR     0x07FF
#define C_PURPLE  0xF81F

// ============================================================
// OBJECTS
// ============================================================
Adafruit_ILI9341 tft(&SPI, TFT_DC, TFT_CS, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS);
RF24 radio(NRF_CE, NRF_CSN);

bool nrfOk = false;
bool bleReady = false;

// ============================================================
// BUTTON STRUCT
// ============================================================
struct Button {
  int16_t x, y, w, h;
  const char* label;
  uint16_t color;
};

// ============================================================
// MENU BUTTONS
// ============================================================
Button menuButtons[] = {
  {  5,  30, 100, 26, "WiFi Tara",   C_ACCENT},
  {110,  30, 100, 26, "BLE Tara",    C_BAR},
  {215,  30, 100, 26, "NRF Spec",    C_WARN},
  {  5,  61, 100, 26, "NRF Sayac",   C_PURPLE},
  {110,  61, 100, 26, "WiFi Detay",  C_BAR},
  {215,  61, 100, 26, "Donanim",     C_DIM},
  {  5,  92, 100, 26, "Kanal Anal",  C_ACCENT},
  {110,  92, 100, 26, "Hakkinda",    C_DIM},
  {215,  92, 100, 26, "DOS/Log",     C_ERR},
  {  5, 123, 100, 26, "WiFi Beacon", C_WARN},
  {110, 123, 100, 26, "BLE Beacon",  C_PURPLE},
  {215, 123, 100, 26, "DeauthDet",   C_ERR},
  {  5, 154, 100, 26, "Ayarlar",     C_DIM},
  {110, 154, 100, 26, "Sistem Izle", C_PURPLE},
  {215, 154, 100, 26, "Canli Ag",    C_ACCENT},
  {  5, 185, 100, 26, "Sinyal Grafik", C_BAR},
  {110, 185, 100, 26, "BLE Detay",    C_PURPLE},
  {215, 185, 100, 26, "Hata Kontrol", C_WARN},
};

const int MENU_COUNT = sizeof(menuButtons) / sizeof(menuButtons[0]);

Button backBtn = { 230, 200, 80, 34, "GERI", C_ERR };

// ============================================================
// LOG SYSTEM
// ============================================================
#define LOG_MAX 40
#define LOG_LINE_LEN 40

struct LogEntry {
  char line[LOG_LINE_LEN];
  uint16_t color;
};

static LogEntry logBuf[LOG_MAX];
static volatile int logHead = 0;
static volatile int logCount = 0;
static SemaphoreHandle_t logMutex = nullptr;

void logAdd(const char* msg, uint16_t color) {
  if (logMutex == nullptr) return;
  if (xSemaphoreTake(logMutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
  int idx = logHead;
  strncpy(logBuf[idx].line, msg, LOG_LINE_LEN - 1);
  logBuf[idx].line[LOG_LINE_LEN - 1] = '\0';
  logBuf[idx].color = color;
  logHead = (logHead + 1) % LOG_MAX;
  if (logCount < LOG_MAX) logCount++;
  xSemaphoreGive(logMutex);
}

// ============================================================
// BLE CALLBACK
// ============================================================
#define BLE_MAX_DEVICES 30
#define BLE_SHOW_ROWS   13

struct BleDevice {
  char addr[18];
  char name[24];
  int16_t rssi;
};

static BleDevice bleList[BLE_MAX_DEVICES];
static volatile int bleListCount = 0;

class MarauderScanCallbacks : public NimBLEScanCallbacks {
public:
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    if (dev == nullptr) return;
    String addr = dev->getAddress().toString().c_str();
    for (int i = 0; i < bleListCount; i++) {
      if (strcmp(bleList[i].addr, addr.c_str()) == 0) {
        bleList[i].rssi = dev->getRSSI();
        std::string stdName = dev->getName();
        if (!stdName.empty()) {
          strncpy(bleList[i].name, stdName.c_str(), sizeof(bleList[i].name) - 1);
          bleList[i].name[sizeof(bleList[i].name) - 1] = '\0';
        }
        return;
      }
    }
    if (bleListCount >= BLE_MAX_DEVICES) return;
    BleDevice &d = bleList[bleListCount];
    strncpy(d.addr, addr.c_str(), sizeof(d.addr) - 1);
    d.addr[sizeof(d.addr) - 1] = '\0';
    std::string stdName = dev->getName();
    if (stdName.empty()) {
      strncpy(d.name, "-", sizeof(d.name) - 1);
    } else {
      strncpy(d.name, stdName.c_str(), sizeof(d.name) - 1);
    }
    d.name[sizeof(d.name) - 1] = '\0';
    d.rssi = dev->getRSSI();
    bleListCount++;
  }
};

static MarauderScanCallbacks bleCallbacks;

// ============================================================
// TOUCH
// ============================================================
bool getTouchRaw(uint16_t &x, uint16_t &y) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  int sx = map(p.x, TS_MINX, TS_MAXX, 0, SCREEN_W);
  int sy = map(p.y, TS_MINY, TS_MAXY, 0, SCREEN_H);
  if (TS_INVERT_X) sx = SCREEN_W - sx;
  if (TS_INVERT_Y) sy = SCREEN_H - sy;
  x = constrain(sx, 0, SCREEN_W - 1);
  y = constrain(sy, 0, SCREEN_H - 1);
  return true;
}

bool getTouch(uint16_t &x, uint16_t &y) {
  if (!getTouchRaw(x, y)) return false;
  delay(TOUCH_DEBOUNCE_MS);
  uint16_t x2, y2;
  if (!getTouchRaw(x2, y2)) return false;
  x = x2; y = y2;
  return true;
}

bool inside(const Button& b, uint16_t x, uint16_t y) {
  return x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h;
}

void waitRelease() {
  uint16_t x, y;
  while (getTouchRaw(x, y)) delay(10);
}

// ============================================================
// TFT HELPER
// ============================================================
void drawStatusBar(const char* title, uint16_t color) {
  tft.fillRect(0, 0, SCREEN_W, 22, color);
  tft.setTextSize(2);
  tft.setTextColor(C_BG, color);
  tft.setCursor(8, 3);
  tft.print(title);
  tft.drawFastHLine(0, SCREEN_H - 18, SCREEN_W, C_DIM);
  tft.setTextSize(1);
  tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(6, SCREEN_H - 14);
  tft.print("Marauder C5 v2");
  tft.setCursor(SCREEN_W - 90, SCREEN_H - 14);
  tft.printf("Heap:%6lu", (unsigned long)ESP.getFreeHeap());
}

void drawHeader(const char* title, uint16_t color) {
  tft.fillScreen(C_BG);
  drawStatusBar(title, color);
}

void drawButton(const Button& b) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 5, b.color);
  tft.drawRoundRect(b.x, b.y, b.w, b.h, 5, C_FG);
  tft.setTextColor(C_BG, b.color);
  tft.setTextSize(1);
  int16_t x1, y1;
  uint16_t tw, th;
  tft.getTextBounds(b.label, 0, 0, &x1, &y1, &tw, &th);
  tft.setCursor(b.x + (b.w - tw) / 2, b.y + (b.h - th) / 2);
  tft.print(b.label);
}

bool backPressed() {
  uint16_t x, y;
  return getTouchRaw(x, y) && inside(backBtn, x, y);
}

void waitBack() {
  drawButton(backBtn);
  waitRelease();
  while (!backPressed()) delay(20);
  waitRelease();
}

void drawMenu() {
  tft.fillScreen(C_BG);
  drawStatusBar("Marauder C5 v2", C_ACCENT);
  for (int i = 0; i < MENU_COUNT; i++) {
    drawButton(menuButtons[i]);
  }
}

// ============================================================
// 1 - WiFi SCAN
// ============================================================
void performWiFiScan() {
  drawHeader("WiFi Taraniyor...", C_ACCENT);
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks(false, true);
  drawHeader("WiFi Sonuc", C_FG);
  tft.setTextSize(1);
  if (n <= 0) {
    tft.setTextColor(C_ERR, C_BG);
    tft.setCursor(10, 45);
    tft.println("Ag bulunamadi.");
  } else {
    tft.setTextColor(C_ACCENT, C_BG);
    tft.setCursor(240, 30);
    tft.printf("%d ag", n);
    int shown = min(n, 14);
    char line[64];
    for (int i = 0; i < shown; i++) {
      snprintf(line, sizeof(line), "%-18.18s %4d CH%-3d",
               WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
      tft.setCursor(6, 32 + i * 12);
      tft.setTextColor(C_BAR, C_BG);
      tft.print(line);
    }
  }
  WiFi.scanDelete();
  waitBack();
}

// ============================================================
// 2 - BLE SCAN
// ============================================================
void performBLEScan() {
  drawHeader("BLE Taraniyor...", C_BAR);
  if (!bleReady) {
    tft.setTextColor(C_ERR, C_BG);
    tft.setCursor(10, 50);
    tft.println("BLE baslatilamadi.");
    waitBack();
    return;
  }
  bleListCount = 0;
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scan == nullptr) {
    tft.setTextColor(C_ERR, C_BG);
    tft.setCursor(10, 50);
    tft.println("BLE scan nesnesi yok.");
    waitBack();
    return;
  }
  scan->clearResults();
  scan->getResults(5000, false);

  drawHeader("BLE Sonuc", C_FG);
  tft.setTextSize(1);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.setCursor(240, 30);
  tft.printf("%d cihaz", bleListCount);

  if (bleListCount == 0) {
    tft.setCursor(6, 50);
    tft.setTextColor(C_ERR, C_BG);
    tft.println("Cihaz bulunamadi.");
  } else {
    char line[64];
    int shown = min((int)bleListCount, BLE_SHOW_ROWS);
    for (int i = 0; i < shown; i++) {
      snprintf(line, sizeof(line), "%-12.12s %-17s %4d",
               bleList[i].name, bleList[i].addr, bleList[i].rssi);
      tft.setCursor(4, 32 + i * 13);
      tft.setTextColor(C_BAR, C_BG);
      tft.print(line);
    }
  }
  scan->clearResults();
  waitBack();
}

// ============================================================
// 3 - NRF SPEKTRUM
// ============================================================
void performNRFScan() {
  drawHeader("NRF Spektrum", C_WARN);
  if (!nrfOk) {
    tft.setTextSize(2);
    tft.setCursor(10, 60);
    tft.setTextColor(C_ERR, C_BG);
    tft.println("NRF HATASI!");
    logAdd("[ERR] NRF spektrum baslatilamadi", C_ERR);
    waitBack();
    return;
  }
  radio.stopListening();
  radio.flush_rx();
  radio.setAutoAck(false);
  radio.setDataRate(RF24_2MBPS);
  radio.disableCRC();
  radio.setPALevel(RF24_PA_LOW);

  const int GX = 20, GY = 185, GH = 130, GW = 280;
  uint8_t score[126] = {0};
  tft.setTextSize(1);
  tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(20, 31);
  tft.print("RPD aktivitesi: kanal 0-125; olcum yaklasiktir");

  tft.drawRect(GX - 1, GY - GH - 1, GW + 2, GH + 2, C_DIM);
  tft.setTextSize(1);
  tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(GX, GY + 4);       tft.print("0");
  tft.setCursor(GX + 120, GY + 4); tft.print("CH 60");
  tft.setCursor(GX + 250, GY + 4); tft.print("125");

  drawButton(backBtn);
  waitRelease();

  while (!backPressed()) {
    for (int ch = 0; ch < 126; ch++) {
      radio.setChannel(ch);
      radio.startListening();
      delayMicroseconds(200);
      bool hit = radio.testRPD();
      radio.stopListening();
      score[ch] = (score[ch] * 3) / 4 + (hit ? 25 : 0);
      int h = (score[ch] * GH) / 100;
      if (h > GH) h = GH;
      int x = GX + (ch * GW) / 126;
      tft.fillRect(x, GY - GH, 2, GH - h, C_BG);
      tft.fillRect(x, GY - h, 2, h, h > GH * 0.6 ? C_ERR : C_WARN);
    }
  }
  radio.stopListening();
  waitRelease();
}

// ============================================================
// 4 - NRF COUNTER
// ============================================================
void performNRFCounter() {
  drawHeader("NRF Sayac", C_PURPLE);
  if (!nrfOk) {
    tft.setTextSize(2);
    tft.setCursor(10, 60);
    tft.setTextColor(C_ERR, C_BG);
    tft.println("NRF HATASI!");
    waitBack();
    return;
  }
  const int TEST_CHANNEL = 76;
  radio.stopListening();
  radio.flush_rx();
  radio.setAutoAck(false);
  radio.setDataRate(RF24_1MBPS);
  radio.setPALevel(RF24_PA_LOW);
  radio.setChannel(TEST_CHANNEL);
  radio.startListening();

  unsigned long count = 0, lastUpdate = 0;
  waitRelease();

  while (!backPressed()) {
    if (radio.available()) {
      uint8_t buf[32];
      radio.read(&buf, sizeof(buf));
      count++;
    }
    if (millis() - lastUpdate > 250) {
      lastUpdate = millis();
      tft.fillRect(0, 40, SCREEN_W, 90, C_BG);
      tft.setTextSize(4);
      tft.setTextColor(C_ACCENT, C_BG);
      tft.setCursor(20, 55);
      tft.printf("%lu", count);
      tft.setTextSize(1);
      tft.setCursor(20, 110);
      tft.setTextColor(C_DIM, C_BG);
      tft.printf("CH %d | 1 Mbps", TEST_CHANNEL);
    }
  }
  radio.stopListening();
  waitRelease();
}

// ============================================================
// 5 - WiFi CHANNEL DETAİLS
// ============================================================
void performWiFiDetail() {
  drawHeader("WiFi Ag Detaylari", C_BAR);
  WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  int n = WiFi.scanNetworks(false, true);
  if (n < 0) {
    tft.setTextColor(C_ERR, C_BG);
    tft.setCursor(10, 45);
    tft.println("WiFi tarama hatasi!");
    logAdd("[ERR] WiFi taramasi basarisiz", C_ERR);
    waitBack();
    return;
  }
  tft.setTextSize(1);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.setCursor(6, 27); tft.printf("Bulunan ag: %d", n);
  tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(5, 40); tft.print("SSID / RSSI / CH / Guvenlik");
  int shown = min(n, 10);
  for (int i = 0; i < shown; i++) {
    int y = 53 + i * 14;
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) ssid = "<gizli>";
    if (ssid.length() > 12) ssid = ssid.substring(0, 12);
    wifi_auth_mode_t auth = WiFi.encryptionType(i);
    const char* sec = (auth == WIFI_AUTH_OPEN) ? "Acik" : "Korumali";
    int rssi = WiFi.RSSI(i);
    tft.setCursor(5, y);
    tft.setTextColor(rssi > -60 ? C_ACCENT : (rssi > -75 ? C_WARN : C_FG), C_BG);
    tft.printf("%-12s %4d %2d %-9s", ssid.c_str(), rssi, WiFi.channel(i), sec);
  }
  if (n == 0) {
    tft.setCursor(8, 60); tft.setTextColor(C_WARN, C_BG); tft.print("Ag bulunamadi.");
  }
  WiFi.scanDelete();
  waitBack();
}

// ============================================================
// 6 - HARDWARE STATUS
// ============================================================
void performHardwareInfo() {
  drawHeader("Donanim Durumu", C_ACCENT);
  tft.setTextSize(1);
  tft.setTextColor(C_FG, C_BG);
  tft.setCursor(10, 35); tft.print("Chip: "); tft.println(ESP.getChipModel());
  tft.setCursor(10, 50); tft.print("CPU: ");  tft.print(ESP.getCpuFreqMHz()); tft.println(" MHz");
  tft.setCursor(10, 65); tft.print("Heap: "); tft.print(ESP.getFreeHeap());   tft.println(" byte");
  tft.setCursor(10, 80); tft.print("Flash: ");tft.print(ESP.getFlashChipSize()/1024/1024); tft.println(" MB");

  tft.setCursor(10, 100);
  tft.setTextColor(nrfOk ? C_ACCENT : C_ERR, C_BG);
  tft.print("NRF24: "); tft.println(nrfOk ? "OK" : "HATA");

  tft.setCursor(10, 115);
  tft.setTextColor(bleReady ? C_ACCENT : C_ERR, C_BG);
  tft.print("BLE: "); tft.println(bleReady ? "OK" : "HATA");

  tft.setCursor(10, 130);
  tft.setTextColor(C_ACCENT, C_BG);
  tft.print("WiFi: "); tft.println(WiFi.getMode() == WIFI_OFF ? "OFF" : "ON");

  tft.setCursor(10, 150);
  tft.setTextColor(C_DIM, C_BG);
  tft.println("SPI");
  tft.setCursor(20, 165);
  tft.printf("SCK=%d MISO=%d MOSI=%d", SPI_SCK, SPI_MISO, SPI_MOSI);
  waitBack();
}

// ============================================================
// 7 - WiFi CHANNEL ANALYSIS
// ============================================================
void performWiFiChannelAnalyzer() {
  drawHeader("WiFi Kanal Analiz", C_ACCENT);
  tft.setTextSize(1);
  tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(8, 30);
  tft.println("Pasif WiFi taramasi yapiliyor...");

  WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  int n = WiFi.scanNetworks(false, true);

  if (n < 0) {
    drawHeader("Kanal Analiz", C_ERR);
    tft.setTextColor(C_ERR, C_BG);
    tft.setCursor(10, 55);
    tft.println("Tarama basarisiz!");
    waitBack();
    return;
  }

  int channelCount[14] = {0};
  for (int i = 0; i < n; i++) {
    int c = WiFi.channel(i);
    if (c >= 1 && c <= 13) channelCount[c]++;
  }

  int maxCount = 1;
  for (int ch = 1; ch <= 13; ch++)
    if (channelCount[ch] > maxCount) maxCount = channelCount[ch];

  drawHeader("Kanal Yogunlugu", C_ACCENT);

  const int graphX = 10, graphY = 48, graphW = 300, graphH = 125;
  tft.drawRect(graphX, graphY, graphW, graphH, C_DIM);
  tft.setTextSize(1);
  tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(4, graphY - 5);           tft.print(maxCount);
  tft.setCursor(4, graphY + graphH - 4);  tft.print("0");

  for (int ch = 1; ch <= 13; ch++) {
    int x = graphX + 4 + ((ch - 1) * 22);
    int barHeight = (channelCount[ch] * (graphH - 20)) / maxCount;
    if (barHeight > graphH - 20) barHeight = graphH - 20;
    int y = graphY + graphH - barHeight - 1;
    if (channelCount[ch] > 0)
      tft.fillRect(x, y, 16, barHeight, C_BAR);
    tft.setTextColor(C_FG, C_BG);
    tft.setCursor(x + 4, graphY + graphH + 4);
    tft.print(ch);
    if (channelCount[ch] > 0) {
      tft.setTextColor(C_ACCENT, C_BG);
      tft.setCursor(x + 4, y - 9);
      tft.print(channelCount[ch]);
    }
  }

  tft.setTextColor(C_FG, C_BG);
  tft.setCursor(10, 194);
  tft.printf("Toplam AP: %d", n);

  int busiest = 1;
  for (int ch = 2; ch <= 13; ch++)
    if (channelCount[ch] > channelCount[busiest]) busiest = ch;

  tft.setCursor(10, 208);
  tft.setTextColor(C_WARN, C_BG);
  tft.printf("En yogun: CH%d (%d AP)", busiest, channelCount[busiest]);

  drawButton(backBtn);
  WiFi.scanDelete();
  waitRelease();
  while (!backPressed()) delay(20);
  waitRelease();
}

// ============================================================
// 8 - SETTINGS / SYSTEM MONITORING / LIVE NETWORK ANALYSIS
// ============================================================
static bool backlightEnabled = true;
static bool liveAutoRefresh = true;
static uint32_t liveRefreshMs = 5000;

void drawSettingValue(int y, const char* label, const char* value, uint16_t color) {
  tft.setTextSize(1);
  tft.setTextColor(C_FG, C_BG);
  tft.setCursor(12, y);
  tft.print(label);
  tft.setTextColor(color, C_BG);
  tft.setCursor(170, y);
  tft.print(value);
}

void performSettings() {
  drawHeader("Ayarlar", C_DIM);
  Button lightBtn = {10, 42, 145, 34, "Arka Isik", C_BAR};
  Button intervalBtn = {165, 42, 145, 34, "Tarama Araligi", C_PURPLE};
  Button autoBtn = {10, 88, 145, 34, "Oto Yenileme", C_ACCENT};
  char intervalText[20];

  auto drawSettingsPage = [&]() {
    drawHeader("Ayarlar", C_DIM);
    drawButton(lightBtn);
    drawButton(intervalBtn);
    drawButton(autoBtn);
    drawSettingValue(135, "Arka isik:", backlightEnabled ? "ACIK" : "KAPALI",
                     backlightEnabled ? C_ACCENT : C_ERR);
    snprintf(intervalText, sizeof(intervalText), "%lus", (unsigned long)(liveRefreshMs / 1000));
    drawSettingValue(151, "Tarama araligi:", intervalText, C_BAR);
    drawSettingValue(167, "Oto yenileme:", liveAutoRefresh ? "ACIK" : "KAPALI",
                     liveAutoRefresh ? C_ACCENT : C_ERR);
    drawButton(backBtn);
  };

  drawSettingsPage();
  waitRelease();
  while (!backPressed()) {
    uint16_t x, y;
    if (!getTouch(x, y)) { delay(25); continue; }
    waitRelease();

    if (inside(lightBtn, x, y)) {
      backlightEnabled = !backlightEnabled;
      digitalWrite(TFT_LED, backlightEnabled ? HIGH : LOW);
      logAdd(backlightEnabled ? "[SET] Arka isik acik" : "[SET] Arka isik kapali",
             backlightEnabled ? C_ACCENT : C_WARN);
      drawSettingsPage();
    } else if (inside(intervalBtn, x, y)) {
      if (liveRefreshMs == 3000) liveRefreshMs = 5000;
      else if (liveRefreshMs == 5000) liveRefreshMs = 10000;
      else liveRefreshMs = 3000;
      logAdd("[SET] Tarama araligi degisti", C_BAR);
      drawSettingsPage();
    } else if (inside(autoBtn, x, y)) {
      liveAutoRefresh = !liveAutoRefresh;
      logAdd(liveAutoRefresh ? "[SET] Oto yenileme acik" : "[SET] Oto yenileme kapali",
             liveAutoRefresh ? C_ACCENT : C_WARN);
      drawSettingsPage();
    }
  }
  waitRelease();
}

void performSystemMonitor() {
  drawHeader("Sistem Izleme", C_PURPLE);
  drawButton(backBtn);
  waitRelease();
  unsigned long lastUpdate = 0;

  while (!backPressed()) {
    if (millis() - lastUpdate >= 500) {
      lastUpdate = millis();
      tft.fillRect(0, 28, SCREEN_W, 165, C_BG);
      tft.setTextSize(1);
      tft.setTextColor(C_FG, C_BG);
      tft.setCursor(10, 34); tft.printf("Chip: %s", ESP.getChipModel());
      tft.setCursor(10, 50); tft.printf("CPU: %u MHz", ESP.getCpuFreqMHz());
      tft.setCursor(10, 66); tft.printf("Heap bos: %lu B", (unsigned long)ESP.getFreeHeap());
      tft.setCursor(10, 82); tft.printf("Heap min: %lu B", (unsigned long)ESP.getMinFreeHeap());
      tft.setCursor(10, 98); tft.printf("Uptime: %lu sn", (unsigned long)(millis() / 1000));
      tft.setCursor(10, 114);
      tft.setTextColor(nrfOk ? C_ACCENT : C_ERR, C_BG);
      tft.printf("NRF24: %s", nrfOk ? "HAZIR" : "HATA");
      tft.setCursor(10, 130);
      tft.setTextColor(bleReady ? C_ACCENT : C_ERR, C_BG);
      tft.printf("BLE: %s", bleReady ? "HAZIR" : "HATA");
      tft.setCursor(10, 146);
      tft.setTextColor(WiFi.getMode() == WIFI_OFF ? C_WARN : C_ACCENT, C_BG);
      tft.printf("WiFi modu: %s", WiFi.getMode() == WIFI_OFF ? "KAPALI" : "ETKIN");
      tft.setCursor(10, 162);
      tft.setTextColor(C_BAR, C_BG);
      if (WiFi.status() == WL_CONNECTED) tft.printf("Bagli RSSI: %d dBm", WiFi.RSSI());
      else tft.print("WiFi istemci baglantisi yok");
      tft.setCursor(10, 180);
      tft.setTextColor(C_DIM, C_BG);
      tft.printf("Log: %d/%d", (int)logCount, LOG_MAX);
    }
    delay(20);
  }
  waitRelease();
}

void performLiveNetworkAnalysis() {
  drawHeader("Canli Ag Analizi", C_ACCENT);
  tft.setTextSize(1);
  tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(8, 27);
  tft.print("Pasif Wi-Fi taramasi");
  drawButton(backBtn);
  waitRelease();

  unsigned long lastScan = 0;
  while (!backPressed()) {
    if (lastScan == 0 || (liveAutoRefresh && millis() - lastScan >= liveRefreshMs)) {
      lastScan = millis();
      WiFi.mode(WIFI_STA);
      WiFi.scanDelete();
      int n = WiFi.scanNetworks(false, true);

      drawHeader("Canli Ag Analizi", C_ACCENT);
      tft.setTextSize(1);
      tft.setTextColor(C_BAR, C_BG);
      tft.setCursor(8, 27);
      tft.printf("Ag: %d | Yenile: %lus", n > 0 ? n : 0,
                 (unsigned long)(liveRefreshMs / 1000));

      int channelCount[14] = {0};
      int shown = min(n > 0 ? n : 0, 8);
      for (int i = 0; i < n; i++) {
        int ch = WiFi.channel(i);
        if (ch >= 1 && ch <= 13) channelCount[ch]++;
      }
      for (int i = 0; i < shown; i++) {
        int y = 43 + i * 15;
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) ssid = "<gizli>";
        if (ssid.length() > 14) ssid = ssid.substring(0, 14);
        tft.setCursor(5, y);
        tft.setTextColor(C_FG, C_BG);
        tft.print(ssid);
        int rssi = WiFi.RSSI(i);
        tft.setTextColor(rssi > -60 ? C_ACCENT : (rssi > -75 ? C_WARN : C_DIM), C_BG);
        tft.setCursor(132, y); tft.printf("%d dBm", rssi);
        tft.setTextColor(C_BAR, C_BG);
        tft.setCursor(232, y); tft.printf("CH%d", WiFi.channel(i));
      }

      int busiest = 1;
      for (int ch = 2; ch <= 13; ch++)
        if (channelCount[ch] > channelCount[busiest]) busiest = ch;

      tft.drawFastHLine(6, 170, 308, C_DIM);
      tft.setCursor(8, 177);
      tft.setTextColor(C_FG, C_BG);
      tft.printf("En yogun kanal: CH%d (%d ag)", busiest, channelCount[busiest]);
      tft.setCursor(8, 190);
      tft.setTextColor(C_DIM, C_BG);
      tft.print("Yalnizca gorunen aglar sayilir.");
      WiFi.scanDelete();
      drawButton(backBtn);
    }
    delay(20);
  }
  WiFi.scanDelete();
  waitRelease();
}

// ============================================================
// 8 - ABOUT
// ============================================================
void performAbout() {
  drawHeader("Hakkinda", C_DIM);
  tft.setTextSize(1);
  tft.setTextColor(C_FG, C_BG);
  tft.setCursor(10, 40); tft.println("Marauder C5 v2");
  tft.setCursor(10, 55); tft.println("ESP32-C5 + NRF24L01");
  tft.setCursor(10, 70); tft.println("ILI9341 + XPT2046");
  tft.setCursor(10, 90); tft.println("WiFi / BLE / NRF");
  tft.setCursor(10, 105); tft.println("tarama ve analiz araclari");
  tft.setCursor(10, 140);
  tft.setTextColor(C_DIM, C_BG);
  tft.println("Egitim ve test amacli.");
  waitBack();
}

// ============================================================
// 9 - DOS / LOG
// ============================================================
void performDOSLog() {
  drawHeader("DOS / Log", C_ERR);

  const int LOG_SHOW = 11;
  const int LOG_Y0 = 30;
  const int LINE_H = 13;

  tft.setTextSize(1);
  tft.fillRect(0, LOG_Y0, SCREEN_W, LOG_SHOW * LINE_H, C_BG);

  if (logCount == 0) {
    tft.setTextColor(C_DIM, C_BG);
    tft.setCursor(8, LOG_Y0 + 4);
    tft.println("Log bos. Tarama yap...");
  } else {
    if (xSemaphoreTake(logMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      int start = (logHead - logCount + LOG_MAX) % LOG_MAX;
      for (int i = 0; i < logCount && i < LOG_SHOW; i++) {
        int idx = (start + i) % LOG_MAX;
        tft.setCursor(4, LOG_Y0 + i * LINE_H);
        tft.setTextColor(logBuf[idx].color, C_BG);
        tft.print(logBuf[idx].line);
      }
      xSemaphoreGive(logMutex);
    }
  }

  tft.drawFastHLine(0, 180, SCREEN_W, C_DIM);
  tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(4, 185);
  tft.printf("Heap:%lu  Log:%d  Up:%lus",
             (unsigned long)ESP.getFreeHeap(),
             (int)logCount,
             (unsigned long)(millis() / 1000));

  drawButton(backBtn);
  waitRelease();
  while (!backPressed()) {
    delay(100);
    if (xSemaphoreTake(logMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      int start = (logHead - logCount + LOG_MAX) % LOG_MAX;
      for (int i = 0; i < logCount && i < LOG_SHOW; i++) {
        int idx = (start + i) % LOG_MAX;
        tft.setCursor(4, LOG_Y0 + i * LINE_H);
        tft.setTextColor(logBuf[idx].color, C_BG);
        tft.print(logBuf[idx].line);
      }
      xSemaphoreGive(logMutex);
    }
    tft.setCursor(4, 185);
    tft.setTextColor(C_DIM, C_BG);
    tft.printf("Heap:%lu  Log:%d  Up:%lus   ",
               (unsigned long)ESP.getFreeHeap(),
               (int)logCount,
               (unsigned long)(millis() / 1000));
  }
  waitRelease();
}

// ============================================================
// 10 - WiFi BEACON SPAM
// ============================================================
static const char* beaconSSIDs[] = {
  "Free WiFi", "Starbucks", "TP-LINK", "Marauder", "FBI VAN",
  "Hidden Net", "HOME-2G", "Guest", "IoT-Dev", "Test-AP",
  "FreeNet", "PublicWiFi", "AndroidAP", "iPhone", "NETGEAR"
};
const int BEACON_SSID_COUNT = sizeof(beaconSSIDs) / sizeof(beaconSSIDs[0]);

static uint8_t beaconFrame[128];

int buildBeaconFrame(const char* ssid) {
  int len = strlen(ssid);
  if (len > 32) len = 32;

  int p = 0;
  beaconFrame[p++] = 0x80; beaconFrame[p++] = 0x00;
  beaconFrame[p++] = 0x00; beaconFrame[p++] = 0x00;
  for (int i = 0; i < 6; i++) beaconFrame[p++] = 0xFF;
  for (int i = 0; i < 6; i++) beaconFrame[p++] = (uint8_t)(esp_random() & 0xFF);
  for (int i = 0; i < 6; i++) beaconFrame[p++] = beaconFrame[10 + i];
  beaconFrame[p++] = 0x00; beaconFrame[p++] = 0x00;
  for (int i = 0; i < 8; i++) beaconFrame[p++] = 0x00;
  beaconFrame[p++] = 0x64; beaconFrame[p++] = 0x00;
  beaconFrame[p++] = 0x01; beaconFrame[p++] = 0x04;
  beaconFrame[p++] = 0x00;
  beaconFrame[p++] = (uint8_t)len;
  for (int i = 0; i < len; i++) beaconFrame[p++] = ssid[i];
  beaconFrame[p++] = 0x01;
  beaconFrame[p++] = 0x04;
  beaconFrame[p++] = 0x82; beaconFrame[p++] = 0x84;
  beaconFrame[p++] = 0x8B; beaconFrame[p++] = 0x96;
  beaconFrame[p++] = 0x03;
  beaconFrame[p++] = 0x01;
  beaconFrame[p++] = 0x06;

  return p;
}

void performWiFiBeaconSpam() {
  drawHeader("WiFi Beacon Spam", C_WARN);

  tft.setTextSize(1);
  tft.setTextColor(C_FG, C_BG);
  tft.setCursor(8, 30);
  tft.println("Sahte SSID yayini (TX)...");

  WiFi.mode(WIFI_AP_STA);
  delay(100);

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);

  tft.setTextColor(C_ACCENT, C_BG);
  tft.setCursor(8, 46);
  tft.println("CH6'da yayin yapiliyor.");

  tft.setCursor(8, 60);
  tft.setTextColor(C_DIM, C_BG);
  tft.print("TX hata verirse C5 kisitli.");

  unsigned long txCount = 0;
  unsigned long lastUpdate = 0;
  int ssidIdx = 0;

  drawButton(backBtn);
  waitRelease();

  while (!backPressed()) {
    const char* ssid = beaconSSIDs[ssidIdx];
    ssidIdx = (ssidIdx + 1) % BEACON_SSID_COUNT;

    int flen = buildBeaconFrame(ssid);

    esp_err_t err = esp_wifi_80211_tx(
      WIFI_IF_AP,
      beaconFrame,
      flen,
      false
    );

    if (err == ESP_OK) {
      txCount++;
      char lbuf[40];
      snprintf(lbuf, sizeof(lbuf), "[BEACON] %s OK", ssid);
      logAdd(lbuf, C_ACCENT);
    } else {
      static unsigned long lastErrLog = 0;
      if (millis() - lastErrLog > 2000) {
        lastErrLog = millis();
        char lbuf[40];
        snprintf(lbuf, sizeof(lbuf), "[BEACON] TX hata %d", (int)err);
        logAdd(lbuf, C_ERR);
      }
    }

    if (millis() - lastUpdate > 500) {
      lastUpdate = millis();
      tft.fillRect(0, 80, SCREEN_W, 100, C_BG);
      tft.setTextSize(3);
      tft.setTextColor(C_ACCENT, C_BG);
      tft.setCursor(10, 90);
      tft.printf("%lu", txCount);
      tft.setTextSize(1);
      tft.setCursor(10, 130);
      tft.setTextColor(C_FG, C_BG);
      tft.printf("Son SSID: %s", ssid);
      tft.setCursor(10, 145);
      tft.setTextColor(C_DIM, C_BG);
      tft.printf("CH6 | %d SSID | Heap:%lu",
                 BEACON_SSID_COUNT,
                 (unsigned long)ESP.getFreeHeap());
    }

    delay(5);
  }

  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_STA);
  waitRelease();
}

// ============================================================
// 11 - BLE BEACON SPAM
// ============================================================
struct BeaconDef {
  const char* label;
  const uint8_t* data;
  uint8_t len;
};

// Apple iBeacon
static const uint8_t appleBeacon[] = {
  0x02, 0x01, 0x06,
  0x1A, 0xFF,
  0x4C, 0x00,
  0x02, 0x15,
  0xE2, 0xC5, 0x6D, 0xB5, 0xDF, 0xFB,
  0x48, 0xD2, 0xB0, 0x60, 0xD0, 0xF5,
  0xA7, 0x10, 0x96, 0xE0,
  0x00, 0x01,
  0x00, 0x01,
  0xC5
};

// Google FastPair
static const uint8_t googleBeacon[] = {
  0x02, 0x01, 0x06,
  0x03, 0x03, 0x2C, 0xFE,
  0x06, 0x16, 0x2C, 0xFE, 0x00, 0x00, 0x00,
  0x02, 0x0A, 0x00
};

// Microsoft SwiftPair
static const uint8_t msBeacon[] = {
  0x02, 0x01, 0x06,
  0x03, 0x03, 0x03, 0xFE,
  0x0D, 0x16, 0x03, 0xFE, 0x01, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
  0x02, 0x0A, 0x00
};

static BeaconDef bleBeacons[] = {
  { "Apple iBeacon",   appleBeacon,  sizeof(appleBeacon)  },
  { "Google FastPair", googleBeacon, sizeof(googleBeacon) },
  { "MS SwiftPair",    msBeacon,     sizeof(msBeacon)     },
};
const int BLE_BEACON_COUNT = sizeof(bleBeacons) / sizeof(bleBeacons[0]);

void performBLEBeaconSpam() {
  drawHeader("BLE Beacon Spam", C_PURPLE);

  if (!bleReady) {
    tft.setTextColor(C_ERR, C_BG);
    tft.setCursor(10, 50);
    tft.println("BLE baslatilamadi.");
    waitBack();
    return;
  }

  tft.setTextSize(1);
  tft.setTextColor(C_FG, C_BG);
  tft.setCursor(8, 30);
  tft.println("BLE beacon yayini (TX)...");

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  if (adv == nullptr) {
    tft.setTextColor(C_ERR, C_BG);
    tft.setCursor(10, 50);
    tft.println("Advertising yok.");
    waitBack();
    return;
  }

  adv->stop();

  unsigned long txCount = 0;
  unsigned long lastUpdate = 0;
  int idx = 0;

  drawButton(backBtn);
  waitRelease();

  while (!backPressed()) {

    BeaconDef& b = bleBeacons[idx];
    idx = (idx + 1) % BLE_BEACON_COUNT;

    NimBLEAdvertisementData advData;
    advData.setFlags(0x06);
    advData.addData(b.data, b.len);

    adv->stop();
    adv->setAdvertisementData(advData);
    adv->setMinInterval(32);
    adv->setMaxInterval(48);
    bool ok = adv->start(0, 0);

    if (ok) {
      txCount++;
      char lbuf[40];
      snprintf(lbuf, sizeof(lbuf), "[BLE] %s OK", b.label);
      logAdd(lbuf, C_PURPLE);
    } else {
      char lbuf[40];
      snprintf(lbuf, sizeof(lbuf), "[BLE] %s HATA", b.label);
      logAdd(lbuf, C_ERR);
    }

    if (millis() - lastUpdate > 500) {
      lastUpdate = millis();
      tft.fillRect(0, 60, SCREEN_W, 120, C_BG);
      tft.setTextSize(3);
      tft.setTextColor(C_PURPLE, C_BG);
      tft.setCursor(10, 70);
      tft.printf("%lu", txCount);
      tft.setTextSize(1);
      tft.setCursor(10, 115);
      tft.setTextColor(C_FG, C_BG);
      tft.printf("Aktif: %s", b.label);
      tft.setCursor(10, 130);
      tft.setTextColor(C_DIM, C_BG);
      tft.printf("%d beacon | Heap:%lu",
                 BLE_BEACON_COUNT,
                 (unsigned long)ESP.getFreeHeap());
      tft.setCursor(10, 145);
      tft.setTextColor(C_ACCENT, C_BG);
      tft.println("C5'te TAM calisir.");
    }

    delay(200);
  }

  adv->stop();
  waitRelease();
}

// ============================================================
// 12 - DEAUTH DETECT
// ============================================================
static volatile uint32_t deauthCount = 0;
static volatile uint32_t disassocCount = 0;
static volatile uint8_t  lastDeauthSrc[6] = {0};
static volatile uint8_t  lastDeauthDst[6] = {0};

static void IRAM_ATTR deauthSniffer(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  const uint8_t* p = pkt->payload;
  uint16_t len = pkt->rx_ctrl.sig_len;

  if (len < 24) return;

  uint8_t fc0 = p[0];
  uint8_t subtype = (fc0 >> 4) & 0x0F;

  if (subtype == 0x0C) {
    deauthCount++;
    for (int i = 0; i < 6; i++) lastDeauthSrc[i] = p[10 + i];
    for (int i = 0; i < 6; i++) lastDeauthDst[i] = p[4 + i];
  } else if (subtype == 0x0A) {
    disassocCount++;
  }
}

void performDeauthDetect() {
  drawHeader("Deauth Detect", C_ERR);

  tft.setTextSize(1);
  tft.setTextColor(C_FG, C_BG);
  tft.setCursor(8, 30);
  tft.println("Promiscuous modda dinleniyor...");

  deauthCount = 0;
  disassocCount = 0;

  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&deauthSniffer);
  esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);

  logAdd("[DEAUTH] Sniffer aktif CH6", C_ERR);

  unsigned long lastUpdate = 0;
  drawButton(backBtn);
  waitRelease();

  while (!backPressed()) {

    if (millis() - lastUpdate > 400) {
      lastUpdate = millis();

      tft.fillRect(0, 50, SCREEN_W, 130, C_BG);

      tft.setTextSize(2);
      tft.setTextColor(C_ERR, C_BG);
      tft.setCursor(10, 55);
      tft.printf("Deauth: %lu", (unsigned long)deauthCount);

      tft.setTextColor(C_WARN, C_BG);
      tft.setCursor(10, 80);
      tft.printf("Disassoc: %lu", (unsigned long)disassocCount);

      tft.setTextSize(1);
      tft.setTextColor(C_DIM, C_BG);
      tft.setCursor(10, 110);
      if (deauthCount > 0) {
        tft.printf("Son src: %02X:%02X:%02X:%02X:%02X:%02X",
                   lastDeauthSrc[0], lastDeauthSrc[1], lastDeauthSrc[2],
                   lastDeauthSrc[3], lastDeauthSrc[4], lastDeauthSrc[5]);
        tft.setCursor(10, 124);
        tft.printf("Son dst: %02X:%02X:%02X:%02X:%02X:%02X",
                   lastDeauthDst[0], lastDeauthDst[1], lastDeauthDst[2],
                   lastDeauthDst[3], lastDeauthDst[4], lastDeauthDst[5]);
      } else {
        tft.setTextColor(C_ACCENT, C_BG);
        tft.println("Saldiri yok. Temiz.");
      }

      tft.setCursor(10, 145);
      tft.setTextColor(C_ACCENT, C_BG);
      tft.println("CH6 | Promiscuous aktif");
    }

    static unsigned long lastLog = 0;
    if (millis() - lastLog > 5000) {
      lastLog = millis();
      char lb[40];
      snprintf(lb, sizeof(lb), "[DEAUTH] D:%lu A:%lu",
               (unsigned long)deauthCount,
               (unsigned long)disassocCount);
      logAdd(lb, deauthCount > 0 ? C_ERR : C_ACCENT);
    }

    delay(50);
  }

  esp_wifi_set_promiscuous(false);
  waitRelease();
}

// ============================================================
// EK OZELLIKLER: SINYAL GRAFIGI, BLE DETAY, HATA KONTROLU
// ============================================================
void performSignalGraph() {
  const int GX = 18, GY = 48, GW = 284, GH = 112;
  int16_t samples[48];
  for (int i = 0; i < 48; i++) samples[i] = -100;
  int sampleCount = 0;
  unsigned long lastSample = 0;
  drawHeader("Canli Sinyal Graf", C_BAR);
  tft.setTextSize(1);
  tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(8, 27); tft.print("En guclu gorunen WiFi agi");
  tft.drawRect(GX, GY, GW, GH, C_DIM);
  tft.setCursor(2, GY - 2); tft.print("-30");
  tft.setCursor(2, GY + GH/2); tft.print("-65");
  tft.setCursor(2, GY + GH - 7); tft.print("-100");
  drawButton(backBtn);
  waitRelease();
  while (!backPressed()) {
    if (millis() - lastSample >= 3000) {
      lastSample = millis();
      WiFi.mode(WIFI_STA);
      WiFi.scanDelete();
      int n = WiFi.scanNetworks(false, true);
      int bestRssi = -100;
      String bestName = "Ag yok";
      for (int i = 0; i < n; i++) {
        if (WiFi.RSSI(i) > bestRssi) { bestRssi = WiFi.RSSI(i); bestName = WiFi.SSID(i); }
      }
      if (sampleCount < 48) samples[sampleCount++] = bestRssi;
      else { for (int i = 1; i < 48; i++) samples[i-1] = samples[i]; samples[47] = bestRssi; }
      WiFi.scanDelete();
      tft.fillRect(42, 27, 270, 16, C_BG);
      tft.setCursor(42, 27); tft.setTextColor(C_ACCENT, C_BG);
      if (bestName.length() > 18) bestName = bestName.substring(0,18);
      tft.printf("%s %d dBm", bestName.c_str(), bestRssi);
      tft.fillRect(GX+1, GY+1, GW-2, GH-2, C_BG);
      for (int i = 1; i < sampleCount; i++) {
        int x1 = GX + 2 + (i-1)*(GW-5)/47;
        int x2 = GX + 2 + i*(GW-5)/47;
        int y1 = GY + GH - 3 - constrain((samples[i-1] + 100)* (GH-6) / 70, 0, GH-6);
        int y2 = GY + GH - 3 - constrain((samples[i] + 100)* (GH-6) / 70, 0, GH-6);
        tft.drawLine(x1,y1,x2,y2,C_BAR);
      }
      tft.setCursor(8, 171); tft.setTextColor(C_DIM, C_BG);
      tft.print("Her 3 sn'de olcum. -100 dBm = zayif/yok.");
      drawButton(backBtn);
    }
    delay(20);
  }
  WiFi.scanDelete();
  waitRelease();
}

void performBLEDetails() {
  drawHeader("BLE Cihaz Detay", C_PURPLE);
  if (!bleReady) {
    tft.setCursor(8, 45); tft.setTextColor(C_ERR, C_BG); tft.print("BLE baslatilamadi");
    logAdd("[ERR] BLE hazir degil", C_ERR);
    waitBack(); return;
  }
  bleListCount = 0;
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scan == nullptr) {
    logAdd("[ERR] BLE scan nesnesi yok", C_ERR);
    tft.setCursor(8, 45); tft.setTextColor(C_ERR, C_BG); tft.print("Scan nesnesi yok");
    waitBack(); return;
  }
  scan->clearResults();
  scan->getResults(6000, false);
  drawHeader("BLE Cihaz Detay", C_PURPLE);
  tft.setTextSize(1);
  tft.setCursor(5, 27); tft.setTextColor(C_ACCENT, C_BG); tft.printf("Cihaz sayisi: %d", (int)bleListCount);
  int shown = min((int)bleListCount, 10);
  for (int i = 0; i < shown; i++) {
    int y = 43 + i*15;
    tft.setCursor(5, y); tft.setTextColor(C_FG, C_BG);
    String nm = bleList[i].name;
    if (nm.length() > 11) nm = nm.substring(0,11);
    tft.printf("%-11s", nm.c_str());
    tft.setCursor(105, y); tft.setTextColor(C_BAR, C_BG); tft.print(bleList[i].addr);
    tft.setCursor(250, y); tft.setTextColor(bleList[i].rssi > -65 ? C_ACCENT : C_WARN, C_BG);
    tft.printf("%d", bleList[i].rssi);
  }
  if (bleListCount == 0) {
    tft.setCursor(8, 50); tft.setTextColor(C_WARN, C_BG); tft.print("Cihaz bulunamadi");
    logAdd("[BLE] Cihaz bulunamadi", C_WARN);
  }
  scan->clearResults();
  waitBack();
}

void performErrorCheck() {
  drawHeader("Hata Denetimi", C_WARN);
  tft.setTextSize(1);
  int y = 35;
  auto report = [&](const char* label, bool ok) {
    tft.setCursor(8, y); tft.setTextColor(ok ? C_ACCENT : C_ERR, C_BG);
    tft.printf("%s: %s", label, ok ? "OK" : "KONTROL ET");
    y += 18;
    if (!ok) logAdd(label, C_ERR);
  };
  report("Log mutex", logMutex != nullptr);
  report("NRF24 baslatma", nrfOk);
  report("BLE baslatma", bleReady);
  report("Heap > 20 KB", ESP.getFreeHeap() > 20 * 1024);
  report("WiFi modu", WiFi.getMode() != WIFI_OFF);
  tft.setCursor(8, y+4); tft.setTextColor(C_DIM, C_BG);
  tft.printf("Heap: %lu byte", (unsigned long)ESP.getFreeHeap());
  tft.setCursor(8, y+20); tft.print("Bu kontrol temel durum testidir.");
  logAdd("[SYS] Hata denetimi tamamlandi", C_ACCENT);
  waitBack();
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("--- Marauder C5 v2 baslatiliyor ---");

  logMutex = xSemaphoreCreateMutex();

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, -1);

  pinMode(TFT_LED, OUTPUT);  digitalWrite(TFT_LED, HIGH);
  pinMode(TFT_CS, OUTPUT);   digitalWrite(TFT_CS, HIGH);
  pinMode(TOUCH_CS, OUTPUT); digitalWrite(TOUCH_CS, HIGH);
  pinMode(NRF_CSN, OUTPUT);  digitalWrite(NRF_CSN, HIGH);

  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(C_BG);

  ts.begin();
  ts.setRotation(1);

  nrfOk = radio.begin();
  if (!nrfOk) {
    Serial.println("NRF24L01 baslatilamadi!");
  } else {
    Serial.println("NRF24L01 HAZIR");
    radio.setPALevel(RF24_PA_LOW);
    radio.stopListening();
    radio.flush_rx();
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  Serial.println("WiFi HAZIR");

  bleReady = NimBLEDevice::init("MarauderC5");
  if (bleReady) {
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (scan != nullptr) {
      scan->setScanCallbacks(&bleCallbacks, false);
      scan->setActiveScan(true);
      scan->setInterval(100);
      scan->setWindow(99);
      Serial.println("BLE HAZIR");
    } else {
      bleReady = false;
    }
  }

  logAdd("[SYS] Marauder C5 v2 hazir", C_ACCENT);
  drawMenu();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  uint16_t x, y;
  if (!getTouch(x, y)) { delay(30); return; }
  waitRelease();

  if      (inside(menuButtons[0],  x, y)) { performWiFiScan();           drawMenu(); }
  else if (inside(menuButtons[1],  x, y)) { performBLEScan();            drawMenu(); }
  else if (inside(menuButtons[2],  x, y)) { performNRFScan();            drawMenu(); }
  else if (inside(menuButtons[3],  x, y)) { performNRFCounter();         drawMenu(); }
  else if (inside(menuButtons[4],  x, y)) { performWiFiDetail();         drawMenu(); }
  else if (inside(menuButtons[5],  x, y)) { performHardwareInfo();       drawMenu(); }
  else if (inside(menuButtons[6],  x, y)) { performWiFiChannelAnalyzer();drawMenu(); }
  else if (inside(menuButtons[7],  x, y)) { performAbout();              drawMenu(); }
  else if (inside(menuButtons[8],  x, y)) { performDOSLog();             drawMenu(); }
  else if (inside(menuButtons[9],  x, y)) { performWiFiBeaconSpam();     drawMenu(); }
  else if (inside(menuButtons[10], x, y)) { performBLEBeaconSpam();      drawMenu(); }
  else if (inside(menuButtons[11], x, y)) { performDeauthDetect();       drawMenu(); }
  else if (inside(menuButtons[12], x, y)) { performSettings();           drawMenu(); }
  else if (inside(menuButtons[13], x, y)) { performSystemMonitor();       drawMenu(); }
  else if (inside(menuButtons[14], x, y)) { performLiveNetworkAnalysis(); drawMenu(); }
  else if (inside(menuButtons[15], x, y)) { performSignalGraph();         drawMenu(); }
  else if (inside(menuButtons[16], x, y)) { performBLEDetails();          drawMenu(); }
  else if (inside(menuButtons[17], x, y)) { performErrorCheck();          drawMenu(); }
}
