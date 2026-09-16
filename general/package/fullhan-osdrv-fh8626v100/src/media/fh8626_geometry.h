/* FH8626V100 stock geometry adapter, snapshot D / analysis H, 2026-09-05.
 * Integrated startup-only core: host fault tests and ARM build are recorded
 * in GEOMETRY_HANDOFF_INTEGRATION_20260905.md. Hardware acceptance separate.
 * No ownership of fds, allocations, sensor state, encoders, or threads.
 * Derived from the operator handoff candidate; current contracts are in the
 * session report. No hot resize/crop/rotation or channel2 backend.
 */
#ifndef FH8626_GEOMETRY_H
#define FH8626_GEOMETRY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FHG_COEFF_INHERIT (-1)
#define FHG_REQUEST_INITIALIZER {0u, 0u, 0u, 0u, 0u, 0u, 0u, FHG_COEFF_INHERIT}
#define FHG_MIN_OUTPUT 32u
#define FHG_MAX_WIDTH 1920u
#define FHG_MAX_HEIGHT 2048u
#define FHG_ALIGNMENT 16u
#define FHG_MEM_ALIGNMENT 32u

/* These are the exact stock requests, not recomputed _IOWR macros. */
#define FHG_IOCTL_GET_VI       UINT32_C(0xc00c6947)
#define FHG_IOCTL_QUERY_MEMORY UINT32_C(0xc0106943)
#define FHG_IOCTL_SET_MEMORY   UINT32_C(0xc0186944)
#define FHG_IOCTL_SET_GEOMETRY UINT32_C(0xc00c6948)
#define FHG_IOCTL_GET_GEOMETRY UINT32_C(0xc00c6949)
#define FHG_IOCTL_SET_COEFF    UINT32_C(0xc0086966)

/* Fixed ARM32 little-endian wire words. user_va32 is NOT a host pointer. */
struct fhg_wire_vi { uint32_t width, height, mean_fps; };
struct fhg_wire_query { uint32_t channel, width, height, bytes; };
struct fhg_wire_memory {
    uint32_t channel, physical, user_va32, bytes, max_width, max_height;
};
struct fhg_wire_geometry { uint32_t channel, width, height; };
struct fhg_wire_coeff { uint32_t channel, selector; };

typedef char fhg_vi_size_must_be_12[(sizeof(struct fhg_wire_vi) == 12) ? 1 : -1];
typedef char fhg_query_size_must_be_16[(sizeof(struct fhg_wire_query) == 16) ? 1 : -1];
typedef char fhg_memory_size_must_be_24[(sizeof(struct fhg_wire_memory) == 24) ? 1 : -1];
typedef char fhg_geometry_size_must_be_12[(sizeof(struct fhg_wire_geometry) == 12) ? 1 : -1];
typedef char fhg_coeff_size_must_be_8[(sizeof(struct fhg_wire_coeff) == 8) ? 1 : -1];

enum fhg_status {
    FHG_OK = 0,
    FHG_E_ARGUMENT = -1,
    FHG_E_UNSUPPORTED = -2,
    FHG_E_RANGE = -3,
    FHG_E_STATE = -4,
    FHG_E_GUARD = -5,
    FHG_E_TRANSPORT = -6,
    FHG_E_DRIVER = -7,
    FHG_E_NATIVE_MISMATCH = -8,
    FHG_E_MEMORY = -9,
    FHG_E_READBACK = -10
};

enum fhg_phase { FHG_PHASE_QUERY = 1, FHG_PHASE_CONFIGURE_NEW = 2 };
enum fhg_stage {
    FHG_STAGE_NONE = 0, FHG_STAGE_GUARD, FHG_STAGE_GET_VI,
    FHG_STAGE_QUERY_MEMORY, FHG_STAGE_MEMORY_CHECK, FHG_STAGE_SET_MEMORY,
    FHG_STAGE_SET_GEOMETRY, FHG_STAGE_SET_COEFF, FHG_STAGE_GET_GEOMETRY
};
enum fhg_state {
    FHG_STATE_FRESH = 0,
    FHG_STATE_RECOVERY_REQUIRED = 1,
    FHG_STATE_CONFIGURED_NOT_STREAMING = 2
};
enum fhg_dependency {
    FHG_DEP_SOURCE_AND_BOUND_CONSUMERS = 1u,
    FHG_DEP_MAIN_BGM_CPY = 2u,
    FHG_DEP_SUB_AND_CASCADE_CH2 = 4u
};

struct fhg_request {
    uint32_t channel;                /* 0 or 1; ch2 has a different cascade. */
    uint32_t native_width, native_height;
    uint32_t visible_width, visible_height;
    uint32_t capacity_width, capacity_height; /* both zero => current size */
    int32_t coefficient;             /* explicit 0..15, or explicit INHERIT */
};

struct fhg_plan {
    struct fhg_request request;
    uint32_t surface_width, surface_height;    /* scaler/encoder input: align16 */
    uint32_t capacity_width, capacity_height;  /* SET_MEMORY envelope: align16 */
    uint32_t allocation_width, allocation_height; /* internal layout: align32 */
    uint32_t scaler_step_x, scaler_step_y;     /* exact stock 26-bit operands */
    uint32_t right_crop_pixels, bottom_crop_pixels; /* SPS trims, NOT VPU crop */
    uint32_t media_source_id;
    uint32_t dependencies;
    uint32_t needs_owner_pacing_restore;
};

struct fhg_memory {
    uint32_t physical, user_va32, bytes;
};

struct fhg_error {
    int status;
    enum fhg_stage stage;
    uint32_t ioctl_request;
    uint32_t driver_result;          /* preserves raw 0x8009xxxx errors */
    int system_errno;               /* errno only when syscall failed */
    int callback_status;
};

/* OWNER CONTRACT (not a claim that the kernel provides these guarantees):
 * enter() returns zero with the owner's EXCLUSIVE configuration lock held.
 * It must verify the exact supported module ABI, initialized native VI with no
 * private crop override, no changing system memory/format/BGM settings, and
 * initialized register defaults (including nonzero ch2 geometry for ch1), and
 * the relevant dependency group stopped/drained. For CONFIGURE_NEW it must
 * also prove there is NO previously registered channel allocation.
 * Merely observing "not streaming" is NOT sufficient for reallocation.
 * On enter failure it leaves no lock held. leave() only unlocks: never starts
 * capture or frees memory. Callback storage/fd must remain alive for the call.
 *
 * control(): zero means syscall returned; store its raw driver value in
 * *driver_result, even if it is a negative signed vendor error. Nonzero means
 * transport failure; store errno (or zero if unavailable) in *system_errno.
 * No retries. All callbacks must be present. No callback may longjmp/cancel
 * across this transaction. The caller owns concurrency for the session too.
 */
struct fhg_ops {
    void *context;
    int (*enter)(void *, const struct fhg_plan *, enum fhg_phase);
    void (*leave)(void *);
    int (*control)(void *, uint32_t, void *, size_t, uint32_t *, int *);
};

struct fhg_requirements {
    struct fhg_plan plan;
    uint32_t bytes;                  /* obtained from stock QUERY_MEMORY */
    uint32_t mean_fps_diagnostic;    /* NOT wall-clock FPS / RC FPS */
};

/* Initialize a NEW session with {0}, once. Never clear/reinitialize a dirty or
 * configured session to bypass recovery. This object is not persistent storage.
 */
struct fhg_session {
    enum fhg_state state;
    enum fhg_stage last_attempt;
    struct fhg_plan plan;
    struct fhg_memory retained_memory;
    uint32_t required_bytes;
    uint32_t geometry_driver_readback_matches;
    uint32_t coefficient_write_acknowledged; /* no coefficient GET exists here */
};

/* Pure calculation: no callbacks, no device access. */
int fhg_make_plan(const struct fhg_request *, struct fhg_plan *);
/* Read-only device query under the owner guard. Re-query occurs during apply. */
int fhg_query_requirements(const struct fhg_ops *, const struct fhg_request *,
                           struct fhg_requirements *, struct fhg_error *);
/* Startup-only configuration. Does NOT open/enable/bind/start a channel.
 * From BEFORE first SET_MEMORY onward, failure retains memory and requires
 * owner recovery. Success still retains memory and does not authorize streaming.
 */
int fhg_configure_new_channel(const struct fhg_ops *, const struct fhg_request *,
                              const struct fhg_memory *, struct fhg_session *,
                              struct fhg_error *);
size_t fhg_wire_size(uint32_t request); /* zero for requests outside this subset */
const char *fhg_status_string(int status);

#ifdef __cplusplus
}
#endif
#endif
