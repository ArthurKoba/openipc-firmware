#include "fh8626_scene_transaction.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct fixture {struct fh_scene_state hw;unsigned calls,failmask;int fields[16],values[16];};
static int apply(void*p,enum fh_scene_field f,int value)
{
    struct fixture*x=p;assert(x->calls<16);x->fields[x->calls]=f;x->values[x->calls]=value;
    x->calls++;x->hw.value[f]=value; /* side effect even if error is returned */
    return x->failmask&(1u<<x->calls)?-EIO:0;
}
int main(void)
{
    const struct fh_scene_state old[3]={{{0,0,0,0}},{{1,0,1,1}},{{0,1,0,2}}};
    for(int from=0;from<3;from++)for(int to=0;to<3;to++){
        struct fixture success={0};struct fh_scene_state s=old[from];struct fh_scene_report r;
        success.hw=s;assert(!fh_scene_transition(&s,to,apply,&success,&r));
        assert(!memcmp(&s,&old[to],sizeof(s))&&!memcmp(&s,&success.hw,sizeof(s)));
        assert(success.calls==4&&!r.failed_step&&!r.undo_attempts&&!r.rollback_error);
        for(unsigned failed=1;failed<=4;failed++){
            struct fixture x={0};s=old[from];x.hw=s;x.failmask=1u<<failed;
            assert(fh_scene_transition(&s,to,apply,&x,&r)==-EIO);
            assert(!memcmp(&s,&old[from],sizeof(s))&&!memcmp(&s,&x.hw,sizeof(s)));
            assert(r.failed_step==failed&&r.undo_attempts==failed&&!r.rollback_error);
            assert(x.calls==2*failed);
            for(unsigned j=0;j<failed;j++){
                assert(x.fields[failed+j]==success.fields[failed-1-j]);
                assert(x.values[failed+j]==old[from].value[x.fields[failed+j]]);
            }
            for(unsigned undo=1;undo<=failed;undo++){
                memset(&x,0,sizeof(x));s=old[from];x.hw=s;
                x.failmask=(1u<<failed)|(1u<<(failed+undo));
                assert(fh_scene_transition(&s,to,apply,&x,&r)==-EIO);
                assert(r.rollback_error==-EIO&&r.undo_attempts==failed);
                assert(x.calls==2*failed+2); /* all undos plus both safe-off attempts */
                assert(s.value[FH_SCENE_IR]==0&&s.value[FH_SCENE_WHITE]==0);
            }
        }
    }
    {
        struct fh_scene_state s={{-1,-1,-1,0}};struct fh_scene_report r;struct fixture x={0};
        x.hw=s;x.failmask=1u<<1;
        assert(fh_scene_transition(&s,FH_SCENE_DAY,apply,&x,&r)==-EIO);
        assert(r.rollback_error==-ENODATA&&x.calls==3); /* no invented unknown undo */
        assert(s.value[FH_SCENE_IRCUT]==-1&&s.value[FH_SCENE_IR]==0&&s.value[FH_SCENE_WHITE]==0);
        s=(struct fh_scene_state){{-1,-1,-1,0}};memset(&x,0,sizeof(x));x.hw=s;
        x.failmask=(1u<<1)|(1u<<2)|(1u<<3);
        assert(fh_scene_transition(&s,FH_SCENE_DAY,apply,&x,&r)==-EIO);
        assert(s.value[FH_SCENE_IR]==-1&&s.value[FH_SCENE_WHITE]==-1);
    }
    puts("Scene transition/all-step undo/unknown-state/failsafe errors: PASS");return 0;
}
