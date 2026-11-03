/* audio.c — waveOut playback worker (thread + ring of buffers).
 * Device/buffers/buffer-size come from the Soundcard dialog (src/dialogs.c),
 * mirroring the original's Options>Soundcard settings page. */
#include "audio.h"
#include "dialogs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BUFS 10          /* Soundcard dialog allows 4..10 */
#define DEF_BUFS 4
#define DEF_BUFBY 32768      /* default allocation until configured */

struct AudioPlayer {
    HWND       wnd;
    UINT       doneMsg;
    HWAVEOUT   hwo;
    WAVEHDR    hdr[MAX_BUFS];
    int16_t   *buf[MAX_BUFS];
    DWORD      bufBy;        /* current allocation per buffer */
    int        nbufs;        /* ring slots in play */
    CRITICAL_SECTION lock;
    HANDLE     thread;
    HANDLE     sem;
    volatile LONG run;        /* thread keep-alive; only audio_free clears it */
    volatile LONG playing;
    volatile LONG pos;
    const WaveDoc *doc;
    int32_t    frame;
    AudioPosCb cb;
    void      *cbctx;
};

static DWORD WINAPI worker(LPVOID arg) {
    AudioPlayer *ap=(AudioPlayer*)arg;
    for (;;) {
        WaitForSingleObject(ap->sem, INFINITE);
        if (!ap->run) break;                 /* shutdown (audio_free only) */
        if (!ap->playing) continue;          /* spurious slot (stop/end raced) */
        /* map an unlocked buffer */
        int blk=-1;
        EnterCriticalSection(&ap->lock);
        for (int i=0;i<ap->nbufs;i++) {
            if (ap->hdr[i].dwFlags & WHDR_DONE) { blk=i; break; }
        }
        LeaveCriticalSection(&ap->lock);
        if (blk<0) { Sleep(5); continue; }
        int got=0;
        EnterCriticalSection(&ap->lock);
        int32_t frames = ap->bufBy / 4; /* 16-bit stereo */
        const int16_t *src=(const int16_t*)ap->doc->pcmData;
        size_t totalFrames=ap->doc->pcmBytes/4;
        int16_t *dst=ap->buf[blk];
        memset(dst,0,ap->bufBy);
        if ((size_t)ap->frame < totalFrames) {
            size_t n=frames;
            if (ap->frame + (int32_t)n > (int32_t)totalFrames) n=totalFrames-ap->frame;
            memcpy(dst, src + (size_t)ap->frame*2, n*2*sizeof(int16_t));
            ap->frame += (int32_t)n;
            got=1;
        }
        LeaveCriticalSection(&ap->lock);
        ap->pos = ap->frame;                 /* cursor follows the last decoded block */
        if (ap->cb) ap->cb(ap->cbctx, ap->frame);
        if (got) {
            ap->hdr[blk].dwBufferLength=ap->bufBy;
            ap->hdr[blk].dwFlags &= ~WHDR_DONE;
            waveOutWrite(ap->hwo, &ap->hdr[blk], sizeof(WAVEHDR));
        } else {
            ap->playing=0;
            PostMessage(ap->wnd, ap->doneMsg, 0, 0);
            /* end of track: keep the thread alive (a later seek/play reuses it);
             * drain leftover scheduled slots so ReleaseSemaphore counts stay aligned */
            while (WaitForSingleObject(ap->sem, 0) == WAIT_OBJECT_0) { }
            continue;
        }
    }
    return 0;
}

AudioPlayer *audio_create(HWND notifyWnd, UINT doneMsg) {
    AudioPlayer *ap=(AudioPlayer*)calloc(1,sizeof(AudioPlayer));
    if (!ap) return NULL;
    ap->wnd=notifyWnd; ap->doneMsg=doneMsg;
    ap->hwo=NULL;
    ap->nbufs=DEF_BUFS; ap->bufBy=DEF_BUFBY;
    InitializeCriticalSection(&ap->lock);
    ap->sem=CreateSemaphoreW(NULL,0,MAX_BUFS*4,NULL);
    ap->run=1;                           /* keep-alive starts enabled; audio_free clears it */
    ap->thread=CreateThread(NULL,0,worker,ap,0,NULL);
    return ap;
}

static void alloc_bufs(AudioPlayer *ap, int nbufs, DWORD bufBy) {
    /* buffers are never freed while the worker runs (use-after-free window);
     * they are only re-allocated when a larger size is needed, and released
     * once, after the thread has exited (audio_free). */
    for (int i=0;i<nbufs;i++) {
        if (!ap->buf[i]) {
            ap->buf[i]=(int16_t*)malloc(bufBy);
        } else if (ap->bufBy<bufBy) {
            int16_t *nb=(int16_t*)realloc(ap->buf[i],bufBy);
            if (nb) ap->buf[i]=nb;
        }
        if (ap->buf[i]) {
            ZeroMemory(&ap->hdr[i],sizeof(WAVEHDR));
            ap->hdr[i].lpData=(LPSTR)ap->buf[i];
            ap->hdr[i].dwBufferLength=bufBy;
            ap->hdr[i].dwFlags=WHDR_DONE;
        }
    }
    ap->nbufs=nbufs; ap->bufBy=bufBy;
}

static void free_bufs(AudioPlayer *ap) {
    for (int i=0;i<MAX_BUFS;i++) { free(ap->buf[i]); ap->buf[i]=NULL; }
}

BOOL audio_open(AudioPlayer *ap, const WaveDoc *doc) {
    audio_stop(ap);
    /* Configuration mirrors the original's Soundcard page: device pick,
     * buffer count 4..10 and buffer size in bytes. Stored in the neutral
     * registry key; st_* accessors clamp to the dialog's documented ranges. */
    int dev = snd_play_dev();               /* -1 = WAVE_MAPPER (system) */
    UINT deviceId = (dev<0) ? WAVE_MAPPER : (UINT)dev;
    ap->nbufs = snd_play_bufs();            /* 4..10 */
    DWORD wantBy = (DWORD)snd_play_bufsize(); /* 16384..131072 */
    WAVEFORMATEX f;
    f.wFormatTag=WAVE_FORMAT_PCM; f.nChannels=2; f.wBitsPerSample=16;
    f.nSamplesPerSec=(DWORD)(doc->sampleRate?doc->sampleRate:44100);
    f.nBlockAlign=(WORD)(f.nChannels*f.wBitsPerSample/8);
    f.nAvgBytesPerSec=f.nSamplesPerSec*f.nBlockAlign;
    f.cbSize=0;
    MMRESULT r=waveOutOpen(&ap->hwo,deviceId,&f,(DWORD_PTR)ap->wnd,0,CALLBACK_WINDOW);
    if (r!=MMSYSERR_NOERROR) { ap->hwo=NULL; return FALSE; }
    alloc_bufs(ap, ap->nbufs, wantBy);
    for (int i=0;i<ap->nbufs;i++) {
        if (!ap->buf[i]) { waveOutClose(ap->hwo); ap->hwo=NULL; return FALSE; }
        /* MME contract: prepare before waveOutWrite, unprepare before close */
        waveOutPrepareHeader(ap->hwo, &ap->hdr[i], sizeof(WAVEHDR));
        ap->hdr[i].dwFlags |= WHDR_DONE;   /* ready for the ring */
    }
    ap->doc=doc; ap->frame=0; ap->playing=0;
    return TRUE;
}

BOOL audio_is_playing(const AudioPlayer *ap) { return ap->playing!=0; }
int32_t audio_cursor_frame(const AudioPlayer *ap) { return ap->pos; }

void audio_seek_and_play(AudioPlayer *ap, const WaveDoc *doc, int32_t frameIndex) {
    /* audio_open stops any prior playback (parks the worker, drains slots),
     * then OPENS the device fresh for this play. Do not call audio_stop after
     * opening: it closes hwo and every following waveOutWrite would fail. */
    if (!audio_open(ap,doc)) return;
    EnterCriticalSection(&ap->lock);
    ap->doc=doc;
    ap->frame=frameIndex;
    ap->pos=frameIndex;
    LeaveCriticalSection(&ap->lock);
    ap->playing=1;
    ReleaseSemaphore(ap->sem, ap->nbufs, NULL);
}

void audio_stop(AudioPlayer *ap) {
    if (ap->hwo) {
        waveOutReset(ap->hwo);
        EnterCriticalSection(&ap->lock);
        for (int i=0;i<MAX_BUFS;i++) ap->hdr[i].dwFlags|=WHDR_DONE;
        LeaveCriticalSection(&ap->lock);
        /* unprepare while the handle is still open (worker is parked: playing=0) */
        for (int i=0;i<ap->nbufs;i++) {
            if (ap->hdr[i].lpData)
                waveOutUnprepareHeader(ap->hwo, &ap->hdr[i], sizeof(WAVEHDR));
        }
        waveOutClose(ap->hwo);
        ap->hwo=NULL;
    }
    ap->playing=0;
    /* drop any leftover scheduled slots (worker will park itself) */
    while (WaitForSingleObject(ap->sem, 0) == WAIT_OBJECT_0) { }
}

void audio_free(AudioPlayer *ap) {
    if (!ap) return;
    audio_stop(ap);
    ap->run=0;
    ReleaseSemaphore(ap->sem,1,NULL);
    if (ap->thread) { WaitForSingleObject(ap->thread,INFINITE); CloseHandle(ap->thread); }
    if (ap->sem) CloseHandle(ap->sem);
    DeleteCriticalSection(&ap->lock);
    free_bufs(ap);
    free(ap);
}

void audio_set_pos_cb(AudioPlayer *ap, AudioPosCb cb, void *ctx) {
    ap->cb=cb; ap->cbctx=ctx;
}
