// web_fncast.h - BO1_FNCAST for the web build (universal/q_shared.h defines the macro for both builds).
//
// Decompiled code calls functions through casts to other function types: a function taking (name) is called as
// (name, alloc), a 3-parameter allocator through a 4-parameter table slot, a 1-parameter callback as (header, data).
// x86 cdecl tolerates this (the caller pushes and pops the arguments; the callee reads what it declares). WebAssembly
// does not: an indirect call whose type differs from the callee's traps ("function signature mismatch").
//
// BO1_FNCAST(T, fn) is the plain cast `(T)(fn)` on Windows. On the web it is a thunk of exactly type T that calls fn
// with the arguments fn declares - each converted the way the x86 stack slot would be reinterpreted (same size: the
// same bits; integers / floats: a value conversion), missing ones zero - and converts fn's result back to T's
// (void -> zero). fn must be a function name (a constant).
#pragma once
#ifdef __cplusplus
#include <bit>
#include <cstddef>
#include <cstring>
#include <tuple>
#include <type_traits>
#include <utility>

template <typename F> struct bo1_web_fn_traits;
template <typename R, typename... A> struct bo1_web_fn_traits<R (*)(A...)>
{
    using ret = R;
    using args = std::tuple<A...>;
    static constexpr std::size_t arity = sizeof...(A);
};

template <typename To, typename From> inline To bo1_web_fn_conv(const From &v)
{
    using T = std::remove_cv_t<To>;
    using F = std::remove_cv_t<From>;
    if constexpr (std::is_same_v<T, F>)
        return v;
    else if constexpr (std::is_same_v<T, bool> && std::is_integral_v<F>)
        return (v & 0xFF) != 0;   // a bool is read from the low byte (al)
    else if constexpr (std::is_arithmetic_v<T> && std::is_arithmetic_v<F>)
        return static_cast<To>(v);
    else if constexpr (sizeof(T) == sizeof(F) && std::is_trivially_copyable_v<T> && std::is_trivially_copyable_v<F>)
        return std::bit_cast<T>(v);
    else if constexpr (std::is_convertible_v<F, T>)
        return static_cast<To>(v);
    else
        return T{};
}

template <typename To, auto Fn> struct bo1_web_fn_thunk;

template <typename R, typename... A, auto Fn> struct bo1_web_fn_thunk<R (*)(A...), Fn>
{
    using FnTraits = bo1_web_fn_traits<std::remove_cv_t<decltype(Fn)>>;

    template <std::size_t I, typename P> static P Arg(const std::tuple<A...> &in)
    {
        if constexpr (I < sizeof...(A))
            return bo1_web_fn_conv<P>(std::get<I>(in));
        else
            return P{};
    }

    template <std::size_t... I> static R Invoke(const std::tuple<A...> &in, std::index_sequence<I...>)
    {
        using FR = typename FnTraits::ret;
        if constexpr (std::is_void_v<FR>)
        {
            Fn(Arg<I, std::tuple_element_t<I, typename FnTraits::args>>(in)...);
            if constexpr (!std::is_void_v<R>)
                return R{};
        }
        else
        {
            FR r = Fn(Arg<I, std::tuple_element_t<I, typename FnTraits::args>>(in)...);
            if constexpr (!std::is_void_v<R>)
                return bo1_web_fn_conv<R>(r);
        }
    }

    static R call(A... a)
    {
        const std::tuple<A...> in(a...);
        return Invoke(in, std::make_index_sequence<FnTraits::arity>{});
    }
};

template <typename To, auto Fn> constexpr To bo1_web_fncast()
{
    if constexpr (std::is_same_v<std::remove_cv_t<To>, std::remove_cv_t<decltype(Fn)>>)
        return Fn;
    else
        return &bo1_web_fn_thunk<To, Fn>::call;
}
#endif
