/*
 * HLK LD2450 mmWave Radar Visualizer — M5Stack Tab5  v3  (Cyberpunk Edition)
 * ESP32-P4 · 1280×720 IPS · Arduino + M5Unified
 *
 * Wiring (GPIO_EXT 10-pin header, bottom edge of Tab5):
 *   EXT 5V → LD2450 VCC     GND → LD2450 GND
 *   G49    → LD2450 TX      G50 → LD2450 RX
 *
 * Controls:
 *   Tap radar area  — cycle zoom (2 m / 4 m / 6 m)
 *   Tap ≡ top-right — Settings menu (IMU, rotation, LD2450 config, sensor orientation)
 */

#include <Arduino.h>
#include <M5Unified.h>
#include <math.h>

// ════════════════════════════════════════════════════════════════════════════
//  UART  — LD2450 on Serial1 (G49/G50), LD2410B on Serial2 (G51/G52)
// ════════════════════════════════════════════════════════════════════════════
#define LD_RX_PIN    49
#define LD_TX_PIN    50
#define LD_BAUD      256000
static HardwareSerial radarSer(1);

#define LD2410_RX_PIN  51    // G51 ← LD2410B TX
#define LD2410_TX_PIN  52    // G52 → LD2410B RX
#define LD2410_BAUD    256000
static HardwareSerial radarSer2(2);

// ════════════════════════════════════════════════════════════════════════════
//  PROTOCOL — LD2450 (multi-target X/Y) & LD2410B (single-target distance)
// ════════════════════════════════════════════════════════════════════════════
// ── LD2450 ─────────────────────────────────────────────────────────────────
#define MAX_TGTS   3
#define FRAME_LEN  30
static const uint8_t FHDR[4] = {0xAA, 0xFF, 0x03, 0x00};
static const uint8_t FFTR[2] = {0x55, 0xCC};
static const int32_t RANGES[3] = {2000, 4000, 6000};
#define RANGE_CNT  3

// ── LD2410B ─────────────────────────────────────────────────────────────────
// Standard data frame: FD FC FB FA | 0D 00 | 02 AA [9 data bytes] 55 00 | 04 03 02 01
// Total 23 bytes.  Type=0x02, head=0xAA in positions [6][7].
#define LD2410_FRAME_LEN  23
static const uint8_t LD2410_FHDR[4] = {0xFD, 0xFC, 0xFB, 0xFA};
static const uint8_t LD2410_FFTR[4] = {0x04, 0x03, 0x02, 0x01};

enum LD2410State : uint8_t {
    LD_NO_TARGET  = 0,
    LD_MOVING     = 1,
    LD_STATIONARY = 2,
    LD_BOTH       = 3
};
struct LD2410Data {
    LD2410State state;
    uint16_t movDist_cm;    // moving target distance
    uint8_t  movEnergy;     // 0–100
    uint16_t statDist_cm;   // stationary target distance
    uint8_t  statEnergy;    // 0–100
    uint16_t detDist_cm;    // combined detection distance
    bool     present;
};

// ════════════════════════════════════════════════════════════════════════════
//  SCREEN LAYOUT  (landscape 1280 × 720)
// ════════════════════════════════════════════════════════════════════════════
#define SCR_W    1280
#define SCR_H    720
#define SB_H     108       // status bar at top

#define RDR_CX   640       // radar origin X
#define RDR_CY   715       // radar origin Y  (near bottom)
#define RDR_R    600       // max radius; arc top at y ≈ 115

// ── Menu button: full-height, right-justified ──────────────────────────────
#define MBTN_W   110
#define MBTN_H   (SB_H - 8)            // 100 px — full bar height
#define MBTN_X   (SCR_W - MBTN_W - 4)  // 1166
#define MBTN_Y   4

// ── Battery: same height, immediately left of menu button ─────────────────
#define BATT_W   182
#define BATT_H   (SB_H - 8)            // 100 px
#define BATT_X   (MBTN_X - BATT_W - 6) // 978
#define BATT_Y   4

// ── Imperial unit conversions ──────────────────────────────────────────────
#define MM_TO_FT  0.003281f   // mm → decimal feet
#define MM_TO_IN  0.03937f    // mm → inches

// ════════════════════════════════════════════════════════════════════════════
//  CYBERPUNK PALETTE  (0xRRGGBB)
//  Canvas runs at 24-bit depth so these are decoded as true RGB888.
// ════════════════════════════════════════════════════════════════════════════
#define CP_BG          0x000000   // pure black background
#define CP_SB_BG       0x011C01   // near-black green top bar
#define CP_SB_TITLE    0x8000FF   // purple — RANGE / TARGETS header text
#define CP_RADAR_FILL  0x050F07   // very dark green radar fill
#define CP_ARC1        0x0D3B22   // dim green ring (inner)
#define CP_ARC2        0x1A6644   // mid green ring
#define CP_ARC_OUT     0x00FF88   // bright neon green outer ring
#define CP_SECTOR      0x1A4A2A   // sector dividers (dotted lines)
#define CP_BASELINE    0x1A8855   // horizon baseline
#define CP_FWD         0x22FFAA   // forward centreline
#define CP_SENSOR      0xFFFFFF   // sensor origin dot (white)
#define CP_TEXT_HI     0x00FFCC   // high-contrast neon teal text
#define CP_TEXT_MID    0x22AA88   // mid-brightness text
#define CP_TEXT_ERR    0xFF3333   // error / no-signal text
#define CP_MENU_BG     0x042B03   // menu panel background
#define CP_MENU_BDR    0x00CCAA   // menu border (neon teal)
#define CP_MENU_HL     0x00FFCC   // menu highlight
#define CP_MENU_ITEM   0xFFFFFF   // menu item text (white)
#define CP_MENU_BTN    0x10F00A   // menu button fill (bright green)
#define CP_MENU_DIM    0x1A3828   // menu item disabled
#define CP_ALERT       0xFF5500   // alert / factory-reset

static const uint32_t C_TGT[3] = {0xFF00FF, 0xFFCC00, 0x00FFFF};

// ════════════════════════════════════════════════════════════════════════════
//  PULSE ANIMATION
// ════════════════════════════════════════════════════════════════════════════
#define PULSE_PERIOD_MS  2000      // 2-second pulse repeat
#define PULSE_HIT_PX     50        // pixel radius within which pulse lights a target

// ════════════════════════════════════════════════════════════════════════════
//  MENU GEOMETRY
// ════════════════════════════════════════════════════════════════════════════
#define MNU_W         610
#define MNU_X         (SCR_W - MNU_W - 10)  // 660
#define MNU_Y         (SB_H + 5)             // 113
#define MNU_IH        90
#define MNU_PAD       20
#define MNU_ITEM_Y0   (MNU_Y + MNU_PAD + 50)

// ════════════════════════════════════════════════════════════════════════════
//  TYPES
// ════════════════════════════════════════════════════════════════════════════
struct Target { int16_t x, y, speed; bool present; };

enum SensorOrient { OR_NORMAL=0, OR_MIRROR, OR_CW90, OR_CCW90 };
// OR_NORMAL = no correction (raw sensor X/Y used as-is)
// OR_MIRROR = flip X (matches the typical physical mounting for this project)
static const char* OR_LABELS[4] = {"Direct", "Standard", "Rotate CW", "Rotate CCW"};

enum MenuPage { MP_CLOSED=0, MP_MAIN, MP_LD2450, MP_ORIENT };

// ════════════════════════════════════════════════════════════════════════════
//  GLOBALS
// ════════════════════════════════════════════════════════════════════════════
static Target       tgts[MAX_TGTS];
static portMUX_TYPE tgts_mux    = portMUX_INITIALIZER_UNLOCKED;
static uint32_t     lastFrameMs = 0;

static float smoothX[MAX_TGTS]     = {};
static float smoothY[MAX_TGTS]     = {};
static float sweepBright[MAX_TGTS] = {};
static bool  hadPresent[MAX_TGTS]  = {};

// ── LD2450 parser state ──────────────────────────────────────────────────────
static uint8_t  fb[FRAME_LEN];
static uint8_t  hm  = 0;
static uint16_t fp  = 0;
static bool     inf = false;

// ── LD2410B data + parser state ──────────────────────────────────────────────
static LD2410Data   ld2410       = {};
static portMUX_TYPE ld2410_mux   = portMUX_INITIALIZER_UNLOCKED;
static uint32_t     lastLD2410Ms = 0;

static uint8_t  ld2410_fb[LD2410_FRAME_LEN];
static uint8_t  ld2410_hm  = 0;
static uint16_t ld2410_fp  = 0;
static bool     ld2410_inf = false;

static uint8_t      rangeIdx     = RANGE_CNT - 1;
static SensorOrient sensorOrient = OR_MIRROR;  // flip-X matches user's physical mounting
static bool         imuEnabled   = true;
static int          manualRot    = 1;
static bool         multiTarget  = true;
static MenuPage     menuPage     = MP_CLOSED;

// Pulse state
static float    pulseR       = 0.0f;
static float    pulseBright  = 1.0f;
static uint32_t pulseStartMs = 0;

// Battery cache (updated every 5 s)
static int16_t  battLevel    = -2;   // -2 = not yet read; -1 = no battery
static bool     battCharging = false;
static uint32_t lastBattMs   = 0;

static uint32_t lastImuMs   = 0;
static uint32_t lastDrawMs  = 0;

static M5Canvas cv(&M5.Display);

// ════════════════════════════════════════════════════════════════════════════
//  DRAWING HELPERS
//  fillArc angle-wrapping is unreliable on this platform, so all semicircle
//  shapes are drawn as full circles and then the below-horizon strip is erased
//  at the end of drawFrame() — only 4 px wide (RDR_CY=715, SCR_H=720).
// ════════════════════════════════════════════════════════════════════════════
// Solid filled disk (full circle — caller must erase below horizon)
static inline void fillSemiDisk(uint32_t col) {
    cv.fillCircle(RDR_CX, RDR_CY, RDR_R, col);
}
// Thick ring drawn as concentric circles (full 360° — erase below horizon later)
static inline void drawRing(int32_t r, int32_t thk, uint32_t col) {
    for (int32_t t = -thk; t <= thk; t++)
        cv.drawCircle(RDR_CX, RDR_CY, r + t, col);
}
// Single-pixel circle ring
static inline void drawCircleRing(int32_t r, uint32_t col) {
    cv.drawCircle(RDR_CX, RDR_CY, r, col);
}
// Dotted line — dot/gap in pixels; draws 3px thick for readability
static void drawDottedLine(int x1, int y1, int x2, int y2, uint32_t col,
                           int dotLen=9, int gapLen=7) {
    float dx=(float)(x2-x1), dy=(float)(y2-y1);
    float len=sqrtf(dx*dx+dy*dy); if(len<1.0f) return;
    float ux=dx/len, uy=dy/len;
    float d=0.0f; bool on=true;
    while (d<len) {
        float seg=(on?dotLen:gapLen); if(d+seg>len) seg=len-d;
        if (on) {
            int ax=x1+(int)(ux*d),          ay=y1+(int)(uy*d);
            int bx=x1+(int)(ux*(d+seg)),    by=y1+(int)(uy*(d+seg));
            cv.drawLine(ax,ay,bx,by,col);
            cv.drawLine(ax+1,ay,bx+1,by,col);
            cv.drawLine(ax,ay+1,bx,by+1,col);
        }
        d+=seg; on=!on;
    }
}

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
//  LD2450 CONFIGURATION
// ════════════════════════════════════════════════════════════════════════════
static void ld2450Send(const uint8_t* data, size_t len) {
    static const uint8_t H[4] = {0xFD,0xFC,0xFB,0xFA};
    static const uint8_t T[4] = {0x04,0x03,0x02,0x01};
    uint8_t lb[2] = {(uint8_t)(len&0xFF),(uint8_t)(len>>8)};
    radarSer.write(H,4); radarSer.write(lb,2);
    radarSer.write(data,len); radarSer.write(T,4);
    delay(80);
}
static void ld2450EnterCfg() { const uint8_t c[]={0xFF,0x00,0x01,0x00}; ld2450Send(c,4); }
static void ld2450ExitCfg()  { const uint8_t c[]={0xFE,0x00};           ld2450Send(c,2); }
static void ld2450SetTrackMode(bool multi) {
    ld2450EnterCfg();
    uint8_t c[]={0x90,0x00,(uint8_t)(multi?1:0),0x00}; ld2450Send(c,4);
    ld2450ExitCfg(); multiTarget=multi;
}
static void ld2450FactoryReset() {
    ld2450EnterCfg();
    const uint8_t c[]={0xA2,0x00}; ld2450Send(c,2);
    ld2450ExitCfg(); delay(500);
}

// ════════════════════════════════════════════════════════════════════════════
//  FRAME PARSER
// ════════════════════════════════════════════════════════════════════════════
static void commitFrame() {
    Target tmp[MAX_TGTS];
    for (int i=0;i<MAX_TGTS;i++) {
        const uint8_t* d=&fb[4+i*8];
        uint16_t rx=(uint16_t)(d[0]|((uint16_t)d[1]<<8));
        uint16_t ry=(uint16_t)(d[2]|((uint16_t)d[3]<<8));
        uint16_t rs=(uint16_t)(d[4]|((uint16_t)d[5]<<8));
        tmp[i].present=(rx|ry|rs)!=0;
        tmp[i].x=decodeCoord(rx); tmp[i].y=decodeCoord(ry); tmp[i].speed=decodeCoord(rs);
    }
    portENTER_CRITICAL(&tgts_mux);
    memcpy(tgts,tmp,sizeof(tmp)); lastFrameMs=millis();
    portEXIT_CRITICAL(&tgts_mux);
}
static void processByte(uint8_t b) {
    if (!inf) {
        if (b==FHDR[hm]) { fb[hm++]=b; if(hm==4){inf=true;fp=4;hm=0;} }
        else { hm=(b==FHDR[0])?1:0; if(hm==1)fb[0]=b; }
    } else {
        fb[fp++]=b;
        if(fp==FRAME_LEN){ if(fb[28]==FFTR[0]&&fb[29]==FFTR[1])commitFrame(); inf=false;fp=0; }
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  LD2410B FRAME PARSER
//  Frame layout (23 bytes):
//  [0..3]  FD FC FB FA  header
//  [4..5]  0D 00        data length = 13
//  [6]     02           reporting frame type
//  [7]     AA           head marker
//  [8]     state        00=none 01=moving 02=static 03=both
//  [9..10] mov dist cm  (LE)
//  [11]    mov energy   0–100
//  [12..13] stat dist cm (LE)
//  [14]    stat energy  0–100
//  [15..16] det dist cm  (LE)
//  [17]    55           tail marker
//  [18]    00           checksum (fixed)
//  [19..22] 04 03 02 01  footer
// ════════════════════════════════════════════════════════════════════════════
static void commitLD2410Frame() {
    LD2410Data tmp;
    tmp.state       = (LD2410State)ld2410_fb[8];
    tmp.movDist_cm  = (uint16_t)(ld2410_fb[ 9] | ((uint16_t)ld2410_fb[10] << 8));
    tmp.movEnergy   = ld2410_fb[11];
    tmp.statDist_cm = (uint16_t)(ld2410_fb[12] | ((uint16_t)ld2410_fb[13] << 8));
    tmp.statEnergy  = ld2410_fb[14];
    tmp.detDist_cm  = (uint16_t)(ld2410_fb[15] | ((uint16_t)ld2410_fb[16] << 8));
    tmp.present     = (tmp.state != LD_NO_TARGET && tmp.detDist_cm > 0);
    portENTER_CRITICAL(&ld2410_mux);
    ld2410 = tmp; lastLD2410Ms = millis();
    portEXIT_CRITICAL(&ld2410_mux);
}
static void processLD2410Byte(uint8_t b) {
    if (!ld2410_inf) {
        if (b==LD2410_FHDR[ld2410_hm]) {
            ld2410_fb[ld2410_hm++]=b;
            if (ld2410_hm==4) { ld2410_inf=true; ld2410_fp=4; ld2410_hm=0; }
        } else { ld2410_hm=(b==LD2410_FHDR[0])?1:0; if(ld2410_hm==1)ld2410_fb[0]=b; }
    } else {
        ld2410_fb[ld2410_fp++]=b;
        if (ld2410_fp==LD2410_FRAME_LEN) {
            // Verify footer, type byte, and head marker
            if (ld2410_fb[19]==LD2410_FFTR[0] && ld2410_fb[20]==LD2410_FFTR[1] &&
                ld2410_fb[21]==LD2410_FFTR[2] && ld2410_fb[22]==LD2410_FFTR[3] &&
                ld2410_fb[ 6]==0x02            && ld2410_fb[ 7]==0xAA) {
                commitLD2410Frame();
            }
            ld2410_inf=false; ld2410_fp=0;
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  IMU
// ════════════════════════════════════════════════════════════════════════════
static void checkRotation() {
    if (!imuEnabled) return;
    float ax,ay,az;
    if (!M5.Imu.getAccel(&ax,&ay,&az)) return;
    int r=(ax>0.3f)?3:1;
    if (r!=M5.Display.getRotation()) { M5.Display.setRotation(r); manualRot=r; }
}

// ════════════════════════════════════════════════════════════════════════════
//  PULSE ANIMATION UPDATE
// ════════════════════════════════════════════════════════════════════════════
static void updatePulse(uint32_t now) {
    if (pulseStartMs==0) pulseStartMs=now;
    float phase=(float)(now-pulseStartMs)/(float)PULSE_PERIOD_MS;
    if (phase>=1.0f) { phase=0.0f; pulseStartMs=now; }
    pulseR = phase * (float)RDR_R;
    // Stay at full brightness until 70% expanded, then fade over the last 30%
    pulseBright = (phase < 0.70f) ? 1.0f : (1.0f - (phase - 0.70f) / 0.30f);
}

// ════════════════════════════════════════════════════════════════════════════
//  TARGET SMOOTH & SWEEP-BRIGHTNESS UPDATE
// ════════════════════════════════════════════════════════════════════════════
static void updateTargets() {
    Target snap[MAX_TGTS];
    portENTER_CRITICAL(&tgts_mux);
    memcpy(snap,tgts,sizeof(snap));
    portEXIT_CRITICAL(&tgts_mux);

    float maxRange=(float)RANGES[rangeIdx];

    for (int i=0;i<MAX_TGTS;i++) {
        if (!snap[i].present) { sweepBright[i]*=0.93f; continue; }

        float dx,dy;
        applyOrient((float)snap[i].x,(float)snap[i].y,dx,dy);

        if (!hadPresent[i]) { smoothX[i]=dx; smoothY[i]=dy; hadPresent[i]=true; }
        else { smoothX[i]=smoothX[i]*0.50f+dx*0.50f; smoothY[i]=smoothY[i]*0.50f+dy*0.50f; }

        // Distance from radar origin in pixels
        float tgtPx=sqrtf(smoothX[i]*smoothX[i]+smoothY[i]*smoothY[i])*(float)RDR_R/maxRange;
        float diff=fabsf(pulseR-tgtPx);
        if (diff<(float)PULSE_HIT_PX) {
            float hit=1.0f-diff/(float)PULSE_HIT_PX;
            if (hit>sweepBright[i]) sweepBright[i]=hit;
        } else {
            sweepBright[i]*=0.97f;
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  BATTERY UPDATE
// ════════════════════════════════════════════════════════════════════════════
static void updateBattery(uint32_t now) {
    if (now-lastBattMs > 1000 || battLevel == -2) {
        battLevel    = (int16_t)M5.Power.getBatteryLevel();
        battCharging = M5.Power.isCharging();
        lastBattMs   = now;
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  DRAW — RADAR BACKGROUND
// ════════════════════════════════════════════════════════════════════════════
static void drawRadarBackground() {
    // Filled semicircle — full circle, below-horizon erased in drawFrame()
    fillSemiDisk(CP_RADAR_FILL);

    // Range rings — drawRing uses full circles, clips handled by drawFrame()
    int32_t maxR=RANGES[rangeIdx];
    for (int i=0;i<RANGE_CNT;i++) {
        if (RANGES[i]>maxR) break;
        int32_t r=(int32_t)RANGES[i]*RDR_R/maxR;
        bool  out=(RANGES[i]==maxR);
        uint32_t col=out?CP_ARC_OUT:(i==1?CP_ARC2:CP_ARC1);
        int   thk=out?4:2;
        drawRing(r, thk, col);
    }

    // Sector dividers ±30°, ±60° — dotted, thicker
    static const int SECS[]={-60,-30,30,60};
    for (int i=0;i<4;i++) {
        float rad=SECS[i]*(float)M_PI/180.0f;
        int ex=RDR_CX+(int)(RDR_R*sinf(rad));
        int ey=RDR_CY-(int)(RDR_R*cosf(rad));
        drawDottedLine(RDR_CX,RDR_CY,ex,ey,CP_SECTOR);
    }

    // Horizon baseline
    cv.drawLine(RDR_CX-RDR_R,RDR_CY,   RDR_CX+RDR_R,RDR_CY,   CP_BASELINE);
    cv.drawLine(RDR_CX-RDR_R,RDR_CY+1, RDR_CX+RDR_R,RDR_CY+1, CP_BASELINE);

    // Forward centreline
    cv.drawLine(RDR_CX,   RDR_CY, RDR_CX,   SB_H, CP_FWD);
    cv.drawLine(RDR_CX+1, RDR_CY, RDR_CX+1, SB_H, CP_FWD);

    // ── Labels drawn OVER arcs for maximum contrast ──────────────────────
    cv.setTextDatum(BL_DATUM);
    // Range arc labels: bold white, imperial (feet), drawn after arcs
    for (int i=0;i<RANGE_CNT;i++) {
        if (RANGES[i]>maxR) break;
        int32_t r=(int32_t)RANGES[i]*RDR_R/maxR;
        float   ft=RANGES[i]*MM_TO_FT;
        char lbl[12];
        if (ft<10.0f) snprintf(lbl,sizeof(lbl),"%.1fft",ft);
        else          snprintf(lbl,sizeof(lbl),"%.0fft",ft);
        cv.setFont(&fonts::FreeSansBold18pt7b);
        cv.setTextColor(0xFFFFFF);
        cv.drawString(lbl, RDR_CX+r-4, RDR_CY-18);
    }
    // Angle labels: bold white, inside arc tip
    cv.setTextDatum(MC_DATUM);
    for (int i=0;i<4;i++) {
        float rad=SECS[i]*(float)M_PI/180.0f;
        char albl[10]; snprintf(albl,sizeof(albl),"%d\xc2\xb0",SECS[i]);
        cv.setFont(&fonts::FreeSansBold18pt7b);
        cv.setTextColor(0xFFFFFF);
        cv.drawString(albl, RDR_CX+(int)((RDR_R-52)*sinf(rad)),
                            RDR_CY-(int)((RDR_R-52)*cosf(rad)));
    }
    cv.setTextDatum(TL_DATUM);
}

// ════════════════════════════════════════════════════════════════════════════
//  DRAW — EXPANDING PULSE
//  Single sonar-ping ring expands from centre every 2 s.
//  Full drawCircle is used so both halves render; below-horizon strip is
//  erased in drawFrame() after all elements are stamped.
// ════════════════════════════════════════════════════════════════════════════
static void drawPulse() {
    if (pulseR < 2.0f) return;
    float bf = pulseBright;   // 1.0 until 70%, then linear fade

    // White pulse — symmetric soft halo ±7 px, equal weight inner & outer
    for (int t = -7; t <= 7; t++) {
        float fade = 1.0f - fabsf((float)t) / 8.0f;  // peaks at t=0
        uint8_t v  = (uint8_t)(0xFF * bf * fade * 0.60f);
        int32_t ri = (int32_t)(pulseR) + t;
        if (ri > 0) cv.drawCircle(RDR_CX, RDR_CY, ri, cv.color888(v, v, v));
    }
    // Bright white core — 3 px wide, perfectly centred
    uint8_t wc = (uint8_t)(0xFF * bf);
    for (int t = -1; t <= 1; t++) {
        int32_t ri = (int32_t)(pulseR) + t;
        if (ri > 0) cv.drawCircle(RDR_CX, RDR_CY, ri, cv.color888(wc, wc, wc));
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  DRAW — TARGET DOTS (glow + pulse-driven brightness)
// ════════════════════════════════════════════════════════════════════════════
static void drawTargets() {
    bool pres[MAX_TGTS];
    portENTER_CRITICAL(&tgts_mux);
    for (int i=0;i<MAX_TGTS;i++) pres[i]=tgts[i].present;
    portEXIT_CRITICAL(&tgts_mux);

    int32_t maxRange=RANGES[rangeIdx];
    for (int i=0;i<MAX_TGTS;i++) {
        if (!pres[i]||!hadPresent[i]) continue;
        int px=RDR_CX+(int)(smoothX[i]*RDR_R/maxRange);
        int py=RDR_CY-(int)(smoothY[i]*RDR_R/maxRange);
        px=constrain(px,12,SCR_W-12);
        py=constrain(py,SB_H+12,RDR_CY-12);

        // Full target colour — always at 100% saturation
        uint32_t bc = C_TGT[i];
        uint8_t r = (bc >> 16) & 0xFF;
        uint8_t g = (bc >>  8) & 0xFF;
        uint8_t b = (bc      ) & 0xFF;

        // Glow intensity driven by sweepBright (pulse hit), but core is ALWAYS bright
        float sw = sweepBright[i];   // 0..1

        // White outer halo — scales with sweep
        uint8_t wg = (uint8_t)(200.0f * sw);
        if (wg > 4) {
            cv.fillCircle(px,py,52,cv.color888(wg/6, wg/6, wg/6));
            cv.fillCircle(px,py,44,cv.color888(wg/3, wg/3, wg/3));
            cv.fillCircle(px,py,36,cv.color888(wg/2, wg/2, wg/2));
        }
        // Coloured mid-glow (always some presence even when sweep=0)
        float mg = 0.25f + sw * 0.75f;
        cv.fillCircle(px,py,26,cv.color888((uint8_t)(r*mg/4),(uint8_t)(g*mg/4),(uint8_t)(b*mg/4)));
        cv.fillCircle(px,py,18,cv.color888((uint8_t)(r*mg/2),(uint8_t)(g*mg/2),(uint8_t)(b*mg/2)));
        cv.fillCircle(px,py,12,cv.color888((uint8_t)(r*mg*3/4),(uint8_t)(g*mg*3/4),(uint8_t)(b*mg*3/4)));
        // Full-bright colour core — always fully saturated
        cv.fillCircle(px,py, 8,cv.color888(r, g, b));
        // Always-visible white border ring — 2 px glow around colour core
        cv.drawCircle(px,py, 9,0xFFFFFF);
        cv.drawCircle(px,py,10,0xFFFFFF);
        cv.drawCircle(px,py,11,cv.color888(180,180,180));
        // White-hot pinpoint centre
        cv.fillCircle(px,py, 3,0xFFFFFF);

        // Label in white so it's visible on both dark and coloured backgrounds
        char n[3]; snprintf(n,sizeof(n),"%d",i+1);
        cv.setFont(&fonts::FreeSansBold18pt7b);
        cv.setTextDatum(MC_DATUM);
        cv.setTextColor(0xFFFFFF);
        cv.drawString(n,px,py);
        cv.setTextDatum(TL_DATUM);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  DRAW — BATTERY INDICATOR
// ════════════════════════════════════════════════════════════════════════════
// Lightning bolt — drawn OUTSIDE and to the RIGHT of the battery body
static void drawLightningBolt(int cx, int cy, uint32_t col) {
    // Scale up bolt to be visible at full SB_H height
    int tx=cx+7,  ty=cy-16;   // top-right tip
    int ml=cx-6,  mr=cx+4, my=cy;
    int bx=cx-7,  by=cy+16;
    for (int d=-2; d<=2; d++) {
        cv.drawLine(tx,ty,   ml+d,my,   col);
        cv.drawLine(ml+d,my, mr+d,my,   col);
        cv.drawLine(mr+d,my, bx,  by,   col);
    }
}

static void drawBattery() {
    cv.fillRect(BATT_X, BATT_Y, BATT_W, BATT_H, CP_SB_BG);

    // Body occupies left portion, bolt floats to right of terminal
    const int termW  = 10;
    const int boltW  = 28;   // space reserved for bolt to the RIGHT of terminal
    const int bodyX  = BATT_X + 4;
    const int bodyY  = BATT_Y + 8;
    const int bodyW  = BATT_W - termW - boltW - 8;
    const int bodyH  = BATT_H - 16;

    if (battLevel < 0) {
        cv.drawRect(bodyX, bodyY, bodyW, bodyH, CP_MENU_BDR);
        cv.fillRect(bodyX+bodyW, bodyY+(bodyH/4), termW, bodyH/2, CP_MENU_BDR);
        cv.setFont(&fonts::FreeSansBold18pt7b);
        cv.setTextDatum(MC_DATUM);
        cv.setTextColor(CP_TEXT_MID);
        cv.drawString("USB", bodyX+bodyW/2, bodyY+bodyH/2);
        cv.setTextDatum(TL_DATUM);
        return;
    }

    uint32_t fillCol;
    if      (battLevel <= 20) fillCol = 0xFF2222;
    else if (battLevel <= 40) fillCol = 0xFFBB00;
    else                      fillCol = 0x22CC66;

    // Outline + terminal bump
    cv.drawRect(bodyX, bodyY, bodyW, bodyH, CP_MENU_BDR);
    cv.drawRect(bodyX+1, bodyY+1, bodyW-2, bodyH-2, CP_MENU_BDR); // double border
    cv.fillRect(bodyX+bodyW, bodyY+(bodyH/4), termW, bodyH/2, CP_MENU_BDR);

    // Fill bar
    int fillW=(int)((float)(bodyW-4)*battLevel/100.0f);
    if (fillW>0) cv.fillRect(bodyX+2, bodyY+2, fillW, bodyH-4, fillCol);

    // Percentage label
    char pct[7]; snprintf(pct,sizeof(pct),"%d%%",battLevel);
    cv.setFont(&fonts::FreeSansBold18pt7b);
    cv.setTextDatum(MC_DATUM);
    cv.setTextColor(0xFFFFFF);
    cv.drawString(pct, bodyX+bodyW/2, bodyY+bodyH/2);
    cv.setTextDatum(TL_DATUM);

    // Charging bolt: positioned clearly RIGHT of the terminal, not overlapping
    if (battCharging) {
        int boltCX = bodyX + bodyW + termW + boltW/2;
        drawLightningBolt(boltCX, BATT_Y + BATT_H/2, 0xFFFF00);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  DRAW — STATUS BAR (TOP)
//  Two-row layout — all measurements in plain English at large font size.
//  Row 1 (y≈30): Range setting | Signal / target count
//  Row 2 (y≈76): One block per detected target: distance + LEFT/RIGHT offset
// ════════════════════════════════════════════════════════════════════════════
// ════════════════════════════════════════════════════════════════════════════
//  DRAW — LD2410B PRESENCE RINGS
//  Drawn over the radar arcs:
//   • Amber  semicircle = stationary target at that distance
//   • Yellow semicircle = moving target at that distance
//  Full drawCircle; below-horizon strip erased later in drawFrame().
// ════════════════════════════════════════════════════════════════════════════
static void drawLD2410Presence() {
    bool stale = (lastLD2410Ms == 0) || (millis() - lastLD2410Ms > 2000);
    if (stale) return;

    LD2410Data snap;
    portENTER_CRITICAL(&ld2410_mux);
    snap = ld2410;
    portEXIT_CRITICAL(&ld2410_mux);

    if (!snap.present) return;

    int32_t maxRange = RANGES[rangeIdx];

    // Stationary ring — amber, 5 px thick
    if ((snap.state == LD_STATIONARY || snap.state == LD_BOTH) && snap.statDist_cm > 0) {
        int32_t r = (int32_t)(snap.statDist_cm) * 10 * RDR_R / maxRange;
        if (r > 4 && r <= RDR_R + 10) {
            for (int t = -2; t <= 2; t++)
                cv.drawCircle(RDR_CX, RDR_CY, r + t, 0xFF8800);
            // Bright core
            cv.drawCircle(RDR_CX, RDR_CY, r, 0xFFAA00);
        }
    }

    // Moving ring — yellow, 3 px thick (LD2450 already shows moving dots;
    // this ring confirms overall distance when dots are off-angle)
    if ((snap.state == LD_MOVING || snap.state == LD_BOTH) && snap.movDist_cm > 0) {
        int32_t r = (int32_t)(snap.movDist_cm) * 10 * RDR_R / maxRange;
        if (r > 4 && r <= RDR_R + 10) {
            for (int t = -1; t <= 1; t++)
                cv.drawCircle(RDR_CX, RDR_CY, r + t, 0xFFEE00);
        }
    }
}

static void drawStatusBar() {
    cv.fillRect(0, 0, SCR_W, SB_H, CP_SB_BG);
    // Bottom border
    cv.drawLine(0, SB_H-3, SCR_W, SB_H-3, CP_MENU_BDR);
    cv.drawLine(0, SB_H-1, SCR_W, SB_H-1, 0x1A5A3A);

    // Version stamp
    cv.setFont(&fonts::FreeSans9pt7b);
    cv.setTextDatum(TR_DATUM);
    cv.setTextColor(0x44AA44);
    cv.drawString("v3.6", BATT_X - 8, SB_H - 6);
    cv.setTextDatum(TL_DATUM);

    bool noSig = (lastFrameMs==0) || (millis()-lastFrameMs > 2000);
    int32_t maxR = RANGES[rangeIdx];

    cv.setFont(&fonts::FreeSansBold18pt7b);
    cv.setTextDatum(ML_DATUM);
    const int ROW1 = SB_H / 4;       // ≈ 27 px from top
    const int ROW2 = SB_H * 3 / 4;   // ≈ 81 px from top

    // ── Row 1: RANGE + TARGET COUNT + LD2410B status ─────────────────────────
    float maxFt = maxR * MM_TO_FT;
    char rng[24];
    if (maxFt < 10.0f) snprintf(rng, sizeof(rng), "RANGE %.1fft", maxFt);
    else               snprintf(rng, sizeof(rng), "RANGE %.0fft", maxFt);
    cv.setTextColor(CP_SB_TITLE);
    cv.drawString(rng, 20, ROW1);

    if (noSig) {
        cv.setTextColor(CP_TEXT_ERR);
        cv.drawString("NO SIGNAL", 340, ROW1);
    } else {
        bool pres[MAX_TGTS];
        portENTER_CRITICAL(&tgts_mux);
        for (int i=0;i<MAX_TGTS;i++) pres[i]=tgts[i].present;
        portEXIT_CRITICAL(&tgts_mux);

        int cnt = 0;
        for (int i=0;i<MAX_TGTS;i++) if(pres[i]) cnt++;

        char tcnt[24];
        snprintf(tcnt, sizeof(tcnt), cnt==1 ? "1 TARGET" : "%d TARGETS", cnt);
        cv.setTextColor(CP_SB_TITLE);
        cv.drawString(tcnt, 340, ROW1);

        // ── LD2410B indicator (right of target count, before battery) ─────────
        bool ld_stale = (lastLD2410Ms == 0) || (millis() - lastLD2410Ms > 2000);
        if (!ld_stale) {
            LD2410Data snap2;
            portENTER_CRITICAL(&ld2410_mux);
            snap2 = ld2410;
            portEXIT_CRITICAL(&ld2410_mux);

            if (snap2.present) {
                char ldbuf[24];
                float detFt = snap2.detDist_cm * 0.0328084f;  // cm → ft
                if (snap2.state == LD_STATIONARY || snap2.state == LD_BOTH) {
                    snprintf(ldbuf, sizeof(ldbuf), "STATIC %.1fft", detFt);
                    cv.setTextColor(0xFF8800);   // amber
                } else {
                    snprintf(ldbuf, sizeof(ldbuf), "MOVING %.1fft", detFt);
                    cv.setTextColor(0xFFEE00);   // yellow
                }
                cv.drawString(ldbuf, 580, ROW1);
            }
        }

        // ── Row 2: per-target readout — white text, imperial ─────────────────
        // Format: "T1  6.2ft  14in LEFT"  or  "T1  6.2ft  CTR"
        int xpos = 20;
        for (int i=0;i<MAX_TGTS;i++) {
            if (!pres[i] || !hadPresent[i]) continue;

            float depth_ft = smoothY[i] * MM_TO_FT;
            float lat_in   = fabsf(smoothX[i]) * MM_TO_IN;
            const char* dir = smoothX[i] < -25.4f ? "LEFT"
                            : smoothX[i] >  25.4f ? "RIGHT"
                            : "CTR";

            char tbuf[48];
            if (lat_in < 1.0f) {
                snprintf(tbuf, sizeof(tbuf), "T%d  %.1fft  CTR", i+1, depth_ft);
            } else {
                snprintf(tbuf, sizeof(tbuf), "T%d  %.1fft  %.0fin %s",
                         i+1, depth_ft, lat_in, dir);
            }
            cv.setTextColor(0xFFFFFF);
            cv.drawString(tbuf, xpos, ROW2);
            xpos += 420;
        }
    }
    cv.setTextDatum(TL_DATUM);

    // Battery + menu button overlaid on right side
    drawBattery();
}

// ════════════════════════════════════════════════════════════════════════════
//  DRAW — MENU BUTTON  (≡ semi-transparent, below battery)
// ════════════════════════════════════════════════════════════════════════════
static void drawMenuButton() {
    bool open=(menuPage!=MP_CLOSED);
    uint32_t bg = open ? 0x225522 : 0x081808;
    uint32_t lc = open ? 0xFFFFFF : CP_MENU_HL;

    cv.fillRoundRect(MBTN_X, MBTN_Y, MBTN_W, MBTN_H, 10, bg);
    cv.drawRoundRect(MBTN_X, MBTN_Y, MBTN_W, MBTN_H, 10, CP_MENU_BDR);
    cv.drawRoundRect(MBTN_X+2, MBTN_Y+2, MBTN_W-4, MBTN_H-4, 8, open?CP_MENU_HL:0x1A5A3A);

    int mx=MBTN_X+MBTN_W/2, my=MBTN_Y+MBTN_H/2;
    // Three hamburger lines — spaced for full-height button
    for (int l=-1;l<=1;l++) {
        int ly=my+l*18, hw=(l==0)?22:16;
        cv.drawLine(mx-hw,ly,   mx+hw,ly,   lc);
        cv.drawLine(mx-hw,ly+1, mx+hw,ly+1, lc);
        cv.drawLine(mx-hw,ly+2, mx+hw,ly+2, lc);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  DRAW — UI WIDGETS (toggle, pill button)
// ════════════════════════════════════════════════════════════════════════════
static void drawToggle(int x, int y, bool on) {
    const int W=130,H=54;
    cv.fillRoundRect(x,y,W,H,H/2, on?CP_MENU_BTN:0x101818);
    cv.drawRoundRect(x,y,W,H,H/2, CP_MENU_BDR);
    int kx=on?(x+W-H/2-2):(x+H/2+2);
    cv.fillCircle(kx,y+H/2,H/2-4,0xFFFFFF);
    cv.setFont(&fonts::FreeSansBold18pt7b); cv.setTextDatum(MC_DATUM);
    cv.setTextColor(on?0x011C01:0xAAAAAA);
    cv.drawString(on?"ON":"OFF", on?(x+W/2-14):(x+W/2+14), y+H/2);
    cv.setTextDatum(TL_DATUM);
}
static void drawPillBtn(int x, int y, const char* lbl, bool sel) {
    const int W=150,H=62;
    cv.fillRoundRect(x,y,W,H,10, sel?CP_MENU_BTN:0x101818);
    cv.drawRoundRect(x,y,W,H,10, CP_MENU_BDR);
    cv.setFont(&fonts::FreeSansBold18pt7b); cv.setTextDatum(MC_DATUM);
    cv.setTextColor(sel?0x011C01:0xAAAAAA);
    cv.drawString(lbl,x+W/2,y+H/2);
    cv.setTextDatum(TL_DATUM);
}

// ════════════════════════════════════════════════════════════════════════════
//  DRAW — SENSOR ORIENTATION ICON
// ════════════════════════════════════════════════════════════════════════════
// Orientation icon — 3× scale PCB with asymmetric dot to distinguish flip
// OR_NORMAL (Direct): dot on right side of PCB face
// OR_MIRROR (Standard): dot on left side (physically mirrored)
// OR_CW90 / OR_CCW90: rotated accordingly
static void drawOrientIcon(int cx, int cy, SensorOrient o) {
    float angle=0; bool flipX=false;
    switch(o){
        case OR_MIRROR:  flipX=true;  break;
        case OR_CW90:    angle= 90;   break;
        case OR_CCW90:   angle=-90;   break;
        default:                      break;
    }
    float rad=angle*(float)M_PI/180.0f, ca=cosf(rad), sa=sinf(rad);
    auto xfm=[&](float lx,float ly,int&sx,int&sy){
        if(flipX) lx=-lx;
        sx=cx+(int)(lx*ca+ly*sa+0.5f);
        sy=cy+(int)(-lx*sa+ly*ca+0.5f);
    };

    // PCB outline (±27×±42)
    const float PW=27, PH=42;
    float pcb[4][2]={{-PW,-PH},{PW,-PH},{PW,PH},{-PW,PH}};
    int ps[4][2];
    for(int i=0;i<4;i++) xfm(pcb[i][0],pcb[i][1],ps[i][0],ps[i][1]);
    for(int i=0;i<4;i++){
        int j=(i+1)%4;
        cv.drawLine(ps[i][0],ps[i][1],ps[j][0],ps[j][1],CP_MENU_ITEM);
        cv.drawLine(ps[i][0]+1,ps[i][1],ps[j][0]+1,ps[j][1],CP_MENU_ITEM);
    }

    // Filled PCB body (dark) so it reads as a solid board
    int ipx[4],ipy[4];
    for(int i=0;i<4;i++) xfm(
        (pcb[i][0]>0)?(pcb[i][0]-3):(pcb[i][0]+3),
        (pcb[i][1]>0)?(pcb[i][1]-3):(pcb[i][1]+3),
        ipx[i],ipy[i]);
    // Simplified fill: draw the inner rect (works for non-rotated cases)
    int fillX=min(ipx[0],ipx[2]),fillY=min(ipy[0],ipy[2]);
    int fillW=abs(ipx[1]-ipx[0])+1, fillH=abs(ipy[2]-ipy[0])+1;
    cv.fillRect(min({ps[0][0],ps[1][0],ps[2][0],ps[3][0]})+2,
                min({ps[0][1],ps[1][1],ps[2][1],ps[3][1]})+2,
                abs(ps[1][0]-ps[0][0])-3, abs(ps[3][1]-ps[0][1])-3,
                0x081808);

    // Asymmetric marker dot: top-right corner of PCB face (RIGHT side when not flipped)
    // This is what shows whether the board is flipped or not
    int mx,my; xfm(PW-8, -PH+8, mx, my);
    cv.fillCircle(mx,my,6,CP_ARC_OUT);

    // Connector stub at bottom (cable exit)
    int c1x,c1y,c2x,c2y; xfm(-14,PH+2,c1x,c1y); xfm(14,PH+2,c2x,c2y);
    int ox=(int)(sa*5),oy=(int)(-ca*5);
    for(int d=1;d<=3;d++)
        cv.drawLine(c1x+ox*d,c1y+oy*d,c2x+ox*d,c2y+oy*d,0xFF8800);

    // Detection arrow pointing away from sensor face (upward in standard orientation)
    int ax1,ay1,ax2,ay2,ah1x,ah1y,ah2x,ah2y;
    xfm(0,-PH,ax1,ay1); xfm(0,-PH-26,ax2,ay2);
    for(int d=-1;d<=1;d++) cv.drawLine(ax1+d,ay1,ax2+d,ay2,CP_FWD);
    xfm(-10,-PH-16,ah1x,ah1y); xfm(10,-PH-16,ah2x,ah2y);
    cv.drawLine(ax2,ay2,ah1x,ah1y,CP_FWD);
    cv.drawLine(ax2,ay2,ah2x,ah2y,CP_FWD);
}

// ════════════════════════════════════════════════════════════════════════════
//  DRAW — MENU OVERLAY
// ════════════════════════════════════════════════════════════════════════════
// Draw a full-width chunky action button inside the menu
static void drawMenuRowBtn(int iy, const char* lbl, uint32_t fillCol, uint32_t txtCol,
                           bool useFreeSansBold24=false) {
    int bx=MNU_X+MNU_PAD, bw=MNU_W-MNU_PAD*2, bh=MNU_IH-10;
    int by=iy+5;
    cv.fillRoundRect(bx,by,bw,bh,12,fillCol);
    cv.drawRoundRect(bx,by,bw,bh,12,CP_MENU_BDR);
    cv.setFont(&fonts::FreeSansBold18pt7b);
    cv.setTextDatum(MC_DATUM);
    cv.setTextColor(txtCol);
    cv.drawString(lbl, MNU_X+MNU_W/2, iy+MNU_IH/2);
    cv.setTextDatum(ML_DATUM);
}

static void drawMenuOverlay() {
    if (menuPage==MP_CLOSED) return;
    // Panel height depends on page type
    int panelH;
    if (menuPage==MP_ORIENT) {
        // 2×2 grid: 2 rows of cells (180px each) + 10px gap + back btn + padding
        panelH = MNU_PAD + 50 + 2*180 + 10 + MNU_IH + MNU_PAD;  // = 560
    } else {
        int rows=(menuPage==MP_LD2450)?3:5;
        panelH=MNU_PAD*2+50+rows*MNU_IH;
    }

    // Panel background + border
    cv.fillRoundRect(MNU_X,MNU_Y,MNU_W,panelH,14,CP_MENU_BG);
    cv.drawRoundRect(MNU_X,MNU_Y,MNU_W,panelH,14,CP_MENU_BDR);
    cv.drawRoundRect(MNU_X+2,MNU_Y+2,MNU_W-4,panelH-4,12,0x1A5A3A);

    // Title
    int ty=MNU_Y+MNU_PAD;
    cv.setFont(&fonts::FreeSansBold18pt7b); cv.setTextDatum(TL_DATUM);
    cv.setTextColor(CP_MENU_HL);
    if      (menuPage==MP_MAIN)   cv.drawString("SETTINGS",       MNU_X+MNU_PAD,ty);
    else if (menuPage==MP_LD2450) cv.drawString("LD2450 CONFIG",  MNU_X+MNU_PAD,ty);
    else                          cv.drawString("SENSOR MOUNT",   MNU_X+MNU_PAD,ty);
    ty+=40;
    cv.drawLine(MNU_X+MNU_PAD,ty,MNU_X+MNU_W-MNU_PAD,ty,0x1A5A3A);
    ty+=10;   // ty is now MNU_ITEM_Y0

    cv.setTextDatum(ML_DATUM);

    if (menuPage==MP_MAIN) {
        // Row 0 — IMU toggle
        cv.setFont(&fonts::FreeSansBold18pt7b);
        cv.setTextColor(CP_MENU_ITEM);
        cv.drawString("IMU Auto-Rotate", MNU_X+MNU_PAD, ty+MNU_IH*0+MNU_IH/2);
        drawToggle(MNU_X+MNU_W-MNU_PAD-130, ty+MNU_IH*0+MNU_IH/2-27, imuEnabled);

        // Row 1 — rotation (only when IMU off)
        cv.setTextColor(imuEnabled?CP_MENU_DIM:CP_MENU_ITEM);
        cv.drawString("Screen Rotation", MNU_X+MNU_PAD, ty+MNU_IH*1+MNU_IH/2);
        if (!imuEnabled) {
            drawPillBtn(MNU_X+MNU_W-MNU_PAD-310, ty+MNU_IH*1+MNU_IH/2-31, "0\xc2\xb0",   manualRot==1);
            drawPillBtn(MNU_X+MNU_W-MNU_PAD-155, ty+MNU_IH*1+MNU_IH/2-31, "180\xc2\xb0", manualRot==3);
        }

        // Row 2 — LD2450 Config button
        drawMenuRowBtn(ty+MNU_IH*2, "LD2450 Config  >", 0x0A2010, CP_MENU_ITEM);

        // Row 3 — Sensor Mount button
        drawMenuRowBtn(ty+MNU_IH*3, "Sensor Mount  >", 0x0A2010, CP_MENU_ITEM);

        // Row 4 — Close (back) — larger, green fill
        drawMenuRowBtn(ty+MNU_IH*4, "CLOSE", CP_MENU_BTN, 0x011C01);

    } else if (menuPage==MP_LD2450) {
        // Row 0 — Track Mode
        cv.setFont(&fonts::FreeSansBold18pt7b);
        cv.setTextColor(CP_MENU_ITEM);
        cv.drawString("Track Mode", MNU_X+MNU_PAD, ty+MNU_IH*0+MNU_IH/2);
        drawPillBtn(MNU_X+MNU_W-MNU_PAD-310, ty+MNU_IH*0+MNU_IH/2-31, "Single", !multiTarget);
        drawPillBtn(MNU_X+MNU_W-MNU_PAD-155, ty+MNU_IH*0+MNU_IH/2-31, "Multi",   multiTarget);

        // Row 1 — Factory Reset (alert colour)
        drawMenuRowBtn(ty+MNU_IH*1, "FACTORY RESET", CP_ALERT, 0xFFFFFF);

        // Row 2 — Back (large green)
        drawMenuRowBtn(ty+MNU_IH*2, "<  BACK", CP_MENU_BTN, 0x011C01);

    } else if (menuPage==MP_ORIENT) {
        // Grid order: Standard (OR_MIRROR) first — that's the user's physical mounting
        static const SensorOrient GRID_ORIENT[4] = {OR_MIRROR, OR_NORMAL, OR_CW90, OR_CCW90};
        const int CW  = (MNU_W - MNU_PAD*3) / 2;  // cell width  = 275
        const int CH  = 180;                        // cell height
        const int CG  = 10;                         // gap between rows
        const int gX0 = MNU_X + MNU_PAD;            // left edge of grid = 680
        const int gY0 = ty;                          // top of grid (= MNU_ITEM_Y0 = 183)

        for (int i=0;i<4;i++) {
            int row=i/2, col=i%2;
            SensorOrient o=GRID_ORIENT[i];
            bool sel=(sensorOrient==o);
            int cx=gX0 + col*(CW+MNU_PAD);
            int cy=gY0 + row*(CH+CG);

            // Cell background + border
            cv.fillRoundRect(cx,cy,CW,CH,14, sel?0x0D3B22:0x061406);
            cv.drawRoundRect(cx,cy,CW,CH,14, sel?CP_MENU_HL:0x1A3828);
            if(sel) cv.drawRoundRect(cx+2,cy+2,CW-4,CH-4,12,0x1A6644);

            // Selection indicator — top-right corner
            if(sel) cv.fillCircle(cx+CW-16,cy+16,10,CP_MENU_HL);
            else    cv.drawCircle(cx+CW-16,cy+16,10,0x337733);

            // Orientation icon centred in upper portion of cell
            drawOrientIcon(cx+CW/2, cy+88, o);

            // Label — bold, bottom of cell
            cv.setFont(&fonts::FreeSansBold18pt7b);
            cv.setTextDatum(BC_DATUM);
            cv.setTextColor(sel?CP_MENU_HL:CP_MENU_ITEM);
            cv.drawString(OR_LABELS[o], cx+CW/2, cy+CH-8);
            cv.setTextDatum(ML_DATUM);
        }
        // Back button below grid
        drawMenuRowBtn(gY0+2*(CH+CG), "<  BACK", CP_MENU_BTN, 0x011C01);
    }
    cv.setTextDatum(TL_DATUM);
}

// ════════════════════════════════════════════════════════════════════════════
//  TOUCH HANDLING
// ════════════════════════════════════════════════════════════════════════════
static void handleTap(int tx, int ty) {
    // Menu button — full-height, right side of status bar
    if (tx>=MBTN_X && tx<=MBTN_X+MBTN_W && ty>=MBTN_Y && ty<=MBTN_Y+MBTN_H) {
        menuPage=(menuPage==MP_CLOSED)?MP_MAIN:MP_CLOSED; return;
    }
    if (menuPage==MP_CLOSED) { rangeIdx=(rangeIdx+1)%RANGE_CNT; return; }

    // Tap outside open menu panel → close
    int panelH;
    if (menuPage==MP_ORIENT)
        panelH = MNU_PAD + 50 + 2*180 + 10 + MNU_IH + MNU_PAD;  // 560
    else {
        int rows=(menuPage==MP_LD2450)?3:5;
        panelH=MNU_PAD*2+50+rows*MNU_IH;
    }
    if (tx<MNU_X || tx>MNU_X+MNU_W || ty<MNU_Y || ty>MNU_Y+panelH) {
        menuPage=MP_CLOSED; return;
    }

    int row=(ty-MNU_ITEM_Y0)/MNU_IH;
    if (row<0) return;

    if (menuPage==MP_MAIN) {
        if (row==0) { imuEnabled=!imuEnabled; }
        else if (row==1 && !imuEnabled) {
            // Two pill buttons: 0° and 180°
            int b0x=MNU_X+MNU_W-MNU_PAD-310;
            int b1x=MNU_X+MNU_W-MNU_PAD-155;
            if      (tx>=b0x && tx<b0x+150) { manualRot=1; M5.Display.setRotation(1); }
            else if (tx>=b1x && tx<b1x+150) { manualRot=3; M5.Display.setRotation(3); }
        }
        else if (row==2) menuPage=MP_LD2450;
        else if (row==3) menuPage=MP_ORIENT;
        else if (row==4) menuPage=MP_CLOSED;

    } else if (menuPage==MP_LD2450) {
        int b0x=MNU_X+MNU_W-MNU_PAD-310;
        int b1x=MNU_X+MNU_W-MNU_PAD-155;
        if      (row==0 && tx>=b0x && tx<b0x+150) ld2450SetTrackMode(false);
        else if (row==0 && tx>=b1x && tx<b1x+150) ld2450SetTrackMode(true);
        else if (row==1) ld2450FactoryReset();
        else if (row==2) menuPage=MP_MAIN;

    } else if (menuPage==MP_ORIENT) {
        static const SensorOrient GRID_ORIENT[4] = {OR_MIRROR, OR_NORMAL, OR_CW90, OR_CCW90};
        const int CW=( MNU_W - MNU_PAD*3)/2;  // 275
        const int CH=180, CG=10;
        const int gX0=MNU_X+MNU_PAD;
        const int gY0=MNU_ITEM_Y0;

        // Back button occupies the row below the 2×2 grid
        int backY = gY0 + 2*(CH+CG);
        if (ty >= backY && ty <= backY+MNU_IH) { menuPage=MP_MAIN; return; }

        // Grid cell hit test
        int relX=tx-gX0, relY=ty-gY0;
        if (relY>=0 && relX>=0) {
            int gcol=relX/(CW+MNU_PAD);
            int grow=relY/(CH+CG);
            if (grow>=0 && grow<2 && gcol>=0 && gcol<2) {
                sensorOrient=GRID_ORIENT[grow*2+gcol];
                for(int i=0;i<MAX_TGTS;i++) hadPresent[i]=false;
            }
        }
    }
}
static void handleTouch() {
    // wasPressed() fires exactly once per touch-down — no debounce needed.
    auto tp = M5.Touch.getDetail(0);
    if (tp.wasPressed()) {
        handleTap(tp.x, tp.y);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  COMPOSITE FRAME RENDER
// ════════════════════════════════════════════════════════════════════════════
static void drawFrame() {
    cv.fillScreen(CP_BG);
    drawRadarBackground();   // fillCircle + drawCircle rings (full 360°)
    drawLD2410Presence();    // LD2410B amber/yellow distance rings (full 360°)
    drawPulse();             // drawCircle pulse (full 360°)
    drawTargets();

    // Erase the 4-pixel below-horizon strip produced by full-circle draws
    // (RDR_CY = 715, SCR_H = 720 → only 4 rows need clearing)
    cv.fillRect(0, RDR_CY + 1, SCR_W, SCR_H - RDR_CY - 1, CP_BG);

    // Re-stamp elements the circles may have overwritten
    cv.fillCircle(RDR_CX,RDR_CY,10,CP_SENSOR);
    cv.drawLine(RDR_CX-RDR_R,RDR_CY,  RDR_CX+RDR_R,RDR_CY,  CP_BASELINE);
    cv.drawLine(RDR_CX-RDR_R,RDR_CY+1,RDR_CX+RDR_R,RDR_CY+1,CP_BASELINE);
    cv.drawLine(RDR_CX,  RDR_CY,RDR_CX,  SB_H,CP_FWD);
    cv.drawLine(RDR_CX+1,RDR_CY,RDR_CX+1,SB_H,CP_FWD);
    drawStatusBar();   // covers top SB_H px
    drawMenuButton();
    drawMenuOverlay();
    cv.pushSprite(0,0);
}

// ════════════════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════════════════
void setup() {
    auto cfg=M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(1);
    M5.Display.setBrightness(220);

    // 24-bit canvas: avoids RGB565→RGB888 conversion artifacts on MIPI-DSI.
    // 1280×720×3 = 2.76 MB — well within 32 MB PSRAM.
    cv.setColorDepth(24);
    if (!cv.createSprite(SCR_W,SCR_H)) {
        M5.Display.fillScreen(TFT_RED);
        M5.Display.setFont(&fonts::FreeSans12pt7b);
        M5.Display.drawString("PSRAM sprite alloc failed",20,20);
        while(true) delay(1000);
    }
    radarSer.setRxBufferSize(1024);
    radarSer.begin(LD_BAUD, SERIAL_8N1, LD_RX_PIN, LD_TX_PIN);

    radarSer2.setRxBufferSize(512);
    radarSer2.begin(LD2410_BAUD, SERIAL_8N1, LD2410_RX_PIN, LD2410_TX_PIN);

    Serial.begin(115200);
    Serial.println("LD2450 + LD2410B Tab5 Radar v3.6 — ready");
}

// ════════════════════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════════════════════
void loop() {
    M5.update();
    while (radarSer.available())  processByte((uint8_t)radarSer.read());
    while (radarSer2.available()) processLD2410Byte((uint8_t)radarSer2.read());

    uint32_t now=millis();
    updatePulse(now);
    updateTargets();
    updateBattery(now);

    if (now-lastImuMs>500) { checkRotation(); lastImuMs=now; }
    handleTouch();

    if (now-lastDrawMs>=33) { drawFrame(); lastDrawMs=now; }
}
