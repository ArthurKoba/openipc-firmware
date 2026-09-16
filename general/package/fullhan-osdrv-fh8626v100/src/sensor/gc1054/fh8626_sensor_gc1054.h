#ifndef FH8626_SENSOR_GC1054_H
#define FH8626_SENSOR_GC1054_H

#include <stdint.h>
#include <stddef.h>

#define FH_SENSOR_CB_SIZE 0x68u

struct fh_sensor_gc1054 {
    void *dl_mipi;
    void *dl_sensor;
    uint8_t *cb;
};

int fh_sensor_gc1054_open(struct fh_sensor_gc1054 *s, const char *mipi_so, const char *sensor_so);
void fh_sensor_gc1054_close(struct fh_sensor_gc1054 *s);
void *fh_sensor_gc1054_cb(const struct fh_sensor_gc1054 *s, unsigned off);
const char *fh_sensor_gc1054_name(const struct fh_sensor_gc1054 *s);
int fh_sensor_gc1054_init(struct fh_sensor_gc1054 *s);
int fh_sensor_gc1054_set_fmt(struct fh_sensor_gc1054 *s, uint32_t fmt);
int fh_sensor_gc1054_set_intt(struct fh_sensor_gc1054 *s, uint32_t intt);
int fh_sensor_gc1054_set_gain(struct fh_sensor_gc1054 *s, uint32_t gain);
int fh_sensor_gc1054_get_gain(struct fh_sensor_gc1054 *s, uint32_t *gain);
int fh_sensor_gc1054_get_intt(struct fh_sensor_gc1054 *s, uint32_t *intt);
/* Optional CAFC0 callback +5c. Absence and ignored return match stock. */
void fh_sensor_gc1054_awb_gain(void *opaque,uint32_t gain[3]);
/* Optional CB580 query +58, same persistent three-word buffer. */
void fh_sensor_gc1054_awb_query(void *opaque,uint32_t gain[3]);
int fh_sensor_gc1054_set_vts_multiplier(struct fh_sensor_gc1054 *s, uint32_t multiplier);
int fh_sensor_gc1054_get_vi_attr(struct fh_sensor_gc1054 *s, void *attr);
int fh_sensor_gc1054_write_reg(struct fh_sensor_gc1054 *s, uint32_t reg, uint32_t value);
int fh_sensor_gc1054_read_reg(struct fh_sensor_gc1054 *s, uint32_t reg, uint32_t *value);
void fh_sensor_gc1054_dump(const struct fh_sensor_gc1054 *s);

#endif
