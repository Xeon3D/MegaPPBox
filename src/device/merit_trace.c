/*
 * TouchPPBox diagnostic trace (Phase 2 trial only).
 *
 * When the environment variable TOUCHPPBOX_TRACE is set, logs every guest
 * access to the Merit I/O card window 0x220-0x22F to the 86Box log, with the
 * guest EIP, then falls back to open-bus behaviour (reads return 0xFF), i.e.
 * the machine behaves exactly like stock 86Box with no card fitted.
 * Repeated identical accesses are collapsed into a count so tight polling
 * loops do not flood the log.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/plat_unused.h>
#include <86box/timer.h>
#include <86box/mem.h>
#include "cpu.h"

int merit_trace_enabled = 0;

static struct {
    char     dir;
    uint16_t port;
    uint8_t  val;
    uint32_t pc;
    uint32_t repeat;
} last;

static uint64_t total;

/* Dump guest code around the current CS:EIP (once per call site of interest). */
static void
dump_code(const char *why, int before, int len)
{
    uint32_t lin = (uint32_t) (cs + cpu_state.pc) - before;
    char     line[3 * 32 + 1];
    pclog("[TR] code dump (%s) cs=%04X base=%08X eip=%08X, from lin %08X:\n", why, CS, (uint32_t) cs, cpu_state.pc, lin);
    for (int row = 0; row < len; row += 32) {
        for (int i = 0; i < 32; i++) {
            uint32_t a = lin + row + i;
            uint64_t p = (cr0 & 0x80000000) ? mmutranslate_noabrt(a, 0) : a;
            if (p == 0xffffffffffffffffULL)
                snprintf(line + 3 * i, 4, "?? ");
            else
                snprintf(line + 3 * i, 4, "%02X ", mem_readb_phys((uint32_t) p));
        }
        pclog("[TR]   %08X: %s\n", lin + row, line);
    }
}

static int dumped_224;

static void
trace_flush(void)
{
    if (last.repeat > 1)
        pclog("[TR]   ... repeated %u times\n", last.repeat);
}

static void
trace_access(char dir, uint16_t port, uint8_t val)
{
    total++;
    if ((last.dir == dir) && (last.port == port) && (last.val == val) && (last.pc == cpu_state.pc)) {
        last.repeat++;
        if ((last.repeat & 0xfffff) == 0)
            pclog("[TR]   ... still repeating (%u)\n", last.repeat);
        return;
    }
    trace_flush();
    last.dir    = dir;
    last.port   = port;
    last.val    = val;
    last.pc     = cpu_state.pc;
    last.repeat = 1;
    pclog("[TR] %c %03X %02X  cs:eip=%04X:%08X  #%llu\n", dir, port, val, CS, cpu_state.pc,
          (unsigned long long) total);
}

/* Idle value of the coin/service port 0x22C on a real card (MAME mtouchxl):
   bits 0-1 Setup/Calibrate active low, bits 2-5 coins active high, 6-7 high.
   Open bus (0xFF) would read as all four coin lines asserted. */
static uint8_t idle_22c = 0xff;

static void
maybe_dump(uint16_t port)
{
    if (((port == 0x224) || (port == 0x225)) && !dumped_224) {
        dumped_224 = 1;
        dump_code("first 0x224/0x225 access", 0x200, 0x400);
    }
}

static uint8_t
trace_read(uint16_t port, UNUSED(void *priv))
{
    maybe_dump(port);
    uint8_t val = (port == 0x22c) ? idle_22c : 0xff;
    trace_access('R', port, val);
    return val;
}

static void
trace_write(uint16_t port, uint8_t val, UNUSED(void *priv))
{
    maybe_dump(port);
    trace_access('W', port, val);
}

/* Whole-bus byte I/O trace, armed by the first guest read of 0x22C (the first
   program that looks at the I/O card) and capped so polling loops cannot fill
   the disk. The card window itself is logged by trace_access() above. */
static int      armed;
static int      port_lo = 0, port_hi = 0xffff; /* TOUCHPPBOX_TRACE_PORTS=lo-hi (hex) */
static uint32_t io_lines;
#define IO_LINE_CAP 40000

void
merit_trace_io(char dir, uint16_t port, uint8_t val)
{
    if ((port >= 0x220) && (port <= 0x22f)) {
        if ((port == 0x22c) && (dir == 'R') && !armed) {
            armed = 1;
            pclog("[TR] whole-bus trace armed\n");
        }
        return;
    }
    if ((port < port_lo) || (port > port_hi))
        return;
    if (!armed || (io_lines >= IO_LINE_CAP))
        return;
    if ((port >= 0x3b0) && (port <= 0x3df)) /* VGA: retrace polling floods the log */
        return;
    if ((port == 0x61) || (port == 0x80) || (port == 0xeb) || /* BIOS delay loops */
        ((port >= 0x1f0) && (port <= 0x1f7)) || (port == 0x3f6)) /* disk (IDE commands logged separately) */
        return;
    trace_access(dir, port, val);
    if (++io_lines == IO_LINE_CAP)
        pclog("[TR] whole-bus trace cap reached\n");
}

/* Periodic CPU position sampler: where is the guest spinning? */
static pc_timer_t sample_timer;

static void
sample_cb(UNUSED(void *priv))
{
    uint32_t lin = (uint32_t) (cs + cpu_state.pc);
    char     hex[3 * 16 + 1];
    for (int i = 0; i < 16; i++) {
        uint32_t a = lin + i;
        uint64_t p = (cr0 & 0x80000000) ? mmutranslate_noabrt(a, 0) : a;
        if (p == 0xffffffffffffffffULL)
            snprintf(hex + 3 * i, 4, "?? ");
        else
            snprintf(hex + 3 * i, 4, "%02X ", mem_readb_phys((uint32_t) p));
    }
    pclog("[TR] sample cs=%04X base=%08X eip=%08X lin=%08X ss:esp=%04X:%08X eax=%08X ecx=%08X edx=%08X | %s\n",
          CS, (uint32_t) cs, cpu_state.pc, lin, SS, ESP, EAX, ECX, EDX, hex);
    {
        /* TOUCHPPBOX_DUMP_C000=<file>: save C0000-CFFFF (option ROMs as
           shadowed after POST) once, 30 s into the run. */
        static int   ticks;
        const char *fn = getenv("TOUCHPPBOX_DUMP_C000");
        if (fn && (++ticks == 120)) {
            FILE *f = fopen(fn, "wb");
            if (f) {
                for (uint32_t a = 0xc0000; a < 0xd0000; a++)
                    fputc(mem_readb_phys(a), f);
                fclose(f);
                pclog("[TR] C000 dumped to %s\n", fn);
            }
        }
    }
    timer_on_auto(&sample_timer, 250000.0);
}

/* Accesses made to the emulated I/O card are reported here by merit_io.c. */
void
merit_trace_note(char dir, uint16_t port, uint8_t val)
{
    maybe_dump(port);
    trace_access(dir, port, val);
}

void
merit_trace_init(void)
{
    merit_trace_enabled = (getenv("TOUCHPPBOX_TRACE") != NULL);
    if (!merit_trace_enabled)
        return;
    last.repeat = 0;
    armed       = (getenv("TOUCHPPBOX_ARM_EARLY") != NULL); /* whole-bus trace from power-on */
    port_lo = 0;
    port_hi = 0xffff;
    if (getenv("TOUCHPPBOX_TRACE_PORTS"))
        sscanf(getenv("TOUCHPPBOX_TRACE_PORTS"), "%x-%x", &port_lo, &port_hi);
    dumped_224  = 0;
    io_lines    = 0;
    idle_22c    = getenv("TOUCHPPBOX_IDLE22C") ? (uint8_t) strtoul(getenv("TOUCHPPBOX_IDLE22C"), NULL, 16) : 0xff;
    pclog("[TR] trace enabled: ports 0x220-0x22F, IDE commands; 0x22C idle=%02X\n", idle_22c);
    io_sethandler(0x0220, 0x0008, trace_read, NULL, NULL, trace_write, NULL, NULL, NULL);
    if (getenv("TOUCHPPBOX_SAMPLE")) {
        timer_add(&sample_timer, sample_cb, NULL, 0);
        timer_on_auto(&sample_timer, 250000.0);
    }
}
