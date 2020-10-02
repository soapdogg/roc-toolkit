/*
 * Copyright (c) 2020 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_audio/sample_spec.h"
#include "roc_core/log.h"
#include "roc_core/panic.h"

namespace roc {
namespace audio {

SampleSpec::SampleSpec(size_t sample_rate,
                       size_t num_channels) {
    sample_rate_ = sample_rate;
    num_channels_ = num_channels;
}

size_t SampleSpec::getSampleRate(){
    return sample_rate_;
}

void SampleSpec::setSampleRate(size_t sample_rate) {
    sample_rate_ = sample_rate;
}

size_t SampleSpec::getNumChannels(){
    return num_channels_;
}

void SampleSpec::setNumChannels(size_t num_channels) {
    num_channels_ = num_channels;
}

} // namespace audio
} // namespace ro