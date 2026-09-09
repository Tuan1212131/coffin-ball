// autocof.so: 注入后自扫EntityManager -> 50ms 遍历 type6(status==0) 写 open值
// 导出 fuck_you() 供注入器(dlsym)启动线程; 写值从 /data/local/tmp/coffin_val 读(默认0x12)
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

#define REG_LO 0x30000000UL
#define REG_HI 0x1000000000UL
#define ENT 0x4d0
static int g_open = 0x12;

typedef struct { uint64_t s,e; } R;
static R g_r[4096]; static int g_n=0;
static int inr(uint64_t a){ for(int i=0;i<g_n;i++) if(a>=g_r[i].s && a+8<=g_r[i].e) return 1; return 0; }
static void load_regs(void){
    g_n=0; FILE*f=fopen("/proc/self/maps","r"); char ln[512];
    while(f&&fgets(ln,sizeof(ln),f)&&g_n<4096){
        uint64_t a,b; char per[8],path[300]; path[0]=0;
        if(sscanf(ln,"%lx-%lx %7s %*x %*x:%*x %*d %299s",(unsigned long*)&a,(unsigned long*)&b,per,path)>=3){
            if(per[1]=='w'&&a>=REG_LO&&a<REG_HI){
                if(!path[0]||strstr(path,"/data/")||strstr(path,"/memfd")||strstr(path,"[anon:libc")||strstr(path,"[anon:scudo")){ g_r[g_n].s=a; g_r[g_n].e=b; g_n++; }
            }
        }
    }
    if(f)fclose(f);
}
static int rd32(uint64_t a,int*v){ if(!inr(a))return -1; *v=*(int*)a; return 0; }
static int rdu64(uint64_t a,uint64_t*v){ if(!inr(a))return -1; *v=*(uint64_t*)a; return 0; }
static int wr32(uint64_t a,int v){ if(!inr(a))return -1; *(int*)a=v; return 0; }
static int load_val(void){
    int fd=open("/data/local/tmp/coffin_val",O_RDONLY);
    if(fd<0) return 0;
    int v=0x12; if(read(fd,&v,4)==4) g_open=v; close(fd); return 1;
}
static int em_ok(uint64_t em){
    int cnt; if(rd32(em+0x960,&cnt)) return 0;
    if(cnt<=0||cnt>3000) return 0;
    uint64_t arr; if(rdu64(em+0x988,&arr)) return 0;
    if(!arr||!inr(arr)) return 0;
    for(int i=0;i<cnt&&i<400;i++){
        uint64_t E; if(rdu64(arr+i*8,&E)||!E||!inr(E)) continue;
        int sc; if(rd32(E+0x980,&sc)||sc<=0||sc>2048) continue;
        uint64_t sub; if(rdu64(E+0x988,&sub)||!sub||!inr(sub)) continue;
        int m=sc<80?sc:80;
        for(int j=0;j<m;j++){ int t; uint64_t sa=sub+j*0xc8;
            if(rd32(sa,&t)) continue; if(t==6) return 1; }
    }
    return 0;
}
static uint64_t find_em(void){
    for(int i=0;i<g_n;i++){
        uint64_t a=g_r[i].s;
        // 8步进扫, 预算保护
        for(uint64_t x=a; x+0x990<g_r[i].e && x<a+0x20000000; x+=8){
            if(em_ok(x)) return x;
        }
    }
    return 0;
}
static void* loop(void*arg){
    sleep(3); load_regs(); load_val();
    uint64_t em=find_em();
    fprintf(stderr,"[autocof] EM=%llx\n",(unsigned long long)em);
    int tick=0;
    while(1){
        if(!em){ if(tick%20==0){ em=find_em(); fprintf(stderr,"[autocof] retry EM\n");} }
        else {
            int cnt; if(rd32(em+0x960,&cnt)==0 && cnt>0 && cnt<=3000){
                uint64_t arr; if(rdu64(em+0x988,&arr)==0 && arr){
                    for(int i=0;i<cnt&&i<400;i++){
                        uint64_t E; if(rdu64(arr+i*8,&E)||!E||!inr(E)) continue;
                        int sc; if(rd32(E+0x980,&sc)||sc<=0||sc>2048) continue;
                        uint64_t sub; if(rdu64(E+0x988,&sub)||!sub||!inr(sub)) continue;
                        int m=sc<80?sc:80;
                        for(int j=0;j<m;j++){ uint64_t sa=sub+j*0xc8; int t;
                            if(rd32(sa,&t)||t!=6) continue; int st;
                            if(rd32(sa+0x18,&st)==0 && st==0){ wr32(sa+0x18,g_open); } }
                    }
                }
            }
        }
        if(tick%20==0) load_val();
        tick++; usleep(50000);
    }
    return NULL;
}
void fuck_you(void){ pthread_t t; pthread_create(&t,NULL,loop,NULL); pthread_detach(t); }
