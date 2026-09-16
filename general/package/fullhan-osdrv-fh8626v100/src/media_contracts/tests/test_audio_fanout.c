#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../audio/fh8626_audio_fanout.h"
int main(void){struct fh_audio_fanout f;struct fh_audio_cursor http,file,slow;struct fh_audio_frame a,b;fh_audio_fanout_init(&f,1);fh_audio_cursor_subscribe(&f,&http);fh_audio_cursor_subscribe(&f,&file);char x[]="abc";assert(fh_audio_fanout_publish(&f,10,160,1,x,3,NULL)==0);assert(fh_audio_cursor_read(&f,&http,&a)==1);assert(fh_audio_cursor_read(&f,&file,&b)==1);assert(a.size==b.size&&!memcmp(a.bytes,b.bytes,a.size));fh_audio_cursor_subscribe(&f,&slow);for(int i=0;i<10;i++){char q=(char)i;assert(fh_audio_fanout_publish(&f,20+i,80,1,&q,1,NULL)==0);}assert(fh_audio_cursor_read(&f,&slow,&a)==1);assert(slow.dropped_frames==2&&slow.samples_lost==160&&slow.discontinuities==1);assert(fh_audio_fanout_set_epoch(&f,2)==0);assert(fh_audio_cursor_read(&f,&file,&a)==0&&file.discontinuities==1);puts("test_audio_fanout: PASS");return 0;}
