/* waveio.c — WAV loader supporting PCM 8/16/24/32-bit + float32; converts to int16 stereo internally when needed */
#include "waveio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RIFF chunk reader (small files only assumed; guards included) */
typedef struct { uint32_t id; uint32_t size; } RiffChunk;

static uint32_t tag(const uint8_t *p) { return p[0] | (p[1]<<8) | (p[2]<<16) | ((uint32_t)p[3]<<24); }

/* convert one raw sample to int16; src is a single channel's bytes */
static int16_t conv_sample(const uint8_t *p, int bits, int fmtTag) {
    switch (bits) {
    case 8:  return (int16_t)(((int)p[0]-128) << 8);
    case 16: return (int16_t)(p[0] | (p[1] << 8));
    case 24: {
        int32_t s = (int32_t)(((uint32_t)p[2] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[0] << 8));
        s >>= 8;                    /* arithmetic shift: sign-extends */
        return (int16_t)(s >> 8);   /* scale down to 16-bit */
    }
    case 32:
        if (fmtTag == 3) {          /* IEEE float32 [-1,+1] */
            float f; memcpy(&f, p, 4);
            if (f >= 1.0f)  return 32767;
            if (f <= -1.0f) return -32768;
            return (int16_t)(f * 32767.0f);
        }
        return (int16_t)((int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)) >> 16);
    default: return 0;
    }
}

WaveErr wave_load(const wchar_t *path, WaveDoc **outDoc) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return WAVE_ENOENT;
    uint8_t hdr[12];
    if (fread(hdr,1,12,f)!=12 || tag(hdr)!=tag((const uint8_t*)"RIFF") || tag(hdr+8)!=tag((const uint8_t*)"WAVE")) { fclose(f); return WAVE_ENOFORMAT; }
    int64_t fsize = _ftelli64(f); fseek(f,0,SEEK_END); int64_t flen=_ftelli64(f); fsize=flen; (void)fsize;
    int32_t fmtTag=0, ch=0, rate=0, bits=0; int64_t dataOff=-1; uint64_t dataLen=0;
    fseek(f,12,SEEK_SET);
    for (;;) {
        uint8_t ch8[8];
        if (fread(ch8,1,8,f)!=8) break;
        uint32_t sz = ch8[4] | (ch8[5]<<8) | (ch8[6]<<16) | ((uint32_t)ch8[7]<<24);
        if (tag(ch8)==tag((const uint8_t*)"fmt ")) {
            uint8_t fmt[40]; size_t take = sz<40?sz:40;
            if (fread(fmt,1,take,f)!=take) break;
            fmtTag = fmt[0] | (fmt[1]<<8);
            ch     = fmt[2] | (fmt[3]<<8);
            rate   = fmt[4] | (fmt[5]<<8) | (fmt[6]<<16) | ((uint32_t)fmt[7]<<24);
            bits   = fmt[14] | (fmt[15]<<8);
            fseek(f, (long)sz - (long)take, SEEK_CUR);
        } else if (tag(ch8)==tag((const uint8_t*)"data")) {
            dataOff = _ftelli64(f); dataLen = sz;
            fseek(f, (long)((sz+1)&~1u), SEEK_CUR);
        } else {
            fseek(f, (long)((sz+1)&~1u), SEEK_CUR);
        }
        if ((uint64_t)_ftelli64(f) % 2) fseek(f,1,SEEK_CUR);
    }
    fclose(f);
    if (dataOff<0 || !rate || !ch) return WAVE_ENOFORMAT;

    /* accept PCM(1) 8/16/24/32 and float32(3); reject others like the original does for its engine */
    if (!((fmtTag==1 && (bits==8||bits==16||bits==24||bits==32)) || (fmtTag==3 && bits==32)))
        return WAVE_EMBEDFMT;

    WaveDoc *doc = (WaveDoc *)calloc(1, sizeof(WaveDoc));
    if (!doc) return WAVE_ENOMEM;
    doc->sampleRate=rate; doc->channels=ch; doc->bytesPerSample=bits/8;
    uint64_t frameBytes=(uint64_t)ch*(bits/8);
    uint64_t frames=(frameBytes? dataLen/frameBytes : 0);
#ifdef CAUDOKDBG
    fprintf(stderr,"dbg: dl=%llu fb=%llu fr=%llu rate=%d ch=%d bits=%d\n",
        (unsigned long long)dataLen,(unsigned long long)frameBytes,(unsigned long long)frames,rate,ch,bits);
#endif
    doc->pcmBytes=(uint64_t)frames*4;   /* convert to 16-bit stereo */
    doc->pcmData=(uint8_t*)malloc((size_t)doc->pcmBytes ? (size_t)doc->pcmBytes : 1);
    if (!doc->pcmData){ wave_free(doc); return WAVE_ENOMEM; }
    if (wcslen(path) < MAX_PATH) wcscpy(doc->path, path);
    /* load raw and convert */
    FILE *g=_wfopen(path,L"rb");
    fseek(g,(long)dataOff,SEEK_SET);
    uint64_t rawBytes=frames*frameBytes;
    uint8_t *raw=(uint8_t*)malloc((size_t)(rawBytes?rawBytes:1));
    if (!raw || fread(raw,1,(size_t)rawBytes,g)!=(size_t)rawBytes) {
        free(raw); fclose(g); wave_free(doc); return WAVE_ENOFORMAT;
    }
    fclose(g);
    int16_t *dst=(int16_t*)doc->pcmData;
    for (uint64_t i=0;i<frames;i++) {
        uint8_t *p=raw + i*frameBytes;
        int16_t Lv=conv_sample(p,bits,fmtTag);
        int16_t Rv=ch>=2 ? conv_sample(p+(bits/8),bits,fmtTag) : Lv;
        dst[i*2]=Lv; dst[i*2+1]=Rv;
    }
#ifdef CAUDOKDBG
    fprintf(stderr,"dbg2: doc=%p sizeof(WaveDoc)=%zu pcmBytes=%llu\n",(void*)doc,sizeof(WaveDoc),(unsigned long long)doc->pcmBytes);
#endif
    free(raw);
    *outDoc=doc; return WAVE_OK;
}


WaveErr wave_save(const WaveDoc *doc, const wchar_t *path) {
    /* write PCM 16 stereo */
    FILE *f=_wfopen(path,L"wb");
    if (!f) return WAVE_ENOENT;
    uint32_t riff_len=36+(uint32_t)doc->pcmBytes;
    uint8_t h[12]={ 'R','I','F','F' };
    h[4]=riff_len; h[5]=riff_len>>8; h[6]=riff_len>>16; h[7]=riff_len>>24;
    memcpy(h+8,"WAVE",4); fwrite(h,1,12,f);
    uint32_t bytesec = (uint32_t)doc->sampleRate * 4;
    uint8_t fmt2[24]={ 'f','m','t',' ',16,0,0,0, 1,0, 2,0,
        (doc->sampleRate)&0xFF, ((doc->sampleRate)>>8)&0xFF, ((doc->sampleRate)>>16)&0xFF, ((doc->sampleRate)>>24)&0xFF,
        (bytesec)&0xFF, (bytesec>>8)&0xFF, (bytesec>>16)&0xFF, (bytesec>>24)&0xFF, 4,0, 0x10,0 };
    fwrite(fmt2,1,24,f);
    const uint8_t dhdr[8]={ 'd','a','t','a', 0,0,0,0 };
    uint8_t db[8]; memcpy(db,dhdr,8);
    db[4]=(uint8_t)(doc->pcmBytes); db[5]=doc->pcmBytes>>8; db[6]=doc->pcmBytes>>16; db[7]=doc->pcmBytes>>24;
    fwrite(db,1,8,f);
    fwrite(doc->pcmData,1,doc->pcmBytes,f);
    fclose(f);
    return WAVE_OK;
}

void wave_free(WaveDoc *doc) {
    if (!doc) return;
    free(doc->pcmData);
    free(doc);
}

Peaks *peaks_build(const WaveDoc *doc, int32_t columns) {
    if (!doc || !doc->pcmData || columns<1) return NULL;
    Peaks *pk=(Peaks*)calloc(1,sizeof(Peaks));
    if (!pk) return NULL;
    pk->cols=columns;
    pk->mins=(int32_t*)calloc((size_t)columns,sizeof(int32_t));
    pk->maxs=(int32_t*)calloc((size_t)columns,sizeof(int32_t));
    if (!pk->mins || !pk->maxs) { peaks_free(pk); return NULL; }
    const int16_t *s=(const int16_t*)doc->pcmData;
    size_t frames=doc->pcmBytes/4;
    for (int32_t c=0;c<columns;c++) {
        int32_t lo=INT16_MAX, hi=INT16_MIN;
        size_t a=(size_t)((int64_t)frames*c/columns), b=(size_t)((int64_t)frames*(c+1)/columns);
        for (size_t f=a; f<b; f++) {
            int16_t l=s[f*2], r=s[f*2+1];
            int16_t mn = l<r?l:r, mx=l>r?l:r;
            if (mn<lo) lo=mn;
            if (mx>hi) hi=mx;
        }
        if (lo>hi) { lo=0; hi=0; }
        pk->mins[c]=lo; pk->maxs[c]=hi;
    }
    return pk;
}

void peaks_free(Peaks *pk) {
    if (!pk) return;
    free(pk->mins); free(pk->maxs); free(pk);
}
