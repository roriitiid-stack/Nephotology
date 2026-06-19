// Teslasense.ino — companion Tesla-coil spark sensor for Nephotology.
//
// Target: any ESP32 (classic / S2 / S3 / C3). No external libraries —
// uses only the ESP-NOW + WiFi stack that ships with the ESP32 core.
//
// PURPOSE
//   Parks next to the Tesla coil so the Nephotology rig doesn't have to.
//   A wire antenna on SPARK_PIN picks up the coil's EMI; every spark burst
//   induces edges, an ISR counts them, and the count is broadcast over
//   ESP-NOW ~20x per second. The rig merges these into Mode 31
//   (Wardenclyffe) exactly as if the antenna were wired locally —
//   no BLE command needed on the rig, packets are always accepted.
//
// PROTOCOL
//   Packet format MUST match the receiver in Nephotomentals.ino:
//     #define SPARK_MAGIC 0x54
//     typedef struct { uint8_t magic; uint8_t seq; uint16_t edges; }
//       __attribute__((packed)) SparkPacket;            // 4 bytes
//   4 bytes + magic keeps it unmistakable next to the 2-byte PmPacket.
//   Sent to broadcast MAC (FF:FF:FF:FF:FF:FF) on channel 1, no pairing.
//
// WIRING
//   Antenna   → SPARK_PIN (GPIO 4 by default): 5–20 cm of stiff wire,
//               nothing else. Closer coil = shorter wire. If it floods
//               (rate pegged even when idle), shorten or move the wire.
//   That's the whole circuit. A detector module's digital output works
//   on the same pin too (it idles low, pulses high).
//
// STATUS LED
//   Blinks on every packet that carries at least one edge, so you can
//   see sparks being heard from across the room.
//
// TUNING
//   The ISR ignores edges closer than 100 µs apart — that counts spark
//   EVENTS, not the RF carrier's MHz ringing, and caps interrupt load.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "driver/gpio.h"

#ifndef SPARK_PIN
  #define SPARK_PIN 4
#endif

#ifndef SEND_PERIOD_MS
  #define SEND_PERIOD_MS 50      // 20 Hz — keeps strike timing tight on the rig
#endif

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
    #define STATUS_LED_PIN 2
  #endif
#endif

// Packed packet format — MUST match Nephotomentals.ino exactly.
#define SPARK_MAGIC 0x54   // 'T'
typedef struct { uint8_t magic; uint8_t seq; uint16_t edges; } __attribute__((packed)) SparkPacket;
typedef struct { uint16_t pm25; } __attribute__((packed)) PmPacket;

// Optional PMS5003 rides along on this node. Always listening — plug the
// sensor in and valid frames start relaying (~1/s, its natural rate);
// unplug it and the node just goes quiet on the PM side. The header +
// checksum verification means a floating RX pin can never fake a reading.
#ifndef PMS_RX_PIN
  #define PMS_RX_PIN 16
#endif
HardwareSerial Pms(2);

static const uint8_t kBroadcastMac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

volatile uint32_t gEdges  = 0;   // ISR-counted antenna edges (monotonic)
static uint32_t lastSent  = 0;   // edge total at the previous packet
static uint32_t lastSendMs = 0;
static uint32_t ledOffAtMs = 0;
static bool     ledIsOn    = false;
static bool     peerAdded  = false;
static uint8_t  seq        = 0;
static uint32_t packetsSent = 0;

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

static inline void setStatusLed(uint8_t r, uint8_t g, uint8_t b) {
#ifdef STATUS_LED_IS_RGB
  rgbLedWrite(STATUS_LED_PIN, r, g, b);
#else
  digitalWrite(STATUS_LED_PIN, (r | g | b) ? HIGH : LOW);
#endif
}

static bool ensurePeer() {
  if (peerAdded) return true;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, kBroadcastMac, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[ESP-NOW] add_peer failed");
    return false;
  }
  peerAdded = true;
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("==================================");
  Serial.println("  Teslasense  (sparks -> ESP-NOW)");
  Serial.println("==================================");

  pinMode(STATUS_LED_PIN, OUTPUT);
  setStatusLed(0, 0, 0);

  gpio_install_isr_service(0);
  gpio_config_t cfg = {};
  cfg.pin_bit_mask  = 1ULL << SPARK_PIN;
  cfg.mode          = GPIO_MODE_INPUT;
  cfg.pull_up_en    = GPIO_PULLUP_DISABLE;
  cfg.pull_down_en  = GPIO_PULLDOWN_ENABLE;
  cfg.intr_type     = GPIO_INTR_POSEDGE;
  gpio_config(&cfg);
  gpio_isr_handler_add((gpio_num_t)SPARK_PIN, sparkIsr, NULL);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: esp_now_init failed — halting");
    while (1) { delay(1000); }
  }

  Pms.begin(9600, SERIAL_8N1, PMS_RX_PIN, -1);

  Serial.print("[ESP-NOW] MAC:     ");
  Serial.println(WiFi.macAddress());
  Serial.print("[SPARK]   Antenna: GPIO ");
  Serial.println(SPARK_PIN);
  Serial.print("[PMS]     RX:      GPIO ");
  Serial.println(PMS_RX_PIN);
  Serial.println("Broadcasting on FF:FF:FF:FF:FF:FF — any Nephotology rig in");
  Serial.println("range feeds these sparks straight into Mode 31 (Wardenclyffe).");
  Serial.println();
}

// PMS5003 frame parser -> PM2.5 atmospheric (same frame layout the rig uses).
static bool readPms(uint16_t &pm25) {
  static uint8_t b[32];
  while (Pms.available() >= 32) {
    if (Pms.peek() != 0x42) { Pms.read(); continue; }
    Pms.readBytes(b, 32);
    if (b[1] != 0x4D) continue;
    uint16_t sum = 0; for (int i = 0; i < 30; i++) sum += b[i];
    if (sum != (uint16_t)((b[30] << 8) | b[31])) continue;
    pm25 = (b[12] << 8) | b[13];
    return true;
  }
  return false;
}

void loop() {
  uint32_t now = millis();

  if (ledIsOn && (int32_t)(now - ledOffAtMs) >= 0) {
    setStatusLed(0, 0, 0);
    ledIsOn = false;
  }

  if (now - lastSendMs >= SEND_PERIOD_MS) {
    lastSendMs = now;
    uint32_t total = gEdges;
    uint32_t delta = total - lastSent;
    lastSent = total;
    if (delta > 0 && ensurePeer()) {
      SparkPacket pkt;
      pkt.magic = SPARK_MAGIC;
      pkt.seq   = seq++;
      pkt.edges = (delta > 65535) ? 65535 : (uint16_t)delta;
      if (esp_now_send(kBroadcastMac, (const uint8_t*)&pkt, sizeof(pkt)) == ESP_OK) {
        packetsSent++;
        setStatusLed(0, 24, 40);     // electric-blue blink: spark heard, spark sent
        ledIsOn = true;
        ledOffAtMs = now + 25;
      }
    }
  }

  // PM2.5 relay — always listening; only transmits while a PMS is talking
  uint16_t pm;
  if (readPms(pm) && ensurePeer()) {
    PmPacket pp = { pm };
    esp_now_send(kBroadcastMac, (const uint8_t*)&pp, sizeof(pp));
    Serial.printf("[PMS]     pm2.5=%u\n", pm);
  }

  // once-a-second heartbeat log so the serial monitor shows life
  static uint32_t lastLogMs = 0;
  if (now - lastLogMs >= 1000) {
    lastLogMs = now;
    Serial.printf("[%6lus] edges=%lu  sent=%lu pkts\n",
                  (unsigned long)(now / 1000),
                  (unsigned long)gEdges,
                  (unsigned long)packetsSent);
  }

  delay(5);
}
