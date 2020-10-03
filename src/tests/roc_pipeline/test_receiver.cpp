/*
 * Copyright (c) 2015 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <CppUTest/TestHarness.h>

#include "roc_audio/pcm_funcs.h"
#include "roc_core/buffer_pool.h"
#include "roc_core/heap_allocator.h"
#include "roc_fec/codec_map.h"
#include "roc_packet/packet_pool.h"
#include "roc_pipeline/receiver.h"
#include "roc_rtp/composer.h"
#include "roc_rtp/format_map.h"

#include "test_frame_reader.h"
#include "test_packet_writer.h"

namespace roc {
namespace pipeline {

namespace {

const rtp::PayloadType PayloadType = rtp::PayloadType_L16_Stereo;

enum {
    MaxBufSize = 500,

    SampleRate = 44100,
    ChMask = 0x3,
    NumCh = 2,

    SamplesPerFrame = 20,
    SamplesPerPacket = 100,
    FramesPerPacket = SamplesPerPacket / SamplesPerFrame,

    Latency = SamplesPerPacket * 8,
    Timeout = Latency * 13,

    ManyPackets = Latency / SamplesPerPacket * 10,

    MaxSnJump = ManyPackets * 5,
    MaxTsJump = ManyPackets * 7 * SamplesPerPacket
};

core::HeapAllocator allocator;
core::BufferPool<audio::sample_t> sample_buffer_pool(allocator, MaxBufSize, true);
core::BufferPool<uint8_t> byte_buffer_pool(allocator, MaxBufSize, true);
packet::PacketPool packet_pool(allocator, true);

fec::CodecMap codec_map;
rtp::FormatMap format_map;
rtp::Composer rtp_composer(NULL);

} // namespace

TEST_GROUP(receiver) {
    ReceiverConfig config;

    packet::Address src1;
    packet::Address src2;

    PortConfig port1;
    PortConfig port2;

    void setup() {
        config.common.output_sample_rate = SampleRate;
        config.common.output_channels = ChMask;
        config.common.internal_frame_size = MaxBufSize;

        config.common.resampling = false;
        config.common.timing = false;
        config.common.poisoning = true;

        config.default_session.sample_spec.setChannels(ChMask);

        config.default_session.target_latency = Latency * core::Second / SampleRate;

        config.default_session.latency_monitor.min_latency =
            -Timeout * 10 * core::Second / SampleRate;
        config.default_session.latency_monitor.max_latency =
            +Timeout * 10 * core::Second / SampleRate;

        config.default_session.watchdog.no_playback_timeout =
            Timeout * core::Second / SampleRate;

        config.default_session.rtp_validator.max_sn_jump = MaxSnJump;
        config.default_session.rtp_validator.max_ts_jump =
            MaxTsJump * core::Second / SampleRate;

        src1 = new_address(1);
        src2 = new_address(2);

        port1.address = new_address(3);
        port1.protocol = Proto_RTP;

        port2.address = new_address(4);
        port2.protocol = Proto_RTP;
    }
};

TEST(receiver, no_sessions) {
    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());

    FrameReader frame_reader(receiver, sample_buffer_pool);

    for (size_t nf = 0; nf < ManyPackets * FramesPerPacket; nf++) {
        frame_reader.skip_zeros(SamplesPerFrame * NumCh);

        UNSIGNED_LONGS_EQUAL(0, receiver.num_sessions());
    }
}

TEST(receiver, no_ports) {
    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    packet_writer.write_packets(Latency / SamplesPerPacket, SamplesPerPacket, ChMask);

    for (size_t np = 0; np < ManyPackets; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.skip_zeros(SamplesPerFrame * NumCh);

            UNSIGNED_LONGS_EQUAL(0, receiver.num_sessions());
        }

        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }
}

TEST(receiver, one_session) {
    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    packet_writer.write_packets(Latency / SamplesPerPacket, SamplesPerPacket, ChMask);

    for (size_t np = 0; np < ManyPackets; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);

            UNSIGNED_LONGS_EQUAL(1, receiver.num_sessions());
        }

        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }
}

TEST(receiver, one_session_long_run) {
    enum { NumIterations = 10 };

    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    packet_writer.write_packets(Latency / SamplesPerPacket, SamplesPerPacket, ChMask);

    for (size_t ni = 0; ni < NumIterations; ni++) {
        for (size_t np = 0; np < ManyPackets; np++) {
            for (size_t nf = 0; nf < FramesPerPacket; nf++) {
                frame_reader.read_samples(SamplesPerFrame * NumCh, 1);

                UNSIGNED_LONGS_EQUAL(1, receiver.num_sessions());
            }

            packet_writer.write_packets(1, SamplesPerPacket, ChMask);
        }
    }
}

TEST(receiver, initial_latency) {
    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    for (size_t np = 0; np < Latency / SamplesPerPacket - 1; np++) {
        packet_writer.write_packets(1, SamplesPerPacket, ChMask);

        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.skip_zeros(SamplesPerFrame * NumCh);
        }

        UNSIGNED_LONGS_EQUAL(1, receiver.num_sessions());
    }

    packet_writer.write_packets(1, SamplesPerPacket, ChMask);

    for (size_t np = 0; np < Latency / SamplesPerPacket; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }

        UNSIGNED_LONGS_EQUAL(1, receiver.num_sessions());
    }
}

TEST(receiver, initial_latency_timeout) {
    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    packet_writer.write_packets(1, SamplesPerPacket, ChMask);

    for (size_t np = 0; np < Timeout / SamplesPerPacket; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.skip_zeros(SamplesPerFrame * NumCh);
        }

        UNSIGNED_LONGS_EQUAL(1, receiver.num_sessions());
    }

    frame_reader.skip_zeros(SamplesPerFrame * NumCh);

    UNSIGNED_LONGS_EQUAL(0, receiver.num_sessions());
}

TEST(receiver, timeout) {
    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    packet_writer.write_packets(Latency / SamplesPerPacket, SamplesPerPacket, ChMask);

    for (size_t np = 0; np < Latency / SamplesPerPacket; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }

        UNSIGNED_LONGS_EQUAL(1, receiver.num_sessions());
    }

    while (receiver.num_sessions() != 0) {
        frame_reader.skip_zeros(SamplesPerFrame * NumCh);
    }
}

TEST(receiver, initial_trim) {
    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    packet_writer.write_packets(Latency * 3 / SamplesPerPacket, SamplesPerPacket, ChMask);

    frame_reader.set_offset(Latency * 2 * NumCh);

    for (size_t np = 0; np < ManyPackets; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);

            UNSIGNED_LONGS_EQUAL(1, receiver.num_sessions());
        }

        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }
}

TEST(receiver, two_sessions_synchronous) {
    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer1(allocator, receiver, rtp_composer, format_map,
                                packet_pool, byte_buffer_pool, PayloadType, src1,
                                port1.address);

    PacketWriter packet_writer2(allocator, receiver, rtp_composer, format_map,
                                packet_pool, byte_buffer_pool, PayloadType, src2,
                                port1.address);

    for (size_t np = 0; np < Latency / SamplesPerPacket; np++) {
        packet_writer1.write_packets(1, SamplesPerPacket, ChMask);
        packet_writer2.write_packets(1, SamplesPerPacket, ChMask);
    }

    for (size_t np = 0; np < ManyPackets; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 2);

            UNSIGNED_LONGS_EQUAL(2, receiver.num_sessions());
        }

        packet_writer1.write_packets(1, SamplesPerPacket, ChMask);
        packet_writer2.write_packets(1, SamplesPerPacket, ChMask);
    }
}

TEST(receiver, two_sessions_overlapping) {
    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer1(allocator, receiver, rtp_composer, format_map,
                                packet_pool, byte_buffer_pool, PayloadType, src1,
                                port1.address);

    packet_writer1.write_packets(Latency / SamplesPerPacket, SamplesPerPacket, ChMask);

    for (size_t np = 0; np < ManyPackets; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);

            UNSIGNED_LONGS_EQUAL(1, receiver.num_sessions());
        }

        packet_writer1.write_packets(1, SamplesPerPacket, ChMask);
    }

    PacketWriter packet_writer2(allocator, receiver, rtp_composer, format_map,
                                packet_pool, byte_buffer_pool, PayloadType, src2,
                                port1.address);

    packet_writer2.set_offset(packet_writer1.offset() - Latency * NumCh);
    packet_writer2.write_packets(Latency / SamplesPerPacket, SamplesPerPacket, ChMask);

    for (size_t np = 0; np < ManyPackets; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 2);

            UNSIGNED_LONGS_EQUAL(2, receiver.num_sessions());
        }

        packet_writer1.write_packets(1, SamplesPerPacket, ChMask);
        packet_writer2.write_packets(1, SamplesPerPacket, ChMask);
    }
}

TEST(receiver, two_sessions_two_ports) {
    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());

    CHECK(receiver.add_port(port1));
    CHECK(receiver.add_port(port2));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer1(allocator, receiver, rtp_composer, format_map,
                                packet_pool, byte_buffer_pool, PayloadType, src1,
                                port1.address);

    PacketWriter packet_writer2(allocator, receiver, rtp_composer, format_map,
                                packet_pool, byte_buffer_pool, PayloadType, src2,
                                port2.address);

    for (size_t np = 0; np < Latency / SamplesPerPacket; np++) {
        packet_writer1.write_packets(1, SamplesPerPacket, ChMask);
        packet_writer2.write_packets(1, SamplesPerPacket, ChMask);
    }

    for (size_t np = 0; np < ManyPackets; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 2);

            UNSIGNED_LONGS_EQUAL(2, receiver.num_sessions());
        }

        packet_writer1.write_packets(1, SamplesPerPacket, ChMask);
        packet_writer2.write_packets(1, SamplesPerPacket, ChMask);
    }
}

TEST(receiver, two_sessions_same_address_same_stream) {
    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());

    CHECK(receiver.add_port(port1));
    CHECK(receiver.add_port(port2));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer1(allocator, receiver, rtp_composer, format_map,
                                packet_pool, byte_buffer_pool, PayloadType, src1,
                                port1.address);

    PacketWriter packet_writer2(allocator, receiver, rtp_composer, format_map,
                                packet_pool, byte_buffer_pool, PayloadType, src1,
                                port2.address);

    packet_writer1.set_source(11);
    packet_writer2.set_source(11);

    packet_writer2.set_offset(77);

    for (size_t np = 0; np < Latency / SamplesPerPacket; np++) {
        packet_writer1.write_packets(1, SamplesPerPacket, ChMask);
        packet_writer2.write_packets(1, SamplesPerPacket, ChMask);
    }

    for (size_t np = 0; np < ManyPackets; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);

            UNSIGNED_LONGS_EQUAL(1, receiver.num_sessions());
        }

        packet_writer1.write_packets(1, SamplesPerPacket, ChMask);
        packet_writer2.write_packets(1, SamplesPerPacket, ChMask);
    }
}

TEST(receiver, two_sessions_same_address_different_streams) {
    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());

    CHECK(receiver.add_port(port1));
    CHECK(receiver.add_port(port2));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer1(allocator, receiver, rtp_composer, format_map,
                                packet_pool, byte_buffer_pool, PayloadType, src1,
                                port1.address);

    PacketWriter packet_writer2(allocator, receiver, rtp_composer, format_map,
                                packet_pool, byte_buffer_pool, PayloadType, src1,
                                port2.address);

    packet_writer1.set_source(11);
    packet_writer2.set_source(22);

    packet_writer2.set_offset(77);
    packet_writer2.set_seqnum(5);
    packet_writer2.set_timestamp(5 * SamplesPerPacket);

    for (size_t np = 0; np < Latency / SamplesPerPacket; np++) {
        packet_writer1.write_packets(1, SamplesPerPacket, ChMask);
        packet_writer2.write_packets(1, SamplesPerPacket, ChMask);
    }

    for (size_t np = 0; np < ManyPackets; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);

            UNSIGNED_LONGS_EQUAL(1, receiver.num_sessions());
        }

        packet_writer1.write_packets(1, SamplesPerPacket, ChMask);
        packet_writer2.write_packets(1, SamplesPerPacket, ChMask);
    }
}

TEST(receiver, seqnum_overflow) {
    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    packet_writer.set_seqnum(packet::seqnum_t(-1) - ManyPackets / 2);
    packet_writer.write_packets(Latency / SamplesPerPacket, SamplesPerPacket, ChMask);

    for (size_t np = 0; np < ManyPackets; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }
        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }
}

TEST(receiver, seqnum_small_jump) {
    enum { SmallJump = 5 };

    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    packet_writer.write_packets(Latency / SamplesPerPacket, SamplesPerPacket, ChMask);

    for (size_t np = 0; np < ManyPackets; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }
        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }

    packet_writer.set_seqnum(packet_writer.seqnum() + SmallJump);

    for (size_t np = 0; np < ManyPackets; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }
        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }
}

TEST(receiver, seqnum_large_jump) {
    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    packet_writer.write_packets(Latency / SamplesPerPacket, SamplesPerPacket, ChMask);

    for (size_t np = 0; np < ManyPackets; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }
        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }

    packet_writer.set_seqnum(packet_writer.seqnum() + MaxSnJump);

    for (size_t np = 0; np < Latency / SamplesPerPacket; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }
        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }

    while (receiver.num_sessions() != 0) {
        frame_reader.skip_zeros(SamplesPerFrame * NumCh);
    }
}

TEST(receiver, seqnum_reorder) {
    enum { ReorderWindow = Latency / SamplesPerPacket };

    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    size_t pos = 0;

    for (size_t ni = 0; ni < ManyPackets / ReorderWindow; ni++) {
        if (pos >= Latency / SamplesPerPacket) {
            for (size_t nf = 0; nf < ReorderWindow * FramesPerPacket; nf++) {
                frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
            }
        }

        for (ssize_t np = ReorderWindow - 1; np >= 0; np--) {
            packet_writer.shift_to(pos + size_t(np), SamplesPerPacket, ChMask);
            packet_writer.write_packets(1, SamplesPerPacket, ChMask);
        }

        pos += ReorderWindow;
    }
}

TEST(receiver, seqnum_late) {
    enum { DelayedPackets = 5 };

    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    packet_writer.write_packets(Latency / SamplesPerPacket, SamplesPerPacket, ChMask);
    packet_writer.shift_to(Latency / SamplesPerPacket + DelayedPackets, SamplesPerPacket,
                           ChMask);

    for (size_t np = 0; np < Latency / SamplesPerPacket; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }
        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }

    for (size_t np = 0; np < DelayedPackets; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 0);
        }
    }

    for (size_t np = 0; np < Latency / SamplesPerPacket; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }
        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }

    packet_writer.shift_to(Latency / SamplesPerPacket, SamplesPerPacket, ChMask);
    packet_writer.write_packets(DelayedPackets, SamplesPerPacket, ChMask);

    for (size_t np = 0; np < Latency / SamplesPerPacket; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }
    }

    frame_reader.read_samples(SamplesPerFrame * NumCh, 0);
}

TEST(receiver, timestamp_overflow) {
    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    packet_writer.set_timestamp(packet::timestamp_t(-1)
                                - ManyPackets * SamplesPerPacket / 2);

    packet_writer.write_packets(Latency / SamplesPerPacket, SamplesPerPacket, ChMask);

    for (size_t np = 0; np < ManyPackets; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }
        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }
}

TEST(receiver, timestamp_small_jump) {
    enum { ShiftedPackets = 5 };

    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    packet_writer.write_packets(Latency / SamplesPerPacket, SamplesPerPacket, ChMask);

    packet_writer.set_timestamp(Latency + ShiftedPackets * SamplesPerPacket);
    packet_writer.set_offset((Latency + ShiftedPackets * SamplesPerPacket) * NumCh);

    for (size_t np = 0; np < Latency / SamplesPerPacket; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }
        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }

    for (size_t np = 0; np < ShiftedPackets; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 0);
        }
        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }

    for (size_t np = 0; np < ManyPackets; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }
        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }
}

TEST(receiver, timestamp_large_jump) {
    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    packet_writer.write_packets(Latency / SamplesPerPacket, SamplesPerPacket, ChMask);

    packet_writer.set_timestamp(Latency + MaxTsJump);
    packet_writer.set_offset((Latency + MaxTsJump) * NumCh);

    for (size_t np = 0; np < Latency / SamplesPerPacket; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }
        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }

    while (receiver.num_sessions() != 0) {
        frame_reader.skip_zeros(SamplesPerFrame * NumCh);
    }
}

TEST(receiver, timestamp_overlap) {
    enum { OverlappedSamples = SamplesPerPacket / 2 };

    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    packet_writer.write_packets(Latency / SamplesPerPacket, SamplesPerPacket, ChMask);

    packet_writer.set_timestamp(Latency - OverlappedSamples);
    packet_writer.set_offset((Latency - OverlappedSamples) * NumCh);

    for (size_t np = 0; np < ManyPackets; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }
        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }
}

TEST(receiver, timestamp_reorder) {
    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    packet_writer.write_packets(Latency / SamplesPerPacket, SamplesPerPacket, ChMask);

    for (ssize_t np = Latency / SamplesPerPacket - 1; np >= 0; np--) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }

        packet_writer.set_offset((Latency + size_t(np) * SamplesPerPacket) * NumCh);

        packet_writer.set_timestamp(
            packet::timestamp_t(Latency + size_t(np) * SamplesPerPacket));

        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }

    packet_writer.set_offset(Latency * 2 * NumCh);
    packet_writer.set_timestamp(Latency * 2);

    for (size_t np = 0; np < Latency / SamplesPerPacket - 1; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 0);
        }
        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }

    for (size_t np = 0; np < ManyPackets; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }
        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }
}

TEST(receiver, timestamp_late) {
    enum { DelayedPackets = 5 };

    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    packet_writer.write_packets(Latency / SamplesPerPacket, SamplesPerPacket, ChMask);

    packet_writer.set_timestamp(Latency + DelayedPackets * SamplesPerPacket);
    packet_writer.set_offset((Latency + DelayedPackets * SamplesPerPacket) * NumCh);

    for (size_t np = 0; np < Latency / SamplesPerPacket; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }
        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }

    for (size_t np = 0; np < DelayedPackets; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 0);
        }
    }

    for (size_t np = 0; np < Latency / SamplesPerPacket; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }
        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }

    packet_writer.set_timestamp(Latency);
    packet_writer.set_offset(Latency * NumCh);

    packet_writer.write_packets(DelayedPackets, SamplesPerPacket, ChMask);

    for (size_t np = 0; np < Latency / SamplesPerPacket; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }
    }

    frame_reader.read_samples(SamplesPerFrame * NumCh, 0);
}

TEST(receiver, packet_size_small) {
    enum {
        SmallPacketsPerFrame = 2,
        SamplesPerSmallPacket = SamplesPerFrame / SmallPacketsPerFrame,
        ManySmallPackets = Latency / SamplesPerSmallPacket * 10
    };

    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    packet_writer.write_packets(Latency / SamplesPerSmallPacket, SamplesPerSmallPacket,
                                ChMask);

    for (size_t nf = 0; nf < ManySmallPackets / SmallPacketsPerFrame; nf++) {
        frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        for (size_t np = 0; np < SmallPacketsPerFrame; np++) {
            packet_writer.write_packets(1, SamplesPerSmallPacket, ChMask);
        }
    }
}

TEST(receiver, packet_size_large) {
    enum {
        FramesPerLargePacket = 2,
        SamplesPerLargePacket = SamplesPerFrame * FramesPerLargePacket,
        ManyLargePackets = Latency / SamplesPerLargePacket * 10
    };

    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    packet_writer.write_packets(Latency / SamplesPerLargePacket, SamplesPerLargePacket,
                                ChMask);

    for (size_t np = 0; np < ManyLargePackets; np++) {
        for (size_t nf = 0; nf < FramesPerLargePacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }
        packet_writer.write_packets(1, SamplesPerLargePacket, ChMask);
    }
}

TEST(receiver, packet_size_variable) {
    enum {
        SmallPacketsPerFrame = 2,
        SamplesPerSmallPacket = SamplesPerFrame / SmallPacketsPerFrame,

        FramesPerLargePacket = 2,
        SamplesPerLargePacket = SamplesPerFrame * FramesPerLargePacket,

        SamplesPerTwoPackets = (SamplesPerSmallPacket + SamplesPerLargePacket),

        NumIterations = Latency / SamplesPerTwoPackets * 10
    };

    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    size_t available = 0;

    for (size_t ni = 0; ni < NumIterations; ni++) {
        for (; available >= Latency; available -= SamplesPerFrame) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);
        }

        packet_writer.write_packets(1, SamplesPerSmallPacket, ChMask);
        packet_writer.write_packets(1, SamplesPerLargePacket, ChMask);

        available += SamplesPerTwoPackets;
    }
}

TEST(receiver, corrupted_packets_new_session) {
    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    packet_writer.set_corrupt(true);

    packet_writer.write_packets(Latency / SamplesPerPacket, SamplesPerPacket, ChMask);

    for (size_t np = 0; np < ManyPackets; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.skip_zeros(SamplesPerFrame * NumCh);

            UNSIGNED_LONGS_EQUAL(0, receiver.num_sessions());
        }

        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }
}

TEST(receiver, corrupted_packets_existing_session) {
    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    FrameReader frame_reader(receiver, sample_buffer_pool);

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    packet_writer.write_packets(Latency / SamplesPerPacket, SamplesPerPacket, ChMask);
    packet_writer.set_corrupt(true);

    for (size_t np = 0; np < Latency / SamplesPerPacket; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);

            UNSIGNED_LONGS_EQUAL(1, receiver.num_sessions());
        }

        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }

    packet_writer.set_corrupt(false);

    for (size_t np = 0; np < Latency / SamplesPerPacket; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 0);

            UNSIGNED_LONGS_EQUAL(1, receiver.num_sessions());
        }

        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }

    for (size_t np = 0; np < Latency / SamplesPerPacket; np++) {
        for (size_t nf = 0; nf < FramesPerPacket; nf++) {
            frame_reader.read_samples(SamplesPerFrame * NumCh, 1);

            UNSIGNED_LONGS_EQUAL(1, receiver.num_sessions());
        }

        packet_writer.write_packets(1, SamplesPerPacket, ChMask);
    }
}

TEST(receiver, status) {
    Receiver receiver(config, codec_map, format_map, packet_pool, byte_buffer_pool,
                      sample_buffer_pool, allocator);

    CHECK(receiver.valid());
    CHECK(receiver.add_port(port1));

    PacketWriter packet_writer(allocator, receiver, rtp_composer, format_map, packet_pool,
                               byte_buffer_pool, PayloadType, src1, port1.address);

    core::Slice<audio::sample_t> samples(
        new (sample_buffer_pool) core::Buffer<audio::sample_t>(sample_buffer_pool));

    CHECK(samples);
    samples.resize(FramesPerPacket * NumCh);

    CHECK(receiver.state() == sndio::ISource::Inactive);

    {
        audio::Frame frame(samples.data(), samples.size());
        receiver.read(frame);
    }

    packet_writer.write_packets(Latency / SamplesPerPacket, SamplesPerPacket, ChMask);

    CHECK(receiver.state() == sndio::ISource::Active);

    {
        audio::Frame frame(samples.data(), samples.size());
        receiver.read(frame);
    }

    for (;;) {
        audio::Frame frame(samples.data(), samples.size());
        receiver.read(frame);

        if (receiver.state() == sndio::ISource::Inactive) {
            break;
        }
    }
}

} // namespace pipeline
} // namespace roc
