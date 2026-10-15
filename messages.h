// The Hello screen and the five animated messages (ported 1:1 from the browser preview).
#pragma once

namespace messages {

static const int COUNT = 5;

float duration(int i);       // seconds until message i's animation is complete
void draw(int i, float t);   // draw message i at t seconds into the frame buffer (via g2d)
void drawHello(float t);  // t = seconds since the Hello screen appeared

}  // namespace messages
