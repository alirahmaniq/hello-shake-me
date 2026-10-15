#include "audio.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <math.h>
#include "es8311.h"
#include "pin_config.h"

namespace audio {

// ---------- hardware ----------
static const int SAMPLE_RATE = 16000;
static const int CODEC_VOLUME = 80;  // ES8311 output level 0..100; VOLUME scales on top of this
static const int I2S_MCK_IO = 16, I2S_BCK_IO = 9, I2S_DI_IO = 10, I2S_WS_IO = 45, I2S_DO_IO = 8;

static I2SClass i2s;

// ---------- music data (same as the preview) ----------
struct Note {
  float at;   // seconds from the start
  int midi;
  float dur;  // seconds until the bell has faded out
  float lvl;
};

enum : int { F4 = 65, G4 = 67, A4 = 69, B4 = 71, C5 = 72, D5 = 74, E5 = 76, F5 = 77, G5 = 79, A5 = 81,
             B5 = 83, C6 = 84, D6 = 86, E6 = 88, G6 = 91 };

// Intro jingles: {at, note, dur, level multiplier}
static const Note INTRO0[] = {{0, C5, .9, 1}, {.2, E5, .9, 1}, {.4, G5, .9, 1}, {.6, C6, 1.6, 1}, {1.95, E6, .6, .5},
                              {2.05, G6, .6, .5}, {2.15, C6, .6, .5}, {2.4, G5, .9, 1}, {2.4, C5, 1.8, .6}, {2.4, E5, 1.8, .6}};
static const Note INTRO1[] = {{0, G4, .9, 1}, {.25, C5, .9, 1}, {.5, E5, .9, 1}, {.75, D5, .9, 1}, {1, C5, .9, 1},
                              {1.3, E5, .9, 1}, {1.55, G5, .9, 1}, {1.8, A5, .9, 1}, {2.2, G5, 1.6, 1}, {2.2, C5, 1.6, .6},
                              {2.2, E5, 1.6, .6}};
static const Note INTRO2[] = {{0, C5, .9, 1}, {.15, E5, .9, 1}, {.3, G5, .9, 1}, {.6, E5, .9, 1}, {.75, G5, .9, 1},
                              {.9, C6, 1.8, 1}, {.9, G5, 1.8, .6}, {.9, E5, 1.8, .6}};
static const Note INTRO3[] = {{0, F4, .9, 1}, {.35, A4, .9, 1}, {.7, C5, .9, 1}, {1.05, A4, .9, 1}, {1.4, G4, .9, 1},
                              {1.75, C5, 2, 1}, {1.75, E5, 2, .6}, {1.75, G4, 2, .5}};
static const Note INTRO4[] = {{0, A4, .9, 1}, {.2, C5, .9, 1}, {.4, E5, .9, 1}, {.6, G5, .9, 1}, {.8, A5, 1.2, 1},
                              {1.3, G5, .9, 1}, {1.5, A5, .9, 1}, {1.7, C6, 2, 1}, {1.7, E5, 2, .6}, {1.7, A4, 2, .5}};
static const Note *INTROS[5] = {INTRO0, INTRO1, INTRO2, INTRO3, INTRO4};
static const int INTRO_LEN[5] = {sizeof(INTRO0) / sizeof(Note), sizeof(INTRO1) / sizeof(Note), sizeof(INTRO2) / sizeof(Note),
                                 sizeof(INTRO3) / sizeof(Note), sizeof(INTRO4) / sizeof(Note)};

// Background chord progressions and closing chords.
static const int PROG[5][4][3] = {
    {{72, 76, 79}, {69, 72, 76}, {65, 69, 72}, {67, 71, 74}},
    {{72, 76, 79}, {71, 74, 79}, {69, 72, 76}, {65, 69, 72}},
    {{72, 76, 79}, {65, 69, 72}, {67, 71, 74}, {72, 76, 79}},
    {{65, 69, 72}, {67, 72, 76}, {65, 69, 74}, {67, 72, 76}},
    {{69, 72, 76}, {65, 69, 72}, {67, 72, 76}, {67, 71, 74}}};
static const int FINAL[5][3] = {{72, 76, 79}, {72, 76, 79}, {72, 76, 79}, {65, 69, 72}, {72, 76, 79}};

static const int MAX_NOTES = 128;
static Note song[MAX_NOTES];
static int songLen = 0;

static void addNote(float at, int midi, float dur, float lvl) {
  if (songLen < MAX_NOTES) song[songLen++] = {at, midi, dur, lvl};
}

// Intro jingle, then a soft arpeggio over the progression, then a closing chord that rings out
// just as the progress bar ends.
static void buildSong(int i, float total) {
  songLen = 0;
  float introEnd = 0;
  for (int k = 0; k < INTRO_LEN[i]; k++) {
    const Note &n = INTROS[i][k];
    addNote(n.at, n.midi, n.dur, 0.35f * n.lvl);
    introEnd = max(introEnd, n.at);
  }
  introEnd += 1.2f;
  const float endAt = total - 2.2f;
  float t = introEnd;
  for (int c = 0; t + 1.6f <= endAt; c++, t += 1.6f) {
    const int *ch = PROG[i][c % 4];
    addNote(t, ch[0] - 24, 1.8f, 0.12f);
    const int arp[4] = {ch[0], ch[1], ch[2], ch[1]};
    for (int k = 0; k < 4; k++) addNote(t + k * 0.4f, arp[k], 1.1f, 0.13f);
  }
  const int *f = FINAL[i];
  addNote(endAt, f[0] - 24, 2.6f, 0.16f);
  for (int k = 0; k < 3; k++) addNote(endAt + k * 0.06f, f[k], 2.6f, 0.2f);
  addNote(endAt + 0.25f, f[0] + 12, 2.4f, 0.12f);
  // sort by start time
  for (int a = 1; a < songLen; a++) {
    Note n = song[a];
    int b = a;
    while (b > 0 && song[b - 1].at > n.at) { song[b] = song[b - 1]; b--; }
    song[b] = n;
  }
}

// ---------- synth ----------
static const int TABLE = 1024;
static float sineTab[TABLE];

struct Voice {
  bool on;
  uint32_t ph1, inc1, ph2, inc2;
  float amp, peak, decay;
  int attackLeft, attackLen, left;
};
static const int MAX_VOICES = 24;
static Voice voices[MAX_VOICES];

static void startVoice(const Note &n) {
  Voice *v = nullptr;
  for (auto &cand : voices) if (!cand.on) { v = &cand; break; }
  if (!v) {  // steal the quietest
    v = &voices[0];
    for (auto &cand : voices) if (cand.amp < v->amp) v = &cand;
  }
  const float hz = 440.0f * powf(2.0f, (n.midi - 69) / 12.0f);
  v->inc1 = (uint32_t)(hz / SAMPLE_RATE * 4294967296.0);
  v->inc2 = (uint32_t)(hz * 2.01f / SAMPLE_RATE * 4294967296.0);
  v->ph1 = v->ph2 = 0;
  v->peak = n.lvl;
  v->amp = 0;
  v->attackLen = v->attackLeft = (int)(0.008f * SAMPLE_RATE);
  const int total = max(1, (int)(n.dur * SAMPLE_RATE) - v->attackLen);
  v->left = total;
  v->decay = powf(0.0005f / n.lvl, 1.0f / total);  // exponential ramp to 0.0005, like WebAudio
  v->on = true;
}

// ---------- control (main core -> audio core) ----------
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static volatile int pendingMsg = -1;
static volatile float pendingTotal = 0, pendingFade = 0;
static volatile bool pendingStop = false;

void play(int message, float totalSec, float fadeSec) {
  portENTER_CRITICAL(&mux);
  pendingMsg = message;
  pendingTotal = totalSec;
  pendingFade = fadeSec;
  pendingStop = false;
  portEXIT_CRITICAL(&mux);
}

void stop() {
  portENTER_CRITICAL(&mux);
  pendingMsg = -1;
  pendingStop = true;
  portEXIT_CRITICAL(&mux);
}

static void audioTask(void *) {
  static int16_t buf[128 * 2];
  bool playing = false, stopping = false;
  uint32_t pos = 0, totalSamples = 0, fadeSamples = 1, idleBlocks = 0;
  int next = 0;
  float stopGain = 1;
  const float master = VOLUME / 10.0f * 0.5f;

  for (;;) {
    int msg;
    float total, fade;
    bool stopReq;
    portENTER_CRITICAL(&mux);
    msg = pendingMsg; total = pendingTotal; fade = pendingFade; stopReq = pendingStop;
    pendingMsg = -1; pendingStop = false;
    portEXIT_CRITICAL(&mux);

    if (msg >= 0) {
      buildSong(msg, total);
      for (auto &v : voices) v.on = false;
      pos = 0; next = 0; stopGain = 1;
      totalSamples = (uint32_t)(total * SAMPLE_RATE);
      fadeSamples = max(1, (int)(fade * SAMPLE_RATE));
      playing = true; stopping = false;
      digitalWrite(PA, HIGH);  // speaker amplifier on
    }
    if (stopReq && playing) stopping = true;

    for (int i = 0; i < 128; i++) {
      float s = 0;
      if (playing) {
        while (next < songLen && song[next].at * SAMPLE_RATE <= pos) startVoice(song[next++]);
        for (auto &v : voices) {
          if (!v.on) continue;
          if (v.attackLeft > 0) {
            v.amp = v.peak * (1.0f - (float)v.attackLeft / v.attackLen);
            v.attackLeft--;
          } else {
            v.amp *= v.decay;
            if (--v.left <= 0) { v.on = false; continue; }
          }
          s += (sineTab[v.ph1 >> 22] + 0.25f * sineTab[v.ph2 >> 22]) * v.amp;
          v.ph1 += v.inc1;
          v.ph2 += v.inc2;
        }
        float g = master;
        if (pos > totalSamples) g *= max(0.0f, 1.0f - (float)(pos - totalSamples) / fadeSamples);  // fades with the screen
        if (stopping) { stopGain *= 0.9984f; g *= stopGain; }  // ~40 ms time constant
        s *= g;
        pos++;
      }
      const int v = (int)(constrain(s, -1.0f, 1.0f) * 32767);
      buf[2 * i] = buf[2 * i + 1] = (int16_t)v;
    }

    if (playing && (pos > totalSamples + fadeSamples || (stopping && stopGain < 1e-4f))) {
      playing = false;
      for (auto &v : voices) v.on = false;
      idleBlocks = 0;
    }
    if (!playing && ++idleBlocks == 20) digitalWrite(PA, LOW);  // amp off after ~160 ms of silence
    i2s.write((uint8_t *)buf, sizeof(buf));  // blocks until the DMA has room: paces this loop
  }
}

static bool codecInit() {
  es8311_handle_t es = es8311_create(0, ES8311_ADDRRES_0);
  if (!es) return false;
  const es8311_clock_config_t clk = {
      .mclk_inverted = false,
      .sclk_inverted = false,
      .mclk_from_mclk_pin = true,
      .mclk_frequency = SAMPLE_RATE * 256,
      .sample_frequency = SAMPLE_RATE,
  };
  if (es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) return false;
  if (es8311_sample_frequency_config(es, clk.mclk_frequency, clk.sample_frequency) != ESP_OK) return false;
  return es8311_voice_volume_set(es, CODEC_VOLUME, nullptr) == ESP_OK;
}

bool begin() {
  for (int i = 0; i < TABLE; i++) sineTab[i] = sinf(2 * PI * i / TABLE);
  pinMode(PA, OUTPUT);
  digitalWrite(PA, LOW);
  i2s.setPins(I2S_BCK_IO, I2S_WS_IO, I2S_DO_IO, I2S_DI_IO, I2S_MCK_IO);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) return false;
  const bool ok = codecInit();
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 5, nullptr, 0);
  return ok;
}

}  // namespace audio
