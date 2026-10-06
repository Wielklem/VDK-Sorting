// Manual hardware check for the Daheng adapter (P20.30): lists the connected cameras.
#include <iostream>

#include <vsort/camera/daheng.hpp>

int main() {
    using namespace vsort::camera;

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
