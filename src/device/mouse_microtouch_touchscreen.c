/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          3M MicroTouch Serial emulation.
 *
 * Authors: Cacodemon345, mourix
 *
 *          Copyright 2024 Cacodemon345
 */

/* Reference: https://www.touchwindow.com/mm5/drivers/mtsctlrm.pdf
   (MicroTouch Touch Controllers Reference Guide; command section, pp. 24-81). */

/* MegaPPBox: formats Tablet, Decimal, Hexadecimal, Binary (+ Stream), Zone, Raw and
   Calibrate Raw; modes Stream, Point, Down/Up, Polled (XON), Inactive and Status;
   Calibrate New/Extended/Interactive with the manual's acknowledgements; the
   parameter commands (PL, Ppds[b], SEn, FNnn, AD/AE, ^CFnn, RD) and their NOVRAM.

   TODO:
    - GP/SP blocks are not documented anywhere; GP1 still answers a dummy block.
    - NM (sent by 3M TouchWare) is undocumented; it gets a positive response.
    - Format Raw / Calibrate Raw data are synthesised from the touch position. */
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/mouse.h>
#include <86box/serial.h>
#include <86box/plat.h>
#include <86box/fifo8.h>
#include <86box/fifo.h>
#include <86box/video.h>
#include <86box/nvr.h>
#include <86box/path.h>
#include <stddef.h>
#include "cpu.h"

/* NOVRAM: 0-15 calibration (4 floats); 16 on: the settings Parameter Lock,
   Parameter Set and Sensitivity Set store (older 16-byte files load as defaults). */
#define NVR_SIZE        32
#define NVR_MAGIC       16 /* 'M' when the settings below are valid */
#define NVR_FORMAT      17
#define NVR_MODE        18
#define NVR_FLAGS       19 /* bit 0 = Mode Status, bit 1 = AutoBaud */
#define NVR_BAUD        20 /* Parameter Set rate code, 1-5 */
#define NVR_SENSITIVITY 21
#define NVR_FILTER      22
#define NVR_SERIAL      23 /* parity (bits 0-1), 7 data bits (bit 2), 2 stop bits (bit 3) */

enum mtouch_formats {
    FORMAT_DEC     = 1,
    FORMAT_HEX     = 2,
    FORMAT_RAW     = 3,
    FORMAT_TABLET  = 4,
    FORMAT_BINARY  = 5,
    FORMAT_ZONE    = 6,
    FORMAT_CAL_RAW = 7
};

enum mtouch_modes {
    MODE_DOWNUP   = 1,
    MODE_INACTIVE = 2,
    MODE_POINT    = 3,
    MODE_STREAM   = 4,
    MODE_POLLED   = 5
};

enum mtouch_events {
    EV_NONE = 0,
    EV_DOWN,     /* touchdown */
    EV_CONT,     /* continued touch */
    EV_UP        /* liftoff */
};

enum mtouch_cal {
    CAL_NEW = 1, /* CN: targets at the corners */
    CAL_EXTENDED, /* CX: targets 1/8 inward */
    CAL_INTERACTIVE /* CI: corners, no range check, swaps reversed points */
};

#define TT_WAIT_MS 800 /* a line is written this long after the release, or at the next press */

typedef struct mtouch_trace_t {
    FILE    *fp;
    int      seq, n, marks;
    int      active, seen, lifted, read, lost, check;
    uint32_t h_press, h_release, h_seen, h_lift, check_at;
    uint32_t prev_press, prev_release; /* the previous click, host ms (0: none) */
    double   g_seen, g_lift, g_read;
    int      reports, x, y;
    double   x0, y0, wander; /* guest pixels the touch moved from its touchdown */
    int      rsr_busy;       /* times a byte had to wait for the UART (would have been overwritten) */
} mtouch_trace_t;

static const char *mtouch_identity[] = {
    "A30100", /* SMT2 Serial / SMT3(R)V */
    "A40100", /* SMT2 PCBus */
    "P50100", /* TouchPen 4(+) */
    "Q10100", /* SMT3(R) Serial */
};

typedef struct mouse_microtouch_t {
    char         cmd[256];
    double       abs_x, abs_y;       /* calibrated, 0..1, y from the top */
    double       raw_x, raw_y;       /* before calibration, 0..1 */
    double       last_x, last_y;     /* last touched point (for the liftoff report) */
    double       last_raw_x, last_raw_y;
    bool         in_zone, last_in_zone;
    float        scale_x, scale_y, off_x, off_y;
    int          but, but_old;
    int          baud_rate, cmd_pos;
    uint8_t      format, prev_format, mode;
    uint8_t      id, cal_cntr, cal_type, pen_mode;
    uint8_t      sensitivity, filter, serial_cfg;
    int          touch_samples;      /* reports' worth of samples in the current touch */
    double       cal_x[2], cal_y[2]; /* raw calibration points */
    bool         mode_status, soh, autobaud;
    bool         in_reset, reset, power_on, ut_reset;
    /* Mode Polled */
    bool         xon, pq_down, pq_up;
    double       pq_down_x, pq_down_y, pq_up_x, pq_up_y;
    bool         pq_down_zone, pq_up_zone;
    uint8_t     *nvr;
    char         nvr_path[64];
    serial_t    *serial;
    mtouch_trace_t *tt;
    Fifo8        resp;
    pc_timer_t   host_to_serial_timer;
    pc_timer_t   reset_timer;
} mouse_microtouch_t;

static mouse_microtouch_t *mtouch_inst = NULL;

extern double mouse_x_abs, mouse_y_abs;
extern uint8_t *ram;
extern uint32_t pic_trace_count[16];

/* twring: 3M TWDrv's event ring in guest RAM (256 entries of 10 bytes:
   flags, 0, x16, y16, 0, 0, 0, id; flags 01 = button event, 08 = down, 80 = read
   by a process).  The write index byte follows the ring at +0xa01. */
static uint32_t tw_ring;      /* kernel virtual address of entry 0, 0 = not found */
static int      tw_ring_dump; /* entries to log before each scripted press */
static uint32_t tw_pgdir = 0x00101000; /* swapper_pg_dir (Crown's System.map), physical */

/* Guest kernel virtual -> physical through the kernel page tables (no PAE). */
static int
gv_phys(uint32_t va, uint32_t *pa)
{
    uint32_t lim = mem_size * 1024, pde, pte;

    if ((tw_pgdir + 4096) > lim)
        return 0;
    pde = *(uint32_t *) &ram[tw_pgdir + (va >> 22) * 4];
    if (!(pde & 1))
        return 0;
    if (pde & 0x80) { /* 4 MB page */
        *pa = (pde & 0xffc00000) | (va & 0x3fffff);
        return *pa < lim;
    }
    if (((pde & ~0xfff) + 4096) > lim)
        return 0;
    pte = *(uint32_t *) &ram[(pde & ~0xfff) + ((va >> 12) & 0x3ff) * 4];
    if (!(pte & 1))
        return 0;
    *pa = (pte & ~0xfff) | (va & 0xfff);
    return *pa < lim;
}

static int
gv_byte(uint32_t va, uint8_t *b)
{
    uint32_t pa;

    if (!gv_phys(va, &pa))
        return 0;
    *b = ram[pa];
    return 1;
}

static int
gv_read(uint32_t va, uint8_t *out, int n)
{
    for (int i = 0; i < n; i++)
        if (!gv_byte(va + i, &out[i]))
            return 0;
    return 1;
}

static int
tw_entry_ok(const uint8_t *e)
{
    return !e[1] && !e[6] && !e[7] && !e[8] && !(e[0] & 0x74);
}

static void
tw_ring_find(FILE *log)
{
    static uint8_t buf[0x800000];
    uint32_t       base = 0xc4800000, len = sizeof(buf), mapped = 0;
    int            found = 0;

    tw_ring = 0;
    /* copy the start of the vmalloc area (modules live there) into buf */
    for (uint32_t off = 0; off < len; off += 4096) {
        uint32_t pa;
        if (gv_phys(base + off, &pa) && ((pa & ~0xfff) + 4096) <= mem_size * 1024) {
            memcpy(&buf[off], &ram[pa & ~0xfff], 4096);
            mapped++;
        } else
            memset(&buf[off], 0xff, 4096);
    }
    fprintf(log, "      TWRING: %u pages mapped in %08x-%08x\n", mapped, base, base + len);
    for (uint32_t a = 0; (a + 0xa40 + 48) <= len; a += 2) {
        int ok = 1, events = 0;
        for (int i = 0; i < 256 && ok; i++) {
            const uint8_t *e = &buf[a + i * 10];
            ok = tw_entry_ok(e);
            events += ((e[0] & 0x81) == 0x81) && (e[2] | e[3] | e[4] | e[5]);
        }
        if (ok && events >= 4) {
            /* the scan can start early on zero bytes: entry 0 is the first non-empty one */
            uint32_t r = a, used = 0;
            while ((r < a + 20) && !(buf[r] | buf[r + 2] | buf[r + 3] | buf[r + 4] | buf[r + 5]))
                r += 2;
            for (int i = 0; i < 256; i++)
                used += (buf[r + i * 10] | buf[r + i * 10 + 2] | buf[r + i * 10 + 4]) != 0;
            fprintf(log, "      TWRING candidate at %08x (scan %08x): %d read button events, %u entries used, byte at +0xa01 = %u\n",
                    base + r, base + a, events, used, buf[r + 0xa01]);
            fprintf(log, "        +0x9f0:");
            for (int i = 0x9f0; i < 0xa60; i++)
                fprintf(log, "%s%02x", (i % 16) ? " " : "\n          ", buf[r + i]);
            fprintf(log, "\n        entries 0-7:");
            for (int i = 0; i < 8; i++)
                fprintf(log, " [%02x %u,%u id %02x]", buf[r + i * 10], buf[r + i * 10 + 2] | (buf[r + i * 10 + 3] << 8),
                        buf[r + i * 10 + 4] | (buf[r + i * 10 + 5] << 8), buf[r + i * 10 + 9]);
            fprintf(log, "\n");
            /* entry 0 is the first touchdown ever written */
            while ((r < a + 60) && ((buf[r] & 0x09) != 0x09))
                r += 10;
            fprintf(log, "        ring starts at %08x, write index %u, readers pid %u idx %u, pid %u idx %u\n", base + r,
                    buf[r + 0xa01], *(uint32_t *) &buf[r + 0xa20], buf[r + 0xa28], *(uint32_t *) &buf[r + 0xa2c], buf[r + 0xa34]);
            if (!found++)
                tw_ring = base + r;
            a += 2560;
        }
    }
    if (!found)
        fprintf(log, "      TWRING: not found\n");
    fflush(log);
}

static void
tw_ring_log(FILE *log, int n)
{
    uint8_t w, e[10], rd[4];

    if (!tw_ring || !gv_byte(tw_ring + 0xa01, &w))
        return;
    /* per-process read positions: table at bss+0xa40 (= ring+0xa20), 12 bytes each, index at +8 */
    for (int i = 0; i < 4; i++)
        if (!gv_byte(tw_ring + 0xa20 + i * 12 + 8, &rd[i]))
            rd[i] = 0xff;
    fprintf(log, "      TWRING w=%3u read=%u,%u,%u,%u:", w, rd[0], rd[1], rd[2], rd[3]);
    for (int i = n; i > 0; i--) {
        if (!gv_read(tw_ring + ((uint8_t) (w - i)) * 10, e, 10))
            continue;
        fprintf(log, " [%s%s%s %u,%u]", (e[0] & 0x80) ? "r" : "-", (e[0] & 0x01) ? ((e[0] & 0x08) ? "DN" : "UP") : "mv",
                (e[0] & 0x02) ? "R" : "", e[2] | (e[3] << 8), e[4] | (e[5] << 8));
    }
    fprintf(log, "\n");
    fflush(log);
}

/* irqrate: interrupts acknowledged per IRQ over a window */
static struct {
    uint32_t   start[16];
    double     g0;
    pc_timer_t timer;
    FILE      *log;
} irq_rt;

static double tt_guest_ms(void);

static void
irq_rate_done(UNUSED(void *priv))
{
    double secs = (tt_guest_ms() - irq_rt.g0) / 1000.;

    fprintf(irq_rt.log, "      IRQRATE over %.1f guest s (per second):", secs);
    for (int i = 0; i < 16; i++)
        if (pic_trace_count[i] != irq_rt.start[i])
            fprintf(irq_rt.log, " IRQ%d=%.1f", i, (pic_trace_count[i] - irq_rt.start[i]) / secs);
    fprintf(irq_rt.log, "\n");
    fflush(irq_rt.log);
}

#define SMP_BIN 50
#define SMP_MAX 2000
#define SMP_TOP 64
static struct {
    int        ms, left, t;
    int        bin[SMP_MAX / SMP_BIN][3]; /* idle, kernel, user */
    uint32_t   cr3[SMP_TOP], kpg[SMP_TOP];
    int        cr3n[SMP_TOP], kpgn[SMP_TOP];
    pc_timer_t timer;
    FILE      *log;
} smp;

static void
smp_count(uint32_t *keys, int *counts, uint32_t k)
{
    for (int i = 0; i < SMP_TOP; i++) {
        if (counts[i] && (keys[i] == k)) {
            counts[i]++;
            return;
        }
        if (!counts[i]) {
            keys[i]   = k;
            counts[i] = 1;
            return;
        }
    }
}

static void
smp_top(FILE *log, const char *what, uint32_t *keys, int *counts)
{
    fprintf(log, "        %s:", what);
    for (int n = 0; n < 5; n++) {
        int best = -1;
        for (int i = 0; i < SMP_TOP; i++)
            if (counts[i] > 0 && (best < 0 || counts[i] > counts[best]))
                best = i;
        if (best < 0)
            break;
        fprintf(log, " %08x x%d", keys[best], counts[best]);
        counts[best] = -counts[best];
    }
    for (int i = 0; i < SMP_TOP; i++)
        if (counts[i] < 0)
            counts[i] = -counts[i];
    fprintf(log, "\n");
}

static void
smp_tick(UNUSED(void *priv))
{
    int      b    = smp.t / SMP_BIN;
    uint32_t lin  = cpu_state.seg_cs.base + cpu_state.pc;

    if (CPL == 0) {
        uint32_t ph = lin - 0xc0000000;
        int      halted = (ph >= 1) && (ph < mem_size * 1024) && ((ram[ph] == 0xf4) || (ram[ph - 1] == 0xf4));

        smp.bin[b][halted ? 0 : 1]++;
        if (!halted)
            smp_count(smp.kpg, smp.kpgn, lin & ~0xff);
    } else {
        smp.bin[b][2]++;
        smp_count(smp.cr3, smp.cr3n, cr3);
    }
    smp.t++;
    if (--smp.left > 0) {
        timer_on_auto(&smp.timer, 1000.);
        return;
    }
    fprintf(smp.log, "      SAMPLE %d ms after the press, per %d ms (idle/kernel/user %%):", smp.t, SMP_BIN);
    for (int i = 0; i * SMP_BIN < smp.t; i++) {
        int n = smp.bin[i][0] + smp.bin[i][1] + smp.bin[i][2];
        if (n)
            fprintf(smp.log, " %d/%d/%d", smp.bin[i][0] * 100 / n, smp.bin[i][1] * 100 / n, smp.bin[i][2] * 100 / n);
    }
    fprintf(smp.log, "\n");
    smp_top(smp.log, "user cr3", smp.cr3, smp.cr3n);
    smp_top(smp.log, "kernel eip/256", smp.kpg, smp.kpgn);
    fflush(smp.log);
}

static void
smp_start(FILE *log)
{
    if (smp.ms <= 0)
        return;
    int ms = smp.ms;
    pc_timer_t t = smp.timer;
    memset(&smp, 0, sizeof(smp));
    smp.timer = t;
    smp.ms    = ms;
    smp.left  = (ms > SMP_MAX) ? SMP_MAX : ms;
    smp.log   = log;
    timer_on_auto(&smp.timer, 1000.);
}
static double tt_guest_ms(void);

/* MegaPPBox touch trace: scripted touches for testing, read from touchcmd.txt
   in the VM folder (the file is deleted once read).  Positions are percent of
   the guest screen; times are host ms.
       tap X Y HOLD
       cycle PERIOD HOLD COUNT X1 Y1 [X2 Y2 ...]   (COUNT 0 = until stop)
       stop
       probe GX GY        (guest pixel; before each scripted tap, and one period
                           after the last, the log lists the probes that are
                           yellow -- a selected tile's frame)
       probe clear
       jitter PX          (while a scripted touch is held, it wanders up to PX
                           guest pixels from its point; 0 = still)
       twparams           (find 3M TWDrv's settings block in guest RAM and log
                           it: TouchMode, TouchRange, ... TouchEnable)
       sample MS          (after each scripted press, sample the guest CPU every
                           1 ms for MS ms: idle (HLT) / kernel / user per 50 ms,
                           and the busiest user page tables and kernel pages)
       seq HOLD D1 X1 Y1 D2 X2 Y2 ...   (touch k presses Dk ms after touch
                           k-1 was released -- D1 after now; probes are logged
                           before each touch and 1500 ms after the last)    */
#define TA_PTS 16
static struct {
    int      on, down, n, idx, count, done;
    uint32_t period, hold, next, up, check;
    double   x[TA_PTS], y[TA_PTS];
    int      nprobe, px[TA_PTS], py[TA_PTS], final, jitter;
    int      watch;      /* probe to watch after the current tap, -1 = none */
    int      seq;        /* seq command: delays are counted from the release */
    uint32_t delay[TA_PTS];
    uint32_t watch_from; /* host ms of that tap's press */
    double   watch_g;    /* guest ms of that tap's press */
} ta;

static int
ta_probe_yellow(int i)
{
    const monitor_t *m = &monitors[0];
    const bitmap_t  *b = m->target_buffer;
    int              x = m->mon_overscan_x / 2 + ta.px[i], y = m->mon_overscan_y / 2 + ta.py[i];

    if (!b || (x >= b->w) || (y >= b->h) || (y >= 2112))
        return 0;
    uint32_t c = b->line[y][x];
    return (((c >> 16) & 0xff) > 200) && (((c >> 8) & 0xff) > 200) && ((c & 0xff) < 80);
}

static void
ta_log_probes(FILE *log, const char *when)
{
    const monitor_t *m = &monitors[0];
    const bitmap_t  *b = m->target_buffer;
    int              ox = m->mon_overscan_x / 2, oy = m->mon_overscan_y / 2;
    char             buf[128] = "";

    if (!ta.nprobe)
        return;
    for (int i = 0; i < ta.nprobe; i++) {
        int x = ox + ta.px[i], y = oy + ta.py[i];

        if (!b || (x >= b->w) || (y >= b->h) || (y >= 2112))
            continue;
        uint32_t c = b->line[y][x];
        if ((((c >> 16) & 0xff) > 200) && (((c >> 8) & 0xff) > 200) && ((c & 0xff) < 80))
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " %d", i);
    }
    fprintf(log, "      PROBE %s: yellow%s\n", when, buf[0] ? buf : " none");
    fflush(log);
}

static void
ta_command(FILE *log, char *line)
{
    char  *tok[3 + 2 * TA_PTS + 1];
    int    n = 0;

    for (char *t = strtok(line, " \t\r\n"); t && (n < (int) (sizeof(tok) / sizeof(tok[0]))); t = strtok(NULL, " \t\r\n"))
        tok[n++] = t;
    if (!n)
        return;
    if (!strcmp(tok[0], "probe") && (n >= 2)) {
        if (!strcmp(tok[1], "clear"))
            ta.nprobe = 0;
        else if ((n >= 3) && (ta.nprobe < TA_PTS)) {
            ta.px[ta.nprobe] = atoi(tok[1]);
            ta.py[ta.nprobe] = atoi(tok[2]);
            ta.nprobe++;
        }
        return;
    }
    if (!strcmp(tok[0], "twring")) {
        tw_ring_find(log);
        tw_ring_dump = (n >= 2) ? atoi(tok[1]) : 0;
        return;
    }
    if (!strcmp(tok[0], "irqrate") && (n >= 2)) {
        memcpy(irq_rt.start, pic_trace_count, sizeof(irq_rt.start));
        irq_rt.g0  = tt_guest_ms();
        irq_rt.log = log;
        timer_on_auto(&irq_rt.timer, atoi(tok[1]) * 1000.);
        return;
    }
    if (!strcmp(tok[0], "sample") && (n >= 2)) {
        smp.ms = atoi(tok[1]);
        fprintf(log, "      CMD sample %d\n", smp.ms);
        fflush(log);
        return;
    }
    if (!strcmp(tok[0], "twparams")) {
        static const char *names[20] = { "TouchMode", "TouchRange", "VertOffEn", "VertOffset", "HorizOffEn",
                                         "XIsHoriz", "FlipX", "FlipY", "DoubleClickTime", "DoubleClickDist",
                                         "SteadyTime", "LiftoffTimeout", "TdFlashTime", "LoFlashTime",
                                         "SwapButton", "BeepTd", "BeepLo", "BeepFreq", "BeepMs", "TouchEnable" };
        int hits = 0;

        for (uint32_t a = 0; (a + 80) <= (mem_size * 1024) && hits < 8; a += 4) {
            const uint32_t *w = (const uint32_t *) &ram[a];
            /* TouchMode <= 6, flags 0/1, BeepFreq <= 5000, BeepMs <= 5000, TouchEnable 0/1,
               DoubleClickDist <= 1280, times <= 50, and a plausible TouchRange. */
            if ((w[0] <= 6) && (w[1] <= 256) && (w[1] > 0) && (w[4] <= 1) && (w[5] <= 1) && (w[6] <= 1) &&
                (w[7] <= 1) && (w[8] <= 50) && (w[9] <= 1280) && (w[9] > 0) && (w[10] <= 50) && (w[11] <= 50) &&
                (w[12] <= 50) && (w[13] <= 50) && (w[14] <= 1) && (w[15] <= 1) && (w[16] <= 1) &&
                (w[17] >= 100) && (w[17] <= 5000) && (w[18] <= 5000) && (w[19] <= 1)) {
                fprintf(log, "      TWPARAMS at %08x:", a);
                for (int i = 0; i < 20; i++)
                    fprintf(log, " %s=%u", names[i], w[i]);
                fprintf(log, "\n");
                hits++;
            }
        }
        if (!hits)
            fprintf(log, "      TWPARAMS: not found\n");
        fflush(log);
        return;
    }
    if (!strcmp(tok[0], "jitter") && (n >= 2)) {
        ta.jitter = atoi(tok[1]);
        fprintf(log, "      CMD jitter %d\n", ta.jitter);
        fflush(log);
        return;
    }
    if (!strcmp(tok[0], "stop")) {
        ta.on = 0;
        if (ta.down)
            mouse_set_buttons_ex(0);
        ta.down = 0;
    } else if (!strcmp(tok[0], "tap") && (n >= 4)) {
        memset(&ta, 0, offsetof(typeof(ta), nprobe));
        ta.watch  = -1;
        ta.x[0]   = atof(tok[1]) / 100.;
        ta.y[0]   = atof(tok[2]) / 100.;
        ta.hold   = atoi(tok[3]);
        ta.period = ta.hold + 1;
        ta.n = ta.count = 1;
        ta.on           = 1;
        ta.next         = plat_get_ticks();
    } else if (!strcmp(tok[0], "seq") && (n >= 5)) {
        memset(&ta, 0, offsetof(typeof(ta), nprobe));
        ta.watch = -1;
        ta.seq   = 1;
        ta.hold  = atoi(tok[1]);
        for (int i = 2; (i + 2) < n && ta.n < TA_PTS; i += 3) {
            ta.delay[ta.n] = atoi(tok[i]);
            ta.x[ta.n]     = atof(tok[i + 1]) / 100.;
            ta.y[ta.n]     = atof(tok[i + 2]) / 100.;
            ta.n++;
        }
        ta.count = ta.n;
        ta.on    = 1;
        ta.next  = plat_get_ticks() + ta.delay[0];
    } else if (!strcmp(tok[0], "cycle") && (n >= 6)) {
        memset(&ta, 0, offsetof(typeof(ta), nprobe));
        ta.watch  = -1;
        ta.period = atoi(tok[1]);
        ta.hold   = atoi(tok[2]);
        ta.count  = atoi(tok[3]);
        for (int i = 4; (i + 1) < n && ta.n < TA_PTS; i += 2) {
            ta.x[ta.n] = atof(tok[i]) / 100.;
            ta.y[ta.n] = atof(tok[i + 1]) / 100.;
            ta.n++;
        }
        ta.on   = 1;
        ta.next = plat_get_ticks();
    } else
        return;
    fprintf(log, "      CMD %s%s%s ...\n", tok[0], (n > 1) ? " " : "", (n > 1) ? tok[1] : "");
    fflush(log);
}

/* Called at each poll before the buttons are read. */
static void
ta_poll(mouse_microtouch_t *dev)
{
    uint32_t now = plat_get_ticks();

    if ((int32_t) (now - ta.check) >= 0) {
        char  path[1024], line[512];
        FILE *fp;

        ta.check = now + 100;
        path_append_filename(path, usr_path, "touchcmd.txt");
        if ((fp = fopen(path, "r"))) {
            while (fgets(line, sizeof(line), fp))
                ta_command(dev->tt->fp, line);
            fclose(fp);
            remove(path);
        }
    }
    if (!ta.on)
        return;
    if ((ta.watch >= 0) && ta_probe_yellow(ta.watch)) {
        fprintf(dev->tt->fp, "      REACT: tile %d yellow %u ms after its press (%.0f guest ms)\n",
                ta.watch, now - ta.watch_from, tt_guest_ms() - ta.watch_g);
        ta.watch = -1;
    }
    if (ta.down) {
        mouse_x_abs = ta.x[ta.idx];
        mouse_y_abs = ta.y[ta.idx];
        if (ta.jitter) {
            mouse_x_abs += ((rand() % (2 * ta.jitter + 1)) - ta.jitter) / 640.;
            mouse_y_abs += ((rand() % (2 * ta.jitter + 1)) - ta.jitter) / 480.;
        }
        if ((int32_t) (now - ta.up) >= 0) {
            mouse_set_buttons_ex(0);
            ta.down = 0;
            ta.idx  = (ta.idx + 1) % ta.n;
            if (ta.count && (++ta.done >= ta.count))
                ta.final = 1; /* one more period, then the last probe */
            if (ta.seq)
                ta.next = now + (ta.final ? 1500 : ta.delay[ta.idx]);
        }
    } else if ((int32_t) (now - ta.next) >= 0) {
        ta_log_probes(dev->tt->fp, ta.final ? "after the last tap" : "before tap");
        if (tw_ring_dump)
            tw_ring_log(dev->tt->fp, tw_ring_dump);
        if (ta.final) {
            ta.on = ta.final = 0;
            return;
        }
        mouse_x_abs = ta.x[ta.idx];
        mouse_y_abs = ta.y[ta.idx];
        mouse_set_buttons_ex(1);
        if (ta.watch >= 0)
            fprintf(dev->tt->fp, "      REACT: tile %d not yellow within %u ms\n", ta.watch, now - ta.watch_from);
        ta.watch      = (ta.idx < ta.nprobe && !ta_probe_yellow(ta.idx)) ? ta.idx : -1;
        ta.watch_from = now;
        ta.watch_g    = tt_guest_ms();
        smp_start(dev->tt->fp);
        ta.down = 1;
        ta.up   = now + ta.hold;
        ta.next = now + ta.period;
    }
}

static double
tt_guest_ms(void)
{
    return (cpuclock > 0.) ? ((double) tsc * 1000. / cpuclock) : 0.;
}

/* Finish the tracked click and write its line. */
static void
tt_finish(mouse_microtouch_t *dev, UNUSED(uint32_t now))
{
    mtouch_trace_t *t = dev->tt;
    char            scr[64] = "first click";

    if (t->prev_press)
        snprintf(scr, sizeof(scr), "%5u ms after the previous release (%5u after its press)",
                 t->h_press - t->prev_release, t->h_press - t->prev_press);
    if (!t->lost && !t->seen)
        fprintf(t->fp, "#%-4d host pressed, no poll saw it before the next click | %s\n", t->n, scr);
    else if (!t->lost && !t->lifted)
        fprintf(t->fp, "#%-4d touch seen +%u ms, %d reports, NO LIFTOFF before the next click | %s\n",
                t->n, t->h_seen - t->h_press, t->reports, scr);
    else if (t->lost)
        fprintf(t->fp, "#%-4d host held %4u ms | LOST: pressed and released between two polls | %s\n",
                t->n, t->h_release - t->h_press, scr);
    else
        fprintf(t->fp, "#%-4d host held %4u ms, poll saw it +%3u ms | touch %6.1f ms guest (%4u ms host), %3d reports, "
                "at %3d%%,%3d%% moved %4.1f px | UART waits %d | liftoff read %s%5.1f ms after sent | %s\n",
                t->n, t->h_release - t->h_press, t->h_seen - t->h_press, t->g_lift - t->g_seen, t->h_lift - t->h_seen,
                t->reports, t->x, t->y, t->wander, t->rsr_busy, t->read ? "" : "NOT YET ", t->read ? (t->g_read - t->g_lift) : 0., scr);
    fflush(t->fp);
    t->active = t->check = 0;
}

/* Called at each poll, after the button state is read. */
static void
tt_poll(mouse_microtouch_t *dev)
{
    mtouch_trace_t *t = dev->tt;
    uint32_t        press, release, now = plat_get_ticks();
    int             seq;

    tablet_get_trace(&seq, &press, &release);
    if (tablet_get_marks() != t->marks) {
        if (t->active)
            tt_finish(dev, now);
        t->marks = tablet_get_marks();
        fprintf(t->fp, "      MARK (middle click) after #%d\n", t->n);
        fflush(t->fp);
    }
    if (seq != t->seq) {
        if (t->active)
            tt_finish(dev, now); /* the next click came before the screen check */
        if ((seq - t->seq) > 1)
            fprintf(t->fp, "      (%d more host presses since the last poll, all lost)\n", seq - t->seq - 1);
        FILE    *fp        = t->fp;
        uint32_t prev_p    = t->h_press;
        uint32_t prev_r    = t->h_release;
        int      have_prev = (t->n != 0);
        int      marks     = t->marks;

        memset(t, 0, sizeof(mtouch_trace_t));
        t->fp           = fp;
        t->marks        = marks;
        t->seq          = seq;
        t->n            = seq; /* click number = host press count */
        t->active       = 1;
        t->h_press      = press;
        t->prev_press   = have_prev ? prev_p : 0;
        t->prev_release = have_prev ? prev_r : 0;
    }
    if (!t->active)
        return;
    if ((dev->but & 1) && !t->seen) {
        t->seen   = 1;
        t->h_seen = now;
        t->g_seen = tt_guest_ms();
        t->x      = (int) (dev->abs_x * 100.);
        t->y      = (int) (dev->abs_y * 100.);
        t->x0     = dev->abs_x;
        t->y0     = dev->abs_y;
    }
    if ((dev->but & 1) && t->seen && !t->lifted) {
        double dx = (dev->abs_x - t->x0) * 640., dy = (dev->abs_y - t->y0) * 480.;
        double d  = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
        if (d > t->wander)
            t->wander = d;
    }
    if (!(dev->but & 1) && !t->seen && ((int32_t) (release - press) >= 0) && !t->check) {
        t->lost      = 1;
        t->h_release = release;
        t->check     = 1;
        t->check_at  = now + TT_WAIT_MS;
    }
    if (t->check && ((int32_t) (now - t->check_at) >= 0))
        tt_finish(dev, now);
}


static void
mtouch_savenvr(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;

    FILE *fp;

    fp = nvr_fopen(dev->nvr_path, "wb");
    if (fp) {
        fwrite(dev->nvr, 1, NVR_SIZE, fp);
        fclose(fp);
        fp = NULL;
    }
}

static void
mtouch_writenvr(void *priv, float scale_x, float scale_y, float off_x, float off_y)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;

    memcpy(&dev->nvr[0], &scale_x, 4);
    memcpy(&dev->nvr[4], &scale_y, 4);
    memcpy(&dev->nvr[8], &off_x, 4);
    memcpy(&dev->nvr[12], &off_y, 4);
}

static void
mtouch_readnvr(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;
    memcpy(&dev->scale_x, &dev->nvr[0], 4);
    memcpy(&dev->scale_y, &dev->nvr[4], 4);
    memcpy(&dev->off_x, &dev->nvr[8], 4);
    memcpy(&dev->off_y, &dev->nvr[12], 4);

    pclog("MT NVR CAL: scale_x=%f, scale_y=%f, off_x=%f, off_y=%f\n", dev->scale_x, dev->scale_y, dev->off_x, dev->off_y);
}

static void
mtouch_initnvr(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;
    FILE *fp;

    /* Allocate and initialize the EEPROM. */
    dev->nvr = (uint8_t *) calloc(1, NVR_SIZE);

    fp = nvr_fopen(dev->nvr_path, "rb");
    if (fp) {
        /* Files from before the settings bytes hold only the calibration. */
        if (fread(dev->nvr, 1, NVR_SIZE, fp) < 16)
            mtouch_writenvr(dev, 1, 1, 0, 0);
        fclose(fp);
        fp = NULL;
    } else
        mtouch_writenvr(dev, 1, 1, 0, 0);
}

static int
mtouch_baud_from_code(char c)
{
    switch (c) {
        case '1': return 19200;
        case '3': return 4800;
        case '4': return 2400;
        case '5': return 1200;
        default:  return 9600;
    }
}

static char
mtouch_code_from_baud(int baud)
{
    switch (baud) {
        case 19200: return '1';
        case 4800:  return '3';
        case 2400:  return '4';
        case 1200:  return '5';
        default:    return '2';
    }
}

/* Parameter Lock, Parameter Set and Sensitivity Set store these. */
static void
mtouch_store_settings(mouse_microtouch_t *dev)
{
    dev->nvr[NVR_MAGIC]       = 'M';
    dev->nvr[NVR_FORMAT]      = dev->format;
    dev->nvr[NVR_MODE]        = dev->mode;
    dev->nvr[NVR_FLAGS]       = (dev->mode_status ? 1 : 0) | (dev->autobaud ? 2 : 0);
    dev->nvr[NVR_BAUD]        = (uint8_t) mtouch_code_from_baud(dev->baud_rate);
    dev->nvr[NVR_SENSITIVITY] = dev->sensitivity;
    dev->nvr[NVR_FILTER]      = dev->filter;
    dev->nvr[NVR_SERIAL]      = dev->serial_cfg;
    mtouch_savenvr(dev);
}

static void
mtouch_set_baud(mouse_microtouch_t *dev, int baud)
{
    dev->baud_rate = baud;
    timer_stop(&dev->host_to_serial_timer);
    timer_on_auto(&dev->host_to_serial_timer, (1000000. / dev->baud_rate) * 10);
}

/* Factory defaults (Table 11): SMT2 Decimal, the others Tablet; Mode Stream;
   Pen or Finger; 9600 baud; normal sensitivity. */
static void
mtouch_defaults(mouse_microtouch_t *dev)
{
    dev->format      = (dev->id < 2) ? FORMAT_DEC : FORMAT_TABLET;
    dev->prev_format = dev->format;
    dev->mode        = MODE_STREAM;
    dev->mode_status = false;
    dev->autobaud    = false;
    dev->pen_mode    = 3;
    dev->sensitivity = 0;
    dev->filter      = 0;
    dev->serial_cfg  = (dev->id < 2) ? 0x0c : 0x00; /* SMT2: N72, SMT3: N81 */
}

static void
mtouch_load_settings(mouse_microtouch_t *dev)
{
    mtouch_defaults(dev);
    if (dev->nvr[NVR_MAGIC] != 'M')
        return;
    if ((dev->nvr[NVR_FORMAT] >= FORMAT_DEC) && (dev->nvr[NVR_FORMAT] <= FORMAT_ZONE) && (dev->nvr[NVR_FORMAT] != FORMAT_RAW))
        dev->format = dev->prev_format = dev->nvr[NVR_FORMAT];
    if ((dev->nvr[NVR_MODE] >= MODE_DOWNUP) && (dev->nvr[NVR_MODE] <= MODE_POLLED))
        dev->mode = dev->nvr[NVR_MODE];
    dev->mode_status = !!(dev->nvr[NVR_FLAGS] & 1);
    dev->autobaud    = !!(dev->nvr[NVR_FLAGS] & 2);
    dev->baud_rate   = mtouch_baud_from_code((char) dev->nvr[NVR_BAUD]);
    dev->sensitivity = dev->nvr[NVR_SENSITIVITY] & 3;
    dev->filter      = dev->nvr[NVR_FILTER];
    dev->serial_cfg  = dev->nvr[NVR_SERIAL];
}

static void
mtouch_ack(mouse_microtouch_t *dev, char c)
{
    fifo8_push(&dev->resp, 0x01);
    fifo8_push(&dev->resp, (uint8_t) c);
    fifo8_push(&dev->resp, 0x0d);
}

static void
mtouch_reset_complete(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;

    dev->reset = true;
    dev->ut_reset = true;
    dev->in_reset = false;
    mtouch_ack(dev, '0');
}

/* ---- reports ---------------------------------------------------------------- */

/* Status byte of Mode Status / Format Binary: ^Y touchdown, ^\ continued, ^R liftoff. */
static uint8_t
mtouch_status_byte(int ev)
{
    return (ev == EV_DOWN) ? 0x19 : ((ev == EV_UP) ? 0x18 : 0x1c);
}

static void
mtouch_push_1023(mouse_microtouch_t *dev, int v)
{
    fifo8_push(&dev->resp, 0x20 | ((v >> 5) & 0x1f));
    fifo8_push(&dev->resp, 0x20 | (v & 0x1f));
}

/* Calibrate Raw coordinate: 10 bits and a sign, -1024..1023. */
static void
mtouch_push_signed(mouse_microtouch_t *dev, double v01)
{
    int v = (int) (v01 * 2047.) - 1024;

    if (v < -1024)
        v = -1024;
    if (v > 1023)
        v = 1023;
    fifo8_push(&dev->resp, ((v & 0x0f) << 3) | ((v >> 4) & 0x07));
    fifo8_push(&dev->resp, ((v < 0) ? 0x40 : 0x00) | (((v >> 7) & 0x07) << 3));
}

static void
mtouch_report(mouse_microtouch_t *dev, int ev, double x, double y, double rx, double ry, bool zone)
{
    char     buffer[16];
    uint16_t x14, y14;
    int      x10, y10;
    int      but = dev->but;

    switch (dev->format) {
        case FORMAT_TABLET:
            /* Bit 3 is set in every Tablet report: 3M's TouchWare driver of 2002
               (TWDrv.o in MAXX Ruby 2 V11.00) reads a clear bit 3 as the 11-byte
               "double touch" report; the 2003 driver calls a report a double touch
               only when bits 4:3 are 10, so both take 0xC8/0x88. */
            x14 = (uint16_t) (16383 * x);
            y14 = (uint16_t) (16383 * (1 - y));
            fifo8_push(&dev->resp, 0x88 | ((ev != EV_UP) ? 0x40 : 0x00) |
                       ((dev->pen_mode == 2) ? ((1 << 5) | ((ev != EV_UP) ? (but & 3) : 0)) : 0));
            fifo8_push(&dev->resp, x14 & 0x7f);
            fifo8_push(&dev->resp, (x14 >> 7) & 0x7f);
            fifo8_push(&dev->resp, y14 & 0x7f);
            fifo8_push(&dev->resp, (y14 >> 7) & 0x7f);
            break;

        case FORMAT_DEC:
        case FORMAT_HEX:
            fifo8_push(&dev->resp, dev->mode_status ? mtouch_status_byte(ev) : 0x01);
            if (dev->format == FORMAT_DEC)
                snprintf(buffer, sizeof(buffer), "%03d,%03d\r", (uint16_t) (999 * x), (uint16_t) (999 * (1 - y)));
            else
                snprintf(buffer, sizeof(buffer), "%03X,%03X\r", (uint16_t) (1023 * x), (uint16_t) (1023 * (1 - y)));
            fifo8_push_all(&dev->resp, (uint8_t *) buffer, strlen(buffer));
            break;

        case FORMAT_BINARY:
        case FORMAT_ZONE:
            x10 = (int) (1023 * x);
            y10 = (int) (1023 * (1 - y));
            if ((dev->format == FORMAT_BINARY) && (ev == EV_DOWN))
                fifo8_push_all(&dev->resp, (uint8_t *) "\x17\x20\x20\x20\x20", 5);
            if ((dev->format == FORMAT_ZONE) && !dev->mode_status) {
                static const char zone_in[]  = { 0, 'D', 'B', 'A' };
                static const char zone_out[] = { 0, 'L', 'J', 'I' };
                fifo8_push(&dev->resp, (uint8_t) (zone ? zone_in[ev] : zone_out[ev]));
            } else
                fifo8_push(&dev->resp, mtouch_status_byte(ev));
            mtouch_push_1023(dev, x10);
            mtouch_push_1023(dev, y10);
            break;

        case FORMAT_CAL_RAW:
            fifo8_push(&dev->resp, 0x80 | ((ev != EV_UP) ? 0x40 : 0x00) |
                       ((dev->pen_mode == 2) ? ((1 << 5) | ((ev != EV_UP) ? (but & 3) : 0)) : 0));
            mtouch_push_signed(dev, rx);
            mtouch_push_signed(dev, 1 - ry);
            break;

        default:
            break;
    }
}

/* Format Raw: the four corner signal levels, one 7-byte packet per call. */
static void
mtouch_report_raw(mouse_microtouch_t *dev)
{
    double x = dev->raw_x, y = dev->raw_y, amount = dev->but ? (500. + 100. * dev->sensitivity) : 0.;
    int    ul = 200 + (int) (amount * (1 - x) * (1 - y));
    int    ur = 200 + (int) (amount * x * (1 - y));
    int    ll = 200 + (int) (amount * (1 - x) * y);
    int    lr = 200 + (int) (amount * x * y);

    fifo8_push(&dev->resp, 0x80 | 0x0a); /* drive level */
    fifo8_push(&dev->resp, ((ul >> 7) & 7) | (((ll >> 7) & 7) << 4));
    fifo8_push(&dev->resp, ((lr >> 7) & 7) | (((ur >> 7) & 7) << 4));
    fifo8_push(&dev->resp, ll & 0x7f);
    fifo8_push(&dev->resp, ul & 0x7f);
    fifo8_push(&dev->resp, ur & 0x7f);
    fifo8_push(&dev->resp, lr & 0x7f);
}

/* ---- calibration ------------------------------------------------------------ */

/* A calibration touch ends: the controller uses the point just before liftoff. */
static void
mtouch_calibration_point(mouse_microtouch_t *dev)
{
    int    idx   = 2 - dev->cal_cntr; /* 0 = lower left, 1 = upper right */
    double x     = dev->last_raw_x, y = dev->last_raw_y;
    bool   dec   = (dev->format == FORMAT_DEC) || (dev->format == FORMAT_HEX);
    char   pos   = '1', neg = '0';
    bool   valid;

    /* Calibrate New / Interactive, upper right, Decimal or Hexadecimal: the
       positive response is 0 and the negative 1 (Table 7). */
    if ((dev->cal_type != CAL_EXTENDED) && (idx == 1) && dec) {
        pos = '0';
        neg = '1';
    }

    /* Too short a touch for an accurate point (under ~30 ms: while calibrating
       the controller samples once per byte slot): the SMT3 and TouchPen answer
       2, the others nothing. */
    if (dev->touch_samples < 30) {
        if (dev->id >= 2)
            mtouch_ack(dev, '2');
        return;
    }

    if (dev->cal_type == CAL_INTERACTIVE)
        valid = true;
    else if (idx == 0)
        valid = (x < (1. / 3.)) && (y > (2. / 3.));
    else
        valid = (x > (2. / 3.)) && (y < (1. / 3.));
    if (!valid) {
        mtouch_ack(dev, neg);
        return;
    }

    dev->cal_x[idx] = x;
    dev->cal_y[idx] = y;
    mtouch_ack(dev, pos);
    if (--dev->cal_cntr)
        return;

    double x1 = dev->cal_x[0], y1 = dev->cal_y[0];
    double x2 = dev->cal_x[1], y2 = dev->cal_y[1];
    double inset = (dev->cal_type == CAL_EXTENDED) ? 0.125 : 0.;

    if (dev->cal_type == CAL_INTERACTIVE) {
        /* Swap the points if the lower left and upper right are reversed. */
        if (x1 > x2) {
            double t = x1;
            x1 = x2;
            x2 = t;
        }
        if (y1 < y2) {
            double t = y1;
            y1 = y2;
            y2 = t;
        }
    }
    if ((x2 - x1) < 0.05 || (y1 - y2) < 0.05)
        return; /* the SMT3 keeps its previous calibration (Table 6) */

    dev->scale_x = (float) (((1. - inset) - inset) / (x2 - x1));
    dev->off_x   = (float) (inset - dev->scale_x * x1);
    dev->scale_y = (float) ((inset - (1. - inset)) / (y2 - y1));
    dev->off_y   = (float) ((1. - inset) - dev->scale_y * y1);

    pclog("MT NEW CAL: scale_x=%f, scale_y=%f, off_x=%f, off_y=%f\n", dev->scale_x, dev->scale_y, dev->off_x, dev->off_y);
    mtouch_writenvr(dev, dev->scale_x, dev->scale_y, dev->off_x, dev->off_y);
    mtouch_savenvr(dev);
}

/* ---- commands --------------------------------------------------------------- */

static void
mtouch_set_format(mouse_microtouch_t *dev, uint8_t format)
{
    if ((format == FORMAT_DEC) || (format == FORMAT_HEX) || (format == FORMAT_ZONE)) {
        /* Resets Mode Status (Format Zone reports its zone letters until a
           Mode Status follows); after Format Binary, sets Mode Stream. */
        if (dev->format == FORMAT_BINARY)
            dev->mode = MODE_STREAM;
        dev->mode_status = false;
    }
    if ((format != FORMAT_RAW) && (format != FORMAT_CAL_RAW))
        dev->prev_format = format;
    dev->format = format;
}

static void
mtouch_process_commands(mouse_microtouch_t *dev)
{
    const char *c = dev->cmd;
    size_t      len;

    dev->cmd[strcspn(dev->cmd, "\r")] = '\0';
    len = strlen(c);
    pclog("MT Command: %s\n", dev->cmd);

    if (!strcmp(c, "AD")) {                               /* AutoBaud Disable */
        dev->autobaud = false;
    } else if (!strcmp(c, "AE")) {                        /* AutoBaud Enable */
        dev->autobaud = true;
    } else if (!strcmp(c, "CN") || !strcmp(c, "CX") || !strcmp(c, "CI")) { /* Calibrate New / Extended / Interactive */
        dev->cal_type = (c[1] == 'N') ? CAL_NEW : ((c[1] == 'X') ? CAL_EXTENDED : CAL_INTERACTIVE);
        dev->cal_cntr = 2;
    } else if (!strcmp(c, "CR")) {                        /* Calibrate Raw */
        mtouch_set_format(dev, FORMAT_CAL_RAW);
    } else if ((c[0] == 'F') && (c[1] == 'N') && isdigit((uint8_t) c[2])) { /* Filter Number */
        dev->filter = (uint8_t) atoi(&c[2]);
    } else if (!strcmp(c, "FO")) {                        /* Finger Only */
        dev->pen_mode = 1;
    } else if (!strcmp(c, "FB") || !strcmp(c, "FBS")) {   /* Format Binary [Stream] */
        mtouch_set_format(dev, FORMAT_BINARY);
        dev->mode        = (c[2] == 'S') ? MODE_STREAM : MODE_POLLED;
        dev->mode_status = true;
    } else if (!strcmp(c, "FD")) {                        /* Format Decimal */
        mtouch_set_format(dev, FORMAT_DEC);
    } else if (!strcmp(c, "FH")) {                        /* Format Hexadecimal */
        mtouch_set_format(dev, FORMAT_HEX);
    } else if (!strcmp(c, "FR")) {                        /* Format Raw (streams until Reset) */
        mtouch_set_format(dev, FORMAT_RAW);
        dev->cal_cntr = 0;
    } else if (!strcmp(c, "FT")) {                        /* Format Tablet */
        mtouch_set_format(dev, FORMAT_TABLET);
    } else if (!strcmp(c, "FZ")) {                        /* Format Zone */
        mtouch_set_format(dev, FORMAT_ZONE);
        dev->mode = MODE_STREAM;
    } else if ((c[0] == 0x03) && (c[1] == 'F')) {         /* Frequency Adjust (^C Fnn) */
        /* no analog front end to retune */
    } else if (!strcmp(c, "GP1")) {                       /* Get Parameter Block 1 (undocumented; dummy) */
        mtouch_ack(dev, 'A');
        fifo8_push_all(&dev->resp, (uint8_t *) "0000000000000000000000000\r", 26);
    } else if (!strcmp(c, "MDU")) {                       /* Mode Down/Up */
        dev->mode = MODE_DOWNUP;
    } else if (!strcmp(c, "MI")) {                        /* Mode Inactive */
        dev->mode = MODE_INACTIVE;
    } else if (!strcmp(c, "MP")) {                        /* Mode Point */
        dev->mode = MODE_POINT;
    } else if (!strcmp(c, "MQ")) {                        /* Mode Polled */
        dev->mode = MODE_POLLED;
        dev->xon = dev->pq_down = dev->pq_up = false;
    } else if (!strcmp(c, "MT")) {                        /* Mode Status */
        dev->mode_status = true;
    } else if (!strcmp(c, "MS")) {                        /* Mode Stream */
        dev->mode = MODE_STREAM;
    } else if (!strcmp(c, "OI")) {                        /* Output Identity */
        fifo8_push(&dev->resp, 0x01);
        fifo8_push_all(&dev->resp, (uint8_t *) mtouch_identity[dev->id], 6);
        fifo8_push(&dev->resp, 0x0D);
        return;
    } else if (!strcmp(c, "OS")) {                        /* Output Status */
        /* a: bit 6 always 1, bit 5 power-on flag (SMT2 only, cleared by OS);
           b: bit 6 always 1, bit 5 software reset flag. */
        fifo8_push(&dev->resp, 0x01);
        fifo8_push(&dev->resp, 0x40 | ((dev->power_on && (dev->id < 2)) ? 0x20 : 0x00));
        fifo8_push(&dev->resp, 0x40 | (dev->reset ? 0x20 : 0x00));
        fifo8_push(&dev->resp, 0x0D);
        dev->power_on = false;
        return;
    } else if (!strcmp(c, "PL")) {                        /* Parameter Lock */
        mtouch_store_settings(dev);
    } else if (!strcmp(c, "PO")) {                        /* Pen Only */
        dev->pen_mode = 2;
    } else if (!strcmp(c, "PF")) {                        /* Pen or Finger */
        dev->pen_mode = 3;
    } else if ((c[0] == 'P') && ((len == 4) || (len == 5)) && strchr("NOE", c[1]) &&
               ((c[2] == '7') || (c[2] == '8')) && ((c[3] == '1') || (c[3] == '2'))) { /* Parameter Set */
        dev->serial_cfg = ((c[1] == 'O') ? 1 : ((c[1] == 'E') ? 2 : 0)) | ((c[2] == '7') ? 4 : 0) | ((c[3] == '2') ? 8 : 0);
        mtouch_ack(dev, '0');
        if (len == 5)
            mtouch_set_baud(dev, mtouch_baud_from_code(c[4]));
        mtouch_store_settings(dev);
        return;
    } else if (!strcmp(c, "R") || !strcmp(c, "RD")) {     /* Reset / Restore Defaults */
        /* Stops sending data; cancels calibration, Format Raw and Calibrate Raw. */
        fifo8_reset(&dev->resp);
        dev->in_reset = true;
        dev->cal_cntr = 0;
        dev->xon = dev->pq_down = dev->pq_up = false;
        dev->but_old = 0;
        if ((dev->format == FORMAT_RAW) || (dev->format == FORMAT_CAL_RAW))
            dev->format = dev->prev_format;
        if (c[1] == 'D') {
            /* Factory defaults, including the factory calibration. */
            mtouch_defaults(dev);
            if (dev->baud_rate != 9600)
                mtouch_set_baud(dev, 9600);
            dev->scale_x = dev->scale_y = 1;
            dev->off_x = dev->off_y = 0;
            mtouch_writenvr(dev, 1, 1, 0, 0);
            mtouch_store_settings(dev);
        }
        timer_on_auto(&dev->reset_timer, 500. * 1000.);
        return;
    } else if ((c[0] == 'S') && (c[1] == 'E') && isdigit((uint8_t) c[2])) { /* Sensitivity Set */
        dev->sensitivity = (c[2] - '0') & 3;
        mtouch_store_settings(dev);
    } else if (!strcmp(c, "SP1")) {                       /* Set Parameter Block 1 (undocumented) */
        mtouch_ack(dev, 'A');
        return;
    } else if (!strcmp(c, "UT") || !strcmp(c, "UV")) {    /* Unit Type [Verify] */
        char ut[12];
        /* TouchPen status bit 5: no Unit Type since the last reset. */
        snprintf(ut, sizeof(ut), "%s****%02X", (dev->id == 2) ? "TP" : "QM",
                 ((dev->id == 2) && dev->ut_reset) ? 0x20 : 0x00);
        dev->ut_reset = false;
        fifo8_push(&dev->resp, 0x01);
        fifo8_push_all(&dev->resp, (uint8_t *) ut, 8);
        fifo8_push(&dev->resp, 0x0D);
        return;
    }
    /* Z (Null Command), NM (sent by 3M TouchWare, undocumented) and anything
       else get the positive response. */

    mtouch_ack(dev, '0');
}

static void
mtouch_write(UNUSED(serial_t *serial), void *priv, uint8_t data)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;

    if (data == '\x1') {
        dev->soh = true;
        dev->cmd_pos = 0;
    }
    else if (dev->soh) {
        if (data != '\r') {
            if (dev->cmd_pos < sizeof(dev->cmd) - 2) {
                dev->cmd[dev->cmd_pos++] = data;
            }
        } else {
            dev->soh = false;

            if (!dev->cmd_pos) {
                return;
            }

            dev->cmd[dev->cmd_pos++] = data;
            dev->cmd[dev->cmd_pos] = '\0';
            dev->cmd_pos = 0;
            mtouch_process_commands(dev);
        }
    }
    else if (data == 0x11) {
        /* XON (^Q): Mode Polled asks for one packet. */
        dev->xon = true;
    }
}

/* ---- touch reporting -------------------------------------------------------- */

static int
mtouch_prepare_transmit(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;

    int ev = EV_NONE;

    if (dev->format == FORMAT_RAW) {
        /* Format Raw streams corner data continuously, touched or not. */
        mtouch_report_raw(dev);
        return 0;
    }

    if (dev->but && !dev->but_old)
        ev = EV_DOWN;
    else if (dev->but)
        ev = EV_CONT;
    else if (dev->but_old)
        ev = EV_UP;
    dev->but_old = dev->but;

    if (ev == EV_DOWN)
        dev->touch_samples = 0;
    if ((ev == EV_DOWN) || (ev == EV_CONT)) {
        dev->touch_samples++;
        dev->last_x       = dev->abs_x;
        dev->last_y       = dev->abs_y;
        dev->last_raw_x   = dev->raw_x;
        dev->last_raw_y   = dev->raw_y;
        dev->last_in_zone = dev->in_zone;
    }

    if (dev->cal_cntr) {
        /* Calibrating: no coordinates, one acknowledgement per touch. */
        if (ev == EV_UP)
            mtouch_calibration_point(dev);
        return 0;
    }

    switch (dev->mode) {
        case MODE_STREAM:
            if (ev != EV_NONE)
                mtouch_report(dev, ev, dev->last_x, dev->last_y, dev->last_raw_x, dev->last_raw_y, dev->last_in_zone);
            break;
        case MODE_POINT:
            if (ev == EV_DOWN)
                mtouch_report(dev, ev, dev->last_x, dev->last_y, dev->last_raw_x, dev->last_raw_y, dev->last_in_zone);
            break;
        case MODE_DOWNUP:
            if ((ev == EV_DOWN) || (ev == EV_UP))
                mtouch_report(dev, ev, dev->last_x, dev->last_y, dev->last_raw_x, dev->last_raw_y, dev->last_in_zone);
            break;
        case MODE_POLLED:
            /* One touchdown and one liftoff are remembered (the most recent);
               each ^Q sends one packet: the touchdown, then a continued touch
               per ^Q while touching, then the liftoff. */
            if (ev == EV_DOWN) {
                dev->pq_down      = true;
                dev->pq_up        = false;
                dev->pq_down_x    = dev->last_x;
                dev->pq_down_y    = dev->last_y;
                dev->pq_down_zone = dev->last_in_zone;
            } else if (ev == EV_UP) {
                dev->pq_up      = true;
                dev->pq_up_x    = dev->last_x;
                dev->pq_up_y    = dev->last_y;
                dev->pq_up_zone = dev->last_in_zone;
            }
            if (dev->xon) {
                if (dev->pq_down) {
                    mtouch_report(dev, EV_DOWN, dev->pq_down_x, dev->pq_down_y, dev->last_raw_x, dev->last_raw_y, dev->pq_down_zone);
                    dev->pq_down = false;
                    dev->xon     = false;
                } else if (dev->but && (ev != EV_DOWN)) {
                    mtouch_report(dev, EV_CONT, dev->last_x, dev->last_y, dev->last_raw_x, dev->last_raw_y, dev->last_in_zone);
                    dev->xon = false;
                } else if (dev->pq_up) {
                    mtouch_report(dev, EV_UP, dev->pq_up_x, dev->pq_up_y, dev->last_raw_x, dev->last_raw_y, dev->pq_up_zone);
                    dev->pq_up = false;
                    dev->xon   = false;
                }
            }
            break;
        default: /* MODE_INACTIVE */
            break;
    }
    if (dev->tt && dev->tt->active && dev->tt->seen && !dev->tt->lifted) {
        if ((ev == EV_DOWN) || (ev == EV_CONT))
            dev->tt->reports++;
        else if (ev == EV_UP) {
            int      seq;
            uint32_t press;

            dev->tt->reports++;
            dev->tt->lifted   = 1;
            dev->tt->g_lift   = tt_guest_ms();
            dev->tt->h_lift   = plat_get_ticks();
            tablet_get_trace(&seq, &press, &dev->tt->h_release);
            dev->tt->check    = 1;
            dev->tt->check_at = plat_get_ticks() + TT_WAIT_MS;
        }
    }
    return 0;
}

static void
mtouch_write_to_host(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;

    if (dev->serial == NULL)
        goto no_write_to_machine;
    if ((dev->serial->type >= SERIAL_16550) && dev->serial->fifo_enabled) {
        if (fifo_get_full(dev->serial->rcvr_fifo)) {
            goto no_write_to_machine;
        }
    } else {
        if (dev->serial->lsr & 1) {
            goto no_write_to_machine;
        }
    }
    if (dev->in_reset) {
        goto no_write_to_machine;
    }
    /* MegaPPBox: serial_write_fifo() only parks the byte in the UART's receive
       shift register; the UART's own receive timer moves it into RBR or the
       FIFO.  Writing again before that happened overwrote the previous byte.
       A lost report byte is harmless (the next report resyncs), but a lost
       liftoff status byte (0x88) made 3M's TouchWare driver drop the liftoff:
       the finger stayed down, and the next touch became a drag from the old
       spot, so the Linux MAXX releases missed taps.  Wait until the receive
       shift register is free, as the bytes of a real serial line would. */
    if (dev->serial->out_new != 0xffff) {
        if (dev->tt)
            dev->tt->rsr_busy++;
        goto no_write_to_machine;
    }
    if (dev->tt && dev->tt->lifted && !dev->tt->read && !fifo8_num_used(&dev->resp) &&
        !(dev->serial->lsr & 1) && (!dev->serial->fifo_enabled || fifo_get_empty(dev->serial->rcvr_fifo))) {
        dev->tt->read   = 1;
        dev->tt->g_read = tt_guest_ms();
    }
    if (fifo8_num_used(&dev->resp)) {
        serial_write_fifo(dev->serial, fifo8_pop(&dev->resp));
    }
    else {
        mtouch_prepare_transmit(dev);
    }

no_write_to_machine:
    timer_on_auto(&dev->host_to_serial_timer, (1000000.0 / (double) dev->baud_rate) * (double) (1 + 8 + 1));
}

static int
mtouch_poll(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;

    /* MegaPPBox: a press seen since the last poll counts even if the button is
       already up again, so a very quick tap still gives a touchdown and a
       liftoff instead of nothing. */
    if (dev->tt)
        ta_poll(dev);
    dev->but = tablet_get_buttons_ex() | tablet_take_pressed();
    mouse_get_abs_coords(&dev->raw_x, &dev->raw_y);

    if (enable_overscan && mouse_tablet_in_proximity > 0) {
        int index = mouse_tablet_in_proximity - 1;

        dev->raw_x *= monitors[index].mon_unscaled_size_x - 1;
        dev->raw_y *= monitors[index].mon_efscrnsz_y - 1;

        if (dev->raw_x <= (monitors[index].mon_overscan_x / 2.)) {
            dev->raw_x = (monitors[index].mon_overscan_x / 2.);
        }
        if (dev->raw_y <= (monitors[index].mon_overscan_y / 2.)) {
            dev->raw_y = (monitors[index].mon_overscan_y / 2.);
        }
        dev->raw_x -= (monitors[index].mon_overscan_x / 2.);
        dev->raw_y -= (monitors[index].mon_overscan_y / 2.);
        dev->raw_x = dev->raw_x / (double) monitors[index].mon_xsize;
        dev->raw_y = dev->raw_y / (double) monitors[index].mon_ysize;
    }

    if (dev->raw_x >= 1.0) dev->raw_x = 1.0;
    if (dev->raw_y >= 1.0) dev->raw_y = 1.0;
    if (dev->raw_x <= 0.0) dev->raw_x = 0.0;
    if (dev->raw_y <= 0.0) dev->raw_y = 0.0;

    dev->abs_x = dev->scale_x * dev->raw_x + dev->off_x;
    dev->abs_y = dev->scale_y * dev->raw_y + dev->off_y;

    /* Format Zone: inside or outside the calibrated area. */
    dev->in_zone = (dev->abs_x >= 0.0) && (dev->abs_x <= 1.0) && (dev->abs_y >= 0.0) && (dev->abs_y <= 1.0);

    if (dev->abs_x >= 1.0) dev->abs_x = 1.0;
    if (dev->abs_y >= 1.0) dev->abs_y = 1.0;
    if (dev->abs_x <= 0.0) dev->abs_x = 0.0;
    if (dev->abs_y <= 0.0) dev->abs_y = 0.0;

    if (dev->tt)
        tt_poll(dev);
    return 0;
}

static int
mtouch_poll_global(void* arg)
{
    (void)arg;
    return mtouch_poll(mtouch_inst);
}

void *
mtouch_init(UNUSED(const device_t *info))
{
    mouse_microtouch_t *dev = calloc(1, sizeof(mouse_microtouch_t));

    dev->serial = serial_attach(device_get_config_int("port"), NULL, mtouch_write, dev);
    if (dev->serial) {
        serial_set_cts(dev->serial, 1);
        serial_set_dsr(dev->serial, 1);
        serial_set_dcd(dev->serial, 1);
    }

    dev->id = device_get_config_int("identity");
    snprintf(dev->nvr_path, sizeof(dev->nvr_path), "mtouch_%s.nvr", mtouch_identity[dev->id]);
    mtouch_initnvr(dev);
    mtouch_readnvr(dev);
    mtouch_load_settings(dev);
    dev->power_on = true;
    dev->ut_reset = true;

    fifo8_create(&dev->resp, 256);
    timer_add(&dev->host_to_serial_timer, mtouch_write_to_host, dev, 0);
    timer_add(&dev->reset_timer, mtouch_reset_complete, dev, 0);
    if (!dev->baud_rate)
        dev->baud_rate = 9600;
    timer_on_auto(&dev->host_to_serial_timer, (1000000. / dev->baud_rate) * 10);

    if (getenv("MEGAPPBOX_TOUCH_TRACE") && *getenv("MEGAPPBOX_TOUCH_TRACE")) {
        char path[1024];

        dev->tt = calloc(1, sizeof(mtouch_trace_t));
        path_append_filename(path, usr_path, "touchtrace.log");
        dev->tt->fp = fopen(path, "a");
        if (dev->tt->fp) {
            time_t now = time(NULL);
            fprintf(dev->tt->fp, "=== MicroTouch trace, %s", ctime(&now));
            tablet_get_trace(&dev->tt->seq, &dev->tt->h_press, &dev->tt->h_release);
            dev->tt->marks     = tablet_get_marks();
            tablet_trace_marks = 1;
            timer_add(&smp.timer, smp_tick, NULL, 0);
            timer_add(&irq_rt.timer, irq_rate_done, NULL, 0);
        } else {
            free(dev->tt);
            dev->tt = NULL;
        }
    }

    mouse_set_buttons(2);
    mouse_set_poll_ex(mtouch_poll_global, dev);
    mtouch_inst = dev;

    return dev;
}

void
mtouch_close(void *priv)
{
    mouse_microtouch_t *dev = (mouse_microtouch_t *) priv;
    
    fifo8_destroy(&dev->resp);
    if (dev->tt) {
        fclose(dev->tt->fp);
        free(dev->tt);
    }
    /* Detach serial port from the mouse. */
    if (dev && dev->serial && dev->serial->sd) {
        memset(dev->serial->sd, 0, sizeof(serial_device_t));
    }
    
    if (dev->nvr != NULL)
        free(dev->nvr);
    
    free(dev);
    mtouch_inst = NULL;
}

static const device_config_t mtouch_config[] = {
  // clang-format off
    {
        .name           = "port",
        .description    = "Serial Port",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "COM1", .value = 0 },
            { .description = "COM2", .value = 1 },
            { .description = "COM3", .value = 2 },
            { .description = "COM4", .value = 3 },
            { .description = ""                 }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "identity",
        .description    = "Controller",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "A3 - SMT2 Serial / SMT3(R)V", .value = 0 },
            { .description = "A4 - SMT2 PCBus",             .value = 1 },
            { .description = "P5 - TouchPen 4(+)",          .value = 2 },
            { .description = "Q1 - SMT3(R) Serial",         .value = 3 },
            { .description = ""                                        }
        },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
  // clang-format on
};

const device_t mouse_mtouch_device = {
    .name          = "3M MicroTouch (Serial)",
    .internal_name = "microtouch_touchpen",
    .flags         = DEVICE_COM,
    .local         = 0,
    .init          = mtouch_init,
    .close         = mtouch_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = mtouch_config
};