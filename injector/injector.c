/* arm64 适配版: 用 PTRACE_GETREGSET/SETREGSET + struct user_pt_regs
   用法: ./injector <pid> <so_path> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <elf.h>
#include <asm/ptrace.h>

#ifndef NT_PRSTATUS
#define NT_PRSTATUS 1
#endif

typedef struct user_pt_regs armregs;

static int regs_get(pid_t pid, armregs *r){
    struct iovec iov={r,sizeof(*r)};
    return ptrace(PTRACE_GETREGSET,pid,(void*)NT_PRSTATUS,&iov);
}
static int regs_set(pid_t pid, armregs *r){
    struct iovec iov={r,sizeof(*r)};
    return ptrace(PTRACE_SETREGSET,pid,(void*)NT_PRSTATUS,&iov);
}

static long remote_call(pid_t pid, unsigned long func_addr,
                        unsigned long *args, int num_args, armregs *old){
    armregs regs; memcpy(&regs,old,sizeof(regs));
    for (int i=0;i<num_args && i<8;i++) regs.regs[i]=args[i];
    regs.regs[30]=0; regs.pc=func_addr;
    if (regs_set(pid,&regs)<0){perror("SETREGS");return -1;}
    if (ptrace(PTRACE_CONT,pid,NULL,NULL)<0){perror("CONT");return -1;}
    int st; waitpid(pid,&st,0);
    armregs out; if (regs_get(pid,&out)<0){perror("GETREGS");return -1;}
    return out.regs[0];
}
static int write_remote(pid_t pid, unsigned long addr, const void *data, size_t len){
    const unsigned long *ptr=(const unsigned long*)data; size_t i=0;
    for (;i+sizeof(long)<=len;i+=sizeof(long),ptr++)
        if (ptrace(PTRACE_POKETEXT,pid,(void*)(addr+i),*ptr)<0){perror("POKE");return -1;}
    if (i<len){ unsigned long val=ptrace(PTRACE_PEEKTEXT,pid,(void*)(addr+i),NULL);
        memcpy((char*)&val,(const char*)data+i,len-i);
        if (ptrace(PTRACE_POKETEXT,pid,(void*)(addr+i),val)<0){perror("POKEt");return -1;} }
    return 0;
}
static unsigned long find_remote(pid_t pid, const char *lib, const char *sym){
    char mp[64]; snprintf(mp,sizeof(mp),"/proc/%d/maps",pid);
    FILE*f=fopen(mp,"r"); if(!f)return 0; unsigned long base=0; char line[512];
    while(fgets(line,sizeof(line),f)) if(strstr(line,lib)&&strstr(line,"r-xp")){sscanf(line,"%lx-",&base);break;}
    fclose(f); if(!base){fprintf(stderr,"no %s map\n",lib);return 0;}
    void*h=dlopen(lib,RTLD_NOW|RTLD_GLOBAL); if(!h){fprintf(stderr,"local dlopen %s: %s\n",lib,dlerror());return 0;}
    void*fs=dlsym(h,sym); unsigned long off=0;
    if(fs){ FILE*sf=fopen("/proc/self/maps","r"); unsigned long lb=0;
        while(sf&&fgets(line,sizeof(line),sf)) if(strstr(line,lib)&&strstr(line,"r-xp")){sscanf(line,"%lx-",&lb);break;}
        if(sf)fclose(sf); off=(unsigned long)fs-lb; }
    dlclose(h); if(!fs){fprintf(stderr,"no local sym %s\n",sym);return 0;}
    printf("[*] %s!%s off=%lx remote=%lx\n",lib,sym,off,base+off);
    return base+off;
}
int main(int argc,char**argv){
    if(argc<3){fprintf(stderr,"Usage: %s <pid> <so_path>\n",argv[0]);return 1;}
    pid_t pid=atoi(argv[1]); const char*so=argv[2];
    printf("=== injector(arm64) pid=%d so=%s ===\n",pid,so);
    if(access(so,F_OK)){fprintf(stderr,"no so\n");return 1;}
    if(ptrace(PTRACE_ATTACH,pid,NULL,NULL)<0){perror("ATTACH");return 1;}
    waitpid(pid,NULL,0); printf("[+] attached\n");
    armregs old; if(regs_get(pid,&old)<0){perror("GETREGS");ptrace(PTRACE_DETACH,pid,NULL,NULL);return 1;}
    unsigned long dl=find_remote(pid,"libdl.so","dlopen");
    if(!dl) dl=find_remote(pid,"libc.so","dlopen");
    unsigned long sy=find_remote(pid,"libdl.so","dlsym");
    if(!sy) sy=find_remote(pid,"libc.so","dlsym");
    unsigned long mm=find_remote(pid,"libc.so","mmap");
    if(!dl||!sy||!mm){fprintf(stderr,"cannot find remote funcs dl=%lx sy=%lx mm=%lx\n",dl,sy,mm);
        regs_set(pid,&old);ptrace(PTRACE_DETACH,pid,NULL,NULL);return 1;}
    unsigned long args[6]={0,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,(unsigned long)-1,0};
    unsigned long buf=remote_call(pid,mm,args,6,&old);
    if(!buf||buf==(unsigned long)-1){fprintf(stderr,"mmap fail\n");regs_set(pid,&old);ptrace(PTRACE_DETACH,pid,NULL,NULL);return 1;}
    printf("[+] buf=0x%lx\n",buf);
    write_remote(pid,buf,so,strlen(so)+1);
    unsigned long a2[2]={buf,RTLD_NOW|RTLD_GLOBAL};
    unsigned long h=remote_call(pid,dl,a2,2,&old);
    if(!h){fprintf(stderr,"dlopen fail\n");regs_set(pid,&old);ptrace(PTRACE_DETACH,pid,NULL,NULL);return 1;}
    printf("[+] dlopen ok handle=0x%lx\n",h);
    write_remote(pid,buf+512,"fuck_you",9);
    unsigned long a3[2]={h,buf+512};
    unsigned long fa=remote_call(pid,sy,a3,2,&old);
    if(!fa){fprintf(stderr,"dlsym fuck_you fail\n");regs_set(pid,&old);ptrace(PTRACE_DETACH,pid,NULL,NULL);return 1;}
    printf("[+] fuck_you@0x%lx\n",fa);
    remote_call(pid,fa,NULL,0,&old);
    regs_set(pid,&old); ptrace(PTRACE_DETACH,pid,NULL,NULL);
    printf("[+] injected & fuck_you called\n"); return 0;
}
