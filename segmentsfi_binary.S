// Simple add function compiled with Clang -O3 and zerocost flags. See file "simpleAddNoPrintTestSegmentSfi.c"
// Assembly extracted from compiled files
.globl simpleAddNoPrintTest.cfi
.type simpleAddNoPrintTest.cfi,@function
.align 16
simpleAddNoPrintTest.cfi:
    // 0001d0a0 <simpleAddNoPrintTest.cfi>:
    //    1d0a0:       8b 44 24 08             mov    0x8(%esp),%eax
    //    1d0a4:       03 44 24 04             add    0x4(%esp),%eax
    //    1d0a8:       c3                      ret
    mov    0x8(%esp),%eax
    add    0xc(%esp),%eax
    ret

.globl simpleAddNoPrintTest
.type simpleAddNoPrintTest,@function
.align 16
simpleAddNoPrintTest:
    //    1e070:       e9 2b f0 ff ff          jmp    1d0a0 <simpleAddNoPrintTest.cfi>
    jmp    simpleAddNoPrintTest.cfi

// Trampoline code modified for direct function calls
#define SEGMENTSFI_TRAMPOLINE_MODIFY_FUNCTION_CALL(x) calll simpleAddNoPrintTest
#include "trampoline.S"
