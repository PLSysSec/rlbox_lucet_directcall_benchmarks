// Simple add function compiled with Clang -O3
// Checked on godbolt.org
.globl simpleAddNoPrintTest
.type simpleAddNoPrintTest,@function
.align 16
simpleAddNoPrintTest:
_simpleAddNoPrintTest:
    mov    0x8(%esp),%eax
    add    0x4(%esp),%eax
    ret

// Trampoline code modified for direct function calls
#define HEAVY_TRAMPOLINE_MODIFY_FUNCTION_CALL(x) calll simpleAddNoPrintTest
#include "trampoline_asm_32.S"
