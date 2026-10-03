# MegaPPBox documentation

MegaPPBox runs untouched Merit Megatouch XL and MAXX disk and CD images. This
page is what *Help → Documentation* opens; the [README](../README.md) walks
through the first start step by step, and the [technical notes](technical.md)
cover the hardware in more depth.

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
  TournaMAXX/MegaNET kits fitted one. On by default for the releases with an
  on-line client (MAXX Diamond on), off for the others. *Modem settings...*
  (beside it, or in *Tools*) can make it dial a TCP host instead of a line,
  e.g. a [TournaMAXX-Revival](https://github.com/Xeon3D/TournaMAXX-Revival)
  server for the DOS releases' tournaments and updates; these settings are
  the same for every image.
- **Network card** (MAXX only), behind NAT. Off by default.
  - On MAXX (Old): an ISA TRENDnet TE-16XP, jumperless at 0x340, IRQ 11. Every
    DOS release from MAXX 2K on loads its drivers from `C:\ETHERNET` (`LSL`,
    `ODI.COM`, `NPTSR`). (From 2K Plus on they also knew the newer TE-16PT;
    2K did not, so the one card that works for all is fitted.) Double Diamond
    on look for the card only on the original MAXX's
    Mitsubishi "Apricot" board, so with a card fitted the emulated BIOS carries
    that board's identity too.
  - On MAXX (New): an RTL8139, which the Linux releases (Ruby onward) can use.

Both options are kept with each image, in `MegaPPBox.cfg`. The network card
takes effect when the image starts. The modem goes in or out at once
(*Tools > Modem on COM2*), with no reset: a release that already looked for
it at boot only sees the change after a restart, which MegaPPBox offers but
does not make. A new telephone line or host (*Tools > Modem settings...*)
is taken at once too; a call in progress keeps its host, and drops if the
line is now not connected. *Network settings...* beside the option sets the
selected image's card (see below), running or not.

### Network settings and Mega-Link

*Tools > Network settings...* fits the cabinet's card (or *None*) and chooses
what it is plugged into: **SLiRP** (NAT to the internet), **PCap** (a real
network card on this PC), or a **switch** shared with other MegaPPBox cabinets
— **Local Switch** (this PC or LAN) or **Remote Switch** (over the internet).
The settings, and the card's MAC address, are kept with the image; with the
network option off the dialog still shows them, and fitting the card turns it on.

Mega-Link — up to 8 cabinets linked for head-to-head games, over a crossover
cable or a hub on the real thing — is the switch:

1. Run each cabinet from its own MegaPPBox folder, with its own copy of the
   image (two cabinets must never share one image file).
2. In each, *Tools > Network settings...*: the network card, connection
   **Local Switch** (cabinets on this PC or this LAN; give them the same
   shared secret, or none) or **Remote Switch** (over the internet, to a
   switch server). Reset when asked.
3. Allow MegaPPBox through the Windows firewall (private networks) when
   Windows asks.
4. The **Mega-Link** button appears on the main menu once two or more games
   see each other (*Setup > Games > Options > Linked Games Enabled*). All
   linked games must run the same software version.

Tested on a local switch with every DOS release from MAXX 2K and every Linux
release (up to six cabinets).

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

Every Megatouch release checks a Dallas security key on the I/O board.
MegaPPBox includes no key dumps: import a dump of your key with **Import key…**
in the Machine Manager (a 264-byte DS1991 "full" dump from keyflasher, or a
192-byte DS1205 MultiKey dump). The release it is for is read from the dump,
and it is copied into the `keys\` folder next to `MegaPPBox.exe`; from then on
the manager fits it to every image of that release. MAXX and XL 6000 dumps
share a format: the import asks which it is, unless the file name says.

To change the key while a game runs, click **Key:** in the status bar. The
list has the dumps in the `keys\` folder, **Import key...**, **Other key file…**
for a dump used where it is (not copied), and **Remove key**. Games read the key at start-up: reset the machine after a
change.

## Cabinet controls

| Toolbar and *Tools* menu | Does |
|---|---|
| Coin 1-4 | drops a coin in that slot |
| Operator Setup (spanner) | the setup button inside the cabinet |
| Calibrate (crosshair) | the touch-screen calibration button |

The key is in the *Tools* menu too. The status bar shows the profile in use,
the fitted key (click it to change it) and the emulation speed. The touch screen
is the mouse. A new Linux MAXX image restarts once on its first
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
