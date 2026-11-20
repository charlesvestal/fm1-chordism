// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Charles Vestal
// The engine against the original: every factory preset played on both (app/dsp/chd.c and the original module,
// tests/ref/chordism_plugin.cpp), the same notes, the same blocks, and the outputs compared.
//   dry   the delay and the plate off on both: the voices, the filter, drive and lo-fi must match (SNR in dB over
//         the first 300 blocks; the level over all of it)
//   full  as the preset is but the plate (the FM-1 has AMB-1's): the half-rate delay differs in its top end, so the
//         levels must match (dB apart)
// Presets that draw random numbers (random or coin-toss control, a random arp, stray vibrato) are compared by level
// only: the two generators differ.
//   c++ -O2 -Iapp/dsp -Ibase/include -o chd_test tests/host/chd_test.cpp chd.o && ./chd_test [-v]
#include <vector>
#include "../ref/chordism_plugin.cpp"
extern "C" {
#include "chd.h"
void chd_test_render(int16_t *lr, uint32_t n);
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); fails++; } } while (0)

struct ev { int at, st, d1, d2; };           // at: the block it comes before
static const ev SCRIPT[] = {
    {0, 0x90, 60, 100},  {150, 0x80, 60, 0},  {260, 0x90, 64, 80}, {300, 0xA0, 64, 90}, {320, 0x90, 67, 120},
    {420, 0x80, 67, 0},  {470, 0x80, 64, 0},  {560, 0x90, 57, 64}, {700, 0x80, 57, 0},
};
#define BLOCKS 900                           // x 128 frames: 2.6 s
#define FR 128

static double snr(const std::vector<int16_t> &a, const std::vector<int16_t> &b)
{
    double s = 0, n = 0;
    for (size_t i = 0; i < a.size(); i++) {
        s += (double)a[i] * a[i];
        n += ((double)a[i] - b[i]) * ((double)a[i] - b[i]);
    }
    return s < 1 ? 99 : n < 1e-9 ? 99 : 10 * log10(s / n);
}
static double rms_db(const std::vector<int16_t> &a)
{
    double s = 0;
    for (int16_t v : a)
        s += (double)v * v;
    return 10 * log10(s / a.size() + 1e-9);
}

static void run(int preset, int dry, std::vector<int16_t> &ref, std::vector<int16_t> &out)
{
    host_api_v1_t host = {};
    host.sample_rate = 44100;
    plugin_api_v2_t *api = move_plugin_init_v2(&host);
    void *in = api->create_instance("", "");
    char b[16];
    snprintf(b, sizeof b, "%d", preset);
    api->set_param(in, "preset", b);
    chd_init();
    chd_patch(&CHD_PRESET[preset]);
    if (getenv("CHD_SET")) {                 // (debugging: CHD_SET=key=value,key=value on both)
        char t[512], *k;
        snprintf(t, sizeof t, "%s", getenv("CHD_SET"));
        for (k = strtok(t, ","); k; k = strtok(0, ",")) {
            char *eq = strchr(k, '=');
            *eq = 0;
            api->set_param(in, k, eq + 1);
            for (int q = 0; q < CP_N; q++)
                if (!strcmp(CHD_PARAM[q].key, k))
                    chd_set(q, CHD_PARAM[q].hi == 10000 ? (int)lround(atof(eq + 1) * 10000) : atoi(eq + 1));
        }
    }
    api->set_param(in, "reverb_mix", "0");      // (the plate is AMB-1's on the FM-1, not the original's)
    chd_set(CP_RV_MIX, 0);
    if (dry) {
        api->set_param(in, "reverb_mix", "0");
        api->set_param(in, "delay_mix", "0");
        api->set_param(in, "delay_feedback", "0");
        chd_set(CP_RV_MIX, 0);
        chd_set(CP_DL_MIX, 0);
        chd_set(CP_DL_FB, 0);
    }
    ref.assign(BLOCKS * FR * 2, 0);
    out.assign(BLOCKS * FR * 2, 0);
    for (int k = 0; k < BLOCKS; k++) {
        for (const ev &e : SCRIPT)
            if (e.at == k) {
                uint8_t m[3] = {(uint8_t)e.st, (uint8_t)e.d1, (uint8_t)e.d2};
                api->on_midi(in, m, 3, 0);
                if (e.st == 0x90)
                    chd_note_on(e.d1, e.d2);
                else if (e.st == 0x80)
                    chd_note_off(e.d1);
                else
                    chd_pressure(e.d1, e.d2);
            }
        api->render_block(in, &ref[k * FR * 2], FR);
        chd_test_render(&out[k * FR * 2], FR);
    }
    api->destroy_instance(in);
}

int main(int argc, char **argv)
{
    int verbose = argc > 1 && !strcmp(argv[1], "-v");
    // every preset: dry and full
    std::vector<int16_t> ref, out;
    double worst_dry = 99, worst_lvl = 0;
    for (int p = 0; p < CHD_NPRESET; p++) {
        if (getenv("CHD_ONLY") && p != atoi(getenv("CHD_ONLY")))
            continue;
        const int16_t *v = CHD_PRESET[p].p;
        int random = v[CP_CTRL_SRC] == 1 || v[CP_CTRL_SRC] == 2 || (v[CP_ARP] && v[CP_ARP_DIR] == 3) || v[CP_VIB_STRAY];
        run(p, 1, ref, out);
        if (getenv("CHD_DIFF"))                  // (debugging: where the dry path first parts, and how)
            for (size_t i = 0; i < ref.size(); i++)
                if (abs(ref[i] - out[i]) > 8) {
                    printf("first apart at frame %zu (block %zu): %d vs %d\n", i / 2, i / 2 / FR, ref[i], out[i]);
                    for (size_t w = 0; w < ref.size() / (FR * 2 * 20); w++) {   // the SNR in windows of 20 blocks
                        std::vector<int16_t> a(ref.begin() + w * FR * 40, ref.begin() + (w + 1) * FR * 40);
                        std::vector<int16_t> b(out.begin() + w * FR * 40, out.begin() + (w + 1) * FR * 40);
                        printf("  blocks %3zu.. %6.1f dB  rms %6.1f %6.1f\n", w * 20, snr(a, b), rms_db(a), rms_db(b));
                    }
                    break;
                }
        // (the SNR until the pressure at block 300: after it, a resonant filter swept a sample at a time grows the
        // last bits' differences, the libm's and fastmath's, into different waveforms at the same level)
        std::vector<int16_t> r3(ref.begin(), ref.begin() + 300 * FR * 2), o3(out.begin(), out.begin() + 300 * FR * 2);
        double d = snr(r3, o3), ld = fabs(rms_db(ref) - rms_db(out));
        run(p, 0, ref, out);
        double f = snr(ref, out), lf = fabs(rms_db(ref) - rms_db(out));
        double lmax = random ? 1.5 : 1.5;   // (a chord takes a silent bank where the original cut a tail: pads ring a
                                             // touch longer, up to ~1.2 dB louder over the phrase)
        if (verbose || (!random && d < 40) || ld > lmax || lf > 1.5)
            printf("%-18s dry %5.1f dB (level %+.2f)  full %5.1f dB (level %+.2f)%s\n", CHD_PRESET[p].name, d, ld, f, lf,
                   random ? "  (random)" : "");
        if (!random) {
            CHECK(d >= 40, "%s: the dry path %.1f dB from the original", CHD_PRESET[p].name, d);
            if (d < worst_dry)
                worst_dry = d;
        }
        CHECK(ld <= lmax, "%s: the dry level %.2f dB off", CHD_PRESET[p].name, ld);
        CHECK(lf <= 1.5, "%s: the full level %.2f dB off", CHD_PRESET[p].name, lf);
        if (lf > worst_lvl)
            worst_lvl = lf;
    }
    printf("%d presets: dry path within %.1f dB SNR at worst, full levels within %.2f dB\n", CHD_NPRESET, worst_dry,
           worst_lvl);
    printf(fails ? "chd_test: %d FAILED\n" : "chd_test: ok\n", fails);
    return fails ? 1 : 0;
}
