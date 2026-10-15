// g2d - a tiny anti-aliased 2D renderer for an RGB565 framebuffer.
//
// Works like a stripped-down HTML canvas: build a Path (with an affine transform applied as points
// are added), then fill() or stroke() it with a colour and an alpha. Shapes are rasterised into a
// coverage mask (4 sub-scanlines per pixel, exact horizontal coverage) and blended once, so
// overlapping parts of one stroke don't get darker. Text uses pre-rendered 8-bit alpha glyphs.
#pragma once
#include <stdint.h>
#include "font_types.h"

namespace g2d {

// Affine transform, same convention as canvas: x' = a*x + c*y + e, y' = b*x + d*y + f.
struct Xf {
  float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
  Xf &translate(float x, float y);
  Xf &rotate(float r);
  Xf &scale(float sx, float sy);
  Xf &scale(float s) { return scale(s, s); }
  void apply(float x, float y, float &ox, float &oy) const {
    ox = a * x + c * y + e;
    oy = b * x + d * y + f;
  }
  float lineScale() const;  // how much a line width grows under this transform
};

class Path {
 public:
  static const int MAX_PTS = 640;
  static const int MAX_SUB = 48;

  explicit Path(const Xf &xf = Xf()) : xf(xf) {}
  Xf xf;  // applied to every point as it is added

  Path &moveTo(float x, float y);
  Path &lineTo(float x, float y);
  Path &quadTo(float cx, float cy, float x, float y);
  Path &bezierTo(float c1x, float c1y, float c2x, float c2y, float x, float y);
  Path &arc(float cx, float cy, float r, float a0, float a1);  // continues the current subpath
  Path &circle(float cx, float cy, float r);                   // closed subpath of its own
  Path &ellipse(float cx, float cy, float rx, float ry, float rot);
  Path &rect(float x, float y, float w, float h);
  Path &roundRect(float x, float y, float w, float h, float r);
  Path &close();

  void fill(uint32_t rgb, float alpha = 1) const;
  void stroke(float width, uint32_t rgb, float alpha = 1, bool round = true) const;

 private:
  void addPoint(float x, float y);  // already-transformed point
  float px[MAX_PTS], py[MAX_PTS];
  int n = 0;
  int subStart[MAX_SUB], subLen[MAX_SUB];
  bool subClosed[MAX_SUB];
  int subs = 0;
  float lastX = 0, lastY = 0;  // last point in user space (for curves)
};

void begin(uint16_t *framebuffer, int width, int height);
int width();
int height();

void clear(uint32_t rgb);
void dim(float keep);  // multiply every pixel by `keep` (fade to black)

void setClip(int x0, int y0, int x1, int y1);  // pixels inside [x0,x1) x [y0,y1) are drawn
void resetClip();

// Convenience shapes.
void fillCircle(float cx, float cy, float r, uint32_t rgb, float alpha = 1);

// Soft radial glow: white in the centre fading through `rgb` to transparent at radius r.
void glow(float cx, float cy, float r, float alpha, uint32_t rgb);

// Direct pixel access for custom effects.
uint16_t *framebuffer();
uint16_t rgb565(uint32_t rgb);
uint16_t blendPixel(uint16_t dst, uint16_t src, uint32_t alpha256);  // alpha 0..256

// Text centred on (cx, cyMid) and rotated by `angle` radians about that point.
void drawTextRotated(const Font &font, const char *s, float cx, float cyMid, float angle, uint32_t rgb,
                     float alpha = 1);

// Text: (x, yMid) is the left end of the text's middle line (canvas textBaseline = 'middle').
// `scale` scales the text about that point.
float measure(const Font &font, const char *s, int len = -1);
void drawText(const Font &font, const char *s, float x, float yMid, uint32_t rgb, float alpha = 1,
              float scale = 1, int len = -1);

}  // namespace g2d
