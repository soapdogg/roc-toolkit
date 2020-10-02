/*
 * Copyright (c) 2020 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

//! @file roc_audio/sample_spec.h

#ifndef ROC_AUDIO_SAMPLE_SPEC_H_
#define ROC_AUDIO_SAMPLE_SPEC_H_

#include "roc_packet/units.h"

namespace roc {
namespace audio {

class SampleSpec {
public:
    SampleSpec(size_t sample_rate, size_t num_channels);

    virtual size_t getSampleRate();
    virtual void setSampleRate(size_t sample_rate);
    virtual size_t getNumChannels();
    virtual void setNumChannels(size_t num_channels);

private:
    size_t sample_rate_;
    size_t num_channels_;
};

} // namespace audio
} // namespace roc

#endif // ROC_AUDIO_SAMPLE_SPEC_H_