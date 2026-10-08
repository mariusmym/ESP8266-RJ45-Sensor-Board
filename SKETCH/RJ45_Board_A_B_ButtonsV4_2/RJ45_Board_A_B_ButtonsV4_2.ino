/*
  RJ45 Board — A/B Buttons
  ===========================================================================
  V4.2
  - DS18B20 robustness: up to 3 re-reads per sample, a sensor is only marked
    "err" after 3 consecutive failed samples (keeps last good value meanwhile)
  - 85.0 °C power-on value only rejected when it's an implausible jump
  - Diagnostics: per-sensor error counter + last bad raw value (OLED Info,
    web cards, Serial). -127 = no answer/CRC fail (wiring, pull-up),
    85 = conversion didn't run (power / parasite mode)
  - Parasite-power detection reported on Serial + Info screen
  V4.1
  - Web dashboard on the LAN: http://<STATIC_IP>/ or http://rj45-board.local
    live sensor cards, alarm banner with "Silence" button, 12 h history chart
  - Fixed IP (configured in secrets.h, DHCP fallback by commenting it out)
  - JSON API: /api/data, /api/history, POST /api/ack
  - On-board history buffer: 360 samples x 2 min per sensor (+ ambient)
  V4.0
  - Non-blocking DS18B20 reads: buttons stay instant (V3 blocked ~750 ms per frame)
  - Sensors are read by ROM address, bus is rescanned every 30 s (hot-plug)
  - Button engine: debounce, short press, long press, hold-to-repeat
  - New screens: Overview (all sensors), Ambient (SHT31 / BME280 auto-detect), Info
  - Per-sensor min/max + 1-minute trend arrow
  - Temperature alarms per sensor: ON/OFF, HIGH + LOW limit, sensor-lost alarm,
    0.5 °C hysteresis, buzzer pattern, display wakes up, any button = acknowledge
  - Settings: key beep, alarm sound, screen-off timeout, contrast
  - Alarms + settings saved in flash (EEPROM), survive power loss
  - Check-mark graphic shown on save
  - Time via ESP8266 built-in SNTP + POSIX TZ (DST automatic)
    -> NTPClient, Timezone and TimeLib libraries are no longer needed
  - WiFi credentials moved to secrets.h
  - Fixed: T4/T5 were reading sensor #2, RSSI bar gaps at exactly -55/-65.. dBm,
    '&' instead of '&&', SHT31/BME280 declared but never started
  V3.3
  - Implemented 6 menu Items
  - New startup sound
  V3.2
  - Horizontal lines, bigger menu font, displayTemp()/displayError(), NTP

  CONTROLS (A = left, B = right)
  ---------------------------------------------------------------------------
  Menu        A tap: next item      A hold: previous    B tap: open
  View screen A tap: next screen    A hold: previous    B tap: back to menu
              B hold on a sensor: reset its min/max (on Overview: reset all)
  Edit screen A tap: + / toggle     A hold: - (repeats, speeds up after ~2 s)
              B tap: next field     B hold: SAVE & exit
              (no button for 60 s = leave without saving)
  Alarm       any button = acknowledge (silence) — alarm re-arms after it clears
  Screen off  any button wakes the display (that press does nothing else)

  Libraries: U8g2, OneWire, DallasTemperature, Adafruit SHT31,
             Adafruit BME280 (+ Adafruit Unified Sensor, Adafruit BusIO)
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <time.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "Adafruit_SHT31.h"

#include "secrets.h"  // WIFI_SSID / WIFI_PASS
#include "graphic.h"  // check_mark_bits (64x64 XBM)
#include "webpage.h"  // INDEX_HTML (dashboard)

#define FW_VERSION "4.2"

// ───────────────────────── Pins ─────────────────────────
constexpr uint8_t PIN_ONEWIRE = 0;   // GPIO0  (D3) DS18B20 bus, 4.7k pull-up
constexpr uint8_t PIN_BTN_A   = 12;  // GPIO12 (D6) left button
constexpr uint8_t PIN_BTN_B   = 14;  // GPIO14 (D5) right button
constexpr uint8_t PIN_BUZZER  = 13;  // GPIO13 (D7)

// ─────────────────────── Tunables ───────────────────────
constexpr uint8_t  MAX_SENSORS        = 5;
constexpr uint32_t TEMP_INTERVAL_MS   = 2000;   // new reading every 2 s
constexpr uint32_t TEMP_CONVERSION_MS = 800;    // 12-bit conversion = 750 ms
constexpr uint32_t BUS_RESCAN_MS      = 30000;
constexpr uint32_t TREND_SAMPLE_MS    = 10000;  // 6 samples x 10 s = 1 min trend
constexpr uint8_t  TREND_SAMPLES      = 6;
constexpr float    TREND_THRESHOLD    = 0.2f;   // °C per minute to show an arrow
constexpr uint32_t FRAME_MS           = 100;
constexpr uint32_t EDIT_TIMEOUT_MS    = 60000;
constexpr uint32_t SAVED_SPLASH_MS    = 900;
constexpr uint32_t RESET_MSG_MS       = 1200;
constexpr uint32_t DEBOUNCE_MS        = 30;
constexpr uint32_t LONG_PRESS_MS      = 600;
constexpr uint32_t REPEAT_MS          = 150;
constexpr uint16_t FAST_AFTER_REPEATS = 12;     // hold ~2 s -> 2.0 °C steps
constexpr float    ALARM_HYSTERESIS   = 0.5f;
constexpr float    ALARM_MIN_GAP      = 2.0f;   // HIGH must be >= LOW + 2 °C
constexpr uint8_t  READ_RETRIES       = 3;      // scratchpad re-reads per sample
constexpr uint8_t  SENSOR_FAIL_LIMIT  = 3;      // consecutive bad samples before "err"
constexpr float    TEMP_MIN_LIMIT     = -55.0f;
constexpr float    TEMP_MAX_LIMIT     = 125.0f;

const char* const TZ_INFO = "EET-2EEST,M3.5.0/3,M10.5.0/4";  // Romania, DST auto
const char* const NTP_1   = "ro.pool.ntp.org";
const char* const NTP_2   = "pool.ntp.org";

constexpr uint16_t HISTORY_LEN         = 360;     // 360 x 2 min = 12 h
constexpr uint32_t HISTORY_INTERVAL_MS = 120000;
constexpr int16_t  HIST_NONE           = INT16_MIN;

const uint8_t SCREEN_OFF_MIN[] = { 0, 1, 5, 15, 30 };  // 0 = never
constexpr uint8_t SCREEN_OFF_COUNT = sizeof(SCREEN_OFF_MIN);

// ─────────────────────── Hardware objects ───────────────────────
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
OneWire oneWire(PIN_ONEWIRE);
DallasTemperature sensors(&oneWire);
Adafruit_SHT31 sht31;
Adafruit_BME280 bme;
ESP8266WebServer server(80);

// ─────────────────────── Buttons ───────────────────────
enum BtnEvent : uint8_t { EV_NONE, EV_SHORT, EV_LONG, EV_REPEAT };

class Button {
 public:
  explicit Button(uint8_t pin) : pin_(pin) {}
  void begin() { pinMode(pin_, INPUT_PULLUP); }

  // SHORT fires on release (if no long press happened),
  // LONG fires once while held, then REPEAT every REPEAT_MS.
  BtnEvent update() {
    const uint32_t now = millis();
    const bool raw = (digitalRead(pin_) == LOW);
    if (raw != lastRaw_) { lastRaw_ = raw; changedAt_ = now; }

    if (raw != stable_ && now - changedAt_ >= DEBOUNCE_MS) {
      stable_ = raw;
      if (stable_) { pressedAt_ = now; longFired_ = false; repeats_ = 0; return EV_NONE; }
      return longFired_ ? EV_NONE : EV_SHORT;
    }
    if (stable_) {
      if (!longFired_ && now - pressedAt_ >= LONG_PRESS_MS) {
        longFired_ = true; lastRepeat_ = now; return EV_LONG;
      }
      if (longFired_ && now - lastRepeat_ >= REPEAT_MS) {
        lastRepeat_ = now; repeats_++; return EV_REPEAT;
      }
    }
    return EV_NONE;
  }
  bool isDown() const { return stable_; }
  uint16_t repeats() const { return repeats_; }

 private:
  uint8_t  pin_;
  bool     lastRaw_ = false, stable_ = false, longFired_ = false;
  uint32_t changedAt_ = 0, pressedAt_ = 0, lastRepeat_ = 0;
  uint16_t repeats_ = 0;
};

Button btnA(PIN_BTN_A);
Button btnB(PIN_BTN_B);

// ─────────────────────── Settings (flash) ───────────────────────
struct AlarmCfg {
  uint8_t enabled;
  float   low;
  float   high;
};

struct Settings {
  uint32_t magic;
  AlarmCfg alarm[MAX_SENSORS];
  uint8_t  keyBeep;
  uint8_t  alarmSound;
  uint8_t  screenOffIdx;
  uint8_t  contrast;  // 1..8
};
constexpr uint32_t SETTINGS_MAGIC = 0x52344A01;

Settings settings;  // active
Settings edit;      // working copy while an edit screen is open

// ─────────────────────── Sensor + alarm state ───────────────────────
enum AlarmKind : uint8_t { AL_NONE, AL_HIGH, AL_LOW, AL_LOST };

struct AlarmState {
  AlarmKind kind  = AL_NONE;
  bool      acked = false;
};

struct SensorState {
  DeviceAddress addr;
  bool  present = false;
  bool  valid   = false;
  float temp = NAN, minT = NAN, maxT = NAN;
  float hist[TREND_SAMPLES];
  uint8_t  failStreak = 0;
  uint32_t reads = 0, errors = 0;
  float    lastBad = NAN;
};
bool parasitePower = false;

SensorState sens[MAX_SENSORS];
AlarmState  alarms[MAX_SENSORS];
uint8_t     sensorCount = 0;

enum AmbientType : uint8_t { AMB_NONE, AMB_SHT31, AMB_BME280 };
AmbientType ambType = AMB_NONE;
float ambT = NAN, ambH = NAN, ambP = NAN;

// ─────────────────────── UI state ───────────────────────
enum MenuId : uint8_t {
  M_OVERVIEW, M_T1, M_T2, M_T3, M_T4, M_T5, M_AMBIENT, M_ALARMS, M_SETTINGS, M_INFO, M_COUNT
};
const char* const MENU_LABELS[M_COUNT] = {
  "Overview", "Sensor T1", "Sensor T2", "Sensor T3", "Sensor T4", "Sensor T5",
  "Ambient", "Alarms", "Settings", "Info"
};

enum Screen : uint8_t { SCR_MENU, SCR_VIEW, SCR_ALARM_EDIT, SCR_SETTINGS, SCR_SAVED };

Screen   screen        = SCR_MENU;
uint8_t  menuIdx       = M_OVERVIEW;
uint8_t  viewIdx       = M_OVERVIEW;
uint8_t  field         = 0;
uint8_t  editSensor    = 0;
uint32_t savedUntil    = 0;
uint32_t resetMsgUntil = 0;
uint32_t lastActivity  = 0;
uint32_t lastFrame     = 0;
bool     displayAsleep = false;
bool     suppressInput = false;  // swallow events until both buttons released

// timing
uint32_t lastTempRequest = 0, lastBusScan = 0, lastTrendSample = 0;
bool     convPending = false, firstReadDone = false;
uint8_t  histPos = 0;
uint32_t nextAlarmBeep = 0;
uint8_t  alarmBeepStep = 0;

// history for the web chart: temps in tenths of a degree (int16 = 2 bytes)
constexpr uint8_t HIST_SERIES = MAX_SENSORS + 1;  // T1..T5 + ambient
int16_t  history[HIST_SERIES][HISTORY_LEN];
uint16_t histHead = 0, histCount = 0;
uint32_t lastHistory = 0;
bool     mdnsStarted = false;

// ─────────────────────── Helpers ───────────────────────
bool ambientAvailable() { return ambType != AMB_NONE; }
bool menuVisible(uint8_t i) { return i != M_AMBIENT || ambientAvailable(); }
bool isViewItem(uint8_t i) { return i != M_ALARMS && i != M_SETTINGS && menuVisible(i); }
bool isSensorItem(uint8_t i) { return i >= M_T1 && i <= M_T5; }

uint8_t stepMenu(uint8_t i, int8_t dir, bool viewsOnly) {
  for (uint8_t n = 0; n < M_COUNT; n++) {
    i = (uint8_t)((i + M_COUNT + dir) % M_COUNT);
    if (viewsOnly ? isViewItem(i) : menuVisible(i)) return i;
  }
  return i;
}

void fmtTemp(char* buf, size_t n, float t) {
  if (isnan(t)) strlcpy(buf, "--.-", n);
  else snprintf(buf, n, "%.1f", t);
}

bool timeString(char* buf, size_t n) {
  time_t now = time(nullptr);
  if (now < 1700000000) { strlcpy(buf, "--:--", n); return false; }  // not synced yet
  struct tm tmv;
  localtime_r(&now, &tmv);
  if (tmv.tm_sec & 1) snprintf(buf, n, "%02d %02d", tmv.tm_hour, tmv.tm_min);  // blinking colon
  else                snprintf(buf, n, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
  return true;
}

// ─────────────────────── Sound ───────────────────────
void click() {
  if (settings.keyBeep) tone(PIN_BUZZER, 2300, 30);
}

void confirmBeep() {
  if (settings.keyBeep) tone(PIN_BUZZER, 1800, 90);
}

void playStartupSound() {
  tone(PIN_BUZZER, 750, 100);
  delay(200);
  tone(PIN_BUZZER, 750, 100);
}

// ─────────────────────── Settings load/save ───────────────────────
void defaultSettings(Settings& s) {
  memset(&s, 0, sizeof(s));
  s.magic = SETTINGS_MAGIC;
  for (auto& a : s.alarm) { a.enabled = 0; a.low = 5.0f; a.high = 35.0f; }
  s.keyBeep      = 1;
  s.alarmSound   = 1;
  s.screenOffIdx = 2;  // 5 min
  s.contrast     = 8;
}

void sanitizeSettings(Settings& s) {
  for (auto& a : s.alarm) {
    a.enabled = a.enabled ? 1 : 0;
    if (isnan(a.low)  || a.low  < TEMP_MIN_LIMIT || a.low  > TEMP_MAX_LIMIT) a.low  = 5.0f;
    if (isnan(a.high) || a.high < TEMP_MIN_LIMIT || a.high > TEMP_MAX_LIMIT) a.high = 35.0f;
    if (a.low > a.high - ALARM_MIN_GAP) { a.low = 5.0f; a.high = 35.0f; }
  }
  s.keyBeep    = s.keyBeep ? 1 : 0;
  s.alarmSound = s.alarmSound ? 1 : 0;
  if (s.screenOffIdx >= SCREEN_OFF_COUNT) s.screenOffIdx = 2;
  if (s.contrast < 1 || s.contrast > 8) s.contrast = 8;
}

void loadSettings() {
  EEPROM.begin(sizeof(Settings));
  EEPROM.get(0, settings);
  if (settings.magic != SETTINGS_MAGIC) defaultSettings(settings);
  sanitizeSettings(settings);
}

void saveSettings() {
  EEPROM.put(0, settings);
  EEPROM.commit();
}

void applyContrast(uint8_t level) { u8g2.setContrast((uint8_t)(level * 32 - 1)); }

// ─────────────────────── Display power ───────────────────────
void wakeDisplay() {
  if (displayAsleep) { u8g2.setPowerSave(0); displayAsleep = false; }
  lastActivity = millis();
}

// ─────────────────────── Alarms ───────────────────────
bool anyAlarmActive() {
  for (auto& a : alarms) if (a.kind != AL_NONE) return true;
  return false;
}

int8_t firstUnacked() {
  for (uint8_t i = 0; i < MAX_SENSORS; i++)
    if (alarms[i].kind != AL_NONE && !alarms[i].acked) return (int8_t)i;
  return -1;
}

bool anyUnacked() { return firstUnacked() >= 0; }

void ackAlarms() {
  for (auto& a : alarms) if (a.kind != AL_NONE) a.acked = true;
  noTone(PIN_BUZZER);
}

void raiseAlarm(AlarmState& a, AlarmKind k) {
  a.kind  = k;
  a.acked = false;
  alarmBeepStep = 0;
  nextAlarmBeep = millis();
  wakeDisplay();
}

void evaluateAlarms() {
  if (!firstReadDone) return;  // no "sensor lost" false alarms during boot
  for (uint8_t i = 0; i < MAX_SENSORS; i++) {
    const AlarmCfg&    c = settings.alarm[i];
    const SensorState& s = sens[i];
    AlarmState&        a = alarms[i];

    if (!c.enabled) { a.kind = AL_NONE; a.acked = false; continue; }

    AlarmKind cond = !s.valid        ? AL_LOST
                   : s.temp >= c.high ? AL_HIGH
                   : s.temp <= c.low  ? AL_LOW
                                      : AL_NONE;
    if (a.kind == AL_NONE) {
      if (cond != AL_NONE) raiseAlarm(a, cond);
    } else {
      bool clear = s.valid && s.temp < c.high - ALARM_HYSTERESIS && s.temp > c.low + ALARM_HYSTERESIS;
      if (clear) { a.kind = AL_NONE; a.acked = false; }
      else if (cond != AL_NONE && cond != a.kind) raiseAlarm(a, cond);
    }
  }
}

void updateBuzzer(uint32_t now) {
  if (!settings.alarmSound || !anyUnacked()) return;
  static const uint16_t gaps[3] = { 200, 200, 1400 };  // beep-beep-beep ... pause
  if ((int32_t)(now - nextAlarmBeep) >= 0) {
    tone(PIN_BUZZER, 2800, 120);
    nextAlarmBeep = now + gaps[alarmBeepStep];
    alarmBeepStep = (alarmBeepStep + 1) % 3;
  }
}

// ─────────────────────── Sensors ───────────────────────
void resetMinMax(SensorState& s) { s.minT = s.maxT = NAN; }

void resetStats(SensorState& s) {
  resetMinMax(s);
  for (auto& h : s.hist) h = NAN;
}

void scanBus() {
  sensors.begin();
  sensors.setWaitForConversion(false);
  const uint8_t n = sensors.getDeviceCount();
  sensorCount = n > MAX_SENSORS ? MAX_SENSORS : n;
  const bool para = sensors.isParasitePowerMode();
  if (para != parasitePower || lastBusScan == 0) {
    Serial.printf("OneWire: %u sensor(s), %s power\n", n, para ? "PARASITE" : "normal 3-wire");
  }
  parasitePower = para;

  for (uint8_t i = 0; i < MAX_SENSORS; i++) {
    SensorState& s = sens[i];
    DeviceAddress a;
    const bool ok = (i < n) && sensors.getAddress(a, i);
    if (!ok) {
      if (s.present) resetStats(s);
      s.present = false;
      s.valid   = false;
      continue;
    }
    if (!s.present || memcmp(a, s.addr, sizeof(DeviceAddress)) != 0) {
      memcpy(s.addr, a, sizeof(DeviceAddress));
      resetStats(s);  // a different sensor landed in this slot
      s.failStreak = 0; s.reads = s.errors = 0; s.lastBad = NAN; s.temp = NAN;
    }
    s.present = true;
  }
}

bool plausible(float t, const SensorState& s) {
  if (t == DEVICE_DISCONNECTED_C || t < -55.5f || t > 125.5f) return false;
  // 85.0 is the DS18B20 power-on value: reject it only if it's a sudden jump
  if (t == 85.0f && (isnan(s.temp) || fabsf(s.temp - 85.0f) > 5.0f)) return false;
  return true;
}

float readSensor(const SensorState& s) {
  float t = DEVICE_DISCONNECTED_C;
  for (uint8_t attempt = 0; attempt < READ_RETRIES; attempt++) {
    t = sensors.getTempC(s.addr);  // re-reads the scratchpad, checks CRC
    if (plausible(t, s)) break;
    delayMicroseconds(500);
  }
  return t;
}

float trendOf(const SensorState& s) {
  const float oldest = s.hist[histPos];
  return (s.valid && !isnan(oldest)) ? s.temp - oldest : 0.0f;
}

void readAmbient() {
  if (ambType == AMB_SHT31) {
    ambT = sht31.readTemperature();
    ambH = sht31.readHumidity();
  } else if (ambType == AMB_BME280) {
    ambT = bme.readTemperature();
    ambH = bme.readHumidity();
    ambP = bme.readPressure() / 100.0f;
  }
}

void logReadings() {
  char b[8];
  Serial.print(F("Temps:"));
  for (auto& s : sens) {
    fmtTemp(b, sizeof b, s.valid ? s.temp : NAN);
    Serial.print(' ');
    Serial.print(b);
  }
  if (ambientAvailable()) { Serial.print(F(" | amb ")); Serial.print(ambT, 1); }
  Serial.println();
}

int16_t toTenths(float t) { return isnan(t) ? HIST_NONE : (int16_t)lroundf(t * 10.0f); }

void recordHistory(uint32_t now) {
  lastHistory = now;
  for (uint8_t i = 0; i < MAX_SENSORS; i++)
    history[i][histHead] = toTenths(sens[i].valid ? sens[i].temp : NAN);
  history[MAX_SENSORS][histHead] = toTenths(ambientAvailable() ? ambT : NAN);
  histHead = (histHead + 1) % HISTORY_LEN;
  if (histCount < HISTORY_LEN) histCount++;
}

void updateTemps(uint32_t now) {
  if (!convPending) {
    if (now - lastTempRequest < TEMP_INTERVAL_MS) return;
    if (now - lastBusScan >= BUS_RESCAN_MS) { scanBus(); lastBusScan = now; }
    sensors.requestTemperatures();  // returns immediately (async)
    lastTempRequest = now;
    convPending = true;
    return;
  }
  if (now - lastTempRequest < TEMP_CONVERSION_MS) return;
  convPending = false;

  for (auto& s : sens) {
    if (!s.present) { s.valid = false; continue; }
    const float t = readSensor(s);
    s.reads++;
    if (!plausible(t, s)) {
      s.errors++;
      s.lastBad = t;
      Serial.printf("T%u bad read: %.2f (streak %u, %lu/%lu errors)\n",
                    (unsigned)(&s - sens) + 1, t, s.failStreak + 1,
                    (unsigned long)s.errors, (unsigned long)s.reads);
      if (s.failStreak < 255) s.failStreak++;
      if (s.failStreak >= SENSOR_FAIL_LIMIT) s.valid = false;  // else keep last good value
      continue;
    }
    s.failStreak = 0;
    s.valid = true;
    s.temp = t;
    s.minT = isnan(s.minT) ? t : fminf(s.minT, t);
    s.maxT = isnan(s.maxT) ? t : fmaxf(s.maxT, t);
  }
  readAmbient();

  if (now - lastTrendSample >= TREND_SAMPLE_MS) {
    lastTrendSample = now;
    for (auto& s : sens) s.hist[histPos] = s.valid ? s.temp : NAN;
    histPos = (histPos + 1) % TREND_SAMPLES;
    logReadings();
  }

  if (histCount == 0 || now - lastHistory >= HISTORY_INTERVAL_MS) recordHistory(now);

  firstReadDone = true;
  evaluateAlarms();
}

// ─────────────────────── Drawing primitives ───────────────────────
void drawCentered(const char* s, int y, const uint8_t* font) {
  u8g2.setFont(font);
  u8g2.drawStr((128 - u8g2.getStrWidth(s)) / 2, y, s);
}

void drawSignal() {
  if (WiFi.status() != WL_CONNECTED) {  // small "x" = no WiFi
    u8g2.drawLine(116, 2, 122, 8);
    u8g2.drawLine(122, 2, 116, 8);
    return;
  }
  const long rssi = WiFi.RSSI();
  const uint8_t bars = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : rssi > -85 ? 1 : 0;
  for (uint8_t b = 0; b < 4; b++) {
    const uint8_t h = 2 + b * 2, x = 112 + b * 4;
    if (b < bars) u8g2.drawBox(x, 10 - h, 3, h);
    else          u8g2.drawHLine(x, 9, 3);
  }
}

void drawHeader(const char* center) {
  char t[6];
  timeString(t, sizeof t);
  u8g2.setFont(u8g2_font_spleen6x12_mr);
  u8g2.drawStr(1, 10, t);
  if (center) {
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(69 - u8g2.getStrWidth(center) / 2, 9, center);
  }
  if (anyAlarmActive()) {  // acknowledged-but-still-active alarm reminder
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(105, 9, "!");
  }
  drawSignal();
  u8g2.drawHLine(0, 12, 128);
}

void drawFooter(const char* left, const char* right) {
  u8g2.drawHLine(0, 52, 128);
  u8g2.setFont(u8g2_font_6x10_tr);
  if (left) {  // triangle points at button A
    u8g2.drawTriangle(0, 54, 5, 63, 10, 54);
    u8g2.drawStr(13, 63, left);
  }
  if (right) {  // triangle points at button B
    u8g2.drawTriangle(108, 54, 113, 63, 118, 54);
    u8g2.drawStr(105 - u8g2.getStrWidth(right), 63, right);
  }
}

// Big temperature with a drawn degree sign and optional trend arrow
void drawBigTemp(float t, int y, int8_t trend) {
  char buf[8];
  fmtTemp(buf, sizeof buf, t);
  u8g2.setFont(u8g2_font_profont29_tf);
  const int w = u8g2.getStrWidth(buf);
  const int x = (128 - (w + 13)) / 2;
  u8g2.drawStr(x, y, buf);
  u8g2.drawCircle(x + w + 4, y - 17, 2);
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(x + w + 7, y - 10, "C");
  if (trend > 0)      u8g2.drawTriangle(120, y - 4, 127, y - 4, 123, y - 11);  // rising
  else if (trend < 0) u8g2.drawTriangle(120, y - 11, 127, y - 11, 123, y - 4); // falling
}

// One editable row (4 rows fit between the two lines)
void drawRow(uint8_t row, const char* label, const char* value) {
  const int y = 21 + row * 9;
  u8g2.setFont(u8g2_font_6x10_tr);
  if (row == field) {
    u8g2.drawBox(0, y - 8, 128, 9);
    u8g2.setDrawColor(0);
  }
  u8g2.drawStr(3, y, label);
  u8g2.drawStr(125 - u8g2.getStrWidth(value), y, value);
  u8g2.setDrawColor(1);
}

// ─────────────────────── Screens ───────────────────────
void menuPreview(uint8_t i, char* buf, size_t n) {
  char t[8];
  switch (i) {
    case M_OVERVIEW: {
      uint8_t online = 0;
      for (auto& s : sens) online += s.valid;
      snprintf(buf, n, "%u of %u sensors online", online, MAX_SENSORS);
      break;
    }
    case M_AMBIENT:
      fmtTemp(t, sizeof t, ambT);
      if (isnan(ambH)) snprintf(buf, n, "%s C", t);
      else snprintf(buf, n, "%s C   %.0f %%RH", t, ambH);
      break;
    case M_ALARMS: {
      uint8_t on = 0;
      for (auto& a : settings.alarm) on += a.enabled;
      snprintf(buf, n, anyAlarmActive() ? "%u enabled - ACTIVE!" : "%u of 5 enabled", on);
      break;
    }
    case M_SETTINGS:
      strlcpy(buf, "sound / display", n);
      break;
    case M_INFO:
      if (WiFi.status() == WL_CONNECTED) snprintf(buf, n, "IP %s", WiFi.localIP().toString().c_str());
      else strlcpy(buf, "WiFi offline", n);
      break;
    default: {  // sensors
      const uint8_t si = i - M_T1;
      const SensorState& s = sens[si];
      if (!s.present)     strlcpy(buf, "not connected", n);
      else if (!s.valid)  strlcpy(buf, "read error", n);
      else {
        fmtTemp(t, sizeof t, s.temp);
        snprintf(buf, n, settings.alarm[si].enabled ? "%s C   alarm on" : "%s C", t);
      }
    }
  }
}

void drawMenu() {
  uint8_t pos = 0, total = 0;
  for (uint8_t i = 0; i < M_COUNT; i++) {
    if (!menuVisible(i)) continue;
    total++;
    if (i == menuIdx) pos = total;
  }
  char hdr[8];
  snprintf(hdr, sizeof hdr, "%u/%u", pos, total);
  drawHeader(hdr);

  drawCentered(MENU_LABELS[menuIdx], 35, u8g2_font_10x20_tr);
  char sub[28];
  menuPreview(menuIdx, sub, sizeof sub);
  drawCentered(sub, 48, u8g2_font_5x7_tr);

  drawFooter("Scroll", "Select");
}

void drawOverview(uint32_t now) {
  drawHeader("OVERVIEW");
  const bool blink = (now / 400) & 1;
  u8g2.setFont(u8g2_font_6x10_tr);

  for (uint8_t i = 0; i < 6; i++) {
    const int x = (i & 1) ? 66 : 2;
    const int y = 24 + (i >> 1) * 12;
    char cell[16], t[8];
    bool highlight = false;

    if (i < MAX_SENSORS) {
      const SensorState& s = sens[i];
      if (!s.present)    strlcpy(t, "--", sizeof t);
      else if (!s.valid) strlcpy(t, "err", sizeof t);
      else               fmtTemp(t, sizeof t, s.temp);
      snprintf(cell, sizeof cell, "T%u %s", i + 1, t);
      highlight = alarms[i].kind != AL_NONE && blink;
    } else if (ambientAvailable()) {
      fmtTemp(t, sizeof t, ambT);
      snprintf(cell, sizeof cell, "Am %s", t);
    } else {
      continue;
    }

    if (highlight) { u8g2.drawBox(x - 2, y - 9, 62, 11); u8g2.setDrawColor(0); }
    u8g2.drawStr(x, y, cell);
    u8g2.setDrawColor(1);
  }
  drawFooter("Next", "Back");
}

void drawSensorView(uint8_t si, uint32_t now) {
  char title[12];
  snprintf(title, sizeof title, "SENSOR T%u", si + 1);
  drawHeader(title);

  const SensorState& s = sens[si];
  if (!s.present) {
    drawCentered("Not connected", 36, u8g2_font_6x10_tr);
  } else if (!s.valid) {
    drawCentered("Read error", 36, u8g2_font_6x10_tr);
  } else {
    const float tr = trendOf(s);
    drawBigTemp(s.temp, 40, tr > TREND_THRESHOLD ? 1 : tr < -TREND_THRESHOLD ? -1 : 0);

    char line[28];
    if (now < resetMsgUntil) {
      strlcpy(line, "min/max reset", sizeof line);
    } else {
      char a[8], b[8];
      fmtTemp(a, sizeof a, s.minT);
      fmtTemp(b, sizeof b, s.maxT);
      snprintf(line, sizeof line, "min %s   max %s", a, b);
    }
    drawCentered(line, 50, u8g2_font_5x7_tr);
  }
  drawFooter("Next", "Back");
}

void drawAmbient() {
  drawHeader(ambType == AMB_SHT31 ? "AMBIENT SHT31" : "AMBIENT BME280");
  drawBigTemp(ambT, 38, 0);
  char line[28];
  if (ambType == AMB_BME280) snprintf(line, sizeof line, "%.0f %%RH    %.0f hPa", ambH, ambP);
  else                       snprintf(line, sizeof line, "%.1f %%RH", ambH);
  drawCentered(line, 50, u8g2_font_5x7_tr);
  drawFooter("Next", "Back");
}

void drawInfo() {
  drawHeader("INFO");
  u8g2.setFont(u8g2_font_5x7_tr);
  char l[28];

  if (WiFi.status() == WL_CONNECTED) {
    snprintf(l, sizeof l, "IP   %s", WiFi.localIP().toString().c_str());
    u8g2.drawStr(2, 20, l);
    snprintf(l, sizeof l, "RSSI %ld dBm  ch %d", (long)WiFi.RSSI(), WiFi.channel());
  } else {
    strlcpy(l, "WiFi connecting...", sizeof l);
    u8g2.drawStr(2, 20, l);
    snprintf(l, sizeof l, "SSID %s", WIFI_SSID);
  }
  u8g2.drawStr(2, 28, l);

  const uint32_t up = millis() / 1000;
  snprintf(l, sizeof l, "Up   %lud %02lu:%02lu:%02lu",
           (unsigned long)(up / 86400), (unsigned long)(up / 3600 % 24),
           (unsigned long)(up / 60 % 60), (unsigned long)(up % 60));
  u8g2.drawStr(2, 36, l);

  snprintf(l, sizeof l, "NTP  %s   Sensors %u/%u",
           time(nullptr) > 1700000000 ? "ok" : "--", sensorCount, MAX_SENSORS);
  u8g2.drawStr(2, 44, l);

  uint32_t errs = 0;
  for (auto& s : sens) errs += s.errors;
  snprintf(l, sizeof l, "v%s %s  err %lu", FW_VERSION, parasitePower ? "PARA" : "3w", (unsigned long)errs);
  u8g2.drawStr(2, 51, l);

  drawFooter("Next", "Back");
}

void drawAlarmEdit() {
  drawHeader("HOLD B = SAVE");
  const AlarmCfg&    c = edit.alarm[editSensor];
  const SensorState& s = sens[editSensor];
  char v[16], t[8];

  if (s.valid) { fmtTemp(t, sizeof t, s.temp); snprintf(v, sizeof v, "T%u (%s)", editSensor + 1, t); }
  else         snprintf(v, sizeof v, "T%u (--)", editSensor + 1);
  drawRow(0, "Sensor", v);

  drawRow(1, "Alarm", c.enabled ? "ON" : "OFF");

  fmtTemp(t, sizeof t, c.high);
  snprintf(v, sizeof v, "%s C", t);
  drawRow(2, "High >=", v);

  fmtTemp(t, sizeof t, c.low);
  snprintf(v, sizeof v, "%s C", t);
  drawRow(3, "Low <=", v);

  drawFooter("+ / hold -", "Next");
}

void drawSettings() {
  drawHeader("HOLD B = SAVE");
  char v[12];
  drawRow(0, "Key beep", edit.keyBeep ? "ON" : "OFF");
  drawRow(1, "Alarm sound", edit.alarmSound ? "ON" : "OFF");
  const uint8_t m = SCREEN_OFF_MIN[edit.screenOffIdx];
  if (m == 0) strlcpy(v, "never", sizeof v);
  else snprintf(v, sizeof v, "%u min", m);
  drawRow(2, "Screen off", v);
  snprintf(v, sizeof v, "%u/8", edit.contrast);
  drawRow(3, "Contrast", v);
  drawFooter("+ / hold -", "Next");
}

void drawSaved() {
  u8g2.drawXBMP(32, 0, check_mark_width, check_mark_height, check_mark_bits);
}

void drawAlarmOverlay(uint32_t now) {
  const int8_t i = firstUnacked();
  if (i < 0) return;
  const SensorState& s = sens[i];
  const AlarmCfg&    c = settings.alarm[i];

  if ((now / 300) & 1) { u8g2.drawBox(0, 0, 128, 13); u8g2.setDrawColor(0); }
  drawCentered("!!  ALARM  !!", 10, u8g2_font_6x10_tr);
  u8g2.setDrawColor(1);

  const char* kind = alarms[i].kind == AL_HIGH ? "HIGH" : alarms[i].kind == AL_LOW ? "LOW" : "LOST";
  char l[28], a[8], b[8];
  snprintf(l, sizeof l, "T%d %s", i + 1, kind);
  drawCentered(l, 34, u8g2_font_10x20_tr);

  if (alarms[i].kind == AL_LOST) {
    strlcpy(l, "sensor not responding", sizeof l);
  } else {
    fmtTemp(a, sizeof a, s.temp);
    fmtTemp(b, sizeof b, alarms[i].kind == AL_HIGH ? c.high : c.low);
    snprintf(l, sizeof l, "now %s C  limit %s C", a, b);
  }
  drawCentered(l, 47, u8g2_font_5x7_tr);
  drawFooter("Ack", "Ack");
}

void render(uint32_t now) {
  u8g2.clearBuffer();
  if (anyUnacked()) {
    drawAlarmOverlay(now);
  } else {
    switch (screen) {
      case SCR_MENU:       drawMenu(); break;
      case SCR_ALARM_EDIT: drawAlarmEdit(); break;
      case SCR_SETTINGS:   drawSettings(); break;
      case SCR_SAVED:      drawSaved(); break;
      case SCR_VIEW:
        if (viewIdx == M_OVERVIEW)      drawOverview(now);
        else if (viewIdx == M_AMBIENT)  drawAmbient();
        else if (viewIdx == M_INFO)     drawInfo();
        else                            drawSensorView(viewIdx - M_T1, now);
        break;
    }
  }
  u8g2.sendBuffer();
}

// ─────────────────────── Input handling ───────────────────────
void openMenuItem(uint8_t i) {
  if (i == M_ALARMS) {
    edit = settings; field = 0; screen = SCR_ALARM_EDIT;
  } else if (i == M_SETTINGS) {
    edit = settings; field = 0; screen = SCR_SETTINGS;
  } else {
    viewIdx = i; screen = SCR_VIEW;
  }
}

void commitEdit() {
  settings = edit;
  sanitizeSettings(settings);
  saveSettings();
  applyContrast(settings.contrast);
  evaluateAlarms();
  confirmBeep();
  screen = SCR_SAVED;
  savedUntil = millis() + SAVED_SPLASH_MS;
}

void leaveEditWithoutSaving() {
  applyContrast(settings.contrast);  // undo live contrast preview
  screen = SCR_MENU;
}

void adjustAlarmField(int8_t dir, bool repeat, float step) {
  AlarmCfg& c = edit.alarm[editSensor];
  switch (field) {
    case 0: editSensor = (uint8_t)((editSensor + MAX_SENSORS + dir) % MAX_SENSORS); break;
    case 1: if (!repeat) c.enabled = !c.enabled; break;
    case 2: c.high = constrain(c.high + dir * step, c.low + ALARM_MIN_GAP, TEMP_MAX_LIMIT); break;
    case 3: c.low  = constrain(c.low  + dir * step, TEMP_MIN_LIMIT, c.high - ALARM_MIN_GAP); break;
  }
}

void adjustSettingsField(int8_t dir, bool repeat) {
  switch (field) {
    case 0: if (!repeat) edit.keyBeep    = !edit.keyBeep; break;
    case 1: if (!repeat) edit.alarmSound = !edit.alarmSound; break;
    case 2: edit.screenOffIdx = (uint8_t)((edit.screenOffIdx + SCREEN_OFF_COUNT + dir) % SCREEN_OFF_COUNT); break;
    case 3:
      edit.contrast = (uint8_t)constrain(edit.contrast + dir, 1, 8);
      applyContrast(edit.contrast);  // live preview
      break;
  }
}

void handleButtons(uint32_t now) {
  const BtnEvent a = btnA.update();
  const BtnEvent b = btnB.update();

  if (suppressInput) {
    if (!btnA.isDown() && !btnB.isDown()) suppressInput = false;
    return;
  }
  if (a == EV_NONE && b == EV_NONE) return;
  lastActivity = now;

  if (displayAsleep) { wakeDisplay(); suppressInput = true; return; }
  if (anyUnacked())  { ackAlarms(); click(); suppressInput = true; return; }
  if (screen == SCR_SAVED) return;

  const bool aNext  = (a == EV_SHORT);
  const bool aPrev  = (a == EV_LONG || a == EV_REPEAT);
  const bool bShort = (b == EV_SHORT);
  const bool bLong  = (b == EV_LONG);
  const int8_t dir  = aNext ? 1 : -1;

  switch (screen) {
    case SCR_MENU:
      if (aNext || aPrev) { menuIdx = stepMenu(menuIdx, dir, false); click(); }
      else if (bShort)    { openMenuItem(menuIdx); click(); }
      break;

    case SCR_VIEW:
      if (aNext || aPrev) { viewIdx = stepMenu(viewIdx, dir, true); menuIdx = viewIdx; click(); }
      else if (bShort)    { menuIdx = viewIdx; screen = SCR_MENU; click(); }
      else if (bLong) {
        if (isSensorItem(viewIdx)) resetMinMax(sens[viewIdx - M_T1]);
        else if (viewIdx == M_OVERVIEW) for (auto& s : sens) resetMinMax(s);
        else break;
        resetMsgUntil = now + RESET_MSG_MS;
        confirmBeep();
      }
      break;

    case SCR_ALARM_EDIT:
      if (aNext || aPrev) {
        const float step = btnA.repeats() > FAST_AFTER_REPEATS ? 2.0f : 0.5f;
        adjustAlarmField(dir, a == EV_REPEAT, step);
        click();
      } else if (bShort) { field = (field + 1) % 4; click(); }
      else if (bLong)    { commitEdit(); }
      break;

    case SCR_SETTINGS:
      if (aNext || aPrev) { adjustSettingsField(dir, a == EV_REPEAT); click(); }
      else if (bShort)    { field = (field + 1) % 4; click(); }
      else if (bLong)     { commitEdit(); }
      break;

    case SCR_SAVED:
      break;
  }
}

void updateUiTimers(uint32_t now) {
  if (screen == SCR_SAVED && (int32_t)(now - savedUntil) >= 0) screen = SCR_MENU;

  if ((screen == SCR_ALARM_EDIT || screen == SCR_SETTINGS) && now - lastActivity >= EDIT_TIMEOUT_MS)
    leaveEditWithoutSaving();

  const uint8_t mins = SCREEN_OFF_MIN[settings.screenOffIdx];
  if (!displayAsleep && mins && !anyUnacked() && now - lastActivity >= mins * 60000UL) {
    u8g2.setPowerSave(1);
    displayAsleep = true;
  }
}

// ─────────────────────── Web server ───────────────────────
void jsonNum(String& j, float v, uint8_t dec = 1) {
  if (isnan(v)) { j += F("null"); return; }
  char b[16];
  snprintf(b, sizeof b, "%.*f", dec, v);
  j += b;
}

const char* alarmName(AlarmKind k) {
  return k == AL_HIGH ? "HIGH" : k == AL_LOW ? "LOW" : k == AL_LOST ? "LOST" : "NONE";
}

void handleData() {
  String j;
  j.reserve(1600);
  char b[40];

  j += F("{\"fw\":\"" FW_VERSION "\",\"time\":\"");
  time_t now = time(nullptr);
  const bool synced = now > 1700000000;
  if (synced) {
    struct tm tmv;
    localtime_r(&now, &tmv);
    snprintf(b, sizeof b, "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    j += b;
  }
  j += F("\",\"synced\":");      j += synced ? F("true") : F("false");
  j += F(",\"uptime\":");        j += String(millis() / 1000);
  j += F(",\"ip\":\"");          j += WiFi.localIP().toString();
  j += F("\",\"rssi\":");        j += String(WiFi.RSSI());
  j += F(",\"heap\":");          j += String(ESP.getFreeHeap());
  j += F(",\"parasite\":");      j += parasitePower ? F("true") : F("false");
  j += F(",\"alarmSound\":");    j += settings.alarmSound ? '1' : '0';
  j += F(",\"sensors\":[");

  for (uint8_t i = 0; i < MAX_SENSORS; i++) {
    const SensorState& s = sens[i];
    const AlarmCfg&    c = settings.alarm[i];
    if (i) j += ',';
    j += F("{\"id\":");      j += String(i + 1);
    j += F(",\"present\":"); j += s.present ? F("true") : F("false");
    j += F(",\"valid\":");   j += s.valid ? F("true") : F("false");
    j += F(",\"t\":");       jsonNum(j, s.valid ? s.temp : NAN);
    j += F(",\"min\":");     jsonNum(j, s.minT);
    j += F(",\"max\":");     jsonNum(j, s.maxT);
    j += F(",\"trend\":");   jsonNum(j, trendOf(s), 2);
    j += F(",\"reads\":");   j += String(s.reads);
    j += F(",\"errors\":");  j += String(s.errors);
    j += F(",\"lastBad\":"); jsonNum(j, s.lastBad, 2);
    j += F(",\"addr\":\"");
    if (s.present) {
      for (uint8_t k = 0; k < 8; k++) { snprintf(b, sizeof b, "%02X", s.addr[k]); j += b; }
    }
    j += F("\",\"al\":{\"on\":"); j += c.enabled ? F("true") : F("false");
    j += F(",\"lo\":");     jsonNum(j, c.low);
    j += F(",\"hi\":");     jsonNum(j, c.high);
    j += F(",\"state\":\""); j += alarmName(alarms[i].kind);
    j += F("\",\"ack\":");  j += alarms[i].acked ? F("true") : F("false");
    j += F("}}");
  }
  j += ']';

  j += F(",\"ambient\":");
  if (ambientAvailable()) {
    j += F("{\"type\":\"");  j += ambType == AMB_SHT31 ? F("SHT31") : F("BME280");
    j += F("\",\"t\":");    jsonNum(j, ambT);
    j += F(",\"h\":");      jsonNum(j, ambH);
    j += F(",\"p\":");      jsonNum(j, ambType == AMB_BME280 ? ambP : NAN, 0);
    j += '}';
  } else {
    j += F("null");
  }
  j += '}';

  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.send(200, F("application/json"), j);
}

// Streams the history in chunks so we never build one big String in RAM
void handleHistory() {
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, F("application/json"), "");

  char b[64];
  snprintf(b, sizeof b, "{\"interval\":%lu,\"age\":%lu,\"series\":[",
           (unsigned long)(HISTORY_INTERVAL_MS / 1000),
           (unsigned long)(histCount ? (millis() - lastHistory) / 1000 : 0));
  server.sendContent(b);

  String chunk;
  chunk.reserve(560);
  for (uint8_t s = 0; s < HIST_SERIES; s++) {
    chunk = s ? F(",[") : F("[");
    for (uint16_t k = 0; k < histCount; k++) {
      const uint16_t idx = (histHead + HISTORY_LEN - histCount + k) % HISTORY_LEN;
      const int16_t v = history[s][idx];
      if (k) chunk += ',';
      if (v == HIST_NONE) chunk += F("null");
      else chunk += String(v);
      if (chunk.length() > 500) { server.sendContent(chunk); chunk = ""; }
    }
    chunk += ']';
    server.sendContent(chunk);
  }
  server.sendContent(F("]}"));
  server.sendContent("");  // end of chunked response
}

void handleAck() {
  ackAlarms();
  server.send(204);
}

void setupWeb() {
  server.on("/", HTTP_GET, [] {
    server.send_P(200, PSTR("text/html; charset=utf-8"), INDEX_HTML);
  });
  server.on("/api/data", HTTP_GET, handleData);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/ack", HTTP_POST, handleAck);
  server.onNotFound([] { server.send(404, F("text/plain"), F("Not found")); });
  server.begin();
}

void updateNetwork() {
  server.handleClient();
  if (WiFi.status() == WL_CONNECTED) {
    if (!mdnsStarted && MDNS.begin(MDNS_NAME)) {
      MDNS.addService("http", "tcp", 80);
      mdnsStarted = true;
      Serial.print(F("Dashboard: http://"));
      Serial.println(WiFi.localIP());
    }
    if (mdnsStarted) MDNS.update();
  }
}

// ─────────────────────── Setup / loop ───────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUZZER, OUTPUT);
  btnA.begin();
  btnB.begin();
  loadSettings();
  for (auto& s : sens) resetStats(s);
  for (auto& row : history) for (auto& v : row) v = HIST_NONE;

  u8g2.setBusClock(400000);
  u8g2.begin();
  u8g2.setFontMode(1);  // transparent fonts -> inverted text on highlight boxes
  applyContrast(settings.contrast);

  u8g2.clearBuffer();
  drawCentered("RJ45 BOARD", 30, u8g2_font_10x20_tr);
  drawCentered("v" FW_VERSION "  starting...", 46, u8g2_font_5x7_tr);
  u8g2.sendBuffer();

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.hostname(MDNS_NAME);
#ifdef USE_STATIC_IP
  WiFi.config(IPAddress(STATIC_IP), IPAddress(STATIC_GW), IPAddress(STATIC_MASK), IPAddress(STATIC_DNS));
#endif
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  configTime(TZ_INFO, NTP_1, NTP_2);  // syncs in the background, DST automatic
  setupWeb();

  scanBus();
  if (sht31.begin(0x44))                     ambType = AMB_SHT31;
  else if (bme.begin(0x76) || bme.begin(0x77)) ambType = AMB_BME280;

  const uint32_t now = millis();
  lastBusScan     = now;
  lastTempRequest = now - TEMP_INTERVAL_MS;  // first reading right away
  lastActivity    = now;

  playStartupSound();
}

void loop() {
  const uint32_t now = millis();
  updateTemps(now);
  handleButtons(now);
  updateNetwork();
  updateBuzzer(now);
  updateUiTimers(now);

  if (!displayAsleep && now - lastFrame >= FRAME_MS) {
    lastFrame = now;
    render(now);
  }
}
