#include "ipc/overlay_messages.hpp"

#include <vsort/common/timestamp.hpp>
#include <vsort/ipc/envelope.hpp>

namespace vsort::service {

namespace fb = ipc::fb;

std::vector<std::uint8_t> makeAnalysisOverlayEnvelope(const AnalysisOverlay& overlay) {
    flatbuffers::FlatBufferBuilder fbb{1024};
    std::vector<flatbuffers::Offset<fb::DetectedObject>> objects;
    objects.reserve(overlay.objects.size());
    for (const auto& o : overlay.objects) {
        objects.push_back(fb::CreateDetectedObject(fbb, fbb.CreateVector(o.contour), o.counted,
                                                   static_cast<float>(o.lengthMm),
                                                   static_cast<float>(o.widthMm)));
    }
    const auto event = fb::CreateAnalysisOverlayEvent(
        fbb, overlay.cameraId, overlay.sensorId, overlay.frameId, overlay.frameWidth,
        overlay.frameHeight, overlay.lane.x, overlay.lane.y, overlay.lane.width,
        overlay.lane.height, fbb.CreateVector(objects));
    return ipc::finishEnvelope(fbb,
                               {.type = fb::MsgType::AnalysisOverlay,
                                .requestId = 0,
                                .timestampNs = static_cast<std::uint64_t>(Timestamp::now().ns()),
                                .status = 0,
                                .errorText = {}},
                               fb::Payload::AnalysisOverlayEvent, event.Union());
}

} // namespace vsort::service
