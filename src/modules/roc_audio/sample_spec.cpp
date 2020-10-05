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

SampleSpec::SampleSpec() {
    sample_rate_ = DefaultSampleRate;
    channels_ = DefaultChannelMask;
    num_channels_ = calc_num_channels();
}

SampleSpec::SampleSpec(size_t sample_rate,
                       packet::channel_mask_t channels) {
    sample_rate_ = sample_rate;
    channels_ = channels;
    num_channels_ = calc_num_channels();
}

size_t SampleSpec::getSampleRate() const {
    return sample_rate_;
}

void SampleSpec::setSampleRate(size_t sample_rate) {
    sample_rate_ = sample_rate;
}

packet::channel_mask_t SampleSpec::getChannels() const {
    return channels_;
}

void SampleSpec::setChannels(packet::channel_mask_t channels) {
    channels_ = channels;
    num_channels_ = calc_num_channels();
}

size_t SampleSpec::num_channels() const {
    return num_channels_;
}

size_t SampleSpec::calc_num_channels() {
    size_t n_ch = 0;
    packet::channel_mask_t ch_mask = channels_;
    for (; ch_mask != 0; ch_mask >>= 1) {
        if (ch_mask & 1) {
            n_ch++;
        }
    }
    return n_ch;
}

} // namespace audio
} // namespace ro