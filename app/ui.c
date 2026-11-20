/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Chordism's UI: the settings and the user sounds (saved), the input, and the screen in LOOM-8's bands (a header,
 * the picture, the tag, the four knobs, the hints), coloured as an aurora: a night sky, its light green, teal, violet
 * and pink (a voice each). Part of the unity build (app.c), after pages.c. */

/* ----------------------------------------------------------- the look --- */
#define K_BG RGB(6, 10, 26)
#define K_SKY RGB(16, 24, 54)                         /* the sky's foot (the picture's gradient runs from K_BG) */
#define K_PANEL RGB(20, 30, 60)
#define K_PANEL2 RGB(34, 48, 88)
#define K_LINE RGB(44, 60, 104)
#define K_FAINT RGB(76, 96, 146)
#define K_DIM RGB(146, 164, 206)
#define K_TEXT RGB(232, 242, 255)
#define K_REC RGB(255, 84, 132)
static const uint16_t INK[4] = {RGB(80, 255, 170), RGB(60, 206, 236), RGB(172, 122, 255), RGB(255, 118, 200)};
#define K_NOW INK[0]                                  /* the bars, the picture's light */

/* the bands (LOOM-8's) */
#define HDR_H 24
#define MAIN_Y 24
#define MAIN_H 116                       /* (the header the sound; the page's title over the knobs) */
#define TTL_Y 140
#define TTL_H 18
#define KNB_Y 158
#define KNB_H 50
#define FOOT_Y 208

/* ------------------------------------------------------ settings, sounds --- */
#define SET_MAGIC 0x31444843u                         /* "CHD1" */
#define SET_FORMAT (0x100u + (uint32_t)CP_N)          /* (another parameter list: the defaults; prerelease, no migrations) */
enum { SND_USER = CHD_NPRESET, NSOUNDS = CHD_NPRESET + NUSER };
typedef struct {
    uint32_t magic, format;
    chd_patch_t snd;                     /* the sound playing, as it is now */
    int16_t sound;                       /* where it came from: 0.. factory, SND_USER + n a user slot */
    int8_t octave;
    uint8_t lights, velo, press, midiout, ble_off, map_pc, edited;
    uint8_t midi_in_ch;                  /* 0 any, 1..16 */
    uint8_t midi_out_ch;                 /* 0..15 */
    uint8_t rsv[6];
} settings_t;
typedef struct {
    uint32_t magic, format;
    uint8_t used[8];
    chd_patch_t s[8];
} bank_t;
static settings_t set;
static bank_t bank[2];

static void settings_defaults(void)
{
    memset(&set, 0, sizeof set);
    set.magic = SET_MAGIC;
    set.format = SET_FORMAT;
    set.snd = CHD_PRESET[1];             /* Lush Pad: a first sound to hear */
    set.sound = 1;
    set.velo = set.press = set.midiout = 1;
}
static void settings_load(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC ||
        set.format != SET_FORMAT || set.octave < -3 || set.octave > 3 || set.sound < 0 || set.sound >= NSOUNDS)
        settings_defaults();
    set.snd.name[sizeof set.snd.name - 1] = 0;
    set.lights %= 3u;
    set.midi_in_ch %= 17u;
    set.midi_out_ch %= 16u;
    set.map_pc %= 12u;
    for (int i = 0; i < CP_N; i++)
        set.snd.p[i] = (int16_t)clamp(set.snd.p[i], CHD_PARAM[i].lo, CHD_PARAM[i].hi);
}
static void banks_load(void)
{
    for (int b = 0; b < 2; b++)
        if (plat_store_load(OBJ_USER0 + (uint32_t)b, &bank[b], sizeof bank[b]) != (int)sizeof bank[b] ||
            bank[b].magic != SET_MAGIC || bank[b].format != SET_FORMAT) {
            memset(&bank[b], 0, sizeof bank[b]);
            bank[b].magic = SET_MAGIC;
            bank[b].format = SET_FORMAT;
        }
}
static int user_used(int n) { return bank[n / 8].used[n % 8]; }
static const chd_patch_t *sound_patch(int s)
{
    return s < SND_USER ? &CHD_PRESET[s] : &bank[(s - SND_USER) / 8].s[(s - SND_USER) % 8];
}
int chd_ble_off(void) { return set.ble_off; }
int chd_midi_in_ch(void) { return set.midi_in_ch; }

/* ---------------------------------------------------------------- the UI --- */
#define LIST_MAX 80
static struct {
    int page;
    uint8_t last[NB];                    /* each button's page last shown */
    uint8_t dirty;                       /* changed since the last save: SAVE lit */
    uint32_t keys, btn;
    uint8_t key_note[NKEYS];             /* the note each held key sounds (the octave may change meanwhile) */
    uint8_t prs[NKEYS];
    uint32_t btn_t[NB];                  /* when each button went down (0: up, or its press already used) */
    int card;                            /* a button's help shown: its B_* + 1 */
    int list, list_sel, list_top, list_n;
    int16_t list_k[LIST_MAX];
    char msg[2][32];
    uint32_t msg_until;
    int touched;                         /* the knob last turned (lit a moment) */
    uint8_t voice;                       /* Oscillators: the voice its knobs set, 1..4; 0 all four */
    int last_note;                       /* the key last pressed's note (-1 none): the map's chord in the tag */
    uint32_t touch_until;
    uint32_t sig[6];                     /* each band's last drawn content (a hash): drawn again only when it changed */
    uint32_t anim;                       /* the picture's last frame */
    uint8_t save_used;                   /* SAVE's press already did something (no settings save on its release) */
    uint8_t saving, save_pick;           /* the slots open (SAVE held a moment): the slot picked, 0..15 */
    uint32_t note_t;                     /* when a key last started a chord (the delay's picture) */
    uint8_t looking;                     /* REC: the Bluetooth devices' scan */
} ui;

/* bounded text (the device has no printf): s_put(d, n, a, b, c, e): those parts after d's own text, cut at n */
static void s_put(char *d, uint32_t n, const char *a, const char *b, const char *c, const char *e)
{
    const char *part[4] = {a, b, c, e};
    uint32_t i = 0;
    while (i < n - 1u && d[i])
        i++;
    for (int k = 0; k < 4; k++)
        for (const char *q = part[k]; q && *q && i < n - 1u; q++)
            d[i++] = *q;
    d[i] = 0;
}
static uint32_t strlen_(const char *a)
{
    uint32_t n = 0;
    while (a[n])
        n++;
    return n;
}
static int s_eq(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static uint32_t pic_us, pic_every;         /* the picture's drawing (us, smoothed) and how often it is drawn (ms) */
static uint32_t hash_u(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static uint32_t hash_s(uint32_t h, const char *s)
{
    while (*s)
        h = hash_u(h, (uint8_t)*s++);
    return h;
}

void app_say(const char *a, const char *b)
{
    char t[72];
    ui.msg[0][0] = ui.msg[1][0] = 0;
    s_put(ui.msg[0], sizeof ui.msg[0], a, 0, 0, 0);
    s_put(ui.msg[1], sizeof ui.msg[1], b, 0, 0, 0);
    ui.msg_until = plat_ms() + 1300u;
    if (ann_on()) {
        t[0] = 0;
        s_put(t, sizeof t, ui.msg[0], " ", ui.msg[1], 0);
        ann_say(t, 0, ANN_NOW);
    }
}

static int par_of(int k)                  /* a knob's parameter (K_MAPCHORD: the picked note's chord; K_V*: the
                                           * picked voice's, voice 1's for all four, FM's amount for all) */
{
    int v = ui.voice ? ui.voice - 1 : 0;
    switch (k) {
    case K_MAPCHORD: return CP_PC0 + set.map_pc;
    case K_VWAVE: return CP_WAVE1 + v;
    case K_VSHAPE: return CP_SHAPE1 + v;
    case K_VMIX: return CP_MIX1 + v;
    case K_VFM: return ui.voice ? CP_FM1 + v : CP_FM_AMT;
    default: return k;
    }
}
static int shape_wave(int k)               /* the wave a shape knob's voice plays (pages.c value_text) */
{
    return set.snd.p[CP_WAVE1 + (par_of(k) - CP_SHAPE1)];
}
static int knob_get(int k)
{
    switch (k) {
    case K_NONE: return 0;
    case K_LIGHTS: return set.lights;
    case K_VELO: return set.velo;
    case K_PRESS: return set.press;
    case K_MIDIOUT: return set.midiout;
    case K_MIDIIN_CH: return set.midi_in_ch;
    case K_MIDIOUT_CH: return set.midi_out_ch;
    default: return set.snd.p[par_of(k)];
    }
}
static void knob_range(int k, int *lo, int *hi)
{
    if (k >= K_MAPCHORD && k <= K_VFM)
        k = par_of(k);
    if (k >= K_LIGHTS) {
        *lo = 0;
        *hi = k == K_LIGHTS ? 2 : k == K_MAPCHORD ? CHD_NCHORD - 1 : k == K_MIDIIN_CH ? 16 : k == K_MIDIOUT_CH ? 15 : 1;
        return;
    }
    *lo = CHD_PARAM[k].lo;
    *hi = CHD_PARAM[k].hi;
}
static void mark(void)
{
    ui.dirty = 1;
}
static void par_set(int p, int v)         /* an engine parameter: the sound playing, and the engine */
{
    v = clamp(v, CHD_PARAM[p].lo, CHD_PARAM[p].hi);
    if (set.snd.p[p] == v)
        return;
    set.snd.p[p] = (int16_t)v;
    set.edited = 1;
    chd_set(p, v);
    mark();
}
static void knob_set(int k, int v)
{
    int lo, hi;
    if (k == K_NONE)
        return;
    knob_range(k, &lo, &hi);
    v = clamp(v, lo, hi);
    switch (k) {
    case K_LIGHTS: set.lights = (uint8_t)v; mark(); return;
    case K_VELO: set.velo = (uint8_t)v; mark(); return;
    case K_PRESS: set.press = (uint8_t)v; mark(); return;
    case K_MIDIOUT: set.midiout = (uint8_t)v; mark(); return;
    case K_MIDIIN_CH: set.midi_in_ch = (uint8_t)v; mark(); return;
    case K_MIDIOUT_CH: set.midi_out_ch = (uint8_t)v; mark(); return;
    case K_VWAVE: case K_VSHAPE: case K_VMIX:
        if (!ui.voice) {                   /* all four voices at once */
            for (int i = 0; i < 4; i++)
                par_set(par_of(k) + i, v);
            return;
        }
        par_set(par_of(k), v);
        return;
    default: par_set(par_of(k), v); return;
    }
}
static void say_knob(int k)
{
    char v[24], t[48];
    value_text(par_of(k), knob_get(k), v);
    if (ann_on()) {
        t[0] = 0;
        s_put(t, sizeof t, knob_name(k), " ", v, 0);
        ann_say(t, 0, ANN_NOW);
    }
}

/* a knob's turn: e detents. A value 0..1 moves 1 % a detent (with HOME held, 0.1 %), a fast turn more */
static void knob_turn(int k, int32_t e)
{
    int lo, hi, v = knob_get(k), step;
    if (k == K_NONE || !e)
        return;
    knob_range(k, &lo, &hi);
    if (par_of(k) == CP_ROTATION) {        /* (four places: a detent each) */
        static const int16_t ROTV[4] = {0, 2501, 5002, 7502};
        int r = clamp((int)((float)v * 0.0003999f) + (e > 0 ? 1 : -1), 0, 3);
        knob_set(k, ROTV[r]);
        say_knob(k);
        return;
    }
    if (hi - lo >= 10000)
        step = (ui.btn >> B_HOME & 1u) ? 10 : (e > 2 || e < -2) ? 300 : 100;
    else
        step = 1;
    v = clamp(v + (int)e * step, lo, hi);
    knob_set(k, v);
    say_knob(k);
}

static void sound_load(int s)
{
    const chd_patch_t *p = sound_patch(s);
    set.snd = *p;
    set.sound = (int16_t)s;
    set.edited = 0;
    while (chd_patch(&set.snd))            /* (the engine takes one whole sound a block) */
        plat_ms();
    mark();
}
static void sound_name(int s, char *t)    /* "12 Lush Pad", "U3 Lush Pad" */
{
    *t = 0;
    if (s < SND_USER)
        num_i(s + 1, t);
    else
        num_i(s - SND_USER + 1, s_cat(t, "U"));
    s_cat(s_cat(t, " "), sound_patch(s)->name);
}
static void sound_step(int d)
{
    int s = set.sound;
    for (int n = 0; n < NSOUNDS; n++) {
        s = (s + d + NSOUNDS) % NSOUNDS;
        if (s < SND_USER || user_used(s - SND_USER))
            break;
    }
    if (s == set.sound && !set.edited)
        return;
    sound_load(s);
    {
        char t[32];
        sound_name(s, t);
        app_say("SOUND", t);
    }
}

static int settings_save(void)
{
    if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
        ui.dirty = 0;
        app_say("SAVED", "");
        return 0;
    }
    app_say("SAVE", "FAILED");
    return -1;
}
static void user_save(int n)              /* the sound playing into user slot n */
{
    bank_t *b = &bank[n / 8];
    char t[24];
    b->s[n % 8] = set.snd;
    b->used[n % 8] = 1;
    if (plat_store_save(OBJ_USER0 + (uint32_t)(n / 8), b, sizeof *b) != 0) {
        app_say("SAVE", "FAILED");
        return;
    }
    set.sound = (int16_t)(SND_USER + n);
    set.edited = 0;
    settings_save();
    num_i(n + 1, (t[0] = 'U', t + 1));
    s_put(t, sizeof t, " ", set.snd.name, 0, 0);
    app_say("SAVED TO", t);
}

/* ------------------------------------------------------------ pages, list --- */
static int page_n(int btn, int *first)
{
    int n = 0;
    *first = -1;
    for (int p = 0; p < NPAGES; p++)
        if (PAGE[p].btn == btn) {
            if (*first < 0)
                *first = p;
            n++;
        }
    return n;
}
static void page_go(int p)
{
    char t[96], v[24];
    ui.page = p;
    ui.last[PAGE[p].btn] = (uint8_t)p;
    if (ann_on()) {                        /* the page and its knobs, for a screen reader */
        t[0] = 0;
        s_put(t, sizeof t, PAGE[p].name, ".", 0, 0);
        for (int i = 0; i < 4; i++)
            if (PAGE[p].k[i] != K_NONE) {
                int k = PAGE[p].k[i];
                value_text(par_of(k), knob_get(k), v);
                const char *kn = knob_name(k);
                if (kn[0] == (PAGE[p].name[0] | 0x20) && s_eq(kn + 1, PAGE[p].name + 1))
                    kn = "";                       /* (its name is the page's: said once) */
                s_put(t, sizeof t, " ", kn, *kn ? " " : "", v);
                s_put(t, sizeof t, ".", 0, 0, 0);
            }
        ann_say(t, 0, ANN_NOW);
    }
}
/* a screen reader that has just started listening (or asks where it is) hears the page: its name, the sound, the
 * knobs; on Setup the audio's load too */
static void speak(void)
{
    if (ann_on() && ann_where()) {
        char t[48];
        page_go(ui.page);
        sound_name(set.sound, t);
        ann_say(t, 0, 0);
        if (ui.page == P_SETUP) {
            t[0] = 0;
            num_i((int32_t)plat_cpu_pct(), s_cat(t, "cpu "));
            s_cat(t, " percent");
            ann_say(t, 0, 0);
            t[0] = 0;
            s_cat(num_i((int32_t)(1000u / (pic_every ? pic_every : 1u)), s_cat(num_i((int32_t)(pic_us / 1000u), s_cat(t, "picture ")), " ms, ")), " frames a second");
            ann_say(t, 0, 0);
            {
                extern uint32_t chd_guarded;
                t[0] = 0;
                num_i((int32_t)chd_guarded, s_cat(num_i((int32_t)plat_xruns(), s_cat(t, "xruns ")), " guard "));
                ann_say(t, 0, 0);
            }
#ifdef CHD_PROF
            {   /* each pass's share of the time a sample has (22.7 us), percent */
                extern volatile uint32_t chd_prof[5];
                uint32_t ns = chd_prof[4] ? chd_prof[4] : 1;
                t[0] = 0;
                for (int k = 0; k < 4; k++) {
                    num_i((int32_t)(chd_prof[k] * 100u / 24u / ns * 100u / 2268u), s_cat(t, k ? " " : "pass "));
                    chd_prof[k] = 0;
                }
                chd_prof[4] = 0;
                ann_say(t, 0, 0);
            }
#endif
        }
    }
}
static void page_button(int b)
{
    int first, n = page_n(b, &first);
    if (!n)
        return;
    if (ui.list)
        ui.list = 0;
    if (PAGE[ui.page].btn == b)            /* again: its next page */
        page_go(ui.page + 1 < first + n ? ui.page + 1 : first);
    else
        page_go(PAGE[ui.last[b]].btn == b ? ui.last[b] : first);
}
static void page_turn(int d)
{
    int first, n = page_n(PAGE[ui.page].btn, &first);
    page_go(first + (ui.page - first + d + n) % n);
}

#define LIST_HEAD(p) (-2 - (p))              /* a section's name in the list: page p's (NPAGES: "more") */
#define IS_HEAD(k) ((k) <= -2)
static int list_step(int at, int d)        /* the next row from at that is a setting, d's way (at itself if it is one) */
{
    int i = at;
    while (i >= 0 && i < ui.list_n && IS_HEAD(ui.list_k[i]))
        i += d;
    if (i < 0 || i >= ui.list_n)           /* (none that way: the other) */
        for (i = at; i >= 0 && i < ui.list_n && IS_HEAD(ui.list_k[i]); i -= d)
            ;
    return i < 0 ? 0 : i >= ui.list_n ? ui.list_n - 1 : i;
}
static void list_build(void)
{
    int b = PAGE[ui.page].btn, first, n = page_n(b, &first);
    const int16_t *ex = b == B_EDIT ? EXTRA_EDIT : b == B_ENV ? EXTRA_ENV : b == B_LFO ? EXTRA_LFO : b == B_FX ? EXTRA_FX
                      : b == B_GLO ? EXTRA_GLO : b == B_SEQ ? EXTRA_SEQ : 0;
    ui.list_n = 0;
    for (int p = first; p < first + n; p++) {   /* each page's knobs under its name (LIST_HEAD(p)) */
        int head = ui.list_n;
        if (ui.list_n < LIST_MAX)
            ui.list_k[ui.list_n++] = (int16_t)LIST_HEAD(p);
        for (int i = 0; i < 4; i++) {
            int k = PAGE[p].k[i], dup = 0;
            if (k == K_NONE || k == K_MAPCHORD)
                continue;
            for (int j = 0; j < ui.list_n; j++)
                dup |= ui.list_k[j] == k;
            if (!dup && ui.list_n < LIST_MAX)
                ui.list_k[ui.list_n++] = (int16_t)k;
        }
        if (ui.list_n == head + 1)
            ui.list_n = head;                /* (nothing new under it: no name) */
    }
    if (ex && *ex != K_NONE && ui.list_n < LIST_MAX)
        ui.list_k[ui.list_n++] = (int16_t)LIST_HEAD(NPAGES);   /* then the settings no page shows: "more" */
    for (; ex && *ex != K_NONE && ui.list_n < LIST_MAX; ex++)
        ui.list_k[ui.list_n++] = *ex;
    if (ui.list_sel >= ui.list_n)
        ui.list_sel = ui.list_n - 1;
    if (ui.list_sel < 0 || IS_HEAD(ui.list_k[ui.list_sel]))
        ui.list_sel = list_step(ui.list_sel < 0 ? 0 : ui.list_sel, 1);
}

/* SEL on HOME: every sound by name (the factory's, then the slots kept), the one playing picked */
static void sounds_build(void)
{
    ui.list_n = 0;
    for (int s = 0; s < NSOUNDS && ui.list_n < LIST_MAX; s++)
        if (s < SND_USER || user_used(s - SND_USER)) {
            if (s == set.sound)
                ui.list_sel = ui.list_n;
            ui.list_k[ui.list_n++] = (int16_t)s;
        }
}

/* ------------------------------------------------------------------ input --- */
static int key_note(int k) { return 53 + k + 12 * set.octave; }
static void note_out(uint32_t st, int note, int v)
{
    if (set.midiout)
        plat_midi_send(st >> 4 | (st | set.midi_out_ch) << 8 | (uint32_t)note << 16 | (uint32_t)v << 24, TO_USB | TO_BLE);
}
static int white_index(int k)
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS;
    for (int i = 0; i < 16; i++)
        if (WK[i] == k)
            return i;
    return -1;
}
static void octave_step(int d)
{
    int o = clamp(set.octave + d, -3, 3);
    char t[4];
    if (o == set.octave)
        return;
    set.octave = (int8_t)o;
    mark();
    t[0] = o < 0 ? '-' : '+';
    t[1] = (char)('0' + (o < 0 ? -o : o));
    t[2] = 0;
    app_say("OCTAVE", t);
}

static void ble_device_say(void)
{
    char name[21];
    if (btdev_found(btdev_pick(), name, 0))
        app_say("DEVICE", name);
    else
        app_say("BLE", "SCANNING");
}

static void button_down(int b)
{
    if (ui.saving && b != B_SAVE) {        /* the slots open: any other button closes them, nothing kept */
        ui.saving = 0;
        app_say("NOT SAVED", "");
    }
    switch (b) {
    case B_HOME: case B_SEQ: case B_EDIT: case B_ENV: case B_LFO: case B_FX: case B_ARP: case B_GLO:
        page_button(b);
        break;
    case B_SEL:                           /* SEL: the button's list; on HOME, the sounds */
        ui.list = ui.list ? 0 : PAGE[ui.page].btn == B_HOME ? 2 : 1;
        if (ui.list == 2) {
            sounds_build();
            ui.list_top = 0;
        } else if (ui.list) {
            ui.list_sel = ui.list_top = 0;
            list_build();
        }
        break;
    case B_PLAY:
        par_set(CP_ARP, !set.snd.p[CP_ARP]);
        app_say("ARP", set.snd.p[CP_ARP] ? "ON" : "OFF");
        break;
    case B_OCTDN: octave_step(-1); break;
    case B_OCTUP: octave_step(1); break;
    case B_SAVE:
        ui.save_used = 0;
        if (ui.saving) {                  /* the slots open: SAVE keeps the sound in the one picked */
            ui.saving = 0;
            ui.save_used = 1;
            user_save(ui.save_pick);
        }
        break;
    default: break;
    }
}
static void button_up(int b, uint32_t held_ms)
{
    if (b == B_SAVE && !ui.save_used && held_ms < 1000u)
        settings_save();
    if (b == B_REC && FM1B_BLE) {
        if (held_ms >= 1000u)
            return;                       /* (its hold acted already) */
        if (FM1B_BLE_CENTRAL && !set.ble_off) {
            int ev;
            if (!btdev_busy() && !ui.looking) {
                ui.looking = 1;
                app_say("BLE", "SCANNING");
                return;
            }
            ev = btdev_save();
            ui.looking = 0;
            app_say(ev == BTDEV_CONNECTING ? btdev_word(ev) : "BLE", ev == BTDEV_CONNECTING ? btdev_name() : btdev_word(ev));
        }
    }
}

static void keys_input(void)
{
    uint32_t keys;
    touch_poll();
    keys = touch_keys();
    for (uint32_t k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            int note = key_note((int)k), v = set.velo ? (int)touch_vel(k) : 100, w = white_index((int)k);
            if (ui.saving) {               /* the slots open: a white key picks its slot (the keys don't play) */
                if (w >= 0) {
                    char t[8];
                    ui.save_pick = (uint8_t)w;
                    t[0] = 0;
                    num_i(w + 1, s_cat(t, "U"));
                    app_say("SLOT", t);
                }
                ui.key_note[k] = 0xFF;
                continue;
            }
            if (v < 1)
                v = 1;
            ui.key_note[k] = (uint8_t)note;
            ui.last_note = note;
            ui.prs[k] = 0;
            if (ui.page == P_MAP)
                set.map_pc = (uint8_t)(note % 12);
            chd_note_on(note, v);
            ui.note_t = plat_ms();
            note_out(0x90u, note, v);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            if (ui.key_note[k] != 0xFF) {
                chd_note_off(ui.key_note[k]);
                note_out(0x80u, ui.key_note[k], 0);
            }
        } else if ((keys & bit) && set.press && ui.key_note[k] != 0xFF && touch_pressure(k) != ui.prs[k]) {
            ui.prs[k] = touch_pressure(k);
            chd_pressure(ui.key_note[k], ui.prs[k]);
            note_out(0xA0u, ui.key_note[k], ui.prs[k]);
        }
    }
    ui.keys = keys;
}

static void input(void)
{
    uint32_t btn = plat_buttons(), now = plat_ms();
    int32_t e;
    keys_input();
    for (int b = 0; b < NB; b++) {
        uint32_t bit = 1u << b;
        if ((btn & bit) && !(ui.btn & bit)) {
            ui.btn_t[b] = now ? now : 1;
            ui.btn |= bit;                 /* (HOME as a shift: seen held at once) */
            button_down(b);
        } else if (!(btn & bit) && (ui.btn & bit)) {
            button_up(b, ui.btn_t[b] ? now - ui.btn_t[b] : 0);
            ui.btn_t[b] = 0;
            if (ui.card == b + 1)
                ui.card = 0;
        }
    }
    ui.btn = btn;
    if (!ui.saving && (btn >> B_SAVE & 1u) && ui.btn_t[B_SAVE] && now - ui.btn_t[B_SAVE] >= 400u && !ui.save_used) {
        ui.saving = 1;                     /* SAVE held: the slots open, on the sound's own or the first empty one */
        ui.save_used = 1;                  /* (letting go of SAVE now saves nothing) */
        ui.save_pick = 0;
        if (set.sound >= SND_USER)
            ui.save_pick = (uint8_t)(set.sound - SND_USER);
        else
            for (int i = NUSER - 1; i >= 0; i--)
                if (!user_used(i))
                    ui.save_pick = (uint8_t)i;
        app_say("PICK A SLOT", "then press SAVE");
    }
    /* a button held alone a second: its card (REC: Bluetooth off or on) */
    for (int b = 0; b < NB; b++)
        if ((btn >> b & 1u) && ui.btn_t[b] && now - ui.btn_t[b] >= 1000u && !ui.keys && !(btn & ~(1u << b))) {
            if (b == B_REC && FM1B_BLE) {
                set.ble_off = (uint8_t)!set.ble_off;
                mark();
                plat_ble_enable(!set.ble_off);
                app_say("BLUETOOTH", set.ble_off ? "OFF" : "ON");
            } else if (b != B_SAVE) {         /* (SAVE held shows the slots, not a card) */
                ui.card = b + 1;
                if (ann_on())
                    for (uint32_t i = 0; i < NCARDS; i++)
                        if (CARD[i].btn == b) {
                            char t[160];
                            t[0] = 0;
                            s_put(t, sizeof t, CARD[i].title, ". ", CARD[i].l1, " ");
                            s_put(t, sizeof t, CARD[i].l2, 0, 0, 0);
                            ann_say(t, 0, ANN_NOW);
                        }
            }
            if (b != B_SAVE)               /* (SAVE keeps its time: let go after looking at the slots, no save) */
                ui.btn_t[b] = 0;               /* (once a hold) */
        }
    /* the encoders */
    if (ui.list == 2 && ((e = plat_enc(EN_SELECT)) != 0 || (e = plat_enc(EN_ALGO)) != 0 ||
                         (e = plat_enc(EN_PRESET)) != 0)) {   /* the sounds: each turn plays the next */
        char t[32];
        ui.list_sel = clamp(ui.list_sel + (int)e, 0, ui.list_n - 1);
        sound_load(ui.list_k[ui.list_sel]);
        sound_name(ui.list_k[ui.list_sel], t);
        if (ann_on())
            ann_say(t, 0, ANN_NOW);
        e = 0;
    } else if ((e = plat_enc(EN_SELECT)) != 0) {
        if (ui.list) {
            ui.list_sel = list_step(clamp(ui.list_sel + (int)e, 0, ui.list_n - 1), e > 0 ? 1 : -1);
            say_knob(ui.list_k[ui.list_sel]);
        } else
            page_turn(e > 0 ? 1 : -1);
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        if (FM1B_BLE_CENTRAL && ui.looking) {
            btdev_choose(btdev_pick() + e);
            ble_device_say();
        } else if (ui.list)
            knob_turn(ui.list_k[ui.list_sel], e);
        else {
            int k = ui.page == P_MAP ? K_MAPCHORD : CP_CHORD;
            char v[24];
            knob_set(k, knob_get(k) + (e > 0 ? 1 : -1));
            value_text(par_of(k), knob_get(k), v);
            app_say(ui.page == P_MAP ? NOTE_NAME[set.map_pc] : "CHORD", v);
        }
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        int d = e > 0 ? 1 : -1, pr = PAGE[ui.page].preset;
        char v[24];
        if (pr == PR_SOUND)
            sound_step(d);
        else if (pr == PR_VOICE) {         /* Oscillators: the voice its knobs set (or all four) */
            ui.voice = (uint8_t)((ui.voice + d + 5) % 5);
            if (ui.voice) {
                v[0] = (char)('0' + ui.voice);
                v[1] = 0;
                app_say("VOICE", v);
            } else
                app_say("ALL VOICES", "");
        } else {
            int p = pr == PR_FTYPE ? CP_FMODE : pr == PR_DMODE ? CP_DL_MODE
                  : PAGE[ui.page].k[2] == CP_LFO_SHAPE || PAGE[ui.page].k[2] == CP_ALFO_SHAPE ? PAGE[ui.page].k[2]
                                                                                                : PAGE[ui.page].k[3];
            int n = CHD_PARAM[p].hi + 1;
            par_set(p, (set.snd.p[p] + d + n) % n);
            value_text(p, set.snd.p[p], v);
            app_say(pr == PR_FTYPE ? "FILTER" : pr == PR_DMODE ? "DELAY" : "SHAPE", v);
        }
    }
    for (int i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + i)) != 0 && !ui.list) {
            knob_turn(PAGE[ui.page].k[i], e);
            ui.touched = i;
            ui.touch_until = now + 700u;
        }
    /* the lights: the page's button, PLAY with the arp, SAVE with changes; the keys held and the chord's notes */
    {
        uint32_t lb = 1u << PAGE[ui.page].btn | (set.snd.p[CP_ARP] ? 1u << B_PLAY : 0u) | (ui.dirty ? 1u << B_SAVE : 0u)
                    | (ui.list ? 1u << B_SEL : 0u) | (btn & (1u << B_SAVE | 1u << B_HOME)), lk = ui.keys;
        for (int i = 0; i < 4; i++) {
            int n = chd_sounding[i], k = n - 53 - 12 * set.octave;
            if (n >= 0 && k >= 0 && k < NKEYS && chd_level[i] > 20)
                lk |= 1u << k;
        }
        if (ui.saving) {                   /* the slots: lit where a sound is kept, the one picked blinking */
            static const uint8_t WK[16] = PLAT_WHITE_KEYS;
            lk = 0;
            for (int i = 0; i < NUSER; i++)
                if ((user_used(i) && i != ui.save_pick) || (i == ui.save_pick && (now / 150u) % 2u == 0u))
                    lk |= 1u << WK[i];
            lb = 1u << B_SAVE;
            plat_glow(0);
            plat_leds(lb, lk);
        } else {
        plat_glow(set.lights == 0);
        plat_leds(set.lights == 2 ? 0u : lb, set.lights == 2 ? ui.keys : lk);
        }
    }
}

/* --------------------------------------------------------------- drawing --- */
static void cv_round(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
#ifdef GFX_AUDIT
    gfx_audit_box(x, y, w, h);
#endif
    if (r > h / 2)
        r = h / 2;
    if (r > w / 2)
        r = w / 2;
    if (r < 1) {
        cv_rect(x, y, w, h, c);
        return;
    }
    cv_rect(x, y + r, w, h - 2 * r, c);
    cv_rect(x + r, y, w - 2 * r, r, c);
    cv_rect(x + r, y + h - r, w - 2 * r, r, c);
    for (int32_t j = 0; j < r; j++)
        for (int32_t i = 0; i < r; i++) {
            float dx = (float)r - ((float)i + 0.5f), dy = (float)r - ((float)j + 0.5f), d2 = dx * dx + dy * dy;
            float cov = (float)r + 0.5f - (d2 > 1.0f ? f_sqrt1(d2) : 1.0f);
            uint32_t a = cov >= 1.0f ? 256u : cov <= 0.0f ? 0u : (uint32_t)(cov * 256.0f);
            cv_blend(x + i, y + j, c, a);
            cv_blend(x + w - 1 - i, y + j, c, a);
            cv_blend(x + i, y + h - 1 - j, c, a);
            cv_blend(x + w - 1 - i, y + h - 1 - j, c, a);
        }
}
static int32_t cap_y(int32_t top, int32_t h, const felucca_font_t *f)
{
    int32_t c2 = f == &FONT_L ? 28 : f == &FONT_M ? 21 : 14;
    return (2 * top + h - 1 - c2) >> 1;
}
static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}
static int32_t text_fit(int32_t x, int32_t y, const felucca_font_t *f, const char *s, int32_t w, uint16_t c)
{
    char b[48];
    uint32_t n;
    for (n = 0; n < sizeof b - 1u && s[n]; n++)
        b[n] = s[n];
    b[n] = 0;
    while (n && text_w(f, b) > w)
        b[--n] = 0;
    return cv_text(x, y, f, b, c);
}
static uint16_t col_mix(uint16_t a, uint16_t b, uint32_t s)   /* a moved s/16 of the way to b */
{
    uint32_t ar = a >> 11, ag = a >> 5 & 63u, ab = a & 31u, br = b >> 11, bg = b >> 5 & 63u, bb = b & 31u;
    return (uint16_t)(((ar * (16u - s) + br * s) / 16u) << 11 | ((ag * (16u - s) + bg * s) / 16u) << 5 |
                      (ab * (16u - s) + bb * s) / 16u);
}

/* the header: the button and the page, or a message for a moment; at the right "unsaved", the battery */
static void draw_batt(int32_t x, int b)
{
    uint16_t c = b == 0 ? K_REC : K_DIM;
    cv_rect(x + 2, 4, 3, 2, c);
    cv_rect(x, 6, 7, 14, c);
    cv_rect(x + 1, 7, 5, 12, K_BG);
    if (b >= 4) {                         /* a bolt: a computer powers it */
        cv_rect(x + 3, 8, 2, 4, INK[0]);
        cv_rect(x + 2, 11, 2, 4, INK[0]);
        return;
    }
    for (int i = 0; i < b && i < 3; i++)
        cv_rect(x + 2, 15 - 4 * i, 3, 3, c);
}
static void draw_header(void)
{
    int msg = plat_ms() < ui.msg_until, batt = plat_battery();
    uint32_t h = hash_u(hash_u(2166136261u, (uint32_t)msg << 8 | (uint32_t)ui.dirty << 9 | (uint32_t)set.edited << 11 |
                                               (uint32_t)set.sound << 16), (uint32_t)(batt + 1));
    if (msg)
        h = hash_s(hash_s(h, ui.msg[0]), ui.msg[1]);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (msg) {
        int32_t x;
        cv_round(1, 1, 238, HDR_H - 2, 7, K_FAINT);
        cv_round(2, 2, 236, HDR_H - 4, 6, K_PANEL);
        cv_round(6, 4, 16, 16, 8, INK[0]);
        text_c(14, cap_y(4, 16, &FONT_B), &FONT_B, "i", K_BG);
        x = cv_text(28, 4, &FONT_B, ui.msg[0], K_TEXT);
        text_fit(x + 8, text_w(&FONT_B, ui.msg[1]) <= 224 - x ? 4 : 6,
                 text_w(&FONT_B, ui.msg[1]) <= 224 - x ? &FONT_B : &FONT_XS, ui.msg[1], 230 - (x + 8), K_DIM);
    } else {   /* the sound, whole: its number and name ("*" changed since it was picked), a dot before it while the
                * settings are unsaved; the battery at the right */
        char nm[32];
        int32_t r = 232, x = 8;
        if (batt >= 0) {
            r -= 7;
            draw_batt(r, batt);
            r -= 8;
        }
        if (ui.dirty) {
            cv_round(x, 10, 5, 5, 2, K_DIM);
            x += 10;
        }
        sound_name(set.sound, nm);
        if (set.edited)
            s_cat(nm, "*");
        text_fit(x, 4, &FONT_B, nm, r - x, INK[0]);
    }
    cv_blit(0, 0);
}

/* the title strip, over the knobs: the button, the page's name, a dot for each of the button's pages (this one lit) */
static void draw_title(void)
{
    const char *bn = ui.saving ? "SAVE" : ui.list ? "SEL" : BTN_LABEL[PAGE[ui.page].btn];
    const char *pn = ui.saving ? "Slots" : PAGE[ui.page].name;
    int first, n = ui.saving ? 0 : page_n(PAGE[ui.page].btn, &first);
    int32_t w = text_w(&FONT_XS, bn) + 6, x;
    uint32_t h = hash_u(0x7171u, (uint32_t)ui.page | (uint32_t)ui.list << 8 | (uint32_t)ui.saving << 10);
    if (h == ui.sig[3])
        return;
    ui.sig[3] = h;
    cv_begin(240, TTL_H, K_BG);
    cv_round(8, 1, w, 15, 3, K_DIM);
    cv_round(9, 2, w - 2, 13, 2, K_BG);
    cv_text(11, cap_y(1, 15, &FONT_XS) - 1, &FONT_XS, bn, K_DIM);
    x = cv_text(8 + w + 6, cap_y(0, TTL_H, &FONT_B) - 1, &FONT_B, pn, K_TEXT) + 7;
    for (int i = 0; n > 1 && i < n; i++)
        cv_round(x + i * 8, 6, 5, 5, 2, first + i == ui.page ? K_TEXT : K_LINE);
    cv_blit(0, TTL_Y);
}

static int knob_live(int k);
static void draw_knobs(void)
{
    char v[4][24];
    const char *nm[4];
    int hot = plat_ms() < ui.touch_until ? ui.touched : -1, lo, hi;
    uint32_t h = hash_u(2166136261u, (uint32_t)ui.page | (uint32_t)(hot + 1) << 8 | (uint32_t)ui.voice << 16 |
                                     (uint32_t)ui.saving << 24);
    if (ui.saving) {                       /* (the slots: the knobs set nothing, their panel empty) */
        if (h != ui.sig[2]) {
            ui.sig[2] = h;
            cv_begin(240, KNB_H, K_BG);
            cv_round(4, 0, 232, KNB_H - 2, 6, K_PANEL);
            cv_blit(0, KNB_Y);
        }
        return;
    }
    for (int i = 0; i < 4; i++) {
        int k = PAGE[ui.page].k[i];
        nm[i] = k == K_NONE ? 0 : knob_name(k);
        v[i][0] = 0;
        if (k != K_NONE)
            value_text(par_of(k), knob_get(k), v[i]);
        h = hash_s(hash_u(h, (uint32_t)knob_get(k) ^ (uint32_t)(knob_live(k) + 1) << 16), v[i]);
    }
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, KNB_H, K_BG);
    cv_round(4, 0, 232, KNB_H - 2, 6, K_PANEL);
    for (int i = 0; i < 4; i++) {
        int32_t x = i * 60, cx = x + 30;
        int k = PAGE[ui.page].k[i];
        uint16_t vc = i == hot ? INK[0] : K_TEXT;
        if (i)
            cv_rect(x, 10, 1, KNB_H - 22, K_LINE);
        if (!nm[i])
            continue;
        text_fit(cx - (text_w(&FONT_XS, nm[i]) < 46 ? text_w(&FONT_XS, nm[i]) : 46) / 2, 0, &FONT_XS, nm[i], 46,
                 i == hot ? K_TEXT : K_DIM);
        if (text_w(&FONT_B, v[i]) <= 52)
            text_c(cx, cap_y(12, 28, &FONT_B), &FONT_B, v[i], vc);
        else if (text_w(&FONT_XS, v[i]) <= 48)
            text_c(cx, cap_y(12, 28, &FONT_XS), &FONT_XS, v[i], vc);
        else if ((loom_track = 1, text_w(&FONT_XS, v[i]) <= 48)) {   /* (a pixel or two over: the letters 1 px closer) */
            text_c(cx, cap_y(12, 28, &FONT_XS), &FONT_XS, v[i], vc);
            loom_track = 0;
        }
        else {
            loom_track = 0;
            text_fit(cx - 24, cap_y(12, 28, &FONT_XS), &FONT_XS, v[i], 48, vc);
        }
        knob_range(k, &lo, &hi);
        {
            int32_t val = knob_get(k) - lo, span = hi - lo, bw = span ? 44 * val / span : 0;
            cv_round(cx - 22, 41, 44, 6, 3, K_LINE);
            if (bw > 0)
                cv_round(cx - 22, 41, bw < 6 ? 6 : bw, 6, 3, i == hot ? K_TEXT : INK[i]);
            if (k != K_NONE && knob_live(k) >= 0 && span) {   /* moved by the sound now: a dot where it is */
                int32_t lx = cx - 22 + 38 * (clamp(knob_live(k), lo, hi) - lo) / span;
                cv_round(lx - 1, 39, 8, 10, 4, K_BG);
                cv_round(lx, 40, 6, 8, 3, K_TEXT);
            }
        }
    }
    cv_blit(0, KNB_Y);
}

/* the hints: two lines, a control's name in an outline and what it does here */
static int32_t hint_part(int32_t x, int32_t y, const char *ctl, const char *what, int draw)
{
    int enc = s_eq(ctl, "SELECT") || s_eq(ctl, "ALGO") || s_eq(ctl, "PRESET");
    int32_t pad = enc ? 5 : 3, w = text_w(&FONT_XS, ctl) + 2 * pad;
    if (draw) {
        cv_round(x, y, w, 15, enc ? 7 : 2, K_TEXT);
        cv_round(x + 1, y + 1, w - 2, 13, enc ? 6 : 1, K_BG);
        cv_text(x + pad, cap_y(y, 15, &FONT_XS), &FONT_XS, ctl, K_TEXT);
        cv_text(x + w + 3, cap_y(y, 15, &FONT_S), &FONT_S, what, K_TEXT);
    }
    return w + 3 + text_w(&FONT_S, what);
}
static void hint_line(int32_t y, const char *const *p, int n)
{
    int32_t tw = 0, gap = 0, x = 6;
    for (int i = 0; i < n; i++)
        tw += hint_part(0, 0, p[2 * i], p[2 * i + 1], 0);
    if (n > 1)
        gap = (228 - tw) / (n - 1) > 28 ? 28 : (228 - tw) / (n - 1);
    for (int i = 0; i < n; i++)
        x += hint_part(x, y, p[2 * i], p[2 * i + 1], 1) + gap;
}
static void draw_footer(void)
{
    const char *l1[4], *l2[4];
    int pr = PAGE[ui.page].preset;
    uint32_t h = hash_u(2166136261u, (uint32_t)ui.page | (uint32_t)ui.list << 8 | (uint32_t)(ui.card != 0) << 9 |
                                     (uint32_t)ui.saving << 10);
    if (h == ui.sig[4])
        return;
    ui.sig[4] = h;
    if (ui.saving) {
        l1[0] = "KEYS", l1[1] = "save slot", l1[2] = "SAVE", l1[3] = "keep";
        l2[0] = "HOME", l2[1] = "cancel", l2[2] = 0, l2[3] = 0;
    } else if (ui.list == 2) {
        l1[0] = "SELECT", l1[1] = "next sound", l1[2] = "KEYS", l1[3] = "play";
        l2[0] = "SEL", l2[1] = "close", l2[2] = "SAVE", l2[3] = "keep";
    } else if (ui.list) {
        l1[0] = "SELECT", l1[1] = "move", l1[2] = "ALGO", l1[3] = "change";
        l2[0] = "SEL", l2[1] = "close", l2[2] = "KEYS", l2[3] = "play";
    } else {
        l1[0] = "SELECT", l1[1] = "page", l1[2] = "ALGO", l1[3] = ui.page == P_MAP ? "its chord" : "chord";
        l2[0] = "PRESET";
        l2[1] = pr == PR_VOICE ? "voice" : pr == PR_FTYPE ? "filter type" : pr == PR_DMODE ? "delay mode"
              : pr == PR_LSHAPE ? "lfo shape" : "sound";
        l2[2] = "KEYS", l2[3] = ui.page == P_MAP ? "pick a note" : "play";
    }
    cv_begin(240, 32, K_BG);
    hint_line(0, l1, 2);
    hint_line(16, l2, l2[2] ? 2 : 1);
    cv_blit(0, FOOT_Y);
}

/* ---------------------------------------------------------- the aurora --- */
static uint32_t rnd_u(uint32_t i)         /* a fixed scatter: the stars */
{
    i = (i ^ 61u) ^ (i >> 16);
    i *= 9u;
    i ^= i >> 4;
    i *= 0x27d4eb2du;
    return i ^ (i >> 15);
}
static void sky(uint32_t t)               /* the night: a gradient and the stars, some twinkling */
{
    for (int32_t y = 0; y < MAIN_H; y++)
        cv_rect(0, y, 240, 1, col_mix(K_BG, K_SKY, (uint32_t)(y * 16 / MAIN_H)));
    for (uint32_t i = 0; i < 46u; i++) {
        uint32_t r = rnd_u(i + 7u), x = r % 240u, y = (r >> 8) % 90u, tw = (r >> 20) & 3u;
        uint32_t a = 70u + ((r >> 12) & 127u);
        if (tw == 0)
            a = (uint32_t)((float)a * (0.55f + 0.45f * fm_sin_turns((float)t * 0.0007f + (float)(r & 255u) * 0.01f)));
        cv_blend((int32_t)x, (int32_t)y, K_TEXT, a);
    }
}
/* one curtain of light: columns rising from a wavy foot, bright at the foot and fading upward */
static void curtain(float cx, float half, float level, uint16_t ink, float t, float seed)
{
    if (level <= 0.01f)
        return;
    for (int32_t x = ((int32_t)(cx - half) + 1) & ~1; x <= (int32_t)(cx + half); x += 2) {   /* (two pixels a column) */
        float u = ((float)x - cx) / half, env, sway;
        int32_t foot, hgt, a0, y;
        if (x < 0 || x >= 239 || u <= -1.0f || u >= 1.0f)
            continue;
        env = 1.0f - u * u;
        sway = fm_sin_turns((float)x * 0.011f + t * 0.21f + seed) * 0.5f +
               fm_sin_turns((float)x * 0.031f - t * 0.37f + seed * 2.0f) * 0.5f;
        foot = (int32_t)((float)(MAIN_H - 22) + 7.0f * sway);
        hgt = (int32_t)(level * env * (40.0f + 34.0f * (0.5f + 0.5f * fm_sin_turns((float)x * 0.02f + t * 0.13f + seed * 3.0f))));
        a0 = (int32_t)(200.0f * env * (0.6f + 0.4f * level));
        for (y = 0; y <= hgt; y++) {                 /* bright at the foot, fading upward (the square of the way up) */
            int32_t k = hgt - y, a = (a0 * k / (hgt + 1) * k / (hgt + 1)) & ~31;   /* (in steps of 32: the colours
                                                                                         * repeat, the mirror's code
                                                                                         * keeps them small) */
            cv_blend(x, foot - y, ink, (uint32_t)a);
            cv_blend(x + 1, foot - y, ink, (uint32_t)a);
        }
        cv_blend(x, foot, K_TEXT, (uint32_t)(90.0f * env * level));   /* (the bright hem) */
        cv_blend(x + 1, foot, K_TEXT, (uint32_t)(90.0f * env * level));
    }
}

static float wave_at(int w, float ph, float sh)   /* a voice's wave, as the engine makes it (no anti-aliasing) */
{
    switch (w) {
    case 1: {
        float x = (1.0f + sh * 5.0f) * fm_sin_turns(ph);
        for (int i = 0; i < 8 && (x > 1.0f || x < -1.0f); i++)
            x = x > 1.0f ? 2.0f - x : -2.0f - x;
        return x;
    }
    case 2: {
        float pk = 0.5f * (1.0f - sh);
        if (pk < 1e-4f)
            pk = 1e-4f;
        return ph < pk ? ph / pk * 2.0f - 1.0f : 1.0f - (ph - pk) / (1.0f - pk) * 2.0f;
    }
    case 3: {
        float p2 = ph * 2.0f;
        if (p2 >= 1.0f)
            p2 -= 1.0f;
        return (2.0f * ph - 1.0f) * (1.0f - sh) + (2.0f * p2 - 1.0f) * sh;
    }
    case 4: return ph < 0.05f + sh * 0.9f ? 1.0f : -1.0f;
    case 5: return ph < 0.02f + sh * 0.28f ? 1.0f : -1.0f;
    case 6: {
        float s = 0.0f;
        int n = 1 + (int)(sh * 7.0f);
        for (int h = 1; h <= n; h++)
            s += fm_sin_turns(ph * (float)h) / (float)h;
        return s * 0.5f;
    }
    default: return 0.0f;
    }
}
static float lfo_at(int shape, float ph)
{
    return shape == 0 ? 4.0f * fm_fabsf(ph - 0.5f) - 1.0f : shape == 1 ? 2.0f * ph - 1.0f : shape == 2 ? 1.0f - 2.0f * ph
                                                                                                    : ph < 0.5f ? 1.0f : -1.0f;
}
/* a glowing line through n points: the glow first, whole, then the bright core over it, each with round joins (a
 * dot at every point: no notch where the curve bends) */
#define GLOW_MAX 128
static void glow_poly(const float *xs, const float *ys, int n, uint16_t ink)
{
    uint16_t dim = col_mix(K_BG, ink, 6);
    for (int pass = 0; pass < 2; pass++) {
        float w = pass ? 1.6f : 4.0f;
        uint16_t c = pass ? ink : dim;
        for (int i = 0; i < n; i++) {
            if (i)
                cv_linef(xs[i - 1], ys[i - 1], xs[i], ys[i], c, w);
            cv_dotf(xs[i], ys[i], w * 0.5f, c);
        }
    }
}
/* a curve over the band: f(i/n) in -1..1, as a glowing line with light below it */
static void plot(float (*f)(float, const void *), const void *arg, int32_t x0, int32_t w, int32_t ymid, int32_t amp,
                 uint16_t ink)
{
    float xs[GLOW_MAX], ys[GLOW_MAX];
    int n = 0;
    for (int32_t i = 0; i <= w && n < GLOW_MAX; i += 2) {
        float v = f((float)i / (float)w, arg), y = (float)ymid - v * (float)amp;
        for (int32_t yy = (int32_t)y; yy < ymid + amp; yy++)   /* (the light under the curve) */
            cv_blend(x0 + i, yy, ink, (uint32_t)(40 - 34 * (yy - (int32_t)y) / (ymid + amp - (int32_t)y + 1)));
        xs[n] = (float)(x0 + i);
        ys[n++] = y;
    }
    glow_poly(xs, ys, n, ink);
}
typedef struct { int w; float sh; } wave_arg_t;
static float f_wave(float u, const void *a)
{
    const wave_arg_t *w = (const wave_arg_t *)a;
    return 0.9f * wave_at(w->w, u * 2.0f - fm_floorf(u * 2.0f), w->sh);
}
static float f_lfo(float u, const void *a)
{
    int s = *(const int *)a;
    return 0.9f * lfo_at(s, u);           /* (one cycle across: the dots travel all of it) */
}
typedef struct { float a, r, peak; int mode; } env_arg_t;
static float f_env(float u, const void *p)
{
    const env_arg_t *e = (const env_arg_t *)p;
    float v;
    if (u < e->a)
        v = u / e->a;
    else if (e->mode == 1 && u < e->peak)
        v = 1.0f;
    else {
        float s = e->mode == 1 ? e->peak : e->a;
        v = fm_expf(-5.0f * (u - s) / e->r);
    }
    return v * 1.8f - 0.9f;
}
typedef struct { float fc, q; int mode, slope; } flt_arg_t;
static float f_filter(float u, const void *p)    /* the response, log frequency 20 Hz .. 20 kHz, dB -30..+18 */
{
    const flt_arg_t *f = (const flt_arg_t *)p;
    float r = fm_exp2f(9.966f * u) * 20.0f / f->fc, r2 = r * r, den = (1.0f - r2) * (1.0f - r2) + r2 / (f->q * f->q);
    float g = f->mode == 0 ? 1.0f / den : f->mode == 1 ? r2 * r2 / den : r2 / (f->q * f->q) / den, db;
    if (f->mode == 0 || f->slope)
        g *= g;
    db = 10.0f * fm_log2f(g + 1e-9f) * 0.30103f;
    return fm_tanhf((db + 6.0f) / 22.0f);   /* (squeezed, not cut: a tall resonant peak stays round at the top) */
}

/* the page's picture moves: a voice sounds, or an LFO it shows runs (it is drawn again, ~15 times a second) */
static int pic_animated(void)
{
    const int16_t *p = set.snd.p;
    int pic = PAGE[ui.page].pic;
    (void)pic;
    return 1;                             /* (the aurora behind every page moves) */
    for (int i = 0; i < 4; i++)
        if (chd_level[i])
            return 1;
    return (ui.page == P_MORPH && p[CP_LM_DEPTH]) || (ui.page == P_PANM && p[CP_PM_DEPTH]) ||
           (pic == PIC_WAVE && p[CP_LFO_DEPTH]) || (pic == PIC_FILTER && p[CP_FLFO_DEPTH]);
}

/* a knob's value now, where the sound moves it (an LFO, an envelope, pressure), in its own units; -1: as set */
static int knob_live(int k)
{
    const volatile chd_live_t *L = &chd_live;
    float v;
    switch (k) {
    case CP_CUTOFF: v = L->cutoff; break;
    case CP_SHAPE1: case CP_SHAPE2: case CP_SHAPE3: case CP_SHAPE4: v = L->shape[k - CP_SHAPE1]; break;
    case K_VSHAPE: v = L->shape[ui.voice ? ui.voice - 1 : 0]; k = par_of(k); break;
    default: return -1;
    }
    {
        int n = (int)(v * 10000.0f + 0.5f);
        return n - knob_get(k) > 60 || knob_get(k) - n > 60 ? n : -1;   /* (only when it is away from the setting) */
    }
}
static float lfo_phase_of(int page)
{
    const volatile chd_live_t *L = &chd_live;
    return page == P_FLFO ? L->flfo : page == P_TREM ? L->alfo : page == P_VIB ? L->vib : L->lfo;
}

static void draw_main(void)
{
    char t[40];
    const page_t *pg = &PAGE[ui.page];
    uint32_t now = plat_ms(), h = hash_u(2166136261u, (uint32_t)ui.page);
    float tt = (float)now * 0.001f;
    int moving = pic_animated();
    for (int i = 0; i < 4; i++)
        h = hash_u(h, (uint32_t)pg->k[i] == (uint32_t)K_NONE ? 0u : (uint32_t)knob_get(pg->k[i]));
    h = hash_u(h, (uint32_t)set.snd.p[CP_FMODE] | (uint32_t)set.snd.p[CP_DL_MODE] << 4 | (uint32_t)set.map_pc << 8);
    for (int p = CP_PC0; p <= CP_PC11; p++)
        h = hash_u(h, (uint32_t)set.snd.p[p]);
    if (moving) {
        if (now - ui.anim < 33u && ui.sig[1])   /* (30 a second: the picture takes ~15-20 ms to draw on the FM-1) */
            return;
        ui.anim = now;
        h = hash_u(h, now);
    }
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    sky(now);
    {   /* behind every page: the voices' light, faint, as each voice sounds; the plate's share a wide wash under it
         * (what is placed, what is spread), the voices dimmed by as much */
        float idle = 0.18f, wet = u01v(set.snd.p[CP_RV_MIX]), all = 0.0f;
        for (int i = 0; i < 4; i++)
            all += (float)chd_level[i] / (255.0f * 4.0f);
        if (wet > 0.0f && all > 0.0f)
            curtain(120.0f, 119.0f, 0.8f * wet * all, INK[1], tt * 0.6f, 1.7f);
        for (int i = 0; i < 4; i++) {
            float lv = (float)chd_level[i] / 255.0f * (1.0f - 0.5f * wet);
            float x = chd_level[i] ? 30.0f + 180.0f * chd_live.pan[i] : 30.0f + 60.0f * (float)i;   /* (at its place) */
            curtain(x, 46.0f, lv > idle ? lv : idle, INK[i], tt,   /* (the same on every page: the aurora is the
                                                                       * ground the pictures stand on) */
                    (float)i * 0.37f);
        }
    }
    switch (pg->pic) {
    case PIC_CURTAINS: {   /* the chord: its name, the notes under their light */
        int any = 0;
        for (int i = 0; i < 4; i++) {
            int n = chd_sounding[i];
            if (n >= 0 && chd_level[i]) {
                char *e = s_cat((t[0] = 0, t), NOTE_NAME[n % 12]);
                num_i(n / 12 - 1, e);
                text_c((int32_t)(30.0f + 180.0f * chd_live.pan[i]), MAIN_H - 17, &FONT_S, t, INK[i]);
                any = 1;
            }
        }
        if (ui.page == P_INTERVALS) {
            for (int i = 0; i < 4; i++) {
                int iv = i ? set.snd.p[CP_INT1 + i - 1] : 0;
                t[0] = 0;
                if (iv > 0)
                    s_cat(t, "+");
                num_i(iv, t + (iv > 0));
                text_c(30 + 60 * i, 6, &FONT_M, t, any ? K_DIM : K_TEXT);
            }
        } else if (ui.page == P_SETUP) {
            cv_text(10, 6, &FONT_M, "CHORDISM", K_TEXT);
            cv_text(10, 36, &FONT_S, "one key in, a chord out", K_DIM);
            t[0] = 0;
            s_cat(num_i((int32_t)plat_cpu_pct(), s_cat(t, "audio ")), "%");
            cv_text(10, 58, &FONT_XS, t, K_FAINT);
        } else {
            int pc = any && chd_sounding[0] >= 0 ? chd_sounding[0] % 12 : set.map_pc;   /* (map: the chord of the note
                                                                                       * sounding, or the one last played) */
            const char *cn = set.snd.p[CP_TUNING] == 1 ? "Intervals" : set.snd.p[CP_TUNING] == 2
                                 ? CHD_CHORD_NAME[set.snd.p[CP_PC0 + pc]] : CHD_CHORD_NAME[set.snd.p[CP_CHORD]];
            t[0] = 0;
            if (any && chd_sounding[0] >= 0)
                s_cat(s_cat(t, NOTE_NAME[chd_sounding[0] % 12]), " ");
            else if (set.snd.p[CP_TUNING] == 2)
                s_cat(s_cat(t, NOTE_NAME[pc]), " ");
            s_cat(t, cn);
            cv_text(10, 6, &FONT_L, t, K_TEXT);
        }
        break;
    }
    case PIC_WAVE: {
        wave_arg_t a;
        if (ui.page == P_LOFI) {
            a.w = 1;
            a.sh = 0.0f;
            plot(f_wave, &a, 16, 208, 58, 40, INK[0]);
            if (set.snd.p[CP_GRIND] + set.snd.p[CP_DECIM]) {   /* the steps lo-fi leaves */
                int hold = 1 + set.snd.p[CP_DECIM] * 32 / 10000;
                float steps = fm_exp2f(16.0f - u01v(set.snd.p[CP_GRIND]) * 14.0f) * 0.5f;
                if (steps > 64.0f)
                    steps = 64.0f;
                for (int32_t i = 0; i < 208; i += hold * 2) {
                    float v2 = fm_floorf(wave_at(1, (float)i / 104.0f - fm_floorf((float)i / 104.0f), 0.0f) * 0.9f * steps + 0.5f) / steps;
                    cv_rect(16 + i, (int32_t)(58.0f - v2 * 40.0f), hold * 2, 2, INK[3]);
                }
            }
            cv_text(10, 4, &FONT_XS, "lo-fi", K_DIM);
            break;
        }
        for (int v = 3; v >= 0; v--) {    /* the voices' waves: the one picked bright, the others faint (all: each) */
            int on = !ui.voice || ui.voice == v + 1;
            if (!on && set.snd.p[CP_WAVE1 + v] == 0)
                continue;
            a.w = set.snd.p[CP_WAVE1 + v];
            a.sh = knob_live(CP_SHAPE1 + v) >= 0 ? chd_live.shape[v] : u01v(set.snd.p[CP_SHAPE1 + v]);
            if (on)
                plot(f_wave, &a, 16, 208, 62, 34, INK[v]);
            else {
                float px = 0.0f, py = 0.0f;
                for (int32_t i = 0; i <= 208; i += 4) {
                    float y = 62.0f - f_wave((float)i / 208.0f, &a) * 34.0f, x = (float)(16 + i);
                    if (i)
                        cv_linef(px, py, x, y, col_mix(K_BG, INK[v], 5), 1.0f);
                    px = x, py = y;
                }
            }
        }
        for (int v = 0; v < 5; v++) {     /* the chips: all, 1, 2, 3, 4; the one picked filled */
            static const char *const VN[5] = {"all", "1", "2", "3", "4"};
            int32_t x = v ? 50 + (v - 1) * 26 : 10, w = v ? 22 : 34;
            int on = v == ui.voice;
            uint16_t c = v ? INK[v - 1] : K_TEXT;
            cv_round(x, 2, w, 15, 7, on ? c : col_mix(K_BG, c, 5));
            if (!on)
                cv_round(x + 1, 3, w - 2, 13, 6, K_BG);
            text_c(x + w / 2, cap_y(2, 15, &FONT_XS), &FONT_XS, VN[v], on ? K_BG : c);
        }
        break;
    }
    case PIC_LFO: {   /* the curve as deep as the LFO goes (the full range faint behind), and where it acts: a dot
                       * for each place it moves, at its phase now */
        int s = set.snd.p[pg->k[2] == CP_LFO_SHAPE || pg->k[2] == CP_ALFO_SHAPE ? pg->k[2]
                          : pg->k[3] == CP_FLFO_SHAPE ? CP_FLFO_SHAPE : CP_LFO_SHAPE];
        int dp = ui.page == P_SLFO ? CP_LFO_DEPTH : ui.page == P_FLFO ? CP_FLFO_DEPTH : ui.page == P_TREM ? CP_ALFO_DEPTH
                                                                                                         : CP_VIB_DEPTH;
        float depth = u01v(set.snd.p[dp]), ph = lfo_phase_of(ui.page), off[4] = {0, 0, 0, 0};
        int nd = 1, amp;
        const char *where = "";
        char wt[32];
        if (ui.page == P_VIB)
            s = -1;                       /* (vibrato: a sine) */
        amp = (int)(36.0f * depth + 0.5f);
        {
            float px = 0.0f, py = 0.0f;   /* the full range, faint */
            for (int32_t i = 0; i <= 208; i += 4) {
                float p = (float)i / 208.0f;
                float y = 60.0f - (s < 0 ? fm_sin_turns(p) : lfo_at(s, p)) * 36.0f, x = (float)(16 + i);
                if (i)
                    cv_linef(px, py, x, y, K_LINE, 1.0f);
                px = x, py = y;
            }
        }
        if (amp > 0) {
            int sh = s < 0 ? 0 : s;
            if (s < 0) {
                float xs[GLOW_MAX], ys[GLOW_MAX];
                int n = 0;
                for (int32_t i = 0; i <= 208 && n < GLOW_MAX; i += 2) {
                    xs[n] = (float)(16 + i);
                    ys[n++] = 60.0f - fm_sin_turns((float)i / 208.0f) * (float)amp;
                }
                glow_poly(xs, ys, n, INK[1]);
            } else
                plot(f_lfo, &sh, 16, 208, 60, (int32_t)((float)amp / 0.9f), INK[1]);
        }
        if (ui.page == P_SLFO) {          /* every voice, each at its own phase */
            nd = 4;
            for (int i = 0; i < 4; i++)
                off[i] = u01v(set.snd.p[CP_LFO_PH1 + i]);
            where = "every voice, its own phase";
        } else if (ui.page == P_FLFO) {   /* the filter, left and right apart by the spread */
            nd = 2;
            off[1] = u01v(set.snd.p[CP_FLFO_SPREAD]) * 0.5f;
            where = "the filter, left and right";
        } else if (ui.page == P_TREM)
            where = "the whole sound";
        else {
            mask_text(set.snd.p[CP_VIB_OSC], s_cat((wt[0] = 0, wt), "the pitch of voices "));
            where = wt;
        }
        for (int i = 0; i < nd; i++) {
            float p = ph + off[i], y, x;
            p -= fm_floorf(p);
            y = 60.0f - (s < 0 ? fm_sin_turns(p) : lfo_at(s, p)) * (float)amp;
            x = 16.0f + 208.0f * p;
            cv_dotf(x, y, 6.0f, col_mix(K_BG, nd == 4 ? INK[i] : nd == 2 ? INK[i ? 2 : 0] : INK[0], 8));
            cv_dotf(x, y, 3.0f, nd == 4 ? INK[i] : K_TEXT);
            if (nd == 2 && off[1] > 0.04f)   /* (apart enough to tell: named) */
                cv_text((int32_t)x - 3, (int32_t)y - 22, &FONT_XS, i ? "R" : "L", K_TEXT);
        }
        cv_text(10, MAIN_H - 15, &FONT_XS, where, K_DIM);
        break;
    }
    case PIC_ENV: {
        env_arg_t e;
        int fe = ui.page == P_FENV, sw = ui.page == P_SWEEP;
        float a = u01v(set.snd.p[fe ? CP_FENV_A : CP_ATTACK]), r = u01v(set.snd.p[fe ? CP_FENV_D : CP_RELEASE]);
        if (sw) {
            a = 0.0f;
            r = 1.0f - u01v(set.snd.p[CP_SWEEP_RATE]);
        }
        e.a = 0.02f + 0.3f * a;
        e.r = 0.05f + 0.6f * r;
        e.mode = sw ? 0 : set.snd.p[fe ? CP_FENV_MODE : CP_VCA_MODE];
        e.peak = e.a + 0.3f;
        plot(f_env, &e, 16, 208, 60, 42, INK[fe ? 2 : sw ? 3 : 0]);
        if (!sw)                          /* where each voice's envelope is now (the filter's: one) */
            for (int i = 0; i < (fe ? 1 : 4); i++) {
                float v = fe ? chd_live.fenv : chd_live.env[i];
                if (v > 0.002f) {
                    cv_rect(226 + 3 * i - (fe ? 3 : 0), (int32_t)(102.0f - v * 84.0f), 2, (int32_t)(v * 84.0f), INK[fe ? 2 : i]);
                }
            }
        break;
    }
    case PIC_FILTER: {
        flt_arg_t f;
        f.fc = 20.0f * fm_exp2f(9.96578428f * u01v(set.snd.p[CP_CUTOFF]));
        f.q = 0.5f + 11.5f * u01v(set.snd.p[CP_RESO]);
        f.mode = set.snd.p[CP_FMODE];
        f.slope = set.snd.p[CP_SLOPE];
        if (knob_live(CP_CUTOFF) >= 0) {   /* moving (an envelope, an LFO, pressure): the setting's curve faint, the
                                            * cutoff now's bright */
            float pv = 0.0f, px = 0.0f;
            for (int32_t i = 0; i <= 208; i += 4) {
                float v = f_filter((float)i / 208.0f, &f), y = 58.0f - v * 40.0f, x = (float)(16 + i);
                if (i)
                    cv_linef(px, pv, x, y, K_FAINT, 1.2f);
                px = x, pv = y;
            }
            f.fc = 20.0f * fm_exp2f(9.96578428f * chd_live.cutoff);
        }
        plot(f_filter, &f, 16, 208, 58, 40, INK[1]);
        value_text(CP_FMODE, f.mode, t);
        cv_text(10, MAIN_H - 15, &FONT_XS, t, K_DIM);
        break;
    }
    case PIC_SCALE: {      /* the twelve notes, stars on an arc: the scale's lit, the root pink */
        int sc = set.snd.p[CP_SCALE], root = set.snd.p[CP_ROOT];
        for (int i = 0; i < 12; i++) {
            float x = 20.0f + (float)i * 18.2f, y = 70.0f - 26.0f * fm_sin_turns((float)i / 22.0f + 0.02f);
            int pc = (i - root + 12) % 12, in = chd_in_scale(sc, pc);
            uint16_t c = !in ? K_LINE : pc == 0 ? INK[3] : INK[0];
            cv_dotf(x, y, in ? 5.0f : 3.0f, col_mix(K_BG, c, 6));
            cv_dotf(x, y, in ? 3.0f : 2.0f, c);
            text_c((int32_t)x, (int32_t)y + 9, &FONT_XS, NOTE_NAME[i], in ? K_TEXT : K_FAINT);
        }
        cv_text(10, 4, &FONT_B, CHD_SCALE_NAME[sc], K_TEXT);
        break;
    }
    case PIC_MAP: {        /* a chord for each note: twelve cells, the note picked framed */
        for (int i = 0; i < 12; i++) {
            int32_t x = 6 + (i % 4) * 57, y = 4 + (i / 4) * 38;
            int on = i == set.map_pc;
            cv_round(x, y, 54, 34, 5, on ? K_TEXT : K_PANEL);
            cv_round(x + 1, y + 1, 52, 32, 4, on ? K_PANEL2 : col_mix(K_BG, K_PANEL, 10));
            cv_text(x + 5, y + 1, &FONT_XS, NOTE_NAME[i], on ? INK[0] : K_DIM);
            text_fit(x + 5, y + 15, &FONT_S, CHD_CHORD_NAME[set.snd.p[CP_PC0 + i]], 46, K_TEXT);
        }
        break;
    }
    case PIC_BARS: {       /* four columns of light, a voice each */
        for (int i = 0; i < 4; i++) {
            int k = pg->k[i];
            float lv = k == K_NONE ? 0.0f : u01v(knob_get(k));
            int32_t x = 18 + i * 56, hgt = (int32_t)(lv * 80.0f);
            if (ui.page == P_MORPH || ui.page == P_PANM) {   /* the levels, or the places, the morph gives */
                float g[4], pn[4];
                chd_morph_view(u01v(set.snd.p[CP_MORPH]), u01v(set.snd.p[CP_MORPH_INT]), u01v(set.snd.p[CP_PAN]),
                               u01v(set.snd.p[CP_PAN_INT]), g, pn);
                if (set.snd.p[CP_LM_DEPTH] || set.snd.p[CP_CT_MORPH])   /* (moving: as it is now) */
                    g[i] = chd_live.gain[i];
                if (set.snd.p[CP_PM_DEPTH])
                    pn[i] = (chd_live.pan[i] - 0.5f - ((float)i / 3.0f - 0.5f) * u01v(set.snd.p[CP_WIDTH])) * 2.0f;
                if (ui.page == P_PANM) {
                    float px = 120.0f + 100.0f * fm_clampf((float)i / 3.0f - 0.5f + pn[i] * 0.5f, -1.0f, 1.0f);
                    cv_dotf(px, 30.0f + 18.0f * (float)i, 9.0f, col_mix(K_BG, INK[i], 6));
                    cv_dotf(px, 30.0f + 18.0f * (float)i, 5.0f, INK[i]);
                    continue;
                }
                lv = g[i];
                hgt = (int32_t)(lv * 80.0f);
            }
            for (int32_t y = 0; y < hgt; y++)
                cv_rect(x, 96 - y, 36, 1, col_mix(K_BG, INK[i], (uint32_t)(14 - 12 * y / (hgt + 1))));
            if (hgt)
                cv_rect(x, 96 - hgt, 36, 2, K_TEXT);
            t[0] = 0;
            num_i(i + 1, t);
            text_c(x + 18, 100, &FONT_XS, t, K_DIM);
        }
        if (ui.page == P_PANM)
            cv_text(10, MAIN_H - 15, &FONT_XS, "left", K_DIM), cv_text(208, MAIN_H - 15, &FONT_XS, "right", K_DIM);
        break;
    }
    case PIC_VOICES: {     /* each voice: the note it plays, its interval and its detune (a key's chord: the one held,
                            * else C4's) */
        int notes[4], root = chd_sounding[0] >= 0 && chd_level[0] ? chd_sounding[0] : 60;
        PLAT_FAR(chd_voicing)(root, notes);
        if (chd_sounding[0] >= 0 && chd_level[0])
            for (int i = 0; i < 4; i++)
                notes[i] = chd_sounding[i] >= 0 ? chd_sounding[i] : notes[i];
        for (int i = 0; i < 4; i++) {
            int32_t y = 6 + i * 26, iv = notes[i] - notes[0] + (notes[0] - root);
            int ct = i * set.snd.p[CP_DETUNE] * 50 / 10000;
            char a[16], c[16];
            cv_round(10, y, 22, 20, 10, INK[i]);
            t[0] = (char)('1' + i);
            t[1] = 0;
            text_c(21, cap_y(y, 20, &FONT_B), &FONT_B, t, K_BG);
            t[0] = 0;
            num_i(notes[i] / 12 - 1, s_cat(t, NOTE_NAME[notes[i] % 12]));
            cv_text(42, cap_y(y, 20, &FONT_B), &FONT_B, t, K_TEXT);
            a[0] = 0;
            s_cat(num_i(iv, s_cat(a, iv >= 0 ? "+" : "")), " st");
            cv_text(100, cap_y(y, 20, &FONT_S), &FONT_S, a, K_DIM);
            c[0] = 0;
            s_cat(num_i(ct, s_cat(c, "+")), " ct");
            cv_text(170, cap_y(y, 20, &FONT_S), &FONT_S, ct ? c : "", INK[i]);
        }
        break;
    }
    case PIC_PLATE: {      /* the plate: rings that spread from the middle while it rings, as big as the size, fading as
                            * slowly as the decay */
        static float energy;
        static uint32_t last;
        float sz = 0.45f + 0.55f * u01v(set.snd.p[CP_RV_SIZE]), dc = u01v(set.snd.p[CP_RV_DECAY]);
        float wet = u01v(set.snd.p[CP_RV_MIX]), in = 0.0f, dt = (float)(now - last) * 0.001f;
        float speed = 18.0f + 30.0f * (1.0f - dc), rmax = 130.0f * sz;
        last = now;
        for (int i = 0; i < 4; i++)
            in += (float)chd_level[i] / (255.0f * 4.0f);
        energy *= fm_expf(-dt / (0.3f + 3.0f * dc));
        if (in * wet > energy)
            energy = in * wet;
        for (int k = 0; k < 7; k++) {
            float r = fm_fabsf((float)now * 0.001f * speed + (float)k * rmax / 7.0f);
            float shim = set.snd.p[CP_RV_SHIMMER] > 5000 ? 0.5f * fm_sin_turns((float)now * 0.0005f + (float)k * 0.3f) : 0.0f;
            uint32_t a;
            r -= rmax * fm_floorf(r / rmax);
            a = (uint32_t)(255.0f * (0.3f + 0.7f * energy) * (1.0f - r / rmax));
            for (int j = 0; j < 120; j++) {      /* (each ring two pixels wide) */
                float u = (float)j / 120.0f, x = 120.0f + r * fm_sin_turns(u), y = 54.0f + (r * 0.45f + shim) * fm_sin_turns(u + 0.25f);
                cv_blend((int32_t)x, (int32_t)y, INK[(k + 1) % 4], a);
                cv_blend((int32_t)x + 1, (int32_t)y, INK[(k + 1) % 4], a / 2u);
            }
        }
        break;
    }
    case PIC_DELAY: {      /* the repeats: spaced by the time, each as loud as the feedback leaves it, on the side it
                            * plays (ping-pong and flip-flop bounce; long is in the middle; zenith and interval climb);
                            * the one sounding now lit, from the last chord played */
        int md = set.snd.p[CP_DL_MODE];
        float tms = (0.005f + 1.395f * u01v(set.snd.p[CP_DL_TIME])) * 1000.0f, fb = 0.95f * u01v(set.snd.p[CP_DL_FB]);
        float gap = 210.0f * tms / 2800.0f, lv = 1.0f;
        int on = ui.note_t ? (int)((float)(now - ui.note_t) / tms) : -1;
        if (gap < 6.0f)
            gap = 6.0f;
        cv_rect(14, 50, 212, 1, K_LINE);
        for (int k = 1; k < 24 && lv > 0.03f; k++) {
            float x = 16.0f + gap * (float)k;
            int side = md == 1 || md == 2 ? (k & 1 ? -1 : 1) : md == 3 ? 0 : 0;   /* (-1 left, above; 1 right, below) */
            int32_t h;
            uint16_t c = INK[(k - 1) % 4];
            lv *= k == 1 ? 1.0f : fb;
            if (x > 226.0f)
                break;
            h = (int32_t)(40.0f * lv);
            if (k == on)
                c = K_TEXT;
            if (md == 4 || md == 5) {        /* (climbing: each a step higher) */
                int32_t y = 92 - (k - 1) * 9;
                if (y < 8)
                    break;
                cv_round((int32_t)x - 2, y - h / 3, 5, h / 3 + 2, 2, col_mix(K_BG, c, k == on ? 16 : 11));
            } else if (side < 0)
                cv_round((int32_t)x - 2, 50 - h, 5, h, 2, col_mix(K_BG, c, k == on ? 16 : 11));
            else if (side > 0)
                cv_round((int32_t)x - 2, 51, 5, h, 2, col_mix(K_BG, c, k == on ? 16 : 11));
            else
                cv_round((int32_t)x - 2, 50 - h / 2, 5, h, 2, col_mix(K_BG, c, k == on ? 16 : 11));
        }
        if (md == 1 || md == 2) {
            cv_text(10, 4, &FONT_XS, "left", K_DIM);
            cv_text(10, 84, &FONT_XS, "right", K_DIM);
        }
        value_text(CP_DL_MODE, md, t);
        cv_text(10, MAIN_H - 15, &FONT_XS, t, K_DIM);
        break;
    }
    case PIC_CLOCK: {      /* the MIDI clock coming in: its tempo, a dot on each beat */
        static uint32_t t0, ticks0, bpm10, lastbeat, flash;
        uint32_t ticks = chd_clock_ticks, beat = ticks / 24u;
        if (now - t0 >= 1000u) {             /* (the tempo from the clocks of the last second) */
            uint32_t n = ticks - ticks0;
            bpm10 = n ? n * 600u * 1000u / 24u / (now - t0) : 0u;
            t0 = now;
            ticks0 = ticks;
        }
        if (beat != lastbeat) {
            lastbeat = beat;
            flash = now;
        }
        t[0] = 0;
        if (bpm10)
            s_cat(num_dec((int32_t)bpm10, t), " bpm");
        else
            s_cat(t, "no clock");
        cv_text(10, 6, &FONT_L, t, bpm10 ? K_TEXT : K_DIM);
        cv_text(10, 44, &FONT_S, !set.snd.p[CP_ARP_SYNC] ? "the arp keeps its own tempo" : bpm10 ? "the arp follows it" : "own tempo till a clock comes",
                K_DIM);
        for (int i = 0; i < 4; i++) {
            int lit = bpm10 && (int)(beat % 4u) == i && now - flash < 150u;
            cv_dotf(40.0f + 52.0f * (float)i, 80.0f, lit ? 9.0f : 5.0f, lit ? INK[i] : col_mix(K_BG, INK[i], 5));
        }
        break;
    }
    case PIC_ARP: {        /* the rhythm: a ring of steps, the beats lit */
        int st = set.snd.p[CP_ARP_STEPS], bt = set.snd.p[CP_ARP_BEATS];
        for (int i = 0; i < st; i++) {
            float a = (float)i / (float)st, x = 120.0f + 44.0f * fm_sin_turns(a), y = 56.0f - 44.0f * fm_sin_turns(a + 0.25f);
            int on = bt == 0 || ((i * bt) % st) < bt, now = set.snd.p[CP_ARP] && chd_held &&
                                                              i == (chd_live.arp_pos + st - 1) % st;
            cv_dotf(x, y, now ? 9.0f : on ? 6.0f : 3.0f, now ? col_mix(K_BG, INK[0], 9) : on ? col_mix(K_BG, INK[2], 7) : K_LINE);
            if (on || now)
                cv_dotf(x, y, now ? 4.0f : 3.0f, now ? K_TEXT : INK[2]);
        }
        cv_text(10, 4, &FONT_B, set.snd.p[CP_ARP] ? "arp on" : "arp off", set.snd.p[CP_ARP] ? INK[0] : K_DIM);
        break;
    }
    case PIC_SPACE: {      /* the plate's rings or the delay's echoes, fading */
        if (ui.page == P_DELAY) {
            float gap = 8.0f + 60.0f * u01v(set.snd.p[CP_DL_TIME]), lv = 1.0f, fb = 0.95f * u01v(set.snd.p[CP_DL_FB]);
            for (int i = 0; i < 12 && lv > 0.03f; i++, lv *= fb) {
                float x = 20.0f + gap * (float)i;
                if (x > 230.0f)
                    break;
                cv_rect((int32_t)x, (int32_t)(96.0f - 70.0f * lv), 4, (int32_t)(70.0f * lv), col_mix(K_BG, INK[i % 4], 12));
            }
            value_text(CP_DL_MODE, set.snd.p[CP_DL_MODE], t);
            cv_text(10, 4, &FONT_B, t, K_TEXT);
        } else {
            float sz = 0.4f + 0.6f * u01v(set.snd.p[CP_RV_SIZE]), dc = u01v(set.snd.p[CP_RV_DECAY]);
            for (int i = 1; i <= 6; i++) {
                float r = sz * 16.0f * (float)i;
                uint16_t c = col_mix(K_BG, INK[(i + 1) % 4], (uint32_t)(14 - i * (2 - (int)(dc + 0.5f))));
                for (int a = 0; a < 64; a++) {
                    float u = (float)a / 64.0f;
                    cv_blend((int32_t)(120.0f + r * fm_sin_turns(u)), (int32_t)(60.0f + r * 0.55f * fm_sin_turns(u + 0.25f)), c, 200);
                }
            }
        }
        break;
    }
    case PIC_CTRL: {
        value_text(CP_CTRL_SRC, set.snd.p[CP_CTRL_SRC], t);
        cv_text(10, 4, &FONT_M, t, K_TEXT);
        cv_text(10, 32, &FONT_S, "moves", K_DIM);
        {
            static const int R[5] = {CP_CT_CUTOFF, CP_CT_MORPH, CP_CT_VIB, CP_CT_SHAPE, CP_CT_FM};
            for (int i = 0; i < 5; i++) {
                int32_t y = 52 + i * 12, w = set.snd.p[R[i]] * 150 / 10000;
                cv_text(10, y - 3, &FONT_XS, knob_name(R[i]), K_DIM);
                cv_round(70, y, 152, 6, 3, K_LINE);
                if (w > 0)
                    cv_round(70, y, w < 6 ? 6 : w, 6, 3, INK[i % 4]);
            }
        }
        break;
    }
    default: break;
    }
    cv_blit(0, MAIN_Y);
}

/* SEL's list, over the picture, the tag and the knobs */
static void draw_list(void)
{
    char v[24];
    uint32_t h = hash_u(2166136261u, (uint32_t)ui.list_sel | (uint32_t)ui.page << 8);
    const int rows = 9;
    for (int i = 0; i < ui.list_n; i++)
        h = hash_u(h, ui.list == 2 || IS_HEAD(ui.list_k[i]) ? (uint32_t)ui.list_k[i] : (uint32_t)knob_get(ui.list_k[i]));
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    ui.sig[2] = ui.sig[3] = 0;
    if (ui.list_sel < ui.list_top)
        ui.list_top = ui.list_sel;
    if (ui.list_sel >= ui.list_top + rows)
        ui.list_top = ui.list_sel - rows + 1;
    for (int band = 0; band < 2; band++) {    /* (two canvases: the canvas holds 144 rows) */
        int32_t y0 = band ? 102 : 0, hh = band ? 82 : 102;   /* (split between rows 5 and 6) */
        cv_begin(240, (uint32_t)hh, K_BG);
        for (int r = 0; r < rows; r++) {
            int i = ui.list_top + r, on = i == ui.list_sel;
            int32_t y = 2 + r * 20 - y0;
            if (i >= ui.list_n)
                break;
            if (y + 20 < 0 || y >= hh)
                continue;
            if (IS_HEAD(ui.list_k[i])) {    /* a section: its page's name, small, a rule after it */
                int pg = -2 - ui.list_k[i];
                int32_t e = cv_text(10, cap_y(y, 18, &FONT_XS) - 1, &FONT_XS, pg < NPAGES ? PAGE[pg].name : "more",
                                    K_DIM);
                cv_rect(e + 6, y + 9, 222 - e, 1, K_FAINT);
                continue;
            }
            cv_round(6, y, 222, 19, 5, on ? K_TEXT : K_PANEL);
            if (ui.list == 2) {             /* a sound: its number and name */
                char nm[32];
                sound_name(ui.list_k[i], nm);
                text_fit(14, cap_y(y, 18, &FONT_S) - 1, &FONT_S, nm, 200, on ? K_BG : K_TEXT);
                continue;
            }
            text_fit(14, cap_y(y, 18, &FONT_S) - 1, &FONT_S, knob_name(ui.list_k[i]), 110, on ? K_BG : K_TEXT);
            value_text(par_of(ui.list_k[i]), knob_get(ui.list_k[i]), v);
            cv_text(220 - text_w(&FONT_B, v), cap_y(y, 18, &FONT_B) - 1, &FONT_B, v, on ? K_BG : INK[0]);
        }
        if (ui.list_n > rows) {
            int32_t th = 180, bh = th * rows / ui.list_n, by = th * ui.list_top / ui.list_n;
            cv_round(232, 2 - y0, 4, th, 2, K_PANEL);
            cv_round(232, 2 + by - y0, 4, bh, 2, K_FAINT);
        }
        cv_blit(0, (uint32_t)(MAIN_Y + y0));
    }
}

/* a button's card over the picture */
static int32_t wrap(int32_t x, int32_t y, int32_t w, const felucca_font_t *f, const char *s, uint16_t c)
{
    char line[48];
    while (*s) {
        uint32_t n = 0, cut = 0;
        while (s[n] && n < sizeof line - 1u) {
            line[n] = s[n];
            line[n + 1] = 0;
            if (text_w(f, line) > w)
                break;
            n++;
            if (!s[n] || s[n] == ' ')
                cut = n;
        }
        if (!cut)
            cut = n ? n : 1;
        line[cut] = 0;
        cv_text(x, y, f, line, c);
        y += 17;
        s += cut;
        while (*s == ' ')
            s++;
    }
    return y;
}
static void draw_card(void)
{
    int b = ui.card - 1, i;
    uint32_t h = hash_u(0xCA4Du, (uint32_t)b);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    for (i = 0; i < (int)NCARDS && CARD[i].btn != b; i++)
        ;
    cv_begin(240, MAIN_H, K_BG);
    cv_round(8, 4, 224, MAIN_H - 8, 8, K_PANEL);
    {
        int32_t w = text_w(&FONT_S, BTN_LABEL[b]) + 16, y;
        cv_round(18, 12, w, 20, 6, K_TEXT);
        cv_round(19, 13, w - 2, 18, 5, K_PANEL);
        text_c(18 + w / 2, 14, &FONT_S, BTN_LABEL[b], K_TEXT);
        if (i < (int)NCARDS) {
            cv_text(18 + w + 10, cap_y(12, 20, &FONT_B), &FONT_B, CARD[i].title, INK[0]);
            cv_rect(18, 38, 204, 1, K_FAINT);
            y = wrap(20, 42, 200, &FONT_S, CARD[i].l1, K_TEXT);
            if (CARD[i].l2)
                wrap(20, y + 2, 200, &FONT_S, CARD[i].l2, K_DIM);
        }
    }
    cv_blit(0, MAIN_Y);
}

/* SAVE held: the 16 slots (the white keys), what each holds, and what pressing one does */
static void draw_slots(void)
{
    char t[48], n[24];
    uint32_t h = hash_u(0x5A5Eu, (uint32_t)set.sound | (uint32_t)ui.save_pick << 8);
    int pick = ui.save_pick;
    for (int i = 0; i < NUSER; i++)
        h = hash_u(h, (uint32_t)user_used(i));
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    t[0] = 0;                             /* (the sound is in the header: here, where it goes) */
    num_i(pick + 1, s_cat(t, "keep it in U"));
    text_fit(8, 2, &FONT_B, t, 224, INK[0]);
    t[0] = 0;
    if (user_used(pick))
        s_put(t, sizeof t, "replacing ", bank[pick / 8].s[pick % 8].name, 0, 0);
    else
        s_cat(t, "an empty slot");
    text_fit(8, 20, &FONT_XS, t, 224, K_DIM);
    for (int i = 0; i < NUSER; i++) {
        int32_t x = 6 + (i % 4) * 57, y = 38 + (i / 4) * 19;
        int used = user_used(i), on = i == pick;
        cv_round(x, y, 54, 17, 4, on ? K_TEXT : used ? K_PANEL2 : K_PANEL);
        n[0] = 0;
        num_i(i + 1, n);
        cv_text(x + 4, cap_y(y, 17, &FONT_XS), &FONT_XS, n, on ? K_BG : K_DIM);
        text_fit(x + 18, cap_y(y, 17, &FONT_XS), &FONT_XS, used ? bank[i / 8].s[i % 8].name : "empty", 34,
                 on ? K_BG : used ? K_TEXT : K_FAINT);
    }
    cv_blit(0, MAIN_Y);
}

static void draw(void)
{
    draw_header();
    if (ui.saving) {                       /* the slots open */
        draw_slots();
        draw_title();
        draw_knobs();
    } else if (ui.card) {
        draw_card();
        draw_title();
        draw_knobs();
    } else if (ui.list)
        draw_list();
    else {
        uint32_t t0 = plat_ticks24(), s1 = ui.sig[1];
        static uint32_t last;
        draw_main();
        if (ui.sig[1] != s1) {            /* (drawn this frame) */
            uint32_t now = plat_ms();
            pic_us = (pic_us * 7u + (plat_ticks24() - t0) / PLAT_TICKS_PER_US) / 8u;
            pic_every = (pic_every * 7u + (now - last)) / 8u;
            last = now;
        }
        draw_title();
        draw_knobs();
    }
    draw_footer();
}
