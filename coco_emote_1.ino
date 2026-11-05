/*
  ╔══════════════════════════════════════════╗
  ║            C O C O   E M O T E           ║
  ║     animated-eyes keychain · v1.0        ║
  ╚══════════════════════════════════════════╝

  Hardware : ESP32-C3 SuperMini + 1.3" I2C OLED (SH1106) + LiPo + TP4056 USB-C charger
  Libraries: U8g2 (by olikraus) — everything else ships with the ESP32 core
  Board    : "ESP32C3 Dev Module"  (Tools > USB CDC On Boot: Enabled)

  NO BUTTON NEEDED (default, HAS_BUTTON 0)
    Power on -> WiFi control turns on by itself.
    WiFi stays on for 3 minutes after power-on, and keeps going while you use the page;
    after 3 minutes with nobody on the page it switches off (saves battery);
    the eyes keep going in the last mood you picked.
    To change mood again: flip the power switch off and on.

  OPTIONAL BUTTON (set HAS_BUTTON 1, button between GPIO3 and GND)
    tap            -> next mood
    hold 1.5 s     -> (release) WiFi control ON / OFF
    hold 4 s       -> (release) sleep  (tap again to wake)

  WIFI CONTROL
    Join the WiFi "Coco-Emote-XXXX" — the control page opens by itself
    (or browse to http://192.168.4.1).
*/

#include <Arduino.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "web_page.h"

// ================== PINS ==================
// ESP32-C3 SuperMini (the number printed next to the pin = GPIO number)
//   pin 8 = OLED SDA
//   pin 9 = OLED SCL
//   pin 3 = optional button (other leg to GND)
// Seeed XIAO ESP32C3 instead: SDA 6 (D4), SCL 7 (D5), button 3 (D1)
#define PIN_SDA 8
#define PIN_SCL 9
#define PIN_BTN 3   // must be GPIO0–5 so it can wake the chip from deep sleep

#define HAS_BUTTON 0   // 0 = no button (WiFi on at power-up), 1 = button fitted

// 1.3" OLED = SH1106. If the image is shifted / garbled, use the SSD1306 line.
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_SCL, PIN_SDA);
// U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_SCL, PIN_SDA);

// ================== MOODS ==================
enum Mood : uint8_t { IDLE, HAPPY, SAD, ANGRY, SLEEPY, LOVE, SURPRISED, MOOD_COUNT };
const char* MOOD_KEYS[MOOD_COUNT]  = {"idle", "happy", "sad", "angry", "sleepy", "love", "surprised"};
const char* MOOD_NAMES[MOOD_COUNT] = {"Chill", "Happy", "Sad", "Angry", "Sleepy", "Love", "Surprised"};

struct MoodStyle {
  float open, sx, sy;          // eye openness and width/height scale
  float squint, sad, angry;    // lid shapes (0..1)
  float heart;                 // 1 = heart eyes
  int   lookRange, lookYBias;  // how far it looks around
  float lookSpeed;             // how fast eyes move
  int   blinkMin, blinkMax;    // ms between blinks
  float radius;                // corner roundness
};

const MoodStyle STYLES[MOOD_COUNT] = {
  // open  sx    sy   squint sad angry heart range ybias speed  bmin  bmax   r
  { 1.00, 1.00, 1.00,  0,    0,  0,    0,    13,   0,  0.25f, 2000, 5000,  8 },  // IDLE
  { 1.00, 1.05, 1.00,  1,    0,  0,    0,     8,  -2,  0.25f, 2500, 5500,  8 },  // HAPPY
  { 0.85, 1.00, 0.90,  0,    1,  0,    0,     5,   7,  0.08f, 3000, 6000,  8 },  // SAD
  { 0.80, 1.05, 0.90,  0,    0,  1,    0,     6,  -1,  0.35f, 3000, 7000,  6 },  // ANGRY
  { 0.30, 1.00, 1.00,  0,    0,  0,    0,     5,   4,  0.05f, 4000, 8000,  8 },  // SLEEPY
  { 1.00, 1.00, 1.00,  0,    0,  0,    1,     5,   0,  0.15f, 2500, 5000,  8 },  // LOVE
  { 1.00, 0.85, 1.18,  0,    0,  0,    0,     4,  -2,  0.40f, 5000, 9000, 14 },  // SURPRISED
};

// ================== GEOMETRY ==================
const int EYE_W = 36, EYE_H = 36, GAP = 14;
const int CX = 64, CY = 32;

// ================== STATE ==================
Mood mood = IDLE;
uint8_t brightness = 160;

// eased (current) values
float lookX = 0, lookY = 0, tgtX = 0, tgtY = 0;
float openL = 0, openR = 0;               // start closed -> "wakes up" on boot
float sxL = 1, sxR = 1, syL = 1, syR = 1;
float squint = 0, sadA = 0, angryA = 0, heartA = 0, radius = 8;

// timers
unsigned long nextLook = 0, nextBlink = 1200, nextAction = 4000;
unsigned long blinkUntil = 0, winkUntil = 0, curiousUntil = 0, tempHappyUntil = 0;
unsigned long hopStart = 0, nextHop = 0, nodUntil = 0, nextNod = 0;
unsigned long tearStart = 0, nextTear = 0;
bool winkLeft = false, curiousLeft = false;

// toast / info screen
char toastMsg[32] = "";
unsigned long toastUntil = 0;
unsigned long infoUntil = 0;

// button
bool btnRawLast = false, btnStable = false, ignoreRelease = false;
unsigned long btnRawChange = 0, pressStart = 0;
uint8_t holdStage = 0;

// wifi
WebServer server(80);
DNSServer dns;
const IPAddress AP_IP(192, 168, 4, 1);
bool apOn = false;
unsigned long lastWeb = 0;
const unsigned long WIFI_IDLE_OFF_MS = 3UL * 60UL * 1000UL;   // on for 3 min after power-on (longer while you use the page)
char ssid[24];
bool sleepRequested = false;

Preferences prefs;

// ================== HELPERS ==================
float ease(float cur, float tgt, float k) { return cur + (tgt - cur) * k; }

void toast(const char* msg, uint16_t ms = 1200) {
  strncpy(toastMsg, msg, sizeof(toastMsg) - 1);
  toastMsg[sizeof(toastMsg) - 1] = 0;
  toastUntil = millis() + ms;
}

void safeRBox(int x, int y, int w, int h, int r) {
  if (w < 1 || h < 1) return;
  if (h < 2 * (r + 1)) r = h / 2 - 1;
  if (w < 2 * (r + 1)) r = w / 2 - 1;
  if (r < 1) u8g2.drawBox(x, y, w, h);
  else       u8g2.drawRBox(x, y, w, h, r);
}

// lid triangle cutting the top-left or top-right corner of an eye box
void lidCut(int x, int y, int w, int depth, bool leftSide) {
  int y0 = max(0, y - 1);
  if (leftSide) u8g2.drawTriangle(max(0, x - 1), y0, x + (int)(w * 0.7f), y0, max(0, x - 1), y + depth);
  else          u8g2.drawTriangle(x + (int)(w * 0.3f), y0, x + w + 1, y0, x + w + 1, y + depth);
}

void drawHeart(int cx, int cy, float scale) {
  int r = max(2, (int)(10 * scale));
  int top = cy - r / 2;
  u8g2.drawDisc(cx - r, top, r);
  u8g2.drawDisc(cx + r, top, r);
  u8g2.drawTriangle(cx - 2 * r, top, cx + 2 * r, top, cx, cy + (int)(1.6f * r));
}

void drawEye(int cx, int cy, float open, float sx, float sy, bool isLeft) {
  u8g2.setDrawColor(1);

  if (heartA > 0.5f) {   // heart eyes (they shrink when blinking + gently pulse)
    float pulse = 1.0f + 0.08f * sinf(millis() / 180.0f);
    drawHeart(cx, cy, max(0.2f, open) * pulse * ((heartA - 0.5f) * 2.0f));
    return;
  }

  int w = (int)(EYE_W * sx);
  int h = max(2, (int)(EYE_H * sy * open));
  int x = constrain(cx - w / 2, 0, 127 - w);
  int y = constrain(cy - h / 2, 0, 63 - h);

  safeRBox(x, y, w, h, (int)radius);

  u8g2.setDrawColor(0);
  if (squint > 0.05f && h > 10) {                    // happy ^ ^
    int cutY = y + (int)(h * (1.0f - 0.55f * squint));
    safeRBox(max(0, x - 4), cutY, w + 8, h, 8);
  }
  if (sadA > 0.05f && h > 8)   lidCut(x, y, w, (int)(h * 0.55f * sadA), isLeft);    // outer corners droop
  if (angryA > 0.05f && h > 8) lidCut(x, y, w, (int)(h * 0.55f * angryA), !isLeft); // inner corners slant
  u8g2.setDrawColor(1);
}

float hopOffset(unsigned long now) {
  unsigned long t = now - hopStart;
  if (t < 500) return -fabsf(sinf(t / 250.0f * PI)) * 4.0f;
  return 0;
}

// ================== MOOD CONTROL ==================
void setMood(Mood m, bool announce = true) {
  if (m >= MOOD_COUNT) return;
  mood = m;
  prefs.putUChar("mood", (uint8_t)m);
  if (announce) toast(MOOD_NAMES[m]);
  nextLook = 0;
  curiousUntil = tempHappyUntil = 0;
}

void nextMood() { setMood((Mood)((mood + 1) % MOOD_COUNT)); }

void doBlink() { blinkUntil = millis() + 120; }
void doWink()  { winkLeft = random(2); winkUntil = millis() + 350; }
void doHop()   { hopStart = millis(); }
void doLook()  { tgtX = random(2) ? 14 : -14; tgtY = random(-6, 7); nextLook = millis() + 1500; }

void setBrightness(int v) {
  brightness = constrain(v, 10, 255);
  u8g2.setContrast(brightness);
  prefs.putUChar("bright", brightness);
}

// ================== WIFI + WEB ==================
String stateJson() {
  String j = "{\"name\":\"";
  j += ssid;
  j += "\",\"mood\":\"";
  j += MOOD_KEYS[mood];
  j += "\",\"bright\":";
  j += brightness;
  j += "}";
  return j;
}

void sendState() {
  lastWeb = millis();
  server.send(200, "application/json", stateJson());
}

void redirectToPortal() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

void setupRoutes() {
  server.on("/", HTTP_GET, []() {
    lastWeb = millis();
    Serial.println("[web] phone opened the page");
    server.send(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.on("/api/state", HTTP_GET, sendState);
  server.on("/api/mood", HTTP_POST, []() {
    String m = server.arg("m");
    for (uint8_t i = 0; i < MOOD_COUNT; i++)
      if (m == MOOD_KEYS[i]) setMood((Mood)i);
    sendState();
  });
  server.on("/api/action", HTTP_POST, []() {
    String a = server.arg("a");
    if (a == "blink") doBlink();
    else if (a == "wink") doWink();
    else if (a == "hop") doHop();
    else if (a == "look") doLook();
    sendState();
  });
  server.on("/api/bright", HTTP_POST, []() {
    setBrightness(server.arg("v").toInt());
    sendState();
  });
  server.on("/api/sleep", HTTP_POST, []() {
    sendState();
    sleepRequested = true;
  });
  server.onNotFound(redirectToPortal);   // captive portal: every unknown URL -> our page
}

void startWiFi() {
  setCpuFrequencyMhz(160);                // full speed while WiFi is on = more stable
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);     // SuperMini fix: its antenna misbehaves at full power
  delay(100);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  bool ok = WiFi.softAP(ssid, NULL, 6, 0, 4);   // open, channel 6, visible, max 4 phones
  WiFi.setTxPower(WIFI_POWER_8_5dBm);     // set again after start (some cores reset it)
  Serial.printf("[wifi] hotspot %s %s at %s\n", ssid, ok ? "STARTED" : "FAILED",
                WiFi.softAPIP().toString().c_str());
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", AP_IP);
  server.begin();
  apOn = true;
  lastWeb = millis();
  infoUntil = millis() + 4500;
}

void stopWiFi() {
  if (!apOn) return;
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  apOn = false;
  setCpuFrequencyMhz(80);                 // back to battery-saving speed
  Serial.println("[wifi] off");
}

void toggleWiFi() {
  if (apOn) { stopWiFi(); toast("WiFi OFF"); }
  else      { startWiFi(); }
}

// ================== SLEEP ==================
void goToSleep() {
  stopWiFi();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB10_tr);
  const char* s = "Good night";
  u8g2.drawStr(64 - u8g2.getStrWidth(s) / 2, 38, s);
  u8g2.sendBuffer();
  delay(900);
  u8g2.setPowerSave(1);

  while (digitalRead(PIN_BTN) == LOW) delay(10);   // wait for release or it wakes instantly
  delay(100);

#if HAS_BUTTON
  gpio_pullup_en((gpio_num_t)PIN_BTN);
  gpio_pulldown_dis((gpio_num_t)PIN_BTN);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_BTN, ESP_GPIO_WAKEUP_GPIO_LOW);
#endif
  esp_deep_sleep_start();   // no button: stays asleep until the power switch is flipped
}

// ================== BUTTON ==================
void handleButton(unsigned long now) {
  bool raw = digitalRead(PIN_BTN) == LOW;
  if (raw != btnRawLast) { btnRawLast = raw; btnRawChange = now; }
  if (now - btnRawChange < 25) return;              // debounce

  if (raw != btnStable) {
    btnStable = raw;
    if (btnStable) {
      pressStart = now;
      holdStage = 0;
    } else {
      if (ignoreRelease) { ignoreRelease = false; return; }
      if (holdStage == 2)      goToSleep();
      else if (holdStage == 1) toggleWiFi();
      else if (now - pressStart < 700) nextMood();
    }
  }

  if (btnStable && !ignoreRelease) {
    unsigned long held = now - pressStart;
    if (holdStage == 0 && held > 1500) { holdStage = 1; toast(apOn ? "Release: WiFi OFF" : "Release: WiFi ON", 2600); }
    if (holdStage == 1 && held > 4000) { holdStage = 2; toast("Release: Sleep", 3000); }
  }
}

// ================== BEHAVIOUR ==================
void updateBehaviour(unsigned long now) {
  const MoodStyle& st = STYLES[mood];

  // looking around (paused while curious)
  if (now > nextLook && now > curiousUntil) {
    int r = random(6), R = st.lookRange;
    if (r == 0 && mood == IDLE) { tgtX = random(2) ? R : -R; tgtY = random(2) ? 8 : -8; }   // dart to corner
    else if (r == 1)            { tgtX = 0; tgtY = st.lookYBias; }                          // back to centre
    else                        { tgtX = random(-R, R + 1); tgtY = st.lookYBias + random(-4, 5); }
    tgtY = constrain(tgtY, -9, 9);
    nextLook = now + (mood == SLEEPY ? random(2500, 5000) : random(700, 2500));
  }

  // blinking (20% double blink)
  if (now > nextBlink) {
    blinkUntil = now + 110;
    nextBlink = (random(5) == 0) ? now + 250 : now + random(st.blinkMin, st.blinkMax);
  }

  // per-mood extras
  switch (mood) {
    case IDLE:
      if (now > nextAction) {
        switch (random(4)) {
          case 0: doWink(); break;
          case 1: curiousLeft = random(2); curiousUntil = now + 1800;
                  tgtX = curiousLeft ? -10 : 10; tgtY = -6; break;
          case 2: tempHappyUntil = now + 1600; doHop(); tgtX = 0; tgtY = 0; break;
          default: break;                                   // just chill
        }
        nextAction = now + random(4000, 8000);
      }
      break;
    case HAPPY:
    case LOVE:
      if (now > nextHop) { doHop(); nextHop = now + random(2500, 6000); }
      break;
    case SLEEPY:
      if (now > nextNod) { nodUntil = now + 1500; nextNod = now + random(5000, 9000); }
      break;
    case SAD:
      if (now > nextTear) { tearStart = now; nextTear = now + random(5000, 9000); }
      break;
    case ANGRY:
      lookX += random(-1, 2) * 0.6f;                         // little rage tremble
      break;
    default: break;
  }
}

void updateAnimation(unsigned long now) {
  const MoodStyle& st = STYLES[mood];

  float tOpenL = st.open, tOpenR = st.open;
  if (now < blinkUntil || now < nodUntil) { tOpenL = 0; tOpenR = 0; }
  if (now < winkUntil) { if (winkLeft) tOpenL = 0; else tOpenR = 0; }

  float cL = 1, cR = 1;                                      // curious size
  if (now < curiousUntil) { cL = curiousLeft ? 1.18f : 0.85f; cR = curiousLeft ? 0.85f : 1.18f; }

  float tSquint = (now < tempHappyUntil) ? 1.0f : st.squint;

  lookX  = ease(lookX, tgtX, st.lookSpeed);
  lookY  = ease(lookY, tgtY, st.lookSpeed);
  openL  = ease(openL, tOpenL, 0.5f);
  openR  = ease(openR, tOpenR, 0.5f);
  sxL    = ease(sxL, st.sx * cL, 0.2f);
  sxR    = ease(sxR, st.sx * cR, 0.2f);
  syL    = ease(syL, st.sy * cL, 0.2f);
  syR    = ease(syR, st.sy * cR, 0.2f);
  squint = ease(squint, tSquint, 0.2f);
  sadA   = ease(sadA, st.sad, 0.15f);
  angryA = ease(angryA, st.angry, 0.15f);
  heartA = ease(heartA, st.heart, 0.15f);
  radius = ease(radius, st.radius, 0.2f);
}

// ================== DRAWING ==================
void drawCentered(const char* s, int y) { u8g2.drawStr(64 - u8g2.getStrWidth(s) / 2, y, s); }

void drawInfoScreen() {
  u8g2.setFont(u8g2_font_6x10_tr);
  drawCentered("Join this WiFi:", 14);
  u8g2.setFont(u8g2_font_helvB10_tr);
  drawCentered(ssid, 34);
  u8g2.setFont(u8g2_font_6x10_tr);
  drawCentered("page opens itself", 52);
  drawCentered("or 192.168.4.1", 63);
}

void drawOverlays(unsigned long now, int eyeY) {
  // sleepy Zzz
  if (mood == SLEEPY) {
    int k = (now / 400) % 6;
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(108 + k, 16 - k, "z");
    if (k > 2) u8g2.drawStr(116 + k / 2, 9 - k / 2, "z");
  }

  // sad tear rolling down from the left eye
  if (mood == SAD) {
    unsigned long t = now - tearStart;
    int ty = eyeY + 12 + (int)(t * 0.03f);
    if (t < 1500 && ty < 61) {
      int tx = constrain(CX - GAP / 2 - EYE_W + 6 + (int)lookX, 3, 124);
      u8g2.drawDisc(tx, ty, 2);
      u8g2.drawPixel(tx, ty - 3);
    }
  }

  // wifi icon top-left while control mode is on
  if (apOn) {
    u8g2.drawCircle(7, 9, 6, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
    u8g2.drawCircle(7, 9, 3, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
    u8g2.drawPixel(7, 9);
  }

  // toast pill at the bottom
  if (now < toastUntil && toastMsg[0]) {
    u8g2.setFont(u8g2_font_6x10_tr);
    int w = min(128, (int)u8g2.getStrWidth(toastMsg) + 8);
    int bx = 64 - w / 2;
    u8g2.setDrawColor(1);
    u8g2.drawRBox(bx, 51, w, 13, 3);
    u8g2.setDrawColor(0);
    u8g2.drawStr(bx + 4, 61, toastMsg);
    u8g2.setDrawColor(1);
  }
}

void drawFrame(unsigned long now) {
  u8g2.clearBuffer();
  if (now < infoUntil) {
    drawInfoScreen();
  } else {
    int y  = CY + (int)lookY + (int)hopOffset(now);
    int lx = CX - GAP / 2 - EYE_W / 2 + (int)lookX;
    int rx = CX + GAP / 2 + EYE_W / 2 + (int)lookX;
    drawEye(lx, y, openL, sxL, syL, true);
    drawEye(rx, y, openR, sxR, syR, false);
    drawOverlays(now, y);
  }
  u8g2.sendBuffer();
}

void splash() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_helvB12_tr);
  drawCentered("Coco Emote", 36);
  u8g2.setFont(u8g2_font_6x10_tr);
  drawCentered("hi there!", 54);
  u8g2.sendBuffer();
  delay(1200);
}

// ================== SETUP / LOOP ==================
void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(80);            // battery-saving speed (WiFi bumps it to 160)
  WiFi.mode(WIFI_OFF);
  randomSeed(esp_random());

#if HAS_BUTTON
  pinMode(PIN_BTN, INPUT_PULLUP);
  if (digitalRead(PIN_BTN) == LOW) ignoreRelease = true;   // woke up from a button press
#endif

  uint64_t mac = ESP.getEfuseMac();
  snprintf(ssid, sizeof(ssid), "Coco-Emote-%04X", (uint16_t)(mac >> 32));

  prefs.begin("coco", false);
  mood = (Mood)prefs.getUChar("mood", IDLE);
  if (mood >= MOOD_COUNT) mood = IDLE;
  brightness = prefs.getUChar("bright", 160);

  u8g2.begin();
  u8g2.setBusClock(400000);
  u8g2.setContrast(brightness);

  setupRoutes();
  splash();
#if !HAS_BUTTON
  startWiFi();                       // no button: control page is ready right after power-on
#endif
  nextBlink = millis() + 1500;
  nextAction = millis() + 4000;
}

void loop() {
  unsigned long now = millis();

  if (apOn) {
    dns.processNextRequest();
    server.handleClient();
    // read millis() fresh: a phone request above may have just updated lastWeb,
    // and "now" from the top of loop() would be older -> unsigned wrap -> instant WiFi OFF (old bug)
    if (millis() - lastWeb > WIFI_IDLE_OFF_MS) { stopWiFi(); toast("WiFi OFF"); }
  }
  if (sleepRequested) { delay(300); goToSleep(); }

#if HAS_BUTTON
  handleButton(now);
#endif
  updateBehaviour(now);
  updateAnimation(now);
  drawFrame(now);

  delay(16);
}
