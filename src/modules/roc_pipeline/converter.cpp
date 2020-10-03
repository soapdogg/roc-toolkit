/*
 * Copyright (c) 2019 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_pipeline/converter.h"
#include "roc_core/log.h"
#include "roc_core/panic.h"

namespace roc {
namespace pipeline {

Converter::Converter(const ConverterConfig& config,
                     audio::IWriter* output_writer,
                     core::BufferPool<audio::sample_t>& pool,
                     core::IAllocator& allocator)
    : audio_writer_(NULL)
    , config_(config) {
    audio::IWriter* awriter = output_writer;
    if (!awriter) {
        awriter = &null_writer_;
    }

    if (config.resampling) {
        if (config.poisoning) {
            resampler_poisoner_.reset(new (allocator) audio::PoisonWriter(*awriter),
                                      allocator);
            if (!resampler_poisoner_) {
                return;
            }
            awriter = resampler_poisoner_.get();
        }
        resampler_.reset(new (allocator) audio::ResamplerWriter(
                             *awriter, pool, allocator, config.resampler,
                             config.output_sample_spec, config.internal_frame_size),
                         allocator);
        if (!resampler_ || !resampler_->valid()) {
            return;
        }
        if (!resampler_->set_scaling(float(config.input_sample_spec.getSampleRate())
                                     / config.output_sample_spec.getSampleRate())) {
            return;
        }
        awriter = resampler_.get();
    }

    profiler_.reset(new (allocator) audio::ProfilingWriter(
                        *awriter, config.input_sample_spec.getChannels(), config.input_sample_spec.getSampleRate()),
                    allocator);
    if (!profiler_) {
        return;
    }
    awriter = profiler_.get();

    if (config.poisoning) {
        pipeline_poisoner_.reset(new (allocator) audio::PoisonWriter(*awriter),
                                 allocator);
        if (!pipeline_poisoner_) {
            return;
        }
        awriter = pipeline_poisoner_.get();
    }

    audio_writer_ = awriter;
}

bool Converter::valid() {
    return audio_writer_;
}

size_t Converter::sample_rate() const {
    return config_.output_sample_spec.getSampleRate();
}

bool Converter::has_clock() const {
    return false;
}

void Converter::write(audio::Frame& frame) {
    roc_panic_if(!valid());

    audio_writer_->write(frame);
}

} // namespace pipeline
} // namespace roc
