#include "fh8626_board_dualsensor.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static int pins[15],writes,reads,fail_write,fail_read,ignore_write;
static int trace_pin[8],trace_value[8];
int fh8626_gpio_open(struct fh8626_gpio*g,const char*p){(void)p;g->fd=7;return 0;}
void fh8626_gpio_close(struct fh8626_gpio*g){g->fd=-1;}
int fh8626_gpio_get_value(struct fh8626_gpio*g,unsigned n,int*v)
{(void)g;if(++reads==fail_read)return -ENXIO;*v=pins[n];return 0;}
int fh8626_gpio_write_once(struct fh8626_gpio*g,int n,int v)
{
    (void)g;assert(writes<8);trace_pin[writes]=n;trace_value[writes]=v;writes++;
    if(writes!=ignore_write)pins[n]=v;
    return writes==fail_write?-EIO:0; /* failure may occur AFTER side effect */
}
static void init(struct fh8626_dualsensor_board*b,int old)
{
    memset(b,0,sizeof(*b));memset(pins,0,sizeof(pins));
    b->opened=1;pins[4]=old==FH8626_LENS_WIDE;pins[14]=old==FH8626_LENS_TELE;
    writes=reads=fail_write=fail_read=ignore_write=0;
}
int main(void)
{
    struct fh8626_dualsensor_board b;
    for(int target=1;target<=2;target++){
        int old=3-target;uint32_t before=0,after=0;
        int first=target==1?4:14,second=target==1?14:4;
        init(&b,old);assert(!fh8626_dualsensor_select(&b,target,&before,&after));
        assert(writes==2&&trace_pin[0]==first&&trace_pin[1]==second&&b.switches==1);
        for(int fault=1;fault<=4;fault++){
            init(&b,old);
            if(fault<=2)fail_write=fault;
            else if(fault==3)fail_read=3; /* first verification read after writes */
            else ignore_write=2; /* successful call but physical mismatch */
            assert(fh8626_dualsensor_select(&b,target,&before,&after)<0);
            assert(b.rollback_attempts==1&&!b.rollback_failures&&!b.last_rollback_error);
            assert(pins[4]==(old==1)&&pins[14]==(old==2)&&before==after);
            assert(trace_pin[writes-2]==second&&trace_pin[writes-1]==first);
            assert(b.current_target==old&&!b.switches);
        }
        init(&b,old);fail_read=3;fail_write=3;
        assert(fh8626_dualsensor_select(&b,target,NULL,NULL)==-ENXIO);
        assert(b.rollback_attempts==1&&b.rollback_failures==1&&b.last_rollback_error==-EIO);
        assert(writes==4); /* second undo not skipped */
        init(&b,old);fail_read=3;ignore_write=4;
        assert(fh8626_dualsensor_select(&b,target,NULL,NULL)==-ENXIO);
        assert(b.rollback_failures==1&&b.current_target==FH8626_LENS_UNKNOWN);
        init(&b,old);fail_read=1;
        assert(fh8626_dualsensor_select(&b,target,NULL,NULL)==-ENXIO);
        assert(!writes&&!b.rollback_attempts&&b.current_target==FH8626_LENS_UNKNOWN);
    }
    puts("Dual sensor reverse-order rollback/failure/readback: PASS");return 0;
}
