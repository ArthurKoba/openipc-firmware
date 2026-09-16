#include "fh8626_sidecar_publisher.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void)
{
 struct fh8626_sidecar_publisher p={0};
 const unsigned char sps[]={0,0,0,1,0x67,1},pps[]={0,0,0,1,0x68,2},idr[]={0,0,0,1,0x65,3};
 p.initialized=1;p.listen_fd=-1;p.client_fd=123; /* pending-buffer unit only, no fd operations */
 p.max_payload=128;p.pending=calloc(1,160);assert(p.pending);p.waiting_for_idr=1;
 assert(fh8626_sidecar_publisher_publish(&p,idr,sizeof(idr),0,0)==0);
 assert(p.frames_queued==0 && p.waiting_for_idr);
 assert(fh8626_sidecar_publisher_publish_idr(&p,idr,sizeof(idr),7,sps,sizeof(sps),pps,sizeof(pps))==1);
 assert(p.pending_len==32+18 && !p.waiting_for_idr);
 assert(!memcmp(p.pending+32,sps,6) && !memcmp(p.pending+38,pps,6) && !memcmp(p.pending+44,idr,6));
 p.pending_len=0;
 assert(fh8626_sidecar_publisher_publish_idr(&p,idr,6,8,NULL,SIZE_MAX,pps,6)==-EINVAL);
 assert(fh8626_sidecar_publisher_publish_idr(&p,idr,6,8,sps,SIZE_MAX,pps,6)==-EMSGSIZE);
 assert(!p.pending_len);
 assert(fh8626_sidecar_publisher_publish_idr(&p,idr,6,8,NULL,0,NULL,0)==1);
 assert(p.pending_len==38 && !memcmp(p.pending+32,idr,6));
 free(p.pending);puts("IDR current-epoch header prefix / overflow / waiting gate: PASS");return 0;
}
