/*
 * HLK LD2450 mmWave Sensor — 2D Radar Visualizer
 * Flipper Zero FAP
 *
 * Wiring (Flipper GPIO header):
 *   Pin  1 (5V)  → LD2450 5V
 *   Pin 11 (GND) → LD2450 GND
 *   Pin 13 (TX)  → LD2450 RX
 *   Pin 14 (RX)  → LD2450 TX
 *
 * Controls:
 *   UP / DOWN  — zoom in / out (7 ft, 13 ft, 20 ft range)
 *   BACK       — exit
 *
 * Protocol: 256000 8N1  — 30-byte data frames @ ~10 Hz
 *   Header  : AA FF 03 00
 *   Payload : 3 × 8 bytes (one per target)
 *   Footer  : 55 CC
 *
 *   Per-target encoding (8 bytes, little-endian):
 *     [0-1] X      MSB=1→positive(raw-32768), MSB=0→negative(-raw), mm
 *     [2-3] Y      MSB always 1, positive(raw-32768), mm (+forward)
 *     [4-5] Speed  same encoding as X, cm/s (+away / -toward)
 *     [6-7] Resolution  uint16, mm
 */

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  Constants                                                           */
/* ------------------------------------------------------------------ */

#define TAG             "LD2450Radar"
#define LD2450_BAUD     256000
#define MAX_TARGETS     3
#define FRAME_LEN       30
#define STREAM_BUF_SIZE 512

/* Screen layout
 *   y = 0 .. RADAR_CY-1  : radar plot area
 *   y = RADAR_CY          : sensor / base line
 *   y = RADAR_CY+1 .. 63  : status bar
 */
#define SCREEN_W  128
#define SCREEN_H  64
#define RADAR_CX  64   /* sensor X (horizontal centre)   */
#define RADAR_CY  54   /* sensor Y; also = max arc radius */
#define STATUS_Y  63   /* text baseline for status bar    */

static const uint8_t FRAME_HDR[4] = {0xAA, 0xFF, 0x03, 0x00};
static const uint8_t FRAME_FTR[2] = {0x55, 0xCC};

/* Zoom levels in mm.  Max of each range fills the full arc radius. */
static const int32_t RANGES[] = {2000, 4000, 6000};
#define RANGE_COUNT 3

/*
 * Sector-line endpoints (pre-computed, fixed geometry):
 *   RADAR_CX=64, RADAR_CY=54, R=54
 *   ±30°: sin30=0.500 → 27px,  cos30=0.866 → 47px
 *   ±60°: sin60=0.866 → 47px,  cos60=0.500 → 27px
 *   ±90°: sin90=1.000 → 54px,  cos90=0.000 →  0px  (= base-line ends)
 */
#define S30 27
#define C30 47
#define S60 47
#define C60 27

/* ------------------------------------------------------------------ */
/*  Data types                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    int16_t  x;
    int16_t  y;
    int16_t  speed;
    uint16_t resolution;
    bool     present;
} Target;

typedef struct {
    Gui*              gui;
    ViewPort*         view_port;
    FuriMessageQueue* queue;

    FuriHalSerialHandle* serial;
    FuriStreamBuffer*    stream;

    uint8_t  fbuf[FRAME_LEN];
    uint8_t  hdr_match;
    uint16_t fpos;
    bool     in_frame;

    Target     targets[MAX_TARGETS];
    FuriMutex* mtx;
    uint32_t   last_tick;

    uint8_t range_idx;
    bool    running;
} App;

/* ------------------------------------------------------------------ */
/*  UART callback  (interrupt context)                                  */
/* ------------------------------------------------------------------ */

static void uart_rx_cb(FuriHalSerialHandle* handle,
                       FuriHalSerialRxEvent  event,
                       void*                ctx) {
    App* app = ctx;
    if(event & FuriHalSerialRxEventData) {
        uint8_t b = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(app->stream, &b, 1, 0);
    }
}

/* ------------------------------------------------------------------ */
/*  Frame parser  (main-thread context)                                 */
/* ------------------------------------------------------------------ */

/*
 * Per HLK spec (manual p.15-16):
 *   MSB = 1  →  positive value:  raw - 32768  (= raw & 0x7FFF)
 *   MSB = 0  →  negative value:  0 - raw      (negate)
 * Works for X, Y (always MSB=1), and Speed.
 */
static int16_t decode_coord(uint16_t raw) {
    return (raw & 0x8000) ? (int16_t)(raw & 0x7FFF) : -(int16_t)raw;
}

static void commit_frame(App* app) {
    Target tmp[MAX_TARGETS];
    for(int i = 0; i < MAX_TARGETS; i++) {
        const uint8_t* d = &app->fbuf[4 + i * 8];
        uint16_t rx = (uint16_t)(d[0] | ((uint16_t)d[1] << 8));
        uint16_t ry = (uint16_t)(d[2] | ((uint16_t)d[3] << 8));
        uint16_t rs = (uint16_t)(d[4] | ((uint16_t)d[5] << 8));
        uint16_t rr = (uint16_t)(d[6] | ((uint16_t)d[7] << 8));
        if(rx == 0 && ry == 0 && rs == 0) {
            tmp[i].present = false;
        } else {
            tmp[i].x          = decode_coord(rx);
            tmp[i].y          = decode_coord(ry);   /* Y: MSB always 1 */
            tmp[i].speed      = decode_coord(rs);
            tmp[i].resolution = rr;
            tmp[i].present    = true;
        }
    }
    furi_mutex_acquire(app->mtx, FuriWaitForever);
    memcpy(app->targets, tmp, sizeof(tmp));
    app->last_tick = furi_get_tick();
    furi_mutex_release(app->mtx);
}

static void process_byte(App* app, uint8_t b) {
    if(!app->in_frame) {
        if(b == FRAME_HDR[app->hdr_match]) {
            app->fbuf[app->hdr_match] = b;
            app->hdr_match++;
            if(app->hdr_match == 4) {
                app->in_frame  = true;
                app->fpos      = 4;
                app->hdr_match = 0;
            }
        } else {
            app->hdr_match = (b == FRAME_HDR[0]) ? 1 : 0;
            if(app->hdr_match == 1) app->fbuf[0] = b;
        }
    } else {
        app->fbuf[app->fpos++] = b;
        if(app->fpos == FRAME_LEN) {
            if(app->fbuf[28] == FRAME_FTR[0] &&
               app->fbuf[29] == FRAME_FTR[1])
                commit_frame(app);
            app->in_frame = false;
            app->fpos     = 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Drawing primitives — integer only, no math.h                        */
/* ------------------------------------------------------------------ */

static int32_t isqrt(int32_t n) {
    if(n <= 0) return 0;
    int32_t x = n, y = (n + 1) >> 1;
    while(y < x) { x = y; y = (x + n / x) >> 1; }
    return x;
}

/* Upper semicircle arc centred at (cx, cy) with radius r.
 * dotted=true → draw 2, skip 2 pattern. */
static void draw_arc(Canvas* canvas,
                     int32_t cx, int32_t cy, int32_t r,
                     bool dotted) {
    for(int32_t dx = -r; dx <= r; dx++) {
        int32_t dy = isqrt(r * r - dx * dx);
        int32_t x  = cx + dx;
        int32_t y  = cy - dy;
        if(!dotted || (((dx + r) & 3) < 2)) {
            if(x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H)
                canvas_draw_dot(canvas, x, y);
        }
    }
}

/* Dotted Bresenham line (draw 2 px, skip 2 px). */
static void draw_dotted_line(Canvas* canvas,
                             int32_t x0, int32_t y0,
                             int32_t x1, int32_t y1) {
    int32_t dx  =  (x1 > x0 ? x1 - x0 : x0 - x1);
    int32_t sx  =  (x0 < x1 ?  1 : -1);
    int32_t dy  = -(y1 > y0 ? y1 - y0 : y0 - y1);
    int32_t sy  =  (y0 < y1 ?  1 : -1);
    int32_t err = dx + dy;
    int32_t tog = 0;
    for(;;) {
        if((tog & 3) < 2 &&
           x0 >= 0 && x0 < SCREEN_W &&
           y0 >= 0 && y0 < SCREEN_H)
            canvas_draw_dot(canvas, x0, y0);
        tog++;
        if(x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if(e2 >= dy) { err += dy; x0 += sx; }
        if(e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* ------------------------------------------------------------------ */
/*  Draw callback                                                        */
/* ------------------------------------------------------------------ */

static void draw_cb(Canvas* canvas, void* ctx) {
    App*    app       = ctx;
    int32_t max_range = RANGES[app->range_idx];

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    /* ---- Radar frame ---- */

    /* Horizontal base line (sensor horizon, full arc diameter) */
    canvas_draw_line(canvas,
                     RADAR_CX - RADAR_CY, RADAR_CY,
                     RADAR_CX + RADAR_CY, RADAR_CY);

    /* Range arcs: inner rings dotted, outermost solid */
    for(int i = 0; i < RANGE_COUNT; i++) {
        if(RANGES[i] > max_range) break;
        int32_t r      = RANGES[i] * RADAR_CY / max_range;
        bool    is_max = (RANGES[i] == max_range);
        draw_arc(canvas, RADAR_CX, RADAR_CY, r, !is_max);
    }

    /* Sector divider lines (dotted) at ±30° and ±60° from forward */
    draw_dotted_line(canvas, RADAR_CX, RADAR_CY,
                     RADAR_CX - S60, RADAR_CY - C60);  /* -60° */
    draw_dotted_line(canvas, RADAR_CX, RADAR_CY,
                     RADAR_CX - S30, RADAR_CY - C30);  /* -30° */
    draw_dotted_line(canvas, RADAR_CX, RADAR_CY,
                     RADAR_CX + S30, RADAR_CY - C30);  /* +30° */
    draw_dotted_line(canvas, RADAR_CX, RADAR_CY,
                     RADAR_CX + S60, RADAR_CY - C60);  /* +60° */

    /* Centre line (0° / straight ahead) — solid */
    canvas_draw_line(canvas, RADAR_CX, RADAR_CY, RADAR_CX, 0);

    /* Sensor dot */
    canvas_draw_disc(canvas, RADAR_CX, RADAR_CY, 2);

    /* Range label — top right, shows current max range */
    canvas_set_font(canvas, FontSecondary);
    {
        char lbl[12];
        snprintf(lbl, sizeof(lbl), "%dft", (int)(max_range / 305));
        canvas_draw_str(canvas, SCREEN_W - 22, 7, lbl);
    }

    /* Separator above status bar */
    canvas_draw_line(canvas, 0, RADAR_CY + 2, SCREEN_W - 1, RADAR_CY + 2);

    /* ---- Acquire target snapshot ---- */
    furi_mutex_acquire(app->mtx, FuriWaitForever);
    Target   snap[MAX_TARGETS];
    uint32_t last_tick = app->last_tick;
    memcpy(snap, app->targets, sizeof(snap));
    furi_mutex_release(app->mtx);

    /* ---- Plot targets ---- */
    int    active  = 0;
    /* Per-target "N:xft,yft": X is negative=left, positive=right; Y is depth.
     * Shown in status bar so you can confirm the X value actually changes
     * when moving left/right (diagnoses decode vs. scale vs. visual issues). */
    char   crd_buf[48];
    size_t crd_len = 0;
    crd_buf[0] = '\0';

    for(int i = 0; i < MAX_TARGETS; i++) {
        if(!snap[i].present) continue;
        active++;

        /* Map mm → pixels using same scale for X and Y (equal aspect) */
        int32_t px = RADAR_CX + (int32_t)snap[i].x * RADAR_CY / max_range;
        int32_t py = RADAR_CY - (int32_t)snap[i].y * RADAR_CY / max_range;

        if(px <  2)           px = 2;
        if(px > SCREEN_W - 3) px = SCREEN_W - 3;
        if(py <  2)           py = 2;
        if(py > RADAR_CY - 2) py = RADAR_CY - 2;

        canvas_draw_disc(canvas, px, py, 4);

        /* Target number — float above-right, nudge inward near edges */
        char num[3] = {(char)('1' + i), '\0', '\0'};
        int32_t lx = px + 6;
        int32_t ly = py - 4;
        if(lx > SCREEN_W - 6) lx = px - 10;
        if(ly < 7)             ly = py + 10;
        canvas_draw_str(canvas, lx, ly, num);

        /* Accumulate X,Y in dm (100 mm = ~4 in) for status bar.
         * Format: " N:Xdm,Ydm"  X<0=left, X>0=right, Y=depth.
         * dm gives 3x more resolution than feet — 4-inch moves show. */
        if(crd_len < sizeof(crd_buf) - 1) {
            int xdm     = (int)((int32_t)snap[i].x / 100);
            int ydm     = (int)((int32_t)snap[i].y / 100);
            int written = snprintf(
                crd_buf + crd_len, sizeof(crd_buf) - crd_len,
                " %d:%d,%d", i + 1, xdm, ydm);
            if(written > 0) crd_len += (size_t)written;
        }
    }

    /* ---- Status bar ---- */
    canvas_set_font(canvas, FontSecondary);
    if(furi_get_tick() - last_tick > 2000 || last_tick == 0) {
        canvas_draw_str(canvas, 0, STATUS_Y, "No signal");
    } else {
        char hdr[24];
        snprintf(hdr, sizeof(hdr), "%dft T:%d", (int)(max_range / 305), active);
        canvas_draw_str(canvas, 0, STATUS_Y, hdr);
        if(crd_len > 0)
            canvas_draw_str(canvas, 48, STATUS_Y, crd_buf);
    }
}

/* ------------------------------------------------------------------ */
/*  Input callback  (GUI thread)                                        */
/* ------------------------------------------------------------------ */

static void input_cb(InputEvent* event, void* ctx) {
    App* app = ctx;
    furi_message_queue_put(app->queue, event, 0);
}

/* ------------------------------------------------------------------ */
/*  App lifecycle                                                        */
/* ------------------------------------------------------------------ */

static App* app_alloc(void) {
    App* app = malloc(sizeof(App));
    furi_check(app);
    memset(app, 0, sizeof(App));

    app->range_idx = RANGE_COUNT - 1;
    app->running   = true;
    app->mtx       = furi_mutex_alloc(FuriMutexTypeNormal);
    app->stream    = furi_stream_buffer_alloc(STREAM_BUF_SIZE, 1);
    app->queue     = furi_message_queue_alloc(16, sizeof(InputEvent));

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port,  draw_cb,  app);
    view_port_input_callback_set(app->view_port, input_cb, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    furi_check(app->serial);
    furi_hal_serial_init(app->serial, LD2450_BAUD);
    furi_hal_serial_async_rx_start(app->serial, uart_rx_cb, app, false);

    return app;
}

static void app_free(App* app) {
    furi_hal_serial_async_rx_stop(app->serial);
    furi_hal_serial_deinit(app->serial);
    furi_hal_serial_control_release(app->serial);

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);

    furi_message_queue_free(app->queue);
    furi_stream_buffer_free(app->stream);
    furi_mutex_free(app->mtx);
    free(app);
}

/* ------------------------------------------------------------------ */
/*  Entry point                                                         */
/* ------------------------------------------------------------------ */

int32_t ld2450_radar_app(void* p) {
    UNUSED(p);
    App* app = app_alloc();

    while(app->running) {
        uint8_t b;
        while(furi_stream_buffer_receive(app->stream, &b, 1, 0) == 1)
            process_byte(app, b);

        InputEvent ev;
        if(furi_message_queue_get(app->queue, &ev, 5) == FuriStatusOk) {
            if(ev.type == InputTypeShort || ev.type == InputTypeRepeat) {
                switch(ev.key) {
                case InputKeyBack:
                    app->running = false;
                    break;
                case InputKeyUp:
                    if(app->range_idx > 0) app->range_idx--;
                    break;
                case InputKeyDown:
                    if(app->range_idx < RANGE_COUNT - 1) app->range_idx++;
                    break;
                default:
                    break;
                }
            }
        }

        view_port_update(app->view_port);
    }

    app_free(app);
    return 0;
}
