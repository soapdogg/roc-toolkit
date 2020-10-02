/*
 * Copyright (c) 2018 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

//! @file roc_audio/resampler_writer.h
//! @brief Resampler.

#ifndef ROC_AUDIO_RESAMPLER_WRITER_H_
#define ROC_AUDIO_RESAMPLER_WRITER_H_

#include "roc_audio/frame.h"
#include "roc_audio/iwriter.h"
#include "roc_audio/resampler.h"
#include "roc_audio/sample_spec.h"
#include "roc_audio/units.h"
#include "roc_core/array.h"
#include "roc_core/noncopyable.h"
#include "roc_core/slice.h"
#include "roc_core/stddefs.h"
#include "roc_packet/units.h"

namespace roc {
namespace audio {

//! Resamples audio stream with non-integer dynamically changing factor.
//! @remarks
//!  Typicaly being used with factor close to 1 ( 0.9 < factor < 1.1 ).
class ResamplerWriter : public IWriter, public core::NonCopyable<> {
public:
    //! Initialize.
    //!
    //! @b Parameters
    //!  - @p writer specifies output audio stream used in write()
    //!  - @p buffer_pool is used to allocate temporary buffers
    //!  - @p frame_size is number of samples per resampler frame per audio channel
    //!  - @p channels is the bitmask of audio channels
    ResamplerWriter(IWriter& writer,
                    core::BufferPool<sample_t>& buffer_pool,
                    core::IAllocator& allocator,
                    const ResamplerConfig& config,
                    const SampleSpec& sample_spec,
                    size_t frame_size);

    //! Check if object is successfully constructed.
    bool valid() const;

    //! Read audio frame.
    //! @remarks
    //!  Calculates everything during this call so it may take time.
    virtual void write(Frame&);

    //! Set new resample factor.
    //! @remarks
    //!  Resampling algorithm needs some window of input samples. The length of the window
    //!  (length of sinc impulse response) is a compromise between SNR and speed. It
    //!  depends on current resampling factor. So we choose length of input buffers to let
    //!  it handle maximum length of input. If new scaling factor breaks equation this
    //!  function returns false.
    bool set_scaling(float);

private:
    bool init_(core::BufferPool<sample_t>&);

    Resampler resampler_;
    IWriter& writer_;

    core::Slice<sample_t> output_;

    core::Slice<sample_t> frames_[3];
    size_t frame_pos_;
    const size_t frame_size_;

    bool valid_;
};

} // namespace audio
} // namespace roc

#endif // ROC_AUDIO_RESAMPLER_WRITER_H_
