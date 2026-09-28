# MegaPPBox documentation

MegaPPBox runs untouched Merit Megatouch XL and MAXX disk and CD images. This
page is what *Help → Documentation* opens; the [project README](../README.md)
covers the hardware in more depth.

## Getting started

1. Put `MegaPPBox.exe` and its `roms\` folder in a folder of their own. That
   folder is one cabinet: its settings (`MegaPPBox.cfg`) and CMOS (`nvr\`) stay
   in it.
2. Run `MegaPPBox.exe`. The **Machine Manager** opens.
3. Press **Browse…** and choose the folder that holds your images (subfolders
   are searched too). The manager reads every `.img`, `.hdd`, `.raw` and `.iso`
   and lists the Megatouch releases it can run.
4. Pick one and press **Run**. The next start boots straight into it.

**Use copies of your images.** The games write to their disks (operator
settings, `NVRAM.DAT`, logs), and MegaPPBox runs the images where they are.

## The Machine Manager

Open it from the toolbar or *Tools → Machine Manager…*. For each image it
shows the release and version it found inside the image, and the hardware
profile it will use. Before pressing **Run** you can change:

- **Hardware profile**: XL (CD Boot), XL (CD Boot, early), XL (HDD Boot),
  MAXX (Old), MAXX (New). The defaults are the right ones; see below.
- **Motherboard** (MAXX only): the ASUS TX97, or a spare i430VX board.
- **Key**: the security key fitted to the Merit I/O board.
- **Modem on COM2**: a 56K modem on COM2 (0x2F8, IRQ 3), as the MAXX
  TournaMAXX/MegaNET kits fitted one. Off by default. *Tools > Modem
  settings...* can make it dial a TCP host instead of a line, e.g. a
  [TournaMAXX-Revival](https://github.com/Xeon3D/TournaMAXX-Revival) server
  for the DOS releases' tournaments and updates.
- **Network card** (MAXX only), behind NAT. Off by default.
  - On MAXX (Old): a TRENDnet TE100-PC16 in socket A of the I/O board's PC Card
    slots. The DOS releases ship no driver for it; TRENDnet's own work, e.g.
    `LANEN.EXE` (with or without CardSoft) and then `LE100PD.COM 0x62 <irq> <port>`.
  - On MAXX (New): an RTL8139, which the Linux releases (Ruby onward) can use.

Both options are kept with each image, in `MegaPPBox.cfg`, and take effect
when the image starts.

*Show images that cannot run here* also lists restore discs and unrecognised
files, with the reason each one is not offered.

## Hardware profiles

| Profile | For |
|---|---|
| XL (CD Boot) | XL 6000 and XL Gold CDs |
| XL (CD Boot, early) | XL (R0-R3) and XL Super 5000 CDs |
| XL (HDD Boot) | XL Platinum, Double Platinum, Titanium, Titanium 2 disks |
| MAXX (Old) | MAXX, 2000, 2000 Plus, Diamond, Double Diamond, Emerald, Emerald 2 (DOS) |
| MAXX (New) | Ruby, Ruby 2, Sapphire, Sapphire 2, Jade, Jade 2, Crown (Linux) |

The profile in use is shown in the middle of the status bar.

## Security keys

Every Megatouch release checks a Dallas security key on the I/O board. The
known keys are built into MegaPPBox and named by release and territory, e.g.
*MAXX Jade (USA) 1*, *MAXX Crown (Canada)*, *XL 6000*. The manager fits the one
that suits the image.

To change the key while a game runs, click **Key:** in the status bar. The
list has the built-in keys, any dumps in a `keys\` folder next to
`MegaPPBox.exe`, **Other key file…** for a dump of your own (264-byte DS1991
"full" dumps from keyflasher, or 192-byte DS1205 MultiKey dumps), and
**Remove key**. Games read the key at start-up: reset the machine after a
change.

## Cabinet controls

| Toolbar and *Tools* menu | Does |
|---|---|
| Coin 1-4 | drops a coin in that slot |
| Operator Setup (spanner) | the setup button inside the cabinet |
| Calibrate (crosshair) | the touch-screen calibration button |

The key is in the *Tools* menu too. The touch screen is the mouse. A new Linux MAXX image restarts once on its first
boot (and after a hardware change), then asks for touch calibration.

The MAXX I/O board's PC Card slots are there (a Cirrus PD6722, both slots
empty), so the Linux releases start their PC Card services without errors.

## Files in a cabinet folder

| File | What |
|---|---|
| `MegaPPBox.cfg` | every setting: the images folder, the image, profile, board and key in use, each image's modem and network options, window and emulator preferences |
| `nvr\` | the boards' CMOS and flash, the touch calibration |
| `keys\` | optional: your own key dumps |

A folder from an earlier build is converted at the first start
(`86box.cfg` → `MegaPPBox.cfg`; `86box_global.cfg` and
`megappbox-library.ini` are folded in and removed).
