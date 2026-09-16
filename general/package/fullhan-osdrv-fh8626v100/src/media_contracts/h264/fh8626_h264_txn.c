#include "fh8626_h264_txn.h"

#include <errno.h>
#include <string.h>

void fh_h264_txn_init(struct fh_h264_txn *t){if(t)memset(t,0,sizeof(*t));}
int fh_h264_txn_begin(struct fh_h264_txn *t,uint64_t config_generation,uint64_t output_generation,uint64_t *txn_id){if(!t||config_generation==0)return -EINVAL;if(t->state!=FH_H264_TXN_IDLE&&t->state!=FH_H264_TXN_OUTPUT_CONFIRMED&&t->state!=FH_H264_TXN_FAILED)return -EBUSY;if(t->txn_id==UINT64_MAX)return -EOVERFLOW;t->txn_id++;t->requested_config_generation=config_generation;t->output_generation_before=output_generation;t->confirmed_output_generation=0;t->error=0;t->state=FH_H264_TXN_REQUESTED;if(txn_id)*txn_id=t->txn_id;return 0;}
static int idok(const struct fh_h264_txn*t,uint64_t id){return t&&id==t->txn_id;}
int fh_h264_txn_accept(struct fh_h264_txn*t,uint64_t id){if(!idok(t,id)||t->state!=FH_H264_TXN_REQUESTED)return -EINVAL;t->state=FH_H264_TXN_ACCEPTED;return 0;}
int fh_h264_txn_applied(struct fh_h264_txn*t,uint64_t id){if(!idok(t,id)||t->state!=FH_H264_TXN_ACCEPTED)return -EINVAL;t->state=FH_H264_TXN_APPLIED;return 0;}
int fh_h264_txn_output(struct fh_h264_txn*t,uint64_t id,uint64_t outgen){if(!idok(t,id)||t->state!=FH_H264_TXN_APPLIED)return -EINVAL;if(outgen<=t->output_generation_before)return -EAGAIN;t->confirmed_output_generation=outgen;t->state=FH_H264_TXN_OUTPUT_CONFIRMED;return 0;}
int fh_h264_txn_fail(struct fh_h264_txn*t,uint64_t id,int error){if(!idok(t,id)||t->state==FH_H264_TXN_IDLE||t->state==FH_H264_TXN_OUTPUT_CONFIRMED||t->state==FH_H264_TXN_FAILED)return -EINVAL;t->error=error?error:-EIO;t->state=FH_H264_TXN_FAILED;return 0;}
