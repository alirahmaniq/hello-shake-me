#include "messages.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>
#include "fonts.h"
#include "g2d.h"

using g2d::Path;
using g2d::Xf;

namespace messages {

static const float R = 233;  // screen centre / radius (466 x 466 round panel)
static const uint32_t PINK = 0xC2185B;
static const uint32_t WHITE = 0xFFFFFF;

// ---------- easing (same as the preview) ----------
static inline float cl(float t) { return t < 0 ? 0 : (t > 1 ? 1 : t); }
static inline float oc(float t) { t = 1 - cl(t); return 1 - t * t * t; }
static inline float ic(float t) { t = cl(t); return t * t * t; }
static inline float ob(float t) {
  t = cl(t);
  const float c = 1.7f, u = t - 1;
  return 1 + (c + 1) * u * u * u + c * u * u;
}
static inline float f(float t, float s, float d) { return cl((t - s) / d); }
static inline float lerp(float a, float b, float p) { return a + (b - a) * p; }

// ---------- text layout ----------
struct Word {
  const char *s;
  int len;
  float x, y;
};
struct Line {
  const char *s;
  int len;
};
static const int MAX_WORDS = 24, MAX_LINES = 8;

// Greedy word wrap like the preview's wrap(): '\n' forces a break.
static int wrap(const Font &font, const char *txt, float maxW, Line *out) {
  int n = 0;
  const char *p = txt;
  while (*p && n < MAX_LINES) {
    const char *paraEnd = strchr(p, '\n');
    if (!paraEnd) paraEnd = p + strlen(p);
    const char *lineStart = p, *lastFit = nullptr;
    const char *q = p;
    while (q <= paraEnd) {
      if (q == paraEnd || *q == ' ') {
        const float w = g2d::measure(font, lineStart, q - lineStart);
        if (w > maxW && lastFit) {
          out[n++] = {lineStart, (int)(lastFit - lineStart)};
          lineStart = lastFit + 1;
          lastFit = nullptr;
          continue;  // re-measure from the new line start with the same q
        }
        lastFit = q;
      }
      q++;
    }
    if (n < MAX_LINES) out[n++] = {lineStart, (int)(paraEnd - lineStart)};
    p = *paraEnd ? paraEnd + 1 : paraEnd;
  }
  return n;
}

static int layout(const Font &font, const char *txt, float cy, float lh, float w, Word *out) {
  Line lines[MAX_LINES];
  const int nl = wrap(font, txt, w, lines);
  const float y0 = cy - (nl - 1) * lh / 2;
  const float space = g2d::measure(font, " ", 1);
  int n = 0;
  for (int li = 0; li < nl; li++) {
    float x = R - g2d::measure(font, lines[li].s, lines[li].len) / 2;
    const char *p = lines[li].s, *end = p + lines[li].len;
    while (p < end && n < MAX_WORDS) {
      const char *sp = (const char *)memchr(p, ' ', end - p);
      const int len = sp ? sp - p : end - p;
      out[n++] = {p, len, x, y0 + li * lh};
      x += g2d::measure(font, p, len) + space;
      p += len + 1;
    }
  }
  return n;
}

// Words fade up one after another. Returns the time the last word starts.
static float words(const Font &font, const char *txt, uint32_t color, float cy, float lh, float w, float t,
                   float start, float per) {
  Word ws[MAX_WORDS];
  const int n = layout(font, txt, cy, lh, w, ws);
  for (int i = 0; i < n; i++) {
    const float a = oc(f(t, start + i * per, .5f));
    if (a > 0) g2d::drawText(font, ws[i].s, ws[i].x, ws[i].y + (1 - a) * 10, color, a, 1, ws[i].len);
  }
  return start + n * per;
}

// Handwriting: each line is revealed left to right. Returns the end time; pen = writing position.
static float writeOn(const Font &font, const char *txt, uint32_t color, float cy, float lh, float w, float t,
                     float start, float cps, bool &penOn, float &penX, float &penY) {
  Line lines[MAX_LINES];
  const int nl = wrap(font, txt, w, lines);
  const float y0 = cy - (nl - 1) * lh / 2;
  float tt = start;
  penOn = false;
  for (int li = 0; li < nl; li++) {
    const float tw = g2d::measure(font, lines[li].s, lines[li].len);
    const float x = R - tw / 2, y = y0 + li * lh, dur = lines[li].len / cps;
    const float p = f(t, tt, dur);
    if (p > 0) {
      g2d::setClip((int)(x - 4), (int)(y - lh), (int)(x - 4 + (tw + 8) * p), (int)(y + lh));
      g2d::drawText(font, lines[li].s, x, y, color, 1, 1, lines[li].len);
      g2d::resetClip();
      if (p < 1) { penOn = true; penX = x + tw * p; penY = y; }
    }
    tt += dur;
  }
  return tt;
}

static void drawCentered(const Font &font, const char *s, float cy, uint32_t color, float alpha = 1) {
  g2d::drawText(font, s, R - g2d::measure(font, s) / 2, cy, color, alpha);
}

// ---------- shapes ----------
static void disc(uint32_t color, float t) {
  const float r = R * oc(f(t, 0, .7f));
  if (r >= R - 0.5f) { g2d::clear(color); return; }  // round panel: corners are invisible anyway
  g2d::clear(0);
  g2d::fillCircle(R, R, r, color);
}

static void star(float x, float y, float r, float rot, float a) {
  if (r <= 0.2f || a <= 0) return;
  Path p(Xf().translate(x, y).rotate(rot));
  for (int i = 0; i < 10; i++) {
    const float rr = (i % 2) ? r * .5f : r, an = i * PI / 5 - PI / 2;
    if (i == 0) p.moveTo(cosf(an) * rr, sinf(an) * rr); else p.lineTo(cosf(an) * rr, sinf(an) * rr);
  }
  p.close().stroke(1.6f, 0x465552, a * .55f);
}

static void sparkle(float x, float y, float s, uint32_t c, float a) {
  Path p(Xf().translate(x, y));
  p.moveTo(0, -s).quadTo(0, 0, s, 0).quadTo(0, 0, 0, s).quadTo(0, 0, -s, 0).quadTo(0, 0, 0, -s).close();
  p.fill(c, a);
}

static void heart(float x, float y, float s, uint32_t c, float a = 1) {
  if (s <= 0.01f || a <= 0) return;
  Path p(Xf().translate(x, y).scale(s));
  p.moveTo(0, 6).bezierTo(-14, -4, -7, -14, 0, -7).bezierTo(7, -14, 14, -4, 0, 6).close();
  p.fill(c, a);
}

static void rrect(const Xf &xf, float x, float y, float w, float h, float r, uint32_t c, float a = 1) {
  Path p(xf);
  p.roundRect(x, y, w, h, r).fill(c, a);
}

// ---------- 1: "Your presence is a present to the world." (gift box) ----------
static void msgPresent(float t) {
  disc(0xA7BCB8, t);
  static const float st[4][4] = {{110, 115, 26, .5f}, {350, 105, 22, .7f}, {372, 330, 20, .9f}, {95, 320, 18, 1.1f}};
  for (auto &s : st)
    star(s[0], s[1], s[2] * ob(f(t, s[3], .6f)), (1 - oc(f(t, s[3], .8f))) * 2 + t * .15f, f(t, s[3], .3f) * .8f);

  const float bx = R, by = 345, bp = ob(f(t, .5f, .5f));
  if (bp > 0.01f) {
    const Xf box = Xf().translate(bx, by).scale(bp);
    rrect(box, -62, -40, 124, 80, 6, 0xF4F1EA);
    rrect(box, -10, -40, 20, 80, 0, 0xD98E8A);
  }
  const float wob = (f(t, 1.3f, .6f) < 1 && t > 1.3f) ? sinf(t * 40) * .08f * (1 - f(t, 1.3f, .6f)) : 0;
  const float fly = f(t, 1.9f, .7f);
  if (fly < 1 && bp > 0.01f) {  // the lid: wobbles, then flies off
    const Xf lid = Xf().translate(bx + fly * 120, by - 44 * bp - fly * 220).rotate(wob + fly * 1.4f).scale(bp);
    const float a = 1 - ic(fly);
    rrect(lid, -70, -12, 140, 24, 6, WHITE, a);
    rrect(lid, -10, -12, 20, 24, 0, 0xD98E8A, a);
    Path bow(lid);
    bow.ellipse(-14, -18, 14, 8, -.5f).ellipse(14, -18, 14, 8, .5f).fill(0xD98E8A, a);
  }
  for (int i = 0; i < 16; i++) {
    const float p = f(t, 1.95f + i * .02f, .9f);
    if (p <= 0 || p >= 1) continue;
    const float a = -PI / 2 + (i / 15.0f - .5f) * 2.4f, d = 40 + oc(p) * 150;
    sparkle(bx + cosf(a) * d, by - 40 + sinf(a) * d, 5 + (i % 3) * 3, (i % 2) ? WHITE : 0xFBE7A1, 1 - p);
  }
  // Words fly up out of the box into place.
  Word ws[MAX_WORDS];
  const int n = layout(FONT_LORA30, "Your presence is a present to the world.", 165, 42, 300, ws);
  for (int i = 0; i < n; i++) {
    const float p = f(t, 2.3f + i * .2f, .9f);
    if (p <= 0) continue;
    const float e = oc(p);
    g2d::drawText(FONT_LORA30, ws[i].s, lerp(bx - 20, ws[i].x, e), lerp(by - 40, ws[i].y, e), WHITE, cl(p * 2),
                  .4f + .6f * e, ws[i].len);
  }
}

// ---------- 2: "hey stranger!" (envelope + handwriting) ----------
static void msgStranger(float t) {
  disc(0xF3D7DF, t);
  const float ep = ob(f(t, .5f, .5f)), slide = oc(f(t, 1.8f, .8f)), away = ic(f(t, 2.4f, .6f));
  const float ey = 300 + away * 220;
  if (ep > 0.01f) rrect(Xf().translate(R, ey).scale(ep), -90, -55, 180, 110, 8, WHITE);

  const float grow = oc(f(t, 2.3f, .6f));
  const float cw = lerp(150, 330, grow), ch = lerp(90, 300, grow);
  if (t > 1.8f) {  // the card slides out of the envelope and grows
    if (t < 2.3f) g2d::setClip(0, 0, 466, (int)ey);
    rrect(Xf(), R - cw / 2, lerp(ey - 40 - slide * 90, R - ch / 2, grow), cw, ch, 12, 0xFFFDF8);
    g2d::resetClip();
  }
  if (ep > 0.01f) {
    const Xf env = Xf().translate(R, ey).scale(ep);
    Path front(env);
    front.moveTo(-90, 55).lineTo(0, 0).lineTo(90, 55).close();
    front.fill(0xFDEFF3);
    front.stroke(2, 0xE8B4C4);
    const float fl = 1 - 2 * oc(f(t, 1.2f, .5f));  // flap flips open
    if (fabsf(fl) > 0.01f) {
      Path flap(Xf(env).scale(1, fl));
      flap.moveTo(-90, -55).lineTo(0, 10).lineTo(90, -55).close();
      flap.fill(fl > 0 ? 0xF7DCE5 : 0xFBE9EE);
      flap.stroke(2, 0xE8B4C4);
    }
  }
  if (t < 1.2f) heart(R, ey + 5, ep * 1.1f, PINK);

  bool penOn;
  float penX = 0, penY = 0;
  const float e = writeOn(FONT_NOTE34, "hey stranger!\nYou are so loved and you make the world a better place", PINK, 205,
                          38, 290, t, 3.1f, 15, penOn, penX, penY);
  if (penOn) {
    const Xf pen = Xf().translate(penX + 8, penY - 14).rotate(.6f);
    rrect(pen, -3, -28, 6, 28, 2, 0x555555);
    Path nib(pen);
    nib.moveTo(-3, 0).lineTo(0, 7).lineTo(3, 0).close().fill(0x222222);
  }
  const float hb = f(t, e, .4f);
  if (hb > 0) {
    heart(R, 335, 1.5f * ob(hb) * (1 + .1f * max(0.0f, sinf((t - e) * 8))), PINK);
    for (int i = 0; i < 6; i++) {
      const float p = f(t, e + .4f + i * .25f, 1.6f);
      if (p <= 0 || p >= 1) continue;
      heart(R + sinf(i * 2.1f + p * 6) * 30, 335 - p * 120, .6f, PINK, (1 - p) * .8f);
    }
  }
  const float pp = f(t, e + 1.2f, 1.8f);  // paper plane flies across
  if (pp > 0 && pp < 1) {
    Path plane(Xf().translate(lerp(80, 420, pp), 385 - sinf(pp * PI) * 60 - pp * 40).rotate(-.35f + pp * .3f));
    plane.moveTo(18, 0).lineTo(-14, -10).lineTo(-8, 0).lineTo(-14, 10).close();
    plane.fill(WHITE);
    plane.stroke(1.5f, 0xC98AA0);
  }
}

// ---------- 3: "I'm proud of you!" ----------
static void msgProud(float t) {
  disc(0xF7F5F0, t);
  for (int i = 0; i < 14; i++) {
    const float p = f(t, .4f + i * .12f, 2.6f);
    if (p <= 0 || p >= 1) continue;
    const float a = i * 2.4f;
    heart(R + cosf(a) * (60 + p * 150), R + sinf(a) * (60 + p * 150) - p * 30, .8f + (i % 3) * .3f, PINK, (1 - p) * .7f);
  }
  bool penOn;
  float px, py;
  const float e = writeOn(FONT_PROUD64, "I'm proud\nof you!", PINK, 215, 66, 340, t, .8f, 9, penOn, px, py);
  const float p = f(t, e, .6f);
  if (p > 0) {  // smiley
    Path smile;
    smile.arc(R, 300, 26, .15f * PI, (.15f + .7f * p) * PI);
    smile.stroke(4, PINK);
    g2d::fillCircle(R - 11, 288, 4, PINK, p);
    g2d::fillCircle(R + 11, 288, 4, PINK, p);
  }
}

// ---------- 4: "Be gentle with yourself." (rising sun) ----------
static void msgGentle(float t) {
  disc(0xF6DCC6, t);
  const float sy = 470 - 150 * oc(f(t, .5f, 1.6f));
  Xf rays = Xf().translate(R, sy).rotate(t * .25f);
  for (int i = 0; i < 12; i++) {
    rays.rotate(PI / 6);
    Path ray(rays);
    ray.moveTo(0, -78).lineTo(0, -98).stroke(4, 0xE88C3C, .55f);
  }
  g2d::fillCircle(R, sy, 62, 0xF2A65A);
  words(FONT_LORA30, "Be gentle with yourself.\nYou're doing better than you think.", 0x6B3E1E, 185, 40, 320, t, 1.6f,
        .2f);
}

// ---------- 5: "Keep going." (night sky) ----------
static void msgKeepGoing(float t) {
  disc(0x1B2340, t);
  for (int i = 0; i < 26; i++) {
    const float x = R + cosf(i * 7.1f) * (40 + (i * 37) % 190), y = R + sinf(i * 7.1f) * (40 + (i * 53) % 190);
    const float a = f(t, .4f + i * .05f, .4f) * (.5f + .5f * sinf(t * 3 + i));
    if (a > 0) g2d::fillCircle(x, y, 1.2f + (i % 3) * .7f, WHITE, a);
  }
  const float mp = oc(f(t, .6f, 1));
  if (mp > 0) {  // crescent moon
    g2d::fillCircle(R, 120 - 20 * (1 - mp), 30, 0xF4E7B0, mp);
    g2d::fillCircle(R + 13, 112 - 20 * (1 - mp), 27, 0x1B2340, mp);
  }
  const float e = words(FONT_LORA30, "Even the longest night ends with a sunrise.", WHITE, 250, 40, 310, t, 1.6f, .22f);
  drawCentered(FONT_KEEP40, "Keep going.", 350, 0xF4E7B0, oc(f(t, e + .4f, .8f)));
}

// ---------- public ----------
static const float DURATIONS[COUNT] = {7.5f, 9.5f, 7.0f, 8.0f, 8.5f};

float duration(int i) { return DURATIONS[i]; }

void draw(int i, float t) {
  t = min(t, DURATIONS[i]);
  switch (i) {
    case 0: msgPresent(t); break;
    case 1: msgStranger(t); break;
    case 2: msgProud(t); break;
    case 3: msgGentle(t); break;
    default: msgKeepGoing(t); break;
  }
}

// ---------- Hello screen (handwritten "hello" + wiggling "shake me", same as the preview) ----------
static inline float io(float t) {
  t = cl(t);
  return t < .5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2;
}

static const float HELLO_Y = 208;  // middle line of the word (R - 25)

// Gradient colour of the word at screen column x: #7FD3FF -> #C9A7FF -> #FFB0C8.
static void helloGradient(float x, float &r, float &g, float &b) {
  const float t = cl((x - HELLO_WORD_X0) / (HELLO_WORD_X1 - HELLO_WORD_X0));
  if (t < .5f) {
    const float k = t / .5f;
    r = lerp(0x7F, 0xC9, k); g = lerp(0xD3, 0xA7, k); b = lerp(0xFF, 0xFF, k);
  } else {
    const float k = (t - .5f) / .5f;
    r = lerp(0xC9, 0xFF, k); g = lerp(0xA7, 0xB0, k); b = lerp(0xFF, 0xC8, k);
  }
}

// Blits the pre-rendered word: gradient colour, soft-edged pen reveal, optional glint band,
// and a sub-pixel vertical offset (so the gentle float doesn't jump in 1 px steps).
static void drawHelloWord(float yMid, float reveal, bool glintOn, float glintX) {
  static uint16_t colour[HELLO_WORD_W];
  static uint16_t mask[HELLO_WORD_W];
  const float rx = HELLO_WORD_X0 + (HELLO_WORD_X1 - HELLO_WORD_X0 + 60) * reveal;
  for (int x = 0; x < HELLO_WORD_W; x++) {
    const float sx = HELLO_WORD_X + x + 0.5f;
    float r, g, b;
    helloGradient(sx, r, g, b);
    if (glintOn) {  // white band, 0.85 at its centre, 50 px each side
      const float k = max(0.0f, 1 - fabsf(sx - glintX) / 50) * 0.85f;
      r = lerp(r, 255, k); g = lerp(g, 255, k); b = lerp(b, 255, k);
    }
    colour[x] = g2d::rgb565(((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
    mask[x] = reveal >= 1 ? 256 : (uint16_t)(cl((rx - sx) / 60) * 256);
  }
  uint16_t *fb = g2d::framebuffer();
  const float top = yMid + HELLO_WORD_TOP;
  const int iy = (int)floorf(top);
  const float fr = top - iy;
  const int f1 = (int)(fr * 256), f0 = 256 - f1;
  for (int yy = 0; yy <= HELLO_WORD_H; yy++) {
    const int Y = iy + yy;
    if (Y < 0 || Y >= 466) continue;
    const uint8_t *cur = yy < HELLO_WORD_H ? HELLO_WORD_IMG + yy * HELLO_WORD_W : nullptr;
    const uint8_t *prev = yy > 0 ? HELLO_WORD_IMG + (yy - 1) * HELLO_WORD_W : nullptr;
    uint16_t *row = fb + Y * 466 + HELLO_WORD_X;
    for (int x = 0; x < HELLO_WORD_W; x++) {
      const int a = ((cur ? cur[x] * f0 : 0) + (prev ? prev[x] * f1 : 0)) >> 8;  // 0..255
      if (!a || !mask[x]) continue;
      row[x] = g2d::blendPixel(row[x], colour[x], ((a + (a >> 7)) * mask[x]) >> 8);
    }
  }
}

// ---------- butterflies on the "hello" (fly in after the pen, then rest with slowly moving wings) ----------
struct Butterfly {
  uint32_t outer, inner, edge;  // wing colours (two-tone instead of a gradient) and outline
  float size, rot, phase, delay;
  float fromX, fromY;           // where it flies in from
};
static const Butterfly BUTTERFLIES[2] = {
    {0x7FD3FF, 0xB48CFF, 0x5A3FB8, 1.25f, -.25f, 0.0f, 0.0f, 40, 60},   // lilac-blue, sits on the "h"
    {0xFFD1E0, 0xFF8FB8, 0xC2185B, 0.95f, .30f, 2.1f, 0.9f, 440, 120},  // pink, sits on the "o"
};

// Perch = top of the ink in a column range of the word image (screen x), relative to the middle line.
static void findPerch(float xa, float xb, float &px, float &py) {
  int bestRow = HELLO_WORD_H, bestCol = (int)((xa + xb) / 2) - HELLO_WORD_X;
  for (int c = max(0, (int)xa - HELLO_WORD_X); c < min(HELLO_WORD_W, (int)xb - HELLO_WORD_X); c++)
    for (int r = 0; r < bestRow; r++)
      if (HELLO_WORD_IMG[r * HELLO_WORD_W + c] > 150) { bestRow = r; bestCol = c; break; }
  px = HELLO_WORD_X + bestCol;
  py = HELLO_WORD_TOP + (bestRow < HELLO_WORD_H ? bestRow : HELLO_WORD_H / 2);
}

// How open the wings are (1 = fully open, small = folded up), like a resting butterfly.
static float wingOpen(float t, float ph) {
  const float base = .5f + .5f * cosf((t + ph) * 2 * PI / 2.2f);
  float s = .18f + .82f * powf(base, .6f);
  float fl = fmodf(t + ph * 1.7f, 6.5f);
  if (fl < 0) fl += 6.5f;
  if (fl < .6f) s = .2f + .8f * fabsf(cosf(fl * PI / .24f));  // a quick flutter now and then
  return s;
}

// Each helper owns exactly one Path (~5 KB) and is not inlined, so only one Path is on the stack at a time.
enum WingOp { FILL, OUTLINE };

__attribute__((noinline)) static void wing(const Xf &xf, bool upper, WingOp op, uint32_t col, float alpha = 1) {
  Path p(xf);
  if (upper) p.moveTo(1, -3).bezierTo(10, -26, 32, -24, 27, -7).bezierTo(25, 0, 9, 0, 1, 0).close();
  else p.moveTo(1, 1).bezierTo(17, 2, 25, 14, 15, 21).bezierTo(8, 24, 3, 12, 1, 3).close();
  if (op == FILL) p.fill(col, alpha);
  else p.stroke(1.1f, col, alpha, false);
}

__attribute__((noinline)) static void wingSpots(const Xf &xf) {
  Path p(xf);
  p.circle(21, -14, 2.6f).circle(13, 12, 1.8f).fill(WHITE, .75f);
}

__attribute__((noinline)) static void butterflyBody(const Xf &xf) {
  {
    Path torso(xf);
    torso.ellipse(0, 1, 2.3f, 11, 0).circle(0, -11, 2.6f).circle(-8, -24, 1.3f).circle(8, -24, 1.3f).fill(0x2B2238);
  }
}

__attribute__((noinline)) static void butterflyAntennae(const Xf &xf) {
  Path p(xf);
  p.moveTo(-1, -13).quadTo(-5, -22, -8, -24).moveTo(1, -13).quadTo(5, -22, 8, -24);
  p.stroke(1, 0x2B2238, 1, false);
}

static void drawButterfly(const Butterfly &b, float x, float y, float rot, float open) {
  const Xf body = Xf().translate(x, y).rotate(rot).scale(b.size);
  open = max(open, .05f);
  for (int side = -1; side <= 1; side += 2) {
    const Xf w = Xf(body).scale(side * open, 1);
    const Xf inner = Xf(w).scale(.62f, .62f);  // smaller copy in the second colour = two-tone wing
    wing(w, true, FILL, b.outer);
    wing(w, false, FILL, b.outer);
    wing(inner, true, FILL, b.inner);
    wing(inner, false, FILL, b.inner);
    wing(w, true, OUTLINE, b.edge);
    wing(w, false, OUTLINE, b.edge);
    wingSpots(w);
  }
  butterflyBody(body);
  butterflyAntennae(body);
}
static void drawButterflies(float ht, float after, float bob) {
  static bool ready = false;
  static float perchX[2], perchY[2];
  if (!ready) {
    const float w = HELLO_WORD_X1 - HELLO_WORD_X0;
    findPerch(HELLO_WORD_X0 + w * .08f, HELLO_WORD_X0 + w * .3f, perchX[0], perchY[0]);
    findPerch(HELLO_WORD_X0 + w * .8f, HELLO_WORD_X1 - 12, perchX[1], perchY[1]);
    ready = true;
  }
  for (int i = 0; i < 2; i++) {
    const Butterfly &b = BUTTERFLIES[i];
    if (after < b.delay) continue;  // not arrived yet
    const float tx = perchX[i], ty = HELLO_Y + perchY[i] + 2 - 10 * b.size;
    const float lt = f(after, b.delay, 1.6f);
    float x = tx, y = ty + bob, rot = b.rot, open;
    if (lt < 1) {  // flying in along a curve, wings beating fast
      const float e = oc(lt), u = 1 - e;
      const float cx = (b.fromX + tx) / 2 + (i ? 60 : -60), cy = min(b.fromY, ty) - 80;
      x = u * u * b.fromX + 2 * u * e * cx + e * e * tx;
      y = u * u * b.fromY + 2 * u * e * cy + e * e * (ty + bob);
      rot = b.rot * e + (i ? .5f : -.5f) * (1 - e);
      open = .15f + .85f * fabsf(cosf(ht * PI / .13f));
    } else {
      open = wingOpen(after - b.delay - 1.6f, b.phase);
    }
    drawButterfly(b, x, y, rot, open);
  }
}

void drawHello(float ht) {
  g2d::clear(0);
  const float d0 = .2f;  // pen starts after a short pause
  const float rev = io(f(ht, d0, 1.4f)), after = ht - d0 - 1.4f;
  const float bob = after > 0 ? sinf(after * 1.6f) * 3 * cl(after) : 0;
  bool glintOn = false;
  float glintX = 0;
  if (after > .8f) {
    const float g = fmodf(after - .8f, 4.5f);  // a glint sweeps across every 4.5 s
    if (g < .9f) { glintOn = true; glintX = lerp(HELLO_WORD_X0 - 60, HELLO_WORD_X1 + 60, io(g / .9f)); }
  }
  if (rev > 0) drawHelloWord(HELLO_Y + bob, rev, glintOn, glintX);
  if (after > 0) drawButterflies(ht, after, bob);
  if (rev > 0 && rev < 1) {  // glowing pen tip
    const float rx = HELLO_WORD_X0 + (HELLO_WORD_X1 - HELLO_WORD_X0 + 60) * rev;
    const float x = min(HELLO_WORD_X1, rx - 30), y = HELLO_Y + sinf(rev * 40) * 14;
    g2d::glow(x, y, 18, .9f, 0xC9A7FF);
    g2d::fillCircle(x, y, 3, WHITE);
  }

  const float sa = f(ht, d0 + 1.2f, .6f);  // "shake me" fades in, then wiggles every 3 s
  if (sa > 0) {
    const float w = after > 1.5f ? fmodf(after - 1.5f, 3) : 9;
    const float wig = w < .6f ? sinf(w * 30) * .12f * (1 - w / .6f) : 0;
    g2d::drawTextRotated(FONT_SHAKE, "shake me", R, R + 62, wig, 0x9A9A9A, sa);
    if (w < .6f) {  // little vibration arcs on both sides
      const float la = sa * (1 - w / .6f);
      for (int s = -1; s <= 1; s += 2)
        for (int k = 0; k < 2; k++) {
          const float a0 = s > 0 ? -.6f : PI - .6f;
          Path arc;
          arc.arc(R + s * (70 + k * 9), R + 62, 8 + k * 5, a0, a0 + 1.2f);
          arc.stroke(2, 0x9A9A9A, la);
        }
    }
  }
}

}  // namespace messages