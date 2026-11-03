/* dsp.h — offline processor API.
 * Module names match the original strings found (Declicker/Decrackler/Denoiser).
 * Parameter defaults are provisional until the decompile pass documents the
 * original's exact control curves (README #5). */
#ifndef CAUDOK_DSP_H
#define CAUDOK_DSP_H

#include <stdint.h>
#include <stddef.h>

typedef struct ProcReport {
    int32_t clicksRepaired;
    int32_t cracklesRepaired;
    int32_t framesProcessed;
} ProcReport;

/* sensitivity 0..1 (0=gentle, 1=aggressive); widthMs is the repair window in ms */
int32_t dsp_declick(int16_t *pcm, size_t frames, float sensitivity, float widthMs, ProcReport *out);

/* spectral-subtraction denoiser: reductionDb 0..40, strength 0..1
 * (proved against a synthetic fixture in tests/dsp) */
int32_t dsp_denoise(int16_t *pcm, size_t frames, int32_t rate, int reductionDb, float noiseLearnRatio, ProcReport *out);

/* calibration tone WAV bytes generator: replicates the disc's measured file format */
uint8_t *dsp_calibration_tone(int32_t frequency, int32_t rate, int32_t seconds, size_t *bytesOut);

#endif
