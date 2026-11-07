// Executes the aarch64 inline hook for real: install, call, unhook-free check.
// Build & run: tools/run_tests.sh
#include "rc.h"
#include <cstdio>
#include <cstdarg>

void rc_log(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    printf("    [log] "); vprintf(fmt, ap); printf("\n");
    va_end(ap);
}
const char* rc_log_line(int) { return ""; }
int rc_log_count() { return 0; }

static int fails = 0;
static void check(bool ok, const char* what) {
    if (!ok) { printf("  FAIL %s\n", what); fails++; }
    else printf("  ok   %s\n", what);
}

// Long enough prologue (>= 16 bytes), no pc-relative insns at the entry.
__attribute__((noinline)) static int work(int a, int b) {
    volatile int t = a;
    t = t * 3 + b;
    t = t ^ 0x55;
    t = t + 1;
    return t;
}

__attribute__((noinline)) static int work2(int a, int b) {
    volatile int t = a;
    t = t + b * 2;
    t = t ^ 0x33;
    return t;
}

static int (*volatile fp)(int, int) = work;
static int (*volatile fp2)(int, int) = work2;
static int (*orig)(int, int) = nullptr;
static int (*orig2)(int, int) = nullptr;
static int detourCalls = 0;

static int detour(int a, int b) {
    detourCalls++;
    return orig(a, b) + 1000;
}

static int detour2(int a, int b) {
    return orig2(a, b) * 2;
}

int main() {
    const int a = 4, b = 9;
    const int baseline = fp(a, b);
    const int baseline2 = fp2(3, 4);
    printf("baseline work(%d,%d) = %d\n", a, b, baseline);

    rchook::Hook h;
    bool ok = rchook::install(h, (void*)work, (void*)&detour);
    check(ok, "hook installed");
    if (!ok) return 1;

    orig = (int (*)(int, int))h.tramp;

    int hooked = fp(a, b);
    check(hooked == baseline + 1000, "calls are routed to the detour");
    check(detourCalls == 1, "detour ran once");
    check(orig(a, b) == baseline, "trampoline reproduces the original");
    check(fp2(3, 4) == baseline2, "other functions unaffected");

    // second hook on another function, trampolines must not collide
    rchook::Hook h2;
    ok = rchook::install(h2, (void*)work2, (void*)&detour2);
    check(ok, "second hook installed");
    if (ok) {
        orig2 = (int (*)(int, int))h2.tramp;
        check(fp2(3, 4) == baseline2 * 2, "second hook routes to its detour");
        check(fp(a, b) == baseline + 1000, "first hook still alive");
    }

    // install must reject a prologue it cannot relocate
    rchook::Hook h3;
    check(!rchook::install(h3, (void*)&work2, (void*)&detour), "patching an already hooked prologue is refused");

    printf(fails ? "\nFAILED %d\n" : "\nALL PASSED\n", fails);
    return fails ? 1 : 0;
}
