#define _CRT_SECURE_NO_WARNINGS
#include "miniaac.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#if defined(_WIN32)
#define seek64 _fseeki64
#define tell64 _ftelli64
#else
#define seek64 fseeko
#define tell64 ftello
#endif

typedef struct { FILE *file; uint64_t calls,bytes; size_t max_read; int force_failure; } reader;
static int read_at(void *u,uint64_t at,void *dst,size_t bytes) {
    reader *r=(reader *)u;
    if(r->force_failure) return -1;
    r->calls++; r->bytes+=bytes; if(bytes>r->max_read) r->max_read=bytes;
    return seek64(r->file,(int64_t)at,SEEK_SET) || fread(dst,1,bytes,r->file)!=bytes;
}
static uint32_t hash_pcm(const int16_t *pcm,size_t count) {
    uint32_t h=0;
    while(count--) h=h*33u+(uint16_t)*pcm++;
    return h;
}
int main(int argc,char **argv) {
    reader r={0}; miniaac_source source; miniaac *d=NULL; miniaac_info info;
    miniaac_status rc; uint64_t frames=0; size_t count,bytes=miniaac_workspace_size(),first_count=0;
    uint32_t first_hash=0,pcm_hash=0; unsigned repeat;
    void *arena; FILE *out=NULL; int16_t pcm[MINIAAC_PCM_SAMPLES]; clock_t start=clock();
    if(argc<2) { fprintf(stderr,"usage: miniaac_decode input.m4a [output.s16le]\n"); return 2; }
    r.file=fopen(argv[1],"rb"); if(!r.file) { perror(argv[1]); return 2; }
    seek64(r.file,0,SEEK_END); source.size=(uint64_t)tell64(r.file);
    source.user=&r; source.read_at=read_at;
    arena=malloc(bytes); if(!arena) return 2;
    rc=miniaac_open(arena,bytes,&source,&d);
    if(rc!=MINIAAC_OK) { fprintf(stderr,"open: %s (%d)\n",miniaac_status_string(rc),rc); free(arena); fclose(r.file); return 1; }
    miniaac_get_info(d,&info);
    if(argc>2) { out=fopen(argv[2],"wb"); if(!out) { perror(argv[2]); return 2; } }
    /* A short output buffer must not consume the first frame. */
    count=99;
    if(miniaac_decode_next(d,pcm,1,&count)!=MINIAAC_OUTPUT_TOO_SMALL || count) return 3;
    while((rc=miniaac_decode_next(d,pcm,MINIAAC_PCM_SAMPLES,&count))==MINIAAC_OK) {
        if(!first_count) { first_count=count; first_hash=hash_pcm(pcm,count); }
        { size_t i; for(i=0;i<count;i++) pcm_hash=pcm_hash*33u+(uint16_t)pcm[i]; }
        frames+=count/info.channels;
        if(out && fwrite(pcm,sizeof(*pcm),count,out)!=count) return 2;
    }
    if(out) fclose(out);
    printf("{\"rate\":%u,\"channels\":%u,\"frames\":%llu,\"declared_frames\":%llu,"
           "\"workspace\":%zu,\"pcm_buffer\":%zu,\"total_component_ram\":%zu,"
           "\"read_calls\":%llu,\"read_bytes\":%llu,\"max_read\":%zu,\"pcm_hash\":%u,\"cpu_seconds\":%.6f,\"status\":%d}\n",
           (unsigned)info.sample_rate,(unsigned)info.channels,(unsigned long long)frames,
           (unsigned long long)info.total_frames,bytes,sizeof(pcm),bytes+sizeof(pcm),
           (unsigned long long)r.calls,(unsigned long long)r.bytes,r.max_read,(unsigned)pcm_hash,
           (double)(clock()-start)/CLOCKS_PER_SEC,rc);
    miniaac_close(d); miniaac_close(d);
    if(rc==MINIAAC_EOF) {
        /* Reuse one caller-owned arena; each open must reset codec/parser state. */
        for(repeat=0;repeat<10;repeat++) {
            if(miniaac_open(arena,bytes,&source,&d)!=MINIAAC_OK) return 3;
            if(miniaac_decode_next(d,pcm,MINIAAC_PCM_SAMPLES,&count)!=MINIAAC_OK ||
               count!=first_count || hash_pcm(pcm,count)!=first_hash) return 3;
            miniaac_close(d);
        }
        if(miniaac_get_info(d,&info)!=MINIAAC_ARGUMENT) return 3;
        if(miniaac_open(arena,bytes,&source,&d)!=MINIAAC_OK) return 3;
        r.force_failure=1;
        if(miniaac_decode_next(d,pcm,MINIAAC_PCM_SAMPLES,&count)!=MINIAAC_IO || count) return 3;
        r.force_failure=0;
        if(miniaac_decode_next(d,pcm,MINIAAC_PCM_SAMPLES,&count)!=MINIAAC_IO || count) return 3;
        miniaac_close(d);
    }
    free(arena); fclose(r.file);
    if(rc!=MINIAAC_EOF) { fprintf(stderr,"decode: %s (%d)\n",miniaac_status_string(rc),rc); return 1; }
    return 0;
}
