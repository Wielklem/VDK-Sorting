#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <vsort/common/config_store.hpp>
#include <vsort/common/timestamp.hpp>
#include <vsort/ipc/envelope.hpp>

#include "cups/cup_types.hpp"
#include "ipc/camera_access.hpp"
#include "ipc/preview_hub.hpp"

namespace vsort::service {

// Turns one request envelope into one reply envelope (docs/ipc-design.md section 3).
// No sockets here, so it is unit-testable. Thread-safe as far as its dependencies are.
class CommandHandler {
public:
    // `config` may be null: GetConfig/SetConfig then answer NotSupported.
    CommandHandler(PreviewHub& hub, ICameraAccess& cameras, IConfigStore* config,
                   std::string serviceVersion);

    // Always returns a reply. Unparsable input gives a ParseError reply with request_id 0;
    // unknown or unsupported commands give NotSupported.
    [[nodiscard]] std::vector<std::uint8_t> handle(std::span<const std::uint8_t> request) const;

    [[nodiscard]] std::uint64_t uptimeMs() const noexcept;

    // Product Monitor data (P80.100). Null: GetCupSnapshot answers NotSupported.
    // Set before the IPC thread starts; `source` must outlive the handler.
    void setCupSource(const ICupSource* source) noexcept { cups_ = source; }

private:
    struct Reply {
        ipc::fb::Payload type{ipc::fb::Payload::NONE};
        flatbuffers::Offset<void> body;
    };

    [[nodiscard]] Result<Reply> dispatch(flatbuffers::FlatBufferBuilder& fbb,
                                         const ipc::fb::Envelope& request) const;
    [[nodiscard]] Result<Reply> onHello(flatbuffers::FlatBufferBuilder& fbb) const;
    [[nodiscard]] Result<Reply> onGetCameraList(flatbuffers::FlatBufferBuilder& fbb) const;
    [[nodiscard]] Result<Reply> onSetPreview(flatbuffers::FlatBufferBuilder& fbb,
                                             const ipc::fb::SetPreviewRequest& req) const;
    [[nodiscard]] Result<Reply>
    onGetCameraSettings(flatbuffers::FlatBufferBuilder& fbb,
                        const ipc::fb::GetCameraSettingsRequest& req) const;
    [[nodiscard]] Result<Reply>
    onSetCameraSettings(const ipc::fb::SetCameraSettingsRequest& req) const;
    [[nodiscard]] Result<Reply> onGetConfig(flatbuffers::FlatBufferBuilder& fbb,
                                            const ipc::fb::GetConfigRequest& req) const;
    [[nodiscard]] Result<Reply> onSetConfig(flatbuffers::FlatBufferBuilder& fbb,
                                            const ipc::fb::SetConfigRequest& req) const;
    [[nodiscard]] Result<Reply> onGetCupSnapshot(flatbuffers::FlatBufferBuilder& fbb,
                                                 const ipc::fb::GetCupSnapshotRequest& req) const;

    PreviewHub& hub_;
    ICameraAccess& cameras_;
    IConfigStore* config_;
    std::string serviceVersion_;
    Timestamp started_;
    const ICupSource* cups_{nullptr};
};

} // namespace vsort::service
