#include "../firmware/cydpod_player/app/flac_compact.h"
#include "../firmware/cydpod_player/app/flac_pool.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

static void require(bool ok, const char *why) {
    if (!ok) { fprintf(stderr, "FAIL %s\n", why); std::exit(1); }
}
struct Source {
    FILE *file;
    unsigned split;
    uint64_t calls = 0, bytes = 0;
};
static int read_at(void *opaque, uint64_t at, uint8_t *dst, size_t n) {
    auto &s = *static_cast<Source *>(opaque);
    if (s.split && n > s.split) n = s.split;
    if (fseeko(s.file, static_cast<off_t>(at), SEEK_SET)) return -1;
    ++s.calls;
    size_t got = fread(dst, 1, n, s.file);
    s.bytes += got;
    return ferror(s.file) ? -1 : static_cast<int>(got);
}
static int memory_read(void *opaque, uint64_t at, uint8_t *dst, size_t n) {
    auto &v = *static_cast<std::vector<uint8_t> *>(opaque);
    if (at >= v.size()) return 0;
    n = std::min<size_t>(n, v.size() - static_cast<size_t>(at));
    memcpy(dst, v.data() + at, n);
    return static_cast<int>(n);
}
static std::vector<uint8_t> slurp(const char *path) {
    FILE *f = fopen(path, "rb"); require(f, "fixture open");
    std::vector<uint8_t> out;
    uint8_t bytes[4096]; size_t n;
    while ((n = fread(bytes, 1, sizeof bytes, f))) out.insert(out.end(), bytes, bytes + n);
    fclose(f); return out;
}
struct Guarded {
    uint64_t before = UINT64_C(0x123456789abcdef0);
    flac_compact_state state{};
    uint64_t between = UINT64_C(0x123456789abcdef0);
    std::array<int16_t, FLAC_COMPACT_PCM_SAMPLES> pcm{};
    uint64_t after = UINT64_C(0x123456789abcdef0);
    void check() const {
        require(before == UINT64_C(0x123456789abcdef0) && between == before && after == before, "state/PCM guard");
    }
};
static void kernel() {
    uint32_t seed = 17;
    auto rng = [&]() { seed = 1664525u * seed + 1013904223u; return seed; };
    unsigned fast = 0, wide = 0;
    for (unsigned bits : {16u, 17u}) for (unsigned precision : {12u, 13u, 14u, 15u})
    for (unsigned order = 1; order <= 32; ++order) for (unsigned it = 0; it < 128; ++it) {
        int32_t h[32] = {}; int16_t c[32] = {}; int64_t ref = 0;
        for (unsigned j = 0; j < order; ++j) {
            int lim = 1 << (precision - 1), sm = 1 << (bits - 1);
            c[j] = static_cast<int16_t>(it == 0 ? -lim : static_cast<int>(rng() % (2 * lim)) - lim);
            int32_t sample = it == 0 ? -sm : static_cast<int>(rng() % (2 * sm)) - sm;
            h[(32 - j - 1) & 31] = sample;
            ref += int64_t(c[j]) * sample;
        }
        for (unsigned shift = 0; shift < 16; ++shift) {
            int64_t actual = 0; int used_wide = -1;
            require(flac_lpc(h, c, 32, order, shift, bits, &actual, &used_wide) == FLACX_FRAME, "LPC call");
            int64_t wanted = ref >= 0 ? ref / (INT64_C(1) << shift) : -1 - ((-1 - ref) / (INT64_C(1) << shift));
            require(actual == wanted, "LPC bit exact");
            if (!shift) used_wide ? ++wide : ++fast;
        }
    }
    int32_t h[32] = {}; int16_t c[32] = {}; int64_t out;
    h[31] = -65537;
    require(flac_lpc(h,c,32,1,0,17,&out,nullptr) == FLACX_RANGE, "LPC invalid extrema");
    require(flac_lpc(h,c,32,33,0,17,&out,nullptr) == FLACX_ARGUMENT, "LPC invalid order");
    printf("PASS LPC 32768 vectors x16 shifts incl extrema/side17 fast=%u wide=%u\n", fast, wide);
}
static int compact(int argc, char **argv) {
    require(argc >= 4, "compact args");
    Source src{fopen(argv[2], "rb"), argc > 4 ? static_cast<unsigned>(atoi(argv[4])) : 0};
    require(src.file, "input open");
    FILE *out = fopen(argv[3], "wb"); require(out, "output open");
    Guarded g; auto &s = g.state; auto &b = s.book.v;
    int r = flac_compact_init(&s, read_at, &src);
    uint64_t frames = 0, samples = 0;
    double us = 0;
    bool recovering = argc > 5 && !strcmp(argv[5], "recover");
    while (r == FLACX_FRAME) {
        auto start = std::chrono::steady_clock::now();
        r = recovering ? flac_compact_next(&s, read_at, &src, g.pcm.data()) :
                         flac_compact_decode_frame(&s, read_at, &src, g.pcm.data());
        us += std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now() - start).count();
        g.check();
        if (r != FLACX_FRAME) { require(!b.published, "error PCM gate"); break; }
        require(b.published, "successful PCM gate");
        ++frames;
        int16_t slice[2304]; unsigned n;
        while ((n = flac_compact_slice(&s, g.pcm.data(), slice, 1152))) {
            for (unsigned i = 0; i < n; ++i) {
                require(fwrite(slice + 2 * i, 2, b.channels, out) == b.channels, "output write");
                if (b.channels == 1) require(slice[2*i] == slice[2*i+1], "mono duplication");
            }
            samples += n;
        }
    }
    fclose(src.file); fclose(out);
    if (r < 0) { printf("REJECT %s code=%d frames=%llu published=%u\n", flac_compact_error(r), r, (unsigned long long)frames, b.published); return 3; }
    printf("PASS compact frames=%llu samples=%llu state=%zu total=%u frame_us=%.2f drops=%u reads=%llu\n",
           (unsigned long long)frames,(unsigned long long)samples,sizeof s,FLAC_COMPACT_TOTAL_BYTES,
           frames ? us / frames : 0,b.drops,(unsigned long long)src.calls);
    return 0;
}
static int reference(int argc, char **argv) {
    require(argc == 4, "reference args");
    FlacState s{}; int32_t right[FlacState::maxBlock + FlacState::slack]; uint8_t input[2048];
    s.right = right;
    File file(argv[2], "r"); require(bool(file), "reference open");
    FILE *out = fopen(argv[3], "wb"); require(out, "reference output");
    const char *error = s.open(file, input);
    if (error) { printf("REJECT %s\n", error); fclose(out); return 3; }
    uint64_t frames = 0, samples = 0; double us = 0;
    for (;;) {
        auto start = std::chrono::steady_clock::now();
        int r = s.frame(file, input, error);
        us += std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
        if (r <= 0) {
            if (r < 0) { printf("REJECT %s\n",error); fclose(out); return 3; }
            break;
        }
        ++frames;
        unsigned n = s.blockSize;
        if (s.total && s.curStart + n > s.total) n = static_cast<unsigned>(s.total - s.curStart);
        for (unsigned i=0;i<n;++i) {
            int16_t v[2] = {s.clip(s.block[i]),s.channels==2?s.clip(right[i]):s.clip(s.block[i])};
            require(fwrite(v,2,s.channels,out)==s.channels,"reference write");
        }
        samples += n;
    }
    fclose(out);
    printf("PASS reference frames=%llu samples=%llu frame_us=%.2f\n",(unsigned long long)frames,(unsigned long long)samples,frames?us/frames:0);
    return 0;
}
static void restart(const char *path) {
    auto data = slurp(path);
    Guarded a, b;
    require(flac_compact_init(&a.state,memory_read,&data)==FLACX_FRAME,"restart init");
    require(flac_compact_decode_frame(&a.state,memory_read,&data,a.pcm.data())==FLACX_FRAME,"restart first");
    uint64_t at = a.state.book.v.cursor, sample = a.state.book.v.next_sample;
    require(flac_compact_decode_frame(&a.state,memory_read,&data,a.pcm.data())==FLACX_FRAME,"restart second");
    require(flac_compact_init(&b.state,memory_read,&data)==FLACX_FRAME,"restart second init");
    flac_compact_reset(&b.state,at,sample);
    require(flac_compact_decode_frame(&b.state,memory_read,&data,b.pcm.data())==FLACX_FRAME,"restart reset");
    require(a.state.book.v.frame_sample == b.state.book.v.frame_sample &&
            a.state.book.v.block_size == b.state.book.v.block_size && a.pcm==b.pcm,"restart PCM");
    // In-place 1152-frame publication must leave unconsumed source intact.
    auto original = b.pcm; unsigned used=0,n;
    while ((n=flac_compact_slice(&b.state,b.pcm.data(),b.pcm.data(),1152))) {
        require(!memcmp(b.pcm.data(),original.data()+2*used,n*4),"overlapping slices"); used+=n;
    }
    require(used == b.state.book.v.block_size,"slice full block");
    a.check(); b.check(); puts("PASS frame-boundary reset + overlapping 1152-frame slices");
}
static void hostile(const char *path) {
    auto original=slurp(path); uint32_t seed=12345;
    auto rng=[&](){seed=seed*1103515245u+12345;return seed>>8;};
    for(unsigned v=0;v<160;++v) {
        auto data=original;
        if(v%3==0) data.resize(42+rng()%(data.size()-42));
        else for(unsigned k=0;k<1+v/8;++k) data[8+rng()%(data.size()-8)]^=1u<<(rng()%8);
        Guarded g; int r=flac_compact_init(&g.state,memory_read,&data);
        unsigned frames=0;
        while(r==FLACX_FRAME) {
            r=flac_compact_next(&g.state,memory_read,&data,g.pcm.data());
            require(++frames<1000,"hostile termination");
            g.check();
        }
        require(!g.state.book.v.published,"hostile CRC publication");
    }
    puts("PASS 160 hostile variants terminate with guarded state/PCM");
}
int main(int argc,char **argv) {
    if(argc<2)return 2;
    if(!strcmp(argv[1],"kernel")){kernel();return 0;}
    if(!strcmp(argv[1],"compact"))return compact(argc,argv);
    if(!strcmp(argv[1],"reference"))return reference(argc,argv);
    if(argc==3&&!strcmp(argv[1],"restart")){restart(argv[2]);return 0;}
    if(argc==3&&!strcmp(argv[1],"hostile")){hostile(argv[2]);return 0;}
    return 2;
}
