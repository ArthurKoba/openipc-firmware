#include "fh8626_mp4_sample.h"

#include <errno.h>
#include <string.h>

static int nal_view(const uint8_t *p, size_t n, size_t *prefix, uint8_t *type)
{
    size_t off;
    if (!p || !prefix || !type)
        return -EINVAL;
    if (n >= 4 && p[0]==0 && p[1]==0 && p[2]==0 && p[3]==1)
        off = 4;
    else if (n >= 3 && p[0]==0 && p[1]==0 && p[2]==1)
        off = 3;
    else
        return -EPROTO;
    if (n <= off)
        return -EPROTO;
    *prefix = off;
    *type = p[off] & 0x1fU;
    return (*type == 0U || *type > 31U) ? -EPROTO : 0;
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}

int fh_mp4_sample_from_au(const struct fh_h264_au_snapshot *au,
                          struct fh_mp4_sample_snapshot *out)
{
    size_t i,total=0; int has_vcl=0,key=0; size_t sps_off=0,sps_len=0,pps_off=0,pps_len=0;
    if(!au||!out||!out->bytes||au->entry_count==0||au->entry_count>FH_H264_MAX_ENTRIES) return -EINVAL;
    for(i=0;i<au->entry_count;i++){
        size_t pre,nlen; uint8_t type; const struct fh_h264_au_span *sp=&au->span[i];
        if(sp->len==0||sp->offset>au->size||sp->len>au->size-sp->offset) return -EPROTO;
        if(nal_view(au->bytes+sp->offset,sp->len,&pre,&type)) return -EPROTO;
        nlen=sp->len-pre;
        if(type>=1U&&type<=5U){
            if(nlen>UINT32_MAX||total>SIZE_MAX-(4+nlen)) return -EOVERFLOW;
            total+=4+nlen;has_vcl=1;if(type==5U)key=1;
        }else if(type==7U && sps_len==0){sps_off=sp->offset+pre;sps_len=nlen;}
        else if(type==8U && pps_len==0){pps_off=sp->offset+pre;pps_len=nlen;}
    }
    if(!has_vcl){out->valid=0;out->size=0;out->key=0;out->sps_offset=sps_off;out->sps_len=sps_len;out->pps_offset=pps_off;out->pps_len=pps_len;out->pts=au->pts;out->source_generation=au->generation;return 0;}
    if(total>out->capacity) return -ENOSPC;
    total=0;
    for(i=0;i<au->entry_count;i++){
        size_t pre,nlen; uint8_t type; const struct fh_h264_au_span *sp=&au->span[i];
        if(nal_view(au->bytes+sp->offset,sp->len,&pre,&type)) return -EPROTO;
        if(type<1U||type>5U) continue;
        nlen=sp->len-pre; be32(out->bytes+total,(uint32_t)nlen); total+=4;
        memcpy(out->bytes+total,au->bytes+sp->offset+pre,nlen); total+=nlen;
    }
    out->size=total;out->pts=au->pts;out->source_generation=au->generation;out->valid=1;out->key=key;
    out->sps_offset=sps_off;out->sps_len=sps_len;out->pps_offset=pps_off;out->pps_len=pps_len;return 1;
}
