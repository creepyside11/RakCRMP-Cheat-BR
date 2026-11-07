// AArch64 inline hook: 16-byte absolute jump + relocated trampoline.
#include "rc.h"
#include <sys/mman.h>
#include <dlfcn.h>
#include <unistd.h>
#include <link.h>
#include <string.h>
#include <stdint.h>

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

static int base_cb(struct dl_phdr_info* info, size_t, void* data) {
    struct Find { const char* name; uintptr_t base; }* f =
        (struct Find*)data;
    if (info->dlpi_name && strstr(info->dlpi_name, f->name)) {
        f->base = (uintptr_t)info->dlpi_addr;
        return 1;
    }
    return 0;
}

// Follow thunks that sit in front of the real implementation:
//   b <target>                    (bionic libEGL style)
//   adrp xN, page; ldr xN,[xN,#off]; br xN     (PLT style)
// Returns the resolved address or nullptr when the entry is a real function.
static void* follow_thunk(void* entry) {
    const uint32_t* c = (const uint32_t*)entry;
    uint32_t i0 = c[0];

    if ((i0 >> 26) == 0x05) {                      // b imm26
        int32_t imm = (int32_t)(i0 & 0x03FFFFFF);
        if (imm & 0x02000000) imm -= 0x04000000;
        return (void*)((uintptr_t)entry + (intptr_t)imm * 4);
    }

    // adrp xN, page
    if ((i0 & 0x9F000000u) == 0x90000000u) {
        uint32_t rd = i0 & 0x1F;
        int64_t immlo = (i0 >> 29) & 0x3;
        int64_t immhi = (i0 >> 5) & 0x7FFFF;
        int64_t imm = (immhi << 2) | immlo;
        if (imm & (1LL << 20)) imm -= (1LL << 21);
        uintptr_t page = ((uintptr_t)entry & ~0xFFFULL) + (uintptr_t)(imm << 12);
        uint32_t i1 = c[1];
        // ldr xN, [xN, #imm12*8]   (64-bit, unsigned offset)
        if ((i1 & 0xFFC00000u) == 0xF9400000u && (i1 & 0x1F) == rd) {
            uintptr_t slot = page + ((i1 >> 10) & 0xFFF) * 8;
            void* fn = *(void**)slot;
            if (fn) return fn;
        }
        // add xN, xN, #imm ; ldr xN, [xN]
        if ((i1 & 0x7F800000u) == 0x11000000u && (i1 & 0x1F) == rd && ((i1 >> 5) & 0x1F) == rd) {
            uintptr_t addr = page + ((i1 >> 10) & 0xFFF);
            uint32_t i2 = c[2];
            if ((i2 & 0xFFC00000u) == 0xF9400000u) {
                void* fn = *(void**)(addr + ((i2 >> 10) & 0xFFF) * 8);
                if (fn) return fn;
            }
        }
    }
    return nullptr;
}

uintptr_t image_base(const char* soname) {
    struct Find { const char* name; uintptr_t base; } f;
    f.name = soname;
    f.base = 0;
    dl_iterate_phdr(base_cb, &f);
    return f.base;
}

void* sym_or_offset(void* handle, const char* name, uintptr_t offset) {
    void* p = handle ? dlsym(handle, name) : nullptr;
    if (p) return p;
    uintptr_t base = image_base("libblackrussia-client.so");
    if (!base) return nullptr;
    rc_log("символ %s не найден, беру смещение +0x%lx", name, (unsigned long)offset);
    return (void*)(base + offset);
}

bool install(Hook& h, void* target, void* detour) {
    if (!target || !detour) return false;

    // walk through entry thunks so we patch the body, not the jump
    for (int hop = 0; hop < 4; hop++) {
        void* next = follow_thunk(target);
        if (!next || next == target) break;
        rc_log("хук: %p -> %p (переход через thunk)", target, next);
        target = next;
    }

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
