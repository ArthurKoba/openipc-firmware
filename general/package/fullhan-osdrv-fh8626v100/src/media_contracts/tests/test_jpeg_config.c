#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../jpeg/fh8626_jpeg_config.h"
int main(void){
 struct fh_jpeg_cfg_wire c; memset(&c,0,sizeof(c)); c.mode=2;c.width=640;c.height=360;c.src_fps_packed=(25u<<16)|1u;c.dst_fps_packed=(25u<<16)|1u;c.qp=20;c.min_qp=10;c.max_qp=50;c.rate_selector=9;
 assert(fh_jpeg_cfg_validate_sdk(&c)==0); c.qp=99; assert(fh_jpeg_cfg_validate_sdk(&c)<0); c.qp=20;
 assert(fh_jpeg_rate_selector_words[0]==16 && fh_jpeg_rate_selector_words[9]==368);
 struct fh_jpeg_drop_wire d; assert(fh_jpeg_drop_build_safe(&d,(25u<<16)|1u,(12u<<16)|1u,80,(6u<<16)|1u,1000,(5u<<16)|1u)==0);
 assert(d.validator_fps_packed==d.instant_rate_fps_packed);
 d.validator_fps_packed=(4u<<16)|1u; assert(fh_jpeg_drop_validate_driver(&d)==0); assert(fh_jpeg_drop_validate_sdk(&d)<0);
 puts("test_jpeg_config: PASS");return 0;}
