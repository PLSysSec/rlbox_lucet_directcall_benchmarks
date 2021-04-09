// Simple add function compiled with lucet
// Assembly extracted from compiled lucet files
.globl guest_func_simpleAddNoPrintTest
.type guest_func_simpleAddNoPrintTest,@function
.align 16
guest_func_simpleAddNoPrintTest:
_guest_func_simpleAddNoPrintTest:
    // d780:       40 55                   rex push %rbp
    // d782:       48 89 e5                mov    %rsp,%rbp
    // d785:       40 01 f2                rex add %esi,%edx
    // d788:       40 89 d0                rex mov %edx,%eax
    // d78b:       40 5d                   rex pop %rbp
    // d78d:       c3                      retq
    // d78e:       66 90                   xchg   %ax,%ax
    rex push %rbp
    mov    %rsp,%rbp
    rex add %esi,%edx
    rex mov %edx,%eax
    rex pop %rbp
    retq
    xchg   %ax,%ax

// Trampoline code modified for direct function calls
#define HEAVY_TRAMPOLINE_MODIFY_FUNCTION_CALL(x) callq guest_func_simpleAddNoPrintTest
#include "trampoline_asm_64.S"
