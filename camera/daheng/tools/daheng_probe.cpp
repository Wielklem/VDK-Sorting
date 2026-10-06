// Manual hardware check for the Daheng adapter (P20.30).
//   vsort_daheng_probe                                      list cameras
//   vsort_daheng_probe <serial> [seconds] [exposureUs] [freerun]
//       open, grab for <seconds> and print the frames that arrive.
//       Default: hardware trigger on Line0 (rising edge). "freerun": no trigger needed.
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include <vsort/camera/daheng.hpp>

namespace {

using namespace vsort;
using namespace vsort::camera;

int listCameras() {
    const auto found = makeDahengDiscovery()->discover();
    if (!found) {
        std::cerr << found.error().what() << '\n';
        return 1;
    }
    std::cout << found->size() << " camera(s)\n";
    for (const auto& cam : *found) {
        std::cout << "  " << cam.serial << "  " << cam.model << "  "
                  << (cam.transport == Transport::GigE ? "GigE" : "USB3");
        if (cam.transport == Transport::GigE) {
            std::cout << "  mac " << cam.mac << "  ip " << cam.ip << "  mask " << cam.subnetMask
                      << "  gw " << cam.gateway;
        }
        std::cout << '\n';
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        return listCameras();
    }
    const std::string serial = argv[1];
    const long seconds = argc > 2 ? std::strtol(argv[2], nullptr, 10) : 10;
    const double exposureUs = argc > 3 ? std::strtod(argv[3], nullptr) : 10000.0;
    const bool freeRun = argc > 4 && std::string{argv[4]} == "freerun";

    const auto camera = makeDahengCamera();
    if (const auto r = camera->open(serial); !r) {
        std::cerr << "open: " << r.error().what() << '\n';
        return 1;
    }
    if (const auto info = camera->info()) {
        std::cout << "opened " << info->serial << "  " << info->model << "  sensor "
                  << info->sensorWidth << "x" << info->sensorHeight << '\n';
    }

    CameraSettings wanted; // defaults: hardware trigger, rising edge
    wanted.exposureUs = exposureUs;
    if (freeRun) {
        wanted.triggerMode = TriggerMode::FreeRun; // no trigger signal needed
    }
    if (const auto r = camera->applySettings(wanted); !r) {
        std::cerr << "applySettings: " << r.error().what() << '\n';
        return 1;
    }
    if (const auto s = camera->settings()) {
        // trigger mode: 1 free run, 2 software, 3 hardware. edge: 1 rising, 2 falling.
        std::cout << "read back: exposure " << s->exposureUs << " us, gain " << s->gainDb
                  << " dB, trigger mode " << int(s->triggerMode) << ", edge "
                  << int(s->triggerEdge) << '\n';
    }

    // Single grab thread, read only after stop(): no locking needed.
    std::uint64_t count = 0;
    std::uint64_t gaps = 0;
    std::uint64_t lastId = 0;
    std::uint64_t lastDeviceNs = 0;
    camera->setFrameCallback([&](const Frame& frame) {
        const std::uint64_t id = frame.meta.frameId.value();
        const std::uint64_t deviceNs = frame.meta.deviceTimestampNs;
        if (count > 0 && id != lastId + 1) {
            ++gaps;
        }
        if (count < 20 || count % 100 == 0) {
            const double deltaMs = (count > 0 && deviceNs > lastDeviceNs)
                                       ? double(deviceNs - lastDeviceNs) / 1e6
                                       : 0.0;
            // pixel format: 1 Mono8, 2 BayerRG8, 3 Rgb8, 4 Bgr8
            std::cout << "frame " << id << "  " << frame.meta.width << "x" << frame.meta.height
                      << "  stride " << frame.meta.strideBytes << "  format "
                      << int(frame.meta.pixelFormat) << "  device ts " << deviceNs << " ns  (+"
                      << deltaMs << " ms)\n";
        }
        lastId = id;
        lastDeviceNs = deviceNs;
        ++count;
    });

    if (const auto r = camera->start(); !r) {
        std::cerr << "start: " << r.error().what() << '\n';
        return 1;
    }
    std::cout << (freeRun ? "free run" : "waiting for triggers on Line0") << " for " << seconds
              << " s ...\n";
    std::this_thread::sleep_for(std::chrono::seconds{seconds});
    camera->stop();

    std::cout << count << " frames, " << gaps << " frame-ID gaps, "
              << (seconds > 0 ? double(count) / double(seconds) : 0.0) << " fps average\n";
    camera->close();
    return 0;
}
