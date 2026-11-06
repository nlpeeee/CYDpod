#ifndef CYDPOD_FLAC_COMPACT_H
#define CYDPOD_FLAC_COMPACT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Default owner policy. 4608 is available ONLY for historical host comparison. */
#ifndef FLAC_COMPACT_MAX_BLOCK
#define FLAC_COMPACT_MAX_BLOCK 4096
#endif
#define FLAC_COMPACT_INPUT_BYTES 2048
#define FLAC_COMPACT_BOOK_BYTES 616
#define FLAC_COMPACT_PCM_SAMPLES (2 * FLAC_COMPACT_MAX_BLOCK)
#define FLAC_COMPACT_STATE_BYTES (2048 + 128 + 64 + FLAC_COMPACT_MAX_BLOCK / 8 + 616)
#define FLAC_COMPACT_TOTAL_BYTES (FLAC_COMPACT_STATE_BYTES + 4 * FLAC_COMPACT_MAX_BLOCK)

enum flac_compact_result {
    FLACX_EOF = 0, FLACX_FRAME = 1,
    FLACX_HEADER = -1, FLACX_METADATA = -2, FLACX_STREAMINFO = -3,
    FLACX_BITS = -4, FLACX_CHANNELS = -5, FLACX_BLOCK_SIZE = -6,
    FLACX_SAMPLE_RATE = -7, FLACX_RATE_CHANGE = -8, FLACX_NO_AUDIO = -9,
    FLACX_READ = -10, FLACX_TRUNCATED = -11, FLACX_CRC = -12,
    FLACX_BAD_FRAME = -13, FLACX_RANGE = -14, FLACX_ARGUMENT = -15
};

/* Read at absolute offset: positive byte count (short reads allowed), 0 EOF,
 * negative I/O failure. Never return more than capacity. No retained pointers. */
typedef int (*flac_compact_read_fn)(void *user, uint64_t offset,
                                   uint8_t *dst, size_t capacity);

typedef struct flac_compact_book {
    uint64_t cursor, first, total, next_sample, frame_sample, frame_number, frame_start;
    uint32_t rate, fixed_block;
    int32_t error;
    uint16_t pos, len, block_size, used, crc16;
    uint8_t byte, bits, crc8, channels, assignment, blocking, drops, skipped;
    uint8_t initialized, published;
} flac_compact_book;

typedef struct flac_compact_state {
    uint8_t input[FLAC_COMPACT_INPUT_BYTES];
    int32_t history[32];
    int16_t coefficients[32];
    uint8_t side[FLAC_COMPACT_MAX_BLOCK / 8];
    /* PLAN-FONIX reserves 616 bytes for reader/seek/CRC bookkeeping. The unused
     * portion is reserved for pool integration, not another sample buffer. */
    union { flac_compact_book v; uint8_t bytes[FLAC_COMPACT_BOOK_BYTES]; } book;
} flac_compact_state;

/* Static assertions on modern firmware/C++, with a strict-C99 equivalent. */
#if defined(__cplusplus)
#define FLACX_ASSERT(c, name) static_assert(c, #name)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define FLACX_ASSERT(c, name) _Static_assert(c, #name)
#else
#define FLACX_ASSERT(c, name) typedef char name[(c) ? 1 : -1]
#endif
FLACX_ASSERT(FLAC_COMPACT_MAX_BLOCK % 64 == 0, flacx_block_alignment);
FLACX_ASSERT(sizeof(flac_compact_book) <= 616, flacx_book_budget);
FLACX_ASSERT(sizeof(flac_compact_state) == FLAC_COMPACT_STATE_BYTES, flacx_state_budget);
FLACX_ASSERT(FLAC_COMPACT_MAX_BLOCK != 4096 || FLAC_COMPACT_TOTAL_BYTES == 19752, flacx_total_budget);
#undef FLACX_ASSERT

/* init walks native FLAC metadata without caching tags; returns FRAME or error.
 * decode_frame strictly decodes ONE frame. On FRAME only, pcm contains block_size
 * interleaved stereo int16 frames; mono is duplicated, as FlacState::read does.
 * pcm must have PCM_SAMPLES entries. On error it is scratch, never publish it.
 * next implements the pool's bounded sync search/drop policy around decode_frame.
 * reset restarts at an absolute frame boundary, preserving STREAMINFO; the pool
 * supplies the next sample for fixed streams whose block size is still unknown.
 * slice copies <=1152 stereo frames, trimming to total; dst may equal pcm. */
int flac_compact_init(flac_compact_state *, flac_compact_read_fn, void *);
void flac_compact_reset(flac_compact_state *, uint64_t offset, uint64_t next_sample);
int flac_compact_decode_frame(flac_compact_state *, flac_compact_read_fn, void *, int16_t *pcm);
int flac_compact_next(flac_compact_state *, flac_compact_read_fn, void *, int16_t *pcm);
unsigned flac_compact_slice(flac_compact_state *, const int16_t *pcm, int16_t *dst, unsigned capacity_frames);
const char *flac_compact_error(int);

/* Replaceable plain-C LPC kernel: history is a 32-entry ring; index is the
 * sample being predicted. Validate extrema before choosing the proved int32
 * accumulator; otherwise use int64. Shift must be 0..15, order 0..32, bps 1..17.
 * Returns FRAME and a floor-shifted prediction, or an error; wide is optional. */
int flac_lpc(const int32_t *history, const int16_t *coefficients, unsigned index,
             unsigned order, unsigned shift, unsigned effective_bps,
             int64_t *prediction, int *wide);
#ifdef __cplusplus
}
#endif
#endif
