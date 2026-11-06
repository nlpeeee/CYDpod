// App-owned miniflac reader overlay (0BSD, see vendor/miniflac/miniflac.h).
// Applied only to a staged header by tools/stage_flac.py. Never prefetch past
// the requested byte: frame/header CRC resets depend on exact consumption.
static __attribute__((noinline)) int
miniflac_bitreader_refill(miniflac_bitreader_t* br, uint8_t bits) {
    if (bits == 0) return 0;
    while (br->bits < bits && br->pos < br->len) {
        if (bits - br->bits > 24 && br->bits <= 32 && br->len - br->pos >= 4) {
            const uint8_t* p = br->buffer + br->pos;
            uint32_t word = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
                            (uint32_t)p[2] << 8 | p[3];
            br->val = (br->val << 32) | word;
            br->bits += 32; br->pos += 4; br->tot += 4;
            for (unsigned i = 0; i < 4; ++i) {
                uint8_t byte = p[i];
                br->crc8 = miniflac_crc8_table[br->crc8 ^ byte];
                br->crc16 = miniflac_crc16_table[(br->crc16 >> 8) ^ byte] ^ ((br->crc16 & 255) << 8);
            }
        } else {
            uint8_t byte = br->buffer[br->pos++];
            br->val = (br->val << 8) | byte;
            br->bits += 8; ++br->tot;
            br->crc8 = miniflac_crc8_table[br->crc8 ^ byte];
            br->crc16 = miniflac_crc16_table[(br->crc16 >> 8) ^ byte] ^ ((br->crc16 & 255) << 8);
        }
    }
    return br->bits < bits;
}

// Keep the cached-bit test inline, but share CRC/refill work in flash. There
// is no call for each cached bit (Rice unary codes usually consume one).
static inline __attribute__((always_inline)) int
miniflac_bitreader_fill(miniflac_bitreader_t* br, uint8_t bits) {
    return br->bits >= bits ? 0 : miniflac_bitreader_refill(br, bits);
}

// Wide metadata fields retain the general 64-bit path out of line.
static __attribute__((noinline)) uint64_t
miniflac_bitreader_read_wide(miniflac_bitreader_t* br, uint8_t bits) {
    br->bits -= bits;
    uint64_t r = br->val >> br->bits;
    if (bits < 64) r &= UINT64_MAX >> (64 - bits);
    br->val &= br->bits ? UINT64_MAX >> (64 - br->bits) : 0;
    return r;
}
static inline __attribute__((always_inline)) uint64_t
miniflac_bitreader_read(miniflac_bitreader_t* br, uint8_t bits) {
    if (bits == 0) return 0;
    if (br->bits > 32) return miniflac_bitreader_read_wide(br, bits);
    // Audio/Rice reads fit in one 32-bit word: no Xtensa __lshrdi3.
    br->bits -= bits;
    uint32_t r = (uint32_t)br->val >> br->bits;
    r &= UINT32_MAX >> (32 - bits);
    br->val &= br->bits ? UINT32_MAX >> (32 - br->bits) : 0;
    return r;
}
