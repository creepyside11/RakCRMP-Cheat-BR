#!/usr/bin/env python3
"""Apply small Android portability fixes to the pinned Dobby checkout."""
import pathlib
import sys

root = pathlib.Path(sys.argv[1])

memory = root / "common/os_arch_features.h"
s = memory.read_text()
include_anchor = '''#include <sys/types.h>
#include <stddef.h>'''
include_patch = '''#include <sys/types.h>
#include <stddef.h>
#include <sys/mman.h>
#include <unistd.h>'''
if include_anchor in s:
    s = s.replace(include_anchor, include_patch, 1)
elif include_patch not in s:
    raise SystemExit("Dobby memory header include anchor changed")
old = '''#if defined(ANDROID)
  auto page = (void *)ALIGN_FLOOR(address, OSMemory::PageSize());
  if (!OSMemory::SetPermission(page, OSMemory::PageSize(), kReadExecute)) {
    return;
  }
#endif'''
new = '''#if defined(ANDROID)
  const size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  auto page = reinterpret_cast<void *>(ALIGN_FLOOR(address, page_size));
  mprotect(page, page_size, PROT_READ | PROT_EXEC);
#endif'''
if old in s:
    s = s.replace(old, new, 1)
elif new not in s:
    raise SystemExit("Dobby Android memory-permission anchor changed")
memory.write_text(s)

asm = root / "source/TrampolineBridge/ClosureTrampolineBridge/arm64/closure_bridge_arm64.asm"
s = asm.read_text()
old = '''adrp TMP_REG_0, cdecl(common_closure_bridge_handler)@PAGE
add TMP_REG_0, TMP_REG_0, cdecl(common_closure_bridge_handler)@PAGEOFF'''
new = '''#if defined(__ANDROID__)
adrp TMP_REG_0, cdecl(common_closure_bridge_handler)
add TMP_REG_0, TMP_REG_0, :lo12:cdecl(common_closure_bridge_handler)
#else
adrp TMP_REG_0, cdecl(common_closure_bridge_handler)@PAGE
add TMP_REG_0, TMP_REG_0, cdecl(common_closure_bridge_handler)@PAGEOFF
#endif'''
if old in s:
    s = s.replace(old, new, 1)
elif new not in s:
    raise SystemExit("Dobby ARM64 assembler anchor changed")
asm.write_text(s)
print("Applied Android portability fixes to Dobby")
