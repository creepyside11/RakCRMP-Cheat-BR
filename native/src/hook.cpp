// AArch64 inline hook: 16-byte absolute jump + relocated trampoline.
#include "rc.h"
#include <sys/mman.h>
#include <dlfcn.h>
#include <unistd.h>

namespace rchook {

static bool pc_relative(uint32_t insn) {
    uint32_t top6 = insn >> 26;
    if (top6 == 0x05 || top6 == 0x25) return true;                 // B / BL
    if ((insn & 0xFF000000u) == 0x54000000u) return true;          // B.cond
    if ((insn & 0x7E000000u) == 0x34000000u) return true;          // CBZ / CBNZ
    if ((insn & 0x7E000000u) == 0x36000000u) return true;          // TBZ / TBNZ
    if ((insn & 0x9F000000u) == 0x10000000u) return true;          // ADR
    if ((insn & 0x9F000000u) == 0x90000000u) return true;          // ADRP
    if ((insn & 0x3B000000u) == 0x18000000u) return true;          // LDR literal
    return false;
}

void* resolve(void* handle, const char* sym) {
    if (!handle) return nullptr;
    return dlsym(handle, sym);
}

bool install(Hook& h, void* target, void* detour) {
    if (!target || !detour) return false;
    h.target = target;
    const uint32_t* code = (const uint32_t*)target;

    for (int i = 0; i < 4; i++) {
        h.saved[i] = code[i];
        if (pc_relative(code[i])) {
            rc_log("hook: %p prologue insn %d is pc-relative (0x%08x) — skip", target, i, code[i]);
            return false;
        }
    }

    void* tramp = mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (tramp == MAP_FAILED) return false;

    uint32_t* t = (uint32_t*)tramp;
    for (int i = 0; i < 4; i++) t[i] = h.saved[i];
    t[4] = 0x58000050u;                                // ldr x16, #8
    t[5] = 0xD61F0200u;                                // br  x16
    *(uint64_t*)(t + 6) = (uint64_t)target + 16;
    mprotect(tramp, 4096, PROT_READ | PROT_EXEC);
    __builtin___clear_cache((char*)tramp, (char*)tramp + 64);

    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    uintptr_t begin = (uintptr_t)target & ~(page - 1);
    uintptr_t end   = ((uintptr_t)target + 16 + page - 1) & ~(page - 1);
    if (mprotect((void*)begin, end - begin, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        rc_log("hook: mprotect(%p) failed", target);
        munmap(tramp, 4096);
        return false;
    }

    uint32_t patch[4];
    patch[0] = 0x58000050u;                            // ldr x16, #8
    patch[1] = 0xD61F0200u;                            // br  x16
    *(uint64_t*)(patch + 2) = (uint64_t)detour;
    memcpy(target, patch, 16);
    __builtin___clear_cache((char*)target, (char*)target + 16);

    mprotect((void*)begin, end - begin, PROT_READ | PROT_EXEC);
    h.tramp = tramp;
    h.installed = true;
    return true;
}

} // namespace rchook
