/*
 * Copyright (c) 2017 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

//! @file roc_audio/iframe_encoder.h
//! @brief Audio frame encoder interface.

#ifndef ROC_AUDIO_IFRAME_ENCODER_H_
#define ROC_AUDIO_IFRAME_ENCODER_H_

#include "roc_audio/sample_spec.h"
#include "roc_audio/units.h"
#include "roc_core/stddefs.h"
#include "roc_packet/packet.h"
#include "roc_packet/units.h"

namespace roc {
namespace audio {

//! Audio frame encoder interface.
class IFrameEncoder {
public:
    virtual ~IFrameEncoder();

    //! Get encoded frame size for given number of samples per channel.
    virtual size_t encoded_size(size_t num_samples) const = 0;

    //! Start encoding a new frame.
    //!
    //! @remarks
    //!  After this call, write() will store samples to the given @p frame_data
    //!  until @p frame_size bytes are written or end() is called.
    virtual void begin(void* frame_data, size_t frame_size) = 0;

    //! Write samples into current frame.
    //!
    //! @b Parameters
    //!  - @p samples - samples to be encoded
    //!  - @p n_samples - number of samples to be encoded per channel
    //!  - @p channels - channel mask of the samples to be encoded
    //!
    //! @remarks
    //!  Encodes samples and writes to the current frame.
    //!
    //! @returns
    //!  number of samples encoded per channel. The returned value can be fewer than
    //!  @p n_samples if the frame is full and no more samples can be written to it.
    //!
    //! @pre
    //!  This method may be called only between begin() and end() calls.
    //!
    //! @note
    //!  Encoded and decoded channel masks may differ. If the provided samples have
    //!  extra channels, they are ignored. If they don't have some channels, these
    //!  channels are filled with zeros.
    virtual size_t
    write(const sample_t* samples, size_t n_samples, SampleSpec& sample_spec) = 0;

    //! Finish encoding current frame.
    //!
    //! @remarks
    //!  After this call, the frame is fully encoded and no more samples will be
    //!  written to the frame. A new frame should be started by calling begin().
    virtual void end() = 0;
};

} // namespace audio
} // namespace roc

#endif // ROC_AUDIO_IFRAME_ENCODER_H_
