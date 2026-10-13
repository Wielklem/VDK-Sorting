#pragma once

#include "capture/capture_types.hpp"

namespace vsort::service {

// What the IPC command handler needs from the capture side. Thread-safe: called from the IPC
// thread.
class ICaptureControl {
public:
    ICaptureControl() = default;
    virtual ~ICaptureControl() = default;
    ICaptureControl(const ICaptureControl&) = delete;
    ICaptureControl& operator=(const ICaptureControl&) = delete;
    ICaptureControl(ICaptureControl&&) = delete;
    ICaptureControl& operator=(ICaptureControl&&) = delete;

    // InvalidArgument: bad folder, count, filter or sensor list. AlreadyExists: already running.
    // IoError: folder cannot be created or is (almost) full. Returns the new status.
    [[nodiscard]] virtual Result<CaptureStatus> startCapture(const CaptureRequest& request) = 0;
    // Ends a running capture (photos already queued are still written); no-op when not running.
    [[nodiscard]] virtual CaptureStatus stopCapture() = 0;
    [[nodiscard]] virtual CaptureStatus captureStatus() const = 0;
};

} // namespace vsort::service
