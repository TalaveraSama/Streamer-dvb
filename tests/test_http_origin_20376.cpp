// 203.76: same-origin / CSRF policy for state-changing panel requests.
// The rules are implemented in src/HttpOriginPolicy.h, which has no external
// dependencies, so this test runs standalone (no GStreamer, no Boost).
#include "../src/HttpOriginPolicy.h"

#include <iostream>
#include <string>

namespace hs = tvs::http_security;

namespace {

// The test must keep working when CMake builds with RelWithDebInfo/Release,
// where NDEBUG turns assert() into a no-op; failures are counted explicitly.
int failures = 0;

void expectAllowed(const std::string& host,
                   const std::string& origin,
                   const std::string& referer,
                   const std::string& what) {
    const auto verdict = hs::evaluate(host, origin, referer);
    if (!verdict.allowed) {
        std::cerr << "FAIL: expected allowed: " << what << std::endl;
        ++failures;
    }
}

void expectBlocked(const std::string& host,
                   const std::string& origin,
                   const std::string& referer,
                   const std::string& what) {
    const auto verdict = hs::evaluate(host, origin, referer);
    if (verdict.allowed) {
        std::cerr << "FAIL: expected blocked: " << what << std::endl;
        ++failures;
    }
}

} // namespace

int main() {
    // Clients that send neither Origin nor Referer cannot be browser-forged.
    expectAllowed("panel.example.com:9000", "", "", "no source headers");
    if (hs::evaluate("panel.example.com:9000", "", "").sourcePresent) {
        std::cerr << "FAIL: empty Origin/Referer must not be reported as present" << std::endl;
        ++failures;
    }

    // Straightforward same-origin requests from the panel itself.
    expectAllowed("panel.example.com:9000", "http://panel.example.com:9000", "",
                  "same scheme host and port");
    expectAllowed("10.0.0.5:9000", "http://10.0.0.5:9000", "", "ipv4 same origin");
    expectAllowed("panel.example.com:9000", "", "http://panel.example.com:9000/streams",
                  "referer fallback when Origin is absent");

    // Cross-origin requests: the attack this policy exists to stop.
    expectBlocked("panel.example.com:9000", "http://evil.example.net", "",
                  "different host");
    expectBlocked("panel.example.com:9000", "http://evil.example.com", "",
                  "different host, same registrable prefix");
    expectBlocked("panel.example.com:9000", "null", "", "sandboxed or file:// origin");
    expectBlocked("panel.example.com:9000", "http://panel.example.com:8080", "",
                  "same host, different explicit port");
    expectBlocked("panel.example.com:9000", "", "http://evil.example.net/page",
                  "cross-site referer");

    // Reverse proxy in front of the panel: the browser omits the default port.
    expectAllowed("panel.example.com:9000", "https://panel.example.com", "",
                  "proxy terminated tls, origin without port");
    expectAllowed("panel.example.com", "https://panel.example.com:443", "",
                  "host header without port, origin with default port");

    // Credentials embedded in the URL must not confuse the authority parsing.
    expectAllowed("panel.example.com:9000", "http://user:pass@panel.example.com:9000", "",
                  "userinfo is stripped");
    expectBlocked("panel.example.com:9000", "http://panel.example.com@evil.example.net", "",
                  "userinfo prefix cannot impersonate the panel");

    // IPv6 literals.
    expectAllowed("[::1]:9000", "http://[::1]:9000", "", "ipv6 loopback same origin");
    expectBlocked("[::1]:9000", "http://[::2]:9000", "", "different ipv6 host");
    expectBlocked("[::1]:9000", "http://[::1]:8080", "", "ipv6 different port");

    // Malformed sources are rejected rather than guessed.
    expectBlocked("panel.example.com:9000", "not-a-url", "", "unparsable origin");
    expectBlocked("panel.example.com:9000", "http://", "", "authority missing");

    // Host header absent (HTTP/1.0) is not browser-driven.
    expectAllowed("", "http://evil.example.net", "", "no host header");

    // Case insensitivity.
    expectAllowed("Panel.Example.COM:9000", "http://panel.example.com:9000", "",
                  "case-insensitive host comparison");

    if (failures != 0) {
        std::cerr << failures << " same-origin policy case(s) failed" << std::endl;
        return 1;
    }

    std::cout << "PASS: 203.76 same-origin policy for state-changing requests" << std::endl;
    return 0;
}
