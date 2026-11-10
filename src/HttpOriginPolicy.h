#pragma once

// 203.76: same-origin policy for state-changing panel requests.
//
// The panel authenticates with HTTP Basic credentials. A browser replays those
// credentials automatically for the panel origin, so without an explicit check a
// hostile page could POST to http://panel:9000/api/... and mutate the server even
// though it can never read the response. The rules implemented here are
// deliberately dependency-free (no Boost, no Beast) so they can be unit tested
// directly: see tests/test_http_origin_20376.cpp.
//
// Decision table for a state-changing request:
//   * no Origin and no Referer  -> allowed (curl, scripts, players, old clients)
//   * "Origin: null"           -> rejected (sandboxed iframe, file://, data:)
//   * different host            -> rejected
//   * same host, different non-default port -> rejected
//   * same host, port missing on one side    -> allowed (TLS reverse proxy)
//   * unparsable Origin/Referer -> rejected

#include <cstddef>
#include <string>

namespace tvs::http_security {

inline std::string lowerAscii(const std::string& value) {
    std::string lowered = value;
    for (char& ch : lowered) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return lowered;
}

// Returns true when the authority carries an explicit port (IPv6 literals are
// bracketed, so only a colon after the closing bracket counts).
inline bool hasExplicitPort(const std::string& authority) {
    if (authority.empty()) {
        return false;
    }
    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string::npos) {
            return false;
        }
        return authority.find(':', close) != std::string::npos;
    }
    return authority.find(':') != std::string::npos;
}

// "panel.example.com:9000" -> "panel.example.com"; "[::1]:9000" -> "[::1]".
inline std::string hostOfAuthority(const std::string& authority) {
    if (authority.empty()) {
        return std::string();
    }
    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string::npos) {
            return std::string();
        }
        return lowerAscii(authority.substr(0, close + 1));
    }
    const auto colon = authority.find(':');
    return lowerAscii(colon == std::string::npos ? authority : authority.substr(0, colon));
}

// Port of an authority, or -1 when it has none. Bracketed IPv6 is supported.
inline int portOfAuthority(const std::string& authority) {
    std::size_t colon = std::string::npos;
    if (!authority.empty() && authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string::npos) {
            return -1;
        }
        colon = authority.find(':', close);
    } else {
        colon = authority.find(':');
    }
    if (colon == std::string::npos) {
        return -1;
    }
    int port = 0;
    for (std::size_t i = colon + 1; i < authority.size(); ++i) {
        const char ch = authority[i];
        if (ch < '0' || ch > '9') {
            return -1;
        }
        port = port * 10 + (ch - '0');
        if (port > 65535) {
            return -1;
        }
    }
    return port;
}

// Extracts the authority ("host:port") from a URL, dropping scheme, path, query,
// fragment and userinfo. Returns an empty string when the value is not a URL.
inline std::string authorityFromUrl(const std::string& url) {
    const auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
        return std::string();
    }
    std::size_t begin = schemeEnd + 3;
    std::string rest = url.substr(begin);
    const auto end = rest.find_first_of("/?#");
    if (end != std::string::npos) {
        rest.resize(end);
    }
    const auto at = rest.rfind('@');
    if (at != std::string::npos) {
        rest = rest.substr(at + 1);
    }
    if (rest.empty()) {
        return std::string();
    }
    return lowerAscii(rest);
}

// Compares the Host header with an Origin/Referer authority.
inline bool sameAuthority(const std::string& host, const std::string& source) {
    if (host.empty() || source.empty()) {
        return false;
    }
    const std::string hostLower = lowerAscii(host);
    if (hostLower == source) {
        return true;
    }
    if (hostOfAuthority(hostLower) != hostOfAuthority(source)) {
        return false;
    }
    const int hostPort = portOfAuthority(hostLower);
    const int sourcePort = portOfAuthority(source);
    if (hostPort < 0 || sourcePort < 0) {
        // One side relies on the scheme default (normally a TLS reverse proxy in
        // front of the panel), so the host comparison is all we can do.
        return true;
    }
    return hostPort == sourcePort;
}

struct OriginVerdict {
    bool allowed = true;
    bool sourcePresent = false;
};

// Evaluates a state-changing request against the same-origin policy.
inline OriginVerdict evaluate(const std::string& hostHeader,
                              const std::string& originHeader,
                              const std::string& refererHeader) {
    OriginVerdict verdict;
    if (hostHeader.empty()) {
        // HTTP/1.0 clients without Host cannot be browser-driven here.
        return verdict;
    }

    std::string source = originHeader.empty() ? refererHeader : originHeader;
    if (source.empty()) {
        return verdict;
    }
    verdict.sourcePresent = true;

    if (lowerAscii(source) == "null") {
        verdict.allowed = false;
        return verdict;
    }

    const std::string authority = authorityFromUrl(source);
    if (authority.empty()) {
        verdict.allowed = false;
        return verdict;
    }

    verdict.allowed = sameAuthority(hostHeader, authority);
    return verdict;
}

} // namespace tvs::http_security
