#pragma once

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>

namespace tvs {
// Replace a configuration file only after its complete private temporary file
// has been written and synced. The temporary is on the same filesystem, so
// rename is atomic and a write error cannot truncate the previous config.
inline bool writePrivateAtomic(const std::filesystem::path& path, const std::string& data) {
    std::string name = path.string() + ".tmp.XXXXXX";
    std::vector<char> temp(name.begin(), name.end());
    temp.push_back('\0');
    const int fd = ::mkstemp(temp.data()); // creates mode 0600
    if (fd < 0) return false;
    bool ok = true;
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t n = ::write(fd, data.data() + offset, data.size() - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = false; break; }
        offset += static_cast<std::size_t>(n);
    }
    if (ok && ::fsync(fd) != 0) ok = false;
    if (::close(fd) != 0) ok = false;
    if (ok && ::rename(temp.data(), path.c_str()) != 0) ok = false;
    if (!ok) ::unlink(temp.data());
    return ok;
}
}
