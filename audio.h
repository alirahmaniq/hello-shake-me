// Speaker output: ES8311 codec + a small bell synthesizer running on core 0.
// Each message has its own melody (same notes as the browser preview) that lasts exactly as long
// as the message's progress bar, ending on a closing chord and fading out with the screen.
#pragma once

namespace audio {

static const int VOLUME = 2;  // 0..10, like the preview's slider

bool begin();                                       // after Wire.begin()
void play(int message, float totalSec, float fadeSec);  // start message 0..4's music
void stop();                                        // quick fade-out (button pressed)

}  // namespace audio
