/*
 * PomoSense for M5Cardputer
 * 
 * Features:
 * - Monica flip-clock style digit cards
 * - Breathing progress bar
 * - Gradient background (cool→warm based on time)
 * - 3 background modes: None / Rain / Matrix (B key to switch)
 * - Completion animation (expanding rings)
 * - Auto-continuous timer (focus→break→focus)
 * - WiFi NTP time sync
 * - Pause/Resume (Space key)
 * - Low battery warning
 * 
 * Directions: UP=25min, DOWN=60min, LEFT=5min, RIGHT=15min
 * Keys: Q/7=UP, W/9=DOWN, E/1=LEFT, R/3=RIGHT
 *       Space=Pause, Ctrl=Quit, `=Reset, B=Background, F=Auto, T=Help
 *       Enter=WiFi Setup
 * 
 * Based on TomatoClock_M5Cardputer by Faisal F Rafat
 * Enhanced by Li Chunlei
 * 
 * SPDX-License-Identifier: MIT
 */

#include "M5Cardputer.h"
#include "M5GFX.h"
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>

// ========== Configuration ==========
const int NTP_TZ_OFFSET = 8;  // Timezone offset (8 = UTC+8 / CST)
Preferences prefs;

struct TimerConfig {
    int duration;
    uint16_t minColor;
    uint16_t secColor;
    uint16_t barColor;
    uint16_t bgStart;
    uint16_t bgEnd;
};

// Unified colors: minutes = Morandi yellow, seconds = Morandi blue
const uint16_t MIN_COLOR = 0xB5A6;  // Morandi yellow
const uint16_t SEC_COLOR = 0x6B5D;  // Morandi blue
const uint16_t STATUS_COLOR = 0x8D51;  // Morandi sage green for status indicators (WiFi, battery)

TimerConfig timers[] = {
    {25*60, MIN_COLOR, SEC_COLOR, 0x4A3D, 0x0000, 0x0000},
    {60*60, MIN_COLOR, SEC_COLOR, 0x6A68, 0x0000, 0x0000},
    {5*60,  MIN_COLOR, SEC_COLOR, 0x8A7A, 0x0000, 0x0000},
    {15*60, MIN_COLOR, SEC_COLOR, 0x4A7A, 0x0000, 0x0000},
};

enum Direction { NONE, UP, DOWN, LEFT, RIGHT };
enum TimerState { IDLE, RUNNING, PAUSED, FINISHED, ANIMATING };
enum BgMode { BG_NONE = 0, BG_RAIN = 1, BG_MATRIX = 2, BG_COUNT = 3 };

Direction selectedDirection = NONE;
Direction currentOrientation = NONE;
TimerState state = IDLE;
BgMode bgMode = BG_MATRIX;
int currentTimerIndex = -1;
unsigned long startTime = 0;
int totalDuration = 0;
unsigned long pauseStartTime = 0;
unsigned long totalPausedTime = 0;
bool isPaused = false;

char prevDigits[4] = {'\0', '\0', '\0', '\0'};
bool firstDraw = true;

unsigned long animStartTime = 0;
const int ANIM_DURATION = 4000;

bool autoContinue = false;
int pomodoroCount = 0;
const int POMODORO_CYCLE = 4;
bool showHelp = false;

bool wifiConnected = false;
bool ntpSynced = false;
unsigned long lastNtpSync = 0;
const unsigned long NTP_INTERVAL = 3600000;

// ========== WiFi Setup ==========
enum WifiSetupState { WIFI_SETUP_IDLE, WIFI_SETUP_SSID, WIFI_SETUP_PASS, WIFI_SETUP_CONNECTING };
WifiSetupState wifiSetupState = WIFI_SETUP_IDLE;
String wifiInputSsid = "";
String wifiInputPass = "";
bool wifiSetupShowPass = false;
bool idleLocked = false;  // When true, stay on idle screen until ` is pressed

// ========== Rain Particles ==========
struct Particle {
    int x, y, speed, len;
    uint16_t color;
};
#define MAX_PARTICLES 20
Particle particles[MAX_PARTICLES];
bool particlesInited = false;

// ========== Matrix Rain ==========
#define MAX_MATRIX_COLS 30   // max 240 / 8
#define MAX_MATRIX_ROWS 30   // max 240 / 8
int matrixCols = 30;
int matrixRows = 17;
int matrixY[MAX_MATRIX_COLS];       // head Y position (pixel)
int matrixLen[MAX_MATRIX_COLS];     // trail length (pixel)
int matrixSpeed[MAX_MATRIX_COLS];   // speed (pixel per frame)
char matrixChars[MAX_MATRIX_COLS][MAX_MATRIX_ROWS]; // current chars per column
bool matrixInited = false;
int matrixScreenW = 0, matrixScreenH = 0;

const uint16_t CARD_BG   = 0x2945;
const uint16_t CARD_FLIP = 0x18C3;
const uint16_t DOT_COLOR = 0x632C;
const float ORIENTATION_THRESHOLD = 0.4;

// ========== Color Utilities ==========
uint16_t lerpColor565(uint16_t c1, uint16_t c2, float t) {
    t = constrain(t, 0.0, 1.0);
    uint8_t r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
    uint8_t r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;
    uint8_t r = r1 + (r2 - r1) * t, g = g1 + (g2 - g1) * t, b = b1 + (b2 - b1) * t;
    return (r << 11) | (g << 5) | b;
}

uint16_t dimColor565(uint16_t c, float brightness) {
    brightness = constrain(brightness, 0.0, 1.0);
    uint8_t r = ((c >> 11) & 0x1F) * brightness;
    uint8_t g = ((c >> 5) & 0x3F) * brightness;
    uint8_t b = (c & 0x1F) * brightness;
    return (r << 11) | (g << 5) | b;
}

// ========== Orientation Detection ==========
Direction detectOrientation() {
    auto data = M5.Imu.getImuData();
    float ax = data.accel.x, ay = data.accel.y, az = data.accel.z;
    float absX = fabs(ax), absY = fabs(ay), absZ = fabs(az);
    
    if (absZ > absX && absZ > absY && absZ > ORIENTATION_THRESHOLD) {
        if (ay > 0.3) return UP;
        else if (ay < -0.3) return DOWN;
    } else if (absX > absY && absX > ORIENTATION_THRESHOLD) {
        if (ax > 0.3) return LEFT;
        else if (ax < -0.3) return RIGHT;
    } else if (absY > absX && absY > ORIENTATION_THRESHOLD) {
        if (ay > 0.3) return UP;
        else if (ay < -0.3) return DOWN;
    }
    return NONE;
}

void setDisplayRotation(Direction dir) {
    switch (dir) {
        case UP:    M5Cardputer.Display.setRotation(1); break;
        case DOWN:  M5Cardputer.Display.setRotation(3); break;
        case LEFT:  M5Cardputer.Display.setRotation(2); break;
        case RIGHT: M5Cardputer.Display.setRotation(0); break;
        default:    M5Cardputer.Display.setRotation(1); break;
    }
}

// ========== WiFi / NTP ==========
void connectWiFiWithCreds(const char* ssid, const char* pass) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) { delay(500); attempts++; }
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        configTime(NTP_TZ_OFFSET * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");
        ntpSynced = true;
        lastNtpSync = millis();
    }
}

void connectWiFi() {
    prefs.begin("tomato", true);  // read-only
    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    prefs.end();
    if (ssid.length() > 0) {
        connectWiFiWithCreds(ssid.c_str(), pass.c_str());
    }
}

void saveWiFiCreds(const char* ssid, const char* pass) {
    prefs.begin("tomato", false);  // read-write
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
}

void checkNtpSync() {
    if (!wifiConnected) return;
    if (millis() - lastNtpSync > NTP_INTERVAL) {
        configTime(NTP_TZ_OFFSET * 3600, 0, "ntp.aliyun.com", "pool.ntp.org");
        lastNtpSync = millis();
    }
}

String getCurrentTimeStr() {
    if (!ntpSynced) return "";
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 100)) return "";
    char buf[9];
    strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
    return String(buf);
}

void getCurrentTimeHMS(int &hh, int &mm, int &ss) {
    hh = mm = ss = 0;
    if (!ntpSynced) return;
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 100)) return;
    hh = timeinfo.tm_hour;
    mm = timeinfo.tm_min;
    ss = timeinfo.tm_sec;
}

// ========== Rain Particles (no trail, behind cards) ==========
void initParticles(int w, int h) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particles[i].x = random(0, w);
        particles[i].y = random(-h, 0);
        particles[i].speed = random(3, 7);
        particles[i].len = random(4, 10);
        particles[i].color = dimColor565(0x6B5D, random(3, 7) / 10.0);
    }
    particlesInited = true;
}

void updateRainParticles() {
    auto& disp = M5Cardputer.Display;
    int w = disp.width();
    int h = disp.height();
    if (!particlesInited) initParticles(w, h);
    
    for (int i = 0; i < MAX_PARTICLES; i++) {
        // Erase old position with black (skip if hits card zone)
        if (!vLineHitsCardZone(particles[i].x, particles[i].y, particles[i].len)) {
            disp.drawFastVLine(particles[i].x, particles[i].y, particles[i].len, BLACK);
        }
        
        particles[i].y += particles[i].speed;
        
        if (particles[i].y > h) {
            particles[i].x = random(0, w);
            particles[i].y = random(-20, -5);
            particles[i].speed = random(3, 7);
            particles[i].len = random(4, 10);
        }
        
        // Skip drawing if particle hits card zone
        if (vLineHitsCardZone(particles[i].x, particles[i].y, particles[i].len)) continue;
        
        // Draw new position
        disp.drawFastVLine(particles[i].x, particles[i].y, particles[i].len, particles[i].color);
    }
}

// ========== Matrix Rain (behind cards, no trail on screen) ==========
char randomMatrixChar() {
    // Mix of digits, letters, symbols for hacker aesthetic
    const char chars[] = "0123456789ABCDEFabcdef!@#$%^&*<>{}[]|/\\~:;+=?";
    return chars[random(sizeof(chars) - 1)];
}

void initMatrixRain() {
    auto& disp = M5Cardputer.Display;
    matrixScreenW = disp.width();
    matrixScreenH = disp.height();
    matrixCols = matrixScreenW / 8;
    matrixRows = matrixScreenH / 8;
    if (matrixCols > MAX_MATRIX_COLS) matrixCols = MAX_MATRIX_COLS;
    if (matrixRows > MAX_MATRIX_ROWS) matrixRows = MAX_MATRIX_ROWS;
    
    for (int c = 0; c < matrixCols; c++) {
        matrixY[c] = random(-matrixScreenH, 0);
        matrixLen[c] = random(40, matrixScreenH);
        matrixSpeed[c] = random(2, 4);
        for (int r = 0; r < matrixRows; r++) {
            matrixChars[c][r] = randomMatrixChar();
        }
    }
    matrixInited = true;
}

void updateMatrixRain() {
    auto& disp = M5Cardputer.Display;
    int w = disp.width();
    int h = disp.height();
    
    if (!matrixInited || w != matrixScreenW || h != matrixScreenH) initMatrixRain();
    
    disp.setFont(&fonts::Font0);
    disp.setTextSize(1);
    
    for (int c = 0; c < matrixCols; c++) {
        int x = c * 8;
        int headY = matrixY[c];
        int prevHeadY = headY - matrixSpeed[c];
        
        // Erase old head position (draw black over previous bright char)
        if (prevHeadY >= 0 && prevHeadY < h) {
            int prevRow = prevHeadY / 8;
            if (prevRow >= 0 && prevRow < matrixRows) {
                if (!isInCardZone(x, prevRow * 8)) {
                    disp.setTextColor(BLACK);
                    disp.setCursor(x, prevRow * 8);
                    disp.print(matrixChars[c][prevRow]);
                }
            }
        }
        
        // Draw dim trail characters (2-3 chars behind head)
        for (int trail = 1; trail <= 3; trail++) {
            int trailY = headY - trail * 8;
            if (trailY >= 0 && trailY < h) {
                int trailRow = trailY / 8;
                if (trailRow >= 0 && trailRow < matrixRows) {
                    if (isInCardZone(x, trailRow * 8)) continue;
                    float dimFactor = (1.0 - trail * 0.3) * 0.5;
                    uint16_t trailColor = dimColor565(0x07E0, dimFactor);
                    disp.setTextColor(trailColor);
                    disp.setCursor(x, trailRow * 8);
                    disp.print(matrixChars[c][trailRow]);
                }
            }
        }
        
        // Erase tail (beyond trail length)
        int tailY = headY - 4 * 8;
        if (tailY >= 0 && tailY < h) {
            int tailRow = tailY / 8;
            if (tailRow >= 0 && tailRow < matrixRows) {
                if (!isInCardZone(x, tailRow * 8)) {
                    disp.setTextColor(BLACK);
                    disp.setCursor(x, tailRow * 8);
                    disp.print(matrixChars[c][tailRow]);
                }
            }
        }
        
        // Draw bright head character
        if (headY >= 0 && headY < h) {
            int headRow = headY / 8;
            if (headRow >= 0 && headRow < matrixRows) {
                // Randomly change character for flickering effect
                if (random(100) < 20) {
                    matrixChars[c][headRow] = randomMatrixChar();
                }
                // Skip if in card zone
                if (!isInCardZone(x, headRow * 8)) {
                    // Head is dim white-green (50% brightness)
                    disp.setTextColor(0x5FE0);
                    disp.setCursor(x, headRow * 8);
                    disp.print(matrixChars[c][headRow]);
                }
            }
        }
        
        // Update position
        matrixY[c] += matrixSpeed[c];
        
        // Reset column when fully off screen
        if (matrixY[c] - matrixLen[c] > h) {
            matrixY[c] = random(-40, -8);
            matrixLen[c] = random(40, h);
            matrixSpeed[c] = random(2, 4);
            // Refresh chars
            for (int r = 0; r < matrixRows; r++) {
                matrixChars[c][r] = randomMatrixChar();
            }
        }
    }
}

// ========== Background Dispatcher ==========
void updateBackground() {
    switch (bgMode) {
        case BG_RAIN:    updateRainParticles(); break;
        case BG_MATRIX:  updateMatrixRain(); break;
        case BG_NONE:    break;
    }
}

// ========== Gradient Background ==========
void drawGradientBg(int w, int h, uint16_t colorStart, uint16_t colorEnd, float progress) {
    auto& disp = M5Cardputer.Display;
    float t = progress * 0.3;
    for (int y = 0; y < h; y++) {
        float rowT = (float)y / h;
        uint16_t c = lerpColor565(colorStart, colorEnd, rowT * t);
        disp.drawFastHLine(0, y, w, c);
    }
}

// ========== Breathing Progress Bar ==========
void drawBreathingProgressBar(float progress, uint16_t barColor) {
    auto& disp = M5Cardputer.Display;
    int w = disp.width(), h = disp.height(), bt = 4;
    float breath = 0.6 + 0.4 * sin(millis() / 800.0);
    uint16_t breathColor = dimColor565(barColor, breath);
    
    int perimeter = 2 * (w + h) - 4 * bt;
    int filledLength = progress * perimeter;
    
    if (filledLength > 0) { int seg = min(filledLength, w - bt); disp.fillRect(0, 0, seg, bt, breathColor); filledLength -= seg; }
    if (filledLength > 0) { int seg = min(filledLength, h - bt); disp.fillRect(w - bt, bt, bt, seg, breathColor); filledLength -= seg; }
    if (filledLength > 0) { int seg = min(filledLength, w - bt); disp.fillRect(w - bt - seg, h - bt, seg, bt, breathColor); filledLength -= seg; }
    if (filledLength > 0) { int seg = min(filledLength, h - 2 * bt); disp.fillRect(0, h - bt - seg, bt, seg, breathColor); }
}

// ========== Card exclusion zone ==========
// Cards area to protect from background effects
int cardZoneX = 0, cardZoneY = 0, cardZoneW = 0, cardZoneH = 0;
bool hasCardZone = false;

bool isInCardZone(int px, int py) {
    if (!hasCardZone) return false;
    return (px >= cardZoneX && px < cardZoneX + cardZoneW &&
            py >= cardZoneY && py < cardZoneY + cardZoneH);
}

// Check if a vertical line overlaps with card zone
bool vLineHitsCardZone(int x, int y, int len) {
    if (!hasCardZone) return false;
    if (x < cardZoneX || x >= cardZoneX + cardZoneW) return false;
    int lineEnd = y + len;
    int zoneEnd = cardZoneY + cardZoneH;
    return (y < zoneEnd && lineEnd > cardZoneY);
}

// ========== Digit Card ==========
void drawCardBg(int x, int y, int cardW, int cardH) {
    auto& disp = M5Cardputer.Display;
    disp.fillRoundRect(x, y, cardW, cardH, 5, CARD_BG);
    disp.drawFastHLine(x + 3, y + cardH / 2, cardW - 6, CARD_FLIP);
}

void drawDigitInCard(int x, int y, int cardW, int cardH, char digit, uint16_t digitColor, int index) {
    if (!firstDraw && prevDigits[index] == digit) return;
    prevDigits[index] = digit;
    
    auto& disp = M5Cardputer.Display;
    // Redraw card background to clear old digit segments
    disp.fillRoundRect(x, y, cardW, cardH, 5, CARD_BG);
    disp.drawFastHLine(x + 3, y + cardH / 2, cardW - 6, CARD_FLIP);
    
    disp.setTextDatum(middle_center);
    disp.setFont(&fonts::Font7);
    disp.setTextSize(1);
    disp.setTextColor(digitColor);
    char str[2] = {digit, '\0'};
    disp.drawString(str, x + cardW / 2, y + cardH / 2);
}

void drawColonDots(int centerX, int centerY) {
    auto& disp = M5Cardputer.Display;
    disp.fillCircle(centerX, centerY - 7, 3, DOT_COLOR);
    disp.fillCircle(centerX, centerY + 7, 3, DOT_COLOR);
}

// ========== Low Battery ==========
bool isLowBattery() {
    auto& power = M5Cardputer.Power;
    int level = power.getBatteryLevel();
    return (level >= 0 && level < 20);
}

void drawLowBatteryWarning(int w, int h) {
    auto& disp = M5Cardputer.Display;
    uint16_t blinkColor = (millis() / 1000) % 2 == 0 ? 0xF800 : BLACK;
    disp.setFont(&fonts::Font0);
    disp.setTextDatum(top_right);
    disp.setTextSize(1);
    disp.setTextColor(blinkColor);
    disp.drawString("LOW BAT", w - 6, 2);
}

// ========== Idle Screen ==========
// ========== Help Screen ==========
void drawHelpScreen() {
    M5Cardputer.Display.setRotation(1);
    auto& disp = M5Cardputer.Display;
    int w = disp.width(), h = disp.height();
    
    disp.fillScreen(BLACK);
    disp.setTextDatum(middle_center);
    disp.setFont(&fonts::Font2);
    disp.setTextColor(0x8410);
    disp.setTextSize(1.5);
    disp.drawString("Keys", w / 2, 10);
    
    disp.setFont(&fonts::Font0);
    disp.setTextSize(1);
    disp.setTextDatum(top_left);
    
    int x1 = 10, x2 = 50, y = 24, dy = 12;
    uint16_t keyColor = 0xB5A6;  // morandi orange for keys
    uint16_t descColor = 0x9B8D;  // morandi blue for descriptions
    
    // Column 1
    disp.setTextColor(keyColor);  disp.drawString("Q/7", x1, y);
    disp.setTextColor(descColor); disp.drawString("UP 25m", x2, y);
    y += dy;
    disp.setTextColor(keyColor);  disp.drawString("W/9", x1, y);
    disp.setTextColor(descColor); disp.drawString("DN 60m", x2, y);
    y += dy;
    disp.setTextColor(keyColor);  disp.drawString("E/1", x1, y);
    disp.setTextColor(descColor); disp.drawString("LT 5m", x2, y);
    y += dy;
    disp.setTextColor(keyColor);  disp.drawString("R/3", x1, y);
    disp.setTextColor(descColor); disp.drawString("RT 15m", x2, y);
    
    // Column 2
    int x3 = 130, x4 = 170;
    y = 24;
    disp.setTextColor(keyColor);  disp.drawString("SPC", x3, y);
    disp.setTextColor(descColor); disp.drawString("Pause", x4, y);
    y += dy;
    disp.setTextColor(keyColor);  disp.drawString("Ctrl", x3, y);
    disp.setTextColor(descColor); disp.drawString("Quit", x4, y);
    y += dy;
    disp.setTextColor(keyColor);  disp.drawString("`", x3, y);
    disp.setTextColor(descColor); disp.drawString("Reset", x4, y);
    y += dy;
    disp.setTextColor(keyColor);  disp.drawString("B", x3, y);
    disp.setTextColor(descColor); disp.drawString("BgMode", x4, y);
    y += dy;
    disp.setTextColor(keyColor);  disp.drawString("F", x3, y);
    disp.setTextColor(descColor); disp.drawString("Auto", x4, y);
    
    // Bottom
    disp.setTextDatum(middle_center);
    disp.setTextColor(0x632C);
    disp.drawString("Enter=WiFi  T to close", w / 2, h - 10);
}

// ========== WiFi Setup Screen ==========
void drawWifiSetupScreen() {
    M5Cardputer.Display.setRotation(1);
    auto& disp = M5Cardputer.Display;
    int w = disp.width(), h = disp.height();
    disp.fillScreen(BLACK);
    
    disp.setFont(&fonts::Font0);
    disp.setTextSize(1);
    disp.setTextDatum(top_left);
    
    uint16_t labelColor = 0xB5A6;  // morandi orange
    uint16_t valueColor = 0xFFFF;  // white
    uint16_t hintColor  = 0x632C;  // dim
    
    int y = 8;
    
    if (wifiSetupState == WIFI_SETUP_SSID) {
        disp.setTextColor(labelColor);
        disp.drawString("SSID:", 8, y);
        y += 14;
        disp.setTextColor(valueColor);
        String displayStr = wifiInputSsid;
        // Truncate to fit screen
        if (displayStr.length() > 26) displayStr = displayStr.substring(displayStr.length() - 26);
        disp.drawString(displayStr, 8, y);
        // Blinking cursor
        if ((millis() / 400) % 2 == 0) {
            int cursorX = 8 + disp.textWidth(displayStr);
            disp.drawString("_", cursorX, y);
        }
        y += 18;
        disp.setTextColor(hintColor);
        disp.drawString("Enter=OK  `=Cancel", 8, y);
    } else if (wifiSetupState == WIFI_SETUP_PASS) {
        disp.setTextColor(labelColor);
        disp.drawString("SSID:", 8, y);
        y += 14;
        disp.setTextColor(0x9B8D);  // dim for confirmed SSID
        String ssidDisplay = wifiInputSsid;
        if (ssidDisplay.length() > 26) ssidDisplay = ssidDisplay.substring(ssidDisplay.length() - 26);
        disp.drawString(ssidDisplay, 8, y);
        y += 18;
        disp.setTextColor(labelColor);
        disp.drawString("PASS:", 8, y);
        y += 14;
        disp.setTextColor(valueColor);
        String passDisplay = wifiSetupShowPass ? wifiInputPass : "";
        if (!wifiSetupShowPass) {
            for (unsigned int i = 0; i < wifiInputPass.length(); i++) passDisplay += '*';
        }
        if (passDisplay.length() > 26) passDisplay = passDisplay.substring(passDisplay.length() - 26);
        disp.drawString(passDisplay, 8, y);
        if ((millis() / 400) % 2 == 0) {
            int cursorX = 8 + disp.textWidth(passDisplay);
            disp.drawString("_", cursorX, y);
        }
        y += 18;
        disp.setTextColor(hintColor);
        disp.drawString("Enter=OK  S=Show  `=Back", 8, y);
    } else if (wifiSetupState == WIFI_SETUP_CONNECTING) {
        disp.setTextDatum(middle_center);
        disp.setTextColor(0xFFE0);
        disp.drawString("Connecting...", w / 2, h / 2 - 10);
        disp.setTextColor(0x9B8D);
        disp.drawString(wifiInputSsid, w / 2, h / 2 + 10);
    }
}

void handleWifiSetupInput() {
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
    auto keys = M5Cardputer.Keyboard.keysState();
    
    // Check for backtick (cancel/back) in any wifi setup state
    bool backtickPressed = false;
    for (auto c : keys.word) {
        if (c == '`') { backtickPressed = true; break; }
    }

    if (wifiSetupState == WIFI_SETUP_SSID) {
        // ` = cancel
        if (backtickPressed) {
            wifiSetupState = WIFI_SETUP_IDLE;
            drawIdleScreen();
            return;
        }
        // Enter = confirm SSID, move to password
        if (keys.enter) {
            if (wifiInputSsid.length() > 0) {
                wifiSetupState = WIFI_SETUP_PASS;
                wifiInputPass = "";
                drawWifiSetupScreen();
            }
            return;
        }
        // Backspace
        for (auto k : keys.hid_keys) {
            if (k == 0x2A) {  // HID Backspace
                if (wifiInputSsid.length() > 0) {
                    wifiInputSsid.remove(wifiInputSsid.length() - 1);
                    drawWifiSetupScreen();
                }
                return;
            }
        }
        // Character input
        for (auto c : keys.word) {
            if (c >= 32 && c <= 126 && wifiInputSsid.length() < 63) {
                wifiInputSsid += c;
            }
        }
        drawWifiSetupScreen();
    }
    else if (wifiSetupState == WIFI_SETUP_PASS) {
        // ` = back to SSID
        if (backtickPressed) {
            wifiSetupState = WIFI_SETUP_SSID;
            drawWifiSetupScreen();
            return;
        }
        // Enter = confirm and connect
        if (keys.enter) {
            wifiSetupState = WIFI_SETUP_CONNECTING;
            drawWifiSetupScreen();
            // Save and connect
            saveWiFiCreds(wifiInputSsid.c_str(), wifiInputPass.c_str());
            connectWiFiWithCreds(wifiInputSsid.c_str(), wifiInputPass.c_str());
            wifiSetupState = WIFI_SETUP_IDLE;
            drawIdleScreen();
            return;
        }
        // S = toggle show password
        for (auto c : keys.word) {
            if (c == 'S' || c == 's') {
                wifiSetupShowPass = !wifiSetupShowPass;
                drawWifiSetupScreen();
                return;
            }
        }
        // Backspace
        for (auto k : keys.hid_keys) {
            if (k == 0x2A) {
                if (wifiInputPass.length() > 0) {
                    wifiInputPass.remove(wifiInputPass.length() - 1);
                    drawWifiSetupScreen();
                }
                return;
            }
        }
        // Character input
        for (auto c : keys.word) {
            if (c >= 32 && c <= 126 && wifiInputPass.length() < 63) {
                wifiInputPass += c;
            }
        }
        drawWifiSetupScreen();
    }
}

// ========== Idle Screen ==========
char prevIdleDigits[6] = {'\0', '\0', '\0', '\0', '\0', '\0'};
bool idleFirstDraw = true;

void drawIdleClockCard(int x, int y, int cardW, int cardH, char digit, uint16_t digitColor, uint16_t cardBg, int index) {
    auto& disp = M5Cardputer.Display;
    if (!idleFirstDraw && prevIdleDigits[index] == digit) return;
    prevIdleDigits[index] = digit;
    
    // Redraw card background
    disp.fillRoundRect(x, y, cardW, cardH, 5, cardBg);
    disp.drawFastHLine(x + 3, y + cardH / 2, cardW - 6, CARD_FLIP);
    
    disp.setTextDatum(middle_center);
    disp.setFont(&fonts::Font7);
    disp.setTextSize(1);
    disp.setTextColor(digitColor);
    char str[2] = {digit, '\0'};
    disp.drawString(str, x + cardW / 2, y + cardH / 2);
}

void drawIdleScreen() {
    M5Cardputer.Display.setRotation(1);
    auto& disp = M5Cardputer.Display;
    int w = disp.width(), h = disp.height();
    
    disp.fillScreen(BLACK);
    idleFirstDraw = true;
    memset(prevIdleDigits, 0, sizeof(prevIdleDigits));
    hasCardZone = true;
}

void drawIdleScreenFrame() {
    auto& disp = M5Cardputer.Display;
    int w = disp.width(), h = disp.height();
    
    // Update Matrix background
    updateBackground();
    
    if (ntpSynced) {
        int hh, mm, ss;
        getCurrentTimeHMS(hh, mm, ss);
        char hStr[3], mStr[3], sStr[3];
        sprintf(hStr, "%02d", hh);
        sprintf(mStr, "%02d", mm);
        sprintf(sStr, "%02d", ss);
        
        // Card layout: 6 cards for HH:MM:SS
        int cardW = 32, cardH = 62;
        int cardGap = 4;
        int groupGap = 10;
        // [H0][gap][H1] [groupGap] [M0][gap][M1] [groupGap] [S0][gap][S1]
        int totalW = cardW * 6 + cardGap * 3 + groupGap * 2;
        int startX = (w - totalW) / 2;
        int startY = (h - cardH) / 2;
        
        // Set card exclusion zone (with padding to protect cards from background)
        cardZoneX = startX - 4;
        cardZoneY = startY - 4;
        cardZoneW = totalW + 8;
        cardZoneH = cardH + 8;
        
        int x0 = startX;
        int x1 = x0 + cardW + cardGap;
        int x2 = x1 + cardW + groupGap;
        int x3 = x2 + cardW + cardGap;
        int x4 = x3 + cardW + groupGap;
        int x5 = x4 + cardW + cardGap;
        
        uint16_t hourColor = MIN_COLOR;   // Morandi yellow
        uint16_t minColor  = MIN_COLOR;   // Morandi yellow
        uint16_t secColor  = SEC_COLOR;   // Morandi blue
        uint16_t hourCardBg = 0x3949;     // light yellow-tinted card bg
        uint16_t minCardBg  = 0x3949;     // light yellow-tinted card bg
        uint16_t secCardBg  = 0x3186;     // light blue-tinted card bg
        
        drawIdleClockCard(x0, startY, cardW, cardH, hStr[0], hourColor, hourCardBg, 0);
        drawIdleClockCard(x1, startY, cardW, cardH, hStr[1], hourColor, hourCardBg, 1);
        drawIdleClockCard(x2, startY, cardW, cardH, mStr[0], minColor, minCardBg, 2);
        drawIdleClockCard(x3, startY, cardW, cardH, mStr[1], minColor, minCardBg, 3);
        drawIdleClockCard(x4, startY, cardW, cardH, sStr[0], secColor, secCardBg, 4);
        drawIdleClockCard(x5, startY, cardW, cardH, sStr[1], secColor, secCardBg, 5);
        
        // Draw colon dots between groups
        int colon1X = x1 + cardW + groupGap / 2;
        int colon2X = x3 + cardW + groupGap / 2;
        int colonY = startY + cardH / 2;
        disp.fillCircle(colon1X, colonY - 10, 3, 0x8410);
        disp.fillCircle(colon1X, colonY + 10, 3, 0x8410);
        disp.fillCircle(colon2X, colonY - 10, 3, 0x8410);
        disp.fillCircle(colon2X, colonY + 10, 3, 0x8410);
        
        // WiFi indicator (Morandi sage green)
        disp.setFont(&fonts::Font0);
        disp.setTextDatum(top_left);
        disp.setTextSize(1);
        disp.setTextColor(STATUS_COLOR);
        disp.drawString("WiFi", 4, 2);

        // Battery percentage (top-right, same Morandi color)
        int batLevel = M5Cardputer.Power.getBatteryLevel();
        if (batLevel >= 0) {
            disp.setTextDatum(top_right);
            char batStr[8];
            sprintf(batStr, "%d%%", batLevel);
            disp.setTextColor(STATUS_COLOR);
            disp.drawString(batStr, w - 4, 2);
        }
    } else {
        // No WiFi - show setup prompt
        cardZoneX = 0; cardZoneY = 0; cardZoneW = 0; cardZoneH = 0;
        hasCardZone = false;
        disp.setTextDatum(middle_center);
        disp.setFont(&fonts::Font2);
        disp.setTextSize(1.5);
        disp.setTextColor(0x8410);
        disp.drawString("PomoSense", w / 2, h / 2 - 20);
        disp.setFont(&fonts::Font0);
        disp.setTextSize(1);
        disp.setTextColor(0xF800);
        disp.drawString("No WiFi", w / 2, h / 2);
        disp.setTextColor(0xB5A6);
        disp.drawString("Enter=Setup WiFi", w / 2, h / 2 + 14);

        // Battery percentage (top-right, Morandi sage green)
        int batLevel = M5Cardputer.Power.getBatteryLevel();
        if (batLevel >= 0) {
            disp.setTextDatum(top_right);
            disp.setFont(&fonts::Font0);
            disp.setTextSize(1);
            char batStr[8];
            sprintf(batStr, "%d%%", batLevel);
            disp.setTextColor(STATUS_COLOR);
            disp.drawString(batStr, w - 4, 2);
        }
    }
    
    // Low battery
    if (isLowBattery()) drawLowBatteryWarning(w, h);
    
    idleFirstDraw = false;
}

// ========== Timer Screen ==========
void drawTimerScreen(int remaining) {
    int minutes = remaining / 60;
    int seconds = remaining % 60;
    
    TimerConfig& config = timers[currentTimerIndex];
    auto& disp = M5Cardputer.Display;
    
    setDisplayRotation(selectedDirection);
    int w = disp.width(), h = disp.height();
    
    // 1. Black background (only on first draw)
    float timeProgress = 1.0 - (float)remaining / totalDuration;
    if (firstDraw) {
        disp.fillScreen(BLACK);
    }
    
    // 2. Background effect (behind cards) - skip when paused
    if (!isPaused) updateBackground();
    
    // 3. Breathing progress bar
    drawBreathingProgressBar(timeProgress, config.barColor);
    
    // 4. Digit cards (on top of background)
    char minStr[3], secStr[3];
    sprintf(minStr, "%02d", minutes);
    sprintf(secStr, "%02d", seconds);
    
    if (selectedDirection == UP || selectedDirection == DOWN) {
        int cardW = 44, cardH = 62, cardGap = 6, groupGap = 18;
        // Layout: [card][gap][card]  [groupGap]  [card][gap][card]
        int totalW = cardW * 4 + cardGap * 2 + groupGap;
        int startX = (w - totalW) / 2;
        int startY = (h - cardH) / 2;
        
        // Set card exclusion zone for background effects
        cardZoneX = startX - 2;
        cardZoneY = startY - 2;
        cardZoneW = totalW + 4;
        cardZoneH = cardH + 4;
        hasCardZone = true;
        
        int x0 = startX;
        int x1 = x0 + cardW + cardGap;
        int x2 = x1 + cardW + groupGap;
        int x3 = x2 + cardW + cardGap;
        
        // Draw card backgrounds only on first frame
        if (firstDraw) {
            drawCardBg(x0, startY, cardW, cardH);
            drawCardBg(x1, startY, cardW, cardH);
            drawCardBg(x2, startY, cardW, cardH);
            drawCardBg(x3, startY, cardW, cardH);
            drawColonDots(x1 + cardW + groupGap / 2, h / 2);
        }
        
        // Draw digits only when changed (redraws card bg if digit changes)
        drawDigitInCard(x0, startY, cardW, cardH, minStr[0], config.minColor, 0);
        drawDigitInCard(x1, startY, cardW, cardH, minStr[1], config.minColor, 1);
        drawDigitInCard(x2, startY, cardW, cardH, secStr[0], config.secColor, 2);
        drawDigitInCard(x3, startY, cardW, cardH, secStr[1], config.secColor, 3);
    } else {
        int cardW = 44, cardH = 62, cardGap = 8, rowGap = 18;
        int totalW = cardW * 2 + cardGap, totalH = cardH * 2 + rowGap;
        int startX = (w - totalW) / 2, startY = (h - totalH) / 2;
        
        // Set card exclusion zone for background effects
        cardZoneX = startX - 2;
        cardZoneY = startY - 2;
        cardZoneW = totalW + 4;
        cardZoneH = totalH + 4;
        hasCardZone = true;
        
        int x0 = startX;
        int x1 = x0 + cardW + cardGap;
        int secStartY = startY + cardH + rowGap;
        
        // Draw card backgrounds only on first frame
        if (firstDraw) {
            drawCardBg(x0, startY, cardW, cardH);
            drawCardBg(x1, startY, cardW, cardH);
            drawCardBg(x0, secStartY, cardW, cardH);
            drawCardBg(x1, secStartY, cardW, cardH);
        }
        
        // Draw digits only when changed
        drawDigitInCard(x0, startY, cardW, cardH, minStr[0], config.minColor, 0);
        drawDigitInCard(x1, startY, cardW, cardH, minStr[1], config.minColor, 1);
        drawDigitInCard(x0, secStartY, cardW, cardH, secStr[0], config.secColor, 2);
        drawDigitInCard(x1, secStartY, cardW, cardH, secStr[1], config.secColor, 3);
    }
    
    // 5. Pause indicator
    if (isPaused) {
        disp.setFont(&fonts::Font0);
        disp.setTextDatum(middle_center);
        disp.setTextSize(1);
        disp.setTextColor(0xFFE0);
        if ((millis() / 600) % 2 == 0) disp.drawString("PAUSED", w / 2, h - 12);
    }
    
    // 6. Pomodoro count
    if (pomodoroCount > 0) {
        disp.setFont(&fonts::Font0);
        disp.setTextDatum(top_left);
        disp.setTextSize(1);
        disp.setTextColor(0x8410);
        for (int i = 0; i < pomodoroCount && i < POMODORO_CYCLE; i++)
            disp.drawString("*", 4 + i * 8, 2);
    }
    
    // 7. Low battery
    if (isLowBattery()) drawLowBatteryWarning(w, h);
    
    firstDraw = false;
}

// ========== Completion Animation ==========
void drawCompletionAnimation() {
    auto& disp = M5Cardputer.Display;
    int w = disp.width(), h = disp.height();
    unsigned long elapsed = millis() - animStartTime;
    
    if (elapsed > ANIM_DURATION) {
        state = IDLE;
        selectedDirection = NONE;
        currentOrientation = NONE;
        firstDraw = true;
        hasCardZone = false;
        memset(prevDigits, 0, sizeof(prevDigits));
        
        if (autoContinue) {
            // Auto cycle: 25min focus -> 5min break -> 25min focus -> ...
            if (currentTimerIndex == 0) {
                // Just finished 25min focus, start 5min break
                startTimer(LEFT);  // LEFT = 5min
            } else if (currentTimerIndex == 2) {
                // Just finished 5min break, start 25min focus
                startTimer(UP);    // UP = 25min
            } else {
                // Other timer types: just go back to idle
                autoContinue = false;
                drawIdleScreen();
            }
            return;
        }
        drawIdleScreen();
        return;
    }
    
    float t = (float)elapsed / ANIM_DURATION;
    uint8_t brightness = 255 * (1.0 - t);
    uint16_t bgColor = (brightness >> 3) << 11 | (brightness >> 2) << 5 | (brightness >> 3);
    disp.fillScreen(bgColor);
    
    int cx = w / 2, cy = h / 2, maxR = max(w, h) / 2;
    for (int ring = 0; ring < 3; ring++) {
        float ringT = t - ring * 0.15;
        if (ringT < 0 || ringT > 1.0) continue;
        int r = ringT * maxR;
        uint16_t ringColor = dimColor565(timers[currentTimerIndex].minColor, 1.0 - ringT);
        disp.drawCircle(cx, cy, r, ringColor);
        if (r > 2) disp.drawCircle(cx, cy, r - 2, ringColor);
    }
    
    disp.setFont(&fonts::Font2);
    disp.setTextDatum(middle_center);
    disp.setTextSize(2);
    disp.setTextColor(dimColor565(WHITE, 1.0 - t * 0.5));
    disp.drawString("Done!", cx, cy);
    
    if (pomodoroCount > 0) {
        disp.setTextSize(1);
        disp.setTextColor(dimColor565(0xFFE0, 1.0 - t));
        disp.drawString(String(pomodoroCount) + "/4", cx, cy + 20);
    }
}

// ========== Timer Control ==========
void startTimer(Direction dir) {
    if (dir == NONE) return;
    idleLocked = false;
    selectedDirection = dir;
    switch (dir) {
        case UP:    currentTimerIndex = 0; break;
        case DOWN:  currentTimerIndex = 1; break;
        case LEFT:  currentTimerIndex = 2; break;
        case RIGHT: currentTimerIndex = 3; break;
        default: return;
    }
    totalDuration = timers[currentTimerIndex].duration;
    startTime = millis();
    totalPausedTime = 0;
    isPaused = false;
    state = RUNNING;
    firstDraw = true;
    particlesInited = false;
    matrixInited = false;
    memset(prevDigits, 0, sizeof(prevDigits));
}

void togglePause() {
    if (state == RUNNING) {
        isPaused = true;
        state = PAUSED;
        pauseStartTime = millis();
    } else if (state == PAUSED) {
        isPaused = false;
        state = RUNNING;
        totalPausedTime += millis() - pauseStartTime;
        firstDraw = true;
        memset(prevDigits, 0, sizeof(prevDigits));
    }
}

void playBeep() {
    M5Cardputer.Speaker.tone(800, 200); delay(250);
    M5Cardputer.Speaker.tone(1000, 200); delay(250);
    M5Cardputer.Speaker.tone(1200, 400); delay(500);
}

unsigned long orientationCooldownUntil = 0;

void checkOrientationForTimerStart() {
    if (millis() < orientationCooldownUntil) return;
    Direction newOri = detectOrientation();
    if (newOri != NONE && newOri != currentOrientation) {
        currentOrientation = newOri;
        startTimer(newOri);
    } else if (newOri == NONE) {
        currentOrientation = NONE;
    }
}

// ========== Setup ==========
void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    drawIdleScreen();
    connectWiFi();
    drawIdleScreen();
}

// ========== Main Loop ==========
void loop() {
    M5Cardputer.update();
    M5.Imu.update();
    if (wifiConnected) checkNtpSync();
    
    // Handle WiFi setup input
    if (wifiSetupState != WIFI_SETUP_IDLE) {
        handleWifiSetupInput();
        delay(50);
        return;
    }
    
    // Skip rendering when help screen is shown (only process keyboard)
    if (showHelp) {
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            auto keys = M5Cardputer.Keyboard.keysState();
            for (auto c : keys.word) {
                if (c == 0x09 || c == 'T' || c == 't' || c == '`') {
                    showHelp = false;
                    firstDraw = true;
                    memset(prevDigits, 0, sizeof(prevDigits));
                    if (state == IDLE) drawIdleScreen();
                    else { hasCardZone = false; }
                }
            }
        }
        delay(50);
        return;
    }
    
    if (state == ANIMATING) {
        drawCompletionAnimation();
        delay(30);
        return;
    }
    
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        auto keys = M5Cardputer.Keyboard.keysState();
        
        if (keys.space) { togglePause(); return; }
        
        // Enter = Start WiFi setup (when idle)
        if (keys.enter && state == IDLE) {
            wifiSetupState = WIFI_SETUP_SSID;
            wifiInputSsid = "";
            wifiInputPass = "";
            wifiSetupShowPass = false;
            drawWifiSetupScreen();
            return;
        }
        
        // Ctrl = Quit to idle screen (locked, no auto-orientation)
        if (keys.ctrl) {
            state = IDLE;
            selectedDirection = NONE;
            currentOrientation = NONE;
            isPaused = false;
            autoContinue = false;
            idleLocked = true;
            hasCardZone = false;
            firstDraw = true;
            particlesInited = false;
            matrixInited = false;
            memset(prevDigits, 0, sizeof(prevDigits));
            drawIdleScreen();
            return;
        }

        // ` = Start/Reset timer based on current orientation
        for (auto c : keys.word) {
            if (c == '`') {
                if (showHelp) {
                    showHelp = false;
                    firstDraw = true;
                    memset(prevDigits, 0, sizeof(prevDigits));
                    if (state == IDLE) drawIdleScreen();
                    else { hasCardZone = false; }
                } else if (state == IDLE && idleLocked) {
                    // Exit idle lock: detect orientation and start timer
                    idleLocked = false;
                    Direction newDir = detectOrientation();
                    if (newDir != NONE) {
                        startTimer(newDir);
                    }
                } else if (state == RUNNING || state == PAUSED) {
                    // Reset: detect current orientation and start corresponding timer
                    Direction newDir = detectOrientation();
                    if (newDir != NONE) {
                        startTimer(newDir);
                    } else {
                        // No clear orientation, restart with current direction
                        startTime = millis();
                        totalPausedTime = 0;
                        isPaused = false;
                        state = RUNNING;
                        firstDraw = true;
                        particlesInited = false;
                        matrixInited = false;
                        memset(prevDigits, 0, sizeof(prevDigits));
                    }
                }
                return;
            }
        }
        
        // TAB = Toggle help screen (0x09 or T key)
        for (auto c : keys.word) {
            if (c == 0x09 || c == 'T' || c == 't') {
                showHelp = !showHelp;
                if (showHelp) {
                    drawHelpScreen();
                } else {
                    firstDraw = true;
                    memset(prevDigits, 0, sizeof(prevDigits));
                    if (state == IDLE) drawIdleScreen();
                    else { hasCardZone = false; }
                }
                return;
            }
        }
        
        for (auto c : keys.word) {
            switch (c) {
                case 'B': case 'b':
                    bgMode = (BgMode)((bgMode + 1) % BG_COUNT);
                    particlesInited = false;
                    matrixInited = false;
                    firstDraw = true;
                    memset(prevDigits, 0, sizeof(prevDigits));
                    if (state == IDLE) drawIdleScreen();
                    return;
                case 'F': case 'f':
                    autoContinue = !autoContinue;
                    if (state == IDLE) drawIdleScreen();
                    return;
            }
        }
        
        for (auto c : keys.word) {
            switch (c) {
                case 'Q': case 'q': startTimer(UP); return;
                case 'W': case 'w': startTimer(DOWN); return;
                case 'E': case 'e': startTimer(LEFT); return;
                case 'R': case 'r': startTimer(RIGHT); return;
            }
        }
        for (auto c : keys.word) {
            if (c == '7') { startTimer(UP); return; }
            else if (c == '9') { startTimer(DOWN); return; }
            else if (c == '1') { startTimer(LEFT); return; }
            else if (c == '3') { startTimer(RIGHT); return; }
        }
    }
    
    if (state == IDLE) {
        if (!idleLocked) checkOrientationForTimerStart();
        static unsigned long lastIdleRefresh = 0;
        if (millis() - lastIdleRefresh > 1000) {
            lastIdleRefresh = millis();
            drawIdleScreenFrame();
        }
    }
    
    if (state == RUNNING) {
        int elapsed = (millis() - startTime - totalPausedTime) / 1000;
        int remaining = totalDuration - elapsed;
        if (remaining <= 0) {
            remaining = 0;
            state = ANIMATING;
            animStartTime = millis();
            playBeep();
        }
        drawTimerScreen(remaining);
    }
    
    if (state == PAUSED) {
        int elapsed = (pauseStartTime - startTime - totalPausedTime) / 1000;
        int remaining = totalDuration - elapsed;
        if (remaining < 0) remaining = 0;
        drawTimerScreen(remaining);
    }
    
    delay(50);
}
