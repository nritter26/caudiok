/* declick.c — impulse (click) and burst (crackle) repair.
 * Detection (per channel):
 *   d[i] = v[i] - (v[i-1] + v[i+1])/2      (slope-discontinuity / second difference)
 *   flag(i) := |d[i]| > threshold   AND   |d[i]| > 3·mean(|d| over ring ±radius,
 *                                                    itself and both neighbors excluded)
 * The ring-average guard keeps continuous high-frequency program material
 * (10 kHz tones produce large second differences legitimately) from being
 * flagged, while isolated discontinuities pass. A flagged sample starts a
 * burst that extends while successors also flag; bursts longer than one
 * sample are counted as crackle, single samples as click.
 * Repair: linear interpolation between the signal anchors around the record
 * window.
 * Evidence-mapped parameters (see spec/dsp-parity.md; original data mined from
 * the SddSpp* DLLs and the Preferences-dialog templates):
 *   sensitivity s → dB-tier thresholds: High −20 dB (s=1.0), Medium −30 dB
 *   (s=0.5, original constructor default), Normal −40 dB (s=0.0);
 *   in slope units  t = 32768·10^(dB/20), dB = −40 + 20·s
 *   guard ring radius = widthMs·44.1 samples; default 11.2 ms ≈ half the
 *   original's 992-sample repair template (SddSpp constant 0x3e0 = 992);
 *   record window [D, 0x10038740 tail]: (start−4, run+4) — repair begins 4
 *   samples before the first flagged sample; a new flag run within
 *   max-gap 200 samples of a pending record extends that record instead of
 *   opening a new one. */
#include "dsp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

/* flag test at sample i on channel c; returns 1 if flagged */
static int flag_at(const int16_t *pcm, int64_t i, int64_t frames, int c,
                   int radius, double threshold) {
    double d = (double)pcm[i*2 + c]
             - 0.5*((double)pcm[(i-1)*2 + c] + (double)pcm[(i+1)*2 + c]);
    if (fabs(d) <= threshold) return 0;
    /* ring guard: mean |slope| of nearby samples excluding i-2..i+2 */
    double sum = 0.0; int cnt = 0;
    for (int64_t j = i - radius; j <= i + radius; j++) {
        if (j < 1 || j >= frames - 1) continue;
        int64_t dd = j - i; if (dd < 0) dd = -dd;
        if (dd <= 1) continue;
        double dj = (double)pcm[j*2 + c]
                  - 0.5*((double)pcm[(j-1)*2 + c] + (double)pcm[(j+1)*2 + c]);
        dj = fabs(dj); sum += dj; cnt++;
    }
    if (cnt == 0) return (int)(fabs(d) > threshold);
    return (fabs(d) > 3.0*(sum/cnt) + 100.0);
}

/* commit one repair record: window (start−4, run+4) per the mined original
 * record writer; linear interpolate from the signal anchors around it */
static void repair_run(int16_t *pcm, int64_t frames, int c,
                       int64_t start, int64_t end,
                       int32_t *clicks, int32_t *crackles) {
    int64_t w0 = start - 4;                       /* [D] start−4 */
    if (w0 < 0) w0 = 0;
    if (w0 > end) w0 = end;
    if (end > frames - 2) end = frames - 2;
    int16_t a = (w0 > 0) ? pcm[(w0-1)*2 + c] : pcm[(end+1)*2 + c];
    int16_t b = (end + 1 < frames) ? pcm[(end+1)*2 + c] : a;
    for (int64_t j = w0; j <= end; j++) {
        double t = (double)(j - w0 + 1) / (double)(end - w0 + 2);
        double v = (double)a + t*((double)b - (double)a);
        if (v >  32767.0) v =  32767.0;
        if (v < -32768.0) v = -32768.0;
        pcm[j*2 + c] = (int16_t)v;
    }
    if (end - start + 1 > 3) (*crackles)++;      /* ≤3-flagged analog still open (§3) */
    else (*clicks)++;
}

int32_t dsp_declick(int16_t *pcm, size_t frames, float sensitivity, float widthMs, ProcReport *out) {
    if (!pcm || frames < 16) return -1;
    if (sensitivity < 0.0f) sensitivity = 0.0f;
    if (sensitivity > 1.0f) sensitivity = 1.0f;
    if (widthMs   < 0.0f) widthMs   = 0.0f;
    if (widthMs   > 22.5f) widthMs  = 22.5f;  /* original template: 992-smp guard + 32-smp tail = 1024 floats ≈ 22.5 ms */

    /* threshold: dB tier mapped to slope amplitude; High −20 / Medium −30 / Normal −40 */
    double threshold = 32768.0 * pow(10.0, (-40.0 + 20.0 * (double)sensitivity) / 20.0) * 0.5;
    int radius = (int)lrint((double)widthMs * 44.1);
    if (radius < 2) radius = 2;
    if (radius > 500) radius = 500;

    int32_t clicks = 0, crackles = 0;

    for (int c = 0; c < 2; c++) {
        int64_t i = radius + 1;
        int64_t pstart = -1, pend = -1;           /* pending repair record */
        while (i + radius + 2 < (int64_t)frames) {
            if (flag_at(pcm, i, (int64_t)frames, c, radius, threshold)) {
                if (pstart >= 0) {
                    /* [D] max-gap 200: a flag run ≤200 samples after a pending
                     * record extends that record instead of opening a new one */
                    if (i - pend <= 200) { pend = i; i++; continue; }
                    repair_run(pcm, (int64_t)frames, c, pstart, pend, &clicks, &crackles);
                    pstart = -1;
                }
                int64_t end = i;
                while (end + radius + 2 < (int64_t)frames &&
                       flag_at(pcm, end + 1, (int64_t)frames, c, radius, threshold))
                    end++;                        /* contiguous run */
                pstart = i; pend = end;
                i = end + 1;
            } else {
                if (pstart >= 0 && i - pend > 200) {
                    repair_run(pcm, (int64_t)frames, c, pstart, pend, &clicks, &crackles);
                    pstart = -1;
                }
                i++;
            }
        }
        if (pstart >= 0)
            repair_run(pcm, (int64_t)frames, c, pstart, pend, &clicks, &crackles);
    }

    if (out) {
        out->clicksRepaired    = clicks;
        out->cracklesRepaired  = crackles;
        out->framesProcessed   = (int32_t)frames;
    }
    return 0;
}
