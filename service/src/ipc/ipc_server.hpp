#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <zmq.hpp>

#include <vsort/common/config_store.hpp>
#include <vsort/common/message_bus.hpp>
#include <vsort/common/module.hpp>
#include <vsort/ipc/envelope.hpp>

#include "analysis/analysis_overlay.hpp"
#include "analysis/camera_rates.hpp"
#include "cups/cup_types.hpp"
#include "ipc/camera_access.hpp"
#include "ipc/command_handler.hpp"
#include "ipc/preview_hub.hpp"

namespace vsort::service {

struct IpcConfig {
    std::string bindAddress{"127.0.0.1"}; // loopback only, no authentication in V1
    std::uint16_t commandPort{ipc::kDefaultCommandPort};
    std::uint16_t eventPort{ipc::kDefaultEventPort};
    std::chrono::milliseconds heartbeatInterval{1000};
    PreviewDefaults preview;
};

// Service side of the IPC (P30.20): ZeroMQ ROUTER for commands, PUB for events, and the preview
// hub that fills the shared-memory rings. One I/O thread owns both sockets.
class IpcServer final : public IModule {
public:
    // `config` (may be null) must outlive the server.
    IpcServer(IpcConfig config, std::unique_ptr<ICameraAccess> cameras, IConfigStore* configStore);
    ~IpcServer() override;

    [[nodiscard]] std::string_view name() const noexcept override { return "ipc"; }
    [[nodiscard]] Result<> init(ModuleContext& context) override;
    [[nodiscard]] Result<> start() override;
    void stop() noexcept override;
    [[nodiscard]] Health health() const override;

    // Camera modules hand their frames to preview().submit().
    [[nodiscard]] PreviewHub& preview() noexcept { return hub_; }

    // Publishes CameraListChanged. Thread-safe; may be called before start().
    void notifyCamerasChanged();

    // Product Monitor (P80.100): snapshot requests go to `source`. Call before start().
    void setCupSource(const ICupSource* source) noexcept { handler_.setCupSource(source); }

private:
    void run(const std::stop_token& stop);
    void serviceCommands();
    void drainBus();
    void drainOutbox();
    void publish(std::string_view topic, const std::vector<std::uint8_t>& message);
    void enqueue(std::string topic, std::vector<std::uint8_t> message);
    void onStreamChanged(const PreviewStream& stream);

    IpcConfig config_;
    std::unique_ptr<ICameraAccess> cameras_;
    PreviewHub hub_;
    CommandHandler handler_;

    std::shared_ptr<Subscription<ConfigChanged>> configSub_;
    std::shared_ptr<Subscription<CupUpdate>> cupSub_;
    std::shared_ptr<Subscription<CameraRates>> ratesSub_;
    std::shared_ptr<Subscription<AnalysisOverlay>> overlaySub_;

    zmq::context_t context_{1};
    zmq::socket_t router_;
    zmq::socket_t pub_;
    std::jthread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> failed_{false};

    std::mutex outboxMutex_;
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> outbox_;
};

} // namespace vsort::service
