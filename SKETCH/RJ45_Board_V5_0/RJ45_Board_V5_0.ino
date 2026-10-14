/*
  ESP8266 RJ45 Sensor Board — plug & play sensors
  ===========================================================================
  V5.0
  - Plug & play: every sensor is detected on its own and gets a "slot"
    (up to 8). No sketch changes, no recompiling. New sensors show up on the
    OLED and the web page within 30 s, or right after a restart.
      1-Wire  DS18B20 ................ RJ1-RJ5 pin 2 (IO0), any number of them
      I2C     SHT30/31/35, SHT40/41/45 (0x44 / 0x45), AHT20/21 (0x38),
              BME280 / BMP280 (0x76 / 0x77) ... RJ3, RJ4 or the I2C header
      SPI     MAX6675 thermocouple ... RJ5 or the SPI header (SPI port mode)
  - Sensor drivers live in src/sensors/ (one small header per chip, no
    Adafruit libraries needed any more). Adding a chip = one new header.
  - Each sensor is bound to its identity in flash (DS18B20: ROM address,
    I2C: chip type + address), so its slot, label, offset and alarm stay put
    when other sensors come and go. A missing sensor stays listed (LOST)
    until you "Forget" it on the web page.
  - Humidity, dew point and pressure shown when the sensor has them, and
    written to the long-term log + CSV
  - Per-sensor calibration offset (web page), RH recomputed to match
  - SPI port mode (web page -> Device): IO13/IO14 are shared between
    button B + buzzer and the SPI port. "Buttons" = normal; "SPI" = MAX6675
    on RJ5 / SPI header, button A cycles the screens, setup via the web page.
  - Web: Device panel (sound, key beep, screen off, contrast, SPI port),
    I2C bus scan in the footer, per-kind alarm ranges
  - Carried over from Stackable V4.10: robust DS18B20 handling (search misses
    never drop a sensor), dropouts counter, 10-min flash log (LittleFS) with
    CSV export, 12 h RAM history, alarms, NTP + DST, mDNS, JSON API
  V4.2 and earlier: see RJ45_Board_A_B_ButtonsV4_2.ino

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
  SPI mode    only A: tap next screen, hold previous, any press acks an alarm

  Libraries: U8g2, OneWire, DallasTemperature   (everything else is built in)
  Board:     Generic ESP8266 Module, Flash Size "4MB (FS:2MB OTA:~1019KB)"
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
#include <LittleFS.h>

#include "src/sensors/Sensors.h"  // sensor drivers + auto-detection
#include "secrets.h"              // WIFI_SSID / WIFI_PASS / static IP / MDNS_NAME
#include "graphic.h"              // check_mark_bits (64x64 XBM)
#include "webpage.h"              // INDEX_HTML (dashboard)

#define FW_VERSION "5.0"

// ═════════════════════════ BOARD CONFIG ═════════════════════════

// OLED: the board's 1.3" display is an SH1106. Set to 0 for a 0.96" SSD1306.
#define OLED_SH1106 1
constexpr uint8_t  OLED_ADDR   = 0x3C;
// The OLED shares SDA/SCL with RJ3 / RJ4. If long I2C cables make the screen
// glitch, lower this to 100000.
constexpr uint32_t OLED_I2C_HZ = 400000;

constexpr uint8_t PIN_SDA     = 4;   // IO4  - I2C header, RJ3/RJ4 pin 6
constexpr uint8_t PIN_SCL     = 5;   // IO5  - I2C header, RJ3/RJ4 pin 5
constexpr uint8_t PIN_ONEWIRE = 0;   // IO0  - RJ1..RJ5 pin 2, 4.7k pull-up per port
constexpr uint8_t PIN_BTN_A   = 12;  // IO12 - button A (jumper U7)
constexpr uint8_t PIN_BTN_B   = 14;  // IO14 - button B (jumper U8)  = SPI SCK
constexpr uint8_t PIN_BUZZER  = 13;  // IO13 - buzzer   (jumper U9)  = SPI data
// SPI port (RJ5 pins 5/6/7 and the SPI header). MAX6675: SCK, SO, CS.
constexpr uint8_t PIN_P_SCK = 14;  // RJ5 pin 5
constexpr uint8_t PIN_P_SO  = 13;  // RJ5 pin 6
constexpr uint8_t PIN_P_CS  = 15;  // RJ5 pin 7 (10k pull-down R6, fine for CS)

// ═════════════════════════ Tunables ═════════════════════════
constexpr uint8_t NUM_CH    = 8;   // sensor slots
constexpr uint8_t LABEL_LEN = 12;  // 11 characters + terminator

constexpr uint32_t TEMP_INTERVAL_MS   = 2000;   // new reading every 2 s
constexpr uint32_t BUS_RESCAN_MS      = 30000;  // look for new / returning sensors
constexpr uint32_t TREND_SAMPLE_MS    = 10000;  // 6 samples x 10 s = 1 min trend
constexpr uint8_t  TREND_SAMPLES      = 6;
constexpr float    TREND_THRESHOLD    = 0.2f;   // °C per minute to show an arrow
constexpr uint32_t FRAME_MS           = 100;
constexpr uint32_t OVERVIEW_PAGE_MS   = 3000;
constexpr uint32_t EDIT_TIMEOUT_MS    = 60000;
constexpr uint32_t SAVED_SPLASH_MS    = 900;
constexpr uint32_t RESET_MSG_MS       = 1200;
constexpr uint32_t DEBOUNCE_MS        = 30;
constexpr uint32_t LONG_PRESS_MS      = 600;
constexpr uint32_t REPEAT_MS          = 150;
constexpr uint16_t FAST_AFTER_REPEATS = 12;     // hold ~2 s -> 2.0 °C steps
constexpr float    ALARM_HYSTERESIS   = 0.5f;
constexpr float    ALARM_MIN_GAP      = 2.0f;   // HIGH must be >= LOW + 2 °C
constexpr uint8_t  READ_RETRIES       = 3;      // DS18B20 re-reads per sample
constexpr uint8_t  SENSOR_FAIL_LIMIT  = 3;      // consecutive bad samples before "err"
constexpr int16_t  OFFSET_MAX_TENTHS  = 200;    // calibration offset limit, ±20.0 °C

const char* const TZ_INFO = "EET-2EEST,M3.5.0/3,M10.5.0/4";  // Romania, DST auto
const char* const NTP_1   = "ro.pool.ntp.org";
const char* const NTP_2   = "pool.ntp.org";

constexpr uint16_t HISTORY_LEN         = 360;     // 360 x 2 min = 12 h
constexpr uint32_t HISTORY_INTERVAL_MS = 120000;
constexpr int16_t  HIST_NONE           = INT16_MIN;

const uint8_t SCREEN_OFF_MIN[] = { 0, 1, 5, 15, 30 };  // 0 = never
constexpr uint8_t SCREEN_OFF_COUNT = sizeof(SCREEN_OFF_MIN);

// ─────────────────────── Hardware objects ───────────────────────
#if OLED_SH1106
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_SCL, PIN_SDA);
#else
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_SCL, PIN_SDA);
#endif
OneWire oneWire(PIN_ONEWIRE);
DallasTemperature ds(&oneWire);
MAX6675 thermo(PIN_P_SCK, PIN_P_SO, PIN_P_CS);
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

// One slot = one physical sensor. kind + id say WHICH sensor it is.
struct SlotCfg {
  uint8_t  kind;              // SensorKind, SK_NONE = free slot
  uint8_t  id[8];             // DS18B20: ROM / I2C: [0] = address / MAX6675: zeros
  char     label[LABEL_LEN];  // "" = default name
  int16_t  offset;            // calibration, tenths of °C
  AlarmCfg alarm;
};

struct Settings {
  uint32_t magic;
  SlotCfg  slot[NUM_CH];
  uint8_t  keyBeep;
  uint8_t  alarmSound;
  uint8_t  screenOffIdx;
  uint8_t  contrast;  // 1..8
  uint8_t  spiMode;   // 0 = button B + buzzer on IO13/14, 1 = SPI port (MAX6675)
};
constexpr uint32_t SETTINGS_MAGIC = 0x524A3501;  // "RJ5" v1 layout

Settings settings;  // active
Settings edit;      // working copy while an edit screen is open
bool     settingsDirty = false;

// ─────────────────────── Sensor + alarm state ───────────────────────
enum AlarmKind : uint8_t { AL_NONE, AL_HIGH, AL_LOW, AL_LOST };

struct AlarmState {
  AlarmKind kind  = AL_NONE;
  bool      acked = false;
};

struct SensorState {
  bool  present = false;
  bool  valid   = false;
  bool  tcOpen  = false;  // MAX6675: chip answers, thermocouple open
  float temp = NAN, minT = NAN, maxT = NAN;
  float hum  = NAN;       // %RH, when the sensor has it
  float pres = NAN;       // hPa, when the sensor has it
  float hist[TREND_SAMPLES];
  uint8_t  failStreak = 0;
  uint32_t reads = 0, errors = 0;
  uint32_t drops = 0;     // times the sensor went from valid to invalid (LOST)
  float    lastBad = NAN; // raw value of the last bad sample
};

SensorState sens[NUM_CH];
AlarmState  alarms[NUM_CH];
I2cSensor*  drv[NUM_CH]     = {};  // I2C driver attached to each slot
bool        pending[NUM_CH] = {};  // I2C measurement triggered, result due
uint32_t    dsSearchMiss[NUM_CH] = {};
char        chNameBuf[NUM_CH][LABEL_LEN];  // label or short default name
char        defNameBuf[NUM_CH][24];        // long default name (web)

bool    dsConvPending = false;
bool    dsParasite    = false;
uint8_t dsOnBus       = 0;

constexpr uint8_t I2C_SCAN_MAX = 16;
uint8_t i2cSeen[I2C_SCAN_MAX];  // addresses that answered at the last scan
uint8_t i2cSeenN = 0;

// ─────────────────────── UI state ───────────────────────
constexpr uint8_t M_OVERVIEW = 0;
constexpr uint8_t M_SENS0    = 1;
constexpr uint8_t M_ALARMS   = M_SENS0 + NUM_CH;
constexpr uint8_t M_SETTINGS = M_ALARMS + 1;
constexpr uint8_t M_INFO     = M_SETTINGS + 1;
constexpr uint8_t M_COUNT    = M_INFO + 1;

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
uint32_t restartAt     = 0;      // web asked for a restart (SPI port mode change)

// timing
uint32_t lastTempRequest = 0, lastDsScan = 0, lastI2cScan = 0, lastTrendSample = 0;
bool     firstReadDone = false;
uint8_t  histPos = 0;
uint32_t nextAlarmBeep = 0;
uint8_t  alarmBeepStep = 0;

// history for the web chart: temps in tenths of a degree (int16 = 2 bytes)
int16_t  history[NUM_CH][HISTORY_LEN];
uint16_t histHead = 0, histCount = 0;
uint32_t lastHistory = 0;
bool     mdnsStarted = false;

// ─────────────────────── Long-term log (flash) ───────────────────────
// Two append-only files: when the current one is full, the older one is
// deleted and reused. Append-only suits LittleFS: wear is spread over the
// whole filesystem, 144 writes/day = decades of flash life.
constexpr uint32_t LOG_INTERVAL_S = 600;            // one record per 10 min
constexpr uint32_t LOG_FILE_RECS  = 7UL * 24 * 6;   // 7 days per file -> 7..14 days kept
constexpr uint32_t LOG_MAGIC      = 0x32474C52;     // "RLG2"
const char* const  LOG_FILES[2]   = { "/log0.bin", "/log1.bin" };

struct __attribute__((packed)) LogHdr {
  uint32_t magic;
  uint8_t  nch;
  uint8_t  recSize;
  uint16_t pad;
};

struct __attribute__((packed)) LogRec {
  uint32_t t;           // UTC epoch at the END of the 10-min interval
  int16_t  v[NUM_CH];   // temperature average, tenths of °C, HIST_NONE = no data
  int16_t  h[NUM_CH];   // humidity average, tenths of %RH
  int16_t  p[NUM_CH];   // pressure average, tenths of hPa
};

enum LogQty : uint8_t { LQ_T, LQ_H, LQ_P, LQ_N };

bool     logOk       = false;
uint8_t  logCur      = 0;              // file being appended
uint32_t logFileRecs = LOG_FILE_RECS;  // reduced automatically on a small filesystem
uint32_t logRecs[2]  = { 0, 0 };
uint32_t logOldest   = 0, logNewest = 0;
uint32_t logSlot     = 0;              // current 10-min slot (epoch / 600), 0 = no time yet
float    logSum[LQ_N][NUM_CH];
uint16_t logN[LQ_N][NUM_CH];

// ─────────────────────── Helpers ───────────────────────
uint8_t slotKind(uint8_t ch) { return settings.slot[ch].kind; }
bool slotBound(uint8_t ch)   { return slotKind(ch) != SK_NONE; }
bool isDs(uint8_t ch)        { return slotKind(ch) == SK_DS18B20; }
bool isTc(uint8_t ch)        { return slotKind(ch) == SK_MAX6675; }
bool isI2cKind(uint8_t k)    { return k == SK_SHT3X || k == SK_SHT4X || k == SK_AHT2X || k == SK_BME280 || k == SK_BMP280; }
bool isI2c(uint8_t ch)       { return isI2cKind(slotKind(ch)); }
const KindInfo& kindOf(uint8_t ch) { return kindInfo(slotKind(ch)); }
bool spiMode()               { return settings.spiMode != 0; }

// Channel shown on OLED / web = slot bound to a sensor
bool chVisible(uint8_t ch) { return ch < NUM_CH && slotBound(ch); }

const char* chName(uint8_t ch) { return chNameBuf[ch]; }

void fmtRom(char* buf, size_t n, const uint8_t* rom) {
  if (n < 17) { if (n) buf[0] = 0; return; }
  for (uint8_t k = 0; k < 8; k++) snprintf(buf + k * 2, 3, "%02X", rom[k]);
}

// Where the sensor is connected, for the web card / OLED
void sourceText(uint8_t ch, char* buf, size_t n) {
  const SlotCfg& c = settings.slot[ch];
  if (isDs(ch))       fmtRom(buf, n, c.id);
  else if (isI2c(ch)) snprintf(buf, n, "I2C 0x%02X", c.id[0]);
  else if (isTc(ch))  strlcpy(buf, "SPI RJ5", n);
  else if (n)         buf[0] = 0;
}

// Default names: "DS #2", "SHT3x", "BME280" (or "SHT3x 45" when there are two)
void refreshNames() {
  uint8_t total[SK_COUNT] = {}, ord[SK_COUNT] = {};
  for (uint8_t ch = 0; ch < NUM_CH; ch++) total[slotKind(ch)]++;
  for (uint8_t ch = 0; ch < NUM_CH; ch++) {
    const uint8_t k = slotKind(ch);
    const KindInfo& ki = kindInfo(k);
    const uint8_t n = ++ord[k];
    char shortName[LABEL_LEN];
    if (k == SK_NONE) {
      snprintf(shortName, sizeof shortName, "Slot %u", ch + 1);
      strlcpy(defNameBuf[ch], shortName, sizeof defNameBuf[ch]);
    } else if (k == SK_DS18B20) {
      snprintf(shortName, sizeof shortName, "DS #%u", n);
      snprintf(defNameBuf[ch], sizeof defNameBuf[ch], "DS18B20 #%u", n);
    } else if (isI2cKind(k)) {
      const uint8_t a = settings.slot[ch].id[0];
      if (total[k] > 1) snprintf(shortName, sizeof shortName, "%s %02X", ki.name, a);
      else              strlcpy(shortName, ki.name, sizeof shortName);
      snprintf(defNameBuf[ch], sizeof defNameBuf[ch], "%s @0x%02X", ki.name, a);
    } else {
      strlcpy(shortName, ki.name, sizeof shortName);
      strlcpy(defNameBuf[ch], "MAX6675", sizeof defNameBuf[ch]);
    }
    if (settings.slot[ch].label[0]) strlcpy(chNameBuf[ch], settings.slot[ch].label, LABEL_LEN);
    else                            strlcpy(chNameBuf[ch], shortName, LABEL_LEN);
  }
}

bool isSensorItem(uint8_t i) { return i >= M_SENS0 && i < M_ALARMS; }
uint8_t channelOf(uint8_t menuItem) { return menuItem - M_SENS0; }
bool menuVisible(uint8_t i) {
  if (isSensorItem(i)) return chVisible(channelOf(i));
  if (spiMode() && (i == M_ALARMS || i == M_SETTINGS)) return false;  // no button B to edit
  return true;
}
bool isViewItem(uint8_t i) { return i != M_ALARMS && i != M_SETTINGS && menuVisible(i); }

const char* menuLabel(uint8_t i) {
  if (isSensorItem(i)) return chName(channelOf(i));
  switch (i) {
    case M_OVERVIEW: return "Overview";
    case M_ALARMS:   return "Alarms";
    case M_SETTINGS: return "Settings";
    default:         return "Info";
  }
}

uint8_t stepMenu(uint8_t i, int8_t dir, bool viewsOnly) {
  for (uint8_t n = 0; n < M_COUNT; n++) {
    i = (uint8_t)((i + M_COUNT + dir) % M_COUNT);
    if (viewsOnly ? isViewItem(i) : menuVisible(i)) return i;
  }
  return i;
}

uint8_t nextVisibleChannel(uint8_t ch, int8_t dir) {
  for (uint8_t n = 0; n < NUM_CH; n++) {
    ch = (uint8_t)((ch + NUM_CH + dir) % NUM_CH);
    if (chVisible(ch)) return ch;
  }
  return ch;
}

uint8_t visibleCount() {
  uint8_t n = 0;
  for (uint8_t ch = 0; ch < NUM_CH; ch++) n += chVisible(ch);
  return n;
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

const char* wifiStatusText() {
  switch (WiFi.status()) {
    case WL_CONNECTED:      return "connected";
    case WL_NO_SSID_AVAIL:  return "SSID not found";
    case WL_WRONG_PASSWORD: return "wrong password";
    case WL_CONNECT_FAILED: return "connect failed";
    case WL_CONNECTION_LOST:return "connection lost";
    default:                return "connecting...";
  }
}

// Magnus formula, good to ~0.35 °C for -45..60 °C
float dewPoint(float t, float rh) {
  if (isnan(t) || isnan(rh) || rh <= 0.0f) return NAN;
  const float a = 17.62f, b = 243.12f;
  const float g = logf(rh / 100.0f) + a * t / (b + t);
  return b * g / (a - g);
}

// After a temperature offset, RH is recomputed at the corrected temperature
// (the amount of water in the air, i.e. the dew point, doesn't change).
float compensateRH(float tMeas, float rh, float tTrue) {
  if (isnan(rh) || tMeas == tTrue) return rh;
  const float dp = dewPoint(tMeas, rh);
  if (isnan(dp)) return rh;
  const float a = 17.62f, b = 243.12f;
  const float r = 100.0f * expf(a * dp / (b + dp) - a * tTrue / (b + tTrue));
  return constrain(r, 0.0f, 100.0f);
}

// Labels: printable ASCII the OLED fonts can draw, safe inside JSON / HTML
bool labelCharOk(char c) {
  return c && (isalnum((unsigned char)c) || strchr(" _-.#/()+:", c));
}

void cleanLabel(const char* in, char* out) {
  uint8_t n = 0;
  while (*in == ' ') in++;
  for (; *in && n < LABEL_LEN - 1; in++) if (labelCharOk(*in)) out[n++] = *in;
  while (n && out[n - 1] == ' ') n--;
  out[n] = 0;
}

// ─────────────────────── Sound ───────────────────────
// In SPI port mode IO13 carries the MAX6675 data: the buzzer stays silent.
void beep(uint16_t f, uint32_t ms) {
  if (!spiMode()) tone(PIN_BUZZER, f, ms);
}

void click()       { if (settings.keyBeep) beep(2300, 30); }
void confirmBeep() { if (settings.keyBeep) beep(1800, 90); }

void playStartupSound() {
  if (spiMode()) return;
  beep(750, 100);
  delay(200);
  beep(750, 100);
}

// ─────────────────────── Settings load/save ───────────────────────
void defaultAlarm(AlarmCfg& a, uint8_t kind) {
  const KindInfo& k = kindInfo(kind);
  a.enabled = 0;
  a.low  = constrain(5.0f, k.tMin, k.tMax);
  a.high = constrain(35.0f, a.low + ALARM_MIN_GAP, k.tMax);
}

void defaultSettings(Settings& s) {
  memset(&s, 0, sizeof(s));
  s.magic = SETTINGS_MAGIC;
  for (auto& c : s.slot) defaultAlarm(c.alarm, SK_DS18B20);
  s.keyBeep      = 1;
  s.alarmSound   = 1;
  s.screenOffIdx = 2;  // 5 min
  s.contrast     = 8;
}

void sanitizeSettings(Settings& s) {
  for (auto& c : s.slot) {
    if (c.kind >= SK_COUNT) { memset(&c, 0, sizeof c); defaultAlarm(c.alarm, SK_DS18B20); }
    const KindInfo& k = kindInfo(c.kind == SK_NONE ? (uint8_t)SK_DS18B20 : c.kind);
    AlarmCfg& a = c.alarm;
    a.enabled = a.enabled ? 1 : 0;
    if (isnan(a.low) || isnan(a.high) || a.low < k.tMin || a.high > k.tMax || a.low > a.high - ALARM_MIN_GAP)
      defaultAlarm(a, c.kind == SK_NONE ? (uint8_t)SK_DS18B20 : c.kind);
    char tmp[LABEL_LEN];
    c.label[LABEL_LEN - 1] = 0;
    cleanLabel(c.label, tmp);
    strlcpy(c.label, tmp, LABEL_LEN);
    c.offset = constrain(c.offset, -OFFSET_MAX_TENTHS, OFFSET_MAX_TENTHS);
  }
  s.keyBeep    = s.keyBeep ? 1 : 0;
  s.alarmSound = s.alarmSound ? 1 : 0;
  s.spiMode    = s.spiMode ? 1 : 0;
  if (s.screenOffIdx >= SCREEN_OFF_COUNT) s.screenOffIdx = 2;
  if (s.contrast < 1 || s.contrast > 8) s.contrast = 8;
}

void loadSettings() {
  EEPROM.begin(sizeof(Settings));
  EEPROM.get(0, settings);
  if (settings.magic != SETTINGS_MAGIC) defaultSettings(settings);
  sanitizeSettings(settings);
  refreshNames();
}

void saveSettings() {
  EEPROM.put(0, settings);
  EEPROM.commit();
  settingsDirty = false;
}

void flushSettings() {
  if (!settingsDirty) return;
  saveSettings();
  refreshNames();
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
  for (uint8_t i = 0; i < NUM_CH; i++)
    if (alarms[i].kind != AL_NONE && !alarms[i].acked) return (int8_t)i;
  return -1;
}

bool anyUnacked() { return firstUnacked() >= 0; }

void ackAlarms() {
  for (auto& a : alarms) if (a.kind != AL_NONE) a.acked = true;
  if (!spiMode()) noTone(PIN_BUZZER);
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
  for (uint8_t i = 0; i < NUM_CH; i++) {
    const AlarmCfg&    c = settings.slot[i].alarm;
    const SensorState& s = sens[i];
    AlarmState&        a = alarms[i];

    if (!c.enabled || !chVisible(i)) { a.kind = AL_NONE; a.acked = false; continue; }
    if ((s.present || isDs(i)) && s.reads == 0) continue;  // first reading not in yet

    AlarmKind cond = !s.valid         ? AL_LOST
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
  if (spiMode() || !settings.alarmSound || !anyUnacked()) return;
  static const uint16_t gaps[3] = { 200, 200, 1400 };  // beep-beep-beep ... pause
  if ((int32_t)(now - nextAlarmBeep) >= 0) {
    tone(PIN_BUZZER, 2800, 120);
    nextAlarmBeep = now + gaps[alarmBeepStep];
    alarmBeepStep = (alarmBeepStep + 1) % 3;
  }
}

// ─────────────────────── Slots ───────────────────────
void resetMinMax(SensorState& s) { s.minT = s.maxT = NAN; }

void resetStats(SensorState& s) {
  resetMinMax(s);
  for (auto& h : s.hist) h = NAN;
}

void resetChannel(uint8_t ch) {
  SensorState& s = sens[ch];
  s = SensorState();
  resetStats(s);
  alarms[ch] = AlarmState();
  drv[ch] = nullptr;
  pending[ch] = false;
  dsSearchMiss[ch] = 0;
}

int8_t findSlot(uint8_t kind, const uint8_t* id) {
  for (uint8_t s = 0; s < NUM_CH; s++)
    if (settings.slot[s].kind == kind && memcmp(settings.slot[s].id, id, 8) == 0) return (int8_t)s;
  return -1;
}

// Slot of this sensor; a new sensor takes a free slot (saved by flushSettings)
int8_t bindSlot(uint8_t kind, const uint8_t* id, bool* isNew = nullptr) {
  int8_t s = findSlot(kind, id);
  if (isNew) *isNew = false;
  if (s >= 0) return s;
  for (uint8_t f = 0; f < NUM_CH; f++) {
    if (slotBound(f)) continue;
    SlotCfg& c = settings.slot[f];
    memset(&c, 0, sizeof c);
    c.kind = kind;
    memcpy(c.id, id, 8);
    defaultAlarm(c.alarm, kind);
    resetChannel(f);
    for (auto& v : history[f]) v = HIST_NONE;
    settingsDirty = true;
    if (isNew) *isNew = true;
    return (int8_t)f;
  }
  return -1;
}

void forgetSlot(uint8_t ch) {
  memset(&settings.slot[ch], 0, sizeof settings.slot[ch]);
  defaultAlarm(settings.slot[ch].alarm, SK_DS18B20);
  resetChannel(ch);
  for (auto& v : history[ch]) v = HIST_NONE;
  saveSettings();
  refreshNames();
}

// Feeds one sample into a channel. tRaw is the sensor's own value, the
// calibration offset is added here. Keeps the last good value until
// SENSOR_FAIL_LIMIT consecutive failures. Returns true on a good sample.
bool sampleChannel(uint8_t ch, float tRaw, float badDiag) {
  SensorState& s = sens[ch];
  const KindInfo& k = kindOf(ch);
  s.reads++;
  if (isnan(tRaw) || tRaw < k.tMin - 0.5f || tRaw > k.tMax + 0.5f) {
    s.errors++;
    s.lastBad = badDiag;
    if (s.failStreak < 255) s.failStreak++;
    if (s.failStreak <= SENSOR_FAIL_LIMIT)
      Serial.printf("%s bad read (diag %.1f, streak %u, %lu/%lu errors)\n",
                    chName(ch), badDiag, s.failStreak,
                    (unsigned long)s.errors, (unsigned long)s.reads);
    if (s.failStreak >= SENSOR_FAIL_LIMIT) {
      if (s.valid) {
        s.drops++;
        Serial.printf("%s LOST (dropout #%lu)\n", chName(ch), (unsigned long)s.drops);
      }
      s.valid = false;
      s.hum = s.pres = NAN;
    }
    return false;
  }
  const float t = tRaw + settings.slot[ch].offset / 10.0f;
  s.failStreak = 0;
  s.valid = true;
  s.temp = t;
  s.minT = isnan(s.minT) ? t : fminf(s.minT, t);
  s.maxT = isnan(s.maxT) ? t : fmaxf(s.maxT, t);
  return true;
}

// ─────────────────────── DS18B20 (1-Wire) ───────────────────────
// Finds every DS18B20 on the bus. Known ROMs keep their slot (label + alarm),
// new ROMs take a free slot and are saved to flash.
void scanDs() {
  ds.begin();
  ds.setWaitForConversion(false);
  dsParasite = ds.isParasitePowerMode();
  dsOnBus = ds.getDeviceCount();

  bool seen[NUM_CH] = {};
  DeviceAddress a;
  for (uint8_t i = 0; i < dsOnBus; i++) {
    if (!ds.getAddress(a, i)) continue;
    char r[17];
    fmtRom(r, sizeof r, a);
    bool isNew;
    const int8_t slot = bindSlot(SK_DS18B20, a, &isNew);
    if (slot < 0) {
      Serial.printf("DS18B20 %s ignored: all %u slots used (Forget one on the web page)\n", r, NUM_CH);
      continue;
    }
    if (isNew) Serial.printf("New DS18B20 %s -> slot %u\n", r, slot + 1);
    seen[slot] = true;
  }
  for (uint8_t s = 0; s < NUM_CH; s++) {
    if (!isDs(s)) continue;
    SensorState& st = sens[s];
    // A search can glitch (WiFi interrupts, long cable) while reading the
    // sensor by its ROM still works, so a missed search never drops a known
    // sensor. The reads decide: 3 failed in a row = LOST.
    if (seen[s]) {
      st.present = true;
    } else if (st.present) {
      dsSearchMiss[s]++;
      Serial.printf("%s: bus search missed it (%lu times) - still read by ROM\n",
                    chName(s), (unsigned long)dsSearchMiss[s]);
    }
  }
  flushSettings();
}

bool dsPlausible(float t, uint8_t ch) {
  if (t == DEVICE_DISCONNECTED_C || t < -55.5f || t > 125.5f) return false;
  // 85.0 is the DS18B20 power-on value: reject it only if it's a sudden jump
  const float prev = sens[ch].temp - settings.slot[ch].offset / 10.0f;
  if (t == 85.0f && (isnan(prev) || fabsf(prev - 85.0f) > 5.0f)) return false;
  return true;
}

float readDs(uint8_t ch) {
  float t = DEVICE_DISCONNECTED_C;
  for (uint8_t k = 0; k < READ_RETRIES; k++) {
    t = ds.getTempC(settings.slot[ch].id);  // re-reads the scratchpad, checks CRC
    if (dsPlausible(t, ch)) break;
    delayMicroseconds(500);
  }
  return t;
}

void updateDs(uint32_t now) {
  // read the conversion started on the previous tick (2 s ago, needs 750 ms),
  // rescan every 30 s, then start the next conversion for all sensors at once
  bool anyDs = false;
  if (dsConvPending) {
    for (uint8_t s = 0; s < NUM_CH; s++) {
      if (!isDs(s)) continue;  // every known sensor is read by ROM, found by the search or not
      SensorState& st = sens[s];
      const float t = readDs(s);
      if (sampleChannel(s, dsPlausible(t, s) ? t : NAN, t)) st.present = true;
      else if (st.failStreak >= SENSOR_FAIL_LIMIT)          st.present = false;
    }
    dsConvPending = false;
  }
  if (now - lastDsScan >= BUS_RESCAN_MS) { lastDsScan = now; scanDs(); }
  for (uint8_t s = 0; s < NUM_CH; s++) anyDs |= isDs(s);
  if (anyDs) {
    ds.requestTemperatures();  // async, returns immediately
    dsConvPending = true;
  }
}

// ─────────────────────── I2C sensors ───────────────────────
bool i2cAddrInUse(uint8_t addr) {
  for (uint8_t s = 0; s < NUM_CH; s++)
    if (isI2c(s) && settings.slot[s].id[0] == addr && sens[s].present && drv[s]) return true;
  return false;
}

// Pings the whole bus (diagnostics), then identifies every supported chip that
// isn't attached yet. Known chips keep their slot; new ones get a free slot.
void scanI2c() {
  i2cSeenN = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    if (i2c::ping(a) && i2cSeenN < I2C_SCAN_MAX) i2cSeen[i2cSeenN++] = a;
    yield();
  }
  for (uint8_t a : sensors::I2C_CANDIDATES) {
    bool there = false;
    for (uint8_t k = 0; k < i2cSeenN; k++) there |= i2cSeen[k] == a;
    if (!there || i2cAddrInUse(a)) continue;

    I2cSensor* d = sensors::identifyI2c(a);
    if (!d) { Serial.printf("I2C 0x%02X answers but isn't a supported sensor\n", a); continue; }
    const uint8_t id[8] = { a };
    bool isNew;
    const int8_t s = bindSlot(d->kind(), id, &isNew);
    if (s < 0) {
      Serial.printf("%s at 0x%02X ignored: all %u slots used (Forget one on the web page)\n",
                    kindInfo(d->kind()).model, a, NUM_CH);
      continue;
    }
    drv[s] = d;
    sens[s].present = true;
    sens[s].failStreak = 0;
    pending[s] = d->trigger();
    Serial.printf("%s %s at 0x%02X -> slot %u\n", isNew ? "New" : "Back:", kindInfo(d->kind()).model, a, s + 1);
  }
  flushSettings();
}

void updateI2c() {
  for (uint8_t s = 0; s < NUM_CH; s++) {
    if (!isI2c(s)) continue;
    SensorState& st = sens[s];
    if (!drv[s] || !st.present) { st.valid = false; st.hum = st.pres = NAN; continue; }

    Reading r;
    const bool ok = pending[s] && drv[s]->fetch(r);
    if (sampleChannel(s, ok ? r.t : NAN, r.diag)) {
      st.hum  = compensateRH(r.t, r.h, st.temp);
      st.pres = r.p;
    } else if (st.failStreak >= SENSOR_FAIL_LIMIT) {
      st.present = false;
      pending[s] = false;
      Serial.printf("%s not answering - looking for it every 30 s\n", chName(s));
      continue;
    }
    pending[s] = drv[s]->trigger();  // result is read on the next tick
  }
}

// ─────────────────────── MAX6675 (SPI port mode) ───────────────────────
void updateThermo() {
  if (!spiMode()) return;
  Reading r;
  const MAX6675::Status st = thermo.read(r);
  static const uint8_t ZERO[8] = {};
  int8_t s = findSlot(SK_MAX6675, ZERO);

  if (st == MAX6675::NO_CHIP) {
    if (s >= 0 && sens[s].present) {
      sampleChannel(s, NAN, r.diag);
      if (sens[s].failStreak >= SENSOR_FAIL_LIMIT) sens[s].present = false;
    }
    return;
  }
  if (s < 0) {
    // an all-zero word is also what a line held LOW gives (jumper U9 left on:
    // the buzzer pulls IO13 down), so a NEW thermocouple must read non-zero
    if (r.diag == 0) {
      static bool warned = false;
      if (!warned) { warned = true; Serial.println(F("SPI port: data line stuck LOW - is jumper U9 (buzzer) removed?")); }
      return;
    }
    s = bindSlot(SK_MAX6675, ZERO);
    if (s < 0) return;
    Serial.printf("New MAX6675 on the SPI port -> slot %u\n", s + 1);
    flushSettings();
  }
  SensorState& ss = sens[s];
  ss.present = true;
  ss.tcOpen  = st == MAX6675::OPEN_TC;
  sampleChannel(s, st == MAX6675::OK ? r.t : NAN, r.diag);
}

// ─────────────────────── Trend + Serial ───────────────────────
float trendOf(const SensorState& s) {
  const float oldest = s.hist[histPos];
  return (s.valid && !isnan(oldest)) ? s.temp - oldest : 0.0f;
}

void logReadings() {
  char t[8];
  for (uint8_t ch = 0; ch < NUM_CH; ch++) {
    if (!chVisible(ch)) continue;
    fmtTemp(t, sizeof t, sens[ch].valid ? sens[ch].temp : NAN);
    Serial.printf("%s %s C", chName(ch), t);
    if (!isnan(sens[ch].hum))  Serial.printf(" %.1f %%RH", sens[ch].hum);
    if (!isnan(sens[ch].pres)) Serial.printf(" %.1f hPa", sens[ch].pres);
    Serial.print(F(" | "));
  }
  Serial.println();
}

int16_t toTenths(float t) { return isnan(t) ? HIST_NONE : (int16_t)lroundf(t * 10.0f); }

// ─────────────────────── Long-term log ───────────────────────
void logResetAcc() {
  memset(logSum, 0, sizeof logSum);
  memset(logN, 0, sizeof logN);
}

// Records in file i. Invalid files (other layout) are deleted, a torn last
// record (power cut during a write) is cut off.
uint32_t logFileCount(uint8_t i) {
  if (!LittleFS.exists(LOG_FILES[i])) return 0;
  File f = LittleFS.open(LOG_FILES[i], "r");
  if (!f) return 0;
  LogHdr h;
  const bool ok = f.read((uint8_t*)&h, sizeof h) == sizeof h && h.magic == LOG_MAGIC &&
                  h.nch == NUM_CH && h.recSize == sizeof(LogRec);
  const size_t sz = f.size();
  f.close();
  if (!ok) { LittleFS.remove(LOG_FILES[i]); return 0; }
  const uint32_t n = (sz - sizeof(LogHdr)) / sizeof(LogRec);
  if ((sz - sizeof(LogHdr)) % sizeof(LogRec)) {
    File w = LittleFS.open(LOG_FILES[i], "r+");
    if (w) { w.truncate(sizeof(LogHdr) + n * sizeof(LogRec)); w.close(); }
  }
  return n;
}

bool logReadRec(uint8_t i, uint32_t idx, LogRec& r) {
  File f = LittleFS.open(LOG_FILES[i], "r");
  if (!f) return false;
  const bool ok = f.seek(sizeof(LogHdr) + idx * sizeof(LogRec)) &&
                  f.read((uint8_t*)&r, sizeof r) == sizeof r;
  f.close();
  return ok;
}

void logRefreshBounds() {
  const uint8_t old = logCur ^ 1;
  LogRec r;
  logOldest = logNewest = 0;
  if (logRecs[old] && logReadRec(old, 0, r))                         logOldest = r.t;
  else if (logRecs[logCur] && logReadRec(logCur, 0, r))              logOldest = r.t;
  if (logRecs[logCur] && logReadRec(logCur, logRecs[logCur] - 1, r)) logNewest = r.t;
}

void startLog() {
  logResetAcc();
  if (!LittleFS.begin()) {
    Serial.println(F("Log: no filesystem - pick a Flash Size with FS in Tools (log disabled)"));
    return;
  }
  FSInfo fi;
  LittleFS.info(fi);
  // both files together use at most ~40% of the filesystem
  const int32_t perFile = ((int32_t)(fi.totalBytes / 5) - (int32_t)sizeof(LogHdr)) / (int32_t)sizeof(LogRec);
  if (perFile < 36) {  // less than 6 h per file is not worth it
    Serial.printf("Log: filesystem too small (%u bytes) - log disabled\n", (unsigned)fi.totalBytes);
    return;
  }
  logFileRecs = (uint32_t)perFile < LOG_FILE_RECS ? (uint32_t)perFile : LOG_FILE_RECS;
  logRecs[0] = logFileCount(0);
  logRecs[1] = logFileCount(1);

  // append to the file holding the newest record
  LogRec a, b;
  const bool ha = logRecs[0] && logReadRec(0, logRecs[0] - 1, a);
  const bool hb = logRecs[1] && logReadRec(1, logRecs[1] - 1, b);
  logCur = (hb && (!ha || b.t > a.t)) ? 1 : 0;
  logOk = true;
  logRefreshBounds();
  Serial.printf("Log: %lu records stored, keeps %lu..%lu days (FS %u KB)\n",
                (unsigned long)(logRecs[0] + logRecs[1]),
                (unsigned long)(logFileRecs * LOG_INTERVAL_S / 86400),
                (unsigned long)(2 * logFileRecs * LOG_INTERVAL_S / 86400),
                (unsigned)(fi.totalBytes / 1024));
}

// Adds the latest readings to the running 10-min averages
void logAccumulate() {
  if (!logOk || !logSlot) return;
  for (uint8_t ch = 0; ch < NUM_CH; ch++) {
    const SensorState& s = sens[ch];
    if (!chVisible(ch) || !s.valid) continue;
    logSum[LQ_T][ch] += s.temp; logN[LQ_T][ch]++;
    if (!isnan(s.hum))  { logSum[LQ_H][ch] += s.hum;  logN[LQ_H][ch]++; }
    if (!isnan(s.pres)) { logSum[LQ_P][ch] += s.pres; logN[LQ_P][ch]++; }
  }
}

void logWrite(uint32_t t) {
  LogRec r;
  r.t = t;
  bool any = false;
  for (uint8_t ch = 0; ch < NUM_CH; ch++) {
    r.v[ch] = logN[LQ_T][ch] ? toTenths(logSum[LQ_T][ch] / logN[LQ_T][ch]) : HIST_NONE;
    r.h[ch] = logN[LQ_H][ch] ? toTenths(logSum[LQ_H][ch] / logN[LQ_H][ch]) : HIST_NONE;
    r.p[ch] = logN[LQ_P][ch] ? toTenths(logSum[LQ_P][ch] / logN[LQ_P][ch]) : HIST_NONE;
    any |= r.v[ch] != HIST_NONE;
  }
  logResetAcc();
  if (!any) return;  // no sensor had data in the whole interval

  if (logRecs[logCur] >= logFileRecs) {  // current file full: recycle the older one
    logCur ^= 1;
    LittleFS.remove(LOG_FILES[logCur]);
    logRecs[logCur] = 0;
  }
  File f = LittleFS.open(LOG_FILES[logCur], "a");
  if (!f) { Serial.println(F("Log: write failed")); return; }
  bool ok = true;
  if (f.size() == 0) {
    const LogHdr h = { LOG_MAGIC, NUM_CH, (uint8_t)sizeof(LogRec), 0 };
    ok = f.write((const uint8_t*)&h, sizeof h) == sizeof h;
  }
  ok = ok && f.write((const uint8_t*)&r, sizeof r) == sizeof r;
  f.close();
  if (!ok) { Serial.println(F("Log: write failed")); return; }
  logRecs[logCur]++;
  logRefreshBounds();
}

// Writes a record each time the clock passes a 10-min mark (needs NTP time)
void logTick() {
  if (!logOk) return;
  const time_t now = time(nullptr);
  if (now < 1700000000) return;  // no NTP time yet
  const uint32_t slot = (uint32_t)now / LOG_INTERVAL_S;
  if (!logSlot) { logSlot = slot; logResetAcc(); return; }  // start the first (partial) interval
  if (slot <= logSlot) return;                               // same interval, or clock stepped back
  logWrite((logSlot + 1) * LOG_INTERVAL_S);
  logSlot = slot;
}

void csvNum(String& s, int16_t v, bool eu) {
  if (v == HIST_NONE) return;
  char b[10];
  const int a = v < 0 ? -v : v;
  snprintf(b, sizeof b, "%s%d%c%d", v < 0 ? "-" : "", a / 10, eu ? ',' : '.', a % 10);
  s += b;
}

void recordHistory(uint32_t now) {
  lastHistory = now;
  for (uint8_t i = 0; i < NUM_CH; i++)
    history[i][histHead] = toTenths(sens[i].valid ? sens[i].temp : NAN);
  histHead = (histHead + 1) % HISTORY_LEN;
  if (histCount < HISTORY_LEN) histCount++;
}

void updateTemps(uint32_t now) {
  if (now - lastTempRequest < TEMP_INTERVAL_MS) return;
  lastTempRequest = now;

  if (now - lastI2cScan >= BUS_RESCAN_MS) { lastI2cScan = now; scanI2c(); }
  updateI2c();
  updateThermo();
  updateDs(now);

  if (now - lastTrendSample >= TREND_SAMPLE_MS) {
    lastTrendSample = now;
    for (auto& s : sens) s.hist[histPos] = s.valid ? s.temp : NAN;
    histPos = (histPos + 1) % TREND_SAMPLES;
    logReadings();
  }

  if (histCount == 0 || now - lastHistory >= HISTORY_INTERVAL_MS) recordHistory(now);

  logAccumulate();
  firstReadDone = true;
  evaluateAlarms();
}

// ─────────────────────── Drawing primitives ───────────────────────
void drawCentered(const char* s, int y, const uint8_t* font) {
  u8g2.setFont(font);
  u8g2.drawStr((128 - u8g2.getStrWidth(s)) / 2, y, s);
}

// Big font if it fits on one line, otherwise the smaller one
void drawCenteredFit(const char* s, int y, const uint8_t* big, const uint8_t* small) {
  u8g2.setFont(big);
  if (u8g2.getStrWidth(s) > 126) u8g2.setFont(small);
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

// views: no "Back" in SPI port mode (button B is the SPI clock there)
void drawViewFooter() { drawFooter("Next", spiMode() ? nullptr : "Back"); }

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
const char* errorText(uint8_t ch) {
  if (isTc(ch) && sens[ch].tcOpen) return "TC open";
  return "read error";
}

void menuPreview(uint8_t i, char* buf, size_t n) {
  char t[8];
  if (i == M_OVERVIEW) {
    uint8_t online = 0, total = 0;
    for (uint8_t k = 0; k < NUM_CH; k++) if (chVisible(k)) { total++; online += sens[k].valid; }
    if (total) snprintf(buf, n, "%u of %u sensors online", online, total);
    else       strlcpy(buf, "no sensors found yet", n);
  } else if (i == M_ALARMS) {
    uint8_t on = 0, total = 0;
    for (uint8_t k = 0; k < NUM_CH; k++) if (chVisible(k)) { total++; on += settings.slot[k].alarm.enabled; }
    if (anyAlarmActive()) snprintf(buf, n, "%u enabled - ACTIVE!", on);
    else                  snprintf(buf, n, "%u of %u enabled", on, total);
  } else if (i == M_SETTINGS) {
    strlcpy(buf, "sound / display", n);
  } else if (i == M_INFO) {
    if (WiFi.status() == WL_CONNECTED) snprintf(buf, n, "IP %s", WiFi.localIP().toString().c_str());
    else snprintf(buf, n, "WiFi: %s", wifiStatusText());
  } else {  // sensors
    const uint8_t ch = channelOf(i);
    const SensorState& s = sens[ch];
    if (!s.present)     strlcpy(buf, "not connected", n);
    else if (!s.valid)  strlcpy(buf, errorText(ch), n);
    else {
      fmtTemp(t, sizeof t, s.temp);
      if (!isnan(s.hum)) snprintf(buf, n, "%s C   %.0f %%RH", t, s.hum);
      else snprintf(buf, n, settings.slot[ch].alarm.enabled ? "%s C   alarm on" : "%s C", t);
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

  drawCenteredFit(menuLabel(menuIdx), 35, u8g2_font_10x20_tr, u8g2_font_6x10_tr);
  char sub[28];
  menuPreview(menuIdx, sub, sizeof sub);
  drawCentered(sub, 48, u8g2_font_5x7_tr);

  drawFooter("Scroll", "Select");
}

// Overview: one row per sensor, 4 rows per page, pages flip every 3 s
void drawOverview(uint32_t now) {
  constexpr uint8_t PER_PAGE = 4;
  uint8_t rows[NUM_CH], nr = 0;
  for (uint8_t ch = 0; ch < NUM_CH; ch++) if (chVisible(ch)) rows[nr++] = ch;

  const uint8_t pages = nr ? (nr + PER_PAGE - 1) / PER_PAGE : 1;
  const uint8_t page  = pages > 1 ? (now / OVERVIEW_PAGE_MS) % pages : 0;
  char hdr[16];
  if (pages > 1) snprintf(hdr, sizeof hdr, "OVERVIEW %u/%u", page + 1, pages);
  else           strlcpy(hdr, "OVERVIEW", sizeof hdr);
  drawHeader(hdr);

  if (!nr) {
    drawCentered("No sensors yet", 28, u8g2_font_6x10_tr);
    drawCentered("plug one in, wait 30 s", 40, u8g2_font_5x7_tr);
    drawViewFooter();
    return;
  }

  const bool blink = (now / 400) & 1;
  u8g2.setFont(u8g2_font_6x10_tr);
  char v[20], t[8];

  for (uint8_t r = 0; r < PER_PAGE; r++) {
    const uint8_t idx = page * PER_PAGE + r;
    if (idx >= nr) break;
    const int y = 21 + r * 9;
    const uint8_t ch = rows[idx];
    const SensorState& s = sens[ch];
    if (!s.present)    strlcpy(v, "--", sizeof v);
    else if (!s.valid) strlcpy(v, isTc(ch) && s.tcOpen ? "open" : "err", sizeof v);
    else {
      fmtTemp(t, sizeof t, s.temp);
      if (!isnan(s.hum)) snprintf(v, sizeof v, "%sC %.0f%%", t, s.hum);
      else               snprintf(v, sizeof v, "%s C", t);
    }

    const bool highlight = alarms[ch].kind != AL_NONE && blink;
    if (highlight) { u8g2.drawBox(0, y - 8, 128, 9); u8g2.setDrawColor(0); }
    u8g2.drawStr(2, y, chName(ch));
    u8g2.drawStr(125 - u8g2.getStrWidth(v), y, v);
    u8g2.setDrawColor(1);
  }

  drawViewFooter();
}

void drawSensorView(uint8_t ch, uint32_t now) {
  drawHeader(chName(ch));

  const SensorState& s = sens[ch];
  char line[28], a[8], b[8];

  if (!s.present) {
    drawCentered("Not connected", 30, u8g2_font_6x10_tr);
  } else if (!s.valid) {
    drawCentered(isTc(ch) && s.tcOpen ? "Thermocouple open" : "Read error", 30, u8g2_font_6x10_tr);
  } else {
    const float tr = trendOf(s);
    drawBigTemp(s.temp, 36, tr > TREND_THRESHOLD ? 1 : tr < -TREND_THRESHOLD ? -1 : 0);

    if (now < resetMsgUntil) {
      strlcpy(line, "min/max reset", sizeof line);
    } else {
      fmtTemp(a, sizeof a, s.minT);
      fmtTemp(b, sizeof b, s.maxT);
      snprintf(line, sizeof line, "min %s   max %s", a, b);
    }
    drawCentered(line, 50, u8g2_font_5x7_tr);
  }

  // second line: humidity / pressure, or where the sensor is connected
  if (s.valid && !isnan(s.hum) && !isnan(s.pres)) {
    snprintf(line, sizeof line, "RH %.0f%%   %.1f hPa", s.hum, s.pres);
  } else if (s.valid && !isnan(s.hum)) {
    fmtTemp(a, sizeof a, dewPoint(s.temp, s.hum));
    snprintf(line, sizeof line, "RH %.1f%%   dp %s C", s.hum, a);
  } else if (s.valid && !isnan(s.pres)) {
    snprintf(line, sizeof line, "P %.1f hPa", s.pres);
  } else if (isDs(ch) && !s.valid && !isnan(s.lastBad)) {
    snprintf(line, sizeof line, "last bad %.1f  %s", s.lastBad, dsParasite ? "PARA" : "3w");
  } else {
    char src[20];
    sourceText(ch, src, sizeof src);
    snprintf(line, sizeof line, "%s%s", isDs(ch) ? "ROM " : "", src);
  }
  drawCentered(line, s.valid ? 43 : 42, u8g2_font_5x7_tr);

  drawViewFooter();
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
    snprintf(l, sizeof l, "WiFi %s", wifiStatusText());
    u8g2.drawStr(2, 20, l);
    snprintf(l, sizeof l, "SSID %s", WIFI_SSID);
  }
  u8g2.drawStr(2, 28, l);

  const uint32_t up = millis() / 1000;
  snprintf(l, sizeof l, "Up   %lud %02lu:%02lu:%02lu",
           (unsigned long)(up / 86400), (unsigned long)(up / 3600 % 24),
           (unsigned long)(up / 60 % 60), (unsigned long)(up % 60));
  u8g2.drawStr(2, 36, l);

  uint8_t online = 0, i2cN = 0;
  for (uint8_t s = 0; s < NUM_CH; s++) { online += sens[s].valid; i2cN += isI2c(s) && sens[s].present; }
  snprintf(l, sizeof l, "NTP %s  I2C %u  ok %u/%u", time(nullptr) > 1700000000 ? "ok" : "--",
           i2cN, online, visibleCount());
  u8g2.drawStr(2, 44, l);

  uint32_t errs = 0;
  for (auto& s : sens) errs += s.errors;
  snprintf(l, sizeof l, "v%s %s %s err %lu", FW_VERSION, spiMode() ? "SPI" : "btn",
           dsParasite ? "PARA" : "3w", (unsigned long)errs);
  u8g2.drawStr(2, 51, l);

  drawViewFooter();
}

void drawAlarmEdit() {
  if (!chVisible(editSensor)) editSensor = nextVisibleChannel(editSensor, 1);
  drawHeader("HOLD B = SAVE");
  if (!chVisible(editSensor)) {
    drawCentered("No sensors yet", 32, u8g2_font_6x10_tr);
    drawFooter(nullptr, "Next");
    return;
  }
  const AlarmCfg& c = edit.slot[editSensor].alarm;
  char v[16], t[8];

  drawRow(0, "Sensor", chName(editSensor));
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
  const AlarmCfg&    c = settings.slot[i].alarm;

  if ((now / 300) & 1) { u8g2.drawBox(0, 0, 128, 13); u8g2.setDrawColor(0); }
  drawCentered("!!  ALARM  !!", 10, u8g2_font_6x10_tr);
  u8g2.setDrawColor(1);

  const char* kind = alarms[i].kind == AL_HIGH ? "HIGH" : alarms[i].kind == AL_LOW ? "LOW" : "LOST";
  char l[28], a[8], b[8];
  snprintf(l, sizeof l, "%s %s", chName(i), kind);
  drawCenteredFit(l, 34, u8g2_font_10x20_tr, u8g2_font_6x10_tr);

  if (alarms[i].kind == AL_LOST) {
    strlcpy(l, isTc(i) && s.tcOpen ? "thermocouple open" : "sensor not responding", sizeof l);
  } else {
    fmtTemp(a, sizeof a, s.temp);
    fmtTemp(b, sizeof b, alarms[i].kind == AL_HIGH ? c.high : c.low);
    snprintf(l, sizeof l, "now %s C  limit %s C", a, b);
  }
  drawCentered(l, 47, u8g2_font_5x7_tr);
  drawFooter("Ack", spiMode() ? nullptr : "Ack");
}

void render(uint32_t now) {
  // a sensor may have been forgotten from the web page while it was on screen
  if (!menuVisible(menuIdx)) menuIdx = M_OVERVIEW;
  if (screen == SCR_VIEW && !isViewItem(viewIdx)) viewIdx = M_OVERVIEW;

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
        if (viewIdx == M_OVERVIEW)  drawOverview(now);
        else if (viewIdx == M_INFO) drawInfo();
        else                        drawSensorView(channelOf(viewIdx), now);
        break;
    }
  }
  u8g2.sendBuffer();
}

// ─────────────────────── Input handling ───────────────────────
void openMenuItem(uint8_t i) {
  if (i == M_ALARMS) {
    edit = settings; field = 0; screen = SCR_ALARM_EDIT;
    if (!chVisible(editSensor)) editSensor = nextVisibleChannel(editSensor, 1);
  } else if (i == M_SETTINGS) {
    edit = settings; field = 0; screen = SCR_SETTINGS;
  } else {
    viewIdx = i; screen = SCR_VIEW;
  }
}

void commitEdit() {
  // Sensors may have been added / renamed / forgotten from the web page or a
  // bus scan while this screen was open: only take what this screen edits.
  if (screen == SCR_ALARM_EDIT) {
    for (uint8_t i = 0; i < NUM_CH; i++)
      if (settings.slot[i].kind == edit.slot[i].kind) settings.slot[i].alarm = edit.slot[i].alarm;
  } else {
    settings.keyBeep      = edit.keyBeep;
    settings.alarmSound   = edit.alarmSound;
    settings.screenOffIdx = edit.screenOffIdx;
    settings.contrast     = edit.contrast;
  }
  sanitizeSettings(settings);
  saveSettings();
  refreshNames();
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
  if (!chVisible(editSensor)) return;
  AlarmCfg& c = edit.slot[editSensor].alarm;
  const KindInfo& k = kindOf(editSensor);
  switch (field) {
    case 0: if (!repeat) editSensor = nextVisibleChannel(editSensor, dir); break;
    case 1: if (!repeat) c.enabled = !c.enabled; break;
    case 2: c.high = constrain(c.high + dir * step, c.low + ALARM_MIN_GAP, k.tMax); break;
    case 3: c.low  = constrain(c.low  + dir * step, k.tMin, c.high - ALARM_MIN_GAP); break;
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
  const BtnEvent b = spiMode() ? EV_NONE : btnB.update();  // IO14 = SPI clock in SPI mode
  const bool bDown = !spiMode() && btnB.isDown();

  if (suppressInput) {
    if (!btnA.isDown() && !bDown) suppressInput = false;
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
        if (isSensorItem(viewIdx)) resetMinMax(sens[channelOf(viewIdx)]);
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
  if (isnan(v) || isinf(v)) { j += F("null"); return; }
  char b[16];
  snprintf(b, sizeof b, "%.*f", dec, v);
  j += b;
}

void jsonBool(String& j, bool v) { j += v ? F("true") : F("false"); }

const char* alarmName(AlarmKind k) {
  return k == AL_HIGH ? "HIGH" : k == AL_LOW ? "LOW" : k == AL_LOST ? "LOST" : "NONE";
}

// Name of what answers at an I2C address: the attached sensor, or a guess
const char* i2cName(uint8_t a) {
  for (uint8_t s = 0; s < NUM_CH; s++)
    if (isI2c(s) && settings.slot[s].id[0] == a && sens[s].present) return kindOf(s).name;
  return sensors::i2cGuess(a);
}

void handleData() {
  String j;
  j.reserve(5200);
  char b[48];

  j += F("{\"fw\":\"" FW_VERSION "\",\"time\":\"");
  time_t now = time(nullptr);
  const bool synced = now > 1700000000;
  if (synced) {
    struct tm tmv;
    localtime_r(&now, &tmv);
    snprintf(b, sizeof b, "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    j += b;
  }
  j += F("\",\"synced\":");      jsonBool(j, synced);
  j += F(",\"uptime\":");        j += String(millis() / 1000);
  j += F(",\"ip\":\"");          j += WiFi.localIP().toString();
  j += F("\",\"rssi\":");        j += String(WiFi.RSSI());
  j += F(",\"heap\":");          j += String(ESP.getFreeHeap());
  j += F(",\"dev\":{\"sound\":"); j += settings.alarmSound ? '1' : '0';
  j += F(",\"beep\":");          j += settings.keyBeep ? '1' : '0';
  j += F(",\"off\":");           j += String(settings.screenOffIdx);
  j += F(",\"offMin\":[");
  for (uint8_t i = 0; i < SCREEN_OFF_COUNT; i++) { if (i) j += ','; j += String(SCREEN_OFF_MIN[i]); }
  j += F("],\"contrast\":");     j += String(settings.contrast);
  j += F(",\"spi\":");           j += settings.spiMode ? '1' : '0';
  j += F(",\"restart\":");       jsonBool(j, restartAt != 0);
  j += F("},\"dsOnBus\":");      j += String(dsOnBus);
  j += F(",\"slots\":");         j += String(NUM_CH);
  j += F(",\"gap\":");           jsonNum(j, ALARM_MIN_GAP);
  j += F(",\"i2c\":[");
  for (uint8_t k = 0; k < i2cSeenN; k++) {
    if (k) j += ',';
    snprintf(b, sizeof b, "\"0x%02X %s\"", i2cSeen[k], i2cName(i2cSeen[k]));
    j += b;
  }
  j += F("],\"log\":{\"ok\":");  jsonBool(j, logOk);
  j += F(",\"recs\":");          j += String(logRecs[0] + logRecs[1]);
  j += F(",\"oldest\":");        j += String(logOldest);
  j += F(",\"newest\":");        j += String(logNewest);
  j += F(",\"keepMin\":");       j += String(logFileRecs * LOG_INTERVAL_S / 3600);
  j += F(",\"keepMax\":");       j += String(2 * logFileRecs * LOG_INTERVAL_S / 3600);
  j += F(",\"every\":");         j += String(LOG_INTERVAL_S / 60);
  j += '}';
  j += F(",\"sensors\":[");

  for (uint8_t i = 0; i < NUM_CH; i++) {
    const SensorState& s = sens[i];
    const SlotCfg&     c = settings.slot[i];
    const KindInfo&    k = kindOf(i);
    if (i) j += ',';
    j += F("{\"id\":");       j += String(i + 1);
    j += F(",\"kind\":\"");   j += k.code;
    j += F("\",\"model\":\""); j += k.model;
    j += F("\",\"name\":\""); j += chName(i);
    j += F("\",\"label\":\""); j += c.label;
    j += F("\",\"def\":\"");  j += defNameBuf[i];
    sourceText(i, b, sizeof b);
    j += F("\",\"src\":\"");  j += b;
    j += F("\",\"visible\":"); jsonBool(j, chVisible(i));
    if (!chVisible(i)) { j += '}'; continue; }
    j += F(",\"present\":"); jsonBool(j, s.present);
    j += F(",\"valid\":");   jsonBool(j, s.valid);
    j += F(",\"t\":");       jsonNum(j, s.valid ? s.temp : NAN);
    j += F(",\"min\":");     jsonNum(j, s.minT);
    j += F(",\"max\":");     jsonNum(j, s.maxT);
    j += F(",\"trend\":");   jsonNum(j, trendOf(s), 2);
    j += F(",\"h\":");       jsonNum(j, s.hum);
    j += F(",\"dp\":");      jsonNum(j, s.valid ? dewPoint(s.temp, s.hum) : NAN);
    j += F(",\"p\":");       jsonNum(j, s.pres);
    j += F(",\"hasH\":");    jsonBool(j, k.hum);
    j += F(",\"hasP\":");    jsonBool(j, k.pres);
    j += F(",\"off\":");     jsonNum(j, c.offset / 10.0f);
    j += F(",\"tmin\":");    jsonNum(j, k.tMin);
    j += F(",\"tmax\":");    jsonNum(j, k.tMax);
    j += F(",\"reads\":");   j += String(s.reads);
    j += F(",\"errors\":");  j += String(s.errors);
    j += F(",\"drops\":");   j += String(s.drops);
    j += F(",\"lastBad\":"); jsonNum(j, s.lastBad, 0);
    if (isDs(i)) {
      j += F(",\"parasite\":"); jsonBool(j, dsParasite);
      j += F(",\"miss\":");     j += String(dsSearchMiss[i]);
    } else if (isTc(i)) {
      j += F(",\"tcOpen\":");   jsonBool(j, s.tcOpen);
    }
    j += F(",\"al\":{\"on\":"); jsonBool(j, c.alarm.enabled);
    j += F(",\"lo\":");     jsonNum(j, c.alarm.low);
    j += F(",\"hi\":");     jsonNum(j, c.alarm.high);
    j += F(",\"state\":\""); j += alarmName(alarms[i].kind);
    j += F("\",\"ack\":");  jsonBool(j, alarms[i].acked);
    j += F("}}");
  }
  j += F("]}");

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
  for (uint8_t s = 0; s < NUM_CH; s++) {
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

// GET /api/log.csv[?eu=1]  - the flash log as CSV, oldest first, streamed in
// chunks straight from the files (no big buffer in RAM). Columns = sensors
// shown right now, headed with their labels; + RH / pressure where they exist.
void handleLogCsv() {
  if (!logOk) {
    server.send(503, F("text/plain"), F("Log not available: no filesystem (Tools > Flash Size)"));
    return;
  }
  const bool eu  = server.arg("eu") == "1";
  const char sep = eu ? ';' : ',';
  uint8_t cols[NUM_CH], nc = 0;
  for (uint8_t ch = 0; ch < NUM_CH; ch++) if (chVisible(ch)) cols[nc++] = ch;

  char b[96];
  time_t now = time(nullptr);
  struct tm tmv;
  localtime_r(&now, &tmv);
  snprintf(b, sizeof b, "attachment; filename=\"%s_log_%04d-%02d-%02d_%02d%02d.csv\"", MDNS_NAME,
           tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
  server.sendHeader(F("Content-Disposition"), b);
  server.sendHeader(F("Cache-Control"), F("no-store"));
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, F("text/csv; charset=utf-8"), "");

  String chunk;
  chunk.reserve(1200);
  chunk = "\xEF\xBB\xBF" "Time";  // UTF-8 BOM so Excel shows °C
  for (uint8_t k = 0; k < nc; k++) {
    const uint8_t ch = cols[k];
    chunk += sep; chunk += chName(ch); chunk += F(" (°C)");
    if (kindOf(ch).hum)  { chunk += sep; chunk += chName(ch); chunk += F(" RH (%)"); }
    if (kindOf(ch).pres) { chunk += sep; chunk += chName(ch); chunk += F(" P (hPa)"); }
  }
  chunk += F("\r\n");

  LogRec buf[8];
  for (uint8_t pass = 0; pass < 2; pass++) {
    const uint8_t fi = pass == 0 ? (logCur ^ 1) : logCur;  // older file first
    if (!logRecs[fi]) continue;
    File f = LittleFS.open(LOG_FILES[fi], "r");
    if (!f) continue;
    f.seek(sizeof(LogHdr));
    for (;;) {
      const int got = (int)(f.read((uint8_t*)buf, sizeof buf) / sizeof(LogRec));
      if (got <= 0) break;
      for (int k = 0; k < got; k++) {
        const LogRec& r = buf[k];
        const time_t t = r.t;
        localtime_r(&t, &tmv);
        snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
        chunk += b;
        for (uint8_t c = 0; c < nc; c++) {
          const uint8_t ch = cols[c];
          chunk += sep; csvNum(chunk, r.v[ch], eu);
          if (kindOf(ch).hum)  { chunk += sep; csvNum(chunk, r.h[ch], eu); }
          if (kindOf(ch).pres) { chunk += sep; csvNum(chunk, r.p[ch], eu); }
        }
        chunk += F("\r\n");
        if (chunk.length() > 1000) { server.sendContent(chunk); chunk = ""; }
      }
      yield();
    }
    f.close();
  }
  if (chunk.length()) server.sendContent(chunk);
  server.sendContent("");  // end of chunked response
}

void handleAck() {
  ackAlarms();
  server.send(204);
}

void badRequest(const __FlashStringHelper* msg) {
  server.send(400, F("text/plain; charset=utf-8"), msg);
}

int webChannel() {
  if (!server.hasArg("ch")) return -1;
  const int ch = server.arg("ch").toInt();
  return (ch >= 0 && ch < NUM_CH && chVisible(ch)) ? ch : -1;
}

bool parseNum(const char* name, float& out) {
  if (!server.hasArg(name)) return true;  // not sent = unchanged
  String v = server.arg(name);
  v.trim();
  v.replace(',', '.');
  if (!v.length()) return false;
  const float f = v.toFloat();
  if (f == 0.0f && v[0] != '0' && v[0] != '-' && v[0] != '.') return false;
  out = roundf(f * 10.0f) / 10.0f;
  return true;
}

// POST /api/config  ch=<0..> [label=] [off=] [on=0|1] [lo=] [hi=]
void handleConfig() {
  const int ch = webChannel();
  if (ch < 0) return badRequest(F("Unknown sensor"));
  SlotCfg& sc = settings.slot[ch];
  const KindInfo& k = kindOf(ch);

  AlarmCfg c = sc.alarm;
  if (server.hasArg("on")) c.enabled = server.arg("on") == "1" ? 1 : 0;
  if (!parseNum("lo", c.low) || !parseNum("hi", c.high)) return badRequest(F("Limits must be numbers"));
  if (c.low < k.tMin || c.high > k.tMax) {
    String m = F("Limits must be between ");
    m += String(k.tMin, 0); m += F(" and "); m += String(k.tMax, 0); m += F(" °C for this sensor");
    server.send(400, F("text/plain; charset=utf-8"), m);
    return;
  }
  if (c.high < c.low + ALARM_MIN_GAP) return badRequest(F("HIGH must be at least 2 °C above LOW"));

  float off = sc.offset / 10.0f;
  if (!parseNum("off", off)) return badRequest(F("Offset must be a number"));
  if (fabsf(off) > OFFSET_MAX_TENTHS / 10.0f) return badRequest(F("Offset must be between -20 and 20 °C"));

  sc.alarm = c;
  const int16_t newOff = (int16_t)lroundf(off * 10.0f);
  if (newOff != sc.offset) {  // shift the shown value now, min/max start over
    SensorState& s = sens[ch];
    if (s.valid) s.temp += (newOff - sc.offset) / 10.0f;
    sc.offset = newOff;
    resetMinMax(s);
  }
  if (server.hasArg("label")) cleanLabel(server.arg("label").c_str(), sc.label);

  saveSettings();
  refreshNames();
  if (!c.enabled) { alarms[ch] = AlarmState(); if (!spiMode()) noTone(PIN_BUZZER); }
  evaluateAlarms();
  server.send(204);
}

// POST /api/forget  ch=<slot>  - frees the slot of a sensor that is gone
void handleForget() {
  const int ch = webChannel();
  if (ch < 0)           return badRequest(F("Unknown sensor"));
  if (sens[ch].present) return badRequest(F("This sensor is still connected - unplug it first"));
  forgetSlot(ch);
  server.send(204);
}

// POST /api/device  sound= beep= off=<index> contrast=1..8 spi=0|1
void handleDevice() {
  auto flag = [](const char* n, uint8_t& v) { if (server.hasArg(n)) v = server.arg(n) == "1" ? 1 : 0; };
  flag("sound", settings.alarmSound);
  flag("beep", settings.keyBeep);
  if (server.hasArg("off")) {
    const int i = server.arg("off").toInt();
    if (i < 0 || i >= SCREEN_OFF_COUNT) return badRequest(F("Bad screen-off value"));
    settings.screenOffIdx = (uint8_t)i;
  }
  if (server.hasArg("contrast")) {
    const int c = server.arg("contrast").toInt();
    if (c < 1 || c > 8) return badRequest(F("Contrast must be 1..8"));
    settings.contrast = (uint8_t)c;
  }
  const uint8_t oldSpi = settings.spiMode;
  flag("spi", settings.spiMode);
  const bool restart = settings.spiMode != oldSpi;

  saveSettings();
  applyContrast(settings.contrast);
  if (!settings.alarmSound && !spiMode()) noTone(PIN_BUZZER);
  lastActivity = millis();
  if (restart) {
    settings.spiMode = oldSpi;  // pins stay as they are until the restart
    restartAt = millis() + 800;
  }
  server.send(200, F("application/json"), restart ? F("{\"restart\":true}") : F("{\"restart\":false}"));
}

void setupWeb() {
  server.on("/", HTTP_GET, [] {
    server.send_P(200, PSTR("text/html; charset=utf-8"), INDEX_HTML);
  });
  server.on("/api/data", HTTP_GET, handleData);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/ack", HTTP_POST, handleAck);
  server.on("/api/config", HTTP_POST, handleConfig);
  server.on("/api/forget", HTTP_POST, handleForget);
  server.on("/api/device", HTTP_POST, handleDevice);
  server.on("/api/log.csv", HTTP_GET, handleLogCsv);
  server.onNotFound([] { server.send(404, F("text/plain"), F("Not found")); });
  server.begin();
}

void updateNetwork() {
  server.handleClient();

  static wl_status_t lastSt = WL_IDLE_STATUS;
  const wl_status_t st = WiFi.status();
  if (st != lastSt) {
    lastSt = st;
    if (st == WL_CONNECTED) {
      Serial.printf("WiFi connected: IP %s  GW %s  mask %s  ch %d  %ld dBm\n",
                    WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(),
                    WiFi.subnetMask().toString().c_str(), WiFi.channel(), (long)WiFi.RSSI());
    } else {
      Serial.printf("WiFi: %s (status %d)\n", wifiStatusText(), (int)st);
    }
  }

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
  loadSettings();

  // IO13 / IO14: button B + buzzer, or the SPI port (see the Device panel)
  btnA.begin();
  if (spiMode()) {
    thermo.begin();
  } else {
    pinMode(PIN_BUZZER, OUTPUT);
    btnB.begin();
  }

  for (auto& s : sens) resetStats(s);
  for (auto& row : history) for (auto& v : row) v = HIST_NONE;

  // One I2C bus for the OLED and the sensors. Set the pins first.
  Wire.begin(PIN_SDA, PIN_SCL);

  u8g2.setI2CAddress(OLED_ADDR * 2);
  u8g2.setBusClock(OLED_I2C_HZ);
  u8g2.begin();
  u8g2.setFontMode(1);  // transparent fonts -> inverted text on highlight boxes
  applyContrast(settings.contrast);

  u8g2.clearBuffer();
  drawCentered("RJ45 BOARD", 30, u8g2_font_10x20_tr);
  drawCentered("v" FW_VERSION "  finding sensors...", 46, u8g2_font_5x7_tr);
  u8g2.sendBuffer();

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.hostname(MDNS_NAME);

  // Diagnostic: is our SSID visible on 2.4 GHz? (blocks ~2 s, once at boot)
  Serial.printf("\nRJ45 Board v%s - scanning for \"%s\"...\n", FW_VERSION, WIFI_SSID);
  const int nNet = WiFi.scanNetworks(false, true);
  bool seen = false;
  for (int i = 0; i < nNet; i++) {
    if (WiFi.SSID(i) == WIFI_SSID) {
      seen = true;
      Serial.printf("  found: ch %d, %d dBm, encryption %d\n",
                    WiFi.channel(i), WiFi.RSSI(i), WiFi.encryptionType(i));
    }
  }
  if (!seen) {
    Serial.printf("  NOT found among %d networks. 2.4 GHz SSIDs seen:\n", nNet);
    for (int i = 0; i < nNet; i++) Serial.printf("    \"%s\"  ch %d\n", WiFi.SSID(i).c_str(), WiFi.channel(i));
  }
  WiFi.scanDelete();
#ifdef USE_STATIC_IP
  WiFi.config(IPAddress(STATIC_IP), IPAddress(STATIC_GW), IPAddress(STATIC_MASK), IPAddress(STATIC_DNS));
#endif
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  configTime(TZ_INFO, NTP_1, NTP_2);  // syncs in the background, DST automatic
  setupWeb();

  // find everything that is plugged in right now
  scanI2c();
  scanDs();
  updateThermo();
  startLog();
  Serial.printf("SPI port: %s\n", spiMode() ? "SPI mode (MAX6675), button B + buzzer off" : "button B + buzzer");
  Serial.printf("I2C: %u device(s) answering\n", i2cSeenN);
  Serial.printf("1-Wire: %u DS18B20 on the bus, %s power\n", dsOnBus, dsParasite ? "PARASITE" : "normal 3-wire");
  for (uint8_t s = 0; s < NUM_CH; s++)
    if (chVisible(s)) Serial.printf("  slot %u: %s (%s)%s\n", s + 1, chName(s), kindOf(s).model,
                                    sens[s].present ? "" : " - not connected");

  const uint32_t now = millis();
  lastDsScan      = now;
  lastI2cScan     = now - BUS_RESCAN_MS / 2;  // spread the two bus scans 15 s apart
  lastTempRequest = now - TEMP_INTERVAL_MS;   // first reading right away
  lastActivity    = now;
  if (spiMode()) { screen = SCR_VIEW; viewIdx = menuIdx = M_OVERVIEW; }

  playStartupSound();
}

void loop() {
  const uint32_t now = millis();
  logTick();
  updateTemps(now);
  handleButtons(now);
  updateNetwork();
  updateBuzzer(now);
  updateUiTimers(now);

  if (restartAt && (int32_t)(now - restartAt) >= 0) {
    Serial.println(F("Restarting for the new SPI port mode..."));
    delay(50);
    ESP.restart();
  }

  if (!displayAsleep && now - lastFrame >= FRAME_MS) {
    lastFrame = now;
    render(now);
  }
}
