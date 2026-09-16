#ifndef FH8626_BITRATE_OBSERVER_H
#define FH8626_BITRATE_OBSERVER_H

#include <stdint.h>

struct fh_bitrate_window {
    uint64_t epoch;
    uint64_t start_ms;
    uint64_t last_ms;
    uint64_t applied_config_generation;
    uint64_t output_generation;
    uint64_t applied_target_bps;
    uint64_t producer_bytes;
    uint64_t producer_dropped_bytes;
    uint64_t mux_bytes;
    uint64_t file_bytes;
    uint64_t network_queued_bytes;
    uint64_t network_sent_bytes;
    uint64_t network_failed_bytes;
    uint64_t producer_aus;
    uint64_t dropped_aus;
};

struct fh_bitrate_report {
    uint64_t epoch;
    uint64_t duration_ms;
    uint64_t applied_config_generation;
    uint64_t output_generation;
    uint64_t applied_target_bps;
    uint64_t producer_bps;
    uint64_t producer_drop_bps;
    uint64_t mux_bps;
    uint64_t file_bps;
    uint64_t network_queued_bps;
    uint64_t network_sent_bps;
    uint64_t network_failed_bps;
    uint64_t producer_aus;
    uint64_t dropped_aus;
};

void fh_bitrate_window_init(struct fh_bitrate_window *w,uint64_t epoch,uint64_t start_ms);
int fh_bitrate_set_applied_target(struct fh_bitrate_window *w,uint64_t epoch,uint64_t now_ms,
                                  uint64_t config_generation,uint64_t target_bps);
int fh_bitrate_set_output_generation(struct fh_bitrate_window *w,uint64_t epoch,uint64_t now_ms,
                                     uint64_t output_generation);
int fh_bitrate_record_producer(struct fh_bitrate_window *w,uint64_t epoch,uint64_t now_ms,uint64_t bytes);
int fh_bitrate_record_drop(struct fh_bitrate_window *w,uint64_t epoch,uint64_t now_ms,uint64_t bytes);
int fh_bitrate_record_mux(struct fh_bitrate_window *w,uint64_t epoch,uint64_t now_ms,uint64_t bytes);
int fh_bitrate_record_file(struct fh_bitrate_window *w,uint64_t epoch,uint64_t now_ms,uint64_t bytes);
int fh_bitrate_record_network_queued(struct fh_bitrate_window *w,uint64_t epoch,uint64_t now_ms,uint64_t bytes);
int fh_bitrate_record_network_sent(struct fh_bitrate_window *w,uint64_t epoch,uint64_t now_ms,uint64_t bytes);
int fh_bitrate_record_network_failed(struct fh_bitrate_window *w,uint64_t epoch,uint64_t now_ms,uint64_t bytes);
int fh_bitrate_report(const struct fh_bitrate_window *w,uint64_t epoch,uint64_t end_ms,struct fh_bitrate_report *out);

#endif
