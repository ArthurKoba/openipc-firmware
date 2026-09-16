#ifndef FH8626_SCENE_TRANSACTION_H
#define FH8626_SCENE_TRANSACTION_H
#include <errno.h>
enum fh_scene_field { FH_SCENE_IR, FH_SCENE_WHITE, FH_SCENE_IRCUT, FH_SCENE_PROFILE };
enum fh_scene_mode { FH_SCENE_DAY, FH_SCENE_NIGHT, FH_SCENE_WLIGHT };
struct fh_scene_state { int value[4]; }; /* -1=unknown, not assumed off/day */
struct fh_scene_report { unsigned failed_step, undo_attempts; int rollback_error; };
typedef int (*fh_scene_apply_fn)(void*,enum fh_scene_field,int);
/* Product policy, not a claimed stock hardware-atomic transaction. Caller
 * holds the owner control mutex. Callback success means command completion,
 * not mechanical IRcut readback. Include failed commands in undo, since they
 * may have side effects. Unknown prior states cannot be honestly restored. */
static inline int fh_scene_transition(struct fh_scene_state*s,int target,
                                      fh_scene_apply_fn apply,void*opaque,
                                      struct fh_scene_report*r)
{
    static const int plan[3][4][2]={
        {{FH_SCENE_IR,0},{FH_SCENE_WHITE,0},{FH_SCENE_IRCUT,0},{FH_SCENE_PROFILE,0}},
        {{FH_SCENE_WHITE,0},{FH_SCENE_PROFILE,1},{FH_SCENE_IRCUT,1},{FH_SCENE_IR,1}},
        {{FH_SCENE_IR,0},{FH_SCENE_IRCUT,0},{FH_SCENE_PROFILE,2},{FH_SCENE_WHITE,1}}
    };
    struct fh_scene_state old;
    int rc=0;
    unsigned i;
    if(!s||!apply||!r||target<0||target>2)return -EINVAL;
    *r=(struct fh_scene_report){0};old=*s;
    for(i=0;i<4;i++)if(old.value[i]<0||old.value[i]>(i==FH_SCENE_PROFILE?2:1))old.value[i]=-1;
    *s=old;
    for(i=0;i<4;i++){
        enum fh_scene_field field=(enum fh_scene_field)plan[target][i][0];
        int value=plan[target][i][1];
        rc=apply(opaque,field,value);
        s->value[field]=rc?-1:value;
        if(rc)break;
    }
    if(!rc)return 0;
    r->failed_step=i+1;
    for(int j=(int)i;j>=0;j--){
        enum fh_scene_field field=(enum fh_scene_field)plan[target][j][0];
        int undo;
        r->undo_attempts++;
        undo=old.value[field]<0?-ENODATA:apply(opaque,field,old.value[field]);
        s->value[field]=undo?-1:old.value[field];
        if(undo&&!r->rollback_error)r->rollback_error=undo;
    }
    if(r->rollback_error){
        /* A failed/unknown undo cannot claim the old scene. Attempt both
         * lamps off; never claim off when the command itself fails. */
        int ir=apply(opaque,FH_SCENE_IR,0),white=apply(opaque,FH_SCENE_WHITE,0);
        s->value[FH_SCENE_IR]=ir?-1:0;
        s->value[FH_SCENE_WHITE]=white?-1:0;
    }
    return rc;
}
#endif
