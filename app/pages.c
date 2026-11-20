/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Chordism's pages: which button opens which, their four knobs, how each value reads. LOOM-8's grammar: a button opens
 * its first page, SELECT turns its pages, PRESET steps the sounds (or the page's type), ALGORITHM the chord, SEL lists
 * every setting of the button, a button held alone explains itself. Part of the unity build (app.c), before ui.c. */

enum { P_CHORD, P_VOICING, P_SCALE, P_MAP, P_INTERVALS, P_OSC, P_FM, P_ENV, P_FILTER, P_FENV,
       P_SLFO, P_VIB, P_SWEEP, P_FLFO, P_TREM, P_PLATE, P_SHIMMER, P_DELAY, P_LOFI, P_ARP, P_RHYTHM, P_CLOCK, P_MORPH,
       P_PANM, P_CTRL, P_CTRL2, P_SETUP, P_MIDI, NPAGES };

/* the app's own settings, as knobs (beyond the engine's parameters) */
enum { K_NONE = -1, K_LIGHTS = 200, K_VELO, K_PRESS, K_MIDIOUT, K_MAPCHORD,
       K_VWAVE, K_VSHAPE, K_VMIX, K_VFM, K_MIDIIN_CH, K_MIDIOUT_CH };   /* (K_V*: the voice PRESET picked on Oscillators, or all four) */
static int par_of(int k);                  /* (ui.c: a knob's parameter) */
/* the pictures */
enum { PIC_CURTAINS, PIC_WAVE, PIC_ENV, PIC_FILTER, PIC_LFO, PIC_SCALE, PIC_MAP, PIC_BARS, PIC_ARP, PIC_SPACE, PIC_CTRL,
       PIC_VOICES, PIC_PLATE, PIC_DELAY, PIC_CLOCK };
/* what PRESET turns on a page */
enum { PR_SOUND, PR_VOICE, PR_FTYPE, PR_DMODE, PR_LSHAPE };

typedef struct {
    uint8_t btn;
    const char *name;
    int16_t k[4];
    uint8_t pic, preset;
} page_t;

static const page_t PAGE[NPAGES] = {
    {B_HOME, "Chord", {CP_CHORD, CP_SPREAD, CP_WIDTH, CP_VOLUME}, PIC_CURTAINS, PR_SOUND},
    {B_HOME, "Voicing", {CP_ROTATION, CP_DETUNE, CP_GLIDE, CP_LEGATO}, PIC_VOICES, PR_SOUND},
    {B_SEQ, "Scale", {CP_TUNING, CP_SCALE, CP_ROOT, K_NONE}, PIC_SCALE, PR_SOUND},
    {B_SEQ, "Chord map", {K_MAPCHORD, K_NONE, K_NONE, K_NONE}, PIC_MAP, PR_SOUND},
    {B_SEQ, "Intervals", {CP_INT1, CP_INT2, CP_INT3, K_NONE}, PIC_CURTAINS, PR_SOUND},
    {B_EDIT, "Oscillators", {K_VWAVE, K_VSHAPE, K_VMIX, K_VFM}, PIC_WAVE, PR_VOICE},
    {B_EDIT, "FM", {CP_FM_AMT, CP_FM_MOD, CP_FM_POS, K_NONE}, PIC_BARS, PR_SOUND},
    {B_ENV, "Envelope", {CP_ATTACK, CP_RELEASE, CP_VCA_MODE, CP_DRONE}, PIC_ENV, PR_SOUND},
    {B_ENV, "Filter", {CP_CUTOFF, CP_RESO, CP_DRIVE, CP_SLOPE}, PIC_FILTER, PR_FTYPE},
    {B_ENV, "Filter env", {CP_FENV_A, CP_FENV_D, CP_FENV_AMT, CP_FENV_MODE}, PIC_ENV, PR_SOUND},
    {B_LFO, "Shape LFO", {CP_LFO_RATE, CP_LFO_DEPTH, CP_LFO_SHAPE, CP_LFO_MODE}, PIC_LFO, PR_LSHAPE},
    {B_LFO, "Vibrato", {CP_VIB_SPEED, CP_VIB_DEPTH, CP_VIB_DELAY, CP_VIB_STRAY}, PIC_LFO, PR_SOUND},
    {B_LFO, "Sweep", {CP_SWEEP_AMT, CP_SWEEP_RATE, K_NONE, K_NONE}, PIC_ENV, PR_SOUND},
    {B_LFO, "Filter LFO", {CP_FLFO_RATE, CP_FLFO_DEPTH, CP_FLFO_SPREAD, CP_FLFO_SHAPE}, PIC_LFO, PR_LSHAPE},
    {B_LFO, "Tremolo", {CP_ALFO_RATE, CP_ALFO_DEPTH, CP_ALFO_SHAPE, K_NONE}, PIC_LFO, PR_LSHAPE},
    {B_FX, "Plate", {CP_RV_MIX, CP_RV_DECAY, CP_RV_DAMP, CP_RV_SIZE}, PIC_PLATE, PR_SOUND},
    {B_FX, "Shimmer", {CP_RV_SHIMMER, CP_RV_LOWCUT, CP_RV_MODRATE, CP_RV_MODDEPTH}, PIC_PLATE, PR_SOUND},
    {B_FX, "Delay", {CP_DL_MIX, CP_DL_TIME, CP_DL_FB, CP_DL_TONE}, PIC_DELAY, PR_DMODE},
    {B_FX, "Lo-fi", {CP_GRIND, CP_SHIFT, CP_DECIM, CP_LOFI_POS}, PIC_WAVE, PR_SOUND},
    {B_ARP, "Arp", {CP_ARP, CP_ARP_TEMPO, CP_ARP_DIR, CP_ARP_HOLD}, PIC_ARP, PR_SOUND},
    {B_ARP, "Rhythm", {CP_ARP_STEPS, CP_ARP_BEATS, CP_ARP_VINT, CP_ARP_VARS}, PIC_ARP, PR_SOUND},
    {B_ARP, "Clock", {CP_ARP_SYNC, CP_ARP_DIV, K_NONE, K_NONE}, PIC_CLOCK, PR_SOUND},
    {B_GLO, "Morph", {CP_MORPH, CP_MORPH_INT, CP_LM_RATE, CP_LM_DEPTH}, PIC_BARS, PR_SOUND},
    {B_GLO, "Pan morph", {CP_PAN, CP_PAN_INT, CP_PM_RATE, CP_PM_DEPTH}, PIC_BARS, PR_SOUND},
    {B_GLO, "Control", {CP_CTRL_SRC, CP_CTRL_CC, CP_CT_CUTOFF, CP_CT_MORPH}, PIC_CTRL, PR_SOUND},
    {B_GLO, "Control 2", {CP_CT_VIB, CP_CT_SHAPE, CP_CT_FM, K_NONE}, PIC_CTRL, PR_SOUND},
    {B_GLO, "Setup", {K_LIGHTS, K_VELO, K_PRESS, K_MIDIOUT}, PIC_CURTAINS, PR_SOUND},
    {B_GLO, "MIDI", {K_MIDIIN_CH, K_MIDIOUT_CH, K_NONE, K_NONE}, PIC_CURTAINS, PR_SOUND},
};

/* SEL's list: every setting a button's pages hold, then the ones no page shows */
static const int16_t EXTRA_EDIT[] = {CP_WAVE1, CP_SHAPE1, CP_MIX1, CP_FM1, CP_WAVE2, CP_SHAPE2, CP_MIX2, CP_FM2, CP_WAVE3,
                                     CP_SHAPE3, CP_MIX3, CP_FM3, CP_WAVE4, CP_SHAPE4, CP_MIX4, CP_FM4, K_NONE};
static const int16_t EXTRA_ENV[] = {CP_VCA_HARD, CP_FENV_HARD, CP_FMODE, K_NONE};
static const int16_t EXTRA_LFO[] = {CP_LFO_PH1, CP_LFO_PH2, CP_LFO_PH3, CP_LFO_PH4, CP_VIB_OSC, CP_SWEEP_OSC, CP_FLFO_MODE,
                                    K_NONE};
static const int16_t EXTRA_FX[] = {CP_DL_MODE, CP_DL_SHIFT, K_NONE};
static const int16_t EXTRA_GLO[] = {CP_LM_SHAPE, CP_LM_MODE, CP_PM_SHAPE, CP_PM_MODE, K_NONE};
static const int16_t EXTRA_SEQ[] = {CP_PC0, CP_PC1, CP_PC2, CP_PC3, CP_PC4, CP_PC5, CP_PC6, CP_PC7, CP_PC8, CP_PC9,
                                    CP_PC10, CP_PC11, K_NONE};

static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                          "PLAY", "REC", "OCT-", "OCT+"};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

/* ------------------------------------------------------------- names --- */
static const char *knob_name(int k)
{
    switch (k) {
    case CP_CHORD: return "chord";
    case CP_SPREAD: return "spread";
    case CP_ROTATION: return "rotate";
    case CP_DETUNE: return "detune";
    case CP_WIDTH: return "width";
    case CP_TUNING: return "tuning";
    case CP_INT1: return "voice 2";
    case CP_INT2: return "voice 3";
    case CP_INT3: return "voice 4";
    case CP_SCALE: return "scale";
    case CP_ROOT: return "root";
    case CP_WAVE1: return "wave 1";
    case CP_WAVE2: return "wave 2";
    case CP_WAVE3: return "wave 3";
    case CP_WAVE4: return "wave 4";
    case CP_SHAPE1: return "shape 1";
    case CP_SHAPE2: return "shape 2";
    case CP_SHAPE3: return "shape 3";
    case CP_SHAPE4: return "shape 4";
    case CP_MIX1: return "level 1";
    case CP_MIX2: return "level 2";
    case CP_MIX3: return "level 3";
    case CP_MIX4: return "level 4";
    case CP_FM_MOD: return "fm from";
    case CP_FM_AMT: return "fm";
    case CP_FM_POS: return "fm after";
    case CP_FM1: return "fm 1";
    case CP_FM2: return "fm 2";
    case CP_FM3: return "fm 3";
    case CP_FM4: return "fm 4";
    case CP_MORPH: return "pattern";
    case CP_MORPH_INT: return "amount";
    case CP_PAN: return "pattern";
    case CP_PAN_INT: return "amount";
    case CP_LFO_RATE: case CP_FLFO_RATE: case CP_ALFO_RATE: case CP_LM_RATE: case CP_PM_RATE: return "rate";
    case CP_LFO_DEPTH: case CP_FLFO_DEPTH: case CP_ALFO_DEPTH: case CP_LM_DEPTH: case CP_PM_DEPTH: return "depth";
    case CP_LFO_SHAPE: case CP_FLFO_SHAPE: case CP_ALFO_SHAPE: return "shape";
    case CP_LM_SHAPE: return "morph lfo";
    case CP_PM_SHAPE: return "pan lfo";
    case CP_LFO_MODE: return "mode";
    case CP_FLFO_MODE: return "filter lfo";
    case CP_LM_MODE: return "morph mode";
    case CP_PM_MODE: return "pan mode";
    case CP_LFO_PH1: return "phase 1";
    case CP_LFO_PH2: return "phase 2";
    case CP_LFO_PH3: return "phase 3";
    case CP_LFO_PH4: return "phase 4";
    case CP_VIB_SPEED: return "speed";
    case CP_VIB_DEPTH: return "depth";
    case CP_VIB_DELAY: return "delay";
    case CP_VIB_STRAY: return "stray";
    case CP_VIB_OSC: return "vib voices";
    case CP_SWEEP_AMT: return "amount";
    case CP_SWEEP_RATE: return "time";
    case CP_SWEEP_OSC: return "sweep voices";
    case CP_GLIDE: return "glide";
    case CP_LEGATO: return "legato";
    case CP_CUTOFF: return "cutoff";
    case CP_RESO: return "reso";
    case CP_FMODE: return "type";
    case CP_SLOPE: return "slope";
    case CP_DRIVE: return "drive";
    case CP_FENV_A: return "attack";
    case CP_FENV_D: return "decay";
    case CP_FENV_AMT: return "amount";
    case CP_FENV_MODE: return "mode";
    case CP_FENV_HARD: return "filter retrig";
    case CP_FLFO_SPREAD: return "spread";
    case CP_ATTACK: return "attack";
    case CP_RELEASE: return "release";
    case CP_VOLUME: return "volume";
    case CP_VCA_MODE: return "mode";
    case CP_VCA_HARD: return "amp retrig";
    case CP_DRONE: return "drone";
    case CP_RV_MIX: return "mix";
    case CP_RV_DECAY: return "decay";
    case CP_RV_DAMP: return "damp";
    case CP_RV_SHIMMER: return "shimmer";
    case CP_RV_LOWCUT: return "low cut";
    case CP_RV_SIZE: return "size";
    case CP_RV_MODRATE: return "mod rate";
    case CP_RV_MODDEPTH: return "mod depth";
    case CP_GRIND: return "grind";
    case CP_SHIFT: return "shift";
    case CP_DECIM: return "decimate";
    case CP_LOFI_POS: return "where";
    case CP_DL_MIX: return "mix";
    case CP_DL_TIME: return "time";
    case CP_DL_FB: return "feedback";
    case CP_DL_TONE: return "tone";
    case CP_DL_MODE: return "delay mode";
    case CP_DL_SHIFT: return "interval";
    case CP_ARP: return "arp";
    case CP_ARP_TEMPO: return "tempo";
    case CP_ARP_DIR: return "order";
    case CP_ARP_HOLD: return "hold";
    case CP_ARP_STEPS: return "steps";
    case CP_ARP_BEATS: return "beats";
    case CP_ARP_VINT: return "shift";
    case CP_ARP_VARS: return "repeats";
    case CP_ARP_SYNC: return "clock";
    case CP_ARP_DIV: return "rate";
    case CP_CTRL_SRC: return "source";
    case CP_CTRL_CC: return "cc";
    case CP_CT_CUTOFF: return "cutoff";
    case CP_CT_MORPH: return "morph";
    case CP_CT_VIB: return "vibrato";
    case CP_CT_SHAPE: return "shape";
    case CP_CT_FM: return "fm";
    case K_LIGHTS: return "lights";
    case K_VELO: return "velocity";
    case K_PRESS: return "pressure";
    case K_MIDIOUT: return "midi out";
    case K_MAPCHORD: return "chord";
    case K_VWAVE: return "wave";
    case K_VSHAPE: return "shape";
    case K_VMIX: return "level";
    case K_VFM: return "fm";
    case K_MIDIIN_CH: return "in";
    case K_MIDIOUT_CH: return "out";
    default:
        if (k >= CP_PC0 && k <= CP_PC11) {
            static const char *const PCN[12] = {"C chord", "C# chord", "D chord", "Eb chord", "E chord", "F chord",
                                                "F# chord", "G chord", "Ab chord", "A chord", "Bb chord", "B chord"};
            return PCN[k - CP_PC0];
        }
        return "";
    }
}

static char *s_cat(char *b, const char *s)
{
    while (*b)
        b++;
    while (*s)
        *b++ = *s++;
    *b = 0;
    return b;
}
static char *num_i(int32_t v, char *b)              /* signed, at b; returns the end */
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}
static char *num_dec(int32_t v10, char *b)          /* tenths: "2.5", "12" past 10 */
{
    if (v10 < 0) {
        *b++ = '-';
        v10 = -v10;
    }
    if (v10 >= 100)
        return num_i((v10 + 5) / 10, b);
    b = num_i(v10 / 10, b);
    *b++ = '.';
    return num_i(v10 % 10, b);
}
static void hz_text(float hz, char *t)              /* "0.25 Hz", "12 Hz", "2.4 kHz" */
{
    char *e = t;
    *t = 0;
    if (hz >= 1000.0f) {
        e = num_dec((int32_t)(hz / 100.0f + 0.5f), t);
        s_cat(e, " kHz");
    } else if (hz >= 10.0f) {
        e = num_i((int32_t)(hz + 0.5f), t);
        s_cat(e, " Hz");
    } else if (hz >= 1.0f) {
        e = num_dec((int32_t)(hz * 10.0f + 0.5f), t);
        s_cat(e, " Hz");
    } else {
        int32_t c = (int32_t)(hz * 100.0f + 0.5f);
        e = s_cat(t, "0.");
        if (c < 10)
            e = s_cat(e, "0");
        e = num_i(c, e);
        s_cat(e, " Hz");
    }
}
static void secs_text(float s, char *t)             /* "4 ms", "350 ms", "1.2 s" */
{
    *t = 0;
    if (s < 1.0f)
        s_cat(num_i((int32_t)(s * 1000.0f + 0.5f), t), " ms");
    else
        s_cat(num_dec((int32_t)(s * 10.0f + 0.5f), t), " s");
}
static float u01v(int v) { return (float)v * 0.0001f; }
static float exp_map(float v01, float lo, float hi) { return lo * fm_powf(hi / lo, v01); }

static void mask_text(int m, char *t)               /* the voices a bit mask names: "1234", "1-3-" */
{
    for (int i = 0; i < 4; i++)
        t[i] = (char)(m >> i & 1 ? '1' + i : '-');
    t[4] = 0;
}

/* the morphs' patterns, by name: the levels (Morph) and the places (Pan morph), the original's 16 each */
static const char *const LEVEL_PAT[16] = {"all equal", "ramp up", "ramp down", "root only", "top only", "root + top",
                                          "inner pair", "odd", "even", "log up", "middle", "outer", "top heavy",
                                          "bottom heavy", "low pair", "high pair"};
static const char *const PAN_PAT[16] = {"center", "L to R", "R to L", "L R L R", "R L R L", "split", "outer wide",
                                        "inner wide", "zigzag", "zagzig", "wide ramp", "kick right", "kick left",
                                        "first wide", "last wide", "outside in"};

static int shape_wave(int k);              /* (ui.c: the wave a shape knob's voice plays) */

/* a parameter's value as the strip shows it */
static void value_text(int k, int v, char *t)
{
    static const char *const MODE3[3] = {"AD", "ASR", "loop"};
    static const char *const LSHAPE[4] = {"tri", "ramp up", "ramp down", "square"};
    static const char *const LMODE[3] = {"free", "retrig", "once"};
    static const char *const FTYPE[3] = {"low pass", "high pass", "band pass"};
    static const char *const DMODE[6] = {"stereo", "ping-pong", "flip-flop", "long", "zenith", "interval"};
    static const char *const ADIR[4] = {"up", "down", "up-down", "random"};
    static const char *const DIV[6] = {"1/4", "1/4T", "1/8", "1/8T", "1/16", "1/32"};
    static const char *const SRC[5] = {"pressure", "random", "coin toss", "cc", "velocity"};
    static const char *const TUNE[3] = {"chord", "interval", "map"};
    *t = 0;
    switch (k) {
    case CP_CHORD: s_cat(t, CHD_CHORD_NAME[v % CHD_NCHORD]); return;
    case CP_TUNING: s_cat(t, TUNE[v % 3]); return;
    case CP_SCALE: s_cat(t, CHD_SCALE_NAME[v % CHD_NSCALE]); return;
    case CP_ROOT: s_cat(t, NOTE_NAME[v % 12]); return;
    case CP_INT1: case CP_INT2: case CP_INT3: case CP_ARP_VINT:
        if (v > 0)
            *t = '+', t[1] = 0;
        s_cat(num_i(v, t + (v > 0)), " st");
        return;
    case CP_WAVE1: case CP_WAVE2: case CP_WAVE3: case CP_WAVE4: s_cat(t, CHD_WAVE_NAME[v % CHD_NWAVE]); return;
    case CP_FM_MOD: s_cat(num_i(v + 1, s_cat(t, "voice ")), ""); return;
    case CP_FM_POS: s_cat(t, v ? "morph" : "before"); return;
    case CP_DETUNE: s_cat(num_i((v * 50 + 5000) / 10000, t), " ct"); return;
    case CP_SPREAD: s_cat(num_i((v * 2 + 50) / 100, t), "%"); return;   /* (100 %: the chord as written) */
    case CP_ROTATION: {                   /* (the original's 3.999 x: four steps) */
        static const char *const ROT[4] = {"none", "by 1", "by 2", "by 3"};
        s_cat(t, ROT[(int)((float)v * 0.0003999f) & 3]);
        return;
    }
    case CP_SHAPE1: case CP_SHAPE2: case CP_SHAPE3: case CP_SHAPE4: case K_VSHAPE: {   /* what it does to the wave */
        int w = shape_wave(k);
        if (w == 4)
            s_cat(num_i((500 + v * 90 / 100 + 50) / 100, t), "% pw");        /* square: 5 .. 95 % */
        else if (w == 5)
            s_cat(num_i((200 + v * 28 / 100 + 50) / 100, t), "% pw");        /* pulse: 2 .. 30 % */
        else if (w == 6) {
            int n = 1 + v * 7 / 10000;
            s_cat(num_i(n, t), n == 1 ? " partial" : " partials");
        } else
            s_cat(num_i((v + 50) / 100, t), "%");
        return;
    }
    case CP_VIB_DEPTH: s_cat(num_i((v + 50) / 100, t), " ct"); return;   /* (100 cents at the top) */
    case CP_LFO_PH1: case CP_LFO_PH2: case CP_LFO_PH3: case CP_LFO_PH4: s_cat(num_i((v * 360 + 5000) / 10000, t), " deg"); return;
    case CP_RV_SHIMMER:                   /* below the middle the plate's chorus; above it octaves too */
        if (v <= 5000)                    /* (the chorus's share; past the middle, "+oct" and the octaves' share) */
            s_cat(num_i((v + 25) / 50, t), "%");
        else
            s_cat(num_i((v - 5000 + 25) / 50, s_cat(t, "+oct ")), "%");
        return;
    case CP_RV_MODRATE: hz_text(0.4f + 2.5f * u01v(v), t); return;
    case CP_GRIND:
        if (!v)
            s_cat(t, "off");
        else
            s_cat(num_i((int32_t)(16.0f - 14.0f * u01v(v) + 0.5f), t), " bits");
        return;
    case CP_DECIM: {
        int hold = (int)(u01v(v) * 32.0f);
        if (!v)
            s_cat(t, "off");
        else
            hz_text(44100.0f / (float)(hold < 1 ? 1 : hold), t);
        return;
    }
    case CP_LFO_SHAPE: case CP_FLFO_SHAPE: case CP_ALFO_SHAPE: case CP_LM_SHAPE: case CP_PM_SHAPE:
        s_cat(t, LSHAPE[v % 4]);
        return;
    case CP_LFO_MODE: case CP_FLFO_MODE: case CP_LM_MODE: case CP_PM_MODE: s_cat(t, LMODE[v % 3]); return;
    case CP_VCA_MODE: case CP_FENV_MODE: s_cat(t, MODE3[v % 3]); return;
    case CP_FMODE: s_cat(t, FTYPE[v % 3]); return;
    case CP_SLOPE: s_cat(t, v ? "24 dB" : "12 dB"); return;
    case CP_DL_MODE: s_cat(t, DMODE[v % 6]); return;
    case CP_ARP_DIR: s_cat(t, ADIR[v % 4]); return;
    case CP_ARP_DIV: s_cat(t, DIV[v % 6]); return;
    case CP_ARP_SYNC: s_cat(t, v ? "MIDI" : "own"); return;
    case CP_CTRL_SRC: s_cat(t, SRC[v % 5]); return;
    case CP_LOFI_POS: s_cat(t, v ? "before" : "after"); return;
    case CP_VIB_OSC: case CP_SWEEP_OSC: mask_text(v, t); return;
    case CP_ARP_STEPS: case CP_ARP_BEATS: case CP_ARP_VARS: case CP_CTRL_CC: num_i(v, t); return;
    case CP_ARP: case CP_ARP_HOLD: case CP_DRONE: case CP_LEGATO: case CP_VIB_STRAY: case CP_VCA_HARD: case CP_FENV_HARD:
    case K_VELO: case K_PRESS: case K_MIDIOUT:
        s_cat(t, v ? "on" : "off");
        return;
    case K_LIGHTS: {
        static const char *const L[3] = {"on", "keys", "off"};
        s_cat(t, L[v % 3]);
        return;
    }
    case K_MAPCHORD: s_cat(t, CHD_CHORD_NAME[v % CHD_NCHORD]); return;
    case K_MIDIIN_CH:                     /* (the channels: "any" or "ch 3") */
        if (!v)
            s_cat(t, "any");
        else
            num_i(v, s_cat(t, "ch "));
        return;
    case K_MIDIOUT_CH: num_i(v + 1, s_cat(t, "ch ")); return;
    case CP_MORPH: s_cat(t, LEVEL_PAT[(v * 15 + 5000) / 10000]); return;   /* (the nearest pattern) */
    case CP_PAN: s_cat(t, PAN_PAT[(v * 15 + 5000) / 10000]); return;
    case CP_CUTOFF: hz_text(20.0f * fm_exp2f(9.96578428f * u01v(v)), t); return;
    case CP_LFO_RATE: case CP_FLFO_RATE: case CP_ALFO_RATE: case CP_LM_RATE: case CP_PM_RATE:
        hz_text(exp_map(u01v(v), 0.02f, 50.0f), t);
        return;
    case CP_VIB_SPEED: hz_text(exp_map(u01v(v), 0.1f, 12.0f), t); return;
    case CP_ATTACK: case CP_RELEASE: case CP_FENV_A: case CP_FENV_D: secs_text(exp_map(u01v(v), 0.001f, 16.0f), t); return;
    case CP_VIB_DELAY: secs_text(u01v(v) * 2.0f, t); return;
    case CP_GLIDE: secs_text(u01v(v) * 2.0f, t); return;
    case CP_SWEEP_RATE: secs_text(4.0f / fm_powf(400.0f, u01v(v)), t); return;
    case CP_SWEEP_AMT: {
        int32_t st10 = v * 12 / 1000;            /* tenths of a semitone */
        if (v > 0)
            *t = '+', t[1] = 0;
        s_cat(num_dec(st10, t + (v > 0)), " st");
        return;
    }
    case CP_DL_TIME: secs_text(0.005f + u01v(v) * 1.395f, t); return;
    case CP_DL_SHIFT: {
        int32_t st = (v * 24 + 5000) / 10000 - 12;
        if (st > 0)
            *t = '+', t[1] = 0;
        s_cat(num_i(st, t + (st > 0)), " st");
        return;
    }
    case CP_ARP_TEMPO: s_cat(num_i((int32_t)(15.0f + 105.0f * u01v(v) + 0.5f), t), " bpm"); return;   /* (the original's 30..240 steps a minute are eighths: chd.c arp_recompute) */
    case CP_FENV_AMT:
        if (v > 0)
            *t = '+', t[1] = 0;
        s_cat(num_i(v / 100, t + (v > 0)), "%");
        return;
    default:
        if (k >= CP_PC0 && k <= CP_PC11) {
            s_cat(t, CHD_CHORD_NAME[v % CHD_NCHORD]);
            return;
        }
        s_cat(num_i((v + 50) / 100, t), "%");
        return;
    }
}

/* the help cards: a button held alone a second says what it does */
static const struct { uint8_t btn; const char *title, *l1, *l2; } CARD[] = {
    {B_HOME, "Chord", "Each key plays a chord. Turn ALGORITHM for its type,",
     "PRESET for the next sound. Hold HOME and turn a knob to set it finely."},
    {B_SEQ, "Harmony", "The scale the chords snap to, a chord for each note of it,",
     "and your own intervals. On Chord map, press a key to pick its note."},
    {B_EDIT, "Oscillators", "Turn PRESET to pick a voice, or all four. Its wave, shape,",
     "level and FM. Press EDIT again for FM: which voice modulates the others."},
    {B_ENV, "Envelope", "The voices' envelope, the filter, then the filter's envelope.",
     "Press harder on a key to open the filter."},
    {B_LFO, "Movement", "The shape LFO, vibrato, a pitch sweep at each chord,",
     "the filter's LFO and tremolo."},
    {B_FX, "Effects", "The plate, its shimmer, a delay (PRESET picks its mode)", "and lo-fi."},
    {B_ARP, "Arp", "PLAY turns the arp on and off. It steps through the keys you hold,",
     "in a Euclidean rhythm on Rhythm."},
    {B_GLO, "Morph", "Morph moves the voices' levels, Pan morph their places.",
     "Control routes pressure, velocity or a CC. Setup is last."},
    {B_SEL, "Lists", "Every setting of this button, the hidden ones too. On HOME, every sound.",
     "Turn SELECT to move, ALGORITHM to change it. Press SEL to leave."},
    {B_SAVE, "Save", "Tap to keep this sound and your settings for the next start.",
     "Hold it to open your 16 slots, press a white key to pick one, then tap SAVE."},
    {B_PLAY, "Arp", "Press to turn the arp on or off.", 0},
    {B_REC, "Bluetooth", "Press to look for Bluetooth MIDI devices, again to connect.",
     "Hold for a second to turn Bluetooth off or on."},
    {B_OCTDN, "Octave", "Press for the keys an octave lower.", 0},
    {B_OCTUP, "Octave", "Press for the keys an octave higher.", 0},
};
#define NCARDS (sizeof CARD / sizeof CARD[0])
