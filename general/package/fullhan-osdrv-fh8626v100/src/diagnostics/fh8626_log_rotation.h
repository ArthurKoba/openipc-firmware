#ifndef FH8626_LOG_ROTATION_H
#define FH8626_LOG_ROTATION_H
/* Product logging policy, not an ISP algorithm. For combined regular-file
 * stdout/stderr only. Two generations; threshold checked once a second by
 * the owner loop. A generation may exceed the threshold by one check interval
 * (including startup/blocking calls), not a strict per-write byte quota. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <limits.h>
struct fh_log_rotation {
    char path[PATH_MAX], backup[PATH_MAX], pending[PATH_MAX];
    off_t limit;
    uint64_t next_ms;
    unsigned rotations, failures;
    int enabled;
};
static int fh_log_same(const struct stat *a,const struct stat *b)
{ return a->st_dev==b->st_dev&&a->st_ino==b->st_ino; }
static int fh_log_rotation_init(struct fh_log_rotation *r,off_t limit)
{
    struct stat out,err,path;
    ssize_t len;int flags;
    memset(r,0,sizeof(*r));
    if(limit<4096)return -EINVAL;
    if(fstat(STDOUT_FILENO,&out)||fstat(STDERR_FILENO,&err))return -errno;
    if(!S_ISREG(out.st_mode)||!fh_log_same(&out,&err))return 0;
    len=readlink("/proc/self/fd/1",r->path,sizeof(r->path)-16);
    if(len<0)return -errno;
    if(len==0||len>=(ssize_t)sizeof(r->path)-16)return -ENAMETOOLONG;
    r->path[len]=0;
    if(r->path[0]!='/'||lstat(r->path,&path)||!S_ISREG(path.st_mode)||
       !fh_log_same(&out,&path))return -ESTALE;
    memcpy(r->backup,r->path,(size_t)len);memcpy(r->backup+len,".1",3);
    memcpy(r->pending,r->path,(size_t)len);memcpy(r->pending+len,".rotate-new",12);
    flags=fcntl(STDOUT_FILENO,F_GETFL);
    if(flags<0||fcntl(STDOUT_FILENO,F_SETFL,flags|O_APPEND)<0)return -errno;
    flags=fcntl(STDERR_FILENO,F_GETFL);
    if(flags<0||fcntl(STDERR_FILENO,F_SETFL,flags|O_APPEND)<0)return -errno;
    r->limit=limit;r->enabled=1;return 0;
}
static int fh_log_rotation_tick(struct fh_log_rotation *r,uint64_t now)
{
    struct stat out,err,path;int fd=-1,rc=0;
    if(!r->enabled)return 0;
    if(now&&now<r->next_ms)return 0;
    r->next_ms=now+1000u;
    /* Same lock order for both stdio streams; no control or sensor lock. */
    flockfile(stdout);flockfile(stderr);
    if(fstat(1,&out)||fstat(2,&err)){rc=-errno;goto done;}
    if(!fh_log_same(&out,&err)||lstat(r->path,&path)||
       !S_ISREG(path.st_mode)||!fh_log_same(&out,&path)){rc=-ESTALE;goto done;}
    if(out.st_size<r->limit)goto done;
    if(fflush(stdout)||fflush(stderr)){rc=-errno;goto done;}
    fd=open(r->pending,O_WRONLY|O_CREAT|O_EXCL|O_APPEND|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0){rc=-errno;goto done;}
    if(rename(r->path,r->backup)){rc=-errno;unlink(r->pending);goto done;}
    if(rename(r->pending,r->path)){
        rc=-errno;(void)rename(r->backup,r->path);unlink(r->pending);goto done;
    }
    /* fd>=3 and valid; dup2 target descriptors already exist. */
    if(dup2(fd,STDOUT_FILENO)<0||dup2(fd,STDERR_FILENO)<0){rc=-errno;goto done;}
    clearerr(stdout);clearerr(stderr);r->rotations++;
done:
    if(fd>=0)close(fd);
    if(rc)r->failures++;
    funlockfile(stderr);funlockfile(stdout);
    return rc;
}
#endif
