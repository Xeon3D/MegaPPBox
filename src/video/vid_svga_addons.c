/*
 * MegaPPBox - Merit Megatouch cabinets on 86Box.
 *
 *          Inert 8514/A and XGA hooks for the SVGA core.
 *
 *          86Box's SVGA core can host an 8514/A or an XGA alongside the VGA,
 *          and reaches them from dozens of call sites threaded through its
 *          memory, timing and rendering paths.  Every one of those sites is
 *          gated on ibm8514_active or xga_active, and the two entry points
 *          called unguarded -- xga_read_test() and xga_write_test() on the
 *          Cirrus and S3 memory paths -- test xga_active as their first act.
 *
 *          The Megatouch cards (CL-GD5430, Mach64 VT, Trio64V2/DX) are none of
 *          those things and cannot host either, so both flags stay zero and
 *          none of this can run.  The card implementations (vid_8514a.c,
 *          vid_xga.c, the Mach8 behind them) are gone; what remains is the
 *          flags and no-op bodies for the entry points the SVGA core links
 *          against.  Deleting the call sites instead would mean edits all
 *          through the hottest code in the emulator, for branches that are
 *          never taken.  (After PeepeeBox.)
 *
 * Authors: MegaPPBox contributors
 *
 *          Released under the GNU General Public License version 2 or
 *          later.  See COPYING for more information.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/plat_unused.h>

/* Never set: nothing in this build can add an 8514/A or an XGA. */
int ibm8514_active = 0;
int xga_active     = 0;

void
ibm8514_set_poll(UNUSED(svga_t *svga))
{
    /* unreachable: every call site tests ibm8514_active first */
}

void
ibm8514_recalctimings(UNUSED(svga_t *svga))
{
    /* unreachable: every call site tests ibm8514_active first */
}

void
xga_set_poll(UNUSED(svga_t *svga))
{
    /* unreachable: every call site tests xga_active first */
}

void
xga_recalctimings(UNUSED(svga_t *svga))
{
    /* unreachable: every call site tests xga_active first */
}

void
xga_write_test(UNUSED(uint32_t addr), UNUSED(uint8_t val), UNUSED(void *priv))
{
    /* upstream returns at once unless xga_active */
}

uint8_t
xga_read_test(UNUSED(uint32_t addr), UNUSED(void *priv))
{
    /* upstream returns 0x00 at once unless xga_active */
    return 0x00;
}
