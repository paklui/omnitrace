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

#include "api.hpp"
#include "common/setup.hpp"
#include "library/components/category_region.hpp"
#include "library/components/exit_gotcha.hpp"
#include "library/components/fork_gotcha.hpp"
#include "library/components/fwd.hpp"
#include "library/components/mpi_gotcha.hpp"
#include "library/components/pthread_gotcha.hpp"
#include "library/components/rocprofiler.hpp"
#include "library/config.hpp"
#include "library/coverage.hpp"
#include "library/critical_trace.hpp"
#include "library/debug.hpp"
#include "library/defines.hpp"
#include "library/gpu.hpp"
#include "library/ompt.hpp"
#include "library/process_sampler.hpp"
#include "library/ptl.hpp"
#include "library/rcclp.hpp"
#include "library/rocprofiler.hpp"
#include "library/runtime.hpp"
#include "library/sampling.hpp"
#include "library/thread_data.hpp"
#include "library/thread_info.hpp"
#include "library/timemory.hpp"
#include "library/tracing.hpp"

#include <timemory/hash/types.hpp>
#include <timemory/operations/types/file_output_message.hpp>
#include <timemory/sampling/signals.hpp>
#include <timemory/utility/backtrace.hpp>
#include <timemory/utility/procfs/maps.hpp>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string_view>

using namespace omnitrace;

//======================================================================================//

namespace
{
auto
ensure_finalization(bool _static_init = false)
{
    const auto& _info = thread_info::init();
    auto        _tid  = _info->index_data;
    OMNITRACE_CI_THROW(_tid->internal_value != threading::get_id(),
                       "Error! internal tid != %li :: %li", threading::get_id(),
                       _tid->internal_value);
    OMNITRACE_CI_THROW(_tid->system_value != threading::get_sys_tid(),
                       "Error! system tid != %li :: %li", threading::get_sys_tid(),
                       _tid->system_value);

    if(!get_env("OMNITRACE_COLORIZED_LOG", true)) tim::log::colorized() = false;

    if(!_static_init)
    {
        OMNITRACE_DEBUG_F("\n");
    }
    else
    {
        OMNITRACE_BASIC_DEBUG_F("\n");
    }

    auto _verbose = get_verbose_env() + ((get_debug_env() || get_debug_init()) ? 16 : 0);
    auto _search_paths = JOIN(':', tim::get_env<std::string>("OMNITRACE_PATH", ""),
                              tim::get_env<std::string>("PWD"), ".",
                              tim::get_env<std::string>("LD_LIBRARY_PATH", ""),
                              tim::get_env<std::string>("LIBRARY_PATH", ""),
                              tim::get_env<std::string>("PATH", ""));
    common::setup_environ(_verbose, _search_paths);

    return scope::destructor{ []() { omnitrace_finalize_hidden(); } };
}

auto
is_system_backend()
{
    // if get_backend() returns 'system' or 'all', this is true
    return (get_backend() != "inprocess");
}

using Device = critical_trace::Device;
using Phase  = critical_trace::Phase;
}  // namespace

//======================================================================================//

extern "C" void
omnitrace_push_trace_hidden(const char* name)
{
    component::category_region<category::host>::start(name);
}

extern "C" void
omnitrace_pop_trace_hidden(const char* name)
{
    component::category_region<category::host>::stop(name);
}

//======================================================================================//
///
///
///
//======================================================================================//

extern "C" void
omnitrace_push_region_hidden(const char* name)
{
    component::category_region<category::user>::start(name);
}

extern "C" void
omnitrace_pop_region_hidden(const char* name)
{
    component::category_region<category::user>::stop(name);
}

//======================================================================================//
///
///
///
//======================================================================================//

namespace
{
struct set_env_s  // NOLINT
{};
}  // namespace

extern "C" void
omnitrace_set_env_hidden(const char* env_name, const char* env_val)
{
    tim::auto_lock_t _lk{ tim::type_mutex<set_env_s>() };

    static auto _set_envs = std::set<std::string_view>{};
    bool        _success  = _set_envs.emplace(env_name).second;

    // just search env to avoid initializing the settings
    OMNITRACE_CONDITIONAL_PRINT_F(get_debug_init() || get_verbose_env() > 2,
                                  "Setting env: %s=%s\n", env_name, env_val);

    tim::set_env(env_name, env_val, 0);

    OMNITRACE_CONDITIONAL_THROW(
        _success && get_state() >= State::Init &&
            (config::get_is_continuous_integration() || get_debug_init()),
        "omnitrace_set_env(\"%s\", \"%s\") called after omnitrace was initialized. state "
        "= %s",
        env_name, env_val, std::to_string(get_state()).c_str());
}

//======================================================================================//
///
///
///
//======================================================================================//

namespace
{
bool                  _set_mpi_called   = false;
std::function<void()> _preinit_callback = []() {};
}  // namespace

extern "C" void
omnitrace_set_mpi_hidden(bool use, bool attached)
{
    static bool _once = false;
    static auto _args = std::make_pair(use, attached);

    // this function may be called multiple times if multiple libraries are instrumented
    // we want to guard against multiple calls which with different arguments
    if(_once && std::tie(_args.first, _args.second) == std::tie(use, attached)) return;
    _once = true;

    // just search env to avoid initializing the settings
    OMNITRACE_CONDITIONAL_PRINT_F(get_debug_init() || get_verbose_env() > 2,
                                  "use: %s, attached: %s\n", (use) ? "y" : "n",
                                  (attached) ? "y" : "n");

    _set_mpi_called       = true;
    config::is_attached() = attached;

    if(use && !attached && get_state() == State::PreInit)
    {
        tim::set_env("OMNITRACE_USE_PID", "ON", 1);
    }
    else if(!use)
    {
        trait::runtime_enabled<mpi_gotcha_t>::set(false);
    }

    OMNITRACE_CONDITIONAL_THROW(
        get_state() >= State::Init &&
            (config::get_is_continuous_integration() || get_debug_init()),
        "omnitrace_set_mpi(use=%s, attached=%s) called after omnitrace was initialized. "
        "state = %s",
        std::to_string(use).c_str(), std::to_string(attached).c_str(),
        std::to_string(get_state()).c_str());

    _preinit_callback();
}

//======================================================================================//

extern "C" void
omnitrace_init_library_hidden()
{
    auto _tid = threading::get_id();
    (void) _tid;

    static bool _once       = false;
    auto        _debug_init = get_debug_init();

    OMNITRACE_CONDITIONAL_BASIC_PRINT_F(_debug_init, "State is %s...\n",
                                        std::to_string(get_state()).c_str());

    OMNITRACE_CI_THROW(get_state() != State::PreInit, "State is not PreInit :: %s",
                       std::to_string(get_state()).c_str());

    if(get_state() != State::PreInit || get_state() == State::Init || _once) return;
    _once = true;

    OMNITRACE_SCOPED_THREAD_STATE(ThreadState::Internal);

    OMNITRACE_CONDITIONAL_BASIC_PRINT_F(_debug_init, "State is %s. Setting to %s...\n",
                                        std::to_string(get_state()).c_str(),
                                        std::to_string(State::Init).c_str());

    OMNITRACE_CONDITIONAL_BASIC_PRINT_F(
        _debug_init, "Calling backtrace once so that the one-time call of malloc in "
                     "glibc's backtrace() occurs...\n");
    {
        std::stringstream _ss{};
        timemory_print_backtrace<16>(_ss);
        (void) _ss;
    }

    set_state(State::Init);

    OMNITRACE_CI_THROW(get_state() != State::Init,
                       "set_state(State::Init) failed. state is %s",
                       std::to_string(get_state()).c_str());

    OMNITRACE_CONDITIONAL_BASIC_PRINT_F(_debug_init, "Configuring settings...\n");

    // configure the settings
    configure_settings();

    auto _debug_value = get_debug();
    if(_debug_init) config::set_setting_value("OMNITRACE_DEBUG", true);
    scope::destructor _debug_dtor{ [_debug_value, _debug_init]() {
        if(_debug_init) config::set_setting_value("OMNITRACE_DEBUG", _debug_value);
    } };

    // below will effectively do:
    //      get_cpu_cid_stack(0)->emplace_back(-1);
    // plus query some env variables
    add_critical_trace<Device::CPU, Phase::NONE>(0, -1, 0, 0, 0, 0, 0, 0, 0, -1, 0);

    tim::trait::runtime_enabled<comp::roctracer>::set(get_use_roctracer());
    tim::trait::runtime_enabled<comp::roctracer_data>::set(get_use_roctracer() &&
                                                           get_use_timemory());

    OMNITRACE_CONDITIONAL_BASIC_PRINT_F(_debug_init, "\n");
}

//======================================================================================//

extern "C" bool
omnitrace_init_tooling_hidden()
{
    if(!tim::get_env("OMNITRACE_INIT_TOOLING", true))
    {
        omnitrace_init_library_hidden();
        return false;
    }

    static bool _once       = false;
    static auto _debug_init = get_debug_init();

    OMNITRACE_CONDITIONAL_BASIC_PRINT_F(_debug_init, "State is %s...\n",
                                        std::to_string(get_state()).c_str());

    if(get_state() != State::PreInit || get_state() == State::Init || _once) return false;
    _once = true;

    OMNITRACE_SCOPED_THREAD_STATE(ThreadState::Internal);

    OMNITRACE_CONDITIONAL_THROW(
        get_state() == State::Init,
        "%s called after omnitrace_init_library() was explicitly called",
        OMNITRACE_FUNCTION);

    OMNITRACE_CONDITIONAL_BASIC_PRINT_F(get_verbose_env() >= 0,
                                        "Instrumentation mode: %s\n",
                                        std::to_string(config::get_mode()).c_str());

    OMNITRACE_CONDITIONAL_BASIC_PRINT_F(_debug_init, "Printing banner...\n");

    if(get_verbose_env() >= 0) print_banner();

    OMNITRACE_CONDITIONAL_BASIC_PRINT_F(_debug_init,
                                        "Calling omnitrace_init_library()...\n");

    omnitrace_init_library_hidden();

    OMNITRACE_DEBUG_F("\n");

    auto _dtor = scope::destructor{ []() {
        // if set to finalized, don't continue
        if(get_state() > State::Active) return;
        if(get_use_process_sampling())
        {
            OMNITRACE_SCOPED_SAMPLING_ON_CHILD_THREADS(false);
            process_sampler::setup();
        }
        if(get_use_sampling())
        {
            OMNITRACE_SCOPED_SAMPLING_ON_CHILD_THREADS(false);
            sampling::setup();
        }
        if(get_use_sampling())
        {
            push_enable_sampling_on_child_threads(get_use_sampling());
            sampling::unblock_signals();
        }
        get_main_bundle()->start();
        set_state(State::Active);  // set to active as very last operation
    } };

    OMNITRACE_SCOPED_SAMPLING_ON_CHILD_THREADS(false);

    // start these gotchas once settings have been initialized
    get_init_bundle()->start();

    if(get_use_sampling()) sampling::block_signals();

    if(get_use_critical_trace())
    {
        // initialize the thread pool
        (void) tasking::critical_trace::get_task_group();
    }

    if(get_use_timemory())
    {
        comp::user_global_bundle::global_init();
        std::set<int> _comps{};
        // convert string into set of enumerations
        for(auto&& itr : tim::delimit(tim::settings::global_components()))
            _comps.emplace(tim::runtime::enumerate(itr));
        if(_comps.size() == 1 && _comps.find(TIMEMORY_WALL_CLOCK) != _comps.end())
        {
            // using wall_clock directly is lower overhead than using it via user_bundle
            instrumentation_bundle_t::get_initializer() =
                [](instrumentation_bundle_t& _bundle) {
                    _bundle.initialize<comp::wall_clock>();
                };
        }
        else if(!_comps.empty())
        {
            // use user_bundle for other than wall-clock
            instrumentation_bundle_t::get_initializer() =
                [](instrumentation_bundle_t& _bundle) {
                    _bundle.initialize<comp::user_global_bundle>();
                };
        }
        else
        {
            tim::trait::runtime_enabled<project::omnitrace>::set(false);
        }
    }

    perfetto::TracingInitArgs               args{};
    perfetto::TraceConfig                   cfg{};
    perfetto::protos::gen::TrackEventConfig track_event_cfg{};

    // perfetto initialization
    if(get_use_perfetto())
    {
        // environment settings
        auto shmem_size_hint = get_perfetto_shmem_size_hint();
        auto buffer_size     = get_perfetto_buffer_size();

        auto _policy =
            get_perfetto_fill_policy() == "discard"
                ? perfetto::protos::gen::TraceConfig_BufferConfig_FillPolicy_DISCARD
                : perfetto::protos::gen::TraceConfig_BufferConfig_FillPolicy_RING_BUFFER;
        auto* buffer_config = cfg.add_buffers();
        buffer_config->set_size_kb(buffer_size);
        buffer_config->set_fill_policy(_policy);

        std::set<std::string> _available_categories = {};
        std::set<std::string> _disabled_categories  = {};
        for(auto itr : { OMNITRACE_PERFETTO_CATEGORIES })
            _available_categories.emplace(itr.name);
        auto _enabled_categories = config::get_perfetto_categories();
        for(const auto& itr : _available_categories)
        {
            if(!_enabled_categories.empty() && _enabled_categories.count(itr) == 0)
                _disabled_categories.emplace(itr);
        }

        for(const auto& itr : _disabled_categories)
        {
            OMNITRACE_VERBOSE_F(1, "Disabling perfetto track event category: %s\n",
                                itr.c_str());
            track_event_cfg.add_disabled_categories(itr);
        }

        auto* ds_cfg = cfg.add_data_sources()->mutable_config();
        ds_cfg->set_name("track_event");  // this MUST be track_event
        ds_cfg->set_track_event_config_raw(track_event_cfg.SerializeAsString());

        args.shmem_size_hint_kb = shmem_size_hint;

        if(get_backend() != "inprocess") args.backends |= perfetto::kSystemBackend;
        if(get_backend() != "system") args.backends |= perfetto::kInProcessBackend;

        perfetto::Tracing::Initialize(args);
        perfetto::TrackEvent::Register();
    }

    if(get_use_ompt())
    {
        OMNITRACE_VERBOSE_F(1, "Setting up OMPT...\n");
        ompt::setup();
    }

    if(get_use_rcclp())
    {
        OMNITRACE_VERBOSE_F(1, "Setting up RCCLP...\n");
        rcclp::setup();
    }

    if(get_use_perfetto() && !is_system_backend())
    {
#if defined(CUSTOM_DATA_SOURCE)
        // Add the following:
        perfetto::DataSourceDescriptor dsd{};
        dsd.set_name("com.example.custom_data_source");
        CustomDataSource::Register(dsd);
        auto* ds_cfg = cfg.add_data_sources()->mutable_config();
        ds_cfg->set_name("com.example.custom_data_source");
        CustomDataSource::Trace([](CustomDataSource::TraceContext ctx) {
            auto packet = ctx.NewTracePacket();
            packet->set_timestamp(perfetto::TrackEvent::GetTraceTimeNs());
            packet->set_for_testing()->set_str("Hello world!");
            PRINT_HERE("%s", "Trace");
        });
#endif
        auto& tracing_session = tracing::get_trace_session();
        tracing_session       = perfetto::Tracing::NewTrace();
        tracing_session->Setup(cfg);
        tracing_session->StartBlocking();
    }

    // if static objects are destroyed in the inverse order of when they are
    // created this should ensure that finalization is called before perfetto
    // ends the tracing session
    static auto _ensure_finalization = ensure_finalization();

    if(dmp::rank() == 0 && get_verbose() >= 0) fprintf(stderr, "\n");

    return true;
}

//======================================================================================//

extern "C" void
omnitrace_init_hidden(const char* _mode, bool _is_binary_rewrite, const char* _argv0)
{
    static int  _total_count = 0;
    static auto _args = std::make_pair(std::string_view{ _mode }, _is_binary_rewrite);

    auto _count   = _total_count++;
    auto _mode_sv = std::string_view{ _mode };
    // this function may be called multiple times if multiple libraries are instrumented
    // we want to guard against multiple calls which with different arguments
    if(_count > 0 &&
       std::tie(_args.first, _args.second) == std::tie(_mode_sv, _is_binary_rewrite))
        return;

    OMNITRACE_CONDITIONAL_THROW(
        _count > 0 &&
            std::tie(_args.first, _args.second) != std::tie(_mode_sv, _is_binary_rewrite),
        "\nomnitrace_init(...) called multiple times with different arguments for mode "
        "and/or is_binary_rewrite:"
        "\n    Invocation #1: omnitrace_init(mode=%-8s, is_binary_rewrite=%-5s, ...)"
        "\n    Invocation #%i: omnitrace_init(mode=%-8s, is_binary_rewrite=%-5s, ...)",
        _args.first.data(), std::to_string(_args.second).c_str(), _count + 1, _mode,
        std::to_string(_is_binary_rewrite).c_str());

    // always the first
    (void) get_state();
    (void) tracing::push_count();
    (void) tracing::pop_count();

    OMNITRACE_CONDITIONAL_THROW(
        get_state() >= State::Init &&
            (config::get_is_continuous_integration() || get_debug_init()),
        "omnitrace_init(mode=%s, is_binary_rewrite=%s, argv0=%s) called after omnitrace "
        "was initialized. state = %s",
        _mode, std::to_string(_is_binary_rewrite).c_str(), _argv0,
        std::to_string(get_state()).c_str());

    tracing::get_finalization_functions().emplace_back([_argv0]() {
        OMNITRACE_CI_THROW(get_state() != State::Active,
                           "Finalizer function for popping main invoked in non-active "
                           "state :: state = %s\n",
                           std::to_string(get_state()).c_str());
        if(get_state() == State::Active)
        {
            // if main hasn't been popped yet, pop it
            OMNITRACE_BASIC_VERBOSE(2, "Running omnitrace_pop_trace(%s)...\n", _argv0);
            omnitrace_pop_trace_hidden(_argv0);
        }
    });

    std::atexit([]() {
        // if active (not already finalized) then we should finalize
        if(get_state() == State::Active) omnitrace_finalize_hidden();
    });

    OMNITRACE_CONDITIONAL_BASIC_PRINT_F(
        get_debug_env() || get_verbose_env() > 2,
        "mode: %s | is binary rewrite: %s | command: %s\n", _mode,
        (_is_binary_rewrite) ? "y" : "n", _argv0);

    tim::set_env("OMNITRACE_MODE", _mode, 0);
    config::is_binary_rewrite() = _is_binary_rewrite;

    if(!_set_mpi_called)
    {
        _preinit_callback = []() { get_preinit_bundle()->start(); };
    }
    else
    {
        get_preinit_bundle()->start();
    }
}

//======================================================================================//

extern "C" void
omnitrace_finalize_hidden(void)
{
    // disable thread id recycling during finalization
    threading::recycle_ids() = false;

    set_thread_state(ThreadState::Completed);

    // return if not active
    if(get_state() != State::Active)
    {
        OMNITRACE_BASIC_DEBUG_F("State = %s. Finalization skipped\n",
                                std::to_string(get_state()).c_str());
        return;
    }

    if(get_verbose() >= 0 || get_debug()) fprintf(stderr, "\n");

    OMNITRACE_VERBOSE_F(0, "finalizing...\n");
    thread_info::set_stop(comp::wall_clock::record());

    tim::sampling::block_signals(get_sampling_signals(),
                                 tim::sampling::sigmask_scope::process);

    // some functions called during finalization may alter the push/pop count so we need
    // to save them here
    auto _push_count = tracing::push_count().load();
    auto _pop_count  = tracing::pop_count().load();

    // e.g. omnitrace_pop_trace("main");
    if(_push_count > _pop_count)
    {
        for(auto& itr : tracing::get_finalization_functions())
        {
            itr();
            ++_pop_count;
        }
    }

    set_state(State::Finalized);

    push_enable_sampling_on_child_threads(false);
    set_sampling_on_all_future_threads(false);

    auto _debug_init  = get_debug_finalize();
    auto _debug_value = get_debug();
    if(_debug_init) config::set_setting_value("OMNITRACE_DEBUG", true);
    scope::destructor _debug_dtor{ [_debug_value, _debug_init]() {
        if(_debug_init) config::set_setting_value("OMNITRACE_DEBUG", _debug_value);
    } };

    auto& _thread_bundle = thread_data<thread_bundle_t>::instance();
    if(_thread_bundle) _thread_bundle->stop();

    if(dmp::rank() == 0 && get_verbose() >= 0) fprintf(stderr, "\n");
    if(get_verbose() > 0 || get_debug()) config::print_settings();

    OMNITRACE_VERBOSE_F(1, "omnitrace_push_trace :: called %zux\n", _push_count);
    OMNITRACE_VERBOSE_F(1, "omnitrace_pop_trace  :: called %zux\n", _pop_count);

    OMNITRACE_DEBUG_F("Copying over all timemory hash information to main thread...\n");
    // copy these over so that all hashes are known
    auto& _hzero = tracing::get_timemory_hash_ids(0);
    auto& _azero = tracing::get_timemory_hash_aliases(0);
    for(size_t i = 1; i < max_supported_threads; ++i)
    {
        auto& _hitr = tracing::get_timemory_hash_ids(i);
        auto& _aitr = tracing::get_timemory_hash_aliases(i);
        if(_hzero && _hitr)
        {
            for(const auto& itr : *_hitr)
                _hzero->emplace(itr.first, itr.second);
        }
        if(_azero && _aitr)
        {
            for(auto itr : *_aitr)
                _azero->emplace(itr.first, itr.second);
        }
    }

    // stop the main bundle which has stats for run
    if(get_main_bundle())
    {
        OMNITRACE_DEBUG_F("Stopping main bundle...\n");
        get_main_bundle()->stop();
    }

    if(get_use_rcclp())
    {
        OMNITRACE_VERBOSE_F(1, "Shutting down RCCLP...\n");
        rcclp::shutdown();
    }

    if(get_use_ompt())
    {
        OMNITRACE_VERBOSE_F(1, "Shutting down OMPT...\n");
        ompt::shutdown();
    }

    OMNITRACE_DEBUG_F("Stopping and destroying instrumentation bundles...\n");
    for(size_t i = 0; i < max_supported_threads; ++i)
    {
        auto&       itr   = instrumentation_bundles::instances().at(i);
        const auto& _info = thread_info::get(i, InternalTID);
        while(!itr.bundles.empty())
        {
            int _lvl = 1;
            if(_info->is_offset)
            {
                ++_pop_count;
                _lvl = 4;
            }
            OMNITRACE_VERBOSE_F(_lvl,
                                "Warning! instrumentation bundle on thread %zu (TID=%li) "
                                "with label '%s' was not stopped.\n",
                                i, itr.bundles.back()->tid(),
                                itr.bundles.back()->key().c_str());

            itr.bundles.back()->stop();
            itr.bundles.back()->pop();
            itr.allocator.destroy(itr.bundles.back());
            itr.allocator.deallocate(itr.bundles.back(), 1);
            itr.bundles.pop_back();
        }
    }

    // stop the main gotcha which shuts down the pthread gotchas
    if(get_init_bundle())
    {
        OMNITRACE_DEBUG_F("Stopping main gotcha...\n");
        get_init_bundle()->stop();
    }

    // stop the gotcha bundle
    if(get_preinit_bundle())
    {
        OMNITRACE_VERBOSE_F(1, "Shutting down miscellaneous gotchas...\n");
        get_preinit_bundle()->stop();
        component::mpi_gotcha::shutdown();
    }

    if(get_use_process_sampling())
    {
        OMNITRACE_VERBOSE_F(1, "Shutting down background sampler...\n");
        process_sampler::shutdown();
    }

    if(get_use_roctracer())
    {
        OMNITRACE_VERBOSE_F(1, "Shutting down roctracer...\n");
        // ensure that threads running roctracer callbacks shutdown
        comp::roctracer::shutdown();

        // join extra thread(s) used by roctracer
        OMNITRACE_VERBOSE_F(2, "Waiting on roctracer tasks...\n");
        tasking::join();
    }

    if(get_use_rocprofiler())
    {
        OMNITRACE_VERBOSE_F(1, "Shutting down rocprofiler...\n");
        rocprofiler::post_process();
        rocprofiler::rocm_cleanup();
    }

    if(get_use_sampling())
    {
        OMNITRACE_VERBOSE_F(1, "Shutting down sampling...\n");
        sampling::shutdown();
    }

    OMNITRACE_VERBOSE_F(3, "Reporting the process- and thread-level metrics...\n");
    // report the high-level metrics for the process
    if(get_main_bundle())
    {
        if(get_verbose() >= 0 || get_debug()) fprintf(stderr, "\n");
        std::string _msg = JOIN("", *get_main_bundle());
        auto        _pos = _msg.find(">>>  ");
        if(_pos != std::string::npos) _msg = _msg.substr(_pos + 5);
        OMNITRACE_VERBOSE_F(0, "%s\n", _msg.c_str());
        OMNITRACE_DEBUG_F("Resetting main bundle...\n");
        get_main_bundle()->reset();
    }

    // print out thread-data if they are not still running
    // if they are still running (e.g. thread-pool still alive), the
    // thread-specific data will be wrong if try to stop them from
    // the main thread.
    for(auto& itr : thread_data<thread_bundle_t>::instances())
    {
        if(itr && itr->get<comp::wall_clock>() &&
           !itr->get<comp::wall_clock>()->get_is_running())
        {
            std::string _msg = JOIN("", *itr);
            auto        _pos = _msg.find(">>>  ");
            if(_pos != std::string::npos) _msg = _msg.substr(_pos + 5);
            OMNITRACE_VERBOSE_F(0, "%s\n", _msg.c_str());
        }
    }

    if(get_verbose() >= 0 || get_debug()) fprintf(stderr, "\n");

    // ensure that all the MT instances are flushed
    if(get_use_sampling())
    {
        OMNITRACE_VERBOSE_F(1, "Post-processing the sampling backtraces...\n");
        sampling::post_process();
    }

    if(get_use_critical_trace() || (get_use_rocm_smi() && get_use_roctracer()))
    {
        OMNITRACE_VERBOSE_F(1, "Generating the critical trace...\n");
        // (potentially) increase the thread-pool size since application
        // shouldn't be using threads during finalization
        tasking::initialize_threadpool(std::min<uint64_t>(
            { std::thread::hardware_concurrency(), get_thread_pool_size(), 8 }));

        for(size_t i = 0; i < max_supported_threads; ++i)
        {
            using critical_trace_hash_data =
                thread_data<critical_trace::hash_ids, critical_trace::id>;

            if(critical_trace_hash_data::instances().at(i))
                critical_trace::add_hash_id(*critical_trace_hash_data::instances().at(i));
        }

        for(size_t i = 0; i < max_supported_threads; ++i)
        {
            using critical_trace_chain_data = thread_data<critical_trace::call_chain>;

            if(critical_trace_chain_data::instances().at(i))
                critical_trace::update(i);  // launch update task
        }

        OMNITRACE_VERBOSE_F(1, "Waiting on critical trace updates...\n");
        tasking::join();
    }

    if(get_use_process_sampling())
    {
        OMNITRACE_VERBOSE_F(1, "Post-processing the system-level samples...\n");
        process_sampler::post_process();
    }

    if(get_use_critical_trace())
    {
        // make sure outstanding hash tasks completed before compute
        tasking::join();

        // launch compute task
        OMNITRACE_VERBOSE_F(1, "launching critical trace compute task...\n");
        critical_trace::compute();

        OMNITRACE_VERBOSE_F(1, "Waiting on critical trace tasks...\n");
        tasking::join();
    }

    // shutdown tasking before timemory is finalized, especially the roctracer thread-pool
    OMNITRACE_VERBOSE_F(1, "Shutting down thread-pools...\n");
    tasking::shutdown();

    if(get_use_code_coverage())
    {
        OMNITRACE_VERBOSE_F(1, "Post-processing the code coverage...\n");
        coverage::post_process();
    }

    bool _perfetto_output_error = false;
    if(get_use_perfetto() && !is_system_backend())
    {
        auto& tracing_session = tracing::get_trace_session();

        OMNITRACE_CI_THROW(tracing_session == nullptr,
                           "Null pointer to the tracing session");

        OMNITRACE_VERBOSE_F(0, "Finalizing perfetto...\n");

        // Make sure the last event is closed for this example.
        perfetto::TrackEvent::Flush();
        tracing_session->FlushBlocking();

        OMNITRACE_VERBOSE_F(3, "Stopping the blocking perfetto trace session...\n");
        tracing_session->StopBlocking();

        using char_vec_t = std::vector<char>;
        OMNITRACE_VERBOSE_F(3, "Getting the trace data...\n");

        auto trace_data = char_vec_t{};
#if defined(TIMEMORY_USE_MPI) && TIMEMORY_USE_MPI > 0
        if(get_perfetto_combined_traces())
        {
            using perfetto_mpi_get_t =
                tim::operation::finalize::mpi_get<char_vec_t, true>;

            char_vec_t              _trace_data{ tracing_session->ReadTraceBlocking() };
            std::vector<char_vec_t> _rank_data = {};
            auto _combine = [](char_vec_t& _dst, const char_vec_t& _src) -> char_vec_t& {
                _dst.reserve(_dst.size() + _src.size());
                for(auto&& itr : _src)
                    _dst.emplace_back(itr);
                return _dst;
            };

            perfetto_mpi_get_t{ get_perfetto_combined_traces(), settings::node_count() }(
                _rank_data, _trace_data, _combine);
            for(auto& itr : _rank_data)
                trace_data =
                    (trace_data.empty()) ? std::move(itr) : _combine(trace_data, itr);
        }
        else
        {
            trace_data = tracing_session->ReadTraceBlocking();
        }
#else
        trace_data = tracing_session->ReadTraceBlocking();
#endif

        if(!trace_data.empty())
        {
            operation::file_output_message<tim::project::omnitrace> _fom{};
            // Write the trace into a file.
            if(get_verbose() >= 0)
                _fom(get_perfetto_output_filename(), std::string{ "perfetto" },
                     " (%.2f KB / %.2f MB / %.2f GB)... ",
                     static_cast<double>(trace_data.size()) / units::KB,
                     static_cast<double>(trace_data.size()) / units::MB,
                     static_cast<double>(trace_data.size()) / units::GB);
            std::ofstream ofs{};
            if(!tim::filepath::open(ofs, get_perfetto_output_filename(),
                                    std::ios::out | std::ios::binary))
            {
                _fom.append("Error opening '%s'...",
                            get_perfetto_output_filename().c_str());
                _perfetto_output_error = true;
            }
            else
            {
                // Write the trace into a file.
                ofs.write(&trace_data[0], trace_data.size());
                if(get_verbose() >= 0) _fom.append("%s", "Done");  // NOLINT
                auto _manager = tim::manager::instance();
                if(_manager)
                    _manager->add_file_output("protobuf", "perfetto",
                                              get_perfetto_output_filename());
            }
            ofs.close();
        }
        else if(dmp::rank() == 0)
        {
            OMNITRACE_VERBOSE_F(0,
                                "trace data is empty. File '%s' will not be written...\n",
                                get_perfetto_output_filename().c_str());
        }
    }

    tim::manager::instance()->add_metadata([](auto& ar) {
        auto _maps = tim::procfs::read_maps(process::get_id());
        auto _libs = std::set<std::string>{};
        for(auto& itr : _maps)
        {
            auto&& _path = itr.pathname;
            if(!_path.empty() && _path.at(0) != '[') _libs.emplace(_path);
        }
        ar(tim::cereal::make_nvp("memory_maps_files", _libs),
           tim::cereal::make_nvp("memory_maps", _maps));
    });

    OMNITRACE_VERBOSE_F(1, "Finalizing timemory...\n");
    tim::timemory_finalize();

    if(_perfetto_output_error)
    {
        OMNITRACE_THROW("Error opening perfetto output file: %s",
                        get_perfetto_output_filename().c_str());
    }

    OMNITRACE_CI_THROW(
        _push_count > _pop_count, "%s",
        TIMEMORY_JOIN(" ",
                      "omnitrace_push_trace was called more times than "
                      "omnitrace_pop_trace. The inverse is fine but the current state "
                      "means not every measurement was ended :: pushed:",
                      _push_count, "vs. popped:", _pop_count)
            .c_str());

    config::finalize();

    OMNITRACE_VERBOSE_F(0, "Finalized\n");
}

//======================================================================================//

namespace
{
// if static objects are destroyed randomly (relatively uncommon behavior)
// this might call finalization before perfetto ends the tracing session
// but static variable in omnitrace_init_tooling_hidden is more likely
auto _ensure_finalization = ensure_finalization(true);
}  // namespace
