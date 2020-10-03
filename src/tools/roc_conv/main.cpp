/*
 * Copyright (c) 2019 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_audio/resampler_profile.h"
#include "roc_audio/sample_spec.h"
#include "roc_core/colors.h"
#include "roc_core/crash.h"
#include "roc_core/heap_allocator.h"
#include "roc_core/log.h"
#include "roc_core/scoped_destructor.h"
#include "roc_core/unique_ptr.h"
#include "roc_pipeline/converter.h"
#include "roc_sndio/backend_dispatcher.h"
#include "roc_sndio/print_drivers.h"
#include "roc_sndio/pump.h"

#include "roc_conv/cmdline.h"

using namespace roc;

int main(int argc, char** argv) {
    core::CrashHandler crash_handler;

    gengetopt_args_info args;

    const int code = cmdline_parser(argc, argv, &args);
    if (code != 0) {
        return code;
    }

    core::ScopedDestructor<gengetopt_args_info*, cmdline_parser_free> args_destructor(
        &args);

    core::Logger::instance().set_level(
        LogLevel(core::DefaultLogLevel + args.verbose_given));

    switch ((unsigned)args.color_arg) {
    case color_arg_auto:
        core::Logger::instance().set_colors(
            core::colors_available() ? core::ColorsEnabled : core::ColorsDisabled);
        break;

    case color_arg_always:
        core::Logger::instance().set_colors(core::ColorsMode(core::ColorsEnabled));
        break;

    case color_arg_never:
        core::Logger::instance().set_colors(core::ColorsMode(core::ColorsDisabled));
        break;

    default:
        break;
    }

    core::HeapAllocator allocator;

    if (args.list_drivers_given) {
        if (!sndio::print_drivers(allocator)) {
            return 1;
        }
        return 0;
    }

    pipeline::ConverterConfig config;

    if (args.frame_size_given) {
        if (args.frame_size_arg <= 0) {
            roc_log(LogError, "invalid --frame-size: should be > 0");
            return 1;
        }
        config.internal_frame_size = (size_t)args.frame_size_arg;
    }

    sndio::BackendDispatcher::instance().set_frame_size(config.internal_frame_size);

    core::BufferPool<audio::sample_t> pool(allocator, config.internal_frame_size,
                                           args.poisoning_flag);

    sndio::Config source_config;
    source_config.sample_spec = audio::SampleSpec(0, config.input_sample_spec.getChannels());
    source_config.frame_size = config.internal_frame_size;

    core::UniquePtr<sndio::ISource> source(
        sndio::BackendDispatcher::instance().open_source(allocator, NULL, args.input_arg,
                                                         source_config),
        allocator);
    if (!source) {
        roc_log(LogError, "can't open input: %s", args.input_arg);
        return 1;
    }
    if (source->has_clock()) {
        roc_log(LogError, "unsupported input: %s", args.input_arg);
        return 1;
    }

    config.input_sample_spec.setSampleRate(source->sample_rate());

    if (args.rate_given) {
        config.output_sample_spec.setSampleRate((size_t)args.rate_arg);
    } else {
        config.output_sample_spec.setSampleRate(config.input_sample_spec.getSampleRate());
    }

    switch ((unsigned)args.resampler_profile_arg) {
    case resampler_profile_arg_low:
        config.resampler = audio::resampler_profile(audio::ResamplerProfile_Low);
        break;

    case resampler_profile_arg_medium:
        config.resampler = audio::resampler_profile(audio::ResamplerProfile_Medium);
        break;

    case resampler_profile_arg_high:
        config.resampler = audio::resampler_profile(audio::ResamplerProfile_High);
        break;

    default:
        break;
    }

    if (args.resampler_interp_given) {
        config.resampler.window_interp = (size_t)args.resampler_interp_arg;
    }
    if (args.resampler_window_given) {
        config.resampler.window_size = (size_t)args.resampler_window_arg;
    }

    config.resampling = !args.no_resampling_flag;
    config.poisoning = args.poisoning_flag;

    audio::IWriter* output_writer = NULL;

    sndio::Config sink_config;
    sink_config.sample_spec = config.output_sample_spec;
    sink_config.frame_size = config.internal_frame_size;

    core::UniquePtr<sndio::ISink> sink;
    if (args.output_given) {
        sink.reset(sndio::BackendDispatcher::instance().open_sink(
                       allocator, NULL, args.output_arg, sink_config),
                   allocator);
        if (!sink) {
            roc_log(LogError, "can't open output: %s", args.output_arg);
            return 1;
        }
        if (sink->has_clock()) {
            roc_log(LogError, "unsupported output: %s", args.output_arg);
            return 1;
        }
        output_writer = sink.get();
    }

    pipeline::Converter converter(config, output_writer, pool, allocator);
    if (!converter.valid()) {
        roc_log(LogError, "can't create converter pipeline");
        return 1;
    }

    sndio::Pump pump(pool, *source, converter, config.internal_frame_size,
                     sndio::Pump::ModePermanent);
    if (!pump.valid()) {
        roc_log(LogError, "can't create audio pump");
        return 1;
    }

    const bool ok = pump.run();

    return ok ? 0 : 1;
}
