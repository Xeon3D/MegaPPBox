/*
 * MegaPPBox - Merit Megatouch cabinets on 86Box.
 *
 *          Android front end, native half: what the desktop builds' Qt main()
 *          and renderer do, behind a JNI interface for the Kotlin app
 *          (app/, class io.github.xeon3d.megappbox.Native).
 *
 *          - nativeInit(dir): pc_init with -P <dir>/ -R <dir>/roms/ -L <dir>/86box.log
 *            (the app's external files directory: MegaPPBox.cfg, nvr\, images).
 *          - nativeStart / nativeStop: the machine and its emulation thread.
 *          - nativeSetSurface: where frames go.  A frame is copied into the
 *            surface's buffer at the guest's own size (ANativeWindow geometry);
 *            the compositor scales it to the view, which the app keeps 4:3.
 *          - nativeTouch: a touch on the picture, as the MicroTouch's absolute
 *            position and button.
 *          - nativePulse: the I/O board's coin / Setup / Calibrate lines.
 *
 * Authors: MegaPPBox contributors
 */
#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "android_ident.hpp"

extern "C" {
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/ini.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/char.h> /* after device.h */
#include <86box/merit_io.h>
#include <86box/megatouch.h>
#include <86box/megatouch_keys.h>
#include <86box/mouse.h>
#include <86box/thread.h>
#include <86box/timer.h>
#include <86box/network.h> /* after thread.h and timer.h */
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/video.h>

extern bool fast_forward; /* sound.h declares it only when bool is a C macro */
extern int  nvr_dosave;
extern int  hard_reset_pending;
extern void ack_pause(void);
extern volatile int android_speed_percent; /* android_ui.c */
}

#define LOG_TAG "MegaPPBox"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

bool cpu_thread_running = false;

static std::thread     emu_thread;
static std::thread     onesec_thread;
static std::atomic_bool onesec_run { false };

/* The surface, guarded: the app replaces or drops it (rotation, background)
   while the emulation thread blits. */
static std::mutex      surface_mtx;
static ANativeWindow  *surface     = nullptr;
static int             surface_w   = 0;
static int             surface_h   = 0;

/* 86Box's startblit/endblit: kept for the core, which brackets its own buffer
   changes with them. */
static std::recursive_mutex blit_mtx;
extern "C" void startblit(void) { blit_mtx.lock(); }
extern "C" void endblit(void) { blit_mtx.unlock(); }

/* ---- frames ---------------------------------------------------------------- */

/* A finished frame: the visible part of monitor 0's buffer, x/y/w/h, as XRGB
   words (B, G, R, X in memory).  The surface is RGBX (R, G, B, X): swap R and B. */
static void
android_blit(int x, int y, int w, int h, int monitor_index)
{
    if ((monitor_index != 0) || (w <= 0) || (h <= 0) || (monitors[0].target_buffer == NULL)) {
        video_blit_complete_monitor(monitor_index);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(surface_mtx);
        if (surface) {
            if ((w != surface_w) || (h != surface_h)) {
                ANativeWindow_setBuffersGeometry(surface, w, h, WINDOW_FORMAT_RGBX_8888);
                surface_w = w;
                surface_h = h;
            }
            ANativeWindow_Buffer buf;
            if (ANativeWindow_lock(surface, &buf, nullptr) == 0) {
                const int rows = (h < buf.height) ? h : buf.height;
                const int cols = (w < buf.width) ? w : buf.width;
                for (int r = 0; r < rows; r++) {
                    const uint32_t *src = &(monitors[0].target_buffer->line[y + r][x]);
                    uint32_t       *dst = (uint32_t *) buf.bits + ((size_t) r * buf.stride);
                    for (int c = 0; c < cols; c++) {
                        const uint32_t p = src[c];
                        dst[c]           = 0xff000000 | ((p & 0xff) << 16) | (p & 0xff00) | ((p >> 16) & 0xff);
                    }
                }
                ANativeWindow_unlockAndPost(surface);
            }
        }
    }
    video_blit_complete_monitor(monitor_index);
}

/* ---- the emulation thread (qt_main.cpp's main_thread_fn) ------------------- */

static void
emu_thread_fn()
{
    using clock = std::chrono::steady_clock;
    plat_set_thread_name(nullptr, "main_thread");
    is_cpu_thread = 1;

    const int64_t quantum_ns  = force_10ms ? 10000000LL : 1000000LL;
    const int64_t max_debt_ns = 50000000LL;
    int64_t       debt_ns     = 0;
    int           frames      = 0;
    auto          old_t       = clock::now();

    while (!is_quit && cpu_thread_run) {
        const auto now = clock::now();
        debt_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(now - old_t).count();
        old_t = now;
        if (debt_ns > max_debt_ns)
            debt_ns = max_debt_ns;

        if (((debt_ns >= quantum_ns) || fast_forward) && !dopause) {
            pc_run();

            /* Every 2 emulated seconds the machine status is saved. */
            if ((++frames >= (force_10ms ? 200 : 2000)) && nvr_dosave) {
                nvr_save();
                nvr_dosave = 0;
                frames     = 0;
            }

            if (!fast_forward && (debt_ns >= quantum_ns))
                debt_ns -= quantum_ns;
            else
                debt_ns = 0;
        } else {
            if (hard_reset_pending) {
                hard_reset_pending = 0;
                pc_reset_hard_close();
                pc_reset_hard_init();
            }
            if (dopause)
                ack_pause();
            plat_delay_ms(1);
        }
    }

    cpu_thread_running = false;
    is_quit            = 1;
}

/* ---- JNI --------------------------------------------------------------------- */

#define JNI(name) Java_io_github_xeon3d_megappbox_Native_##name

extern "C" JNIEXPORT jint JNICALL
JNI(nativeInit)(JNIEnv *env, jclass, jstring jdir)
{
    const char *d = env->GetStringUTFChars(jdir, nullptr);
    std::string dir(d);
    env->ReleaseStringUTFChars(jdir, d);
    if (dir.empty() || (dir.back() != '/'))
        dir += '/';

    static std::string usr, roms, log;
    usr  = dir;
    roms = dir + "roms/";
    log  = dir + "86box.log";
    char *argv[] = { (char *) "MegaPPBox", (char *) "-P", (char *) usr.c_str(), (char *) "-R", (char *) roms.c_str(),
                     (char *) "-L", (char *) log.c_str(), nullptr };

    if (!pc_init(7, argv)) {
        LOGE("pc_init failed");
        return 1;
    }
    if (!pc_init_roms()) {
        LOGE("no ROMs in %s", roms.c_str());
        return 2;
    }
    pc_init_modules();
    /* The touch screen is the cabinet's only pointer: absolute input from the start
       (mouse_reset restores this at every hard reset).  On the desktop the Qt main
       window chooses it; without it, presses would go to the relative mouse. */
    mouse_input_mode_initial = 1;
    mouse_input_mode         = 1;
    video_setblit(android_blit);
    LOGI("initialised in %s", usr.c_str());
    return 0;
}

extern "C" JNIEXPORT void JNICALL
JNI(nativeStart)(JNIEnv *, jclass)
{
    if (cpu_thread_running)
        return;
    pc_reset_hard_init();
    plat_pause(0);

    cpu_thread_run     = 1;
    cpu_thread_running = true;
    emu_thread         = std::thread(emu_thread_fn);

    onesec_run    = true;
    onesec_thread = std::thread([]() {
        plat_set_thread_name(nullptr, "onesec");
        auto next = std::chrono::steady_clock::now();
        while (onesec_run) {
            next += std::chrono::seconds(1);
            std::this_thread::sleep_until(next);
            if (onesec_run)
                pc_onesec();
        }
    });
}

extern "C" JNIEXPORT void JNICALL
JNI(nativeStop)(JNIEnv *, jclass)
{
    onesec_run = false;
    if (onesec_thread.joinable())
        onesec_thread.join();
    cpu_thread_run = 0;
    if (emu_thread.joinable())
        emu_thread.join();
    nvr_save();
    config_save();
    pc_close(nullptr);
}

extern "C" JNIEXPORT void JNICALL
JNI(nativeSetSurface)(JNIEnv *env, jclass, jobject jsurface)
{
    std::lock_guard<std::mutex> lock(surface_mtx);
    if (surface) {
        ANativeWindow_release(surface);
        surface = nullptr;
    }
    if (jsurface) {
        surface   = ANativeWindow_fromSurface(env, jsurface);
        surface_w = surface_h = 0; /* set the geometry on the next frame */
    }
}

/* Background / foreground.  Android may end a background app without notice, so
   a pause also saves what the machine keeps (the disk is written through). */
extern "C" JNIEXPORT void JNICALL
JNI(nativePause)(JNIEnv *, jclass, jboolean paused)
{
    plat_pause(paused ? 1 : 0);
    if (paused) {
        nvr_save();
        config_save();
    }
}

extern "C" JNIEXPORT jboolean JNICALL
JNI(nativeIsPaused)(JNIEnv *, jclass)
{
    return dopause ? JNI_TRUE : JNI_FALSE;
}

/* x, y: 0..1 across the picture; down: finger on the glass. */
extern "C" JNIEXPORT void JNICALL
JNI(nativeTouch)(JNIEnv *, jclass, jfloat x, jfloat y, jboolean down)
{
    mouse_x_abs               = (x < 0.0f) ? 0.0 : ((x > 1.0f) ? 1.0 : x);
    mouse_y_abs               = (y < 0.0f) ? 0.0 : ((y > 1.0f) ? 1.0 : y);
    mouse_tablet_in_proximity = 1;
    if (mouse_input_mode == 0)
        mouse_input_mode = 1; /* the buttons go to the tablet only in absolute mode */
    mouse_set_buttons_ex(down ? 1 : 0);
}

extern "C" JNIEXPORT void JNICALL
JNI(nativePulse)(JNIEnv *, jclass, jint line)
{
    merit_io_pulse(line, 0);
}

extern "C" JNIEXPORT void JNICALL
JNI(nativeHardReset)(JNIEnv *, jclass)
{
    hard_reset_pending = 1;
}

/* For the app bar: the speed (percent of real time), the frame size, whether the
   machine still runs (the guest can power it off), and what it is. */
extern "C" JNIEXPORT jint JNICALL
JNI(nativeSpeed)(JNIEnv *, jclass)
{
    return android_speed_percent;
}

extern "C" JNIEXPORT jintArray JNICALL
JNI(nativeFrameSize)(JNIEnv *env, jclass)
{
    jint       wh[2] = { surface_w, surface_h };
    jintArray  out   = env->NewIntArray(2);
    env->SetIntArrayRegion(out, 0, 2, wh);
    return out;
}

extern "C" JNIEXPORT jboolean JNICALL
JNI(nativeIsRunning)(JNIEnv *, jclass)
{
    return cpu_thread_running ? JNI_TRUE : JNI_FALSE;
}

/* { title, profile name } */
extern "C" JNIEXPORT jobjectArray JNICALL
JNI(nativeCabinetInfo)(JNIEnv *env, jclass)
{
    const int   p       = megatouch_profile();
    const char *title   = megatouch_title();
    const char *profile = ((p >= 0) && (p < MT_PROFILE_COUNT)) ? mt_profiles[p].name : "";
    jobjectArray out    = env->NewObjectArray(2, env->FindClass("java/lang/String"), nullptr);
    env->SetObjectArrayElement(out, 0, env->NewStringUTF(title ? title : ""));
    env->SetObjectArrayElement(out, 1, env->NewStringUTF(profile ? profile : ""));
    return out;
}

/* ---- the Machine Manager (ManagerActivity) -------------------------------- */

static jobjectArray
strings(JNIEnv *env, const std::vector<std::string> &v)
{
    jobjectArray out = env->NewObjectArray((jsize) v.size(), env->FindClass("java/lang/String"), nullptr);
    for (size_t i = 0; i < v.size(); i++)
        env->SetObjectArrayElement(out, (jsize) i, env->NewStringUTF(v[i].c_str()));
    return out;
}

/* { runnable "1"/"0", release, version, profile index, default key, note, evidence }.
   Needs no machine: it only reads the image. */
extern "C" JNIEXPORT jobjectArray JNICALL
JNI(nativeIdentify)(JNIEnv *env, jclass, jstring jpath)
{
    const char          *p  = env->GetStringUTFChars(jpath, nullptr);
    const MtIdentAndroid id = mt_identify_android(p);
    /* Imported keys live in the images' folder's keys/. */
    std::string keys = p;
    keys             = keys.substr(0, keys.find_last_of('/') + 1) + "keys";
    env->ReleaseStringUTFChars(jpath, p);
    return strings(env, { id.runnable() ? "1" : "0", id.release, id.version, std::to_string(id.profile),
                          mt_default_key_android(id, keys), id.note, id.evidence, id.modem ? "1" : "0",
                          id.keyPrefix });
}

/* ---- Key import: which release a dump is for, read from the dump itself ---- */

/* The families a dump could be for, best first; empty when it is no key this build reads. */
extern "C" JNIEXPORT jobjectArray JNICALL
JNI(nativeKeyIdentify)(JNIEnv *env, jclass, jbyteArray jdata)
{
    std::vector<uint8_t> d(env->GetArrayLength(jdata));
    env->GetByteArrayRegion(jdata, 0, (jsize) d.size(), reinterpret_cast<jbyte *>(d.data()));
    const char *cand[8];
    const int   n = mt_key_identify(d.data(), d.size(), cand, 8);
    std::vector<std::string> v(cand, cand + n);
    return strings(env, v);
}

/* The family a file name gives ("MEMERALD_full_..."), or "". */
extern "C" JNIEXPORT jstring JNICALL
JNI(nativeKeyFamilyFromName)(JNIEnv *env, jclass, jstring jname)
{
    const char            *name = env->GetStringUTFChars(jname, nullptr);
    const mt_key_family_t *f    = mt_key_family_from_name(name);
    env->ReleaseStringUTFChars(jname, name);
    return env->NewStringUTF(f ? f->prefix : "");
}

/* { prefix, releases } per family. */
extern "C" JNIEXPORT jobjectArray JNICALL
JNI(nativeKeyFamilies)(JNIEnv *env, jclass)
{
    std::vector<std::string> v;
    for (const mt_key_family_t *f = mt_key_families; f->prefix; f++) {
        v.emplace_back(f->prefix);
        v.emplace_back(f->releases);
    }
    return strings(env, v);
}

/* The name the dump is kept under in keys/, or "" if it is not a dump. */
extern "C" JNIEXPORT jstring JNICALL
JNI(nativeKeyFileName)(JNIEnv *env, jclass, jbyteArray jdata, jstring jprefix)
{
    std::vector<uint8_t> d(env->GetArrayLength(jdata));
    env->GetByteArrayRegion(jdata, 0, (jsize) d.size(), reinterpret_cast<jbyte *>(d.data()));
    const char *prefix = env->GetStringUTFChars(jprefix, nullptr);
    char        name[64];
    const int   ok = mt_key_file_name(d.data(), d.size(), prefix, name, sizeof(name));
    env->ReleaseStringUTFChars(jprefix, prefix);
    return env->NewStringUTF(ok ? name : "");
}

/* { name, description } per profile, in profile order. */
extern "C" JNIEXPORT jobjectArray JNICALL
JNI(nativeProfiles)(JNIEnv *env, jclass)
{
    std::vector<std::string> v;
    for (int i = 0; i < MT_PROFILE_COUNT; i++) {
        v.emplace_back(mt_profiles[i].name);
        v.emplace_back(mt_profiles[i].description ? mt_profiles[i].description : "");
    }
    return strings(env, v);
}

/* { ref, name } per built-in key. */
extern "C" JNIEXPORT jobjectArray JNICALL
JNI(nativeKeys)(JNIEnv *env, jclass)
{
    std::vector<std::string> v;
    for (const mt_builtin_key_t *k = mt_builtin_keys; k->id; k++) {
        v.emplace_back(std::string(MT_BUILTIN_PREFIX) + k->id);
        v.emplace_back(k->name);
    }
    return strings(env, v);
}

/* Make this image the cabinet's pick (the Qt MachineManager::applySelection).  After
   nativeInit; if the machine already runs, it is rebuilt on a hard reset, as the
   desktop's Machine Manager does. */
/* A MAXX image the phone has not set up yet (its card on SLiRP, the core's default,
   which this build has not got) gets its card fitted on the local switch with the
   link name "megatouch": Mega-Link with every cabinet on the same Wi-Fi out of the
   box.  Whatever the Mega-Link dialog set since -- the card out, too -- stays. */
static void
network_default(void)
{
    const char *card = megatouch_network_card();
    if (!card)
        return;
    netcard_conf_t nc;
    if (megatouch_network_saved(0, &nc) && (nc.net_type != NET_TYPE_SLIRP))
        return;
    memset(&nc, 0, sizeof(nc));
    nc.device_num = network_card_get_from_internal_name((char *) card);
    nc.net_type   = NET_TYPE_NLSWITCH;
    snprintf(nc.secret, sizeof(nc.secret), "%s", "megatouch");
    memset(net_cards_conf, 0, sizeof(net_cards_conf));
    net_cards_conf[0] = nc;
    megatouch_set_image_option(megatouch_image(), MT_OPT_NETWORK, 1);
    megatouch_network_to_image();
}

extern "C" JNIEXPORT void JNICALL
JNI(nativeSelect)(JNIEnv *env, jclass, jint profile, jstring jimage, jstring jkey, jstring jtitle, jboolean modem)
{
    const char *image = env->GetStringUTFChars(jimage, nullptr);
    const char *key   = env->GetStringUTFChars(jkey, nullptr);
    const char *title = env->GetStringUTFChars(jtitle, nullptr);

    const bool running = cpu_thread_running;
    const int  was     = dopause;
    if (running)
        plat_pause(1);
    megatouch_select(profile, image, key, MT_BOARD_DEFAULT, title);
    /* The image's own modem choice, else its release's (fitted from Diamond on), kept
       with it so that is what the machine is built with -- as the desktop's Run. */
    if (megatouch_image_option_saved(image, MT_OPT_MODEM) < 0)
        megatouch_set_image_option(image, MT_OPT_MODEM, modem ? 1 : 0);
    network_default();
    megatouch_apply_profile();
    config_save();
    if (running) {
        config_changed = 2;
        pc_reset_hard();
        plat_pause(was);
    }

    env->ReleaseStringUTFChars(jimage, image);
    env->ReleaseStringUTFChars(jkey, key);
    env->ReleaseStringUTFChars(jtitle, title);
}

/* ---- Mega-Link: the cabinet's network card (the desktop's Network dialog) -- */

/* { has a card "1"/"0" (a MAXX profile), fitted "1"/"0", type ("lswitch", "rswitch",
   ...), remote switch host, secret, card name }, from the running image's settings. */
extern "C" JNIEXPORT jobjectArray JNICALL
JNI(nativeNetworkGet)(JNIEnv *env, jclass)
{
    const char    *card = megatouch_network_card();
    netcard_conf_t nc;
    const int      saved = megatouch_network_saved(0, &nc);
    const char    *type  = "lswitch";
    if (saved && (nc.net_type == NET_TYPE_NRSWITCH))
        type = "rswitch";
    const char *dev = (saved && nc.device_num) ? network_card_get_internal_name(nc.device_num) : card;
    return strings(env, { card ? "1" : "0",
                          (card && megatouch_image_option(megatouch_image(), MT_OPT_NETWORK)) ? "1" : "0",
                          type, saved ? nc.nrs_hostname : "", saved ? nc.secret : "", dev ? dev : "" });
}

/* Fit (or take out) the card, on the local switch (the same network: multicast, as
   the desktop's Local Switch) or a remote one (one cabinet, host[:port]), and keep it
   with the image.  The cabinet restarts on it, as a real one would be switched off to
   fit a card.  SLiRP and PCap are not in the Android build. */
extern "C" JNIEXPORT void JNICALL
JNI(nativeNetworkSet)(JNIEnv *env, jclass, jboolean on, jboolean remote, jstring jhost, jstring jsecret)
{
    const char *card = megatouch_network_card();
    if (!card)
        return;
    const char *host   = env->GetStringUTFChars(jhost, nullptr);
    const char *secret = env->GetStringUTFChars(jsecret, nullptr);

    netcard_conf_t nc;
    if (!megatouch_network_saved(0, &nc) || !nc.device_num) {
        memset(&nc, 0, sizeof(nc));
        nc.device_num = network_card_get_from_internal_name((char *) card);
    }
    nc.net_type = remote ? NET_TYPE_NRSWITCH : NET_TYPE_NLSWITCH;
    snprintf(nc.nrs_hostname, sizeof(nc.nrs_hostname), "%s", remote ? host : "");
    snprintf(nc.secret, sizeof(nc.secret), "%s", secret);
    nc.host_dev_name[0] = '\0';

    const bool running = cpu_thread_running;
    const int  was     = dopause;
    if (running)
        plat_pause(1);
    megatouch_set_image_option(megatouch_image(), MT_OPT_NETWORK, on ? 1 : 0);
    if (on) {
        memset(net_cards_conf, 0, sizeof(net_cards_conf));
        net_cards_conf[0] = nc;
        megatouch_network_to_image();
    }
    config_save();
    if (running) {
        config_changed = 2;
        pc_reset_hard();
        plat_pause(was);
    }

    env->ReleaseStringUTFChars(jhost, host);
    env->ReleaseStringUTFChars(jsecret, secret);
}

/* The modem on COM2 (the ActionTec of the TournaMAXX / MegaNET kits): fitted per image,
   as the desktop's Tools > Modem on COM2; the line behind it is the same for every image
   (the desktop's Modem settings: the device's own section, instance 2 on COM2), and a dial
   opens a TCP connection to that host -- a TournaMAXX-Revival server, say.
   Returns {fitted, host, port, sounds}. */
static const char *modem_section = "ActionTec 56K PC Card (FM560LK) #2";

extern "C" JNIEXPORT jobjectArray JNICALL
JNI(nativeModemGet)(JNIEnv *env, jclass)
{
    const char *img  = megatouch_image();
    const char *host = config_get_string((char *) modem_section, (char *) "host", (char *) "");
    const bool  tcp  = config_get_int((char *) modem_section, (char *) "line", 0) == 1;
    const std::string port = std::to_string(config_get_int((char *) modem_section, (char *) "host_port", 23));
    return strings(env, { (img && img[0] && megatouch_image_option(img, MT_OPT_MODEM)) ? "1" : "0",
                          (tcp && host) ? host : "", port.c_str(),
                          megatouch_modem_sounds() ? "1" : "0" });
}

/* Plug the modem in (or out) and set its line: an empty host is a dead line.  All at
   once with the cabinet running, as the desktop: 86Box's hot-plug of a COM device, and
   the fitted modem takes the new host (a call in progress keeps the host it reached, and
   drops if the line is now dead).  Returns whether the modem went in or out -- a release
   that looked for it at boot only notices after a restart, which the caller offers. */
extern "C" JNIEXPORT jboolean JNICALL
JNI(nativeModemSet)(JNIEnv *env, jclass, jboolean on, jstring jhost, jint port, jboolean sounds)
{
    const char *img = megatouch_image();
    if (!img || !img[0])
        return JNI_FALSE;
    const char *host = env->GetStringUTFChars(jhost, nullptr);

    megatouch_set_modem_sounds(sounds ? 1 : 0);
    const bool plug = (megatouch_image_option(img, MT_OPT_MODEM) != 0) != (bool) on;
    megatouch_set_image_option(img, MT_OPT_MODEM, on ? 1 : 0);
    config_set_int((char *) modem_section, (char *) "line", host[0] ? 1 : 0);
    config_set_string((char *) modem_section, (char *) "host", (char *) host);
    config_set_int((char *) modem_section, (char *) "host_port", port);
    config_save();

    if (cpu_thread_running) {
        startblit();
        if (plug || (on && !megatouch_modem_plugged()))
            megatouch_modem_plug(on ? 1 : 0);
        else if (on)
            modem_megatouch_reconfigure();
        endblit();
    }

    env->ReleaseStringUTFChars(jhost, host);
    return plug ? JNI_TRUE : JNI_FALSE;
}
