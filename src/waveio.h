/* waveio.h — WAV in/32kB-slices; keeps 44.1/16/stereo working set like original */
#ifndef CAUDOK_WAVEIO_H
#define CAUDOK_WAVEIO_H

#if defined(_WIN32) || defined(__MINGW32__)
#include <windows.h>
#else
#include "lin-shim.h"   /* headless Linux test harness only; not shipped */
#endif
#include <stdint.h>

typedef struct WaveDoc {
    int32_t  sampleRate;
    int32_t  channels;
    int32_t  bytesPerSample;
    uint8_t *pcmData;        /* interleaved */
    size_t   pcmBytes;
    wchar_t  path[MAX_PATH];
} WaveDoc;

typedef enum { WAVE_OK, WAVE_ENOENT, WAVE_ENOFORMAT, WAVE_ENOMEM, WAVE_EMBEDFMT } WaveErr;

WaveErr wave_load(const wchar_t *path, WaveDoc **out);
WaveErr wave_save(const WaveDoc *doc, const wchar_t *path);
void    wave_free(WaveDoc *doc);
/* waveform peak cache (like analysis .pk files: per-column min/max int16 pairs) */
typedef struct Peaks { int32_t *mins; int32_t *maxs; int32_t cols; int32_t colsPerSample; } Peaks;
Peaks  *peaks_build(const WaveDoc *doc, int32_t columns);
void    peaks_free(Peaks *p);

#endif
