#define _GNU_SOURCE
#include "fh8626_log_rotation.h"
#include <assert.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <pthread.h>
static void *writer(void *p)
{
    (void)p;
    for(int i=0;i<1000;i++)fprintf(stderr,"thread message %d\n",i);
    return NULL;
}
int main(void)
{
    char dir[]="/tmp/fh8626-log-test.XXXXXX",path[PATH_MAX],backup[PATH_MAX],pending[PATH_MAX];
    struct stat st;pid_t pid;int status;
    assert(mkdtemp(dir));
    assert(snprintf(path,sizeof(path),"%s/owner.log",dir)>0);
    assert(snprintf(backup,sizeof(backup),"%s/owner.log.1",dir)>0);
    assert(snprintf(pending,sizeof(pending),"%s/owner.log.rotate-new",dir)>0);
    pid=fork();assert(pid>=0);
    if(!pid){
        struct fh_log_rotation r;
        int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0600);assert(fd>=0);
        assert(dup2(fd,1)==1&&dup2(fd,2)==2);close(fd);
        setvbuf(stdout,NULL,_IOLBF,0);
        assert(fh_log_rotation_init(&r,4096)==0&&r.enabled);
        assert(fcntl(1,F_GETFL)&O_APPEND);
        for(int i=0;i<900;i++)puts("test log bounded generation");
        assert(fh_log_rotation_tick(&r,1000)==0&&r.rotations==1);
        assert(stat(path,&st)==0&&st.st_size==0);
        puts("new stdout");fprintf(stderr,"new stderr\n");
        assert(fh_log_rotation_tick(&r,1001)==0&&r.rotations==1);
        for(int round=0;round<20;round++){
            for(int i=0;i<200;i++)puts("repeated log bounded generation");
            assert(fh_log_rotation_tick(&r,2000+(uint64_t)round*1000)==0);
        }
        assert(stat(backup,&st)==0&&st.st_size<8000);
        /* O_EXCL/O_NOFOLLOW: do not overwrite an unexpected pending path. */
        for(int i=0;i<200;i++)puts("fill before pending-path failure");
        assert(symlink(backup,pending)==0);
        assert(fh_log_rotation_tick(&r,30000)==-EEXIST);
        assert(unlink(pending)==0);
        assert(fh_log_rotation_tick(&r,31000)==0);
        pthread_t t;assert(!pthread_create(&t,NULL,writer,NULL));
        for(int i=0;i<100;i++)assert(fh_log_rotation_tick(&r,32000+(uint64_t)i*1000)==0);
        assert(!pthread_join(t,NULL));
        assert(fh_log_rotation_tick(&r,150000)==0);
        puts("final marker");fflush(stdout);
        assert(stat(path,&st)==0&&st.st_size<4096);
        /* Foreign path replacement must not be rotated under our descriptor. */
        assert(rename(path,pending)==0);
        fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);assert(fd>=0);close(fd);
        assert(fh_log_rotation_tick(&r,151000)==-ESTALE);
        _exit(0);
    }
    assert(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
    assert(unlink(path)==0&&unlink(backup)==0&&unlink(pending)==0&&rmdir(dir)==0);
    puts("Log rotation generations/append/concurrent stderr/stale-path guards PASS");
    return 0;
}
