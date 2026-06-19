// Nephosense.ino — companion PM2.5 sensor for Nephotology.
//
// Target: any ESP32 (classic / S2 / S3 / C3) with a PMS5003 wired up.
// Required Boards Manager: "esp32 by Espressif Systems" v3.x or newer.
// No external libraries — uses only the ESP-NOW + WiFi stack that ships
// with the ESP32 Arduino core.
//
// PURPOSE
//   Reads a Plantower PMS5003 over UART and broadcasts the PM2.5 reading
//   via ESP-NOW once per second. The main Nephotology rig, when switched
//   to wireless mode via the BLE command "SENSOR ESPNOW", picks up these
//   packets and drives its LED effects from the live data — even though
//   no PMS is wired to the rig itself.
//
//   The user can hot-switch the rig between local (wired) and remote
//   (this sketch) over BLE at runtime; both stacks always run on the
//   receiver side, only the gate flips.
//
// PROTOCOL
//   Packet format MUST match the receiver in Nephotomentals.ino:
//     typedef struct { uint16_t pm25; } __attribute__((packed)) PmPacket;
//   Sent to broadcast MAC (FF:FF:FF:FF:FF:FF), no pairing needed.
//
// WIRING (override any #define on the build line if you wire differently)
//   PMS5003 TX     → ESP32 GPIO 36   (UART2 RX)
//   PMS5003 VCC    → 5V
//   PMS5003 GND    → GND
//   PMS5003 SET    → 3.3V or leave floating (sensor always on)
//   PMS5003 RESET  → 3.3V or leave floating
//
// STATUS LED
//   Built-in LED blinks ~30 ms on every successful packet transmission,
//   so you can see at a glance that the node is alive and broadcasting.
//
// CHANNEL
//   ESP-NOW peers must share a WiFi channel. Neither this sketch nor the
//   Nephotology receiver connects to an AP, so both stay on channel 1 by
//   default and find each other automatically.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#ifndef PMS_RX_PIN
  #define PMS_RX_PIN 36       // ESP32 UART2 RX ← PMS5003 TX
#endif
#ifndef PMS_TX_PIN
  #define PMS_TX_PIN -1       // not used — we never send to the sensor
#endif
#ifndef PMS_BAUD
  #define PMS_BAUD 9600
#endif

#ifndef BROADCAST_PERIOD_MS
  #define BROADCAST_PERIOD_MS 1000   // 1 Hz — matches PMS frame rate
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

#ifndef ESPNOW_CHANNEL
  #define ESPNOW_CHANNEL 1     // must match receiver (default WiFi STA channel)
#endif

HardwareSerial& PmsSerial = Serial2;

// Packed packet format — MUST match Nephotomentals.ino exactly.
typedef struct { uint16_t pm25; } __attribute__((packed)) PmPacket;

static const uint8_t kBroadcastMac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static bool     peerAdded     = false;
static uint16_t lastPm25      = 0;
static uint32_t framesParsed  = 0;
static uint32_t packetsSent   = 0;
static uint32_t packetsFailed = 0;
static uint32_t lastSendMs    = 0;
static uint32_t ledOffAtMs    = 0;
static bool     ledIsOn       = false;

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

// -----------------------------
// PMS5003 frame parser — Plantower's 32-byte protocol with 0x42 0x4D header
// and a 16-bit checksum at the end. Identical to the receiver-side parser
// so behaviour is symmetrical.
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

static void broadcastReading(uint16_t pm25) {
  if (!ensurePeer()) return;
  PmPacket pkt;
  pkt.pm25 = pm25;
  esp_err_t r = esp_now_send(kBroadcastMac, (const uint8_t*)&pkt, sizeof(pkt));
  if (r == ESP_OK) {
    packetsSent++;
    setStatusLed(40, 40, 40);       // dim white pulse on RGB boards; plain LEDs just go on
    ledIsOn = true;
    ledOffAtMs = millis() + 30;     // 30 ms blink
  } else {
    Serial.print("[ESP-NOW] send error: ");
    Serial.println(r);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("================================");
  Serial.println("  Nephosense  (PM2.5 → ESP-NOW)");
  Serial.println("================================");

  pinMode(STATUS_LED_PIN, OUTPUT);
  setStatusLed(0, 0, 0);

  PmsSerial.begin(PMS_BAUD, SERIAL_8N1, PMS_RX_PIN, PMS_TX_PIN);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  // Lock to a known channel so the receiver (also on this channel) hears us.
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: esp_now_init failed — halting");
    while (1) { delay(1000); }
  }
  esp_now_register_send_cb(onEspNowSend);

  Serial.print("[ESP-NOW] MAC:     ");
  Serial.println(WiFi.macAddress());
  Serial.print("[ESP-NOW] Channel: ");
  Serial.println(ESPNOW_CHANNEL);
  Serial.print("[ESP-NOW] Period:  ");
  Serial.print(BROADCAST_PERIOD_MS);
  Serial.println(" ms");
  Serial.print("[PMS]     RX pin:  ");
  Serial.println(PMS_RX_PIN);
  Serial.println("Broadcasting on FF:FF:FF:FF:FF:FF — any Nephotology rig in");
  Serial.println("range, switched to SENSOR ESPNOW, will receive these packets.");
  Serial.println();
}

void loop() {
  // Drain any available PMS frames; keep the latest reading.
  uint8_t frame[32];
  while (PmsSerial.available() >= 32) {
    if (!readPMSFrame(frame)) break;
    framesParsed++;
    // Byte 12..13 = PM2.5 atmospheric concentration (µg/m³).
    lastPm25 = ((uint16_t)frame[12] << 8) | frame[13];
  }

  uint32_t now = millis();

  // Status LED off-timer (non-blocking blink)
  if (ledIsOn && (int32_t)(now - ledOffAtMs) >= 0) {
    setStatusLed(0, 0, 0);
    ledIsOn = false;
  }

  // Broadcast on the configured cadence. We always send, even before the
  // first PMS frame arrives — the receiver just sees pm25=0 briefly.
  if (now - lastSendMs >= BROADCAST_PERIOD_MS) {
    lastSendMs = now;
    broadcastReading(lastPm25);
    Serial.printf("[%6lus] PM2.5=%4u µg/m³   frames=%lu  sent=%lu  failed=%lu\n",
                  (unsigned long)(now / 1000),
                  (unsigned)lastPm25,
                  (unsigned long)framesParsed,
                  (unsigned long)packetsSent,
                  (unsigned long)packetsFailed);
  }

  delay(10);
}
