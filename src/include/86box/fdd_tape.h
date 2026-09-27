/*
 * MegaPPBox - Merit Megatouch cabinets on 86Box.
 *
 *          Inert floppy-tape (QIC-40/80) hooks.
 *
 *          Upstream lets a QIC-117 tape drive sit on the floppy controller,
 *          and the FDC and FDD code asks "is this drive a tape?" from about
 *          twenty places woven through seek, step, track-0 and media-change
 *          handling.  The tape drives are gone from MegaPPBox (no Megatouch
 *          cabinet had one), so the answer is always no, and the other hooks
 *          have nothing to do.  Keeping the questions and answering them here
 *          leaves the controller code exactly as upstream wrote it.
 *
 * Authors: MegaPPBox contributors
 *
 *          Released under the GNU General Public License version 2 or
 *          later.  See COPYING for more information.
 */
#ifndef EMU_FDD_TAPE_H
#define EMU_FDD_TAPE_H

static inline void fdd_tape_init(void) { }
static inline void fdd_tape_close(void) { }
static inline void fdd_tape_eject(void) { }
static inline void fdd_tape_set_fdc(void *fdc) { (void) fdc; }

static inline int fdd_tape_present(int drive) { (void) drive; return 0; }

/* Unreachable: every caller tests fdd_tape_present() first. */
static inline int fdd_tape_step(int drive, int steps) { (void) drive; (void) steps; return 0; }
static inline int fdd_tape_track0(int drive) { (void) drive; return 0; }
static inline int fdd_tape_get_flags(int drive) { (void) drive; return 0; }

#endif /*EMU_FDD_TAPE_H*/
