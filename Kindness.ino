// Kindness - Waveshare ESP32-S3-Touch-AMOLED-1.75C
//
//   1. Hello screen: a glowing pen writes "hello", which then floats and glints;
//      "shake me" wiggles every few seconds.
//   2. Each shake plays the next of 5 animated kind messages (shake 6 -> message 1 again),
//      with its own music for the length of the message.
//      Shakes while a message is showing are ignored.
//   3. A few seconds after a message finishes, it fades back to the Hello screen.
//   4. Pressing BOOT or PWR goes straight back to "Hello" (music stops).
//
// Serial test hooks (115200 baud): 's' = shake, 'b' = button, '1'..'5' = play that message.

#include <Arduino.h>
#include <Wire.h>
#include "pin_config.h"
#include "Arduino_GFX_Library.h"
#include "SensorQMI8658.hpp"
#include "XPowersLib.h"
#include "g2d.h"
#include "messages.h"
#include "audio.h"

SET_LOOP_TASK_STACK_SIZE(48 * 1024);  // the renderer keeps paths on the stack

// ---------- Settings ----------
static const float HOLD_SEC = 4.0f;  // message stays up this long after its animation ends
static const float FADE_SEC = 0.5f;  // fade back to Hello

static const float SHAKE_THRESHOLD_G = 1.2f;  // deviation from 1 g that counts as a jolt
static const int SHAKE_JOLTS_NEEDED = 3;      // jolts needed ...
static const uint32_t SHAKE_WINDOW_MS = 1000; // ... within this time window
static const uint32_t JOLT_DEBOUNCE_MS = 80;

static const int BOOT_BUTTON_PIN = 0;
static const uint8_t BRIGHTNESS = 200;    // 0..255
static const int32_t QSPI_HZ = 80000000;  // display bus clock (library default is 40 MHz)

// ---------- Hardware ----------
Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *panel = new Arduino_CO5300(bus, LCD_RESET, 0, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);
// Full-screen PSRAM framebuffer: every frame is drawn here (by g2d), then sent to the panel in one go.
Arduino_Canvas *screenFb = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, panel);
SensorQMI8658 imu;
XPowersPMU pmu;

bool imuOk = false;
bool pmuOk = false;

// ---------- State ----------
enum State { HELLO, MESSAGE };
State state = HELLO;
int shakes = 0;        // total shakes that started a message
int current = 0;       // message being shown
uint32_t startMs = 0;  // when the current message started
uint32_t helloMs = 0;  // when the Hello screen appeared
uint32_t frames = 0;
uint32_t drawUs = 0, flushUs = 0;  // per-message timing totals

// ---------- Input ----------
// Shake detection runs in its own task so it keeps sampling while frames are being sent.
static int jolts = 0;
static volatile bool shakePending = false;  // set by the IMU task, taken by loop()
static volatile bool shakeReset = false;    // ask the IMU task to forget partial shakes

static bool shakeDetected() {
  static uint32_t lastJolt = 0;
  static uint32_t windowStart = 0;

  if (!imuOk || !imu.getDataReady()) return false;
  float ax, ay, az;
  if (!imu.getAccelerometer(ax, ay, az)) return false;

  const float deviation = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1.0f);
  const uint32_t now = millis();

  if (jolts > 0 && now - windowStart > SHAKE_WINDOW_MS) jolts = 0;

  if (deviation > SHAKE_THRESHOLD_G && now - lastJolt > JOLT_DEBOUNCE_MS) {
    lastJolt = now;
    if (jolts == 0) windowStart = now;
    jolts++;
    if (jolts >= SHAKE_JOLTS_NEEDED) {
      jolts = 0;
      return true;
    }
  }
  return false;
}

static void imuTask(void *) {
  for (;;) {
    if (shakeReset) { jolts = 0; shakeReset = false; }
    if (shakeDetected()) shakePending = true;
    vTaskDelay(pdMS_TO_TICKS(4));
  }
}

static bool takeShake() {
  const bool s = shakePending;
  shakePending = false;
  return s;
}

static bool bootButtonPressed() {
  static bool wasDown = false;
  static uint32_t lastChange = 0;
  const bool down = digitalRead(BOOT_BUTTON_PIN) == LOW;
  if (down != wasDown && millis() - lastChange > 30) {
    lastChange = millis();
    wasDown = down;
    return down;  // report the press, not the release
  }
  return false;
}

static bool pwrButtonPressed() {
  static uint32_t lastPoll = 0;
  if (!pmuOk || millis() - lastPoll < 50) return false;
  lastPoll = millis();
  pmu.getIrqStatus();
  const bool pressed = pmu.isPekeyShortPressIrq();
  pmu.clearIrqStatus();
  return pressed;
}

// ---------- Screens ----------
static void showHello() {
  state = HELLO;
  helloMs = millis();
  shakeReset = true;  // shakes made during the message don't count towards the next one
  shakePending = false;
  Serial.println("[screen] Hello");
}

static void startMessage(int i) {
  state = MESSAGE;
  current = i;
  startMs = millis();
  frames = 0;
  drawUs = flushUs = 0;
  audio::play(i, messages::duration(i) + HOLD_SEC, FADE_SEC);
  Serial.printf("[screen] message %d (shake #%d)\n", i + 1, shakes);
}

// Draws one frame of the current message; returns false once it has faded back out.
static bool messageFrame() {
  const float t = (millis() - startMs) / 1000.0f;
  const float total = messages::duration(current) + HOLD_SEC;
  const uint32_t d0 = micros();
  messages::draw(current, t);
  if (t > total) {
    const float p = min(1.0f, (t - total) / FADE_SEC);
    g2d::dim(1 - p);
    if (p >= 1) {
      Serial.printf("[anim] message %d: %lu frames in %.1f s (%.1f fps), draw %lu us, flush %lu us per frame\n", current + 1,
                    frames, t, frames / t, drawUs / max(1UL, frames), flushUs / max(1UL, frames));
      return false;
    }
  }
  const uint32_t f0 = micros();
  drawUs += f0 - d0;
  screenFb->flush();
  flushUs += micros() - f0;
  frames++;
  return true;
}

// ---------- Main ----------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nKindness starting");

  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  Wire.begin(IIC_SDA, IIC_SCL);

  pmuOk = pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  if (pmuOk) {
    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    pmu.clearIrqStatus();
    pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
  }
  Serial.printf("PMU (AXP2101): %s\n", pmuOk ? "ok" : "NOT FOUND");

  imuOk = imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  if (imuOk) {
    imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_250Hz, SensorQMI8658::LPF_MODE_0);
    imu.enableAccelerometer();
  }
  Serial.printf("IMU (QMI8658): %s\n", imuOk ? "ok" : "NOT FOUND");
  if (imuOk) xTaskCreatePinnedToCore(imuTask, "imu", 4096, nullptr, 1, nullptr, 0);

  if (!screenFb->begin(QSPI_HZ)) Serial.println("Display / framebuffer init FAILED");
  panel->setBrightness(BRIGHTNESS);
  g2d::begin(screenFb->getFramebuffer(), LCD_WIDTH, LCD_HEIGHT);

  Serial.printf("Audio (ES8311): %s\n", audio::begin() ? "ok" : "FAILED");
  Serial.printf("Free PSRAM: %u bytes, free heap: %u bytes\n", ESP.getFreePsram(), ESP.getFreeHeap());
  showHello();
}

void loop() {
  const int cmd = Serial.available() ? Serial.read() : -1;
  const bool shake = takeShake() || cmd == 's';
  const bool button = (bootButtonPressed() | pwrButtonPressed()) || cmd == 'b';  // poll both every loop

  if (state == HELLO) {
    if (shake) {
      shakes++;
      startMessage((shakes - 1) % messages::COUNT);
    } else if (cmd >= '1' && cmd <= '5') {
      startMessage(cmd - '1');
    } else {
      messages::drawHello((millis() - helloMs) / 1000.0f);
      screenFb->flush();
    }
    return;
  }

  // MESSAGE: shakes are ignored; the button returns to Hello at once.
  if (button) {
    audio::stop();
    showHello();
    return;
  }
  if (!messageFrame()) showHello();
}
