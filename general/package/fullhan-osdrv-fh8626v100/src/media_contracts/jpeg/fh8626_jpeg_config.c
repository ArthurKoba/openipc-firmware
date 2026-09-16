#include "fh8626_jpeg_config.h"

#include <errno.h>
#include <string.h>

const uint32_t fh_jpeg_rate_selector_words[10] = {16,32,48,80,112,144,272,304,336,368};

static int fps_valid(uint32_t p)
{
    return (p & 0xffffU) != 0U && (p >> 16) != 0U;
}

int fh_jpeg_cfg_validate_driver(const struct fh_jpeg_cfg_wire *cfg)
{
    if (!cfg || (cfg->mode != 1U && cfg->mode != 2U) || !cfg->width || !cfg->height)
        return -EINVAL;
    if (!fps_valid(cfg->src_fps_packed) || !fps_valid(cfg->dst_fps_packed))
        return -EINVAL;
    if (cfg->qp > FH_JPEG_QP_MAX || cfg->min_qp > FH_JPEG_QP_MAX || cfg->max_qp > FH_JPEG_QP_MAX)
        return -EINVAL;
    return 0;
}

int fh_jpeg_cfg_validate_sdk(const struct fh_jpeg_cfg_wire *cfg)
{
    if (fh_jpeg_cfg_validate_driver(cfg))
        return -EINVAL;
    if (cfg->min_qp > cfg->max_qp || cfg->rate_selector >= 10U)
        return -EINVAL;
    return 0;
}

int fh_jpeg_drop_validate_driver(const struct fh_jpeg_drop_wire *drop)
{
    if (!drop || !fps_valid(drop->src_fps_packed) || !fps_valid(drop->fixed_dst_fps_packed))
        return -EINVAL;
    if (drop->instant_rate_enable && !fps_valid(drop->validator_fps_packed))
        return -EINVAL;
    return 0;
}

int fh_jpeg_drop_validate_sdk(const struct fh_jpeg_drop_wire *drop)
{
    if (fh_jpeg_drop_validate_driver(drop))
        return -EINVAL;
    if (drop->qp_threshold > FH_JPEG_QP_MAX || !fps_valid(drop->qp_fps_packed))
        return -EINVAL;
    if (drop->instant_rate_enable && (!fps_valid(drop->instant_rate_fps_packed) ||
        drop->validator_fps_packed != drop->instant_rate_fps_packed))
        return -EINVAL;
    return 0;
}

int fh_jpeg_drop_build_safe(struct fh_jpeg_drop_wire *out,
                            uint32_t src_fps_packed,
                            uint32_t fixed_dst_fps_packed,
                            uint32_t qp_threshold,
                            uint32_t qp_fps_packed,
                            uint32_t instant_rate_threshold,
                            uint32_t instant_rate_fps_packed)
{
    struct fh_jpeg_drop_wire tmp;
    if (!out)
        return -EINVAL;
    memset(&tmp, 0, sizeof(tmp));
    tmp.src_fps_packed = src_fps_packed;
    tmp.fixed_dst_fps_packed = fixed_dst_fps_packed;
    tmp.qp_threshold = qp_threshold;
    tmp.qp_fps_packed = qp_fps_packed;
    tmp.instant_rate_enable = instant_rate_threshold != 0U;
    tmp.validator_fps_packed = instant_rate_fps_packed;
    tmp.instant_rate_threshold = instant_rate_threshold;
    tmp.instant_rate_fps_packed = instant_rate_fps_packed;
    if (fh_jpeg_drop_validate_sdk(&tmp))
        return -EINVAL;
    *out = tmp;
    return 0;
}
