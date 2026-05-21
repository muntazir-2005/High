// src/hook.cpp
// Ultimate Game Protection Hook for macOS
// Compile: clang++ -dynamiclib -o ultimate_hook.dylib hook.cpp -framework Security -framework CoreFoundation -framework SystemConfiguration -framework ApplicationServices -stdlib=libc++ -O3 -Wall -fvisibility=hidden

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
#include <random>
#include <vector>
#include <string>

// ========================== GLOBAL DYNAMIC SIGNATURE DATA ==========================
static char g_dynamic_signature_identifier[128] = {0};
static uint8_t g_fake_certificate_data[256] = {0};
static size_t g_fake_cert_size = 0;
static pthread_once_t g_sig_init_once = PTHREAD_ONCE_INIT;

static void generate_new_dynamic_signature() {
    FILE* urandom = fopen("/dev/urandom", "rb");
    if (urandom) {
        uint8_t rand_bytes[16];
        fread(rand_bytes, 1, 16, urandom);
        fclose(urandom);
        snprintf(g_dynamic_signature_identifier, sizeof(g_dynamic_signature_identifier),
                 "com.dynamic.hook.%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 rand_bytes[0], rand_bytes[1], rand_bytes[2], rand_bytes[3],
                 rand_bytes[4], rand_bytes[5], rand_bytes[6], rand_bytes[7],
                 rand_bytes[8], rand_bytes[9], rand_bytes[10], rand_bytes[11],
                 rand_bytes[12], rand_bytes[13], rand_bytes[14], rand_bytes[15]);
    } else {
        snprintf(g_dynamic_signature_identifier, sizeof(g_dynamic_signature_identifier), "dynamic.hook.%ld", time(NULL));
    }
    size_t cert_len = 128 + (rand() % 128);
    if (cert_len > sizeof(g_fake_certificate_data)) cert_len = sizeof(g_fake_certificate_data);
    g_fake_cert_size = cert_len;
    for (size_t i = 0; i < cert_len; i++) {
        g_fake_certificate_data[i] = rand() % 256;
    }
    fprintf(stderr, "[HOOK] New dynamic signature generated: %s\n", g_dynamic_signature_identifier);
}

static void init_dynamic_signature() {
    srand((unsigned)time(NULL));
    generate_new_dynamic_signature();
}

extern "C" void rotate_hook_signature(void) {
    fprintf(stderr, "[HOOK] Signature rotation triggered.\n");
    generate_new_dynamic_signature();
}

// ========================== HOOKED SECURITY FUNCTIONS ==========================
extern "C" {

OSStatus SecStaticCodeCheckValidity(SecStaticCodeRef code, SecCSFlags flags, SecRequirementRef requirement) {
    fprintf(stderr, "[HOOK] SecStaticCodeCheckValidity called - returning success.\n");
    return errSecSuccess;
}

OSStatus SecCodeCopySigningInformation(SecStaticCodeRef code, SecCSFlags flags, CFDictionaryRef *info) {
    fprintf(stderr, "[HOOK] SecCodeCopySigningInformation - creating fake dynamic info.\n");
    pthread_once(&g_sig_init_once, init_dynamic_signature);
    
    CFMutableDictionaryRef dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (!dict) return errSecInternalError;
    
    CFStringRef identifier = CFStringCreateWithCString(kCFAllocatorDefault, g_dynamic_signature_identifier, kCFStringEncodingUTF8);
    CFDictionarySetValue(dict, kSecCodeInfoIdentifier, identifier);
    CFRelease(identifier);
    
    char team_id[32];
    snprintf(team_id, sizeof(team_id), "TEAM%06X", rand() & 0xFFFFFF);
    CFStringRef team = CFStringCreateWithCString(kCFAllocatorDefault, team_id, kCFStringEncodingUTF8);
    CFDictionarySetValue(dict, kSecCodeInfoTeamIdentifier, team);
    CFRelease(team);
    
    CFDataRef certData = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, g_fake_certificate_data, g_fake_cert_size, kCFAllocatorNull);
    if (certData) {
        CFArrayRef certChain = CFArrayCreate(kCFAllocatorDefault, (const void**)&certData, 1, &kCFTypeArrayCallBacks);
        if (certChain) {
            CFDictionarySetValue(dict, kSecCodeInfoCertificates, certChain);
            CFRelease(certChain);
        }
        CFRelease(certData);
    }
    
    CFNumberRef format = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, (int[]){kSecCodeFormatMagic});
    CFDictionarySetValue(dict, kSecCodeInfoFormat, format);
    CFRelease(format);
    
    *info = dict;
    return errSecSuccess;
}

OSStatus SecCodeCheckValidity(SecCodeRef code, SecCSFlags flags, SecRequirementRef requirement) {
    fprintf(stderr, "[HOOK] SecCodeCheckValidity called - returning success.\n");
    return errSecSuccess;
}

SecStaticCodeRef g_fake_static_code = (SecStaticCodeRef)0xDEADBEEF;
OSStatus SecStaticCodeCreateWithPath(CFURLRef path, SecCSFlags flags, SecStaticCodeRef *code) {
    fprintf(stderr, "[HOOK] SecStaticCodeCreateWithPath returning fake code object.\n");
    *code = g_fake_static_code;
    return errSecSuccess;
}

void _exit(int status) {
    fprintf(stderr, "[HOOK] _exit(%d) blocked.\n", status);
    while (1) { pause(); }
}

int _kill(pid_t pid, int sig) {
    fprintf(stderr, "[HOOK] kill(%d,%d) blocked.\n", pid, sig);
    return 0;
}

void __assert_rtn(const char *file, int line, const char *func, const char *expr) {
    fprintf(stderr, "[HOOK] assert ignored at %s:%d\n", file, line);
}

void __stack_chk_fail(void) {
    fprintf(stderr, "[HOOK] stack_chk_fail ignored\n");
}

long syscall(long number, ...) {
    long forbidden[] = { 26, 31, 0 };
    for (int i = 0; forbidden[i]; ++i) {
        if (number == forbidden[i]) {
            fprintf(stderr, "[HOOK] syscall(%ld) blocked\n", number);
            return 0;
        }
    }
    auto original = (long(*)(long,...))dlsym(RTLD_NEXT, "syscall");
    return original ? original(number, (va_list)0) : -1;
}

int ptrace(int request, pid_t pid, caddr_t addr, int data) {
    fprintf(stderr, "[HOOK] ptrace() blocked\n");
    return 0;
}

int sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
    if (namelen >= 4 && name[0] == CTL_KERN && name[1] == KERN_PROC && name[2] == KERN_PROC_PID) {
        if (oldp && oldlenp) *(int*)oldp = 0, *oldlenp = sizeof(int);
        return 0;
    }
    auto original = (int(*)(int*,u_int,void*,size_t*,void*,size_t))dlsym(RTLD_NEXT, "sysctl");
    return original ? original(name, namelen, oldp, oldlenp, newp, newlen) : -1;
}

int __cxa_guard_acquire(void *guard) {
    *(volatile int*)guard = 1;
    return 1;
}
void __cxa_guard_release(void *guard) { }
void __cxa_guard_abort(void *guard) { }

void __cxa_throw(void *obj, void *tinfo, void (*dest)(void*)) {
    if (dest) dest(obj);
    fprintf(stderr, "[HOOK] __cxa_throw blocked\n");
}
void* __cxa_begin_catch(void *exc) { return exc; }
void __cxa_end_catch(void) { }
void __cxa_pure_virtual(void) { fprintf(stderr, "[HOOK] pure virtual ignored\n"); }
void __cxa_atexit(void (*func)(void*), void *arg, void *dso) { }

char* getenv(const char* name) {
    if (name && strcmp(name, "DYLD_INSERT_LIBRARIES") == 0) return nullptr;
    auto original = (char*(*)(const char*))dlsym(RTLD_NEXT, "getenv");
    return original ? original(name) : nullptr;
}

const char* dyld_get_image_name(uint32_t index) {
    auto original = (const char*(*)(uint32_t))dlsym(RTLD_NEXT, "dyld_get_image_name");
    const char* name = original ? original(index) : nullptr;
    if (name && strstr(name, "ultimate_hook.dylib")) return "";
    return name;
}

FILE* fopen(const char *path, const char *mode) {
    if (path && (strstr(path, "/proc/") || strstr(path, "/tmp/debug"))) return nullptr;
    auto original = (FILE*(*)(const char*,const char*))dlsym(RTLD_NEXT, "fopen");
    return original ? original(path, mode) : nullptr;
}

int open(const char *path, int flags, ...) {
    if (path && (strstr(path, "/proc/") || strstr(path, "/dev/tty"))) {
        errno = EACCES;
        return -1;
    }
    auto original = (int(*)(const char*,int,...))dlsym(RTLD_NEXT, "open");
    return original ? original(path, flags) : -1;
}

sighandler_t signal(int sig, sighandler_t handler) {
    if (sig == SIGABRT || sig == SIGSEGV || sig == SIGBUS) return SIG_IGN;
    auto original = (sighandler_t(*)(int,sighandler_t))dlsym(RTLD_NEXT, "signal");
    return original ? original(sig, handler) : SIG_ERR;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    if (addr->sa_family == AF_INET) {
        const struct sockaddr_in *in_addr = (const struct sockaddr_in *)addr;
        if ((ntohl(in_addr->sin_addr.s_addr) & 0xFF000000) == 0x7F000000) {
            fprintf(stderr, "[HOOK] connect to localhost blocked\n");
            errno = EACCES;
            return -1;
        }
    }
    auto original = (int(*)(int,const struct sockaddr*,socklen_t))dlsym(RTLD_NEXT, "connect");
    return original ? original(sockfd, addr, addrlen) : -1;
}

} // extern "C"

// ========================== INITIALIZATION ==========================
__attribute__((constructor))
static void init_hook() {
    pthread_once(&g_sig_init_once, init_dynamic_signature);
    unsetenv("DYLD_INSERT_LIBRARIES");
    fprintf(stderr, "[+] ULTIMATE HOOK LOADED (all protections active)\n");
}
