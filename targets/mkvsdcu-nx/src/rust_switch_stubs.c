/* Link compatibility for aarch64-unknown-linux-gnu Rust libraries against
 * Switch/newlib.
 */

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Winvalid-memory-model"
#endif

#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>

/* Bridge Linux errno access to newlib. */
int *__errno_location(void) {
    return __errno();
}

/* libgcc provides the actual unwinder on Switch; only stub backtrace helpers. */

/* Program-header enumeration is unavailable. */
int dl_iterate_phdr(void *callback, void *data) { return 0; }

/* No auxiliary-vector CPU features are exposed. */
unsigned long getauxval(unsigned long type) { return 0; }

/* Use newlib's memalign. */
int posix_memalign(void **memptr, size_t alignment, size_t size) {
    void *p = memalign(alignment, size);
    if (!p) return ENOMEM;
    *memptr = p;
    return 0;
}

/* Anonymous memory mapping over Horizon. newlib ships no <sys/mman.h> and there
 * is no raw mmap syscall, so back private/anonymous requests with page-aligned,
 * zeroed heap memory (Horizon page size = 0x1000). abseil's LowLevelAlloc and
 * Rust std only ever request MAP_ANONYMOUS|MAP_PRIVATE with fd == -1 and later
 * munmap the exact region, so a memalign/free pair is a faithful backing.
 * File-backed mappings (fd >= 0) are unsupported and fail. */
void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
    (void)addr; (void)prot; (void)flags; (void)off;
    if (len == 0 || fd >= 0) { errno = EINVAL; return (void *)-1; /* MAP_FAILED */ }
    void *p = memalign(0x1000, len);
    if (!p) { errno = ENOMEM; return (void *)-1; }
    memset(p, 0, len);
    return p;
}
int munmap(void *addr, size_t len) {
    (void)len;
    if (addr && addr != (void *)-1) free(addr);
    return 0;
}
void *mmap64(void *addr, size_t len, int prot, int flags, int fd, long long off) {
    return mmap(addr, len, prot, flags, fd, (off_t)off);
}

/* Linux syscalls are unsupported. */
long syscall(long num, ...) { return -1; }

int __xpg_strerror_r(int errnum, char *buf, size_t buflen) {
    if (buf && buflen > 0) {
        strncpy(buf, "error", buflen);
        buf[buflen - 1] = '\0';
    }
    return 0;
}

/* Bridge Linux file entrypoints to newlib. */
#include <fcntl.h>
int open64(const char *path, int flags, ...) {
    return open(path, flags);
}

#include <sys/stat.h>
#include <unistd.h>
int fstat64(int fd, void *buf) { return fstat(fd, (struct stat *)buf); }
int stat64(const char *path, void *buf) { return stat(path, (struct stat *)buf); }
long long lseek64(int fd, long long off, int whence) { return lseek(fd, (off_t)off, whence); }

/* Rust std requires writev on devkitA64. */
struct iovec {
    void *iov_base;
    size_t iov_len;
};

ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        ssize_t ret = write(fd, iov[i].iov_base, iov[i].iov_len);
        if (ret < 0) return -1;
        total += ret;
    }
    return total;
}

/* DRM syncobj compatibility symbol for builds without DRM. */
int vk_drm_syncobj_copy_payloads(void *device, unsigned int wait_count, const void *waits, unsigned int signal_count, const void *signals) {
    return 0; /* VK_SUCCESS */
}

/* ---------------------------------------------------------------------------
 * Rust std (aarch64-unknown-linux-gnu) glibc surface that Horizon/newlib lacks.
 * These are referenced by libstd but sit on cold paths a shader compiler never
 * takes (process spawning, signals, credentials). Benign definitions satisfy
 * the final NRO link; anything genuinely reachable bridges to newlib.
 * Enumerated from the Dawn+NVK+libnx link smoke test (switch/tests/link_smoke).
 * ------------------------------------------------------------------------- */

/* *at() base entrypoints newlib lacks: honor AT_FDCWD, else ENOSYS. */
int openat(int dirfd, const char *path, int flags, ...) {
    if (dirfd == AT_FDCWD) return open(path, flags);
    errno = ENOSYS; return -1;
}
int fstatat(int dirfd, const char *path, struct stat *buf, int flags) {
    (void)flags;
    if (dirfd == AT_FDCWD) return stat(path, buf);
    errno = ENOSYS; return -1;
}

/* 64-bit file entrypoints → the native-width equivalents above / in newlib. */
int   openat64(int dirfd, const char *path, int flags, ...) { return openat(dirfd, path, flags); }
int   lstat64(const char *path, void *buf)         { return lstat(path, (struct stat *)buf); }
int   fstatat64(int dirfd, const char *path, void *buf, int flags) { return fstatat(dirfd, path, (struct stat *)buf, flags); }
int   ftruncate64(int fd, long long len)           { return ftruncate(fd, (off_t)len); }
long long pread64(int fd, void *buf, size_t n, long long off) {
    if (lseek(fd, (off_t)off, SEEK_SET) < 0) return -1;
    return read(fd, buf, n);
}
long long pwrite64(int fd, const void *buf, size_t n, long long off) {
    if (lseek(fd, (off_t)off, SEEK_SET) < 0) return -1;
    return write(fd, buf, n);
}
struct dirent *readdir64(DIR *dirp) { return readdir(dirp); }

/* Vectored/positional I/O and directory helpers. */
ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        ssize_t r = read(fd, iov[i].iov_base, iov[i].iov_len);
        if (r < 0) return -1;
        total += r;
        if ((size_t)r < iov[i].iov_len) break;
    }
    return total;
}
ssize_t preadv(int fd, const struct iovec *iov, int iovcnt, long long off)  { (void)off; return readv(fd, iov, iovcnt); }
ssize_t pwritev(int fd, const struct iovec *iov, int iovcnt, long long off) { (void)off; return writev(fd, iov, iovcnt); }
int dirfd(DIR *dirp) { (void)dirp; return -1; }
DIR *fdopendir(int fd) { (void)fd; return 0; }

/* *at() metadata ops Horizon has no analog for. */
int fchmodat(int d, const char *p, unsigned m, int f) { (void)d;(void)p;(void)m;(void)f; errno = ENOSYS; return -1; }
int mkdirat(int d, const char *p, unsigned m)         { (void)d;(void)p;(void)m; errno = ENOSYS; return -1; }
int unlinkat(int d, const char *p, int f)             { (void)d;(void)p;(void)f; errno = ENOSYS; return -1; }
int renameat(int od, const char *op, int nd, const char *np) { (void)od;(void)op;(void)nd;(void)np; errno = ENOSYS; return -1; }
int linkat(int od, const char *op, int nd, const char *np, int f) { (void)od;(void)op;(void)nd;(void)np;(void)f; errno = ENOSYS; return -1; }
int futimens(int fd, const struct timespec times[2])  { (void)fd;(void)times; errno = ENOSYS; return -1; }
int utimensat(int d, const char *p, const struct timespec t[2], int f) { (void)d;(void)p;(void)t;(void)f; errno = ENOSYS; return -1; }
int mkfifo(const char *p, mode_t m)                   { (void)p;(void)m; errno = ENOSYS; return -1; }
int flock(int fd, int op)                             { (void)fd;(void)op; return 0; }
int fdatasync(int fd)                                 { (void)fd; return 0; }
int sendfile64(int o, int i, long long *off, size_t n){ (void)o;(void)i;(void)off;(void)n; errno = ENOSYS; return -1; }
long splice(int i, long long *io, int o, long long *oo, size_t n, unsigned f) { (void)i;(void)io;(void)o;(void)oo;(void)n;(void)f; errno = ENOSYS; return -1; }
int pipe2(int fds[2], int flags)                      { (void)fds;(void)flags; errno = ENOSYS; return -1; }
int accept4(int s, void *a, void *l, int f)           { (void)s;(void)a;(void)l;(void)f; errno = ENOSYS; return -1; }

/* Ownership/credentials — Horizon is single-user. */
int chown(const char *p, uid_t u, gid_t g)            { (void)p;(void)u;(void)g; return 0; }
int lchown(const char *p, uid_t u, gid_t g)           { (void)p;(void)u;(void)g; return 0; }
int fchown(int fd, uid_t u, gid_t g)                  { (void)fd;(void)u;(void)g; return 0; }
int chroot(const char *p)                             { (void)p; errno = ENOSYS; return -1; }
uid_t getuid(void)  { return 0; }
pid_t getppid(void) { return 0; }
int setuid(uid_t u) { (void)u; return 0; }
int setgid(gid_t g) { (void)g; return 0; }
int setpgid(pid_t a, pid_t b) { (void)a;(void)b; return 0; }
pid_t setsid(void) { return 0; }
int setgroups(int n, const gid_t *g) { (void)n;(void)g; return 0; }
int getpwuid_r(unsigned uid, void *pwd, char *buf, size_t buflen, void **res) {
    (void)uid;(void)pwd;(void)buf;(void)buflen; if (res) *res = 0; return 0;
}

/* Process control / spawning — never reached; stub to ENOSYS. */
int   execvp(const char *f, char *const argv[]) { (void)f;(void)argv; errno = ENOSYS; return -1; }
pid_t waitpid(pid_t pid, int *st, int opt) { (void)pid;(void)st;(void)opt; errno = ENOSYS; return -1; }
int   waitid(int t, int id, void *info, int opt) { (void)t;(void)id;(void)info;(void)opt; errno = ENOSYS; return -1; }
int   killpg(pid_t pgrp, int sig) { (void)pgrp;(void)sig; errno = ENOSYS; return -1; }
int   pause(void) { errno = ENOSYS; return -1; }
int   posix_spawnp(void *pid, const char *file, const void *fa, const void *attr, char *const argv[], char *const envp[]) {
    (void)pid;(void)file;(void)fa;(void)attr;(void)argv;(void)envp; return ENOSYS;
}
int posix_spawnattr_init(void *a) { (void)a; return 0; }
int posix_spawnattr_destroy(void *a) { (void)a; return 0; }
int posix_spawnattr_setflags(void *a, short f) { (void)a;(void)f; return 0; }
int posix_spawnattr_setpgroup(void *a, int p) { (void)a;(void)p; return 0; }
int posix_spawnattr_setsigdefault(void *a, const void *s) { (void)a;(void)s; return 0; }
int posix_spawn_file_actions_init(void *a) { (void)a; return 0; }
int posix_spawn_file_actions_destroy(void *a) { (void)a; return 0; }
int posix_spawn_file_actions_adddup2(void *a, int fd, int nfd) { (void)a;(void)fd;(void)nfd; return 0; }

/* Signals — Horizon has none; report empty/success. */
int sigaction(int s, const struct sigaction *act, struct sigaction *old) { (void)s;(void)act;(void)old; return 0; }
/* newlib exposes these as macros; Rust std wants the real function symbols. */
#undef sigemptyset
#undef sigaddset
int sigemptyset(sigset_t *set) { if (set) *set = 0; return 0; }
int sigaddset(sigset_t *set, int s) { (void)set;(void)s; return 0; }
int sigaltstack(const stack_t *ss, stack_t *old) { (void)ss;(void)old; return 0; }
int pthread_sigmask(int how, const sigset_t *set, sigset_t *old) { (void)how;(void)set;(void)old; return 0; }

/* Threads / scheduling introspection. */
int pthread_getattr_np(void *thread, void *attr) { (void)thread;(void)attr; return 0; }
int pthread_setname_np(void *thread, const char *name) { (void)thread;(void)name; return 0; }
int sched_getaffinity(int pid, size_t cpusetsize, void *mask) {
    (void)pid; if (mask && cpusetsize) memset(mask, 0xFF, cpusetsize); return 0;
}

/* Misc glibc-isms. */
long sysconf(int name) {
    /* _SC_PAGESIZE / _SC_PAGE_SIZE == 8 in newlib's <sys/unistd.h>. Report the
     * Horizon 4 KiB page so callers that size allocations by it behave. */
    if (name == 8) return 0x1000;
    return -1;
}
int clock_nanosleep(int clk, int flags, const void *req, void *rem) {
    (void)clk;(void)flags; return nanosleep((const struct timespec *)req, (struct timespec *)rem);
}
int __res_init(void) { return 0; }
const char *gnu_get_libc_version(void) { return "2.0"; }
int mprotect(void *addr, size_t len, int prot) { (void)addr;(void)len;(void)prot; return 0; }

/* No dynamic loading on Horizon. */
void *dlsym(void *handle, const char *symbol) { (void)handle;(void)symbol; return 0; }

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
