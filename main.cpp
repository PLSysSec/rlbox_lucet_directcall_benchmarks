#include <chrono>
#include <cstdint>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

using namespace std::chrono;

void guest_func_malloc() {abort();}
void guest_func_free() {abort();}

#define RLBOX_SINGLE_THREADED_INVOCATIONS
#define RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
#include "rlbox_helpers.hpp"

#ifdef RLBOX_ZEROCOST_TRANSITION_MPKFULLSAVE
  #define RLBOX_USE_STATIC_CALLS() rlbox_mpk_sandbox_lookup_symbol
  #include "rlbox_mpk_heavy_sandbox.hpp"
  using sandbox_t = rlbox::rlbox_mpk_sandbox;
  RLBOX_MPK_SANDBOX_STATIC_VARIABLES();
#else
  #define RLBOX_USE_STATIC_CALLS() rlbox_lucet_sandbox_lookup_symbol
  #ifdef RLBOX_ZEROCOST_TRANSITION_ZEROCOST
      #include "rlbox_lucet_direct_sandbox.hpp"
  #else
      #include "rlbox_lucet_heavy_direct_sandbox.hpp"
  #endif
  using sandbox_t = rlbox::rlbox_lucet_sandbox;
  RLBOX_LUCET_SANDBOX_STATIC_VARIABLES();
#endif

#include "rlbox.hpp"

#ifndef GLUE_LIB_LUCET_PATH
#  error "Missing definition for GLUE_LIB_LUCET_PATH"
// For intellisense
#define GLUE_LIB_LUCET_PATH ""
#endif

using wasm_ulong = uint32_t;

extern "C" {
wasm_ulong guest_func_simpleAddNoPrintTest(void* heap, wasm_ulong a, wasm_ulong b);
long simpleAddNoPrintTest(long a, long b);
}

int main(int argc, char const *argv[])
{
    // Warm up the timer. The first call is always slow (at least on the test
    // platform)
    for (int i = 0; i < 10; i++) {
      auto val = high_resolution_clock::now();
      RLBOX_UNUSED(val);
    }

    rlbox::rlbox_sandbox<sandbox_t> sandbox;
    sandbox.create_sandbox(GLUE_LIB_LUCET_PATH);

    const int test_iterations = 1000000;
    const int val1 = 2;
    const int val2 = 3;

    uint64_t result2 = 0;
    {
      auto enter_time = high_resolution_clock::now();
      for (int i = 0; i < test_iterations; i++) {
        // to make sure the optimizer doesn't try to be too clever and eliminate
        // the call
        result2 +=
          sandbox.invoke_sandbox_function(simpleAddNoPrintTest, val1, val2)
            .unverified_safe_because("test");
      }
      auto exit_time = high_resolution_clock::now();

      int64_t ns = duration_cast<nanoseconds>(exit_time - enter_time).count();
      std::cout << "Sandboxed function invocation time: "
                << (ns / test_iterations) << "\n";
    }

    return 0;
}
