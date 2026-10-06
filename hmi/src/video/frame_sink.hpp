#pragma once

#include "video/frame_mailbox.hpp"

namespace vsort::hmi {

// Anything that can show preview frames (VideoItem, test doubles).
// submitFrame() must be thread-safe; the newest frame wins.
class IFrameSink {
public:
    IFrameSink() = default;
    IFrameSink(const IFrameSink&) = delete;
    IFrameSink& operator=(const IFrameSink&) = delete;
    IFrameSink(IFrameSink&&) = delete;
    IFrameSink& operator=(IFrameSink&&) = delete;
    virtual ~IFrameSink() = default;

    virtual void submitFrame(FramePtr frame) = 0; // nullptr clears the picture
    virtual void clear() = 0;
};

} // namespace vsort::hmi
