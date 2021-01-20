#pragma once

#include "ctx_save_trampoline.h"

#include <cstdint>
#include <cstdlib>
#include <dlfcn.h>
#include <mutex>
#ifndef RLBOX_USE_CUSTOM_SHARED_LOCK
#  include <shared_mutex>
#endif
#include <string.h>
#include <utility>

#include "rlbox_helpers.hpp"

#define RLBOX_MPK_UNUSED(...) (void)__VA_ARGS__

extern "C" {
  sandbox_thread_ctx** get_sandbox_current_thread_app_ctx();
  sandbox_thread_ctx** get_sandbox_current_thread_sbx_ctx();
}

namespace rlbox {

namespace mpk_detail {

  template<typename T>
  constexpr bool false_v = false;

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

  ///////////////////////////////////////////////////////////////

  namespace change_return_type_detail {
    template<typename T, typename T_RetNew>
    struct helper;

    template<typename T_RetNew, typename T_Ret, typename... T_Args>
    struct helper<T_Ret(T_Args...), T_RetNew>
    {
      using type = T_RetNew(T_Args...);
    };
  }

  template<typename T_Func, typename T_RetNew>
  using change_return_type =
    typename change_return_type_detail::helper<T_Func, T_RetNew>::type;

  ///////////////////////////////////////////////////////////////

  namespace change_class_arg_types_detail {
    template<typename T, typename T_ArgNew>
    struct helper;

    template<typename T_ArgNew, typename T_Ret, typename... T_Args>
    struct helper<T_Ret(T_Args...), T_ArgNew>
    {
      using type =
        T_Ret(std::conditional_t<std::is_class_v<T_Args>, T_ArgNew, T_Args>...);
    };
  }

  template<typename T_Func, typename T_ArgNew>
  using change_class_arg_types =
    typename change_class_arg_types_detail::helper<T_Func, T_ArgNew>::type;
}

class rlbox_mpk_sandbox;

struct rlbox_mpk_sandbox_thread_data
{
  rlbox_mpk_sandbox* sandbox;
  uint32_t last_callback_invoked;
  sandbox_thread_ctx* sandbox_current_thread_app_ctx;
  sandbox_thread_ctx* sandbox_current_thread_sbx_ctx;
};

#  define RLBOX_MPK_SANDBOX_STATIC_VARIABLES()                                \
    extern "C" {                                                                                    \
      thread_local rlbox::rlbox_mpk_sandbox_thread_data* rlbox_mpk_sandbox_thread_info_ptr =        \
        (rlbox::rlbox_mpk_sandbox_thread_data*) malloc(sizeof(rlbox::rlbox_mpk_sandbox_thread_data));                                     \
      rlbox::rlbox_mpk_sandbox_thread_data* get_rlbox_mpk_sandbox_thread_data()                 \
      {                                                                                             \
        return rlbox_mpk_sandbox_thread_info_ptr;                                                 \
      }                                                                                             \
    }                                                                                               \
    static_assert(true, "Enforce semi-colon")

extern "C" {
    rlbox_mpk_sandbox_thread_data* get_rlbox_mpk_sandbox_thread_data();
}

#define SET_MPK_PERMISSIONS(pkru) {}

// #define SET_MPK_PERMISSIONS(pkru)                       \
//   {                                                     \
//       unsigned int eax = pkru;                          \
//       unsigned int ecx = 0;                             \
//       unsigned int edx = 0;                             \
//       asm volatile(".byte 0x0f,0x01,0xef\n\t"           \
//                   : : "a" (eax), "c" (ecx), "d" (edx)); \
//   }


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
  using T_PointerType = void*;
  using T_ShortType = short;
  // You can transfer buffers at the page level with mpk
  // But this is too expensive as it involves a syscall
  // Copies are usually faster, so no transfer support
  // using can_grant_deny_access = void;

private:
  void* sandbox = nullptr;
  size_t return_slot_size = 0;
  T_PointerType return_slot = 0;

  const uint32_t mpk_app_domain_perms = 0;
  // 0b1100 --- disallow access to domain 1
  const uint32_t mpk_sbx_domain_perms = 12;

  char* sandbox_stack_pointer = 0;
  char* curr_sandbox_stack_pointer = 0;

  RLBOX_SHARED_LOCK(callback_mutex);
  static inline const uint32_t MAX_CALLBACKS = 64;
  void* callback_unique_keys[MAX_CALLBACKS]{ 0 };
  void* callbacks[MAX_CALLBACKS]{ 0 };

  template<uint32_t N, typename T_Ret, typename... T_Args>
  static T_Ret callback_trampoline(T_Args... params)
  {
    #ifndef RLBOX_ZEROCOST_NOSWITCHSTACK
      auto& sandbox_current_thread_app_ctx = *get_sandbox_current_thread_app_ctx();
      const auto stack_param_size = get_stack_param_size<0, 0>(callback_trampoline<N, T_Ret, T_Args...>);
      const auto stack_param_ret_size = stack_param_size + sizeof(uintptr_t) + 16;
      const auto curr_sbx_stack = save_sbx_stack_and_switch_to_app_stack(sandbox_current_thread_app_ctx->rsp, stack_param_ret_size);
    #endif

    auto& sandbox_current_thread_sbx_ctx = *get_sandbox_current_thread_sbx_ctx();
    sandbox_current_thread_sbx_ctx->rip = reinterpret_cast<uint64_t>(__builtin_extract_return_addr (__builtin_return_address (0)));
    auto& thread_data = *get_rlbox_mpk_sandbox_thread_data();

    SET_MPK_PERMISSIONS(thread_data.sandbox->mpk_app_domain_perms);

    #ifndef RLBOX_ZEROCOST_NOSWITCHSTACK
      const auto prev_sbx_stack = thread_data.sandbox->curr_sandbox_stack_pointer;
      thread_data.sandbox->curr_sandbox_stack_pointer = (char*) curr_sbx_stack;
      // keep stack 16 byte aligned
      thread_data.sandbox->curr_sandbox_stack_pointer -= (reinterpret_cast<uintptr_t>(thread_data.sandbox->curr_sandbox_stack_pointer) % 16);
    #endif

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
      #ifndef RLBOX_ZEROCOST_NOSWITCHSTACK
        set_return_target(reinterpret_cast<uint64_t>(__builtin_frame_address(0)), reinterpret_cast<uint64_t>(context_switch_to_sbx_callback));
        thread_data.sandbox->curr_sandbox_stack_pointer = prev_sbx_stack;
      #else
        set_return_target(reinterpret_cast<uint64_t>(__builtin_frame_address(0)), reinterpret_cast<uint64_t>(context_switch_to_sbx_callback_noswitchstack));
      #endif
    } else {
      auto ret = func(params...);
      push_return(ret);
      #ifndef RLBOX_ZEROCOST_NOSWITCHSTACK
        set_return_target(reinterpret_cast<uint64_t>(__builtin_frame_address(0)), reinterpret_cast<uint64_t>(context_switch_to_sbx_callback));
        thread_data.sandbox->curr_sandbox_stack_pointer = prev_sbx_stack;
      #else
        set_return_target(reinterpret_cast<uint64_t>(__builtin_frame_address(0)), reinterpret_cast<uint64_t>(context_switch_to_sbx_callback_noswitchstack));
      #endif
      return ret;
    }
  }

  void ensure_return_slot_size(size_t size)
  {
    if (size > return_slot_size) {
      if (return_slot_size) {
        impl_free_in_sandbox(return_slot);
      }
      return_slot = impl_malloc_in_sandbox(size);
      detail::dynamic_check(
        return_slot != 0,
        "Error initializing return slot. Sandbox may be out of memory!");
      return_slot_size = size;
    }
  }

  template<typename T_Arg>
  static inline uint64_t serialize_to_uint64(T_Arg arg) {
    uint64_t val = 0;
    // memcpy will be removed by any decent compiler
    if constexpr(sizeof(T_Arg) == 8) {
      memcpy(&val, &arg, sizeof(T_Arg));
    } else if constexpr(sizeof(T_Arg) == 4){
      uint32_t tmp = 0;
      memcpy(&tmp , &arg, sizeof(T_Arg));
      val = tmp;
    }
    return val;
  }

  template<size_t T_IntegerNum, size_t T_FloatNum, typename T_Ret, typename... T_FormalArgs>
  static inline size_t get_stack_param_size(T_Ret(*)(T_FormalArgs...)) { return 0; }

  template<size_t T_IntegerNum, size_t T_FloatNum, typename T_Ret, typename T_FormalArg, typename... T_FormalArgs>
  static inline size_t get_stack_param_size(T_Ret(*)(T_FormalArg, T_FormalArgs...)) {
    size_t curr_val = 0;

    if constexpr (std::is_integral_v<T_FormalArg> || std::is_pointer_v<T_FormalArg> || std::is_reference_v<T_FormalArg> || std::is_enum_v<T_FormalArg>) {
      if constexpr (T_IntegerNum > 5) {
        curr_val = 8;
      }
      auto ret = curr_val + get_stack_param_size<T_IntegerNum + 1, T_FloatNum>(reinterpret_cast<T_Ret(*)(T_FormalArgs...)>(0));
      return ret;
    } else if constexpr (std::is_same_v<T_FormalArg, float> || std::is_same_v<T_FormalArg, double>) {
      if constexpr (T_FloatNum > 7) {
        curr_val = 8;
      }
      auto ret = curr_val + get_stack_param_size<T_IntegerNum, T_FloatNum + 1>(reinterpret_cast<T_Ret(*)(T_FormalArgs...)>(0));
      return ret;
    } else if constexpr (std::is_class_v<T_FormalArg>) {
      auto ret = sizeof(T_FormalArg) + get_stack_param_size<T_IntegerNum, T_FloatNum>(reinterpret_cast<T_Ret(*)(T_FormalArgs...)>(0));
      return ret;
    } else {
      static_assert(mpk_detail::false_v<T_Ret>, "Unknown case");
    }
  }

  // push's parameters into the target context registers
  // first param is an in out parameter: current position of the stack pointer
  template<size_t T_IntegerNum, size_t T_FloatNum, typename T_Ret, typename... T_FormalArgs, typename... T_ActualArgs>
  static inline void push_parameters(char* stack_pointer, T_Ret(*)(T_FormalArgs...), T_ActualArgs&&...) { }

  template<size_t T_IntegerNum, size_t T_FloatNum, typename T_Ret, typename T_FormalArg, typename... T_FormalArgs, typename T_ActualArg, typename... T_ActualArgs>
  static inline void push_parameters(char* stack_pointer, T_Ret(*)(T_FormalArg, T_FormalArgs...), T_ActualArg&& arg, T_ActualArgs&&... args) {
    T_FormalArg arg_conv = arg;
    auto& sandbox_current_thread_sbx_ctx = *get_sandbox_current_thread_sbx_ctx();
    uint64_t u64val = serialize_to_uint64(arg_conv);

    if constexpr (std::is_integral_v<T_FormalArg> || std::is_pointer_v<T_FormalArg> || std::is_reference_v<T_FormalArg> || std::is_enum_v<T_FormalArg>) {

      if constexpr (T_IntegerNum == 0) {
        sandbox_current_thread_sbx_ctx->rdi = u64val;
      } else if constexpr (T_IntegerNum == 1) {
        sandbox_current_thread_sbx_ctx->rsi = u64val;
      } else if constexpr (T_IntegerNum == 2) {
        sandbox_current_thread_sbx_ctx->rdx = u64val;
      } else if constexpr (T_IntegerNum == 3) {
        sandbox_current_thread_sbx_ctx->rcx = u64val;
      } else if constexpr (T_IntegerNum == 4) {
        sandbox_current_thread_sbx_ctx->r8 = u64val;
      } else if constexpr (T_IntegerNum == 5) {
        sandbox_current_thread_sbx_ctx->r9 = u64val;
      } else {
        #ifndef RLBOX_ZEROCOST_NOSWITCHSTACK
          memcpy(stack_pointer, &u64val, sizeof(u64val));
          stack_pointer += sizeof(u64val);
        #endif
      }

      push_parameters<T_IntegerNum + 1, T_FloatNum>(stack_pointer, reinterpret_cast<T_Ret(*)(T_FormalArgs...)>(0), std::forward<T_ActualArgs>(args)...);

    } else if constexpr (std::is_same_v<T_FormalArg, float> || std::is_same_v<T_FormalArg, double>) {

      if constexpr (T_FloatNum == 0) {
        sandbox_current_thread_sbx_ctx->xmm0 = u64val;
      } else if constexpr (T_FloatNum == 1) {
        sandbox_current_thread_sbx_ctx->xmm1 = u64val;
      } else if constexpr (T_FloatNum == 2) {
        sandbox_current_thread_sbx_ctx->xmm2 = u64val;
      } else if constexpr (T_FloatNum == 3) {
        sandbox_current_thread_sbx_ctx->xmm3 = u64val;
      } else if constexpr (T_FloatNum == 4) {
        sandbox_current_thread_sbx_ctx->xmm4 = u64val;
      } else if constexpr (T_FloatNum == 5) {
        sandbox_current_thread_sbx_ctx->xmm5 = u64val;
      } else if constexpr (T_FloatNum == 6) {
        sandbox_current_thread_sbx_ctx->xmm6 = u64val;
      } else if constexpr (T_FloatNum == 7) {
        sandbox_current_thread_sbx_ctx->xmm7 = u64val;
      } else {
        #ifndef RLBOX_ZEROCOST_NOSWITCHSTACK
          memcpy(stack_pointer, &u64val, sizeof(u64val));
          stack_pointer += sizeof(u64val);
        #endif
      }

      push_parameters<T_IntegerNum, T_FloatNum + 1>(stack_pointer, reinterpret_cast<T_Ret(*)(T_FormalArgs...)>(0), std::forward<T_ActualArgs>(args)...);
    } else if constexpr (std::is_class_v<T_FormalArg>) {
        #ifndef RLBOX_ZEROCOST_NOSWITCHSTACK
          memcpy(stack_pointer, &arg_conv, sizeof(arg_conv));
          stack_pointer += sizeof(arg_conv);
        #endif

        push_parameters<T_IntegerNum, T_FloatNum>(stack_pointer, reinterpret_cast<T_Ret(*)(T_FormalArgs...)>(0), std::forward<T_ActualArgs>(args)...);
    } else {
      static_assert(mpk_detail::false_v<T_Ret>, "Unknown case");
    }
  }

  template<typename T_Ret>
  static inline void push_return(T_Ret ret) {
    auto& sandbox_current_thread_sbx_ctx = *get_sandbox_current_thread_sbx_ctx();
    if constexpr (std::is_integral_v<T_Ret> || std::is_pointer_v<T_Ret>) {
      uint64_t val = serialize_to_uint64(ret);
      sandbox_current_thread_sbx_ctx->rax = val;
    } else if constexpr (std::is_same_v<T_Ret, float> || std::is_same_v<T_Ret, double>) {
      uint64_t val = serialize_to_uint64(ret);
      sandbox_current_thread_sbx_ctx->xmm0 = val;
    } else {
      static_assert(mpk_detail::false_v<T_Ret>, "WASM should not have class returns");
    }
  }

protected:
  inline void impl_create_sandbox(const char* path) {
    sandbox = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    if (sandbox == nullptr) {
      char* error = dlerror();
      detail::dynamic_check(sandbox != nullptr, error);
    }

    #ifndef RLBOX_ZEROCOST_NOSWITCHSTACK
      // allocate a 16M sandbox stack by default
      const uint64_t stack_size = 16*1024*1024;
      sandbox_stack_pointer = new char[stack_size];
      detail::dynamic_check(sandbox_stack_pointer != nullptr, "Could not allocate sandbox stack");
      curr_sandbox_stack_pointer = sandbox_stack_pointer + stack_size;
      // keep stack 16 byte aligned
      curr_sandbox_stack_pointer -= (reinterpret_cast<uintptr_t>(curr_sandbox_stack_pointer) % 16);
    #endif
  }

  inline void impl_destroy_sandbox() {
    dlclose(sandbox);
    #ifndef RLBOX_ZEROCOST_NOSWITCHSTACK
      delete[] sandbox_stack_pointer;
    #endif
  }

  template<typename T>
  inline void* impl_get_unsandboxed_pointer(T_PointerType p) const
  {
    return p;
  }

  template<typename T>
  inline T_PointerType impl_get_sandboxed_pointer(const void* p) const
  {
    return const_cast<T_PointerType>(p);
  }

  template<typename T>
  static inline void* impl_get_unsandboxed_pointer_no_ctx(
    T_PointerType p,
    const void* /* example_unsandboxed_ptr */,
    rlbox_mpk_sandbox* (*/* expensive_sandbox_finder */)(
      const void* example_unsandboxed_ptr))
  {
    return p;
  }

  template<typename T>
  static inline T_PointerType impl_get_sandboxed_pointer_no_ctx(
    const void* p,
    const void* /* example_unsandboxed_ptr */,
    rlbox_mpk_sandbox* (*/* expensive_sandbox_finder */)(
      const void* example_unsandboxed_ptr))
  {
    return const_cast<T_PointerType>(p);
  }

  inline T_PointerType impl_malloc_in_sandbox(size_t size)
  {
    void* p = malloc(size);
    return p;
  }

  inline void impl_free_in_sandbox(T_PointerType p)
  {
    free(p);
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
    auto& thread_data = *get_rlbox_mpk_sandbox_thread_data();
    thread_data.sandbox = this;

    // Functions are mangled in the following manner
    // 1. All primitive types are left as is
    // 2. All pointers are changed to u64 types
    // 3. Returned class are returned as an out parameter before the actual
    // function parameters
    //
    // RLBox accounts for the first 2 differences in T_Converted type, but we
    // need to handle the rest

    // Handle point 3
    using T_Ret = mpk_detail::return_argument<T_Converted>;
    if constexpr (std::is_class_v<T_Ret>) {
      using T_Conv1 = mpk_detail::change_return_type<T_Converted, void>;
      using T_Conv2 = mpk_detail::prepend_arg_type<T_Conv1, T_PointerType>;
      auto func_ptr_conv =
        reinterpret_cast<T_Conv2*>(reinterpret_cast<uintptr_t>(func_ptr));
      ensure_return_slot_size(sizeof(T_Ret));
      impl_invoke_with_func_ptr<T>(func_ptr_conv, return_slot, params...);

      auto ptr = reinterpret_cast<T_Ret*>(
        impl_get_unsandboxed_pointer<T_Ret*>(return_slot));
      T_Ret ret = *ptr;
      return ret;
    }

    auto& sandbox_current_thread_sbx_ctx = *get_sandbox_current_thread_sbx_ctx();
    auto& sandbox_current_thread_app_ctx = *get_sandbox_current_thread_app_ctx();

    sandbox_thread_ctx app_ctx {0};
    sandbox_thread_ctx sbx_ctx {0};
    sbx_ctx.mxcsr = 0x1f80;
    sandbox_thread_ctx* old_app_ctx = sandbox_current_thread_app_ctx;
    sandbox_thread_ctx* old_sbx_ctx = sandbox_current_thread_sbx_ctx;
    sandbox_current_thread_app_ctx = &app_ctx;
    sandbox_current_thread_sbx_ctx = &sbx_ctx;

    using T_ConvHeap = mpk_detail::prepend_arg_type<T_Converted, uint64_t>;

    // Function invocation
    auto func_ptr_conv =
      reinterpret_cast<T_ConvHeap*>(reinterpret_cast<uintptr_t>(func_ptr));

    #ifdef RLBOX_ZEROCOST_WINDOWSMODE
      #if defined(RLBOX_ZEROCOST_NOSWITCHSTACK)
        #error "Zerocost: disabling stack switching is not supported in windows mode"
      #endif
      auto context_switcher =
        reinterpret_cast<T_ConvHeap*>(reinterpret_cast<uintptr_t>(context_switch_to_sbx_func_windowsmode));
    #elif !defined(RLBOX_ZEROCOST_NOSWITCHSTACK)
      auto context_switcher =
        reinterpret_cast<T_ConvHeap*>(reinterpret_cast<uintptr_t>(context_switch_to_sbx_func));
    #else
      auto context_switcher =
        reinterpret_cast<T_ConvHeap*>(reinterpret_cast<uintptr_t>(context_switch_to_sbx_func_noswitchstack));
    #endif

    using T_NoVoidRet =
      std::conditional_t<std::is_void_v<T_Ret>, uint32_t, T_Ret>;
    T_NoVoidRet ret;

    sandbox_current_thread_sbx_ctx->rip = reinterpret_cast<uint64_t>(func_ptr_conv);

    #ifndef RLBOX_ZEROCOST_NOSWITCHSTACK
      char* prev_sandbox_stack_pointer = curr_sandbox_stack_pointer;
      // keep stack 16 byte aligned
      const auto stack_param_size = get_stack_param_size<0, 0>(func_ptr_conv);
      curr_sandbox_stack_pointer -= stack_param_size;
      const auto stack_correction = (16 - (reinterpret_cast<uintptr_t>(curr_sandbox_stack_pointer) % 16)) % 16;
      curr_sandbox_stack_pointer -= stack_correction;
    #else
      char* curr_sandbox_stack_pointer = nullptr; // dummy
    #endif

    push_parameters<0, 0>(curr_sandbox_stack_pointer /* in-out param */, reinterpret_cast<T_Converted*>(func_ptr_conv), params...);

    #ifndef RLBOX_ZEROCOST_NOSWITCHSTACK
      // make room for return address, which is filled in by the trampoline
      curr_sandbox_stack_pointer -= sizeof(size_t);
      sandbox_current_thread_sbx_ctx->rsp = reinterpret_cast<uintptr_t>(curr_sandbox_stack_pointer);
    #endif

    SET_MPK_PERMISSIONS(mpk_sbx_domain_perms);

    #ifndef RLBOX_ZEROCOST_NOSWITCHSTACK
    #else
      #error "Todo: Need to implement first integer reg param skip to use rlbox_mpk without stack switching."
    #endif

    if constexpr (std::is_void_v<T_Ret>) {
      RLBOX_MPK_UNUSED(ret);
      context_switcher(reinterpret_cast<uint64_t>(&thread_data), params...);
    } else {
      ret = context_switcher(reinterpret_cast<uint64_t>(&thread_data), params...);
    }

    #ifndef RLBOX_ZEROCOST_NOSWITCHSTACK
      // restore the old stack pointer
      curr_sandbox_stack_pointer = prev_sandbox_stack_pointer;
    #endif

    sandbox_current_thread_app_ctx = old_app_ctx;
    sandbox_current_thread_sbx_ctx = old_sbx_ctx;

    if constexpr (!std::is_void_v<T_Ret>) {
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

  static inline std::pair<rlbox_mpk_sandbox*, void*>
  impl_get_executed_callback_sandbox_and_key()
  {
    auto& thread_data = *get_rlbox_mpk_sandbox_thread_data();
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
