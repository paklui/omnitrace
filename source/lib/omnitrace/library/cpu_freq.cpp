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

#include "library/cpu_freq.hpp"
#include "library/common.hpp"
#include "library/components/cpu_freq.hpp"
#include "library/components/fwd.hpp"
#include "library/config.hpp"
#include "library/debug.hpp"
#include "library/defines.hpp"
#include "library/perfetto.hpp"
#include "library/thread_data.hpp"
#include "library/thread_info.hpp"
#include "library/timemory.hpp"

#include <timemory/components/rusage/backends.hpp>
#include <timemory/mpl/types.hpp>
#include <timemory/units.hpp>
#include <timemory/utility/procfs/cpuinfo.hpp>
#include <timemory/utility/type_list.hpp>

#include <cstddef>
#include <cstdlib>
#include <string>
#include <sys/resource.h>
#include <tuple>
#include <utility>
#include <vector>

namespace omnitrace
{
namespace cpu_freq
{
using namespace ::tim::cpu_freq;
using cpu_freq_component = component::cpu_freq;

template <typename... Tp>
using type_list = tim::type_list<Tp...>;

namespace
{
using cpu_data_tuple_t = std::tuple<size_t, int64_t, int64_t, int64_t, int64_t, int64_t,
                                    int64_t, int64_t, cpu_freq_component>;
std::deque<cpu_data_tuple_t> cpu_data = {};

template <typename... Types>
void init_perfetto_counter_tracks(type_list<Types...>)
{
    (perfetto_counter_track<Types>::init(), ...);
}
}  // namespace
}  // namespace cpu_freq
}  // namespace omnitrace

namespace omnitrace
{
namespace cpu_freq
{
void
setup()
{
    init_perfetto_counter_tracks(
        type_list<cpu_freq_component, cpu_page, cpu_virt, cpu_peak, cpu_context_switch,
                  cpu_page_fault, cpu_user_mode_time, cpu_kernel_mode_time>{});
}

void
config()
{
    cpu_freq_component::configure();
}

void
sample()
{
    auto _ts = tim::get_clock_real_now<size_t, std::nano>();

    auto _rcache = tim::rusage_cache{ RUSAGE_SELF };
    auto _freqs  = cpu_freq_component{}.sample();

    // user and kernel mode times are in microseconds
    cpu_data.emplace_back(
        _ts, tim::get_page_rss(), tim::get_virt_mem(), _rcache.get_peak_rss(),
        _rcache.get_num_priority_context_switch() +
            _rcache.get_num_voluntary_context_switch(),
        _rcache.get_num_major_page_faults() + _rcache.get_num_minor_page_faults(),
        _rcache.get_user_mode_time() * 1000, _rcache.get_kernel_mode_time() * 1000,
        std::move(_freqs));
}

void
shutdown()
{}

namespace
{
template <typename... Types, size_t N = sizeof...(Types)>
void
config_perfetto_counter_tracks(type_list<Types...>, std::array<const char*, N> _labels,
                               std::array<const char*, N> _units)
{
    static_assert(sizeof...(Types) == N,
                  "Error! Number of types != number of labels/units");

    auto _config = [&](auto _t) {
        using type          = std::decay_t<decltype(_t)>;
        using track         = perfetto_counter_track<type>;
        constexpr auto _idx = tim::index_of<type, type_list<Types...>>::value;
        if(!track::exists(0))
        {
            auto addendum = [&](const char* _v) { return JOIN(" ", "CPU", _v, "(S)"); };
            track::emplace(0, addendum(_labels.at(_idx)), _units.at(_idx));
        }
    };

    (_config(Types{}), ...);
}

struct index
{
    size_t value = 0;
};

template <typename Tp, typename... Args>
void
write_perfetto_counter_track(Args... _args)
{
    using track = perfetto_counter_track<Tp>;
    TRACE_COUNTER(trait::name<Tp>::value, track::at(0, 0), _args...);
}

template <typename Tp, typename... Args>
void
write_perfetto_counter_track(index&& _idx, Args... _args)
{
    using track = perfetto_counter_track<Tp>;
    TRACE_COUNTER(trait::name<Tp>::value, track::at(_idx.value, 0), _args...);
}
}  // namespace

void
post_process()
{
    OMNITRACE_VERBOSE(1,
                      "Post-processing %zu cpu frequency and memory usage entries...\n",
                      cpu_data.size());
    auto _process_frequencies = [](size_t _idx, size_t _offset) {
        using freq_track = perfetto_counter_track<cpu_freq_component>;

        const auto& _thread_info = thread_info::get(0, LookupTID);
        OMNITRACE_CI_THROW(!_thread_info, "Missing thread info for thread 0");
        if(!_thread_info) return;

        if(!freq_track::exists(_idx))
        {
            auto addendum = [&](const char* _v) {
                return JOIN(" ", "CPU", _v, JOIN("", '[', _idx, ']'), "(S)");
            };
            freq_track::emplace(_idx, addendum("Frequency"), "MHz");
        }

        for(auto& itr : cpu_data)
        {
            uint64_t _ts   = std::get<0>(itr);
            double   _freq = std::get<8>(itr).at(_offset);
            if(!_thread_info->is_valid_time(_ts)) continue;
            write_perfetto_counter_track<cpu_freq_component>(index{ _idx }, _ts, _freq);
        }

        auto _end_ts = _thread_info->get_stop();
        write_perfetto_counter_track<cpu_freq_component>(index{ _idx }, _end_ts, 0);
    };

    auto _process_cpu_rusage = []() {
        config_perfetto_counter_tracks(
            type_list<cpu_page, cpu_virt, cpu_peak, cpu_context_switch, cpu_page_fault,
                      cpu_user_mode_time, cpu_kernel_mode_time>{},
            { "Memory Usage", "Virtual Memory Usage", "Peak Memory", "Context Switches",
              "Page Faults", "User Time", "Kernel Time" },
            { "MB", "MB", "MB", "", "", "sec", "sec" });

        const auto& _thread_info = thread_info::get(0, LookupTID);
        OMNITRACE_CI_THROW(!_thread_info, "Missing thread info for thread 0");
        if(!_thread_info) return;

        for(auto& itr : cpu_data)
        {
            uint64_t _ts = std::get<0>(itr);
            if(!_thread_info->is_valid_time(_ts)) continue;

            double   _page = std::get<1>(itr);
            double   _virt = std::get<2>(itr);
            double   _peak = std::get<3>(itr);
            uint64_t _cntx = std::get<4>(itr);
            uint64_t _flts = std::get<5>(itr);
            double   _user = std::get<6>(itr);
            double   _kern = std::get<7>(itr);
            write_perfetto_counter_track<cpu_page>(_ts, _page / units::megabyte);
            write_perfetto_counter_track<cpu_virt>(_ts, _virt / units::megabyte);
            write_perfetto_counter_track<cpu_peak>(_ts, _peak / units::megabyte);
            write_perfetto_counter_track<cpu_context_switch>(_ts, _cntx);
            write_perfetto_counter_track<cpu_page_fault>(_ts, _flts);
            write_perfetto_counter_track<cpu_user_mode_time>(_ts, _user / units::sec);
            write_perfetto_counter_track<cpu_kernel_mode_time>(_ts, _kern / units::sec);
        }

        auto _end_ts = _thread_info->get_stop();
        write_perfetto_counter_track<cpu_page>(_end_ts, 0.0);
        write_perfetto_counter_track<cpu_virt>(_end_ts, 0.0);
        write_perfetto_counter_track<cpu_peak>(_end_ts, 0.0);
        write_perfetto_counter_track<cpu_context_switch>(_end_ts, 0);
        write_perfetto_counter_track<cpu_page_fault>(_end_ts, 0);
        write_perfetto_counter_track<cpu_user_mode_time>(_end_ts, 0.0);
        write_perfetto_counter_track<cpu_kernel_mode_time>(_end_ts, 0.0);
    };

    _process_cpu_rusage();

    auto& enabled_cpu_freqs = cpu_freq_component::get_enabled_cpus();
    for(auto itr = enabled_cpu_freqs.begin(); itr != enabled_cpu_freqs.end(); ++itr)
    {
        auto _idx    = *itr;
        auto _offset = std::distance(enabled_cpu_freqs.begin(), itr);
        _process_frequencies(_idx, _offset);
    }
    enabled_cpu_freqs.clear();
}
}  // namespace cpu_freq
}  // namespace omnitrace
