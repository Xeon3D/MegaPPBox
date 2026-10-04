/*
 * MegaPPBox   86Box stripped down to run untouched Merit Megatouch images.
 *
 *             The PC Card slots on the MAXX I/O board (merit_pcic.c) and
 *             what a 16-bit PC Card plugged into one of them provides.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#ifndef EMU_MERIT_PCIC_H
#define EMU_MERIT_PCIC_H

/* A 16-bit PC Card as the socket sees it.  Addresses are card addresses:
   attribute and common memory from 0, I/O as the host port (a card decodes
   only its own low address lines).  reset is the RESET pin, and power-off
   resets a card too. */
typedef struct pccard_t {
    uint8_t (*attr_read)(uint32_t addr, void *priv);
    void (*attr_write)(uint32_t addr, uint8_t val, void *priv);
    uint8_t (*io_read)(uint16_t port, void *priv);
    uint16_t (*io_readw)(uint16_t port, void *priv);
    void (*io_write)(uint16_t port, uint8_t val, void *priv);
    void (*io_writew)(uint16_t port, uint16_t val, void *priv);
    void (*reset)(void *priv);
    void *priv;
} pccard_t;

/* Plug a card into a socket (0 = A, 1 = B), or pull it out (NULL).  The
   network cards are added before the I/O board, so a card may arrive before
   the controller does; it is picked up when the controller is created. */
extern void merit_pcic_insert(int socket, const pccard_t *card);

/* The card's IREQ line: 1 = interrupt requested.  The controller steers it
   to the ISA IRQ the driver chose, once the card is in I/O mode. */
extern void merit_pcic_card_irq(int socket, int level);

#endif /*EMU_MERIT_PCIC_H*/
