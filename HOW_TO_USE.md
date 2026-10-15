# How to use — Hello, shake me

A quick guide to using the Kindness app on the **Waveshare ESP32-S3-Touch-AMOLED-1.75C**.

## What you need

- The board with the Kindness firmware installed (see [Build and flash](README.md#build-and-flash)).
- A 3.7 V Li-ion/LiPo battery with an MX1.25 2-pin plug (optional — it also runs from USB).
- A USB-C cable for charging and uploading.

## Switching on and off

| To | Do this |
|----|---------|
| Switch on | Press **PWR**, or plug in USB-C |
| Switch off | Hold **PWR** for a few seconds (handled by the board's power chip) |
| Charge | Plug in USB-C — the battery charges automatically, also while it's running |

## Using it

**1. Say hello.** When the board starts, a glowing pen writes *hello*. Two butterflies fly in and land
on the word, then rest there opening and closing their wings. *shake me* appears underneath and gives a
little wiggle every few seconds.

**2. Shake it.** Hold the board firmly and give it **three quick shakes** within about a second — like
shaking a snow globe. A soft shake is enough; you don't need to swing your arm.

**3. Enjoy the message.** A kind message plays with its own animation and melody:

| Shake | Message |
|-------|---------|
| 1st | *Your presence is a present to the world.* — a gift box opens |
| 2nd | *hey stranger! You are so loved…* — an envelope with a hand-written note |
| 3rd | *I'm proud of you!* — hand-writing, a smiley and floating hearts |
| 4th | *Be gentle with yourself…* — a rising sun |
| 5th | *Even the longest night ends with a sunrise. Keep going.* — a night sky |
| 6th | back to the first message |

**4. Back to hello.** A few seconds after the message ends, the screen fades back to *hello* and the pen
writes it again. Want to go back sooner? Press **BOOT** or **PWR**.

Shaking while a message is playing does nothing — it always finishes (or waits for a button).

## Buttons at a glance

| Button | Hello screen | During a message |
|--------|--------------|------------------|
| **Shake** (3 quick jolts) | Plays the next message | Ignored |
| **BOOT** | — | Back to hello |
| **PWR** (short press) | — | Back to hello |
| **PWR** (hold) | Switch off | Switch off |

## Tips

- **Sound:** the music is quiet on purpose (2 of 10). It's best in a calm room. The volume can be
  changed in `audio.h` (`VOLUME`).
- **Battery:** the hello screen is always gently animated, so a charge lasts hours rather than days.
  Charge it like a phone.
- **Placing it:** it looks lovely on a desk, a bedside table or in a small stand. Pick it up whenever
  you need a kind word.

## Troubleshooting

| Problem | Try this |
|---------|----------|
| Shaking does nothing | Make sure you're on the hello screen (not during a message). Shake a little quicker — three jolts within one second. |
| No sound | The volume is low by design — listen closely in a quiet room. Check the speaker is connected to the board's speaker pads. |
| Screen stays black | Press **PWR**. If the battery is empty, plug in USB-C for a few minutes. |
| It restarts or freezes | Unplug USB, hold **PWR** to switch off, then switch on again. |
| Can't upload new firmware | Hold **PWR** to switch off, then hold **BOOT** while plugging in USB-C, and upload again. |

## Care

- It is not waterproof — keep it away from water and rain.
- Shake it in your hand; don't throw or drop it.
- Use a protected Li-ion/LiPo battery and don't leave it charging unattended for long periods.

---

© 2026 alirahmaniq. All rights reserved. See [LICENSE](LICENSE).
