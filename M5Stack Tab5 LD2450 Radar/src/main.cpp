// PlatformIO only — Arduino IDE uses ld2450_tab5.ino directly.
#ifdef PLATFORMIO
/*
 * HLK LD2450 mmWave Radar Visualizer — M5Stack Tab5  v2  (Cyberpunk Edition)
 * ESP32-P4 · 1280×720 IPS · Arduino + M5Unified
 *
 * Wiring (GPIO_EXT 10-pin header, bottom edge of Tab5):
 *   EXT 5V → LD2450 VCC     GND → LD2450 GND
 *   G49    → LD2450 TX      G50 → LD2450 RX
 *   No level-shifter needed (LD2450 UART is 3.3 V even on 5 V supply).
 *
 * Controls:
 *   Tap radar area    — cycle zoom (2 m / 4 m / 6 m)
 *   Tap ≡ top-right   — open Settings menu
 *   Menu: IMU on/off, manual rotation, LD2450 config, sensor orientation
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <math.h>

// ════════════════════════════════════════════════════════════════════════════
//  UART
// ════════════════════════════════════════════════════════════════════════════
#define LD_RX_PIN  49
#define LD_TX_PIN  50
#define LD_BAUD    256000
static HardwareSerial radarSer(1);

// ════════════════════════════════════════════════════════════════════════════
//  PROTOCOL
// ════════════════════════════════════════════════════════════════════════════
#define MAX_TGTS   3
#define FRAME_LEN  30
static const uint8_t FHDR[4] = {0xAA, 0xFF, 0x03, 0x00};
static const uint8_t FFTR[2] = {0x55, 0xCC};
static const int32_t RANGES[3] = {2000, 4000, 6000};
#define RANGE_CNT  3

// ════════════════════════════════════════════════════════════════════════════
//  SCREEN LAYOUT  (landscape 1280 × 720)
// ════════════════════════════════════════════════════════════════════════════
#define SCR_W    1280
#define SCR_H    720
#define SB_H     108       // status bar at top
#define RDR_CX   640       // radar origin X (screen centre)
#define RDR_CY   715       // radar origin Y (near bottom — maximises arc height)
#define RDR_R    600       // max radius; arc top at y = RDR_CY - RDR_R = 115

// ════════════════════════════════════════════════════════════════════════════
//  CYBERPUNK PALETTE  (0xRRGGBB)
// ════════════════════════════════════════════════════════════════════════════
#define CP_BG          0x000A0A
#define CP_SB_BG       0x000F18
#define CP_RADAR_FILL  0x000E08
#define CP_ARC1        0x003322
#define CP_ARC2        0x006644
#define CP_ARC_OUT     0x00FF88
#define CP_SECTOR      0x002218
#define CP_BASELINE    0x008855
#define CP_FWD         0x00FFAA
#define CP_SWEEP_TIP   0x44FFD0
#define CP_SENSOR      0xFFFFFF
#define CP_TEXT_HI     0x00FFCC
#define CP_TEXT_MID    0x008877
#define CP_TEXT_ERR    0xFF3333
#define CP_MENU_BG     0x020E12
#define CP_MENU_BDR    0x00CCAA
#define CP_MENU_HL     0x00FFCC
#define CP_MENU_ITEM   0x009980
#define CP_MENU_DIM    0x1A3838
#define CP_ALERT       0xFF5500

static const uint32_t C_TGT[3] = {0xFF00FF, 0xFFCC00, 0x00FFFF};

// ════════════════════════════════════════════════════════════════════════════
//  SWEEP ANIMATION
// ════════════════════════════════════════════════════════════════════════════
#define SWEEP_DPS   90.0f    // degrees/second → 4-second full rotation
#define TRAIL_SEGS  14
#define TRAIL_DEG   70.0f

// ════════════════════════════════════════════════════════════════════════════
//  MENU GEOMETRY
// ════════════════════════════════════════════════════════════════════════════
#define MNU_X         860
#define MNU_Y         (SB_H + 5)
#define MNU_W         410
#define MNU_IH        68     // item row height
#define MNU_PAD       18
#define MNU_ITEM_Y0   (MNU_Y + MNU_PAD + 46)  // y of first item row

#define MBTN_X        (SCR_W - 88)
#define MBTN_Y        8
#define MBTN_W        76
#define MBTN_H        (SB_H - 16)

// ════════════════════════════════════════════════════════════════════════════
//  TYPES
// ════════════════════════════════════════════════════════════════════════════
struct Target { int16_t x, y, speed; bool present; };

enum SensorOrient { OR_NORMAL=0, OR_MIRROR, OR_CW90, OR_CCW90 };
static const char* OR_LABELS[4] = {"Standard","X-Mirror","90 CW","90 CCW"};

enum MenuPage { MP_CLOSED=0, MP_MAIN, MP_LD2450, MP_ORIENT };

// ════════════════════════════════════════════════════════════════════════════
//  GLOBALS
// ════════════════════════════════════════════════════════════════════════════

// --- shared target data (spinlock protected) ---
static Target       tgts[MAX_TGTS];
static portMUX_TYPE tgts_mux   = portMUX_INITIALIZER_UNLOCKED;
static uint32_t     lastFrameMs = 0;

// --- smoothed position & sweep brightness (main-loop only, no lock needed) ---
static float smoothX[MAX_TGTS]     = {};
static float smoothY[MAX_TGTS]     = {};
static float sweepBright[MAX_TGTS] = {};
static bool  hadPresent[MAX_TGTS]  = {};

// --- frame parser ---
static uint8_t  fb[FRAME_LEN];
static uint8_t  hm  = 0;
static uint16_t fp  = 0;
static bool     inf = false;

// --- app state ---
static uint8_t      rangeIdx    = RANGE_CNT - 1;
static SensorOrient sensorOrient = OR_NORMAL;
static bool         imuEnabled  = true;
static int          manualRot   = 1;
static bool         multiTarget = true;
static MenuPage     menuPage    = MP_CLOSED;

// --- animation & timing ---
static float    sweepAngle  = 270.0f;  // M5GFX degrees (0=top CW)
static uint32_t lastAnimMs  = 0;
static uint32_t lastImuMs   = 0;
static uint32_t lastTouchMs = 0;
static uint32_t lastDrawMs  = 0;

static M5Canvas cv(&M5.Display);

// ════════════════════════════════════════════════════════════════════════════
//  COORDINATE DECODE & ORIENTATION
// ════════════════════════════════════════════════════════════════════════════

static int16_t decodeCoord(uint16_t raw) {
    return (raw & 0x8000) ? (int16_t)(raw & 0x7FFF) : -(int16_t)raw;
}

static void applyOrient(float sx, float sy, float& dx, float& dy) {
    switch (sensorOrient) {
        case OR_MIRROR: dx = -sx; dy =  sy; break;
        case OR_CW90:   dx = -sy; dy =  sx; break;
        case OR_CCW90:  dx =  sy; dy =  sx; break;
        default:        dx =  sx; dy =  sy; break;
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  LD2450 CONFIGURATION COMMANDS
//  Per HLK-LD2450 protocol: FD FC FB FA [len LE] [cmd+data] 04 03 02 01
// ════════════════════════════════════════════════════════════════════════════

static void ld2450Send(const uint8_t* data, size_t len) {
    static const uint8_t H[4] = {0xFD,0xFC,0xFB,0xFA};
    static const uint8_t T[4] = {0x04,0x03,0x02,0x01};
    uint8_t lb[2] = {(uint8_t)(len & 0xFF), (uint8_t)(len >> 8)};
    radarSer.write(H, 4);
    radarSer.write(lb, 2);
    radarSer.write(data, len);
    radarSer.write(T, 4);
    delay(80);
}

static void ld2450EnterCfg() {
    const uint8_t c[] = {0xFF,0x00,0x01,0x00}; ld2450Send(c, 4);
}
static void ld2450ExitCfg() {
    const uint8_t c[] = {0xFE,0x00}; ld2450Send(c, 2);
}
static void ld2450SetTrackMode(bool multi) {
    ld2450EnterCfg();
    uint8_t c[] = {0x90,0x00,(uint8_t)(multi?1:0),0x00};
    ld2450Send(c, 4);
    ld2450ExitCfg();
    multiTarget = multi;
}
static void ld2450FactoryReset() {
    ld2450EnterCfg();
    const uint8_t c[] = {0xA2,0x00}; ld2450Send(c, 2);
    ld2450ExitCfg();
    delay(500);
}
// Note: LD2450 also supports zone filtering and baud-rate configuration via
// the same protocol — expandable via additional ld2450Send() calls.

// ════════════════════════════════════════════════════════════════════════════
//  FRAME PARSER
// ════════════════════════════════════════════════════════════════════════════

static void commitFrame() {
    Target tmp[MAX_TGTS];
    for (int i = 0; i < MAX_TGTS; i++) {
        const uint8_t* d = &fb[4 + i * 8];
        uint16_t rx = (uint16_t)(d[0] | ((uint16_t)d[1] << 8));
        uint16_t ry = (uint16_t)(d[2] | ((uint16_t)d[3] << 8));
        uint16_t rs = (uint16_t)(d[4] | ((uint16_t)d[5] << 8));
        tmp[i].present = (rx | ry | rs) != 0;
        tmp[i].x       = decodeCoord(rx);
        tmp[i].y       = decodeCoord(ry);
        tmp[i].speed   = decodeCoord(rs);
    }
    portENTER_CRITICAL(&tgts_mux);
    memcpy(tgts, tmp, sizeof(tmp));
    lastFrameMs = millis();
    portEXIT_CRITICAL(&tgts_mux);
}

static void processByte(uint8_t b) {
    if (!inf) {
        if (b == FHDR[hm]) {
            fb[hm++] = b;
            if (hm == 4) { inf = true; fp = 4; hm = 0; }
        } else {
            hm = (b == FHDR[0]) ? 1 : 0;
            if (hm == 1) fb[0] = b;
        }
    } else {
        fb[fp++] = b;
        if (fp == FRAME_LEN) {
            if (fb[28] == FFTR[0] && fb[29] == FFTR[1]) commitFrame();
            inf = false; fp = 0;
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  IMU
// ════════════════════════════════════════════════════════════════════════════

static void checkRotation() {
    if (!imuEnabled) return;
    float ax, ay, az;
    if (!M5.Imu.getAccel(&ax, &ay, &az)) return;
    int r = (ax > 0.3f) ? 3 : 1;
    if (r != M5.Display.getRotation()) {
        M5.Display.setRotation(r);
        manualRot = r;
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  SWEEP ANIMATION & TARGET SMOOTHING
// ════════════════════════════════════════════════════════════════════════════

static void updateSweep(uint32_t nowMs) {
    if (lastAnimMs == 0) { lastAnimMs = nowMs; return; }
    float dt = (nowMs - lastAnimMs) / 1000.0f;
    lastAnimMs = nowMs;
    sweepAngle = fmodf(sweepAngle + SWEEP_DPS * dt, 360.0f);
}

static void updateTargets() {
    Target snap[MAX_TGTS];
    portENTER_CRITICAL(&tgts_mux);
    memcpy(snap, tgts, sizeof(snap));
    portEXIT_CRITICAL(&tgts_mux);

    for (int i = 0; i < MAX_TGTS; i++) {
        if (!snap[i].present) {
            sweepBright[i] *= 0.93f;
            continue;
        }
        // Apply orientation transform
        float dx, dy;
        applyOrient((float)snap[i].x, (float)snap[i].y, dx, dy);

        // Low-pass smooth (α=0.35 for new position)
        if (!hadPresent[i]) {
            smoothX[i] = dx; smoothY[i] = dy; hadPresent[i] = true;
        } else {
            smoothX[i] = smoothX[i] * 0.65f + dx * 0.35f;
            smoothY[i] = smoothY[i] * 0.65f + dy * 0.35f;
        }

        // Sweep brightness: max 1.0 when sweep just passed, fades to 0
        if (smoothY[i] <= 0) { sweepBright[i] *= 0.95f; continue; }
        float tAngle = atan2f(smoothX[i], smoothY[i]) * 180.0f / (float)M_PI;
        if (tAngle < 0) tAngle += 360.0f;

        float diff = fmodf(sweepAngle - tAngle + 360.0f, 360.0f);
        if (diff <= TRAIL_DEG) {
            float hit = 1.0f - diff / TRAIL_DEG;
            if (hit > sweepBright[i]) sweepBright[i] = hit;
        } else {
            sweepBright[i] *= 0.97f;
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  DRAWING — RADAR BACKGROUND
// ════════════════════════════════════════════════════════════════════════════

static void drawRadarBackground() {
    // Semicircle fill (upper half: 270° CW through 0° to 90°)
    cv.fillArc(RDR_CX, RDR_CY, 0, RDR_R, 270.0f, 90.0f, CP_RADAR_FILL);

    // Range rings
    int32_t maxR = RANGES[rangeIdx];
    for (int i = 0; i < RANGE_CNT; i++) {
        if (RANGES[i] > maxR) break;
        int32_t r   = (int32_t)RANGES[i] * RDR_R / maxR;
        bool    out = (RANGES[i] == maxR);
        uint32_t col = out ? CP_ARC_OUT : (i == 1 ? CP_ARC2 : CP_ARC1);
        int      thk = out ? 4 : 2;
        cv.fillArc(RDR_CX, RDR_CY, r - thk, r + thk, 270.0f, 90.0f, col);

        // Range label — right end of arc, just above baseline
        char lbl[10];
        snprintf(lbl, sizeof(lbl), "%dm", (int)(RANGES[i] / 1000));
        cv.setFont(&fonts::FreeSans9pt7b);
        cv.setTextDatum(BL_DATUM);
        cv.setTextColor(col);
        cv.drawString(lbl, RDR_CX + r - 8, RDR_CY - 14);
    }

    // Sector dividers at ±30° and ±60°
    static const int SECS[] = {-60, -30, 30, 60};
    for (int i = 0; i < 4; i++) {
        float rad = SECS[i] * (float)M_PI / 180.0f;
        int ex = RDR_CX + (int)(RDR_R * sinf(rad));
        int ey = RDR_CY - (int)(RDR_R * cosf(rad));
        cv.drawLine(RDR_CX, RDR_CY, ex, ey, CP_SECTOR);

        // Angle label near arc edge
        char albl[6];
        snprintf(albl, sizeof(albl), "%d", SECS[i]);
        cv.setFont(&fonts::FreeSans9pt7b);
        cv.setTextDatum(MC_DATUM);
        cv.setTextColor(CP_SECTOR);
        int lx = RDR_CX + (int)((RDR_R - 30) * sinf(rad));
        int ly = RDR_CY - (int)((RDR_R - 30) * cosf(rad));
        cv.drawString(albl, lx, ly);
    }
    cv.setTextDatum(TL_DATUM);

    // Horizon baseline
    cv.drawLine(RDR_CX - RDR_R, RDR_CY,     RDR_CX + RDR_R, RDR_CY,     CP_BASELINE);
    cv.drawLine(RDR_CX - RDR_R, RDR_CY + 1, RDR_CX + RDR_R, RDR_CY + 1, CP_BASELINE);

    // Forward centreline
    cv.drawLine(RDR_CX,     RDR_CY, RDR_CX,     SB_H, CP_FWD);
    cv.drawLine(RDR_CX + 1, RDR_CY, RDR_CX + 1, SB_H, CP_FWD);
}

// ════════════════════════════════════════════════════════════════════════════
//  DRAWING — SWEEP PULSE
// ════════════════════════════════════════════════════════════════════════════

static void drawSweep() {
    // Gradient trail: TRAIL_SEGS sectors from dimmest (far) to brightest (near)
    for (int i = TRAIL_SEGS - 1; i >= 0; i--) {
        float a1 = sweepAngle - TRAIL_DEG * (float)i / TRAIL_SEGS;
        float a0 = sweepAngle - TRAIL_DEG * (float)(i + 1) / TRAIL_SEGS;
        while (a0 < 0) a0 += 360.0f;
        while (a1 < 0) a1 += 360.0f;
        a0 = fmodf(a0, 360.0f);
        a1 = fmodf(a1, 360.0f);

        // Quadratic brightness falloff: bright near sweep, black at tail
        float frac = (float)(TRAIL_SEGS - i) / TRAIL_SEGS;
        frac = frac * frac;

        uint8_t g = (uint8_t)(0x88 * frac);
        uint8_t b = (uint8_t)(0x55 * frac);
        cv.fillArc(RDR_CX, RDR_CY, 0, RDR_R, a0, a1, cv.color888(0, g, b));
    }

    // Bright sweep line — only when in visible upper semicircle
    float na = fmodf(sweepAngle, 360.0f);
    if (na >= 270.0f || na <= 90.0f) {
        float rad = sweepAngle * (float)M_PI / 180.0f;
        int ex = RDR_CX + (int)((float)RDR_R * sinf(rad));
        int ey = RDR_CY - (int)((float)RDR_R * cosf(rad));

        // 3-pixel-wide line
        cv.drawLine(RDR_CX - 1, RDR_CY, ex - 1, ey, 0x00886A);
        cv.drawLine(RDR_CX,     RDR_CY, ex,     ey, CP_SWEEP_TIP);
        cv.drawLine(RDR_CX + 1, RDR_CY, ex + 1, ey, 0x00886A);

        // Bright tip dot
        cv.fillCircle(ex, ey, 8, 0xBBFFEE);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  DRAWING — TARGET DOTS (glow + sweep brightness)
// ════════════════════════════════════════════════════════════════════════════

static void drawTargets() {
    bool pres[MAX_TGTS];
    portENTER_CRITICAL(&tgts_mux);
    for (int i = 0; i < MAX_TGTS; i++) pres[i] = tgts[i].present;
    portEXIT_CRITICAL(&tgts_mux);

    int32_t maxRange = RANGES[rangeIdx];

    for (int i = 0; i < MAX_TGTS; i++) {
        if (!pres[i] || !hadPresent[i]) continue;

        int px = RDR_CX + (int)(smoothX[i] * RDR_R / maxRange);
        int py = RDR_CY - (int)(smoothY[i] * RDR_R / maxRange);
        px = constrain(px, 12, SCR_W - 12);
        py = constrain(py, SB_H + 12, RDR_CY - 12);

        uint32_t bc = C_TGT[i];
        float br = 0.12f + sweepBright[i] * 0.88f;

        uint8_t r = (uint8_t)(((bc >> 16) & 0xFF) * br);
        uint8_t g = (uint8_t)(((bc >>  8) & 0xFF) * br);
        uint8_t b = (uint8_t)(((bc      ) & 0xFF) * br);

        // Glow rings (outer → inner, increasing brightness)
        cv.fillCircle(px, py, 34, cv.color888(r/12, g/12, b/12));
        cv.fillCircle(px, py, 24, cv.color888(r/6,  g/6,  b/6));
        cv.fillCircle(px, py, 15, cv.color888(r/2,  g/2,  b/2));
        cv.fillCircle(px, py,  8, cv.color888(r*3/4, g*3/4, b*3/4));
        // Bright core
        uint8_t rc = (uint8_t)constrain((int)r + 40, 0, 255);
        uint8_t gc = (uint8_t)constrain((int)g + 40, 0, 255);
        uint8_t bc2= (uint8_t)constrain((int)b + 40, 0, 255);
        cv.fillCircle(px, py,  4, cv.color888(rc, gc, bc2));

        // Target number (centred in disc)
        char n[3]; snprintf(n, sizeof(n), "%d", i + 1);
        cv.setFont(&fonts::FreeSansBold18pt7b);
        cv.setTextDatum(MC_DATUM);
        cv.setTextColor(0x000000);
        cv.drawString(n, px, py);
        cv.setTextDatum(TL_DATUM);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  DRAWING — STATUS BAR (TOP)
// ════════════════════════════════════════════════════════════════════════════

static void drawStatusBar() {
    cv.fillRect(0, 0, SCR_W, SB_H, CP_SB_BG);
    // Border
    cv.drawLine(0, SB_H - 3, SCR_W, SB_H - 3, CP_MENU_BDR);
    cv.drawLine(0, SB_H - 1, SCR_W, SB_H - 1, 0x002A1A);

    bool noSig = (lastFrameMs == 0) || (millis() - lastFrameMs > 2000);
    int32_t maxR = RANGES[rangeIdx];

    // Range indicator
    char rng[16]; snprintf(rng, sizeof(rng), "RNG %dm", (int)(maxR / 1000));
    cv.setFont(&fonts::FreeSansBold18pt7b);
    cv.setTextDatum(ML_DATUM);
    cv.setTextColor(CP_ARC_OUT);
    cv.drawString(rng, 20, SB_H / 2);

    if (noSig) {
        cv.setTextColor(CP_TEXT_ERR);
        cv.drawString("NO SIGNAL", 240, SB_H / 2);
    } else {
        bool pres[MAX_TGTS];
        portENTER_CRITICAL(&tgts_mux);
        for (int i = 0; i < MAX_TGTS; i++) pres[i] = tgts[i].present;
        portEXIT_CRITICAL(&tgts_mux);

        int cnt = 0;
        for (int i = 0; i < MAX_TGTS; i++) if (pres[i]) cnt++;

        char tcnt[10]; snprintf(tcnt, sizeof(tcnt), "TGT %d", cnt);
        cv.setTextColor(CP_TEXT_HI);
        cv.drawString(tcnt, 240, SB_H / 2);

        // Per-target coordinates
        cv.setFont(&fonts::FreeSans12pt7b);
        int xpos = 395;
        for (int i = 0; i < MAX_TGTS; i++) {
            if (!pres[i] || !hadPresent[i]) continue;
            int xdm = (int)(smoothX[i] / 100.0f);
            int ydm = (int)(smoothY[i] / 100.0f);
            char tbuf[30];
            snprintf(tbuf, sizeof(tbuf), "#%d %+ddm,%ddm", i + 1, xdm, ydm);
            cv.setTextColor(C_TGT[i]);
            cv.drawString(tbuf, xpos, SB_H / 2);
            xpos += 280;
        }
    }
    cv.setTextDatum(TL_DATUM);
}

// ════════════════════════════════════════════════════════════════════════════
//  DRAWING — MENU BUTTON  (≡ hamburger, top-right)
// ════════════════════════════════════════════════════════════════════════════

static void drawMenuButton() {
    bool open = (menuPage != MP_CLOSED);
    uint32_t bg = open ? CP_MENU_HL : 0x001C1C;
    uint32_t lc = open ? CP_BG      : CP_MENU_HL;

    cv.fillRoundRect(MBTN_X, MBTN_Y, MBTN_W, MBTN_H, 8, bg);
    cv.drawRoundRect(MBTN_X, MBTN_Y, MBTN_W, MBTN_H, 8, CP_MENU_BDR);

    int mx = MBTN_X + MBTN_W / 2;
    int my = MBTN_Y + MBTN_H / 2;
    for (int l = -1; l <= 1; l++) {
        int ly = my + l * 11;
        int hw = (l == 0) ? 18 : 13;
        cv.drawLine(mx - hw, ly,     mx + hw, ly,     lc);
        cv.drawLine(mx - hw, ly + 1, mx + hw, ly + 1, lc);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  DRAWING — UI WIDGETS
// ════════════════════════════════════════════════════════════════════════════

static void drawToggle(int x, int y, bool on) {
    const int W = 80, H = 36;
    cv.fillRoundRect(x, y, W, H, H/2, on ? 0x00CC88 : 0x101818);
    cv.drawRoundRect(x, y, W, H, H/2, CP_MENU_BDR);
    int knobX = on ? (x + W - H/2 - 2) : (x + H/2 + 2);
    cv.fillCircle(knobX, y + H/2, H/2 - 4, 0xFFFFFF);
    cv.setFont(&fonts::FreeSans9pt7b);
    cv.setTextDatum(MC_DATUM);
    cv.setTextColor(on ? 0x000000 : CP_MENU_ITEM);
    cv.drawString(on ? "ON" : "OFF", on ? (x + W/2 - 12) : (x + W/2 + 12), y + H/2);
    cv.setTextDatum(TL_DATUM);
}

static void drawPillBtn(int x, int y, const char* label, bool sel) {
    const int W = 95, H = 38;
    cv.fillRoundRect(x, y, W, H, 6, sel ? 0x00CC88 : 0x101818);
    cv.drawRoundRect(x, y, W, H, 6, CP_MENU_BDR);
    cv.setFont(&fonts::FreeSans9pt7b);
    cv.setTextDatum(MC_DATUM);
    cv.setTextColor(sel ? 0x000000 : CP_MENU_ITEM);
    cv.drawString(label, x + W/2, y + H/2);
    cv.setTextDatum(TL_DATUM);
}

// ════════════════════════════════════════════════════════════════════════════
//  DRAWING — SENSOR ORIENTATION ICON
//  Draws a tiny PCB diagram with detection arrow, rotated per orientation.
// ════════════════════════════════════════════════════════════════════════════

static void drawOrientIcon(int cx, int cy, SensorOrient o) {
    float angle = 0;
    bool  flipX = false;
    switch (o) {
        case OR_MIRROR: flipX = true; break;
        case OR_CW90:   angle =  90;  break;
        case OR_CCW90:  angle = -90;  break;
        default:        break;
    }
    float rad = angle * (float)M_PI / 180.0f;
    float ca = cosf(rad), sa = sinf(rad);

    // Rotate a local point (lx,ly) → screen (sx,sy)
    auto xfm = [&](float lx, float ly, int& sx, int& sy) {
        if (flipX) lx = -lx;
        sx = cx + (int)(lx * ca + ly * sa + 0.5f);
        sy = cy + (int)(-lx * sa + ly * ca + 0.5f);
    };

    // PCB outline (±9 wide, ±14 tall; top = detection side)
    float pcb[4][2] = {{-9,-14},{9,-14},{9,14},{-9,14}};
    int ps[4][2];
    for (int i = 0; i < 4; i++) xfm(pcb[i][0], pcb[i][1], ps[i][0], ps[i][1]);
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        cv.drawLine(ps[i][0], ps[i][1], ps[j][0], ps[j][1], CP_MENU_ITEM);
    }

    // Connector stub at bottom (y=+16)
    int c1x,c1y, c2x,c2y;
    xfm(-5, 16, c1x, c1y); xfm(5, 16, c2x, c2y);
    int ox=(int)(sa*4), oy=(int)(-ca*4);   // outward from PCB edge
    cv.drawLine(c1x+ox, c1y+oy, c2x+ox, c2y+oy, 0xFF8800);
    cv.drawLine(c1x+ox*2, c1y+oy*2, c2x+ox*2, c2y+oy*2, 0xFF8800);

    // Detection arrow pointing away from top face (y=-14 → y=-24)
    int ax1,ay1, ax2,ay2;
    xfm(0, -14, ax1, ay1);  xfm(0, -24, ax2, ay2);
    cv.drawLine(ax1, ay1, ax2, ay2, CP_SWEEP_TIP);
    // Arrowhead
    int ah1x,ah1y, ah2x,ah2y;
    xfm(-4, -19, ah1x, ah1y);  xfm(4, -19, ah2x, ah2y);
    cv.drawLine(ax2, ay2, ah1x, ah1y, CP_SWEEP_TIP);
    cv.drawLine(ax2, ay2, ah2x, ah2y, CP_SWEEP_TIP);

    if (flipX) {
        cv.setFont(&fonts::FreeSans9pt7b);
        cv.setTextDatum(MC_DATUM);
        cv.setTextColor(CP_ALERT);
        cv.drawString("M", cx, cy);
        cv.setTextDatum(TL_DATUM);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  DRAWING — MENU OVERLAY
// ════════════════════════════════════════════════════════════════════════════

static void drawMenuOverlay() {
    if (menuPage == MP_CLOSED) return;

    int rows = (menuPage == MP_LD2450) ? 3 : 5;
    int panelH = MNU_PAD * 2 + 46 + rows * MNU_IH;

    // Panel
    cv.fillRoundRect(MNU_X, MNU_Y, MNU_W, panelH, 12, CP_MENU_BG);
    cv.drawRoundRect(MNU_X, MNU_Y, MNU_W, panelH, 12, CP_MENU_BDR);
    cv.drawRoundRect(MNU_X+2, MNU_Y+2, MNU_W-4, panelH-4, 10, 0x002A1A);

    // Title
    int ty = MNU_Y + MNU_PAD;
    cv.setFont(&fonts::FreeSansBold18pt7b);
    cv.setTextDatum(TL_DATUM);
    cv.setTextColor(CP_MENU_HL);
    if      (menuPage == MP_MAIN)   cv.drawString("SETTINGS",      MNU_X + MNU_PAD, ty);
    else if (menuPage == MP_LD2450) cv.drawString("< LD2450 CONFIG", MNU_X + MNU_PAD, ty);
    else                            cv.drawString("< SENSOR MOUNT", MNU_X + MNU_PAD, ty);
    ty += 36;
    cv.drawLine(MNU_X+MNU_PAD, ty, MNU_X+MNU_W-MNU_PAD, ty, 0x003020);
    ty += 10;
    // ty is now MNU_ITEM_Y0

    cv.setFont(&fonts::FreeSans12pt7b);
    cv.setTextDatum(ML_DATUM);

    if (menuPage == MP_MAIN) {
        // Row 0 — IMU
        cv.setTextColor(CP_MENU_ITEM);
        cv.drawString("IMU Auto-Rotate",
            MNU_X+MNU_PAD, ty + MNU_IH*0 + MNU_IH/2);
        drawToggle(MNU_X+MNU_W-100, ty + MNU_IH*0 + MNU_IH/2 - 18, imuEnabled);

        // Row 1 — Screen rotation (greyed when IMU on)
        cv.setTextColor(imuEnabled ? CP_MENU_DIM : CP_MENU_ITEM);
        cv.drawString("Screen Rotation",
            MNU_X+MNU_PAD, ty + MNU_IH*1 + MNU_IH/2);
        if (!imuEnabled) {
            drawPillBtn(MNU_X+MNU_W-210, ty+MNU_IH*1+MNU_IH/2-19, "0 deg",   manualRot==1);
            drawPillBtn(MNU_X+MNU_W-110, ty+MNU_IH*1+MNU_IH/2-19, "180 deg", manualRot==3);
        }

        // Row 2 — LD2450 Config
        cv.setTextColor(CP_MENU_ITEM);
        cv.drawString("LD2450 Config",
            MNU_X+MNU_PAD, ty + MNU_IH*2 + MNU_IH/2);
        cv.setTextColor(CP_MENU_HL);
        cv.drawString(">", MNU_X+MNU_W-MNU_PAD-24, ty + MNU_IH*2 + MNU_IH/2);

        // Row 3 — Sensor Mount
        cv.setTextColor(CP_MENU_ITEM);
        cv.drawString("Sensor Mount",
            MNU_X+MNU_PAD, ty + MNU_IH*3 + MNU_IH/2);
        cv.setFont(&fonts::FreeSans9pt7b);
        cv.setTextColor(CP_TEXT_MID);
        cv.drawString(OR_LABELS[sensorOrient], MNU_X+MNU_W-195, ty + MNU_IH*3 + MNU_IH/2);
        cv.setFont(&fonts::FreeSans12pt7b);
        cv.setTextColor(CP_MENU_HL);
        cv.drawString(">", MNU_X+MNU_W-MNU_PAD-24, ty + MNU_IH*3 + MNU_IH/2);

        // Row 4 — Close
        cv.setTextColor(CP_TEXT_ERR);
        cv.drawString("Close", MNU_X+MNU_PAD, ty + MNU_IH*4 + MNU_IH/2);

    } else if (menuPage == MP_LD2450) {
        // Row 0 — Tracking mode
        cv.setTextColor(CP_MENU_ITEM);
        cv.drawString("Track Mode", MNU_X+MNU_PAD, ty + MNU_IH*0 + MNU_IH/2);
        drawPillBtn(MNU_X+MNU_W-210, ty+MNU_IH*0+MNU_IH/2-19, "Single", !multiTarget);
        drawPillBtn(MNU_X+MNU_W-110, ty+MNU_IH*0+MNU_IH/2-19, "Multi",   multiTarget);

        // Row 1 — Factory reset
        cv.setTextColor(CP_ALERT);
        cv.drawString("Factory Reset", MNU_X+MNU_PAD, ty + MNU_IH*1 + MNU_IH/2);

        // Row 2 — Back
        cv.setTextColor(CP_TEXT_ERR);
        cv.drawString("< Back", MNU_X+MNU_PAD, ty + MNU_IH*2 + MNU_IH/2);

    } else if (menuPage == MP_ORIENT) {
        for (int o = 0; o < 4; o++) {
            bool sel = (sensorOrient == (SensorOrient)o);
            int iy = ty + o * MNU_IH;
            if (sel) {
                cv.fillRoundRect(MNU_X+MNU_PAD, iy+4,
                                 MNU_W-MNU_PAD*2, MNU_IH-8, 6, 0x001A10);
                cv.drawRoundRect(MNU_X+MNU_PAD, iy+4,
                                 MNU_W-MNU_PAD*2, MNU_IH-8, 6, 0x003020);
            }
            // Selection dot
            if (sel) cv.fillCircle(MNU_X+MNU_PAD+20, iy+MNU_IH/2, 8, CP_MENU_HL);
            else     cv.drawCircle(MNU_X+MNU_PAD+20, iy+MNU_IH/2, 8, CP_MENU_ITEM);

            cv.setTextColor(sel ? CP_MENU_HL : CP_MENU_ITEM);
            cv.drawString(OR_LABELS[o], MNU_X+MNU_PAD+40, iy+MNU_IH/2);

            // Orientation icon on right
            drawOrientIcon(MNU_X+MNU_W-54, iy+MNU_IH/2, (SensorOrient)o);
        }
        // Back row (row 4)
        cv.setTextColor(CP_TEXT_ERR);
        cv.drawString("< Back", MNU_X+MNU_PAD, ty + 4*MNU_IH + MNU_IH/2);
    }

    cv.setTextDatum(TL_DATUM);
}

// ════════════════════════════════════════════════════════════════════════════
//  TOUCH HANDLING
// ════════════════════════════════════════════════════════════════════════════

static void handleTap(int tx, int ty) {
    // Menu button
    if (tx >= MBTN_X && tx <= MBTN_X+MBTN_W && ty >= MBTN_Y && ty <= MBTN_Y+MBTN_H) {
        menuPage = (menuPage == MP_CLOSED) ? MP_MAIN : MP_CLOSED;
        return;
    }

    if (menuPage == MP_CLOSED) {
        // Tap radar area → cycle zoom
        rangeIdx = (rangeIdx + 1) % RANGE_CNT;
        return;
    }

    // Tap outside panel → close menu
    if (tx < MNU_X || tx > MNU_X+MNU_W || ty < MNU_Y) {
        menuPage = MP_CLOSED;
        return;
    }

    int row = (ty - MNU_ITEM_Y0) / MNU_IH;
    if (row < 0) return;  // tap in title area

    if (menuPage == MP_MAIN) {
        if (row == 0) {
            imuEnabled = !imuEnabled;
        } else if (row == 1 && !imuEnabled) {
            int bx = MNU_X + MNU_W - 210;
            if      (tx >= bx     && tx < bx+95)  { manualRot=1; M5.Display.setRotation(1); }
            else if (tx >= bx+105 && tx < bx+200) { manualRot=3; M5.Display.setRotation(3); }
        } else if (row == 2) { menuPage = MP_LD2450; }
        else if (row == 3) { menuPage = MP_ORIENT; }
        else if (row == 4) { menuPage = MP_CLOSED; }

    } else if (menuPage == MP_LD2450) {
        if (row == 0) {
            int bx = MNU_X + MNU_W - 210;
            if      (tx >= bx     && tx < bx+95)  ld2450SetTrackMode(false);
            else if (tx >= bx+105 && tx < bx+200) ld2450SetTrackMode(true);
        } else if (row == 1) {
            ld2450FactoryReset();
        } else if (row == 2) {
            menuPage = MP_MAIN;
        }

    } else if (menuPage == MP_ORIENT) {
        if (row >= 0 && row <= 3) {
            sensorOrient = (SensorOrient)row;
            // Reset smooth state so new orientation takes effect immediately
            for (int i = 0; i < MAX_TGTS; i++) hadPresent[i] = false;
        } else {  // Back (row 4)
            menuPage = MP_MAIN;
        }
    }
}

static void handleTouch() {
    if (M5.Touch.getCount() > 0 && millis() - lastTouchMs > 300) {
        auto tp = M5.Touch.getDetail(0);
        handleTap(tp.x, tp.y);
        lastTouchMs = millis();
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  COMPOSITE FRAME RENDER
// ════════════════════════════════════════════════════════════════════════════

static void drawFrame() {
    cv.fillScreen(CP_BG);
    drawRadarBackground();
    drawSweep();
    drawTargets();
    // Re-stamp sensor dot and baseline on top of glow / sweep
    cv.fillCircle(RDR_CX, RDR_CY, 10, CP_SENSOR);
    cv.drawLine(RDR_CX - RDR_R, RDR_CY,     RDR_CX + RDR_R, RDR_CY,     CP_BASELINE);
    cv.drawLine(RDR_CX - RDR_R, RDR_CY + 1, RDR_CX + RDR_R, RDR_CY + 1, CP_BASELINE);
    drawStatusBar();     // covers top SB_H px — hides any leaked drawing
    drawMenuButton();
    drawMenuOverlay();
    cv.pushSprite(0, 0);
}

// ════════════════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════════════════

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(220);

    cv.setColorDepth(16);
    if (!cv.createSprite(SCR_W, SCR_H)) {
        M5.Display.fillScreen(TFT_RED);
        M5.Display.setFont(&fonts::FreeSans12pt7b);
        M5.Display.drawString("PSRAM sprite alloc failed", 20, 20);
        while (true) delay(1000);
    }

    radarSer.setRxBufferSize(1024);
    radarSer.begin(LD_BAUD, SERIAL_8N1, LD_RX_PIN, LD_TX_PIN);

    Serial.begin(115200);
    Serial.println("LD2450 Tab5 Radar v2 — ready");
    Serial.printf("UART1 RX=G%d TX=G%d @ %d baud\n", LD_RX_PIN, LD_TX_PIN, LD_BAUD);
}

// ════════════════════════════════════════════════════════════════════════════
//  LOOP  (~30 fps render, immediate UART drain, 500 ms IMU poll)
// ════════════════════════════════════════════════════════════════════════════

void loop() {
    M5.update();

    // Drain UART immediately — don't wait for render tick
    while (radarSer.available()) processByte((uint8_t)radarSer.read());

    uint32_t now = millis();

    // Advance sweep animation and update target smoothing every frame
    updateSweep(now);
    updateTargets();

    // IMU auto-rotation every 500 ms
    if (now - lastImuMs > 500) { checkRotation(); lastImuMs = now; }

    // Touch input (debounced 300 ms)
    handleTouch();

    // Render at ~30 fps (33 ms budget per frame)
    if (now - lastDrawMs >= 33) {
        drawFrame();
        lastDrawMs = now;
    }
}

#endif // PLATFORMIO
