#ifndef FH8626_GPIO_H
#define FH8626_GPIO_H

struct fh8626_gpio {
    int fd;
};

int fh8626_gpio_open(struct fh8626_gpio *g, const char *path);
void fh8626_gpio_close(struct fh8626_gpio *g);
int fh8626_gpio_request(struct fh8626_gpio *g, unsigned gpio);
int fh8626_gpio_release(struct fh8626_gpio *g, unsigned gpio);
int fh8626_gpio_set_direction(struct fh8626_gpio *g, unsigned gpio, int output);
int fh8626_gpio_set_value(struct fh8626_gpio *g, unsigned gpio, int value);
int fh8626_gpio_get_value(struct fh8626_gpio *g, unsigned gpio, int *value);
int fh8626_gpio_write_once(struct fh8626_gpio *g, int signed_gpio, int value);

#endif
