/*
Compile with

/mnt/sata/ffbuilds/zerocost_llvm_install/bin/clang simpleAddNoPrintTestSegmentSfi.c -O3 -m32 -fno-asm -fno-asm-blocks -Werror=return-type -fsanitize=safe-stack -fstack-clash-protection -flto -fsanitize=cfi-icall -fsanitize-cfi-canonical-jump-tables -fsanitize-cfi-cross-dso -ftrivial-auto-var-init=zero -enable-trivial-auto-var-init-zero-knowing-it-will-be-removed-from-clang -mseparate-stack-seg -o /tmp/simpleAddNoPrintTestSegmentSfi.o

*/

unsigned long __attribute__ ((noinline)) simpleAddNoPrintTest(unsigned long a, unsigned long b)
{
  return a + b;
}

int main(int argc, char** argv) {
    return simpleAddNoPrintTest(argc, 1);
}