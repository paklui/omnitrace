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

#include "api.hpp"
#include "library/common.hpp"
#include "library/components/exit_gotcha.hpp"
#include "library/components/fork_gotcha.hpp"
#include "library/components/mpi_gotcha.hpp"
#include "library/components/pthread_gotcha.hpp"
#include "library/components/roctracer.hpp"
#include "library/defines.hpp"
#include "library/state.hpp"
#include "library/thread_data.hpp"
#include "library/timemory.hpp"

#include <timemory/backends/threading.hpp>
#include <timemory/macros/language.hpp>

#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>

namespace omnitrace
{
// started during preinit phase
using preinit_bundle_t =
    tim::lightweight_tuple<exit_gotcha_t, fork_gotcha_t, mpi_gotcha_t>;

// started during init phase
using init_bundle_t = tim::lightweight_tuple<pthread_gotcha>;

// bundle of components around omnitrace_init and omnitrace_finalize
using main_bundle_t =
    tim::lightweight_tuple<comp::wall_clock, comp::peak_rss, comp::page_rss,
                           comp::cpu_clock, comp::cpu_util>;

// bundle of components around each thread
#if defined(TIMEMORY_RUSAGE_THREAD) && TIMEMORY_RUSAGE_THREAD > 0
using thread_bundle_t = tim::lightweight_tuple<comp::wall_clock, comp::thread_cpu_clock,
                                               comp::thread_cpu_util, comp::peak_rss>;
#else
using thread_bundle_t = tim::lightweight_tuple<comp::wall_clock, comp::thread_cpu_clock,
                                               comp::thread_cpu_util>;
#endif

std::unique_ptr<main_bundle_t>&
get_main_bundle();

std::unique_ptr<init_bundle_t>&
get_init_bundle();

std::unique_ptr<preinit_bundle_t>&
get_preinit_bundle();

int
get_realtime_signal();

int
get_cputime_signal();

std::set<int>
get_sampling_signals(int64_t _tid = 0);

std::atomic<uint64_t>&
get_cpu_cid() TIMEMORY_HOT;

unique_ptr_t<std::vector<uint64_t>>&
get_cpu_cid_stack(int64_t _tid = threading::get_id(), int64_t _parent = 0) TIMEMORY_HOT;

using cpu_cid_data_t       = std::tuple<uint64_t, uint64_t, uint32_t>;
using cpu_cid_pair_t       = std::tuple<uint64_t, uint32_t>;
using cpu_cid_parent_map_t = std::unordered_map<uint64_t, cpu_cid_pair_t>;

unique_ptr_t<cpu_cid_parent_map_t>&
get_cpu_cid_parents(int64_t _tid = threading::get_id()) TIMEMORY_HOT;

cpu_cid_data_t
create_cpu_cid_entry(int64_t _tid = threading::get_id()) TIMEMORY_HOT;

cpu_cid_pair_t
get_cpu_cid_entry(uint64_t _cid, int64_t _tid = threading::get_id()) TIMEMORY_HOT;

tim::mutex_t&
get_cpu_cid_stack_lock(int64_t _tid = threading::get_id()) TIMEMORY_HOT;

ThreadState&
get_thread_state() TIMEMORY_HOT;

/// returns old state
ThreadState set_thread_state(ThreadState) TIMEMORY_HOT;

ThreadState push_thread_state(ThreadState) TIMEMORY_HOT;

ThreadState
pop_thread_state() TIMEMORY_HOT;

struct scoped_thread_state
{
    scoped_thread_state(ThreadState _v) { push_thread_state(_v); }
    ~scoped_thread_state() { pop_thread_state(); }
};

// query current value
bool
sampling_enabled_on_child_threads();

// use this to disable sampling in a region (e.g. right before thread creation)
bool
push_enable_sampling_on_child_threads(bool _v);

// use this to restore previous setting
bool
pop_enable_sampling_on_child_threads();

// make sure every newly created thead starts with this value
void
set_sampling_on_all_future_threads(bool _v);

struct scoped_child_sampling
{
    scoped_child_sampling(bool _v) { push_enable_sampling_on_child_threads(_v); }
    ~scoped_child_sampling() { pop_enable_sampling_on_child_threads(); }
};
}  // namespace omnitrace

#define OMNITRACE_SCOPED_THREAD_STATE(STATE)                                             \
    ::omnitrace::scoped_thread_state OMNITRACE_VARIABLE(_scoped_thread_state_, __LINE__) \
    {                                                                                    \
        ::omnitrace::STATE                                                               \
    }

#define OMNITRACE_SCOPED_SAMPLING_ON_CHILD_THREADS(VALUE)                                \
    ::omnitrace::scoped_child_sampling OMNITRACE_VARIABLE(_scoped_child_sampling_,       \
                                                          __LINE__)                      \
    {                                                                                    \
        VALUE                                                                            \
    }
