#ifndef FH8626_AUDIO_FANOUT_H
#define FH8626_AUDIO_FANOUT_H

#include <stddef.h>
#include <stdint.h>

#define FH_AUDIO_FANOUT_SLOTS 8U
#define FH_AUDIO_FRAME_MAX 4096U

struct fh_audio_frame {
    uint64_t epoch;
    uint64_t capture_seq;
    uint64_t pts;
    uint64_t sample_start_total;
    uint32_t sample_count;
    uint32_t codec;
    size_t size;
    uint8_t bytes[FH_AUDIO_FRAME_MAX];
};
struct fh_audio_fanout {
    struct fh_audio_frame slot[FH_AUDIO_FANOUT_SLOTS];
    uint64_t epoch;
    uint64_t next_seq;
    uint64_t samples_total;
};
struct fh_audio_cursor {
    uint64_t epoch;
    uint64_t next_seq;
    uint64_t next_sample_total;
    uint64_t dropped_frames;
    uint64_t samples_lost;
    uint64_t discontinuities;
};

void fh_audio_fanout_init(struct fh_audio_fanout *f,uint64_t epoch);
int fh_audio_fanout_set_epoch(struct fh_audio_fanout *f,uint64_t epoch);
int fh_audio_fanout_publish(struct fh_audio_fanout *f,uint64_t pts,uint32_t samples,uint32_t codec,
                            const void *bytes,size_t size,uint64_t *capture_seq);
void fh_audio_cursor_subscribe(const struct fh_audio_fanout *f,struct fh_audio_cursor *c);
/* 1 frame returned, 0 no frame, <0 error. */
int fh_audio_cursor_read(const struct fh_audio_fanout *f,struct fh_audio_cursor *c,
                         struct fh_audio_frame *out);

#endif
