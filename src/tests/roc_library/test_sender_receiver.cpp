/*
 * Copyright (c) 2015 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <CppUTest/TestHarness.h>

#include <stdio.h>
#include <unistd.h>

#include "roc_address/socket_addr.h"
#include "roc_core/buffer_pool.h"
#include "roc_core/heap_allocator.h"
#include "roc_core/log.h"
#include "roc_core/panic.h"
#include "roc_core/random.h"
#include "roc_core/stddefs.h"
#include "roc_core/thread.h"
#include "roc_core/time.h"
#include "roc_fec/codec_map.h"
#include "roc_netio/event_loop.h"
#include "roc_packet/packet_pool.h"
#include "roc_packet/queue.h"

#include "roc/context.h"
#include "roc/endpoint.h"
#include "roc/log.h"
#include "roc/receiver.h"
#include "roc/sender.h"

namespace roc {

namespace {

enum {
    MaxBufSize = 500,

    SampleRate = 44100,
    NumChans = 2,

    SourcePackets = 10,
    RepairPackets = 5,

    PacketSamples = 100,
    FrameSamples = PacketSamples * 2,
    TotalSamples = PacketSamples * SourcePackets * 3,

    Latency = TotalSamples / NumChans,
    Timeout = TotalSamples * 10
};

enum { FlagFEC = (1 << 0) };

float increment_sample_value(float sample_value, float sample_step) {
    sample_value += sample_step;
    if (sample_value + sample_step > 1.0f) {
        sample_value = sample_step;
    }
    return sample_value;
}

core::HeapAllocator allocator;
packet::PacketPool packet_pool(allocator, true);
core::BufferPool<uint8_t> byte_buffer_pool(allocator, MaxBufSize, true);

class Context : public core::NonCopyable<> {
public:
    Context()
        : ctx_(NULL) {
        roc_context_config config;
        memset(&config, 0, sizeof(config));

        CHECK(roc_context_open(&config, &ctx_) == 0);
        CHECK(ctx_);
    }

    ~Context() {
        CHECK(roc_context_close(ctx_) == 0);
    }

    roc_context* get() {
        return ctx_;
    }

private:
    roc_context* ctx_;
};

class Sender : public core::Thread {
public:
    Sender(Context& context,
           roc_sender_config& config,
           const roc_endpoint* receiver_source_endp,
           const roc_endpoint* receiver_repair_endp,
           float sample_step,
           size_t frame_size,
           unsigned flags)
        : sndr_(NULL)
        , sample_step_(sample_step)
        , frame_size_(frame_size) {
        CHECK(roc_sender_open(context.get(), &config, &sndr_) == 0);
        CHECK(sndr_);

        if (flags & FlagFEC) {
            CHECK(roc_sender_connect(sndr_, ROC_INTERFACE_AUDIO_SOURCE,
                                     receiver_source_endp)
                  == 0);
            CHECK(roc_sender_connect(sndr_, ROC_INTERFACE_AUDIO_REPAIR,
                                     receiver_repair_endp)
                  == 0);
        } else {
            CHECK(roc_sender_connect(sndr_, ROC_INTERFACE_AUDIO_SOURCE,
                                     receiver_source_endp)
                  == 0);
        }
    }

    ~Sender() {
        CHECK(roc_sender_close(sndr_) == 0);
    }

    void stop() {
        stopped_ = 1;
    }

private:
    virtual void run() {
        float sample_value = sample_step_;
        float samples[TotalSamples];

        while (!stopped_) {
            for (size_t i = 0; i < TotalSamples; ++i) {
                samples[i] = sample_value;
                sample_value = increment_sample_value(sample_value, sample_step_);
            }

            for (size_t off = 0; off < TotalSamples; off += frame_size_) {
                if (off + frame_size_ > TotalSamples) {
                    off = TotalSamples - frame_size_;
                }

                roc_frame frame;
                memset(&frame, 0, sizeof(frame));

                frame.samples = samples + off;
                frame.samples_size = frame_size_ * sizeof(float);

                const int ret = roc_sender_write(sndr_, &frame);
                roc_panic_if_not(ret == 0);
            }
        }
    }

    roc_sender* sndr_;
    const float sample_step_;
    const size_t frame_size_;
    core::Atomic stopped_;
};

class Receiver {
public:
    Receiver(Context& context,
             roc_receiver_config& config,
             float sample_step,
             size_t frame_size,
             unsigned flags)
        : recv_(NULL)
        , source_endp_(NULL)
        , repair_endp_(NULL)
        , sample_step_(sample_step)
        , frame_size_(frame_size) {
        CHECK(roc_receiver_open(context.get(), &config, &recv_) == 0);
        CHECK(recv_);

        if (flags & FlagFEC) {
            CHECK(roc_endpoint_allocate(&source_endp_) == 0);
            CHECK(roc_endpoint_set_uri(source_endp_, "rtp+rs8m://127.0.0.1:0") == 0);

            CHECK(roc_endpoint_allocate(&repair_endp_) == 0);
            CHECK(roc_endpoint_set_uri(repair_endp_, "rs8m://127.0.0.1:0") == 0);

            CHECK(roc_receiver_bind(recv_, ROC_INTERFACE_AUDIO_SOURCE, source_endp_)
                  == 0);
            CHECK(roc_receiver_bind(recv_, ROC_INTERFACE_AUDIO_REPAIR, repair_endp_)
                  == 0);
        } else {
            CHECK(roc_endpoint_allocate(&source_endp_) == 0);
            CHECK(roc_endpoint_set_uri(source_endp_, "rtp://127.0.0.1:0") == 0);

            CHECK(roc_receiver_bind(recv_, ROC_INTERFACE_AUDIO_SOURCE, source_endp_)
                  == 0);
        }
    }

    ~Receiver() {
        if (source_endp_) {
            CHECK(roc_endpoint_deallocate(source_endp_) == 0);
        }

        if (repair_endp_) {
            CHECK(roc_endpoint_deallocate(repair_endp_) == 0);
        }

        CHECK(roc_receiver_close(recv_) == 0);
    }

    const roc_endpoint* source_endpoint() const {
        return source_endp_;
    }

    const roc_endpoint* repair_endpoint() const {
        return repair_endp_;
    }

    void run() {
        float rx_buff[MaxBufSize];

        size_t sample_num = 0;
        size_t frame_num = 0;

        bool wait_for_signal = true;
        size_t identical_sample_num = 0;

        size_t nb_success = PacketSamples * SourcePackets * 4;

        float prev_sample = sample_step_;

        while (identical_sample_num < nb_success) {
            size_t i = 0;
            frame_num++;

            roc_frame frame;
            memset(&frame, 0, sizeof(frame));

            frame.samples = rx_buff;
            frame.samples_size = frame_size_ * sizeof(float);

            roc_panic_if_not(roc_receiver_read(recv_, &frame) == 0);

            if (wait_for_signal) {
                for (; i < frame_size_ && is_zero_(rx_buff[i]); i++) {
                }

                if (i < frame_size_) {
                    wait_for_signal = false;

                    prev_sample = rx_buff[i];
                    i++;
                }
            }

            if (!wait_for_signal) {
                float cur_rx_buff;
                for (; i < frame_size_; i++, sample_num++) {
                    cur_rx_buff = rx_buff[i];

                    if (is_zero_(increment_sample_value(prev_sample, sample_step_)
                                 - cur_rx_buff)) {
                        identical_sample_num++;
                    } else if (!is_zero_(prev_sample)
                               && !is_zero_(cur_rx_buff)) { // Allows stream shifts
                        char sbuff[256];
                        int sbuff_i =
                            snprintf(sbuff, sizeof(sbuff),
                                     "failed comparing sample #%lu\n\nframe_num: %lu\n",
                                     (unsigned long)identical_sample_num,
                                     (unsigned long)frame_num);
                        snprintf(
                            &sbuff[sbuff_i], sizeof(sbuff) - (size_t)sbuff_i,
                            "original: %f,\treceived: %f\n",
                            (double)increment_sample_value(prev_sample, sample_step_),
                            (double)cur_rx_buff);
                        roc_panic("%s", sbuff);
                    }

                    prev_sample = cur_rx_buff;
                }
            }
        }
    }

private:
    static inline bool is_zero_(float s) {
        return fabs(double(s)) < 1e-9;
    }

    roc_receiver* recv_;

    roc_endpoint* source_endp_;
    roc_endpoint* repair_endp_;

    const float sample_step_;
    const size_t frame_size_;
};

class Proxy : private packet::IWriter {
public:
    Proxy(const roc_endpoint* receiver_source_endp,
          const roc_endpoint* receiver_repair_endp,
          size_t n_source_packets,
          size_t n_repair_packets)
        : event_loop_(packet_pool, byte_buffer_pool, allocator)
        , n_source_packets_(n_source_packets)
        , n_repair_packets_(n_repair_packets)
        , pos_(0) {
        CHECK(event_loop_.valid());

        roc_protocol source_proto;
        CHECK(roc_endpoint_get_protocol(receiver_source_endp, &source_proto) == 0);

        roc_protocol repair_proto;
        CHECK(roc_endpoint_get_protocol(receiver_repair_endp, &repair_proto) == 0);

        int source_port = 0;
        CHECK(roc_endpoint_get_port(receiver_source_endp, &source_port) == 0);

        int repair_port = 0;
        CHECK(roc_endpoint_get_port(receiver_repair_endp, &repair_port) == 0);

        receiver_source_endp_.set_host_port(address::Family_IPv4, "127.0.0.1",
                                            source_port);
        receiver_repair_endp_.set_host_port(address::Family_IPv4, "127.0.0.1",
                                            repair_port);

        send_config_.bind_address.set_host_port(address::Family_IPv4, "127.0.0.1", 0);

        recv_source_config_.bind_address.set_host_port(address::Family_IPv4, "127.0.0.1",
                                                       0);
        recv_repair_config_.bind_address.set_host_port(address::Family_IPv4, "127.0.0.1",
                                                       0);

        netio::EventLoop::PortHandle send_port =
            event_loop_.add_udp_sender(send_config_, &writer_);
        CHECK(send_port);
        CHECK(writer_);

        CHECK(event_loop_.add_udp_receiver(recv_source_config_, *this));
        CHECK(event_loop_.add_udp_receiver(recv_repair_config_, *this));

        CHECK(roc_endpoint_allocate(&input_source_endp_) == 0);
        CHECK(roc_endpoint_set_protocol(input_source_endp_, source_proto) == 0);
        CHECK(roc_endpoint_set_host(input_source_endp_, "127.0.0.1") == 0);
        CHECK(roc_endpoint_set_port(input_source_endp_,
                                    recv_source_config_.bind_address.port())
              == 0);

        CHECK(roc_endpoint_allocate(&input_repair_endp_) == 0);
        CHECK(roc_endpoint_set_protocol(input_repair_endp_, repair_proto) == 0);
        CHECK(roc_endpoint_set_host(input_repair_endp_, "127.0.0.1") == 0);
        CHECK(roc_endpoint_set_port(input_repair_endp_,
                                    recv_repair_config_.bind_address.port())
              == 0);
    }

    ~Proxy() {
        CHECK(roc_endpoint_deallocate(input_source_endp_) == 0);
        CHECK(roc_endpoint_deallocate(input_repair_endp_) == 0);
    }

    const roc_endpoint* source_endpoint() const {
        return input_source_endp_;
    }

    const roc_endpoint* repair_endpoint() const {
        return input_repair_endp_;
    }

private:
    virtual void write(const packet::PacketPtr& pp) {
        pp->udp()->src_addr = send_config_.bind_address;

        if (pp->udp()->dst_addr == recv_source_config_.bind_address) {
            pp->udp()->dst_addr = receiver_source_endp_;
            source_queue_.write(pp);
        } else {
            pp->udp()->dst_addr = receiver_repair_endp_;
            repair_queue_.write(pp);
        }

        for (;;) {
            const size_t block_pos = pos_ % (n_source_packets_ + n_repair_packets_);

            if (block_pos < n_source_packets_) {
                if (!send_packet_(source_queue_, block_pos == 1)) {
                    return;
                }
            } else {
                if (!send_packet_(repair_queue_, false)) {
                    return;
                }
            }
        }
    }

    bool send_packet_(packet::IReader& reader, bool drop) {
        packet::PacketPtr pp = reader.read();
        if (!pp) {
            return false;
        }
        pos_++;
        if (!drop) {
            writer_->write(pp);
        }
        return true;
    }

    netio::UdpSenderConfig send_config_;

    roc_endpoint* input_source_endp_;
    roc_endpoint* input_repair_endp_;

    netio::UdpReceiverConfig recv_source_config_;
    netio::UdpReceiverConfig recv_repair_config_;

    address::SocketAddr receiver_source_endp_;
    address::SocketAddr receiver_repair_endp_;

    packet::Queue source_queue_;
    packet::Queue repair_queue_;

    packet::IWriter* writer_;

    netio::EventLoop event_loop_;

    const size_t n_source_packets_;
    const size_t n_repair_packets_;

    size_t pos_;
};

} // namespace

TEST_GROUP(sender_receiver) {
    roc_sender_config sender_conf;
    roc_receiver_config receiver_conf;

    float sample_step;

    void setup() {
        roc_log_set_level((roc_log_level)core::Logger::instance().level());
        sample_step = 1. / 32768.;
    }

    void init_config(unsigned flags) {
        memset(&sender_conf, 0, sizeof(sender_conf));
        sender_conf.frame_sample_rate = SampleRate;
        sender_conf.frame_channels = ROC_CHANNEL_SET_STEREO;
        sender_conf.frame_encoding = ROC_FRAME_ENCODING_PCM_FLOAT;
        sender_conf.clock_source = ROC_CLOCK_INTERNAL;
        sender_conf.resampler_profile = ROC_RESAMPLER_DISABLE;
        sender_conf.packet_length =
            PacketSamples * 1000000000ul / (SampleRate * NumChans);
        if (flags & FlagFEC) {
            sender_conf.fec_code = ROC_FEC_RS8M;
            sender_conf.fec_block_source_packets = SourcePackets;
            sender_conf.fec_block_repair_packets = RepairPackets;
        } else {
            sender_conf.fec_code = ROC_FEC_DISABLE;
        }

        memset(&receiver_conf, 0, sizeof(receiver_conf));
        receiver_conf.frame_sample_rate = SampleRate;
        receiver_conf.frame_channels = ROC_CHANNEL_SET_STEREO;
        receiver_conf.frame_encoding = ROC_FRAME_ENCODING_PCM_FLOAT;
        receiver_conf.clock_source = ROC_CLOCK_INTERNAL;
        receiver_conf.resampler_profile = ROC_RESAMPLER_DISABLE;
        receiver_conf.target_latency = Latency * 1000000000ul / SampleRate;
        receiver_conf.no_playback_timeout = Timeout * 1000000000ul / SampleRate;
    }

    bool is_fec_supported() {
        return fec::CodecMap::instance().is_supported(packet::FEC_ReedSolomon_M8);
    }
};

TEST(sender_receiver, bare_rtp) {
    enum { Flags = 0 };

    init_config(Flags);

    Context context;

    Receiver receiver(context, receiver_conf, sample_step, FrameSamples, Flags);

    Sender sender(context, sender_conf, receiver.source_endpoint(),
                  receiver.repair_endpoint(), sample_step, FrameSamples, Flags);

    sender.start();
    receiver.run();
    sender.stop();
    sender.join();
}

TEST(sender_receiver, fec_without_losses) {
    if (!is_fec_supported()) {
        return;
    }

    enum { Flags = FlagFEC };

    init_config(Flags);

    Context context;

    Receiver receiver(context, receiver_conf, sample_step, FrameSamples, Flags);

    Sender sender(context, sender_conf, receiver.source_endpoint(),
                  receiver.repair_endpoint(), sample_step, FrameSamples, Flags);

    sender.start();
    receiver.run();
    sender.stop();
    sender.join();
}

TEST(sender_receiver, fec_with_losses) {
    if (!is_fec_supported()) {
        return;
    }

    enum { Flags = FlagFEC };

    init_config(Flags);

    Context context;

    Receiver receiver(context, receiver_conf, sample_step, FrameSamples, Flags);

    Proxy proxy(receiver.source_endpoint(), receiver.repair_endpoint(), SourcePackets,
                RepairPackets);

    Sender sender(context, sender_conf, proxy.source_endpoint(), proxy.repair_endpoint(),
                  sample_step, FrameSamples, Flags);

    sender.start();
    receiver.run();
    sender.stop();
    sender.join();
}

} // namespace roc
