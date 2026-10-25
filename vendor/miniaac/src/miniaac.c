/* SPDX-License-Identifier: MIT -- wrapper/parser only; see licenses for Helix. */
#include "miniaac.h"
#include "helix/coder.h"
#include <limits.h>
#include <string.h>

#define FCC(a,b,c,d) (((uint32_t)(a)<<24)|((uint32_t)(b)<<16)|((uint32_t)(c)<<8)|(d))
#define ALIGN8(n) (((n)+7u)&~(size_t)7u)
#define MAGIC 0x4d414143u
#define CACHE_BYTES 256
#define TRY(x) do { miniaac_status rc_ = (x); if (rc_ != MINIAAC_OK) return rc_; } while (0)

typedef struct { uint64_t payload, end; uint32_t type; } box;
typedef struct {
    uint64_t offset;
    uint32_t count, cached_first, cached_count;
    unsigned width;
    uint8_t data[CACHE_BYTES];
} table;

struct miniaac {
    uint32_t magic;
    miniaac_status failed;
    miniaac_source source;
    miniaac_info info;
    HAACDecoder codec;
    uint64_t position, mdat_start, mdat_end;
    uint64_t media_frames, trim_start, trim_end, decoded_frames;
    uint32_t samples, sample, fixed_size;
    uint32_t chunks, chunk, chunk_samples, in_chunk, stsc_index, next_chunk;
    uint64_t chunk_position;
    uint8_t adts;
    table sizes, offsets, mappings;
    uint64_t arena[(ALIGN8(sizeof(AACDecInfo))+ALIGN8(sizeof(PSInfoBase)))/8];
    uint8_t input[MINIAAC_INPUT_BYTES];
};
typedef char workspace_fits_public_bound[(sizeof(miniaac)<=MINIAAC_WORKSPACE_BYTES)?1:-1];

static const uint32_t rates[13] = {
    96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350
};
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static uint64_t be64(const uint8_t *p) { return ((uint64_t)be32(p)<<32)|be32(p+4); }
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0]<<8)|p[1]); }
static unsigned bits(const uint8_t *p,unsigned at,unsigned count) {
    unsigned v=0;
    while(count--) { v=(v<<1)|((p[at/8]>>(7-at%8))&1); at++; }
    return v;
}
static miniaac_status read_bytes(miniaac *d, uint64_t at, void *p, size_t n) {
    if (at > d->source.size || n > d->source.size-at) return MINIAAC_FORMAT;
    return d->source.read_at(d->source.user,at,p,n) ? MINIAAC_IO : MINIAAC_OK;
}
static miniaac_status box_next(miniaac *d, uint64_t *at, uint64_t end, box *b) {
    uint8_t h[16]; uint64_t length; unsigned header=8;
    if (*at == end) return MINIAAC_EOF;
    if (*at > end || end-*at < 8) return MINIAAC_FORMAT;
    TRY(read_bytes(d,*at,h,8)); length=be32(h); b->type=be32(h+4);
    if (length==1) {
        if (end-*at < 16) return MINIAAC_FORMAT;
        TRY(read_bytes(d,*at+8,h+8,8)); length=be64(h+8); header=16;
    } else if (!length) length=end-*at;
    if (length<header || length>end-*at) return MINIAAC_FORMAT;
    b->payload=*at+header; b->end=*at+length; *at=b->end;
    return MINIAAC_OK;
}
static miniaac_status child(miniaac *d, const box *parent, uint32_t type, box *out) {
    uint64_t at=parent->payload; box b;
    while (at<parent->end) {
        TRY(box_next(d,&at,parent->end,&b));
        if (b.type==type) { *out=b; return MINIAAC_OK; }
    }
    return MINIAAC_EOF;
}
static miniaac_status required(miniaac *d,const box *p,uint32_t t,box *b) {
    miniaac_status rc=child(d,p,t,b);
    return rc==MINIAAC_EOF ? MINIAAC_FORMAT : rc;
}
static miniaac_status box_read(miniaac *d,const box *b,uint64_t rel,void *p,size_t n) {
    uint64_t length=b->end-b->payload;
    if (rel>length || n>length-rel) return MINIAAC_FORMAT;
    return read_bytes(d,b->payload+rel,p,n);
}
static miniaac_status setup_table(miniaac *d,const box *b,table *t,unsigned width,unsigned header) {
    uint8_t h[12]; uint64_t length=b->end-b->payload;
    TRY(box_read(d,b,0,h,header));
    if (be32(h)) return MINIAAC_UNSUPPORTED;
    t->count=be32(h+header-4); t->width=width;
    t->offset=b->payload+header;
    if ((uint64_t)t->count*width > length-header) return MINIAAC_FORMAT;
    return MINIAAC_OK;
}
static miniaac_status entry(miniaac *d,table *t,uint32_t index,uint8_t **out) {
    uint32_t n;
    if (index>=t->count) return MINIAAC_FORMAT;
    if (!t->cached_count || index<t->cached_first || index-t->cached_first>=t->cached_count) {
        n=CACHE_BYTES/t->width;
        t->cached_first=(index/n)*n;
        t->cached_count=t->count-t->cached_first;
        if(t->cached_count>n) t->cached_count=n;
        TRY(read_bytes(d,t->offset+(uint64_t)t->cached_first*t->width,
                       t->data,(size_t)t->cached_count*t->width));
    }
    *out=t->data+(index-t->cached_first)*t->width; return MINIAAC_OK;
}

/* DecoderSpecificInfo is nested under ES_Descriptor/DecoderConfigDescriptor.
 * Parse descriptor lengths (not a byte search that could match arbitrary data). */
static miniaac_status descriptor(miniaac *d,uint64_t *at,uint64_t end,
                                 uint8_t *tag,uint64_t *payload,uint64_t *limit) {
    uint8_t v; uint32_t n=0; unsigned i;
    if(*at>=end) return MINIAAC_FORMAT;
    TRY(read_bytes(d,(*at)++,tag,1));
    for(i=0;i<4;i++) {
        if(*at>=end) return MINIAAC_FORMAT;
        TRY(read_bytes(d,(*at)++,&v,1)); n=(n<<7)|(v&127);
        if(!(v&128)) break;
    }
    if(i==4 || n>end-*at) return MINIAAC_FORMAT;
    *payload=*at; *limit=*at+n; *at=*limit; return MINIAAC_OK;
}
static miniaac_status parse_esds(miniaac *d,const box *esds) {
    uint8_t h[16],tag; uint64_t at,p,end,es_end,dc_end;
    unsigned object,index,channels,n,bit;
    TRY(box_read(d,esds,0,h,4)); if(be32(h)) return MINIAAC_UNSUPPORTED;
    at=esds->payload+4;
    TRY(descriptor(d,&at,esds->end,&tag,&p,&end));
    if(tag!=3 || end-p<3) return MINIAAC_FORMAT;
    es_end=end; TRY(read_bytes(d,p,h,3)); at=p+3;
    if(h[2]&128) at+=2;
    if(h[2]&64) {
        if(at>=es_end) return MINIAAC_FORMAT;
        TRY(read_bytes(d,at,h,1)); at+=1+h[0];
    }
    /* Re-read flags because the optional URL read reused h. */
    TRY(read_bytes(d,p+2,h,1)); if(h[0]&32) at+=2;
    TRY(descriptor(d,&at,es_end,&tag,&p,&end));
    if(tag!=4 || end-p<13) return MINIAAC_FORMAT;
    dc_end=end; TRY(read_bytes(d,p,h,13));
    if(h[0]!=0x40 || ((h[1]>>2)&63)!=5) return MINIAAC_UNSUPPORTED;
    at=p+13; TRY(descriptor(d,&at,dc_end,&tag,&p,&end));
    if(tag!=5 || end-p<2) return MINIAAC_FORMAT;
    /* AOT=2, indexed frequency, channelConfiguration=1/2, 1024 samples.
       Accept the common optional sync extension only when SBR is absent. */
    if(end-p>sizeof(h)) return MINIAAC_UNSUPPORTED;
    n=(unsigned)(end-p); TRY(read_bytes(d,p,h,n));
    object=h[0]>>3; index=((h[0]&7)<<1)|(h[1]>>7); channels=(h[1]>>3)&15;
    if(object!=2 || index>=12 || (channels!=1 && channels!=2) || (h[1]&7))
        return MINIAAC_UNSUPPORTED;
    bit=16;
    if(n*8-bit>=17 && bits(h,bit,11)==0x2b7) {
        if(bits(h,bit+11,5)!=5 || bits(h,bit+16,1)) return MINIAAC_UNSUPPORTED;
        bit+=17;
    }
    while(bit<n*8) if(bits(h,bit++,1)) return MINIAAC_UNSUPPORTED;
    d->info.sample_rate=rates[index]; d->info.channels=(uint8_t)channels;
    return MINIAAC_OK;
}
static miniaac_status sample_description(miniaac *d,const box *stbl) {
    box stsd,mp4a,esds; uint8_t h[28]; uint64_t at;
    miniaac_status rc;
    TRY(required(d,stbl,FCC('s','t','s','d'),&stsd));
    TRY(box_read(d,&stsd,0,h,8));
    if(be32(h) || be32(h+4)!=1) return MINIAAC_UNSUPPORTED;
    at=stsd.payload+8; rc=box_next(d,&at,stsd.end,&mp4a);
    if(rc==MINIAAC_EOF) return MINIAAC_FORMAT;
    TRY(rc);
    if(mp4a.type!=FCC('m','p','4','a')) return MINIAAC_UNSUPPORTED;
    TRY(box_read(d,&mp4a,0,h,28));
    if(be16(h+6)!=1 || be16(h+8)!=0) return MINIAAC_UNSUPPORTED;
    mp4a.payload+=28;
    rc=child(d,&mp4a,FCC('s','i','n','f'),&esds);
    if(rc==MINIAAC_OK) return MINIAAC_UNSUPPORTED;
    if(rc!=MINIAAC_EOF) return rc;
    TRY(required(d,&mp4a,FCC('e','s','d','s'),&esds));
    return parse_esds(d,&esds);
}
static miniaac_status times(miniaac *d,const box *stbl,uint32_t timescale) {
    box b; uint8_t h[8]; uint64_t total=0,frames=0; uint32_t count,i,n,t;
    TRY(required(d,stbl,FCC('s','t','t','s'),&b));
    TRY(box_read(d,&b,0,h,8)); if(be32(h)) return MINIAAC_UNSUPPORTED;
    count=be32(h+4);
    if(!count || (uint64_t)count*8>b.end-b.payload-8) return MINIAAC_FORMAT;
    for(i=0;i<count;i++) {
        TRY(box_read(d,&b,8+(uint64_t)i*8,h,8)); n=be32(h); t=be32(h+4);
        if(!n || !t || t>1024 || (t!=1024 && (i+1!=count || n!=1))) return MINIAAC_UNSUPPORTED;
        frames+=n; total+=(uint64_t)n*t;
    }
    if(frames!=d->samples) return MINIAAC_FORMAT;
    if(timescale!=d->info.sample_rate) return MINIAAC_UNSUPPORTED;
    d->media_frames=total; d->trim_end=total;
    return MINIAAC_OK;
}
static miniaac_status edits(miniaac *d,const box *trak,uint32_t movie_scale) {
    box edts,elst; uint8_t h[28]; uint64_t duration,start; unsigned v;
    miniaac_status rc=child(d,trak,FCC('e','d','t','s'),&edts);
    if(rc==MINIAAC_EOF) return MINIAAC_OK;
    TRY(rc); TRY(required(d,&edts,FCC('e','l','s','t'),&elst));
    TRY(box_read(d,&elst,0,h,8)); v=h[0];
    if(v>1 || h[1] || h[2] || h[3] || be32(h+4)!=1) return MINIAAC_UNSUPPORTED;
    TRY(box_read(d,&elst,8,h,v?20:12));
    duration=v?be64(h):be32(h); start=v?be64(h+8):be32(h+4);
    if((v && start>INT64_MAX) || (!v && start>INT32_MAX) || be32(h+(v?16:8))!=0x10000)
        return MINIAAC_UNSUPPORTED;
    if(!movie_scale || start>d->media_frames) return MINIAAC_FORMAT;
    if(duration>UINT64_MAX/d->info.sample_rate) return MINIAAC_FORMAT;
    duration=duration*d->info.sample_rate/movie_scale;
    d->trim_start=start;
    d->trim_end=duration>d->media_frames-start ? d->media_frames : start+duration;
    return MINIAAC_OK;
}
static miniaac_status parse_track(miniaac *d,const box *trak,const box *mdia,uint32_t movie_scale) {
    box minf,stbl,b; uint8_t h[32],*p; uint32_t scale,i,first,per,prev=0,last=0;
    uint64_t total=0;
    TRY(required(d,mdia,FCC('m','d','h','d'),&b)); TRY(box_read(d,&b,0,h,4));
    if(h[0]>1) return MINIAAC_UNSUPPORTED;
    TRY(box_read(d,&b,h[0]?20:12,h,4)); scale=be32(h);
    TRY(required(d,mdia,FCC('m','i','n','f'),&minf));
    TRY(required(d,&minf,FCC('s','t','b','l'),&stbl));
    TRY(sample_description(d,&stbl));
    TRY(required(d,&stbl,FCC('s','t','s','z'),&b));
    TRY(box_read(d,&b,0,h,12)); if(be32(h)) return MINIAAC_UNSUPPORTED;
    d->fixed_size=be32(h+4); d->samples=be32(h+8);
    if(!d->samples) return MINIAAC_FORMAT;
    d->sizes.offset=b.payload+12; d->sizes.count=d->samples; d->sizes.width=4;
    if(!d->fixed_size && (uint64_t)d->samples*4>b.end-b.payload-12) return MINIAAC_FORMAT;
    if(d->fixed_size>MINIAAC_INPUT_BYTES) return MINIAAC_FRAME_TOO_LARGE;
    {
        miniaac_status rc=child(d,&stbl,FCC('s','t','c','o'),&b);
        if(rc==MINIAAC_EOF) {
            TRY(required(d,&stbl,FCC('c','o','6','4'),&b)); TRY(setup_table(d,&b,&d->offsets,8,8));
        } else { TRY(rc); TRY(setup_table(d,&b,&d->offsets,4,8)); }
    }
    d->chunks=d->offsets.count; if(!d->chunks) return MINIAAC_FORMAT;
    TRY(required(d,&stbl,FCC('s','t','s','c'),&b)); TRY(setup_table(d,&b,&d->mappings,12,8));
    if(!d->mappings.count) return MINIAAC_FORMAT;
    /* Validate mapping once with fixed memory; sample sizes/offsets checked lazily. */
    for(i=0;i<d->mappings.count;i++) {
        TRY(entry(d,&d->mappings,i,&p)); first=be32(p); per=be32(p+4);
        if(!first || first<=prev || first>d->chunks || !per || be32(p+8)!=1) return MINIAAC_FORMAT;
        if(!i && first!=1) return MINIAAC_FORMAT;
        if(i) total+=(uint64_t)(first-prev)*last;
        prev=first; last=per;
    }
    total+=((uint64_t)d->chunks-prev+1)*last;
    if(total!=d->samples) return MINIAAC_FORMAT;
    TRY(times(d,&stbl,scale)); TRY(edits(d,trak,movie_scale));
    d->info.total_frames=d->trim_end-d->trim_start;
    return MINIAAC_OK;
}
static miniaac_status parse_m4a(miniaac *d) {
    box root={0,0,0},moov={0,0,0},b,trak,mdia,hdlr; uint8_t h[20];
    uint64_t at=0,inner; uint32_t movie_scale; unsigned mdats=0,found=0;
    root.end=d->source.size;
    while(at<root.end) {
        TRY(box_next(d,&at,root.end,&b));
        if(b.type==FCC('m','o','o','f')) return MINIAAC_UNSUPPORTED;
        if(b.type==FCC('m','o','o','v')) { if(moov.end) return MINIAAC_FORMAT; moov=b; }
        if(b.type==FCC('m','d','a','t')) { d->mdat_start=b.payload; d->mdat_end=b.end; mdats++; }
    }
    if(!moov.end || !mdats) return MINIAAC_FORMAT;
    if(mdats!=1) return MINIAAC_UNSUPPORTED;
    {
        miniaac_status rc=child(d,&moov,FCC('m','v','e','x'),&b);
        if(rc==MINIAAC_OK) return MINIAAC_UNSUPPORTED;
        if(rc!=MINIAAC_EOF) return rc;
    }
    TRY(required(d,&moov,FCC('m','v','h','d'),&b)); TRY(box_read(d,&b,0,h,4));
    if(h[0]>1) return MINIAAC_UNSUPPORTED;
    TRY(box_read(d,&b,h[0]?20:12,h,4)); movie_scale=be32(h);
    inner=moov.payload;
    while(inner<moov.end) {
        TRY(box_next(d,&inner,moov.end,&trak));
        if(trak.type!=FCC('t','r','a','k')) continue;
        TRY(required(d,&trak,FCC('m','d','i','a'),&mdia));
        TRY(required(d,&mdia,FCC('h','d','l','r'),&hdlr)); TRY(box_read(d,&hdlr,0,h,12));
        if(be32(h+8)!=FCC('s','o','u','n')) continue;
        if(found) return MINIAAC_UNSUPPORTED;
        TRY(parse_track(d,&trak,&mdia,movie_scale)); found=1;
    }
    return found ? MINIAAC_OK : MINIAAC_UNSUPPORTED;
}
static miniaac_status adts_header(miniaac *d,uint64_t at,uint32_t *length,unsigned *header) {
    uint8_t h[7]; unsigned index,channels;
    TRY(read_bytes(d,at,h,7));
    if(h[0]!=255 || (h[1]&0xf6)!=0xf0) return MINIAAC_FORMAT;
    index=(h[2]>>2)&15; channels=((h[2]&1)<<2)|(h[3]>>6);
    if((h[2]>>6)!=1 || index>=12 || (channels!=1 && channels!=2) || (h[6]&3))
        return MINIAAC_UNSUPPORTED;
    if(d->info.sample_rate && (d->info.sample_rate!=rates[index] || d->info.channels!=channels))
        return MINIAAC_UNSUPPORTED;
    d->info.sample_rate=rates[index]; d->info.channels=(uint8_t)channels;
    *length=((uint32_t)(h[3]&3)<<11)|((uint32_t)h[4]<<3)|(h[5]>>5);
    *header=(h[1]&1)?7:9;
    if(*length<=*header || *length>d->source.size-at) return MINIAAC_FORMAT;
    if(*length-*header>MINIAAC_INPUT_BYTES) return MINIAAC_FRAME_TOO_LARGE;
    return MINIAAC_OK;
}
static miniaac_status next_m4a_packet(miniaac *d,uint32_t *bytes) {
    uint8_t *p; uint64_t offset; uint32_t first;
    if(d->sample>=d->samples) return MINIAAC_EOF;
    if(!d->chunk || d->in_chunk==d->chunk_samples) {
        d->chunk++; d->in_chunk=0;
        if(d->chunk>d->chunks) return MINIAAC_FORMAT;
        if(!d->chunk_samples || (d->next_chunk && d->chunk==d->next_chunk)) {
            TRY(entry(d,&d->mappings,d->stsc_index,&p));
            d->chunk_samples=be32(p+4); d->stsc_index++; d->next_chunk=0;
            if(d->stsc_index<d->mappings.count) {
                TRY(entry(d,&d->mappings,d->stsc_index,&p)); first=be32(p); d->next_chunk=first;
            }
        }
        TRY(entry(d,&d->offsets,d->chunk-1,&p));
        d->chunk_position=d->offsets.width==8 ? be64(p) : be32(p);
    }
    if(d->fixed_size) *bytes=d->fixed_size;
    else { TRY(entry(d,&d->sizes,d->sample,&p)); *bytes=be32(p); }
    if(!*bytes) return MINIAAC_FORMAT;
    if(*bytes>MINIAAC_INPUT_BYTES) return MINIAAC_FRAME_TOO_LARGE;
    offset=d->chunk_position;
    if(offset<d->mdat_start || offset>d->mdat_end || *bytes>d->mdat_end-offset) return MINIAAC_FORMAT;
    TRY(read_bytes(d,offset,d->input,*bytes));
    d->chunk_position+=*bytes; d->sample++; d->in_chunk++;
    return MINIAAC_OK;
}
static miniaac_status next_packet(miniaac *d,uint32_t *bytes) {
    uint32_t length; unsigned header;
    if(!d->adts) return next_m4a_packet(d,bytes);
    if(d->position==d->source.size) return MINIAAC_EOF;
    TRY(adts_header(d,d->position,&length,&header));
    *bytes=length-header; TRY(read_bytes(d,d->position+header,d->input,*bytes));
    d->position+=length; return MINIAAC_OK;
}
size_t miniaac_workspace_size(void) { return sizeof(miniaac); }
miniaac_status miniaac_open(void *workspace,size_t bytes,const miniaac_source *source,miniaac **out) {
    miniaac *d; miniaac_status rc; uint8_t h[8]; uint32_t length; unsigned header;
    AACFrameInfo frame;
    if(!out) return MINIAAC_ARGUMENT;
    *out=NULL;
    if(!workspace || !source || !source->read_at || ((uintptr_t)workspace&7)) return MINIAAC_ARGUMENT;
    if(bytes<sizeof(miniaac)) return MINIAAC_MEMORY;
    d=(miniaac *)workspace; memset(d,0,sizeof(*d)); d->source=*source;
    rc=read_bytes(d,0,h,8); if(rc) return rc;
    d->info.bits_per_sample=16;
    if(h[0]==255 && (h[1]&0xf6)==0xf0) {
        d->adts=1; d->trim_end=UINT64_MAX; d->info.total_frames=UINT64_MAX;
        rc=adts_header(d,0,&length,&header);
    } else rc=parse_m4a(d);
    if(rc) return rc;
    d->codec=AACInitDecoderPre(d->arena,(int)sizeof(d->arena));
    if(!d->codec) return MINIAAC_MEMORY;
    memset(&frame,0,sizeof(frame)); frame.nChans=d->info.channels;
    frame.sampRateCore=(int)d->info.sample_rate; frame.profile=AAC_PROFILE_LC;
    if(AACSetRawBlockParams(d->codec,0,&frame)) return MINIAAC_CODEC;
    d->magic=MAGIC; *out=d; return MINIAAC_OK;
}
miniaac_status miniaac_get_info(const miniaac *d,miniaac_info *info) {
    if(!d || d->magic!=MAGIC || !info) return MINIAAC_ARGUMENT;
    *info=d->info; return MINIAAC_OK;
}
miniaac_status miniaac_decode_next(miniaac *d,int16_t *pcm,size_t capacity,size_t *count) {
    miniaac_status rc; uint32_t bytes; unsigned char *p; int left,err;
    uint64_t begin,end,keep_begin,keep_end; AACFrameInfo info;
    if(!count) return MINIAAC_ARGUMENT;
    *count=0;
    if(!d || d->magic!=MAGIC || !pcm) return MINIAAC_ARGUMENT;
    if(capacity<MINIAAC_PCM_SAMPLES) return MINIAAC_OUTPUT_TOO_SMALL;
    if(d->failed) return d->failed;
    for(;;) {
        if(d->decoded_frames>=d->trim_end) return MINIAAC_EOF;
        rc=next_packet(d,&bytes);
        if(rc!=MINIAAC_OK) { if(rc!=MINIAAC_EOF) d->failed=rc; return rc; }
        p=d->input; left=(int)bytes; err=AACDecode(d->codec,&p,&left,pcm);
        if(err || left) {
            d->failed=err==ERR_AAC_MPEG4_UNSUPPORTED ? MINIAAC_UNSUPPORTED : MINIAAC_CODEC;
            return d->failed;
        }
        AACGetLastFrameInfo(d->codec,&info);
        if(info.nChans!=d->info.channels || info.sampRateOut!=(int)d->info.sample_rate || info.outputSamps!=1024*info.nChans) {
            d->failed=MINIAAC_UNSUPPORTED; return d->failed;
        }
        begin=d->decoded_frames; end=begin+1024; d->decoded_frames=end;
        keep_begin=begin<d->trim_start ? d->trim_start : begin;
        keep_end=end>d->trim_end ? d->trim_end : end;
        if(keep_end<=keep_begin) continue;
        *count=(size_t)(keep_end-keep_begin)*d->info.channels;
        if(keep_begin!=begin) memmove(pcm,pcm+(size_t)(keep_begin-begin)*d->info.channels,*count*sizeof(*pcm));
        return MINIAAC_OK;
    }
}
void miniaac_close(miniaac *d) {
    if(d && d->magic==MAGIC) { d->codec=NULL; d->magic=0; }
}
const char *miniaac_status_string(miniaac_status rc) {
    switch(rc) {
    case MINIAAC_OK:return "ok"; case MINIAAC_EOF:return "end of audio";
    case MINIAAC_ARGUMENT:return "invalid argument/alignment"; case MINIAAC_MEMORY:return "workspace too small";
    case MINIAAC_IO:return "source read failed"; case MINIAAC_FORMAT:return "malformed/truncated file";
    case MINIAAC_UNSUPPORTED:return "unsupported format/profile/layout";
    case MINIAAC_FRAME_TOO_LARGE:return "AAC packet exceeds 2048 bytes";
    case MINIAAC_CODEC:return "AAC decode failed"; case MINIAAC_OUTPUT_TOO_SMALL:return "PCM buffer too small";
    default:return "unknown error";
    }
}
