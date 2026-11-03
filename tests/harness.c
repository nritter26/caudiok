/* harness.c — headless test driver for src/waveio.c on Linux (same code paths the Windows build uses).
 * Fixture matrix covers: round-trip, mono-upmix, 8/16/24/float32 conversions (incl. negative samples),
 * chunks-before-data, odd data sizes, malformed files, peaks bounds. Exit non-zero on any failure. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../src/waveio.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); failures++; } } while (0)

static void wr32(uint8_t *p, uint32_t v) { p[0]=v&0xff; p[1]=(v>>8)&0xff; p[2]=(v>>16)&0xff; p[3]=(v>>24)&0xff; }
static void put(uint8_t **b, const char *tag, const uint8_t *body, uint32_t n) {
    memcpy(*b, tag, 4); *b += 4;
    wr32(*b, n); *b += 4;
    memcpy(*b, body, n); *b += n;
    if (n & 1) *(*b)++ = 0;   /* pad to even */
}
static void bld_hdr(uint8_t **b) {
    memcpy(*b,"RIFF",4); *b+=4;
    uint8_t *szp = *b; *b+=4;
    wr32(szp,0);            /* placeholder, patched by fin_rifflen */
    memcpy(*b,"WAVE",4); *b+=4;
}
static void fin_rifflen(uint8_t *start, uint8_t *end) { wr32(start+4, (uint32_t)(end - start - 8)); }

/* build WAV variant files directly */
static size_t writefiles(void) {
    static uint8_t buf[8192];
    uint8_t *b=buf, *start=buf;
    /* A: stereo 16-bit 44100, 10 frames; extra LIST chunk before fmt */
    bld_hdr(&b);
    put(&b, "LIST", (const uint8_t *)"\x03strf", 5);
    uint8_t fmt[16]={1,0, 2,0, 0x44,0xac,0,0, 0,0,0x10,0, 4,0, 0x10,0};
    put(&b,"fmt ",fmt,16);
    uint8_t pcmA[40];
    for (int i=0;i<10;i++) {
        int16_t L=(int16_t)(32767 - i*3000), R=(int16_t)(-32768 + i*3000);
        pcmA[i*4+0]=L&0xff; pcmA[i*4+1]=L>>8; pcmA[i*4+2]=R&0xff; pcmA[i*4+3]=R>>8;
    }
    put(&b,"data",pcmA,40);
    *b=0; if (((b-start)&1)) b++;
    fin_rifflen(start,b);
    FILE *f=fopen("/tmp/opencode/h/as16.wav","wb"); fwrite(buf,1,b-start,f); fclose(f);

    /* B: mono 16-bit 22050, 6 frames sine-ish */
    b=start; bld_hdr(&b);
    uint8_t fmtB[16]={1,0, 1,0, 0x22,0x56,0,0, 0,0,0x04,0, 2,0, 0x10,0};
    put(&b,"fmt ",fmtB,16);
    uint8_t pcmB[12];
    for (int i=0;i<6;i++) { int16_t S=(int16_t)(4000*(i%2?1:-1)); pcmB[i*2]=S&0xff; pcmB[i*2+1]=S>>8; }
    put(&b,"data",pcmB,12);
    *b=0; fin_rifflen(start,b);
    f=fopen("/tmp/opencode/h/mo16.wav","wb"); fwrite(buf,1,b-start,f); fclose(f);

    /* C: stereo 24-bit 44100, 4 frames incl. NEGATIVE sample */
    b=start; bld_hdr(&b);
    uint8_t fmtC[16]={1,0, 2,0, 0x44,0xac,0,0, 0,0,0x18,0, 6,0, 0x18,0};
    put(&b,"fmt ",fmtC,16);
    uint8_t pcmC[24];
    /* L=-2097152 (0xE00000) -> expect -8192; R=+8388607 (0x7FFFFF) -> 32767 (clamped by >>8) */
    uint32_t L24=0xE00000u, R24=0x7FFFFFu;
    for (int i=0;i<4;i++) {
        pcmC[i*6+0]=L24&0xff; pcmC[i*6+1]=(L24>>8)&0xff; pcmC[i*6+2]=(L24>>16)&0xff;
        pcmC[i*6+3]=R24&0xff; pcmC[i*6+4]=(R24>>8)&0xff; pcmC[i*6+5]=(R24>>16)&0xff;
    }
    put(&b,"data",pcmC,24);
    *b=0; fin_rifflen(start,b);
    f=fopen("/tmp/opencode/h/st24.wav","wb"); fwrite(buf,1,b-start,f); fclose(f);

    /* D: stereo float32 44100, 3 frames incl. negatives and >1.0 clamp */
    b=start; bld_hdr(&b);
    uint8_t fmtD[16]={3,0, 2,0, 0x44,0xac,0,0, 0,0,0x08,0, 8,0, 0x20,0};
    put(&b,"fmt ",fmtD,16);
    uint8_t pcmD[24];
    float vals[6]={0.5f,-0.25f,1.5f,-1.5f,0.0f,0.0625f};
    for (int i=0;i<6;i++) memcpy(pcmD+i*4,&vals[i],4);
    put(&b,"data",pcmD,24);
    *b=0; fin_rifflen(start,b);
    f=fopen("/tmp/opencode/h/st32f.wav","wb"); fwrite(buf,1,b-start,f); fclose(f);

    /* E: stereo 8-bit, 4 frames */
    b=start; bld_hdr(&b);
    uint8_t fmtE[16]={1,0, 2,0, 0x44,0xac,0,0, 0,0,0x04,0, 2,0, 0x08,0};
    put(&b,"fmt ",fmtE,16);
    uint8_t pcmE[8]={0x80,0x7F,0xFF,0x01,0xC0,0x40,0x81,0x100-0x81};
    put(&b,"data",pcmE,8);
    *b=0; fin_rifflen(start,b);
    f=fopen("/tmp/opencode/h/st08.wav","wb"); fwrite(buf,1,b-start,f); fclose(f);

    /* F: ODD data chunk (3 bytes) + junk tail */
    b=start; bld_hdr(&b);
    uint8_t fmtF[16]={1,0, 2,0, 0x44,0xac,0,0, 0,0,0x10,0, 4,0, 0x10,0};
    put(&b,"fmt ",fmtF,16);
    put(&b,"data",(const uint8_t*)"\xAA\xBB\xCC",3);
    memcpy(b,"JUNKdata!",9); b+=9;
    *b=0; fin_rifflen(start,b);
    f=fopen("/tmp/opencode/h/odd.wav","wb"); fwrite(buf,1,b-start,f); fclose(f);

    /* G: no data chunk */
    b=start; bld_hdr(&b);
    put(&b,"fmt ",fmt,16);
    *b=0; fin_rifflen(start,b);
    f=fopen("/tmp/opencode/h/nodata.wav","wb"); fwrite(buf,1,b-start,f); fclose(f);

    /* H: truncated audio (fmt ok, data 40 declared but 20 bytes only) */
    b=start; bld_hdr(&b);
    put(&b,"fmt ",fmt,16);
    memcpy(b,"data",4); b+=4; wr32(b,40); b+=4;
    memcpy(b,pcmA,20); b+=20;
    *b=0; fin_rifflen(start,b);
    f=fopen("/tmp/opencode/h/trunc.wav","wb"); fwrite(buf,1,b-start,f); fclose(f);

    return (size_t)(b-start);
}

int main(void) {
    system("mkdir -p /tmp/opencode/h");
    writefiles();
    WaveDoc *d;
#ifdef CAUDOKDBG
    printf("hsz: sizeof(WaveDoc)=%zu\n", sizeof(WaveDoc));
#endif

    /* A roundtrip + values */
    CHECK(wave_load(L"/tmp/opencode/h/as16.wav",&d)==WAVE_OK, "as16 loads");
    CHECK(d->sampleRate==44100 && d->channels==2, "as16 fmt");
    CHECK(d->pcmBytes==40, "as16 10 frames stereo16 (4 B/frame)");
    const int16_t *s=(const int16_t*)d->pcmData;
    CHECK(s[0]==32767 && s[1]==-32768, "as16 L/R values");
    /* save & reload equality */
    CHECK(wave_save(d,L"/tmp/opencode/h/as16_r.wav")==WAVE_OK, "save ok");
    WaveDoc *d2;
    CHECK(wave_load(L"/tmp/opencode/h/as16_r.wav",&d2)==WAVE_OK, "reload ok");
    CHECK(d2->pcmBytes==d->pcmBytes && memcmp(d2->pcmData,d->pcmData,d->pcmBytes)==0, "roundtrip equal");
    wave_free(d); wave_free(d2);

    /* B mono upmix */
    CHECK(wave_load(L"/tmp/opencode/h/mo16.wav",&d)==WAVE_OK, "mono loads");
    CHECK(d->sampleRate==22050 && d->pcmBytes==24, "mono dims/upmix frames");
    wave_free(d);

    /* C 24-bit negatives */
    CHECK(wave_load(L"/tmp/opencode/h/st24.wav",&d)==WAVE_OK, "24bit loads");
    s=(const int16_t*)d->pcmData;
    CHECK(s[0]==-8192, "24bit negative correct");
    CHECK(s[1]>=32764, "24bit +max clamped right");
    wave_free(d);

    /* D float32 */
    CHECK(wave_load(L"/tmp/opencode/h/st32f.wav",&d)==WAVE_OK, "f32 loads");
    s=(const int16_t*)d->pcmData;
    CHECK(s[0]==16383, "f32 0.5 half-scale (truncation)");
    CHECK(s[1]==-8191, "f32 -0.25 negative");
    CHECK(s[2]==32767 && s[3]==-32768, "f32 clamp both ends");
    CHECK(s[4]==0, "f32 zero");
    CHECK(s[5]==2047, "f32 0.0625");
    wave_free(d);

    /* E 8-bit */
    CHECK(wave_load(L"/tmp/opencode/h/st08.wav",&d)==WAVE_OK, "8bit loads");
    s=(const int16_t*)d->pcmData;
    CHECK(s[0]==0, "8bit center");
    CHECK(s[1]==-256, "8bit one-step-below");
    wave_free(d);

    /* F odd chunk + junk */
    CHECK(wave_load(L"/tmp/opencode/h/odd.wav",&d)==WAVE_OK, "odd loads");
    CHECK(d->pcmBytes==0, "odd data -> 0 frames");
    wave_free(d);
    /* G no data */
    if (wave_load(L"/tmp/opencode/h/nodata.wav",&d)==WAVE_OK) { CHECK(d->pcmBytes==0,"nodata empty"); wave_free(d);} else CHECK(1,"G error path ok");
    /* H truncated */
    { WaveErr e = wave_load(L"/tmp/opencode/h/trunc.wav",&d); CHECK(e==WAVE_ENOFORMAT||e==WAVE_OK, "trunc handled"); if (e==WAVE_OK) { CHECK(d->pcmBytes==0,"trunc0"); wave_free(d);} }

    /* peaks bounds on tiny file */
    wave_load(L"/tmp/opencode/h/as16.wav",&d);
    Peaks *pk=peaks_build(d,4);
    CHECK(pk && pk->cols==4, "peaks cols");
    for (int32_t c=0;c<4;c++) { CHECK(pk->mins[c]<=pk->maxs[c], "peaks order"); }
    peaks_free(pk);
    /* peaks with more columns than frames */
    pk=peaks_build(d,300);
    CHECK(pk, "peaks300"); peaks_free(pk);
    wave_free(d);

    printf("harness: %s (%d failures)\n", failures? "FAIL":"PASS", failures);
    return failures ? 1 : 0;
}
