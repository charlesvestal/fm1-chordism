/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Charles Vestal */
/* Chordism's engine on the FM-1: the Chordism module for Schwung (schwung-chordism, MIT) in C. One note in, four
 * voices out: a chord from the note (a chord type, your own intervals, or a chord for each pitch class), snapped to a
 * scale, through a filter, drive, lo-fi, a delay and a plate. The main loop sends notes, pressure and parameter
 * changes through a queue; the audio interrupt takes them at the start of each block (chd_render). */
#pragma once
#include <stdint.h>

#define CHD_SR 44100.0f
/* #define CHD_PROF 1: a measurement build (each pass timed, on the Setup page) */
#define CHD_NCHORD 16
#define CHD_NSCALE 25
#define CHD_NWAVE 7
#define CHD_NPRESET 57

/* The parameters, each an integer: a value 0..1 in the original is 0..10000 here (its four decimals, as its state
 * kept them), a bipolar one -10000..10000; the rest are counts and choices. X(id, the original's key, min, max,
 * default): the default is the original's own (create_instance) */
#define CHD_PARAMS(X) \
    X(CP_CHORD, "chord_type", 0, 15, 6) \
    X(CP_SPREAD, "chord_spread", 0, 10000, 5000) \
    X(CP_ROTATION, "chord_rotation", 0, 10000, 0) \
    X(CP_DETUNE, "detune", 0, 10000, 0) \
    X(CP_WIDTH, "width", 0, 10000, 10000) \
    X(CP_TUNING, "tuning_mode", 0, 2, 0) \
    X(CP_INT1, "interval_1", -24, 24, 4) \
    X(CP_INT2, "interval_2", -24, 24, 7) \
    X(CP_INT3, "interval_3", -24, 24, 12) \
    X(CP_SCALE, "scale_index", 0, 24, 0) \
    X(CP_ROOT, "scale_root", 0, 11, 0) \
    X(CP_PC0, "chord_pc_0", 0, 15, 6) \
    X(CP_PC1, "chord_pc_1", 0, 15, 6) \
    X(CP_PC2, "chord_pc_2", 0, 15, 6) \
    X(CP_PC3, "chord_pc_3", 0, 15, 6) \
    X(CP_PC4, "chord_pc_4", 0, 15, 6) \
    X(CP_PC5, "chord_pc_5", 0, 15, 6) \
    X(CP_PC6, "chord_pc_6", 0, 15, 6) \
    X(CP_PC7, "chord_pc_7", 0, 15, 6) \
    X(CP_PC8, "chord_pc_8", 0, 15, 6) \
    X(CP_PC9, "chord_pc_9", 0, 15, 6) \
    X(CP_PC10, "chord_pc_10", 0, 15, 6) \
    X(CP_PC11, "chord_pc_11", 0, 15, 6) \
    X(CP_WAVE1, "wave_1", 0, 6, 1) \
    X(CP_WAVE2, "wave_2", 0, 6, 1) \
    X(CP_WAVE3, "wave_3", 0, 6, 1) \
    X(CP_WAVE4, "wave_4", 0, 6, 1) \
    X(CP_SHAPE1, "shape_1", 0, 10000, 0) \
    X(CP_SHAPE2, "shape_2", 0, 10000, 0) \
    X(CP_SHAPE3, "shape_3", 0, 10000, 0) \
    X(CP_SHAPE4, "shape_4", 0, 10000, 0) \
    X(CP_MIX1, "mix_1", 0, 10000, 10000) \
    X(CP_MIX2, "mix_2", 0, 10000, 10000) \
    X(CP_MIX3, "mix_3", 0, 10000, 10000) \
    X(CP_MIX4, "mix_4", 0, 10000, 10000) \
    X(CP_FM_MOD, "fm_modulator", 0, 3, 0) \
    X(CP_FM_AMT, "fm_amount", 0, 10000, 0) \
    X(CP_FM_POS, "fm_position", 0, 1, 0) \
    X(CP_FM1, "fm_amount_1", 0, 10000, 5000) \
    X(CP_FM2, "fm_amount_2", 0, 10000, 5000) \
    X(CP_FM3, "fm_amount_3", 0, 10000, 5000) \
    X(CP_FM4, "fm_amount_4", 0, 10000, 5000) \
    X(CP_MORPH, "morph_index", 0, 10000, 0) \
    X(CP_MORPH_INT, "morph_intensity", 0, 10000, 0) \
    X(CP_PAN, "pan_morph_index", 0, 10000, 0) \
    X(CP_PAN_INT, "pan_morph_intensity", 0, 10000, 0) \
    X(CP_LFO_RATE, "lfo_rate", 0, 10000, 0) \
    X(CP_LFO_DEPTH, "lfo_depth", 0, 10000, 0) \
    X(CP_LFO_SHAPE, "lfo_shape", 0, 3, 0) \
    X(CP_LFO_MODE, "shape_lfo_mode", 0, 2, 0) \
    X(CP_LFO_PH1, "lfo_phase_1", 0, 10000, 0) \
    X(CP_LFO_PH2, "lfo_phase_2", 0, 10000, 2500) \
    X(CP_LFO_PH3, "lfo_phase_3", 0, 10000, 5000) \
    X(CP_LFO_PH4, "lfo_phase_4", 0, 10000, 7500) \
    X(CP_VIB_SPEED, "vib_speed", 0, 10000, 5000) \
    X(CP_VIB_DEPTH, "vib_depth", 0, 10000, 0) \
    X(CP_VIB_DELAY, "vib_delay", 0, 10000, 2000) \
    X(CP_VIB_STRAY, "vib_stray", 0, 1, 0) \
    X(CP_VIB_OSC, "vib_osc_enable", 0, 15, 15) \
    X(CP_SWEEP_AMT, "sweep_amount", -10000, 10000, 0) \
    X(CP_SWEEP_RATE, "sweep_rate", 0, 10000, 5000) \
    X(CP_SWEEP_OSC, "sweep_osc_enable", 0, 15, 15) \
    X(CP_GLIDE, "glide_rate", 0, 10000, 0) \
    X(CP_LEGATO, "glide_legato", 0, 1, 0) \
    X(CP_CUTOFF, "filter_cutoff", 0, 10000, 10000) \
    X(CP_RESO, "filter_resonance", 0, 10000, 0) \
    X(CP_FMODE, "filter_mode", 0, 2, 0) \
    X(CP_SLOPE, "filter_slope", 0, 1, 0) \
    X(CP_DRIVE, "drive", 0, 10000, 0) \
    X(CP_FENV_A, "filter_env_attack", 0, 10000, 0) \
    X(CP_FENV_D, "filter_env_decay", 0, 10000, 3000) \
    X(CP_FENV_AMT, "filter_env_depth", -10000, 10000, 0) \
    X(CP_FENV_MODE, "fenv_mode", 0, 2, 0) \
    X(CP_FENV_HARD, "fenv_hard_reset", 0, 1, 0) \
    X(CP_FLFO_RATE, "filter_lfo_rate", 0, 10000, 0) \
    X(CP_FLFO_DEPTH, "filter_lfo_depth", 0, 10000, 0) \
    X(CP_FLFO_SPREAD, "filter_lfo_spread", 0, 10000, 0) \
    X(CP_FLFO_SHAPE, "filter_lfo_shape", 0, 3, 0) \
    X(CP_FLFO_MODE, "filter_lfo_mode", 0, 2, 0) \
    X(CP_ATTACK, "attack", 0, 10000, 500) \
    X(CP_RELEASE, "release", 0, 10000, 3000) \
    X(CP_VOLUME, "volume", 0, 10000, 8000) \
    X(CP_VCA_MODE, "vca_mode", 0, 2, 1) \
    X(CP_VCA_HARD, "vca_hard_reset", 0, 1, 0) \
    X(CP_DRONE, "vca_drone", 0, 1, 0) \
    X(CP_ALFO_RATE, "amp_lfo_rate", 0, 10000, 0) \
    X(CP_ALFO_DEPTH, "amp_lfo_depth", 0, 10000, 0) \
    X(CP_ALFO_SHAPE, "amp_lfo_shape", 0, 3, 0) \
    X(CP_LM_RATE, "lm_lfo_rate", 0, 10000, 0) \
    X(CP_LM_DEPTH, "lm_lfo_depth", 0, 10000, 0) \
    X(CP_LM_SHAPE, "lm_lfo_shape", 0, 3, 0) \
    X(CP_LM_MODE, "lm_lfo_mode", 0, 2, 0) \
    X(CP_PM_RATE, "pm_lfo_rate", 0, 10000, 0) \
    X(CP_PM_DEPTH, "pm_lfo_depth", 0, 10000, 0) \
    X(CP_PM_SHAPE, "pm_lfo_shape", 0, 3, 0) \
    X(CP_PM_MODE, "pm_lfo_mode", 0, 2, 0) \
    X(CP_RV_MIX, "reverb_mix", 0, 10000, 0) \
    X(CP_RV_DECAY, "reverb_decay", 0, 10000, 5000) \
    X(CP_RV_DAMP, "reverb_damp", 0, 10000, 3000) \
    X(CP_RV_SHIMMER, "reverb_shimmer", 0, 10000, 5000) \
    X(CP_RV_LOWCUT, "reverb_lowcut", 0, 10000, 0) \
    X(CP_RV_SIZE, "reverb_size", 0, 10000, 5000) \
    X(CP_RV_MODRATE, "reverb_mod_rate", 0, 10000, 0) \
    X(CP_RV_MODDEPTH, "reverb_mod_depth", 0, 10000, 0) \
    X(CP_GRIND, "grind", 0, 10000, 0) \
    X(CP_SHIFT, "bit_shift", 0, 10000, 0) \
    X(CP_DECIM, "decimator", 0, 10000, 0) \
    X(CP_LOFI_POS, "quality_position", 0, 1, 0) \
    X(CP_DL_MIX, "delay_mix", 0, 10000, 0) \
    X(CP_DL_TIME, "delay_time", 0, 10000, 3000) \
    X(CP_DL_FB, "delay_feedback", 0, 10000, 4000) \
    X(CP_DL_TONE, "delay_tone", 0, 10000, 7000) \
    X(CP_DL_MODE, "delay_mode", 0, 5, 0) \
    X(CP_DL_SHIFT, "delay_mod_depth", 0, 10000, 0) \
    X(CP_ARP, "arp_enabled", 0, 1, 0) \
    X(CP_ARP_TEMPO, "arp_tempo", 0, 10000, 4000) \
    X(CP_ARP_DIR, "arp_direction", 0, 3, 0) \
    X(CP_ARP_HOLD, "arp_hold", 0, 1, 0) \
    X(CP_ARP_STEPS, "arp_euclid_steps", 1, 16, 16) \
    X(CP_ARP_BEATS, "arp_euclid_beats", 0, 16, 0) \
    X(CP_ARP_VINT, "arp_variation_interval", -12, 12, 0) \
    X(CP_ARP_VARS, "arp_variations", 1, 8, 1) \
    X(CP_ARP_SYNC, "arp_clock_sync", 0, 1, 0) \
    X(CP_ARP_DIV, "arp_clock_division", 0, 5, 2) \
    X(CP_CTRL_SRC, "ctrl_source", 0, 4, 0) \
    X(CP_CTRL_CC, "ctrl_cc", 0, 127, 1) \
    X(CP_CT_CUTOFF, "ctrl_to_cutoff", 0, 10000, 0) \
    X(CP_CT_MORPH, "ctrl_to_morph", 0, 10000, 0) \
    X(CP_CT_VIB, "ctrl_to_vib", 0, 10000, 0) \
    X(CP_CT_SHAPE, "ctrl_to_shape", 0, 10000, 0) \
    X(CP_CT_FM, "ctrl_to_fm", 0, 10000, 0)

#define CHD_ENUM_(id, key, lo, hi, def) id,
enum { CHD_PARAMS(CHD_ENUM_) CP_N };

typedef struct {
    const char *key;
    int16_t lo, hi, def;
} chd_param_t;
extern const chd_param_t CHD_PARAM[CP_N];

/* a whole sound: every parameter (a factory preset, a user slot, the sound playing) */
typedef struct {
    char name[20];
    int16_t p[CP_N];
} chd_patch_t;
extern const chd_patch_t CHD_PRESET[CHD_NPRESET];   /* the original's 57, in its order (chd_presets.h) */

extern const char *const CHD_CHORD_NAME[CHD_NCHORD];
extern const int8_t CHD_CHORD_TABLE[CHD_NCHORD][4];
extern const char *const CHD_SCALE_NAME[CHD_NSCALE];
extern const char *const CHD_WAVE_NAME[CHD_NWAVE];
int chd_in_scale(int scale, int pc);
void chd_morph_view(float idx, float amt, float pidx, float pamt, float *gains, float *pans);   /* the screen's */     /* 1: pitch class pc (from the root) is in the scale */

/* the main loop's side: these queue an event for the next block (0: queued, -1: the queue is full) */
void chd_init(void);                     /* defaults, buffers cleared (before audio starts) */
int chd_set(int p, int v);               /* a parameter */
int chd_patch(const chd_patch_t *pt);    /* a whole sound (every parameter, then the derived values once) */
int chd_note_on(int note, int vel);      /* a key (or MIDI): note 0..127, velocity 1..127 */
int chd_note_off(int note);
int chd_pressure(int note, int v);       /* polyphonic pressure 0..127 (the sounding note's counts) */
int chd_panic(void);                     /* every note off and silent now */
void chd_midi(uint32_t st, uint32_t d1, uint32_t d2);   /* audio interrupt: a MIDI message in (notes, pressure, CC,
                                                         * clock); call it from the render's MIDI drain */

extern int chd_cores2;                   /* 1: the second core takes half the voices (set in app_init) */

/* the audio interrupt: n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void chd_render(int32_t *lr, uint32_t n, uint32_t gain_q12);

/* what the screen shows (written by the render, read by the main loop) */
extern volatile int8_t chd_sounding[4];  /* the chord's notes now (MIDI, -1 none), voice order */
extern volatile uint8_t chd_level[4];    /* each chord voice's level, 0..255 (its envelope x its mix) */
extern volatile uint8_t chd_held;        /* keys held (the arp's stack) */
typedef struct {
    float gain[4], pan[4], shape[4], env[4];   /* each chord voice: its morph level, its place 0..1, its shape, its env */
    float lfo, flfo, alfo, vib, lm, pm;       /* the LFOs' phases, 0..1: shape, filter, tremolo, vibrato, morphs */
    float cutoff, fenv, ctrl;                 /* the filter's cutoff now (0..1), its envelope, the control source */
    uint8_t arp_pos;                          /* the arp's step in its rhythm */
    uint8_t clock_run;                        /* the MIDI clock is running (start / continue came) */
    uint32_t seq;                             /* counts the blocks (the screen sees it move) */
} chd_live_t;
extern volatile chd_live_t chd_live;     /* what moves now (written each block by the render) */
extern volatile uint32_t chd_clock_ticks; /* MIDI clocks in since the last start (the Clock page's tempo) */
extern volatile uint32_t chd_arp_steps;  /* the arp's steps so far (tests) */
void chd_voicing(int root, int *notes);  /* the chord a key would play now: its four voices' notes (main loop) */
