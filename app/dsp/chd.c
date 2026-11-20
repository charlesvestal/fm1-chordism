/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Charles Vestal */
/* Chordism's engine (chd.h): schwung-chordism's src/dsp/chordism_plugin.cpp in C, step for step, with the FM-1's
 * changes:
 *   - fm1-base's fastmath for libm (single precision, no double);
 *   - the delay at half rate in half floats (the original's two 65536-float lines are 512 KB; these are 128 KB): its
 *     line runs every other sample on the pair's average, and the repeats are drawn between its steps;
 *   - the work that depends only on the block hoisted out of the sample loop (each voice's pan and gain, the
 *     vibrato's ratio once a sample);
 *   - AMB-1's plate for the original's (the paper's lengths scaled, a predelay, seven taps a side), at half the
 *     rate; the original's knobs drive it;
 *   - rand() as a small generator of its own.
 * Everything else is the original's arithmetic, in its order, so tests/host/chd_test.cpp can hold the two side by
 * side. One compilation unit, -O2. */
#include "chd.h"
#ifdef CHD_PROF
#include "plat.h"
#endif
#if !defined CHD_HOST && !defined PLAT_HOST
#define inline inline __attribute__((always_inline))   /* (fastmath inlined whole: the engine runs from RAM and may
                                                       * not call into the flash) */
#include "fastmath.h"
#undef inline
#else
#include "fastmath.h"
#endif

#if defined CHD_HOST || defined PLAT_HOST
#define CHD_POOL
#else
#define CHD_POOL __attribute__((section(".pool")))   /* the big buffers in the pool (fm1-base's app.ld) */
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")
#if defined CHD_HOST || defined PLAT_HOST
#define RAMC
#else
#define RAMC __attribute__((section(".app_ram")))   /* the audio path from RAM (fm1-base PLAT_RAM_CODE), out of the
                                                     * XIP cache's way */
#endif

#define SR CHD_SR
#define TWO_PI 6.28318530717958647692f
#define CHORD_SIZE 4
#define NUM_VOICES 16                    /* 4 banks of CHORD_SIZE: release tails finish before a steal */
#define HELD_STACK_MAX 16

#define PARAM_(id, key, lo, hi, def) {key, lo, hi, def},
const chd_param_t CHD_PARAM[CP_N] = {CHD_PARAMS(PARAM_)};
#include "chd_presets.h"

/* ------------------------------------------------------------------ tables --- */
#define MAX_SCALE_NOTES 12
static const int8_t SCALE_TABLE[CHD_NSCALE][MAX_SCALE_NOTES + 1] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, /* Chromatic */
    {0, 2, 4, 5, 7, 9, 11, 0, 0, 0, 0, 0, 7},   /* Major */
    {0, 2, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0, 7},   /* Natural minor */
    {0, 2, 3, 5, 7, 8, 11, 0, 0, 0, 0, 0, 7},   /* Harmonic minor */
    {0, 2, 4, 7, 9, 0, 0, 0, 0, 0, 0, 0, 5},    /* Pentatonic major */
    {0, 3, 5, 7, 10, 0, 0, 0, 0, 0, 0, 0, 5},   /* Pentatonic minor */
    {0, 2, 3, 5, 6, 8, 9, 11, 0, 0, 0, 0, 8},   /* Diminished (W-H) */
    {0, 2, 3, 5, 7, 9, 10, 0, 0, 0, 0, 0, 7},   /* Dorian */
    {0, 1, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0, 7},   /* Phrygian */
    {0, 2, 4, 6, 7, 9, 11, 0, 0, 0, 0, 0, 7},   /* Lydian */
    {0, 2, 4, 5, 7, 9, 10, 0, 0, 0, 0, 0, 7},   /* Mixolydian */
    {0, 1, 3, 5, 6, 8, 10, 0, 0, 0, 0, 0, 7},   /* Locrian */
    {0, 2, 3, 4, 7, 9, 0, 0, 0, 0, 0, 0, 6},    /* Blues major */
    {0, 3, 5, 6, 7, 10, 0, 0, 0, 0, 0, 0, 6},   /* Blues minor */
    {0, 2, 4, 5, 6, 8, 10, 0, 0, 0, 0, 0, 7},   /* Arabic */
    {0, 1, 4, 5, 7, 8, 10, 0, 0, 0, 0, 0, 7},   /* Arabic (h.mix) */
    {0, 1, 4, 5, 7, 8, 11, 0, 0, 0, 0, 0, 7},   /* Arabic (Hijaz) */
    {0, 1, 5, 6, 10, 0, 0, 0, 0, 0, 0, 0, 5},   /* Iwato */
    {0, 1, 3, 7, 8, 0, 0, 0, 0, 0, 0, 0, 5},    /* Pelog */
    {0, 2, 5, 7, 10, 0, 0, 0, 0, 0, 0, 0, 5},   /* Slendro */
    {0, 2, 3, 7, 8, 10, 0, 0, 0, 0, 0, 0, 6},   /* Folk */
    {0, 1, 5, 7, 8, 0, 0, 0, 0, 0, 0, 0, 5},    /* Japanese */
    {0, 2, 3, 6, 7, 8, 11, 0, 0, 0, 0, 0, 7},   /* Hungarian minor (the original's "Gypsy") */
    {0, 1, 3, 4, 5, 7, 8, 11, 0, 0, 0, 0, 8},   /* Flamenco */
    {0, 2, 4, 6, 8, 10, 0, 0, 0, 0, 0, 0, 6},   /* Whole tone */
};
const char *const CHD_SCALE_NAME[CHD_NSCALE] = {
    "Chromatic", "Major", "Minor", "Harm minor", "Pent major", "Pent minor", "Diminished", "Dorian", "Phrygian",
    "Lydian", "Mixolydian", "Locrian", "Blues major", "Blues minor", "Arabic", "Arabic mix", "Hijaz", "Iwato", "Pelog",
    "Slendro", "Folk", "Japanese", "Hungarian minor", "Flamenco", "Whole tone"};

const int8_t CHD_CHORD_TABLE[CHD_NCHORD][CHORD_SIZE] = {
    {0, 12, 24, 36}, {0, 7, 12, 19}, {0, 3, 7, 12}, {0, 3, 7, 10}, {0, 3, 7, 14}, {0, 3, 7, 17},
    {0, 4, 7, 12},   {0, 4, 7, 11},  {0, 4, 7, 14}, {0, 5, 7, 12}, {0, 4, 9, 14}, {0, 3, 7, 9},
    {0, 4, 7, 16},   {0, 4, 7, 10},  {0, 4, 7, 13}, {0, 3, 6, 10},
};
const char *const CHD_CHORD_NAME[CHD_NCHORD] = {"Octaves", "Fifth", "Minor", "Min7", "Min9", "Min11", "Major",
                                                "Maj7", "Maj9", "Sus4", "6/9", "Min6", "10th", "Dom7", "7b9",
                                                "Half-dim"};
const char *const CHD_WAVE_NAME[CHD_NWAVE] = {"Off", "Sine", "Triangle", "Saw", "Square", "Pulse", "Wavetable"};

int chd_in_scale(int scale, int pc)
{
    const int8_t *s = SCALE_TABLE[scale < 0 || scale >= CHD_NSCALE ? 0 : scale];
    for (int i = 0; i < s[MAX_SCALE_NOTES]; i++)
        if (s[i] == pc)
            return 1;
    return 0;
}

RAMC static int scale_quantize(int note, int scale_idx, int scale_root)
{
    const int8_t *scale;
    int count, rel, octave, pc, best_pc, best_dist, i;
    if (scale_idx < 0)
        scale_idx = 0;
    if (scale_idx >= CHD_NSCALE)
        scale_idx = CHD_NSCALE - 1;
    scale = SCALE_TABLE[scale_idx];
    count = scale[MAX_SCALE_NOTES];
    if (count <= 0)
        return note;
    rel = note - scale_root;
    octave = rel >= 0 ? rel / 12 : (rel - 11) / 12;
    pc = rel - octave * 12;
    best_pc = scale[0];
    best_dist = 99;
    for (i = 0; i < count; ++i) {
        int d = pc - scale[i];
        if (d < 0)
            d = -d;
        if (d < best_dist) {
            best_dist = d;
            best_pc = scale[i];
        }
    }
    return scale_root + octave * 12 + best_pc;
}

#define ATTACK_MIN_S 0.001f
#define ATTACK_MAX_S 16.0f
#define RELEASE_MIN_S 0.001f
#define RELEASE_MAX_S 16.0f
RAMC static float env_time_exp(float v01, float min_s, float max_s)
{
    if (v01 <= 0.0f)
        return min_s;
    if (v01 >= 1.0f)
        return max_s;
    return min_s * fm_powf(max_s / min_s, v01);
}
#define ENV_SILENCE 1e-4f
#define MAX_DETUNE_CENTS 50.0f

static const float PAN_MORPH_LUT[16][CHORD_SIZE] = {
    {0.0f, 0.0f, 0.0f, 0.0f},    {-1.0f, -0.3f, 0.3f, 1.0f},  {1.0f, 0.3f, -0.3f, -1.0f}, {-1.0f, 1.0f, -1.0f, 1.0f},
    {1.0f, -1.0f, 1.0f, -1.0f},  {-1.0f, -1.0f, 1.0f, 1.0f},  {-1.0f, 0.0f, 0.0f, 1.0f},  {0.0f, -1.0f, 1.0f, 0.0f},
    {-0.5f, 0.5f, -0.5f, 0.5f},  {0.5f, -0.5f, 0.5f, -0.5f},  {-1.0f, -0.5f, 0.5f, 1.0f}, {0.0f, -1.0f, 0.0f, 1.0f},
    {0.0f, 1.0f, 0.0f, -1.0f},   {-1.0f, 1.0f, 0.0f, 0.0f},   {0.0f, 0.0f, 1.0f, -1.0f},  {0.7f, -0.7f, -0.7f, 0.7f},
};
#define NUM_LEVEL_MORPHS 16
static const float LEVEL_MORPH_LUT[NUM_LEVEL_MORPHS][CHORD_SIZE] = {
    {1.0f, 1.0f, 1.0f, 1.0f}, {0.2f, 0.5f, 0.8f, 1.0f}, {1.0f, 0.8f, 0.5f, 0.2f}, {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 1.0f}, {0.1f, 0.3f, 0.7f, 1.0f}, {0.3f, 1.0f, 1.0f, 0.3f}, {1.0f, 0.3f, 0.3f, 1.0f},
    {0.0f, 0.0f, 0.5f, 1.0f}, {1.0f, 0.7f, 0.3f, 0.0f}, {1.0f, 1.0f, 0.5f, 0.5f}, {0.5f, 0.5f, 1.0f, 1.0f},
};

/* the delay: the original's line at half the rate (DLY_SIZE half-rate samples: 1.486 s), 16 bits */
#define DLY_SIZE 32768
#define DLY_MASK (DLY_SIZE - 1)
#define DELAY_TIME_MIN_S 0.005f
#define DELAY_TIME_MAX_S 1.4f
#define DELAY_FEEDBACK_MAX 0.95f
enum { DELAY_STEREO, DELAY_PINGPONG, DELAY_FLIPFLOP, DELAY_LONG, DELAY_ZENITH, DELAY_INTERVAL };
enum { ENVMODE_AD, ENVMODE_ASR, ENVMODE_LOOPING };
#define VIB_SPEED_MIN_HZ 0.1f
#define VIB_SPEED_MAX_HZ 12.0f
#define VIB_DEPTH_MAX_CENTS 100.0f
#define VIB_DELAY_MAX_S 2.0f
#define ONE_OVER_1200 (1.0f / 1200.0f)
#define ONE_OVER_12 (1.0f / 12.0f)
#define SWEEP_MAX_SEMITONES 12.0f
#define SWEEP_TIME_MIN_S 0.01f
#define SWEEP_TIME_MAX_S 4.0f
#define GLIDE_TIME_MAX_S 2.0f
enum { ARP_UP, ARP_DOWN, ARP_UPDOWN, ARP_RANDOM };
#define ARP_BPM_MIN 30.0f
#define ARP_BPM_MAX 240.0f
#define FILTER_CUTOFF_MIN_HZ 20.0f
#define FILTER_CUTOFF_MAX_HZ 20000.0f
#define LOG2_CUTOFF_RATIO 9.96578428f    /* log2(20000 / 20) */
#define FILTER_Q_MIN 0.5f
#define FILTER_Q_MAX 12.0f
enum { FILT_LP, FILT_HP, FILT_BP };
enum { ENV_IDLE, ENV_ATTACK, ENV_HOLD, ENV_RELEASE };
enum { WAVE_OFF, WAVE_SINE, WAVE_TRIANGLE, WAVE_SAW, WAVE_SQUARE, WAVE_PULSE_TRAIN, WAVE_WAVETABLE };
enum { LFOMODE_FREE, LFOMODE_NOTE_RESET, LFOMODE_ONE_SHOT };
enum { CTRL_AFTERTOUCH, CTRL_RANDOM, CTRL_COIN_TOSS, CTRL_CC, CTRL_VELOCITY };
enum { LFO_TRIANGLE, LFO_RAMP_UP, LFO_RAMP_DOWN, LFO_SQUARE };
#define LFO_RATE_MIN_HZ 0.02f
#define LFO_RATE_MAX_HZ 50.0f
#define LOG2_LFO_RATIO 11.2877124f       /* log2(50 / 0.02) */

/* ------------------------------------------------------------------- state --- */
typedef struct { float ic1eq, ic2eq; } svf_t;
typedef struct { float s[4], g, k, G, c0, c1, c2, c3, inv; } ladder_t;   /* (g..inv: its coefficients for g) */
/* the plate: AMB-1's Dattorro plate (J. Dattorro, "Effect Design, Part 1", JAES 1997; FoMni's, then AMB-1's): a
 * predelay, four input diffusers, a figure-eight tank, seven taps a side. It runs at half the rate (its input the pair's
 * average, its output drawn between its steps): the paper's lengths for 29761 Hz, PS() scales them to 22050 Hz */
#define PS(n) (((n) * 7409 + 5000) / 10000)
#define P_PRE 662                         /* 30 ms */
#define P_EXC 10                          /* the tank's modulation, half-rate samples at most */
enum { L_PRE, L_IN1, L_IN2, L_IN3, L_IN4, L_APL, L_D1L, L_AP2L, L_D2L, L_APR, L_D1R, L_AP2R, L_D2R,
       L_PRER, L_IN1R, L_IN2R, L_IN3R, L_IN4R, L_N };   /* (the R's: the right channel's own predelay and diffusers) */
static const int16_t PL_LEN[L_N] = {P_PRE, PS(142), PS(107), PS(379), PS(277), PS(672) + P_EXC + 2, PS(4453), PS(1800),
                                    PS(3720), PS(908) + P_EXC + 2, PS(4217), PS(2656), PS(3163),
                                    P_PRE, PS(142), PS(107), PS(379), PS(277)};
#define PL_TOTAL (P_PRE + PS(142) + PS(107) + PS(379) + PS(277) + PS(672) + PS(4453) + PS(1800) + PS(3720) + PS(908) + \
                  PS(4217) + PS(2656) + PS(3163) + 2 * (P_EXC + 2) + P_PRE + PS(142) + PS(107) + PS(379) + PS(277))
typedef struct { float *b; int n, i; } pdl_t;
typedef struct {
    pdl_t d[L_N];
    float bw, bw_r, damp_l, damp_r, lfo_s, lfo_c;
    float acc, acc_r, prev_l, prev_r, cur_l, cur_r;
    int odd, sh_i;
    float sh_d, sh_lp, sh_hp, sh_x1;      /* the shimmer: its heads' distance, its lowpass, its highpass */
} plate_t;
typedef struct { int stage; float value, attack_inc, release_coef; } env_t;
typedef struct {
    uint8_t active, gate;
    int root_note, note, chord_step, waveform;
    float phase, phase_inc, target_phase_inc, glide_step, velocity, pan_offset;
    env_t env;
    float g_l, g_r;                      /* the block's gain into each side (velocity, morph, mix, pan) */
    float g0_l, g0_r;                    /* the gain it had at the last segment's end: a change glides over the next
                                          * (a stolen voice's level and place, the morphs' blocks), no step */
} voice_t;

static float pl_buf[PL_TOTAL] CHD_POOL;
#define SH_W 2048                         /* the shimmer's window, half-rate samples (93 ms) */
static float sh_buf[SH_W] CHD_POOL;       /* the plate's output, for the shimmer's heads */
static plate_t plate;
static uint16_t dly_l[DLY_SIZE] CHD_POOL, dly_r[DLY_SIZE] CHD_POOL;   /* (half floats: f2h, h2f) */

static struct {
    int16_t par[CP_N];
    float attack, release, volume;
    int waveforms[CHORD_SIZE];
    float shapes[CHORD_SIZE];
    float morph_index, morph_intensity, morph_gains[CHORD_SIZE];
    float pan_morph_index, pan_morph_intensity, pan_morph_pans[CHORD_SIZE];
    float chord_spread, chord_rotation;
    int fm_modulator_idx;
    float fm_amount, fm_amounts[CHORD_SIZE], last_modulator_sample;
    float mixer_trims[CHORD_SIZE];
    int vib_osc_enable, sweep_osc_enable;
    int shape_lfo_mode, filter_lfo_mode, lm_lfo_mode, pm_lfo_mode;
    uint8_t shape_lfo_done, filter_lfo_done, lm_lfo_done, pm_lfo_done;
    int fm_position, scale_index, scale_root, tuning_mode, interval[3], chord_pc[12];
    float shape_lfo_phase_offsets[CHORD_SIZE];
    int ctrl_source, ctrl_cc;
    float ctrl_value, ctrl_value_target, vel_baseline, at_smooth;
    float ctrl_to_cutoff, ctrl_to_morph, ctrl_to_vib, ctrl_to_shape, ctrl_to_fm;
    int arp_hold, arp_euclid_steps, arp_euclid_beats, arp_variation_interval, arp_variations;
    uint8_t arp_pattern[16];
    int arp_pattern_pos, arp_variation_idx;
    float lm_lfo_rate, lm_lfo_depth, lm_lfo_phase, lm_lfo_phase_inc;
    int lm_lfo_shape;
    float pm_lfo_rate, pm_lfo_depth, pm_lfo_phase, pm_lfo_phase_inc;
    int pm_lfo_shape;
    float amp_lfo_rate, amp_lfo_depth, amp_lfo_phase, amp_lfo_phase_inc;
    int amp_lfo_shape;
    int vca_mode, vca_hard_reset, vca_drone, fenv_mode, fenv_hard_reset, quality_position;
    float reverb_shimmer, reverb_lowcut, reverb_size, reverb_mod_rate, reverb_mod_depth, reverb_hp_l, reverb_hp_r;
    float delay_mod_depth;
    float delay_grain_phase, dly_ps_l, dly_ps_r;   /* the shifter's heads, its lowpass */
    float lfo_rate, lfo_depth;
    int lfo_shape, chord_type;
    float detune, width, vib_speed, vib_depth, vib_delay, sweep_amount, sweep_rate;
    float vibrato_phase, vibrato_phase_inc, vib_ramp_value, vib_ramp_inc;
    float sweep_value, sweep_coef;
    float filter_cutoff, filter_resonance;
    int filter_mode;
    float filter_env_attack, filter_env_decay, filter_env_depth, drive;
    svf_t filter_l, filter_r, filter_l_b, filter_r_b;
    ladder_t ladder_l, ladder_r;
    float filter_k;
    int filter_slope;
    float filter_lfo_rate, filter_lfo_depth, filter_lfo_spread, filter_lfo_phase, filter_lfo_phase_inc;
    int filter_lfo_shape;
    env_t filter_env;                    /* (attack_inc, release_coef: the original's attack_inc, decay_coef) */
    float grind, bit_shift, decimator;
    int decim_counter;
    float decim_hold_l, decim_hold_r;
    float reverb_mix, reverb_decay, reverb_damp;
    float delay_mix, delay_time, delay_feedback, delay_tone;
    int delay_mode, delay_write_idx, delay_flip_counter;
    float delay_lp_l, delay_lp_r;
    float dly_acc_l, dly_acc_r, dly_prev_l, dly_prev_r, dly_cur_l, dly_cur_r;
    int dly_odd;
    float lfo_phase, lfo_phase_inc;      /* the shape LFO */
    int plate_quiet;                     /* samples the plate has been silent, in and out */
    float live_cutoff;                   /* the filter's cutoff at the block's end, 0..1 (the screen's) */
    float g_eff_l, g_eff_r, g_l, g_r;    /* the filter's coefficients for these cutoffs (-1: none yet) */
    voice_t voices[NUM_VOICES];
    int next_chord_base, cur_base;
    int held_notes[HELD_STACK_MAX], held_count;
    float aftertouch;
    float glide_rate;
    int glide_legato;
    uint8_t has_prev_chord, prev_chord_still_held;
    float prev_phase_inc[CHORD_SIZE];
    int vib_stray;
    float vib_random_value, vib_random_target;
    int vib_random_counter;
    int arp_enabled;
    float arp_tempo;
    int arp_direction, arp_step_idx, arp_step_dir, arp_sample_counter, arp_step_period;
    int arp_clock_sync, arp_clock_division, arp_clock_count;
    uint8_t arp_clock_running;
    uint32_t clock_age;                       /* samples since the last MIDI clock (synced: none for a while, the
                                               * arp's own tempo instead) */
    uint32_t rng;
} S;

volatile int8_t chd_sounding[4] = {-1, -1, -1, -1};
volatile uint8_t chd_level[4];
volatile uint8_t chd_held;
volatile chd_live_t chd_live;
volatile uint32_t chd_clock_ticks, chd_arp_steps;         /* MIDI clocks in (0xF8) since the last start */

/* the original's rand() & 0xFFFF and rand(): a generator of its own (the sequence differs, the spread is the same) */
RAMC static uint32_t rnd(void)
{
    S.rng = S.rng * 1664525u + 1013904223u;
    return S.rng >> 1;
}

/* ----------------------------------------------------------------- helpers --- */
RAMC static float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
RAMC static float midi_to_hz(int note) { return 440.0f * fm_exp2f((float)(note - 69) / 12.0f); }
RAMC static float poly_blep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
RAMC static float lfo_sample(int shape, float phase)
{
    switch (shape) {
    case LFO_TRIANGLE: return 4.0f * fm_fabsf(phase - 0.5f) - 1.0f;
    case LFO_RAMP_UP: return 2.0f * phase - 1.0f;
    case LFO_RAMP_DOWN: return 1.0f - 2.0f * phase;
    case LFO_SQUARE: return phase < 0.5f ? 1.0f : -1.0f;
    default: return 0.0f;
    }
}
RAMC static float lfo_rate_to_hz(float rate01) { return LFO_RATE_MIN_HZ * fm_exp2f(LOG2_LFO_RATIO * rate01); }
RAMC static float vib_speed_to_hz(float speed01)
{
    return VIB_SPEED_MIN_HZ * fm_powf(VIB_SPEED_MAX_HZ / VIB_SPEED_MIN_HZ, speed01);
}
RAMC static float cutoff_to_hz(float c01) { return FILTER_CUTOFF_MIN_HZ * fm_exp2f(LOG2_CUTOFF_RATIO * c01); }

RAMC static void pan_morph_recompute_at(float index01)
{
    const int N = 16;
    float idx, frac;
    int lo, hi, i;
    if (index01 < 0.0f)
        index01 = 0.0f;
    if (index01 > 1.0f)
        index01 = 1.0f;
    idx = index01 * (float)(N - 1);
    lo = (int)idx;
    hi = lo + 1;
    if (lo >= N)
        lo = N - 1;
    if (hi >= N)
        hi = N - 1;
    frac = clampf(idx - (float)lo, 0.0f, 1.0f);
    for (i = 0; i < CHORD_SIZE; ++i)
        S.pan_morph_pans[i] =
            (PAN_MORPH_LUT[lo][i] * (1.0f - frac) + PAN_MORPH_LUT[hi][i] * frac) * S.pan_morph_intensity;
}
RAMC static void morph_recompute_at(float index01)
{
    float idx, frac;
    int lo, hi, i;
    if (index01 < 0.0f)
        index01 = 0.0f;
    if (index01 > 1.0f)
        index01 = 1.0f;
    idx = index01 * (float)(NUM_LEVEL_MORPHS - 1);
    lo = (int)idx;
    hi = lo + 1;
    if (lo >= NUM_LEVEL_MORPHS)
        lo = NUM_LEVEL_MORPHS - 1;
    if (hi >= NUM_LEVEL_MORPHS)
        hi = NUM_LEVEL_MORPHS - 1;
    frac = clampf(idx - (float)lo, 0.0f, 1.0f);
    for (i = 0; i < CHORD_SIZE; ++i) {
        float lut = LEVEL_MORPH_LUT[lo][i] * (1.0f - frac) + LEVEL_MORPH_LUT[hi][i] * frac;
        S.morph_gains[i] = 1.0f + (lut - 1.0f) * S.morph_intensity;
    }
}

RAMC static void apply_lofi(float *l, float *r)
{
    if (S.grind > 0.0f) {
        float bits = 16.0f - S.grind * 14.0f;
        float steps = fm_exp2f(bits) * 0.5f;
        float shift = S.bit_shift * 0.5f;
        *l = (fm_floorf((*l + shift) * steps + 0.5f) / steps) - shift;
        *r = (fm_floorf((*r + shift) * steps + 0.5f) / steps) - shift;
    }
    if (S.decimator > 0.0f) {
        int hold = (int)(S.decimator * 32.0f);
        if (hold < 1)
            hold = 1;
        if (S.decim_counter <= 0) {
            S.decim_hold_l = *l;
            S.decim_hold_r = *r;
            S.decim_counter = hold;
        }
        S.decim_counter--;
        *l = S.decim_hold_l;
        *r = S.decim_hold_r;
    }
}

RAMC static void vibrato_recompute(void)
{
    float delay_samples = S.vib_delay * VIB_DELAY_MAX_S * SR;
    S.vibrato_phase_inc = vib_speed_to_hz(S.vib_speed) / SR;
    if (delay_samples < 1.0f)
        delay_samples = 1.0f;
    S.vib_ramp_inc = 1.0f / delay_samples;
}
RAMC static void sweep_recompute(void)
{
    float time_s = SWEEP_TIME_MAX_S / fm_powf(SWEEP_TIME_MAX_S / SWEEP_TIME_MIN_S, S.sweep_rate);
    float samples = time_s * SR;
    if (samples < 1.0f)
        samples = 1.0f;
    S.sweep_coef = fm_expf(-5.0f / samples);
}
static const int CLOCKS_PER_STEP[6] = {24, 16, 12, 8, 6, 3};   /* 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/32 */
/* the arp's own clock: the original's step (a step each beat of the tempo) is the eighth note here, so the division
 * sets the rate on the arp's own tempo too (the original's only on MIDI clock): 1/8, every factory sound's, as it was */
RAMC static void arp_recompute(void)
{
    float bpm = ARP_BPM_MIN + (ARP_BPM_MAX - ARP_BPM_MIN) * S.arp_tempo;
    int p = (int)((60.0f / bpm) * SR * (float)CLOCKS_PER_STEP[S.arp_clock_division % 6] / 12.0f);
    S.arp_step_period = p < 64 ? 64 : p;
}
RAMC static void arp_euclid_recompute(void)
{
    int s = S.arp_euclid_steps, b = S.arp_euclid_beats, i;
    if (s < 1)
        s = 1;
    if (s > 16)
        s = 16;
    if (b < 0)
        b = 0;
    if (b > s)
        b = s;
    for (i = 0; i < 16; ++i)
        S.arp_pattern[i] = (uint8_t)(i >= s ? 0 : b == 0 ? 1 : ((i * b) % s) < b);
}
RAMC static void filter_recompute(void)
{
    float Q = FILTER_Q_MIN + (FILTER_Q_MAX - FILTER_Q_MIN) * S.filter_resonance;
    if (Q < 0.05f)
        Q = 0.05f;
    S.filter_k = 1.0f / Q;
}
RAMC static void env_recompute_rates(env_t *env, float attack01, float release01)
{
    float attack_samples = env_time_exp(attack01, ATTACK_MIN_S, ATTACK_MAX_S) * SR;
    float release_samples = env_time_exp(release01, RELEASE_MIN_S, RELEASE_MAX_S) * SR;
    if (attack_samples < 1.0f)
        attack_samples = 1.0f;
    env->attack_inc = 1.0f / attack_samples;
    if (release_samples < 1.0f)
        release_samples = 1.0f;
    env->release_coef = fm_expf(-5.0f / release_samples);
}

/* the filter's envelope: AD auto-decays; ASR holds at the peak while a key is held; Looping attacks again while held */
RAMC static float aenv_tick(env_t *env)
{
    int mode = S.fenv_mode, gate = S.held_count > 0;
    switch (env->stage) {
    case ENV_ATTACK:
        env->value += env->attack_inc;
        if (env->value >= 1.0f) {
            env->value = 1.0f;
            env->stage = mode == ENVMODE_ASR ? ENV_HOLD : ENV_RELEASE;
        }
        break;
    case ENV_HOLD:
        if (!gate)
            env->stage = ENV_RELEASE;
        break;
    case ENV_RELEASE:
        env->value *= env->release_coef;
        if (env->value < ENV_SILENCE) {
            env->value = 0.0f;
            env->stage = gate && mode == ENVMODE_LOOPING ? ENV_ATTACK : ENV_IDLE;
        }
        break;
    default:
        break;
    }
    return env->value;
}

/* the screen's picture of the morphs: each voice's level and pan offset for these settings (main loop) */
void chd_morph_view(float idx, float amt, float pidx, float pamt, float *gains, float *pans)
{
    int lo, hi, i;
    float f, x = fm_clampf(idx, 0.0f, 1.0f) * 15.0f, y = fm_clampf(pidx, 0.0f, 1.0f) * 15.0f;
    lo = (int)x;
    hi = lo < 15 ? lo + 1 : 15;
    f = x - (float)lo;
    for (i = 0; i < CHORD_SIZE; i++)
        gains[i] = 1.0f + (LEVEL_MORPH_LUT[lo][i] * (1.0f - f) + LEVEL_MORPH_LUT[hi][i] * f - 1.0f) * amt;
    lo = (int)y;
    hi = lo < 15 ? lo + 1 : 15;
    f = y - (float)lo;
    for (i = 0; i < CHORD_SIZE; i++)
        pans[i] = (PAN_MORPH_LUT[lo][i] * (1.0f - f) + PAN_MORPH_LUT[hi][i] * f) * pamt;
}

/* ------------------------------------------------------------- parameters --- */
RAMC static float u01(int v) { return (float)v * 0.0001f; }

RAMC static void set_param_(int p, int v)       /* audio interrupt: the original's set_param, for one key */
{
    int i;
    if (p < 0 || p >= CP_N)
        return;
    if (v < CHD_PARAM[p].lo)
        v = CHD_PARAM[p].lo;
    if (v > CHD_PARAM[p].hi)
        v = CHD_PARAM[p].hi;
    if (p == CP_ARP_BEATS && v > S.par[CP_ARP_STEPS])
        v = S.par[CP_ARP_STEPS];
    S.par[p] = (int16_t)v;
    switch (p) {
    case CP_CHORD: S.chord_type = v; break;
    case CP_SPREAD: S.chord_spread = u01(v); break;
    case CP_ROTATION: S.chord_rotation = u01(v); break;
    case CP_DETUNE: S.detune = u01(v); break;
    case CP_WIDTH: S.width = u01(v); break;
    case CP_TUNING: S.tuning_mode = v; break;
    case CP_INT1: case CP_INT2: case CP_INT3: S.interval[p - CP_INT1] = v; break;
    case CP_SCALE: S.scale_index = v; break;
    case CP_ROOT: S.scale_root = v; break;
    case CP_WAVE1: case CP_WAVE2: case CP_WAVE3: case CP_WAVE4: S.waveforms[p - CP_WAVE1] = v; break;
    case CP_SHAPE1: case CP_SHAPE2: case CP_SHAPE3: case CP_SHAPE4: S.shapes[p - CP_SHAPE1] = u01(v); break;
    case CP_MIX1: case CP_MIX2: case CP_MIX3: case CP_MIX4: S.mixer_trims[p - CP_MIX1] = u01(v); break;
    case CP_FM_MOD: S.fm_modulator_idx = v; break;
    case CP_FM_AMT: S.fm_amount = u01(v); break;
    case CP_FM_POS: S.fm_position = v; break;
    case CP_FM1: case CP_FM2: case CP_FM3: case CP_FM4: S.fm_amounts[p - CP_FM1] = u01(v); break;
    case CP_MORPH: S.morph_index = u01(v); morph_recompute_at(S.morph_index); break;
    case CP_MORPH_INT: S.morph_intensity = u01(v); morph_recompute_at(S.morph_index); break;
    case CP_PAN: S.pan_morph_index = u01(v); pan_morph_recompute_at(S.pan_morph_index); break;
    case CP_PAN_INT: S.pan_morph_intensity = u01(v); pan_morph_recompute_at(S.pan_morph_index); break;
    case CP_LFO_RATE: S.lfo_rate = u01(v); S.lfo_phase_inc = lfo_rate_to_hz(S.lfo_rate) / SR; break;
    case CP_LFO_DEPTH: S.lfo_depth = u01(v); break;
    case CP_LFO_SHAPE: S.lfo_shape = v; break;
    case CP_LFO_MODE: S.shape_lfo_mode = v; break;
    case CP_LFO_PH1: case CP_LFO_PH2: case CP_LFO_PH3: case CP_LFO_PH4:
        S.shape_lfo_phase_offsets[p - CP_LFO_PH1] = u01(v);
        break;
    case CP_VIB_SPEED: S.vib_speed = u01(v); vibrato_recompute(); break;
    case CP_VIB_DEPTH: S.vib_depth = u01(v); break;
    case CP_VIB_DELAY: S.vib_delay = u01(v); vibrato_recompute(); break;
    case CP_VIB_STRAY: S.vib_stray = v; break;
    case CP_VIB_OSC: S.vib_osc_enable = v; break;
    case CP_SWEEP_AMT: S.sweep_amount = u01(v); break;
    case CP_SWEEP_RATE: S.sweep_rate = u01(v); sweep_recompute(); break;
    case CP_SWEEP_OSC: S.sweep_osc_enable = v; break;
    case CP_GLIDE: S.glide_rate = u01(v); break;
    case CP_LEGATO: S.glide_legato = v; break;
    case CP_CUTOFF: S.filter_cutoff = u01(v); break;
    case CP_RESO: S.filter_resonance = u01(v); filter_recompute(); break;
    case CP_FMODE: S.filter_mode = v; break;
    case CP_SLOPE: S.filter_slope = v; break;
    case CP_DRIVE: S.drive = u01(v); break;
    case CP_FENV_A: case CP_FENV_D:
        if (p == CP_FENV_A)
            S.filter_env_attack = u01(v);
        else
            S.filter_env_decay = u01(v);
        env_recompute_rates(&S.filter_env, S.filter_env_attack, S.filter_env_decay);
        break;
    case CP_FENV_AMT: S.filter_env_depth = u01(v); break;
    case CP_FENV_MODE: S.fenv_mode = v; break;
    case CP_FENV_HARD: S.fenv_hard_reset = v; break;
    case CP_FLFO_RATE:
        S.filter_lfo_rate = u01(v);
        S.filter_lfo_phase_inc = lfo_rate_to_hz(S.filter_lfo_rate) / SR;
        break;
    case CP_FLFO_DEPTH: S.filter_lfo_depth = u01(v); break;
    case CP_FLFO_SPREAD: S.filter_lfo_spread = u01(v); break;
    case CP_FLFO_SHAPE: S.filter_lfo_shape = v; break;
    case CP_FLFO_MODE: S.filter_lfo_mode = v; break;
    case CP_ATTACK: case CP_RELEASE:
        if (p == CP_ATTACK)
            S.attack = u01(v);
        else
            S.release = u01(v);
        for (i = 0; i < NUM_VOICES; ++i)
            env_recompute_rates(&S.voices[i].env, S.attack, S.release);
        break;
    case CP_VOLUME: S.volume = u01(v); break;
    case CP_VCA_MODE: S.vca_mode = v; break;
    case CP_VCA_HARD: S.vca_hard_reset = v; break;
    case CP_DRONE: S.vca_drone = v; break;
    case CP_ALFO_RATE: S.amp_lfo_rate = u01(v); S.amp_lfo_phase_inc = lfo_rate_to_hz(S.amp_lfo_rate) / SR; break;
    case CP_ALFO_DEPTH: S.amp_lfo_depth = u01(v); break;
    case CP_ALFO_SHAPE: S.amp_lfo_shape = v; break;
    case CP_LM_RATE: S.lm_lfo_rate = u01(v); S.lm_lfo_phase_inc = lfo_rate_to_hz(S.lm_lfo_rate) / SR; break;
    case CP_LM_DEPTH: S.lm_lfo_depth = u01(v); break;
    case CP_LM_SHAPE: S.lm_lfo_shape = v; break;
    case CP_LM_MODE: S.lm_lfo_mode = v; break;
    case CP_PM_RATE: S.pm_lfo_rate = u01(v); S.pm_lfo_phase_inc = lfo_rate_to_hz(S.pm_lfo_rate) / SR; break;
    case CP_PM_DEPTH: S.pm_lfo_depth = u01(v); break;
    case CP_PM_SHAPE: S.pm_lfo_shape = v; break;
    case CP_PM_MODE: S.pm_lfo_mode = v; break;
    case CP_RV_MIX: S.reverb_mix = u01(v); break;
    case CP_RV_DECAY: S.reverb_decay = u01(v); break;
    case CP_RV_DAMP: S.reverb_damp = u01(v); break;
    case CP_RV_SHIMMER: S.reverb_shimmer = u01(v); break;
    case CP_RV_LOWCUT: S.reverb_lowcut = u01(v); break;
    case CP_RV_SIZE: S.reverb_size = u01(v); break;
    case CP_RV_MODRATE: S.reverb_mod_rate = u01(v); break;
    case CP_RV_MODDEPTH: S.reverb_mod_depth = u01(v); break;
    case CP_GRIND: S.grind = u01(v); break;
    case CP_SHIFT: S.bit_shift = u01(v); break;
    case CP_DECIM: S.decimator = u01(v); break;
    case CP_LOFI_POS: S.quality_position = v; break;
    case CP_DL_MIX: S.delay_mix = u01(v); break;
    case CP_DL_TIME: S.delay_time = u01(v); break;
    case CP_DL_FB: S.delay_feedback = u01(v); break;
    case CP_DL_TONE: S.delay_tone = u01(v); break;
    case CP_DL_MODE: S.delay_mode = v; break;
    case CP_DL_SHIFT: S.delay_mod_depth = u01(v); break;
    case CP_ARP:
        S.arp_enabled = v;
        if (!v)
            S.arp_sample_counter = 0;
        break;
    case CP_ARP_TEMPO: S.arp_tempo = u01(v); arp_recompute(); break;
    case CP_ARP_DIR: S.arp_direction = v; S.arp_step_dir = 1; break;
    case CP_ARP_HOLD: S.arp_hold = v; break;
    case CP_ARP_STEPS: S.arp_euclid_steps = v; arp_euclid_recompute(); break;
    case CP_ARP_BEATS: S.arp_euclid_beats = v; arp_euclid_recompute(); break;
    case CP_ARP_VINT: S.arp_variation_interval = v; break;
    case CP_ARP_VARS: S.arp_variations = v; break;
    case CP_ARP_SYNC: S.arp_clock_sync = v; break;
    case CP_ARP_DIV: S.arp_clock_division = v; arp_recompute(); break;
    case CP_CTRL_SRC: S.ctrl_source = v; S.ctrl_value = 0.0f; S.ctrl_value_target = 0.0f; break;
    case CP_CTRL_CC: S.ctrl_cc = v; break;
    case CP_CT_CUTOFF: S.ctrl_to_cutoff = u01(v); break;
    case CP_CT_MORPH: S.ctrl_to_morph = u01(v); break;
    case CP_CT_VIB: S.ctrl_to_vib = u01(v); break;
    case CP_CT_SHAPE: S.ctrl_to_shape = u01(v); break;
    case CP_CT_FM: S.ctrl_to_fm = u01(v); break;
    default:
        if (p >= CP_PC0 && p <= CP_PC11)
            S.chord_pc[p - CP_PC0] = v;
        break;
    }
}

RAMC static void revoice(void);
RAMC static void set_param(int p, int v)
{
    set_param_(p, v);
    if ((p >= CP_CHORD && p <= CP_WAVE4) && p != CP_WIDTH)   /* (the voicing: the chord playing takes it now) */
        revoice();
}

RAMC static void patch_apply(const int16_t *p)  /* the original's preset_apply: every value, the source's state cleared */
{
    int i;
    for (i = 0; i < CP_N; i++)
        set_param_(i, i == CP_ARP_BEATS ? 0 : p[i]);
    set_param_(CP_ARP_BEATS, p[CP_ARP_BEATS]);   /* (after the steps: clamped to them) */
    S.ctrl_value = S.ctrl_value_target = S.vel_baseline = S.at_smooth = 0.0f;
    revoice();                            /* (a chord sounding takes the new sound's voicing) */
}

/* ------------------------------------------------------------------ plate --- */
RAMC static float tap(const pdl_t *d, int k)
{
    int j = d->i - k;
    return d->b[j < 0 ? j + d->n : j];
}
RAMC static void dpush(pdl_t *d, float x)
{
    d->b[d->i] = x;
    if (++d->i >= d->n)
        d->i = 0;
}
RAMC static float allpass(pdl_t *d, int len, float x, float g)
{
    float z = tap(d, len), v = x - g * z;
    dpush(d, fm_flush(v));
    return z + g * v;
}
RAMC static float allpass_mod(pdl_t *d, float len, float x, float g)
{
    int k = (int)len;
    float f = len - (float)k, z = tap(d, k) + (tap(d, k + 1) - tap(d, k)) * f, v = x - g * z;
    dpush(d, fm_flush(v));
    return z + g * v;
}
/* one half-rate step: in, the tank's gain (decay), the damping's and the bandwidth's coefficients, the modulation's
 * depth and its turn a step (rad) */
/* the shimmer (not the original's: its "shimmer" only deepened the chorus): the plate's own output an octave up, fed
 * back into its input, so the tail rises in octaves. Two heads half a window apart, crossfading */
RAMC static float shimmer(plate_t *P)
{
    float d1, d2, g1, a, b;
    int i, k;
    P->sh_d -= 1.0f;                      /* (an octave: the heads gain a sample a step on the writing) */
    if (P->sh_d < 2.0f)
        P->sh_d += (float)(SH_W - 4);
    d1 = P->sh_d;
    d2 = d1 + (float)(SH_W / 2 - 2);
    if (d2 >= (float)(SH_W - 2))
        d2 -= (float)(SH_W - 4);
    g1 = 1.0f - fm_fabsf(2.0f * (d1 - 2.0f) / (float)(SH_W - 4) - 1.0f);
    k = (int)d1;
    i = (P->sh_i - k) & (SH_W - 1);
    a = sh_buf[i];
    k = (int)d2;
    i = (P->sh_i - k) & (SH_W - 1);
    b = sh_buf[i];
    P->sh_lp += 0.5f * (a * g1 + b * (1.0f - g1) - P->sh_lp);   /* (its top end softened: the octaves stay sweet) */
    P->sh_hp = 0.97f * (P->sh_hp + P->sh_lp - P->sh_x1);         /* (and its bottom cut, ~100 Hz: no rumble builds up
                                                                    * in the loop) */
    P->sh_x1 = P->sh_lp;
    return P->sh_hp;
}
/* stereo in (not the paper's, which takes one channel): each channel its own predelay and diffusers, the left
 * feeding the tank's left half, the right its right; the tank crosses them over, so a sound placed left stays left in
 * the plate's first moments and spreads as it grows */
RAMC static void plate_step(plate_t *P, float in, float in_r, float decay, float dk, float exc, float w, float oct,
                            float *ol, float *or_)
{
    pdl_t *dl = P->d;
    float x, xr, a, b, fb_l = tap(&dl[L_D2R], PS(3163)), fb_r = tap(&dl[L_D2L], PS(3720)), s, c;
    if (oct > 0.0f) {
        float sh = oct * shimmer(P);
        in += sh;
        in_r += sh;
    }
    dpush(&dl[L_PRE], in);
    x = tap(&dl[L_PRE], P_PRE);
    P->bw += 0.84f * (x - P->bw);
    x = allpass(&dl[L_IN1], PS(142), P->bw, 0.75f);
    x = allpass(&dl[L_IN2], PS(107), x, 0.75f);
    x = allpass(&dl[L_IN3], PS(379), x, 0.625f);
    x = allpass(&dl[L_IN4], PS(277), x, 0.625f);
    dpush(&dl[L_PRER], in_r);
    xr = tap(&dl[L_PRER], P_PRE);
    P->bw_r += 0.84f * (xr - P->bw_r);
    xr = allpass(&dl[L_IN1R], PS(142), P->bw_r, 0.75f);
    xr = allpass(&dl[L_IN2R], PS(107), xr, 0.75f);
    xr = allpass(&dl[L_IN3R], PS(379), xr, 0.625f);
    xr = allpass(&dl[L_IN4R], PS(277), xr, 0.625f);
    s = P->lfo_s + w * P->lfo_c;
    c = P->lfo_c - w * s;
    P->lfo_s = s;
    P->lfo_c = c;
    a = allpass_mod(&dl[L_APL], (float)PS(672) + exc * s, x + decay * fb_l, -0.7f);
    dpush(&dl[L_D1L], a);
    b = tap(&dl[L_D1L], PS(4453));
    P->damp_l += dk * (b - P->damp_l);
    b = allpass(&dl[L_AP2L], PS(1800), P->damp_l * decay, 0.5f);
    dpush(&dl[L_D2L], b);
    a = allpass_mod(&dl[L_APR], (float)PS(908) + exc * c, xr + decay * fb_r, -0.7f);
    dpush(&dl[L_D1R], a);
    b = tap(&dl[L_D1R], PS(4217));
    P->damp_r += dk * (b - P->damp_r);
    b = allpass(&dl[L_AP2R], PS(2656), P->damp_r * decay, 0.5f);
    dpush(&dl[L_D2R], b);
    *ol = 0.6f * (tap(&dl[L_D1R], PS(266)) + tap(&dl[L_D1R], PS(2974)) - tap(&dl[L_AP2R], PS(1913)) +
                  tap(&dl[L_D2R], PS(1996)) - tap(&dl[L_D1L], PS(1990)) - tap(&dl[L_AP2L], PS(187)) -
                  tap(&dl[L_D2L], PS(1066)));
    *or_ = 0.6f * (tap(&dl[L_D1L], PS(353)) + tap(&dl[L_D1L], PS(3627)) - tap(&dl[L_AP2L], PS(1228)) +
                   tap(&dl[L_D2L], PS(2673)) - tap(&dl[L_D1R], PS(2111)) - tap(&dl[L_AP2R], PS(335)) -
                   tap(&dl[L_D2R], PS(121)));
    if (oct > 0.0f) {
        sh_buf[P->sh_i] = (*ol + *or_) * 0.5f;
        P->sh_i = (P->sh_i + 1) & (SH_W - 1);
    }
}
/* ------------------------------------------------------------ oscillators --- */
RAMC static float wavefold(float x)
{
    int i;
    for (i = 0; i < 8; ++i) {
        if (x > 1.0f)
            x = 2.0f - x;
        else if (x < -1.0f)
            x = -2.0f - x;
        else
            break;
    }
    return x;
}
RAMC static float osc_sample(int waveform, float phase, float phase_inc, float shape)
{
    switch (waveform) {
    case WAVE_SINE:
        return wavefold((1.0f + shape * 5.0f) * fm_sin_turns(phase));
    case WAVE_TRIANGLE: {
        float peak = 0.5f * (1.0f - shape);
        if (peak < 1e-4f)
            peak = 1e-4f;
        if (phase < peak)
            return (phase / peak) * 2.0f - 1.0f;
        return 1.0f - ((phase - peak) / (1.0f - peak)) * 2.0f;
    }
    case WAVE_SAW: {
        float s = 2.0f * phase - 1.0f;
        s -= poly_blep(phase, phase_inc);
        if (shape > 0.0f) {
            float p2 = phase * 2.0f, s2;
            if (p2 >= 1.0f)
                p2 -= 1.0f;
            s2 = 2.0f * p2 - 1.0f;
            s2 -= poly_blep(p2, phase_inc * 2.0f);
            s = s * (1.0f - shape) + s2 * shape;
        }
        return s;
    }
    case WAVE_SQUARE:
    case WAVE_PULSE_TRAIN: {
        float pw = waveform == WAVE_SQUARE ? 0.05f + shape * 0.9f : 0.02f + shape * 0.28f;
        float t2 = phase + (1.0f - pw), s;
        if (t2 >= 1.0f)
            t2 -= 1.0f;
        s = phase < pw ? 1.0f : -1.0f;
        s += poly_blep(phase, phase_inc);
        s -= poly_blep(t2, phase_inc);
        return s;
    }
    case WAVE_WAVETABLE: {
        /* the partials' sines by the recurrence sin(h t) = 2 cos t sin((h-1) t) - sin((h-2) t) */
        float s1 = fm_sin_turns(phase), s = s1, c2 = 2.0f * fm_sin_turns(phase + 0.25f), a = 0.0f, b = s1;
        int N = 1 + (int)(shape * 7.0f), h;
        static const float INV[9] = {0.0f, 1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 0.2f, 1.0f / 6.0f, 1.0f / 7.0f, 0.125f};
        for (h = 2; h <= N; ++h) {
            float sh = c2 * b - a;
            a = b;
            b = sh;
            s += sh * INV[h];
        }
        return s * 0.5f;
    }
    default:
        return 0.0f;
    }
}

/* ------------------------------------------------------------------ notes --- */
RAMC static void release_all_voices(void)
{
    int i;
    for (i = 0; i < NUM_VOICES; ++i) {
        voice_t *v = &S.voices[i];
        if (v->active) {
            v->gate = 0;
            if (v->env.stage == ENV_HOLD)
                v->env.stage = ENV_RELEASE;
        }
    }
}

RAMC static void voice_gains(voice_t *v)       /* its pan and gain for the block (the original's per sample: the same) */
{
    float pan = clampf(0.5f + v->pan_offset * S.width + S.pan_morph_pans[v->chord_step] * 0.5f, 0.0f, 1.0f);
    float g = v->velocity * (1.0f / (float)CHORD_SIZE) * S.morph_gains[v->chord_step] * S.mixer_trims[v->chord_step];
    v->g_l = g * fm_sqrtf(1.0f - pan);
    v->g_r = g * fm_sqrtf(pan);
}

RAMC static void voice_start(voice_t *v, int root_note, int interval_semis, float detune_cents, int velocity, int step)
{
    float hz = midi_to_hz(root_note + interval_semis), target_inc;
    int glide_active, was = v->active && v->env.value >= 0.001f;   /* (stealing a voice still sounding) */
    v->active = 1;
    v->gate = 1;
    v->root_note = root_note;
    v->note = root_note + interval_semis;
    v->chord_step = step;
    v->waveform = S.waveforms[step];
    if (!was)                             /* (a voice still sounding, stolen: its phase goes on, the wave continuous, no
                                           * click; the original started it at 0) */
        v->phase = 0.0f;
    if (detune_cents != 0.0f)
        hz *= fm_exp2f(detune_cents / 1200.0f);
    target_inc = hz / SR;
    v->target_phase_inc = target_inc;
    glide_active = S.has_prev_chord && S.glide_rate > 0.0f;
    if (glide_active && S.glide_legato && !S.prev_chord_still_held)
        glide_active = 0;
    if (glide_active) {
        float start = S.prev_phase_inc[step], glide_samples = S.glide_rate * GLIDE_TIME_MAX_S * SR;
        if (glide_samples < 1.0f)
            glide_samples = 1.0f;
        v->phase_inc = start;
        v->glide_step = (target_inc - start) / glide_samples;
    } else {
        v->phase_inc = target_inc;
        v->glide_step = 0.0f;
    }
    v->velocity = (float)velocity / 127.0f;
    v->pan_offset = (float)step / (float)(CHORD_SIZE - 1) - 0.5f;
    voice_gains(v);                       /* (the arp starts chords inside a block) */
    if (!was) {                           /* (a silent voice starts at its gain; a stolen one glides to it) */
        v->g0_l = v->g_l;
        v->g0_r = v->g_r;
    }
    env_recompute_rates(&v->env, S.attack, S.release);
    if (S.vca_hard_reset)
        v->env.value = 0.0f;
    v->env.stage = ENV_ATTACK;
}

/* the chord's four intervals from its root, voice order (the original's chord_on: the type or the intervals or the map,
 * spread, rotated, snapped to the scale) */
RAMC static void chord_intervals(int root_note, int *out)
{
    int chord = S.chord_type, rotation, i, buf[CHORD_SIZE];
    float spread_mult;
    if (S.tuning_mode == 2) {
        int pc = root_note % 12;
        if (pc < 0)
            pc += 12;
        chord = S.chord_pc[pc];
    }
    if (chord < 0)
        chord = 0;
    if (chord >= CHD_NCHORD)
        chord = CHD_NCHORD - 1;
    for (i = 0; i < CHORD_SIZE; i++)
        buf[i] = S.tuning_mode == 1 ? (i ? S.interval[i - 1] : 0) : CHD_CHORD_TABLE[chord][i];
    spread_mult = S.chord_spread * 2.0f;
    rotation = (int)(S.chord_rotation * 3.999f);
    if (rotation < 0)
        rotation = 0;
    if (rotation >= CHORD_SIZE)
        rotation = CHORD_SIZE - 1;
    for (i = 0; i < CHORD_SIZE; ++i) {
        int src = (i + rotation) % CHORD_SIZE, scaled = (int)((float)buf[src] * spread_mult);
        if (S.scale_index > 0)
            scaled = scale_quantize(root_note + scaled, S.scale_index, S.scale_root) - root_note;
        out[i] = scaled;
    }
}

/* a voicing setting turned while a chord sounds: the chord playing takes it now (its pitches, its waves), its
 * envelopes running on; the original took it at the next chord */
RAMC static void revoice(void)
{
    int iv[CHORD_SIZE], i, root;
    voice_t *v0 = &S.voices[S.cur_base];
    for (i = 0; i < NUM_VOICES; i++)       /* every voice sounding, tails too: its wave */
        if (S.voices[i].active)
            S.voices[i].waveform = S.waveforms[S.voices[i].chord_step];
    if (!S.has_prev_chord || !v0->active)
        return;
    root = v0->root_note;
    chord_intervals(root, iv);
    for (i = 0; i < CHORD_SIZE; i++) {
        voice_t *v = &S.voices[S.cur_base + i];
        float hz = midi_to_hz(root + iv[i]), cents = (float)i * S.detune * MAX_DETUNE_CENTS;
        if (!v->active || v->root_note != root)
            continue;
        if (cents != 0.0f)
            hz *= fm_exp2f(cents / 1200.0f);
        v->note = root + iv[i];
        v->target_phase_inc = v->phase_inc = hz / SR;
        v->glide_step = 0.0f;
        S.prev_phase_inc[i] = v->target_phase_inc;
    }
}

/* the screen's: the chord a key would play (its four voices' notes), as the render would build it now */
RAMC void chd_voicing(int root, int *notes)
{
    int iv[CHORD_SIZE], i;
    chord_intervals(root, iv);
    for (i = 0; i < CHORD_SIZE; i++)
        notes[i] = root + iv[i];
}

RAMC static void chord_on(int root_note, int velocity)
{
    int base = S.next_chord_base, i, iv[CHORD_SIZE];
    {   /* the bank: the next in turn, unless it still sounds: then the quietest of the others (the original took the
         * next in turn, a tail cut short) */
        float lo = 2.0f;
        int b, k;
        for (k = 0; k < NUM_VOICES / CHORD_SIZE; k++) {
            float loud = 0.0f;
            b = (S.next_chord_base + k * CHORD_SIZE) % NUM_VOICES;
            if (b == S.cur_base && S.has_prev_chord)
                continue;
            for (i = 0; i < CHORD_SIZE; i++)
                if (S.voices[b + i].active && S.voices[b + i].env.value > loud)
                    loud = S.voices[b + i].env.value;
            if (loud < 0.001f) {
                base = b;
                break;
            }
            if (loud < lo)
                lo = loud, base = b;
        }
    }
    release_all_voices();
    chord_intervals(root_note, iv);
    for (i = 0; i < CHORD_SIZE; ++i)
        voice_start(&S.voices[base + i], root_note, iv[i], (float)i * S.detune * MAX_DETUNE_CENTS, velocity, i);
    S.cur_base = base;
    S.next_chord_base = (base + CHORD_SIZE) % NUM_VOICES;
    for (i = 0; i < CHORD_SIZE; ++i)
        S.prev_phase_inc[i] = S.voices[base + i].target_phase_inc;
    S.has_prev_chord = 1;
    env_recompute_rates(&S.filter_env, S.filter_env_attack, S.filter_env_decay);
    if (S.fenv_hard_reset)
        S.filter_env.value = 0.0f;
    S.filter_env.stage = ENV_ATTACK;
    S.vib_ramp_value = 0.0f;
    S.sweep_value = S.sweep_amount * SWEEP_MAX_SEMITONES;
    if (S.shape_lfo_mode != LFOMODE_FREE) {
        S.lfo_phase = 0.0f;
        S.shape_lfo_done = 0;
    }
    if (S.filter_lfo_mode != LFOMODE_FREE) {
        S.filter_lfo_phase = 0.0f;
        S.filter_lfo_done = 0;
    }
    if (S.lm_lfo_mode != LFOMODE_FREE) {
        S.lm_lfo_phase = 0.0f;
        S.lm_lfo_done = 0;
    }
    if (S.pm_lfo_mode != LFOMODE_FREE) {
        S.pm_lfo_phase = 0.0f;
        S.pm_lfo_done = 0;
    }
}

RAMC static void chord_off(int root_note)
{
    int i;
    for (i = 0; i < NUM_VOICES; ++i) {
        voice_t *v = &S.voices[i];
        if (v->active && v->root_note == root_note) {
            v->gate = 0;
            if (v->env.stage == ENV_HOLD)
                v->env.stage = ENV_RELEASE;
        }
    }
}

RAMC static void held_push(int note)
{
    int i, j;
    for (i = 0; i < S.held_count; ++i)
        if (S.held_notes[i] == note) {
            for (j = i; j < S.held_count - 1; ++j)
                S.held_notes[j] = S.held_notes[j + 1];
            S.held_count--;
            break;
        }
    if (S.held_count < HELD_STACK_MAX)
        S.held_notes[S.held_count++] = note;
}
RAMC static int held_pop(int note)              /* 1: it was the top of the stack (sounding) */
{
    int top, i, j;
    if (S.held_count == 0)
        return 0;
    top = S.held_notes[S.held_count - 1];
    for (i = 0; i < S.held_count; ++i)
        if (S.held_notes[i] == note) {
            for (j = i; j < S.held_count - 1; ++j)
                S.held_notes[j] = S.held_notes[j + 1];
            S.held_count--;
            return note == top;
        }
    return 0;
}

RAMC static void arp_step_and_fire(void)
{
    chd_arp_steps++;
    int hc = S.held_count, fire;
    if (hc <= 0)
        return;
    switch (S.arp_direction) {
    case ARP_UP: S.arp_step_idx = (S.arp_step_idx + 1) % hc; break;
    case ARP_DOWN:
        if (--S.arp_step_idx < 0)
            S.arp_step_idx = hc - 1;
        break;
    case ARP_UPDOWN:
        if (hc <= 1)
            S.arp_step_idx = 0;
        else {
            S.arp_step_idx += S.arp_step_dir;
            if (S.arp_step_idx >= hc) {
                S.arp_step_idx = hc - 2;
                S.arp_step_dir = -1;
            } else if (S.arp_step_idx < 0) {
                S.arp_step_idx = 1;
                S.arp_step_dir = 1;
            }
        }
        break;
    case ARP_RANDOM: S.arp_step_idx = (int)(rnd() % (uint32_t)hc); break;
    }
    if (S.arp_step_idx < 0)
        S.arp_step_idx = 0;
    if (S.arp_step_idx >= hc)
        S.arp_step_idx = hc - 1;
    fire = S.arp_pattern[S.arp_pattern_pos];
    S.arp_pattern_pos = (S.arp_pattern_pos + 1) % S.arp_euclid_steps;
    if (S.arp_pattern_pos == 0 && S.arp_variations > 1)
        S.arp_variation_idx = (S.arp_variation_idx + 1) % S.arp_variations;
    if (fire) {
        int note = S.held_notes[S.arp_step_idx] + S.arp_variation_idx * S.arp_variation_interval;
        chord_on(note < 0 ? 0 : note > 127 ? 127 : note, 100);
    }
}

RAMC static float ctrl_from_pressure(void) { return S.vel_baseline + (1.0f - S.vel_baseline) * S.aftertouch; }

/* the arp follows MIDI clock: asked to, and a clock is running and coming in (none for half a second: its own tempo) */
RAMC static int arp_synced(void) { return S.arp_clock_sync && S.arp_clock_running && S.clock_age < (uint32_t)(SR / 2); }

RAMC void chd_midi(uint32_t raw, uint32_t d1, uint32_t d2)   /* the original's on_midi */
{
    uint32_t status = raw & 0xF0u;
    d1 &= 0x7Fu;
    d2 &= 0x7Fu;
    if (raw == 0xF8u) {                   /* MIDI clock: the arp, synced */
        chd_clock_ticks++;
        S.clock_age = 0;
        if (S.arp_enabled && S.arp_clock_sync && S.arp_clock_running && S.held_count > 0) {
            if (++S.arp_clock_count >= CLOCKS_PER_STEP[S.arp_clock_division < 6 ? S.arp_clock_division : 5]) {
                S.arp_clock_count = 0;
                arp_step_and_fire();
            }
        }
        return;
    }
    if (raw == 0xFAu) {
        chd_clock_ticks = 0;
        S.arp_clock_running = 1;
        S.arp_clock_count = 0;
        return;
    }
    if (raw == 0xFBu) {
        S.arp_clock_running = 1;
        return;
    }
    if (raw == 0xFCu) {
        S.arp_clock_running = 0;
        return;
    }
    if (status == 0x90u && d2 > 0) {
        S.prev_chord_still_held = S.held_count > 0;
        held_push((int)d1);
        switch (S.ctrl_source) {
        case CTRL_RANDOM: S.ctrl_value_target = (float)(rnd() & 0xFFFFu) / 32768.0f - 1.0f; break;
        case CTRL_COIN_TOSS: S.ctrl_value_target = (rnd() & 1u) ? 1.0f : -1.0f; break;
        case CTRL_VELOCITY: S.ctrl_value_target = (float)d2 / 127.0f; break;
        case CTRL_AFTERTOUCH:
            S.vel_baseline = (float)d2 / 127.0f * 0.8f;
            S.ctrl_value_target = ctrl_from_pressure();
            S.ctrl_value = S.ctrl_value_target;
            break;
        default: break;
        }
        chord_on((int)d1, (int)d2);
        if (S.arp_enabled) {
            S.arp_step_idx = S.held_count - 1;
            S.arp_step_dir = 1;
            S.arp_sample_counter = S.arp_step_period;
        }
    } else if (status == 0x80u || status == 0x90u) {
        int was_top;
        if (S.arp_enabled && S.arp_hold)
            return;
        was_top = held_pop((int)d1);
        if (S.arp_enabled) {
            if (S.held_count == 0)
                chord_off((int)d1);
        } else if (was_top) {
            if (S.held_count > 0)
                chord_on(S.held_notes[S.held_count - 1], 100);
            else
                chord_off((int)d1);
        }
    } else if (status == 0xA0u) {
        if (S.held_count > 0 && (int)d1 == S.held_notes[S.held_count - 1]) {
            S.aftertouch = (float)d2 / 127.0f;
            if (S.ctrl_source == CTRL_AFTERTOUCH)
                S.ctrl_value_target = ctrl_from_pressure();
        }
    } else if (status == 0xD0u) {
        S.aftertouch = (float)d1 / 127.0f;
        if (S.ctrl_source == CTRL_AFTERTOUCH)
            S.ctrl_value_target = ctrl_from_pressure();
    } else if (status == 0xB0u) {
        if (d1 == 123u) {
            S.held_count = 0;
            release_all_voices();
        } else if (S.ctrl_source == CTRL_CC && (int)d1 == S.ctrl_cc) {
            S.ctrl_value_target = (float)d2 / 127.0f;
        }
    }
}

/* ------------------------------------------------------------------ queue --- */
/* the main loop's events, taken by the render at the start of a block: one writer, one reader */
enum { EV_PARAM, EV_PATCH, EV_MIDI, EV_PANIC };
typedef struct { uint8_t t, a, b, c; int16_t v; } ev_t;
#define NQ 128u
static ev_t q[NQ];
static volatile uint32_t q_w, q_r;
static chd_patch_t pend_patch;            /* EV_PATCH's: written before the event, read by the render */
static volatile uint8_t pend_busy;

static int push(uint32_t t, uint32_t a, uint32_t b, uint32_t c, int v)
{
    uint32_t w = q_w;
    if (w - q_r >= NQ)
        return -1;
    q[w % NQ].t = (uint8_t)t;
    q[w % NQ].a = (uint8_t)a;
    q[w % NQ].b = (uint8_t)b;
    q[w % NQ].c = (uint8_t)c;
    q[w % NQ].v = (int16_t)v;
    BARRIER();
    q_w = w + 1u;
    return 0;
}
int chd_set(int p, int v) { return p >= 0 && p < CP_N ? push(EV_PARAM, (uint32_t)p, 0, 0, v) : -1; }
int chd_patch(const chd_patch_t *pt)
{
    if (pend_busy)
        return -1;
    pend_patch = *pt;
    pend_busy = 1;
    BARRIER();
    if (push(EV_PATCH, 0, 0, 0, 0)) {
        pend_busy = 0;
        return -1;
    }
    return 0;
}
int chd_note_on(int note, int vel) { return push(EV_MIDI, 0x90u, (uint32_t)note & 127u, (uint32_t)(vel < 1 ? 1 : vel) & 127u, 0); }
int chd_note_off(int note) { return push(EV_MIDI, 0x80u, (uint32_t)note & 127u, 0, 0); }
int chd_pressure(int note, int v) { return push(EV_MIDI, 0xA0u, (uint32_t)note & 127u, (uint32_t)v & 127u, 0); }
int chd_panic(void) { return push(EV_PANIC, 0, 0, 0, 0); }

RAMC static void panic_now(void)
{
    int i;
    S.held_count = 0;
    for (i = 0; i < NUM_VOICES; i++) {
        S.voices[i].active = 0;
        S.voices[i].env.stage = ENV_IDLE;
        S.voices[i].env.value = 0.0f;
    }
}

RAMC static void drain(void)
{
    while (q_r != q_w) {
        const ev_t *e = &q[q_r % NQ];
        BARRIER();
        if (e->t == EV_PARAM)
            set_param(e->a, e->v);
        else if (e->t == EV_PATCH) {
            patch_apply(pend_patch.p);
            pend_busy = 0;
        } else if (e->t == EV_MIDI)
            chd_midi(e->a, e->b, e->c);
        else if (e->t == EV_PANIC)
            panic_now();
        BARRIER();
        q_r = q_r + 1u;
    }
}

RAMC void chd_init(void)
{
    int i;
    unsigned char *z = (unsigned char *)&S;
    for (i = 0; i < (int)sizeof S; i++)   /* (no libc: the state cleared by hand) */
        z[i] = 0;
    for (i = 0; i < CP_N; i++)
        S.par[i] = CHD_PARAM[i].def;
    S.rng = 22222u;
    S.g_eff_l = S.g_eff_r = -1.0f;
    S.ladder_l.g = S.ladder_r.g = -1.0f;
    S.arp_step_dir = 1;
    S.arp_euclid_steps = 16;
    S.arp_variations = 1;
    for (i = 0; i < CHORD_SIZE; i++)
        S.morph_gains[i] = 1.0f;
    for (i = 0; i < NUM_VOICES; i++)
        S.voices[i].env.stage = ENV_IDLE;
    for (i = 0; i < 4; i++)
        chd_sounding[i] = -1;
    S.filter_env.stage = ENV_IDLE;
    {
        int16_t d[CP_N];
        for (i = 0; i < CP_N; i++)
            d[i] = CHD_PARAM[i].def;
        patch_apply(d);
    }
    {
        float *b = pl_buf;
        for (i = 0; i < L_N; i++) {
            plate.d[i].b = b;
            plate.d[i].n = PL_LEN[i];
            plate.d[i].i = 0;
            b += PL_LEN[i];
        }
        for (i = 0; i < (int)PL_TOTAL; i++)
            pl_buf[i] = 0.0f;
        plate.bw = plate.bw_r = plate.damp_l = plate.damp_r = plate.lfo_s = plate.acc = plate.acc_r = 0.0f;
        plate.prev_l = plate.prev_r = plate.cur_l = plate.cur_r = 0.0f;
        plate.lfo_c = 1.0f;
        plate.odd = 0;
        plate.sh_i = 0;
        plate.sh_d = (float)(SH_W / 2);
        plate.sh_lp = plate.sh_hp = plate.sh_x1 = 0.0f;
        for (i = 0; i < SH_W; i++)
            sh_buf[i] = 0.0f;
    }
    for (i = 0; i < DLY_SIZE; i++)
        dly_l[i] = dly_r[i] = 0;
}

/* ------------------------------------------------------------------ delay --- */
/* the line's samples as half-precision floats in 16 bits (11 bits of precision at every level: a quiet tail keeps
 * them, where fixed 16 bits left it a few). Below 6e-5 (-84 dB) a sample is 0 */
RAMC static uint16_t f2h(float f)
{
    union { float f; uint32_t u; } b;
    uint32_t sign, e, m;
    b.f = f;
    sign = (b.u >> 16) & 0x8000u;
    b.u += 0x1000u;                       /* (round the mantissa to 10 bits) */
    e = (b.u >> 23) & 0xFFu;
    m = (b.u >> 13) & 0x3FFu;
    if (e < 113u)
        return (uint16_t)sign;
    if (e > 142u)
        return (uint16_t)(sign | 0x7BFFu);
    return (uint16_t)(sign | (e - 112u) << 10 | m);
}
RAMC static float h2f(uint16_t h)
{
    union { float f; uint32_t u; } b;
    if (!(h & 0x7FFFu))
        return 0.0f;
    b.u = (uint32_t)(h & 0x8000u) << 16 | (((uint32_t)h >> 10 & 31u) + 112u) << 23 | (uint32_t)(h & 0x3FFu) << 13;
    return b.f;
}
/* the pitch shifter of Zenith and Interval: the line read PS_W behind the echo's tap and less, two heads half a window
 * apart crossfading, their distance closing at the shift's rate (each repeat shifted again) */
#define PS_W 1024
RAMC static float shift_read(const uint16_t *buf, int at, float d)
{
    int k = (int)d;
    float f = d - (float)k, a = h2f(buf[(at - k) & DLY_MASK]), b = h2f(buf[(at - k - 1) & DLY_MASK]);
    return a + (b - a) * f;
}
/* one step of the original's delay at half the rate: in, the pair's average; out, the repeats */
RAMC static void delay_step(float in_l, float in_r, float *o_l, float *o_r)
{
    float delay_s = DELAY_TIME_MIN_S + S.delay_time * (DELAY_TIME_MAX_S - DELAY_TIME_MIN_S);
    int ds = (int)(delay_s * SR) / 2, w = S.delay_write_idx, rd;
    float wet_l, wet_r, lp, k, fb, out_l, out_r;
    if (ds < 1)
        ds = 1;
    if (ds >= DLY_SIZE - PS_W - 2)
        ds = DLY_SIZE - PS_W - 2;
    rd = (w - ds) & DLY_MASK;
    wet_l = h2f(dly_l[rd]);
    wet_r = h2f(dly_r[rd]);
    lp = 0.05f + S.delay_tone * 0.95f;
    k = 1.0f - (1.0f - lp) * (1.0f - lp);   /* two of the original's steps in one */
    S.delay_lp_l = k * wet_l + (1.0f - k) * S.delay_lp_l;
    S.delay_lp_r = k * wet_r + (1.0f - k) * S.delay_lp_r;
    fb = S.delay_feedback * DELAY_FEEDBACK_MAX;
    out_l = wet_l;
    out_r = wet_r;
    switch (S.delay_mode) {
    case DELAY_PINGPONG:
    case DELAY_FLIPFLOP:
        in_l = (in_l + in_r) * 0.5f + S.delay_lp_r * fb;
        in_r = S.delay_lp_l * fb;
        if (S.delay_mode == DELAY_FLIPFLOP) {
            if (w % ds == 0)
                S.delay_flip_counter++;
            if (S.delay_flip_counter & 1) {
                float t = out_l;
                out_l = out_r;
                out_r = t;
            }
        }
        break;
    case DELAY_LONG:
        in_l = in_r = (in_l + in_r) * 0.5f + (S.delay_lp_l + S.delay_lp_r) * 0.5f * fb;
        out_l = out_r = (wet_l + wet_r) * 0.5f;
        break;
    case DELAY_ZENITH:
    case DELAY_INTERVAL: {
        /* (the original read the line at a jump each sample: fragments, not a shift. Here a shifter: the repeats
         * Zenith an octave up, Interval by its setting, each one again) */
        float semis = S.delay_mode == DELAY_ZENITH ? 12.0f : S.delay_mod_depth * 24.0f - 12.0f;
        float d1, d2, g1, sl, sr;
        int at = (w - ds) & DLY_MASK;
        S.delay_grain_phase -= fm_exp2f(semis / 12.0f) - 1.0f;   /* (the heads' distance behind the tap, closing) */
        if (S.delay_grain_phase < 0.0f)
            S.delay_grain_phase += (float)PS_W;
        if (S.delay_grain_phase >= (float)PS_W)
            S.delay_grain_phase -= (float)PS_W;
        d1 = S.delay_grain_phase;
        d2 = d1 + (float)(PS_W / 2);
        if (d2 >= (float)PS_W)
            d2 -= (float)PS_W;
        g1 = 1.0f - fm_fabsf(2.0f * d1 / (float)PS_W - 1.0f);   /* (a triangle: 0 at the window's ends) */
        sl = shift_read(dly_l, at, d1) * g1 + shift_read(dly_l, at, d2) * (1.0f - g1);
        sr = shift_read(dly_r, at, d1) * g1 + shift_read(dly_r, at, d2) * (1.0f - g1);
        S.dly_ps_l = k * sl + (1.0f - k) * S.dly_ps_l;          /* (the tone's lowpass on the shifted repeats too) */
        S.dly_ps_r = k * sr + (1.0f - k) * S.dly_ps_r;
        in_l += S.dly_ps_l * fb;
        in_r += S.dly_ps_r * fb;
        break;
    }
    default:
        in_l += S.delay_lp_l * fb;
        in_r += S.delay_lp_r * fb;
        break;
    }
    dly_l[w] = f2h(in_l);
    dly_r[w] = f2h(in_r);
    S.delay_write_idx = (w + 1) & DLY_MASK;
    *o_l = out_l;
    *o_r = out_r;
}

/* ----------------------------------------------------------------- render --- */
RAMC static void advance_block_lfo(float *phase, float inc, int mode, uint8_t *done, uint32_t n)
{
    if (*done)
        return;
    *phase += inc * (float)n;
    if (*phase >= 1.0f) {
        if (mode == LFOMODE_ONE_SHOT) {
            *phase = 1.0f;
            *done = 1;
        } else
            while (*phase >= 1.0f)
                *phase -= 1.0f;
    }
}

/* the block in segments (an arp step starts a chord inside a block: a segment ends there). Each segment in three
 * passes: (A) what all voices share, a value a sample, into arrays; (B) each voice over the whole segment, its state in
 * locals (on the FM-1 a load from memory costs as much as five operations: voice by voice, the state stays in
 * registers); (C) the filter and the effects, a sample at a time, as the original. The arithmetic is the original's;
 * FM's coupling between voices (a carrier reads the modulator's latest sample) is kept exactly: the modulators go
 * first and the carriers read what the original's order would have them read. */
#define SEGMAX 128
#define NBANK (NUM_VOICES / CHORD_SIZE)
static float sg_cv[SEGMAX], sg_ats[SEGMAX], sg_lfo[SEGMAX], sg_rat[3][SEGMAX], sg_fm[SEGMAX], sg_shc[SEGMAX];
static float sg_l[SEGMAX], sg_r[SEGMAX], sg_modv[NBANK][SEGMAX], sg_modend[SEGMAX];
static uint8_t sg_moda[NBANK][SEGMAX];

RAMC static void seg_shared(uint32_t n)   /* pass A */
{
    float cv = S.ctrl_value, ats = S.at_smooth, lp = S.lfo_phase, vp = S.vibrato_phase, ramp = S.vib_ramp_value;
    float sw = S.sweep_value;
    uint32_t i;
    for (i = 0; i < n; i++) {
        float vib_depth_eff, vib_cents;
        cv += (S.ctrl_value_target - cv) * 0.005f;
        ats += (S.aftertouch - ats) * 0.005f;
        if (!S.shape_lfo_done) {
            lp += S.lfo_phase_inc;
            if (lp >= 1.0f) {
                if (S.shape_lfo_mode == LFOMODE_ONE_SHOT) {
                    lp = 1.0f;
                    S.shape_lfo_done = 1;
                } else
                    lp -= 1.0f;
            }
        }
        vp += S.vibrato_phase_inc;
        if (vp >= 1.0f)
            vp -= 1.0f;
        if (ramp < 1.0f) {
            ramp += S.vib_ramp_inc;
            if (ramp > 1.0f)
                ramp = 1.0f;
        }
        vib_depth_eff = clampf(S.vib_depth + cv * S.ctrl_to_vib * 0.5f, 0.0f, 1.0f);
        if (S.vib_stray) {
            if (S.vib_random_counter <= 0) {
                int period = (int)(SR / (vib_speed_to_hz(S.vib_speed) * 2.0f));
                S.vib_random_target = (float)(rnd() & 0xFFFFu) / 32768.0f - 1.0f;
                S.vib_random_counter = period < 16 ? 16 : period;
            }
            S.vib_random_counter--;
            S.vib_random_value += (S.vib_random_target - S.vib_random_value) * 0.002f;
            vib_cents = S.vib_random_value;
        } else
            vib_cents = vib_depth_eff > 0.0f ? fm_sin_turns(vp) : 0.0f;
        vib_cents *= ramp * vib_depth_eff * VIB_DEPTH_MAX_CENTS;
        if (sw != 0.0f) {
            sw *= S.sweep_coef;
            if (sw < 1e-4f && sw > -1e-4f)
                sw = 0.0f;
        }
        /* the pitch ratios: vibrato alone, the sweep alone, both (a voice's bits 0 and 1, less one, index them) */
        sg_rat[0][i] = vib_cents != 0.0f ? fm_exp2f(vib_cents * ONE_OVER_1200) : 1.0f;
        sg_rat[1][i] = sw != 0.0f ? fm_exp2f(sw * ONE_OVER_12) : 1.0f;
        sg_rat[2][i] = vib_cents != 0.0f || sw != 0.0f ? fm_exp2f(vib_cents * ONE_OVER_1200 + sw * ONE_OVER_12) : 1.0f;
        sg_fm[i] = clampf(S.fm_amount + cv * S.ctrl_to_fm * 0.5f, 0.0f, 1.0f);
        sg_shc[i] = cv * S.ctrl_to_shape * 0.5f;
        sg_cv[i] = cv;
        sg_ats[i] = ats;
        sg_lfo[i] = lp;
        sg_l[i] = sg_r[i] = 0.0f;
    }
    S.ctrl_value = cv;
    S.at_smooth = ats;
    S.lfo_phase = lp;
    S.vibrato_phase = vp;
    S.vib_ramp_value = ramp;
    S.sweep_value = sw;
}

/* pass B for one voice. mod: its row in the modulators' arrays (a modulator), else -1; rows/nr: a carrier's
 * modulators before it in the original's order (nr -1: no FM read) */
RAMC static void seg_voice(voice_t *v, uint32_t n, int mod, const int *rows, int nr, float *ol, float *or_)
{
    int stage = v->env.stage, gate = v->gate, step = v->chord_step, wave = v->waveform, vmode = S.vca_mode;
    int ri = ((S.vib_osc_enable >> step) & 1) | ((S.sweep_osc_enable >> step) & 1) << 1;
    int lshape = S.lfo_shape, drone = S.vca_drone, fmpos = S.fm_position;
    float value = v->env.value, ainc = v->env.attack_inc, rcoef = v->env.release_coef;
    float phase = v->phase, pinc = v->phase_inc, gstep = v->glide_step, tgt = v->target_phase_inc;
    float shp = S.shapes[step], off = S.shape_lfo_phase_offsets[step], ldep = S.lfo_depth, fma = S.fm_amounts[step];
    float gl = v->g0_l, gr = v->g0_r, mg = S.morph_gains[step] * S.mixer_trims[step];
    float dgl = (v->g_l - gl) / (float)n, dgr = (v->g_r - gr) / (float)n;   /* (0 when nothing changed: the same sums) */
    const float *rat = ri ? sg_rat[ri - 1] : 0;
    int moving = ldep != 0.0f || S.ctrl_to_shape != 0.0f;   /* (else the shape stays: worked out once) */
    float eff_shape = clampf(shp, 0.0f, 1.0f);
    uint32_t i;
    for (i = 0; i < n; i++) {
        float inc, ph, pu, s;
        switch (stage) {
        case ENV_ATTACK:
            value += ainc;
            if (value >= 1.0f) {
                value = 1.0f;
                stage = vmode == ENVMODE_ASR ? ENV_HOLD : ENV_RELEASE;
            }
            break;
        case ENV_HOLD:
            if (!gate)
                stage = ENV_RELEASE;
            break;
        case ENV_RELEASE:
            value *= rcoef;
            if (value < ENV_SILENCE) {
                value = 0.0f;
                stage = gate && vmode == ENVMODE_LOOPING ? ENV_ATTACK : ENV_IDLE;
            }
            break;
        default:
            break;
        }
        if (stage == ENV_IDLE) {
            v->active = 0;
            break;
        }
        if (gstep != 0.0f) {
            pinc += gstep;
            if ((gstep > 0.0f && pinc >= tgt) || (gstep < 0.0f && pinc <= tgt)) {
                pinc = tgt;
                gstep = 0.0f;
            }
        }
        inc = rat ? pinc * rat[i] : pinc;
        if (moving) {
            ph = sg_lfo[i] + off;
            ph -= fm_floorf(ph);
            eff_shape = clampf(shp + lfo_sample(lshape, ph) * ldep + sg_shc[i], 0.0f, 1.0f);
        }
        pu = phase;
        if (nr >= 0 && sg_fm[i] > 0.0f) {
            float m = i ? sg_modend[i - 1] : S.last_modulator_sample;
            int k;
            for (k = nr - 1; k >= 0; k--)
                if (sg_moda[rows[k]][i]) {
                    m = sg_modv[rows[k]][i];
                    break;
                }
            pu += m * sg_fm[i] * fma;
            pu -= fm_floorf(pu);
        }
        s = osc_sample(wave, pu, inc, eff_shape);
        phase += inc;
        if (phase >= 1.0f)
            phase -= 1.0f;
        else if (phase < 0.0f)
            phase += 1.0f;
        if (mod >= 0) {
            sg_modv[mod][i] = fmpos == 1 ? s * mg : s;
            sg_moda[mod][i] = 1;
        }
        s *= drone ? 1.0f : value;
        gl += dgl;
        gr += dgr;
        ol[i] += s * gl;
        or_[i] += s * gr;
    }
    v->env.stage = stage;
    v->env.value = value;
    v->g0_l = v->g_l;
    v->g0_r = v->g_r;
    v->phase = phase;
    v->phase_inc = pinc;
    v->glide_step = gstep;
}

/* the second core's share of pass B: the list's second half, into its own buffers (fm1-base docs/SECOND-CORE.md) */
typedef struct { int vi, nr, rows[NBANK]; } vjob_t;
static vjob_t vj[NUM_VOICES];
static int vj_from, vj_n;
static float sg_l2[SEGMAX], sg_r2[SEGMAX];
int chd_cores2;                               /* the second core runs (app_init: plat_cpu2_set) */
#if defined CHD_HOST || defined PLAT_HOST
#define C1T
#define CPU2_RUN(f, a) 0
#define CPU2_WAIT() 0
#define CPU2_SYNC() ((void)0)
#else
#include "plat.h"
#define C1T __attribute__((section(".c1_text")))
#define CPU2_RUN(f, a) PLAT_FAR(plat_cpu2_run)(f, a)
#define CPU2_WAIT() PLAT_FAR(plat_cpu2_wait)()
#define CPU2_SYNC() PLAT_FAR(plat_cpu2_sync)()
#endif
C1T static void voices_job(uint32_t n)
{
    uint32_t i;
    int k;
    for (i = 0; i < n; i++)
        sg_l2[i] = sg_r2[i] = 0.0f;
    for (k = vj_from; k < vj_n; k++)
        seg_voice(&S.voices[vj[k].vi], n, -1, vj[k].rows, vj[k].nr, sg_l2, sg_r2);
}

RAMC __attribute__((noinline)) static void seg_voices(uint32_t n)   /* pass B: the modulators first (in the original's
                                                                     * order), then the rest */
{
    int mods[NBANK], nm = 0, nj = 0, handed, vi, k, coupled = S.fm_amount > 0.0f || S.ctrl_to_fm != 0.0f;
    float m = S.last_modulator_sample;      /* (S.last_modulator_sample stays the value before the segment until the
                                              * carriers are done: they read it for their first sample) */
    uint32_t i;
    for (vi = 0; vi < NUM_VOICES; vi++)
        if (S.voices[vi].active && S.voices[vi].chord_step == S.fm_modulator_idx) {
            for (i = 0; i < n; i++)
                sg_moda[nm][i] = 0;
            seg_voice(&S.voices[vi], n, nm, 0, -1, sg_l, sg_r);
            mods[nm++] = vi;
        }
    for (i = 0; i < n; i++) {               /* the modulators' latest sample at each sample's end */
        for (k = nm - 1; k >= 0; k--)
            if (sg_moda[k][i]) {
                m = sg_modv[k][i];
                break;
            }
        sg_modend[i] = m;
    }
    for (vi = 0; vi < NUM_VOICES; vi++) {   /* the rest, a list: the second core may take its second half */
        vjob_t *j = &vj[nj];
        if (!S.voices[vi].active || S.voices[vi].chord_step == S.fm_modulator_idx)
            continue;
        j->vi = vi;
        j->nr = 0;
        for (k = 0; k < nm; k++)
            if (mods[k] < vi)
                j->rows[j->nr++] = k;
        if (!coupled)
            j->nr = -1;
        nj++;
    }
    vj_from = nj > 1 && chd_cores2 ? nj / 2 : nj;
    vj_n = nj;
    handed = vj_from < nj && CPU2_RUN(voices_job, n);
    for (k = 0; k < vj_from; k++)
        seg_voice(&S.voices[vj[k].vi], n, -1, vj[k].rows, vj[k].nr, sg_l, sg_r);
    if (vj_from < nj) {
        if (!handed || CPU2_WAIT() != 0)
            voices_job(n);                   /* (it did not take the job, or dropped it: here) */
        CPU2_SYNC();
        for (i = 0; i < n; i++) {
            sg_l[i] += sg_l2[i];
            sg_r[i] += sg_r2[i];
        }
    }
    S.last_modulator_sample = m;
}

RAMC static void seg_out(float *out_l, float *out_r, uint32_t n)   /* pass C */
{
    uint32_t i;
    for (i = 0; i < n; ++i) {
        float l_mix = sg_l[i], r_mix = sg_r[i], cv = sg_cv[i], ats = sg_ats[i], eff_l, eff_r, fl = 0.0f, fr = 0.0f;
        float dl, dr;
        int lp_mode;
        if (S.quality_position == 1)
            apply_lofi(&l_mix, &r_mix);

        eff_l = eff_r = S.filter_cutoff;
        {
            int env_on = S.filter_env_depth != 0.0f, lfo_on = S.filter_lfo_depth != 0.0f;
            int ctrl_on = S.ctrl_to_cutoff != 0.0f;
            int at_on = S.aftertouch > 0.0f || ats > 1e-4f || fm_fabsf(cv - S.ctrl_value_target) > 1e-4f;
            if (env_on || lfo_on || ctrl_on || at_on) {
                float env_mod = (env_on ? aenv_tick(&S.filter_env) : 0.0f) * S.filter_env_depth;
                float lfo_l_mod = 0.0f, lfo_r_mod = 0.0f, ctrl_mod, at_mod;
                if (lfo_on) {
                    float rp;
                    if (!S.filter_lfo_done) {
                        S.filter_lfo_phase += S.filter_lfo_phase_inc;
                        if (S.filter_lfo_phase >= 1.0f) {
                            if (S.filter_lfo_mode == LFOMODE_ONE_SHOT) {
                                S.filter_lfo_phase = 1.0f;
                                S.filter_lfo_done = 1;
                            } else
                                S.filter_lfo_phase -= 1.0f;
                        }
                    }
                    rp = S.filter_lfo_phase + S.filter_lfo_spread * 0.5f;
                    if (rp >= 1.0f)
                        rp -= 1.0f;
                    lfo_l_mod = lfo_sample(S.filter_lfo_shape, S.filter_lfo_phase) * S.filter_lfo_depth;
                    lfo_r_mod = lfo_sample(S.filter_lfo_shape, rp) * S.filter_lfo_depth;
                }
                ctrl_mod = cv * S.ctrl_to_cutoff * 0.5f;
                at_mod = S.ctrl_source == CTRL_AFTERTOUCH && S.ctrl_to_cutoff == 0.0f ? ats * 0.4f : 0.0f;
                eff_l = clampf(S.filter_cutoff + env_mod + lfo_l_mod + ctrl_mod + at_mod, 0.0f, 1.0f);
                eff_r = clampf(S.filter_cutoff + env_mod + lfo_r_mod + ctrl_mod + at_mod, 0.0f, 1.0f);
            }
        }
        lp_mode = S.filter_mode == FILT_LP;
        S.live_cutoff = eff_l;
        {
            const float max_hz = SR * 0.49f;
            float g_l, g_r;
            if (eff_l != S.g_eff_l) {          /* (the coefficients only when the cutoff moved: the same values) */
                S.g_eff_l = eff_l;
                S.g_l = fm_tanf(3.14159265358979f * clampf(cutoff_to_hz(eff_l), 1.0f, max_hz) / SR);
            }
            if (eff_r != S.g_eff_r) {
                S.g_eff_r = eff_r;
                S.g_r = eff_r == eff_l ? S.g_l : fm_tanf(3.14159265358979f * clampf(cutoff_to_hz(eff_r), 1.0f, max_hz) / SR);
            }
            g_l = S.g_l;
            g_r = S.g_r;
            if (lp_mode) {
                /* the warm ZDF ladder (drive inside it) */
                float res = S.filter_resonance, k = res * 3.8f, sat = 1.6f + res * 1.2f;
                int c;
                for (c = 0; c < 2; c++) {
                    ladder_t *L = c ? &S.ladder_r : &S.ladder_l;
                    float g = c ? g_r : g_l, G, x, Sv, u, y0, y1, y2, y3, out;
                    if (g > 10.0f)
                        g = 10.0f;
                    if (g != L->g || k != L->k) {   /* (its coefficients when g or the resonance moved) */
                        float G2, G4, H;
                        L->g = g;
                        L->k = k;
                        G = g / (1.0f + g);
                        G2 = G * G;
                        G4 = G2 * G2;
                        H = 1.0f - G;
                        L->G = G, L->c0 = G2 * G * H, L->c1 = G2 * H, L->c2 = G * H, L->c3 = H;
                        L->inv = 1.0f / (1.0f + k * G4);
                    }
                    G = L->G;
                    x = c ? r_mix : l_mix;
                    if (S.drive > 0.0f)
                        x = fm_tanhf(x * (1.0f + S.drive * 3.0f));
                    Sv = L->c0 * L->s[0] + L->c1 * L->s[1] + L->c2 * L->s[2] + L->c3 * L->s[3];
                    u = (x - k * Sv) * L->inv;
                    y0 = G * (u - L->s[0]) + L->s[0];
                    L->s[0] = 2.0f * y0 - L->s[0];
                    y1 = G * (y0 - L->s[1]) + L->s[1];
                    L->s[1] = 2.0f * y1 - L->s[1];
                    y2 = G * (y1 - L->s[2]) + L->s[2];
                    L->s[2] = 2.0f * y2 - L->s[2];
                    y3 = G * (y2 - L->s[3]) + L->s[3];
                    L->s[3] = 2.0f * y3 - L->s[3];
                    out = fm_tanhf(y3 * sat);
                    if (!(out > -2.0f && out < 2.0f)) {   /* (not finite: the original's isfinite) */
                        L->s[0] = L->s[1] = L->s[2] = L->s[3] = 0.0f;
                        out = 0.0f;
                    }
                    if (c)
                        fr = out;
                    else
                        fl = out;
                }
            } else {
                float fk = S.filter_k, al1 = 1.0f / (1.0f + g_l * (g_l + fk)), al2 = g_l * al1, al3 = g_l * al2;
                float ar1 = 1.0f / (1.0f + g_r * (g_r + fk)), ar2 = g_r * ar1, ar3 = g_r * ar2, comp;
                svf_t *sv[4] = {&S.filter_l, &S.filter_r, &S.filter_l_b, &S.filter_r_b};
                int st, c;
                fl = l_mix;
                fr = r_mix;
                for (st = 0; st < (S.filter_slope == 1 ? 2 : 1); st++)
                    for (c = 0; c < 2; c++) {
                        svf_t *f = sv[st * 2 + c];
                        float a1 = c ? ar1 : al1, a2 = c ? ar2 : al2, a3 = c ? ar3 : al3, in = c ? fr : fl;
                        float v3 = in - f->ic2eq, v1 = a1 * f->ic1eq + a2 * v3, v2 = f->ic2eq + a2 * f->ic1eq + a3 * v3, o;
                        f->ic1eq = 2.0f * v1 - f->ic1eq;
                        f->ic2eq = 2.0f * v2 - f->ic2eq;
                        o = S.filter_mode == FILT_HP ? in - fk * v1 - v2 : S.filter_mode == FILT_BP ? v1 : v2;
                        if (c)
                            fr = o;
                        else
                            fl = o;
                    }
                comp = 1.0f / (1.0f + S.filter_resonance * (S.filter_slope == 1 ? 7.0f : 4.0f));
                fl *= comp;
                fr *= comp;
            }
        }
        if (!lp_mode && S.drive > 0.0f) {
            float dg = 1.0f + S.drive * 9.0f;
            dl = fm_tanhf(dg * fl);
            dr = fm_tanhf(dg * fr);
        } else {
            dl = fl;
            dr = fr;
        }
        if (S.quality_position == 0)
            apply_lofi(&dl, &dr);

        if (S.delay_mix > 0.0f || S.delay_feedback > 0.0f) {
            float ol, or_, mix = S.delay_mix;
            S.dly_acc_l += dl;
            S.dly_acc_r += dr;
            if (S.dly_odd) {                 /* the line's step: the pair in, its repeats out */
                S.dly_prev_l = S.dly_cur_l;
                S.dly_prev_r = S.dly_cur_r;
                delay_step(S.dly_acc_l * 0.5f, S.dly_acc_r * 0.5f, &S.dly_cur_l, &S.dly_cur_r);
                S.dly_acc_l = S.dly_acc_r = 0.0f;
                ol = (S.dly_prev_l + S.dly_cur_l) * 0.5f;
                or_ = (S.dly_prev_r + S.dly_cur_r) * 0.5f;
            } else {
                ol = S.dly_cur_l;
                or_ = S.dly_cur_r;
            }
            S.dly_odd ^= 1;
            dl = dl * (1.0f - mix) + ol * mix;
            dr = dr * (1.0f - mix) + or_ * mix;
        }

        out_l[i] = dl;
        out_r[i] = dr;
    }
}

/* pass D: the plate (its state in locals over the segment) and the tremolo. The plate sleeps once its input and its
 * tail have been silent a while (two seconds under -100 dB): its tail is gone, its work saved */
#define PLATE_SLEEP 88200
RAMC static void seg_plate(float *out_l, float *out_r, uint32_t n)
{
    uint32_t i;
    if (S.reverb_mix > 0.0f) {
        plate_t *P = &plate;
        float decay = 0.45f + S.reverb_decay * 0.34f + S.reverb_size * 0.11f, mix = S.reverb_mix, peak = 0.0f, inpk = 0.0f;
        float dk = 1.0f - S.reverb_damp * S.reverb_damp;   /* (the original's damping, two of its steps in one) */
        float exc = 0.5f * (3.0f + S.reverb_shimmer * 9.0f + S.reverb_mod_depth * 6.0f);
        float oct = S.reverb_shimmer > 0.5f ? (S.reverb_shimmer - 0.5f) * 0.7f : 0.0f;   /* (the knob's upper half:
                                                                                              * octaves, up to 0.35) */
        float w = 6.2831853f * (0.4f + S.reverb_mod_rate * 2.5f) / (SR * 0.5f);
        if (decay > 0.92f)
            decay = 0.92f;
        if (exc > (float)P_EXC)
            exc = (float)P_EXC;
        for (i = 0; i < n; i++)
            inpk = fm_maxf(inpk, fm_fabsf(out_l[i]) + fm_fabsf(out_r[i]));
        if (inpk > 1e-5f)
            S.plate_quiet = 0;
        if (S.plate_quiet >= PLATE_SLEEP) {     /* asleep: the dry signal only, as the original's tail would be */
            for (i = 0; i < n; i++) {
                out_l[i] *= 1.0f - mix;
                out_r[i] *= 1.0f - mix;
            }
        } else {
            for (i = 0; i < n; i++) {
                float dl = out_l[i], dr = out_r[i], rl, rr;
                P->acc += dl;
                P->acc_r += dr;
                if (P->odd) {                   /* the plate's step: the pair in */
                    P->prev_l = P->cur_l;
                    P->prev_r = P->cur_r;
                    plate_step(P, P->acc * 0.5f, P->acc_r * 0.5f, decay, dk, exc, w, oct, &P->cur_l, &P->cur_r);
                    P->acc = P->acc_r = 0.0f;
                    rl = (P->prev_l + P->cur_l) * 0.5f;
                    rr = (P->prev_r + P->cur_r) * 0.5f;
                } else {
                    rl = P->cur_l;
                    rr = P->cur_r;
                }
                P->odd ^= 1;
                peak = fm_maxf(peak, fm_fabsf(rl) + fm_fabsf(rr));
                if (S.reverb_lowcut > 0.0f) {
                    float hp = 1.0f - S.reverb_lowcut * 0.5f, pl = S.reverb_hp_l, pr = S.reverb_hp_r;
                    S.reverb_hp_l = hp * (pl + rl - S.reverb_hp_l);
                    S.reverb_hp_r = hp * (pr + rr - S.reverb_hp_r);
                    rl = S.reverb_hp_l;
                    rr = S.reverb_hp_r;
                }
                out_l[i] = dl * (1.0f - mix) + rl * mix;
                out_r[i] = dr * (1.0f - mix) + rr * mix;
            }
            if (inpk <= 1e-5f && peak < 1e-5f)
                S.plate_quiet += (int)n;
        }
    }
    if (S.amp_lfo_depth > 0.0f)
        for (i = 0; i < n; i++) {
            float a;
            S.amp_lfo_phase += S.amp_lfo_phase_inc;
            while (S.amp_lfo_phase >= 1.0f)
                S.amp_lfo_phase -= 1.0f;
            a = lfo_sample(S.amp_lfo_shape, S.amp_lfo_phase);
            a = 1.0f - S.amp_lfo_depth * 0.5f * (1.0f - a);
            out_l[i] *= a;
            out_r[i] *= a;
        }
}

#ifdef CHD_PROF
volatile uint32_t chd_prof[5];            /* (a measurement build: each pass's ticks, and the samples) */
uint32_t plat_ticks24(void);
RAMC static uint32_t chd_ticks(void) { return PLAT_FAR(plat_ticks24)(); }
#endif
RAMC static void render_block(float *out_l, float *out_r, uint32_t n)
{
    if (S.clock_age < 0x10000000u)
        S.clock_age += n;
    float morph_idx_base = S.morph_index + S.ctrl_value * S.ctrl_to_morph * 0.5f;
    uint32_t at = 0;
    int vi;
    if (S.lm_lfo_depth > 0.0f) {
        advance_block_lfo(&S.lm_lfo_phase, S.lm_lfo_phase_inc, S.lm_lfo_mode, &S.lm_lfo_done, n);
        morph_recompute_at(morph_idx_base + lfo_sample(S.lm_lfo_shape, S.lm_lfo_phase) * S.lm_lfo_depth * 0.5f);
    } else if (S.ctrl_to_morph != 0.0f)
        morph_recompute_at(morph_idx_base);
    if (S.pm_lfo_depth > 0.0f) {
        advance_block_lfo(&S.pm_lfo_phase, S.pm_lfo_phase_inc, S.pm_lfo_mode, &S.pm_lfo_done, n);
        pan_morph_recompute_at(S.pan_morph_index + lfo_sample(S.pm_lfo_shape, S.pm_lfo_phase) * S.pm_lfo_depth * 0.5f);
    }
    for (vi = 0; vi < NUM_VOICES; vi++)
        voice_gains(&S.voices[vi]);
    while (at < n) {
        uint32_t seg = n - at < SEGMAX ? n - at : SEGMAX;
        if (S.arp_enabled && !arp_synced() && S.held_count > 0) {   /* the arp's tick at the segment's first
                                                                          * sample; the segment ends before its next */
            if (--S.arp_sample_counter <= 0) {
                S.arp_sample_counter = S.arp_step_period;
                arp_step_and_fire();
            }
            if ((uint32_t)S.arp_sample_counter < seg)
                seg = (uint32_t)S.arp_sample_counter;
            S.arp_sample_counter -= (int)seg - 1;
        }
#ifdef CHD_PROF
        {
            uint32_t t0 = chd_ticks(), t1, t2, t3, t4;
            seg_shared(seg);
            t1 = chd_ticks();
            seg_voices(seg);
            t2 = chd_ticks();
            seg_out(out_l + at, out_r + at, seg);
            t3 = chd_ticks();
            seg_plate(out_l + at, out_r + at, seg);
            t4 = chd_ticks();
            chd_prof[0] += t1 - t0, chd_prof[1] += t2 - t1, chd_prof[2] += t3 - t2, chd_prof[3] += t4 - t3;
            chd_prof[4] += seg;
        }
#else
        seg_shared(seg);
        seg_voices(seg);
        seg_out(out_l + at, out_r + at, seg);
        seg_plate(out_l + at, out_r + at, seg);
#endif
        at += seg;
    }
}

RAMC static void show(void)                    /* what the screen draws: the chord sounding, each voice's level */
{
    int i;
    for (i = 0; i < CHORD_SIZE; i++) {
        const voice_t *v = &S.voices[S.cur_base + i];
        float lv = v->active ? (S.vca_drone ? 1.0f : v->env.value) * S.morph_gains[i] * S.mixer_trims[i] : 0.0f;
        chd_sounding[i] = (int8_t)(v->active ? v->note : -1);
        chd_level[i] = (uint8_t)(lv >= 1.0f ? 255 : lv <= 0.0f ? 0 : (int)(lv * 255.0f));
    }
    chd_held = (uint8_t)S.held_count;
    {   /* what moves, for the pictures and the knobs' dots */
        chd_live_t *L = (chd_live_t *)&chd_live;
        for (i = 0; i < CHORD_SIZE; i++) {
            const voice_t *v = &S.voices[S.cur_base + i];
            L->gain[i] = S.morph_gains[i];
            L->pan[i] = clampf(0.5f + ((float)i / 3.0f - 0.5f) * S.width + S.pan_morph_pans[i] * 0.5f, 0.0f, 1.0f);
            L->shape[i] = clampf(S.shapes[i] + lfo_sample(S.lfo_shape, S.lfo_phase + S.shape_lfo_phase_offsets[i] -
                                                          fm_floorf(S.lfo_phase + S.shape_lfo_phase_offsets[i])) *
                                                   S.lfo_depth + S.ctrl_value * S.ctrl_to_shape * 0.5f, 0.0f, 1.0f);
            L->env[i] = v->active ? v->env.value : 0.0f;
        }
        L->lfo = S.lfo_phase;
        L->flfo = S.filter_lfo_phase;
        L->alfo = S.amp_lfo_phase;
        L->vib = S.vibrato_phase;
        L->lm = S.lm_lfo_phase;
        L->pm = S.pm_lfo_phase;
        L->cutoff = S.live_cutoff;
        L->fenv = S.filter_env.value;
        L->ctrl = S.ctrl_value;
        L->arp_pos = (uint8_t)S.arp_pattern_pos;
        L->clock_run = S.arp_clock_running;
        L->seq++;
    }
}

#define BLK 128u
RAMC static float soft(float x)                /* straight to 0.8, then bent toward 1 */
{
    float a = fm_fabsf(x), y;
    if (a <= 0.8f)
        return x;
    y = 0.8f + 0.2f * fm_tanhf((a - 0.8f) * 5.0f);
    return x < 0.0f ? -y : y;
}
/* the guard: a block that took more than GUARD_PCT of its time fades the oldest tail still sounding (its release
 * a 30 ms one), so the next chords keep their time. 16 voices hold four chords' tails; the FM-1 has room for fewer
 * at once on the heavier sounds */
#define GUARD_PCT 80u
RAMC __attribute__((unused)) static void guard_fade(void)
{
    int b, best = -1, age, best_age = 0;
    for (b = 0; b < NBANK; b++) {
        int base = b * CHORD_SIZE, i, any = 0;
        if (base == S.cur_base)
            continue;
        for (i = 0; i < CHORD_SIZE; i++)
            any |= S.voices[base + i].active;
        age = (S.cur_base - base + NUM_VOICES) % NUM_VOICES;   /* (the furthest behind the chord playing: the oldest) */
        if (any && age > best_age)
            best = base, best_age = age;
    }
    if (best >= 0)
        for (int i = 0; i < CHORD_SIZE; i++) {
            voice_t *v = &S.voices[best + i];
            v->gate = 0;
            if (v->env.stage == ENV_HOLD || v->env.stage == ENV_ATTACK)
                v->env.stage = ENV_RELEASE;
            v->env.release_coef = fm_expf(-5.0f / (0.03f * SR));
        }
}
uint32_t chd_guarded;                     /* how often the guard has faded a tail (the Setup page) */

RAMC void chd_render(int32_t *lr, uint32_t n, uint32_t gain_q12)
{
    float l[BLK], r[BLK];
    float out_gain = (float)gain_q12 * (256.0f / 4096.0f);
#if !defined CHD_HOST && !defined PLAT_HOST
    int32_t *lr0 = lr;
    uint32_t t0 = PLAT_FAR(plat_ticks24)(), spent;
    static uint32_t over;
#endif
    drain();
    while (n) {
        uint32_t m = n < BLK ? n : BLK, i;
        float mg = S.volume * (16000.0f / 32767.0f);
        render_block(l, r, m);
        for (i = 0; i < m; i++) {                  /* (the original's level, its peaks bent softly to full scale) */
            lr[2 * i] = (int32_t)(soft(l[i] * mg) * 32767.0f * out_gain);
            lr[2 * i + 1] = (int32_t)(soft(r[i] * mg) * 32767.0f * out_gain);
        }
        lr += 2 * m;
        n -= m;
    }
    show();
#if !defined CHD_HOST && !defined PLAT_HOST
    spent = PLAT_FAR(plat_ticks24)() - t0;   /* (ticks: 24 a microsecond; a block's time 22.68 us a frame) */
    if (spent * 100u > GUARD_PCT * 544u * (uint32_t)(lr - lr0) / 2u) {
        if (++over >= 2u) {                   /* (two blocks in a row: not a single busy one) */
            over = 0;
            guard_fade();
            chd_guarded++;
        }
    } else
        over = 0;
#endif
}

#ifdef CHD_HOST
/* the host's test: the original's int16 output for a block, as its render_block would give it (master_gain
 * volume x 16000, clipped), in blocks of n frames (the original's block-rate work happens per call) */
void chd_test_render(int16_t *lr, uint32_t n)
{
    float l[1024], r[1024], mg;
    uint32_t i;
    drain();
    mg = S.volume * 16000.0f;
    render_block(l, r, n);
    for (i = 0; i < n; i++) {
        float sl = clampf(l[i] * mg, -32768.0f, 32767.0f), sr = clampf(r[i] * mg, -32768.0f, 32767.0f);
        lr[2 * i] = (int16_t)sl;
        lr[2 * i + 1] = (int16_t)sr;
    }
    show();
}
#endif
