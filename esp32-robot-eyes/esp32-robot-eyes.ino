#include <Wire.h>
#include <WebServer.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
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

#define FIRMWARE_VERSION "1.1.0"
#define GITHUB_USER      "IamFishR"
#define GITHUB_REPO      "esp32-robot-eyes"

const char* ssid     = "Airtel_ratn_1517";
const char* password = "air52651";

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WebServer server(80);

// ── Eye geometry ──────────────────────────────────────────────────────────────
#define LEX  34   // left eye center X
#define REX  94   // right eye center X
#define EY   28   // eye center Y
#define EW   13   // eye horizontal radius
#define EH   10   // eye vertical radius
#define PR    7   // pupil radius

// ── Expressions ───────────────────────────────────────────────────────────────
enum Expression {
  EXPR_FRONT = 0, EXPR_NARROW, EXPR_WIDE, EXPR_CROSSED, EXPR_MIDDLE,
  EXPR_DOWN, EXPR_UP, EXPR_RIGHT, EXPR_RIGHT_DOWN, EXPR_RIGHT_UP,
  EXPR_LEFT, EXPR_LEFT_DOWN, EXPR_LEFT_UP,
  EXPR_CONFUSED1, EXPR_CONFUSED2,
  EXPR_CRY, EXPR_DISTRESSED, EXPR_GLARE, EXPR_MAD,
  EXPR_GLASSES, EXPR_SLEEP, EXPR_TIRED, EXPR_NIGHT,
  EXPR_UPPER_LIDS, EXPR_LOWER_LIDS, EXPR_BLANK,
  EXPR_WORD_HELLO, EXPR_WORD_BYE, EXPR_WORD_WHAT,
  EXPR_COUNT
};

const char* exprNames[] = {
  "FRONT","NARROW","WIDE","CROSSED","MIDDLE",
  "DOWN","UP","RIGHT","RIGHT_DOWN","RIGHT_UP",
  "LEFT","LEFT_DOWN","LEFT_UP",
  "CONFUSED1","CONFUSED2",
  "CRY","DISTRESSED","GLARE","MAD",
  "GLASSES","SLEEP","TIRED","NIGHT",
  "UPPER_LIDS","LOWER_LIDS","BLANK",
  "HELLO","BYE","WHAT"
};

// ── State ─────────────────────────────────────────────────────────────────────
Expression currentExpr  = EXPR_WORD_HELLO;
unsigned long exprStart = 0;
unsigned long exprDur   = 2000;

bool blinking           = false;
int  blinkFrame         = 0;
unsigned long lastBlink    = 0;
unsigned long lastOtaCheck = 0;

// ── Low-level helpers ─────────────────────────────────────────────────────────

void drawEyeStd(int cx, int cy, int ew, int eh, int pr,
                int pdx, int pdy, int blinkPct) {
  int h = eh - (eh * blinkPct / 10);
  if (h < 1) h = 1;
  display.fillEllipse(cx, cy, ew, h, WHITE);
  int py = cy + (pdy * h / eh);
  display.fillCircle(cx + pdx, py, pr, BLACK);
  display.fillCircle(cx + pdx - 2, py - 2, 2, WHITE);
}

void drawTear(int x, int y) {
  display.drawLine(x, y, x, y + 7, WHITE);
  display.drawPixel(x - 1, y + 5, WHITE);
  display.drawPixel(x + 1, y + 5, WHITE);
  display.drawPixel(x - 1, y + 6, WHITE);
  display.drawPixel(x + 1, y + 6, WHITE);
}

void drawCrescent(int cx, int cy, int r) {
  display.fillCircle(cx, cy, r + 1, WHITE);
  display.fillCircle(cx + r / 3 + 1, cy - r / 4, r, BLACK);
}

// ── Expression drawers ────────────────────────────────────────────────────────

void exprFront(int bp) {
  drawEyeStd(LEX, EY, EW, EH, PR, 0, 0, bp);
  drawEyeStd(REX, EY, EW, EH, PR, 0, 0, bp);
}

void exprNarrow(int bp) {
  drawEyeStd(LEX, EY, EW, 3, PR - 3, 0, 0, bp);
  drawEyeStd(REX, EY, EW, 3, PR - 3, 0, 0, bp);
}

void exprWide(int bp) {
  drawEyeStd(LEX, EY, EW + 4, EH + 4, PR + 2, 0, 0, bp);
  drawEyeStd(REX, EY, EW + 4, EH + 4, PR + 2, 0, 0, bp);
}

void exprCrossed(int bp) {
  drawEyeStd(LEX, EY, EW, EH, PR,  5, 0, bp);
  drawEyeStd(REX, EY, EW, EH, PR, -5, 0, bp);
}

void exprMiddle(int bp) {
  drawEyeStd(LEX, EY, EW, EH, PR,  3, 0, bp);
  drawEyeStd(REX, EY, EW, EH, PR, -3, 0, bp);
}

void exprDown(int bp) {
  drawEyeStd(LEX, EY, EW, EH, PR, 0, 4, bp);
  drawEyeStd(REX, EY, EW, EH, PR, 0, 4, bp);
}

void exprUp(int bp) {
  drawEyeStd(LEX, EY, EW, EH, PR, 0, -4, bp);
  drawEyeStd(REX, EY, EW, EH, PR, 0, -4, bp);
}

void exprRight(int bp) {
  drawEyeStd(LEX, EY, EW, EH, PR, 5, 0, bp);
  drawEyeStd(REX, EY, EW, EH, PR, 5, 0, bp);
}

void exprRightDown(int bp) {
  drawEyeStd(LEX, EY, EW, EH, PR, 4, 3, bp);
  drawEyeStd(REX, EY, EW, EH, PR, 4, 3, bp);
}

void exprRightUp(int bp) {
  drawEyeStd(LEX, EY, EW, EH, PR, 4, -3, bp);
  drawEyeStd(REX, EY, EW, EH, PR, 4, -3, bp);
}

void exprLeft(int bp) {
  drawEyeStd(LEX, EY, EW, EH, PR, -5, 0, bp);
  drawEyeStd(REX, EY, EW, EH, PR, -5, 0, bp);
}

void exprLeftDown(int bp) {
  drawEyeStd(LEX, EY, EW, EH, PR, -4, 3, bp);
  drawEyeStd(REX, EY, EW, EH, PR, -4, 3, bp);
}

void exprLeftUp(int bp) {
  drawEyeStd(LEX, EY, EW, EH, PR, -4, -3, bp);
  drawEyeStd(REX, EY, EW, EH, PR, -4, -3, bp);
}

void exprConfused1(int bp) {
  // Left eye looks up-left, right eye looks down-right
  drawEyeStd(LEX, EY, EW, EH, PR, -3, -4, bp);
  drawEyeStd(REX, EY, EW, EH, PR,  3,  4, bp);
}

void exprConfused2(int bp) {
  // One eye wide open, other eye squinting
  drawEyeStd(LEX, EY, EW + 3, EH + 3, PR + 1, -3, 0, bp);
  drawEyeStd(REX, EY, EW,     3,       PR - 4,  3, 0, bp);
}

void exprCry(int bp) {
  drawEyeStd(LEX, EY, EW, EH, PR, -2, 2, bp);
  drawEyeStd(REX, EY, EW, EH, PR,  2, 2, bp);
  drawTear(LEX - 3, EY + EH + 1);
  drawTear(LEX + 4, EY + EH + 2);
  drawTear(REX - 4, EY + EH + 2);
  drawTear(REX + 3, EY + EH + 1);
}

void exprDistressed(int bp) {
  drawEyeStd(LEX, EY, EW, 5, PR - 2,  3, 2, bp);
  drawEyeStd(REX, EY, EW, 5, PR - 2, -3, 2, bp);
  // sweat drops on outer sides
  for (int i = 0; i < 5; i++) {
    display.drawPixel(LEX - EW - 4, EY - 1 + i, WHITE);
    display.drawPixel(REX + EW + 4, EY - 1 + i, WHITE);
  }
  display.drawPixel(LEX - EW - 3, EY + 4, WHITE);
  display.drawPixel(REX + EW + 3, EY + 4, WHITE);
}

void exprGlare(int bp) {
  drawEyeStd(LEX, EY, EW, EH, PR, 0, 3, 0);
  drawEyeStd(REX, EY, EW, EH, PR, 0, 3, 0);
  // Mask top half of each eye — intense stare
  display.fillRect(LEX - EW, EY - EH, EW * 2 + 1, EH / 2 + 2, BLACK);
  display.fillRect(REX - EW, EY - EH, EW * 2 + 1, EH / 2 + 2, BLACK);
}

void exprMad(int bp) {
  drawEyeStd(LEX, EY, EW, EH, PR, 0, 2, 0);
  drawEyeStd(REX, EY, EW, EH, PR, 0, 2, 0);
  // Left eye: inner (right/nose) corner drops — angry V
  display.fillTriangle(LEX - EW - 1, EY - EH - 1,
                       LEX + EW + 1, EY - EH - 1,
                       LEX + EW + 1, EY - EH / 3, BLACK);
  // Right eye: inner (left/nose) corner drops
  display.fillTriangle(REX - EW - 1, EY - EH - 1,
                       REX + EW + 1, EY - EH - 1,
                       REX - EW - 1, EY - EH / 3, BLACK);
}

void exprGlasses(int bp) {
  drawEyeStd(LEX, EY, EW, EH, PR, 0, 0, bp);
  drawEyeStd(REX, EY, EW, EH, PR, 0, 0, bp);
  display.drawEllipse(LEX, EY, EW + 3, EH + 3, WHITE);
  display.drawEllipse(REX, EY, EW + 3, EH + 3, WHITE);
  // Bridge between lenses
  display.drawLine(LEX + EW + 3, EY, REX - EW - 3, EY, WHITE);
  // Side arms
  display.drawLine(LEX - EW - 3, EY - 1, LEX - EW - 8, EY + 4, WHITE);
  display.drawLine(REX + EW + 3, EY - 1, REX + EW + 8, EY + 4, WHITE);
}

void exprSleep(int bp) {
  // Bottom arc only — like closed "smile" eyes
  display.fillEllipse(LEX, EY + 2, EW, EH / 2 + 2, WHITE);
  display.fillRect(LEX - EW - 1, EY - EH - 1, EW * 2 + 3, EH + 4, BLACK);
  display.fillEllipse(REX, EY + 2, EW, EH / 2 + 2, WHITE);
  display.fillRect(REX - EW - 1, EY - EH - 1, EW * 2 + 3, EH + 4, BLACK);
  // ZZZ floating up
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(57, 20); display.print("z");
  display.setCursor(64, 13); display.print("z");
  display.setCursor(72, 6);  display.print("Z");
}

void exprTired(int bp) {
  drawEyeStd(LEX, EY, EW, EH, PR, 0, 3, 0);
  drawEyeStd(REX, EY, EW, EH, PR, 0, 3, 0);
  // Heavy drooping upper lids
  display.fillRect(LEX - EW, EY - EH, EW * 2 + 1, EH / 2 + 3, BLACK);
  display.fillRect(REX - EW, EY - EH, EW * 2 + 1, EH / 2 + 3, BLACK);
}

void exprNight(int bp) {
  drawCrescent(LEX, EY, EH);
  drawCrescent(REX, EY, EH);
}

void exprUpperLids(int bp) {
  drawEyeStd(LEX, EY, EW, EH, PR, 0, 0, bp);
  drawEyeStd(REX, EY, EW, EH, PR, 0, 0, bp);
  display.fillRect(LEX - EW, EY - EH, EW * 2 + 1, EH / 3 + 1, BLACK);
  display.fillRect(REX - EW, EY - EH, EW * 2 + 1, EH / 3 + 1, BLACK);
}

void exprLowerLids(int bp) {
  drawEyeStd(LEX, EY, EW, EH, PR, 0, 0, bp);
  drawEyeStd(REX, EY, EW, EH, PR, 0, 0, bp);
  display.fillRect(LEX - EW, EY + EH / 2, EW * 2 + 1, EH / 3 + 1, BLACK);
  display.fillRect(REX - EW, EY + EH / 2, EW * 2 + 1, EH / 3 + 1, BLACK);
}

void exprBlank(int bp) {
  // Pure white ellipses, no pupils — vacant stare
  display.fillEllipse(LEX, EY, EW, EH, WHITE);
  display.fillEllipse(REX, EY, EW, EH, WHITE);
}

void exprWordHello() {
  display.setTextSize(3);
  display.setTextColor(WHITE);
  display.setCursor(8, 18);
  display.print("HELLO");
}

void exprWordBye() {
  display.setTextSize(3);
  display.setTextColor(WHITE);
  display.setCursor(18, 18);
  display.print("BYE");
}

void exprWordWhat() {
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(20, 22);
  display.print("What?");
}

// ── Dispatcher ────────────────────────────────────────────────────────────────

void drawExpression(int blinkPct) {
  display.clearDisplay();
  switch (currentExpr) {
    case EXPR_FRONT:      exprFront(blinkPct);      break;
    case EXPR_NARROW:     exprNarrow(blinkPct);     break;
    case EXPR_WIDE:       exprWide(blinkPct);       break;
    case EXPR_CROSSED:    exprCrossed(blinkPct);    break;
    case EXPR_MIDDLE:     exprMiddle(blinkPct);     break;
    case EXPR_DOWN:       exprDown(blinkPct);       break;
    case EXPR_UP:         exprUp(blinkPct);         break;
    case EXPR_RIGHT:      exprRight(blinkPct);      break;
    case EXPR_RIGHT_DOWN: exprRightDown(blinkPct);  break;
    case EXPR_RIGHT_UP:   exprRightUp(blinkPct);    break;
    case EXPR_LEFT:       exprLeft(blinkPct);       break;
    case EXPR_LEFT_DOWN:  exprLeftDown(blinkPct);   break;
    case EXPR_LEFT_UP:    exprLeftUp(blinkPct);     break;
    case EXPR_CONFUSED1:  exprConfused1(blinkPct);  break;
    case EXPR_CONFUSED2:  exprConfused2(blinkPct);  break;
    case EXPR_CRY:        exprCry(blinkPct);        break;
    case EXPR_DISTRESSED: exprDistressed(blinkPct); break;
    case EXPR_GLARE:      exprGlare(blinkPct);      break;
    case EXPR_MAD:        exprMad(blinkPct);        break;
    case EXPR_GLASSES:    exprGlasses(blinkPct);    break;
    case EXPR_SLEEP:      exprSleep(blinkPct);      break;
    case EXPR_TIRED:      exprTired(blinkPct);      break;
    case EXPR_NIGHT:      exprNight(blinkPct);      break;
    case EXPR_UPPER_LIDS: exprUpperLids(blinkPct);  break;
    case EXPR_LOWER_LIDS: exprLowerLids(blinkPct);  break;
    case EXPR_BLANK:      exprBlank(blinkPct);      break;
    case EXPR_WORD_HELLO: exprWordHello();           break;
    case EXPR_WORD_BYE:   exprWordBye();             break;
    case EXPR_WORD_WHAT:  exprWordWhat();            break;
    default:              exprFront(blinkPct);       break;
  }
}

// ── Animation scheduler ───────────────────────────────────────────────────────

void pickNextExpression() {
  int r = random(100);

  if (r < 40) {
    // Look directions — most common
    const Expression looks[] = {
      EXPR_FRONT, EXPR_LEFT, EXPR_RIGHT, EXPR_UP, EXPR_DOWN,
      EXPR_LEFT_UP, EXPR_LEFT_DOWN, EXPR_RIGHT_UP, EXPR_RIGHT_DOWN, EXPR_MIDDLE
    };
    currentExpr = looks[random(10)];
    exprDur = random(1000, 3000);

  } else if (r < 60) {
    // Emotions
    const Expression emotions[] = {
      EXPR_MAD, EXPR_CRY, EXPR_GLARE, EXPR_DISTRESSED,
      EXPR_CONFUSED1, EXPR_CONFUSED2, EXPR_TIRED
    };
    currentExpr = emotions[random(7)];
    exprDur = random(2000, 4000);

  } else if (r < 80) {
    // Funny / special looks
    const Expression funny[] = {
      EXPR_GLASSES, EXPR_CROSSED, EXPR_BLANK, EXPR_NIGHT,
      EXPR_UPPER_LIDS, EXPR_LOWER_LIDS, EXPR_NARROW, EXPR_WIDE
    };
    currentExpr = funny[random(8)];
    exprDur = random(1500, 3000);

  } else if (r < 93) {
    // Word reactions
    const Expression words[] = { EXPR_WORD_HELLO, EXPR_WORD_BYE, EXPR_WORD_WHAT };
    currentExpr = words[random(3)];
    exprDur = 1800;

  } else {
    // Sleep — rare, held longer
    currentExpr = EXPR_SLEEP;
    exprDur = random(4000, 7000);
  }

  exprStart = millis();
}

// ── OTA via GitHub releases ───────────────────────────────────────────────────

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
  if (tag == String(FIRMWARE_VERSION)) return;

  String binUrl;
  for (JsonObject asset : doc["assets"].as<JsonArray>()) {
    String name = asset["name"].as<String>();
    if (name == "firmware.bin") {
      binUrl = asset["browser_download_url"].as<String>();
      break;
    }
  }
  if (binUrl.isEmpty()) return;

  WiFiClientSecure dl;
  dl.setInsecure();
  HTTPClient dlHttp;
  dlHttp.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  dlHttp.setTimeout(30000);
  if (!dlHttp.begin(dl, binUrl)) return;

  int dlCode = dlHttp.GET();
  if (dlCode != 200) { dlHttp.end(); return; }

  int len = dlHttp.getSize();
  if (!Update.begin(len > 0 ? len : UPDATE_SIZE_UNKNOWN)) { dlHttp.end(); return; }

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

// ── Setup ─────────────────────────────────────────────────────────────────────

void initVariant() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
}

void setup() {
  Serial.begin(115200);

  bool oledOk = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!oledOk) oledOk = display.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  if (!oledOk) Serial.println("OLED not found");

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
    configTime(19800, 0, "pool.ntp.org", "time.google.com");
    delay(1500);
    checkForUpdate();

    // Web UI — main page with expression list
    server.on("/", []() {
      String html = "<h2>ESP32 Robot Eyes v" FIRMWARE_VERSION "</h2>";
      html += "<p>IP: " + WiFi.localIP().toString() + "</p>";
      html += "<p>Expression: <b>" + String(exprNames[currentExpr]) + "</b></p>";
      html += "<h3>Trigger expression:</h3><ul>";
      for (int i = 0; i < EXPR_COUNT; i++) {
        html += "<li><a href='/expr?id=" + String(i) + "'>" + String(exprNames[i]) + "</a></li>";
      }
      html += "</ul>";
      html += "<br><a href='/update'>Check OTA Update</a>";
      server.send(200, "text/html", html);
    });

    server.on("/expr", []() {
      if (server.hasArg("id")) {
        int id = server.arg("id").toInt();
        if (id >= 0 && id < EXPR_COUNT) {
          currentExpr = (Expression)id;
          exprStart   = millis();
          exprDur     = 5000;
        }
      }
      server.sendHeader("Location", "/");
      server.send(302, "text/plain", "");
    });

    server.on("/update", []() {
      server.send(200, "text/plain", "Checking for update...");
      checkForUpdate();
    });

    server.begin();
    Serial.println("Web server: http://" + WiFi.localIP().toString());
  }

  // Start with HELLO greeting
  currentExpr  = EXPR_WORD_HELLO;
  exprStart    = millis();
  exprDur      = 2000;
  lastBlink    = millis();
  lastOtaCheck = millis();
}

// ── Loop ──────────────────────────────────────────────────────────────────────

void loop() {
  unsigned long now = millis();

  // Advance expression when timer expires
  if (now - exprStart > exprDur) {
    pickNextExpression();
  }

  // Blink (skip during sleep, night, and word expressions)
  bool canBlink = (currentExpr != EXPR_SLEEP &&
                   currentExpr != EXPR_NIGHT &&
                   currentExpr != EXPR_WORD_HELLO &&
                   currentExpr != EXPR_WORD_BYE &&
                   currentExpr != EXPR_WORD_WHAT);

  if (!blinking && canBlink && now - lastBlink > (unsigned long)random(3000, 6000)) {
    blinking   = true;
    blinkFrame = 0;
  }

  int blinkPct = 0;
  if (blinking) {
    blinkPct = (blinkFrame < 5) ? blinkFrame * 2 : (10 - blinkFrame) * 2;
    blinkFrame++;
    if (blinkFrame >= 10) {
      blinking  = false;
      lastBlink = now;
    }
  }

  drawExpression(blinkPct);
  display.display();

  server.handleClient();

  if (now - lastOtaCheck > 3600000UL) {
    if (WiFi.status() == WL_CONNECTED) checkForUpdate();
    lastOtaCheck = now;
  }

  delay(50);
}
