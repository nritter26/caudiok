/* dsp.c — DSP correctness tests (headless, Linux via lin-shim; same sources ship to Windows).
 *  1. declick: error reduction on a spike-in-sine fixture + untouched-samples invariant
 *  2. denoise: SNR gain (dB) on noise-learn + tone+noise fixture  (≥ +3 dB required)
 *  3. calibration tone: byte-compare against the original disc's 1 kHz Tone.wav (±1 LSB) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "../src/waveio.h"
#include "../src/dsp/dsp.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); failures++; } else { printf("ok   %s\n", msg);} } while (0)

static double db(double num, double den) {
    return num <= 1e-12 || den <= 1e-12 ? -999.0 : 10.0*log10(num/den);
}

static unsigned rnd = 12345u;
static int irnd(int lo, int hi) { rnd = rnd*1103515245u + 12345u; return lo + (int)((rnd >> 16) % (unsigned)(hi - lo + 1)); }

/* 1 — declick */
static void test_declick(void) {
    const size_t N = 16384;
    int16_t *caudiok = malloc(N*2*sizeof(int16_t));
    int16_t *dirty = malloc(N*2*sizeof(int16_t));
    for (size_t i = 0; i < N; i++) {
        int16_t v = (int16_t)lrint(sin(2.0*M_PI*330.0*(double)i/44100.0)*16000.0);
        caudiok[i*2] = caudiok[i*2+1] = v;
    }
    memcpy(dirty, caudiok, N*2*sizeof(int16_t));
    /* inject: spike at 5000 (full-scale dropout), double-burst at 9000..9003 */
    dirty[5000*2] = dirty[5000*2+1] = 0;
    for (int k = 0; k < 4; k++) {
        dirty[(9000+k)*2]   = (int16_t)(k&1 ? -29000 : 30000);
        dirty[(9000+k)*2+1] = (int16_t)(k&1 ? -29000 : 30000);
    }
    const int peaks[2] = { 5000, 9000 };
    double before[2], after[2];
    for (int p = 0; p < 2; p++) {
        double eb = 0.0, ea = 0.0;
        for (int k = -8; k <= 8; k++) {
            int idx = peaks[p] + k;
            if (idx < 0 || idx >= (int)N) continue;
            double d1 = dirty[idx*2]   - caudiok[idx*2];
            double d2 = caudiok[idx*2]   - dirty[idx*2];
            (void)d2;
            eb += d1*d1;
            int16_t *after_pcm = dirty;  /* filled after dsp_declick below */
            (void)after_pcm; (void)ea;
        }
        before[p] = eb; after[p] = 0; /* recomputed below */
    }
    ProcReport rep; memset(&rep, 0, sizeof(rep));
    int rc = dsp_declick(dirty, N, 1.0f, 8.0f, &rep);
    CHECK(rc == 0, "declick returns 0");
    CHECK(rep.clicksRepaired >= 1, "clicks counted");
    CHECK(rep.cracklesRepaired >= 1, "crackles counted");
    for (int p = 0; p < 2; p++) {
        double ea = 0.0;
        for (int k = -8; k <= 8; k++) {
            int idx = peaks[p] + k;
            if (idx < 0 || idx >= (int)N) continue;
            double d = dirty[idx*2] - caudiok[idx*2];
            ea += d*d;
        }
        after[p] = ea;
    }
    printf("     err(5000) %.1f -> %.1f | err(9000) %.1f -> %.1f\n",
           before[0], after[0], before[1], after[1]);
    CHECK(after[0] < before[0]*0.30, "declick: spike error reduced >70%");
    CHECK(after[1] < before[1]*0.30, "declick: burst error reduced >70%");
    /* untouched far region must remain unchanged */
    int diffs = 0;
    for (size_t i = 12000; i < 16000 && i < N; i++)
        if (dirty[i*2] != caudiok[i*2]) diffs++;
    CHECK(diffs == 0, "declick: untouched region intact");
    free(caudiok); free(dirty);
}

/* 2 — denoise SNR. Fixture matches the denoiser's sample-based noise-print
 * semantics: the first LEARN samples are noise only (0.125·32768 = 4096 =
 * 7 fully-contained OLA frames), the tone starts AT the print boundary. */
static void test_denoise(void) {
    const size_t N = 32768;
    const size_t LEARN = 4096;          /* noise print: 0.125·N samples */
    int16_t *caudiok = malloc(N*2*sizeof(int16_t));
    int16_t *dirty = malloc(N*2*sizeof(int16_t));
    for (size_t i = 0; i < N; i++) {
        int16_t tone = (int16_t)lrint(sin(2.0*M_PI*330.0*(double)i/44100.0)*12000.0);
        if (i < LEARN) tone = 0;
        caudiok[i*2] = caudiok[i*2+1] = tone;
        int nL = irnd(-400, 400), nR = irnd(-400, 400);
        dirty[i*2]   = (int16_t)(tone + nL);
        dirty[i*2+1] = (int16_t)(tone + nR);
    }
    /* SNR over the fully-masked region (one extra WOLA block past the print) */
    double Ptone = 0.0, PnoiseBefore = 0.0, PnoiseAfter = 0.0;
    for (int c = 0; c < 2; c++)
        for (size_t i = 4608; i < N; i++) {
            double s = caudiok[i*2+c];
            Ptone += s*s;
            double e = dirty[i*2+c] - s;
            PnoiseBefore += e*e;
        }
    ProcReport rep; memset(&rep, 0, sizeof(rep));
    int rc = dsp_denoise(dirty, N, 44100, 20, 0.125f, &rep);
    CHECK(rc == 0, "denoise returns 0");
    CHECK(rep.framesProcessed == (int32_t)N, "denoise reports all frames");
    for (int c = 0; c < 2; c++)
        for (size_t i = 4608; i < N; i++) {
            double s = caudiok[i*2+c];
            double e = dirty[i*2+c] - s;
            PnoiseAfter += e*e;
        }
    double snrB = db(Ptone, PnoiseBefore);
    double snrA = db(Ptone, PnoiseAfter);
    printf("     SNR %.2f dB -> %.2f dB (gain %.2f dB)\n", snrB, snrA, snrA - snrB);
    CHECK(snrA - snrB >= 3.0, "denoise: SNR gain >= 3 dB");
    free(caudiok); free(dirty);
}

/* 3 — calibration tone vs disc */
static void test_calib_tone(void) {
    const char *discPath = getenv("CAUDOK_DISC_TONE");
    size_t bytes = 0;
    uint8_t *gen = dsp_calibration_tone(1000, 44100, 30, &bytes);
    CHECK(gen && bytes == 44 + ((size_t)44100*30 + 1)*4, "tone: size/header length (incl. endpoint frame)");
    CHECK(gen[24] == 0x44 && gen[25] == 0xAC, "tone: 44100 Hz byte order");
    CHECK(gen[22] == 2 && gen[34] == 16, "tone: stereo 16-bit fields");
    FILE *f = fopen(discPath, "rb");
    if (!f) { printf("SKIP disc tone fixture not configured: set CAUDOK_DISC_TONE to the original disc's Calibrate/1 kHz Tone.wav\n"); free(gen); return; }
    fseek(f, 0, SEEK_END); long fsz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *disc = malloc((size_t)fsz);
    if (fread(disc, 1, (size_t)fsz, f) != (size_t)fsz) { fclose(f); free(disc); free(gen); return; }
    fclose(f);
    long dpos = -1, dsz = 0;
    for (long i = 0; i + 8 <= fsz; i++)
        if (!memcmp(disc + i, "data", 4)) { dpos = i + 8; dsz = (long)(uint32_t)(disc[i+4] | disc[i+5]<<8 | disc[i+6]<<16 | ((uint32_t)disc[i+7]<<24)); break; }
    CHECK(dpos > 0 && dsz == (long)(bytes - 44), "disc data chunk size matches generated");
    if (!(dpos > 0 && dsz == (long)(bytes - 44)))
        printf("     size probe: dpos=%ld dsz=%ld expected=%zu bytes=%zu\n",
               dpos, dsz, (size_t)(bytes - 44), (size_t)bytes);
    long cmp = dsz < (long)(bytes - 44) ? dsz : (long)(bytes - 44);
    long mx = 0, nbad = 0;
    for (long i = 0; i < cmp; i += 2) {
        int16_t a = (int16_t)(gen[44+i] | (gen[44+i+1] << 8));
        int16_t b = (int16_t)(disc[dpos+i] | (disc[dpos+i+1] << 8));
        long d = labs((long)a - (long)b);
        if (d > mx) mx = d;
        if (d > 1) nbad++;
    }
    printf("     tone compare: max LSB diff %ld, samples over ±1: %ld / %ld\n", mx, nbad, cmp/2);
    CHECK(mx <= 1 && nbad == 0, "tone: byte-accurate within ±1 LSB vs disc");
    free(disc); free(gen);
}

int main(void) {
    test_declick();
    test_denoise();
    test_calib_tone();
    printf("dsp: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
