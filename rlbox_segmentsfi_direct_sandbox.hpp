#pragma once

#include <cstdint>
#include <cstdlib>
#include <dlfcn.h>
#include <mutex>
#ifndef RLBOX_USE_CUSTOM_SHARED_LOCK
#  include <shared_mutex>
#endif
#include <type_traits>
#include <utility>

#include "rlbox_helpers.hpp"

#include "segmentsfi_sandbox_runtime.h"

namespace rlbox {

class rlbox_segmentsfi_sandbox;

struct rlbox_segmentsfi_sandbox_thread_data
{
  rlbox_segmentsfi_sandbox* sandbox;
  uint32_t last_callback_invoked;
};

#ifdef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES

rlbox_segmentsfi_sandbox_thread_data* get_rlbox_segmentsfi_sandbox_thread_data();
#  define RLBOX_SEGMENTSSFI_SANDBOX_STATIC_VARIABLES()                                \
    thread_local rlbox::rlbox_segmentsfi_sandbox_thread_data                         \
      rlbox_segmentsfi_sandbox_thread_info{ 0, 0 };                                  \
    namespace rlbox {                                                          \
      rlbox_segmentsfi_sandbox_thread_data* get_rlbox_segmentsfi_sandbox_thread_data()     \
      {                                                                        \
        return &rlbox_segmentsfi_sandbox_thread_info;                                \
      }                                                                        \
    }                                                                          \
    static_assert(true, "Enforce semi-colon")

#endif

#define GET_CURR_DATA_SEGMENT(curr_ds) { \
  asm volatile("mov %%ds, %0\n\t"        \
            : "=r" (curr_ds)             \
            :                            \
            : );                         \
}

#define CHANGE_DATA_SEGMENT(ds) { \
  asm volatile("mov %0, %%ds\n\t" \
            :                     \
            : "r" (ds)            \
            : );                  \
}

extern "C" {
  struct change_ds_and_invoke_context {
    uint32_t app_ds;
    uint32_t sandbox_ds;
    void* func_ptr;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
  };
  void change_ds_and_invoke(change_ds_and_invoke_context*);

  __attribute__((weak))
  thread_local change_ds_and_invoke_context* curr_segment_sfi_context = 0;

  __attribute__((weak))
  change_ds_and_invoke_context* get_curr_segment_sfi_context() {
    return curr_segment_sfi_context;
  }
}

  ///////////////////////////////////////////////////////////////

namespace segmentssfi_detail {

  // https://stackoverflow.com/questions/6512019/can-we-get-the-type-of-a-lambda-argument
  namespace return_argument_detail {
    template<typename Ret, typename... Rest>
    Ret helper(Ret (*)(Rest...));

    template<typename Ret, typename F, typename... Rest>
    Ret helper(Ret (F::*)(Rest...));

    template<typename Ret, typename F, typename... Rest>
    Ret helper(Ret (F::*)(Rest...) const);

    template<typename F>
    decltype(helper(&F::operator())) helper(F);
  } // namespace return_argument_detail

  template<typename T>
  using return_argument =
    decltype(return_argument_detail::helper(std::declval<T>()));
  ///////////////////////////////////////////////////////////////

  namespace prepend_arg_type_detail {
    template<typename T, typename T_ArgNew>
    struct helper;

    template<typename T_ArgNew, typename T_Ret, typename... T_Args>
    struct helper<T_Ret(T_Args...), T_ArgNew>
    {
      using type = T_Ret(T_ArgNew, T_Args...);
    };
  }

  template<typename T_Func, typename T_ArgNew>
  using prepend_arg_type =
    typename prepend_arg_type_detail::helper<T_Func, T_ArgNew>::type;

}

/**
 * @brief Class that implements the segmentsfi sandbox.
 */
class rlbox_segmentsfi_sandbox
{
public:
  // Stick with the system defaults
  using T_LongLongType = long long;
  using T_LongType = long;
  using T_IntType = int;
  using T_PointerType = void*;
  using T_ShortType = short;
  // You can transfer buffers at the page level with segmentsfi
  // But this is too expensive as it involves a syscall
  // Copies are usually faster, so no transfer support
  // using can_grant_deny_access = void;

private:
  std::unique_ptr<segmentsfi_sandbox> segment_info = nullptr;
  void* sandbox = nullptr;

  void* malloc_index = 0;
  void* free_index = 0;

  uint16_t segmentsfi_app_domain = 0;
  uint16_t segmentsfi_sbx_domain = 0;

  RLBOX_SHARED_LOCK(callback_mutex);
  static inline const uint32_t MAX_CALLBACKS = 64;
  void* callback_unique_keys[MAX_CALLBACKS]{ 0 };
  void* callbacks[MAX_CALLBACKS]{ 0 };

#ifndef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
  thread_local static inline rlbox_segmentsfi_sandbox_thread_data thread_data{ 0, 0 };
#endif

  template<uint32_t N, typename T_Ret, typename... T_Args>
  static T_Ret callback_trampoline(T_Args... params)
  {
#ifdef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
    auto& thread_data = *get_rlbox_segmentsfi_sandbox_thread_data();
#endif
    CHANGE_DATA_SEGMENT(thread_data.sandbox->segmentsfi_app_domain);
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
    if constexpr (std::is_void_v<T_Ret>) {
      func(params...);
      CHANGE_DATA_SEGMENT(thread_data.sandbox->segmentsfi_sbx_domain);
    } else {
      auto ret = func(params...);
      CHANGE_DATA_SEGMENT(thread_data.sandbox->segmentsfi_sbx_domain);
      return ret;
    }
  }

protected:
  inline void impl_destroy_sandbox() {
    dlclose(sandbox);
  }

  template<typename T>
  inline void* impl_get_unsandboxed_pointer(T_PointerType p) const
  {
    // This is the data segment setup we would use once sandboxing is fully setup
    // auto heap_base = segment_info->get_heap_location();
    // auto ret = ((uintptr_t)heap_base) + ((uintptr_t)p);
    // return (void*) ret;
    // This is a workaround that allows access to the full memory
    return (void*) p;
  }

  template<typename T>
  inline T_PointerType impl_get_sandboxed_pointer(const void* p) const
  {
    // This is the data segment setup we would use once sandboxing is fully setup
    // auto ret = ((uintptr_t)p) & (SEGMENT_SFI_HEAP_SIZE - 1);
    // return (T_PointerType) ret;
    // This is a workaround that allows access to the full memory
    return (T_PointerType) p;
  }

  template<typename T>
  static inline void* impl_get_unsandboxed_pointer_no_ctx(
    T_PointerType p,
    const void* example_unsandboxed_ptr,
    rlbox_segmentsfi_sandbox* (* /* expensive_sandbox_finder */)(
      const void* example_unsandboxed_ptr))
  {
    // This is the data segment setup we would use once sandboxing is fully setup
    // auto p_val = (uintptr_t)p;
    // auto heap_base = p_val & ~(SEGMENT_SFI_HEAP_SIZE - 1);
    // auto ret = ((uintptr_t)heap_base) + p_val;
    // return (void*) ret;
    // This is a workaround that allows access to the full memory
    return (void*) p;
  }

  template<typename T>
  static inline T_PointerType impl_get_sandboxed_pointer_no_ctx(
    const void* p,
    const void* /* example_unsandboxed_ptr */,
    rlbox_segmentsfi_sandbox* (* /* expensive_sandbox_finder */)(
      const void* example_unsandboxed_ptr))
  {
    // This is the data segment setup we would use once sandboxing is fully setup
    // auto ret = ((uintptr_t)p) & (SEGMENT_SFI_HEAP_SIZE - 1);
    // return (T_PointerType) ret;
    // This is a workaround that allows access to the full memory
    return (T_PointerType) p;
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
    // There isn't any sandbox memory for the segmentsfi_sandbox as we just redirect
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
      "The segmentsfi_sandbox uses static calls and thus developers should add\n\n"
      "#define RLBOX_USE_STATIC_CALLS() rlbox_segmentsfi_sandbox_lookup_symbol\n\n"
      "to their code, to ensure that static calls are handled correctly.");

    return nullptr;
  }

  #define rlbox_segmentsfi_sandbox_lookup_symbol(func_name)                            \
  (void*) nullptr
  // reinterpret_cast<void*>(&func_name) /* NOLINT */

  template<typename T, typename T_Converted, typename... T_Args>
  auto impl_invoke_with_func_ptr(T_Converted* func_ptr, T_Args&&... params)
  {
#ifdef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
    auto& thread_data = *get_rlbox_segmentsfi_sandbox_thread_data();
#endif
    thread_data.sandbox = this;
    auto invoker = (T_Converted*) change_ds_and_invoke;

    change_ds_and_invoke_context ctx;
    ctx.app_ds = segmentsfi_app_domain;
    ctx.sandbox_ds = segmentsfi_sbx_domain;
    ctx.func_ptr = (void*) func_ptr;
    auto prev_curr_segment_sfi_context = curr_segment_sfi_context;
    curr_segment_sfi_context = &ctx;

    using T_Ret = segmentssfi_detail::return_argument<T_Converted>;

    if constexpr (std::is_void_v<T_Ret>) {
      (*invoker)(params...);
      curr_segment_sfi_context = prev_curr_segment_sfi_context;
    } else {
      auto ret = (*invoker)(params...);
      curr_segment_sfi_context = prev_curr_segment_sfi_context;
      return ret;
    }
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

  inline void impl_create_sandbox(const char* path) {
    sandbox = nullptr;
    // dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    // if (sandbox == nullptr) {
    //   char* error = dlerror();
    //   detail::dynamic_check(sandbox != nullptr, error);
    // }

    segment_info = segmentsfi_sandbox::create_sandbox();
    detail::dynamic_check(segment_info != nullptr, "Setting up segments failed");

    GET_CURR_DATA_SEGMENT(segmentsfi_app_domain);
    segmentsfi_sbx_domain = segment_info->get_heap_segment();

    malloc_index = 0;//impl_lookup_symbol("dlmalloc");
    free_index = 0;//impl_lookup_symbol("dlfree");

    // This is a workaround where we use the actual sandbox heap base in the full address space
    void* heap_base = segment_info->get_heap_location();
    void* func_ptr = 0;//impl_lookup_symbol("segmentsfi_set_alternate_heap_base");
    using T_Func = void (void*);
    impl_invoke_with_func_ptr<T_Func, T_Func>(reinterpret_cast<T_Func*>(func_ptr), heap_base);
  }

  inline T_PointerType impl_malloc_in_sandbox(size_t size)
  {
    using T_Func = void*(size_t);
    T_PointerType ret = impl_invoke_with_func_ptr<T_Func, T_Func>(
      reinterpret_cast<T_Func*>(malloc_index),
      size);
    return ret;
  }

  inline void impl_free_in_sandbox(T_PointerType p)
  {
    using T_Func = void(void*);
    impl_invoke_with_func_ptr<T_Func, T_Func>(
      reinterpret_cast<T_Func*>(free_index), p);
  }

  static inline std::pair<rlbox_segmentsfi_sandbox*, void*>
  impl_get_executed_callback_sandbox_and_key()
  {
#ifdef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
    auto& thread_data = *get_rlbox_segmentsfi_sandbox_thread_data();
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
public:

  static segmentsfi_sandbox* get_active_segmentinfo_sandbox()
  {
#ifdef RLBOX_EMBEDDER_PROVIDES_TLS_STATIC_VARIABLES
    auto& thread_data = *get_rlbox_segmentsfi_sandbox_thread_data();
#endif
    return thread_data.sandbox->segment_info.get();
  }
};

}
