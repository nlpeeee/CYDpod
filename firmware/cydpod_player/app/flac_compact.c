/* Compact FLAC, independently implemented from RFC 9639 and PLAN-FONIX.
 * No Fonix or micro-flac code. No allocations or mutable static storage.
 * See docs/research/flacx-host.md for the pinned comparison and byte budget. */
#if !defined(CYD_UPLOADER) && !defined(CYDPOD_FLAC_LEGACY)
// Match the replaced core's local O2 setting; Arduino's global default is Os.
#if defined(__GNUC__)
#pragma GCC optimize ("O2")
#endif
#include "flac_compact.h"
#include <limits.h>
#include <string.h>

#define B(s) ((s)->book.v)

/* MSB-first CRC tables for polynomials 0x07 and 0x8005. */
static const uint8_t crc8_table[256] = {
    0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15, 0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d,
    0x70, 0x77, 0x7e, 0x79, 0x6c, 0x6b, 0x62, 0x65, 0x48, 0x4f, 0x46, 0x41, 0x54, 0x53, 0x5a, 0x5d,
    0xe0, 0xe7, 0xee, 0xe9, 0xfc, 0xfb, 0xf2, 0xf5, 0xd8, 0xdf, 0xd6, 0xd1, 0xc4, 0xc3, 0xca, 0xcd,
    0x90, 0x97, 0x9e, 0x99, 0x8c, 0x8b, 0x82, 0x85, 0xa8, 0xaf, 0xa6, 0xa1, 0xb4, 0xb3, 0xba, 0xbd,
    0xc7, 0xc0, 0xc9, 0xce, 0xdb, 0xdc, 0xd5, 0xd2, 0xff, 0xf8, 0xf1, 0xf6, 0xe3, 0xe4, 0xed, 0xea,
    0xb7, 0xb0, 0xb9, 0xbe, 0xab, 0xac, 0xa5, 0xa2, 0x8f, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9d, 0x9a,
    0x27, 0x20, 0x29, 0x2e, 0x3b, 0x3c, 0x35, 0x32, 0x1f, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0d, 0x0a,
    0x57, 0x50, 0x59, 0x5e, 0x4b, 0x4c, 0x45, 0x42, 0x6f, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7d, 0x7a,
    0x89, 0x8e, 0x87, 0x80, 0x95, 0x92, 0x9b, 0x9c, 0xb1, 0xb6, 0xbf, 0xb8, 0xad, 0xaa, 0xa3, 0xa4,
    0xf9, 0xfe, 0xf7, 0xf0, 0xe5, 0xe2, 0xeb, 0xec, 0xc1, 0xc6, 0xcf, 0xc8, 0xdd, 0xda, 0xd3, 0xd4,
    0x69, 0x6e, 0x67, 0x60, 0x75, 0x72, 0x7b, 0x7c, 0x51, 0x56, 0x5f, 0x58, 0x4d, 0x4a, 0x43, 0x44,
    0x19, 0x1e, 0x17, 0x10, 0x05, 0x02, 0x0b, 0x0c, 0x21, 0x26, 0x2f, 0x28, 0x3d, 0x3a, 0x33, 0x34,
    0x4e, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5c, 0x5b, 0x76, 0x71, 0x78, 0x7f, 0x6a, 0x6d, 0x64, 0x63,
    0x3e, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2c, 0x2b, 0x06, 0x01, 0x08, 0x0f, 0x1a, 0x1d, 0x14, 0x13,
    0xae, 0xa9, 0xa0, 0xa7, 0xb2, 0xb5, 0xbc, 0xbb, 0x96, 0x91, 0x98, 0x9f, 0x8a, 0x8d, 0x84, 0x83,
    0xde, 0xd9, 0xd0, 0xd7, 0xc2, 0xc5, 0xcc, 0xcb, 0xe6, 0xe1, 0xe8, 0xef, 0xfa, 0xfd, 0xf4, 0xf3,
};
static const uint16_t crc16_table[256] = {
    0x0000, 0x8005, 0x800f, 0x000a, 0x801b, 0x001e, 0x0014, 0x8011,
    0x8033, 0x0036, 0x003c, 0x8039, 0x0028, 0x802d, 0x8027, 0x0022,
    0x8063, 0x0066, 0x006c, 0x8069, 0x0078, 0x807d, 0x8077, 0x0072,
    0x0050, 0x8055, 0x805f, 0x005a, 0x804b, 0x004e, 0x0044, 0x8041,
    0x80c3, 0x00c6, 0x00cc, 0x80c9, 0x00d8, 0x80dd, 0x80d7, 0x00d2,
    0x00f0, 0x80f5, 0x80ff, 0x00fa, 0x80eb, 0x00ee, 0x00e4, 0x80e1,
    0x00a0, 0x80a5, 0x80af, 0x00aa, 0x80bb, 0x00be, 0x00b4, 0x80b1,
    0x8093, 0x0096, 0x009c, 0x8099, 0x0088, 0x808d, 0x8087, 0x0082,
    0x8183, 0x0186, 0x018c, 0x8189, 0x0198, 0x819d, 0x8197, 0x0192,
    0x01b0, 0x81b5, 0x81bf, 0x01ba, 0x81ab, 0x01ae, 0x01a4, 0x81a1,
    0x01e0, 0x81e5, 0x81ef, 0x01ea, 0x81fb, 0x01fe, 0x01f4, 0x81f1,
    0x81d3, 0x01d6, 0x01dc, 0x81d9, 0x01c8, 0x81cd, 0x81c7, 0x01c2,
    0x0140, 0x8145, 0x814f, 0x014a, 0x815b, 0x015e, 0x0154, 0x8151,
    0x8173, 0x0176, 0x017c, 0x8179, 0x0168, 0x816d, 0x8167, 0x0162,
    0x8123, 0x0126, 0x012c, 0x8129, 0x0138, 0x813d, 0x8137, 0x0132,
    0x0110, 0x8115, 0x811f, 0x011a, 0x810b, 0x010e, 0x0104, 0x8101,
    0x8303, 0x0306, 0x030c, 0x8309, 0x0318, 0x831d, 0x8317, 0x0312,
    0x0330, 0x8335, 0x833f, 0x033a, 0x832b, 0x032e, 0x0324, 0x8321,
    0x0360, 0x8365, 0x836f, 0x036a, 0x837b, 0x037e, 0x0374, 0x8371,
    0x8353, 0x0356, 0x035c, 0x8359, 0x0348, 0x834d, 0x8347, 0x0342,
    0x03c0, 0x83c5, 0x83cf, 0x03ca, 0x83db, 0x03de, 0x03d4, 0x83d1,
    0x83f3, 0x03f6, 0x03fc, 0x83f9, 0x03e8, 0x83ed, 0x83e7, 0x03e2,
    0x83a3, 0x03a6, 0x03ac, 0x83a9, 0x03b8, 0x83bd, 0x83b7, 0x03b2,
    0x0390, 0x8395, 0x839f, 0x039a, 0x838b, 0x038e, 0x0384, 0x8381,
    0x0280, 0x8285, 0x828f, 0x028a, 0x829b, 0x029e, 0x0294, 0x8291,
    0x82b3, 0x02b6, 0x02bc, 0x82b9, 0x02a8, 0x82ad, 0x82a7, 0x02a2,
    0x82e3, 0x02e6, 0x02ec, 0x82e9, 0x02f8, 0x82fd, 0x82f7, 0x02f2,
    0x02d0, 0x82d5, 0x82df, 0x02da, 0x82cb, 0x02ce, 0x02c4, 0x82c1,
    0x8243, 0x0246, 0x024c, 0x8249, 0x0258, 0x825d, 0x8257, 0x0252,
    0x0270, 0x8275, 0x827f, 0x027a, 0x826b, 0x026e, 0x0264, 0x8261,
    0x0220, 0x8225, 0x822f, 0x022a, 0x823b, 0x023e, 0x0234, 0x8231,
    0x8213, 0x0216, 0x021c, 0x8219, 0x0208, 0x820d, 0x8207, 0x0202,
};

static int fail(flac_compact_state *s, int error) {
    if (!B(s).error) B(s).error = error;
    return B(s).error;
}
static void position(flac_compact_state *s, uint64_t at) {
    B(s).cursor = at;
    B(s).pos = B(s).len = B(s).bits = 0;
    B(s).error = 0;
}
static int cooperate(flac_compact_state *s) {
    return B(s).cooperate && B(s).cooperate(B(s).cooperate_user) ? fail(s, FLACX_CANCELLED) : 0;
}
void flac_compact_set_cooperate(flac_compact_state *s, flac_compact_cooperate_fn hook, void *user) {
    B(s).cooperate = hook; B(s).cooperate_user = user;
}
static int fill(flac_compact_state *s, flac_compact_read_fn read, void *user) {
    int n;
    if (cooperate(s)) return B(s).error;
    n = read(user, B(s).cursor, B(s).input, B(s).window_capacity);
    if (n < 0 || n > B(s).window_capacity) return fail(s, FLACX_READ);
    B(s).pos = 0;
    B(s).len = (uint16_t)n;
    return n;
}
static uint32_t octet(flac_compact_state *s, flac_compact_read_fn read, void *user) {
    uint32_t v;
    if (B(s).error) return 0;
    if (B(s).pos == B(s).len && fill(s, read, user) <= 0) {
        fail(s, FLACX_TRUNCATED);
        return 0;
    }
    v = B(s).input[B(s).pos++];
    ++B(s).cursor;
    B(s).crc8 = crc8_table[B(s).crc8 ^ v];
    B(s).crc16 = (uint16_t)((B(s).crc16 << 8) ^ crc16_table[(B(s).crc16 >> 8) ^ v]);
    return v;
}
static uint32_t get(flac_compact_state *s, flac_compact_read_fn read, void *user, unsigned n) {
    uint32_t v = 0;
    while (n && !B(s).error) {
        unsigned k;
        if (!B(s).bits) { B(s).byte = (uint8_t)octet(s, read, user); B(s).bits = 8; }
        k = n < B(s).bits ? n : B(s).bits;
        v = (v << k) | ((B(s).byte >> (B(s).bits - k)) & ((1u << k) - 1));
        B(s).bits = (uint8_t)(B(s).bits - k);
        n -= k;
    }
    return v;
}
static int32_t signed_bits(flac_compact_state *s, flac_compact_read_fn read, void *user, unsigned n) {
    uint32_t v = get(s, read, user, n);
    return n ? (int32_t)((int64_t)v - ((v >> (n - 1)) ? (INT64_C(1) << n) : 0)) : 0;
}
static uint32_t unary(flac_compact_state *s, flac_compact_read_fn read, void *user, uint32_t limit) {
    static const uint8_t nibble_width[16] = {0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4};
    uint32_t n = 0;
    while (!B(s).error) {
        if (!B(s).bits) {
            B(s).byte = (uint8_t)octet(s, read, user);
            if (B(s).error) break;
            B(s).bits = 8;
        }
        const unsigned tail = B(s).byte & ((1u << B(s).bits) - 1);
        const unsigned high = tail >> 4;
        const unsigned width = high ? 4u + nibble_width[high] : nibble_width[tail];
        const unsigned zeros = B(s).bits - width;
        if (zeros > limit - n) { fail(s, FLACX_BAD_FRAME); break; }
        n += zeros;
        /* Consume the stop bit when present. Leave lower bits cached for Rice. */
        B(s).bits = width ? (uint8_t)(width - 1) : 0;
        if (width) break;
    }
    return n;
}
static int accepted_rate(uint32_t rate) {
    switch (rate) {
    case 44100: case 48000: case 32000: case 24000: case 22050:
    case 16000: case 12000: case 11025: case 8000: return 1;
    default: return 0;
    }
}
int flac_compact_init(flac_compact_state *s, flac_compact_read_fn read, void *user, uint8_t *window) {
    unsigned blocks;
    int last = 0;
    if (!s || !read || !window) return FLACX_ARGUMENT;
    memset(s, 0, sizeof(*s));
    B(s).input = window; B(s).window_capacity = 64; // metadata needs no audio-sized reads
    if (get(s, read, user, 32) != UINT32_C(0x664c6143)) return fail(s, FLACX_HEADER);
    for (blocks = 0; !last && !B(s).error; ++blocks) {
        unsigned type, n;
        if (blocks == 1024) return fail(s, FLACX_METADATA);
        last = (int)get(s, read, user, 1);
        type = get(s, read, user, 7);
        n = get(s, read, user, 24);
        if (type == 127) return fail(s, FLACX_METADATA);
        if (!blocks) {
            unsigned minb, maxb, bps, i;
            if (type || n != 34) return fail(s, FLACX_STREAMINFO);
            minb = get(s, read, user, 16); maxb = get(s, read, user, 16);
            get(s, read, user, 24); get(s, read, user, 24);
            B(s).rate = get(s, read, user, 20);
            B(s).channels = (uint8_t)(get(s, read, user, 3) + 1);
            bps = get(s, read, user, 5) + 1;
            B(s).total = (uint64_t)get(s, read, user, 4) << 32;
            B(s).total |= get(s, read, user, 32);
            for (i = 0; i < 16; ++i) get(s, read, user, 8);
            if (B(s).error) break;
            if (!B(s).rate || minb < 16 || maxb < minb) return fail(s, FLACX_STREAMINFO);
            if (bps != 16) return fail(s, FLACX_BITS);
            if (B(s).channels > 2) return fail(s, FLACX_CHANNELS);
            if (maxb > FLAC_COMPACT_MAX_BLOCK) { B(s).rejected_block = maxb; return fail(s, FLACX_BLOCK_SIZE); }
            if (!accepted_rate(B(s).rate)) return fail(s, FLACX_SAMPLE_RATE);
            B(s).fixed_block = minb == maxb ? maxb : 0;
        } else {
            uint64_t end = B(s).cursor + n;
            if (!type) return fail(s, FLACX_STREAMINFO);
            if (type == 3 && !B(s).seek_table) {
                B(s).seek_table = B(s).cursor; B(s).seek_bytes = n / 18 * 18;
            }
            /* Validate the skipped metadata extent, reading only its final byte. */
            if (n) { uint8_t byte; if (read(user, end - 1, &byte, 1) != 1) fail(s, FLACX_TRUNCATED); }
            if (!B(s).error) position(s, end);
        }
    }
    if (B(s).error) return B(s).error;
    B(s).first = B(s).cursor;
    if (B(s).pos == B(s).len && fill(s, read, user) <= 0) return fail(s, FLACX_NO_AUDIO);
    B(s).window_capacity = FLAC_COMPACT_INPUT_BYTES;
    B(s).initialized = 1;
    return FLACX_FRAME;
}
void flac_compact_reset(flac_compact_state *s, uint64_t offset, uint64_t next_sample) {
    if (!s) return;
    position(s, offset);
    B(s).next_sample = next_sample;
    B(s).block_size = B(s).used = 0;
    B(s).published = B(s).drops = B(s).skipped = 0;
}

static int lpc_narrow(const int16_t *c, unsigned order, unsigned bps) {
    uint32_t sumabs = 0;
    unsigned j;
    for (j = 0; j < order; ++j) {
        int32_t coefficient = c[j];
        sumabs += coefficient < 0 ? -coefficient : coefficient;
    }
    /* Each tap is bounded by abs(coefficient) * 2^(bps-1). */
    return sumabs <= (UINT32_C(0x7fffffff) >> (bps - 1));
}

static inline int64_t lpc_predict(const int32_t *h, const int16_t *c, unsigned index,
                                  unsigned order, unsigned shift, int narrow) {
    unsigned j;
    /* Shift unsigned magnitudes to round toward negative infinity portably. */
    if (narrow) {
        int32_t acc = 0;
        for (j = 0; j < order; ++j) acc += (int32_t)c[j] * h[(index - j - 1) & 31];
        return acc >= 0 ? (int32_t)((uint32_t)acc >> shift)
                        : -1 - (int32_t)((uint32_t)(-1 - acc) >> shift);
    }
    int64_t acc = 0;
    for (j = 0; j < order; ++j) acc += (int64_t)c[j] * h[(index - j - 1) & 31];
    return acc >= 0 ? (int64_t)((uint64_t)acc >> shift)
                    : -1 - (int64_t)((uint64_t)(-1 - acc) >> shift);
}

int flac_lpc(const int32_t *h, const int16_t *c, unsigned index,
             unsigned order, unsigned shift, unsigned bps, int64_t *prediction, int *wide) {
    unsigned j;
    if (!h || !c || !prediction || order > 32 || shift > 15 || !bps || bps > 17) return FLACX_ARGUMENT;
    const int32_t extent = INT32_C(1) << (bps - 1);
    for (j = 0; j < order; ++j) {
        int32_t v = h[(index - j - 1) & 31];
        if (v < -extent || v >= extent) return FLACX_RANGE;
    }
    const int narrow = lpc_narrow(c, order, bps);
    *prediction = lpc_predict(h, c, index, order, shift, narrow);
    if (wide) *wide = !narrow;
    return FLACX_FRAME;
}

static int save(flac_compact_state *s, int16_t *pcm, unsigned i, unsigned channel,
                int64_t raw, unsigned wasted, unsigned width) {
    int64_t extent = INT64_C(1) << (width - wasted - 1), value;
    uint32_t low;
    if (i >= B(s).block_size || channel >= B(s).channels) return fail(s, FLACX_BAD_FRAME);
    if (raw < -extent || raw >= extent) return fail(s, FLACX_RANGE);
    s->history[i & 31] = (int32_t)raw;
    value = raw * (INT64_C(1) << wasted);
    low = (uint32_t)value & 65535;
    pcm[2 * i + channel] = (int16_t)(low < 32768 ? (int32_t)low : (int32_t)low - 65536);
    if (width == 17) {
        unsigned mask = 1u << (i & 7);
        s->side[i >> 3] = (uint8_t)((s->side[i >> 3] & ~mask) | (((uint32_t)value >> 16 & 1) << (i & 7)));
    }
    return FLACX_FRAME;
}
static int32_t load(const flac_compact_state *s, const int16_t *pcm, unsigned i, unsigned c, unsigned width) {
    uint32_t v;
    if (width == 16) return pcm[2 * i + c];
    v = (uint16_t)pcm[2 * i + c] | ((uint32_t)(s->side[i >> 3] >> (i & 7) & 1) << 16);
    return v & 65536 ? (int32_t)v - 131072 : (int32_t)v;
}
static int subframe(flac_compact_state *s, flac_compact_read_fn read, void *user,
                    int16_t *pcm, unsigned channel, unsigned width) {
    static const int16_t fixed[5][4] = {{0}, {1}, {2,-1}, {3,-3,1}, {4,-6,4,-1}};
    unsigned type, wasted, effective, order, shift = 0, i, method, po, parts, span, p;
    if (get(s, read, user, 1)) return fail(s, FLACX_BAD_FRAME);
    type = get(s, read, user, 6);
    wasted = get(s, read, user, 1) ? unary(s, read, user, width) + 1 : 0;
    if (B(s).error) return B(s).error;
    if (wasted >= width) return fail(s, FLACX_BAD_FRAME);
    effective = width - wasted;
    if (type <= 1) {
        int32_t v = type ? 0 : signed_bits(s, read, user, effective);
        for (i = 0; i < B(s).block_size && !B(s).error; ++i) {
            if (type) v = signed_bits(s, read, user, effective);
            save(s, pcm, i, channel, v, wasted, width);
        }
        return B(s).error;
    }
    if (type >= 8 && type <= 12) {
        order = type - 8;
        memcpy(s->coefficients, fixed[order], order * sizeof(int16_t));
    } else if (type >= 32) order = type - 31;
    else return fail(s, FLACX_BAD_FRAME);
    if (order > B(s).block_size) return fail(s, FLACX_BAD_FRAME);
    for (i = 0; i < order && !B(s).error; ++i)
        save(s, pcm, i, channel, signed_bits(s, read, user, effective), wasted, width);
    if (type >= 32) {
        unsigned precision = get(s, read, user, 4) + 1;
        int sh = signed_bits(s, read, user, 5);
        if (precision == 16 || sh < 0) return fail(s, FLACX_BAD_FRAME);
        shift = (unsigned)sh;
        for (i = 0; i < order; ++i) s->coefficients[i] = (int16_t)signed_bits(s, read, user, precision);
    }
    method = get(s, read, user, 2); po = get(s, read, user, 4);
    if (B(s).error) return B(s).error;
    if (method > 1 || po > 8) return fail(s, FLACX_BAD_FRAME);
    parts = 1u << po; span = B(s).block_size / parts;
    /* Equality intentionally accepts the encoder-oracle empty-first-partition
     * extension; current miniflac rejects it. Never subtract before checking. */
    if (B(s).block_size % parts || span < order) return fail(s, FLACX_BAD_FRAME);
    /* save() validates every warmup and reconstructed sample before it enters history. */
    const int narrow = lpc_narrow(s->coefficients, order, effective);
    i = order;
    for (p = 0; p < parts && !B(s).error; ++p) {
        unsigned k = get(s, read, user, method ? 5 : 4);
        unsigned count = span - (p ? 0 : order), raw = 0;
        int escape = k == (method ? 31u : 15u);
        if (escape) raw = get(s, read, user, 5);
        while (count-- && !B(s).error) {
            int64_t residual, prediction;
            if (escape) residual = signed_bits(s, read, user, raw);
            else {
                uint64_t u = (uint64_t)unary(s, read, user, UINT32_C(0xfffffffe) >> k) << k;
                u |= get(s, read, user, k);
                if (u > UINT32_C(0xfffffffe)) return fail(s, FLACX_BAD_FRAME);
                residual = u & 1 ? -(int64_t)((u >> 1) + 1) : (int64_t)(u >> 1);
            }
            if (B(s).error) break;
            prediction = lpc_predict(s->history, s->coefficients, i, order, shift, narrow);
            /* Addition stays wide even when the LPC accumulator is int32. */
            save(s, pcm, i++, channel, residual + prediction, wasted, width);
        }
    }
    if (!B(s).error && i != B(s).block_size) return fail(s, FLACX_BAD_FRAME);
    return B(s).error;
}

static int header(flac_compact_state *s, flac_compact_read_fn read, void *user) {
    static const uint32_t rates[12] = {0,88200,176400,192000,8000,16000,22050,24000,32000,44100,48000,96000};
    unsigned bc, rc, bps, u, count, i, n;
    uint32_t rate;
    uint64_t number;
    if (get(s, read, user, 14) != 0x3ffe || get(s, read, user, 1)) return fail(s, FLACX_BAD_FRAME);
    B(s).blocking = (uint8_t)get(s, read, user, 1);
    bc = get(s, read, user, 4); rc = get(s, read, user, 4);
    B(s).assignment = (uint8_t)get(s, read, user, 4);
    bps = get(s, read, user, 3);
    if (get(s, read, user, 1) || !bc || rc == 15 || bps == 3 || bps == 7) return fail(s, FLACX_BAD_FRAME);
    u = get(s, read, user, 8); count = 0; number = u;
    if (u & 128) {
        unsigned mask;
        for (mask = 128; u & mask; mask >>= 1) ++count;
        if (count < 2 || count > 7) return fail(s, FLACX_BAD_FRAME);
        number = u & ((1u << (7 - count)) - 1);
        for (i = 1; i < count; ++i) {
            u = get(s, read, user, 8);
            if ((u & 192) != 128) return fail(s, FLACX_BAD_FRAME);
            number = (number << 6) | (u & 63);
        }
        if (number < (UINT64_C(1) << (count == 2 ? 7 : 5 * count - 4))) return fail(s, FLACX_BAD_FRAME);
    }
    if (number >= (UINT64_C(1) << (B(s).blocking ? 36 : 31))) return fail(s, FLACX_BAD_FRAME);
    n = bc == 1 ? 192 : bc <= 5 ? 576u << (bc - 2) :
        bc == 6 ? get(s, read, user, 8) + 1 : bc == 7 ? get(s, read, user, 16) + 1 : 256u << (bc - 8);
    rate = rc == 0 ? B(s).rate : rc < 12 ? rates[rc] :
        rc == 12 ? get(s, read, user, 8) * 1000 : get(s, read, user, 16) * (rc == 14 ? 10 : 1);
    get(s, read, user, 8);
    if (B(s).error) return B(s).error;
    if (B(s).crc8) return fail(s, FLACX_CRC);
    if (n > FLAC_COMPACT_MAX_BLOCK) { B(s).rejected_block = n; return fail(s, FLACX_BLOCK_SIZE); }
    if (bps && bps != 4) return fail(s, FLACX_BITS);
    if (B(s).assignment > 10 || (B(s).assignment > 1 && B(s).assignment < 8) ||
        (B(s).assignment == 0 ? 1 : 2) != B(s).channels) return fail(s, FLACX_CHANNELS);
    if (rate != B(s).rate) return fail(s, FLACX_RATE_CHANGE);
    B(s).block_size = (uint16_t)n;
    B(s).frame_number = number;
    if (!B(s).blocking && !B(s).fixed_block) B(s).fixed_block = n;
    B(s).frame_sample = B(s).blocking ? number : number * B(s).fixed_block;
    return FLACX_FRAME;
}
int flac_compact_header_at(flac_compact_state *s, flac_compact_read_fn read, void *user, uint64_t offset) {
    if (!s || !read || !B(s).initialized) return FLACX_ARGUMENT;
    /* A header is <=16 bytes. Do not overwrite the caller's sync scan window
     * or read an entire input window at every binary-search probe. */
    uint8_t window[16], *saved = B(s).input;
    uint16_t capacity = B(s).window_capacity;
    int r;
    B(s).input = window; B(s).window_capacity = sizeof(window);
    position(s, offset); B(s).crc8 = 0; B(s).crc16 = 0;
    r = header(s, read, user);
    B(s).input = saved; B(s).window_capacity = capacity;
    B(s).pos = B(s).len = B(s).bits = 0;
    return r;
}
int flac_compact_decode_frame(flac_compact_state *s, flac_compact_read_fn read, void *user, int16_t *pcm) {
    unsigned i, w0, w1;
    int result;
    if (!s || !read || !pcm || !B(s).initialized) return FLACX_ARGUMENT;
    B(s).published = 0; B(s).block_size = B(s).used = 0; B(s).error = 0;
    if (B(s).bits) return fail(s, FLACX_BAD_FRAME);
    if (B(s).total && B(s).next_sample >= B(s).total) return FLACX_EOF;
    if (B(s).pos == B(s).len) {
        int n = fill(s, read, user);
        if (n <= 0) return n < 0 ? B(s).error : FLACX_EOF;
    }
    B(s).frame_start = B(s).cursor;
    B(s).crc8 = 0; B(s).crc16 = 0;
    result = header(s, read, user);
    if (result < 0) return result;
    w0 = 16 + (B(s).assignment == 9);
    w1 = 16 + (B(s).assignment == 8 || B(s).assignment == 10);
    subframe(s, read, user, pcm, 0, w0);
    if (!B(s).error) cooperate(s);
    if (!B(s).error && B(s).channels == 2) subframe(s, read, user, pcm, 1, w1);
    if (B(s).bits && get(s, read, user, B(s).bits)) fail(s, FLACX_BAD_FRAME);
    get(s, read, user, 16);
    if (B(s).error) return B(s).error;
    if (B(s).crc16) return fail(s, FLACX_CRC);
    for (i = 0; i < B(s).block_size; ++i) {
        int32_t a = load(s, pcm, i, 0, w0), b = B(s).channels == 2 ? load(s, pcm, i, 1, w1) : a;
        int32_t l = a, r = b;
        if (B(s).assignment == 8) r = a - b;
        else if (B(s).assignment == 9) l = a + b;
        else if (B(s).assignment == 10) {
            int32_t mid = 2 * a + (int32_t)((uint32_t)b & 1);
            l = (mid + b) / 2; r = (mid - b) / 2;
        }
        if (l < -32768 || l > 32767 || r < -32768 || r > 32767) return fail(s, FLACX_RANGE);
        pcm[2 * i] = (int16_t)l; pcm[2 * i + 1] = (int16_t)r;
    }
    B(s).next_sample = B(s).frame_sample + B(s).block_size;
    B(s).published = 1;
    return FLACX_FRAME;
}
int flac_compact_resync(flac_compact_state *s, flac_compact_read_fn read, void *user, uint64_t start) {
    unsigned scanned;
    int previous = -1;
    position(s, start);
    if (cooperate(s)) return B(s).error;
    for (scanned = 0; scanned < 64u * 1024; ++scanned) {
        int c;
        if (B(s).pos == B(s).len) {
            int n = fill(s, read, user);
            if (n <= 0) return n < 0 ? B(s).error : FLACX_EOF;
        }
        c = B(s).input[B(s).pos++]; ++B(s).cursor;
        if (previous == 255 && (c & 254) == 248) {
            position(s, B(s).cursor - 2);
            return FLACX_FRAME;
        }
        previous = c;
    }
    return fail(s, FLACX_BAD_FRAME);
}
int flac_compact_next(flac_compact_state *s, flac_compact_read_fn read, void *user, int16_t *pcm) {
    int recovering = 0;
    if (!s || !read || !pcm || !B(s).initialized) return FLACX_ARGUMENT;
    B(s).skipped = 0;
    for (;;) {
        uint64_t start = B(s).cursor;
        int r;
        if (recovering && cooperate(s)) return B(s).error;
        r = flac_compact_decode_frame(s, read, user, pcm);
        if (r >= 0) { if (r == FLACX_FRAME) B(s).skipped = 0; return r; }
        if (r == FLACX_CANCELLED || r == FLACX_TRUNCATED || r == FLACX_READ || r == FLACX_ARGUMENT) return r;
        if (!recovering && (r == FLACX_BLOCK_SIZE || r == FLACX_BITS || r == FLACX_CHANNELS || r == FLACX_RATE_CHANGE)) return r;
        if (++B(s).skipped > 8 || ++B(s).drops > 64) return r;
        r = flac_compact_resync(s, read, user, start + 1);
        if (r <= 0) return r;
        recovering = 1;
    }
}
unsigned flac_compact_slice(flac_compact_state *s, const int16_t *pcm, int16_t *dst, unsigned capacity) {
    uint64_t at;
    unsigned n;
    if (!s || !pcm || !dst || !B(s).published || B(s).used >= B(s).block_size) return 0;
    n = B(s).block_size - B(s).used;
    if (n > 1152) n = 1152;
    if (n > capacity) n = capacity;
    at = B(s).frame_sample + B(s).used;
    if (B(s).total) {
        if (at >= B(s).total) return 0;
        if (n > B(s).total - at) n = (unsigned)(B(s).total - at);
    }
    memmove(dst, pcm + 2 * B(s).used, n * 2 * sizeof(int16_t));
    B(s).used = (uint16_t)(B(s).used + n);
    return n;
}
const char *flac_compact_error(int r) {
    switch (r) {
    case FLACX_FRAME: return "ok"; case FLACX_EOF: return "eof";
    case FLACX_HEADER: return "flac_header"; case FLACX_METADATA: return "flac_metadata";
    case FLACX_STREAMINFO: return "flac_streaminfo"; case FLACX_BITS: return "flac_bits";
    case FLACX_CHANNELS: return "flac_channels"; case FLACX_BLOCK_SIZE: return "flac_block_size";
    case FLACX_SAMPLE_RATE: return "flac_sample_rate"; case FLACX_RATE_CHANGE: return "flac_rate_change";
    case FLACX_NO_AUDIO: return "flac_no_audio"; case FLACX_READ: return "flac_read";
    case FLACX_TRUNCATED: return "flac_truncated"; case FLACX_CRC: return "flac_crc";
    case FLACX_CANCELLED: return "flac_cancelled";
    case FLACX_ARGUMENT: return "flac_argument"; default: return "flac_frame";
    }
}

#endif
