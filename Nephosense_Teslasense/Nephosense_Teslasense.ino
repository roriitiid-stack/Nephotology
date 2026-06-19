// Nephosense_Teslasense.ino — combined companion sensor for Nephotology.
//
// Target: any ESP32 (classic / S2 / S3 / C3). No external libraries —
// uses only the ESP-NOW + WiFi stack that ships with the ESP32 core.
//
// PURPOSE
//   One node that does BOTH companion jobs at once:
//     * Teslasense — a wire antenna on SPARK_PIN picks up a Tesla coil's
//       EMI; an ISR counts spark edges and the count is broadcast ~20x/s.
//       The rig feeds these straight into Mode 31 (Wardenclyffe), always
//       accepted, no BLE command needed.
//     * Nephosense — a Plantower PMS5003 on PMS_RX_PIN is read and its
//       PM2.5 value is broadcast ~1x/s. The rig uses these when switched
//       to wireless mode via the BLE command "SENSOR ESPNOW".
//   Both run together; wire up whichever sensor(s) you have. A missing
//   PMS just stays quiet (see PM gate below) and a missing antenna just
//   reads no sparks — neither blocks the other.
//
// PROTOCOL — both packet formats MUST match the receiver in Nephotomentals.ino:
//     #define SPARK_MAGIC 0x54
//     typedef struct { uint8_t magic; uint8_t seq; uint16_t edges; }
//       __attribute__((packed)) SparkPacket;            // 4 bytes
//     typedef struct { uint16_t pm25; } __attribute__((packed)) PmPacket; // 2 bytes
//   The magic byte + the size difference keep the two unmistakable.
//   Sent to broadcast MAC (FF:FF:FF:FF:FF:FF) on channel 1, no pairing.
//
// WIRING (override any #define on the build line if you wire differently)
//   Antenna        → SPARK_PIN  (GPIO 4):  5–20 cm of stiff wire, nothing
//                    else. Closer coil = shorter wire. A spark-detector
//                    module's digital output (idles low, pulses high) works
//                    on the same pin too.
//   PMS5003 TX     → PMS_RX_PIN (GPIO 16, UART2 RX — WROOM silk: "RX2")
//   PMS5003 VCC    → 5V          PMS5003 GND → GND
//   PMS5003 SET/RESET → 3.3V or leave floating (sensor always on)
//
// STATUS LED
//   Electric-blue blink  = a spark packet was sent (sparks heard).
//   Dim-white blink      = a PM2.5 packet was sent.
//   So you can tell the two activities apart from across the room.
//
// PM GATE
//   Unlike the standalone Nephosense (a dedicated PM node that always
//   broadcasts), this combined node holds off on PM packets until it has
//   parsed at least one valid PMS frame. That way, running it with no PMS
//   attached won't spam pm25=0 and stomp the rig's reading to zero.
//
// CHANNEL
//   ESP-NOW peers must share a WiFi channel. Neither this sketch nor the
//   rig connects to an AP, so both stay on channel 1 and find each other.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "driver/gpio.h"

// ---- Spark (Teslasense) ----
#ifndef SPARK_PIN
  #define SPARK_PIN 4
#endif
#ifndef SPARK_PERIOD_MS
  #define SPARK_PERIOD_MS 50     // 20 Hz — keeps strike timing tight on the rig
#endif

// ---- PMS5003 (Nephosense) ----
#ifndef PMS_RX_PIN
  #define PMS_RX_PIN 16          // ESP32 UART2 RX ← PMS5003 TX (WROOM silk: "RX2")
#endif
#ifndef PMS_TX_PIN
  #define PMS_TX_PIN -1          // not used — we never send to the sensor
#endif
#ifndef PMS_BAUD
  #define PMS_BAUD 9600
#endif
#ifndef PM_PERIOD_MS
  #define PM_PERIOD_MS 1000      // 1 Hz — matches PMS frame rate
#endif

// ---- Shared ----
#ifndef ESPNOW_CHANNEL
  #define ESPNOW_CHANNEL 1       // must match receiver (default WiFi STA channel)
#endif

#ifndef STATUS_LED_PIN
  #ifdef RGB_BUILTIN
    #define STATUS_LED_PIN RGB_BUILTIN
    #define STATUS_LED_IS_RGB 1
  #elif defined(LED_BUILTIN)
    #define STATUS_LED_PIN LED_BUILTIN
  #else
    #define STATUS_LED_PIN 2     // GPIO 2 = built-in LED on most ESP32 dev kits
  #endif
#endif

HardwareSerial& PmsSerial = Serial2;

// Packed packet formats — MUST match Nephotomentals.ino exactly.
#define SPARK_MAGIC 0x54   // 'T'
typedef struct { uint8_t magic; uint8_t seq; uint16_t edges; } __attribute__((packed)) SparkPacket;
typedef struct { uint16_t pm25; } __attribute__((packed)) PmPacket;

static const uint8_t kBroadcastMac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// Spark state
volatile uint32_t gEdges      = 0;   // ISR-counted antenna edges (monotonic)
static uint32_t   lastEdgeTot = 0;   // edge total at the previous spark packet
static uint32_t   lastSparkMs = 0;
static uint8_t    sparkSeq    = 0;
static uint32_t   sparkSent   = 0;

// PM state
static uint16_t lastPm25      = 0;
static bool     pmEverValid   = false;   // gate: don't broadcast until a real frame seen
static uint32_t framesParsed  = 0;
static uint32_t lastPmMs      = 0;
static uint32_t pmSent        = 0;

// Shared state
static bool     peerAdded     = false;
static uint32_t packetsFailed = 0;
static uint32_t ledOffAtMs    = 0;
static bool     ledIsOn       = false;

// Raw IDF GPIO ISR rather than Arduino attachInterrupt() — the Arduino
// wrapper installs its ISR service via an IPC call that can overflow the
// tiny ipc task stack once the radio is up (seen as a boot-loop on the rig).
void IRAM_ATTR sparkIsr(void* /*arg*/) {
  static uint32_t lastUs = 0;
  uint32_t nowUs = (uint32_t)micros();
  if ((uint32_t)(nowUs - lastUs) < 100) return;   // skip carrier ringing
  lastUs = nowUs;
  gEdges = gEdges + 1;
}

// Drives the status LED uniformly whether the board has an RGB NeoPixel
// (rgbLedWrite) or a single-color LED (digitalWrite). For the plain case
// any non-zero channel turns the LED on.
static inline void setStatusLed(uint8_t r, uint8_t g, uint8_t b) {
#ifdef STATUS_LED_IS_RGB
  rgbLedWrite(STATUS_LED_PIN, r, g, b);
#else
  digitalWrite(STATUS_LED_PIN, (r | g | b) ? HIGH : LOW);
#endif
}

static inline void blinkLed(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) {
  setStatusLed(r, g, b);
  ledIsOn    = true;
  ledOffAtMs = millis() + ms;
}

// ESP32 Arduino core 3.1.0+ changed the send-callback's first arg from
// `const uint8_t* mac` to `const wifi_tx_info_t*`. Guard so we compile on both.
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 1, 0)
static void onEspNowSend(const wifi_tx_info_t* /*info*/, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) packetsFailed++;
}
#else
static void onEspNowSend(const uint8_t* /*mac*/, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) packetsFailed++;
}
#endif

static bool ensurePeer() {
  if (peerAdded) return true;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, kBroadcastMac, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = false;
  esp_err_t r = esp_now_add_peer(&peer);
  if (r != ESP_OK) {
    Serial.print("[ESP-NOW] add_peer failed: ");
    Serial.println(r);
    return false;
  }
  peerAdded = true;
  return true;
}

// -----------------------------
// PMS5003 frame parser — Plantower's 32-byte protocol with 0x42 0x4D header
// and a 16-bit checksum at the end. Byte-by-byte header sync + frame-length
// check (0x001C) so a floating RX pin can never fake a reading.
// -----------------------------
static bool readPMSFrame(uint8_t buf[32]) {
  while (PmsSerial.available()) {
    if (PmsSerial.peek() != 0x42) { PmsSerial.read(); continue; }
    if (PmsSerial.read() != 0x42) continue;
    uint8_t next = 0;
    if (PmsSerial.readBytes(&next, 1) != 1) return false;
    if (next != 0x4D) continue;
    buf[0] = 0x42;
    buf[1] = 0x4D;
    if (PmsSerial.readBytes(buf + 2, 30) != 30) return false;
    if (!(buf[2] == 0x00 && buf[3] == 0x1C)) continue;
    uint16_t sum = 0;
    for (int i = 0; i < 30; i++) sum += buf[i];
    uint16_t csum = ((uint16_t)buf[30] << 8) | buf[31];
    if (sum != csum) continue;
    return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=========================================");
  Serial.println("  Nephosense + Teslasense  (combined node)");
  Serial.println("=========================================");

  pinMode(STATUS_LED_PIN, OUTPUT);
  setStatusLed(0, 0, 0);

  // ---- Spark antenna ISR (raw IDF GPIO) ----
  gpio_install_isr_service(0);
  gpio_config_t cfg = {};
  cfg.pin_bit_mask  = 1ULL << SPARK_PIN;
  cfg.mode          = GPIO_MODE_INPUT;
  cfg.pull_up_en    = GPIO_PULLUP_DISABLE;
  cfg.pull_down_en  = GPIO_PULLDOWN_ENABLE;
  cfg.intr_type     = GPIO_INTR_POSEDGE;
  gpio_config(&cfg);
  gpio_isr_handler_add((gpio_num_t)SPARK_PIN, sparkIsr, NULL);

  // ---- PMS5003 UART ----
  PmsSerial.begin(PMS_BAUD, SERIAL_8N1, PMS_RX_PIN, PMS_TX_PIN);

  // ---- WiFi / ESP-NOW ----
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: esp_now_init failed — halting");
    while (1) { delay(1000); }
  }
  esp_now_register_send_cb(onEspNowSend);

  Serial.print("[ESP-NOW] MAC:     "); Serial.println(WiFi.macAddress());
  Serial.print("[ESP-NOW] Channel: "); Serial.println(ESPNOW_CHANNEL);
  Serial.print("[SPARK]   Antenna: GPIO "); Serial.println(SPARK_PIN);
  Serial.print("[PMS]     RX:      GPIO "); Serial.println(PMS_RX_PIN);
  Serial.println("Broadcasting on FF:FF:FF:FF:FF:FF — sparks feed Mode 31, PM2.5");
  Serial.println("feeds the rig when it is switched to SENSOR ESPNOW.");
  Serial.println();
}

void loop() {
  uint32_t now = millis();

  // Non-blocking LED off-timer (shared by both blink sources)
  if (ledIsOn && (int32_t)(now - ledOffAtMs) >= 0) {
    setStatusLed(0, 0, 0);
    ledIsOn = false;
  }

  // ---- PMS: drain frames, keep latest reading ----
  uint8_t frame[32];
  while (PmsSerial.available() >= 32) {
    if (!readPMSFrame(frame)) break;
    framesParsed++;
    pmEverValid = true;
    // Byte 12..13 = PM2.5 atmospheric concentration (µg/m³).
    lastPm25 = ((uint16_t)frame[12] << 8) | frame[13];
  }

  // ---- Spark: broadcast edge delta at 20 Hz when sparks were heard ----
  if (now - lastSparkMs >= SPARK_PERIOD_MS) {
    lastSparkMs = now;
    uint32_t total = gEdges;
    uint32_t delta = total - lastEdgeTot;
    lastEdgeTot = total;
    if (delta > 0 && ensurePeer()) {
      SparkPacket pkt;
      pkt.magic = SPARK_MAGIC;
      pkt.seq   = sparkSeq++;
      pkt.edges = (delta > 65535) ? 65535 : (uint16_t)delta;
      if (esp_now_send(kBroadcastMac, (const uint8_t*)&pkt, sizeof(pkt)) == ESP_OK) {
        sparkSent++;
        blinkLed(0, 24, 40, 25);     // electric-blue: spark heard, spark sent
      }
    }
  }

  // ---- PM: broadcast latest reading at 1 Hz, but only once a frame is seen ----
  if (pmEverValid && now - lastPmMs >= PM_PERIOD_MS) {
    lastPmMs = now;
    if (ensurePeer()) {
      PmPacket pkt = { lastPm25 };
      if (esp_now_send(kBroadcastMac, (const uint8_t*)&pkt, sizeof(pkt)) == ESP_OK) {
        pmSent++;
        blinkLed(40, 40, 40, 30);    // dim white: PM2.5 sent
      }
    }
  }

  // ---- Once-a-second heartbeat log ----
  static uint32_t lastLogMs = 0;
  if (now - lastLogMs >= 1000) {
    lastLogMs = now;
    Serial.printf("[%6lus] edges=%lu sparkPkts=%lu | PM2.5=%4u%s pmPkts=%lu | failed=%lu\n",
                  (unsigned long)(now / 1000),
                  (unsigned long)gEdges,
                  (unsigned long)sparkSent,
                  (unsigned)lastPm25,
                  pmEverValid ? "" : "(no PMS)",
                  (unsigned long)pmSent,
                  (unsigned long)packetsFailed);
  }

  delay(5);
}
