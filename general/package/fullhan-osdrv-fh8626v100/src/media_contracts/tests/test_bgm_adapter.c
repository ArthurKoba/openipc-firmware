#include "fh8626_bgm_adapter.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

struct fake {
    unsigned long fail_req;
    int fail_free;
    int fail_release;
    int fail_reopen;
    int backend_recovery;
    int partial_alloc_fail;
    uintptr_t alloc_user;
    uint32_t allocs, frees, releases, reopens;
    uint32_t disables, mem_uninit;
    uint64_t pts;
    uint32_t query_bytes, mem_init_bytes;
};

static int al(void *o, uint32_t n, uint32_t align, struct fh_bgm_block *b)
{
    struct fake *f = o;
    assert(align == 0x400u);
    f->allocs++;
    b->phys = 0x100000u;
    b->user = f->alloc_user ? f->alloc_user : (uintptr_t)0x200000u;
    b->bytes = (n + 4095u) & ~4095u; /* allocator may round internally */
    if (f->partial_alloc_fail) {
        f->backend_recovery = 1;
        return -EIO;
    }
    return 0;
}
static int fr(void *o, struct fh_bgm_block *b)
{
    struct fake *f = o;
    if (f->fail_free) { f->backend_recovery = 1; return -EIO; }
    f->frees++;
    f->backend_recovery = 0;
    memset(b, 0, sizeof(*b));
    return 0;
}
static int rel(void *o, struct fh_bgm_block *b)
{
    struct fake *f = o;
    f->releases++;
    if (f->fail_release) { f->backend_recovery = 1; return -EIO; }
    f->backend_recovery = 0;
    memset(b, 0, sizeof(*b));
    return 0;
}
static int reopen(void *o)
{
    struct fake *f = o;
    f->reopens++;
    return f->fail_reopen ? -EIO : 0;
}
static int io(void *o, unsigned long req, void *arg, uint32_t *raw)
{
    struct fake *f = o;
    if (raw) *raw = 0;
    if (req == f->fail_req) return -EIO;
    if (req == FH_BGM_MEM_QUERY) {
        if (!f->query_bytes) f->query_bytes = 0x4101u;
        ((struct fh_bgm_mem_query *)arg)->bytes = f->query_bytes;
    } else if (req == FH_BGM_MEM_INIT) {
        f->mem_init_bytes = ((struct fh_bgm_mem_init *)arg)->bytes;
    } else if (req == FH_BGM_MEM_UNINIT) {
        f->mem_uninit++;
    } else if (req == FH_BGM_DISABLE) {
        f->disables++;
    } else if (req == FH_BGM_GET_SW_RESULT) {
        struct fh_bgm_sw_result *r = arg;
        r->words[10] = (uint32_t)f->pts;
        r->words[11] = (uint32_t)(f->pts >> 32);
    }
    return 0;
}

static int backend_recovery(void *o)
{
    return ((struct fake *)o)->backend_recovery;
}

static struct fh_bgm_ops make_ops(struct fake *f)
{
    struct fh_bgm_ops o = { al, fr, io, rel, reopen, backend_recovery, f };
    return o;
}

int main(void)
{
    struct fake f = {0};
    struct fh_bgm_ops o = make_ops(&f);
    struct fh_bgm_adapter a;
    struct fh_bgm_frame x = {1280, 720, 0x300000u, 0x12345678u};
    struct fh_bgm_result r;
    uint32_t frees_before;

    /* Normal observe lifecycle + exact MEM_QUERY -> MEM_INIT size. */
    assert(!fh_bgm_adapter_init(&a, &o));
    assert(!fh_bgm_adapter_start(&a, 1280, 720, FH_BGM_MODE_OBSERVE));
    assert(f.mem_init_bytes == f.query_bytes);
    {
        struct fh_bgm_adapter_status st;
        assert(!fh_bgm_adapter_get_status(&a, &st));
        assert(st.state == FH_BGM_RUNNING && !st.needs_recovery);
        assert(st.memory_allocated && st.mem_initialized && st.enabled);
    }
    f.pts = x.pts;
    assert(!fh_bgm_adapter_submit_sync(&a, &x, &r));
    assert(r.pts == x.pts && r.generation == a.generation);
    {
        struct fh_bgm_adapter_status st;
        assert(!fh_bgm_adapter_get_status(&a, &st));
        assert(st.submitted == 1 && st.completed == 1 && st.rejected == 0 && st.stale == 0);
    }
    assert(!fh_bgm_adapter_stop(&a));
    assert(!fh_bgm_adapter_stop(&a)); /* repeated stop is idempotent */
    assert(f.allocs == 1 && f.frees == 1);
    assert(!fh_bgm_adapter_destroy(&a));

    /* Native hot stop becomes HELD. Ordinary start cannot hide recovery. */
    memset(&f, 0, sizeof(f)); o = make_ops(&f);
    assert(!fh_bgm_adapter_init(&a, &o));
    assert(!fh_bgm_adapter_start(&a, 160, 90, FH_BGM_MODE_ENCODER_NATIVE));
    assert(fh_bgm_adapter_submit_sync(&a, &x, &r) == -EOPNOTSUPP);
    assert(!fh_bgm_adapter_stop(&a));
    assert(a.state == FH_BGM_HELD && f.mem_uninit == 0 && f.frees == 0);
    assert(fh_bgm_adapter_start(&a, 160, 90, FH_BGM_MODE_ENCODER_NATIVE) == -EUCLEAN);
    assert(fh_bgm_adapter_needs_recovery(&a));
    assert(!fh_bgm_adapter_recover(&a));
    assert(a.state == FH_BGM_CLOSED && f.releases == 1 && f.reopens == 1);
    assert(!fh_bgm_adapter_start(&a, 160, 90, FH_BGM_MODE_ENCODER_NATIVE));
    assert(!fh_bgm_adapter_stop(&a));
    assert(!fh_bgm_adapter_destroy(&a));

    /* MEM_INIT error is completion-unknown: free only after confirmed UNINIT. */
    memset(&f, 0, sizeof(f)); o = make_ops(&f); f.fail_req = FH_BGM_MEM_INIT;
    assert(!fh_bgm_adapter_init(&a, &o));
    assert(fh_bgm_adapter_start(&a, 1280, 720, FH_BGM_MODE_OBSERVE) == -EIO);
    assert(a.state == FH_BGM_CLOSED && f.mem_uninit == 1 && f.frees == 1);
    assert(!fh_bgm_adapter_destroy(&a));


    /* SET_VI failure after confirmed MEM_INIT rolls back through MEM_UNINIT. */
    memset(&f, 0, sizeof(f)); o = make_ops(&f); f.fail_req = FH_BGM_SET_VI_ATTR;
    assert(!fh_bgm_adapter_init(&a, &o));
    assert(fh_bgm_adapter_start(&a, 1280, 720, FH_BGM_MODE_OBSERVE) == -EIO);
    assert(a.state == FH_BGM_CLOSED && f.mem_uninit == 1 && f.frees == 1);
    assert(!fh_bgm_adapter_destroy(&a));

    /* ENABLE failure is completion-unknown: successful DISABLE + UNINIT is
     * required before the allocation may be released. */
    memset(&f, 0, sizeof(f)); o = make_ops(&f); f.fail_req = FH_BGM_ENABLE;
    assert(!fh_bgm_adapter_init(&a, &o));
    assert(fh_bgm_adapter_start(&a, 1280, 720, FH_BGM_MODE_OBSERVE) == -EIO);
    assert(a.state == FH_BGM_CLOSED && f.disables == 1 && f.mem_uninit == 1 && f.frees == 1);
    assert(!fh_bgm_adapter_destroy(&a));

    /* Failed rollback UNINIT => memory hold + POISONED, never free. */
    memset(&f, 0, sizeof(f)); o = make_ops(&f);
    assert(!fh_bgm_adapter_init(&a, &o));
    assert(!fh_bgm_adapter_start(&a, 1280, 720, FH_BGM_MODE_OBSERVE));
    f.fail_req = FH_BGM_MEM_UNINIT;
    frees_before = f.frees;
    assert(fh_bgm_adapter_stop(&a) == -EIO);
    assert(a.state == FH_BGM_POISONED && f.frees == frees_before);
    f.fail_req = 0;
    assert(!fh_bgm_adapter_recover(&a));
    assert(a.state == FH_BGM_CLOSED);
    assert(!fh_bgm_adapter_destroy(&a));

    /* Result failure and stale PTS both poison the current generation. */
    memset(&f, 0, sizeof(f)); o = make_ops(&f);
    assert(!fh_bgm_adapter_init(&a, &o));
    assert(!fh_bgm_adapter_start(&a, 1280, 720, FH_BGM_MODE_OBSERVE));
    f.fail_req = FH_BGM_GET_SW_RESULT;
    assert(fh_bgm_adapter_submit_sync(&a, &x, &r) == -EIO);
    assert(a.state == FH_BGM_POISONED && fh_bgm_adapter_stop(&a) == -EUCLEAN);
    assert(fh_bgm_adapter_start(&a, 1280, 720, FH_BGM_MODE_OBSERVE) == -EUCLEAN);
    f.fail_req = 0;
    assert(!fh_bgm_adapter_recover(&a));
    assert(!fh_bgm_adapter_start(&a, 1280, 720, FH_BGM_MODE_OBSERVE));
    f.pts = x.pts + 1;
    assert(fh_bgm_adapter_submit_sync(&a, &x, &r) == -ESTALE);
    assert(a.state == FH_BGM_POISONED && a.stale == 1);
    assert(!fh_bgm_adapter_recover(&a));
    assert(!fh_bgm_adapter_destroy(&a));



    /* A stale result from the previous generation after explicit restart is
     * rejected; generation fencing is not merely a same-run PTS check. */
    memset(&f, 0, sizeof(f)); o = make_ops(&f);
    assert(!fh_bgm_adapter_init(&a, &o));
    assert(!fh_bgm_adapter_start(&a, 1280, 720, FH_BGM_MODE_OBSERVE));
    f.pts = x.pts;
    assert(!fh_bgm_adapter_submit_sync(&a, &x, &r));
    assert(!fh_bgm_adapter_stop(&a));
    /* CLOSED observe stop is a clean boundary; use a new start/generation. */
    assert(!fh_bgm_adapter_start(&a, 1280, 720, FH_BGM_MODE_OBSERVE));
    {
        struct fh_bgm_frame y = x;
        y.pts = x.pts + 0x100u;
        f.pts = x.pts; /* delayed result from the previous epoch */
        assert(fh_bgm_adapter_submit_sync(&a, &y, &r) == -ESTALE);
        assert(a.state == FH_BGM_POISONED);
    }
    assert(!fh_bgm_adapter_recover(&a));
    assert(!fh_bgm_adapter_destroy(&a));

    /* Partial allocator failure must not be misclassified as clean CLOSED. */
    memset(&f, 0, sizeof(f)); o = make_ops(&f); f.partial_alloc_fail = 1;
    assert(!fh_bgm_adapter_init(&a, &o));
    assert(fh_bgm_adapter_start(&a, 1280, 720, FH_BGM_MODE_OBSERVE) == -EIO);
    assert(a.state == FH_BGM_RECOVERY_REQUIRED);
    f.partial_alloc_fail = 0;
    assert(!fh_bgm_adapter_recover(&a));
    assert(!fh_bgm_adapter_destroy(&a));

    /* Host pointer truncation is rejected before MEM_INIT. */
#if UINTPTR_MAX > UINT32_MAX
    memset(&f, 0, sizeof(f)); o = make_ops(&f); f.alloc_user = (uintptr_t)UINT32_MAX + 1u;
    assert(!fh_bgm_adapter_init(&a, &o));
    assert(fh_bgm_adapter_start(&a, 1280, 720, FH_BGM_MODE_OBSERVE) == -EOVERFLOW);
    assert(f.mem_uninit == 0 && f.frees == 1);
    assert(!fh_bgm_adapter_destroy(&a));
#endif

    /* VMM free failure is not hidden as CLOSED. */
    memset(&f, 0, sizeof(f)); o = make_ops(&f);
    assert(!fh_bgm_adapter_init(&a, &o));
    assert(!fh_bgm_adapter_start(&a, 1280, 720, FH_BGM_MODE_OBSERVE));
    f.fail_free = 1;
    assert(fh_bgm_adapter_stop(&a) == -EIO);
    assert(a.state == FH_BGM_RECOVERY_REQUIRED);
    f.fail_free = 0;
    assert(!fh_bgm_adapter_recover(&a));
    assert(!fh_bgm_adapter_destroy(&a));

    /* Final release failure keeps the object alive/poisoned for explicit retry. */
    memset(&f, 0, sizeof(f)); o = make_ops(&f);
    assert(!fh_bgm_adapter_init(&a, &o));
    assert(!fh_bgm_adapter_start(&a, 160, 90, FH_BGM_MODE_ENCODER_NATIVE));
    assert(!fh_bgm_adapter_stop(&a));
    f.fail_release = 1;
    assert(fh_bgm_adapter_recover(&a) == -EIO);
    assert(a.state == FH_BGM_POISONED);
    f.fail_release = 0;
    assert(!fh_bgm_adapter_recover(&a));
    assert(!fh_bgm_adapter_destroy(&a));

    puts("BGM rollback/recovery/generation lifecycle: PASS");
    return 0;
}
