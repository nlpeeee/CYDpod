#ifndef MINIAAC_H
#define MINIAAC_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define MINIAAC_ALIGNMENT 8
#define MINIAAC_PCM_SAMPLES 2048 /* int16_t values, NOT stereo sample frames */
#define MINIAAC_INPUT_BYTES 2048
#define MINIAAC_WORKSPACE_BYTES (22 * 1024) /* convenient fixed arena; query size for exact allocation */

typedef struct miniaac miniaac;
typedef enum {
    MINIAAC_OK = 0, MINIAAC_EOF = 1,
    MINIAAC_ARGUMENT = -1, MINIAAC_MEMORY = -2, MINIAAC_IO = -3,
    MINIAAC_FORMAT = -4, MINIAAC_UNSUPPORTED = -5,
    MINIAAC_FRAME_TOO_LARGE = -6, MINIAAC_CODEC = -7,
    MINIAAC_OUTPUT_TOO_SMALL = -8
} miniaac_status;

/* Return 0 only when ALL requested bytes were read; otherwise nonzero.
 * Offsets are absolute; the callback may seek. Source remains caller-owned. */
typedef int (*miniaac_read_at)(void *user, uint64_t offset, void *dst, size_t bytes);
typedef struct {
    miniaac_read_at read_at;
    void *user;
    uint64_t size;
} miniaac_source;
typedef struct {
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
    uint64_t total_frames; /* samples PER CHANNEL after edit; UINT64_MAX for ADTS */
} miniaac_info;

size_t miniaac_workspace_size(void); /* excludes caller's 4096-byte PCM buffer */
/* workspace must be 8-byte aligned and remain alive through close.
 * No malloc/free calls, no tasks, and no source ownership transfer.
 * *out is NULL on failure; failed opens can be retried with the same workspace. */
miniaac_status miniaac_open(void *workspace, size_t bytes,
                           const miniaac_source *source, miniaac **out);
miniaac_status miniaac_get_info(const miniaac *decoder, miniaac_info *info);
/* Capacity/count are int16_t values; stereo frames = count / channels.
 * Always provide MINIAAC_PCM_SAMPLES capacity (even for mono).
 * Decode into your own buffer and drain it before calling again.
 * Too-small output does not advance playback. Other decode errors are terminal.
 * Single caller per instance; never run this from an ISR/Bluetooth callback. */
miniaac_status miniaac_decode_next(miniaac *decoder, int16_t *pcm,
                                  size_t capacity, size_t *count);
void miniaac_close(miniaac *decoder); /* idempotent; does not free workspace/source */
const char *miniaac_status_string(miniaac_status status);

#ifdef __cplusplus
}
#endif
#endif
