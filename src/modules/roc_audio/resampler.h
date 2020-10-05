/*
 * Copyright (c) 2015 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

//! @file roc_audio/resampler.h
//! @brief Resampler.

#ifndef ROC_AUDIO_RESAMPLER_H_
#define ROC_AUDIO_RESAMPLER_H_

#include "roc_audio/frame.h"
#include "roc_audio/ireader.h"
#include "roc_audio/sample_spec.h"
#include "roc_audio/units.h"
#include "roc_core/array.h"
#include "roc_core/noncopyable.h"
#include "roc_core/slice.h"
#include "roc_core/stddefs.h"
#include "roc_packet/units.h"

namespace roc {
namespace audio {

//! Resampler parameters.
struct ResamplerConfig {
    //! Sinc table precision.
    //! @remarks
    //!  Affects sync table size.
    //!  Lower values give lower quality but rarer cache misses.
    size_t window_interp;

    //! Resampler internal window length.
    //! @remarks
    //!  Affects sync table size and number of CPU cycles.
    //!  Lower values give lower quality but higher speed and also rarer cache misses.
    size_t window_size;

    ResamplerConfig()
        : window_interp(128)
        , window_size(32) {
    }
};

//! Resamples audio stream with non-integer dynamically changing factor.
class Resampler : public core::NonCopyable<> {
public:
    //! Initialize.
    Resampler(core::IAllocator& allocator,
              const ResamplerConfig& config,
              const SampleSpec& sample_spec,
              size_t frame_size);

    //! Check if object is successfully constructed.
    bool valid() const;

    //! Set new resample factor.
    //! @remarks
    //!  Resampling algorithm needs some window of input samples. The length of the window
    //!  (length of sinc impulse response) is a compromise between SNR and speed. It
    //!  depends on current resampling factor. So we choose length of input buffers to let
    //!  it handle maximum length of input. If new scaling factor breaks equation this
    //!  function returns false.
    bool set_scaling(float);

    //! Resamples the whole output frame.
    bool resample_buff(Frame& out);

    //! Push new buffer on the front of the internal FIFO, which comprisesthree window_.
    void renew_buffers(core::Slice<sample_t>& prev,
                       core::Slice<sample_t>& cur,
                       core::Slice<sample_t>& next);

private:
    typedef uint32_t fixedpoint_t;
    typedef uint64_t long_fixedpoint_t;
    typedef int32_t signed_fixedpoint_t;
    typedef int64_t signed_long_fixedpoint_t;

    SampleSpec sample_spec_;
    inline size_t channelize_index(const size_t i, const size_t ch_offset) const {
        return i * sample_spec_.num_channels() + ch_offset;
    }

    //! Computes single sample of the particular audio channel.
    //!
    //! @param channel_offset a serial number of the channel
    //!  (e.g. left -- 0, right -- 1, etc.).
    sample_t resample_(size_t channel_offset);

    bool check_config_() const;

    bool fill_sinc_();
    sample_t sinc_(fixedpoint_t x, float fract_x);

    sample_t* prev_frame_;
    sample_t* curr_frame_;
    sample_t* next_frame_;

    size_t out_frame_pos_;

    float scaling_;

    const size_t frame_size_;
    const size_t frame_size_ch_;

    const size_t window_size_;
    const fixedpoint_t qt_half_sinc_window_size_;

    const size_t window_interp_;
    const size_t window_interp_bits_;

    core::Array<sample_t> sinc_table_;
    const sample_t* sinc_table_ptr_;

    // half window len in Q8.24 in terms of input signal
    fixedpoint_t qt_half_window_size_;
    const fixedpoint_t qt_epsilon_;

    const fixedpoint_t qt_frame_size_;

    // time position of output sample in terms of input samples indexes
    // for example 0 -- time position of first sample in curr_frame_
    fixedpoint_t qt_sample_;

    // time distance between two output samples, equals to resampling factor
    fixedpoint_t qt_dt_;

    // the step with which we iterate over the sinc_table_
    fixedpoint_t qt_sinc_step_;

    const sample_t cutoff_freq_;

    bool valid_;
};

} // namespace audio
} // namespace roc

#endif // ROC_AUDIO_RESAMPLER_H_
