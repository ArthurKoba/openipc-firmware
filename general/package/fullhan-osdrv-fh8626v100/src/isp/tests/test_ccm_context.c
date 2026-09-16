#include "fh8626_isp_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint16_t u16(const uint8_t*p){return p[0]|((uint16_t)p[1]<<8);}
static int sx(uint16_t v){v&=8191;return v&4096?(int)v-8192:v;}
static uint32_t next(uint32_t*s){*s=*s*1664525u+1013904223u;return *s;}
int main(void)
{
    struct fh_isp_runtime rt;
    uint8_t expected[FH_ISP_CTX_SIZE];uint32_t regs[0x1000],seed=321;
    for(unsigned n=0;n<10000;n++){
        fh_isp_runtime_reset(&rt);memset(regs,0xa5,sizeof(regs));
        assert(!fh_isp_runtime_attach_mmio(&rt,regs,sizeof(regs)));
        for(unsigned i=0;i<sizeof(expected);i++)rt.ctx[i]=next(&seed)>>24;
        rt.ctx[0x12]|=1;memcpy(expected,rt.ctx,sizeof(expected));
        unsigned w=u16(expected+0xb0)&4095,ia=expected[0xb2]&15,ib=expected[0xb1]>>4;
        for(unsigned i=0;i<6;i++){
            uint32_t pair=0;
            for(unsigned h=0;h<2;h++){
                int a=sx(u16(expected+0xe0+24*ia+4*i+2*h));
                int b=sx(u16(expected+0xe0+24*ib+4*i+2*h));
                int v=((int)w*a+(256-(int)w)*b+128)/256;
                pair|=((uint32_t)v&8191)<<(16*h);
            }
            memcpy(expected+0x140+4*i,&pair,4); /* ARM in-place alias order */
        }
        assert(!fh_isp_runtime_apply_ce764(&rt));
        assert(!memcmp(expected,rt.ctx,sizeof(expected)));
        for(unsigned i=0;i<sizeof(regs)/4;i++){
            uint32_t v=0xa5a5a5a5;
            if(i>=0x4c0/4&&i<=0x4d4/4)memcpy(&v,expected+0x140+(i-0x4c0/4)*4,4);
            assert(regs[i]==v);
        }
    }
    rt.ctx[0x12]&=(uint8_t)~1u;memcpy(expected,rt.ctx,sizeof(expected));
    assert(!fh_isp_runtime_apply_ce764(&rt));assert(!memcmp(expected,rt.ctx,sizeof(expected)));
    puts("CCM twelve-bit weight/context publication/aliasing: PASS");return 0;
}
