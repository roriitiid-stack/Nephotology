  #include <Arduino.h>
  #include <FastLED.h>
  #include <NimBLEDevice.h>
  #include <Adafruit_GFX.h>
  #include <Adafruit_SSD1306.h>
  #include <math.h>
  #include <string>
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
  #include <freertos/semphr.h>
  #include <freertos/queue.h>
  #include <stdarg.h>
  #include <stdio.h>
  #include <string.h>

  #define SCREEN_WIDTH  128
  #define SCREEN_HEIGHT  64
  #define OLED_ADDR     0x3C

  #ifndef LED_PIN_STRIP
    #define LED_PIN_STRIP 8
  #endif
  #ifndef LED_PIN_MATRIX
    #define LED_PIN_MATRIX 16
  #endif
  #ifndef LED_PIN_DISC
    #define LED_PIN_DISC 15
  #endif
  #ifndef LED_PIN_CLOUD
    #define LED_PIN_CLOUD 4   // TODO: set to the GPIO you wire the cloud to (was rope pin 4)
  #endif

  #ifndef LED_COUNT_STRIP
    #define LED_COUNT_STRIP 300
  #endif
  #ifndef LED_COUNT_MATRIX
    #define LED_COUNT_MATRIX 320
  #endif
  #ifndef LED_COUNT_DISC
    #define LED_COUNT_DISC 181
  #endif
  #ifndef LED_COUNT_CLOUD
    #define LED_COUNT_CLOUD 50
  #endif

  #ifndef COLOR_ORDER_STRIP
    #define COLOR_ORDER_STRIP GRB
  #endif
  #ifndef COLOR_ORDER_MATRIX
    #define COLOR_ORDER_MATRIX GRB
  #endif
  #ifndef COLOR_ORDER_DISC
    #define COLOR_ORDER_DISC GRB
  #endif
  #ifndef COLOR_ORDER_CLOUD
    #define COLOR_ORDER_CLOUD GRB
  #endif

  #define LED_TYPE WS2812B
  #define DEFAULT_BRIGHT 255

  #ifndef PMS_RX_PIN
    #define PMS_RX_PIN 36    // PMS TX -> GPIO3 (RX0 on the ESP32 module)
  #endif
  #ifndef PMS_TX_PIN
    #define PMS_TX_PIN -1 // PMS RX -> GPIO17 (set to -1 if unused)
  #endif
  #ifndef PMS_BAUD
    #define PMS_BAUD 9600
  #endif
  HardwareSerial& PmsSerial = Serial2;
  // For maximum compatibility with your old code that used Serial1:
  #define Serial1 PmsSerial

  // -----------------------------
  // BLE service & characteristic (same UUIDs as your old sketch)
  // -----------------------------
  static const char* kServiceUUID = "19B10000-E8F2-537E-4F6C-D104768A1214";
  static const char* kCmdUUID     = "19B10001-E8F2-537E-4F6C-D104768A1214";
  static const char* kRespUUID    = "19B10002-E8F2-537E-4F6C-D104768A1214";

  static bool btConnected = false;
  static NimBLECharacteristic* gRespChar = nullptr;
  static QueueHandle_t cmdQueue = nullptr;
  struct CmdMsg { char data[200]; };
  static bool systemPowerOn = true;
  static uint8_t gBrightness = DEFAULT_BRIGHT;
  static uint8_t gMode = 1;
  static uint16_t gFrameDelayMs = 16;
  uint32_t gNowMs = 0;
  static const uint8_t MODE_LOCATOR = 31;

  enum LocatorTarget : uint8_t {
    LOC_ALL = 0,
    LOC_STRIP,
    LOC_MATRIX,
    LOC_DISC,
    LOC_CLOUD
  };

  static uint16_t locatorCount = 0;
  static uint8_t locatorTarget = LOC_ALL;
  static bool locatorActive = false;

  Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

  // Forward
  static void handleCommand(const std::string& s);
  static void respond(const char* msg);
  static void respondf(const char* fmt, ...);

  class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo &connInfo) override {
    btConnected = true;
    Serial.print("[BLE] Connected: ");
    Serial.println(connInfo.getAddress().toString().c_str());
    if (gRespChar != nullptr) {
      gRespChar->setValue("=== Nephotology ===");
      gRespChar->notify();
      gRespChar->setValue("PM2.5 LED Controller");
      gRespChar->notify();
      gRespChar->setValue("Type HELP for commands");
      gRespChar->notify();
    }
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo &connInfo, int reason) override {
    btConnected = false;
    Serial.print("[BLE] Disconnected: ");
    Serial.print(connInfo.getAddress().toString().c_str());
    Serial.print("  reason=");
    Serial.println(reason);

    NimBLEDevice::startAdvertising();
    Serial.println("[BLE] Advertising restarted");
  }
};

  class CommandCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* ch, NimBLEConnInfo &connInfo) override {
      std::string v = ch->getValue();
      if (v.empty() || cmdQueue == nullptr) return;
      CmdMsg msg;
      size_t n = v.size();
      if (n >= sizeof(msg.data)) n = sizeof(msg.data) - 1;
      memcpy(msg.data, v.data(), n);
      msg.data[n] = '\0';
      xQueueSend(cmdQueue, &msg, 0);
    }
  };

  // -----------------------------
  // BLE response helpers — fan responses out to Serial AND notify the
  // response characteristic so untethered clients see the same text.
  // -----------------------------
  static void respond(const char* msg) {
    if (msg == nullptr) return;
    Serial.println(msg);
    if (gRespChar == nullptr || !btConnected) return;
    uint16_t mtu = NimBLEDevice::getMTU();
    size_t maxChunk = (mtu > 3) ? (size_t)(mtu - 3) : 20;
    size_t len = strlen(msg);
    for (size_t off = 0; off < len; off += maxChunk) {
      size_t n = ((len - off) > maxChunk) ? maxChunk : (len - off);
      gRespChar->setValue((const uint8_t*)(msg + off), n);
      gRespChar->notify();
    }
    const uint8_t nl = '\n';
    gRespChar->setValue(&nl, 1);
    gRespChar->notify();
  }

  static void respondf(const char* fmt, ...) {
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    respond(buf);
  }

  // -----------------------------
  // LED arrays
  // -----------------------------
  CRGB ledsStrip[LED_COUNT_STRIP];
  CRGB ledsMatrix[LED_COUNT_MATRIX];
  CRGB ledsDisc[LED_COUNT_DISC];
  CRGB ledsCloud[LED_COUNT_CLOUD];

  // Matrix geometry
  #define MATRIX_WIDTH  8
  #define MATRIX_HEIGHT 40

  static uint16_t xyTable[MATRIX_WIDTH * MATRIX_HEIGHT];
  static float mapXTable[MATRIX_WIDTH];
  static float mapYTable[MATRIX_HEIGHT];
  static bool matrixLutReady = false;

  static void initMatrixLuts() {
    if (matrixLutReady) return;

    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        uint16_t idx;
        if ((y & 1) == 0) idx = (uint16_t)y * MATRIX_WIDTH + (MATRIX_WIDTH - 1 - x);
        else idx = (uint16_t)y * MATRIX_WIDTH + x;
        xyTable[(uint16_t)y * MATRIX_WIDTH + x] = idx;
      }
    }

    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
      mapXTable[x] = (MATRIX_WIDTH <= 1) ? 0.0f
                                         : ((float)x / (float)(MATRIX_WIDTH - 1)) * 2.0f - 1.0f;
    }
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      mapYTable[y] = (MATRIX_HEIGHT <= 1) ? 0.0f
                                          : 1.0f - ((float)y / (float)(MATRIX_HEIGHT - 1)) * 2.0f;
    }

    matrixLutReady = true;
  }

  // 8x40, ROW-serpentine
  // Start at TOP-RIGHT (x=7,y=0) = index 0
  // Row 0 goes right->left, row 1 left->right, etc.
  static inline uint16_t XY(uint8_t x, uint8_t y){
    if (x >= MATRIX_WIDTH || y >= MATRIX_HEIGHT) return 0;
    return xyTable[(uint16_t)y * MATRIX_WIDTH + x];
  }

  static inline float mapX(uint8_t x){
    if (x >= MATRIX_WIDTH) return 0.0f;
    return mapXTable[x];
  }

  static inline float mapY(uint8_t y){
    if (y >= MATRIX_HEIGHT) return 0.0f;
    return mapYTable[y];
  }

  // PM state
  static bool haveReading = false;
  static float pm25_ema = 0.0f;
  static float PM25_MIN = 0.0f;
  static float PM25_MAX = 3500.0f;
  static float EMA_ALPHA = 0.25f;

  // FreeRTOS thread-safety
  SemaphoreHandle_t pmMutex = NULL;

  void updateDisplay(float pm25) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // PM2.5 — large
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print("PM:");
    if (pm25 < 1.0f) display.print("--");
    else display.print((int)round(pm25));

    display.setTextSize(1);
    display.setCursor(104, 8);
    display.print("ug/m3");

    // Mode & Brightness
    display.setCursor(0, 20);
    display.print("M:");
    display.print(gMode);
    display.setCursor(40, 20);
    display.print("B:");
    display.print(gBrightness);

    // Scale
    display.setCursor(0, 32);
    display.print("S:");
    display.print((int)PM25_MAX);

    // Alpha
    display.setCursor(0, 44);
    display.print("A:");
    display.print(EMA_ALPHA, 2);

    display.display();
  }

  // Utils
  static inline float fclamp(float x, float a, float b){ if(x<a) return a; if(x>b) return b; return x; }
  static float pmInvRange = 0.0f;
  static float pmRatioCachePm = -1.0f;
  static float pmRatioCache = 0.0f;

  static inline void updatePmScale() {
    float d = (PM25_MAX - PM25_MIN);
    pmInvRange = (d <= 0.0f) ? 0.0f : (1.0f / d);
    pmRatioCachePm = -1.0f;
  }

  static inline float pmRatio(float pm){
    if (pm == pmRatioCachePm) return pmRatioCache;
    if (pmInvRange <= 0.0f) {
      pmRatioCache = 1.0f;
      pmRatioCachePm = pm;
      return pmRatioCache;
    }
    pmRatioCache = fclamp((pm - PM25_MIN) * pmInvRange, 0.0f, 1.0f);
    pmRatioCachePm = pm;
    return pmRatioCache;
  }
  static inline uint8_t hueForPM(float pm){ uint8_t blue=160, red=0, t=(uint8_t)roundf(255.0f*pmRatio(pm)); return lerp8by8(blue,red,t);}
  static inline void fill_all(const CRGB &c){
    fill_solid(ledsStrip,LED_COUNT_STRIP,c);
    fill_solid(ledsMatrix,LED_COUNT_MATRIX,c);
    fill_solid(ledsDisc,LED_COUNT_DISC,c);
    fill_solid(ledsCloud,LED_COUNT_CLOUD,c);
  }
  static inline void fade_all(uint8_t amt){
    fadeToBlackBy(ledsStrip,LED_COUNT_STRIP,amt);
    fadeToBlackBy(ledsMatrix,LED_COUNT_MATRIX,amt);
    fadeToBlackBy(ledsDisc,LED_COUNT_DISC,amt);
    fadeToBlackBy(ledsCloud,LED_COUNT_CLOUD,amt);
  }

  // -----------------------------
  // PMS parsing (keeps your old frame format)
  // -----------------------------
  static bool readPMSFrame(uint8_t buf[32]){
    while(Serial1.available()){
      if(Serial1.peek()!=0x42){ Serial1.read(); continue; }
      if(Serial1.read()!=0x42) continue;
      uint8_t next=0; if(Serial1.readBytes(&next,1)!=1) return false; if(next!=0x4D) continue;
      buf[0]=0x42; buf[1]=0x4D;
      if(Serial1.readBytes(buf+2,30)!=30) return false;
      if(!(buf[2]==0x00 && buf[3]==0x1C)) continue;
      uint16_t sum=0; for(int i=0;i<30;i++) sum+=buf[i];
      uint16_t csum=((uint16_t)buf[30]<<8)|buf[31];
      if(sum!=csum) continue;
      return true;
    }
    return false;
  }

  static void pollPMS(){
    if(Serial1.available()>=32){
      uint8_t frame[32];
      if(readPMSFrame(frame)){
        uint16_t pm25_atm=((uint16_t)frame[12]<<8)|frame[13];

        float displayPm = pm25_atm;
        // Update PM2.5 value with mutex protection
        if(xSemaphoreTake(pmMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
          if(!haveReading){ pm25_ema = pm25_atm; haveReading = true; }
          else { pm25_ema = EMA_ALPHA*pm25_atm + (1.0f-EMA_ALPHA)*pm25_ema; }
          displayPm = pm25_ema; // show smoothed value on the display
          xSemaphoreGive(pmMutex);
        }

        // Update the display with new PM2.5 value
        updateDisplay(displayPm);
      }
    }
  }
  // Network/Sensor task on Core 0
  void networkTask(void *parameter) {
    for (;;) {
      // Poll sensor data
      pollPMS();

      // Add BLE/WiFi/network routines here as needed
      // Currently just periodic sensor polling

      vTaskDelay(pdMS_TO_TICKS(100)); // Poll every 100ms
    }
  }

  static uint16_t cloudNoiseZ = 0;
  static uint16_t breathPhase = 0;

  // ============================================================
  // Time / disc-fixture helpers (moved out of Nephotokinetics.h so the
  // header stays effects-only). Must be declared BEFORE the include because
  // many effects in that header consume these helpers and constants.
  // ============================================================
  #define NEPHOTO_USE_GNOWMS 1
  static inline uint32_t nowMs() { return gNowMs; }

  // Disc fixture: 8 concentric rings, 181 LEDs total.
  //
  // HARDWARE: the data input wire enters the disc on the OUTERMOST rim, so
  // the physical chain is outer → inner. Ring **index** semantics are still
  // the conventional ones (ring 0 = single centre LED, ring 7 = outer rim),
  // but the per-ring offsets into the led array are reversed: ring 0 lives
  // at idx 180, ring 7 lives at idx 0..47.
  #ifndef DISC_RING_COUNT
  #define DISC_RING_COUNT 8
  #endif
  static const uint8_t  DISC_RING_LENS[DISC_RING_COUNT]    = {   1,   8,  12,  16,  24,  32,  40,  48 };
  static const uint8_t  DISC_RING_OFFSETS[DISC_RING_COUNT] = { 180, 172, 160, 144, 120,  88,  48,   0 };

  // Iterate from ring 0 (highest offset = innermost, last in chain) outward.
  // The first ring whose offset ≤ idx is the owner — the offsets are strictly
  // descending, so ring 0 has the highest offset and ring 7 has offset 0.
  static inline void discIdxToRing(uint16_t idx, uint8_t &ring, uint8_t &pos) {
    for (uint8_t r = 0; r < DISC_RING_COUNT; r++) {
      if (idx >= DISC_RING_OFFSETS[r]) {
        ring = r;
        pos  = (uint8_t)(idx - DISC_RING_OFFSETS[r]);
        return;
      }
    }
    ring = 0; pos = 0;
  }

  static inline float discRadiusNorm(uint8_t ring) {
    return (DISC_RING_COUNT > 1) ? (float)ring / (float)(DISC_RING_COUNT - 1) : 0.0f;
  }

  static inline uint8_t discAngle8(uint8_t ring, uint8_t pos) {
    uint8_t len = DISC_RING_LENS[ring];
    if (len <= 1) return 0;
    return (uint8_t)((uint16_t)pos * 256U / (uint16_t)len);
  }

  static inline void discIdxToXY(uint16_t idx, float &x, float &y, float &r,
                                 uint8_t &ring, uint8_t &pos) {
    discIdxToRing(idx, ring, pos);
    r = discRadiusNorm(ring);
    uint8_t len = DISC_RING_LENS[ring];
    float angle = (len > 1) ? (6.2831853f * (float)pos / (float)len) : 0.0f;
    x = cosf(angle) * r;
    y = sinf(angle) * r;
  }

  // ── Black-hole disc geometry ───────────────────────────────────────────
  // The "Big RGBLED" black-hole effect treats the inner four rings (0..3 =
  // 1+8+12+16 = 37 LEDs) as the dead event horizon — they MUST never light.
  // Ring 4 (24 LEDs) is the accretion disk. Rings 5..7 are the outer halo.
  #define BLACKHOLE_ACCRETION_RING 4
  #define BLACKHOLE_DEAD_RING_MAX  3

  #include "Nephotokinetics.h"

  static const char* locatorTargetName(uint8_t target) {
    switch (target) {
      case LOC_STRIP: return "STRIP";
      case LOC_MATRIX: return "MATRIX";
      case LOC_DISC: return "DISC";
      case LOC_CLOUD: return "CLOUD";
      default: return "ALL";
    }
  }

  static void applyLocatorRange(CRGB* leds, uint16_t count, uint16_t limit, const CRGB& base, const CRGB& head) {
    if (limit == 0) return;
    if (count > limit) count = limit;
    for (uint16_t i = 0; i < count; i++) leds[i] = base;
    if (count > 0) leds[count - 1] = head;
  }

  static void effectLocator() {
    fill_all(CRGB::Black);
    if (!locatorActive) return;

    const CRGB base = CRGB(10, 60, 0);
    const CRGB head = CRGB::White;

    auto paint = [&](CRGB* leds, uint16_t limit) {
      applyLocatorRange(leds, locatorCount, limit, base, head);
    };

    switch (locatorTarget) {
      case LOC_STRIP:
        paint(ledsStrip, LED_COUNT_STRIP);
        break;
      case LOC_MATRIX:
        paint(ledsMatrix, LED_COUNT_MATRIX);
        break;
      case LOC_DISC:
        paint(ledsDisc, LED_COUNT_DISC);
        break;
      case LOC_CLOUD:
        paint(ledsCloud, LED_COUNT_CLOUD);
        break;
      default:
        paint(ledsStrip, LED_COUNT_STRIP);
        paint(ledsMatrix, LED_COUNT_MATRIX);
        paint(ledsDisc, LED_COUNT_DISC);
        paint(ledsCloud, LED_COUNT_CLOUD);
        break;
    }
  }

// Helper functions for command parsing
int extractNumber(const String& cmd, int startPos) {
  String numStr = cmd.substring(startPos);
  numStr.trim();
  return numStr.toInt();
}

float extractFloat(const String& cmd, int startPos) {
  String numStr = cmd.substring(startPos);
  numStr.trim();
  return numStr.toFloat();
}

static void handleCommand(const std::string& s) {
  String cmd = String(s.c_str());
  cmd.trim();
  cmd.toUpperCase();

  respondf("> %s", cmd.c_str());

  // ========== POWER ==========
  if (cmd == "ON" || cmd == "POWER ON") {
    systemPowerOn = true;
    respond("OK Power ON");
    return;
  }

  if (cmd == "OFF" || cmd == "POWER OFF") {
    systemPowerOn = false;
    respond("OK Power OFF");
    return;
  }

  // ========== MODE ==========
  if (cmd.startsWith("MODE")) {
    int m = extractNumber(cmd, 4);
    gMode = (uint8_t)constrain(m, 0, 26);
    respondf("OK Mode %u", gMode);
    return;
  }

  if (cmd.startsWith("M ") || (cmd.startsWith("M") && cmd.length() >= 2 && isdigit(cmd[1]))) {
    int m = extractNumber(cmd, 1);
    gMode = (uint8_t)constrain(m, 0, 26);
    respondf("OK Mode %u", gMode);
    return;
  }

  // Plain number = mode
  if (cmd.length() <= 2 && isdigit(cmd[0])) {
    int m = cmd.toInt();
    gMode = (uint8_t)constrain(m, 0, 26);
    respondf("OK Mode %u", gMode);
    return;
  }

  // ========== BRIGHTNESS ==========
  if (cmd.startsWith("BRIGHTNESS")) {
    int b = extractNumber(cmd, 10);
    gBrightness = constrain(b, 1, 255);
    FastLED.setBrightness(gBrightness);
    respondf("OK Brightness %u", gBrightness);
    return;
  }

  if (cmd.startsWith("BRIGHT")) {
    int b = extractNumber(cmd, 6);
    gBrightness = constrain(b, 1, 255);
    FastLED.setBrightness(gBrightness);
    respondf("OK Brightness %u", gBrightness);
    return;
  }

  if (cmd.startsWith("B ") || (cmd.startsWith("B") && cmd.length() >= 2 && isdigit(cmd[1]))) {
    int b = extractNumber(cmd, 1);
    gBrightness = constrain(b, 1, 255);
    FastLED.setBrightness(gBrightness);
    respondf("OK Brightness %u", gBrightness);
    return;
  }

  // ========== ALPHA (EMA smoothing) ==========
  if (cmd.startsWith("ALPHA")) {
    float a = extractFloat(cmd, 5);
    EMA_ALPHA = constrain(a, 0.0f, 1.0f);
    respondf("OK Alpha %.3f", EMA_ALPHA);
    return;
  }

  if (cmd.startsWith("A ") || (cmd.startsWith("A") && cmd.length() >= 3)) {
    float a = extractFloat(cmd, 1);
    EMA_ALPHA = constrain(a, 0.0f, 1.0f);
    respondf("OK Alpha %.3f", EMA_ALPHA);
    return;
  }

  // ========== SCALE (PM2.5 max value) ==========
  if (cmd.startsWith("SCALE")) {
    float s = extractFloat(cmd, 5);
    PM25_MAX = constrain(s, 100.0f, 9999.0f);
    updatePmScale();
    respondf("OK Scale (PM2.5 max) %.1f", PM25_MAX);
    return;
  }

  if (cmd.startsWith("S ") || (cmd.startsWith("S") && cmd.length() >= 3 && isdigit(cmd[1]))) {
    float s = extractFloat(cmd, 1);
    PM25_MAX = constrain(s, 100.0f, 9999.0f);
    updatePmScale();
    respondf("OK Scale %.1f", PM25_MAX);
    return;
  }

  // ========== PM MIN (baseline) ==========
  if (cmd.startsWith("PMMIN")) {
    float s = extractFloat(cmd, 5);
    PM25_MIN = constrain(s, 0.0f, PM25_MAX - 1.0f);
    updatePmScale();
    respondf("PM min %.1f", PM25_MIN);
    return;
  }

  // ========== FPS / DELAY ==========
  if (cmd == "FPS") {
    uint16_t fps = (gFrameDelayMs > 0) ? (uint16_t)(1000 / gFrameDelayMs) : 0;
    respondf("FPS %u (delay %u ms)", fps, gFrameDelayMs);
    return;
  }

  if (cmd.startsWith("FPS ")) {
    int fps = extractNumber(cmd, 3);
    fps = constrain(fps, 1, 240);
    gFrameDelayMs = (uint16_t)(1000 / fps);
    respondf("FPS %d (delay %u ms)", fps, gFrameDelayMs);
    return;
  }

  if (cmd == "DELAY") {
    respondf("Delay %u ms", gFrameDelayMs);
    return;
  }

  if (cmd.startsWith("DELAY ")) {
    int d = extractNumber(cmd, 5);
    d = constrain(d, 0, 1000);
    gFrameDelayMs = (uint16_t)d;
    respondf("Delay %u ms", gFrameDelayMs);
    return;
  }

  // ========== LOCATOR ==========
  if (cmd.startsWith("FIND ") || cmd.startsWith("LOC ") || cmd.startsWith("LED ")) {
    int startPos = 0;
    if (cmd.startsWith("FIND ")) startPos = 4;
    else if (cmd.startsWith("LOC ")) startPos = 3;
    else startPos = 3;

    String rest = cmd.substring(startPos);
    rest.trim();

    if (rest.length() == 0) {
      respond("Usage: FIND <SEGMENT> <COUNT> | FIND <COUNT> | FIND OFF");
      return;
    }

    if (rest == "OFF") {
      locatorActive = false;
      locatorCount = 0;
      respond("Locator OFF");
      return;
    }

    int spaceIdx = rest.indexOf(' ');
    String tok1 = (spaceIdx < 0) ? rest : rest.substring(0, spaceIdx);
    String tok2 = (spaceIdx < 0) ? String("") : rest.substring(spaceIdx + 1);
    tok1.trim();
    tok2.trim();

    auto isNumber = [&](const String& t) -> bool {
      if (t.length() == 0) return false;
      for (uint16_t i = 0; i < t.length(); i++) {
        if (!isDigit(t[i])) return false;
      }
      return true;
    };

    uint8_t target = locatorTarget;
    uint16_t count = locatorCount;

    if (isNumber(tok1)) {
      target = LOC_ALL;
      count = (uint16_t)tok1.toInt();
    } else {
      if (tok1 == "STRIP") target = LOC_STRIP;
      else if (tok1 == "MATRIX") target = LOC_MATRIX;
      else if (tok1 == "DISC") target = LOC_DISC;
      else if (tok1 == "CLOUD") target = LOC_CLOUD;
      else if (tok1 == "ALL") target = LOC_ALL;
      else {
        respondf("Unknown segment: %s", tok1.c_str());
        respond("Use STRIP, MATRIX, DISC, CLOUD, or ALL");
        return;
      }

      if (!isNumber(tok2)) {
        respond("Missing count. Example: FIND DISC 24");
        return;
      }
      count = (uint16_t)tok2.toInt();
    }

    locatorTarget = target;
    locatorCount = count;
    locatorActive = true;
    gMode = MODE_LOCATOR;
    respondf("Locator %s count=%u", locatorTargetName(locatorTarget), locatorCount);
    return;
  }

  // ========== STATUS ==========
  if (cmd == "STATUS" || cmd == "?") {
    respond("       SYSTEM STATUS");
    respondf("Power:      %s",     systemPowerOn ? "ON" : "OFF");
    respondf("Mode:       %u",     gMode);
    respondf("Brightness: %u",     gBrightness);
    respondf("Alpha:      %.3f",   EMA_ALPHA);
    respondf("Scale:      %.1f",   PM25_MAX);
    respondf("PM min:     %.1f",   PM25_MIN);
    respondf("Delay:      %u ms",  gFrameDelayMs);
    if (haveReading) respondf("PM2.5:      %.1f", pm25_ema);
    respond("--------------------------");
    return;
  }

  // ========== HELP ==========
  if (cmd == "HELP" || cmd == "H") {
    respond("==========================");
    respond("    COMMAND REFERENCE");
    respond("==========================");
    respond("POWER:           ON / OFF");
    respond("MODE 0-26:       MODE 5 | M5 | 5");
    respond("BRIGHT 1-255:    BRIGHTNESS 200 | B200");
    respond("ALPHA 0.0-1.0:   ALPHA 0.25 | A 0.5");
    respond("SCALE 100-9999:  SCALE 4000 | S 2000");
    respond("PMMIN 0-<max>:   PMMIN 25");
    respond("FPS / DELAY:     FPS 60 | DELAY 16");
    respond("LOCATOR:         FIND DISC 24 | LOC STRIP 120");
    respond("                 FIND CLOUD 25 | FIND OFF");
    respond("INFO:            STATUS / ?    HELP / H");
    respond("==========================");
    return;
  }

  // ========== UNKNOWN ==========
  respondf("X Unknown: %s", cmd.c_str());
  respond("Type HELP for commands");
}

  // Setup / Loop
  // -----------------------------
  void setup(){
    delay(200);
    Serial.begin(115200);
    // Serial2 for PMS
    Serial1.begin(PMS_BAUD, SERIAL_8N1, PMS_RX_PIN, PMS_TX_PIN);

    initMatrixLuts();
    updatePmScale();

    // Create the mutex for thread-safe PM2.5 access
    pmMutex = xSemaphoreCreateMutex();
    if (pmMutex == NULL) {
      Serial.println("ERROR: Could not create pmMutex");
      while (1); // Halt if mutex creation fails
    }

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
      Serial.println("SSD1306 init failed");
    } else {
      display.clearDisplay();
      display.display();
    }

    // FastLED buses
    FastLED.addLeds<LED_TYPE, LED_PIN_STRIP,  COLOR_ORDER_STRIP>(ledsStrip,  LED_COUNT_STRIP);
    FastLED.addLeds<LED_TYPE, LED_PIN_MATRIX, COLOR_ORDER_MATRIX>(ledsMatrix, LED_COUNT_MATRIX);
    FastLED.addLeds<LED_TYPE, LED_PIN_DISC, COLOR_ORDER_DISC>(ledsDisc, LED_COUNT_DISC);
    FastLED.addLeds<LED_TYPE, LED_PIN_CLOUD, COLOR_ORDER_CLOUD>(ledsCloud, LED_COUNT_CLOUD);
    FastLED.setBrightness(gBrightness);
    fill_all(CRGB::Black); FastLED.show();

    // Command queue: BLE callback enqueues, loop() drains.
    cmdQueue = xQueueCreate(8, sizeof(CmdMsg));
    if (cmdQueue == NULL) {
      Serial.println("ERROR: Could not create cmdQueue");
      while (1);
    }

    // BLE (same UUIDs)
    NimBLEDevice::init("Nephotology");
    NimBLEDevice::setMTU(247);
    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());
    NimBLEService* svc = server->createService(kServiceUUID);
    NimBLECharacteristic* cmd = svc->createCharacteristic(kCmdUUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    cmd->setCallbacks(new CommandCallbacks());
    gRespChar = svc->createCharacteristic(kRespUUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
    gRespChar->setValue("Nephotology ready");
    svc->start();
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    NimBLEAdvertisementData advData;
    advData.setName("Nephotology");
    advData.addServiceUUID(kServiceUUID);
    adv->setAdvertisementData(advData);
    NimBLEAdvertisementData scanData; // empty scan response is fine
    adv->setScanResponseData(scanData);
    adv->start();

Serial.println("[BLE] Advertising as: Nephotology");
Serial.print("[BLE] MAC: ");
Serial.println(NimBLEDevice::getAddress().toString().c_str());

    // Start network/sensor task on Core 0
    xTaskCreatePinnedToCore(
      networkTask,      // Task function
      "NetworkTask",    // Task name
      4096,             // Stack size (bytes)
      NULL,             // Parameters
      1,                // Priority (0-24, higher is more important)
      NULL,             // Task handle
      0                 // Core 0
    );

    Serial.println("Setup complete: LED effects on Core 1, sensors/network on Core 0");
  }

  void loop(){
    gNowMs = millis();

    // Drain BLE command queue — runs on Core 1 so command-driven state
    // changes (gMode, gBrightness, locator*) land between frames, not
    // mid-dispatch.
    if (cmdQueue != nullptr) {
      CmdMsg msg;
      while (xQueueReceive(cmdQueue, &msg, 0) == pdTRUE) {
        handleCommand(std::string(msg.data));
      }
    }

    // Safely read PM2.5 value from Core 0 task
    float pmValue = 0.0f;
    if(xSemaphoreTake(pmMutex, pdMS_TO_TICKS(1)) == pdTRUE) {
      pmValue = pm25_ema; // copy shared EMA from Core 0
      xSemaphoreGive(pmMutex);
    }

    if(!systemPowerOn){
      fill_all(CRGB::Black);
      FastLED.show();
      delay(gFrameDelayMs);
      return;
    }

    switch(gMode){
      case  0: effectArc(pmValue);                break;
      case  1: effectComet(pmValue);              break;
      case  2: effectBlackHole(pmValue);          break;
      case  3: effectDarkNebula(pmValue);         break;
      case  4: effectStarfield(pmValue);          break;
      case  5: effectQuantumEntanglement(pmValue); break;
      case  6: effectQuantumCascade(pmValue);     break;
      case  7: effectQuantumLattice(pmValue);     break;
      case  8: effectPeople(pmValue);             break;
      case  9: effectCellularInfection(pmValue);  break;
      case 10: effectPulsar(pmValue);             break;
      case 11: effectFluxField(pmValue);          break;
      case 12: effectGravity(pmValue);            break;
      case 13: effectNocturneMirage(pmValue);     break;
      case 14: effectClouds(pmValue);             break;
      case 15: effectMycelialNetwork(pmValue);    break;
      case 16: effectLavaLamp(pmValue);           break;
      case 17: effectElectricalStorm(pmValue);    break;
      case 18: effectChiaroscuroPulse(pmValue);   break;
      case 19: effectElectromagnetism(pmValue);   break;
      case 20: effectTeslaCoil(pmValue);          break;
      case 21: effectLissajousScope(pmValue);     break;
      case 22: effectConvectionCurrents(pmValue); break;
      case 23: effectChronoRift(pmValue);         break;
      case 24: effectChladniCymatics(pmValue);    break;
      case 25: effectPendulumWave(pmValue);       break;
      case 26: effectSynapticCascade(pmValue);    break;
      case MODE_LOCATOR: effectLocator();         break;
      default: effectComet(pmValue);              break;
    }

    FastLED.show();
    delay(gFrameDelayMs); // target frame delay
  }
