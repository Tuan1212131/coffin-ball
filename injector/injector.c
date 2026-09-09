/*
 * 直注注入器 for libnhook.so (唯雨Max 30.0)
 * 用法: ./injector <pid> <so_path>
 * 原理: ptrace attach -> 远程mmap -> 写入so路径 -> dlopen -> dlsym("fuck_you") -> call
 *
 * 编译(手机端Termux):
 *   pkg install clang
 *   clang -o injector injector.c -static
 *
 * 编译(NDK):
 *   $NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android33-clang -o injector injector.c -static
 *
 * 注意: 需要目标进程可被ptrace (同shared_uid或root)
 * 唯雨游戏使用 shared_uid=com.fanxing.czar, 同UID应用可直接ptrace无需root
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/mman.h>

// 远程进程中调用函数
static long remote_call(pid_t pid, unsigned long func_addr,
                        unsigned long *args, int num_args,
                        struct user_regs_struct *old_regs)
{
    struct user_regs_struct regs;
    memcpy(&regs, old_regs, sizeof(regs));

    for (int i = 0; i < num_args && i < 8; i++) {
        (&regs.regs[0])[i] = args[i];
    }

    regs.regs[30] = 0;  // LR = 0 (触发SIGSEGV捕获)
    regs.pc = func_addr;

    if (ptrace(PTRACE_SETREGS, pid, NULL, &regs) < 0) {
        perror("PTRACE_SETREGS");
        return -1;
    }

    if (ptrace(PTRACE_CONT, pid, NULL, NULL) < 0) {
        perror("PTRACE_CONT");
        return -1;
    }

    int status;
    waitpid(pid, &status, 0);

    struct user_regs_struct result_regs;
    if (ptrace(PTRACE_GETREGS, pid, NULL, &result_regs) < 0) {
        perror("PTRACE_GETREGS after call");
        return -1;
    }

    return result_regs.regs[0];
}

static int write_remote(pid_t pid, unsigned long addr, const void *data, size_t len)
{
    const unsigned long *ptr = (const unsigned long *)data;
    size_t i = 0;
    for (; i + sizeof(long) <= len; i += sizeof(long), ptr++) {
        if (ptrace(PTRACE_POKETEXT, pid, (void *)(addr + i), *ptr) < 0) {
            perror("PTRACE_POKETEXT");
            return -1;
        }
    }
    if (i < len) {
        unsigned long val = ptrace(PTRACE_PEEKTEXT, pid, (void *)(addr + i), NULL);
        unsigned char *dst = (unsigned char *)&val;
        const unsigned char *src = (const unsigned char *)data + i;
        memcpy(dst, src, len - i);
        if (ptrace(PTRACE_POKETEXT, pid, (void *)(addr + i), val) < 0) {
            perror("PTRACE_POKETEXT tail");
            return -1;
        }
    }
    return 0;
}

static unsigned long find_remote_func(pid_t pid, const char *lib_name, const char *func_name)
{
    char maps_path[64];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
    FILE *f = fopen(maps_path, "r");
    if (!f) { perror("open maps"); return 0; }

    unsigned long base = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, lib_name) && strstr(line, "r-xp")) {
            sscanf(line, "%lx-", &base);
            break;
        }
    }
    fclose(f);
    if (!base) { fprintf(stderr, "Cannot find %s\n", lib_name); return 0; }

    void *local_handle = dlopen(lib_name, RTLD_NOW | RTLD_GLOBAL);
    if (!local_handle) { fprintf(stderr, "dlopen %s: %s\n", lib_name, dlerror()); return 0; }
    void *local_func = dlsym(local_handle, func_name);
    if (!local_func) { fprintf(stderr, "dlsym %s: %s\n", func_name, dlerror()); dlclose(local_handle); return 0; }

    FILE *self_maps = fopen("/proc/self/maps", "r");
    unsigned long local_base = 0;
    while (fgets(line, sizeof(line), self_maps)) {
        if (strstr(line, lib_name) && strstr(line, "r-xp")) {
            sscanf(line, "%lx-", &local_base);
            break;
        }
    }
    fclose(self_maps);

    unsigned long offset = (unsigned long)local_func - local_base;
    dlclose(local_handle);
    printf("[*] %s!%s: offset=%lx remote=%lx\n", lib_name, func_name, offset, base + offset);
    return base + offset;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <pid> <so_path>\n", argv[0]);
        fprintf(stderr, "  pid: 目标游戏进程PID (com.pi.czrxdfirst)\n");
        fprintf(stderr, "  so_path: libnhook.so在设备上的绝对路径\n\n");
        fprintf(stderr, "示例: %s 24886 /data/local/tmp/libnhook.so\n", argv[0]);
        return 1;
    }

    pid_t pid = atoi(argv[1]);
    const char *so_path = argv[2];

    printf("=== 唯雨libnhook直注注入器 ===\n");
    printf("[*] 目标PID: %d\n", pid);
    printf("[*] SO路径: %s\n", so_path);

    if (access(so_path, F_OK) != 0) {
        fprintf(stderr, "[!] SO文件不存在: %s\n", so_path);
        return 1;
    }

    printf("[*] ptrace attach...\n");
    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) < 0) {
        perror("[!] ptrace ATTACH失败");
        fprintf(stderr, "    解决: root运行, 或与目标进程同shared_uid\n");
        return 1;
    }
    waitpid(pid, NULL, 0);
    printf("[+] attach成功\n");

    struct user_regs_struct old_regs;
    if (ptrace(PTRACE_GETREGS, pid, NULL, &old_regs) < 0) {
        perror("PTRACE_GETREGS");
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }

    unsigned long dlopen_addr = find_remote_func(pid, "libc.so", "dlopen");
    unsigned long dlsym_addr = find_remote_func(pid, "libc.so", "dlsym");
    unsigned long mmap_addr = find_remote_func(pid, "libc.so", "mmap");

    if (!dlopen_addr || !dlsym_addr || !mmap_addr) {
        fprintf(stderr, "[!] 无法找到远程函数\n");
        ptrace(PTRACE_SETREGS, pid, NULL, &old_regs);
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }

    printf("[*] 远程mmap分配内存...\n");
    unsigned long mmap_args[6] = {
        0, 4096, PROT_READ|PROT_WRITE|PROT_EXEC,
        MAP_PRIVATE|MAP_ANONYMOUS, 0xffffffff, 0
    };
    unsigned long remote_buf = remote_call(pid, mmap_addr, mmap_args, 6, &old_regs);
    if (remote_buf == 0 || remote_buf == (unsigned long)-1) {
        fprintf(stderr, "[!] mmap失败: %lx\n", remote_buf);
        ptrace(PTRACE_SETREGS, pid, NULL, &old_regs);
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }
    printf("[+] 远程缓冲区: 0x%lx\n", remote_buf);

    write_remote(pid, remote_buf, so_path, strlen(so_path) + 1);

    printf("[*] 远程dlopen(%s)...\n", so_path);
    unsigned long dlopen_args[2] = { remote_buf, RTLD_NOW | RTLD_GLOBAL };
    unsigned long handle = remote_call(pid, dlopen_addr, dlopen_args, 2, &old_regs);

    if (!handle) {
        fprintf(stderr, "[!] dlopen失败 handle=0\n");
        fprintf(stderr, "    可能: SO路径错误/架构不匹配/依赖缺失\n");
        ptrace(PTRACE_SETREGS, pid, NULL, &old_regs);
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }
    printf("[+] dlopen成功 handle=0x%lx\n", handle);

    const char *symbol = "fuck_you";
    write_remote(pid, remote_buf + 512, symbol, strlen(symbol) + 1);

    printf("[*] 远程dlsym(handle, \"fuck_you\")...\n");
    unsigned long dlsym_args[2] = { handle, remote_buf + 512 };
    unsigned long func_addr = remote_call(pid, dlsym_addr, dlsym_args, 2, &old_regs);

    if (!func_addr) {
        fprintf(stderr, "[!] dlsym失败, 找不到fuck_you\n");
        fprintf(stderr, "    需要唯雨Max 30.0的libnhook.so\n");
        ptrace(PTRACE_SETREGS, pid, NULL, &old_regs);
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }
    printf("[+] dlsym成功 fuck_you@0x%lx\n", func_addr);

    printf("[*] 调用fuck_you()启动辅助...\n");
    remote_call(pid, func_addr, NULL, 0, &old_regs);
    printf("[+] fuck_you调用完成, 辅助线程已启动\n");

    ptrace(PTRACE_SETREGS, pid, NULL, &old_regs);
    ptrace(PTRACE_DETACH, pid, NULL, NULL);

    printf("\n[+] 注入完成!\n");
    printf("[*] 等待几秒后查看游戏中是否出现悬浮辅助菜单\n");
    printf("[*] 辅助初始化有重试循环(最多20次*300ms)\n");

    return 0;
}
