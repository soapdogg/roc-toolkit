/*
 * Copyright (c) 2015 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <CppUTest/TestHarness.h>

#include "test_mock_source.h"

#include "roc_audio/sample_spec.h"
#include "roc_core/buffer_pool.h"
#include "roc_core/heap_allocator.h"
#include "roc_core/stddefs.h"
#include "roc_core/temp_file.h"
#include "roc_sndio/pump.h"
#include "roc_sndio/sox_sink.h"
#include "roc_sndio/sox_source.h"

namespace roc {
namespace sndio {

namespace {

enum { MaxBufSize = 8192, FrameSize = 512, SampleRate = 44100, ChMask = 0x3 };

core::HeapAllocator allocator;
core::BufferPool<audio::sample_t> buffer_pool(allocator, MaxBufSize, true);

} // namespace

TEST_GROUP(sox_source) {
    Config sink_config;
    Config source_config;

    void setup() {
        sink_config.sample_spec = audio::SampleSpec(SampleRate, ChMask);
        sink_config.frame_size = FrameSize;

        source_config.sample_spec = audio::SampleSpec(SampleRate, ChMask);
        source_config.frame_size = FrameSize;
    }
};

TEST(sox_source, noop) {
    SoxSource sox_source(allocator, source_config);
}

TEST(sox_source, error) {
    SoxSource sox_source(allocator, source_config);

    CHECK(!sox_source.open(NULL, "/bad/file"));
}

TEST(sox_source, has_clock) {
    core::TempFile file("test.wav");

    {
        MockSource mock_source;
        mock_source.add(MaxBufSize * 10);

        SoxSink sox_sink(allocator, sink_config);
        CHECK(sox_sink.open(NULL, file.path()));

        Pump pump(buffer_pool, mock_source, sox_sink, FrameSize, Pump::ModeOneshot);
        CHECK(pump.valid());
        CHECK(pump.run());
    }

    SoxSource sox_source(allocator, source_config);

    CHECK(sox_source.open(NULL, file.path()));
    CHECK(!sox_source.has_clock());
}

TEST(sox_source, sample_rate_auto) {
    core::TempFile file("test.wav");

    {
        MockSource mock_source;
        mock_source.add(MaxBufSize * 10);

        SoxSink sox_sink(allocator, sink_config);
        CHECK(sox_sink.open(NULL, file.path()));

        Pump pump(buffer_pool, mock_source, sox_sink, FrameSize, Pump::ModeOneshot);
        CHECK(pump.valid());
        CHECK(pump.run());
    }

    source_config.sample_spec.setSampleRate(0);
    SoxSource sox_source(allocator, source_config);

    CHECK(sox_source.open(NULL, file.path()));
    CHECK(sox_source.sample_rate() == SampleRate);
}

TEST(sox_source, sample_rate_mismatch) {
    core::TempFile file("test.wav");

    {
        MockSource mock_source;
        mock_source.add(MaxBufSize * 10);

        SoxSink sox_sink(allocator, sink_config);
        CHECK(sox_sink.open(NULL, file.path()));

        Pump pump(buffer_pool, mock_source, sox_sink, FrameSize, Pump::ModeOneshot);
        CHECK(pump.valid());
        CHECK(pump.run());
    }

    source_config.sample_spec.setSampleRate(SampleRate * 2);
    SoxSource sox_source(allocator, source_config);

    CHECK(sox_source.open(NULL, file.path()));
    CHECK(sox_source.sample_rate() == SampleRate * 2);
}

} // namespace sndio
} // namespace roc
