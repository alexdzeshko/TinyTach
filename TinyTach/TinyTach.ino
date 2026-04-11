#include <tinyNeoPixel_Static.h>

// ── Pin assignments ──────────────────────────────────────────────────────────
#define LED_PIN     0   // PB0, physical pin 5 — WS2812B data
#define TACH_PIN    2   // PB2, physical pin 7 — tach signal input (INT0)
#define LIGHTS_PIN  3   // PB3, physical pin 2 — headlights on (HIGH = lights on)
#define SHIFT_PIN   4   // PB4, physical pin 3 — shift light output

#define NUM_LEDS    8

// ── Timing ───────────────────────────────────────────────────────────────────
#define MIN_INTERVAL    2000UL      // µs — sanity upper bound (~30,000 RPM)
#define MAX_INTERVAL    2000000UL   // µs — sanity lower bound (~30 RPM)
#define TIMEOUT_MS      2000UL      // ms — engine-off if no pulse received

// ── Pulse calibration ────────────────────────────────────────────────────────
// If RPM reads double the actual value, set this to 2.
#define PULSES_PER_REV  1


// ── Brightness ───────────────────────────────────────────────────────────────
// Each 100 RPM = 10% of the LED's band brightness.
// Max brightness caps (0–255). Adjust to taste.
#define BRIGHTNESS_DAY   178  // 70% — headlights off
#define BRIGHTNESS_NIGHT 102  // 40% — headlights on

// ── Shift light ──────────────────────────────────────────────────────────────
#define SHIFT_RPM        6700  // RPM threshold: LED 7 blinks red + PB4 activates
#define SHIFT_BLINK_MS   40    // rapid blink interval (ms)

// ── Welcome animation ────────────────────────────────────────────────────────
#define WELCOME_STEP_MS  80   // ms per LED during fill / drain

// ── LED color ramp ────────────────────────────────────────────────────────────
// Index 0 = 0–1k RPM … Index 7 = 7k+ RPM
// yellow-green (1–4k) → yellow (4–5k) → orange (6k) → red (7k+)
const uint8_t LED_R[NUM_LEDS] = {200, 200, 200, 200, 255, 255, 255, 200};
const uint8_t LED_G[NUM_LEDS] = {220, 220, 220, 220, 200, 120,  50, 220};
const uint8_t LED_B[NUM_LEDS] = {  0,   0,   0,   0,   0,   0,   0,   0};

byte pixels[NUM_LEDS * 3];
tinyNeoPixel leds = tinyNeoPixel(NUM_LEDS, LED_PIN, NEO_GRB, pixels);

// ── ISR state ─────────────────────────────────────────────────────────────────
volatile uint32_t prevMicros    = 0;
volatile uint32_t pulseInterval = 0;
volatile bool     newPulse      = false;
volatile bool     hasPrev       = false;

void tachISR() { // Interrupt Service Routine
  uint32_t now = micros();
  if (hasPrev) {
    pulseInterval = now - prevMicros;
    newPulse = true;
  }
  prevMicros = now;
  hasPrev    = true;
}

// ── Helpers ───────────────────────────────────────────────────────────────────
static inline uint8_t scaleChannel(uint8_t value, uint8_t brightness) {
  return (uint16_t)value * brightness / 255;
}

// Set LED at idx to its ramp color, scaled by brightness (0–255).
void setLED(uint8_t idx, uint8_t brightness) {
  leds.setPixelColor(idx, leds.Color(
    scaleChannel(LED_R[idx], brightness),
    scaleChannel(LED_G[idx], brightness),
    scaleChannel(LED_B[idx], brightness)
  ));
}

// ── Welcome sequence: fill 0→7, pause, drain 7→0 ─────────────────────────────
void welcomeSequence(uint8_t brightness) {
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    setLED(i, brightness);
    leds.show();
    delay(WELCOME_STEP_MS);
  }
  delay(200);
  for (int8_t i = NUM_LEDS - 1; i >= 0; i--) {
    leds.setPixelColor(i, 0);
    leds.show();
    delay(WELCOME_STEP_MS);
  }
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  pinMode(LED_PIN,    OUTPUT);
  pinMode(TACH_PIN,   INPUT);
  pinMode(LIGHTS_PIN, INPUT);   // HIGH when vehicle headlights are on
  pinMode(SHIFT_PIN,  OUTPUT);

  digitalWrite(SHIFT_PIN, LOW);
  leds.clear();
  leds.show();

  welcomeSequence(BRIGHTNESS_DAY);

  attachInterrupt(digitalPinToInterrupt(TACH_PIN), tachISR, RISING);
}

// ── Main loop ─────────────────────────────────────────────────────────────────
void loop() {
  static uint32_t rpm           = 0;
  static uint32_t lastSignalMs  = 0;
  static bool     engineStarted = false; // stays false until first ISR pulse
  static uint32_t blinkMs       = 0;
  static bool     blinkOn       = false;
  static uint32_t shiftBlinkMs  = 0;
  static bool     shiftBlinkOn  = false;
  static uint32_t lastDisplayMs = 0;

  // Atomically snapshot ISR variables
  noInterrupts();
  bool     got      = newPulse;
  uint32_t interval = pulseInterval;
  if (got) newPulse = false;
  interrupts();

  uint32_t now = millis();

  if (got) {
    engineStarted = true;
    lastSignalMs  = now;
    if (interval >= MIN_INTERVAL && interval <= MAX_INTERVAL) {
      uint32_t rawRpm = 60000000UL / interval / PULSES_PER_REV;
      // Reject spikes more than 2x current RPM — no real engine can double RPM
      // in one pulse. Skip the startup phase (rpm < 500) where jumps are normal.
      if (rpm > 500 && rawRpm > rpm * 2) {
        // spike — ignore
      } else if (rawRpm > rpm) {
        // Rising: smooth to suppress noise
        rpm = (rpm * 5 + rawRpm) / 6;
      } else {
        // Falling: respond immediately
        rpm = rawRpm;
      }
    }
  }

  bool engineRunning = engineStarted && (now - lastSignalMs <= TIMEOUT_MS);
  if (!engineRunning) rpm = 0;

  // Day / night brightness based on headlights input
  uint8_t maxBrightness = digitalRead(LIGHTS_PIN) ? BRIGHTNESS_NIGHT : BRIGHTNESS_DAY;

  if (!engineRunning) {
    // ── Engine off: slow red blink on LED 0, ~1 Hz ──────────────────────────
    if (now - blinkMs >= 500) {
      blinkMs = now;
      blinkOn  = !blinkOn;
      leds.clear();
      if (blinkOn) {
        leds.setPixelColor(0, leds.Color(128, 0, 0)); // fixed 50%
      }
      leds.show();
    }
    digitalWrite(SHIFT_PIN, LOW);

  } else {
    // ── Engine running: RPM bar with per-LED brightness, 50 Hz ──────────────
    if (now - lastDisplayMs >= 20) {
      lastDisplayMs = now;

      // fullLeds = number of LEDs fully lit at maxBrightness
      // partial  = brightness of the next (partially lit) LED
      // Example: 1500 RPM → fullLeds=1, partial=50% → LED0 full, LED1 dim
      uint8_t fullLeds = (uint8_t)(rpm / 1000);
      uint8_t partial  = (uint8_t)((uint32_t)(rpm % 1000) * maxBrightness / 1000);

      if (fullLeds >= NUM_LEDS) {
        fullLeds = NUM_LEDS;
        partial  = 0;
      }

      // Update shift blink state independently of 50 Hz display timer
      if (rpm >= SHIFT_RPM && now - shiftBlinkMs >= SHIFT_BLINK_MS) {
        shiftBlinkMs = now;
        shiftBlinkOn = !shiftBlinkOn;
      }
      if (rpm < SHIFT_RPM) shiftBlinkOn = false;

      leds.clear();
      for (uint8_t i = 0; i < fullLeds; i++) {
        // LED 7 is handled separately when shift light is active
        if (i == NUM_LEDS - 1 && rpm >= SHIFT_RPM) continue;
        setLED(i, maxBrightness);
      }
      if (fullLeds < NUM_LEDS && partial > 0) {
        leds.setPixelColor(fullLeds, leds.Color(
          scaleChannel(LED_R[fullLeds], partial),
          scaleChannel(LED_G[fullLeds], partial),
          scaleChannel(LED_B[fullLeds], partial)
        ));
      }
      // Shift light: LED 7 blinks red, PB4 follows
      if (rpm >= SHIFT_RPM && shiftBlinkOn) {
        leds.setPixelColor(NUM_LEDS - 1, leds.Color(scaleChannel(255, maxBrightness), 0, 0));
      }
      leds.show();

      digitalWrite(SHIFT_PIN, rpm >= SHIFT_RPM ? HIGH : LOW);
    }
  }
}
