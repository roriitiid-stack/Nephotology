// Nephotomentals.ino — Arduino IDE / Arduino Cloud port
//
// Target: ESP32-S3 (any ESP32 with BLE + WiFi works).
//
// Required Boards Manager: "esp32 by Espressif Systems" v2.0.17 or newer.
// Required Libraries:
//   - FastLED
//   - NimBLE-Arduino (h2zero)
//
// Arduino IDE: open this folder ("Nephotomentals") as a sketch.
// Arduino Cloud (Web Editor): upload the whole folder, then add the two
// libraries above from the Libraries panel.

#include <Arduino.h>
#include <FastLED.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <NimBLEDevice.h>
#include "driver/gpio.h"
#include <math.h>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Tell arduino-esp32's initArduino() that BT is in use so it does NOT call
// esp_bt_controller_mem_release(BTDM) before our NimBLE init runs. Must live
// AFTER the includes: as the first function definition in the sketch it would
// otherwise push arduino-cli's auto-generated prototypes above <Arduino.h>,
// where uint8_t/CRGB aren't defined yet.
extern "C" bool btInUse() { return true; }

#ifndef LED_PIN_STRIP
  #define LED_PIN_STRIP 8
#endif
#ifndef LED_PIN_MATRIX
  #define LED_PIN_MATRIX 16
#endif

#ifndef LED_COUNT_STRIP
  #define LED_COUNT_STRIP 298
#endif
#ifndef LED_COUNT_MATRIX
  #define LED_COUNT_MATRIX 320
#endif
#ifndef LED_COUNT_DISC
  #define LED_COUNT_DISC 241
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

#define LED_TYPE WS2812B
#define DEFAULT_BRIGHT 255

#ifndef PMS_RX_PIN
  #define PMS_RX_PIN 36
#endif
#ifndef PMS_TX_PIN
  #define PMS_TX_PIN -1
#endif
#ifndef PMS_BAUD
  #define PMS_BAUD 9600
#endif
HardwareSerial& PmsSerial = Serial2;

// ESP-NOW peers only hear each other on the same WiFi channel. Nephosense
// and Teslasense both pin channel 1; this must match them.
#ifndef ESPNOW_CHANNEL
  #define ESPNOW_CHANNEL 1
#endif

// Onboard status LED — used to show sensor mode at a glance:
//   WIRED   = orange      ESPNOW (wireless) = purple
// Prefers the board's RGB NeoPixel (most ESP32-S3 dev kits define RGB_BUILTIN),
// falls back to a plain LED, falls back to GPIO 2.
#ifndef ONBOARD_LED_PIN
  #ifdef RGB_BUILTIN
    #define ONBOARD_LED_PIN RGB_BUILTIN
    #define ONBOARD_LED_IS_RGB 1
  #elif defined(LED_BUILTIN)
    #define ONBOARD_LED_PIN LED_BUILTIN
  #else
    #define ONBOARD_LED_PIN 2
  #endif
#endif

static inline void setOnboardLed(uint8_t r, uint8_t g, uint8_t b) {
#ifdef ONBOARD_LED_IS_RGB
  // ESP32 Arduino core 3.0+ renamed neopixelWrite() to rgbLedWrite().
  #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    rgbLedWrite(ONBOARD_LED_PIN, r, g, b);
  #else
    neopixelWrite(ONBOARD_LED_PIN, r, g, b);
  #endif
#else
  digitalWrite(ONBOARD_LED_PIN, (r | g | b) ? HIGH : LOW);
#endif
}

// Runtime sensor source — switchable via BLE "SENSOR AUTO" / "SENSOR WIRED" /
// "SENSOR ESPNOW". Both stacks are always initialised. AUTO (the boot default)
// connects to a broadcasting Nephosense node by itself: while its ESP-NOW PM
// packets keep arriving they feed pm25_ema, and when they stop the rig falls
// back to the wired PMS5003 — no BLE command needed either way.
enum SensorMode : uint8_t { SENSOR_WIRED = 0, SENSOR_ESPNOW = 1, SENSOR_AUTO = 2 };
static uint8_t gSensorMode = SENSOR_AUTO;

// AUTO: ESP-NOW counts as "live" while PM packets keep arriving; after this
// long with no packet the rig reverts to the wired sensor. Nephosense
// broadcasts at 1 Hz, so 5 s = five missed packets.
#ifndef ESPNOW_PM_TIMEOUT_MS
  #define ESPNOW_PM_TIMEOUT_MS 5000
#endif
static volatile uint32_t gLastEspNowPmMs = 0;   // millis() of last PM packet (set in WiFi task)
static volatile bool     gEspNowPmSeen   = false;

static inline bool espNowPmLive() {
  return gEspNowPmSeen && (uint32_t)(millis() - gLastEspNowPmMs) < ESPNOW_PM_TIMEOUT_MS;
}

// The source actually feeding pm25_ema right now (resolves AUTO).
static inline uint8_t activePmSource() {
  if (gSensorMode == SENSOR_AUTO) return espNowPmLive() ? SENSOR_ESPNOW : SENSOR_WIRED;
  return gSensorMode;
}

static const char* sensorModeName(uint8_t m) {
  switch (m) {
    case SENSOR_WIRED:  return "WIRED";
    case SENSOR_ESPNOW: return "ESPNOW";
    default:            return "AUTO";
  }
}

// Drives the onboard indicator from the source that is actually live
// (AUTO shows whichever side it has settled on):
//   WIRED  → orange (warm amber)
//   ESPNOW → purple (magenta-violet)
// Values are deliberately low so the onboard NeoPixel reads as a status
// glow, not a flashlight.
static inline void refreshSensorIndicator() {
  if (activePmSource() == SENSOR_ESPNOW) {
    setOnboardLed(20, 0, 32);    // purple
  } else {
    setOnboardLed(40, 12, 0);    // orange
  }
}

// -----------------------------
// BLE service & characteristic (same UUIDs as the IDF sketch)
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
static const uint8_t MODE_LOCATOR = 32;

// Frequency-scope (mode 18) waveform sub-mode. Bare "18" selects the default
// SINE; "18 SQUARE", "18 TRIANGLE", "18 SAW", "18 PULSE", "18 NOISE" pick a
// shape. Declared before the Nephotokinetics.h include so effectFrequencyScope
// can read it.
enum ScopeWave : uint8_t {
  WAVE_SINE = 0,
  WAVE_SQUARE,
  WAVE_TRIANGLE,
  WAVE_SAW,
  WAVE_PULSE,
  WAVE_NOISE
};
static uint8_t gScopeWave = WAVE_SINE;

enum LocatorTarget : uint8_t {
  LOC_ALL = 0,
  LOC_STRIP,
  LOC_MATRIX,
  LOC_DISC
};

static uint16_t locatorCount = 0;
static uint8_t locatorTarget = LOC_ALL;
static bool locatorActive = false;

// Forward
static void handleCommand(const std::string& s);
static void respond(const char* msg);
static void respondf(const char* fmt, ...);

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo &connInfo) override {
    btConnected = true;
    Serial.print("[BLE] Connected: ");
    Serial.println(connInfo.getAddress().toString().c_str());
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

// Fires when a client toggles the CCCD on the response characteristic.
class RespCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* /*ch*/, NimBLEConnInfo& /*info*/, uint16_t subValue) override {
    if (subValue == 0) return;
    respond("== Nephotology subscribed ==");
    respond("Type HELP for commands");
  }
};

// -----------------------------
// BLE response helpers
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
// Matrix and disc are daisy-chained on one data line: matrix DOUT -> disc DIN.
// Treated as one 561-LED chain in hardware, but pointed-into as two segments
// so existing effect code keeps using ledsMatrix[i] / ledsDisc[i] unchanged.
CRGB ledsChainMatrixDisc[LED_COUNT_MATRIX + LED_COUNT_DISC];
CRGB* const ledsMatrix = ledsChainMatrixDisc;
CRGB* const ledsDisc   = ledsChainMatrixDisc + LED_COUNT_MATRIX;

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

// OLED removed in IDF port — keep stub so effect code that may call it links.
static inline void updateDisplay(float /*pm25*/) {}

typedef struct { uint16_t pm25; } __attribute__((packed)) PmPacket;

// Spark packets from a remote "Teslasense" node (an ESP32 parked next to the
// Tesla coil). 4 bytes vs PmPacket's 2, plus a magic byte, so the two can
// never be confused on the wire. `edges` = antenna edges counted since the
// node's previous packet.
#define SPARK_MAGIC 0x54   // 'T'
typedef struct { uint8_t magic; uint8_t seq; uint16_t edges; } __attribute__((packed)) SparkPacket;
volatile uint32_t gSparkRemoteEdges = 0;   // monotonic, bumped in WiFi task

// ESP-NOW receive callback signature changed in ESP32 Arduino core 3.0:
// core 2.x passes the raw MAC pointer; core 3.x wraps it in esp_now_recv_info.
// mac_addr is unused either way — gating only the signature keeps both happy.
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
static void espNowReceive(const esp_now_recv_info_t* /*info*/, const uint8_t* data, int len) {
#else
static void espNowReceive(const uint8_t* /*mac_addr*/, const uint8_t* data, int len) {
#endif
  // Spark packets are accepted regardless of the PM sensor mode — the coil
  // doesn't care which air sensor is active.
  if (len == (int)sizeof(SparkPacket)) {
    SparkPacket sp;
    memcpy(&sp, data, sizeof(sp));
    if (sp.magic == SPARK_MAGIC) gSparkRemoteEdges = gSparkRemoteEdges + sp.edges;
    return;
  }
  if (gSensorMode == SENSOR_WIRED) return;   // manual wired override ignores the radio
  if (len < (int)sizeof(PmPacket)) return;
  PmPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));
  gLastEspNowPmMs = millis();   // AUTO liveness: a PM packet just arrived
  gEspNowPmSeen   = true;
  if (xSemaphoreTake(pmMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    if (!haveReading) { pm25_ema = pkt.pm25; haveReading = true; }
    else { pm25_ema = EMA_ALPHA * pkt.pm25 + (1.0f - EMA_ALPHA) * pm25_ema; }
    xSemaphoreGive(pmMutex);
  }
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
}
static inline void fade_all(uint8_t amt){
  fadeToBlackBy(ledsStrip,LED_COUNT_STRIP,amt);
  fadeToBlackBy(ledsMatrix,LED_COUNT_MATRIX,amt);
  fadeToBlackBy(ledsDisc,LED_COUNT_DISC,amt);
}

// -----------------------------
// PMS parsing (keeps the original frame format)
// -----------------------------
static bool readPMSFrame(uint8_t buf[32]){
  while(PmsSerial.available()){
    if(PmsSerial.peek()!=0x42){ PmsSerial.read(); continue; }
    if(PmsSerial.read()!=0x42) continue;
    uint8_t next=0; if(PmsSerial.readBytes(&next,1)!=1) return false; if(next!=0x4D) continue;
    buf[0]=0x42; buf[1]=0x4D;
    if(PmsSerial.readBytes(buf+2,30)!=30) return false;
    if(!(buf[2]==0x00 && buf[3]==0x1C)) continue;
    uint16_t sum=0; for(int i=0;i<30;i++) sum+=buf[i];
    uint16_t csum=((uint16_t)buf[30]<<8)|buf[31];
    if(sum!=csum) continue;
    return true;
  }
  return false;
}

static void pollPMS(){
  if(PmsSerial.available()>=32){
    uint8_t frame[32];
    if(readPMSFrame(frame)){
      if (activePmSource() != SENSOR_WIRED) return;
      uint16_t pm25_atm=((uint16_t)frame[12]<<8)|frame[13];

      float displayPm = pm25_atm;
      if(xSemaphoreTake(pmMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        if(!haveReading){ pm25_ema = pm25_atm; haveReading = true; }
        else { pm25_ema = EMA_ALPHA*pm25_atm + (1.0f-EMA_ALPHA)*pm25_ema; }
        displayPm = pm25_ema;
        xSemaphoreGive(pmMutex);
      }

      updateDisplay(displayPm);
    }
  }
}

// Network/Sensor task on Core 0
void networkTask(void *parameter) {
  for (;;) {
    pollPMS();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

static uint16_t cloudNoiseZ = 0;
static uint16_t breathPhase = 0;

// ============================================================
// Time / disc-fixture helpers — declared BEFORE the include because
// many effects in Nephotokinetics.h consume these.
// ============================================================
#define NEPHOTO_USE_GNOWMS 1
static inline uint32_t nowMs() { return gNowMs; }

// Disc fixture: 9 concentric rings, 241 LEDs total.
// Data enters on the OUTERMOST rim, so physical chain is outer → inner.
// Ring 0 = single centre LED (idx 240), ring 8 = new outer rim (idx 0..59).
// Ring 8 (60 LEDs) was added outside the old 48-LED rim, so it now leads the
// chain; every inner ring keeps its old ring NUMBER but shifts +60 in index.
#ifndef DISC_RING_COUNT
#define DISC_RING_COUNT 9
#endif
static const uint8_t  DISC_RING_LENS[DISC_RING_COUNT]    = {   1,   8,  12,  16,  24,  32,  40,  48,  60 };
static const uint8_t  DISC_RING_OFFSETS[DISC_RING_COUNT] = { 240, 232, 220, 204, 180, 148, 108,  60,   0 };

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

// ============================================================
// Tesla coil spark sense — Mode 31 (Teslacle) input.
//
// LOCAL: a wire antenna (or a detector's digital output) on
// TESLA_SPARK_PIN. EMI bursts from each spark induce edges; the ISR
// counts them. The 100 µs guard ignores the RF carrier's ringing so a
// hot coil can't starve the CPU — we count spark EVENTS, not carrier
// cycles. Remappable at runtime with "TESLA PIN n".
// REMOTE: a Teslasense node next to the coil broadcasts SparkPacket
// over ESP-NOW; those edges land in gSparkRemoteEdges (see above) and
// are merged with the local count.
// CAL: "TESLA CAL" watches 10 s of coil running and sets the
// full-scale edge rate so the mode's energy curve fits this coil.
// ============================================================
#ifndef TESLA_SPARK_PIN
  #define TESLA_SPARK_PIN 4
#endif
volatile uint32_t gSparkEdges = 0;       // local antenna edges (monotonic)
static uint32_t gSparkSynthEdges = 0;    // ZAP/STORM rehearsal edges (loop context)
static uint32_t gZapUntilMs      = 0;    // ZAP: synthesize one strike until then
static uint32_t gStormUntilMs    = 0;    // STORM: synthesize a session until then
static uint32_t gTeslaCalUntilMs = 0;    // TESLA CAL window end (0 = not calibrating)
static uint16_t gTeslaCalPeak    = 0;    // peak edge rate seen during cal
static uint16_t gTeslaFullHz     = 240;  // edges/s that read as a full storm
static uint16_t gTeslaHz         = 0;    // measured edge rate (0.5 s window)
static uint8_t  gTeslaPin        = TESLA_SPARK_PIN;

// Raw IDF GPIO ISR, NOT Arduino attachInterrupt(): the Arduino wrapper
// installs the ISR service via an IPC call whose small task stack overflows
// once BLE+WiFi are running (boot-loop: "Stack canary watchpoint triggered
// (ipc1)"). gpio_install_isr_service() runs once, early, in setup().
void IRAM_ATTR teslaSparkIsr(void* /*arg*/) {
  static uint32_t lastUs = 0;
  uint32_t nowUs = (uint32_t)micros();
  if ((uint32_t)(nowUs - lastUs) < 100) return;
  lastUs = nowUs;
  gSparkEdges = gSparkEdges + 1;
}

static void teslaAttach(uint8_t pin) {
  gpio_isr_handler_remove((gpio_num_t)gTeslaPin);
  gTeslaPin = pin;
  gpio_config_t cfg = {};
  cfg.pin_bit_mask  = 1ULL << gTeslaPin;
  cfg.mode          = GPIO_MODE_INPUT;
  cfg.pull_up_en    = GPIO_PULLUP_DISABLE;
  cfg.pull_down_en  = GPIO_PULLDOWN_ENABLE;
  cfg.intr_type     = GPIO_INTR_POSEDGE;
  gpio_config(&cfg);
  gpio_isr_handler_add((gpio_num_t)gTeslaPin, teslaSparkIsr, NULL);
}

#include "Nephotokinetics.h"

static const char* locatorTargetName(uint8_t target) {
  switch (target) {
    case LOC_STRIP: return "STRIP";
    case LOC_MATRIX: return "MATRIX";
    case LOC_DISC: return "DISC";
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
    default:
      paint(ledsStrip, LED_COUNT_STRIP);
      paint(ledsMatrix, LED_COUNT_MATRIX);
      paint(ledsDisc, LED_COUNT_DISC);
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

// ---- Frequency-scope waveform sub-mode helpers ----
static const char* scopeWaveName(uint8_t w) {
  switch (w) {
    case WAVE_SQUARE:   return "SQUARE";
    case WAVE_TRIANGLE: return "TRIANGLE";
    case WAVE_SAW:      return "SAW";
    case WAVE_PULSE:    return "PULSE";
    case WAVE_NOISE:    return "NOISE";
    default:            return "SINE";
  }
}

static bool scopeWaveFromName(const String& t, uint8_t& out) {
  if (t == "SINE"   || t == "SIN")                  { out = WAVE_SINE;     return true; }
  if (t == "SQUARE" || t == "SQ"  || t == "SQR")    { out = WAVE_SQUARE;   return true; }
  if (t == "TRIANGLE" || t == "TRI" || t == "TRIANG") { out = WAVE_TRIANGLE; return true; }
  if (t == "SAW"    || t == "SAWTOOTH" || t == "RAMP") { out = WAVE_SAW;    return true; }
  if (t == "PULSE"  || t == "PWM")                     { out = WAVE_PULSE;  return true; }
  if (t == "NOISE"  || t == "RANDOM" || t == "RAND" || t == "SNOW") { out = WAVE_NOISE; return true; }
  return false;
}

// Parse an optional trailing waveform word from a mode command (e.g. the
// "SQUARE" in "18 SQUARE") and apply it. No trailing word, or a numeric one,
// means the bare mode number was given → default SINE. An unrecognized word
// keeps SINE and reports the valid choices.
static void applyScopeWaveToken(const String& cmd) {
  int sp = cmd.lastIndexOf(' ');
  String tok = (sp < 0) ? String("") : cmd.substring(sp + 1);
  tok.trim();
  if (tok.length() == 0 || isDigit(tok[0])) { gScopeWave = WAVE_SINE; return; }
  uint8_t w;
  if (scopeWaveFromName(tok, w)) {
    gScopeWave = w;
  } else {
    gScopeWave = WAVE_SINE;
    respondf("? Wave '%s' - use SINE/SQUARE/TRIANGLE/SAW/PULSE/NOISE", tok.c_str());
  }
}

// Mode-set acknowledgement; appends the waveform for the scope mode.
static void respondMode() {
  if (gMode == 18) respondf("OK Mode %u (%s)", gMode, scopeWaveName(gScopeWave));
  else             respondf("OK Mode %u", gMode);
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
  // Mode 18 (FrequencyScope) accepts an optional waveform word, e.g.
  // "MODE 18 SQUARE" / "M18 TRIANGLE" / "18 SAW". A bare number = SINE.
  if (cmd.startsWith("MODE")) {
    int m = extractNumber(cmd, 4);
    gMode = (uint8_t)constrain(m, 0, 31);
    applyScopeWaveToken(cmd);
    respondMode();
    return;
  }

  if (cmd.startsWith("M ") || (cmd.startsWith("M") && cmd.length() >= 2 && isdigit(cmd[1]))) {
    int m = extractNumber(cmd, 1);
    gMode = (uint8_t)constrain(m, 0, 31);
    applyScopeWaveToken(cmd);
    respondMode();
    return;
  }

  // Plain number = mode, optionally with a scope waveform: "18" or "18 SQUARE".
  if (isdigit(cmd[0])) {
    int sp = cmd.indexOf(' ');
    String numTok = (sp < 0) ? cmd : cmd.substring(0, sp);
    int m = numTok.toInt();
    gMode = (uint8_t)constrain(m, 0, 31);
    applyScopeWaveToken(cmd);
    respondMode();
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

  if (cmd.startsWith("A ") || (cmd.startsWith("A") && cmd.length() >= 3 && isdigit(cmd[1]))) {
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
      else if (tok1 == "ALL") target = LOC_ALL;
      else {
        respondf("Unknown segment: %s", tok1.c_str());
        respond("Use STRIP, MATRIX, DISC, or ALL");
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

  // ========== SENSOR SOURCE ==========
  if (cmd.startsWith("SENSOR")) {
    String rest = cmd.substring(6);
    rest.trim();
    if (rest.length() == 0) {
      if (gSensorMode == SENSOR_AUTO)
        respondf("Sensor: AUTO -> %s", sensorModeName(activePmSource()));
      else
        respondf("Sensor: %s", sensorModeName(gSensorMode));
      return;
    }
    if (rest == "AUTO" || rest == "A") {
      gSensorMode = SENSOR_AUTO;
      refreshSensorIndicator();
      respondf("OK Sensor AUTO (now %s; ESPNOW whenever Nephosense is heard)",
               sensorModeName(activePmSource()));
      return;
    }
    if (rest == "WIRED" || rest == "W" || rest == "UART") {
      gSensorMode = SENSOR_WIRED;
      refreshSensorIndicator();
      respond("OK Sensor WIRED (PMS5003 over UART)");
      return;
    }
    if (rest == "ESPNOW" || rest == "E" || rest == "WIRELESS" || rest == "WIFI") {
      gSensorMode = SENSOR_ESPNOW;
      refreshSensorIndicator();
      respond("OK Sensor ESPNOW (wireless from Nephosense)");
      return;
    }
    respond("Usage: SENSOR AUTO | SENSOR WIRED | SENSOR ESPNOW | SENSOR");
    return;
  }

  // ========== TESLA / ZAP / STORM (Mode 31 — Teslacle) ==========
  if (cmd == "ZAP") {
    gZapUntilMs = gNowMs + 150;
    respondf("OK Zap%s", (gMode != 31) ? " (MODE 31 to watch)" : "");
    return;
  }

  if (cmd.startsWith("STORM")) {
    String rest = cmd.substring(5);
    rest.trim();
    if (rest == "OFF") {
      gStormUntilMs = 0;
      gZapUntilMs = 0;
      respond("OK Storm OFF");
      return;
    }
    int s = (rest.length() == 0) ? 10 : rest.toInt();
    s = constrain(s, 1, 120);
    gStormUntilMs = gNowMs + (uint32_t)s * 1000UL;
    respondf("OK Storm %d s%s", s, (gMode != 31) ? " (MODE 31 to watch)" : "");
    return;
  }

  if (cmd.startsWith("TESLA")) {
    String rest = cmd.substring(5);
    rest.trim();
    if (rest.length() == 0) {
      respond("      TESLA SENSE");
      respondf("Pin:        GPIO %u", gTeslaPin);
      respondf("Edge rate:  %u /s", gTeslaHz);
      respondf("Local:      %lu edges", (unsigned long)gSparkEdges);
      respondf("Remote:     %lu edges", (unsigned long)gSparkRemoteEdges);
      respondf("Full scale: %u /s", gTeslaFullHz);
      respondf("Energy:     %u%%", (uint8_t)(gWcEnergy * 100.0f));
      if (gTeslaCalUntilMs) respond("Calibration RUNNING - keep the coil singing");
      return;
    }
    if (rest == "CAL") {
      gTeslaCalUntilMs = gNowMs + 10000;
      if (gTeslaCalUntilMs == 0) gTeslaCalUntilMs = 1;
      gTeslaCalPeak = 0;
      respond("Calibrating 10 s - run the coil at full song NOW");
      return;
    }
    if (rest.startsWith("PIN")) {
      int p = extractNumber(rest, 3);
      if (p < 0 || p > 48 || p == LED_PIN_STRIP || p == LED_PIN_MATRIX ||
          p == PMS_RX_PIN || p == 19 || p == 20) {
        respondf("X GPIO %d not usable (LED/PMS/USB pins reserved)", p);
        return;
      }
      teslaAttach((uint8_t)p);
      respondf("OK Tesla sense on GPIO %d", p);
      return;
    }
    if (rest.startsWith("SCALE")) {
      int hzv = extractNumber(rest, 5);
      gTeslaFullHz = (uint16_t)constrain(hzv, 20, 5000);
      respondf("OK Tesla full-scale %u edges/s", gTeslaFullHz);
      return;
    }
    respond("Usage: TESLA | TESLA CAL | TESLA PIN n | TESLA SCALE n");
    return;
  }

  // ========== STATUS ==========
  if (cmd == "STATUS" || cmd == "?") {
    respond("       SYSTEM STATUS");
    respondf("Power:      %s",     systemPowerOn ? "ON" : "OFF");
    respondf("Mode:       %u",     gMode);
    if (gMode == 18) respondf("Scope wave: %s", scopeWaveName(gScopeWave));
    respondf("Brightness: %u",     gBrightness);
    respondf("Alpha:      %.3f",   EMA_ALPHA);
    respondf("Scale:      %.1f",   PM25_MAX);
    respondf("PM min:     %.1f",   PM25_MIN);
    respondf("Delay:      %u ms",  gFrameDelayMs);
    if (gSensorMode == SENSOR_AUTO)
      respondf("Sensor:     AUTO -> %s", sensorModeName(activePmSource()));
    else
      respondf("Sensor:     %s",   sensorModeName(gSensorMode));
    respondf("Tesla:      GPIO %u  %u edge/s", gTeslaPin, gTeslaHz);
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
    respond("MODE 0-31:       MODE 5 | M5 | 5");
    respond("SCOPE WAVE (18): SQUARE TRIANGLE SAW PULSE NOISE (bare 18=sine)");
    respond("TESLA (31):      TESLA | TESLA CAL | TESLA PIN 4 | TESLA SCALE 240");
    respond("SPARK TEST:      ZAP | STORM 10 | STORM OFF");
    respond("BRIGHT 1-255:    BRIGHTNESS 200 | B200");
    respond("ALPHA 0.0-1.0:   ALPHA 0.25 | A 0.5");
    respond("SCALE 100-9999:  SCALE 4000 | S 2000");
    respond("PMMIN 0-<max>:   PMMIN 25");
    respond("FPS / DELAY:     FPS 60 | DELAY 16");
    respond("LOCATOR:         FIND DISC 24 | LOC STRIP 120 | FIND OFF");
    respond("SENSOR:          SENSOR AUTO | SENSOR WIRED | SENSOR ESPNOW | SENSOR");
    respond("INFO:            STATUS / ?    HELP / H");
    respond("==========================");
    return;
  }

  // ========== UNKNOWN ==========
  respondf("X Unknown: %s", cmd.c_str());
  respond("Type HELP for commands");
}

// -----------------------------
// Setup / Loop — standard Arduino entry points.
// Arduino-ESP32 already initialises NVS and runs loop() on Core 1.
// -----------------------------
void setup(){
  delay(200);
  Serial.begin(115200);
  PmsSerial.begin(PMS_BAUD, SERIAL_8N1, PMS_RX_PIN, PMS_TX_PIN);

  // GPIO ISR service for the Tesla spark sense — installed early and
  // directly via IDF while the system is quiet (see teslaSparkIsr note).
  gpio_install_isr_service(0);

  initMatrixLuts();
  updatePmScale();

  pmMutex = xSemaphoreCreateMutex();
  if (pmMutex == NULL) {
    Serial.println("ERROR: Could not create pmMutex");
    while (1) { delay(1000); }
  }

  // Command queue: BLE callback enqueues, loop() drains. Must exist
  // BEFORE NimBLEDevice::init in case a peer connects + writes during init.
  cmdQueue = xQueueCreate(8, sizeof(CmdMsg));
  if (cmdQueue == NULL) {
    Serial.println("ERROR: Could not create cmdQueue");
    while (1) { delay(1000); }
  }

  // Onboard indicator — orange in WIRED, purple in ESPNOW. Initialised
  // here so the user sees the boot-default (WIRED → orange) immediately.
  pinMode(ONBOARD_LED_PIN, OUTPUT);
  refreshSensorIndicator();

  // BLE init runs FIRST, before FastLED. The BLE controller reserves a
  // large block of internal RAM at init time; doing this before FastLED's
  // RMT driver allocates its DMA encoding buffers avoids a heap-corruption
  // crash inside btdm_controller_init that was previously observed when
  // FastLED's RMT5 allocator ran first on this 561-LED chain.
  NimBLEDevice::init("Nephotology");
  NimBLEDevice::setMTU(247);
  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  NimBLEService* svc = server->createService(kServiceUUID);

  NimBLECharacteristic* cmd = svc->createCharacteristic(
      kCmdUUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  cmd->setCallbacks(new CommandCallbacks());
  {
    NimBLEDescriptor* d = cmd->createDescriptor("2901", NIMBLE_PROPERTY::READ, 48);
    d->setValue("Command (write text, e.g. HELP)");
  }

  gRespChar = svc->createCharacteristic(
      kRespUUID,
      NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE | NIMBLE_PROPERTY::READ);
  gRespChar->setCallbacks(new RespCallbacks());
  gRespChar->setValue("Nephotology ready - subscribe to receive responses");
  {
    NimBLEDescriptor* d = gRespChar->createDescriptor("2901", NIMBLE_PROPERTY::READ, 48);
    d->setValue("Responses (tap subscribe icon!)");
  }

  svc->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData advData;
  advData.setName("Nephotology");
  advData.addServiceUUID(kServiceUUID);
  adv->setAdvertisementData(advData);
  NimBLEAdvertisementData scanData;
  adv->setScanResponseData(scanData);
  adv->start();

  Serial.println("[BLE] Advertising as: Nephotology");
  Serial.print("[BLE] MAC: ");
  Serial.println(NimBLEDevice::getAddress().toString().c_str());

  // FastLED buses — initialised AFTER BLE so the BLE controller's internal
  // RAM pool is reserved first. HARDWARE: matrix DOUT is wired to disc DIN,
  // so matrix + disc are one 561-LED chain on LED_PIN_MATRIX.
  FastLED.addLeds<LED_TYPE, LED_PIN_STRIP,  COLOR_ORDER_STRIP>(ledsStrip,  LED_COUNT_STRIP);
  FastLED.addLeds<LED_TYPE, LED_PIN_MATRIX, COLOR_ORDER_MATRIX>(
      ledsChainMatrixDisc, LED_COUNT_MATRIX + LED_COUNT_DISC);
  FastLED.setBrightness(gBrightness);
  fill_all(CRGB::Black); FastLED.show();

  // Wired PMS poller on Core 0. Always runs; data is only applied while the
  // wired side is the active source, so it also drains the UART otherwise.
  xTaskCreatePinnedToCore(networkTask, "NetworkTask", 4096, NULL, 1, NULL, 0);

  // WiFi STA (no AP connection) + ESP-NOW — receives PM2.5 from a Nephosense
  // node and sparks from a Teslasense node. The channel is pinned to match
  // the senders so they find each other without any pairing. In the default
  // AUTO sensor mode the PM packets are used the moment they arrive.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: esp_now_init failed");
  } else {
    esp_now_register_recv_cb(espNowReceive);
  }

  // Tesla spark sense — antenna wire on GPIO 4 by default ("TESLA PIN n" to move).
  // Remote Teslasense packets arrive through the ESP-NOW callback above.
  teslaAttach(gTeslaPin);
  Serial.printf("[TESLA] Spark sense on GPIO %u\n", gTeslaPin);

  Serial.println("Setup complete: sensor AUTO - uses Nephosense over ESP-NOW when heard, wired PMS otherwise");
}

void loop(){
  gNowMs = millis();

  // Drain BLE command queue — runs on Core 1 (Arduino's loop task) so
  // command-driven state changes (gMode, gBrightness, locator*) land
  // between frames, not mid-dispatch.
  if (cmdQueue != nullptr) {
    CmdMsg msg;
    while (xQueueReceive(cmdQueue, &msg, 0) == pdTRUE) {
      handleCommand(std::string(msg.data));
    }
  }

  // AUTO sensor mode: re-paint the onboard indicator when the live source
  // flips (Nephosense appears → purple, goes quiet for 5 s → back to orange).
  {
    static uint8_t lastSrc = 255;
    uint8_t src = activePmSource();
    if (src != lastSrc) {
      lastSrc = src;
      refreshSensorIndicator();
    }
  }

  // Tesla edge-rate meter — live regardless of mode, ~2 Hz refresh. Also
  // drives TESLA CAL: track the peak rate over the 10 s window, then set
  // full-scale to ~80% of peak so a well-sung coil actually reaches 100%.
  {
    static uint32_t teslaWinMs    = 0;
    static uint32_t teslaWinEdges = 0;
    if (teslaWinMs == 0) { teslaWinMs = gNowMs; teslaWinEdges = gSparkEdges + gSparkRemoteEdges; }
    if (gNowMs - teslaWinMs >= 500) {
      uint32_t te = gSparkEdges + gSparkRemoteEdges;
      uint32_t r  = (te - teslaWinEdges) * 2;
      gTeslaHz      = (r > 65535) ? 65535 : (uint16_t)r;
      teslaWinEdges = te;
      teslaWinMs    = gNowMs;
      if (gTeslaCalUntilMs) {
        if (gTeslaHz > gTeslaCalPeak) gTeslaCalPeak = gTeslaHz;
        if ((int32_t)(gNowMs - gTeslaCalUntilMs) >= 0) {
          if (gTeslaCalPeak >= 10) {
            uint32_t fs = (uint32_t)gTeslaCalPeak * 8 / 10;
            if (fs < 20)   fs = 20;
            if (fs > 5000) fs = 5000;
            gTeslaFullHz = (uint16_t)fs;
            respondf("OK Cal: peak %u edge/s -> full-scale %u", gTeslaCalPeak, gTeslaFullHz);
          } else {
            respondf("X Cal: only %u edge/s seen - check antenna / TESLA PIN", gTeslaCalPeak);
          }
          gTeslaCalUntilMs = 0;
        }
      }
    }
  }

  // Safely read PM2.5 value from Core 0 task; keep last good value on timeout.
  static float lastPmValue = 0.0f;
  float pmValue = lastPmValue;
  if(xSemaphoreTake(pmMutex, pdMS_TO_TICKS(1)) == pdTRUE) {
    pmValue = pm25_ema;
    xSemaphoreGive(pmMutex);
    lastPmValue = pmValue;
  }

  if(!systemPowerOn){
    fill_all(CRGB::Black);
    FastLED.show();
    delay(gFrameDelayMs);
    return;
  }

  // ============================================================
  //  MODE MAP  -  ascending number = brighter / more active
  //  Tags: [dim] calm & low-light   [mid] moving light   [bright] flashy
  //  Pinned by you: 0 Arc, 1 Comet, 2 ChronoRift, 3 Chiaroscuro, 4 Starfield
  // ------------------------------------------------------------
  //   0  Arc                  [dim]      14  BlackHole           [dim]
  //   1  Comet                [mid]      15  People              [dim]
  //   2  ChronoRift           [mid]      16  CellularInfection   [mid]
  //   3  ChiaroscuroPulse     [mid]      17  Ultrasonic          [mid]  (40kHz phased array; PM = drive)
  //   4  Starfield            [mid]      18  FrequencyScope      [mid]  (+SQUARE/TRIANGLE/SAW/PULSE/NOISE)
  //   5  Anechoic             [dim]      19  QuantumEntanglement [mid]
  //   6  Subduction           [dim]      20  Pulsar              [bright]
  //   7  Clouds               [bright]      21  QuantumCascade      [dim]
  //   8  DarkNebula           [mid]      22  QuantumLattice      [dim]
  //   9  Stigmergy            [dim]      23  Electromagnetism    [mid]
  //  10  NocturneMirage       [dim]      24  SynapticCascade     [bright]
  //  11  Gaze                 [dim]      25  CrystalCascade      [bright]
  //  12  MycelialNetwork      [mid]      26  NikolaTesla         [bright]
  //  13  ConvectionCurrents   [mid]      27  Morphogenesis (sim) [reaction-diffusion]
  //                                      28  Lichtenberg   (sim) [dielectric breakdown]
  //                                      29  Reverie   (Opus 4.8's self-portrait) [dim->peak]
  //                                      30  Ephemeris (Opus 4.8's feeling)       [memory & forgetting]
  //                                      31  Teslacle (Fable 5 / Opus 4.8)        [played by a real Tesla coil; PM is the medium]
  //                                      32  Locator (special)
  // ============================================================
  switch(gMode){
    // ---- pinned ----
    case  0: effectArc(pmValue);                break;  // [dim]
    case  1: effectComet(pmValue);              break;  // [mid]
    case  2: effectChronoRift(pmValue);         break;  // [mid]
    case  3: effectChiaroscuroPulse(pmValue);   break;  // [mid]
    case  4: effectStarfield(pmValue);          break;  // [dim]
    // ---- dim / calm ----
    case  5: effectAnechoic(pmValue);           break;  // [dim]
    case  6: effectSubduction(pmValue);         break;  // [dim]
    case  7: effectClouds(pmValue);             break;  // [bright]
    case  8: effectDarkNebula(pmValue);         break;  // [mid]
    case  9: effectStigmergy(pmValue);          break;  // [dim]
    case 10: effectNocturneMirage(pmValue);     break;  // [dim]
    case 11: effectGaze(pmValue);               break;  // [dim]
    // ---- mid / moving ----
    case 12: effectMycelialNetwork(pmValue);    break;  // [mid]
    case 13: effectConvectionCurrents(pmValue); break;  // [mid]
    case 14: effectBlackHole(pmValue);          break;  // [dim]
    case 15: effectPeople(pmValue);             break;  // [dim]
    case 16: effectCellularInfection(pmValue);  break;  // [mid]
    case 17: effectUltrasonic(pmValue);         break;  // [mid] 40kHz phased standing-wave field
    case 18: effectFrequencyScope(pmValue);     break;  // [mid]
    case 19: effectQuantumEntanglement(pmValue);break;  // [mid]
    case 20: effectPulsar(pmValue);             break;  // [bright]
    case 21: effectQuantumCascade(pmValue);     break;  // [dim]
    // ---- bright / intense ----
    case 22: effectQuantumLattice(pmValue);     break;  // [dim]
    case 23: effectElectromagnetism(pmValue);   break;  // [bright]
    case 24: effectSynapticCascade(pmValue);    break;  // [bright]
    case 25: effectCrystalCascade(pmValue);     break;  // [bright]
    case 26: effectNikolaTesla(pmValue);        break;  // [bright]
    case 27: effectMorphogenesis(pmValue);      break;  // [sim] reaction-diffusion
    case 28: effectLichtenberg(pmValue);        break;  // [sim] dielectric breakdown
    case 29: effectReverie(pmValue);            break;  // [dim->peak] Opus 4.8's self-portrait
    case 30: effectEphemeris(pmValue);          break;  // [feeling] memory & forgetting
    case 31: effectWardenclyffe(pmValue);   break;  // [grand] the tower, played by a real coil
    case MODE_LOCATOR: effectLocator();         break;
    default: effectComet(pmValue);              break;
  }

  FastLED.show();
  delay(gFrameDelayMs);
}
