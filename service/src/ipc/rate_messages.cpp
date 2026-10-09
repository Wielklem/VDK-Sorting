#include "ipc/rate_messages.hpp"

#include <vsort/common/timestamp.hpp>
#include <vsort/ipc/envelope.hpp>

namespace vsort::service {

namespace fb = ipc::fb;

std::vector<std::uint8_t> makeCameraRatesEnvelope(const CameraRates& rates) {
    flatbuffers::FlatBufferBuilder fbb{128};
    std::vector<flatbuffers::Offset<fb::CameraRate>> cameras;
    cameras.reserve(rates.cameras.size());
    for (const auto& rate : rates.cameras) {
        cameras.push_back(fb::CreateCameraRate(fbb, rate.cameraId,
                                               static_cast<float>(rate.incomingFps),
                                               static_cast<float>(rate.analysedFps)));
    }
    const auto event = fb::CreateCameraRatesEvent(fbb, fbb.CreateVector(cameras));
    return ipc::finishEnvelope(fbb,
                               {.type = fb::MsgType::CameraRates,
                                .requestId = 0,
                                .timestampNs = static_cast<std::uint64_t>(Timestamp::now().ns()),
                                .status = 0,
                                .errorText = {}},
                               fb::Payload::CameraRatesEvent, event.Union());
}

} // namespace vsort::service
