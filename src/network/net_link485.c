/*
 * MegaPPBox   86Box stripped down to run untouched Merit Megatouch images.
 *
 *             Mega-Link over RS-485: the cabinet link of the XL releases
 *             and MAXX 1st, carried between emulated cabinets by the
 *             Mega-Link switch.
 *
 * ---------------------------------------------------------------------------
 *
 * Before the TRENDnet Ethernet cards (MAXX 2K on), Merit linked cabinets on
 * an RS-485 bus, through the PC's second serial port.  From MAXX 1st's and
 * XL Platinum's MEGACDLL.EXE (exports CONFIGURE_RS485, SENDPACKET,
 * GETPACKETS2, RS_INRCVD):
 *
 *   - the port is COM2 (0x2F8, IRQ 3), or COM1 (0x3F8, IRQ 4) when the
 *     environment has COMPORT1 -- the touch screen is on COM1, so COM2;
 *   - a 16550 at divisor 1 (115200 baud), 8N1, FIFO on, receive interrupts
 *     only; OUT2 set;
 *   - RTS drives the bus: MCR bit 1 cleared while sending (the bytes are
 *     polled out, then TEMT is waited for), set again to listen;
 *   - a packet is 12 bytes: byte 2 is 0x5A, byte 11 the XOR of bytes 0-10,
 *     the first word carries the cabinet IDs (_MYID);
 *   - before sending, a cabinet waits for its time slot (system timer mod
 *     20) and for a tick with no bytes on the bus, and moves to another slot
 *     when it hears someone else in its own: a shared, half-duplex bus.
 *
 * So the emulated bus is: every byte a cabinet sends reaches every other
 * cabinet, not the sender itself (its receiver is off while it drives).
 * This is a network card in MegaPPBox's sense -- one per cabinet, set up in
 * the Network dialog, on the Local or Remote Switch like the Ethernet
 * Mega-Link -- that plugs into COM2 instead of a slot.  Bytes the game
 * sends are gathered while they keep coming and sent as one broadcast frame
 * once the line has been quiet for a few byte times; bytes from the other
 * cabinets go into the UART one byte time apart, and only when it has taken
 * the last one.
 *
 * Frame: broadcast, EtherType 0x88B5 (local experimental), then "M485", a
 * byte count (16 bits, big-endian) and the bytes.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/thread.h>
#include <86box/random.h>
#include <86box/serial.h>
#include <86box/network.h>
#include <86box/plat_unused.h>

#define LINK_PORT       1      /* COM2 */
#define LINK_ETHERTYPE  0x88b5
#define LINK_MAGIC      "M485"
#define LINK_HDR        (14 + 4 + 2)
#define LINK_TX_MAX     1024   /* bytes per frame */
#define LINK_RX_SIZE    8192   /* receive queue */
#define LINK_QUIET      4      /* byte times of silence that end a burst */

#ifdef ENABLE_LINK485_LOG
int link485_do_log = ENABLE_LINK485_LOG;
#else
int link485_do_log = -1;
#endif

static void
link485_log(const char *fmt, ...)
{
    va_list ap;

    if (link485_do_log < 0) {
        const char *e   = getenv("MEGAPPBOX_LINK485_LOG");
        link485_do_log = (e != NULL) && (e[0] != '\0');
    }
    if (link485_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}

typedef struct link485_t {
    serial_t  *serial;
    netcard_t *card;
    uint8_t    mac[6];

    uint8_t    tx[LINK_HDR + LINK_TX_MAX];
    int        tx_len;     /* bytes of the burst so far */
    pc_timer_t tx_timer;   /* the line has gone quiet */

    uint8_t    rx[LINK_RX_SIZE];
    int        rx_head, rx_tail;
    pc_timer_t rx_timer;   /* one byte into the UART */

    double     byte_us;    /* one 10-bit character at the UART's speed */
} link485_t;

static void
link485_flush(link485_t *dev)
{
    uint8_t *f = dev->tx;
    int      len;

    if (!dev->tx_len)
        return;
    memset(f, 0xff, 6);
    memcpy(f + 6, dev->mac, 6);
    f[12] = LINK_ETHERTYPE >> 8;
    f[13] = LINK_ETHERTYPE & 0xff;
    memcpy(f + 14, LINK_MAGIC, 4);
    f[18] = dev->tx_len >> 8;
    f[19] = dev->tx_len & 0xff;
    len = LINK_HDR + dev->tx_len;
    if (len < 60) {
        memset(f + len, 0, 60 - len);
        len = 60;
    }
    link485_log("Link485: send %d bytes\n", dev->tx_len);
    if (dev->card)
        network_tx(dev->card, f, len);
    dev->tx_len = 0;
}

static void
link485_tx_timer(void *priv)
{
    link485_flush((link485_t *) priv);
}

/* A byte the game has sent onto the bus. */
static void
link485_dev_write(UNUSED(serial_t *serial), void *priv, uint8_t data)
{
    link485_t *dev = (link485_t *) priv;

    dev->tx[LINK_HDR + dev->tx_len++] = data;
    if (dev->tx_len >= LINK_TX_MAX)
        link485_flush(dev);
    timer_on_auto(&dev->tx_timer, dev->byte_us * LINK_QUIET);
}

static void
link485_speed(UNUSED(serial_t *serial), void *priv, double transmit_period)
{
    link485_t *dev = (link485_t *) priv;

    /* transmit_period is one bit time in microseconds. */
    if (transmit_period > 0.0)
        dev->byte_us = transmit_period * 10.0;
}

static void
link485_rx_timer(void *priv)
{
    link485_t *dev = (link485_t *) priv;

    if (dev->rx_head == dev->rx_tail)
        return;
    /* Only once the UART has moved the last byte on (see the MicroTouch's
       note on serial_write_fifo). */
    if (dev->serial && (dev->serial->out_new == 0xffff)) {
        serial_write_fifo(dev->serial, dev->rx[dev->rx_tail]);
        dev->rx_tail = (dev->rx_tail + 1) % LINK_RX_SIZE;
    }
    if (dev->rx_head != dev->rx_tail)
        timer_on_auto(&dev->rx_timer, dev->byte_us);
}

/* A frame from the switch: another cabinet's burst. */
static int
link485_rx(void *priv, uint8_t *buf, int len)
{
    link485_t *dev = (link485_t *) priv;
    int        n;
    int        was_idle;

    if ((len < LINK_HDR) || (((buf[12] << 8) | buf[13]) != LINK_ETHERTYPE) ||
        memcmp(buf + 14, LINK_MAGIC, 4) || !memcmp(buf + 6, dev->mac, 6))
        return 1;
    n = (buf[18] << 8) | buf[19];
    if (n > len - LINK_HDR)
        n = len - LINK_HDR;
    link485_log("Link485: received %d bytes\n", n);

    was_idle = (dev->rx_head == dev->rx_tail);
    for (int i = 0; i < n; i++) {
        int next = (dev->rx_head + 1) % LINK_RX_SIZE;
        if (next == dev->rx_tail)
            break; /* overrun: the rest is lost, as on a real bus */
        dev->rx[dev->rx_head] = buf[LINK_HDR + i];
        dev->rx_head          = next;
    }
    if (was_idle)
        timer_on_auto(&dev->rx_timer, dev->byte_us);
    return 1;
}

static void *
link485_init(UNUSED(const device_t *info))
{
    link485_t *dev = (link485_t *) calloc(1, sizeof(link485_t));
    int        mac;

    dev->byte_us = 1000000.0 / 115200.0 * 10.0;

    /* The cabinet's address on the switch: Merit's OUI is unknown, so a
       locally administered one, the low three bytes kept per image. */
    mac = device_get_config_mac("mac", -1);
    if (mac & 0xff000000) {
        mac = (random_generate() << 16) | (random_generate() << 8) | random_generate();
        device_set_config_mac("mac", mac);
    }
    dev->mac[0] = 0x02;
    dev->mac[1] = 0x4d; /* 'M' */
    dev->mac[2] = 0x4c; /* 'L' */
    dev->mac[3] = (mac >> 16) & 0xff;
    dev->mac[4] = (mac >> 8) & 0xff;
    dev->mac[5] = mac & 0xff;

    timer_add(&dev->tx_timer, link485_tx_timer, dev, 0);
    timer_add(&dev->rx_timer, link485_rx_timer, dev, 0);

    dev->serial = serial_attach_ex(LINK_PORT, NULL, link485_dev_write, link485_speed, NULL, dev);
    if (!dev->serial)
        pclog("Link485: COM2 is taken (the modem?): Mega-Link over RS-485 is off\n");

    dev->card = network_attach(dev, dev->mac, link485_rx, NULL);
    link485_log("Link485: on COM2, %02X:%02X:%02X:%02X:%02X:%02X\n", dev->mac[0], dev->mac[1],
                dev->mac[2], dev->mac[3], dev->mac[4], dev->mac[5]);
    return dev;
}

static void
link485_close(void *priv)
{
    link485_t *dev = (link485_t *) priv;

    timer_disable(&dev->tx_timer);
    timer_disable(&dev->rx_timer);
    if (dev->card)
        netcard_close(dev->card);
    free(dev);
}

// clang-format off
static const device_config_t link485_config[] = {
    {
        .name           = "mac",
        .description    = "MAC Address",
        .type           = CONFIG_MAC,
        .default_string = NULL,
        .default_int    = -1,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};
// clang-format on

const device_t link485_device = {
    .name          = "Mega-Link RS-485 (COM2)",
    .internal_name = "link485",
    .flags         = 0,
    .local         = 0,
    .init          = link485_init,
    .close         = link485_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = link485_config
};
