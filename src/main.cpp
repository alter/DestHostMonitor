#include "cli.hpp"
#include "util/log.hpp"

#include <winsock2.h>

namespace {

// RAII for Winsock startup/teardown (needed by getaddrinfo).
struct WinsockGuard {
    bool ok = false;
    WinsockGuard() {
        WSADATA wsa{};
        ok = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
    }
    ~WinsockGuard() {
        if (ok) WSACleanup();
    }
};

}  // namespace

int main(int argc, char** argv) {
    WinsockGuard winsock;
    if (!winsock.ok) {
        pt::log_error("WSAStartup failed");
        return 1;
    }
    return pt::run_cli(argc, argv);
}
