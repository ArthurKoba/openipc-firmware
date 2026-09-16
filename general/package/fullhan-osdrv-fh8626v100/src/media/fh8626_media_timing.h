#ifndef FH8626_MEDIA_TIMING_H
#define FH8626_MEDIA_TIMING_H
/* Current native-cadence owner policy, not the historical stock 1666/100
 * preset. VPU pacing, PAE configuration and RC/SPS must describe one rate. */
#define FH8626_OWNER_FPS_NUM 25u
#define FH8626_OWNER_FPS_DEN 1u
#define FH8626_OWNER_FPS_PACKED ((FH8626_OWNER_FPS_DEN << 16) | FH8626_OWNER_FPS_NUM)
#endif
