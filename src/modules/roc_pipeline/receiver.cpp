/*
 * Copyright (c) 2017 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_pipeline/receiver.h"
#include "roc_core/log.h"
#include "roc_core/panic.h"
#include "roc_core/shared_ptr.h"
#include "roc_packet/address_to_str.h"
#include "roc_pipeline/port_to_str.h"

namespace roc {
namespace pipeline {

Receiver::Receiver(const ReceiverConfig& config,
                   const fec::CodecMap& codec_map,
                   const rtp::FormatMap& format_map,
                   packet::PacketPool& packet_pool,
                   core::BufferPool<uint8_t>& byte_buffer_pool,
                   core::BufferPool<audio::sample_t>& sample_buffer_pool,
                   core::IAllocator& allocator)
    : codec_map_(codec_map)
    , format_map_(format_map)
    , packet_pool_(packet_pool)
    , byte_buffer_pool_(byte_buffer_pool)
    , sample_buffer_pool_(sample_buffer_pool)
    , allocator_(allocator)
    , ticker_(config.common.output_sample_spec.getSampleRate())
    , audio_reader_(NULL)
    , config_(config)
    , timestamp_(0)
    , num_channels_(config.common.output_sample_spec.num_channels())
    , active_cond_(control_mutex_) {
    mixer_.reset(new (allocator_)
                     audio::Mixer(sample_buffer_pool, config.common.internal_frame_size),
                 allocator_);
    if (!mixer_ || !mixer_->valid()) {
        return;
    }
    audio::IReader* areader = mixer_.get();

    if (config.common.poisoning) {
        poisoner_.reset(new (allocator_) audio::PoisonReader(*areader), allocator_);
        if (!poisoner_) {
            return;
        }
        areader = poisoner_.get();
    }

    audio_reader_ = areader;
}

bool Receiver::valid() {
    return audio_reader_;
}

bool Receiver::add_port(const PortConfig& config) {
    roc_log(LogInfo, "receiver: adding port %s", port_to_str(config).c_str());

    core::Mutex::Lock lock(control_mutex_);

    core::SharedPtr<ReceiverPort> port =
        new (allocator_) ReceiverPort(config, format_map_, allocator_);

    if (!port || !port->valid()) {
        roc_log(LogError, "receiver: can't create port, initialization failed");
        return false;
    }

    ports_.push_back(*port);
    return true;
}

void Receiver::iterate_ports(void (*fn)(void*, const PortConfig&), void* arg) const {
    core::Mutex::Lock lock(control_mutex_);

    core::SharedPtr<ReceiverPort> port;

    for (port = ports_.front(); port; port = ports_.nextof(*port)) {
        fn(arg, port->config());
    }
}

size_t Receiver::num_sessions() const {
    core::Mutex::Lock lock(control_mutex_);

    return sessions_.size();
}

size_t Receiver::sample_rate() const {
    return config_.common.output_sample_spec.getSampleRate();
}

bool Receiver::has_clock() const {
    return config_.common.timing;
}

sndio::ISource::State Receiver::state() const {
    core::Mutex::Lock lock(control_mutex_);

    return state_();
}

void Receiver::wait_active() const {
    core::Mutex::Lock lock(control_mutex_);

    while (state_() != Active) {
        active_cond_.wait();
    }
}

void Receiver::write(const packet::PacketPtr& packet) {
    core::Mutex::Lock lock(control_mutex_);

    const State old_state = state_();

    packets_.push_back(*packet);

    if (old_state != Active) {
        active_cond_.broadcast();
    }
}

bool Receiver::read(audio::Frame& frame) {
    core::Mutex::Lock lock(pipeline_mutex_);

    if (config_.common.timing) {
        ticker_.wait(timestamp_);
    }

    prepare_();

    audio_reader_->read(frame);
    timestamp_ += frame.size() / num_channels_;

    return true;
}

void Receiver::prepare_() {
    core::Mutex::Lock lock(control_mutex_);

    const State old_state = state_();

    fetch_packets_();
    update_sessions_();

    if (old_state != Active && state_() == Active) {
        active_cond_.broadcast();
    }
}

sndio::ISource::State Receiver::state_() const {
    if (sessions_.size() != 0) {
        return Active;
    }

    if (packets_.size() != 0) {
        return Active;
    }

    return Inactive;
}

void Receiver::fetch_packets_() {
    for (;;) {
        packet::PacketPtr packet = packets_.front();
        if (!packet) {
            break;
        }

        packets_.remove(*packet);

        if (!parse_packet_(packet)) {
            continue;
        }

        if (!route_packet_(packet)) {
            continue;
        }
    }
}

bool Receiver::parse_packet_(const packet::PacketPtr& packet) {
    core::SharedPtr<ReceiverPort> port;

    for (port = ports_.front(); port; port = ports_.nextof(*port)) {
        if (port->handle(*packet)) {
            return true;
        }
    }

    roc_log(LogDebug, "receiver: ignoring packet for unknown port");

    return false;
}

bool Receiver::route_packet_(const packet::PacketPtr& packet) {
    core::SharedPtr<ReceiverSession> sess;

    for (sess = sessions_.front(); sess; sess = sessions_.nextof(*sess)) {
        if (sess->handle(packet)) {
            return true;
        }
    }

    if (!can_create_session_(packet)) {
        return false;
    }

    return create_session_(packet);
}

bool Receiver::can_create_session_(const packet::PacketPtr& packet) {
    if (packet->flags() & packet::Packet::FlagRepair) {
        roc_log(LogDebug, "receiver: ignoring repair packet for unknown session");
        return false;
    }

    return true;
}

bool Receiver::create_session_(const packet::PacketPtr& packet) {
    if (!packet->udp()) {
        roc_log(LogError, "receiver: can't create session, unexpected non-udp packet");
        return false;
    }

    if (!packet->rtp()) {
        roc_log(LogError, "receiver: can't create session, unexpected non-rtp packet");
        return false;
    }

    const ReceiverSessionConfig sess_config = make_session_config_(packet);

    const packet::Address src_address = packet->udp()->src_addr;
    const packet::Address dst_address = packet->udp()->dst_addr;

    roc_log(LogInfo, "receiver: creating session: src_addr=%s dst_addr=%s",
            packet::address_to_str(src_address).c_str(),
            packet::address_to_str(dst_address).c_str());

    core::SharedPtr<ReceiverSession> sess = new (allocator_)
        ReceiverSession(sess_config, config_.common, src_address, codec_map_, format_map_,
                        packet_pool_, byte_buffer_pool_, sample_buffer_pool_, allocator_);

    if (!sess || !sess->valid()) {
        roc_log(LogError, "receiver: can't create session, initialization failed");
        return false;
    }

    if (!sess->handle(packet)) {
        roc_log(LogError, "receiver: can't create session, can't handle first packet");
        return false;
    }

    mixer_->add(sess->reader());
    sessions_.push_back(*sess);

    return true;
}

void Receiver::remove_session_(ReceiverSession& sess) {
    roc_log(LogInfo, "receiver: removing session");

    mixer_->remove(sess.reader());
    sessions_.remove(sess);
}

void Receiver::update_sessions_() {
    core::SharedPtr<ReceiverSession> curr, next;

    for (curr = sessions_.front(); curr; curr = next) {
        next = sessions_.nextof(*curr);

        if (!curr->update(timestamp_)) {
            remove_session_(*curr);
        }
    }
}

ReceiverSessionConfig
Receiver::make_session_config_(const packet::PacketPtr& packet) const {
    ReceiverSessionConfig sess_config = config_.default_session;

    packet::RTP* rtp = packet->rtp();
    if (rtp) {
        sess_config.payload_type = rtp->payload_type;
    }

    packet::FEC* fec = packet->fec();
    if (fec) {
        sess_config.fec_decoder.scheme = fec->fec_scheme;
    }

    return sess_config;
}

} // namespace pipeline
} // namespace roc
