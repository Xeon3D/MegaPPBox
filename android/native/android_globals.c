/*
 * MegaPPBox - Merit Megatouch cabinets on 86Box.
 *
 *          Android: the globals and small functions the core expects from its
 *          front end, which the desktop builds get from src/qt.  Most are
 *          window, toolbar and capture settings with no meaning in the native
 *          app; cdrom_mount is the Qt media menu's, without Qt.
 *
 * Authors: MegaPPBox contributors
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/cdrom.h>
#include <86box/config.h>
#include <86box/plat.h>
#include <86box/plat_unused.h>
#include <86box/ui.h>

volatile int cpu_thread_run  = 1;
bool         fast_forward    = false;
int          mouse_capture   = 0;
int          kbd_req_capture = 0;
int          rctrl_is_lalt   = 0;
int          update_icons    = 0;
int          hide_status_bar = 1;
int          hide_tool_bar   = 1;
int          fixed_size_x    = 640;
int          fixed_size_y    = 480;

/* The desktop renderers' user shader lists; config.c keeps them. */
char gl3_shader_file[20][512];
char vk_shader_file[20][512];

/* One renderer: the app's surface (android_main.cpp). */
int
plat_vidapi(UNUSED(const char *name))
{
    return 0;
}

void
plat_cdrom_ui_update(UNUSED(uint8_t id), UNUSED(uint8_t reload))
{
}

void
do_stop(void)
{
    cpu_thread_run = 0;
}

/* src/qt/qt_mediamenu.cpp's MediaMenu::cdromMount, for the core's own calls. */
void
cdrom_mount(uint8_t id, char *fn)
{
    const int was_empty = cdrom_is_empty(id);

    cdrom_exit(id);
    memset(cdrom[id].image_path, 0, sizeof(cdrom[id].image_path));
    if (fn && (strlen(fn) >= 1) && (fn[strlen(fn) - 1] == '\\'))
        fn[strlen(fn) - 1] = '/';
    cdrom_load(&(cdrom[id]), fn, 1);

    /* Signal the media change to the emulated machine. */
    if (cdrom[id].insert) {
        cdrom[id].insert(cdrom[id].priv);
        /* The drive was empty: go straight to UNIT ATTENTION. */
        if (was_empty)
            cdrom[id].insert(cdrom[id].priv);
    }
    config_save();
}
