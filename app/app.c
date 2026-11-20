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

/* ---- the events mirror: the state a viewer starts from (the settings, the user sounds, where the UI is); the rest
 * comes as events. The mirror's signature of it tells a viewer that drifted to ask again */
typedef struct {
    uint32_t magic;
    settings_t set;
    bank_t bank[2];
    int16_t page, card, list, list_sel, list_top, list_n;
    int16_t list_k[LIST_MAX];
    uint8_t last[NB], voice, dirty, saving, save_pick;
} mirror_state_t;
#define MIRROR_MAGIC (0x4D530000u + (uint32_t)sizeof(mirror_state_t))   /* (another build: refused) */
static uint32_t state_fill(mirror_state_t *m)
{
    memset(m, 0, sizeof *m);
    m->magic = MIRROR_MAGIC;
    m->set = set;
    m->bank[0] = bank[0];
    m->bank[1] = bank[1];
    m->page = (int16_t)ui.page;
    m->card = (int16_t)ui.card;
    m->list = (int16_t)ui.list;
    m->list_sel = (int16_t)ui.list_sel;
    m->list_top = (int16_t)ui.list_top;
    m->list_n = (int16_t)(ui.list ? ui.list_n : 0);
    for (int i = 0; i < m->list_n && i < LIST_MAX; i++)
        m->list_k[i] = ui.list_k[i];
    memcpy(m->last, ui.last, sizeof m->last);
    m->voice = ui.voice;
    m->dirty = ui.dirty;
    m->saving = ui.saving;
    m->save_pick = ui.saving ? ui.save_pick : 0;
    return sizeof *m;
}
uint32_t app_state_get(uint8_t *buf, uint32_t max)
{
    if (max < sizeof(mirror_state_t))
        return 0;
    return state_fill((mirror_state_t *)(void *)buf);   /* (buf: 4-byte aligned) */
}
void app_state_set(const uint8_t *buf, uint32_t n)
{
    const mirror_state_t *m = (const mirror_state_t *)(const void *)buf;
    if (n != sizeof *m || m->magic != MIRROR_MAGIC)
        return;
    set = m->set;
    bank[0] = m->bank[0];
    bank[1] = m->bank[1];
    ui.page = m->page;
    ui.card = m->card;
    ui.list = m->list;
    ui.list_sel = m->list_sel;
    ui.list_top = m->list_top;
    ui.list_n = m->list_n;
    memcpy(ui.list_k, m->list_k, sizeof ui.list_k);
    memcpy(ui.last, m->last, sizeof ui.last);
    ui.voice = m->voice;
    ui.dirty = m->dirty;
    ui.saving = m->saving;
    ui.save_pick = m->save_pick;
    while (chd_patch(&set.snd))
        plat_ms();
    memset(ui.sig, 0, sizeof ui.sig);
}

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
    else if (!strcmp(what, "saving"))
        *got = ui.saving;
    else if (!strcmp(what, "card"))
        *got = ui.card;
    else if (!strcmp(what, "arpsteps"))
        *got = (int)chd_arp_steps;
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
