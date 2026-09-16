/* Candidate implementation. No direct MMIO, allocation, execution, or retry. */
#include "fh8626_geometry.h"
#include <string.h>

static void clear_error(struct fhg_error *e)
{
    if (e != NULL) memset(e, 0, sizeof(*e));
}

static int fail(struct fhg_error *e, int status, enum fhg_stage stage)
{
    if (e != NULL) { e->status = status; e->stage = stage; }
    return status;
}

static uint32_t align_up(uint32_t v, uint32_t a)
{
    /* All callers bound v <= 2048 before this addition. */
    return (v + a - 1u) & ~(a - 1u);
}

static int bounded_even(uint32_t v, uint32_t lo, uint32_t hi)
{
    return v >= lo && v <= hi && (v & 1u) == 0u;
}

static int exact_step(uint32_t input, uint32_t output, uint32_t *step)
{
    uint64_t n, q;
    if (output < 32u || (output & 15u) != 0u) return FHG_E_RANGE;
    /* isp!vpu_reg_set_scale_step@1A0F0, __aeabi_uidiv, mask 0x03ffffff.
     * Use 64-bit intermediate and REJECT the wrap that stock would mask away.
     */
    n = ((uint64_t)input << 16) + (uint64_t)(output >> 5);
    q = n / (uint64_t)(output >> 4);
    if (q == 0u || q > UINT32_C(0x03ffffff)) return FHG_E_RANGE;
    *step = (uint32_t)q;
    return FHG_OK;
}

int fhg_make_plan(const struct fhg_request *r, struct fhg_plan *out)
{
    struct fhg_plan p;
    uint32_t cw, ch;
    int status;
    if (r == NULL || out == NULL) return FHG_E_ARGUMENT;
    /* Reject before indexing any channel array or making a stock ioctl. */
    if (r->channel > 1u) return FHG_E_UNSUPPORTED;
    if (r->coefficient < FHG_COEFF_INHERIT || r->coefficient > 15)
        return FHG_E_RANGE;
    if (!bounded_even(r->native_width, 32u, FHG_MAX_WIDTH) ||
        !bounded_even(r->native_height, 32u, FHG_MAX_HEIGHT) ||
        !bounded_even(r->visible_width, FHG_MIN_OUTPUT, FHG_MAX_WIDTH) ||
        !bounded_even(r->visible_height, FHG_MIN_OUTPUT, FHG_MAX_HEIGHT))
        return FHG_E_RANGE;
    if ((r->capacity_width == 0u) != (r->capacity_height == 0u))
        return FHG_E_ARGUMENT;
    cw = r->capacity_width ? r->capacity_width : r->visible_width;
    ch = r->capacity_height ? r->capacity_height : r->visible_height;
    if (!bounded_even(cw, FHG_MIN_OUTPUT, FHG_MAX_WIDTH) ||
        !bounded_even(ch, FHG_MIN_OUTPUT, FHG_MAX_HEIGHT)) return FHG_E_RANGE;
    memset(&p, 0, sizeof(p));
    p.request = *r;
    p.surface_width = align_up(r->visible_width, FHG_ALIGNMENT);
    p.surface_height = align_up(r->visible_height, FHG_ALIGNMENT);
    /* ch1 also recalculates ch2 using SIGNED ((height >> 3) << 23).
     * An aligned height of 2048 sets its sign bit. Decline that stock edge
     * even though the generic channel capability table admits it.
     */
    if (r->channel == 1u && p.surface_height >= 2048u) return FHG_E_RANGE;
    p.capacity_width = align_up(cw, FHG_ALIGNMENT);
    p.capacity_height = align_up(ch, FHG_ALIGNMENT);
    if (p.surface_width > p.capacity_width || p.surface_height > p.capacity_height)
        return FHG_E_MEMORY;
    p.allocation_width = align_up(p.capacity_width, FHG_MEM_ALIGNMENT);
    p.allocation_height = align_up(p.capacity_height, FHG_MEM_ALIGNMENT);
    status = exact_step(r->native_width, p.surface_width, &p.scaler_step_x);
    if (status != FHG_OK) return status;
    status = exact_step(r->native_height, p.surface_height, &p.scaler_step_y);
    if (status != FHG_OK) return status;
    p.right_crop_pixels = p.surface_width - r->visible_width;
    p.bottom_crop_pixels = p.surface_height - r->visible_height;
    p.media_source_id = r->channel + 1u;
    p.dependencies = FHG_DEP_SOURCE_AND_BOUND_CONSUMERS |
        (r->channel == 0u ? FHG_DEP_MAIN_BGM_CPY : FHG_DEP_SUB_AND_CASCADE_CH2);
    p.needs_owner_pacing_restore = 1u; /* SET_GEOMETRY disables frame control. */
    *out = p;
    return FHG_OK;
}

size_t fhg_wire_size(uint32_t request)
{
    switch (request) {
    case FHG_IOCTL_GET_VI: return sizeof(struct fhg_wire_vi);
    case FHG_IOCTL_QUERY_MEMORY: return sizeof(struct fhg_wire_query);
    case FHG_IOCTL_SET_MEMORY: return sizeof(struct fhg_wire_memory);
    case FHG_IOCTL_SET_GEOMETRY:
    case FHG_IOCTL_GET_GEOMETRY: return sizeof(struct fhg_wire_geometry);
    case FHG_IOCTL_SET_COEFF: return sizeof(struct fhg_wire_coeff);
    default: return 0;
    }
}

static int valid_ops(const struct fhg_ops *o)
{
    return o != NULL && o->enter != NULL && o->leave != NULL && o->control != NULL;
}

static int call(const struct fhg_ops *o, uint32_t request, void *payload,
                enum fhg_stage stage, struct fhg_error *e)
{
    uint32_t driver = 0u;
    int sys_errno = 0;
    int rc = o->control(o->context, request, payload, fhg_wire_size(request),
                        &driver, &sys_errno);
    if (rc != 0 || driver != 0u) {
        if (e != NULL) {
            e->ioctl_request = request;
            e->driver_result = driver;
            e->system_errno = sys_errno;
            e->callback_status = rc;
        }
        return fail(e, rc != 0 ? FHG_E_TRANSPORT : FHG_E_DRIVER, stage);
    }
    return FHG_OK;
}

static int enter(const struct fhg_ops *o, const struct fhg_plan *p,
                 enum fhg_phase phase, struct fhg_error *e)
{
    int rc = o->enter(o->context, p, phase);
    if (rc == 0) return FHG_OK;
    if (e != NULL) e->callback_status = rc;
    return fail(e, FHG_E_GUARD, FHG_STAGE_GUARD);
}

/* Caller holds the owner's configuration lock throughout this function. */
static int inspect(const struct fhg_ops *o, const struct fhg_plan *p,
                   struct fhg_requirements *requirements, struct fhg_error *e)
{
    struct fhg_wire_vi vi = {0u, 0u, 0u};
    struct fhg_wire_query q;
    int rc = call(o, FHG_IOCTL_GET_VI, &vi, FHG_STAGE_GET_VI, e);
    if (rc != FHG_OK) return rc;
    if (vi.width != p->request.native_width || vi.height != p->request.native_height)
        return fail(e, FHG_E_NATIVE_MISMATCH, FHG_STAGE_GET_VI);
    q.channel = p->request.channel;
    q.width = p->capacity_width;
    q.height = p->capacity_height;
    q.bytes = 0u;
    rc = call(o, FHG_IOCTL_QUERY_MEMORY, &q, FHG_STAGE_QUERY_MEMORY, e);
    if (rc != FHG_OK) return rc;
    /* Query returns bytes in payload[3], not in the ioctl return code. */
    if (q.channel != p->request.channel || q.width != p->capacity_width ||
        q.height != p->capacity_height || q.bytes == 0u || q.bytes > INT32_MAX)
        return fail(e, FHG_E_READBACK, FHG_STAGE_QUERY_MEMORY);
    requirements->plan = *p;
    requirements->bytes = q.bytes;
    requirements->mean_fps_diagnostic = vi.mean_fps;
    return FHG_OK;
}

int fhg_query_requirements(const struct fhg_ops *o, const struct fhg_request *r,
                           struct fhg_requirements *out, struct fhg_error *e)
{
    struct fhg_plan p;
    struct fhg_requirements local;
    int rc;
    clear_error(e);
    if (!valid_ops(o) || out == NULL)
        return fail(e, FHG_E_ARGUMENT, FHG_STAGE_NONE);
    rc = fhg_make_plan(r, &p);
    if (rc != FHG_OK) return fail(e, rc, FHG_STAGE_NONE);
    rc = enter(o, &p, FHG_PHASE_QUERY, e);
    if (rc != FHG_OK) return rc;
    rc = inspect(o, &p, &local, e);
    o->leave(o->context);
    if (rc == FHG_OK) *out = local;
    return rc;
}

static int valid_memory(const struct fhg_memory *m, uint32_t required)
{
    uint64_t limit = UINT64_C(0x100000000);
    /* 1024 base alignment is a conservative allocator contract compatible with
     * separate Y/C media_malloc and encoder submit; query still decides bytes.
     * No claim that this alone verifies a live VMM allocation or CPU mapping.
     */
    return m->physical != 0u && m->user_va32 != 0u &&
        (m->physical & 1023u) == 0u && (m->user_va32 & 1023u) == 0u &&
        m->bytes >= required && m->bytes <= INT32_MAX &&
        (uint64_t)m->physical + m->bytes <= limit &&
        (uint64_t)m->user_va32 + m->bytes <= limit;
}

int fhg_configure_new_channel(const struct fhg_ops *o, const struct fhg_request *r,
                              const struct fhg_memory *memory,
                              struct fhg_session *s, struct fhg_error *e)
{
    struct fhg_plan p;
    struct fhg_requirements need;
    struct fhg_memory retained;
    struct fhg_wire_memory wm;
    struct fhg_wire_geometry wg;
    struct fhg_wire_coeff wc;
    int rc;
    clear_error(e);
    if (!valid_ops(o) || memory == NULL || s == NULL)
        return fail(e, FHG_E_ARGUMENT, FHG_STAGE_NONE);
    rc = fhg_make_plan(r, &p);
    if (rc != FHG_OK) return fail(e, rc, FHG_STAGE_NONE);
    retained = *memory; /* inputs must not change under another caller */
    rc = enter(o, &p, FHG_PHASE_CONFIGURE_NEW, e);
    if (rc != FHG_OK) return rc;
    /* Check session state under the same owner lock as the mutation. */
    if (s->state != FHG_STATE_FRESH) {
        rc = fail(e, FHG_E_STATE, FHG_STAGE_NONE);
        goto done;
    }
    rc = inspect(o, &p, &need, e);
    if (rc != FHG_OK) goto done;
    if (!valid_memory(&retained, need.bytes)) {
        rc = fail(e, FHG_E_MEMORY, FHG_STAGE_MEMORY_CHECK);
        goto done;
    }
    /* Even ioctl failure may follow a real kernel mutation/copy_to_user fault.
     * Set dirty state and retain allocation BEFORE calling the first setter.
     */
    s->plan = p;
    s->retained_memory = retained;
    s->required_bytes = need.bytes;
    s->geometry_driver_readback_matches = 0u;
    s->coefficient_write_acknowledged = 0u;
    s->state = FHG_STATE_RECOVERY_REQUIRED;
    s->last_attempt = FHG_STAGE_SET_MEMORY;
    wm.channel = p.request.channel;
    wm.physical = retained.physical;
    wm.user_va32 = retained.user_va32;
    wm.bytes = retained.bytes;
    wm.max_width = p.capacity_width;
    wm.max_height = p.capacity_height;
    rc = call(o, FHG_IOCTL_SET_MEMORY, &wm, FHG_STAGE_SET_MEMORY, e);
    if (rc != FHG_OK) goto done;
    wg.channel = p.request.channel;
    wg.width = p.request.visible_width;
    wg.height = p.request.visible_height;
    s->last_attempt = FHG_STAGE_SET_GEOMETRY;
    rc = call(o, FHG_IOCTL_SET_GEOMETRY, &wg, FHG_STAGE_SET_GEOMETRY, e);
    if (rc != FHG_OK) goto done;
    if (p.request.coefficient != FHG_COEFF_INHERIT) {
        wc.channel = p.request.channel;
        wc.selector = (uint32_t)p.request.coefficient;
        s->last_attempt = FHG_STAGE_SET_COEFF;
        rc = call(o, FHG_IOCTL_SET_COEFF, &wc, FHG_STAGE_SET_COEFF, e);
        if (rc != FHG_OK) goto done;
        s->coefficient_write_acknowledged = 1u; /* ACK only, never readback */
    }
    wg.channel = p.request.channel;
    wg.width = 0u;
    wg.height = 0u;
    s->last_attempt = FHG_STAGE_GET_GEOMETRY;
    rc = call(o, FHG_IOCTL_GET_GEOMETRY, &wg, FHG_STAGE_GET_GEOMETRY, e);
    if (rc != FHG_OK) goto done;
    if (wg.channel != p.request.channel || wg.width != p.surface_width ||
        wg.height != p.surface_height) {
        rc = fail(e, FHG_E_READBACK, FHG_STAGE_GET_GEOMETRY);
        goto done;
    }
    s->geometry_driver_readback_matches = 1u;
    s->state = FHG_STATE_CONFIGURED_NOT_STREAMING;
    rc = FHG_OK;
done:
    o->leave(o->context); /* unlock only; never release retained memory/start */
    return rc;
}

const char *fhg_status_string(int status)
{
    switch (status) {
    case FHG_OK: return "configured/query complete; not a streaming-ready signal";
    case FHG_E_ARGUMENT: return "invalid argument";
    case FHG_E_UNSUPPORTED: return "unsupported channel/backend";
    case FHG_E_RANGE: return "unsupported geometry/coefficient or scaler overflow";
    case FHG_E_STATE: return "session already used; owner recovery required";
    case FHG_E_GUARD: return "owner lock/lifecycle/ABI guard rejected operation";
    case FHG_E_TRANSPORT: return "ioctl transport failed; inspect session state";
    case FHG_E_DRIVER: return "stock driver returned nonzero result";
    case FHG_E_NATIVE_MISMATCH: return "native VI differs from requested source";
    case FHG_E_MEMORY: return "allocation contract not satisfied";
    case FHG_E_READBACK: return "stock query/readback differs from plan";
    default: return "unknown geometry status";
    }
}
