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

#include "library/utility.hpp"

#include <timemory/backends/threading.hpp>

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <utility>

namespace omnitrace
{
enum ThreadIdType : int
{
    LookupTID   = 0,
    SystemTID   = 1,
    InternalTID = 2,
};

struct thread_index_data
{
    // the lookup value is always incremented for each thread
    // the system value is the tid provided by the operating system
    // the internal value is the value which the user expects
    int64_t lookup_value   = utility::get_thread_index();
    int64_t system_value   = tim::threading::get_sys_tid();
    int64_t internal_value = tim::threading::get_id();
};

struct thread_info
{
    using index_data_t    = std::optional<thread_index_data>;
    using lifetime_data_t = std::pair<uint64_t, uint64_t>;

    ~thread_info()                  = default;
    thread_info(const thread_info&) = delete;
    thread_info(thread_info&&)      = default;

    thread_info& operator=(const thread_info&) = delete;
    thread_info& operator=(thread_info&&) = default;

    static void set_start(uint64_t, bool _force = false);
    static void set_stop(uint64_t);

    uint64_t get_start() const;
    uint64_t get_stop() const;

    bool            is_valid_time(uint64_t _ts) const;
    bool            is_valid_lifetime(uint64_t _beg, uint64_t _end) const;
    bool            is_valid_lifetime(lifetime_data_t) const;
    lifetime_data_t get_valid_lifetime(lifetime_data_t) const;

    std::string as_string() const;

    static const std::optional<thread_info>& init(bool _offset = false);
    static const std::optional<thread_info>& get();
    static const std::optional<thread_info>& get(int64_t _tid, ThreadIdType _type);

    bool            is_offset  = false;
    index_data_t    index_data = {};
    lifetime_data_t lifetime   = { 0, 0 };

    friend std::ostream& operator<<(std::ostream& _os, const thread_info& _v)
    {
        return (_os << _v.as_string());
    }

private:
    thread_info() = default;
};
}  // namespace omnitrace

namespace std
{
inline std::string
to_string(const omnitrace::thread_info& _info)
{
    return _info.as_string();
}
}  // namespace std
