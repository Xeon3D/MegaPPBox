<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/logo/megappbox-logo-dark.svg">
    <img src="docs/logo/megappbox-logo.svg" alt="MegaPPBox" width="560">
  </picture>
</p>

**MegaPPBox is a fork of [86Box](https://github.com/86Box/86Box) stripped down to
the Merit Megatouch XL and MAXX countertop cabinets, so that untouched disk and CD
images run with zero bytes changed.**

The games check their hardware: a Dallas security key on the Merit I/O board
(a DS1205 MultiKey or a DS1991 iButton), the model string of the hard disk, a
CPUID-capable CPU, and on the Linux releases the motherboard's PCI devices. MegaPPBox answers
those checks in emulated hardware — no patched executables, no bypass.

Everything else about each machine is fixed by its hardware profile, and a
Machine Manager picks the profile and key from what the image says it is.

It runs on Windows, Linux and macOS, and on Android phones and tablets (arm64,
see [android/README.md](android/README.md)); every release carries all four.
No security keys are included: import dumps of your own (see
[Security keys](#security-keys)).

Credit
------

**All credit for the emulator itself belongs to the 86Box project and its
authors** (see [AUTHORS](AUTHORS)). MegaPPBox is a little Megatouch-specific
hardware and a lot of deletion on top of their work. The I/O map and key wiring
of the XL generation were cross-checked against MAME's `mtouchxl` driver, whose
DS1205 emulation (smf, Carl) the early XL key follows, and from whose sets the
early U12 ROM and the Gold battery RAM image come. Key dumps are in MAME's
MultiKey and keyflasher's DS1991 formats; telling which release a DS1991 dump is
for follows keyflasher's DS91Decrypter. Modelled on PeepeeBox, a sibling fork for the
funworld Photo Play cabinets, from which the toolbar coin and calibrate icons also
come.

Released under the GNU General Public License version 2 or later, the same as
86Box. See [COPYING](COPYING).

Running it
----------

A MegaPPBox folder is one cabinet:

```
MegaPPBox.exe
roms\        the ROM set (board and video BIOSes, the I/O board ROM, settled CMOS)
```

Every release also needs a dump of its security key, which MegaPPBox does not
include: see [Security keys](#security-keys).

Run `MegaPPBox.exe`. The first time, the **Machine Manager** opens: point it at a
folder of images (it searches subfolders too), pick one and press **Run**. It
remembers the pick, so the next start boots straight into it; open the manager
again from the toolbar (or *Tools → Machine Manager…*) to switch, which rebuilds
the machine on a hard reset. The manager reads the folder afresh each time it
opens.

Two options are kept per image: **Modem on COM2** (an ActionTec 56K, as the
MAXX TournaMAXX/MegaNET kits fitted; on by default from MAXX Diamond on, which
have an on-line client; it can dial a TCP host, such as a
[TournaMAXX-Revival](https://github.com/Xeon3D/TournaMAXX-Revival) server,
which brings the DOS releases' tournaments back; it can be plugged in or out,
and its line changed, with the cabinet running) and, off by default, on MAXX,
**Network card**: on MAXX (Old) an ISA TRENDnet TE-16XP at 0x340, IRQ 11 — the
card every DOS release from MAXX 2K on loads its ODI driver for (the later
releases also knew the newer TE-16PT; 2K did not, so the one card that works
for all is fitted). Double Diamond on look for it only on the original
MAXX's Mitsubishi "Apricot" board, whose identity the emulated BIOS then
carries. On MAXX (New) an RTL8139, which the Linux releases drive with 8139too. *Tools > Network settings...* (PeepeeBox's
dialog) plugs it into SLiRP NAT, a real network card (PCap) or a switch shared
with other MegaPPBox cabinets — **Mega-Link**, the head-to-head games Merit
linked over a hub (see [docs/README.md](docs/README.md)).

Everything the folder needs lives inside it — all settings in `MegaPPBox.cfg`
(including the images folder and the image in use), the CMOS in `nvr\` —
whatever the working directory, so two folders are two independent cabinets.
`-P <folder>` still points it elsewhere. A folder from an earlier build is taken
over at the first start: `86box.cfg` becomes `MegaPPBox.cfg`, and
`86box_global.cfg` and `megappbox-library.ini` are folded in and removed.

**The games write to their disks** (`NVRAM.DAT`, `DEBUG.DAT`, operator settings).
The manager runs images in place, so point it at copies, not at your only copy.

Hardware profiles
-----------------

| Profile | Board (stands in for) | CPU | RAM | Video | Sound |
|---|---|---|---|---|---|
| XL (CD Boot) | ASUS PVI-486SP3, SiS 496 (Telco 486) | Enhanced Am486DX4 100 | 32 MB | Cirrus CL-GD5430 | CS4231A on the I/O board |
| XL (CD Boot, early) | same | same | 32 MB | same | same |
| XL (HDD Boot) | same | same | 32 MB | same | same |
| MAXX (Old) | ASUS TX97, i430TX (Mitsubishi/Apricot) | IDT WinChip C6 200 | 32 MB | ATI 264VT3 | Crystal CS4236B |
| MAXX (New) | same | Pentium 200 | 64 MB | ATI 264VT3 | C-Media CMI8738 |

None of the original boards is in 86Box. The real MAXX board has an i430TX, a
PIIX4, an onboard ATI Rage IIC, a CS4236B and a Phoenix BIOS ("Apricot BIOS
10.83", which survives only as a runtime dump); MegaPPBox uses the ASUS TX97
(same chipset, Award BIOS) with an ATI 264VT3 card. The video chip matters: the
Linux releases refuse to start ("Invalid motherboard detected") unless
`/proc/pci` shows one of Merit's sets, for MAXX an 82439TX, a Rage IIC or a
264VT3, and an 82371. The 264VT3's video BIOS is the one in the HP Pavilion
81xx (ASUS TX97-XV) system BIOS, which has the same chip on board. The MAXX
profiles can be moved to a spare ASUS P/I-P55TVP4 (i430VX) from the manager.

All of them have the Merit I/O board (coin mech, Operator Setup and Calibrate
buttons, security key; on XL also the CS4231A codec and the U12 ROM), a
MicroTouch touch screen on COM1 and a PS/2 keyboard. XL disks appear as a
`MEGATOUCH MERIT000` drive, which the XL games check.

* **XL (CD Boot)** opens the I/O board's U12 ROM (SA3014-04 R00): its ROM-DOS
  boots, loads the CD driver and runs `LAUNCHIT.BAT` from the disc (XL 6000,
  XL Gold). **XL (CD Boot, early)** has the earlier U12 ROM (SA3014-03 R3) for
  the XL R0-R3 and Super 5000 discs. Both boards carry 32 KB of battery RAM at
  C8000 and a DS1205 MultiKey, and their BIOS has no hard disks set, so POST goes
  straight to the ROM-DOS without looking for one.
* **XL (HDD Boot)** is the Platinum / Titanium board: DS1991 iButton.
* **MAXX**: DS1991 iButton, and the I/O board's PC Card slots (a Cirrus
  PD6722 at 0x3E0, both empty), which the Linux releases' PC Card services and
  MAXX 1st's CardSoft look for. COM2 is left out unless the image has the
  modem fitted (the Linux releases' modem probe waits forever on a silent
  port); its UART is still built, hidden, so the modem can be plugged in
  while the cabinet runs. A Linux release rebooting itself resets the
  whole board, as the reset line does; the TX97's own warm boot hangs there.
  The releases write the cabinet's own board settings into the CMOS, so each
  profile's settled CMOS is put back at every start (on XL too: the three XL
  profiles share one board with different drive settings).

Every drive runs flat out: hard disks without seek or rotation delays, the CD
drive at Turbo, a floppy drive (if you fit one) with turbo timings.

Security keys
-------------

MegaPPBox emulates the key on the Merit I/O board, but the key's contents come
from a dump of a real one, and **none are included**. Import yours with
**Import key…** in the Machine Manager (or *Import key...* in the key menu on
the status bar): a 264-byte file is a DS1991 in keyflasher's "full" format, a
192-byte file a DS1205 MultiKey (MAME's `multikey` dumps). The release a dump
is for is read from the dump itself (the cipher and signature that decrypt it,
as keyflasher and the MTKeyWork study found them), and the dump is copied into
the `keys\` folder next to the executable as `<family>_full_<ROM ID>` or
`<family>_multikey_<part>`; the Machine Manager then fits it to every image of
that release. MAXX and XL 6000 dumps share a format, so for those the import
asks, unless the file name already says. *Other key file…* fits a dump from
anywhere without copying it. See [`keys/README.md`](keys/README.md) for the
families, and for building dumps into a local build.

Two families need a word:

* **MCROWN**: Crown V16 checks the key's serial range ("Key range mismatch"):
  early family-02 parts are outside it.
* **MRUBY2**: Ruby 2 (V11) derives its passwords like keyflasher's MRUBY family
  but without the XOR on the password seeds, so a Ruby 2 dump re-keyed with
  keyflasher's MRUBY rules fails.

How images are identified
-------------------------

From the image's contents, never its file name:

* **DOS disks** (XL Platinum … MAXX Emerald 2): the release string in
  `MERIT2\EXEC\MEGACDLL.EXE`, e.g. `PG3004-01 V1.02`, `PG3002 V9.01`.
* **Linux disks** (MAXX Ruby … Crown): the MD5 of `/usr/local/bin/start`, else
  `/etc/version`, else the version in the game's own log.
* **XL CDs**: `PG3001-00-01 R02` (XL 6000, Gold) or `R5B   (02/11/98)` (XL,
  Super 5000) in the disc's `MEGACDLL.EXE`.

MAXX releases are numbered in one series: V3 MAXX, V4 2000, V5 2000 Plus,
V6 Diamond, V7 Double Diamond, V8 Emerald, V9 Emerald 2, V10 Ruby, V11 Ruby 2,
V12 Sapphire, V13 Sapphire 2, V14 Jade, V15 Jade 2, V16 Crown. The DOS releases
(to Emerald 2) default to MAXX (Old), the Linux ones to MAXX (New). The profile,
board and key can be changed before pressing **Run**; the ones in use are kept
in `MegaPPBox.cfg`.

Restore and upgrade discs and anything unrecognised are hidden unless you tick
*Show images that cannot run here*.

`MegaPPBox.exe --identify <image>...` prints the same information, one line per
image, without starting anything.

Cabinet controls
----------------

The toolbar (and the *Tools* menu) carries the cabinet's own controls: Machine
Manager, Coin 1–4, **Operator Setup** and **Calibrate**. The status bar shows the
profile in use, the fitted key (click it to fit another or pull it) and the
emulation speed. The touch screen is driven with the mouse. Help → Documentation
opens [docs/](docs/).

Status
------

Every image below was booted on MegaPPBox from a scratch copy with its default
profile and key, to its game, attract mode or first-run touch calibration:

* **XL CDs**: XL R1; Super 5000 R5B, R5E; XL 6000 R02, R04, R07; XL Gold R00
  and R01.
* **XL disks**: Platinum V1.02; Double Platinum V2.00; Titanium V3.00;
  Titanium 2 V4.00.
* **MAXX DOS**: MAXX V3.06; 2000 V4.00, V4.01; 2000 Plus V5.00; Diamond V6.03;
  Double Diamond V7.01; Emerald V8.04; Emerald 2 V9.00, V9.01.
* **MAXX Linux**: Ruby 2 V11.00, V11.05; Sapphire V12.01; Sapphire 2 V13.00;
  Jade V14.00, V14.21; Jade 2 V15.10; Crown V16.00, V16.10. A new image restarts
  once and then asks for touch calibration.

Open:

* **MAXX V3.02**: its key bit timing is erratic in emulation (write slots of
  25-75 µs), so the key reads garbled; V3.06 of the same game is fine.
* **Emerald V8.05**: the only known disk has an altered game executable whose
  key check no genuine key can pass.
* **Jade V14.00 (Played)**: the partition table describes a ~61 GB disk; the
  image holds its first 4.36 GB and does not boot.
* **Super 5000 R5I**: the disc lacks `IDLESMK\ENGLISH\TSOL.SMK`, which the game
  opens ("ERROR in openfile"); R5B and R5E have it.

Building
--------

MSYS2 MinGW64 with a static Qt 5, as for 86Box:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTATIC_BUILD=ON -DQT=ON -DUSE_QT6=OFF -DDYNAREC=ON -DNEW_DYNAREC=OFF
cmake --build build
```

`scripts/make-build.ps1 -Name <what> [-Library <folder>]` then stages a runnable
folder (exe, `roms\`). `tools/icons/mkicons.py` redraws the new toolbar
icons.

What was removed
----------------

About 900 machines down to three boards; video to three cards; sound to two cards
and the codec; networking down to two cards on SLiRP, per image (an RTL8139
for the Linux MAXX releases, a TE-16XP ISA card for the DOS ones), SCSI, every non-IDE disk controller, ZIP/MO/tape,
cassette and cartridge, printers, ISA memory/ROM/RTC cards, the proprietary CD
interfaces, the MIDI synthesisers, the Voodoo, the VM manager and the Settings
dialog. The commit log says what went and why, one area at a time.
