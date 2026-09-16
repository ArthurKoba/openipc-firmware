#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include "../h264/fh8626_h264_txn.h"
int main(void){struct fh_h264_txn t;uint64_t id;fh_h264_txn_init(&t);assert(fh_h264_txn_begin(&t,5,10,&id)==0);assert(t.state==FH_H264_TXN_REQUESTED);assert(fh_h264_txn_applied(&t,id)<0);assert(fh_h264_txn_accept(&t,id)==0);assert(fh_h264_txn_applied(&t,id)==0);assert(fh_h264_txn_output(&t,id,10)==-EAGAIN);assert(fh_h264_txn_output(&t,id,11)==0);assert(fh_h264_txn_begin(&t,6,11,&id)==0);assert(fh_h264_txn_accept(&t,id)==0);assert(fh_h264_txn_fail(&t,id,-EIO)==0&&t.state==FH_H264_TXN_FAILED);puts("test_h264_txn: PASS");return 0;}
