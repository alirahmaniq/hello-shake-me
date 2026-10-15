#include "g2d.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>

namespace g2d {

// ---------- state ----------
static uint16_t *fb = nullptr;
static int W = 0, H = 0;
static uint8_t *mask = nullptr;              // coverage mask, W*H, mostly zero
static int mx0, my0, mx1, my1;               // dirty box of the mask, [x0,x1) x [y0,y1)
static int clipX0, clipY0, clipX1, clipY1;
static float *acc = nullptr;                 // one row of coverage accumulators

int width() { return W; }
int height() { return H; }

static inline void maskResetBox() {
  mx0 = W; my0 = H; mx1 = 0; my1 = 0;
}

void begin(uint16_t *framebuffer, int width, int height) {
  fb = framebuffer;
  W = width;
  H = height;
  mask = (uint8_t *)ps_calloc(W * H, 1);
  acc = (float *)calloc(W + 2, sizeof(float));
  maskResetBox();
  resetClip();
}

void setClip(int x0, int y0, int x1, int y1) {
  clipX0 = max(0, x0); clipY0 = max(0, y0);
  clipX1 = min(W, x1); clipY1 = min(H, y1);
}
void resetClip() { setClip(0, 0, W, H); }

// ---------- colour ----------
static inline uint16_t to565(uint32_t rgb) {
  return ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F);
}

// Blend RGB565 colour `s` over pixel `d` with alpha a (0..256).
static inline uint16_t blend(uint16_t d, uint16_t s, uint32_t a) {
  if (a >= 256) return s;
  // Spread the 565 channels apart so one multiply blends all three (classic trick).
  uint32_t dd = (d | ((uint32_t)d << 16)) & 0x07E0F81F;
  uint32_t ss = (s | ((uint32_t)s << 16)) & 0x07E0F81F;
  uint32_t a5 = a >> 3;  // 0..32
  uint32_t r = (dd + (((ss - dd) * a5) >> 5)) & 0x07E0F81F;
  return (uint16_t)(r | (r >> 16));
}

void clear(uint32_t rgb) {
  const uint16_t c = to565(rgb);
  const uint32_t c2 = c | ((uint32_t)c << 16);
  uint32_t *p = (uint32_t *)fb;
  const int n = W * H / 2;
  for (int i = 0; i < n; i++) p[i] = c2;
  if ((W * H) & 1) fb[W * H - 1] = c;
}

void dim(float keep) {
  const uint32_t k = (uint32_t)(constrain(keep, 0.0f, 1.0f) * 32);
  const int n = W * H;
  for (int i = 0; i < n; i++) {
    uint32_t v = (fb[i] | ((uint32_t)fb[i] << 16)) & 0x07E0F81F;
    v = ((v * k) >> 5) & 0x07E0F81F;
    fb[i] = (uint16_t)(v | (v >> 16));
  }
}

// ---------- rasteriser ----------
struct Edge {
  float x0, y0, x1, y1;  // y0 < y1
  int8_t dir;
};
static const int MAX_EDGES = 1024;
static Edge edges[MAX_EDGES];

struct Cross {
  float x;
  int8_t dir;
};

static inline void addSpan(float xa, float xb, int &rx0, int &rx1) {
  if (xa < clipX0) xa = clipX0;
  if (xb > clipX1) xb = clipX1;
  if (xb <= xa) return;
  const float w = 0.25f;  // one of 4 sub-scanlines
  const int ia = (int)xa, ib = (int)xb;
  if (ia == ib) {
    acc[ia] += (xb - xa) * w;
  } else {
    acc[ia] += (ia + 1 - xa) * w;
    for (int x = ia + 1; x < ib; x++) acc[x] += w;
    if (ib < W) acc[ib] += (xb - ib) * w;
  }
  if (ia < rx0) rx0 = ia;
  if (ib > rx1) rx1 = min(ib, W - 1);
}

// Rasterise polygons (non-zero winding) into the mask, max-combining with what is there.
static void rasterize(const float *xs, const float *ys, const int *starts, const int *lens, int nsub) {
  int ne = 0;
  float ymin = 1e9f, ymax = -1e9f;
  for (int s = 0; s < nsub; s++) {
    const int st = starts[s], len = lens[s];
    for (int i = 0; i < len && ne < MAX_EDGES; i++) {
      const int j = (i + 1) % len;
      float x0 = xs[st + i], y0 = ys[st + i], x1 = xs[st + j], y1 = ys[st + j];
      if (y0 == y1) continue;
      Edge &e = edges[ne++];
      if (y0 < y1) { e.x0 = x0; e.y0 = y0; e.x1 = x1; e.y1 = y1; e.dir = 1; }
      else         { e.x0 = x1; e.y0 = y1; e.x1 = x0; e.y1 = y0; e.dir = -1; }
      ymin = min(ymin, e.y0);
      ymax = max(ymax, e.y1);
    }
  }
  if (!ne) return;
  const int ya = max(clipY0, (int)floorf(ymin));
  const int yb = min(clipY1, (int)ceilf(ymax));
  static Cross cross[256];
  for (int y = ya; y < yb; y++) {
    int rx0 = W, rx1 = -1;
    for (int sub = 0; sub < 4; sub++) {
      const float sy = y + (sub + 0.5f) * 0.25f;
      int nc = 0;
      for (int k = 0; k < ne && nc < 256; k++) {
        const Edge &e = edges[k];
        if (sy < e.y0 || sy >= e.y1) continue;
        const float x = e.x0 + (sy - e.y0) * (e.x1 - e.x0) / (e.y1 - e.y0);
        // insertion sort by x
        int p = nc++;
        while (p > 0 && cross[p - 1].x > x) { cross[p] = cross[p - 1]; p--; }
        cross[p].x = x;
        cross[p].dir = e.dir;
      }
      int wind = 0;
      float start = 0;
      for (int k = 0; k < nc; k++) {
        const int prev = wind;
        wind += cross[k].dir;
        if (prev == 0 && wind != 0) start = cross[k].x;
        else if (prev != 0 && wind == 0) addSpan(start, cross[k].x, rx0, rx1);
      }
    }
    if (rx1 < rx0) continue;
    uint8_t *mrow = mask + y * W;
    for (int x = rx0; x <= rx1; x++) {
      const int v = (int)(acc[x] * 255.0f + 0.5f);
      acc[x] = 0;
      if (v <= 0) continue;
      const uint8_t m = v > 255 ? 255 : v;
      if (m > mrow[x]) mrow[x] = m;
    }
    if (rx0 < mx0) mx0 = rx0;
    if (rx1 + 1 > mx1) mx1 = rx1 + 1;
    if (y < my0) my0 = y;
    if (y + 1 > my1) my1 = y + 1;
  }
}

static void rasterizeOne(const float *xs, const float *ys, int n) {
  int st = 0;
  rasterize(xs, ys, &st, &n, 1);
}

// Blend the mask onto the framebuffer with one colour, then clear it.
static void composite(uint32_t rgb, float alpha) {
  if (mx1 <= mx0 || my1 <= my0) return;
  const uint16_t c = to565(rgb);
  const uint32_t a256 = (uint32_t)(constrain(alpha, 0.0f, 1.0f) * 256);
  for (int y = my0; y < my1; y++) {
    uint8_t *mrow = mask + y * W;
    uint16_t *frow = fb + y * W;
    for (int x = mx0; x < mx1; x++) {
      const uint32_t m = mrow[x];
      if (!m) continue;
      mrow[x] = 0;
      frow[x] = blend(frow[x], c, ((m + (m >> 7)) * a256) >> 8);
    }
  }
  maskResetBox();
}

static int circleSegments(float r) {
  int n = (int)(r * 0.8f) + 10;
  return n < 10 ? 10 : (n > 160 ? 160 : n);
}

void fillCircle(float cx, float cy, float r, uint32_t rgb, float alpha) {
  if (r <= 0 || alpha <= 0) return;
  Path p;
  p.circle(cx, cy, r);
  p.fill(rgb, alpha);
}
// ---------- Xf ----------
Xf &Xf::translate(float x, float y) {
  e += a * x + c * y;
  f += b * x + d * y;
  return *this;
}
Xf &Xf::rotate(float r) {
  const float cs = cosf(r), sn = sinf(r);
  const float na = a * cs + c * sn, nb = b * cs + d * sn;
  const float nc = -a * sn + c * cs, nd = -b * sn + d * cs;
  a = na; b = nb; c = nc; d = nd;
  return *this;
}
Xf &Xf::scale(float sx, float sy) {
  a *= sx; b *= sx; c *= sy; d *= sy;
  return *this;
}
float Xf::lineScale() const { return sqrtf(fabsf(a * d - b * c)); }

// ---------- Path ----------
void Path::addPoint(float x, float y) {
  if (n >= MAX_PTS || subs == 0) return;
  px[n] = x;
  py[n] = y;
  n++;
  subLen[subs - 1]++;
}

Path &Path::moveTo(float x, float y) {
  if (subs >= MAX_SUB) return *this;
  subStart[subs] = n;
  subLen[subs] = 0;
  subClosed[subs] = false;
  subs++;
  float tx, ty;
  xf.apply(x, y, tx, ty);
  addPoint(tx, ty);
  lastX = x; lastY = y;
  return *this;
}

Path &Path::lineTo(float x, float y) {
  if (subs == 0) return moveTo(x, y);
  float tx, ty;
  xf.apply(x, y, tx, ty);
  addPoint(tx, ty);
  lastX = x; lastY = y;
  return *this;
}

Path &Path::quadTo(float cx, float cy, float x, float y) {
  const float x0 = lastX, y0 = lastY;
  const int steps = 8;
  for (int i = 1; i <= steps; i++) {
    const float t = (float)i / steps, u = 1 - t;
    lineTo(u * u * x0 + 2 * u * t * cx + t * t * x, u * u * y0 + 2 * u * t * cy + t * t * y);
  }
  return *this;
}

Path &Path::bezierTo(float c1x, float c1y, float c2x, float c2y, float x, float y) {
  const float x0 = lastX, y0 = lastY;
  const int steps = 14;
  for (int i = 1; i <= steps; i++) {
    const float t = (float)i / steps, u = 1 - t;
    const float b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t;
    lineTo(b0 * x0 + b1 * c1x + b2 * c2x + b3 * x, b0 * y0 + b1 * c1y + b2 * c2y + b3 * y);
  }
  return *this;
}

Path &Path::arc(float cx, float cy, float r, float a0, float a1) {
  const float sweep = a1 - a0;
  int steps = (int)(fabsf(sweep) / (2 * PI) * circleSegments(r * xf.lineScale()));
  if (steps < 2) steps = 2;
  for (int i = 0; i <= steps; i++) {
    const float a = a0 + sweep * i / steps;
    lineTo(cx + cosf(a) * r, cy + sinf(a) * r);
  }
  return *this;
}

Path &Path::circle(float cx, float cy, float r) {
  const int steps = circleSegments(r * xf.lineScale());
  moveTo(cx + r, cy);
  for (int i = 1; i < steps; i++) {
    const float a = 2 * PI * i / steps;
    lineTo(cx + cosf(a) * r, cy + sinf(a) * r);
  }
  return close();
}

Path &Path::ellipse(float cx, float cy, float rx, float ry, float rot) {
  const int steps = circleSegments(max(rx, ry) * xf.lineScale());
  const float cs = cosf(rot), sn = sinf(rot);
  for (int i = 0; i < steps; i++) {
    const float a = 2 * PI * i / steps;
    const float ex = cosf(a) * rx, ey = sinf(a) * ry;
    const float x = cx + ex * cs - ey * sn, y = cy + ex * sn + ey * cs;
    if (i == 0) moveTo(x, y); else lineTo(x, y);
  }
  return close();
}

Path &Path::rect(float x, float y, float w, float h) {
  moveTo(x, y).lineTo(x + w, y).lineTo(x + w, y + h).lineTo(x, y + h);
  return close();
}

Path &Path::roundRect(float x, float y, float w, float h, float r) {
  r = min(r, min(w, h) / 2);
  if (r <= 0.5f) return rect(x, y, w, h);
  const int steps = 6;
  const float cxs[4] = {x + w - r, x + w - r, x + r, x + r};
  const float cys[4] = {y + r, y + h - r, y + h - r, y + r};
  const float a0s[4] = {-PI / 2, 0, PI / 2, PI};
  for (int k = 0; k < 4; k++) {
    for (int i = 0; i <= steps; i++) {
      const float a = a0s[k] + (PI / 2) * i / steps;
      const float px_ = cxs[k] + cosf(a) * r, py_ = cys[k] + sinf(a) * r;
      if (k == 0 && i == 0) moveTo(px_, py_); else lineTo(px_, py_);
    }
  }
  return close();
}

Path &Path::close() {
  if (subs) subClosed[subs - 1] = true;
  return *this;
}

void Path::fill(uint32_t rgb, float alpha) const {
  if (alpha <= 0 || !n) return;
  rasterize(px, py, subStart, subLen, subs);
  composite(rgb, alpha);
}

void Path::stroke(float width, uint32_t rgb, float alpha, bool round) const {
  if (alpha <= 0 || !n) return;
  const float hw = width * xf.lineScale() / 2;
  if (hw <= 0.05f) return;
  float qx[4], qy[4];
  for (int s = 0; s < subs; s++) {
    const int st = subStart[s], len = subLen[s];
    const int segs = subClosed[s] ? len : len - 1;
    for (int i = 0; i < segs; i++) {
      const int a = st + i, b = st + (i + 1) % len;
      const float dx = px[b] - px[a], dy = py[b] - py[a];
      const float l = sqrtf(dx * dx + dy * dy);
      if (l < 1e-4f) continue;
      const float nx = -dy / l * hw, ny = dx / l * hw;
      qx[0] = px[a] + nx; qy[0] = py[a] + ny;
      qx[1] = px[b] + nx; qy[1] = py[b] + ny;
      qx[2] = px[b] - nx; qy[2] = py[b] - ny;
      qx[3] = px[a] - nx; qy[3] = py[a] - ny;
      rasterizeOne(qx, qy, 4);
    }
    if (round) {  // round joins and caps
      const int cs = circleSegments(hw);
      static float cx_[160], cy_[160];
      for (int i = 0; i < len; i++) {
        for (int k = 0; k < cs; k++) {
          const float an = 2 * PI * k / cs;
          cx_[k] = px[st + i] + cosf(an) * hw;
          cy_[k] = py[st + i] + sinf(an) * hw;
        }
        rasterizeOne(cx_, cy_, cs);
      }
    }
  }
  composite(rgb, alpha);
}

// ---------- effects ----------
uint16_t *framebuffer() { return fb; }
uint16_t rgb565(uint32_t rgb) { return to565(rgb); }
uint16_t blendPixel(uint16_t dst, uint16_t src, uint32_t alpha256) { return blend(dst, src, alpha256); }

void glow(float cx, float cy, float r, float alpha, uint32_t rgb) {
  if (alpha <= 0 || r <= 0) return;
  const float cr = (rgb >> 16) & 255, cg = (rgb >> 8) & 255, cb = rgb & 255;
  const int x0 = max(clipX0, (int)floorf(cx - r)), x1 = min(clipX1, (int)ceilf(cx + r));
  const int y0 = max(clipY0, (int)floorf(cy - r)), y1 = min(clipY1, (int)ceilf(cy + r));
  for (int y = y0; y < y1; y++) {
    for (int x = x0; x < x1; x++) {
      const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      const float d = sqrtf(dx * dx + dy * dy) / r;
      if (d >= 1) continue;
      // same stops as the preview: 0 -> white, 0.3 -> rgb at 60%, 1 -> rgb at 0%
      float a, k;
      if (d < 0.3f) { k = d / 0.3f; a = 1 - 0.4f * k; }
      else { k = 1; a = 0.6f * (1 - (d - 0.3f) / 0.7f); }
      const uint32_t col = ((uint32_t)(255 + (cr - 255) * k) << 16) | ((uint32_t)(255 + (cg - 255) * k) << 8) |
                           (uint32_t)(255 + (cb - 255) * k);
      uint16_t &p = fb[y * W + x];
      p = blend(p, to565(col), (uint32_t)(a * alpha * 256));
    }
  }
}

// ---------- text ----------
static const Glyph *findGlyph(const Font &font, uint16_t cp) {
  for (int i = 0; i < font.count; i++)
    if (font.glyphs[i].cp == cp) return &font.glyphs[i];
  return nullptr;
}

float measure(const Font &font, const char *s, int len) {
  if (len < 0) len = strlen(s);
  float w = 0;
  for (int i = 0; i < len; i++) {
    const Glyph *g = findGlyph(font, (uint8_t)s[i]);
    if (g) w += g->adv16 / 16.0f;
  }
  return w;
}

void drawText(const Font &font, const char *s, float x, float yMid, uint32_t rgb, float alpha, float scale,
              int len) {
  if (alpha <= 0 || scale <= 0.01f) return;
  if (len < 0) len = strlen(s);
  const uint16_t c = to565(rgb);
  const uint32_t a256 = (uint32_t)(constrain(alpha, 0.0f, 1.0f) * 256);
  // canvas 'middle' = middle of the em box
  const float mid = (float)(font.ascent - font.descent) * font.size / (2.0f * (font.ascent + font.descent));
  const float baseline = yMid + mid * scale;
  float pen = x;
  for (int i = 0; i < len; i++) {
    const Glyph *g = findGlyph(font, (uint8_t)s[i]);
    if (!g) continue;
    const uint8_t *bmp = font.bmp + g->off;
    if (g->w && g->h) {
      if (scale == 1.0f) {
        const int gx = (int)lroundf(pen + g->x), gy = (int)lroundf(baseline + g->y);
        for (int yy = 0; yy < g->h; yy++) {
          const int Y = gy + yy;
          if (Y < clipY0 || Y >= clipY1) continue;
          for (int xx = 0; xx < g->w; xx++) {
            const int X = gx + xx;
            if (X < clipX0 || X >= clipX1) continue;
            const uint32_t m = bmp[yy * g->w + xx];
            if (!m) continue;
            uint16_t &d = fb[Y * W + X];
            d = blend(d, c, ((m + (m >> 7)) * a256) >> 8);
          }
        }
      } else {  // bilinear-scaled glyph
        const float ox = pen + g->x * scale, oy = baseline + g->y * scale;
        const int X0 = max(clipX0, (int)floorf(ox)), X1 = min(clipX1, (int)ceilf(ox + g->w * scale));
        const int Y0 = max(clipY0, (int)floorf(oy)), Y1 = min(clipY1, (int)ceilf(oy + g->h * scale));
        for (int Y = Y0; Y < Y1; Y++) {
          const float v = (Y + 0.5f - oy) / scale - 0.5f;
          const int v0 = (int)floorf(v);
          const float fv = v - v0;
          for (int X = X0; X < X1; X++) {
            const float u = (X + 0.5f - ox) / scale - 0.5f;
            const int u0 = (int)floorf(u);
            const float fu = u - u0;
            float sum = 0;
            for (int k = 0; k < 4; k++) {
              const int uu = u0 + (k & 1), vv = v0 + (k >> 1);
              if (uu < 0 || vv < 0 || uu >= g->w || vv >= g->h) continue;
              const float wgt = ((k & 1) ? fu : 1 - fu) * ((k >> 1) ? fv : 1 - fv);
              sum += bmp[vv * g->w + uu] * wgt;
            }
            if (sum < 1) continue;
            uint16_t &d = fb[Y * W + X];
            d = blend(d, c, ((uint32_t)sum * a256) >> 8);
          }
        }
      }
    }
    pen += g->adv16 / 16.0f * scale;
  }
}

void drawTextRotated(const Font &font, const char *s, float cx, float cyMid, float angle, uint32_t rgb, float alpha) {
  if (alpha <= 0) return;
  if (fabsf(angle) < 0.002f) {
    drawText(font, s, cx - measure(font, s) / 2, cyMid, rgb, alpha);
    return;
  }
  // Render the text's alpha into a small buffer, then draw it rotated (bilinear, inverse mapping).
  static uint8_t buf[320 * 80];
  const int bw = min(320, (int)ceilf(measure(font, s)) + 8), bh = min(80, (int)(font.ascent + font.descent) + 8);
  memset(buf, 0, bw * bh);
  const float mid = (float)(font.ascent - font.descent) * font.size / (2.0f * (font.ascent + font.descent));
  const float baseline = bh / 2.0f + mid;
  float pen = 4;
  for (const char *p = s; *p; p++) {
    const Glyph *g = findGlyph(font, (uint8_t)*p);
    if (!g) continue;
    const int gx = (int)lroundf(pen + g->x), gy = (int)lroundf(baseline + g->y);
    for (int yy = 0; yy < g->h; yy++)
      for (int xx = 0; xx < g->w; xx++) {
        const int X = gx + xx, Y = gy + yy;
        if (X < 0 || Y < 0 || X >= bw || Y >= bh) continue;
        const uint8_t v = font.bmp[g->off + yy * g->w + xx];
        if (v > buf[Y * bw + X]) buf[Y * bw + X] = v;
      }
    pen += g->adv16 / 16.0f;
  }
  const uint16_t c = to565(rgb);
  const uint32_t a256 = (uint32_t)(constrain(alpha, 0.0f, 1.0f) * 256);
  const float cs = cosf(angle), sn = sinf(angle);
  const float hx = bw / 2.0f, hy = bh / 2.0f, rad = sqrtf(hx * hx + hy * hy) + 1;
  const int X0 = max(clipX0, (int)(cx - rad)), X1 = min(clipX1, (int)(cx + rad) + 1);
  const int Y0 = max(clipY0, (int)(cyMid - rad)), Y1 = min(clipY1, (int)(cyMid + rad) + 1);
  for (int Y = Y0; Y < Y1; Y++) {
    for (int X = X0; X < X1; X++) {
      const float dx = X + 0.5f - cx, dy = Y + 0.5f - cyMid;
      const float u = dx * cs + dy * sn + hx - 0.5f, v = -dx * sn + dy * cs + hy - 0.5f;  // inverse rotation
      const int u0 = (int)floorf(u), v0 = (int)floorf(v);
      if (u0 < -1 || v0 < -1 || u0 >= bw || v0 >= bh) continue;
      const float fu = u - u0, fv = v - v0;
      float sum = 0;
      for (int k = 0; k < 4; k++) {
        const int uu = u0 + (k & 1), vv = v0 + (k >> 1);
        if (uu < 0 || vv < 0 || uu >= bw || vv >= bh) continue;
        sum += buf[vv * bw + uu] * (((k & 1) ? fu : 1 - fu) * ((k >> 1) ? fv : 1 - fv));
      }
      if (sum < 1) continue;
      uint16_t &d = fb[Y * W + X];
      d = blend(d, c, ((uint32_t)sum * a256) >> 8);
    }
  }
}

}  // namespace g2d
