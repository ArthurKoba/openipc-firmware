#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../h264/fh8626_h264_stream.h"

struct fake {
    struct fh_media_stream_query_wire q;
    int query_rc, release_rc, releases;
};
static int media_ioctl(void *p, unsigned long req, void *arg)
{
    struct fake *f=p;
    assert(req == FH_MEDIA_QUERY_STREAM);
    if (f->query_rc) return f->query_rc;
    *(struct fh_media_stream_query_wire *)arg = f->q;
    return 0;
}
static int pae_ioctl(void *p, unsigned long req, void *arg)
{
    struct fake *f=p;
    assert(req == FH_PAE_RELEASE_STREAM);
    assert(*(uint32_t *)arg == 0);
    f->releases++;
    return f->release_rc;
}
static void fill(struct fake *f, uint8_t *ring)
{
    memset(f,0,sizeof(*f));
    f->q.returned_type=4; f->q.h264.type=4; f->q.h264.channel=0;
    f->q.h264.entry_count=2; f->q.h264.occupied_extent=32;
    f->q.h264.pts_lo=0x89abcdefu; f->q.h264.pts_hi=0x01234567u;
    f->q.h264.entry[0].phys=0x1004; f->q.h264.entry[0].user=0x2004; f->q.h264.entry[0].len=3;
    f->q.h264.entry[1].phys=0x1010; f->q.h264.entry[1].user=0x2010; f->q.h264.entry[1].len=4;
    memcpy(ring+4,"ABC",3); memcpy(ring+16,"DEFG",4);
}
int main(void)
{
    uint8_t ring[128]={0}, outb[64]={0};
    struct fake f; struct fh_h264_stream_owner o={media_ioctl,pae_ioctl,&f,0,0,0};
    struct fh_h264_stream_map m={0x1000,0x2000,sizeof(ring),ring};
    struct fh_h264_au_snapshot s; memset(&s,0,sizeof(s)); s.bytes=outb; s.capacity=sizeof(outb);
    fill(&f,ring);
    assert(fh_h264_stream_acquire(&o,&m,&s)==0);
    assert(o.held && s.size==7 && !memcmp(outb,"ABCDEFG",7));
    assert(s.span[0].offset==0 && s.span[0].len==3 && s.span[1].offset==3 && s.span[1].len==4);
    assert(s.pts==0x0123456789abcdefULL);
    f.release_rc=-EIO; assert(fh_h264_stream_release(&o)==-EIO && o.held);
    f.release_rc=0; assert(fh_h264_stream_release(&o)==0 && !o.held);
    fill(&f,ring); f.q.h264.entry[1].user=0x2005;
    assert(fh_h264_stream_acquire(&o,&m,&s)<0);
    assert(!o.held && f.releases==1);
    puts("test_h264_stream: PASS");
    return 0;
}
