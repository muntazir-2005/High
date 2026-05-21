// hook.cpp - Fixed for modern macOS SDK (Xcode 16+)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <dlfcn.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <mach/mach.h>
#include <Security/Security.h>
#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <ApplicationServices/ApplicationServices.h>
#include <time.h>

// ========== Dynamic signature (rotates each session) ==========
static char g_dynamic_sig[128] = {0};
static uint8_t g_fake_cert[256] = {0};
static size_t g_fake_cert_size = 0;
static pthread_once_t sig_once = PTHREAD_ONCE_INIT;

static void gen_new_sig() {
    FILE* f = fopen("/dev/urandom", "rb");
    if (f) {
        uint8_t r[16];
        fread(r, 1, 16, f);
        fclose(f);
        snprintf(g_dynamic_sig, sizeof(g_dynamic_sig),
                 "com.dyn.hook.%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 r[0],r[1],r[2],r[3],r[4],r[5],r[6],r[7],r[8],r[9],r[10],r[11],r[12],r[13],r[14],r[15]);
    } else {
        snprintf(g_dynamic_sig, sizeof(g_dynamic_sig), "dyn.%ld", time(NULL));
    }
    g_fake_cert_size = 128 + (rand() % 128);
    for (size_t i=0; i<g_fake_cert_size; i++) g_fake_cert[i] = rand() % 256;
    fprintf(stderr, "[HOOK] New signature: %s\n", g_dynamic_sig);
}

static void init_sig() { srand((unsigned)time(NULL)); gen_new_sig(); }

extern "C" void rotate_hook_signature(void) { gen_new_sig(); }

// ========== HOOKED FUNCTIONS ==========
extern "C" {

OSStatus SecStaticCodeCheckValidity(SecStaticCodeRef c, SecCSFlags f, SecRequirementRef r) {
    fprintf(stderr, "[HOOK] SecStaticCodeCheckValidity -> ok\n");
    return errSecSuccess;
}

OSStatus SecCodeCopySigningInformation(SecStaticCodeRef c, SecCSFlags f, CFDictionaryRef *info) {
    fprintf(stderr, "[HOOK] SecCodeCopySigningInformation -> fake data\n");
    pthread_once(&sig_once, init_sig);
    CFMutableDictionaryRef dict = CFDictionaryCreateMutable(NULL, 0, NULL, NULL);
    if (!dict) return errSecInternalError;
    CFStringRef ident = CFStringCreateWithCString(NULL, g_dynamic_sig, kCFStringEncodingUTF8);
    CFDictionarySetValue(dict, kSecCodeInfoIdentifier, ident);
    CFRelease(ident);
    char team[32]; snprintf(team, sizeof(team), "TEAM%06X", rand()&0xFFFFFF);
    CFStringRef teamstr = CFStringCreateWithCString(NULL, team, kCFStringEncodingUTF8);
    CFDictionarySetValue(dict, kSecCodeInfoTeamIdentifier, teamstr);
    CFRelease(teamstr);
    CFDataRef certData = CFDataCreate(NULL, g_fake_cert, g_fake_cert_size);
    if (certData) {
        CFArrayRef chain = CFArrayCreate(NULL, (const void**)&certData, 1, NULL);
        if (chain) { CFDictionarySetValue(dict, kSecCodeInfoCertificates, chain); CFRelease(chain); }
        CFRelease(certData);
    }
    *info = dict;
    return errSecSuccess;
}

OSStatus SecCodeCheckValidity(SecCodeRef c, SecCSFlags f, SecRequirementRef r) {
    fprintf(stderr, "[HOOK] SecCodeCheckValidity -> ok\n");
    return errSecSuccess;
}

OSStatus SecStaticCodeCreateWithPath(CFURLRef p, SecCSFlags f, SecStaticCodeRef *c) {
    fprintf(stderr, "[HOOK] SecStaticCodeCreateWithPath -> fake\n");
    static SecStaticCodeRef fake = (SecStaticCodeRef)0xDEADBEEF;
    *c = fake;
    return errSecSuccess;
}

void _exit(int status) { fprintf(stderr, "[HOOK] _exit(%d) blocked\n"); while(1) pause(); }
int _kill(pid_t pid, int sig) { fprintf(stderr, "[HOOK] kill(%d,%d) blocked\n", pid, sig); return 0; }

// Correct signature for __assert_rtn in modern macOS SDK
void __assert_rtn(const char *func, const char *file, int line, const char *expr) {
    fprintf(stderr, "[HOOK] assert ignored at %s:%d in %s: %s\n", file, line, func, expr);
}

void __stack_chk_fail(void) { fprintf(stderr, "[HOOK] stack_chk_fail ignored\n"); }

// syscall returns int, not long
int syscall(int number, ...) {
    int forbidden[] = { 26, 31, 0 };
    for (int i = 0; forbidden[i]; ++i) {
        if (number == forbidden[i]) {
            fprintf(stderr, "[HOOK] syscall(%d) blocked\n", number);
            return 0;
        }
    }
    auto original = (int(*)(int,...))dlsym(RTLD_NEXT, "syscall");
    if (original) return original(number, (va_list)0);
    errno = ENOSYS;
    return -1;
}

int ptrace(int request, pid_t pid, caddr_t addr, int data) {
    fprintf(stderr, "[HOOK] ptrace blocked\n");
    return 0;
}

int sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
    if (namelen >= 4 && name[0] == CTL_KERN && name[1] == KERN_PROC && name[2] == KERN_PROC_PID) {
        if (oldp && oldlenp) { *(int*)oldp = 0; *oldlenp = sizeof(int); }
        return 0;
    }
    auto original = (int(*)(int*,u_int,void*,size_t*,void*,size_t))dlsym(RTLD_NEXT, "sysctl");
    return original ? original(name, namelen, oldp, oldlenp, newp, newlen) : -1;
}

int __cxa_guard_acquire(void *g) { *(volatile int*)g = 1; return 1; }
void __cxa_guard_release(void *g) {}
void __cxa_guard_abort(void *g) {}
void __cxa_throw(void *obj, void *tinfo, void (*dest)(void*)) { if (dest) dest(obj); fprintf(stderr, "[HOOK] __cxa_throw blocked\n"); }
void* __cxa_begin_catch(void *exc) { return exc; }
void __cxa_end_catch(void) {}
void __cxa_pure_virtual(void) { fprintf(stderr, "[HOOK] pure virtual ignored\n"); }
void __cxa_atexit(void (*func)(void*), void *arg, void *dso) {}

char* getenv(const char *name) {
    if (name && strcmp(name, "DYLD_INSERT_LIBRARIES")==0) return NULL;
    auto original = (char*(*)(const char*))dlsym(RTLD_NEXT, "getenv");
    return original ? original(name) : NULL;
}
const char* dyld_get_image_name(uint32_t idx) {
    auto original = (const char*(*)(uint32_t))dlsym(RTLD_NEXT, "dyld_get_image_name");
    const char *n = original ? original(idx) : NULL;
    if (n && strstr(n, "ultimate_hook.dylib")) return "";
    return n;
}

FILE* fopen(const char *path, const char *mode) {
    if (path && (strstr(path, "/proc/") || strstr(path, "/tmp/debug"))) return NULL;
    auto original = (FILE*(*)(const char*,const char*))dlsym(RTLD_NEXT, "fopen");
    return original ? original(path, mode) : NULL;
}
int open(const char *path, int flags, ...) {
    if (path && (strstr(path, "/proc/") || strstr(path, "/dev/tty"))) { errno = EACCES; return -1; }
    auto original = (int(*)(const char*,int,...))dlsym(RTLD_NEXT, "open");
    return original ? original(path, flags) : -1;
}

// Use correct signal handler type
typedef void (*sig_t)(int);
sig_t signal(int sig, sig_t handler) {
    if (sig == SIGABRT || sig == SIGSEGV || sig == SIGBUS) return (sig_t)SIG_IGN;
    auto original = (sig_t(*)(int, sig_t))dlsym(RTLD_NEXT, "signal");
    return original ? original(sig, handler) : SIG_ERR;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    if (addr->sa_family == AF_INET) {
        const struct sockaddr_in *in_addr = (const struct sockaddr_in *)addr;
        if ((ntohl(in_addr->sin_addr.s_addr) & 0xFF000000) == 0x7F000000) {
            fprintf(stderr, "[HOOK] connect to localhost blocked\n");
            errno = EACCES; return -1;
        }
    }
    auto original = (int(*)(int,const struct sockaddr*,socklen_t))dlsym(RTLD_NEXT, "connect");
    return original ? original(sockfd, addr, addrlen) : -1;
}

} // extern "C"

// ========== Constructor ==========
__attribute__((constructor))
static void init() {
    pthread_once(&sig_once, init_sig);
    unsetenv("DYLD_INSERT_LIBRARIES");
    fprintf(stderr, "[+] Ultimate Hook loaded (all protections active)\n");
}
