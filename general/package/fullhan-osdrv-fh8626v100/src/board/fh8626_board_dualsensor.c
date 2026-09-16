#include "fh8626_board_dualsensor.h"

#include <errno.h>
#include <string.h>

/* Validated Apollo board mapping: target1/wide GPIO4=1 GPIO14=0;
 * target2/tele GPIO14=1 GPIO4=0. Use the recovered gpiowave8 API rather than
 * modifying a whole GPIO DATA register through /dev/mem. */
static int encode_raw(int g4, int g14)
{
    return (g4 ? (1u << 4) : 0u) | (g14 ? (1u << 14) : 0u);
}

int fh8626_dualsensor_board_open(struct fh8626_dualsensor_board *b, const char *gpio_dev)
{
    int rc;
    if(!b)return -EINVAL;
    memset(b,0,sizeof(*b));
    b->gpio.fd=-1;
    rc=fh8626_gpio_open(&b->gpio,gpio_dev);
    if(rc)return rc;
    b->opened=1;
    b->current_target=FH8626_LENS_UNKNOWN;
    (void)fh8626_dualsensor_get(b,NULL);
    return 0;
}

void fh8626_dualsensor_board_close(struct fh8626_dualsensor_board *b)
{
    if(!b)return;
    fh8626_gpio_close(&b->gpio);
    memset(b,0,sizeof(*b));
    b->gpio.fd=-1;
}

int fh8626_dualsensor_get(struct fh8626_dualsensor_board *b, uint32_t *raw)
{
    int g4=0,g14=0,rc;
    if(!b||!b->opened)return -ENODEV;
    rc=fh8626_gpio_get_value(&b->gpio,4,&g4);
    if(rc){b->current_target=FH8626_LENS_UNKNOWN;return rc;}
    rc=fh8626_gpio_get_value(&b->gpio,14,&g14);
    if(rc){b->current_target=FH8626_LENS_UNKNOWN;return rc;}
    b->gpio4=g4; b->gpio14=g14;
    if(raw)*raw=(uint32_t)encode_raw(g4,g14);
    if(g4==1&&g14==0)b->current_target=FH8626_LENS_WIDE;
    else if(g4==0&&g14==1)b->current_target=FH8626_LENS_TELE;
    else b->current_target=FH8626_LENS_UNKNOWN;
    return b->current_target;
}

int fh8626_dualsensor_select(struct fh8626_dualsensor_board *b, int target, uint32_t *before, uint32_t *after)
{
    int old4,old14,rc,r2,got;
    uint32_t oldraw,newraw=0;
    if(!b||!b->opened)return -ENODEV;
    if(target!=FH8626_LENS_WIDE&&target!=FH8626_LENS_TELE)return -EINVAL;
    b->last_rollback_error=0;
    got=fh8626_dualsensor_get(b,&oldraw);
    if(got<0)return got;
    if(before)*before=oldraw;
    if(got==target){if(after)*after=oldraw;return 0;}
    old4=b->gpio4; old14=b->gpio14;

    /* Exact D8308 board order. Do not reorder these writes: stock asserts the
     * newly selected sensor line first and only then deasserts the old line. */
    if(target==FH8626_LENS_WIDE){
        if(old4!=1){rc=fh8626_gpio_write_once(&b->gpio,4,1);if(rc)goto rollback;}
        if(old14!=0){rc=fh8626_gpio_write_once(&b->gpio,14,0);if(rc)goto rollback;}
    } else {
        if(old14!=1){rc=fh8626_gpio_write_once(&b->gpio,14,1);if(rc)goto rollback;}
        if(old4!=0){rc=fh8626_gpio_write_once(&b->gpio,4,0);if(rc)goto rollback;}
    }

    got=fh8626_dualsensor_get(b,&newraw);
    if(after)*after=newraw;
    if(got!=target){b->verify_failures++;rc=got<0?got:-EIO;goto rollback;}
    b->switches++;
    return 0;
rollback:
    /* Owner recovery policy, not a newly inferred stock rollback contract.
     * Undo in reverse order, including a failed write (possible side effects).
     * Attempt both pins even if the first undo fails, then verify actual bits. */
    b->rollback_attempts++;
    if(target==FH8626_LENS_WIDE){
        r2=fh8626_gpio_write_once(&b->gpio,14,old14);
        if(r2)b->last_rollback_error=r2;
        r2=fh8626_gpio_write_once(&b->gpio,4,old4);
    }else{
        r2=fh8626_gpio_write_once(&b->gpio,4,old4);
        if(r2)b->last_rollback_error=r2;
        r2=fh8626_gpio_write_once(&b->gpio,14,old14);
    }
    if(r2&&!b->last_rollback_error)b->last_rollback_error=r2;
    got=fh8626_dualsensor_get(b,&newraw);
    if(got<0||newraw!=oldraw){
        if(!b->last_rollback_error)b->last_rollback_error=got<0?got:-EIO;
        b->current_target=FH8626_LENS_UNKNOWN;
    }
    if(after)*after=got<0?UINT32_MAX:newraw;
    if(b->last_rollback_error)b->rollback_failures++;
    return rc;
}

const char *fh8626_dualsensor_name(int target)
{
    if(target==FH8626_LENS_WIDE)return "wide";
    if(target==FH8626_LENS_TELE)return "tele";
    return "unknown";
}
