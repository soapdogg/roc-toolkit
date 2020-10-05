/*
 * Copyright (c) 2015 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_audio/packetizer.h"
#include "roc_core/log.h"
#include "roc_core/panic.h"
#include "roc_core/random.h"

namespace roc {
namespace audio {

Packetizer::Packetizer(packet::IWriter& writer,
                       packet::IComposer& composer,
                       IFrameEncoder& payload_encoder,
                       packet::PacketPool& packet_pool,
                       core::BufferPool<uint8_t>& buffer_pool,
                       core::nanoseconds_t packet_length,
                       SampleSpec& sample_spec,
                       unsigned int payload_type)
    : writer_(writer)
    , composer_(composer)
    , payload_encoder_(payload_encoder)
    , packet_pool_(packet_pool)
    , buffer_pool_(buffer_pool)
    , channels_(sample_spec.getChannels())
    , num_channels_(sample_spec.num_channels())
    , samples_per_packet_(
          (packet::timestamp_t)packet::timestamp_from_ns(packet_length, sample_spec.getSampleRate()))
    , payload_type_(payload_type)
    , payload_size_(payload_encoder.encoded_size(samples_per_packet_))
    , packet_pos_(0)
    , source_((packet::source_t)core::random(packet::source_t(-1)))
    , seqnum_((packet::seqnum_t)core::random(packet::seqnum_t(-1)))
    , timestamp_((packet::timestamp_t)core::random(packet::timestamp_t(-1))) {
    roc_log(LogDebug, "packetizer: initializing: n_channels=%lu samples_per_packet=%lu",
            (unsigned long)num_channels_, (unsigned long)samples_per_packet_);
}

void Packetizer::write(Frame& frame) {
    if (frame.size() % num_channels_ != 0) {
        roc_panic("packetizer: unexpected frame size");
    }

    const sample_t* buffer_ptr = frame.data();
    size_t buffer_samples = frame.size() / num_channels_;

    while (buffer_samples != 0) {
        if (!packet_) {
            if (!begin_packet_()) {
                return;
            }
        }

        size_t ns = buffer_samples;
        if (ns > (samples_per_packet_ - packet_pos_)) {
            ns = (samples_per_packet_ - packet_pos_);
        }
        SampleSpec sample_spec = SampleSpec();
        sample_spec.setChannels(channels_);
        const size_t actual_ns = payload_encoder_.write(buffer_ptr, ns, sample_spec);
        roc_panic_if_not(actual_ns == ns);

        buffer_ptr += actual_ns * num_channels_;
        buffer_samples -= actual_ns;

        packet_pos_ += actual_ns;

        if (packet_pos_ == samples_per_packet_) {
            end_packet_();
        }
    }
}

void Packetizer::flush() {
    if (packet_) {
        end_packet_();
    }
}

bool Packetizer::begin_packet_() {
    packet::PacketPtr pp = create_packet_();
    if (!pp) {
        return false;
    }

    packet::RTP* rtp = pp->rtp();
    if (!rtp) {
        roc_panic("packetizer: unexpected non-rtp packet");
    }

    payload_encoder_.begin(rtp->payload.data(), rtp->payload.size());

    rtp->source = source_;
    rtp->seqnum = seqnum_;
    rtp->timestamp = timestamp_;
    rtp->payload_type = payload_type_;

    packet_ = pp;

    return true;
}

void Packetizer::end_packet_() {
    payload_encoder_.end();

    packet_->rtp()->duration = (packet::timestamp_t)packet_pos_;

    if (packet_pos_ < samples_per_packet_) {
        pad_packet_();
    }

    writer_.write(packet_);

    seqnum_++;
    timestamp_ += (packet::timestamp_t)packet_pos_;

    packet_ = NULL;
    packet_pos_ = 0;
}

void Packetizer::pad_packet_() {
    const size_t actual_payload_size = payload_encoder_.encoded_size(packet_pos_);
    roc_panic_if_not(actual_payload_size <= payload_size_);

    if (actual_payload_size == payload_size_) {
        return;
    }

    if (!composer_.pad(*packet_, payload_size_ - actual_payload_size)) {
        roc_panic("packetizer: can't pad packet: orig_size=%lu actual_size=%lu",
                  (unsigned long)payload_size_, (unsigned long)actual_payload_size);
    }
}

packet::PacketPtr Packetizer::create_packet_() {
    packet::PacketPtr packet = new (packet_pool_) packet::Packet(packet_pool_);
    if (!packet) {
        roc_log(LogError, "packetizer: can't allocate packet");
        return NULL;
    }

    packet->add_flags(packet::Packet::FlagAudio);

    core::Slice<uint8_t> data = new (buffer_pool_) core::Buffer<uint8_t>(buffer_pool_);
    if (!data) {
        roc_log(LogError, "packetizer: can't allocate buffer");
        return NULL;
    }

    if (!composer_.prepare(*packet, data, payload_size_)) {
        roc_log(LogError, "packetizer: can't prepare packet");
        return NULL;
    }

    packet->set_data(data);

    return packet;
}

} // namespace audio
} // namespace roc
