
#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include <math.h>

// nowMs(), DISC_RING_*, discIdxToRing/Angle/XY, BLACKHOLE_* constants are
// defined in Nephotology.cpp BEFORE this header is included.

static void effectArc(float pm) {
  // ---- PM ratio, smoothed to reduce jitter ----
  float r = pmRatio(pm);
  r = fclamp(r, 0.0f, 1.0f);

  static float rSmooth = 0.0f;
  const float aUp = 0.10f;   // rise speed
  const float aDn = 0.03f;   // fall speed
  const float a   = (r > rSmooth) ? aUp : aDn;
  rSmooth += (r - rSmooth) * a;

  const uint8_t hue = hueForPM(pm);

  // quadratic makes "almost off" at low PM
  const float vis = rSmooth * rSmooth;
  const uint8_t maxBri = (uint8_t)(220.0f * vis);

  // true gauge: no background glow
  fill_all(CRGB::Black);

  // =========================
  // MATRIX: bottom-up row gauge
  // Each row ramps smoothly to full, then next row begins
  // =========================
  {
    const uint8_t W = MATRIX_WIDTH;
    const uint8_t H = MATRIX_HEIGHT;
    const float fillRows = rSmooth * (float)H; // 0..H

    for (uint8_t y = 0; y < H; y++) {
      const int rowFromBottom = (int)(H - 1 - y);           // bottom=0
      float rowLevel = fillRows - (float)rowFromBottom;     // 0..1..2...
      rowLevel = fclamp(rowLevel, 0.0f, 1.0f);

      const uint8_t bri = (uint8_t)((float)maxBri * rowLevel);
      if (bri == 0) continue;

      for (uint8_t x = 0; x < W; x++) {
        ledsMatrix[XY(x, y)] = CHSV(hue, 255, bri);
      }
    }
  }

  // =========================
  // STRIP: fill from both ends toward center
  // =========================
  {
    if (LED_COUNT_STRIP > 0) {
      const uint16_t last = LED_COUNT_STRIP - 1;
      const float halfLen = (float)last * 0.5f;
      const float fillLen = rSmooth * halfLen;

      for (uint16_t i = 0; i < LED_COUNT_STRIP; i++) {
        const uint16_t d = (i < (last - i)) ? i : (last - i);  // distance to nearest end
        float level = fillLen - (float)d;
        level = fclamp(level, 0.0f, 1.0f);

        const uint8_t bri = (uint8_t)((float)maxBri * level);
        if (bri) ledsStrip[i] = CHSV(hue, 255, bri);
      }
    }
  }

  // =========================
  // =========================
  {
  }

  // =========================
  // DISC: ring-by-ring radial gauge, center outward
  // =========================
  {
    const float fillRings = rSmooth * (float)DISC_RING_COUNT;
    for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
      uint8_t ringIdx, posIdx;
      discIdxToRing(i, ringIdx, posIdx);
      float level = fillRings - (float)ringIdx;
      level = fclamp(level, 0.0f, 1.0f);
      const uint8_t bri = (uint8_t)((float)maxBri * level);
      if (bri) ledsDisc[i] = CHSV(hue, 255, bri);
    }
  }

  // =========================
  // CLOUD: ring-loop gauge — fills outward from a "top" anchor (idx 0) sweeping
  // both ways around the loop. Reads naturally on either a rope or an infinity
  // sign because the two halves grow symmetrically.
  // =========================
  {
    const uint16_t half = LED_COUNT_CLOUD / 2;
    const float fillLen = rSmooth * (float)half;
    for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
      uint16_t d = (i <= half) ? i : (LED_COUNT_CLOUD - i);
      float level = fillLen - (float)d;
      level = fclamp(level, 0.0f, 1.0f);
      const uint8_t bri = (uint8_t)((float)maxBri * level);
      if (bri) ledsCloud[i] = CHSV(hue, 255, bri);
    }
  }
}

void testMatrixWalk(){
  const uint32_t now = nowMs();
  static uint32_t lastStepMs = 0;
  static uint16_t step = 0;

  if (now - lastStepMs >= 80) {
    lastStepMs = now;
    step++;
    if (step >= LED_COUNT_MATRIX) step = 0;
  }

  fill_all(CRGB::Black);

  uint8_t y = step / MATRIX_WIDTH;
  uint8_t col = step % MATRIX_WIDTH;
  uint8_t x = ((y & 1) == 0) ? (MATRIX_WIDTH - 1 - col) : col;
  ledsMatrix[XY(x, y)] = CHSV(0, 0, 200);
}

void effectComet(float pm) {
  
  static uint32_t frame    = 0;
  static uint16_t cometPos = 0;

  frame++;

  
  float r = pmRatio(pm);  
  if (r < 0.0f) r = 0.0f;
  if (r > 1.0f) r = 1.0f;

  
  fadeToBlackBy(ledsMatrix,   LED_COUNT_MATRIX,   20);
  fadeToBlackBy(ledsStrip,   LED_COUNT_STRIP,   20);
  fadeToBlackBy(ledsDisc,   LED_COUNT_DISC,   20);
  fadeToBlackBy(ledsCloud,   LED_COUNT_CLOUD,   20);


  uint8_t starsPerSegment = 1 + (uint8_t)(r * 2.0f);

  auto addStars = [&](CRGB *leds, uint16_t count) {
    if (!count) return;
    for (uint8_t s = 0; s < starsPerSegment; s++) {
      uint16_t idx = random16(count);


      uint8_t choice  = random8(3);
      uint8_t baseHue;
      switch (choice) {
        default:
        case 0: baseHue = 0;   break;
        case 1: baseHue = 8;   break;
        case 2: baseHue = 16;  break;
      }
      uint8_t starHue        = baseHue + random8(4);
      uint8_t starBrightness = 4 + random8(28);

      leds[idx] += CHSV(starHue, 255, starBrightness);
    }
  };


  addStars(ledsMatrix, LED_COUNT_MATRIX);
  addStars(ledsStrip,  LED_COUNT_STRIP);
  addStars(ledsDisc,   LED_COUNT_DISC);
  addStars(ledsCloud,  LED_COUNT_CLOUD);


  const uint16_t TOTAL_SPAN =

      LED_COUNT_MATRIX +
      LED_COUNT_STRIP +
      LED_COUNT_DISC +
      LED_COUNT_CLOUD;

  if (!TOTAL_SPAN) return; 

  
  static float cometPosF = 0.0f;  

  const float minSpeed = 0.08f;   
  const float maxSpeed = 3.0f;    

  float cometSpeed = minSpeed + r * (maxSpeed - minSpeed);  

  cometPosF += cometSpeed;
  
  while (cometPosF >= (float)TOTAL_SPAN) {
    cometPosF -= (float)TOTAL_SPAN;
  }
  while (cometPosF < 0.0f) {
    cometPosF += (float)TOTAL_SPAN;
  }

  cometPos = (uint16_t)cometPosF;


  const uint8_t baseTail   = 8;
  const uint8_t extraTail  = (uint8_t)(r * 8.0f);  
  const uint8_t tailLength = baseTail + extraTail; 

  
  uint8_t shimmerSpeed = 2 + (uint8_t)(r * 10.0f); 
  uint8_t shimmerPhase = frame * shimmerSpeed;

  
  auto paintIndex = [&](uint16_t globalIndex, const CRGB &c) {
    uint16_t idx = globalIndex;

    if (idx < LED_COUNT_MATRIX) {
      ledsMatrix[idx] += c;
      return;
    }
    idx -= LED_COUNT_MATRIX;

    if (idx < LED_COUNT_STRIP) {
      ledsStrip[idx] += c;
      return;
    }
    idx -= LED_COUNT_STRIP;

    if (idx < LED_COUNT_DISC) {
      ledsDisc[idx] += c;
      return;
    }
    idx -= LED_COUNT_DISC;

    if (idx < LED_COUNT_CLOUD) {
      ledsCloud[idx] += c;
      return;
    }
  };

  
  const uint8_t headHue = 8; 

  for (uint8_t t = 0; t < tailLength; t++) {
    uint16_t pos = (cometPos + TOTAL_SPAN - t) % TOTAL_SPAN;

    
    uint8_t tailFade = 255 - (t * (255 / tailLength));

    
    uint8_t shimmer      = sin8(shimmerPhase + t * 20);  
    uint8_t shimmerScale = 170 + (shimmer >> 2);         

    uint8_t brightness = scale8(tailFade, shimmerScale);
    
    brightness = scale8(brightness, 200);                

    
    uint8_t mix = (uint8_t)((t * 255) / (tailLength ? tailLength : 1));
    uint8_t hue = headHue - ((mix * headHue) / 255);     

    CRGB c = CHSV(hue, 255, brightness);
    paintIndex(pos, c);
  }
}

void effectQuantumEntanglement(float pm) {
  
  
  float ratio = pmRatio(pm);
  uint8_t pollutionlevel = uint8_t(ratio * 255);
  
  static uint16_t quantumTime = 0;
  static uint8_t entangledPairs = 8; 
  
  
  struct QuantumPair {

    uint16_t stripPos;
    uint16_t matrixPos;
    uint16_t discPos;
    uint8_t hue;
    uint8_t energy;
    bool collapsed;
    uint8_t collapseTimer;
  };
  
  static QuantumPair pairs[8];
  static bool initialized = false;
  
  if(!initialized) {
    for(uint8_t i = 0; i < entangledPairs; i++) {

      pairs[i].stripPos = random16(LED_COUNT_STRIP);
      pairs[i].matrixPos = random16(LED_COUNT_MATRIX);
      pairs[i].discPos = random16(LED_COUNT_DISC);
      pairs[i].hue = random8();
      pairs[i].energy = random8(100, 255);
      pairs[i].collapsed = false;
      pairs[i].collapseTimer = 0;
    }
    initialized = true;
  }


  uint8_t evolutionSpeed = map(pollutionlevel, 0, 255, 2, 15);
  quantumTime += evolutionSpeed;


  uint8_t fieldDecay = map(pollutionlevel, 0, 255, 80, 40);

  fadeToBlackBy(ledsStrip, LED_COUNT_STRIP, fieldDecay);
  fadeToBlackBy(ledsMatrix, LED_COUNT_MATRIX, fieldDecay);
  fadeToBlackBy(ledsDisc, LED_COUNT_DISC, fieldDecay);
  fadeToBlackBy(ledsCloud, LED_COUNT_CLOUD, fieldDecay);

  for(uint16_t i = 0; i < LED_COUNT_STRIP; i++) {
    uint8_t foam = inoise8(i * 30, quantumTime * 2);
    if(foam > 245) {
      ledsStrip[i] += CHSV(200, 255, (foam - 245) * 4);
    }
  }

  // CLOUD: independent quantum foam at a finer noise frequency
  for(uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
    uint8_t foam = inoise8(i * 180, quantumTime * 3);
    if(foam > 200) {
      ledsCloud[i] += CHSV(190, 220, (foam - 200) * 3);
    }
  }
  
  
  for(uint8_t p = 0; p < entangledPairs; p++) {
    QuantumPair &pair = pairs[p];
    
    
    if(!pair.collapsed) {
      
      
      int8_t drift = sin8(quantumTime + p * 32) > 128 ? 1 : -1;

      pair.stripPos = (pair.stripPos + drift * 2 + LED_COUNT_STRIP) % LED_COUNT_STRIP;
      pair.matrixPos = (pair.matrixPos + drift + LED_COUNT_MATRIX) % LED_COUNT_MATRIX;
      pair.discPos = (pair.discPos + drift + LED_COUNT_DISC) % LED_COUNT_DISC;
      
      
      pair.hue += evolutionSpeed / 4;
      
      
      pair.energy = 128 + (sin8(quantumTime * 2 + p * 64) / 2);
      
      
      uint8_t collapseChance = map(pollutionlevel, 0, 255, 1, 12);
      if(random8(100) < collapseChance) {
        pair.collapsed = true;
        pair.collapseTimer = random8(20, 60);
      }
      
      
      uint8_t spread = 8;


      for(int8_t s = -spread; s <= spread; s++) {


        uint8_t probability = 255 - abs(s) * (255 / spread);
        uint8_t brightness = scale8(pair.energy, probability);

      }
      
      
      for(int8_t s = -spread * 2; s <= spread * 2; s++) {
        int16_t idx = (int16_t)pair.stripPos + s;
        while(idx < 0) idx += LED_COUNT_STRIP;
        idx %= LED_COUNT_STRIP;
        
        uint8_t probability = 255 - abs(s) * (255 / (spread * 2));
        uint8_t brightness = scale8(pair.energy, probability);
        ledsStrip[idx] += CHSV(pair.hue + 30, 180, brightness / 4);
      }
      
      
      uint8_t mx = pair.matrixPos % MATRIX_WIDTH;
      uint8_t my = pair.matrixPos / MATRIX_WIDTH;
      
      for(int8_t dy = -3; dy <= 3; dy++) {
        for(int8_t dx = -3; dx <= 3; dx++) {
          int8_t xx = mx + dx;
          int8_t yy = my + dy;
          
          if(xx >= 0 && xx < MATRIX_WIDTH && yy >= 0 && yy < MATRIX_HEIGHT) {
            uint8_t dist = abs(dx) + abs(dy);
            uint8_t probability = 255 - dist * 36;
            uint8_t brightness = scale8(pair.energy, probability);
            ledsMatrix[XY(xx, yy)] += CHSV(pair.hue + 60, 220, brightness / 3);
          }
        }
      }
      

      // DISC: radial entanglement cloud around discPos
      {
        uint8_t dRing, dPos;
        discIdxToRing(pair.discPos, dRing, dPos);
        uint8_t ringLen = DISC_RING_LENS[dRing];
        for(int8_t s = -spread; s <= spread; s++) {
          // same ring, angular spread
          int16_t localIdx = (int16_t)dPos + s;
          while(localIdx < 0) localIdx += ringLen;
          localIdx %= ringLen;
          uint16_t discIdx = DISC_RING_OFFSETS[dRing] + (uint16_t)localIdx;
          uint8_t probability = 255 - abs(s) * (255 / spread);
          uint8_t brightness = scale8(pair.energy, probability);
          ledsDisc[discIdx] += CHSV(pair.hue + 110, 210, brightness / 3);
        }
        // adjacent rings get a weaker echo
        for (int8_t dr = -1; dr <= 1; dr += 2) {
          int8_t rAdj = (int8_t)dRing + dr;
          if (rAdj < 0 || rAdj >= DISC_RING_COUNT) continue;
          uint8_t aLen = DISC_RING_LENS[rAdj];
          uint8_t aPos = (uint8_t)((uint16_t)dPos * aLen / ringLen);
          uint16_t aIdx = DISC_RING_OFFSETS[rAdj] + aPos;
          ledsDisc[aIdx] += CHSV(pair.hue + 110, 200, pair.energy / 5);
        }
      }

    } else {
      
      
      pair.collapseTimer--;
      if(pair.collapseTimer == 0) {

        pair.collapsed = false;


        pair.stripPos = random16(LED_COUNT_STRIP);
        pair.matrixPos = random16(LED_COUNT_MATRIX);
        pair.discPos = random16(LED_COUNT_DISC);
        pair.hue = random8();
      }
      
      
      uint8_t pulseBright = 200 + sin8(quantumTime * 8 + p * 64) / 4;


      for(int8_t s = -4; s <= 4; s++) {
        int16_t idx = (int16_t)pair.stripPos + s;
        while(idx < 0) idx += LED_COUNT_STRIP;
        idx %= LED_COUNT_STRIP;
        uint8_t bright = pulseBright - abs(s) * 40;
        ledsStrip[idx] = CHSV(pair.hue + 30, 255, bright);
      }
      
      
      uint8_t mx = pair.matrixPos % MATRIX_WIDTH;
      uint8_t my = pair.matrixPos / MATRIX_WIDTH;
      
      ledsMatrix[XY(mx, my)] = CHSV(pair.hue + 60, 255, 255);
      
      
      for(int8_t d = 1; d <= 4; d++) {
        
        if(mx + d < MATRIX_WIDTH) 
          ledsMatrix[XY(mx + d, my)] += CHSV(pair.hue + 60, 255, pulseBright / d);
        if(mx - d >= 0) 
          ledsMatrix[XY(mx - d, my)] += CHSV(pair.hue + 60, 255, pulseBright / d);
        
        
        if(my + d < MATRIX_HEIGHT) 
          ledsMatrix[XY(mx, my + d)] += CHSV(pair.hue + 60, 255, pulseBright / d);
        if(my - d >= 0) 
          ledsMatrix[XY(mx, my - d)] += CHSV(pair.hue + 60, 255, pulseBright / d);
      }
      
      
      // DISC: collapsed spark radiates across the ring it sits in, plus inner/outer echoes
      {
        uint8_t dRing, dPos;
        discIdxToRing(pair.discPos, dRing, dPos);
        uint8_t ringLen = DISC_RING_LENS[dRing];
        ledsDisc[pair.discPos] = CHSV(pair.hue + 110, 255, pulseBright);
        for (int8_t s = 1; s <= 3; s++) {
          uint8_t p1 = (uint8_t)((dPos + s) % ringLen);
          uint8_t p2 = (uint8_t)((dPos + ringLen - (s % ringLen)) % ringLen);
          ledsDisc[DISC_RING_OFFSETS[dRing] + p1] += CHSV(pair.hue + 110, 255, pulseBright / (s + 1));
          ledsDisc[DISC_RING_OFFSETS[dRing] + p2] += CHSV(pair.hue + 110, 255, pulseBright / (s + 1));
        }
        for (int8_t dr = -1; dr <= 1; dr += 2) {
          int8_t rAdj = (int8_t)dRing + dr;
          if (rAdj < 0 || rAdj >= DISC_RING_COUNT) continue;
          uint8_t aLen = DISC_RING_LENS[rAdj];
          uint8_t aPos = (uint8_t)((uint16_t)dPos * aLen / ringLen);
          ledsDisc[DISC_RING_OFFSETS[rAdj] + aPos] += CHSV(pair.hue + 110, 220, pulseBright / 3);
        }
      }
      
      
      if(p < entangledPairs - 1) {
        uint16_t midPoint = (pair.stripPos + pairs[p + 1].stripPos) / 2;
        midPoint %= LED_COUNT_STRIP;
        ledsStrip[midPoint] += CHSV(pair.hue + 128, 255, 180);
      }

      // CLOUD: collapsed pair flashes a soft glow at proportional position.
      {
        uint16_t cIdx = (uint32_t)pair.stripPos * LED_COUNT_CLOUD / LED_COUNT_STRIP;
        if (cIdx >= LED_COUNT_CLOUD) cIdx = LED_COUNT_CLOUD - 1;
        ledsCloud[cIdx] = CHSV(pair.hue + 110, 255, pulseBright);
        for (int8_t s = 1; s <= 3; s++) {
          uint16_t left  = (cIdx + LED_COUNT_CLOUD - s) % LED_COUNT_CLOUD;
          uint16_t right = (cIdx + s) % LED_COUNT_CLOUD;
          ledsCloud[left]  += CHSV(pair.hue + 110, 220, pulseBright / (s + 1));
          ledsCloud[right] += CHSV(pair.hue + 110, 220, pulseBright / (s + 1));
        }
      }
    }
  }
  
  
  uint8_t interferenceChance = map(pollutionlevel, 0, 255, 1, 8);
  
  if(random8(100) < interferenceChance) {
    
    uint8_t ix = random8(MATRIX_WIDTH);
    uint8_t iy = random8(MATRIX_HEIGHT);
    
    for(int8_t dy = -2; dy <= 2; dy++) {
      for(int8_t dx = -2; dx <= 2; dx++) {
        int8_t xx = ix + dx;
        int8_t yy = iy + dy;
        
        if(xx >= 0 && xx < MATRIX_WIDTH && yy >= 0 && yy < MATRIX_HEIGHT) {
          uint8_t wave = sin8((dx * dx + dy * dy) * 40 + quantumTime * 4);
          ledsMatrix[XY(xx, yy)] += CHSV(quantumTime / 4, 255, wave / 3);
        }
      }
    }
  }
}

// Mycelial network: a living web of nodes spread across strip, matrix, and disc.
// Action potentials travel along edges and branch at junctions. Three cross-
// surface "portal" edges let pulses hop between surfaces. Clean PM = healthy
// teal/violet; polluted PM shifts to amber/red-violet and branching falters.
void effectMycelialNetwork(float pm) {
  float r = fclamp(pmRatio(pm), 0.0f, 1.0f);
  uint8_t pollution = (uint8_t)(r * 255.0f + 0.5f);

  uint32_t now = nowMs();
  static uint32_t lastFrame = 0;
  uint32_t dt = (lastFrame == 0) ? 16 : (now - lastFrame);
  uint16_t frameMs = (uint16_t)map(pollution, 0, 255, 22, 14);
  if (lastFrame != 0 && (now - lastFrame) < frameMs) return;
  lastFrame = now;
  if (dt > 200) dt = 200;

  enum : uint8_t { SF_STRIP = 0, SF_MATRIX = 1, SF_DISC = 2 };

  static const uint8_t NODE_COUNT = 18;
  struct MN_Node {
    uint8_t  surface;
    uint16_t pos;
    uint8_t  hueOffset;
    uint8_t  pulsePhase;
  };
  static MN_Node nodes[NODE_COUNT];

  static const uint8_t EDGE_COUNT = 26;
  struct MN_Edge { uint8_t a; uint8_t b; };
  static MN_Edge edges[EDGE_COUNT];

  static const uint8_t PULSE_COUNT = 32;
  struct MN_Pulse {
    bool    active;
    uint8_t edge;
    bool    fromAToB;
    float   progress;
    float   speed;
    uint8_t bright;
    uint8_t hueOffset;
  };
  static MN_Pulse pulses[PULSE_COUNT];

  static bool initialized = false;
  if (!initialized) {
    initialized = true;

    // 6 strip nodes, evenly spaced
    for (uint8_t i = 0; i < 6; i++) {
      nodes[i].surface    = SF_STRIP;
      nodes[i].pos        = (LED_COUNT_STRIP > 1)
                              ? (uint16_t)((uint32_t)i * (LED_COUNT_STRIP - 1) / 5)
                              : 0;
      nodes[i].hueOffset  = random8();
      nodes[i].pulsePhase = random8();
    }
    // 8 matrix nodes climbing the column with a jittered x
    for (uint8_t i = 0; i < 8; i++) {
      uint8_t x = random8(MATRIX_WIDTH);
      uint8_t y = (uint8_t)((uint16_t)i * (MATRIX_HEIGHT - 1) / 7);
      nodes[6 + i].surface    = SF_MATRIX;
      nodes[6 + i].pos        = (uint16_t)y * MATRIX_WIDTH + x;
      nodes[6 + i].hueOffset  = random8();
      nodes[6 + i].pulsePhase = random8();
    }
    // 1 disc center + 3 on inner/middle/outer rings
    nodes[14].surface    = SF_DISC;
    nodes[14].pos        = DISC_RING_OFFSETS[0]; // center is idx 180
    nodes[14].hueOffset  = random8();
    nodes[14].pulsePhase = random8();
    for (uint8_t i = 0; i < 3; i++) {
      uint8_t ring = 2 + i * 2; // rings 2, 4, 6
      uint8_t len  = DISC_RING_LENS[ring];
      nodes[15 + i].surface    = SF_DISC;
      nodes[15 + i].pos        = (uint16_t)DISC_RING_OFFSETS[ring] + random8(len);
      nodes[15 + i].hueOffset  = random8();
      nodes[15 + i].pulsePhase = random8();
    }

    uint8_t e = 0;
    for (uint8_t i = 0; i < 5; i++) edges[e++] = { i, (uint8_t)(i + 1) };       // strip chain
    for (uint8_t i = 0; i < 7; i++) edges[e++] = { (uint8_t)(6 + i), (uint8_t)(7 + i) }; // matrix chain
    edges[e++] = { 14, 15 };  edges[e++] = { 14, 16 };  edges[e++] = { 14, 17 }; // disc hub
    edges[e++] = {  5,  6 };  // portal: strip-end -> matrix-start
    edges[e++] = { 13, 14 };  // portal: matrix-end -> disc-center
    edges[e++] = { 17,  0 };  // portal: disc-outer -> strip-start
    edges[e++] = {  1, 10 };  edges[e++] = {  3,  8 };  // skip edges for richness
    edges[e++] = {  7, 15 };  edges[e++] = { 11, 16 };
    edges[e++] = {  4, 12 };  edges[e++] = {  9, 17 };
    edges[e++] = {  2,  6 };  edges[e++] = { 12, 14 };

    for (uint8_t i = 0; i < PULSE_COUNT; i++) pulses[i].active = false;
    for (uint8_t i = 0; i < 4; i++) {
      pulses[i].active    = true;
      pulses[i].edge      = random8(EDGE_COUNT);
      pulses[i].fromAToB  = (random8() & 1) != 0;
      pulses[i].progress  = 0.0f;
      pulses[i].speed     = 0.0008f + 0.0004f * (float)random8() / 255.0f;
      pulses[i].bright    = random8(180, 255);
      pulses[i].hueOffset = random8();
    }
  }

  uint8_t bgHue    = (uint8_t)(130 - (uint8_t)(105.0f * r));   // teal -> rust
  uint8_t pulseHue = (uint8_t)(200 + (uint8_t)( 50.0f * r));   // violet -> red-violet
  uint8_t nodeHue  = (uint8_t)(160 - (uint8_t)(140.0f * r));   // cyan -> amber
  uint8_t bgSat    = (uint8_t)map(pollution, 0, 255, 200, 255);
  uint8_t fadeAmt  = (uint8_t)map(pollution, 0, 255, 38, 60);
  uint8_t edgeBri  = (uint8_t)map(pollution, 0, 255, 18,  6);

  fadeToBlackBy(ledsStrip,  LED_COUNT_STRIP,  fadeAmt);
  fadeToBlackBy(ledsMatrix, LED_COUNT_MATRIX, fadeAmt);
  fadeToBlackBy(ledsDisc,   LED_COUNT_DISC,   fadeAmt);
  fadeToBlackBy(ledsCloud,  LED_COUNT_CLOUD,  fadeAmt);

  // CLOUD: independent ring of 3 traveling mycelial pulses on the cloud loop.
  // The cloud is treated as a closed 1D loop so this reads on a strip or an
  // infinity-sign loop.
  {
    static float cloudPulsePos[3] = { 0.0f, 17.0f, 34.0f };
    static uint8_t cloudPulseHueOff[3] = { 0, 64, 160 };
    float speed = 0.06f + 0.16f * r;
    for (uint8_t i = 0; i < 3; i++) {
      cloudPulsePos[i] += speed;
      if (cloudPulsePos[i] >= (float)LED_COUNT_CLOUD) cloudPulsePos[i] -= (float)LED_COUNT_CLOUD;
      uint16_t centre = (uint16_t)cloudPulsePos[i];
      uint8_t h = pulseHue + (cloudPulseHueOff[i] >> 3);
      for (int8_t s = -3; s <= 3; s++) {
        uint16_t idx = (centre + LED_COUNT_CLOUD + s) % LED_COUNT_CLOUD;
        uint8_t bri = (uint8_t)(200 - abs(s) * 55);
        ledsCloud[idx] += CHSV(h, bgSat, bri);
      }
    }
  }

  auto plotNode = [&](const MN_Node& n, uint8_t hue, uint8_t bri) {
    if (n.surface == SF_STRIP) {
      if (n.pos < LED_COUNT_STRIP) ledsStrip[n.pos] += CHSV(hue, bgSat, bri);
    } else if (n.surface == SF_MATRIX) {
      uint8_t x = (uint8_t)(n.pos % MATRIX_WIDTH);
      uint8_t y = (uint8_t)(n.pos / MATRIX_WIDTH);
      if (x < MATRIX_WIDTH && y < MATRIX_HEIGHT) ledsMatrix[XY(x, y)] += CHSV(hue, bgSat, bri);
    } else {
      if (n.pos < LED_COUNT_DISC) ledsDisc[n.pos] += CHSV(hue, bgSat, bri);
    }
  };

  auto plotPoint = [&](const MN_Node& a, const MN_Node& b, float p, uint8_t hue, uint8_t bri) {
    if (a.surface != b.surface) {
      // Cross-surface edge: pulse "submerges" at one node, "emerges" at the other.
      if (p < 0.5f) plotNode(a, hue, bri);
      else          plotNode(b, hue, bri);
      return;
    }
    if (a.surface == SF_STRIP) {
      float fpos = (1.0f - p) * (float)a.pos + p * (float)b.pos;
      int16_t i = (int16_t)fpos;
      if (i >= 0 && i < (int16_t)LED_COUNT_STRIP) {
        ledsStrip[i] += CHSV(hue, bgSat, bri);
        if (i > 0)                              ledsStrip[i - 1] += CHSV(hue, bgSat, bri >> 2);
        if (i + 1 < (int16_t)LED_COUNT_STRIP)   ledsStrip[i + 1] += CHSV(hue, bgSat, bri >> 2);
      }
    } else if (a.surface == SF_MATRIX) {
      uint8_t ax = (uint8_t)(a.pos % MATRIX_WIDTH), ay = (uint8_t)(a.pos / MATRIX_WIDTH);
      uint8_t bx = (uint8_t)(b.pos % MATRIX_WIDTH), by = (uint8_t)(b.pos / MATRIX_WIDTH);
      float fx = (1.0f - p) * (float)ax + p * (float)bx;
      float fy = (1.0f - p) * (float)ay + p * (float)by;
      int8_t ix = (int8_t)(fx + 0.5f);
      int8_t iy = (int8_t)(fy + 0.5f);
      if (ix >= 0 && ix < (int8_t)MATRIX_WIDTH && iy >= 0 && iy < (int8_t)MATRIX_HEIGHT) {
        ledsMatrix[XY((uint8_t)ix, (uint8_t)iy)] += CHSV(hue, bgSat, bri);
      }
    } else {
      uint8_t ar, ap, br_, bp_;
      discIdxToRing(a.pos, ar, ap);
      discIdxToRing(b.pos, br_, bp_);
      uint8_t aLen = DISC_RING_LENS[ar];
      uint8_t bLen = DISC_RING_LENS[br_];
      float aAng = aLen ? (float)ap / (float)aLen : 0.0f;
      float bAng = bLen ? (float)bp_ / (float)bLen : 0.0f;
      float diff = bAng - aAng;
      if (diff >  0.5f) diff -= 1.0f;
      if (diff < -0.5f) diff += 1.0f;
      float ang = aAng + diff * p;
      if (ang < 0.0f) ang += 1.0f;
      if (ang >= 1.0f) ang -= 1.0f;
      float fring = (1.0f - p) * (float)ar + p * (float)br_;
      uint8_t ring = (uint8_t)(fring + 0.5f);
      if (ring >= DISC_RING_COUNT) ring = DISC_RING_COUNT - 1;
      uint8_t len = DISC_RING_LENS[ring];
      uint8_t pp  = (len > 0) ? (uint8_t)(ang * (float)len) : 0;
      if (pp >= len && len > 0) pp = len - 1;
      uint16_t idx = (uint16_t)DISC_RING_OFFSETS[ring] + pp;
      if (idx < LED_COUNT_DISC) ledsDisc[idx] += CHSV(hue, bgSat, bri);
    }
  };

  // Faint persistent network scaffold so the topology is always visible.
  for (uint8_t e = 0; e < EDGE_COUNT; e++) {
    const MN_Node& a = nodes[edges[e].a];
    const MN_Node& b = nodes[edges[e].b];
    if (a.surface != b.surface) continue;
    const uint8_t SAMPLES = 10;
    uint8_t h = bgHue + (a.hueOffset >> 4);
    for (uint8_t s = 1; s < SAMPLES; s++) {
      plotPoint(a, b, (float)s / (float)SAMPLES, h, edgeBri);
    }
  }

  // Slow heartbeat on every node.
  uint8_t hbStep = (uint8_t)map(pollution, 0, 255, 1, 4);
  for (uint8_t i = 0; i < NODE_COUNT; i++) {
    nodes[i].pulsePhase += hbStep;
    uint8_t v   = sin8(nodes[i].pulsePhase);
    uint8_t bri = (uint8_t)map(v, 0, 255, 28, 130);
    plotNode(nodes[i], nodeHue + (nodes[i].hueOffset >> 3), bri);
  }

  // Advance pulses, render them with a small leading/trailing trail.
  float dtf = (float)dt;
  uint8_t branchChance = (uint8_t)map(pollution, 0, 255, 200, 60);
  for (uint8_t i = 0; i < PULSE_COUNT; i++) {
    if (!pulses[i].active) continue;
    pulses[i].progress += pulses[i].speed * dtf;

    if (pulses[i].progress >= 1.0f) {
      const MN_Edge& cur = edges[pulses[i].edge];
      uint8_t arrived = pulses[i].fromAToB ? cur.b : cur.a;
      for (uint8_t e = 0; e < EDGE_COUNT; e++) {
        if (e == pulses[i].edge) continue;
        const MN_Edge& cand = edges[e];
        bool aToB;
        if      (cand.a == arrived) aToB = true;
        else if (cand.b == arrived) aToB = false;
        else continue;
        if (random8() >= branchChance) continue;
        for (uint8_t j = 0; j < PULSE_COUNT; j++) {
          if (!pulses[j].active) {
            pulses[j].active    = true;
            pulses[j].edge      = e;
            pulses[j].fromAToB  = aToB;
            pulses[j].progress  = 0.0f;
            pulses[j].speed     = pulses[i].speed * (0.7f + 0.6f * (float)random8() / 255.0f);
            pulses[j].bright    = (uint8_t)((uint16_t)pulses[i].bright * 9 / 10);
            pulses[j].hueOffset = pulses[i].hueOffset + random8(8);
            break;
          }
        }
      }
      pulses[i].active = false;
      continue;
    }

    const MN_Edge& ed = edges[pulses[i].edge];
    const MN_Node& na = pulses[i].fromAToB ? nodes[ed.a] : nodes[ed.b];
    const MN_Node& nb = pulses[i].fromAToB ? nodes[ed.b] : nodes[ed.a];
    uint8_t h = pulseHue + (pulses[i].hueOffset >> 3);
    plotPoint(na, nb, pulses[i].progress, h, pulses[i].bright);
    if (pulses[i].progress > 0.05f)
      plotPoint(na, nb, pulses[i].progress - 0.05f, h, pulses[i].bright >> 2);
    if (pulses[i].progress < 0.95f)
      plotPoint(na, nb, pulses[i].progress + 0.05f, h, pulses[i].bright >> 2);
  }

  // Spontaneous firing: a node occasionally launches a pulse on one of its edges.
  uint8_t spawnChance = (uint8_t)map(pollution, 0, 255, 4, 14);
  if (random8() < spawnChance) {
    uint8_t src = random8(NODE_COUNT);
    uint8_t cand[6]; uint8_t cn = 0;
    for (uint8_t e = 0; e < EDGE_COUNT && cn < 6; e++) {
      if (edges[e].a == src || edges[e].b == src) cand[cn++] = e;
    }
    if (cn > 0) {
      uint8_t pick = cand[random8(cn)];
      bool aToB   = (edges[pick].a == src);
      for (uint8_t j = 0; j < PULSE_COUNT; j++) {
        if (!pulses[j].active) {
          pulses[j].active    = true;
          pulses[j].edge      = pick;
          pulses[j].fromAToB  = aToB;
          pulses[j].progress  = 0.0f;
          pulses[j].speed     = (0.0006f + 0.0010f * (float)random8() / 255.0f) * (0.5f + r);
          pulses[j].bright    = random8(180, 255);
          pulses[j].hueOffset = random8();
          break;
        }
      }
    }
  }
}

void effectPeople(float pm) {
  
  float r = pmRatio(pm);
  if (r < 0.0f) r = 0.0f;
  if (r > 1.0f) r = 1.0f;
  uint8_t pollution = (uint8_t)(r * 255.0f + 0.5f);

  uint32_t now = nowMs();
  static uint32_t lastFrame = 0;
  uint32_t dt = now - lastFrame;

  
  uint16_t frameMs = (uint16_t)map(pollution, 0, 255, 42, 12);
  if (lastFrame != 0 && dt < frameMs) {
    return;
  }
  lastFrame = now;
  if (dt == 0) dt = frameMs;

  
  static float lastR = 0.0f;
  float delta = r - lastR;
  if (delta < 0.0f) delta = -delta;
  lastR = r;

  
  uint8_t moodChaos   = map(pollution, 0, 255, 10, 220);   
  uint8_t moodEnergy  = map(pollution, 0, 255, 40, 230);   
  uint8_t fadeAmt     = map(pollution, 0, 255, 95, 28);    
  uint8_t socialTight = map(pollution, 0, 255, 30, 200);   

  
  static uint8_t globalAlert = 0;
  if (delta > 0.05f) {
    globalAlert = 160; 
  } else if (globalAlert > 0) {
    globalAlert--;
  }
  bool alert = (globalAlert > 0);


  fadeToBlackBy(ledsStrip,    LED_COUNT_STRIP,    fadeAmt);
  fadeToBlackBy(ledsMatrix,   LED_COUNT_MATRIX,   fadeAmt);
  fadeToBlackBy(ledsDisc,     LED_COUNT_DISC,     fadeAmt);
  fadeToBlackBy(ledsCloud,    LED_COUNT_CLOUD,    fadeAmt);

  // CLOUD: 5 "people" wander a 1D loop, heart-rate rising with pollution and
  // brief alert flashes echoed onto the cloud.
  {
    static float cloudPeoplePos[5] = { 5.0f, 14.0f, 23.0f, 32.0f, 41.0f };
    static uint8_t cloudPeopleHue[5] = { 16, 96, 160, 200, 32 };
    static uint8_t cloudPulsePhase[5] = { 0, 51, 102, 153, 204 };
    float speed = 0.04f + 0.30f * r;
    for (uint8_t i = 0; i < 5; i++) {
      // gentle drift
      float dir = (sin8((uint8_t)(now / 80) + i * 50) > 128) ? 1.0f : -1.0f;
      cloudPeoplePos[i] += dir * speed;
      while (cloudPeoplePos[i] < 0.0f) cloudPeoplePos[i] += (float)LED_COUNT_CLOUD;
      while (cloudPeoplePos[i] >= (float)LED_COUNT_CLOUD) cloudPeoplePos[i] -= (float)LED_COUNT_CLOUD;
      cloudPulsePhase[i] += 1 + (moodEnergy >> 5);
      uint8_t heart = sin8(cloudPulsePhase[i] + i * 9 + (uint8_t)(now >> 3));
      uint8_t baseV = scale8(heart, qadd8(30, moodEnergy));
      if (alert) baseV = qadd8(baseV, scale8(sin8(globalAlert * 4 + i * 7), 160));
      uint8_t hueBias = scale8(pollution, 220);
      uint8_t hue = lerp8by8(cloudPeopleHue[i], 0, hueBias);
      uint16_t idx = (uint16_t)cloudPeoplePos[i] % LED_COUNT_CLOUD;
      ledsCloud[idx] += CHSV(hue, 220, baseV);
      ledsCloud[(idx + 1) % LED_COUNT_CLOUD] += CHSV(hue, 220, baseV / 3);
      ledsCloud[(idx + LED_COUNT_CLOUD - 1) % LED_COUNT_CLOUD] += CHSV(hue, 220, baseV / 3);
    }
  }


  const uint8_t MOOD_CHILL = 0;
  const uint8_t MOOD_WALK  = 1;
  const uint8_t MOOD_PACE  = 2;
  const uint8_t MOOD_FLEE  = 3;   
  const uint8_t MOOD_SLEEP = 4;   

  
  struct HBPerson1D {
    bool     active;
    float    pos;        
    float    speed;      
    uint8_t  mood;       
    uint8_t  hue;
    uint8_t  pulsePhase; 
    uint8_t  id;         
  };


  const uint8_t STRIP_PEOPLE = 18;


  static HBPerson1D stripPeople[STRIP_PEOPLE];

  static bool peopleInit = false;
  if (!peopleInit) {
    peopleInit = true;

    for (uint8_t i = 0; i < STRIP_PEOPLE; i++) {
      stripPeople[i].active     = true;
      stripPeople[i].pos        = random16(LED_COUNT_STRIP);
      stripPeople[i].speed      = 0.02f + 0.05f * (float)random8() / 255.0f;
      stripPeople[i].mood       = (i % 3 == 0) ? MOOD_PACE : MOOD_WALK;
      stripPeople[i].hue        = 80 + random8(80); 
      stripPeople[i].pulsePhase = random8();
      stripPeople[i].id         = i + 40;
    }

  }

  
  auto updatePerson1D = [&](HBPerson1D &p, CRGB *leds, uint16_t len, bool isRing) {
    if (!p.active || len == 0) return;

    
    uint8_t moodRoll = random8();
    if (moodRoll < (uint8_t)map(pollution, 0, 255, 1, 5)) {
      
      p.mood = MOOD_CHILL;
    } else if (moodRoll > 245 && pollution > 180) {
      p.mood = MOOD_FLEE;
    } else if (moodRoll > 230 && pollution > 120) {
      p.mood = MOOD_PACE;
    } else if (moodRoll < 20 && pollution < 80) {
      p.mood = MOOD_SLEEP;
    } else if (moodRoll < 40) {
      p.mood = MOOD_WALK;
    }

    
    float baseSpeed = 0.015f + 0.06f * r;
    float moodFactor = 1.0f;
    switch (p.mood) {
      case MOOD_CHILL: moodFactor = 0.4f; break;
      case MOOD_SLEEP: moodFactor = 0.2f; break;
      case MOOD_WALK:  moodFactor = 1.0f; break;
      case MOOD_PACE:  moodFactor = 1.6f; break;
      case MOOD_FLEE:  moodFactor = 2.3f; break;
    }

    
    float dir = (p.id & 0x01) ? 1.0f : -1.0f;

    
    int16_t j = (int16_t)random8() - 128;
    float jitter = (float)j * (float)moodChaos / 255.0f * 0.0007f;

    float step = (baseSpeed * moodFactor + jitter) * (float)dt;
    p.pos += dir * step;

    if (isRing) {
      while (p.pos < 0.0f) p.pos += (float)len;
      while (p.pos >= (float)len) p.pos -= (float)len;
    } else {
      if (p.pos < 0.0f) {
        p.pos = 0.0f;
        
        if (p.mood == MOOD_PACE || p.mood == MOOD_FLEE) {
          p.id ^= 0x01; 
        }
      }
      if (p.pos > (float)(len - 1)) {
        p.pos = (float)(len - 1);
        if (p.mood == MOOD_PACE || p.mood == MOOD_FLEE) {
          p.id ^= 0x01;
        }
      }
    }

    
    p.pulsePhase += 1 + (moodEnergy >> 5);
    uint8_t beat = sin8(p.pulsePhase + (uint8_t)(now >> 2) + p.id * 13);

    uint8_t moodBright;
    switch (p.mood) {
      default:
      case MOOD_CHILL:
        moodBright = scale8(beat, moodEnergy);
        break;
      case MOOD_SLEEP:
        moodBright = scale8(beat, moodEnergy / 3);
        break;
      case MOOD_WALK:
        moodBright = scale8(beat, qadd8(moodEnergy, 40));
        break;
      case MOOD_PACE:
        moodBright = scale8(beat, qadd8(moodEnergy, 80));
        break;
      case MOOD_FLEE:
        moodBright = scale8(beat, qadd8(moodEnergy, 120));
        break;
    }

    
    if (alert) {
      uint8_t alertBoost = sin8(globalAlert * 3 + p.id * 11);
      moodBright = qadd8(moodBright, scale8(alertBoost, 180));
    }

    
    uint8_t moodHue = p.hue;
    uint8_t redBias = scale8(pollution, 200);
    if (p.mood == MOOD_FLEE || p.mood == MOOD_PACE) {
      moodHue = lerp8by8(moodHue, 0, redBias); 
    }

    
    float pf = p.pos;
    if (pf < 0.0f) pf = 0.0f;
    if (pf > (float)(len - 1)) pf = (float)(len - 1);

    uint16_t i0 = (uint16_t)pf;
    uint16_t i1 = (i0 + 1 < len) ? (i0 + 1) : i0;
    float frac = pf - (float)i0;
    uint8_t v0 = (uint8_t)((1.0f - frac) * (float)moodBright);
    uint8_t v1 = (uint8_t)(frac * (float)moodBright);

    uint8_t sat = (p.mood == MOOD_SLEEP || p.mood == MOOD_CHILL) ? 120 : 200;

    leds[i0] += CHSV(moodHue, sat, v0);
    if (i1 != i0) {
      leds[i1] += CHSV(moodHue, sat, v1);
    }

    
    if ((p.mood == MOOD_WALK || p.mood == MOOD_FLEE) && random8() < (moodChaos >> 4)) {
      uint16_t jIdx = (i0 + (p.id & 0x01 ? 1 : len - 1)) % len;
      leds[jIdx] += CHSV(moodHue, sat, v0 / 2);
    }
  };

  for (uint8_t i = 0; i < STRIP_PEOPLE; i++) {
    updatePerson1D(stripPeople[i], ledsStrip, LED_COUNT_STRIP, false);
  }

  
  struct HBPerson2D {
    bool    active;
    float   x, y;
    float   vx, vy;
    uint8_t mood;
    uint8_t hue;
    uint8_t pulsePhase;
    uint8_t id;
  };

  const uint8_t MATRIX_PEOPLE = 20;
  static HBPerson2D gridPeople[MATRIX_PEOPLE];
  static bool gridInit = false;
  if (!gridInit) {
    gridInit = true;
    for (uint8_t i = 0; i < MATRIX_PEOPLE; i++) {
      gridPeople[i].active     = true;
      gridPeople[i].x          = random8(MATRIX_WIDTH);
      gridPeople[i].y          = random8(MATRIX_HEIGHT);
      gridPeople[i].vx         = 0;
      gridPeople[i].vy         = 0;
      gridPeople[i].mood       = (i % 4 == 0) ? MOOD_SLEEP : MOOD_WALK;
      gridPeople[i].hue        = 90 + random8(80);
      gridPeople[i].pulsePhase = random8();
      gridPeople[i].id         = i + 120;
    }
  }

  for (uint8_t i = 0; i < MATRIX_PEOPLE; i++) {
    HBPerson2D &p = gridPeople[i];
    if (!p.active) continue;

    
    uint8_t mRoll = random8();
    if (mRoll > 250 && pollution > 160) {
      p.mood = MOOD_FLEE;
    } else if (mRoll < 4 && pollution < 80) {
      p.mood = MOOD_SLEEP;
    } else if (mRoll < 20) {
      p.mood = MOOD_WALK;
    }

    float accelBase = 0.0015f + 0.0035f * r;
    float accelScale = 1.0f;
    switch (p.mood) {
      case MOOD_SLEEP: accelScale = 0.2f; break;
      case MOOD_CHILL: accelScale = 0.5f; break;
      case MOOD_WALK:  accelScale = 1.0f; break;
      case MOOD_PACE:  accelScale = 1.6f; break;
      case MOOD_FLEE:  accelScale = 2.4f; break;
    }

    int16_t axr = (int16_t)random8() - 128;
    int16_t ayr = (int16_t)random8() - 128;
    float ax = (float)axr * accelBase * accelScale;
    float ay = (float)ayr * accelBase * accelScale;

    
    float cx = (MATRIX_WIDTH  - 1) * 0.5f;
    float cy = (MATRIX_HEIGHT - 1) * 0.5f;
    float dx = cx - p.x;
    float dy = cy - p.y;
    float centerPull = 0.0006f * (1.0f - r);
    float centerPush = 0.0012f * r;

    ax += dx * centerPull - dx * centerPush;
    ay += dy * centerPull - dy * centerPush;

    p.vx += ax * (float)dt;
    p.vy += ay * (float)dt;

    float maxSpeed = 0.015f + 0.05f * r * (p.mood == MOOD_FLEE ? 1.5f : 1.0f);
    float vsq = p.vx * p.vx + p.vy * p.vy;
    float maxSq = maxSpeed * maxSpeed;
    if (vsq > maxSq && vsq > 0.0f) {
      float scale = sqrt(maxSq / vsq);
      p.vx *= scale;
      p.vy *= scale;
    }

    p.x += p.vx * (float)dt;
    p.y += p.vy * (float)dt;

    
    if (p.x < 0.0f) {
      p.x = 0.0f;
      p.vx = -p.vx;
      if (pollution > 150) p.mood = MOOD_PACE;
    } else if (p.x > (float)(MATRIX_WIDTH - 1)) {
      p.x = (float)(MATRIX_WIDTH - 1);
      p.vx = -p.vx;
      if (pollution > 150) p.mood = MOOD_PACE;
    }
    if (p.y < 0.0f) {
      p.y = 0.0f;
      p.vy = -p.vy;
    } else if (p.y > (float)(MATRIX_HEIGHT - 1)) {
      p.y = (float)(MATRIX_HEIGHT - 1);
      p.vy = -p.vy;
    }

    p.pulsePhase += 1 + (moodEnergy >> 5);
    uint8_t heart = sin8(p.pulsePhase + p.id * 9 + (uint8_t)(now >> 3));

    uint8_t baseV = scale8(heart, qadd8(30, moodEnergy));
    if (p.mood == MOOD_SLEEP) {
      baseV = baseV / 3;
    } else if (p.mood == MOOD_FLEE) {
      baseV = qadd8(baseV, 80);
    }

    
    uint8_t crowdHueBias = scale8(pollution, 220);
    uint8_t personHue    = lerp8by8(p.hue, 0, crowdHueBias);

    if (alert) {
      uint8_t alertBoost = sin8(globalAlert * 4 + p.id * 7);
      baseV = qadd8(baseV, scale8(alertBoost, 160));
    }

    if (baseV == 0) continue;

    float fx = p.x;
    float fy = p.y;
    if (fx < 0.0f) fx = 0.0f;
    if (fx > (float)(MATRIX_WIDTH - 1)) fx = (float)(MATRIX_WIDTH - 1);
    if (fy < 0.0f) fy = 0.0f;
    if (fy > (float)(MATRIX_HEIGHT - 1)) fy = (float)(MATRIX_HEIGHT - 1);

    uint8_t x0 = (uint8_t)fx;
    uint8_t y0 = (uint8_t)fy;
    uint8_t x1 = (x0 + 1 < MATRIX_WIDTH)  ? (x0 + 1) : x0;
    uint8_t y1 = (y0 + 1 < MATRIX_HEIGHT) ? (y0 + 1) : y0;

    float fracX = fx - (float)x0;
    float fracY = fy - (float)y0;

    uint8_t v00 = (uint8_t)((1.0f - fracX) * (1.0f - fracY) * (float)baseV);
    uint8_t v10 = (uint8_t)(fracX * (1.0f - fracY) * (float)baseV);
    uint8_t v01 = (uint8_t)((1.0f - fracX) * fracY * (float)baseV);
    uint8_t v11 = (uint8_t)(fracX * fracY * (float)baseV);

    uint8_t sat = (p.mood == MOOD_SLEEP || p.mood == MOOD_CHILL) ? 130 : 230;

    ledsMatrix[XY(x0, y0)] += CHSV(personHue, sat, v00);
    if (v10) ledsMatrix[XY(x1, y0)] += CHSV(personHue, sat, v10);
    if (v01) ledsMatrix[XY(x0, y1)] += CHSV(personHue, sat, v01);
    if (v11) ledsMatrix[XY(x1, y1)] += CHSV(personHue, sat, v11);

    
    int8_t hx = x0;
    int8_t hy = y0;
    if (p.vx > 0.001f && hx + 1 < (int8_t)MATRIX_WIDTH)  hx++;
    if (p.vx < -0.001f && hx - 1 >= 0)                   hx--;
    if (p.vy > 0.001f && hy + 1 < (int8_t)MATRIX_HEIGHT) hy++;
    if (p.vy < -0.001f && hy - 1 >= 0)                   hy--;

    if (hx >= 0 && hy >= 0 && hx < (int8_t)MATRIX_WIDTH && hy < (int8_t)MATRIX_HEIGHT) {
      ledsMatrix[XY((uint8_t)hx, (uint8_t)hy)] += CHSV(personHue, sat, baseV / 4);
    }

    // DISC: project same person onto disc in polar coords (dist from matrix center -> ring)
    {
      float cx = (MATRIX_WIDTH  - 1) * 0.5f;
      float cy = (MATRIX_HEIGHT - 1) * 0.5f;
      float nx = (p.x - cx) / (cx > 0.0f ? cx : 1.0f);
      float ny = (p.y - cy) / (cy > 0.0f ? cy : 1.0f);
      float dd = sqrtf(nx * nx + ny * ny);
      if (dd > 1.0f) dd = 1.0f;
      uint8_t dRing = (uint8_t)(dd * (float)(DISC_RING_COUNT - 1) + 0.5f);
      uint8_t ringLen = DISC_RING_LENS[dRing];
      float ang = atan2f(ny, nx);
      if (ang < 0.0f) ang += 6.2831853f;
      uint8_t dPos = (uint8_t)((ang / 6.2831853f) * (float)ringLen);
      if (dPos >= ringLen) dPos = ringLen - 1;
      uint16_t dIdx = DISC_RING_OFFSETS[dRing] + dPos;
      ledsDisc[dIdx] += CHSV(personHue, sat, baseV);
      if (dRing + 1 < DISC_RING_COUNT) {
        uint8_t nLen = DISC_RING_LENS[dRing + 1];
        uint8_t nPos = (uint8_t)((uint16_t)dPos * nLen / ringLen);
        ledsDisc[DISC_RING_OFFSETS[dRing + 1] + nPos] += CHSV(personHue, sat, baseV / 4);
      }
    }
  }
}

// Magnetic dipole field, calm and structured by design — not a rainbow wash.
//   Strip  = the bar magnet itself: saturated blue N-cap | dark middle | red
//            S-cap, with slow flux pulses sliding N->S along the dim middle.
//   Matrix = iron filings around a vertical bar magnet: a fixed scatter of short
//            filing fragments aligned to the local 2D dipole field direction.
//   Disc   = top-down view of the dipole, N at top of disc, S at bottom; bright
//            pole caps, dim equator.
// Hue/sat use saturation-blending (silver near the magnetic equator) rather than
// hue-wheel lerp, so the in-between never becomes purple/green. Brightness is
// hard-capped low; fill_solid(black) every frame so nothing piles up.
void effectFluxField(float pm) {
  float r = fclamp(pmRatio(pm), 0.0f, 1.0f);
  uint8_t pollution = (uint8_t)(r * 255.0f + 0.5f);

  uint32_t now = nowMs();
  static uint32_t lastFrame = 0;
  uint32_t dt = (lastFrame == 0) ? 24 : (now - lastFrame);
  if (lastFrame != 0 && dt < 24) return;
  lastFrame = now;
  if (dt > 200) dt = 200;

  static uint16_t breathPhase = 0;
  breathPhase += (uint16_t)map(pollution, 0, 255, 30, 80);
  uint8_t breath = sin8((uint8_t)(breathPhase >> 4));
  uint8_t globalScale = (uint8_t)(190 + ((uint16_t)breath * 55 / 255));

  const uint8_t nHue   = 160;     // north = blue
  const uint8_t sHue   = 0;       // south = red
  uint8_t maxV         = (uint8_t)map(pollution, 0, 255, 100, 150);
  uint8_t midFloorSat  = (uint8_t)map(pollution, 0, 255, 50, 90);

  // ============ STRIP ============
  fill_solid(ledsStrip, LED_COUNT_STRIP, CRGB::Black);
  uint16_t capLen = LED_COUNT_STRIP / 8;
  if (capLen < 8) capLen = 8;
  if (capLen > LED_COUNT_STRIP / 2) capLen = LED_COUNT_STRIP / 2;
  for (uint16_t i = 0; i < capLen; i++) {
    float t = 1.0f - (float)i / (float)capLen;
    uint8_t v = (uint8_t)(t * t * (float)maxV);
    v = scale8(v, globalScale);
    if (v == 0) continue;
    ledsStrip[i] = CHSV(nHue, 255, v);
    ledsStrip[LED_COUNT_STRIP - 1 - i] = CHSV(sHue, 255, v);
  }
  if (LED_COUNT_STRIP > 2 * capLen) {
    for (uint16_t i = capLen; i < LED_COUNT_STRIP - capLen; i++) {
      uint8_t hum = inoise8((uint16_t)(i * 4), (uint16_t)(now >> 5));
      if (hum > 200) {
        uint8_t v = scale8((uint8_t)(hum - 200), 28);
        ledsStrip[i] = CHSV(nHue, midFloorSat, v);
      }
    }
  }

  // Slow flux pulses traveling N -> S along the strip.
  static const uint8_t FLUX_COUNT = 3;
  struct FluxParticle { float pos; float speed; };
  static FluxParticle flux[FLUX_COUNT];
  static bool fluxInit = false;
  if (!fluxInit) {
    for (uint8_t i = 0; i < FLUX_COUNT; i++) { flux[i].pos = -1.0f; flux[i].speed = 0.0f; }
    fluxInit = true;
  }
  uint8_t launchChance = (uint8_t)map(pollution, 0, 255, 1, 5);
  for (uint8_t i = 0; i < FLUX_COUNT; i++) {
    if (flux[i].pos < 0.0f) {
      if (random8() < launchChance) {
        flux[i].pos   = 0.0f;
        flux[i].speed = 0.04f + 0.06f * (float)random8() / 255.0f;
      }
      continue;
    }
    flux[i].pos += flux[i].speed * (float)dt;
    if (flux[i].pos >= (float)LED_COUNT_STRIP) { flux[i].pos = -1.0f; continue; }
    int16_t pi = (int16_t)flux[i].pos;
    if (pi < 0 || pi >= (int16_t)LED_COUNT_STRIP) continue;
    float pol = 1.0f - 2.0f * flux[i].pos / (float)(LED_COUNT_STRIP - 1);
    float absPol = pol < 0.0f ? -pol : pol;
    uint8_t hue = (pol >= 0.0f) ? nHue : sHue;
    uint8_t sat = (uint8_t)(60.0f + 195.0f * absPol);
    uint8_t v   = scale8(maxV, globalScale);
    ledsStrip[pi] += CHSV(hue, sat, v);
    for (int8_t d = 1; d <= 3; d++) {
      int16_t p = pi - d;
      if (p < 0) break;
      ledsStrip[p] += CHSV(hue, sat, v >> (d + 1));
    }
  }

  // ============ MATRIX (iron filings) ============
  fill_solid(ledsMatrix, LED_COUNT_MATRIX, CRGB::Black);

  static const uint8_t FILING_COUNT = 28;
  struct Filing { uint8_t x, y; };
  static Filing filings[FILING_COUNT];
  static bool filingsInit = false;
  if (!filingsInit) {
    filingsInit = true;
    uint8_t k = 0;
    for (uint8_t row = 0; row < 7 && k < FILING_COUNT; row++) {
      for (uint8_t col = 0; col < 4 && k < FILING_COUNT; col++) {
        int8_t cx = (int8_t)(col * 2 + 1);
        if (random8() & 1) cx += (int8_t)random8(2) - 1;
        if (cx < 0) cx = 0;
        if (cx >= (int8_t)MATRIX_WIDTH) cx = MATRIX_WIDTH - 1;
        int8_t cy = (int8_t)((uint16_t)row * (MATRIX_HEIGHT - 1) / 6);
        cy += (int8_t)random8(3) - 1;
        if (cy < 0) cy = 0;
        if (cy >= (int8_t)MATRIX_HEIGHT) cy = MATRIX_HEIGHT - 1;
        filings[k].x = (uint8_t)cx;
        filings[k].y = (uint8_t)cy;
        k++;
      }
    }
  }

  const float mcx = (float)(MATRIX_WIDTH - 1) * 0.5f;
  const float pSy = (float)(MATRIX_HEIGHT - 1);
  for (uint8_t k = 0; k < FILING_COUNT; k++) {
    float fx = (float)filings[k].x;
    float fy = (float)filings[k].y;
    float dxn = fx - mcx;  float dyn = fy;
    float dxs = fx - mcx;  float dys = fy - pSy;
    float rn  = sqrtf(dxn * dxn + dyn * dyn) + 0.01f;
    float rs  = sqrtf(dxs * dxs + dys * dys) + 0.01f;
    float rn3 = rn * rn * rn;
    float rs3 = rs * rs * rs;
    float Bx = dxn / rn3 - dxs / rs3;
    float By = dyn / rn3 - dys / rs3;
    float Bm = sqrtf(Bx * Bx + By * By);
    if (Bm < 0.0001f) continue;
    float ux = Bx / Bm;
    float uy = By / Bm;

    float fn = 1.0f / (1.0f + 0.35f * rn);
    float fs = 1.0f / (1.0f + 0.35f * rs);
    float pol = (fn - fs) / (fn + fs + 0.001f);
    float absPol = pol < 0.0f ? -pol : pol;
    uint8_t hue = (pol >= 0.0f) ? nHue : sHue;
    uint8_t sat = (uint8_t)(60.0f + 195.0f * absPol);

    float prox = (fn > fs) ? fn : fs;
    uint8_t v = (uint8_t)(prox * (float)maxV);
    v = scale8(v, globalScale);
    if (v == 0) continue;

    ledsMatrix[XY(filings[k].x, filings[k].y)] += CHSV(hue, sat, v);

    int8_t dxF = (ux >  0.3f) ? 1 : (ux < -0.3f ? -1 : 0);
    int8_t dyF = (uy >  0.3f) ? 1 : (uy < -0.3f ? -1 : 0);
    if (dxF == 0 && dyF == 0) dyF = 1;

    int8_t fxA = (int8_t)filings[k].x + dxF;
    int8_t fyA = (int8_t)filings[k].y + dyF;
    if (fxA >= 0 && fxA < (int8_t)MATRIX_WIDTH && fyA >= 0 && fyA < (int8_t)MATRIX_HEIGHT)
      ledsMatrix[XY((uint8_t)fxA, (uint8_t)fyA)] += CHSV(hue, sat, v >> 1);

    int8_t fxB = (int8_t)filings[k].x - dxF;
    int8_t fyB = (int8_t)filings[k].y - dyF;
    if (fxB >= 0 && fxB < (int8_t)MATRIX_WIDTH && fyB >= 0 && fyB < (int8_t)MATRIX_HEIGHT)
      ledsMatrix[XY((uint8_t)fxB, (uint8_t)fyB)] += CHSV(hue, sat, v >> 1);
  }

  {
    int8_t mid = (int8_t)mcx;
    for (int8_t dx = -1; dx <= 1; dx++) {
      int8_t px = mid + dx;
      if (px < 0 || px >= (int8_t)MATRIX_WIDTH) continue;
      uint8_t poleV = (dx == 0) ? maxV : (uint8_t)((uint16_t)maxV * 5 / 8);
      poleV = scale8(poleV, globalScale);
      ledsMatrix[XY((uint8_t)px, 0)]                 += CHSV(nHue, 255, poleV);
      ledsMatrix[XY((uint8_t)px, MATRIX_HEIGHT - 1)] += CHSV(sHue, 255, poleV);
    }
  }

  // ============ DISC ============
  fill_solid(ledsDisc, LED_COUNT_DISC, CRGB::Black);
  for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
    float dx, dy, dr; uint8_t rIdx, pIdx;
    discIdxToXY(i, dx, dy, dr, rIdx, pIdx);
    float dxn = dx;          float dyn = dy - 1.0f;
    float dxs = dx;          float dys = dy + 1.0f;
    float rn = sqrtf(dxn * dxn + dyn * dyn) + 0.01f;
    float rs = sqrtf(dxs * dxs + dys * dys) + 0.01f;
    float fn = 1.0f / (1.0f + rn * 2.0f);
    float fs = 1.0f / (1.0f + rs * 2.0f);
    float total = fn + fs;
    float pol = (fn - fs) / (total + 0.001f);

    float strength = (total - 0.5f) * 1.6f;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;
    uint8_t v = (uint8_t)(strength * (float)maxV);
    v = scale8(v, globalScale);
    if (v == 0) continue;

    float absPol = pol < 0.0f ? -pol : pol;
    uint8_t hue = (pol >= 0.0f) ? nHue : sHue;
    uint8_t sat = (uint8_t)(60.0f + 195.0f * absPol);
    ledsDisc[i] = CHSV(hue, sat, v);
  }

  // ============ CLOUD (closed loop = bipolar bar magnet wrapped into a ring) ============
  // First half = N hemisphere (blue), second half = S hemisphere (red). The
  // "equator" sits at idx 0 and idx LED_COUNT_CLOUD/2.
  fill_solid(ledsCloud, LED_COUNT_CLOUD, CRGB::Black);
  {
    uint16_t half = LED_COUNT_CLOUD / 2;
    for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
      float pos01 = (float)i / (float)LED_COUNT_CLOUD;            // 0..1 around loop
      float pol = cosf(pos01 * 6.2831853f);                       // +1 at idx 0 (N), -1 at half (S)
      float absPol = pol < 0.0f ? -pol : pol;
      uint8_t hue = (pol >= 0.0f) ? nHue : sHue;
      uint8_t sat = (uint8_t)(60.0f + 195.0f * absPol);
      uint8_t v   = (uint8_t)(absPol * (float)maxV);
      v = scale8(v, globalScale);
      ledsCloud[i] = CHSV(hue, sat, v);
    }
    // Flux pulses slide from N-pole around through S and back.
    static float cloudFluxPos = -1.0f;
    static float cloudFluxSpeed = 0.0f;
    if (cloudFluxPos < 0.0f) {
      if (random8() < launchChance * 2) {
        cloudFluxPos = 0.0f;
        cloudFluxSpeed = 0.06f + 0.10f * (float)random8() / 255.0f;
      }
    } else {
      cloudFluxPos += cloudFluxSpeed * (float)dt;
      if (cloudFluxPos >= (float)LED_COUNT_CLOUD) { cloudFluxPos = -1.0f; }
      else {
        uint16_t pi = (uint16_t)cloudFluxPos;
        float pos01 = (float)pi / (float)LED_COUNT_CLOUD;
        float pol = cosf(pos01 * 6.2831853f);
        uint8_t hue = (pol >= 0.0f) ? nHue : sHue;
        uint8_t v   = scale8(maxV, globalScale);
        ledsCloud[pi] += CHSV(hue, 220, v);
        for (int8_t d = 1; d <= 3; d++) {
          uint16_t p = (pi + LED_COUNT_CLOUD - d) % LED_COUNT_CLOUD;
          ledsCloud[p] += CHSV(hue, 220, v >> (d + 1));
        }
      }
    }
    (void)half;
  }
}

void effectClouds(float pm){
      uint8_t cloudHue = 160;
      uint8_t cloudSat = map((int)(pmRatio(pm)*1000), 0, 1000, 0, 60);
      uint8_t speed = map((int)(pmRatio(pm)*1000), 0, 1000, 1, 6);
      cloudNoiseZ += speed;
      uint8_t cloudThreshold = map((int)(pmRatio(pm)*1000), 0, 1000, 180, 100);
      for(uint8_t y=0; y<MATRIX_HEIGHT; y++){
        for(uint8_t x=0; x<MATRIX_WIDTH; x++){
          uint8_t noise = inoise8(x*50, y*80, cloudNoiseZ);
          if(noise > cloudThreshold){
            ledsMatrix[XY(x, y)] = CHSV(cloudHue, cloudSat, map(noise, cloudThreshold, 255, 100, 230));
          } else {
            ledsMatrix[XY(x, y)] = CHSV(160, 180, 30);
          }
        }
      }

      for(uint16_t i=0; i<LED_COUNT_STRIP; i++) ledsStrip[i] = CHSV(cloudHue, cloudSat, inoise8(i*15, cloudNoiseZ));

      // CLOUD: literally a cloud — slow drifting 1D noise field with brighter
      // "puffs" above the threshold. Uses a separate noise offset so it doesn't
      // exactly mirror the strip.
      for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
        uint8_t noise = inoise8(i * 90, cloudNoiseZ + 4000);
        if (noise > cloudThreshold) {
          ledsCloud[i] = CHSV(cloudHue, cloudSat, map(noise, cloudThreshold, 255, 110, 240));
        } else {
          ledsCloud[i] = CHSV(160, 170, 28);
        }
      }

      // DISC: 2D cloud field sampled at each LED's (x,y), drifting sideways with cloudNoiseZ
      for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
        float dx, dy, dr; uint8_t rIdx, pIdx;
        discIdxToXY(i, dx, dy, dr, rIdx, pIdx);
        uint16_t nx = (uint16_t)((dx + 1.0f) * 2000.0f) + cloudNoiseZ;
        uint16_t ny = (uint16_t)((dy + 1.0f) * 2000.0f);
        uint8_t noise = inoise8(nx, ny, cloudNoiseZ / 2);
        if (noise > cloudThreshold) {
          ledsDisc[i] = CHSV(cloudHue, cloudSat, map(noise, cloudThreshold, 255, 100, 230));
        } else {
          ledsDisc[i] = CHSV(160, 180, 24);
        }
      }
    }

void effectLavaLamp(float pm){
      float ratio = pmRatio(pm);
      uint8_t pollution_level = (uint8_t)(ratio * 255);
      static uint16_t lavaTime = 0;
      uint8_t speed = map(pollution_level, 0, 255, 1, 5);
      lavaTime += speed;
      uint8_t lavaHue = map(pollution_level, 0, 255, 0, 32);
      
      
      for(uint16_t i = 0; i < LED_COUNT_STRIP; i++){
        uint8_t lava = inoise8(i * 30, lavaTime + 1000);
        uint8_t blob = (lava > 150) ? map(lava, 150, 255, 100, 255) : 0;
        ledsStrip[i] = CHSV(lavaHue + 5, 255, blob);
      }

      // CLOUD: independent slow blobs around the loop (different noise seed)
      for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
        uint8_t lava = inoise8(i * 120, lavaTime + 5500);
        uint8_t blob = (lava > 145) ? map(lava, 145, 255, 100, 255) : 0;
        ledsCloud[i] = CHSV(lavaHue + 8, 255, blob);
      }

      
      for(uint8_t y = 0; y < MATRIX_HEIGHT; y++){
        for(uint8_t x = 0; x < MATRIX_WIDTH; x++){
          uint8_t lava = inoise8(x * 60, y * 60, lavaTime);
          uint8_t blob = (lava > 140) ? map(lava, 140, 255, 80, 255) : 0;
          ledsMatrix[XY(x, y)] = CHSV(lavaHue, 255, blob);
        }
      }

      
      // DISC: big slow blobs rising through the disc radially (lava in a glass disc)
      for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
        float dx, dy, dr; uint8_t rIdx, pIdx;
        discIdxToXY(i, dx, dy, dr, rIdx, pIdx);
        uint16_t nx = (uint16_t)(dx * 2500.0f + 4000.0f);
        uint16_t ny = (uint16_t)(dy * 2500.0f + 4000.0f);
        uint8_t lava = inoise8(nx, ny, lavaTime + 2200);
        uint8_t blob = (lava > 145) ? map(lava, 145, 255, 90, 255) : 0;
        // bottom-heavy bias so blobs feel like they're rising
        if (dy > 0.0f) blob = scale8(blob, 180);
        ledsDisc[i] = CHSV(lavaHue + 3, 255, blob);
      }
    }

void effectStarfield(float pm) {
    float ratio = pmRatio(pm);
    uint8_t pollutionlevel = uint8_t(ratio * 255);
    
    static uint16_t spaceTime = 0;
    uint8_t driftSpeed = map(pollutionlevel, 0, 255, 1, 4);
    spaceTime += driftSpeed;
    
    
    uint8_t fadeAmount = map(pollutionlevel, 0, 255, 4, 18);


    fadeToBlackBy(ledsStrip, LED_COUNT_STRIP, fadeAmount);
    fadeToBlackBy(ledsMatrix, LED_COUNT_MATRIX, fadeAmount);
    fadeToBlackBy(ledsCloud, LED_COUNT_CLOUD, fadeAmount);

    // CLOUD: deep-sky background haze + sparse twinkles + occasional comet.
    for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
        uint8_t n = inoise8(i * 110, spaceTime / 3 + 9000);
        uint8_t base = qsub8(n, 140);
        uint8_t b = scale8(base, 22);
        ledsCloud[i] += CRGB(b, b, b);
    }
    if (random8(100) < 4) {
        uint16_t cpos = random16(LED_COUNT_CLOUD);
        uint8_t starB = random8(60, 160);
        ledsCloud[cpos] = CRGB(starB, starB, starB + 25);
    }


    for (uint16_t i = 0; i < LED_COUNT_STRIP; i++) {
        uint8_t n = inoise8(i * 25, spaceTime / 3);
        uint8_t base = qsub8(n, 140);
        uint8_t b = scale8(base, 20);
        ledsStrip[i] += CRGB(b, b, b);
    }
    
    static uint8_t meteorTimer = 0;
    meteorTimer++;
    
    if (meteorTimer > 60 && random8(100) < 3) {
        uint16_t meteorPos = random16(LED_COUNT_STRIP - 20);
        uint8_t meteorLength = random8(8, 16);
        
        for (uint8_t i = 0; i < meteorLength; i++) {
            if (meteorPos + i < LED_COUNT_STRIP) {
                uint8_t brightness = map(i, 0, meteorLength - 1, 180, 20);
                uint8_t hue = map(i, 0, meteorLength - 1, 40, 10);
                ledsStrip[meteorPos + i] = CHSV(hue, 200, brightness);
            }
        }
        meteorTimer = 0;
    }
    
    if (random8(100) < 2) {
        uint16_t pos = random16(LED_COUNT_STRIP);
        uint8_t starBrightness = random8(40, 100);
        ledsStrip[pos] = CRGB(starBrightness, starBrightness, starBrightness + 20);
    }
    
    
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
        for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
            int8_t distanceFromBand = abs((int8_t)x - (int8_t)y * 5);
            uint8_t starDensity = inoise8(x * 40, y * 60, spaceTime / 3);
            
            uint8_t bandInfluence = 255;
            if (distanceFromBand < 15) {
                bandInfluence = map(distanceFromBand, 0, 15, 255, 150);
            } else {
                bandInfluence = 100;
            }
            
            starDensity = scale8(starDensity, bandInfluence);
            
            if (starDensity > 210) {  
                uint8_t brightness = map(starDensity, 210, 255, 30, 100);
                uint8_t depthVariation = inoise8(x * 70, y * 70, spaceTime / 5);
                brightness = scale8(brightness, depthVariation);
                
                uint8_t colorShift = random8(5);
                if (colorShift == 0) {
                    ledsMatrix[XY(x, y)] = CHSV(200, 180, brightness);
                } else if (colorShift == 1) {
                    ledsMatrix[XY(x, y)] = CHSV(30, 160, brightness);
                } else {
                    ledsMatrix[XY(x, y)] = CRGB(brightness, brightness, brightness + 10);
                }
            } else if (starDensity > 160) {
                
                uint8_t brightness = map(starDensity, 160, 210, 5, 30);
                ledsMatrix[XY(x, y)] += CRGB(brightness, brightness, brightness + 5);
            }
            
            if (distanceFromBand < 10) {
                uint8_t galaxyGlow = inoise8(x * 20, y * 30, spaceTime);
                if (galaxyGlow > 200) {
                    uint8_t glow = map(galaxyGlow, 200, 255, 0, 40);
                    ledsMatrix[XY(x, y)] += CHSV(200, 200, glow);
                }
            }
            
            
            if (distanceFromBand < 10) {
                uint8_t hazeNoise = inoise8(x * 15, y * 25, spaceTime / 4);
                uint8_t haze = scale8(qsub8(hazeNoise, 130), 15);
                ledsMatrix[XY(x, y)] += CHSV(200, 80, haze);
            }
        }
    }
    
    
    static uint8_t satelliteAngle = 0;
    satelliteAngle += 2;
    
    for (uint8_t s = 0; s < 3; s++) {
        uint8_t angle = satelliteAngle + (s * 85);

        
        uint8_t blink = sin8((spaceTime / 3) + (s * 80));
        uint8_t brightness = map(blink, 0, 255, 60, 140);
        

    }
    
    
    if (random8(100) < 1) {
        uint8_t strip = random8(6);

        switch(strip) {
            case 0:

                break;
            case 1:

                break;
            case 2:
                ledsStrip[random16(LED_COUNT_STRIP)] = CRGB(random8(90, 130), random8(90, 120), random8(110, 150));
                break;
            case 3:
                ledsMatrix[random16(LED_COUNT_MATRIX)] = CRGB(random8(70, 110), random8(70, 100), random8(90, 130));
                break;
            case 4:

                break;
            case 5:
                ledsDisc[random16(LED_COUNT_DISC)] = CRGB(random8(90, 140), random8(90, 130), random8(110, 160));
                break;
        }
    }

    // DISC: sparse starfield with a faint Milky Way band along horizontal axis
    fadeToBlackBy(ledsDisc, LED_COUNT_DISC, fadeAmount);
    for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
        float dx, dy, dr; uint8_t rIdx, pIdx;
        discIdxToXY(i, dx, dy, dr, rIdx, pIdx);
        uint8_t starN = inoise8(rIdx * 45 + pIdx * 23, spaceTime / 3);
        if (starN > 220) {
            uint8_t twinkle = sin8((spaceTime / 5) + i * 13);
            uint8_t b = scale8(map(starN, 220, 255, 40, 120), twinkle);
            uint8_t pick = random8(10);
            if (pick == 0) ledsDisc[i] = CHSV(10, 180, b);
            else if (pick < 3) ledsDisc[i] = CHSV(160, 200, b);
            else ledsDisc[i] = CRGB(b, b, b + 12);
        }
        // faint band along horizontal axis (|dy| small)
        float absDy = dy < 0.0f ? -dy : dy;
        if (absDy < 0.25f) {
            uint8_t bandNoise = inoise8(i * 30, spaceTime);
            if (bandNoise > 200) {
                uint8_t bv = map(bandNoise, 200, 255, 0, 35);
                ledsDisc[i] += CHSV(200, 200, bv);
            }
        }
    }
    // occasional bright flare at a random point
    if (random8(100) < 3) {
        uint16_t p = random16(LED_COUNT_DISC);
        ledsDisc[p] = CRGB(200, 200, 220);
    }
}

// Mode 27 — "Lissajous"
//
// A proper Lissajous figure that lives across the whole rig.
//   Matrix:  the figure itself, drawn by 3 chromatically-offset pens (RGB-ish)
//            with a long phosphor trail. The trace literally sweeps the curve.
//   Disc:    the SAME (x,y) pen positions reprojected into polar (r, θ),
//            so the figure inscribes itself onto the concentric rings — radius
//            = distance from origin, ring 0 = centre, ring 7 = outer rim.
//            sin(a·t) — the horizontal "input" to the figure.
//   Strip:   the Y driver as a standing waveform, brightness-capped because
//            300 LEDs at full surge would brown the rail out.
//
// The integer ratio (a, b) drifts slowly through a curated table of pairs that
// produce visibly closed figures — you actually see the curve evolve from a
// vertical loop to a ribbon to a more complex weave over ~15-30 seconds.
void effectLissajousScope(float pm) {
    float   ratio  = pmRatio(pm);
    uint8_t pmNorm = (uint8_t)(ratio * 255.0f);

    static uint16_t t      = 0;
    static uint16_t morph  = 0;
    static uint8_t  ratIdx = 0;
    static uint8_t  hue    = 0;

    uint16_t spd = 100 + scale8(pmNorm, 400);
    t     += spd;
    morph += 1;
    hue   += 1 + (pmNorm >> 6);

    // Portrait-tuned (8×40): b >= 3a so Y motion fills the tall axis.
    // c is the disc's "depth" oscillator — gives the disc a different 3D slice.
    static const uint8_t kRat[6][3] = {
        {1, 3, 2},
        {1, 5, 3},
        {2, 5, 3},
        {1, 7, 4},
        {2, 7, 5},
        {3, 5, 2},
    };
    if (morph >= 1500) { morph = 0; ratIdx = (ratIdx + 1) % 6; }
    uint8_t a = kRat[ratIdx][0];
    uint8_t b = kRat[ratIdx][1];
    uint8_t c = kRat[ratIdx][2];

    // Low PM = long dreamy trail; high PM = short agitated trail
    uint8_t fade = 8 + scale8(pmNorm, 20);

    // ── Matrix: (X,Y) projection, 3 chromatic pens 120° apart ──────────────
    fadeToBlackBy(ledsMatrix, LED_COUNT_MATRIX, fade);
    {
        int16_t cx   = (MATRIX_WIDTH  - 1) / 2;
        int16_t cy   = (MATRIX_HEIGHT - 1) / 2;
        int16_t ampX = (MATRIX_WIDTH  - 1) / 2;
        int16_t ampY = (MATRIX_HEIGHT - 1) / 2;

        static const uint16_t kPh[3]  = { 0, 21845, 43690 };   // 0°, 120°, 240°
        static const uint8_t  kHue[3] = { 0, 96, 160 };

        for (uint8_t p = 0; p < 3; p++) {
            for (uint8_t s = 0; s < 4; s++) {
                uint16_t ti = t + kPh[p] + (uint16_t)s * (spd >> 2);
                int16_t sx  = sin16((uint16_t)(a * ti));
                int16_t sy  = sin16((uint16_t)(b * ti + 0x4000));
                int16_t x   = cx + (int16_t)(((int32_t)sx * ampX) >> 15);
                int16_t y   = cy + (int16_t)(((int32_t)sy * ampY) >> 15);
                if (x >= 0 && x < MATRIX_WIDTH && y >= 0 && y < MATRIX_HEIGHT)
                    ledsMatrix[XY((uint8_t)x, (uint8_t)y)] += CHSV(kHue[p] + hue, 220, 150);
            }
        }
    }

    // ── Disc: (X,Z) projection → polar — orthogonal 3D slice to the matrix ─
    // The matrix shows (X,Y); the disc shows (X,Z) of the same 3D figure.
    // Y-value at each sample point cross-couples into the disc hue (depth cue).
    fadeToBlackBy(ledsDisc, LED_COUNT_DISC, fade + 10);
    if (LED_COUNT_DISC > 0) {
        for (uint8_t s = 0; s < 8; s++) {
            uint16_t ti = t + (uint16_t)s * 8192;
            float fx = (float)sin16((uint16_t)(a * ti)) / 32768.0f;
            float fz = (float)sin16((uint16_t)(c * ti + 0x4000)) / 32768.0f;
            float fr = sqrtf(fx * fx + fz * fz);
            if (fr > 1.0f) fr = 1.0f;
            uint8_t ring = (uint8_t)(fr * (float)(DISC_RING_COUNT - 1) + 0.5f);
            if (ring >= DISC_RING_COUNT) ring = DISC_RING_COUNT - 1;
            float   ang  = atan2f(fz, fx);
            if (ang < 0.0f) ang += 6.2831853f;
            uint8_t rLen = DISC_RING_LENS[ring];
            if (rLen == 0) continue;
            uint8_t pos = (uint8_t)((ang / 6.2831853f) * (float)rLen);
            if (pos >= rLen) pos = rLen - 1;
            float   fy   = (float)sin16((uint16_t)(b * ti + 0x4000)) / 32768.0f;
            uint8_t dHue = hue + (uint8_t)((fy + 1.0f) * 64.0f);
            ledsDisc[DISC_RING_OFFSETS[ring] + pos] += CHSV(dHue, 240, 160);
        }
    }

    // ── Strip: Y oscillator wave, brightness capped for 300 LEDs ────────────
    if (LED_COUNT_STRIP > 0) {
        uint8_t t8 = (uint8_t)(t >> 7);
        for (uint16_t i = 0; i < LED_COUNT_STRIP; i++) {
            uint8_t v = scale8(sin8((uint8_t)(b * (uint8_t)(i * 3) + t8 + 64)), 35);
            ledsStrip[i] = CHSV(hue + 64, 230, v);
        }
    }

    // ── CLOUD: figure traced around the loop using parametric (a,b) ratio.
    //   The 50-LED loop is the "phase wheel"; pens at 3 hues leave dots whose
    //   brightness peaks when the Lissajous sample lines up with that idx.
    fadeToBlackBy(ledsCloud, LED_COUNT_CLOUD, fade);
    if (LED_COUNT_CLOUD > 0) {
        for (uint8_t s = 0; s < 6; s++) {
            uint16_t ti = t + (uint16_t)s * 5461;  // ~60deg apart
            float sx = (float)sin16((uint16_t)(a * ti)) / 32768.0f;
            float sy = (float)sin16((uint16_t)(b * ti + 0x4000)) / 32768.0f;
            float ang = atan2f(sy, sx);
            if (ang < 0.0f) ang += 6.2831853f;
            uint16_t idx = (uint16_t)((ang / 6.2831853f) * (float)LED_COUNT_CLOUD);
            if (idx >= LED_COUNT_CLOUD) idx = LED_COUNT_CLOUD - 1;
            uint8_t penHue = hue + (uint8_t)(s * 42);
            ledsCloud[idx] += CHSV(penHue, 230, 180);
        }
    }
}

void effectConvectionCurrents(float pm) {
    float ratio = pmRatio(pm);
    uint8_t pmNorm = (uint8_t)(ratio * 255);

    struct FlowParticle {
        int16_t x;
        int16_t y;
        int8_t vx;
        int8_t vy;
    };

    const uint8_t MAX_PARTICLES = 60;
    static FlowParticle particles[MAX_PARTICLES];
    static bool initialized = false;
    static uint16_t flowZ = 0;

    if (!initialized) {
        for (uint8_t i = 0; i < MAX_PARTICLES; i++) {
            particles[i].x = (int16_t)(random8(MATRIX_WIDTH) << 8);
            particles[i].y = (int16_t)(random8(MATRIX_HEIGHT) << 8);
            particles[i].vx = 0;
            particles[i].vy = 0;
        }
        initialized = true;
    }

    uint8_t speed = 8 + (pmNorm >> 4);
    uint8_t turb = 10 + (pmNorm >> 3);
    uint8_t noiseScale = 18 - (pmNorm >> 6);
    uint8_t decay = 20 + scale8(255 - pmNorm, 35);
    uint8_t count = 20 + (pmNorm >> 3);
    if (count > MAX_PARTICLES) count = MAX_PARTICLES;

    fadeToBlackBy(ledsMatrix, LED_COUNT_MATRIX, decay);

    for (uint8_t i = 0; i < count; i++) {
        uint8_t px = (uint8_t)(particles[i].x >> 8);
        uint8_t py = (uint8_t)(particles[i].y >> 8);
        uint8_t n = inoise8(px * noiseScale, py * noiseScale, flowZ);

        int16_t dx = (int16_t)cos8(n) - 128;
        int16_t dy = (int16_t)sin8(n) - 128;

        int16_t vx = particles[i].vx;
        int16_t vy = particles[i].vy;
        vx = (vx * 7 + ((dx * turb) >> 7)) >> 3;
        vy = (vy * 7 + ((dy * turb) >> 7)) >> 3;
        if (vx > 127) vx = 127;
        if (vx < -127) vx = -127;
        if (vy > 127) vy = 127;
        if (vy < -127) vy = -127;
        particles[i].vx = (int8_t)vx;
        particles[i].vy = (int8_t)vy;

        particles[i].x += (int16_t)(vx * speed);
        particles[i].y += (int16_t)(vy * speed);

        int16_t maxX = (int16_t)(MATRIX_WIDTH << 8);
        int16_t maxY = (int16_t)(MATRIX_HEIGHT << 8);
        while (particles[i].x < 0) particles[i].x += maxX;
        while (particles[i].x >= maxX) particles[i].x -= maxX;
        while (particles[i].y < 0) particles[i].y += maxY;
        while (particles[i].y >= maxY) particles[i].y -= maxY;

        px = (uint8_t)(particles[i].x >> 8);
        py = (uint8_t)(particles[i].y >> 8);

        uint16_t mag = (uint16_t)abs(vx) + (uint16_t)abs(vy);
        if (mag > 255) mag = 255;
        uint8_t brightness = scale8((uint8_t)mag, 200);
        brightness = qadd8(brightness, 30);

        uint8_t hue = lerp8by8(160, 0, pmNorm) + (n >> 3);
        ledsMatrix[XY(px, py)] += CHSV(hue, 200, brightness);

        if (pmNorm > 160) {
            uint8_t glow = brightness >> 2;
            if (px + 1 < MATRIX_WIDTH) ledsMatrix[XY(px + 1, py)] += CHSV(hue, 180, glow);
            if (py + 1 < MATRIX_HEIGHT) ledsMatrix[XY(px, py + 1)] += CHSV(hue, 180, glow);
        }
    }

    flowZ += 1 + (pmNorm >> 6);

    uint8_t spatialScale = 20 - (pmNorm >> 5);
    if (spatialScale < 6) spatialScale = 6;
    uint8_t blurAmount = 80 - (pmNorm >> 2);
    if (blurAmount < 10) blurAmount = 10;

    uint8_t baseHue = lerp8by8(160, 0, pmNorm);
    uint8_t maxV = 80 + (pmNorm >> 1);


    if (LED_COUNT_STRIP > 0) {
        for (uint16_t i = 0; i < LED_COUNT_STRIP; i++) {
            uint8_t v = inoise8(i * spatialScale, flowZ + 2500);
            ledsStrip[i] = CHSV(baseHue + (v >> 3), 200, scale8(v, maxV));
        }
        blur1d(ledsStrip, LED_COUNT_STRIP, blurAmount);
    }


    // DISC: 2D noise field sampled at disc (x,y), center-outward temperature gradient
    fadeToBlackBy(ledsDisc, LED_COUNT_DISC, decay);
    for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
        float dx, dy, dr; uint8_t rIdx, pIdx;
        discIdxToXY(i, dx, dy, dr, rIdx, pIdx);
        uint16_t nx = (uint16_t)((dx + 1.0f) * 1800.0f);
        uint16_t ny = (uint16_t)((dy + 1.0f) * 1800.0f);
        uint8_t n = inoise8(nx, ny, flowZ);
        // convection: hot rises from center, cold sinks outside
        uint8_t radialBias = (uint8_t)(255 - rIdx * 30);
        uint8_t v = scale8(n, radialBias);
        uint8_t hue = lerp8by8(160, 0, pmNorm) + (n >> 3) + rIdx * 3;
        ledsDisc[i] += CHSV(hue, 200, scale8(v, maxV));
    }

    // CLOUD: 1D flow field — independent noise stream, hot/cold gradient.
    if (LED_COUNT_CLOUD > 0) {
        for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
            uint8_t n = inoise8(i * spatialScale * 2, flowZ + 7500);
            uint8_t hue = baseHue + (n >> 3);
            ledsCloud[i] = CHSV(hue, 200, scale8(n, maxV));
        }
        blur1d(ledsCloud, LED_COUNT_CLOUD, blurAmount);
    }
}


void effectBlackHole(float pm){
  float ratio = pmRatio(pm);
  uint8_t pollution = (uint8_t)(ratio * 255.0f);

  static uint16_t phase = 0;
  static uint16_t phaseSlow = 0;

  uint8_t speed = (uint8_t)map(pollution, 0, 255, 2, 10);
  phase += speed;
  phaseSlow += 1 + (speed >> 2);


  const uint8_t diskHue = 200;
  const uint8_t lensHue = 200;
  const uint8_t jetHue  = 216;
  uint8_t satBase = (uint8_t)map(pollution, 0, 255, 150, 230);
  uint8_t shudderAmp = (uint8_t)map(pollution, 0, 255, 0, 45);
  uint8_t jitterAmp = (uint8_t)map(pollution, 0, 255, 4, 22);
  uint8_t pullAmp = (uint8_t)map(pollution, 0, 255, 12, 90);
  int16_t globalShake = (int16_t)sin8((uint8_t)(phaseSlow * 3)) - 128;
  globalShake = (globalShake * shudderAmp) / 128;

  uint8_t pullAmpStrip = pullAmp;

  auto matrixAllowed = [](uint16_t idx) -> bool {
    if (idx <= 98) return true;
    if (idx <= 103) return false;
    if (idx >= 225 && idx <= 226) return true;
    if (idx >= 232) return true;
    return false;
  };

  struct InfallStar1D {
    float pos;
    float speed;
    uint8_t hue;
    uint8_t sat;
    uint8_t val;
  };

  struct InfallStar2D {
    float x;
    float y;
    float speed;
    int8_t spin;
    uint8_t hue;
    uint8_t sat;
    uint8_t val;
  };


  static const uint8_t STRIP_STAR_COUNT = 4;

  static const uint8_t MATRIX_STAR_COUNT = 10;


  static InfallStar1D stripStars[STRIP_STAR_COUNT];

  static InfallStar2D matrixStars[MATRIX_STAR_COUNT];
  static bool stars1DInit = false;
  static bool stars2DInit = false;

  uint8_t starBoost = (uint8_t)map(pollution, 0, 255, 150, 255);
  uint8_t spaceDim = (uint8_t)map(pollution, 0, 255, 70, 110);
  uint8_t spaceCut = (uint8_t)map(pollution, 0, 255, 20, 12);

  auto pickStarColor = [&](uint8_t &h, uint8_t &s, uint8_t &v) {
    uint8_t roll = random8(20);
    if (roll == 0) {
      h = (uint8_t)(18 + random8(18)); s = 120;       // rare orange/gold giant
    } else if (roll <= 2) {
      h = random8(10); s = 200;                        // red giant
    } else if (roll <= 5) {
      h = (uint8_t)(195 + random8(25)); s = 160;      // violet/purple star
    } else {
      h = (uint8_t)(155 + random8(55)); s = (uint8_t)(40 + random8(60));  // blue-white
    }
    v = (uint8_t)(130 + random8(110));
  };

  auto spawnStar1D = [&](InfallStar1D &s, uint16_t count) {
    if (count < 2) return;
    bool fromLeft = (random8() & 0x01) != 0;
    s.pos = fromLeft ? 0.0f : (float)(count - 1);
    s.speed = 0.6f + ((float)random8(80) / 100.0f);
    pickStarColor(s.hue, s.sat, s.val);
  };

  if (!stars1DInit) {

    for (uint8_t i = 0; i < STRIP_STAR_COUNT; i++) spawnStar1D(stripStars[i], LED_COUNT_STRIP);

    stars1DInit = true;
  }

  uint8_t ringVBase = (uint8_t)map(pollution, 0, 255, 210, 255);
  uint8_t ringJitterAmp = (uint8_t)map(pollution, 0, 255, 2, 10);

  uint8_t ropeBase = (uint8_t)map(pollution, 0, 255, 20, 90);
  uint8_t ropeBoost = (uint8_t)map(pollution, 0, 255, 30, 120);


  // STRIP: faint polar jets
  if (LED_COUNT_STRIP > 0) {
    fadeToBlackBy(ledsStrip, LED_COUNT_STRIP, 40);
    uint16_t stripCenter = LED_COUNT_STRIP / 2;
    uint8_t stripBase = (uint8_t)map(pollution, 0, 255, 4, 30);
    uint8_t stripSat = qsub8(satBase, 30);
    for (uint16_t i = 0; i < LED_COUNT_STRIP; i++) {
      uint16_t d = (i > stripCenter) ? (i - stripCenter) : (stripCenter - i);
      uint8_t dist8 = (stripCenter > 0) ? (uint8_t)map(d, 0, stripCenter, 0, 255) : 0;
      uint8_t pullWave = sin8((uint8_t)(phase + dist8 * 2));
      uint8_t tremor = scale8(inoise8((uint16_t)(i * 11), phaseSlow), jitterAmp);
      uint8_t v = qadd8(stripBase, scale8(pullWave, pullAmpStrip));
      v = qadd8(v, tremor);
      v = scale8(v, spaceDim);
      if (v > spaceCut) {
        ledsStrip[i] += CHSV(jetHue, stripSat, v);
      }
    }
    uint16_t jetPos = (uint16_t)((phaseSlow >> 1) % LED_COUNT_STRIP);
    uint16_t jetOpp = (uint16_t)((jetPos + (LED_COUNT_STRIP / 2)) % LED_COUNT_STRIP);
    uint8_t jetV = (uint8_t)map(pollution, 0, 255, 70, 200);

    ledsStrip[jetPos] += CHSV(jetHue, 200, jetV);
    ledsStrip[jetOpp] += CHSV(jetHue, 200, jetV);

    for (uint8_t t = 1; t <= 2; t++) {
      uint16_t l = (uint16_t)((jetPos + LED_COUNT_STRIP - t) % LED_COUNT_STRIP);
      uint16_t r = (uint16_t)((jetPos + t) % LED_COUNT_STRIP);
      uint8_t v = jetV >> t;
      ledsStrip[l] += CHSV(jetHue, 200, v);
      ledsStrip[r] += CHSV(jetHue, 200, v);

      uint16_t lo = (uint16_t)((jetOpp + LED_COUNT_STRIP - t) % LED_COUNT_STRIP);
      uint16_t ro = (uint16_t)((jetOpp + t) % LED_COUNT_STRIP);
      ledsStrip[lo] += CHSV(jetHue, 200, v);
      ledsStrip[ro] += CHSV(jetHue, 200, v);
    }

    if (LED_COUNT_STRIP > 1) {
      const float centerF = (float)stripCenter;
      const float stepBase = 0.07f + 0.20f * ratio;
      for (uint8_t s = 0; s < STRIP_STAR_COUNT; s++) {
        InfallStar1D &st = stripStars[s];
        if (st.pos < centerF) st.pos += stepBase * st.speed;
        else st.pos -= stepBase * st.speed;

        if (st.pos < -1.0f || st.pos > (float)LED_COUNT_STRIP ||
            (st.pos > (centerF - 0.8f) && st.pos < (centerF + 0.8f))) {
          spawnStar1D(st, LED_COUNT_STRIP);
          continue;
        }

        int16_t idx = (int16_t)(st.pos + 0.5f);
        if (idx < 0 || idx >= (int16_t)LED_COUNT_STRIP) {
          spawnStar1D(st, LED_COUNT_STRIP);
          continue;
        }

        uint8_t tw = sin8((uint8_t)(phase + s * 31 + idx * 5));
        uint8_t v = scale8(st.val, starBoost);
        v = scale8(v, (uint8_t)(170 + (tw >> 2)));
        ledsStrip[(uint16_t)idx] += CHSV(st.hue, st.sat, v);

        int16_t tailIdx = (st.pos < centerF) ? (idx - 1) : (idx + 1);
        if (tailIdx >= 0 && tailIdx < (int16_t)LED_COUNT_STRIP) {
          ledsStrip[(uint16_t)tailIdx] += CHSV(st.hue, st.sat, v >> 2);
        }
      }
    }
  }

  // MATRIX: matter streaming upward toward the disc above.
  // Attractor placed above the top edge — stars infall and exit upward.
  const float centerXf = (float)(MATRIX_WIDTH / 2);
  const float centerYf = -10.0f;   // above the matrix, in the direction of the disc

  uint8_t matrixBase  = (uint8_t)map(pollution, 0, 255, 5, 35);
  uint8_t matrixBoost = (uint8_t)map(pollution, 0, 255, 25, 100);

  for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
      uint16_t idx = XY(x, y);
      if (!matrixAllowed(idx)) { ledsMatrix[idx] = CRGB::Black; continue; }

      // Bright at top (y=0, closest to disc), dim at bottom
      uint8_t topness    = (uint8_t)map(y, 0, MATRIX_HEIGHT - 1, 240, 28);
      int16_t swirlPhase = (int16_t)phase * 2 + (int16_t)x * 29 - (int16_t)y * 7;
      uint8_t stream     = sin8((uint8_t)swirlPhase);
      uint8_t noise      = inoise8((uint16_t)(x * 24), (uint16_t)(y * 24), phaseSlow);
      uint8_t v = qadd8(matrixBase, scale8(qadd8(stream >> 1, noise >> 1), matrixBoost));
      v = scale8(v, topness);
      v = qadd8(v, scale8(inoise8((uint16_t)(x * 40), (uint16_t)(y * 40), phase), jitterAmp));
      v = scale8(v, spaceDim);
      if (v <= spaceCut) { ledsMatrix[idx] = CRGB::Black; continue; }
      ledsMatrix[idx] = CHSV(diskHue, satBase, v);
    }
  }

  // Stars spawn from the bottom half and infall upward toward the disc.
  auto spawnStar2D = [&](InfallStar2D &s) {
    for (uint8_t tries = 0; tries < 8; tries++) {
      float x = (float)random8(MATRIX_WIDTH);
      float y = (float)(MATRIX_HEIGHT - 1 - random8(MATRIX_HEIGHT / 2));
      int16_t ix = (int16_t)(x + 0.5f);
      int16_t iy = (int16_t)(y + 0.5f);
      if (ix >= 0 && ix < (int16_t)MATRIX_WIDTH && iy >= 0 && iy < (int16_t)MATRIX_HEIGHT) {
        uint16_t idx = XY((uint8_t)ix, (uint8_t)iy);
        if (matrixAllowed(idx)) {
          s.x = x; s.y = y;
          s.speed = 0.5f + (float)random8(70) / 100.0f;
          s.spin  = (random8() & 0x01) ? 1 : -1;
          pickStarColor(s.hue, s.sat, s.val);
          return;
        }
      }
    }
    s.x = (float)random8(MATRIX_WIDTH);
    s.y = (float)(MATRIX_HEIGHT - 1);
    s.speed = 0.5f + (float)random8(70) / 100.0f;
    s.spin  = (random8() & 0x01) ? 1 : -1;
    pickStarColor(s.hue, s.sat, s.val);
  };

  if (!stars2DInit) {
    for (uint8_t i = 0; i < MATRIX_STAR_COUNT; i++) spawnStar2D(matrixStars[i]);
    stars2DInit = true;
  }

  const float pullBase  = 0.04f + 0.09f * ratio;
  const float swirlBase = 0.015f + 0.03f * ratio;

  for (uint8_t i = 0; i < MATRIX_STAR_COUNT; i++) {
    InfallStar2D &s = matrixStars[i];

    if (s.x < -1.0f || s.x > (float)MATRIX_WIDTH || s.y > (float)MATRIX_HEIGHT) {
      spawnStar2D(s);
      continue;
    }

    float dx   = s.x - centerXf;
    float dy   = s.y - centerYf;   // centerYf=-10, so dy always positive → pulls up
    float dist = sqrtf(dx * dx + dy * dy) + 0.001f;
    float pull  = pullBase  * s.speed;
    float swirl = swirlBase * s.speed * (float)s.spin;

    s.x += (-dx / dist) * pull + (-dy / dist) * swirl;
    s.y += (-dy / dist) * pull + ( dx / dist) * swirl;

    // Exited top toward disc → respawn at bottom
    if (s.y < 0.0f) { spawnStar2D(s); continue; }

    int16_t ix = (int16_t)(s.x + 0.5f);
    int16_t iy = (int16_t)(s.y + 0.5f);
    if (ix < 0 || ix >= (int16_t)MATRIX_WIDTH || iy < 0 || iy >= (int16_t)MATRIX_HEIGHT) {
      spawnStar2D(s); continue;
    }

    uint16_t idx = XY((uint8_t)ix, (uint8_t)iy);
    if (!matrixAllowed(idx)) { spawnStar2D(s); continue; }

    uint8_t tw = sin8((uint8_t)(phaseSlow + i * 31));
    uint8_t v  = scale8(s.val, starBoost);
    v = scale8(v, (uint8_t)(170 + (tw >> 2)));
    ledsMatrix[idx] += CHSV(s.hue, s.sat, v);

    // Tail: one step behind, toward the bottom (away from disc)
    float tailX = s.x + (dx / dist) * 0.8f;
    float tailY = s.y + (dy / dist) * 0.8f;
    int16_t tx  = (int16_t)(tailX + 0.5f);
    int16_t ty  = (int16_t)(tailY + 0.5f);
    if (tx >= 0 && tx < (int16_t)MATRIX_WIDTH && ty >= 0 && ty < (int16_t)MATRIX_HEIGHT) {
      uint16_t tidx = XY((uint8_t)tx, (uint8_t)ty);
      if (matrixAllowed(tidx)) ledsMatrix[tidx] += CHSV(s.hue, s.sat, v >> 2);
    }
  }



  // ── DISC ────────────────────────────────────────────────────────────────
  // Top-down view of the singularity.
  //   Rings 0..3 (1+8+12+16 = 37 LEDs) → DEAD ZONE / event horizon.
  //                                       Forced to black every frame, no
  //                                       exceptions — nothing else in this
  //                                       effect is allowed to write here.
  //   Ring  4   (24 LEDs)              → ACCRETION DISK. Bright, spinning,
  //                                       Doppler-coloured (blueshift on the
  //                                       approaching limb, redshift on the
  //                                       receding limb).
  //   Rings 5..7 (32+40+48 = 120 LEDs) → outer dust halo, dim, slow swirl.

  // Pre-clear event horizon BEFORE the loop — belt, braces, and a padlock.
  for (uint8_t r = 0; r <= BLACKHOLE_DEAD_RING_MAX; r++) {
    uint8_t  len = DISC_RING_LENS[r];
    uint16_t off = DISC_RING_OFFSETS[r];
    for (uint8_t k = 0; k < len; k++) ledsDisc[off + k] = CRGB::Black;
  }

  uint8_t accretionSpin   = (uint8_t)(phase >> 1);
  uint8_t accretionHotV   = (uint8_t)map(pollution, 0, 255, 200, 255);
  uint8_t accretionBaseV  = (uint8_t)map(pollution, 0, 255, 110, 160);
  uint8_t accretionRedHue = 192;    // violet on one limb
  uint8_t accretionBluHue = 220;    // magenta-purple on the other limb

  for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
    uint8_t rIdx, pIdx;
    discIdxToRing(i, rIdx, pIdx);

    // Dead zone — never light, regardless of any other state.
    if (rIdx <= BLACKHOLE_DEAD_RING_MAX) {
      ledsDisc[i] = CRGB::Black;
      continue;
    }

    uint8_t ringLen = DISC_RING_LENS[rIdx];
    uint8_t ang     = (uint8_t)((uint16_t)pIdx * 256U / ringLen);

    // Accretion disk on ring 4.
    if (rIdx == BLACKHOLE_ACCRETION_RING) {
      uint8_t spunAng = (uint8_t)(ang + accretionSpin);
      uint8_t doppler = sin8(spunAng);                           // 0..255
      uint8_t v       = (uint8_t)lerp8by8(accretionBaseV, accretionHotV, doppler);
      uint8_t flicker = scale8(inoise8(pIdx * 36, phase), 25);
      v = qadd8(v, flicker);
      uint8_t hue   = (uint8_t)lerp8by8(accretionRedHue, accretionBluHue, doppler);
      uint8_t spark = (inoise8((uint16_t)(pIdx * 47u), (uint16_t)(phase * 3u)) > 240) ? 40 : 0;
      v = qadd8(v, spark);
      uint8_t aSat  = spark ? 180 : 255;  // hot sparks slightly desaturated (whiter)
      ledsDisc[i] = CHSV(hue, aSat, v);
      continue;
    }

    // Outer halo (rings 5..7). Slow counter-spin, dimmer per ring out.
    int16_t flow   = (int16_t)((rIdx & 1) ? phase : -phase)
                   * (int16_t)(DISC_RING_COUNT - rIdx);
    uint8_t phased = (uint8_t)((int16_t)ang + flow);
    uint8_t stream = sin8(phased);
    uint8_t n      = inoise8(rIdx * 60 + pIdx * 18, phaseSlow);
    uint8_t falloff = (uint8_t)(220 - (rIdx - BLACKHOLE_ACCRETION_RING - 1) * 60);
    uint8_t base   = (uint8_t)map(pollution, 0, 255, 6, 30);
    uint8_t boost  = (uint8_t)map(pollution, 0, 255, 30, 100);
    uint8_t v = qadd8(base, scale8(qadd8(stream, n >> 1), boost));
    v = scale8(v, falloff);
    v = scale8(v, spaceDim);
    if (v <= spaceCut) {
      ledsDisc[i] = CRGB::Black;
      continue;
    }
    ledsDisc[i] = CHSV(lensHue, satBase, v);
  }

  // Belt-and-braces: re-blank the dead rings AFTER everything else, so any
  // future overlay (star particles, etc.) cannot accidentally bleed in.
  for (uint8_t r = 0; r <= BLACKHOLE_DEAD_RING_MAX; r++) {
    uint8_t  len = DISC_RING_LENS[r];
    uint16_t off = DISC_RING_OFFSETS[r];
    for (uint8_t k = 0; k < len; k++) ledsDisc[off + k] = CRGB::Black;
  }

  // CLOUD: faint accretion "halo" — slow swirl around the loop. No dead zone
  // because the cloud is the outer rim of the system.
  if (LED_COUNT_CLOUD > 0) {
    fadeToBlackBy(ledsCloud, LED_COUNT_CLOUD, 40);
    uint8_t spin = (uint8_t)(phase >> 1);
    for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
      uint8_t ang = (uint8_t)((uint32_t)i * 256u / LED_COUNT_CLOUD);
      uint8_t spunAng = (uint8_t)(ang + spin);
      uint8_t doppler = sin8(spunAng);
      uint8_t v = (uint8_t)lerp8by8(60, 200, doppler);
      v = scale8(v, ringVBase);
      uint8_t hue = (uint8_t)lerp8by8(192, 220, doppler);
      ledsCloud[i] = CHSV(hue, satBase, v);
    }
    // Bright "jet" spots opposite each other to echo the strip jets.
    uint16_t jetA = (uint16_t)((phaseSlow >> 1) % LED_COUNT_CLOUD);
    uint16_t jetB = (jetA + LED_COUNT_CLOUD / 2) % LED_COUNT_CLOUD;
    uint8_t jetV = (uint8_t)map(pollution, 0, 255, 90, 220);
    ledsCloud[jetA] += CHSV(216, 200, jetV);
    ledsCloud[jetB] += CHSV(216, 200, jetV);
  }
}

void effectPulsar(float pm){
      float ratio = pmRatio(pm);
      uint8_t pollution_level = (uint8_t)(ratio * 255);
      static uint32_t pulsarCycle = 0;
      uint16_t pulsarSpeed = map(pollution_level, 0, 255, 500, 50);
      uint32_t now = nowMs();
      static uint32_t lastPulse = 0;
      if(now - lastPulse > pulsarSpeed){
        lastPulse = now;
        pulsarCycle++;
      }
      uint32_t timeSincePulse = now - lastPulse;
      uint8_t pulsePhase = map(timeSincePulse, 0, pulsarSpeed, 0, 255);
      uint8_t pulseBrightness = 255 - pulsePhase;
      uint8_t pulseHue = map(pulsePhase, 0, 255, 0, 160);
      uint8_t pulseSat = map(pulsePhase, 0, 255, 0, 255);

      fadeToBlackBy(ledsStrip, LED_COUNT_STRIP, 40);
      if(pulsePhase < 64){
        uint16_t beamPos = map(pulsarCycle % 100, 0, 99, 0, LED_COUNT_STRIP - 20);
        for(uint16_t i = 0; i < 20; i++){
          if(beamPos + i < LED_COUNT_STRIP) ledsStrip[beamPos + i] = CHSV(pulseHue, pulseSat, pulseBrightness);
        }
      }
      const uint8_t cxM = MATRIX_WIDTH  / 2;
      const uint8_t cyM = MATRIX_HEIGHT / 2;
      const uint8_t maxDM = cxM + cyM; // max Manhattan distance from centre to a corner
      for(uint8_t y = 0; y < MATRIX_HEIGHT; y++){
        for(uint8_t x = 0; x < MATRIX_WIDTH; x++){
          uint8_t distance = (uint8_t)(abs((int)x - (int)cxM) + abs((int)y - (int)cyM));
          uint8_t radiation = (distance < maxDM)
                                ? (uint8_t)map(maxDM - distance, 0, maxDM, 0, pulseBrightness)
                                : 0;
          ledsMatrix[XY(x, y)] = CHSV(pulseHue, pulseSat, radiation);
        }
      }
    

      // DISC: true radial pulse - center fires first, each outer ring delayed more
      for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
        uint8_t rIdx, pIdx;
        discIdxToRing(i, rIdx, pIdx);
        uint8_t delay = rIdx * 28;
        uint8_t delayedPhase = (pulsePhase > delay) ? (pulsePhase - delay) : 0;
        uint8_t brightness = 255 - delayedPhase;
        // angular beam sweep - brighter on the "lighthouse" side
        uint8_t angle = discAngle8(rIdx, pIdx);
        uint8_t beam = sin8((uint8_t)(angle + pulsarCycle * 8));
        brightness = scale8(brightness, qadd8(128, beam >> 1));
        ledsDisc[i] = CHSV(pulseHue, pulseSat, brightness);
      }

      // CLOUD: lighthouse beam sweeps the loop; pulse halo around it.
      fadeToBlackBy(ledsCloud, LED_COUNT_CLOUD, 50);
      {
        uint16_t beamCenter = (uint16_t)((pulsarCycle * 3) % LED_COUNT_CLOUD);
        const int8_t halfWidth = 4;
        for (int8_t s = -halfWidth; s <= halfWidth; s++) {
          uint16_t idx = (uint16_t)((beamCenter + LED_COUNT_CLOUD + s) % LED_COUNT_CLOUD);
          uint8_t falloff = (uint8_t)(255 - abs(s) * (255 / (halfWidth + 1)));
          uint8_t v = scale8(pulseBrightness, falloff);
          ledsCloud[idx] += CHSV(pulseHue, pulseSat, v);
        }
        // background pulse halo
        for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
          ledsCloud[i] += CHSV(pulseHue, pulseSat, pulseBrightness / 12);
        }
      }
    }

void effectDarkNebula(float pm) {
    float ratio = pmRatio(pm);
    uint8_t pollutionlevel = uint8_t(ratio * 255);
    
    static uint16_t nebulaTime = 0;
    uint8_t speed = map(pollutionlevel, 0, 255, 2, 12);
    nebulaTime += speed;
    
    
    struct VoidPoint {
        uint8_t x, y;
        uint8_t radius;
        uint8_t strength;
        int16_t velocity;
        uint8_t hue;
    };
    
    static const uint8_t NUM_VOID_POINTS = 5;
    static VoidPoint voidPoints[NUM_VOID_POINTS];
    static bool initialized = false;
    
    if (!initialized) {
        for (uint8_t i = 0; i < NUM_VOID_POINTS; i++) {
            voidPoints[i].x = random8(MATRIX_WIDTH);
            voidPoints[i].y = random8(MATRIX_HEIGHT);
            voidPoints[i].radius = random8(2, 6);
            voidPoints[i].strength = random8(150, 255);
            voidPoints[i].velocity = random8(1, 4);
            voidPoints[i].hue = random8();
        }
        initialized = true;
    }
    
    
    for (uint8_t i = 0; i < NUM_VOID_POINTS; i++) {
        VoidPoint& vp = voidPoints[i];
        
        
        vp.x += cos8(nebulaTime * vp.velocity) > 128 ? 1 : -1;
        vp.y += sin8(nebulaTime * vp.velocity + 64) > 128 ? 1 : -1;
        
        
        if (vp.x >= MATRIX_WIDTH) vp.x = 0;
        if (vp.x == 255) vp.x = MATRIX_WIDTH - 1;
        if (vp.y >= MATRIX_HEIGHT) vp.y = 0;
        if (vp.y == 255) vp.y = MATRIX_HEIGHT - 1;
        
        
        vp.radius = 2 + (sin8(nebulaTime + i * 37) >> 5);
        vp.strength = 150 + (sin8(nebulaTime * 2 + i * 51) >> 2);
        vp.hue += 1;
    }
    
    
    fadeToBlackBy(ledsMatrix, LED_COUNT_MATRIX, 40);


    fadeToBlackBy(ledsStrip, LED_COUNT_STRIP, 30);

    
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
        for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
            
            
            uint8_t noise = inoise8(x * 30 + nebulaTime, y * 30, nebulaTime / 2);
            uint8_t brightness = map(noise, 0, 255, 0, 180);
            uint8_t hue = map(noise, 0, 255, 130, 200); 
            
            
            for (uint8_t i = 0; i < NUM_VOID_POINTS; i++) {
                VoidPoint& vp = voidPoints[i];
                uint8_t dx = abs(x - vp.x);
                uint8_t dy = abs(y - vp.y);
                uint8_t distance = max(dx, dy); 
                
                if (distance <= vp.radius) {
                    
                    brightness = 0;
                } else if (distance <= vp.radius + 2) {
                    
                    brightness = vp.strength;
                    hue = vp.hue;
                }
            }
            
            
            if (noise > 200 && brightness > 0) {
                brightness = qadd8(brightness, 50);
                hue = map(noise, 200, 255, 160, 130);
            }
            
            
            brightness = scale8(brightness, map(pollutionlevel, 0, 255, 150, 255));
            
            ledsMatrix[XY(x, y)] = CHSV(hue, 255, brightness);
        }
    }
    
    
    for (uint16_t i = 0; i < LED_COUNT_STRIP; i++) {
        uint8_t noise1 = inoise8(i * 10, nebulaTime);
        uint8_t noise2 = inoise8(i * 15, nebulaTime + 15000);
        
        uint8_t wave = noise1 + sin8(i * 2 + nebulaTime / 2);
        uint8_t brightness = map(wave, 0, 255, 0, 150);
        
        
        if (noise2 > 200) {
            brightness = 0; 
        } else if (noise2 > 180) {
            brightness = 255; 
        }
        
        uint8_t hue = map(noise1, 0, 255, 150, 210);
        ledsStrip[i] = CHSV(hue, 240, brightness);
    }
    
    
    static uint8_t pulsePos = 0;
    static uint8_t pulseWidth = 5;
    
    if (nebulaTime % 3 == 0) {

    }
    

    // DISC: aerial view of nebula with void punctures. Each void orbits on a ring.
    fadeToBlackBy(ledsDisc, LED_COUNT_DISC, 35);
    for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
        float dx, dy, dr; uint8_t rIdx, pIdx;
        discIdxToXY(i, dx, dy, dr, rIdx, pIdx);
        uint8_t n = inoise8((uint16_t)((dx + 1.0f) * 2000.0f) + nebulaTime,
                            (uint16_t)((dy + 1.0f) * 2000.0f), nebulaTime / 2);
        uint8_t brightness = map(n, 0, 255, 0, 170);
        uint8_t hue = map(n, 0, 255, 130, 200);

        // check each void orbit
        uint8_t ang = discAngle8(rIdx, pIdx);
        for (uint8_t v = 0; v < NUM_VOID_POINTS; v++) {
            // each void is pinned to a specific ring + orbits angularly
            uint8_t voidRing = (uint8_t)((v * 3 + 2) % DISC_RING_COUNT);
            uint8_t voidAng = (uint8_t)((nebulaTime >> 2) + v * (256 / NUM_VOID_POINTS));
            int16_t angDiff = (int16_t)ang - (int16_t)voidAng;
            if (angDiff > 127) angDiff -= 256;
            if (angDiff < -128) angDiff += 256;
            uint8_t angDist = (uint8_t)abs(angDiff);
            uint8_t rDist = (rIdx > voidRing) ? (rIdx - voidRing) : (voidRing - rIdx);
            if (rDist <= 1 && angDist < 25) {
                brightness = 0;  // void
                if (angDist > 18) {
                    brightness = 255;
                    hue = voidPoints[v].hue;
                }
            }
        }
        brightness = scale8(brightness, map(pollutionlevel, 0, 255, 150, 255));
        ledsDisc[i] = CHSV(hue, 255, brightness);
    }

    // CLOUD: nebula wisp around the loop + a slow-orbiting void puncture.
    fadeToBlackBy(ledsCloud, LED_COUNT_CLOUD, 30);
    for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
        uint8_t n = inoise8(i * 80, nebulaTime + 12000);
        uint8_t brightness = map(n, 0, 255, 0, 160);
        uint8_t hue = map(n, 0, 255, 140, 210);
        ledsCloud[i] = CHSV(hue, 240, scale8(brightness, map(pollutionlevel, 0, 255, 150, 255)));
    }
    // Orbiting void: dark notch with a bright rim
    {
        uint16_t voidCentre = (uint16_t)((nebulaTime >> 2) % LED_COUNT_CLOUD);
        for (int8_t s = -2; s <= 2; s++) {
            uint16_t idx = (voidCentre + LED_COUNT_CLOUD + s) % LED_COUNT_CLOUD;
            ledsCloud[idx] = CRGB::Black;
        }
        for (int8_t s = -4; s <= -3; s++) {
            uint16_t idx = (voidCentre + LED_COUNT_CLOUD + s) % LED_COUNT_CLOUD;
            ledsCloud[idx] = CHSV(voidPoints[0].hue, 240, 200);
        }
        for (int8_t s = 3; s <= 4; s++) {
            uint16_t idx = (voidCentre + s) % LED_COUNT_CLOUD;
            ledsCloud[idx] = CHSV(voidPoints[0].hue, 240, 200);
        }
    }
}
void effectChiaroscuroPulse(float pm) {

  float ratio = fclamp(pmRatio(pm), 0.0f, 1.0f);
  uint8_t pollution = (uint8_t)(ratio * 255.0f);

  static uint32_t sceneClock = 0;
  sceneClock += 6 + (pollution >> 5) + (pollution > 180 ? 3 : 0);

  uint8_t fadeBase = 26 + (pollution >> 4);


  fadeToBlackBy(ledsStrip, LED_COUNT_STRIP, fadeBase + 4);
  fadeToBlackBy(ledsMatrix, LED_COUNT_MATRIX, fadeBase + 5);

  fadeToBlackBy(ledsDisc, LED_COUNT_DISC, fadeBase + 3);
  fadeToBlackBy(ledsCloud, LED_COUNT_CLOUD, fadeBase + 4);

  const uint8_t STAGES = 3;
  uint16_t stageCycle = (sceneClock >> 7) % (STAGES * 256);
  uint8_t stageIndex = stageCycle >> 8;
  uint8_t stageFrac = stageCycle & 0xFF;

  uint8_t stageAmp[STAGES] = {0};
  stageAmp[stageIndex] = 255 - stageFrac;
  stageAmp[(stageIndex + 1) % STAGES] = stageFrac;

  uint8_t baseHue = lerp8by8(150, 12, pollution);
  uint8_t accentHue = baseHue + scale8(pollution, 60);

  if(stageAmp[0]) {
    uint8_t weight = stageAmp[0];
    uint16_t swirl = sceneClock >> 2;
    uint8_t arcSize = 3 + (pollution >> 6);

    uint8_t diagSeed = (sceneClock >> 4) % (MATRIX_WIDTH + MATRIX_HEIGHT);
    for(uint8_t y = 0; y < MATRIX_HEIGHT; ++y) {
      int16_t x = diagSeed - (y & 0x07);
      while(x < 0) x += MATRIX_WIDTH;
      x %= MATRIX_WIDTH;
      uint8_t shimmer = triwave8((sceneClock >> 3) + y * 28);
      uint8_t bright = scale8(qadd8(70, shimmer), weight);
      ledsMatrix[XY(x, y)] += CHSV(accentHue + y * 3, 200, bright);
    }
  }

  if(stageAmp[1]) {
    uint8_t weight = stageAmp[1];
    uint16_t ripple = sceneClock >> 1;
    uint8_t gate = (sceneClock >> 6) & 0x03;

    const uint8_t windows = 5;
    for(uint8_t w = 0; w < windows; ++w) {
      uint16_t offset = (sceneClock / (3 + w)) + w * 73;
      uint16_t center = offset % LED_COUNT_STRIP;
      uint8_t span = 2 + ((pollution >> 6) & 0x03) + (w & 0x01);
      for(int8_t d = -span; d <= span; ++d) {
        if(((center / 8) + d + w) & 0x01) continue;
        int32_t idx = (int32_t)center + d;
        while(idx < 0) idx += LED_COUNT_STRIP;
        idx %= LED_COUNT_STRIP;
        uint8_t flare = qsub8(scale8(200 - w * 15, weight), abs(d) * (20 + (pollution >> 5)));
        ledsStrip[idx] += CHSV(baseHue + 80 + w * 9, 220, flare);
      }
    }
  }

  if(stageAmp[2]) {
    uint8_t weight = stageAmp[2];
    uint16_t swing = sceneClock >> 2;
    uint8_t bloom = 2 + (pollution >> 7);

    uint8_t clusterCount = 2 + (pollution > 160);
    for(uint8_t c = 0; c < clusterCount; ++c) {
      uint8_t cx = (uint8_t)((swing / (4 + c)) + c * 11) % MATRIX_WIDTH;
      uint8_t cy = beatsin8(6 + c * 3, 1, MATRIX_HEIGHT - 2);
      for(int8_t dy = -1; dy <= 1; ++dy) {
        for(int8_t dx = -1; dx <= 1; ++dx) {
          if(abs(dx) + abs(dy) > bloom) continue;
          int8_t xx = (int8_t)cx + dx;
          int8_t yy = (int8_t)cy + dy;
          if(xx < 0 || xx >= MATRIX_WIDTH || yy < 0 || yy >= MATRIX_HEIGHT) continue;
          uint8_t ember = qsub8(scale8(150, weight), (abs(dx) + abs(dy)) * 40);
          ledsMatrix[XY(xx, yy)] += CHSV(accentHue + c * 14, 230, ember);
        }
      }
    }

    if(random8() < scale8(weight, 32 + (pollution >> 3))) {
      uint16_t idx = random16(LED_COUNT_STRIP);
      ledsStrip[idx] = CHSV(baseHue + random8(50), 180, scale8(180, weight));
    }
  }

  // DISC: each stage lights a distinct subset of rings (outer-even, middle-inset, core-and-outer)
  {
    if (stageAmp[0]) {
      uint8_t weight = stageAmp[0];
      // stage 0: outer two rings with rotating hot-spot
      for (uint8_t r = DISC_RING_COUNT - 2; r < DISC_RING_COUNT; r++) {
        uint8_t len = DISC_RING_LENS[r];
        uint16_t spot = (uint16_t)((sceneClock >> 3) + r * 30) % len;
        for (int8_t d = -4; d <= 4; d++) {
          int16_t idx = (int16_t)spot + d;
          while (idx < 0) idx += len;
          idx %= len;
          uint8_t halo = qsub8(scale8(220, weight), (uint8_t)abs(d) * 22);
          ledsDisc[DISC_RING_OFFSETS[r] + idx] = blend(ledsDisc[DISC_RING_OFFSETS[r] + idx],
                                                      CHSV(baseHue + r * 5, 220, halo), 200);
        }
      }
    }
    if (stageAmp[1]) {
      uint8_t weight = stageAmp[1];
      // stage 1: middle rings with alternating gates
      for (uint8_t r = 2; r <= 5; r++) {
        uint8_t len = DISC_RING_LENS[r];
        uint8_t gate = (uint8_t)((sceneClock >> 6) + r) & 0x03;
        for (uint8_t p = 0; p < len; p++) {
          if (((p + gate) & 0x03) != 0) continue;
          uint8_t wave = sin8((uint8_t)((sceneClock >> 1) + p * 12 + r * 40));
          uint8_t v = scale8(qadd8(40, wave), weight);
          ledsDisc[DISC_RING_OFFSETS[r] + p] = blend(ledsDisc[DISC_RING_OFFSETS[r] + p],
                                                    CHSV(baseHue + 48 + r * 4, 190, v), 200);
        }
      }
    }
    if (stageAmp[2]) {
      uint8_t weight = stageAmp[2];
      // stage 2: center core pulse + outer sparks
      uint8_t coreV = scale8(220, weight);
      uint16_t centreIdx = DISC_RING_OFFSETS[0];
      ledsDisc[centreIdx] = blend(ledsDisc[centreIdx], CHSV(accentHue, 230, coreV), 220);
      for (uint8_t p = 0; p < DISC_RING_LENS[1]; p++) {
        ledsDisc[DISC_RING_OFFSETS[1] + p] = blend(ledsDisc[DISC_RING_OFFSETS[1] + p],
                                                   CHSV(accentHue + 4, 220, scale8(170, weight)), 210);
      }
      if (random8() < scale8(weight, 24 + (pollution >> 3))) {
        uint16_t pos = random16(LED_COUNT_DISC);
        uint8_t dRing, dPos; discIdxToRing(pos, dRing, dPos);
        if (dRing >= 4) ledsDisc[pos] = CHSV(baseHue + 128 + random8(20), 180, scale8(180, weight));
      }
    }
  }

  // CLOUD: each stage paints a distinct slice of the loop.
  //   Stage 0 — outer "quarters" (idx 0..12 and 38..49) shimmer
  //   Stage 1 — middle band (idx 13..37) windows
  //   Stage 2 — diametric core spots: idx 0 and idx CLOUD/2
  if (stageAmp[0]) {
    uint8_t weight = stageAmp[0];
    for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
      uint16_t q = i;
      if (!(q <= 12 || q >= 38)) continue;
      uint8_t shimmer = triwave8((sceneClock >> 3) + i * 18);
      uint8_t bright = scale8(qadd8(60, shimmer), weight);
      ledsCloud[i] += CHSV(accentHue + (uint8_t)(i * 3), 200, bright);
    }
  }
  if (stageAmp[1]) {
    uint8_t weight = stageAmp[1];
    for (uint16_t i = 13; i < 38 && i < LED_COUNT_CLOUD; i++) {
      uint8_t gate = (uint8_t)((sceneClock >> 6) + i) & 0x03;
      if (((i + gate) & 0x03) != 0) continue;
      uint8_t wave = sin8((uint8_t)((sceneClock >> 1) + i * 12));
      uint8_t v = scale8(qadd8(40, wave), weight);
      ledsCloud[i] += CHSV(baseHue + 48, 190, v);
    }
  }
  if (stageAmp[2]) {
    uint8_t weight = stageAmp[2];
    uint8_t coreV = scale8(220, weight);
    ledsCloud[0] += CHSV(accentHue, 230, coreV);
    if (LED_COUNT_CLOUD >= 2)
      ledsCloud[LED_COUNT_CLOUD / 2] += CHSV(accentHue + 6, 220, coreV);
  }
}

void effectNocturneMirage(float pm) {
  float ratio = fclamp(pmRatio(pm), 0.0f, 1.0f);
  uint8_t pollution = (uint8_t)(ratio * 255.0f);

  static uint32_t chronoFast = 0;
  static uint32_t chronoSlow = 0;
  chronoFast += 5 + (pollution >> 5) + (pollution > 200 ? 2 : 0);
  chronoSlow += 2 + (pollution >> 6);

  uint8_t hush = 18 + (pollution >> 4);


  fadeToBlackBy(ledsStrip, LED_COUNT_STRIP, hush + 1);
  fadeToBlackBy(ledsMatrix, LED_COUNT_MATRIX, hush + 2);

  fadeToBlackBy(ledsDisc, LED_COUNT_DISC, hush + 2);
  fadeToBlackBy(ledsCloud, LED_COUNT_CLOUD, hush + 2);

  uint8_t emberHue = lerp8by8(150, 8, pollution);
  uint8_t accentHue = emberHue + 48;

  uint16_t latticeShift = chronoFast >> 3;

  const uint8_t lanes = 3;
  for(uint8_t lane = 0; lane < lanes; ++lane) {
    uint16_t head = (chronoFast / (lane + 2) + (uint32_t)lane * LED_COUNT_STRIP / lanes) % LED_COUNT_STRIP;
    uint8_t laneWidth = 3 + lane + (pollution >> 7);
    uint8_t laneDepth = 200 - lane * 40;
    for(int8_t d = -8; d <= 8; ++d) {
      if(abs(d) > laneWidth) continue;
      int16_t idx = (int16_t)head + d;
      while(idx < 0) idx += LED_COUNT_STRIP;
      idx %= LED_COUNT_STRIP;
      uint8_t crown = qsub8(220, abs(d) * (18 + lane * 4));
      crown = scale8(crown, laneDepth);
      ledsStrip[idx] = blend(ledsStrip[idx], CHSV(emberHue + lane * 12, 230, crown), 192);
    }
  }

  uint16_t parallax = chronoFast >> 2;
  for(uint8_t y = 0; y < MATRIX_HEIGHT; ++y) {
    uint8_t depth = y * 24;
    for(uint8_t x = 0; x < MATRIX_WIDTH; ++x) {
      uint8_t wave = sin8(parallax + depth + x * 5);
      uint8_t veil = inoise8(x * 17, y * 29, chronoSlow);
      uint8_t gate = scale8(qadd8(qsub8(wave, 140), veil >> 3), 200);
      if(gate > 8) {
        uint8_t hue = accentHue + (depth >> 3) + (x >> 2);
        ledsMatrix[XY(x, y)] += CHSV(hue, 210, gate);
      }
    }
  }

  uint8_t sparkChance = map(pollution, 0, 255, 2, 14);
  if(random8(100) < sparkChance) {
    ledsStrip[random16(LED_COUNT_STRIP)] = CHSV(emberHue + random8(40), 190, 220);
  }
  if(random8(100) < (sparkChance >> 1)) {
    uint8_t sx = random8(MATRIX_WIDTH);
    uint8_t sy = random8(MATRIX_HEIGHT);
    ledsMatrix[XY(sx, sy)] = CHSV(accentHue + random8(50), 200, 255);
  }

  // DISC: mirage petals - 6 rotating petal shapes on different rings, ember shimmer fills the rest
  const uint8_t dPetals = 6;
  for (uint8_t p = 0; p < dPetals; p++) {
    uint8_t pRing = (uint8_t)((2 + p) % DISC_RING_COUNT);
    uint8_t pLen = DISC_RING_LENS[pRing];
    uint16_t center = (uint16_t)(chronoFast / (3 + p * 2) + (uint32_t)p * pLen / dPetals) % pLen;
    uint8_t surge = sin8((uint8_t)((chronoFast >> 1) + p * 57));
    for (int8_t d = -3; d <= 3; d++) {
      int16_t idx = (int16_t)center + d;
      while (idx < 0) idx += pLen;
      idx %= pLen;
      uint8_t v = qsub8(surge, (uint8_t)abs(d) * 30);
      v = scale8(v, 190);
      ledsDisc[DISC_RING_OFFSETS[pRing] + idx] = blend(
        ledsDisc[DISC_RING_OFFSETS[pRing] + idx],
        CHSV(emberHue + p * 7, 220, v), 200);
    }
  }
  // cinder shimmer on remaining LEDs
  for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
    uint8_t cinder = inoise8(i * 43, chronoSlow);
    if (cinder > 240) {
      ledsDisc[i] += CHSV(emberHue + 32, 180, cinder - 170);
    }
  }
  if (random8(100) < sparkChance) {
    ledsDisc[random16(LED_COUNT_DISC)] = CHSV(accentHue + random8(40), 200, 230);
  }

  // CLOUD: shimmering heat-haze lanes around the loop with random cinder sparks.
  {
    const uint8_t cLanes = 3;
    for (uint8_t lane = 0; lane < cLanes; lane++) {
      uint16_t head = (uint16_t)(chronoFast / (lane + 2) + (uint32_t)lane * LED_COUNT_CLOUD / cLanes) % LED_COUNT_CLOUD;
      uint8_t laneWidth = 2 + lane;
      uint8_t laneDepth = 200 - lane * 50;
      for (int8_t d = -laneWidth; d <= laneWidth; d++) {
        uint16_t idx = (uint16_t)(head + LED_COUNT_CLOUD + d) % LED_COUNT_CLOUD;
        uint8_t crown = qsub8(220, (uint8_t)abs(d) * (24 + lane * 4));
        crown = scale8(crown, laneDepth);
        ledsCloud[idx] = blend(ledsCloud[idx], CHSV(emberHue + lane * 12, 230, crown), 192);
      }
    }
    if (random8(100) < sparkChance) {
      ledsCloud[random16(LED_COUNT_CLOUD)] = CHSV(accentHue + random8(40), 200, 230);
    }
  }
}

void effectChronoRift(float pm) {
  const uint32_t now = nowMs();
  float ratio = fclamp(pmRatio(pm), 0.0f, 1.0f);

  static float ratioSmooth = 0.0f;
  const float rise = 0.14f;
  const float fall = 0.05f;
  float k = (ratio > ratioSmooth) ? rise : fall;
  ratioSmooth += (ratio - ratioSmooth) * k;
  ratioSmooth = fclamp(ratioSmooth, 0.0f, 1.0f);

  static uint8_t chronoPhase = 1;
  static uint8_t pendingPhase = 1;
  static bool chronoArmed = true;
  static bool chargePending = false;
  static uint32_t chargeStart = 0;
  static uint32_t transitionStart = 0;
  static uint8_t transitionPhase = 1;

  const float triggerRatio = 0.98f;
  const float rearmRatio = 0.60f;
  const uint32_t chargeHoldMs = 1000;

  if (ratio <= rearmRatio && !chargePending) {
    chronoArmed = true;
  }

  if (chronoArmed && !chargePending && transitionStart == 0 && ratio >= triggerRatio) {
    chronoArmed = false;
    chargePending = true;
    chargeStart = now;
    pendingPhase = chronoPhase + 1;
    if (pendingPhase > 3) pendingPhase = 1;
  }

  bool chargeActive = chargePending && ((now - chargeStart) < chargeHoldMs);
  float power = chargeActive ? 1.0f : ratioSmooth;

  if (chargePending && !chargeActive) {
    chargePending = false;
    transitionStart = now;
    transitionPhase = pendingPhase;
  }

  uint8_t pollution = 0;
  uint8_t motion = 0;
  uint16_t frameMs = 0;
  if (chronoPhase == 1) {
    pollution = (uint8_t)(power * 255.0f + 0.5f);
    motion = pollution;
    frameMs = (uint16_t)map(motion, 0, 255, 32, 10);
  } else if (chronoPhase == 2) {
    float phaseRatio = sqrtf(power);
    pollution = (uint8_t)(phaseRatio * 200.0f + 0.5f);
    motion = qsub8(pollution, 40);
    frameMs = (uint16_t)map(motion, 0, 255, 50, 18);
  } else {
    float phaseRatio = powf(power, 1.25f);
    pollution = (uint8_t)(phaseRatio * 220.0f + 0.5f);
    motion = qadd8(pollution, 32);
    frameMs = (uint16_t)map(motion, 0, 255, 26, 8);
  }
  if (pollution > 255) pollution = 255;

  const uint16_t kTransitionMs = 1200;
  bool inTransition = (transitionStart != 0) && ((now - transitionStart) < kTransitionMs);

  static uint32_t lastFrame = 0;
  if (!inTransition && lastFrame && (now - lastFrame) < frameMs) return;
  uint32_t dt = lastFrame ? (now - lastFrame) : frameMs;
  if (dt > 80) dt = 80;
  lastFrame = now;

  if (chronoPhase == 1) {
    const uint8_t baseHue = lerp8by8(160, 0, pollution);
    const uint8_t accentHue = baseHue + 32;

    static uint16_t chronoFast = 0;
    static uint16_t chronoSlow = 0;
    chronoFast += 3 + (motion >> 5);
    chronoSlow += 1 + (motion >> 6);

    struct ChronoRift {
      bool active;
      uint8_t x;
      uint8_t y;
      uint16_t radius_q8;
      uint16_t speed_q8;
      uint8_t width;
      uint8_t hue;
    };

    const uint8_t MAX_RIFTS = 4;
    static ChronoRift rifts[MAX_RIFTS];
    static bool riftsInit = false;
    static uint8_t lastSpawnX = MATRIX_WIDTH / 2;
    static uint8_t lastSpawnY = MATRIX_HEIGHT / 2;

    if (!riftsInit) {
      for (uint8_t i = 0; i < MAX_RIFTS; i++) rifts[i].active = false;
      riftsInit = true;
    }

    bool spawned = false;
    uint8_t spawnChance = map(motion, 0, 255, 2, 14);
    if (random8(100) < spawnChance) {
      for (uint8_t i = 0; i < MAX_RIFTS; i++) {
        if (!rifts[i].active) {
          uint8_t x = random8(MATRIX_WIDTH);
          uint8_t y = random8(MATRIX_HEIGHT);
          if (random8() < map(pollution, 0, 255, 80, 170)) {
            int8_t dx = (int8_t)random8(7) - 3;
            int8_t dy = (int8_t)random8(7) - 3;
            int16_t nx = (int16_t)lastSpawnX + dx;
            int16_t ny = (int16_t)lastSpawnY + dy;
            if (nx < 0) nx = 0;
            if (nx >= MATRIX_WIDTH) nx = MATRIX_WIDTH - 1;
            if (ny < 0) ny = 0;
            if (ny >= MATRIX_HEIGHT) ny = MATRIX_HEIGHT - 1;
            x = (uint8_t)nx;
            y = (uint8_t)ny;
          }

          rifts[i].active = true;
          rifts[i].x = x;
          rifts[i].y = y;
          rifts[i].radius_q8 = 0;
          rifts[i].speed_q8 = (uint16_t)map(motion, 0, 255, 3, 10);
          rifts[i].width = 1 + (pollution >> 7);
          rifts[i].hue = baseHue + random8(40);
          lastSpawnX = x;
          lastSpawnY = y;
          spawned = true;
          break;
        }
      }
    }

    uint8_t maxDim = (MATRIX_WIDTH > MATRIX_HEIGHT) ? MATRIX_WIDTH : MATRIX_HEIGHT;
    if (maxDim == 0) maxDim = 1;
    uint16_t maxRad_q8 = ((uint16_t)maxDim + 4U) * 256U;

    uint8_t riftPulse = 0;
    for (uint8_t i = 0; i < MAX_RIFTS; i++) {
      if (!rifts[i].active) continue;
      rifts[i].radius_q8 += (uint32_t)rifts[i].speed_q8 * (uint32_t)dt;
      if (rifts[i].radius_q8 > maxRad_q8) {
        rifts[i].active = false;
        continue;
      }
      uint8_t p = (uint8_t)map((uint16_t)(rifts[i].radius_q8 >> 8), 0, maxDim + 4, 40, 220);
      if (p > riftPulse) riftPulse = p;
    }

    // MATRIX: rift field
    uint8_t matrixDecay = map(pollution, 0, 255, 40, 18);
    fadeToBlackBy(ledsMatrix, LED_COUNT_MATRIX, matrixDecay);

    static uint16_t riftNoiseZ = 0;
    riftNoiseZ += 1 + (motion >> 6);

    uint8_t noiseScale = map(pollution, 0, 255, 20, 12);
    uint8_t baseSat = map(pollution, 0, 255, 120, 200);
    uint8_t baseV = qadd8(map(pollution, 0, 255, 8, 60), riftPulse >> 4);

    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        uint8_t n = inoise8(x * noiseScale, y * noiseScale, riftNoiseZ);
        uint8_t v = scale8(n, baseV);
        ledsMatrix[XY(x, y)] += CHSV(baseHue + (n >> 4), baseSat, v);
      }
    }

    for (uint8_t r = 0; r < MAX_RIFTS; r++) {
      if (!rifts[r].active) continue;
      float radius = (float)rifts[r].radius_q8 * (1.0f / 256.0f);
      float width = (float)rifts[r].width;
      float inner = radius - width;
      if (inner < 0.0f) inner = 0.0f;
      float outer = radius + width;
      float innerSq = inner * inner;
      float outerSq = outer * outer;
      float rSq = radius * radius;
      float ringSpan = outerSq - innerSq;
      if (ringSpan < 1.0f) ringSpan = 1.0f;

      for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
        int16_t dy = (int16_t)y - (int16_t)rifts[r].y;
        for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
          int16_t dx = (int16_t)x - (int16_t)rifts[r].x;
          float distSq = (float)(dx * dx + dy * dy);
          uint16_t idx = XY(x, y);
          if (distSq >= innerSq && distSq <= outerSq) {
            float diff = distSq - rSq;
            if (diff < 0.0f) diff = -diff;
            float falloff = 1.0f - (diff / ringSpan);
            uint8_t v = (uint8_t)(falloff * (float)map(pollution, 0, 255, 120, 255));
            ledsMatrix[idx] += CHSV(rifts[r].hue, 210, v);
          } else if (distSq < innerSq) {
            ledsMatrix[idx].nscale8_video(220);
          }
        }
      }
    }


    // STRIP: scanline with echo
    if (LED_COUNT_STRIP > 0) {
      uint8_t stripFade = map(pollution, 0, 255, 48, 22);
      fadeToBlackBy(ledsStrip, LED_COUNT_STRIP, stripFade);

      static int16_t stripPos = 0;
      static int8_t stripDir = 1;
      static uint8_t echoLife = 0;
      static int16_t echoPos = 0;
      static int8_t echoDir = -1;

      uint8_t stripStep = 1 + (motion >> 6);
      stripPos += stripDir * (int16_t)stripStep;
      while (stripPos < 0) stripPos += (int16_t)LED_COUNT_STRIP;
      while (stripPos >= (int16_t)LED_COUNT_STRIP) stripPos -= (int16_t)LED_COUNT_STRIP;

      uint8_t scanLen = 8 + (pollution >> 5);
      uint8_t scanV = qadd8(120, scale8(riftPulse, 100));
      for (uint8_t t = 0; t < scanLen; t++) {
        int16_t idx = stripPos - (stripDir * (int16_t)t);
        while (idx < 0) idx += (int16_t)LED_COUNT_STRIP;
        idx %= (int16_t)LED_COUNT_STRIP;
        uint8_t v = scale8(scanV, (uint8_t)(255 - (uint16_t)t * 255U / (uint16_t)(scanLen ? scanLen : 1)));
        ledsStrip[(uint16_t)idx] += CHSV(accentHue + 20, 220, v);
      }

      if (spawned) {
        echoPos = stripPos;
        echoDir = (int8_t)-stripDir;
        echoLife = map(pollution, 0, 255, 12, 30);
      }

      if (echoLife > 0) {
        echoLife--;
        echoPos += echoDir * (int16_t)stripStep;
        while (echoPos < 0) echoPos += (int16_t)LED_COUNT_STRIP;
        while (echoPos >= (int16_t)LED_COUNT_STRIP) echoPos -= (int16_t)LED_COUNT_STRIP;

        uint8_t echoLen = 5 + (pollution >> 7);
        uint8_t echoV = qadd8(80, riftPulse >> 2);
        for (uint8_t t = 0; t < echoLen; t++) {
          int16_t idx = echoPos + (echoDir * (int16_t)t);
          while (idx < 0) idx += (int16_t)LED_COUNT_STRIP;
          idx %= (int16_t)LED_COUNT_STRIP;
          uint8_t v = scale8(echoV, (uint8_t)(255 - (uint16_t)t * 255U / (uint16_t)(echoLen ? echoLen : 1)));
          ledsStrip[(uint16_t)idx] += CHSV(baseHue + 80, 180, v);
        }
      }
    }


    // DISC phase 1: expanding rift rings emanate from a randomly-chosen center ring
    {
      uint8_t dFade = map(pollution, 0, 255, 44, 22);
      fadeToBlackBy(ledsDisc, LED_COUNT_DISC, dFade);

      static uint8_t riftCenterRing = 0;
      static float riftOutRadius = 0.0f;
      static uint8_t riftOutLife = 0;
      riftOutRadius += (0.03f + 0.09f * (float)motion / 255.0f);
      if (riftOutLife > 0) riftOutLife--;
      if (riftOutLife == 0) {
        riftOutRadius = 0.0f;
        riftCenterRing = random8(DISC_RING_COUNT);
        riftOutLife = map(pollution, 0, 255, 16, 50);
      }

      for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
        uint8_t rIdx, pIdx;
        discIdxToRing(i, rIdx, pIdx);
        uint8_t ang = discAngle8(rIdx, pIdx);
        // base noise field
        uint8_t n = inoise8(ang * 4 + rIdx * 50, chronoSlow);
        uint8_t v = scale8(n, qadd8(20, riftPulse >> 3));
        ledsDisc[i] += CHSV(baseHue + (n >> 4), 200, v);

        // rift wavefront
        float d = (float)rIdx - (float)riftCenterRing;
        if (d < 0.0f) d = -d;
        float diff = d - riftOutRadius;
        if (diff < 0.0f) diff = -diff;
        if (diff < 0.9f) {
          uint8_t rv = (uint8_t)((1.0f - diff / 0.9f) * 220.0f);
          ledsDisc[i] += CHSV(accentHue, 220, rv);
        }
      }
    }
  } else if (chronoPhase == 2) {
    const uint8_t baseHue = 24;
    const uint8_t accentHue = 36;

    uint8_t matrixDecay = map(pollution, 0, 255, 26, 12);
    fadeToBlackBy(ledsMatrix, LED_COUNT_MATRIX, matrixDecay);

    static uint16_t pastTime = 0;
    pastTime += 1 + (motion >> 7);

    uint8_t noiseScale = map(pollution, 0, 255, 26, 14);
    uint8_t baseSat = map(pollution, 0, 255, 60, 130);
    uint8_t baseV = map(pollution, 0, 255, 6, 50);

    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        uint8_t n = inoise8(x * noiseScale, y * noiseScale, pastTime);
        uint8_t v = scale8(n, baseV);
        ledsMatrix[XY(x, y)] += CHSV(baseHue + (n >> 5), baseSat, v);
      }
    }

    static uint8_t scratchX = 0;
    static uint8_t scratchLife = 0;
    if (scratchLife == 0 && random8(100) < map(pollution, 0, 255, 2, 8)) {
      scratchX = random8(MATRIX_WIDTH);
      scratchLife = random8(6, 16);
    }
    if (scratchLife > 0) {
      scratchLife--;
      uint8_t scratchV = map(pollution, 0, 255, 30, 140);
      for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
        ledsMatrix[XY(scratchX, y)] += CHSV(baseHue + 10, 40, scratchV);
      }
    }

    if (random8(100) < map(pollution, 0, 255, 2, 10)) {
      uint8_t sx = random8(MATRIX_WIDTH);
      uint8_t sy = random8(MATRIX_HEIGHT);
      ledsMatrix[XY(sx, sy)] += CHSV(accentHue + random8(18), 120, 160);
    }


    // STRIP: projector sweep
    if (LED_COUNT_STRIP > 0) {
      uint8_t stripFade = map(pollution, 0, 255, 34, 18);
      fadeToBlackBy(ledsStrip, LED_COUNT_STRIP, stripFade);

      static uint16_t stripPhase = 0;
      stripPhase += 1 + (motion >> 7);

      uint16_t beamPos = (uint16_t)(stripPhase >> 1) % LED_COUNT_STRIP;
      uint8_t beamLen = 12 + (pollution >> 6);
      uint8_t beamV = map(pollution, 0, 255, 60, 160);

      for (uint8_t t = 0; t < beamLen; t++) {
        uint16_t idx = (beamPos + t) % LED_COUNT_STRIP;
        uint8_t v = scale8(beamV, (uint8_t)(255 - (uint16_t)t * 255U / (uint16_t)(beamLen ? beamLen : 1)));
        ledsStrip[idx] += CHSV(baseHue + 8, 120, v);
      }

      if (random8(100) < map(pollution, 0, 255, 3, 10)) {
        ledsStrip[random16(LED_COUNT_STRIP)] += CHSV(accentHue + 8, 80, 120);
      }
    }


    // DISC phase 2: sepia film grain on all rings + slow clock-hand sweep
    {
      uint8_t dFade = map(pollution, 0, 255, 30, 16);
      fadeToBlackBy(ledsDisc, LED_COUNT_DISC, dFade);
      static uint16_t discClockPhase = 0;
      discClockPhase += 1 + (motion >> 7);
      uint8_t handAng = (uint8_t)(discClockPhase >> 2);

      for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
        uint8_t rIdx, pIdx;
        discIdxToRing(i, rIdx, pIdx);
        uint8_t ang = discAngle8(rIdx, pIdx);
        uint8_t grain = inoise8(pIdx * 23 + rIdx * 37, pastTime);
        uint8_t v = scale8(grain, map(pollution, 0, 255, 18, 60));
        ledsDisc[i] += CHSV(baseHue + (grain >> 5), 90, v);

        // sepia clock hand: angular sector brighter when aligned
        int16_t dAng = (int16_t)ang - (int16_t)handAng;
        if (dAng > 127) dAng -= 256;
        if (dAng < -128) dAng += 256;
        uint8_t absAng = (uint8_t)abs(dAng);
        if (absAng < 16 && rIdx > 1) {
          uint8_t handV = scale8(map(pollution, 0, 255, 50, 160), (uint8_t)(255 - absAng * 15));
          ledsDisc[i] += CHSV(baseHue + 10, 70, handV);
        }
      }
      // dust scratch occasionally zaps an entire ring
      if (random8(100) < map(pollution, 0, 255, 2, 8)) {
        uint8_t sR = random8(DISC_RING_COUNT);
        uint8_t sLen = DISC_RING_LENS[sR];
        for (uint8_t p = 0; p < sLen; p++) {
          ledsDisc[DISC_RING_OFFSETS[sR] + p] += CHSV(baseHue + 14, 40, map(pollution, 0, 255, 30, 110));
        }
      }
    }
  } else {
    const uint8_t baseHue = 140;
    const uint8_t accentHue = 196;

    uint8_t matrixDecay = map(pollution, 0, 255, 70, 30);
    fadeToBlackBy(ledsMatrix, LED_COUNT_MATRIX, matrixDecay);

    static uint16_t futureTime = 0;
    futureTime += 3 + (motion >> 5);

    uint8_t noiseScale = map(pollution, 0, 255, 22, 12);
    uint8_t baseSat = map(pollution, 0, 255, 160, 220);
    uint8_t baseV = map(pollution, 0, 255, 20, 90);

    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        uint8_t n = inoise8(x * noiseScale, y * noiseScale, futureTime);
        uint8_t v = scale8(n, baseV);
        if (((x + (futureTime >> 5)) & 0x03) == 0) v = qadd8(v, 30);
        if (((y + (futureTime >> 6)) % 5) == 0) v = qadd8(v, 25);
        ledsMatrix[XY(x, y)] += CHSV(baseHue + (n >> 4), baseSat, v);
      }
    }

    uint8_t scanY = (uint8_t)((futureTime >> 3) % MATRIX_HEIGHT);
    uint8_t scanV = map(pollution, 0, 255, 60, 180);
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
      ledsMatrix[XY(x, scanY)] += CHSV(accentHue, 220, scanV);
    }

    uint8_t scanX = (uint8_t)((futureTime >> 4) % MATRIX_WIDTH);
    uint8_t scanV2 = map(pollution, 0, 255, 50, 160);
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      ledsMatrix[XY(scanX, y)] += CHSV(accentHue + 20, 200, scanV2);
    }

    if (random8(100) < map(pollution, 0, 255, 4, 16)) {
      uint8_t sx = random8(MATRIX_WIDTH);
      uint8_t sy = random8(MATRIX_HEIGHT);
      ledsMatrix[XY(sx, sy)] += CHSV(accentHue + random8(40), 220, 200);
    }


    // STRIP: twin beams
    if (LED_COUNT_STRIP > 0) {
      uint8_t stripFade = map(pollution, 0, 255, 70, 28);
      fadeToBlackBy(ledsStrip, LED_COUNT_STRIP, stripFade);

      static uint16_t stripPhase = 0;
      stripPhase += 4 + (motion >> 5);

      uint16_t basePos = stripPhase % LED_COUNT_STRIP;
      uint8_t beamLen = 10 + (pollution >> 5);
      uint8_t beamV = map(pollution, 0, 255, 160, 220);

      for (uint8_t b = 0; b < 2; b++) {
        uint16_t pos = (basePos + (uint16_t)b * (LED_COUNT_STRIP / 3)) % LED_COUNT_STRIP;
        for (uint8_t t = 0; t < beamLen; t++) {
          uint16_t idx = (pos + LED_COUNT_STRIP - t) % LED_COUNT_STRIP;
          uint8_t v = scale8(beamV, (uint8_t)(255 - (uint16_t)t * 255U / (uint16_t)(beamLen ? beamLen : 1)));
          ledsStrip[idx] += CHSV(accentHue + 8, 220, v);
        }
      }

      if (random8(100) < map(pollution, 0, 255, 4, 14)) {
        ledsStrip[random16(LED_COUNT_STRIP)] += CHSV(accentHue + 40, 180, 160);
      }
    }


    // DISC phase 3: futuristic radar sweep + rotating neon lattice
    {
      uint8_t dFade = map(pollution, 0, 255, 72, 30);
      fadeToBlackBy(ledsDisc, LED_COUNT_DISC, dFade);
      static uint16_t discRadarPhase = 0;
      discRadarPhase += 4 + (motion >> 5);
      uint8_t sweepAng = (uint8_t)(discRadarPhase >> 1);

      for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
        uint8_t rIdx, pIdx;
        discIdxToRing(i, rIdx, pIdx);
        uint8_t ang = discAngle8(rIdx, pIdx);
        // neon lattice background
        if ((rIdx & 1) == 0) {
          uint8_t wave = sin8((uint8_t)(ang * 2 + discRadarPhase + rIdx * 20));
          uint8_t v = scale8(wave, map(pollution, 0, 255, 30, 90));
          ledsDisc[i] += CHSV(baseHue + rIdx * 6, 220, v);
        }
        // radar sweep: trailing wedge
        int16_t dAng = (int16_t)ang - (int16_t)sweepAng;
        if (dAng > 127) dAng -= 256;
        if (dAng < -128) dAng += 256;
        if (dAng >= 0 && dAng < 48) {
          uint8_t sv = (uint8_t)map(pollution, 0, 255, 140, 230) * (uint8_t)(48 - dAng) / 48;
          ledsDisc[i] += CHSV(accentHue + rIdx * 3, 220, sv);
        }
      }
      // scanned blip every few frames
      if (random8(100) < map(pollution, 0, 255, 8, 22)) {
        uint16_t bp = random16(LED_COUNT_DISC);
        ledsDisc[bp] = CHSV(accentHue + 40, 200, 220);
      }
    }
  }

  if (inTransition) {
    uint32_t elapsed = now - transitionStart;
    uint8_t prog = (uint8_t)((elapsed * 255UL) / kTransitionMs);
    uint8_t flash = qsub8(255, prog);
    uint8_t burst = scale8(flash, 180);

    uint8_t burstHue = 160;
    uint8_t burstSat = 120;
    if (transitionPhase == 2) {
      burstHue = 24;
      burstSat = 90;
    } else if (transitionPhase == 3) {
      burstHue = 150;
      burstSat = 160;
    }

    if (burst > 0) {
      CHSV burstColor(burstHue, burstSat, burst);
      for (uint16_t i = 0; i < LED_COUNT_STRIP; i++) {
        ledsStrip[i] += burstColor;
      }
      for (uint16_t i = 0; i < LED_COUNT_MATRIX; i++) {
        ledsMatrix[i] += burstColor;
      }
      // CLOUD burst echo
      CHSV cloudBurst(burstHue, burstSat, (uint8_t)(burst >> 1));
      for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
        ledsCloud[i] += cloudBurst;
      }
      for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
        ledsDisc[i] += burstColor;
      }
    }

    uint8_t maxDim = (MATRIX_WIDTH > MATRIX_HEIGHT) ? MATRIX_WIDTH : MATRIX_HEIGHT;
    if (maxDim == 0) maxDim = 1;
    uint16_t maxRad = (uint16_t)(maxDim + 6);
    uint16_t waveQ8 = (uint16_t)((elapsed * (uint32_t)maxRad * 256UL) / kTransitionMs);
    uint16_t radius = waveQ8 >> 8;
    uint8_t thickness = 1 + (uint8_t)((kTransitionMs - elapsed) / 300);
    uint16_t inner = (radius > thickness) ? (radius - thickness) : 0;
    uint16_t outer = radius + thickness;
    uint32_t innerSq = (uint32_t)inner * inner;
    uint32_t outerSq = (uint32_t)outer * outer;

    uint8_t waveV = qadd8(60, burst);
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      int16_t dy = (int16_t)y - (int16_t)(MATRIX_HEIGHT / 2);
      for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        int16_t dx = (int16_t)x - (int16_t)(MATRIX_WIDTH / 2);
        uint32_t distSq = (uint32_t)(dx * dx + dy * dy);
        if (distSq >= innerSq && distSq <= outerSq) {
          ledsMatrix[XY(x, y)] += CHSV(burstHue + 20, burstSat, waveV);
        }
      }
    }
  } else if (transitionStart && (now - transitionStart >= kTransitionMs)) {
    transitionStart = 0;
    chronoPhase = pendingPhase;
    pendingPhase = chronoPhase;
  }

  // CLOUD: phase-aware scanline around the loop. Each phase tints the loop
  // distinctly (cyan rift / sepia film / green radar), with a fast head LED.
  {
    uint8_t cFade = (uint8_t)map(motion, 0, 255, 50, 22);
    fadeToBlackBy(ledsCloud, LED_COUNT_CLOUD, cFade);
    uint8_t cloudHue;
    uint8_t cloudSat;
    switch (chronoPhase) {
      case 1: cloudHue = 130; cloudSat = 220; break;  // phase 1: cyan rift
      case 2: cloudHue =  28; cloudSat = 160; break;  // phase 2: sepia film
      default: cloudHue = 96; cloudSat = 230; break;  // phase 3: green radar
    }
    static float cloudScan = 0.0f;
    cloudScan += 0.20f + 0.40f * (float)motion / 255.0f;
    while (cloudScan >= (float)LED_COUNT_CLOUD) cloudScan -= (float)LED_COUNT_CLOUD;
    uint16_t headIdx = (uint16_t)cloudScan;
    const uint8_t scanLen = 6;
    for (uint8_t t = 0; t < scanLen; t++) {
      uint16_t idx = (headIdx + LED_COUNT_CLOUD - t) % LED_COUNT_CLOUD;
      uint8_t v = (uint8_t)(220 - t * (220 / scanLen));
      ledsCloud[idx] += CHSV(cloudHue + t * 2, cloudSat, v);
    }
    // background hum
    for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
      uint8_t n = inoise8(i * 60, (uint16_t)(now >> 4));
      uint8_t v = scale8(qsub8(n, 160), 40);
      ledsCloud[i] += CHSV(cloudHue, cloudSat, v);
    }
  }
}

void effectQuantumCascade(float pm) {
  float ratio = pmRatio(pm);
  uint8_t pollutionlevel = uint8_t(ratio * 255);
  
  static uint16_t quantumTime = 0;
  uint8_t cascadeSpeed = map(pollutionlevel, 0, 255, 3, 18);
  quantumTime += cascadeSpeed;
  
  
  uint8_t quantumHue = map(pollutionlevel, 0, 255, 160, 0);
  
  
  #define MAX_QUANTUM_PARTICLES 48
  struct QuantumParticle {
    bool active;
    uint16_t position;
    uint8_t strip; 
    int8_t velocity;
    uint8_t energy; 
    uint8_t phase;
    bool entangled;
    uint8_t entangledWith;
  };
  
  static QuantumParticle particles[MAX_QUANTUM_PARTICLES];
  static bool initialized = false;

  // Only strips 2 (Strip), 3 (Matrix), 5 (Disc) are valid targets.
  static const uint8_t VALID_STRIPS[3] = {2, 3, 5};

  if (!initialized) {
    for (uint8_t i = 0; i < MAX_QUANTUM_PARTICLES; i++) {
      particles[i].active = false;
      particles[i].entangled = false;
      particles[i].entangledWith = 0;
    }
    initialized = true;
  }
  
  
  fade_all(90);

  // Wave-event system: long dark silence, then a sudden sweep.
  static uint16_t waveCountdown = 80;
  if (waveCountdown > 0) {
    waveCountdown--;
  } else {
    // At 30fps: clean air ~600-800 frames (20-27 s), max pollution ~250-450 frames (8-15 s).
    uint16_t quietFrames = map(pollutionlevel, 0, 255, 600, 250);
    waveCountdown = quietFrames + random8(200);

    // Pick one surface and sweep it with a coherent wave of particles.
    uint8_t waveStrip = VALID_STRIPS[random8(3)];
    uint16_t waveLen = 0;
    switch(waveStrip) {
      case 2: waveLen = LED_COUNT_STRIP;  break;
      case 3: waveLen = LED_COUNT_MATRIX; break;
      case 5: waveLen = LED_COUNT_DISC;   break;
    }

    uint8_t waveCount = map(pollutionlevel, 0, 255, 5, 12);
    uint16_t spacing  = waveLen / (waveCount + 1);
    int8_t   waveVel  = random8(2) ? random8(3, 7) : -random8(3, 7);
    uint8_t  wavePhase = random8();
    uint8_t  spawned  = 0;

    for (uint8_t i = 0; i < MAX_QUANTUM_PARTICLES && spawned < waveCount; i++) {
      if (particles[i].active) continue;
      particles[i].active       = true;
      particles[i].strip        = waveStrip;
      particles[i].position     = (uint16_t)(spacing * (spawned + 1));
      particles[i].velocity     = waveVel;
      particles[i].energy       = random8(210, 255);
      particles[i].phase        = wavePhase + spawned * 28;
      particles[i].entangled    = false;
      particles[i].entangledWith = 0;
      spawned++;
    }

    // Entangle the first two particles in the wave with each other.
    uint8_t first = 255, second = 255;
    for (uint8_t i = 0; i < MAX_QUANTUM_PARTICLES; i++) {
      if (!particles[i].active || particles[i].strip != waveStrip) continue;
      if (first == 255)       { first  = i; continue; }
      if (second == 255)      { second = i; break; }
    }
    if (first != 255 && second != 255) {
      particles[first].entangled       = true;
      particles[first].entangledWith   = second;
      particles[second].entangled      = true;
      particles[second].entangledWith  = first;
    }
  }
  
  
  for (uint8_t i = 0; i < MAX_QUANTUM_PARTICLES; i++) {
    if (!particles[i].active) continue;
    
    
    uint16_t stripLength = 0;
    CRGB* targetStrip = nullptr;

    switch(particles[i].strip) {
      case 2: stripLength = LED_COUNT_STRIP; targetStrip = ledsStrip; break;
      case 3: stripLength = LED_COUNT_MATRIX; targetStrip = ledsMatrix; break;
      case 5: stripLength = LED_COUNT_DISC; targetStrip = ledsDisc; break;
    }

    if (targetStrip == nullptr || stripLength == 0) {
      particles[i].active = false;
      if (particles[i].entangled && particles[i].entangledWith < MAX_QUANTUM_PARTICLES) {
        particles[particles[i].entangledWith].entangled = false;
      }
      continue;
    }

    int32_t newPos = (int32_t)particles[i].position + (int32_t)particles[i].velocity;
    if (random8(100) < 40 && (newPos < 0 || newPos >= (int32_t)stripLength)) {
      particles[i].strip = VALID_STRIPS[random8(3)];
      particles[i].position = random16(10);
    } else {
      newPos = ((newPos % (int32_t)stripLength) + (int32_t)stripLength) % (int32_t)stripLength;
      particles[i].position = (uint16_t)newPos;
    }

    particles[i].phase += 7;
    
    
    particles[i].energy = qsub8(particles[i].energy, 2);
    if (particles[i].energy < 20) {
      particles[i].active = false;
      if (particles[i].entangled && particles[i].entangledWith < MAX_QUANTUM_PARTICLES) {
        particles[particles[i].entangledWith].entangled = false;
      }
      continue;
    }
    
    
    uint8_t pulseBright = sin8(particles[i].phase);
    uint8_t brightness = scale8(particles[i].energy, pulseBright);

    // Dying flicker: strobe as energy fades out
    if (particles[i].energy < 60) {
      if (sin8(particles[i].phase * 3) < 100) brightness = 0;
    }

    uint8_t hue = quantumHue + (sin8(particles[i].phase * 2) >> 3);

    uint16_t pos = particles[i].position;
    if (brightness > 0) targetStrip[pos] = CHSV(hue, 255, brightness);

    // Direction-aware trail: always behind the particle
    int8_t trailDir = (particles[i].velocity >= 0) ? -1 : 1;
    for (uint8_t t = 1; t <= 4; t++) {
      int32_t tp = (int32_t)pos + (int32_t)trailDir * t;
      if (tp >= 0 && tp < (int32_t)stripLength) {
        targetStrip[tp] += CHSV(hue, 200, brightness >> t);
      }
    }
    
    
    if (particles[i].energy > 150 && random8(100) < 8) {
      for (uint8_t j = 0; j < MAX_QUANTUM_PARTICLES; j++) {
        if (!particles[j].active) {
          particles[j].active = true;
          particles[j].strip = particles[i].strip;
          particles[j].position = particles[i].position;
          particles[j].velocity = -particles[i].velocity; 
          particles[j].energy = particles[i].energy / 2;
          particles[j].phase = particles[i].phase + 128;
          particles[j].entangled = false;
          
          
          particles[i].energy = particles[i].energy / 2;
          break;
        }
      }
    }
    
    
    if (particles[i].entangled
        && particles[i].entangledWith < MAX_QUANTUM_PARTICLES
        && particles[particles[i].entangledWith].active) {
      uint8_t partner = particles[i].entangledWith;

      if (sin8(quantumTime >> 2) > 200) {
        uint8_t flashHue = quantumHue + 64;
        if (targetStrip) {
          targetStrip[particles[i].position] = CHSV(flashHue, 255, 255);
        }

        CRGB* partnerStrip = nullptr;
        uint16_t partnerLen = 0;
        switch(particles[partner].strip) {
          case 2: partnerStrip = ledsStrip;  partnerLen = LED_COUNT_STRIP;  break;
          case 3: partnerStrip = ledsMatrix; partnerLen = LED_COUNT_MATRIX; break;
          case 5: partnerStrip = ledsDisc;   partnerLen = LED_COUNT_DISC;   break;
        }
        if (partnerStrip && particles[partner].position < partnerLen) {
          uint16_t ppos = particles[partner].position;
          // 5-LED bloom at partner: bright center, dimming outward
          partnerStrip[ppos] = CHSV(flashHue, 255, 255);
          for (int8_t b = -2; b <= 2; b++) {
            if (b == 0) continue;
            int32_t bp = (int32_t)ppos + b;
            if (bp >= 0 && bp < (int32_t)partnerLen) {
              partnerStrip[bp] += CHSV(flashHue, 180, 180 >> abs(b));
            }
          }
        }
      }
    }
  }
  
  
  if (random8(100) < 3) {
    // Matrix: noise-driven quantum foam
    for (uint8_t i = 0; i < 5; i++) {
      uint8_t x = random8(MATRIX_WIDTH);
      uint8_t y = random8(MATRIX_HEIGHT);
      uint8_t fieldNoise = inoise8(x * 30, y * 40, quantumTime);
      if (fieldNoise > 235) {
        ledsMatrix[XY(x, y)] += CHSV(quantumHue + 128, 150, 80);
      }
    }

    // Disc: ring-banded quantum foam - every other ring gets a tint
    for (uint8_t r = 0; r < DISC_RING_COUNT; r += 2) {
      uint8_t len = DISC_RING_LENS[r];
      uint16_t p = random16(len);
      uint8_t fn = inoise8(p * 40 + r * 30, quantumTime);
      if (fn > 225) {
        ledsDisc[DISC_RING_OFFSETS[r] + p] += CHSV(quantumHue + 160 - r * 8, 180, 90);
      }
    }

    // Strip: constructive interference nodes between close strip particles
    for (uint8_t i = 0; i < MAX_QUANTUM_PARTICLES; i++) {
      if (!particles[i].active || particles[i].strip != 2) continue;
      for (uint8_t j = i + 1; j < MAX_QUANTUM_PARTICLES; j++) {
        if (!particles[j].active || particles[j].strip != 2) continue;
        uint16_t pa = particles[i].position;
        uint16_t pb = particles[j].position;
        uint16_t dist = (pa > pb) ? (pa - pb) : (pb - pa);
        if (dist < 20 && dist > 0) {
          uint16_t midpoint = (pa + pb) / 2;
          uint8_t nodeHue = quantumHue + 32;
          uint8_t nodeBright = 255 - (dist * 12);
          ledsStrip[midpoint] += CHSV(nodeHue, 200, nodeBright);
          if (midpoint > 0)                    ledsStrip[midpoint - 1] += CHSV(nodeHue, 180, nodeBright >> 1);
          if (midpoint < LED_COUNT_STRIP - 1)  ledsStrip[midpoint + 1] += CHSV(nodeHue, 180, nodeBright >> 1);
        }
      }
    }
  }

  // CLOUD: quantum vacuum foam + projected particle echoes onto the loop.
  // Cloud already gets faded by fade_all(90) above.
  for (uint8_t i = 0; i < MAX_QUANTUM_PARTICLES; i++) {
    if (!particles[i].active) continue;
    // Project particle position onto cloud loop, scaled by its source surface.
    uint16_t srcLen = 1;
    switch (particles[i].strip) {
      case 2: srcLen = LED_COUNT_STRIP;  break;
      case 3: srcLen = LED_COUNT_MATRIX; break;
      case 5: srcLen = LED_COUNT_DISC;   break;
    }
    if (srcLen == 0) continue;
    uint16_t cIdx = (uint32_t)particles[i].position * LED_COUNT_CLOUD / srcLen;
    if (cIdx >= LED_COUNT_CLOUD) cIdx = LED_COUNT_CLOUD - 1;
    uint8_t pulseBright = sin8(particles[i].phase);
    uint8_t brightness = scale8(particles[i].energy, pulseBright);
    if (particles[i].energy < 60 && sin8(particles[i].phase * 3) < 100) brightness = 0;
    uint8_t hue = quantumHue + (sin8(particles[i].phase * 2) >> 3);
    if (brightness) {
      ledsCloud[cIdx] += CHSV(hue, 255, brightness);
      ledsCloud[(cIdx + 1) % LED_COUNT_CLOUD] += CHSV(hue, 200, brightness >> 1);
      ledsCloud[(cIdx + LED_COUNT_CLOUD - 1) % LED_COUNT_CLOUD] += CHSV(hue, 200, brightness >> 1);
    }
  }
  // background foam
  for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
    uint8_t fn = inoise8(i * 100 + 7000, quantumTime);
    if (fn > 225) {
      ledsCloud[i] += CHSV(quantumHue + 128, 160, fn - 200);
    }
  }
}

void effectCellularInfection(float pm) {
  
  float ratio = pmRatio(pm);
  ratio = constrain(ratio, 0.0f, 1.0f);
  uint8_t pollution = uint8_t(ratio * 255.0f);

  
  static uint32_t lastCAUpdate = 0;
  uint32_t now = nowMs();
  uint16_t updateInterval = map(pollution, 0, 255, 85, 25); 
  bool updateAutomata = false;
  if (now - lastCAUpdate > updateInterval) {
    lastCAUpdate = now;
    updateAutomata = true;
  }

  
  static uint8_t stripCells[LED_COUNT_STRIP];
  static uint8_t stripNext[LED_COUNT_STRIP];
  static bool stripInit = false;


  static uint8_t matrixCells[MATRIX_WIDTH][MATRIX_HEIGHT];
  static uint8_t matrixNext[MATRIX_WIDTH][MATRIX_HEIGHT];
  static bool matrixInit = false;
  static uint8_t discCells[LED_COUNT_DISC];
  static uint8_t discNext[LED_COUNT_DISC];
  static bool discCInit = false;
  static uint8_t cloudCells[LED_COUNT_CLOUD];
  static uint8_t cloudNext[LED_COUNT_CLOUD];
  static bool cloudCInit = false;

  
  uint8_t birthThreshold = map(pollution, 0, 255, 4, 2);
  uint8_t surviveMin = map(pollution, 0, 255, 3, 1);
  uint8_t surviveMax = map(pollution, 0, 255, 4, 6);
  uint8_t mutationChance = map(pollution, 0, 255, 1, 25);
  uint8_t decayRate = map(pollution, 0, 255, 7, 2);
  uint8_t outbreakChance = map(pollution, 0, 255, 0, 12);

  static uint16_t colorPhase = 0;
  if (updateAutomata) colorPhase += map(pollution, 0, 255, 1, 8);
  uint8_t baseHue = map(pollution, 0, 255, 180, 0);

  
  if (!stripInit) { for (uint16_t i = 0; i < LED_COUNT_STRIP; i++) stripCells[i] = random8(100) < 25 ? random8(100, 255) : 0; stripInit = true; }

  if (!matrixInit) {
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++)
      for (uint8_t y = 0; y < MATRIX_HEIGHT; y++)
        matrixCells[x][y] = random8(100) < 20 ? random8(100, 255) : 0;
    matrixInit = true;
  }
  if (!discCInit) {
    for (uint16_t i = 0; i < LED_COUNT_DISC; i++) discCells[i] = random8(100) < 25 ? random8(100, 255) : 0;
    discCInit = true;
  }
  if (!cloudCInit) {
    for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) cloudCells[i] = random8(100) < 30 ? random8(120, 255) : 0;
    cloudCInit = true;
  }

  
  if (updateAutomata) {
    
    if (random8(100) < outbreakChance) {


    }

    if (random8(100) < outbreakChance/2) {
      uint16_t pos = random16(LED_COUNT_STRIP);
      for (uint8_t i = 0; i < 5 && (pos+i) < LED_COUNT_STRIP; i++)
        stripCells[pos+i] = random8(180,255);
    }
    if (random8(100) < outbreakChance) {
      uint8_t x = random8(MATRIX_WIDTH), y = random8(MATRIX_HEIGHT);
      for (int8_t dx=-1; dx<=1; dx++) for (int8_t dy=-1; dy<=1; dy++) {
        int8_t nx = x+dx, ny = y+dy;
        if (nx>=0 && nx<MATRIX_WIDTH && ny>=0 && ny<MATRIX_HEIGHT) matrixCells[nx][ny] = random8(180,255);
      }
    }

    
    for (uint16_t i = 0; i < LED_COUNT_STRIP; i++) {
      uint8_t neighbors = 0;
      if (i > 1 && stripCells[i-2] > 50) neighbors++;
      if (i > 0 && stripCells[i-1] > 50) neighbors++;
      if (i < LED_COUNT_STRIP-1 && stripCells[i+1] > 50) neighbors++;
      if (i < LED_COUNT_STRIP-2 && stripCells[i+2] > 50) neighbors++;
      stripNext[i] = stripCells[i] > 50
        ? (neighbors >= surviveMin && neighbors <= surviveMax ? qadd8(stripCells[i], 20) : qsub8(stripCells[i], 60))
        : (neighbors >= birthThreshold ? 140 : qsub8(stripCells[i], decayRate));
      if (random8(100) < mutationChance/2) stripNext[i] = random8(100) < 50 ? 255 : 0;
    }
    memcpy(stripCells, stripNext, sizeof(stripCells));

    
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
      for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
        uint8_t neighbors = 0;
        for (int8_t dx=-1; dx<=1; dx++) for (int8_t dy=-1; dy<=1; dy++) {
          if (!dx && !dy) continue;
          int8_t nx = x+dx, ny = y+dy;
          if (nx>=0 && nx<MATRIX_WIDTH && ny>=0 && ny<MATRIX_HEIGHT && matrixCells[nx][ny] > 50)
            neighbors++;
        }
        matrixNext[x][y] = matrixCells[x][y] > 50
          ? ((neighbors==2||neighbors==3) ? qadd8(matrixCells[x][y],35) : qsub8(matrixCells[x][y],90))
          : ((neighbors==3||(neighbors==4&&pollution>150)) ? 180 : qsub8(matrixCells[x][y],decayRate));
        if (random8(100) < mutationChance) matrixNext[x][y] = random8(100)<50?255:0;
      }
    }
    memcpy(matrixCells, matrixNext, sizeof(matrixCells));

    // DISC CA: each ring wraps; also counts one neighbor on inner + outer ring at same angular fraction
    if (random8(100) < outbreakChance) {
      uint16_t dp = random16(LED_COUNT_DISC);
      discCells[dp] = 255;
    }
    for (uint8_t r = 0; r < DISC_RING_COUNT; r++) {
      uint8_t len = DISC_RING_LENS[r];
      uint8_t off = DISC_RING_OFFSETS[r];
      for (uint8_t p = 0; p < len; p++) {
        uint8_t idx = off + p;
        uint8_t neighbors = 0;
        // same-ring neighbors (wrap)
        if (len > 1) {
          if (discCells[off + ((p + 1) % len)] > 50) neighbors++;
          if (discCells[off + ((p + len - 1) % len)] > 50) neighbors++;
        }
        // inner ring neighbor
        if (r > 0) {
          uint8_t innerLen = DISC_RING_LENS[r - 1];
          uint8_t innerP = (uint8_t)((uint16_t)p * innerLen / len);
          if (discCells[DISC_RING_OFFSETS[r - 1] + innerP] > 50) neighbors++;
        }
        // outer ring neighbor
        if (r + 1 < DISC_RING_COUNT) {
          uint8_t outerLen = DISC_RING_LENS[r + 1];
          uint8_t outerP = (uint8_t)((uint16_t)p * outerLen / len);
          if (discCells[DISC_RING_OFFSETS[r + 1] + outerP] > 50) neighbors++;
        }
        discNext[idx] = discCells[idx] > 50
          ? (neighbors >= surviveMin && neighbors <= (surviveMax + 1) ? qadd8(discCells[idx], 24) : qsub8(discCells[idx], 72))
          : (neighbors >= birthThreshold ? 165 : qsub8(discCells[idx], decayRate));
        if (random8(100) < mutationChance) discNext[idx] = random8(100) < 50 ? 255 : 0;
      }
    }
    memcpy(discCells, discNext, sizeof(discCells));

    // CLOUD CA: 1D wrap-around — neighbours are the immediate left/right cells.
    if (random8(100) < outbreakChance) {
      uint16_t cp = random16(LED_COUNT_CLOUD);
      cloudCells[cp] = 255;
    }
    for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
      uint16_t l = (i + LED_COUNT_CLOUD - 1) % LED_COUNT_CLOUD;
      uint16_t r = (i + 1) % LED_COUNT_CLOUD;
      uint16_t l2 = (i + LED_COUNT_CLOUD - 2) % LED_COUNT_CLOUD;
      uint16_t r2 = (i + 2) % LED_COUNT_CLOUD;
      uint8_t neighbors = 0;
      if (cloudCells[l]  > 50) neighbors++;
      if (cloudCells[r]  > 50) neighbors++;
      if (cloudCells[l2] > 50) neighbors++;
      if (cloudCells[r2] > 50) neighbors++;
      cloudNext[i] = cloudCells[i] > 50
        ? (neighbors >= surviveMin && neighbors <= surviveMax ? qadd8(cloudCells[i], 20) : qsub8(cloudCells[i], 60))
        : (neighbors >= birthThreshold ? 140 : qsub8(cloudCells[i], decayRate));
      if (random8(100) < mutationChance / 2) cloudNext[i] = random8(100) < 50 ? 255 : 0;
    }
    memcpy(cloudCells, cloudNext, sizeof(cloudCells));
  }


  for (uint16_t i = 0; i < LED_COUNT_STRIP; i++)
    ledsStrip[i] = CHSV(baseHue+60+(i/2)+(colorPhase>>4), stripCells[i]>0?220:0, stripCells[i]);


  for (uint8_t x = 0; x < MATRIX_WIDTH; x++)
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      uint8_t val=matrixCells[x][y];
      uint8_t hue=baseHue+(x*4)+(y*8)+(colorPhase>>1);
      if(val>100)val=qadd8(val,sin8((colorPhase+x*20+y*30)&0xFF)>>2);
      ledsMatrix[XY(x,y)]=CHSV(hue,val>0?245:0,val);
    }
  for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
    uint8_t rIdx, pIdx;
    discIdxToRing(i, rIdx, pIdx);
    uint8_t val = discCells[i];
    uint8_t hue = baseHue + 120 + rIdx * 12 + pIdx * 3 + (colorPhase >> 2);
    ledsDisc[i] = CHSV(hue, val > 0 ? 240 : 0, val);
  }
  for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
    uint8_t val = cloudCells[i];
    uint8_t hue = baseHue + 200 + (uint8_t)(i * 6) + (colorPhase >> 3);
    ledsCloud[i] = CHSV(hue, val > 0 ? 230 : 0, val);
  }
}

void effectElectricalStorm(float pm) {
    float ratio = pmRatio(pm);
    uint8_t pollution = uint8_t(ratio * 255.0f);
    
    
    static uint16_t stormTime = 0;
    static uint32_t lastCapacitorCharge = 0;
    static uint8_t lightningTimer = 0;
    static bool capacitorCharged = false;
    
    uint8_t chargeSpeed = map(pollution, 0, 255, 8, 2);  
    uint8_t fieldRotationSpeed = map(pollution, 0, 255, 1, 6);
    uint8_t lightningChance = map(pollution, 0, 255, 1, 15);
    
    stormTime += fieldRotationSpeed;
    
    
    static uint8_t chargeLevel = 0;
    
    uint16_t chargeInterval = max<uint16_t>(10, chargeSpeed * 50); 
    uint32_t now = nowMs();
    if (now - lastCapacitorCharge > chargeInterval) {
        lastCapacitorCharge = now;
        if (!capacitorCharged) {
            chargeLevel = qadd8(chargeLevel, 5);
            if (chargeLevel >= 200) {
                capacitorCharged = true;
                chargeLevel = 255;
            }
        } else {
            chargeLevel = qsub8(chargeLevel, 3);
            if (chargeLevel <= 50) {
                capacitorCharged = false;
            }
        }
    }
    
    
    static uint8_t lightningBranch[MATRIX_HEIGHT][MATRIX_WIDTH];
    static bool lightningActive = false;
    static uint8_t lightningX = 0;
    
    lightningTimer++;
    if (lightningTimer > 100) {
        if (random8(100) < lightningChance) {
            lightningActive = true;
            lightningTimer = 0;
            lightningX = random8(MATRIX_WIDTH);
            
            
            for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
                for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
                    lightningBranch[y][x] = 0;
                }
            }
        }
    }
    
    if (lightningActive) {
        
        static uint8_t lightningY = 0;
        lightningY++;
        
        if (lightningY < MATRIX_HEIGHT) {
            
            for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
                uint8_t distance = abs(int8_t(x) - int8_t(lightningX));
                if (distance < 3) {
                    uint8_t intensity = 255 - (distance * 80);
                    lightningBranch[lightningY][x] = intensity;
                }
            }
            
            
            if (random8(10) < 3) {
                lightningX += random8(2) ? 1 : -1;
                lightningX = constrain(lightningX, 0, MATRIX_WIDTH - 1);
            }
        } else {
            lightningActive = false;
            lightningY = 0;
        }
        
        
        for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
            for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
                if (lightningBranch[y][x] > 0) {
                    ledsMatrix[XY(x, y)] = CHSV(160, 200, lightningBranch[y][x]);
                    lightningBranch[y][x] = lightningBranch[y][x] * 0.8; 
                } else {
                    ledsMatrix[XY(x, y)] = CRGB::Black;
                }
            }
        }
    } else {
        fadeToBlackBy(ledsMatrix, LED_COUNT_MATRIX, 20);
    }
    
    
    static uint16_t emFieldPhase = 0;
    emFieldPhase += fieldRotationSpeed * 2;
    
    
    static uint16_t arcPosition = 0;
    static int8_t arcDirection = 1;
    static uint8_t arcIntensity = 0;
    
    uint8_t arcStep = map(pollution, 0, 255, 1, 4);
    int32_t nextPos = (int32_t)arcPosition + (int32_t)arcDirection * arcStep;
    if (nextPos >= (int32_t)LED_COUNT_STRIP) {
        nextPos = LED_COUNT_STRIP - 1;
        arcDirection = -1;
        arcIntensity = 255;
    } else if (nextPos < 0) {
        nextPos = 0;
        arcDirection = 1;
        arcIntensity = 255;
    } else {
        arcIntensity = map(nextPos, 0, LED_COUNT_STRIP, 100, 150);
    }
    arcPosition = (uint16_t)nextPos;
    
    
    for (uint16_t i = 0; i < LED_COUNT_STRIP; i++) {
        uint16_t distance = abs(int16_t(i) - int16_t(arcPosition));
        if (distance < 5) {
            uint8_t brightness = arcIntensity - (distance * 20);
            ledsStrip[i] = CHSV(45, 255, qadd8(brightness, random8(30))); 
        } else {
            
            ledsStrip[i] = CHSV(24, 200, 10);
        }
    }
    
    
    if (random8(100) < 30) {
        ledsStrip[arcPosition] = CHSV(0, 0, 255); 
    }
    
    
    static uint16_t resonancePhase = 0;
    resonancePhase += fieldRotationSpeed * 3;
    
    uint8_t wavelength = map(pollution, 0, 255, 8, 20); 
    
    
    if (capacitorCharged && chargeLevel < 100) {
        lightningChance = 50; 
    }
    
    
    if (arcPosition % 50 == 0) {

    }

    // DISC: Faraday disc - every ring carries its own standing wave frequency,
    // plus discharge bolts walking inward when the capacitor fires
    fadeToBlackBy(ledsDisc, LED_COUNT_DISC, 18);
    for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
        uint8_t rIdx, pIdx;
        discIdxToRing(i, rIdx, pIdx);
        uint8_t ang = discAngle8(rIdx, pIdx);
        uint8_t fw = sin8((uint8_t)(ang * (3 + rIdx) + resonancePhase));
        uint8_t rw = sin8((uint8_t)(ang * (3 + rIdx) - resonancePhase));
        uint8_t standing = (uint8_t)(((uint16_t)fw + rw) >> 1);
        uint8_t v = scale8(standing, chargeLevel);
        uint8_t hue = capacitorCharged ? 96 : 160;
        ledsDisc[i] = CHSV(hue + rIdx * 4, 200, qadd8(v, 30));
    }
    static uint8_t discBoltAng = 0;
    static int8_t  discBoltRing = -1;
    if (discBoltRing < 0 && capacitorCharged && random8(100) < lightningChance) {
        discBoltAng = random8();
        discBoltRing = DISC_RING_COUNT - 1;
    }
    if (discBoltRing >= 0) {
        uint8_t r = (uint8_t)discBoltRing;
        uint8_t len = DISC_RING_LENS[r];
        uint8_t pos = (uint8_t)((uint16_t)discBoltAng * len / 256U);
        if (pos >= len) pos = len - 1;
        ledsDisc[DISC_RING_OFFSETS[r] + pos] = CRGB(255, 255, 255);
        if (random8(10) < 5) discBoltAng += (uint8_t)((int8_t)random8(3) - 1);
        discBoltRing--;
    }

    // CLOUD: standing-wave electric field around the loop, bolts when capacitor fires.
    fadeToBlackBy(ledsCloud, LED_COUNT_CLOUD, 20);
    for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
        uint8_t ang = (uint8_t)((uint32_t)i * 256u / LED_COUNT_CLOUD);
        uint8_t fw = sin8((uint8_t)(ang * 3 + resonancePhase));
        uint8_t rw = sin8((uint8_t)(ang * 3 - resonancePhase));
        uint8_t standing = (uint8_t)(((uint16_t)fw + rw) >> 1);
        uint8_t v = scale8(standing, chargeLevel);
        uint8_t hue = capacitorCharged ? 96 : 160;
        ledsCloud[i] = CHSV(hue, 200, qadd8(v, 30));
    }
    if (capacitorCharged && random8(100) < lightningChance) {
        // Bolt arcs across one chord of the loop
        uint16_t boltA = random16(LED_COUNT_CLOUD);
        uint16_t boltB = (boltA + LED_COUNT_CLOUD / 2 + (uint16_t)random8(10) - 5) % LED_COUNT_CLOUD;
        ledsCloud[boltA] = CRGB(255, 255, 255);
        ledsCloud[boltB] = CRGB(255, 255, 255);
        for (int8_t s = 1; s <= 3; s++) {
            ledsCloud[(boltA + LED_COUNT_CLOUD - s) % LED_COUNT_CLOUD] += CHSV(160, 100, 200 >> s);
            ledsCloud[(boltA + s) % LED_COUNT_CLOUD] += CHSV(160, 100, 200 >> s);
            ledsCloud[(boltB + LED_COUNT_CLOUD - s) % LED_COUNT_CLOUD] += CHSV(160, 100, 200 >> s);
            ledsCloud[(boltB + s) % LED_COUNT_CLOUD] += CHSV(160, 100, 200 >> s);
        }
    }
}

void effectTeslaCoil(float pm) {
  const float r = pmRatio(pm);                  // 0..1 "energy"
  const uint8_t energy = (uint8_t)(r * 255.0f); // 0..255

  const uint32_t now = nowMs();
  static uint32_t lastFrame = 0;
  const uint16_t frameMs = (uint16_t)map(energy, 0, 255, 26, 10);
  if (lastFrame && (now - lastFrame) < frameMs) return;
  uint32_t dt = lastFrame ? (now - lastFrame) : frameMs;
  if (dt > 80) dt = 80;
  lastFrame = now;

  const uint8_t COIL_HUE    = 176;  // blue-violet
  const uint8_t CORONA_SAT  = 220;  // base corona saturation (high)
  const uint8_t CORONA_V    = (uint8_t)map(energy, 0, 255, 8, 22);
  const uint8_t DECAY_BASE  = (uint8_t)map(energy, 0, 255, 242, 222);
  const uint8_t MICROFLICK  = (uint8_t)map(energy, 0, 255, 10, 40);

  auto wrapIndex = [](int32_t i, uint16_t count) -> uint16_t {
    while (i < 0) i += (int32_t)count;
    while (i >= (int32_t)count) i -= (int32_t)count;
    return (uint16_t)i;
  };
  auto wrapDist = [](uint16_t a, uint16_t b, uint16_t count) -> uint16_t {
    uint16_t d = (a > b) ? (a - b) : (b - a);
    uint16_t alt = count - d;
    return (alt < d) ? alt : d;
  };
  auto decayArr = [&](uint8_t* arr, uint16_t n, uint8_t decay) {
    for (uint16_t i = 0; i < n; i++) arr[i] = scale8(arr[i], decay);
  };
  auto addIon = [&](uint8_t* arr, uint16_t n, uint16_t idx, uint8_t amt) {
    if (!n) return;
    if (idx >= n) return;
    arr[idx] = qadd8(arr[idx], amt);
  };

  static bool inited = false;


  static uint8_t ionStrip[LED_COUNT_STRIP];
  static uint8_t ionMatrix[LED_COUNT_MATRIX];

  static uint8_t ionDisc[LED_COUNT_DISC];
  static uint8_t ionCloud[LED_COUNT_CLOUD];

  if (!inited) {


    memset(ionStrip,  0, sizeof(ionStrip));
    memset(ionMatrix, 0, sizeof(ionMatrix));

    memset(ionDisc,   0, sizeof(ionDisc));
    memset(ionCloud,  0, sizeof(ionCloud));
    inited = true;
  }

  const uint8_t decay = DECAY_BASE;


  decayArr(ionStrip,  LED_COUNT_STRIP,  (uint8_t)qsub8(decay, 6));
  decayArr(ionMatrix, LED_COUNT_MATRIX, (uint8_t)qsub8(decay, 10));

  decayArr(ionDisc,   LED_COUNT_DISC,   (uint8_t)qsub8(decay, 4));
  decayArr(ionCloud,  LED_COUNT_CLOUD,  (uint8_t)qsub8(decay, 4));

  static uint16_t burst = 0;
  static uint8_t strobeFrames = 0;

  if (burst > 0) {
    uint16_t drop = (uint16_t)(dt * (uint32_t)map(energy, 0, 255, 10, 60));
    burst = (drop >= burst) ? 0 : (burst - drop);
  }

  uint8_t baseStrikeChance = (uint8_t)map(energy, 0, 255, 6, 58);
  if (burst > 0) {
    baseStrikeChance = qadd8(baseStrikeChance, (uint8_t)map(energy, 0, 255, 20, 70));
  }
  bool strike = (random8() < baseStrikeChance);

  if (strike) {
    uint16_t add = (uint16_t)(1200 + (uint16_t)energy * 12);
    uint32_t sum = (uint32_t)burst + add;
    burst = (sum > 65000UL) ? 65000 : (uint16_t)sum;

    if (random8() < (uint8_t)map(energy, 0, 255, 10, 34)) {
      strobeFrames = 1 + (random8() & 0x01);
    }
  }
  if (strobeFrames > 0) strobeFrames--;

  struct TC_Shock {
    bool active;
    uint16_t center;
    uint16_t radius_q8;
    uint16_t speed_q8;
    uint8_t  width;
    uint8_t  amp;
    uint16_t lifeMs;
  };
  static TC_Shock shocks[3];
  static bool shocksInit = false;
  if (!shocksInit) {
    for (auto &s : shocks) s.active = false;
    shocksInit = true;
  }

  if (strike) {
    for (auto &s : shocks) {
      if (!s.active) {
        s.active    = true;

        s.radius_q8 = 0;
        s.speed_q8  = (uint16_t)((uint32_t)map(energy, 0, 255, 140, 520) * 256UL);
        s.width     = (uint8_t)map(energy, 0, 255, 2, 5);
        s.amp       = (uint8_t)map(energy, 0, 255, 170, 255);
        s.lifeMs    = (uint16_t)map(energy, 0, 255, 180, 360);
        break;
      }
    }

  }

  for (auto &s : shocks) {
    if (!s.active) continue;
    if (s.lifeMs <= dt) { s.active = false; continue; }
    s.lifeMs -= (uint16_t)dt;

    uint32_t inc = (uint32_t)s.speed_q8 * (uint32_t)dt / 1000UL;
    s.radius_q8 = (uint16_t)(s.radius_q8 + (uint16_t)inc);

  }

  struct TC_Streamer {
    bool active;
    uint16_t base;
    int8_t dir;
    uint8_t len;
    uint8_t amp;
    uint16_t lifeMs;
    uint16_t life0;
  };
  static TC_Streamer streamers[5];
  static bool streamerInit = false;
  if (!streamerInit) {
    for (auto &t : streamers) t.active = false;
    streamerInit = true;
  }

  if (strike) {
    uint8_t spawn = 1 + (random8() % (uint8_t)map(energy, 0, 255, 1, 4));
    for (uint8_t s = 0; s < spawn; s++) {
      for (auto &t : streamers) {
        if (!t.active) {
          t.active = true;

          t.dir    = (random8() & 0x01) ? 1 : -1;
          t.len    = (uint8_t)map(energy, 0, 255, 6, 18) + (random8() % 7);
          t.amp    = (uint8_t)map(energy, 0, 255, 140, 255);
          t.life0  = (uint16_t)map(energy, 0, 255, 120, 260);
          t.lifeMs = t.life0;
          break;
        }
      }
    }
  }

  for (auto &t : streamers) {
    if (!t.active) continue;
    if (t.lifeMs <= dt) { t.active = false; continue; }
    t.lifeMs -= (uint16_t)dt;

    uint8_t lifeK = (uint8_t)((uint32_t)t.lifeMs * 255UL / (uint32_t)t.life0);
    for (uint8_t i = 0; i < t.len; i++) {
      uint8_t tailK = (uint8_t)(255 - (uint16_t)i * 255U / (uint16_t)(t.len ? t.len : 1));
      uint8_t v = scale8(scale8(t.amp, tailK), lifeK);


    }
  }

  static int32_t arcPos = 0;
  static uint16_t arcLife = 0;

  if (strike) {
    arcPos  = (int32_t)random16(LED_COUNT_STRIP);
    arcLife = (uint16_t)map(energy, 0, 255, 260, 900);
  }

  if (arcLife > 0) {
    uint16_t drop = (uint16_t)(dt * (uint32_t)map(energy, 0, 255, 2, 7));
    arcLife = (drop >= arcLife) ? 0 : (arcLife - drop);

    int32_t step = 1 + (energy / 42);
    arcPos += step;
    if (random8() < (uint8_t)map(energy, 0, 255, 18, 70)) {
      arcPos += (int16_t)random16(41) - 20;
    }
    arcPos = (int32_t)wrapIndex(arcPos, LED_COUNT_STRIP);

    uint8_t segCount = 2 + (energy / 64);
    uint8_t headAmp  = (uint8_t)map(energy, 0, 255, 160, 255);

    for (uint8_t s = 0; s < segCount; s++) {
      int16_t off = (int16_t)random16(37) - 18;
      uint16_t start = wrapIndex(arcPos + off, LED_COUNT_STRIP);
      uint8_t len = 4 + (random8() % (uint8_t)map(energy, 0, 255, 6, 16));

      for (uint8_t i = 0; i < len; i++) {
        uint16_t idx = (start + i) % LED_COUNT_STRIP;
        uint8_t k = (uint8_t)(255 - (uint16_t)i * 255U / (uint16_t)(len ? len : 1));
        uint8_t v = scale8(headAmp, k);
        if (random8() < (uint8_t)map(energy, 0, 255, 200, 150)) {
          ionStrip[idx] = qadd8(ionStrip[idx], v);
        }
      }
    }

    addIon(ionStrip, LED_COUNT_STRIP, (uint16_t)arcPos, (uint8_t)qadd8(headAmp, 40));
  }

  auto drawMatrixBolt = [&](uint8_t originX, uint8_t originY, uint8_t amp, uint8_t branchiness) {
    int8_t x = (int8_t)originX;
    int16_t y = (int16_t)originY;

    uint8_t steps = 10 + (energy / 10);
    uint8_t v = amp;

    for (uint8_t s = 0; s < steps; s++) {
      if (y < 0 || y >= MATRIX_HEIGHT) break;
      if (x < 0) x = 0;
      if (x >= MATRIX_WIDTH) x = MATRIX_WIDTH - 1;

      uint16_t idx = XY((uint8_t)x, (uint8_t)y);
      ionMatrix[idx] = qadd8(ionMatrix[idx], v);

      if (random8() < branchiness) {
        int8_t bx = x;
        int16_t by = y;
        uint8_t bv = scale8(v, 180);
        uint8_t bSteps = 3 + (random8() % 8);
        for (uint8_t b = 0; b < bSteps; b++) {
          by += 1;
          if (by >= MATRIX_HEIGHT) break;
          if (random8() < 130) bx += (random8() & 0x01) ? 1 : -1;
          if (bx < 0) bx = 0;
          if (bx >= MATRIX_WIDTH) bx = MATRIX_WIDTH - 1;
          uint16_t bIdx = XY((uint8_t)bx, (uint8_t)by);
          ionMatrix[bIdx] = qadd8(ionMatrix[bIdx], bv);
          bv = scale8(bv, 210);
        }
      }

      y += 1 + (random8() < 22);
      if (random8() < 120) x += (random8() & 0x01) ? 1 : -1;
      v = scale8(v, 210);
      if (v < 12) break;
    }
  };

  uint8_t hazeDots = (uint8_t)map(energy, 0, 255, 1, 12);
  for (uint8_t i = 0; i < hazeDots; i++) {
    uint16_t p = random16(LED_COUNT_MATRIX);
    ionMatrix[p] = qadd8(ionMatrix[p], (uint8_t)map(energy, 0, 255, 4, 18));
  }

  if (strike || (burst > 0 && random8() < (uint8_t)map(energy, 0, 255, 18, 85))) {
    uint8_t ox = random8(MATRIX_WIDTH);
    uint8_t oy = (random8() < 200) ? 0 : random8(4);
    uint8_t amp = (uint8_t)map(energy, 0, 255, 160, 255);
    uint8_t br  = (uint8_t)map(energy, 0, 255, 18, 90);
    drawMatrixBolt(ox, oy, amp, br);
  }

  // DISC: each strike fires a radial plasma streamer from a random angle,
  // walking from center outward. Between strikes, ambient hum lives on outer rings.
  struct TC_DiscStreamer {
    bool active;
    uint8_t angle;      // fixed angular direction (0..255)
    int8_t ring;        // current ring being ignited
    uint8_t amp;
    uint16_t lifeMs;
  };
  static TC_DiscStreamer discStreams[3];
  static bool discStreamsInit = false;
  if (!discStreamsInit) {
    for (auto &d : discStreams) d.active = false;
    discStreamsInit = true;
  }
  if (strike) {
    for (auto &d : discStreams) {
      if (!d.active) {
        d.active = true;
        d.angle = random8();
        d.ring = 0;
        d.amp = (uint8_t)map(energy, 0, 255, 150, 255);
        d.lifeMs = (uint16_t)map(energy, 0, 255, 220, 500);
        break;
      }
    }
    ionDisc[0] = qadd8(ionDisc[0], (uint8_t)map(energy, 0, 255, 180, 255));
  }
  for (auto &d : discStreams) {
    if (!d.active) continue;
    if (d.lifeMs <= dt) { d.active = false; continue; }
    d.lifeMs -= (uint16_t)dt;
    if (d.ring < DISC_RING_COUNT) {
      uint8_t rLen = DISC_RING_LENS[d.ring];
      uint8_t pos = (uint8_t)((uint16_t)d.angle * rLen / 256U);
      if (pos >= rLen) pos = rLen - 1;
      ionDisc[DISC_RING_OFFSETS[d.ring] + pos] = qadd8(ionDisc[DISC_RING_OFFSETS[d.ring] + pos], d.amp);
      // branch sideways a little
      if (random8() < 180 && rLen > 1) {
        ionDisc[DISC_RING_OFFSETS[d.ring] + ((pos + 1) % rLen)] = qadd8(
          ionDisc[DISC_RING_OFFSETS[d.ring] + ((pos + 1) % rLen)], d.amp >> 1);
      }
      d.ring++;
      d.amp = scale8(d.amp, 220);
    }
  }
  // ambient hum on outer rings
  if (random8() < (uint8_t)map(energy, 0, 255, 30, 120)) {
    uint8_t r = DISC_RING_COUNT - 1 - (random8() & 1);
    uint8_t len = DISC_RING_LENS[r];
    uint8_t p = random8(len);
    ionDisc[DISC_RING_OFFSETS[r] + p] = qadd8(ionDisc[DISC_RING_OFFSETS[r] + p],
                                              (uint8_t)map(energy, 0, 255, 6, 26));
  }

  fill_all(CRGB::Black);

  const uint8_t flick = (sin8((uint16_t)(now >> 2)) * MICROFLICK) >> 8;
  const uint8_t coronaV = qadd8(CORONA_V, flick);

  auto paint1D = [&](CRGB* leds, uint16_t n, uint8_t* ion) {
    for (uint16_t i = 0; i < n; i++) {
      leds[i] = CHSV(COIL_HUE, CORONA_SAT, coronaV);
      uint8_t a = ion[i];
      if (a) {
        uint8_t sat = 255 - a;
        uint8_t v   = qadd8(a, coronaV);
        leds[i] += CHSV(COIL_HUE, sat, v);
      }
      if ((i & 0x03) == 0 && random8() < (uint8_t)(MICROFLICK >> 1)) {
        leds[i] += CHSV(COIL_HUE, 255, (uint8_t)map(energy, 0, 255, 3, 14));
      }
    }
  };


  paint1D(ledsStrip,    LED_COUNT_STRIP,    ionStrip);

  paint1D(ledsDisc,     LED_COUNT_DISC,     ionDisc);

  // CLOUD: corona base, with ion sparks injected during strikes (radial-bolt
  // pattern around the loop).
  if (strike) {
    uint8_t bolts = 1 + (energy / 60);
    for (uint8_t b = 0; b < bolts; b++) {
      uint16_t centre = random16(LED_COUNT_CLOUD);
      uint8_t amp = (uint8_t)map(energy, 0, 255, 140, 255);
      for (int8_t s = -2; s <= 2; s++) {
        uint16_t idx = (centre + LED_COUNT_CLOUD + s) % LED_COUNT_CLOUD;
        uint8_t falloff = 255 - (uint8_t)abs(s) * 70;
        ionCloud[idx] = qadd8(ionCloud[idx], scale8(amp, falloff));
      }
    }
  }
  paint1D(ledsCloud,    LED_COUNT_CLOUD,    ionCloud);

  for (uint16_t i = 0; i < LED_COUNT_MATRIX; i++) {
    ledsMatrix[i] = CHSV(COIL_HUE, CORONA_SAT, (uint8_t)qsub8(coronaV, 3));
    uint8_t a = ionMatrix[i];
    if (a) ledsMatrix[i] += CHSV(COIL_HUE, 255 - a, a);
  }

  if (strobeFrames > 0) {
    uint8_t w = (uint8_t)map(energy, 0, 255, 80, 180);
    CRGB st = CRGB(w, w, w);

    for (uint16_t i = 0; i < LED_COUNT_STRIP; i++)    ledsStrip[i]    += st;

    for (uint16_t i = 0; i < LED_COUNT_DISC; i++)     ledsDisc[i]     += st;
    for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++)    ledsCloud[i]    += st;
  }
}

void effectQuantumLattice(float pm) {
  float ratio = pmRatio(pm);
  uint8_t pollution = (uint8_t)(ratio * 255.0f);

  static uint16_t phase = 0;
  static uint16_t phaseFast = 0;
  static uint16_t phaseSlow = 0;

  uint8_t speed = (uint8_t)map(pollution, 0, 255, 1, 8);
  phase += speed;
  phaseFast += (uint16_t)(speed * 3);
  phaseSlow += (uint16_t)(speed + 1);

  uint8_t baseHue = (uint8_t)(150 + (phaseSlow >> 5));
  uint8_t accentHue = baseHue + 40;
  uint8_t flareHue = baseHue + 110;

  uint8_t fade = (uint8_t)map(pollution, 0, 255, 52, 20);
  uint8_t matrixFade = (uint8_t)map(pollution, 0, 255, 44, 18);


  fadeToBlackBy(ledsStrip, LED_COUNT_STRIP, fade);
  fadeToBlackBy(ledsMatrix, LED_COUNT_MATRIX, matrixFade);

  fadeToBlackBy(ledsDisc, LED_COUNT_DISC, fade);
  fadeToBlackBy(ledsCloud, LED_COUNT_CLOUD, fade);

  uint8_t gate = (uint8_t)map(pollution, 0, 255, 210, 145);
  uint8_t baseCap = (uint8_t)map(pollution, 0, 255, 12, 60);
  uint8_t accentCap = (uint8_t)map(pollution, 0, 255, 36, 130);
  uint8_t flareCap = (uint8_t)map(pollution, 0, 255, 70, 200);
  uint8_t satBase = (uint8_t)map(pollution, 0, 255, 110, 190);

  uint8_t radius = (uint8_t)map(pollution, 0, 255, 18, 52);
  uint8_t radiusScale = (uint8_t)(255 / radius);

  struct ChaosOrb {
    uint8_t x;
    uint8_t y;
    uint8_t hue;
    uint8_t energy;
    uint8_t drift;
  };

  static const uint8_t ORB_COUNT = 5;
  static ChaosOrb orbs[ORB_COUNT];
  static bool init = false;
  if (!init) {
    for (uint8_t i = 0; i < ORB_COUNT; i++) {
      orbs[i].x = random8();
      orbs[i].y = random8();
      orbs[i].hue = (uint8_t)(baseHue + random8(80));
      orbs[i].energy = random8(120, 200);
      orbs[i].drift = random8();
    }
    init = true;
  }

  uint8_t phaseFast8 = (uint8_t)phaseFast;
  uint8_t phaseSlow8 = (uint8_t)phaseSlow;

  for (uint8_t i = 0; i < ORB_COUNT; i++) {
    uint16_t nx = (uint16_t)orbs[i].x * 3 + phaseFast;
    uint16_t ny = (uint16_t)orbs[i].y * 3 + phaseSlow;
    uint8_t n = inoise8(nx, ny);

    int8_t dx = ((int8_t)sin8(n + orbs[i].drift) - 128) >> 4;
    int8_t dy = ((int8_t)cos8(n + orbs[i].hue) - 128) >> 4;
    int8_t jx = (int8_t)random8(5) - 2;
    int8_t jy = (int8_t)random8(5) - 2;

    orbs[i].x = (uint8_t)(orbs[i].x + dx + jx);
    orbs[i].y = (uint8_t)(orbs[i].y + dy + jy);
    orbs[i].hue = (uint8_t)(orbs[i].hue + (speed >> 1) + (n >> 6));
    orbs[i].energy = (uint8_t)(120 + (sin8((uint8_t)(phaseFast8 >> 1) + orbs[i].hue) >> 2));
    orbs[i].drift = (uint8_t)(orbs[i].drift + 1 + (n >> 5));
  }

  auto sampleField = [&](uint8_t ux, uint8_t uy, uint8_t &outHue, uint8_t &outVal) {
    uint16_t nx = (uint16_t)ux * 3;
    uint16_t ny = (uint16_t)uy * 3;

    uint8_t n1 = inoise8(nx + phase, ny + phaseSlow);
    uint8_t n2 = inoise8(nx + phaseFast, ny + (phaseFast >> 1), phaseSlow);
    uint8_t wave = (uint8_t)((sin8(ux + phaseFast8) + cos8(uy + phaseSlow8)) >> 1);

    uint8_t base = (uint8_t)(((uint16_t)n1 + n2 + wave) / 3);
    uint8_t v = scale8(base, baseCap);
    uint8_t hue = (uint8_t)(baseHue + (base >> 4));

    if (base > gate) {
      v = qadd8(v, scale8((uint8_t)(base - gate), accentCap));
      hue = (uint8_t)(accentHue + (base >> 3));
    }

    uint8_t glow = 0;
    uint8_t bestWeight = 0;
    uint8_t bestHue = hue;

    for (uint8_t o = 0; o < ORB_COUNT; o++) {
      uint8_t dx = (ux > orbs[o].x) ? (uint8_t)(ux - orbs[o].x) : (uint8_t)(orbs[o].x - ux);
      uint8_t dy = (uy > orbs[o].y) ? (uint8_t)(uy - orbs[o].y) : (uint8_t)(orbs[o].y - uy);
      uint8_t dist = qadd8(dx, dy);

      if (dist < radius) {
        uint8_t weight = (uint8_t)(radius - dist);
        uint8_t weightScaled = (uint8_t)((uint16_t)weight * radiusScale);
        uint8_t g = scale8(orbs[o].energy, weightScaled);
        glow = qadd8(glow, g);
        if (weightScaled > bestWeight) {
          bestWeight = weightScaled;
          bestHue = orbs[o].hue;
        }
      }
    }

    if (glow > 0) {
      v = qadd8(v, scale8(glow, accentCap));
      hue = bestHue;
    }

    outHue = hue;
    outVal = v;
  };


  uint16_t stripDen = (LED_COUNT_STRIP > 1) ? (LED_COUNT_STRIP - 1) : 1;

  uint16_t matrixDenX = (MATRIX_WIDTH > 1) ? (MATRIX_WIDTH - 1) : 1;
  uint16_t matrixDenY = (MATRIX_HEIGHT > 1) ? (MATRIX_HEIGHT - 1) : 1;


  uint8_t satStrip = satBase;
  uint8_t satMatrix = qsub8(satBase, 20);


  for (uint16_t i = 0; i < LED_COUNT_STRIP; i++) {
    uint8_t ux = (uint8_t)((uint32_t)i * 255 / stripDen);
    uint8_t uy = (uint8_t)(192 + (cos8((uint8_t)(i * 5) + phaseSlow8) >> 3));
    uint8_t hue, val;
    sampleField(ux, uy, hue, val);
    if (val) ledsStrip[i] += CHSV(hue, satStrip, val);
  }

  for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
      uint8_t ux = (uint8_t)((uint16_t)x * 255 / matrixDenX);
      uint8_t uy = (uint8_t)((uint16_t)y * 255 / matrixDenY);
      uint8_t hue, val;
      sampleField(ux, uy, hue, val);
      if (val) ledsMatrix[XY(x, y)] += CHSV(hue, satMatrix, val);
    }
  }


  // DISC: sample the shared lattice field at each LED's polar position,
  // scaled so inner rings read a tighter region (microscope view)
  uint8_t satDisc = qadd8(satBase, 30);
  for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
    uint8_t rIdx, pIdx;
    discIdxToRing(i, rIdx, pIdx);
    uint8_t len = DISC_RING_LENS[rIdx];
    uint8_t ang = (len > 1) ? (uint8_t)((uint32_t)pIdx * 256 / len) : 0;
    // inner rings zoom in (smaller sample radius)
    uint8_t zoom = (uint8_t)(64 + rIdx * 24);
    uint8_t ux = (uint8_t)(128 + (((int16_t)sin8(ang) - 128) * zoom / 128));
    uint8_t uy = (uint8_t)(128 + (((int16_t)cos8(ang) - 128) * zoom / 128));
    uint8_t hue, val;
    sampleField(ux, uy, hue, val);
    if (val) ledsDisc[i] += CHSV(hue + rIdx * 4, satDisc, val);
  }

  // CLOUD: sample the same shared lattice field around the loop. Treats the
  // 50-LED loop as a 1D phase wheel; samples the field at points on a unit
  // circle so the cloud reads a slowly-rotating cross-section of the lattice.
  uint8_t satCloud = qadd8(satBase, 20);
  for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
    uint8_t ang = (uint8_t)((uint32_t)i * 256 / LED_COUNT_CLOUD);
    uint8_t ux = (uint8_t)(128 + (((int16_t)sin8(ang) - 128) * 64 / 128));
    uint8_t uy = (uint8_t)(128 + (((int16_t)cos8(ang) - 128) * 64 / 128));
    uint8_t hue, val;
    sampleField(ux, uy, hue, val);
    if (val) ledsCloud[i] += CHSV(hue + (uint8_t)(i * 2), satCloud, val);
  }

  const uint16_t TOTAL_SPAN =


      LED_COUNT_MATRIX +

      LED_COUNT_STRIP +
      LED_COUNT_DISC +
      LED_COUNT_CLOUD;

  auto paintGlobal = [&](uint16_t idx, const CRGB &c) {
    uint16_t offset = idx;

    if (offset < LED_COUNT_MATRIX) {
      ledsMatrix[offset] += c;
      return;
    }
    offset -= LED_COUNT_MATRIX;

    if (offset < LED_COUNT_STRIP) {
      ledsStrip[offset] += c;
      return;
    }
    offset -= LED_COUNT_STRIP;

    if (offset < LED_COUNT_DISC) {
      ledsDisc[offset] += c;
      return;
    }
    offset -= LED_COUNT_DISC;

    if (offset < LED_COUNT_CLOUD) {
      ledsCloud[offset] += c;
      return;
    }
  };

  static uint16_t jumpPos = 0;
  static uint8_t jumpLife = 0;
  static uint8_t jumpHue = 0;

  if (TOTAL_SPAN > 0) {
    uint8_t jumpChance = (uint8_t)map(pollution, 0, 255, 2, 10);
    if (jumpLife == 0 && random8() < jumpChance) {
      jumpPos = random16(TOTAL_SPAN);
      jumpLife = random8(8, 18);
      jumpHue = (uint8_t)(flareHue + random8(30));
    }

    if (jumpLife > 0) {
      uint16_t lifeScale16 = (uint16_t)jumpLife * 14;
      uint8_t lifeScale = (lifeScale16 > 255) ? 255 : (uint8_t)lifeScale16;
      uint8_t lifeV = scale8(flareCap, lifeScale);

      for (int8_t j = -2; j <= 2; j++) {
        uint16_t pos = (uint16_t)((jumpPos + TOTAL_SPAN + j) % TOTAL_SPAN);
        uint8_t dist = (j < 0) ? (uint8_t)(-j) : (uint8_t)j;
        uint8_t fall = (uint8_t)(255 - dist * 70);
        uint8_t v = scale8(lifeV, fall);
        paintGlobal(pos, CHSV(jumpHue, 220, v));
      }
      jumpLife--;
    }
  }
}

// ============================================================================
// Four fundamental forces of physics
// ============================================================================

// Electromagnetism: pulse bouncing between bright (+) and dark (-) ends of the
// strip. Pulse picks a new random color on each bounce; lightning bolt fires
// in that color. Disc poles (right=red, left=blue) brighten on whichever side
// the pulse is currently on.
void effectElectromagnetism(float pm) {
  float ratio = pmRatio(pm);
  uint8_t pollution_level = (uint8_t)(ratio * 255);

  static int16_t emPulsePos = 0;
  static int8_t  emPulseDir = 1;
  static uint8_t emPulseHue = 0;
  static uint8_t emLightningFrames = 0;
  static uint8_t emLightningHue = 0;
  static uint8_t emLightningSeed = 0;

  uint8_t bounceSpeed = map(pollution_level, 0, 255, 1, 5);

  emPulsePos += emPulseDir * bounceSpeed;
  bool hitEnd = false;
  if (emPulsePos >= (int16_t)LED_COUNT_STRIP - 1) {
    emPulsePos = LED_COUNT_STRIP - 1;
    emPulseDir = -1;
    hitEnd = true;
  } else if (emPulsePos <= 0) {
    emPulsePos = 0;
    emPulseDir = 1;
    hitEnd = true;
  }
  if (hitEnd) {
    emPulseHue = random8();
    emLightningFrames = map(pollution_level, 0, 255, 5, 14);
    emLightningHue = emPulseHue;
    emLightningSeed = random8();
  }

  // STRIP: i=0 (right end / data input) is positive, always bright white.
  // i=LED_COUNT_STRIP-1 (left end) is negative, stays dark. Pulse bounces between.
  for (uint16_t i = 0; i < LED_COUNT_STRIP; i++) {
    uint8_t bri = 0;
    uint8_t sat = 0;
    uint8_t hue = 0;

    if (i < 6) {
      bri = map(6 - i, 0, 6, 80, 240);
    }

    int16_t dist = abs((int16_t)i - emPulsePos);
    if (dist < 14) {
      uint8_t pulseBri = map(14 - dist, 0, 14, 0, 240);
      if (pulseBri > bri) {
        bri = pulseBri;
        sat = 220;
        hue = emPulseHue;
      }
    }
    ledsStrip[i] = CHSV(hue, sat, bri);
  }

  // MATRIX: random lightning strikes scattered across the whole panel.
  fadeToBlackBy(ledsMatrix, LED_COUNT_MATRIX, 90);
  if (emLightningFrames > 0) {
    uint8_t strikesPerFrame = map(pollution_level, 0, 255, 1, 4);
    for (uint8_t s = 0; s < strikesPerFrame; s++) {
      uint8_t sx = random8(MATRIX_WIDTH);
      uint8_t sy = random8(MATRIX_HEIGHT);
      uint8_t len = random8(3, 10);
      for (uint8_t k = 0; k < len; k++) {
        uint8_t ky = sy + k;
        if (ky >= MATRIX_HEIGHT) break;
        int8_t kx = (int8_t)sx + (int8_t)random8(3) - 1;
        kx = constrain((int)kx, 0, (int)(MATRIX_WIDTH - 1));
        ledsMatrix[XY((uint8_t)kx, ky)] = CHSV(emLightningHue, 220, 255);
        if (kx > 0)                ledsMatrix[XY((uint8_t)(kx - 1), ky)] = CHSV(emLightningHue, 230, 60);
        if (kx < MATRIX_WIDTH - 1) ledsMatrix[XY((uint8_t)(kx + 1), ky)] = CHSV(emLightningHue, 230, 60);
      }
    }
    emLightningFrames--;
  }

  // DISC: left and right poles (horizontal). Brightness tracks pulse position —
  // pulse near positive end (i=0) brightens the right pole; near negative end brightens left.
  float t = (LED_COUNT_STRIP > 1)
            ? (float)emPulsePos / (float)(LED_COUNT_STRIP - 1)
            : 0.0f;
  uint8_t rightPoleBri = (uint8_t)((1.0f - t) * 255.0f);
  uint8_t leftPoleBri  = (uint8_t)(t * 255.0f);

  for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
    float dx, dy, dr; uint8_t rIdx, pIdx;
    discIdxToXY(i, dx, dy, dr, rIdx, pIdx);
    uint8_t bri;
    uint8_t hue;
    if (dx >= 0.0f) {
      float dist = sqrtf((1.0f - dx) * (1.0f - dx) + dy * dy);
      uint8_t falloff = (dist < 1.4f) ? (uint8_t)((1.4f - dist) * 180.0f) : 0;
      bri = scale8(rightPoleBri, falloff);
      hue = 0;
    } else {
      float dist = sqrtf((1.0f + dx) * (1.0f + dx) + dy * dy);
      uint8_t falloff = (dist < 1.4f) ? (uint8_t)((1.4f - dist) * 180.0f) : 0;
      bri = scale8(leftPoleBri, falloff);
      hue = 160;
    }
    ledsDisc[i] = CHSV(hue, 240, bri);
  }

  // CLOUD: the loop holds the bouncing pulse projected as a moving glow with
  // bright "poles" at idx 0 (positive) and idx LED_COUNT_CLOUD/2 (negative).
  // Lightning frames flash a slash chord across the loop.
  fadeToBlackBy(ledsCloud, LED_COUNT_CLOUD, 60);
  {
    // Always-on positive pole at idx 0 (tracks rightPoleBri)
    for (int8_t s = -2; s <= 2; s++) {
      uint16_t idx = (uint16_t)((LED_COUNT_CLOUD + s) % LED_COUNT_CLOUD);
      uint8_t falloff = (uint8_t)(255 - abs(s) * 60);
      ledsCloud[idx] += CHSV(0, 0, scale8(rightPoleBri, falloff));
    }
    // Negative pole at half-way
    uint16_t neg = LED_COUNT_CLOUD / 2;
    for (int8_t s = -2; s <= 2; s++) {
      uint16_t idx = (neg + LED_COUNT_CLOUD + s) % LED_COUNT_CLOUD;
      uint8_t falloff = (uint8_t)(255 - abs(s) * 60);
      ledsCloud[idx] += CHSV(160, 200, scale8(leftPoleBri, falloff) / 2);
    }
    // Map pulse position onto the loop
    uint16_t cPulse = (uint32_t)emPulsePos * LED_COUNT_CLOUD / LED_COUNT_STRIP;
    if (cPulse >= LED_COUNT_CLOUD) cPulse = LED_COUNT_CLOUD - 1;
    for (int8_t s = -3; s <= 3; s++) {
      uint16_t idx = (cPulse + LED_COUNT_CLOUD + s) % LED_COUNT_CLOUD;
      uint8_t v = (uint8_t)(220 - abs(s) * 40);
      ledsCloud[idx] += CHSV(emPulseHue, 220, v);
    }
  }
  if (emLightningFrames > 0) {
    // brief flash across a few random spots in the lightning hue
    for (uint8_t s = 0; s < 4; s++) {
      uint16_t idx = random16(LED_COUNT_CLOUD);
      ledsCloud[idx] = CHSV(emLightningHue, 220, 255);
    }
  }
}

// Gravity: a solid wave climbs up the matrix toward the disc. Strip lines
// converge from both ends to center in sync. Disc is a dim, dense mass that
// shudders when the wave reaches it.
void effectGravity(float pm) {
  float ratio = pmRatio(pm);
  uint8_t pollution_level = (uint8_t)(ratio * 255);

  static uint8_t gravWaveY = MATRIX_HEIGHT - 1;
  static uint8_t gravStepCounter = 0;
  static uint8_t gravShudderFrames = 0;

  uint8_t waveSpeed = map(pollution_level, 0, 255, 2, 9);
  gravStepCounter += waveSpeed;
  if (gravStepCounter >= 16) {
    gravStepCounter -= 16;
    if (gravWaveY == 0) {
      gravShudderFrames = 10;
      gravWaveY = MATRIX_HEIGHT - 1;
    } else {
      gravWaveY--;
    }
  }

  // MATRIX: thick wave band traveling away from the disc (top → bottom),
  // deep blue → purple. Trail follows behind (above the leading edge).
  fadeToBlackBy(ledsMatrix, LED_COUNT_MATRIX, 35);
  const uint8_t waveThickness = 4;
  for (uint8_t k = 0; k < waveThickness; k++) {
    int8_t y = (int8_t)gravWaveY + k;
    if (y < 0 || y >= (int8_t)MATRIX_HEIGHT) continue;
    uint8_t bri = map(k, 0, waveThickness - 1, 230, 90);
    uint8_t hue = map(y, 0, MATRIX_HEIGHT - 1, 160, 192);
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
      ledsMatrix[XY(x, (uint8_t)y)] = CHSV(hue, 240, bri);
    }
  }

  // DISC: dim mass concentrated toward the center; shudders when wave arrives.
  uint8_t shudderAmt = 0;
  if (gravShudderFrames > 0) {
    shudderAmt = random8(80, 160);
    gravShudderFrames--;
  }
  for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
    float dx, dy, dr; uint8_t rIdx, pIdx;
    discIdxToXY(i, dx, dy, dr, rIdx, pIdx);
    float radial = 1.0f - dr;
    uint8_t baseBri = (uint8_t)(radial * 70.0f) + 18;
    if (gravShudderFrames > 0 && random8() > 200) {
      baseBri = qadd8(baseBri, scale8(shudderAmt, random8()));
    }
    uint8_t hue = 192 - (uint8_t)(dr * 32.0f);
    ledsDisc[i] = CHSV(hue, 240, baseBri);
  }

  // STRIP: two lines spawn at the center, travel outward in sync with the wave.
  fadeToBlackBy(ledsStrip, LED_COUNT_STRIP, 28);
  int16_t center = (int16_t)(LED_COUNT_STRIP / 2);
  int16_t halfStrip = (int16_t)(LED_COUNT_STRIP / 2);
  uint8_t waveProgress = (MATRIX_HEIGHT - 1) - gravWaveY;
  int16_t spreadOffset = (int16_t)map((uint16_t)waveProgress, 0, MATRIX_HEIGHT - 1, 0, halfStrip);
  int16_t rightPos = center + spreadOffset;
  int16_t leftPos  = center - spreadOffset;
  for (int8_t off = -2; off <= 2; off++) {
    int16_t r = rightPos + off;
    int16_t l = leftPos + off;
    uint8_t bri = (off == 0) ? 230 : 110;
    if (r >= 0 && r < (int16_t)LED_COUNT_STRIP) ledsStrip[r] = CHSV(192, 240, bri);
    if (l >= 0 && l < (int16_t)LED_COUNT_STRIP) ledsStrip[l] = CHSV(160, 240, bri);
  }

  // CLOUD: two halves of the loop are the "gravity well rim". Both halves
  // compress inward as the wave climbs (mirrors the strip's converging lines
  // but reversed: starts wide, contracts toward idx 0). Strong shudder
  // brightens the whole loop when the wave reaches the top.
  fadeToBlackBy(ledsCloud, LED_COUNT_CLOUD, 35);
  {
    uint16_t cHalf = LED_COUNT_CLOUD / 2;
    // Position around loop: 0..half = right side, half..end = left side
    uint16_t rPosCloud = (uint16_t)((uint32_t)spreadOffset * cHalf / (halfStrip == 0 ? 1 : halfStrip));
    if (rPosCloud >= cHalf) rPosCloud = cHalf - 1;
    uint16_t lPosCloud = LED_COUNT_CLOUD - 1 - rPosCloud;
    for (int8_t off = -1; off <= 1; off++) {
      int16_t r = (int16_t)rPosCloud + off;
      int16_t l = (int16_t)lPosCloud + off;
      uint8_t bri = (off == 0) ? 230 : 110;
      if (r >= 0 && r < (int16_t)LED_COUNT_CLOUD) ledsCloud[(uint16_t)r] = CHSV(192, 240, bri);
      if (l >= 0 && l < (int16_t)LED_COUNT_CLOUD) ledsCloud[(uint16_t)l] = CHSV(160, 240, bri);
    }
    // Shudder boost
    if (gravShudderFrames > 0) {
      uint8_t boost = random8(60, 140);
      for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
        ledsCloud[i] += CHSV(176, 220, scale8(boost, random8()));
      }
    }
  }
}

// ============================================================
// effectChladniCymatics — vibrating-plate standing-wave nodal patterns.
// Disc is a circular drumhead (angular mode p, radial mode q), matrix is
// a rectangular Chladni superposition, strip is the 1D driving tone, cloud
// is a low-frequency sympathetic rumble. PM raises the driving frequency,
// which walks (n,m) up so the symmetry order morphs over time.
// ============================================================
void effectChladniCymatics(float pm) {
  static float t = 0.0f;
  static float nF = 2.0f, mF = 3.0f;
  static float nTarget = 2.0f, mTarget = 3.0f;
  static uint32_t lastMorphMs = 0;

  const float r = pmRatio(pm);
  const uint32_t now = nowMs();

  const float omega = 0.020f + r * 0.045f;
  t += omega;

  if (now - lastMorphMs > 4000) {
    lastMorphMs = now;
    const uint8_t lo = (uint8_t)(2 + r * 3.0f);
    const uint8_t hi = (uint8_t)(3 + r * 5.0f);
    nTarget = (float)(lo + random8(2));
    mTarget = (float)(hi + random8(2));
    if ((uint8_t)nTarget == (uint8_t)mTarget) mTarget += 1.0f;
  }
  nF += (nTarget - nF) * 0.01f;
  mF += (mTarget - mF) * 0.01f;

  const uint8_t baseHue = 160 + (uint8_t)(r * 60.0f);
  const float phase = t;
  const float cosPhase = cosf(phase);

  // ---- MATRIX: rectangular Chladni superposition u = cos(t) * [cos(n*pi*x)*cos(m*pi*y) - cos(m*pi*x)*cos(n*pi*y)]
  for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
    const float yn = (float)y / (float)(MATRIX_HEIGHT - 1);
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
      const float xn = (float)x / (float)(MATRIX_WIDTH - 1);
      const float a = cosf(nF * 3.14159265f * xn) * cosf(mF * 3.14159265f * yn);
      const float b = cosf(mF * 3.14159265f * xn) * cosf(nF * 3.14159265f * yn);
      const float u = (a - b) * cosPhase;
      float au = fabsf(u);
      if (au < 0.05f) {
        ledsMatrix[XY(x, y)] = CRGB::Black;
      } else {
        uint8_t bri = scale8((uint8_t)(au * 255.0f), 230);
        uint8_t hue = baseHue + (uint8_t)(au * 40.0f);
        ledsMatrix[XY(x, y)] = CHSV(hue, 220 - (uint8_t)(au * 80.0f), bri);
      }
    }
  }

  // ---- DISC: circular drumhead. u = cos(p*theta + phase) * cos(q*pi*r)
  {
    const float p = nF;
    const float q = mF * 0.5f;
    for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
      uint8_t ring, pos;
      float xx, yy, rr;
      discIdxToXY(i, xx, yy, rr, ring, pos);
      uint8_t a8 = discAngle8(ring, pos);
      float theta = (float)a8 * (6.2831853f / 256.0f);
      float u = cosf(p * theta + phase) * cosf(q * 3.14159265f * rr);
      float au = fabsf(u);
      if (au < 0.06f) {
        ledsDisc[i] = CRGB::Black;
      } else {
        uint8_t bri = scale8((uint8_t)(au * 255.0f), 230);
        uint8_t hue = baseHue + (uint8_t)(rr * 30.0f);
        ledsDisc[i] = CHSV(hue, 220, bri);
      }
    }
  }

  // ---- STRIP: 1D standing wave at wavenumber k = 1 + nF
  {
    const float k = 1.0f + nF;
    for (uint16_t i = 0; i < LED_COUNT_STRIP; i++) {
      float xn = (float)i / (float)(LED_COUNT_STRIP - 1);
      float u = sinf(k * 3.14159265f * xn) * cosPhase;
      uint8_t bri = (uint8_t)(fabsf(u) * 220.0f);
      ledsStrip[i] = CHSV(baseHue + 10, 220, bri);
    }
  }

  // ---- CLOUD: slow sympathetic rumble at phase/4
  {
    float u = (sinf(phase * 0.25f) + 1.0f) * 0.5f;
    uint8_t bri = (uint8_t)(u * 120.0f);
    fill_solid(ledsCloud, LED_COUNT_CLOUD, CHSV(baseHue - 20, 200, bri));
  }
}

// ============================================================
// effectPendulumWave — N independent pendulums with offset periods.
// Strip = 300 pendulums in a row (iconic snake/chaos/re-sync pattern).
// Matrix is the spacetime history (newest row at bottom, scrolls up).
// Disc has 8 rings, each ring is one pendulum's orbiting ball.
// Cloud breaks into 10 pendulum cells. PM raises base freq + delta.
// ============================================================
void effectPendulumWave(float pm) {
  static float t = 0.0f;
  static uint8_t pwHist[MATRIX_HEIGHT][MATRIX_WIDTH] = {{0}};
  static uint32_t lastShiftMs = 0;

  const float r = pmRatio(pm);
  const float baseOmega  = 0.030f + r * 0.060f;
  const float deltaOmega = 0.00006f + r * 0.00012f;
  t += 1.0f;

  // ---- STRIP: each LED is one pendulum, brightness = |sin(omega_i * t)|
  for (uint16_t i = 0; i < LED_COUNT_STRIP; i++) {
    float omega = baseOmega + (float)i * deltaOmega;
    float au = fabsf(sinf(omega * t));
    uint8_t bri = (uint8_t)(au * 230.0f);
    uint8_t hue = (uint8_t)(((uint32_t)i * 256U) / LED_COUNT_STRIP) + (uint8_t)(t * 0.2f);
    if (bri < 8) ledsStrip[i] = CRGB::Black;
    else         ledsStrip[i] = CHSV(hue, 220, bri);
  }

  // ---- DISC: 8 rings × 8 pendulums, orbiting "ball" per ring
  fadeToBlackBy(ledsDisc, LED_COUNT_DISC, 40);
  for (uint8_t ring = 0; ring < DISC_RING_COUNT; ring++) {
    float omegaRing = baseOmega * (1.0f + 0.3f * (float)ring);
    uint8_t ringLen = DISC_RING_LENS[ring];
    if (ringLen <= 1) {
      float au = fabsf(sinf(omegaRing * t));
      ledsDisc[DISC_RING_OFFSETS[ring]] = CHSV(0, 0, (uint8_t)(au * 200.0f));
      continue;
    }
    float frac = fmodf(omegaRing * t, 6.2831853f);
    if (frac < 0.0f) frac += 6.2831853f;
    float ballPos = frac / 6.2831853f * (float)ringLen;
    uint8_t hue = (uint8_t)(ring * 32);
    for (uint8_t pidx = 0; pidx < ringLen; pidx++) {
      float d = fabsf((float)pidx - ballPos);
      if (d > (float)ringLen * 0.5f) d = (float)ringLen - d;
      float bri = 1.0f - d / 3.0f;
      if (bri < 0.0f) continue;
      uint8_t b = (uint8_t)(bri * 230.0f);
      if (b) ledsDisc[DISC_RING_OFFSETS[ring] + pidx] += CHSV(hue, 220, b);
    }
  }

  // ---- MATRIX: side-buffer history scroll. Each column is one pendulum.
  {
    const uint32_t now = nowMs();
    const uint16_t shiftInterval = (uint16_t)(60 - (uint16_t)(r * 40.0f));
    if (now - lastShiftMs >= shiftInterval) {
      lastShiftMs = now;
      for (uint8_t y = 0; y < MATRIX_HEIGHT - 1; y++) {
        for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
          pwHist[y][x] = pwHist[y + 1][x];
        }
      }
      for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        float omegaCol = baseOmega * (0.6f + 0.15f * (float)x);
        float au = fabsf(sinf(omegaCol * t));
        pwHist[MATRIX_HEIGHT - 1][x] = (uint8_t)(au * 230.0f);
      }
    }
    for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
      for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
        uint8_t bri = pwHist[y][x];
        if (bri < 4) ledsMatrix[XY(x, y)] = CRGB::Black;
        else         ledsMatrix[XY(x, y)] = CHSV((uint8_t)(x * 32), 220, bri);
      }
    }
  }

  // ---- CLOUD: 10 pendulum cells spread across the loop
  for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
    uint8_t pendIdx = (uint8_t)(((uint32_t)i * 10U) / LED_COUNT_CLOUD);
    float omega = baseOmega * (0.5f + 0.25f * (float)pendIdx);
    float au = fabsf(sinf(omega * t + (float)pendIdx * 0.5f));
    uint8_t bri = (uint8_t)(au * 200.0f);
    ledsCloud[i] = CHSV((uint8_t)(pendIdx * 25), 220, bri);
  }
}

// ============================================================
// effectSynapticCascade — per-LED neurons with membrane potential, threshold
// firing, refractory period, and propagation to topological neighbors. PM
// raises spontaneous excitation; at high PM, seizure-like waves cascade.
// White flash = active spike, blue afterglow = refractory, green->red ramp
// shows subthreshold charge approaching firing. Disc rings 0..2 are "hub"
// neurons that get a small excitation bias (cortical-style cluster).
// ============================================================
struct SynState {
  uint8_t charge;
  uint8_t refractory;
};

void effectSynapticCascade(float pm) {
  static bool inited = false;
  static SynState synStrip[LED_COUNT_STRIP];
  static SynState synMatrix[LED_COUNT_MATRIX];
  static SynState synDisc[LED_COUNT_DISC];
  static SynState synCloud[LED_COUNT_CLOUD];
  static uint8_t spikesStrip[LED_COUNT_STRIP];
  static uint8_t spikesMatrix[LED_COUNT_MATRIX];
  static uint8_t spikesDisc[LED_COUNT_DISC];
  static uint8_t spikesCloud[LED_COUNT_CLOUD];

  if (!inited) {
    memset(synStrip, 0, sizeof(synStrip));
    memset(synMatrix, 0, sizeof(synMatrix));
    memset(synDisc, 0, sizeof(synDisc));
    memset(synCloud, 0, sizeof(synCloud));
    inited = true;
  }

  const float r = pmRatio(pm);
  const uint8_t threshold      = (uint8_t)(220 - r * 80.0f);
  const uint8_t spontMin       = (uint8_t)(1 + r * 5.0f);
  const uint8_t spontThreshold = (uint8_t)(220 - r * 160.0f);
  const uint8_t refracTime     = 35;
  const uint8_t flashWindow    = refracTime - 4;
  const uint8_t spikePropagate = 110;

  auto stepCell = [&](SynState &s) {
    if (s.refractory > 0) {
      s.refractory--;
      s.charge = scale8(s.charge, 220);
    } else {
      if (random8() > spontThreshold) {
        s.charge = qadd8(s.charge, spontMin);
      }
      if (s.charge > 0) s.charge--;
    }
  };

  auto renderCell = [&](const SynState &s) -> CRGB {
    if (s.refractory > flashWindow) return CRGB::White;
    if (s.refractory > 0) {
      uint8_t bri = s.refractory * 6;
      return CHSV(160, 240, bri);
    }
    if (s.charge < 30) return CRGB::Black;
    uint8_t bri = scale8(s.charge, 200);
    uint8_t hue = 96 - scale8(s.charge, 96);
    return CHSV(hue, 240, bri);
  };

  // ===== STRIP =====
  memset(spikesStrip, 0, sizeof(spikesStrip));
  for (uint16_t i = 0; i < LED_COUNT_STRIP; i++) {
    stepCell(synStrip[i]);
    if (synStrip[i].refractory == 0 && synStrip[i].charge >= threshold) {
      spikesStrip[i] = 1;
    }
  }
  for (uint16_t i = 0; i < LED_COUNT_STRIP; i++) {
    if (!spikesStrip[i]) continue;
    synStrip[i].charge = 0;
    synStrip[i].refractory = refracTime;
    if (i > 0) synStrip[i - 1].charge = qadd8(synStrip[i - 1].charge, spikePropagate);
    if (i + 1 < LED_COUNT_STRIP) synStrip[i + 1].charge = qadd8(synStrip[i + 1].charge, spikePropagate);
  }
  for (uint16_t i = 0; i < LED_COUNT_STRIP; i++) ledsStrip[i] = renderCell(synStrip[i]);

  // ===== DISC ===== hub bias on rings 0..2
  memset(spikesDisc, 0, sizeof(spikesDisc));
  for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
    stepCell(synDisc[i]);
    uint8_t ring, pos;
    discIdxToRing(i, ring, pos);
    if (ring <= 2) synDisc[i].charge = qadd8(synDisc[i].charge, (uint8_t)(r * 2.0f));
    if (synDisc[i].refractory == 0 && synDisc[i].charge >= threshold) {
      spikesDisc[i] = 1;
    }
  }
  for (uint16_t i = 0; i < LED_COUNT_DISC; i++) {
    if (!spikesDisc[i]) continue;
    synDisc[i].charge = 0;
    synDisc[i].refractory = refracTime;
    uint8_t ring, pos;
    discIdxToRing(i, ring, pos);
    uint8_t ringLen = DISC_RING_LENS[ring];
    if (ringLen > 1) {
      uint8_t pPrev = (pos == 0) ? ringLen - 1 : pos - 1;
      uint8_t pNext = (pos + 1 >= ringLen) ? 0 : pos + 1;
      uint16_t off = DISC_RING_OFFSETS[ring];
      synDisc[off + pPrev].charge = qadd8(synDisc[off + pPrev].charge, spikePropagate);
      synDisc[off + pNext].charge = qadd8(synDisc[off + pNext].charge, spikePropagate);
    }
    uint8_t a8 = discAngle8(ring, pos);
    if (ring + 1 < DISC_RING_COUNT) {
      uint8_t outerLen = DISC_RING_LENS[ring + 1];
      uint8_t outerPos = (uint8_t)(((uint16_t)a8 * outerLen) / 256U);
      uint16_t idx = DISC_RING_OFFSETS[ring + 1] + outerPos;
      synDisc[idx].charge = qadd8(synDisc[idx].charge, scale8(spikePropagate, 200));
    }
    if (ring > 0) {
      uint8_t innerLen = DISC_RING_LENS[ring - 1];
      uint8_t innerPos = (innerLen <= 1) ? 0 : (uint8_t)(((uint16_t)a8 * innerLen) / 256U);
      uint16_t idx = DISC_RING_OFFSETS[ring - 1] + innerPos;
      synDisc[idx].charge = qadd8(synDisc[idx].charge, scale8(spikePropagate, 200));
    }
  }
  for (uint16_t i = 0; i < LED_COUNT_DISC; i++) ledsDisc[i] = renderCell(synDisc[i]);

  // ===== MATRIX ===== 4-neighbor cortical sheet
  memset(spikesMatrix, 0, sizeof(spikesMatrix));
  for (uint16_t i = 0; i < LED_COUNT_MATRIX; i++) {
    stepCell(synMatrix[i]);
    if (synMatrix[i].refractory == 0 && synMatrix[i].charge >= threshold) {
      spikesMatrix[i] = 1;
    }
  }
  for (uint8_t y = 0; y < MATRIX_HEIGHT; y++) {
    for (uint8_t x = 0; x < MATRIX_WIDTH; x++) {
      uint16_t idx = XY(x, y);
      if (!spikesMatrix[idx]) continue;
      synMatrix[idx].charge = 0;
      synMatrix[idx].refractory = refracTime;
      if (x > 0)                  synMatrix[XY(x - 1, y)].charge = qadd8(synMatrix[XY(x - 1, y)].charge, spikePropagate);
      if (x + 1 < MATRIX_WIDTH)   synMatrix[XY(x + 1, y)].charge = qadd8(synMatrix[XY(x + 1, y)].charge, spikePropagate);
      if (y > 0)                  synMatrix[XY(x, y - 1)].charge = qadd8(synMatrix[XY(x, y - 1)].charge, spikePropagate);
      if (y + 1 < MATRIX_HEIGHT)  synMatrix[XY(x, y + 1)].charge = qadd8(synMatrix[XY(x, y + 1)].charge, spikePropagate);
    }
  }
  for (uint16_t i = 0; i < LED_COUNT_MATRIX; i++) ledsMatrix[i] = renderCell(synMatrix[i]);

  // ===== CLOUD ===== ring topology
  memset(spikesCloud, 0, sizeof(spikesCloud));
  for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
    stepCell(synCloud[i]);
    if (synCloud[i].refractory == 0 && synCloud[i].charge >= threshold) {
      spikesCloud[i] = 1;
    }
  }
  for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) {
    if (!spikesCloud[i]) continue;
    synCloud[i].charge = 0;
    synCloud[i].refractory = refracTime;
    uint16_t prev = (i == 0) ? LED_COUNT_CLOUD - 1 : i - 1;
    uint16_t next = (i + 1 >= LED_COUNT_CLOUD) ? 0 : i + 1;
    synCloud[prev].charge = qadd8(synCloud[prev].charge, spikePropagate);
    synCloud[next].charge = qadd8(synCloud[next].charge, spikePropagate);
  }
  for (uint16_t i = 0; i < LED_COUNT_CLOUD; i++) ledsCloud[i] = renderCell(synCloud[i]);

  // Sparse cross-surface long-range axons — rare ectopic nudges scale with PM.
  if (random8() < (uint8_t)(r * 30.0f)) {
    uint16_t idx = random16(LED_COUNT_STRIP);
    synStrip[idx].charge = qadd8(synStrip[idx].charge, 80);
  }
  if (random8() < (uint8_t)(r * 30.0f)) {
    uint16_t idx = random16(LED_COUNT_MATRIX);
    synMatrix[idx].charge = qadd8(synMatrix[idx].charge, 80);
  }
}
