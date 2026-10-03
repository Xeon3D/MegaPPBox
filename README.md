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
[step 5](#step-5-add-your-keys)).

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

### What you need

- **A Megatouch image**: the hard disk or CD of a real Megatouch cabinet, copied
  into a file (usually ending in `.img` or `.iso`).
- **A dump of the cabinet's security key**: a small file (264 or 192 bytes) read
  from the key on the cabinet's board. MegaPPBox does not come with any keys.
- **A computer** with Windows, macOS (Apple M1 or newer) or Linux, or an
  **Android phone or tablet** (see [android/README.md](android/README.md)).

### Step 1: Download

Open the [latest release](https://github.com/Xeon3D/MegaPPBox/releases/latest)
and, under **Assets**, click the file for your computer:

| You have | Download |
|---|---|
| Windows | `MegaPPBox-windows-x64.zip` |
| Mac (M1 or newer) | `MegaPPBox-macos-arm64.zip` |
| Linux | `MegaPPBox-linux-x86_64.zip` |
| Android | `MegaPPBox-android-arm64.apk` (see [android/README.md](android/README.md)) |

### Step 2: Unzip it

On Windows, find the downloaded file (usually in **Downloads**), right-click it,
choose **Extract All…**, pick a place such as your **Documents** folder, and
click **Extract**. You now have a **MegaPPBox** folder. On a Mac, double-click
the `.zip`.

Keep everything in that folder together: the program needs its `roms` folder.
The folder is your cabinet; it remembers your games and settings.

### Step 3: Start it

- **Windows**: open the MegaPPBox folder and double-click **MegaPPBox.exe**. If
  Windows says *"Windows protected your PC"*, click **More info**, then **Run
  anyway**. (Windows says that about programs that are not signed by a big
  company; it only asks once.)
- **Mac**: double-click **MegaPPBox.app**. If macOS says it cannot be opened,
  open **System Settings → Privacy & Security**, scroll down, click **Open
  Anyway** next to MegaPPBox, and confirm. (macOS says that about apps that are
  not from the App Store; it only asks once.)
- **Linux**: run `bin/MegaPPBox` in the MegaPPBox folder.

The **Machine Manager** window opens. It is where you choose what to play.

### Step 4: Add your images

**Make a copy of your images first.** The games save their settings and scores
onto the image, so keep your original somewhere safe and use a copy.

In the Machine Manager:

1. Click **Browse…** and choose the folder that has your images in it. (No
   folder yet? Make one, then use **Import images…** to copy images into it.)
2. Wait a moment: MegaPPBox looks inside each image and lists the Megatouch
   games it finds, with their names and versions.

### Step 5: Add your keys

1. Click **Import key…** and choose your key file (you can pick several).
2. MegaPPBox tells you which game each key is for. If it asks you to choose
   (MAXX and XL 6000 keys look the same), pick the one you have.

You only do this once: the keys are kept in the MegaPPBox folder, and the right
key is used for each game automatically. **Keys…** shows the keys you have.

### Step 6: Play

Click a game in the list and click **Run**. Next time, MegaPPBox starts
straight into the same game.

- **The mouse is your finger**: click on the screen to touch it.
- **Coin** (toolbar) adds a credit.
- **Operator Setup** (the spanner) opens the cabinet's operator menus.
- **Calibrate** (the crosshair) lines the touch screen up with the mouse.
- **Machine Manager** (toolbar) goes back to the list to change games.

The first time you run one of the later MAXX games (Ruby to Crown), it restarts
once by itself and then asks you to calibrate the touch screen: click each
target as it appears.

### If something goes wrong

- **The game says the key is wrong or missing**: import the key for that game
  (step 5). **Keys…** shows which games each of your keys is for.
- **Your image is not in the list**: tick **Show images that cannot run here**;
  the list then says why each one is not offered.
- **Windows asks about the firewall**: that happens when cabinets link up over
  the network (Mega-Link). Allow it on private networks.

More: the [full documentation](docs/README.md) (Mega-Link, the modem, every
option) and the [technical notes](docs/technical.md) (the emulated hardware,
what runs, building).
