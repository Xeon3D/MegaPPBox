# MegaPPBox on Android: feasibility

> Moved here from MegaPPBox on 2026-09-30, where the work began (worktree `MegaPPBox-android`,
> branch `android-prep`, never pushed). Paths are this repository's: `native/` was
> `src/android`, `app/` and `scripts/` were under `android/`. The core-side hooks that lived in
> MegaPPBox's tree are now `patches/`. The desktop fixes found here (the Merit drive preset
> crash on clang arm64 builds; FreeType and winmm for a shared Qt) went into MegaPPBox itself,
> as `f2c8b6e`.

2026-09-30, against `main` at `2dd4d09`. This started as a desk study of the tree, CI results and
upstream state. Steps A and C have since been run on the Windows PC (**Results**). Nothing has
been run on Android yet.

## Verdict

**It is feasible, and the CPU core is the smallest part of the work.** The arm64 JIT already
exists, and CI builds it green on macOS arm64 *and on Linux arm64*. Linux arm64 is the closer
relative, since Android is Linux/ELF too. What is missing is everything around the core:
building against Qt for Android, storage, the app lifecycle, packaging, and dropping desktop-only
dependencies.

**The biggest risk was not Android at all, and step A has now retired it.** Every Megatouch image
had only been verified on the Windows x64 build, which uses the **old** dynarec
(`NEW_DYNAREC=off`, `release.yml`). On arm64 CMake forces the **new** dynarec
(`CMakeLists.txt:183`), a different code generator that had never run a Megatouch image. The
macOS arm64 zip had shipped with 1.02 through 1.04 without that check. On 2026-09-30, both
dynarecs were swept over all 33 images in `F:\FIXEDIMAGES` and gave the same results (see
**Results** below).

## What already carries over

| Piece | Evidence | Android status |
|---|---|---|
| arm64 recompiler | `src/codegen_new/codegen_backend_arm64*.c`; CMake forces NDR on arm64 | builds green: `CMake (Linux)` matrix `NDR, arm64` on `ubuntu-26.04-arm`, and macOS arm64 |
| JIT memory | `plat_mmap` Unix branch: anonymous RWX `mmap` (`src/qt/qt_platform.cpp:666`); i-cache flush with `__clear_cache` on aarch64 (`codegen_allocator.c:216`) | Android lets app processes map anonymous RWX memory, which is what other JITs on the platform rely on. No `MAP_JIT` / `pthread_jit_write_protect_np` (those are Apple-only and already `#ifdef`'d) |
| Platform paths | `plat_fopen`/`plat_fopen64` take the `Q_OS_LINUX` branch (`qt_platform.cpp:248`) | Qt also defines `Q_OS_LINUX` on Android, so the POSIX code paths are taken |
| DS1991 key timing | the 1-Wire slave reads **emulated** time (`now_us()` from `tsc`, `src/device/merit_io.c:246`) | a slow or throttled phone slows the guest but doesn't break the key protocol. Coin/button pulses use host ms (`merit_io_pulse`); that's harmless |
| Touch screen | MicroTouch reads absolute pointer coords (`mouse_get_abs_coords`, `mouse_microtouch_touchscreen.c:479`) | Qt on Android turns touches into mouse events, so this works as-is. A direct `QTouchEvent` path also exists behind `TOUCH_PR` (`qt_rendererstack.cpp:736`), never enabled |
| Display | 640×480 guest; software renderer `qt_softwarerenderer.cpp` | fine as a first renderer |
| Sound | OpenAL backend (`src/sound/openal.c`) | openal-soft has AAudio / OpenSL ES backends |
| Mega-Link | `net_switch.c`: plain BSD UDP sockets (multicast local switch, or one unicast remote peer) | works; see network row below |
| ROMs | `roms/` is 3.1 MB | trivially bundled as APK assets, extracted on first run |

## What does not carry over

| Area | Problem | Action |
|---|---|---|
| **Qt version** | Every MegaPPBox build is Qt 5 (static Qt5 on Windows). Qt 5.15's open-source Android kits lag the current Android target-SDK requirements | build with Qt 6 for Android. `USE_QT6` exists (`CMakeLists.txt:236`, `src/qt/CMakeLists.txt:26`) and upstream builds Qt 6, but MegaPPBox has never been built with it. That makes a desktop Qt 6 build a prerequisite |
| **UI shape** | the desktop Widgets UI: `qt_mainwindow.cpp` (2942 lines, menus + toolbar), Machine Manager, Megatouch ident, Network dialog. The Setup/coin actions call `merit_io_pulse` from toolbar actions (`qt_mainwindow.cpp:352`) | keep the Machine Manager as the launcher (it works as a list). Run the cabinet fullscreen with a small overlay for Setup / coins / Cabinet / quit. The menus and toolbar get hidden on Android, not ported |
| **OpenGL renderer** | desktop GLSL `#version 130/150` (`qt_openglrenderer.cpp:85`); Vulkan renderer and librashader (Rust) | software renderer first. GLES shader variants later if scaling needs them. Build with `LIBRASHADER=OFF` and no Vulkan |
| **Storage** | disk images are 2.1 GB (XL) to 6.4 GB (MAXX). Scoped storage blocks `fopen` on arbitrary user paths. The blank install disks are sparse, but sparse files don't survive a FAT/exFAT SD card | v1: images live in the app's external files dir (copy them in over USB or the Files app). Later: import through the Storage Access Framework and open via `/proc/self/fd/N`. Images on the phone are always copies, which suits guardrail 1 anyway |
| **Lifecycle** | 86Box has no save states. Android may kill a backgrounded app, which is the same as pulling a cabinet's plug mid-write (guest FAT/ext2, `C:\NVRAM.DAT`, key and CMOS files in `nvr\`) | pause on background; a foreground service while a cabinet runs; flush `nvr\` and the disk on pause |
| **Dependencies** | `libslirp` is `REQUIRED` (`src/network/CMakeLists.txt:21`) and pulls in glib; pcap needs root; VDE, libserialport, rtmidi, FluidSynth, Discord, SDL joystick are desktop-only | cross-build slirp+glib, or make it optional (Mega-Link uses the switch, not slirp). Drop the rest behind Android `if()`s. freetype/png/zlib/zstd/openal build with the NDK (vcpkg has `arm64-android`) |
| **Network** | the Local Switch is UDP multicast. Android drops inbound multicast on Wi-Fi unless the app holds a `WifiManager.MulticastLock`. PCap is impossible without root | take the lock (JNI) while a switch-backed card is fitted. The remote (unicast) switch mode needs nothing special and also covers phone↔PC Mega-Link |
| **Updater** | `qt_autoupdate.cpp` replaces files next to the exe. An APK can't do that | on Android, open the release page, or download the APK and hand it to the package installer |
| **Keyboard** | evdev/xkb/raw-input code is irrelevant on Android. The guest only needs the keyboard for rare setup work | Qt key events from the soft keyboard or a USB/Bluetooth keyboard are enough |
| **Distribution** | the security keys are built into the executable (`release.yml` header) | sideloaded APK from GitHub Releases only; no Play Store. As with every release, only when the user says so, with approved notes |

A second frontend was considered: an SDL3 app (SDL ships its own Android glue). It is not
recommended. MegaPPBox removed upstream's SDL/unix UI (`src/unix/` has no `unix.c` left), and the
Megatouch-specific UI (Machine Manager, ident, network) is all Qt. An SDL frontend would mean
rewriting the `plat_*` layer and all of that UI. Qt 6 keeps one UI codebase for four platforms.

## Risks

1. **New dynarec correctness on these images** (high impact, unknown likelihood). The CPU models
   are `am486dx4_slenh` (XL), `winchip` 200 MHz (MAXX Old) and `pentium_p54c` 200 MHz (MAXX New).
   All the image sweeps so far ran the old dynarec. Anything the new one gets wrong (CauseWay's
   CPUID path, FPU in the games, self-modifying code) would show up on every arm64 platform,
   macOS included. Mitigation: step A. **Result: no difference on any image (Results).** What
   remains untested is the arm64 *backend* itself: step A ran the new dynarec's x86-64 backend,
   so it cleared the shared front end (decoder, IR, block handling) but not
   `codegen_backend_arm64*.c`. Step B covers that.
2. **Speed** (medium). The emulated CPU runs on one host thread. A 486DX4-100 is light. The
   200 MHz MAXX profiles, especially the Linux/XFree86 releases, are the heavy case. Current
   flagship phone cores are roughly Apple-M1-class single-threaded, which should be enough.
   Mid-range cores are well below that, and phones throttle under sustained load. This is an
   estimate, not a measurement: step B measures it. The target phone is a Galaxy S25 Ultra
   (Snapdragon 8 Elite for Galaxy, Oryon cores). That is the fast end of the range, so the
   risk is mainly sustained-load throttling.
3. **Timing-marginal images** (low–medium). The key reads emulated time, so it is robust. But MAXX
   V3.02 already has erratic key-slot timing on desktop, and a slower host may make that worse.
4. **Qt 6 migration** (low–medium). Upstream carries Qt 6 support, but MegaPPBox's own Qt code
   (Machine Manager, ident, network dialog, updater) had only been compiled against Qt 5.
   **Result: every source file compiles against Qt 6.11. Two missing CMake links fixed; the
   sweep is identical (Results).** The Machine Manager, Network dialog and updater were not
   exercised: the sweep boots images directly.
5. **Data loss on kill** (medium, by design of the platform). Addressed by the lifecycle row.
   Worth a test that kills the app mid-attract-mode and reboots the image.

## Prior art

Upstream 86Box has no Android build. It publishes Linux arm64 NDR AppImages, so the arm64 JIT is in
real use. An "86Box Android" discussion has been open upstream since 2022 (#2848). The only
Android repo on GitHub, `ahmedbarakat2007/86Box-droid`, is an empty Gradle stub whose README says
"Initial project not ready". There is nothing to reuse, and nobody to duplicate.

## Proposed steps, each with a gate

| Step | Work | Gate | Rough size |
|---|---|---|---|
| **A** | Windows x64 build with `NEW_DYNAREC=ON`; run `p12-sweep.ps1` and `p12-xlsweep.ps1` | every image that runs on ODR runs on NDR. Fix or report before going on | 1–3 days |
| **B** | run the existing Linux arm64 or macOS arm64 build on real arm64 hardware (Pi 5, an ARM Mac or an ARM VM) with an XL and a MAXX (New) Linux image; log the speed % | full speed on XL; note the MAXX margin | 1–2 days |
| **C** | desktop Qt 6 build of MegaPPBox (Windows) | same sweep result as Qt 5 | 2–4 days |
| **D** | Android bring-up: NDK + Qt 6 arm64-v8a, software renderer, one XL image from the app's files dir, touch, sound | XL Platinum reaches attract mode and takes touches on a phone | 1–2 weeks |
| **E** | Android shell: fullscreen + overlay, Machine Manager as launcher, lifecycle/foreground service, MulticastLock, image import | a Mega-Link game between a phone and a PC cabinet; the kill-and-reboot test passes | 2–4 weeks |
| **F** | packaging: an arm64-v8a APK job in `release.yml`, signing key kept as a secret, updater hook | a tagged build publishes the APK next to the three zips (only when a release is approved) | ~1 week |

A and C are done (below). B is next. One possibly cheaper route to B is the target phone itself:
the Linux arm64 build under Termux with a proot distro and Termux:X11. That would test the arm64
backend on the S25 Ultra's own cores before any porting. It is untried, and proot adds overhead.

A through C improve the desktop builds regardless of Android: A validates the macOS arm64 zip we
already ship, and C removes the dependency on Qt 5. That makes them worth doing even if the
Android port is never finished.

## Results (2026-09-30)

All three builds come from one clean tree: `2dd4d09` plus the CMake fix below, in the worktree
`MegaPPBox-android`, branch `android-prep`.

| Build | Configure |
|---|---|
| `build-odr` (baseline) | mingw64, static Qt 5.15, `NEW_DYNAREC=OFF`: what the Windows release ships |
| `build-ndr` | the same with `NEW_DYNAREC=ON` (x86-64 backend) |
| `build-qt6` | ucrt64, shared Qt 6.11.2, `STATIC_BUILD=OFF`, old dynarec |

**Method.** `TouchPPBox\scripts\p19-sweep.ps1` boots every image in `F:\FIXEDIMAGES`: 21 MAXX
disks, 4 XL disks and 8 XL CDs. It goes through `p12-run.ps1`, three at a time, each in a scratch
VM with a fresh `nvr\`, fast-forward on, and 240 s before a screenshot. Disks run from a scratch
copy (deleted afterwards); CDs run in place, read-only. Output is in
`F:\TouchPPBox Folder\_work\p19\{odr,ndr,qt6}\`: `summary.txt`, `sheet.png`
(`p19-sheet.py`), and `shot.png` plus `86box.log` per image.

**Step A: new dynarec.** The key counts (password OK / WRONG / sessions) are identical to the
baseline on 32 of 33 images. On the 33rd, Sapphire V12.01, the counts are 27/0/35 vs 27/0/36:
one session more in 240 s, which is run-to-run noise. Every screenshot shows the same state as
the baseline:
- 28 images are in attract mode or their menu.
- The same four Linux images sit on the Merit calibration screen: Crown V16.10, Jade V14.00,
  Jade 2 V15.10 and Sapphire V12.01.
- XL Double Platinum stops at ROM-DOS in the same place.

**Gate passed.**

**Step C: Qt 6.** Every source file compiles unchanged against Qt 6.11. The link needed two
libraries that the static Qt 5 build had been supplying implicitly:
- **FreeType**: `imgui_freetype` calls it directly. `src/CMakeLists.txt` now links it on every
  build except static Windows.
- **`winmm`**: `timeBeginPeriod` in `qt_main.cpp`. `src/qt/CMakeLists.txt` now links it on
  Windows.

With those, the sweep matches the baseline image for image, with the same Sapphire wobble.
**Gate passed.** Not covered: a static Qt 6 build (MSYS2 has no `qt6-static`), and the dialogs.

**Found on the way.** Both of these are identical on every build, so they are not dynarec or
Qt 6 issues:
- XL Double Platinum V2.00 boots ROM-DOS, fails to find a CD-ROM and loops in the BIOS at
  `F000:E14C`. It does the same on staged build 11 (`cfe15cc`), the build the `FIXEDIMAGES`
  README lists it as working on.
- Four Linux touchfix images open on the calibration screen under a fresh scratch `nvr\`.

Both are handed to a separate investigation. (The user has since accepted the calibration
screens and will handle Double Platinum.)

**Step D: Android bring-up (in progress).** An arm64-v8a APK builds and is signed. It has not
yet run on a phone.
- **Toolchain.** Everything lives user-local in WSL under `~/mpb-android`; no root was needed.
  `scripts/setup-toolchain.sh` installs Qt 6.11.3 (host + `android_arm64_v8a`, via
  aqtinstall), Corretto 21 and the Android SDK. The SDK part covers platform 36, build-tools
  36.1.0 and NDK 27.2.12479018, the NDK Qt was built with. The SDK licences were accepted with
  the user's OK.
- **Libraries.** `scripts/build-deps.sh` builds zstd, libpng, FreeType, libsndfile and OpenAL
  Soft (OpenSL ES) static for arm64.
- **Build.** `scripts/build.sh` configures, builds and signs, writing
  `out/MegaPPBox-debug.apk`. That is a release build signed with a local debug key.
  `scripts/deploy.ps1` installs it over adb and pushes a test cabinet
  (`android/test/MegaPPBox.cfg`: XL Platinum).

Source changes (all but the last two are behind `ANDROID` / `Q_OS_ANDROID`):
- `qt_add_executable`, which needed every `target_link_libraries(86Box …)` to say `PRIVATE`.
  That is harmless for an executable on desktop.
- No libslirp: `NO_SLIRP` in `network.c`.
- No X11, evdev, xinput2 or Wayland.
- No SDL: a no-op `android_joystick.c`.
- No desktop OpenGL 3 renderer: the software renderer is used. imgui's GLES 3 backend stays
  for the on-screen display.
- No self-updater.
- `qt_android.cpp` hands `pc_init()` its paths: `-P` is the app's external files directory,
  `-R` the ROMs unpacked there from `assets/roms`, `-L` a log file there.
- `cpu.h` `#undef CR0`: bionic's `<sys/ioctl.h>` defines it.
- FreeType and `winmm` now linked on Windows when Qt is shared. That was the step C fix.

Both desktop builds (mingw64 static Qt 5, ucrt64 Qt 6) still build. XL Platinum still boots with
the same key results.

**First Android run: the Windows Android emulator** (2026-09-30).
- **Setup.** The SDK is in `%LOCALAPPDATA%\Android\Sdk`; the virtual device is
  `MegaPPBox_Android16`: Android 16, x86_64, WHPX-accelerated. `scripts/test-emulator.ps1`
  boots it and deploys the app. The x86_64 image runs the arm64 APK through Android's ARM
  translation, including the code the dynarec generates. (The WSL emulator also booted, but
  its window never reached the Windows desktop.)
- **Result.** XL Platinum V1.02 **reaches attract mode with its key accepted, at 99% speed,
  under translation.**

On the way there, a crash exposed a **latent bug that also affects the shipped macOS arm64
build**:
- The Merit drive presets (`merit000`, `merit0011`, used by every XL profile) have no geometry.
  `hdd_preset_apply()` still ran the geometry maths, dividing by the zero zone count.
- Dividing by zero is undefined, so clang assumes `zones != 0`, runs the zone loop anyway and
  walks off the zone array. GCC on Windows happened not to.
- Fix (`src/disk/hdd.c`): skip the geometry for a preset without zones, keeping everything the
  drive actually uses. That is the cache model and the fixed seek time
  `hdd_seek_get_time()` gives a drive without zones.
- The Windows build with the fix gives identical key results on XL Platinum and Titanium 2.
- Any clang arm64 build without the fix should crash on every XL image. That includes macOS;
  not verified there, as there is no Mac to test on.

**First phone run: Galaxy S25 Ultra** (SM-S938B, Android 16, Snapdragon 8 Elite SM8750,
2026-09-30). `scripts/deploy.ps1` installed the APK and pushed XL Platinum. MegaPPBox **boots XL
Platinum natively on arm64 to the game's Megatouch Platinum title screen, with the key
accepted.** Android allowed the dynarec's executable memory (120 MB); the app used about 166% CPU
(of 800%). This is the first run of the arm64 dynarec backend on a Megatouch image, so step B
is largely answered on the target hardware itself. **User-confirmed the same day: 100% speed,
touch works, sound works. Step D's gate passed.**

**Step E: the native app (no Qt), 2026-09-30.** The user asked for Android framework UI
instead of Qt, so the Qt-for-Android build was replaced:
- **Native library.** `libMegaPPBox.so` is the emulator core plus `native/`, a new
  platform layer with no Qt:
  - `android_plat*.c` come from upstream's non-Qt `src/unix/sdl_plat*.c`, with SDL removed;
  - `android_ui.c` holds the `ui_*` calls;
  - `android_globals.c` holds the globals the Qt frontend used to define, and
    `cdrom_mount`;
  - `android_main.cpp` is the JNI bridge: the emulation loop, frames into an
    `ANativeWindow` (RGBX), touch, the Merit lines and pause;
  - `android_ident.cpp` is `qt_megatouch_ident.cpp` in plain C++, with hand-written
    matchers instead of regexes and its own MD5.
- **App.** `app/` is Kotlin with framework widgets only, and no AndroidX:
  - **Machine Manager:** the images in the app folder, identified from their contents. A tap
    runs one with its identified profile and default key; there are no pickers, as the user
    asked. Import copies an image in through the document picker; a long press deletes one.
  - **Cabinet:** an app bar (release, profile · resolution · speed), the 4:3 picture centred
    in a `SurfaceView`, and a bottom bar with Credit, Setup, Calibrate, Fullscreen and More
    (Machine Manager, Pause, Reset, Exit). It pauses and saves in the background, holds the
    Wi-Fi multicast lock, and draws edge to edge.
- **Build.** `scripts/build.sh` makes the library with the NDK toolchain and the APK with
  Gradle 9.3 / AGP 9.0. The APK is about 24 MB, against 66 MB with Qt.
- **Desktop.** Unchanged. The Qt files are back to `main`; only the `PRIVATE` links and the
  step C `winmm`/FreeType fix remain. XL Platinum and Emerald give the same key results.
- **Fix found on the way.** Touch needs `mouse_input_mode = 1` (absolute). On desktop the
  Qt window sets it; the native app sets `mouse_input_mode_initial`.

**On the S25 Ultra**, user-confirmed: XL Platinum runs at 100% with touch and sound; the
Machine Manager lists and runs images.

**Follow-ups:**
- `android_ident.cpp` duplicates `qt_megatouch_ident.cpp`. The desktop could use the plain
  C++ one too, but that needs a sweep.
- The Qt-era Android pieces under `~/mpb-android` (`Qt/`, `build/`, the WSL emulator,
  `extra-libs/`) can be deleted.

**Next (step E, continued):**
- a sweep of the other images on the phone (the user is testing them);
- Mega-Link between the phone and a PC cabinet;
- per-image options (modem, network card) in the Machine Manager.

Sources: [86Box/86Box discussion #2848](https://github.com/86Box/86Box/discussions/2848),
[ahmedbarakat2007/86Box-droid](https://github.com/ahmedbarakat2007/86Box-droid),
[86Box Linux arm64 NDR AppImage (v4.2.1 mirror)](https://sourceforge.net/projects/eighty-six-box.mirror/files/v4.2.1/86Box-NDR-Linux-arm64-b6130.AppImage/download).
