#pragma once

#include "ctx_save_trampoline.hpp"

#include <cstdint>
#include <cstdlib>
#include <dlfcn.h>
#include <mutex>
#ifndef RLBOX_USE_CUSTOM_SHARED_LOCK
#  include <shared_mutex>
#endif
#include <utility>

#include "rlbox_helpers.hpp"

namespace rlbox {

class rlbox_mpk_sandbox;

struct rlbox_mpk_sandbox_thread_data
{
  rlbox_mpk_sandbox* sandbox;
  uint32_t last_callback_invoked;
};

#ifdef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES

rlbox_mpk_sandbox_thread_data* get_rlbox_mpk_sandbox_thread_data();
#  define RLBOX_MPK_SANDBOX_STATIC_VARIABLES()                                \
    thread_local rlbox::rlbox_mpk_sandbox_thread_data                         \
      rlbox_mpk_sandbox_thread_info{ 0, 0 };                                  \
    namespace rlbox {                                                          \
      rlbox_mpk_sandbox_thread_data* get_rlbox_mpk_sandbox_thread_data()     \
      {                                                                        \
        return &rlbox_mpk_sandbox_thread_info;                                \
      }                                                                        \
    }                                                                          \
    static_assert(true, "Enforce semi-colon")

#endif

// #define SET_MPK_PERMISSIONS(pkru)                       \
//   {                                                     \
//       unsigned int eax = pkru;                          \
//       unsigned int ecx = 0;                             \
//       unsigned int edx = 0;                             \
//       asm volatile(".byte 0x0f,0x01,0xef\n\t"           \
//                   : : "a" (eax), "c" (ecx), "d" (edx)); \
//   }

#define SET_MPK_PERMISSIONS(pkru) {}


/**
 * @brief Class that implements the mpk sandbox.
 */
class rlbox_mpk_sandbox
{
public:
  // Stick with the system defaults
  using T_LongLongType = long long;
  using T_LongType = long;
  using T_IntType = int;
  using T_PointerType = uintptr_t;
  using T_ShortType = short;
  // You can transfer buffers at the page level with mpk
  // But this is too expensive as it involves a syscall
  // Copies are usually faster, so no transfer support
  // using can_grant_deny_access = void;

private:
  void* sandbox = nullptr;

  heavy_trampoline trampoline;

  const uint32_t mpk_app_domain_perms = 0;
  // 0b1100 --- disallow access to domain 1
  const uint32_t mpk_sbx_domain_perms = 12;
  RLBOX_SHARED_LOCK(callback_mutex);
  static inline const uint32_t MAX_CALLBACKS = 64;
  void* callback_unique_keys[MAX_CALLBACKS]{ 0 };
  void* callbacks[MAX_CALLBACKS]{ 0 };

#ifndef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
  thread_local static inline rlbox_mpk_sandbox_thread_data thread_data{ 0, 0 };
#endif

  template<uint32_t N, typename T_Ret, typename... T_Args>
  static T_Ret callback_trampoline(T_Args... params)
  {
#ifdef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
    auto& thread_data = *get_rlbox_mpk_sandbox_thread_data();
#endif
    SET_MPK_PERMISSIONS(thread_data.sandbox->mpk_app_domain_perms);
    thread_data.last_callback_invoked = N;
    using T_Func = T_Ret (*)(T_Args...);
    T_Func func;
    {
      RLBOX_ACQUIRE_SHARED_GUARD(lock, thread_data.sandbox->callback_mutex);
      func = reinterpret_cast<T_Func>(thread_data.sandbox->callbacks[N]);
    }
    // Callbacks are invoked through function pointers, cannot use std::forward
    // as we don't have caller context for T_Args, which means they are all
    // effectively passed by value
    // return func(params...);
    return thread_data.sandbox->trampoline.func_call(func, params...);
  }

protected:
  inline void impl_create_sandbox(const char* path) {
    sandbox = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    if (sandbox == nullptr) {
      char* error = dlerror();
      detail::dynamic_check(sandbox != nullptr, error);
    }

    #ifdef RLBOX_ZEROCOST_NOSWITCHSTACK
      const bool should_switch_stacks = false;
    #else
      const bool should_switch_stacks = true;
    #endif

    #ifdef RLBOX_ZEROCOST_WINDOWSMODE
      const bool should_use_windows_mode = true;
    #else
      const bool should_use_windows_mode = false;
    #endif

    trampoline.init(should_switch_stacks, should_use_windows_mode);
  }

  inline void impl_destroy_sandbox() {
    trampoline.destroy();
    dlclose(sandbox);
  }

  template<typename T>
  inline void* impl_get_unsandboxed_pointer(T_PointerType p) const
  {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(p));
  }

  template<typename T>
  inline T_PointerType impl_get_sandboxed_pointer(const void* p) const
  {
    return static_cast<T_PointerType>(reinterpret_cast<uintptr_t>(p));
  }

  template<typename T>
  static inline void* impl_get_unsandboxed_pointer_no_ctx(
    T_PointerType p,
    const void* /* example_unsandboxed_ptr */,
    rlbox_mpk_sandbox* (*/* expensive_sandbox_finder */)(
      const void* example_unsandboxed_ptr))
  {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(p));
  }

  template<typename T>
  static inline T_PointerType impl_get_sandboxed_pointer_no_ctx(
    const void* p,
    const void* /* example_unsandboxed_ptr */,
    rlbox_mpk_sandbox* (*/* expensive_sandbox_finder */)(
      const void* example_unsandboxed_ptr))
  {
    return static_cast<T_PointerType>(reinterpret_cast<uintptr_t>(p));
  }

  inline T_PointerType impl_malloc_in_sandbox(size_t size)
  {
    void* p = malloc(size);
    return reinterpret_cast<uintptr_t>(p);
  }

  inline void impl_free_in_sandbox(T_PointerType p)
  {
    free(reinterpret_cast<void*>(p));
  }

  static inline bool impl_is_in_same_sandbox(const void*, const void*)
  {
    return true;
  }

  inline bool impl_is_pointer_in_sandbox_memory(const void*) { return true; }
  inline bool impl_is_pointer_in_app_memory(const void*) { return true; }

  inline size_t impl_get_total_memory()
  {
    return std::numeric_limits<size_t>::max();
  }

  inline void* impl_get_memory_location()
  {
    // There isn't any sandbox memory for the mpk_sandbox as we just redirect
    // to the app. Also, this is mostly used for pointer swizzling or sandbox
    // bounds checks which is also not present/not required. So we can just
    // return null
    return nullptr;
  }

  // adding a template so that we can use static_assert to fire only if this
  // function is invoked
  template<typename T = void>
  void* impl_lookup_symbol(const char* /* func_name */)
  {
    // Will fire if this impl_lookup_symbol is ever called for the static
    // sandbox
    constexpr bool fail = std::is_same_v<T, void>;
    rlbox_detail_static_fail_because(
      fail,
      "The mpk_sandbox uses static calls and thus developers should add\n\n"
      "#define RLBOX_USE_STATIC_CALLS() rlbox_mpk_sandbox_lookup_symbol\n\n"
      "to their code, to ensure that static calls are handled correctly.");

    return nullptr;
  }

  #define rlbox_mpk_sandbox_lookup_symbol(func_name)                            \
  reinterpret_cast<void*>(&func_name) /* NOLINT */

  template<typename T, typename T_Converted, typename... T_Args>
  auto impl_invoke_with_func_ptr(T_Converted* func_ptr, T_Args&&... params)
  {
#ifdef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
    auto& thread_data = *get_rlbox_mpk_sandbox_thread_data();
#endif
    thread_data.sandbox = this;
    SET_MPK_PERMISSIONS(mpk_sbx_domain_perms);
    // return (*func_ptr)(params...);
    return trampoline.func_call(func_ptr, params...);
  }

  template<typename T_Ret, typename... T_Args>
  inline T_PointerType impl_register_callback(void* key, void* callback)
  {
    RLBOX_ACQUIRE_UNIQUE_GUARD(lock, callback_mutex);

    void* chosen_trampoline = nullptr;

    // need a compile time for loop as we we need I to be a compile time value
    // this is because we are returning the I'th callback trampoline
    detail::compile_time_for<MAX_CALLBACKS>([&](auto I) {
      if (!chosen_trampoline && callback_unique_keys[I.value] == nullptr) {
        callback_unique_keys[I.value] = key;
        callbacks[I.value] = callback;
        chosen_trampoline = reinterpret_cast<void*>(
          callback_trampoline<I.value, T_Ret, T_Args...>);
      }
    });

    return reinterpret_cast<T_PointerType>(chosen_trampoline);
  }

  static inline std::pair<rlbox_mpk_sandbox*, void*>
  impl_get_executed_callback_sandbox_and_key()
  {
#ifdef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
    auto& thread_data = *get_rlbox_mpk_sandbox_thread_data();
#endif
    auto sandbox = thread_data.sandbox;
    auto callback_num = thread_data.last_callback_invoked;
    void* key = sandbox->callback_unique_keys[callback_num];
    return std::make_pair(sandbox, key);
  }

  template<typename T_Ret, typename... T_Args>
  inline void impl_unregister_callback(void* key)
  {
    RLBOX_ACQUIRE_UNIQUE_GUARD(lock, callback_mutex);
    for (uint32_t i = 0; i < MAX_CALLBACKS; i++) {
      if (callback_unique_keys[i] == key) {
        callback_unique_keys[i] = nullptr;
        callbacks[i] = nullptr;
        break;
      }
    }
  }

  template<typename T>
  inline T* impl_grant_access(T* src, size_t num, bool& success)
  {
    RLBOX_UNUSED(num);
    success = true;
    return src;
  }

  template<typename T>
  inline T* impl_deny_access(T* src, size_t num, bool& success)
  {
    RLBOX_UNUSED(num);
    success = true;
    return src;
  }
};

}
