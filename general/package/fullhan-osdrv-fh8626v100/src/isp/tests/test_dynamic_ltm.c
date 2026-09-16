#include "fh8626_isp_runtime.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int awb_hook_calls;
static int control_hook_order;
static unsigned init_calls;
static void init_triplet(void *opaque,const uint32_t triplet[3])
{
    struct fh_isp_runtime *rt=opaque;
    static const uint32_t expected[4][3]={{0x123,0x8000,0xabcd},{0x8000,0x123,0xffff},
                                         {0xabcd,0xffff,0x123},{0xffff,0xabcd,0x8000}};
    unsigned row=(rt->mmio[0x24/4]>>1)&3u;
    assert(!memcmp(triplet,expected[row],sizeof(expected[row])));
    assert(rt->mmio[0x24/4]&0x100000u); /* before CFD00 clears this bit */
    assert(rt->ae_state.dirty_mask==0); /* after C9740 */
    ++init_calls;
}

static void set_group_population(uint8_t *cfg, uint32_t count)
{
    for (unsigned plane = 0; plane < 4u; ++plane)
        for (unsigned tile = 0; tile < 256u; ++tile)
            memcpy(cfg + 0x5c8u + plane * 0x1000u + tile * 16u + 4u,
                   &count, sizeof(count));
}

static int mark_ae_slot(void *opaque)
{
    assert(opaque == &control_hook_order);
    assert(control_hook_order == 0);
    control_hook_order = 1;
    return 0;
}

static int stop_at_awb_slot(void *opaque)
{
    assert(opaque == &awb_hook_calls);
    assert(control_hook_order == 1);
    control_hook_order = 2;
    ++awb_hook_calls;
    return -ECANCELED;
}

static int fail_ae_slot(void *opaque)
{
    (void)mark_ae_slot(opaque);
    return -EIO;
}

static int continue_awb_slot(void *opaque)
{
    (void)stop_at_awb_slot(opaque);
    return 0;
}

int main(void)
{
    for(unsigned row=0;row<4;row++){
        struct fh_isp_runtime startup;
        uint32_t regs[0x1000]={0};
        fh_isp_runtime_reset(&startup);
        assert(!fh_isp_runtime_attach_mmio(&startup,regs,sizeof(regs)));
        startup.awb_init=init_triplet;startup.awb_init_opaque=&startup;
        startup.ae_state.dirty_mask=UINT32_MAX;
        regs[0x24/4]=0x100000u|(row<<1);
        regs[0x224/4]=0x80000123;regs[0x228/4]=0xabcdffff;
        assert(!fh_isp_runtime_tick_with_control_hooks(&startup,NULL,NULL,NULL,NULL));
        assert(init_calls==row+1&&startup.stock_runtime_started==1);
        assert(regs[0x24/4]==row<<1);
        assert(regs[0x224/4]==0x80000123&&regs[0x228/4]==0xabcdffff);
    }
    struct fh_isp_runtime rt;
    uint32_t mmio[0x1000] = {0};
    uint8_t *cfg = calloc(1, 0x22000u);
    uint32_t *control;
    assert(cfg != NULL);
    fh_isp_runtime_reset(&rt);
    assert(fh_isp_runtime_attach_mmio(&rt, mmio, sizeof(mmio)) == 0);
    assert(fh_isp_runtime_attach_isp_cfg(&rt, cfg, 0x22000u) == 0);
    mmio[0x030u / 4u] = (719u << 16) | 1279u;
    mmio[0x1ccu / 4u] = 0u;
    rt.ctx[0x13] = 0x20u;
    rt.ctx[0x66] = 64u;
    rt.ctx[0x265] = 3u;
    rt.ctx[0x268] = 64u;
    rt.ctx[0x269] = 64u;
    rt.ctx[0x26a] = 64u;
    rt.ctx[0x260] = 3u;
    rt.ctx[0x264] = 0x5au;
    rt.ctx[0x26c] = 7u;
    rt.ctx[0x278] = 2u;
    rt.ctx[0x284] = 5u;
    rt.ctx[0x290] = 23u;
    rt.ctx[0x29c] = 0u;
    rt.ctx[0xa70] = 1u;
    {
        uint32_t total_gain = 64u << 12;
        memcpy(rt.ctx + 0x60, &total_gain, sizeof(total_gain));
    }
    control = (uint32_t *)(void *)(cfg + 0x21250u);
    control[0] = 114950u;
    control[1] = 48128u;
    control[2] = 0xbc00u;
    assert(fh_isp_runtime_cb890_init_d0460(&rt) == 0);
    assert(fh_isp_runtime_apply_d0df4_control(&rt) == 0);
    /* The FIRST D0528 call publishes record2 immediately, not merely a
     * diagnostic summary waiting for D0630. Zero grid: log2(FFF),log2(1). */
    assert(rt.ltm_summary_calls == 1u);
    assert(rt.d0460_state.rec[2].base_200 == 3071u);
    assert(rt.d0460_state.rec[2].base_e00 == 0u);
    assert(fh_isp_runtime_apply_dynamic_ltm(&rt) == 0);
    assert(rt.ltm_summary_calls == 2u);
    assert((mmio[0x240u / 4u] & 0xffffu) >= 0x1100u);
    assert((mmio[0x244u / 4u] & 0xffffu) >= 0xa100u);
    assert((mmio[0x248u / 4u] & 0xfffu) >= 0x200u);
    assert((mmio[0x23cu / 4u] & ~0x0003ff00u) == 0u);
    assert(fh_isp_runtime_apply_d0b2c(&rt) == 0);
    assert((mmio[0x23cu / 4u] & 0xffu) == 0x5au);
    assert(mmio[0x270u / 4u] == ((uint32_t)73u << 16));
    assert((mmio[0x2c0u / 4u] & 0xffffu) == 2804u);
    assert(mmio[0x2c4u / 4u] == ((uint32_t)32u << 16));
    assert((mmio[0x30cu / 4u] & 0xffffu) == 4092u);
    assert((mmio[0x2c0u / 4u] >> 16) == 0u);
    assert((mmio[0x30cu / 4u] >> 16) == 0u);

    /* Static selector branch and both row clamps are profile-independent. */
    rt.ctx[0x260] = 0x90u; /* static, mode0, requested 948 row9 */
    rt.ctx[0x261] = 31u;   /* clamps to BE4 row24 */
    rt.ctx[0x262] = 2u;
    rt.ctx[0x263] = 5u;
    rt.ctx[0x266] = 9u;
    assert(fh_isp_runtime_apply_d0b2c(&rt) == 0);
    assert(mmio[0x270u / 4u] == ((uint32_t)43u << 16));
    assert(mmio[0x2c0u / 4u] == 0x07cfu);
    assert(mmio[0x2c4u / 4u] == ((uint32_t)724u << 16));
    assert(mmio[0x30cu / 4u] == 0x0498u);
    assert((mmio[0x254u / 4u] & 0x00ffffffu) == 0x00050020u);
    rt.ctx[0x260] = 0x94u; /* static mode1 */
    mmio[0x254u / 4u] = 0xdeadbeefu;
    assert(fh_isp_runtime_apply_d0b2c(&rt) == 0);
    assert(mmio[0x254u / 4u] == 0x00ffffffu); /* high byte MUST clear */
    rt.ctx[0x260] = 0x98u; /* static mode2 */
    assert(fh_isp_runtime_apply_d0b2c(&rt) == 0);
    assert(mmio[0x254u / 4u] == 0xff000000u);
    {
        static const uint8_t values[]={0,1,127,128,254,255};
        uint32_t old[0x1000];
        for(unsigned mode=0;mode<4;mode++)
        for(unsigned ai=0;ai<6;ai++)for(unsigned bi=0;bi<6;bi++){
            uint32_t a=values[ai],b=values[bi],expected;
            rt.ctx[0x260]=(uint8_t)(0x90u|(mode<<2));
            rt.ctx[0x262]=(uint8_t)a;rt.ctx[0x263]=(uint8_t)b;
            mmio[0x254/4]=0xdeadbeefu;
            memcpy(old,mmio,sizeof(old));
            assert(!fh_isp_runtime_apply_d0b2c(&rt));
            if(!mode&&b<a){assert(!memcmp(old,mmio,sizeof(old)));continue;}
            expected=mode==1?0x00ffffffu:mode>=2?0xff000000u:
                ((((b-a)*2u)&255u)<<24)|(b<<16)|(a<<4);
            assert(mmio[0x254/4]==expected);
        }
    }

    /* D0FEC: prove the Ghidra-observed quadratic gain term. A linear
     * c0*gain implementation produces zero here; c0*gain*gain produces 1. */
    {
        uint32_t mode = 0u;
        int16_t c0 = 4096, c1 = 0;
        mmio[0x450u / 4u] = 0xdeadbeefu;
        rt.ctx[0x11] |= 0x40u;
        memcpy(rt.ctx + 0x11ac, &mode, sizeof(mode));
        assert(fh_isp_runtime_apply_d0fec(&rt) == 0);
        assert(mmio[0x450u / 4u] == 0xdeadbeefu);
        mode = 1u;
        memcpy(rt.ctx + 0x11ac, &mode, sizeof(mode));
        rt.ctx[0x1ec] = 4u;
        rt.ctx[0x1f0] = 16u;
        rt.ctx[0x1f1] = 7u;
        rt.ctx[0x1f4] = 0u;
        rt.ctx[0x1f5] = 0u;
        rt.ctx[0x1fc] = 0u;
        rt.ctx[0x200] = 3u;
        memcpy(rt.ctx + 0x1f8, &c0, sizeof(c0));
        memcpy(rt.ctx + 0x1fa, &c1, sizeof(c1));
        assert(fh_isp_runtime_apply_d0fec(&rt) == 0);
        assert(((mmio[0x450u / 4u] >> 8) & 0xfffffu) == 1u);
        assert(((mmio[0x44cu / 4u] >> 4) & 0xffu) == 3u);
        assert(((mmio[0x46cu / 4u] >> 8) & 0xffu) == 7u);
        /* The ARM high word survives until saturation (D1094..D1104).
         * At maximum C5DB8 gain the old int32 cast could invert its sign. */
        const uint32_t gains[]={0,64,65535,262144,524288,1048575};
        const int16_t coeffs[]={-32768,-4096,-1,0,1,4096,32767};
        for(unsigned gi=0;gi<sizeof(gains)/sizeof(gains[0]);gi++)
        for(unsigned ci=0;ci<sizeof(coeffs)/sizeof(coeffs[0]);ci++)
        for(unsigned bi=0;bi<sizeof(coeffs)/sizeof(coeffs[0]);bi++){
            uint32_t packed_gain=gains[gi]<<12;
            int64_t n=(int64_t)coeffs[ci]*gains[gi]*gains[gi]+0x80000;
            /* Independent floor division, not implementation's signed shift. */
            int64_t expected=n>=0?n/1048576:-((-n+1048575)/1048576);
            expected+=coeffs[bi]+8;
            expected=expected>=0?expected/16:-((-expected+15)/16);
            if(expected>0x7ffff)expected=0x7ffff;
            if(expected< -0x80000)expected= -0x80000;
            memcpy(rt.ctx+0x60,&packed_gain,4);
            memcpy(rt.ctx+0x1f8,&coeffs[ci],2);
            memcpy(rt.ctx+0x1fa,&coeffs[bi],2);
            assert(!fh_isp_runtime_apply_d0fec(&rt));
            assert(((mmio[0x450/4]>>8)&0xfffffu)==((uint32_t)expected&0xfffffu));
        }
        { uint32_t packed_gain=64u<<12;memcpy(rt.ctx+0x60,&packed_gain,4); }
    }

    /* D1648/D16A4/D1724: live 32x24 source at isp_cfg+0x205c8 and
     * relocated stock row0. */
    {
        uint32_t *st = (uint32_t *)(void *)(cfg + 0x205c8u);
        unsigned i, j;
        for (i = 0u; i < 32u; ++i)
            for (j = 0u; j < 5u; ++j) st[i * 6u + j] = 1u;
        rt.ctx[0x12] |= 0x02u;
        rt.ctx[0x999] = 0u;
        rt.ctx[0xa3c] &= (uint8_t)~0x02u;
        assert(fh_isp_runtime_apply_d1724(&rt) == 0);
        assert(mmio[0x40cu / 4u] == 0x80808080u);
        assert(mmio[0x420u / 4u] == 0x00102030u);
        assert(mmio[0x3bcu / 4u] == 0x020600fdu);
    }

    /* The first tick is CB890 initialization. Subsequent AWB failures are
     * diagnostic; CB970 continues late stages, including clearing dirty. */
    {
        struct fh_isp_runtime ordered;
        uint32_t ordered_mmio[0x1000] = {0};
        fh_isp_runtime_reset(&ordered);
        ordered.ctx[0x10] = 1u;
        set_group_population(cfg, 1u);
        assert(fh_isp_runtime_attach_mmio(&ordered, ordered_mmio,
                                          sizeof(ordered_mmio)) == 0);
        assert(fh_isp_runtime_attach_isp_cfg(&ordered, cfg, 0x22000u) == 0);
        assert(fh_isp_runtime_tick_with_control_hooks(&ordered,
                                                      mark_ae_slot,
                                                      &control_hook_order,
                                                      stop_at_awb_slot,
                                                      &awb_hook_calls) == 0);
        assert(awb_hook_calls == 0);
        assert(control_hook_order == 0);
        ordered.params_dirty=1;
        ordered.gamma_candidate_pending=1;
        ordered.ctx[0x5f0]=0x5a;
        assert(fh_isp_runtime_tick_with_control_hooks(&ordered,
                                                      mark_ae_slot,
                                                      &control_hook_order,
                                                      stop_at_awb_slot,
                                                      &awb_hook_calls) == 0);
        assert(awb_hook_calls == 1);
        assert(ordered.last_awb_error==-ECANCELED && !ordered.params_dirty);
        assert(ordered.ctx[0xf20]==0x5a && !ordered.gamma_candidate_pending);
        assert(control_hook_order == 2);
        /* Bad populations suppress AE once, preserve AWB, and recover. */
        set_group_population(cfg, 0u);
        control_hook_order = 1; /* calling AE here would fail its assertion */
        assert(fh_isp_runtime_tick_with_control_hooks(&ordered, mark_ae_slot,
                   &control_hook_order, stop_at_awb_slot, &awb_hook_calls) == 0);
        assert(ordered.c73f8_invalid == 0u && awb_hook_calls == 2);
        set_group_population(cfg, 1u);
        control_hook_order = 0;
        assert(fh_isp_runtime_tick_with_control_hooks(&ordered, mark_ae_slot,
                   &control_hook_order, stop_at_awb_slot, &awb_hook_calls) == 0);
        assert(control_hook_order == 2 && awb_hook_calls == 3);
        /* A recoverable AE failure must not freeze AWB/late ISP. */
        control_hook_order = 0;
        assert(fh_isp_runtime_tick_with_control_hooks(&ordered, fail_ae_slot,
                   &control_hook_order, continue_awb_slot, &awb_hook_calls) == 0);
        assert(ordered.last_ae_error == -EIO);
        assert(ordered.last_awb_error == 0);
        assert(control_hook_order == 2 && awb_hook_calls == 4);
        /* AE-off must still reach AWB and must not consume missing AE stats. */
        ordered.ctx[0x10] = 0u;
        /* This fixture deliberately has no fresh gain publication: leave
         * gain-dependent BLC off instead of testing its separate -EAGAIN. */
        ordered.ctx[0x11] = 0u;
        ordered.isp_cfg = NULL;
        control_hook_order = 1;
        assert(fh_isp_runtime_tick_with_control_hooks(&ordered,
                                                      mark_ae_slot,
                                                      &control_hook_order,
                                                      stop_at_awb_slot,
                                                      &awb_hook_calls) == 0);
        assert(awb_hook_calls == 5 && control_hook_order == 2);
        assert(ordered.last_ae_error == 0);
        /* C9240 bypasses unavailable normal stats/history, but still AWB. */
        ordered.ctx[0x10] = 1u;
        ordered.ctx[0x2c] = 0x10u;
        control_hook_order = 0;
        assert(fh_isp_runtime_tick_with_control_hooks(&ordered, mark_ae_slot,
                   &control_hook_order, stop_at_awb_slot, &awb_hook_calls) == 0);
        assert(control_hook_order == 2 && awb_hook_calls == 6);
    }
    free(cfg);
    return 0;
}
