#include <opencv2/core.hpp>
#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <zmq.hpp>

#include <vsort/common/version.hpp>

int main() {
    spdlog::info("vsort_service {}", vsort::version());
    spdlog::info("OpenCV {}", CV_VERSION);
    spdlog::info("SQLite {}", sqlite3_libversion());
    const auto [major, minor, patch] = zmq::version();
    spdlog::info("ZeroMQ {}.{}.{}", major, minor, patch);
    return 0;
}
