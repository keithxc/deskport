/* Isolated Linux/glibc diagnostic only; never link into shipping binaries.
 * Tracks exact 32 KiB allocations, including realloc/free, without replacing
 * the allocator. PipeWire 1.6.6 native connections allocate two such buffers.
 * Symbol/module stacks identify provenance; other allocation sizes are
 * excluded. Output is private diagnostic data and may contain local filesystem
 * paths.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t, size_t);
extern void *__libc_realloc(void *, size_t);
extern void __libc_free(void *);
static __thread int busy;
static atomic_int enabled;
static int fd = -1;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
struct slot {
  void *p;
  unsigned id;
};
static struct slot slots[65536];
static unsigned serial;
static struct slot *lookup(void *p, int insert) {
  unsigned h = ((uintptr_t)p >> 4) % 65536;
  struct slot *tomb = 0;
  for (unsigned i = 0; i < 65536; i++) {
    struct slot *s = &slots[(h + i) % 65536];
    if (s->p == p)
      return s;
    if (s->p == (void *)1 && !tomb)
      tomb = s;
    if (!s->p)
      return insert ? (tomb ? tomb : s) : 0;
  }
  return insert ? tomb : 0;
}
static unsigned record(void *old, void *p, size_t size) {
  struct slot *s = old ? lookup(old, 0) : 0;
  if (s) {
    dprintf(fd, "FREE %u %p\n", s->id, old);
    s->p = (void *)1;
  }
  if (p && size == 32768) {
    s = lookup(p, 1);
    if (!s)
      _exit(90);
    s->p = p;
    s->id = ++serial;
    return s->id;
  }
  return 0;
}
static void allocation_stack(unsigned id, void *p) {
  if (!id)
    return;
  // Symbol lookup can take the dynamic loader lock. Never do it while holding
  // the ledger lock: another thread may allocate while it holds that lock.
  void *stack[18];
  int n = backtrace(stack, 18);
  char **names = backtrace_symbols(stack, n);
  char message[16384];
  size_t used = snprintf(message, sizeof(message), "ALLOC %u %p\n", id, p);
  for (int i = 0; i < n && names; i++) {
    int wrote =
        snprintf(message + used, sizeof(message) - used, "%s\n", names[i]);
    if (wrote < 0 || (size_t)wrote >= sizeof(message) - used - 64)
      break;
    used += wrote;
  }
  __libc_free(names);
  used += snprintf(message + used, sizeof(message) - used, "END %u\n", id);
  pthread_mutex_lock(&lock);
  if (enabled) {
    size_t sent = 0;
    while (sent < used) {
      ssize_t written = write(fd, message + sent, used - sent);
      if (written < 0 && errno == EINTR)
        continue;
      if (written <= 0)
        break;
      sent += written;
    }
  }
  pthread_mutex_unlock(&lock);
}
static void update(void *old, void *p, size_t size) {
  if (!enabled || busy)
    return;
  int saved = errno;
  busy = 1;
  pthread_mutex_lock(&lock);
  unsigned id = enabled ? record(old, p, size) : 0;
  pthread_mutex_unlock(&lock);
  allocation_stack(id, p);
  busy = 0;
  errno = saved;
}
void *malloc(size_t n) {
  void *p = __libc_malloc(n);
  if (n == 32768)
    update(0, p, n);
  return p;
}
void *calloc(size_t a, size_t b) {
  void *p = __libc_calloc(a, b);
  if (a && b == 32768 / a && a * b == 32768)
    update(0, p, 32768);
  return p;
}
void free(void *p) {
  if (p)
    update(p, 0, 0);
  __libc_free(p);
}
void *realloc(void *p, size_t n) {
  if (!enabled || busy)
    return __libc_realloc(p, n);
  busy = 1;
  pthread_mutex_lock(&lock);
  void *q = __libc_realloc(p, n);
  int saved = errno;
  unsigned id = enabled && (q || !n) ? record(p, q, n) : 0;
  pthread_mutex_unlock(&lock);
  allocation_stack(id, q);
  busy = 0;
  errno = saved;
  return q;
}
__attribute__((constructor)) static void start(void) {
  busy = 1;
  const char *p = getenv("BUFFER_WATCH_LOG");
  if (p)
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  enabled = fd >= 0;
  busy = 0;
}
__attribute__((destructor)) static void finish(void) {
  busy = 1;
  if (!enabled)
    return;
  pthread_mutex_lock(&lock);
  unsigned left = 0;
  for (int i = 0; i < 65536; i++)
    if ((uintptr_t)slots[i].p > 1) {
      dprintf(fd, "LIVE %u %p\n", slots[i].id, slots[i].p);
      left++;
    }
  dprintf(fd, "SUMMARY allocations=%u live=%u\n", serial, left);
  enabled = 0;
  pthread_mutex_unlock(&lock);
  close(fd);
}
