#include "fh8626_gpio.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Exact Apollo gpiowave8 ioctl family used by ut_gpio_* at A3E5C..A40F4. */
#define GW_GPIO_REQUEST       0xC0045808UL
#define GW_GPIO_RELEASE       0xC0045809UL
#define GW_GPIO_SET_DIR       0xC004580AUL
#define GW_GPIO_SET_OUT_VAL   0xC004580BUL
#define GW_GPIO_GET_IN_VAL    0xC004580CUL

static int valid_gpio(unsigned gpio) { return gpio <= 63u; }

int fh8626_gpio_open(struct fh8626_gpio *g, const char *path)
{
    if (!g) return -EINVAL;
    g->fd = -1;
    if (!path || !*path) path = "/dev/gpiowave8";
    g->fd = open(path, O_RDWR);
    return g->fd < 0 ? -errno : 0;
}

void fh8626_gpio_close(struct fh8626_gpio *g)
{
    if (!g) return;
    if (g->fd >= 0) close(g->fd);
    g->fd = -1;
}

int fh8626_gpio_request(struct fh8626_gpio *g, unsigned gpio)
{
    uint32_t v = gpio;
    if (!g || g->fd < 0 || !valid_gpio(gpio)) return -EINVAL;
    return ioctl(g->fd, GW_GPIO_REQUEST, &v) < 0 ? -errno : 0;
}

int fh8626_gpio_release(struct fh8626_gpio *g, unsigned gpio)
{
    uint32_t v = gpio;
    if (!g || g->fd < 0 || !valid_gpio(gpio)) return -EINVAL;
    return ioctl(g->fd, GW_GPIO_RELEASE, &v) < 0 ? -errno : 0;
}

int fh8626_gpio_set_direction(struct fh8626_gpio *g, unsigned gpio, int output)
{
    uint32_t v[2] = {gpio, output ? 1u : 0u};
    if (!g || g->fd < 0 || !valid_gpio(gpio)) return -EINVAL;
    return ioctl(g->fd, GW_GPIO_SET_DIR, v) < 0 ? -errno : 0;
}

int fh8626_gpio_set_value(struct fh8626_gpio *g, unsigned gpio, int value)
{
    uint32_t v[2] = {gpio, value ? 1u : 0u};
    if (!g || g->fd < 0 || !valid_gpio(gpio)) return -EINVAL;
    return ioctl(g->fd, GW_GPIO_SET_OUT_VAL, v) < 0 ? -errno : 0;
}

int fh8626_gpio_get_value(struct fh8626_gpio *g, unsigned gpio, int *value)
{
    uint32_t v[4] = {gpio, 0, 0, 0};
    if (!g || g->fd < 0 || !value || !valid_gpio(gpio)) return -EINVAL;
    if (ioctl(g->fd, GW_GPIO_GET_IN_VAL, v) < 0) return -errno;
    /* Apollo A403C loads the returned value from the word immediately after gpio. */
    *value = v[1] ? 1 : 0;
    return 0;
}

int fh8626_gpio_write_once(struct fh8626_gpio *g, int signed_gpio, int value)
{
    unsigned gpio;
    int physical = value ? 1 : 0;
    int rc, r2;

    if (!g) return -EINVAL;
    if (signed_gpio < 0) {
        if (signed_gpio == INT32_MIN) return -EINVAL;
        gpio = (unsigned)-signed_gpio;
        physical = 1 - physical;
    } else {
        gpio = (unsigned)signed_gpio;
    }
    if (!valid_gpio(gpio)) return -EINVAL;

    rc = fh8626_gpio_request(g, gpio);
    if (rc) return rc;
    rc = fh8626_gpio_set_direction(g, gpio, 1);
    if (!rc) rc = fh8626_gpio_set_value(g, gpio, physical);
    r2 = fh8626_gpio_release(g, gpio);
    return rc ? rc : r2;
}
