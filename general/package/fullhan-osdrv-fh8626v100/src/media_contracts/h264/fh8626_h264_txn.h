#ifndef FH8626_H264_TXN_H
#define FH8626_H264_TXN_H

#include <stdint.h>

enum fh_h264_txn_state {
    FH_H264_TXN_IDLE = 0,
    FH_H264_TXN_REQUESTED,
    FH_H264_TXN_ACCEPTED,
    FH_H264_TXN_APPLIED,
    FH_H264_TXN_OUTPUT_CONFIRMED,
    FH_H264_TXN_FAILED
};

struct fh_h264_txn {
    enum fh_h264_txn_state state;
    uint64_t txn_id;
    uint64_t requested_config_generation;
    uint64_t output_generation_before;
    uint64_t confirmed_output_generation;
    int error;
};

void fh_h264_txn_init(struct fh_h264_txn *t);
int fh_h264_txn_begin(struct fh_h264_txn *t,uint64_t config_generation,uint64_t output_generation,uint64_t *txn_id);
int fh_h264_txn_accept(struct fh_h264_txn *t,uint64_t txn_id);
int fh_h264_txn_applied(struct fh_h264_txn *t,uint64_t txn_id);
int fh_h264_txn_output(struct fh_h264_txn *t,uint64_t txn_id,uint64_t output_generation);
int fh_h264_txn_fail(struct fh_h264_txn *t,uint64_t txn_id,int error);

#endif
