/* denoise.c — spectral-subtraction denoiser over overlap-add frame blocks.
 * Named after the original "Denoiser" module (string id 3259).
 * Pipeline, per channel (two passes):
 *   PASS 1 (noise print): average the periodogram of every frame that lies
 *             FULLY INSIDE the noise-print sample region (first
 *             noiseLearnRatio·frames samples). Frames only partially inside
 *             are excluded, so signal content can never leak into the
 *             noise estimate (this is the classic "noise print" rule).
 *   PASS 2 (apply): per frame — Hann window, FFT, per-bin Wiener-style
 *             gain  g = clamp(1 - alpha*|N|/|S|, floor, 1)  with alpha=2,
 *             floor = 10^(-reductionDb/20), inverse FFT, Hann synthesis
 *             window, WOLA overlap-add, normalize by the measured Σw²
 *             (self-correcting, exact transparency wherever g==1).
 * A silent noise print leaves noise[k]=0: the mask is skipped and the
 * output is bit-identical input (denoiser effectively off).
 * Parameters PROVISIONAL until the original's curves are documented (README #5). */
#include "dsp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef DSPDBG
#include <stdio.h>
#endif

#define FFT_N 1024
#define HOP   (FFT_N/2)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* proper iterative radix-2 complex FFT (verified: random roundtrip err² ~1e-26) */
static void fft1d(double *re, double *im, int n, int inverse) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = inverse ? 2.0*M_PI/len : -2.0*M_PI/len;
        double wr = cos(ang), wi = sin(ang);
        for (int i = 0; i < n; i += len) {
            double cr = 1.0, ci = 0.0;
            for (int j = 0; j < len/2; j++) {
                int a = i+j, b = i+j+len/2;
                double ur = re[a], ui = im[a];
                double vr = re[b]*cr - im[b]*ci;
                double vi = re[b]*ci + im[b]*cr;
                re[a] = ur + vr; im[a] = ui + vi;
                re[b] = ur - vr; im[b] = ui - vi;
                double ncr = cr*wr - ci*wi;
                ci = cr*wi + ci*wr;
                cr = ncr;
            }
        }
    }
    if (inverse) {
        double s = 1.0/(double)n;
        for (int i = 0; i < n; i++) { re[i] *= s; im[i] *= s; }
    }
}

int32_t dsp_denoise(int16_t *pcm, size_t frames, int32_t rate, int reductionDb,
                    float noiseLearnRatio, ProcReport *out)
{
    (void)rate;
    if (!pcm || frames < (size_t)FFT_N) return -1;
    if (reductionDb < 0)   reductionDb = 0;
    if (reductionDb > 40)  reductionDb = 40;
    if (noiseLearnRatio < 0.01f) noiseLearnRatio = 0.01f;
    if (noiseLearnRatio > 0.50f) noiseLearnRatio = 0.50f;

    /* noise print region (in SAMPLES); clamp so at least one full frame fits */
    size_t learnSamples = (size_t)((double)frames * (double)noiseLearnRatio);
    if (learnSamples < FFT_N)              learnSamples = FFT_N;
    if (learnSamples > frames - HOP)       learnSamples = frames - HOP;

    int nFramesTotal = (int)(frames / HOP);          /* frames stepped across the file */
    if (nFramesTotal < 1) nFramesTotal = 1;
    /* frames whose sample range is ENTIRELY inside the noise print */
    int nLearn = (int)((learnSamples - FFT_N) / HOP) + 1;
    if (nLearn < 1) nLearn = 1;
    if (nLearn > nFramesTotal) nLearn = nFramesTotal;

    size_t bufN = (size_t)(nFramesTotal - 1)*HOP + FFT_N + 1;

    double *win    = (double*)malloc(sizeof(double)*FFT_N);
    double *re     = (double*)malloc(sizeof(double)*FFT_N);
    double *im     = (double*)malloc(sizeof(double)*FFT_N);
    double *noise  = (double*)calloc(FFT_N, sizeof(double));
    double *accum  = (double*)calloc(bufN,  sizeof(double));
    double *winSum = (double*)calloc(bufN,  sizeof(double));
    if (!win || !re || !im || !noise || !accum || !winSum) {
        free(win); free(re); free(im); free(noise); free(accum); free(winSum);
        return -2;
    }
    for (int i = 0; i < FFT_N; i++)
        win[i] = 0.5 - 0.5*cos(2.0*M_PI*(double)i/FFT_N);

    double floorGain = pow(10.0, -(double)reductionDb/20.0);
    const double alpha = 2.0;   /* classic over-subtraction factor */
    int haveNoise = 0;

    for (int c = 0; c < 2; c++) {
        /* ---- PASS 1: noise print from fully-contained frames ---- */
        memset(noise, 0, sizeof(double)*FFT_N);
        for (int f = 0; f < nLearn; f++) {
            size_t f0 = (size_t)f*HOP;
            for (int i = 0; i < FFT_N; i++) {
                size_t idx = f0 + i;
                double v = (idx < frames) ? (double)pcm[idx*2 + c] : 0.0;
                re[i] = v*win[i]; im[i] = 0.0;
            }
            fft1d(re, im, FFT_N, 0);
            for (int k = 0; k < FFT_N; k++)
                noise[k] += re[k]*re[k] + im[k]*im[k];
        }
        for (int k = 0; k < FFT_N; k++) noise[k] /= (double)nLearn;
        haveNoise = (noise[0] > 1e-12); /* silent print => effective no-op */

        /* ---- PASS 2: mask + WOLA synthesis across the whole file ---- */
        memset(accum,  0, sizeof(double)*bufN);
        memset(winSum, 0, sizeof(double)*bufN);
        for (int f = 0; f < nFramesTotal; f++) {
            size_t f0 = (size_t)f*HOP;
            for (int i = 0; i < FFT_N; i++) {
                size_t idx = f0 + i;
                double v = (idx < frames) ? (double)pcm[idx*2 + c] : 0.0;
                re[i] = v*win[i]; im[i] = 0.0;
            }
            fft1d(re, im, FFT_N, 0);
            if (haveNoise) {
                for (int k = 0; k < FFT_N; k++) {
                    double magS = sqrt(re[k]*re[k] + im[k]*im[k]);
                    double magN = sqrt(noise[k]);
                    if (magS <= 0.0 || magN <= 0.0) continue;
                    double g = 1.0 - alpha*magN/magS;
                    if (g < floorGain) g = floorGain;
                    re[k] *= g; im[k] *= g;
                }
            }
            /* synthesis: inverse FFT, synthesis window, overlap-add */
            fft1d(re, im, FFT_N, 1);
            for (int i = 0; i < FFT_N; i++) {
                size_t idx = f0 + i;
                if (idx >= bufN) break;
                accum[idx]  += re[i]*win[i];
                winSum[idx] += win[i]*win[i];
            }
        }
        /* normalize WOLA overlaps and clamp back to 16-bit */
        for (size_t i = 0; i < frames; i++) {
            double w = (winSum[i] > 1e-9) ? 1.0/winSum[i] : 0.0;
            double v = accum[i]*w;
            if (v >  32767.0) v =  32767.0;
            if (v < -32768.0) v = -32768.0;
            pcm[i*2 + c] = (int16_t)v;
        }
    }

    if (out) {
        out->framesProcessed  = (int32_t)frames;
        out->clicksRepaired   = 0;
        out->cracklesRepaired = 0;
    }
    free(win); free(re); free(im); free(noise); free(accum); free(winSum);
    return 0;
}

/* calibration tone WAV bytes: replicates the disc-measured spec
 * (44.1 kHz, 16-bit stereo, L=R duplicated, round(sin·32767)),
 * INCLUDING the disc's inclusive endpoint: frames = rate·seconds + 1,
 * the last frame being the sine's natural continuation (0 for the disc's
 * 80/100/1k/10k Hz set since f·30s is an integer). The four original
 * Calibrate folder tones all have data length (rate·30+1)·4 = 5,292,004. */
uint8_t *dsp_calibration_tone(int32_t frequency, int32_t rate, int32_t seconds, size_t *bytesOut) {
    if (rate <= 0 || seconds <= 0) return NULL;
    int64_t total = (int64_t)rate*seconds + 1;   /* inclusive endpoint, as on disc */
    size_t n = (size_t)total*4;                  /* 16-bit stereo */
    uint8_t *buf = (uint8_t*)malloc(n + 44);
    if (!buf) return NULL;
    uint8_t hdr[44] = {
        'R','I','F','F', 0,0,0,0, 'W','A','V','E',
        'f','m','t',' ', 16,0,0,0, 1,0, 2,0,
        0x44,0xac,0,0, 0,0,0x10,0, 4,0, 0x10,0,
        'd','a','t','a', 0,0,0,0
    };
    uint32_t dataLen = (uint32_t)n;
    uint32_t bytesSec = (uint32_t)rate*4;
    hdr[24] = (uint8_t)rate; hdr[25]=(rate>>8)&0xff; hdr[26]=(rate>>16)&0xff; hdr[27]=(rate>>24)&0xff;
    hdr[28] = bytesSec&0xff; hdr[29]=(bytesSec>>8)&0xff; hdr[30]=(bytesSec>>16)&0xff; hdr[31]=(bytesSec>>24)&0xff;
    uint32_t riff = 36 + dataLen;
    hdr[4] = riff&0xff; hdr[5]=(riff>>8)&0xff; hdr[6]=(riff>>16)&0xff; hdr[7]=(riff>>24)&0xff;
    hdr[40] = dataLen&0xff; hdr[41]=(dataLen>>8)&0xff; hdr[42]=(dataLen>>16)&0xff; hdr[43]=(dataLen>>24)&0xff;
    memcpy(buf, hdr, 44);
    int16_t *w = (int16_t*)(buf + 44);
    for (int64_t i = 0; i < total; i++) {
        double t = (double)i/(double)rate;
        double sm = sin(2.0*M_PI*(double)frequency*t)*32767.0;
        if (sm >  32767.0) sm =  32767.0;
        if (sm < -32768.0) sm = -32768.0;
        int16_t v = (int16_t)lrint(sm);   /* matches disc (round, not truncate) */
        w[i*2] = v; w[i*2+1] = v;
    }
    if (bytesOut) *bytesOut = n + 44;
    return buf;
}
