// MIT License
//
// Copyright (c) 2022 Advanced Micro Devices, Inc. All Rights Reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "common/defines.h"
#include "common/join.hpp"

#include <atomic>
#include <cstring>
#include <functional>
#include <string>

#if !defined(OMNITRACE_COMMON_LIBRARY_NAME)
#    define OMNITRACE_COMMON_LIBRARY_NAME "common"
#    error OMNITRACE_COMMON_LIBRARY_NAME must be defined
#endif

#if !defined(OMNITRACE_COMMON_LIBRARY_LOG_START)
#    define OMNITRACE_COMMON_LIBRARY_LOG_START
#endif

#if !defined(OMNITRACE_COMMON_LIBRARY_LOG_END)
#    define OMNITRACE_COMMON_LIBRARY_LOG_END
#endif

namespace omnitrace
{
inline namespace common
{
namespace
{
template <typename FuncT, typename... Args>
inline auto
invoke(const char* _name, FuncT&& _func, Args... _args) OMNITRACE_HIDDEN_API;

inline int32_t&
get_guard()
{
    static thread_local int32_t _v = 0;
    return _v;
}

inline int64_t
get_thread_index()
{
    static std::atomic<int64_t> _c{ 0 };
    static thread_local auto    _v = _c++;
    return _v;
}

template <typename... Args>
auto
ignore(const char* _name, int _verbose, int _value, const char* _reason, Args... _args)
{
    if(_verbose >= _value)
    {
        fflush(stderr);
        fprintf(stderr,
                "[omnitrace][" OMNITRACE_COMMON_LIBRARY_NAME
                "][%li] %s(%s) was ignored :: %s\n",
                get_thread_index(), _name, join(QuoteStrings{}, ", ", _args...).c_str(),
                _reason);
        fflush(stderr);
    }
}

template <typename FuncT, typename... Args>
auto
invoke(const char* _name, int _verbose, bool& _toggle, FuncT&& _func, Args... _args)
{
    if(_func)
    {
        struct decrement_guard
        {
            // decrement the guard as it exits the scope
            ~decrement_guard() { --get_guard(); }
        } _unlk{};

        // if _lk is ever greater than zero on the same thread, this
        // means a function within the current function is calling
        // our instrumentation so we ignore the call
        int32_t _lk = get_guard()++;
        if(_lk == 0)
        {
            _toggle = !_toggle;
            if(_verbose >= 3)
            {
                fflush(stderr);
                OMNITRACE_COMMON_LIBRARY_LOG_START
                fprintf(stderr,
                        "[omnitrace][" OMNITRACE_COMMON_LIBRARY_NAME
                        "][%li][%i] %s(%s)\n",
                        get_thread_index(), _lk, _name,
                        join(QuoteStrings{}, ", ", _args...).c_str());
                OMNITRACE_COMMON_LIBRARY_LOG_END
                fflush(stderr);
            }
            return std::invoke(std::forward<FuncT>(_func), _args...);
        }
        else if(_verbose >= 2)
        {
            fflush(stderr);
            OMNITRACE_COMMON_LIBRARY_LOG_START
            fprintf(stderr,
                    "[omnitrace][" OMNITRACE_COMMON_LIBRARY_NAME
                    "][%li] %s(%s) was guarded :: value = %i\n",
                    get_thread_index(), _name,
                    join(QuoteStrings{}, ", ", _args...).c_str(), _lk);
            OMNITRACE_COMMON_LIBRARY_LOG_END
            fflush(stderr);
        }
    }
    else if(_verbose >= 0)
    {
        OMNITRACE_COMMON_LIBRARY_LOG_START
        fprintf(stderr,
                "[omnitrace][" OMNITRACE_COMMON_LIBRARY_NAME
                "][%li] %s(%s) ignored :: null function pointer\n",
                get_thread_index(), _name, join(QuoteStrings{}, ", ", _args...).c_str());
        OMNITRACE_COMMON_LIBRARY_LOG_END
    }

    using return_type = decltype(std::invoke(std::forward<FuncT>(_func), _args...));
    if constexpr(!std::is_void<return_type>::value) return return_type();
}
}  // namespace
}  // namespace common
}  // namespace omnitrace
