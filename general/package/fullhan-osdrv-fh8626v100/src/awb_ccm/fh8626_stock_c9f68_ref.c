#include "fh8626_stock_c9f68_ref.h"
#include <string.h>

static uint16_t get_u16(const uint8_t *p){uint16_t v;memcpy(&v,p,2);return v;}
static uint32_t get_u32(const uint8_t *p){uint32_t v;memcpy(&v,p,4);return v;}
static void put_u16(uint8_t *p,uint16_t v){memcpy(p,&v,2);}
static int32_t signed16(uint32_t v)
{
    v&=0xffffu;
    return v<0x8000u?(int32_t)v:(int32_t)v-65536;
}
static int32_t signed32(uint32_t v)
{
    return v<=0x7fffffffu?(int32_t)v:-1-(int32_t)(~v);
}
static uint32_t dot32(int32_t ax,int32_t ay,int32_t bx,int32_t by)
{
    return (uint32_t)ax*(uint32_t)bx+(uint32_t)ay*(uint32_t)by;
}

uint32_t fh_stock_d27c4(uint32_t x)
{
    uint32_t t=x>>1,n;
    if(!t){ return (x-1u)<<8; } /* D2828: wrap32, zero -> FFFFFF00 */
    n=0;
    while((t>>=1)!=0) ++n;
    ++n; /* stock r2: bit-length(x)-1 */
    if(n>7){
        uint32_t sh=n-8;
        return (n<<8) + ((x-(1u<<n)) >> sh);
    }
    return (n<<8) + ((x-(1u<<n)) << (8u-n));
}

uint32_t fh_stock_c9db0(uint32_t p)
{
    uint32_t lo=p&0xffffu, hi=p>>16;
    uint32_t m=(lo+hi+0x2002u)>>2;
    uint32_t a=fh_stock_d27c4(lo);
    uint32_t c=fh_stock_d27c4(m);
    uint32_t b=fh_stock_d27c4(hi);
    int32_t y=(int32_t)(uint16_t)b-(int32_t)(uint16_t)c;
    int32_t x=(int32_t)(uint16_t)a-(int32_t)(uint16_t)c;
    return ((uint32_t)(uint16_t)x<<16) | (uint16_t)y;
}

uint32_t fh_stock_c9f30(uint32_t a,uint32_t b)
{
    int32_t ax=signed16(a>>16), ay=signed16(a);
    int32_t bx=signed16(b>>16), by=signed16(b);
    int32_t dx=ax-bx, dy=ay-by;
    return dot32(dx,dy,dx,dy); /* C9F44 MUL + C9F54 MLA, low32 */
}

static void sort4(uint32_t d[4],uint32_t idx[4])
{
    unsigned i,j;
    for(i=0;i<4;i++) idx[i]=i;
    /* D282C is a selection sort.  Delaying the swap until the end of the
     * outer iteration preserves stock ordering when distances are equal. */
    for(i=0;i<3;i++) {
        unsigned lowest=i;
        for(j=i+1;j<4;j++) if(signed32(d[j])<signed32(d[lowest])) lowest=j;
        if(lowest!=i) {
            uint32_t td=d[i],ti=idx[i];
            d[i]=d[lowest];idx[i]=idx[lowest];
            d[lowest]=td;idx[lowest]=ti;
        }
    }
}

void fh_stock_c9f68(uint8_t *ctx)
{
    uint32_t anchors[4],dist[4],idx[4],tp[3];
    uint32_t cur;
    int32_t x0,y0,x1,y1,x2,y2;
    uint32_t dot;
    int32_t w;
    uint16_t b0;
    uint8_t b1,b2;
    unsigned i;

    /* C9F68 packs current ratios as low=a8, high=aa after its ROR16. */
    cur=(uint32_t)get_u16(ctx+0xa8) | ((uint32_t)get_u16(ctx+0xaa)<<16);
    cur=fh_stock_c9db0(cur);
    for(i=0;i<4;i++){
        anchors[i]=get_u32(ctx+0x88+4*i);
        dist[i]=fh_stock_c9f30(fh_stock_c9db0(anchors[i]),cur);
    }
    sort4(dist,idx);
    tp[0]=fh_stock_c9db0(anchors[idx[0]]);
    tp[1]=fh_stock_c9db0(anchors[idx[1]]);
    tp[2]=fh_stock_c9db0(anchors[idx[2]]);

    b1=ctx[0xb1]; b2=ctx[0xb2];
    b1=(uint8_t)((b1&0x0f)|((idx[0]&0x0f)<<4));
    b2=(uint8_t)((b2&0xf0)|(idx[1]&0x0f));

    x0=signed16(tp[0]>>16); y0=signed16(tp[0]);
    x1=signed16(tp[1]>>16); y1=signed16(tp[1]);
    x2=signed16(tp[2]>>16); y2=signed16(tp[2]);

    /* Stock projects the current point onto anchor0->anchor1.  For internal
     * anchors (not endpoint 0/3), a negative projection selects anchor2. */
    {
        int32_t cx=signed16(cur>>16), cy=signed16(cur);
        dot=dot32(x1-x0,y1-y0,cx-x0,cy-y0);
    }
    if(idx[0]!=0u && idx[0]!=3u && (dot&0x80000000u)){
        b2=(uint8_t)((b2&0xf0)|(idx[2]&0x0f));
        x1=x2; y1=y2;
    }
    ctx[0xb1]=b1; ctx[0xb2]=b2;

    /* Projection of current-anchor0 onto chosen anchor segment, Q8 clamp. */
    {
        int32_t cx=signed16(cur>>16), cy=signed16(cur);
        int32_t vx=x1-x0,vy=y1-y0;
        int32_t den=signed32(dot32(vx,vy,vx,vy));
        uint32_t num=dot32(cx-x0,cy-y0,vx,vy);
        if(den<1) den=1;
        /* CA0DC truncates to low32 BEFORE signed idiv. Widening num*256
           changes stock weights on extreme anchor/ratio combinations. */
        w=signed32(num<<8)/den;
        if(w>255) w=256;
        else if(w<0) w=0;
    }
    b0=get_u16(ctx+0xb0);
    b0=(uint16_t)((b0&0xf000u)|((uint32_t)w&0x0fffu));
    put_u16(ctx+0xb0,b0);
}
