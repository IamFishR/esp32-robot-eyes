#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <time.h>

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define OLED_ADDRESS 0x3C

#define FIRMWARE_VERSION "1.0.0"
#define GITHUB_USER      "IamFishR"
#define GITHUB_REPO      "esp32-robot-eyes"

const char* ssid     = "Airtel_ratn_1517";
const char* password = "air52651";

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---- Eye state ----
int pupilDX = 0, pupilDY = 0;
bool blinking      = false;
int  blinkFrame    = 0;

unsigned long lastBlink   = 0;
unsigned long lastLook    = 0;
unsigned long lastOtaCheck = 0;

// ---- Draw one eye ----
// cx,cy = centre   ew,eh = outer ellipse   pr = pupil radius   blinkPct 0..10
void drawEye(int cx, int cy, int ew, int eh, int pr, int blinkPct) {
  int h = eh - (eh * blinkPct / 10);
  if (h < 2) h = 2;

  display.fillEllipse(cx, cy, ew, h, WHITE);

  int py = cy + (pupilDY * h / eh);
  display.fillCircle(cx + pupilDX, py, pr, BLACK);

  // small highlight dot
  display.fillCircle(cx + pupilDX - 3, py - 3, 2, WHITE);
}

void drawFace(int blinkPct) {
  display.clearDisplay();
  drawEye(34, 28, 13, 10, 7, blinkPct);
  drawEye(94, 28, 13, 10, 7, blinkPct);
}

// ---- Time strip at bottom ----
void drawTime() {
  struct tm t;
  if (!getLocalTime(&t)) return;

  char buf[12];
  strftime(buf, sizeof(buf), "%H:%M:%S", &t);

  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(36, 54);
  display.print(buf);
}

// ---- OTA via GitHub releases ----
void checkForUpdate() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String url = "https://api.github.com/repos/"
               GITHUB_USER "/" GITHUB_REPO "/releases/latest";

  if (!https.begin(client, url)) return;
  https.addHeader("User-Agent", "ESP32");

  int code = https.GET();
  if (code != 200) { https.end(); return; }

  String body = https.getString();
  https.end();

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, body)) return;

  String tag = doc["tag_name"].as<String>();
  if (tag == String(FIRMWARE_VERSION)) return;  // already up to date

  // find .bin in assets
  String binUrl;
  for (JsonObject asset : doc["assets"].as<JsonArray>()) {
    String name = asset["name"].as<String>();
    if (name.endsWith(".bin")) {
      binUrl = asset["browser_download_url"].as<String>();
      break;
    }
  }
  if (binUrl.isEmpty()) return;

  // download & flash
  WiFiClientSecure dl;
  dl.setInsecure();
  HTTPClient dlHttp;
  if (!dlHttp.begin(dl, binUrl)) return;

  int dlCode = dlHttp.GET();
  if (dlCode != 200) { dlHttp.end(); return; }

  int len = dlHttp.getSize();
  if (!Update.begin(len)) { dlHttp.end(); return; }

  // show updating screen
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(10, 24);
  display.print("Updating firmware...");
  display.display();

  Update.writeStream(*dlHttp.getStreamPtr());
  dlHttp.end();

  if (Update.end(true)) ESP.restart();
}

// ---- Setup ----
void setup() {
  Serial.begin(115200);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED not found");
    while (1);
  }

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(20, 28);
  display.print("Connecting WiFi");
  display.display();

  WiFi.begin(ssid, password);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    // IST = UTC + 5h30m = 19800 s
    configTime(19800, 0, "pool.ntp.org", "time.google.com");
    delay(1500);
    checkForUpdate();
  }

  lastBlink    = millis();
  lastLook     = millis();
  lastOtaCheck = millis();
}

// ---- Loop ----
void loop() {
  unsigned long now = millis();

  // trigger blink
  if (!blinking && now - lastBlink > (unsigned long)random(3000, 6000)) {
    blinking   = true;
    blinkFrame = 0;
    lastBlink  = now;
  }

  // random look direction
  if (now - lastLook > (unsigned long)random(2000, 5000)) {
    pupilDX = random(-5, 6);
    pupilDY = random(-3, 4);
    lastLook = now;
  }

  // blink animation (frame 0-9)
  int blinkPct = 0;
  if (blinking) {
    blinkPct = (blinkFrame < 5) ? blinkFrame * 2 : (10 - blinkFrame) * 2;
    blinkFrame++;
    if (blinkFrame >= 10) blinking = false;
  }

  drawFace(blinkPct);
  drawTime();
  display.display();

  // OTA check every hour
  if (now - lastOtaCheck > 3600000UL) {
    if (WiFi.status() == WL_CONNECTED) checkForUpdate();
    lastOtaCheck = now;
  }

  delay(50);
}
