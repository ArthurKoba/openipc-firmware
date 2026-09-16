#define _GNU_SOURCE
#include "fh8626_nr3d_kernel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
struct fake { uint32_t mode; };
static int io(void *o,unsigned long req,void *arg){struct fake*f=o;(void)req;((struct fh_nr3d_driver_config*)arg)->mode=f->mode;return 0;}
int main(void){char path[]="/tmp/fh_nr3d_test_XXXXXX";int fd=mkstemp(path);struct fake f={0};struct fh_nr3d_kernel k={io,&f,path};struct fh_nr3d_driver_config c;char b[32]={0};FILE *p;assert(fd>=0);close(fd);
f.mode=1;assert(!fh_nr3d_kernel_set_verified(&k,1,&c));p=fopen(path,"r");assert(p);assert(fgets(b,sizeof(b),p));fclose(p);assert(!strcmp(b,"nr3d_on\n"));
f.mode=0;assert(!fh_nr3d_kernel_set_verified(&k,0,&c));memset(b,0,sizeof(b));p=fopen(path,"r");assert(p);assert(fgets(b,sizeof(b),p));fclose(p);assert(!strcmp(b,"nr3d_off\n"));
f.mode=1;assert(fh_nr3d_kernel_set_verified(&k,0,&c)!=0);unlink(path);puts("NR3D proc token + readback fence: PASS");return 0;}
