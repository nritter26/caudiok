/* audio.h — playback engine (waveOut), added in v0.1; behavior stands in for Dac32 path */
#ifndef CAUDOK_AUDIO_H
#define CAUDOK_AUDIO_H

#include <windows.h>
#include "../src/waveio.h"

typedef struct AudioPlayer AudioPlayer;

typedef void (*AudioPosCb)(void *ctx, int32_t frameIndex);

AudioPlayer *audio_create(HWND notifyWnd, UINT playDoneMsg);
BOOL  audio_open(AudioPlayer *ap, const WaveDoc *doc);
BOOL  audio_is_playing(const AudioPlayer *ap);
int32_t audio_cursor_frame(const AudioPlayer *ap);
void  audio_seek_and_play(AudioPlayer *ap, const WaveDoc *doc, int32_t frameIndex);
void  audio_stop(AudioPlayer *ap);
void  audio_free(AudioPlayer *ap);
void  audio_set_pos_cb(AudioPlayer *ap, AudioPosCb cb, void *ctx);

#endif
