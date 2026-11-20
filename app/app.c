/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Chordism on fm1-base: the app's unity build (compiled into fm1-base's, after the platform: gfx.c and announce.c are in
 * scope) and the glue between fm1-base's contract (app.h) and Chordism's parts: the drawing (gfx_loom.c), the pages
 * (pages.c), the UI (ui.c) and the engine (dsp/chd.c, its own -O2 unit). */
#include "fastmath.h"
#include "gfx_loom.c"
#include "pages.c"
#include "ui.c"

/* ---- the contract */
void app_init(void)
{
    PLAT_FAR(chd_init)();             /* (in RAM with the engine) */
    chd_cores2 = plat_cpu2_set(1) == 0;  /* the second core: half the voices */
    settings_load();
    banks_load();
    chd_patch(&set.snd);                 /* (taken by the first block) */
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {  /* MIDI in: notes, pressure, CCs, the clock (USB, Bluetooth, TRS); a channel's
                                          * messages only on that channel (Setup's MIDI page), the clock always */
        uint32_t st = (pkt >> 8) & 0xFFu;
        if (st < 0xF0u && chd_midi_in_ch() && (st & 15u) != (uint32_t)(chd_midi_in_ch() - 1))
            continue;
        PLAT_FAR(chd_midi)(st, (pkt >> 16) & 0x7Fu, pkt >> 24);
    }
    PLAT_FAR(chd_render)(lr, n, master_q12);   /* (the engine runs from RAM: a pointer reaches it) */
}

void app_panic(void) { chd_panic(); }

void app_ui_init(void)
{
    touch_on(1);                         /* velocity and pressure: the keys' readings on */
    lcd_fill(0, 0, 240, 240, K_BG);
    ui.page = P_CHORD;
    ui.last_note = -1;
    for (int b = 0; b < NB; b++) {
        int first;
        page_n(b, &first);
        ui.last[b] = (uint8_t)(first < 0 ? 0 : first);
    }
}

void app_input_only(void) { input(); }

void app_frame(void)
{
    input();
    if (FM1B_BLE_CENTRAL) {              /* looking for devices: a device found is named at once */
        static int found;
        int n = ui.looking ? btdev_count() : 0, ev;
        btdev_page(ui.looking && !set.ble_off);
        if (n > found && btdev_pick() < n)
            ble_device_say();
        found = n;
        while ((ev = btdev_event()) != BTDEV_NONE)
            if (ev == BTDEV_CONNECTED || ev == BTDEV_NO_ANSWER || ev == BTDEV_LOST)
                app_say("DEVICE", btdev_word(ev));
    }
    speak();
    ann_tick(plat_ms());
    draw();
}

void app_redraw(void) { memset(ui.sig, 0, sizeof ui.sig); }   /* (each band's hash forgotten: all drawn again) */

/* ---- the companion: the settings and the user sounds as files */
int app_file_busy(void) { return 0; }
void app_file_changed(uint32_t obj)
{
    if (obj == OBJ_SETTINGS) {
        settings_load();
        while (chd_patch(&set.snd))
            plat_ms();
        ui.dirty = 0;
    } else if (obj == OBJ_USER0 || obj == OBJ_USER1)
        banks_load();
    memset(ui.sig, 0, sizeof ui.sig);
}

#ifdef PLAT_HOST
#ifdef GFX_AUDIT
static const char *gfx_audit_where(void) { return PAGE[ui.page].name; }
#endif
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "sound"))
        *got = set.sound;
    else if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "dirty"))
        *got = ui.dirty;
    else if (!strcmp(what, "edited"))
        *got = set.edited;
    else if (!strcmp(what, "list"))
        *got = ui.list;
    else if (!strcmp(what, "card"))
        *got = ui.card;
    else if (!strcmp(what, "held"))
        *got = chd_held;
    else if (!strncmp(what, "note", 4))      /* noteN: chord voice N's note (-1 none) */
        *got = chd_sounding[atoi(what + 4) & 3];
    else if (!strncmp(what, "level", 5))
        *got = chd_level[atoi(what + 5) & 3];
    else if (!strncmp(what, "par", 3)) {     /* parKEY: a parameter by the original's key, as the sound holds it */
        int i;
        for (i = 0; i < CP_N && strcmp(CHD_PARAM[i].key, what + 3); i++)
            ;
        if (i == CP_N)
            return 0;
        *got = set.snd.p[i];
    } else if (!strncmp(what, "user", 4))
        *got = user_used(atoi(what + 4));
    else
        return 0;
    return 1;
}
#endif
