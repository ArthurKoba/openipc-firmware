#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/ioctl.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define PWM_DEVICE "/dev/fh_pwm"
#define PINCTRL_FILE "/proc/driver/pinctrl"
#define LOCK_FILE "/var/run/fh8626-ptz.lock"
#define DEFAULT_STATE_FILE "/etc/openipc/ptz.state"
#define PWM_IOCTL_MAGIC 'p'
#define DISABLE_PWM _IOWR(PWM_IOCTL_MAGIC,1,uint32_t)
#define SET_PWM_DUTY_CYCLE _IOWR(PWM_IOCTL_MAGIC,2,uint32_t)
#define ENABLE_MUL_PWM _IOWR(PWM_IOCTL_MAGIC,6,uint32_t)
#define WAIT_PWM_FINISHALL _IOWR(PWM_IOCTL_MAGIC,12,uint32_t)

struct fh_pwm_config { uint32_t period_ns,duty_ns,pulses,stop,delay_ns,phase_ns,percent,finish_once,finish_all; };
struct fh_pwm_status { uint32_t done_cnt,total_cnt,busy,error; };
struct fh_pwm_chip_data { int32_t id; struct fh_pwm_config config; struct fh_pwm_status status; };
struct axis { const char *name; int pwm[4],gpio[4],mux[4],min,max,home; uint32_t init_ns,normal_ns; int invert; };
struct position_state { int valid,calibrated,pan,tilt; };
typedef int (*ioctl_fn)(void *,unsigned long,void *);
typedef int (*save_fn)(void *,const struct position_state *);
struct backend { ioctl_fn call; void *opaque; };

static struct axis axes[2]={
 {"pan",{11,10,9,6},{51,50,6,3},{0,0,0,1},0,1028,514,7000000U,15000000U,1},
 {"tilt",{5,4,3,7},{2,1,0,7},{1,2,2,1},0,250,200,8000000U,25000000U,0}
};
static int pwm_fd=-1,lock_fd=-1,skip_hardware_setup;
static volatile sig_atomic_t stop_requested;
static struct backend backend;
static save_fn save_hook; static void *save_opaque;
_Static_assert(sizeof(struct fh_pwm_chip_data)==56,"FH PWM ABI size");

static int real_ioctl(void *o,unsigned long r,void *a){(void)o;return ioctl(pwm_fd,r,a);}
static int pwm_call(unsigned long r,void *a){return backend.call(backend.opaque,r,a);}
static int write_text(const char *p,const char *s){int f=open(p,O_WRONLY|O_CLOEXEC);ssize_t n;size_t z=strlen(s);if(f<0)return -1;n=write(f,s,z);if(close(f)||n!=(ssize_t)z){errno=EIO;return -1;}return 0;}
static int gpio_low_one(int g){char p[96],v[16];snprintf(p,sizeof(p),"/sys/class/gpio/GPIO%d",g);if(access(p,F_OK)){snprintf(v,sizeof(v),"%d\n",g);if(write_text("/sys/class/gpio/export",v)&&errno!=EBUSY)return -1;}snprintf(p,sizeof(p),"/sys/class/gpio/GPIO%d/direction",g);if(write_text(p,"out\n"))return -1;snprintf(p,sizeof(p),"/sys/class/gpio/GPIO%d/value",g);return write_text(p,"0\n");}
static int mux(const char *k,int n,int i){char s[64];snprintf(s,sizeof(s),"mux,%s%d,%s%d,%d\n",k,n,k,n,i);return write_text(PINCTRL_FILE,s);}
static int axis_gpio_low(const struct axis *a){int i,r=0;if(skip_hardware_setup)return 0;for(i=0;i<4;i++)if(gpio_low_one(a->gpio[i]))r=-1;for(i=0;i<4;i++)if(mux("GPIO",a->gpio[i],0))r=-1;return r;}
static int axis_pwm_mux(const struct axis *a){int i;if(skip_hardware_setup)return 0;for(i=0;i<4;i++)if(mux("PWM",a->pwm[i],a->mux[i]))return -1;return 0;}
static void axis_disable(const struct axis *a){struct fh_pwm_chip_data d;int i;if(!backend.call)return;memset(&d,0,sizeof(d));for(i=0;i<4;i++){d.id=a->pwm[i];(void)pwm_call(DISABLE_PWM,&d);}(void)axis_gpio_low(a);}
static void cleanup(void){if(pwm_fd>=0){axis_disable(&axes[0]);axis_disable(&axes[1]);close(pwm_fd);}pwm_fd=-1;if(lock_fd>=0)close(lock_fd);lock_fd=-1;}
static void on_signal(int s){(void)s;stop_requested=1;}
static int parse_int(const char *s,int *v){char *e;long n;errno=0;n=strtol(s,&e,10);if(errno||e==s||*e||n<INT32_MIN||n>INT32_MAX)return -1;*v=(int)n;return 0;}
static int parse_range(const char *s,struct axis *a){int lo,hi,h;char x;if(sscanf(s,"%d,%d,%d%c",&lo,&hi,&h,&x)!=3||lo<-8192||hi>8192||lo>=hi||h<lo||h>hi)return -1;a->min=lo;a->max=hi;a->home=h;return 0;}
static int env_value(const char *k,char *v,size_t z){char c[96];FILE *p;int st;size_t n;snprintf(c,sizeof(c),"fw_printenv -n %s 2>/dev/null",k);if(!(p=popen(c,"r")))return -1;if(!fgets(v,(int)z,p)){pclose(p);return -1;}st=pclose(p);if(st)return -1;n=strlen(v);while(n&&(v[n-1]=='\n'||v[n-1]=='\r'))v[--n]=0;return n?0:-1;}
static void apply_axis_inversion(struct axis *a){int v;if(!a->invert)return;v=a->pwm[1];a->pwm[1]=a->pwm[3];a->pwm[3]=v;v=a->mux[1];a->mux[1]=a->mux[3];a->mux[3]=v;}
static void load_board_config(void){char v[128];unsigned lo,hi,n;int x;if(!env_value("pan_range",v,sizeof(v)))parse_range(v,&axes[0]);if(!env_value("tilt_range",v,sizeof(v)))parse_range(v,&axes[1]);if(!env_value("pan_period",v,sizeof(v))&&sscanf(v,"%u,%u,%u",&lo,&hi,&n)==3&&lo>70&&lo<=1000000&&hi>=lo&&hi<=1000000&&n>=lo&&n<=hi){axes[0].init_ns=lo*1000U;axes[0].normal_ns=n*1000U;}if(!env_value("tilt_period",v,sizeof(v))&&sscanf(v,"%u,%u,%u",&lo,&hi,&n)==3&&lo>70&&lo<=1000000&&hi>=lo&&hi<=1000000&&n>=lo&&n<=hi){axes[1].init_ns=lo*1000U;axes[1].normal_ns=n*1000U;}if(!env_value("pan_invert",v,sizeof(v))||!env_value("pwm_invert",v,sizeof(v))){if(!parse_int(v,&x))axes[0].invert=!!x;}if(!env_value("tilt_invert",v,sizeof(v))&&!parse_int(v,&x))axes[1].invert=!!x;apply_axis_inversion(&axes[0]);apply_axis_inversion(&axes[1]);}
static const char *state_path(void){const char *p=getenv("PTZ_STATE_FILE");return p&&*p?p:DEFAULT_STATE_FILE;}
static int ensure_parent(const char *p){char d[256],*s;if(strlen(p)>=sizeof(d)){errno=ENAMETOOLONG;return -1;}strcpy(d,p);s=strrchr(d,'/');if(!s||s==d)return 0;*s=0;return mkdir(d,0755)&&errno!=EEXIST?-1:0;}
static int load_state(struct position_state *s){FILE *f=fopen(state_path(),"r");int v;memset(s,0,sizeof(*s));if(!f)return -1;if(fscanf(f,"version=%d\ncalibrated=%d\npan=%d\ntilt=%d",&v,&s->calibrated,&s->pan,&s->tilt)!=4||v!=1){fclose(f);return -1;}fclose(f);if(s->pan<axes[0].min||s->pan>axes[0].max||s->tilt<axes[1].min||s->tilt>axes[1].max)return -1;s->valid=1;return 0;}
static int save_file(const struct position_state *s){char t[320];FILE *f;const char *p=state_path();if(ensure_parent(p)||snprintf(t,sizeof(t),"%s.new",p)>=(int)sizeof(t)){errno=ENAMETOOLONG;return -1;}if(!(f=fopen(t,"w")))return -1;if(fprintf(f,"version=1\ncalibrated=%d\npan=%d\ntilt=%d\n",s->calibrated,s->pan,s->tilt)<0||fflush(f)||fsync(fileno(f))){fclose(f);return -1;}if(fclose(f))return -1;return rename(t,p);}
static int persist(const struct position_state *s){return save_hook?save_hook(save_opaque,s):save_file(s);}
static int invalidate_position(struct position_state *s){s->valid=s->calibrated=0;return persist(s);}
static uint32_t step_period(uint32_t timing,int step,int total){uint32_t t=timing-70000U;if(step<2||step>=total-2)return 4*t;if(step<4||step>=total-5)return 2*t;return t;}
static int configure_cycle(const struct axis *a,int dir,uint32_t p,int terminal){struct fh_pwm_chip_data d;uint32_t q=p/4,forward[4]={0,q,2*q,3*q},reverse[4]={3*q,2*q,q,0},*phase=dir>0?forward:reverse,duty=p/2;int i;for(i=0;i<4;i++){memset(&d,0,sizeof(d));d.id=a->pwm[i];d.config.period_ns=p;d.config.duty_ns=duty;d.config.pulses=1;d.config.stop=!terminal&&phase[i]+duty>p?3U:0U;d.config.phase_ns=phase[i];d.config.finish_all=i==0;if(pwm_call(SET_PWM_DUTY_CYCLE,&d))return -1;}return 0;}
static int run_axis_steps_at(const struct axis *a,int signed_steps,uint32_t timing){uint32_t mask=0,wait;int dir,total,step=0,i;if(!signed_steps)return 0;if(signed_steps<-8192||signed_steps>8192){errno=ERANGE;return -1;}dir=signed_steps>0?1:-1;total=signed_steps>0?signed_steps:-signed_steps;if(axis_gpio_low(a)||axis_pwm_mux(a))goto fail;for(i=0;i<4;i++)mask|=1U<<a->pwm[i];wait=(uint32_t)a->pwm[0];for(step=0;step<total;step++){uint32_t p=step_period(timing,step,total);if(stop_requested){errno=EINTR;goto fail;}if(configure_cycle(a,dir,p,step==total-1)||pwm_call(ENABLE_MUL_PWM,&mask)||pwm_call(WAIT_PWM_FINISHALL,&wait)<0)goto fail;}axis_disable(a);return 0;fail:fprintf(stderr,"%s failed after %d/%d cycles: %s\n",a->name,step,total,strerror(errno));axis_disable(a);return -1;}
static int run_axis_steps(const struct axis *a,int n){return run_axis_steps_at(a,n,a->normal_ns);}
static int run_axes_stock(const int first[2],const int second[2],const uint32_t timing[2],const char *name){int i;for(i=0;i<2;i++){if(run_axis_steps_at(&axes[i],first[i],timing[i]))return -1;if(second[i]&&run_axis_steps_at(&axes[i],second[i],timing[i]))return -1;printf("%s %s complete\n",name,axes[i].name);}return 0;}
static int coordinated_move(struct position_state *s,int pan,int tilt){int d[2],zero[2]={0,0};uint32_t t[2]={axes[0].normal_ns,axes[1].normal_ns};if(!s->valid||!s->calibrated){errno=EBUSY;fprintf(stderr,"position unknown or initialization busy\n");return -1;}if(pan<axes[0].min||pan>axes[0].max||tilt<axes[1].min||tilt>axes[1].max){errno=ERANGE;return -1;}d[0]=s->pan-pan;d[1]=s->tilt-tilt;if(!d[0]&&!d[1])return 0;if(invalidate_position(s))return -1;if(run_axes_stock(d,zero,t,"goto"))return -1;s->pan=pan;s->tilt=tilt;s->valid=s->calibrated=1;return persist(s);}
static int calibrate_type2(struct position_state *s){int rp=axes[0].home,rt=axes[1].home,first[2]={axes[0].max-axes[0].min,axes[1].max-axes[1].min},second[2]={-first[0],-first[1]},restore[2],zero[2]={0,0};uint32_t init[2]={axes[0].init_ns,axes[1].init_ns},normal[2]={axes[0].normal_ns,axes[1].normal_ns};if(s->valid&&s->calibrated){rp=s->pan;rt=s->tilt;}if(invalidate_position(s))return -1;printf("startup stock-unbound type=2 pan=(+%d,-%d) tilt=(+%d,-%d)\n",first[0],first[0],first[1],first[1]);if(run_axes_stock(first,second,init,"type2"))return -1;s->pan=axes[0].max;s->tilt=axes[1].max;printf("type2 reference endpoints pan=%d tilt=%d\n",s->pan,s->tilt);restore[0]=s->pan-rp;restore[1]=s->tilt-rt;if(run_axes_stock(restore,zero,normal,"return"))return -1;s->pan=rp;s->tilt=rt;s->valid=s->calibrated=1;printf("startup return complete at pan=%d tilt=%d\n",rp,rt);return persist(s);}
static int acquire(void){lock_fd=open(LOCK_FILE,O_RDWR|O_CREAT|O_CLOEXEC,0644);if(lock_fd<0||flock(lock_fd,LOCK_EX|LOCK_NB)){errno=EBUSY;fprintf(stderr,"PTZ owner is busy or initializing\n");return -1;}pwm_fd=open(PWM_DEVICE,O_RDWR|O_CLOEXEC);if(pwm_fd<0)return -1;backend=(struct backend){real_ioctl,NULL};return 0;}
static int busy(void){int f=open(LOCK_FILE,O_RDWR|O_CREAT|O_CLOEXEC,0644),b;if(f<0)return 0;b=flock(f,LOCK_EX|LOCK_NB)&&errno==EWOULDBLOCK;close(f);return b;}
static void usage(const char *p){fprintf(stderr,"usage: %s status|startup|calibrate|home|raw AXIS STEPS|move PAN TILT|goto PAN TILT\n",p);}
#ifndef FH8626_PTZ_TEST
static void enable_motor_realtime(void)
{
    struct sched_param param;

    memset(&param,0,sizeof(param));
    param.sched_priority=20;
    if(sched_setscheduler(0,SCHED_FIFO,&param))
        fprintf(stderr,"warning: cannot enable PTZ real-time scheduling: %s\n",strerror(errno));
}

int main(int ac,char **av){struct position_state s;int a,b,r=1;load_board_config();(void)load_state(&s);if(ac==2&&!strcmp(av[1],"status")){int q=busy();printf("busy=%d calibrated=%d pan=%d range=%d,%d,%d tilt=%d range=%d,%d,%d state=%s\n",q,!q&&s.valid&&s.calibrated,s.pan,axes[0].min,axes[0].max,axes[0].home,s.tilt,axes[1].min,axes[1].max,axes[1].home,state_path());return q?2:0;}enable_motor_realtime();atexit(cleanup);signal(SIGINT,on_signal);signal(SIGTERM,on_signal);signal(SIGHUP,on_signal);if(acquire())return errno==EBUSY?2:1;if(ac==4&&!strcmp(av[1],"raw")&&!parse_int(av[3],&a)){const struct axis *axis=!strcmp(av[2],"pan")?&axes[0]:!strcmp(av[2],"tilt")?&axes[1]:NULL;if(!axis)usage(av[0]);else if(!invalidate_position(&s))r=!!run_axis_steps(axis,a);}else if(ac==4&&!strcmp(av[1],"move")&&!parse_int(av[2],&a)&&!parse_int(av[3],&b))r=!!coordinated_move(&s,s.pan+a,s.tilt+b);else if(ac==4&&!strcmp(av[1],"goto")&&!parse_int(av[2],&a)&&!parse_int(av[3],&b))r=!!coordinated_move(&s,a,b);else if(ac==2&&!strcmp(av[1],"home"))r=!!coordinated_move(&s,axes[0].home,axes[1].home);else if(ac==2&&(!strcmp(av[1],"startup")||!strcmp(av[1],"calibrate")))r=!!calibrate_type2(&s);else usage(av[0]);return r;}
#endif
