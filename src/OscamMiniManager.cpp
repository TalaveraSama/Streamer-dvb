#include "OscamMiniManager.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <spawn.h>
#include <unistd.h>

extern char** environ;

namespace fs = std::filesystem;

namespace {
using IniSection = std::map<std::string, std::string>;

std::string trimLocal(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char c) {
        return !std::isspace(c);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char c) {
        return !std::isspace(c);
    }).base(), value.end());
    return value;
}

std::vector<IniSection> parseIniSections(const std::string& text, const std::string& wanted) {
    std::vector<IniSection> result;
    std::istringstream input(text);
    std::string line;
    IniSection* current = nullptr;

    while (std::getline(input, line)) {
        auto comment = line.find_first_of("#;");
        if (comment != std::string::npos) {
            line.resize(comment);
        }
        line = trimLocal(line);
        if (line.empty()) {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            const std::string sectionName = trimLocal(line.substr(1, line.size() - 2));
            if (sectionName == wanted) {
                result.emplace_back();
                current = &result.back();
            } else {
                current = nullptr;
            }
            continue;
        }

        if (!current) {
            continue;
        }

        auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        (*current)[trimLocal(line.substr(0, equals))] = trimLocal(line.substr(equals + 1));
    }
    return result;
}

IniSection parseFirstSection(const std::string& text, const std::string& wanted) {
    auto sections = parseIniSections(text, wanted);
    return sections.empty() ? IniSection{} : sections.front();
}

std::string getValue(const IniSection& section, const std::string& key, const std::string& fallback = {}) {
    auto it = section.find(key);
    return it == section.end() ? fallback : it->second;
}

int getInt(const IniSection& section, const std::string& key, int fallback) {
    try {
        return std::stoi(getValue(section, key, std::to_string(fallback)));
    } catch (...) {
        return fallback;
    }
}

bool isTrue(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value == "1" || value == "yes" || value == "true" || value == "on";
}

std::string safeIni(std::string value) {
    value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
    value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
    return trimLocal(value);
}

bool isHex(const std::string& value, std::size_t length) {
    return value.size() == length && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}

bool isSafeName(const std::string& value) {
    if (value.empty() || value.size() > 64) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == '@';
    });
}

bool validGroups(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    bool expectDigit = true;
    int current = 0;
    for (unsigned char c : value) {
        if (std::isdigit(c)) {
            current = current * 10 + (c - '0');
            if (current > 64) {
                return false;
            }
            expectDigit = false;
        } else if (c == ',') {
            if (expectDigit || current < 1) {
                return false;
            }
            current = 0;
            expectDigit = true;
        } else if (!std::isspace(c)) {
            return false;
        }
    }
    return !expectDigit && current >= 1 && current <= 64;
}

struct PortDef {
    int port = 0;
    std::string caid;
    std::string provider;
};

std::vector<PortDef> parsePortDefs(const std::string& value) {
    std::vector<PortDef> result;
    std::istringstream input(value);
    std::string item;
    while (std::getline(input, item, ';')) {
        item = trimLocal(item);
        if (item.empty()) {
            continue;
        }
        auto at = item.find('@');
        auto colon = item.find(':', at == std::string::npos ? 0 : at + 1);
        if (at == std::string::npos || colon == std::string::npos) {
            continue;
        }
        PortDef def;
        try {
            def.port = std::stoi(trimLocal(item.substr(0, at)));
        } catch (...) {
            continue;
        }
        def.caid = trimLocal(item.substr(at + 1, colon - at - 1));
        def.provider = trimLocal(item.substr(colon + 1));
        if (def.port > 0 && def.port <= 65535 && isHex(def.caid, 4) && isHex(def.provider, 6)) {
            result.push_back(def);
        }
    }
    return result;
}

std::pair<std::string, std::string> accountCaidProvider(const IniSection& account) {
    std::string caid = getValue(account, "caid", "");
    std::string provider = "";
    std::string ident = getValue(account, "ident", "");
    if (!ident.empty()) {
        auto colon = ident.find(':');
        if (colon != std::string::npos) {
            if (caid.empty()) {
                caid = trimLocal(ident.substr(0, colon));
            }
            provider = trimLocal(ident.substr(colon + 1));
            auto comma = provider.find(',');
            if (comma != std::string::npos) {
                provider.resize(comma);
            }
        }
    }
    return {caid, provider};
}
}

OscamMiniManager& OscamMiniManager::instance() {
    static OscamMiniManager instance;
    return instance;
}

std::string OscamMiniManager::trim(std::string value) {
    return trimLocal(std::move(value));
}

std::string OscamMiniManager::readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        return {};
    }
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

bool OscamMiniManager::writeAtomic(const std::string& path, const std::string& content, std::string& error) {
    std::string temp;
    int fd = -1;
    try {
        fs::create_directories(fs::path(path).parent_path());
        std::string pattern = path + ".tmp.XXXXXX";
        std::vector<char> name(pattern.begin(), pattern.end());
        name.push_back('\0');
        // mkstemp creates the file with mode 0600 and cannot follow a pre-existing
        // symlink. Credentials must never be briefly world-readable.
        fd = mkstemp(name.data());
        if (fd < 0) { error = "Unable to create temporary file: " + std::string(std::strerror(errno)); return false; }
        temp = name.data();
        std::size_t written = 0;
        while (written < content.size()) {
            const auto n = ::write(fd, content.data() + written, content.size() - written);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { error = "Failed to write " + temp; ::close(fd); ::unlink(temp.c_str()); return false; }
            written += static_cast<std::size_t>(n);
        }
        if (::close(fd) != 0) { fd = -1; error = "Failed to close " + temp; ::unlink(temp.c_str()); return false; }
        fd = -1;
        fs::rename(temp, path);
        return true;
    } catch (const std::exception& ex) {
        if (fd >= 0) ::close(fd);
        if (!temp.empty()) ::unlink(temp.c_str());
        error = ex.what();
        return false;
    }
}

// 203.76: commands are executed with posix_spawnp and an explicit argv vector.
// The previous popen()/shell implementation concatenated service names, PIDs and
// user-supplied actions into a command line, so any future argument that reached
// this function would have been a shell-injection primitive. stdout and stderr
// are still merged into the captured output, matching the old "2>&1" behaviour.
std::string OscamMiniManager::run(const std::vector<std::string>& argv, int* rc) {
    if (rc) {
        *rc = -1;
    }
    if (argv.empty() || argv.front().empty()) {
        return "empty command";
    }

    int pipeFds[2] = {-1, -1};
    if (::pipe(pipeFds) != 0) {
        return std::string("pipe failed: ") + std::strerror(errno);
    }

    std::vector<char*> argvPtrs;
    argvPtrs.reserve(argv.size() + 1);
    for (const auto& argument : argv) {
        argvPtrs.push_back(const_cast<char*>(argument.c_str()));
    }
    argvPtrs.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipeFds[0]);
    posix_spawn_file_actions_addclose(&actions, pipeFds[1]);

    pid_t child = -1;
    const int spawnResult = ::posix_spawnp(&child, argvPtrs[0], &actions, nullptr,
                                           argvPtrs.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(pipeFds[1]);

    if (spawnResult != 0) {
        ::close(pipeFds[0]);
        return std::string("posix_spawnp failed: ") + std::strerror(spawnResult);
    }

    std::array<char, 4096> buffer{};
    std::string output;
    ssize_t bytesRead = 0;
    while ((bytesRead = ::read(pipeFds[0], buffer.data(), buffer.size())) > 0) {
        output.append(buffer.data(), static_cast<std::size_t>(bytesRead));
    }
    ::close(pipeFds[0]);

    int status = 0;
    pid_t waited = -1;
    do {
        waited = ::waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);

    if (rc) {
        *rc = (waited == child && WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
    }
    return output;
}

std::vector<std::string> OscamMiniManager::ttyDevices() {
    std::vector<std::string> devices;
    try {
        if (fs::exists("/dev/serial/by-id")) {
            for (const auto& entry : fs::directory_iterator("/dev/serial/by-id")) {
                devices.push_back(entry.path().string());
            }
        }
        for (const auto& entry : fs::directory_iterator("/dev")) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("ttyUSB", 0) == 0 || name.rfind("ttyACM", 0) == 0) {
                devices.push_back(entry.path().string());
            }
        }
    } catch (...) {
    }
    std::sort(devices.begin(), devices.end());
    devices.erase(std::unique(devices.begin(), devices.end()), devices.end());
    return devices;
}

std::string OscamMiniManager::json(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

Json::Value OscamMiniManager::parse(const std::string& body, std::string& error) {
    Json::Value value;
    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(body.data(), body.data() + body.size(), &value, &error)) {
        return {};
    }
    return value;
}

OscamMiniManager::Settings OscamMiniManager::loadLocked() {
    Settings settings;

    const auto newcamd = parseFirstSection(readFile(std::string(kConfigDir) + "/oscam.conf"), "newcamd");
    settings.bindIp = getValue(newcamd, "serverip", "127.0.0.1");
    settings.key = getValue(newcamd, "key", settings.key);
    settings.keepalive = isTrue(getValue(newcamd, "keepalive", "1"));

    auto ports = parsePortDefs(getValue(newcamd, "port", ""));
    auto accounts = parseIniSections(readFile(std::string(kConfigDir) + "/oscam.user"), "account");
    std::vector<bool> usedPorts(ports.size(), false);

    for (std::size_t i = 0; i < accounts.size(); ++i) {
        const auto& account = accounts[i];
        NewcamdUser user;
        user.user = getValue(account, "user", "tvstreamer" + std::to_string(i + 1));
        user.password = getValue(account, "pwd", "tvstreamer");
        user.groups = getValue(account, "group", "1");
        user.au = isTrue(getValue(account, "au", "1"));

        auto [accountCaid, accountProvider] = accountCaidProvider(account);
        int selected = -1;
        for (std::size_t p = 0; p < ports.size(); ++p) {
            if (usedPorts[p]) {
                continue;
            }
            if (!accountCaid.empty() && ports[p].caid != accountCaid) {
                continue;
            }
            if (!accountProvider.empty() && ports[p].provider != accountProvider) {
                continue;
            }
            selected = static_cast<int>(p);
            break;
        }
        if (selected < 0) {
            for (std::size_t p = 0; p < ports.size(); ++p) {
                if (!usedPorts[p]) {
                    selected = static_cast<int>(p);
                    break;
                }
            }
        }
        if (selected >= 0) {
            usedPorts[static_cast<std::size_t>(selected)] = true;
            user.port = ports[static_cast<std::size_t>(selected)].port;
            user.caid = ports[static_cast<std::size_t>(selected)].caid;
            user.provider = ports[static_cast<std::size_t>(selected)].provider;
        } else {
            user.port = 11220 + static_cast<int>(i);
            user.caid = isHex(accountCaid, 4) ? accountCaid : "0652";
            user.provider = isHex(accountProvider, 6) ? accountProvider : "000000";
        }
        settings.users.push_back(user);
    }

    // Preserve port definitions even if a legacy config has fewer accounts than ports.
    for (std::size_t p = 0; p < ports.size(); ++p) {
        if (usedPorts[p]) {
            continue;
        }
        NewcamdUser user;
        user.user = "user" + std::to_string(settings.users.size() + 1);
        user.password = "change_me";
        user.port = ports[p].port;
        user.caid = ports[p].caid;
        user.provider = ports[p].provider;
        user.groups = "1";
        settings.users.push_back(user);
    }

    if (settings.users.empty()) {
        settings.users.push_back(NewcamdUser{});
    }

    for (const auto& entry : parseIniSections(readFile(std::string(kConfigDir) + "/oscam.server"), "reader")) {
        Reader reader;
        reader.label = getValue(entry, "label", "Reader");
        reader.protocol = getValue(entry, "protocol", "mouse");
        reader.device = getValue(entry, "device", "");
        reader.caid = getValue(entry, "caid", "");
        reader.detect = getValue(entry, "detect", "cd");
        reader.mhz = getInt(entry, "mhz", 600);
        reader.cardmhz = getInt(entry, "cardmhz", 600);
        reader.group = getInt(entry, "group", 1);
        reader.ident = getValue(entry, "ident", "");
        reader.boxkey = getValue(entry, "boxkey", "");
        reader.rsakey = getValue(entry, "rsakey", "");
        reader.auprovid = getValue(entry, "auprovid", "");
        reader.emmcache = getValue(entry, "emmcache", "1,3,2,0");
        reader.enabled = !isTrue(getValue(entry, "disable", "0"));
        settings.readers.push_back(reader);
    }

    return settings;
}

bool OscamMiniManager::saveLocked(const Settings& settings, std::string& error) {
    if (!isHex(settings.key, 28)) {
        error = "DES key must contain exactly 28 hexadecimal characters";
        return false;
    }
    if (settings.users.empty()) {
        error = "Add at least one Newcamd user";
        return false;
    }

    std::set<int> ports;
    std::set<std::string> usernames;
    for (const auto& user : settings.users) {
        if (!isSafeName(user.user)) {
            error = "Invalid username: " + user.user;
            return false;
        }
        if (user.password.empty() || user.password.size() > 64 || user.password.find('\n') != std::string::npos || user.password.find('\r') != std::string::npos) {
            error = "Invalid password for user " + user.user;
            return false;
        }
        if (user.port < 1 || user.port > 65535) {
            error = "Invalid port for user " + user.user;
            return false;
        }
        if (!ports.insert(user.port).second) {
            error = "Port " + std::to_string(user.port) + " is used more than once";
            return false;
        }
        if (!usernames.insert(user.user).second) {
            error = "User " + user.user + " is listed more than once";
            return false;
        }
        if (!isHex(user.caid, 4)) {
            error = "CAID for user " + user.user + " must contain 4 hexadecimal characters";
            return false;
        }
        if (!isHex(user.provider, 6)) {
            error = "Provider for user " + user.user + " must contain 6 hexadecimal characters";
            return false;
        }
        if (!validGroups(user.groups)) {
            error = "Invalid groups for user " + user.user;
            return false;
        }
    }

    std::ostringstream conf;
    conf << "[global]\n"
         << "logfile = stdout\n"
         << "clienttimeout = 5000\n"
         << "clientmaxidle = 120\n"
         << "waitforcards = 1\n\n"
         << "[newcamd]\n"
         << "serverip = " << safeIni(settings.bindIp) << "\n"
         << "port = ";
    for (std::size_t i = 0; i < settings.users.size(); ++i) {
        if (i) {
            conf << ';';
        }
        const auto& user = settings.users[i];
        conf << user.port << '@' << safeIni(user.caid) << ':' << safeIni(user.provider);
    }
    conf << "\nkey = " << safeIni(settings.key) << "\n"
         << "keepalive = " << (settings.keepalive ? 1 : 0) << "\n";

    std::ostringstream users;
    for (const auto& user : settings.users) {
        users << "[account]\n"
              << "user = " << safeIni(user.user) << "\n"
              << "pwd = " << safeIni(user.password) << "\n"
              << "group = " << safeIni(user.groups) << "\n"
              << "au = " << (user.au ? 1 : 0) << "\n"
              << "caid = " << safeIni(user.caid) << "\n"
              << "ident = " << safeIni(user.caid) << ':' << safeIni(user.provider) << "\n"
              << "allowedprotocols = newcamd\n\n";
    }

    std::ostringstream server;
    for (const auto& reader : settings.readers) {
        if (reader.label.empty()) {
            error = "Reader name is required";
            return false;
        }
        const bool pcsc = reader.protocol == "pcsc";
        const bool serialDevice = reader.device.rfind("/dev/", 0) == 0 &&
            reader.device.size() > 5 && reader.device.size() <= 256 &&
            reader.device.find_first_of("\r\n;#") == std::string::npos;
        // OSCam's PC/SC backend selects a zero-based index from SCardListReaders.
        // Never pass an OMNIKEY device path or arbitrary text as a PC/SC index.
        const bool pcscIndex = !reader.device.empty() && reader.device.size() <= 3 &&
            std::all_of(reader.device.begin(), reader.device.end(), [](unsigned char c) {
                return std::isdigit(c) != 0;
            }) && std::stoul(reader.device) <= 999;
        if ((pcsc && !pcscIndex) || (!pcsc && !serialDevice)) {
            error = pcsc ? "Specify a PC/SC reader index (0, 1, ...): " + reader.label
                         : "Invalid device for reader " + reader.label;
            return false;
        }
        if (!isHex(reader.caid, 4)) {
            error = "CAID for reader " + reader.label + " must contain 4 hexadecimal characters";
            return false;
        }
        if (reader.group < 1 || reader.group > 64) {
            error = "Invalid group for reader " + reader.label;
            return false;
        }
        if (reader.mhz < 100 || reader.mhz > 2000 || reader.cardmhz < 100 || reader.cardmhz > 2000) {
            error = "Invalid frequency for reader " + reader.label;
            return false;
        }
        if (reader.protocol != "mouse" && reader.protocol != "phoenix" &&
            reader.protocol != "pcsc") {
            error = "Supported protocols are mouse/phoenix/pcsc: " + reader.label;
            return false;
        }
        if (!reader.boxkey.empty() && !isHex(reader.boxkey, 16)) {
            error = "BoxKey for reader " + reader.label + " must contain 16 hexadecimal characters";
            return false;
        }
        if (!reader.rsakey.empty() && (reader.rsakey.size() < 32 || (reader.rsakey.size() % 2) != 0 || !std::all_of(reader.rsakey.begin(), reader.rsakey.end(), [](unsigned char c) { return std::isxdigit(c) != 0; }))) {
            error = "RSAKey for reader " + reader.label + " must contain an even-length hexadecimal string";
            return false;
        }
        if (!reader.auprovid.empty() && !isHex(reader.auprovid, 6)) {
            error = "AU Provider for reader " + reader.label + " must contain 6 hexadecimal characters";
            return false;
        }

        server << "[reader]\n"
               << "label = " << safeIni(reader.label) << "\n"
               << "protocol = " << safeIni(reader.protocol) << "\n"
               << "device = " << safeIni(reader.device) << "\n"
               << "caid = " << safeIni(reader.caid) << "\n";
        if (!reader.boxkey.empty()) {
            server << "boxkey = " << safeIni(reader.boxkey) << "\n";
        }
        if (!reader.rsakey.empty()) {
            server << "rsakey = " << safeIni(reader.rsakey) << "\n";
        }
        server << "detect = " << (pcsc ? "none" : safeIni(reader.detect)) << "\n";
        // PC/SC negotiates clock and card parameters through pcscd.  Phoenix
        // settings must not be imposed on USB OMNIKEY readers.
        if (!pcsc) {
            server << "mhz = " << reader.mhz << "\n"
                   << "cardmhz = " << reader.cardmhz << "\n";
        }
        server << "group = " << reader.group << "\n";
        if (!reader.ident.empty()) {
            server << "ident = " << safeIni(reader.ident) << "\n";
        }
        if (!reader.auprovid.empty()) {
            server << "auprovid = " << safeIni(reader.auprovid) << "\n";
        }
        if (!reader.emmcache.empty()) {
            server << "emmcache = " << safeIni(reader.emmcache) << "\n";
        }
        if (!reader.enabled) {
            server << "disable = 1\n";
        }
        server << '\n';
    }

    return writeAtomic(std::string(kConfigDir) + "/oscam.conf", conf.str(), error)
        && writeAtomic(std::string(kConfigDir) + "/oscam.user", users.str(), error)
        && writeAtomic(std::string(kConfigDir) + "/oscam.server", server.str(), error);
}

Json::Value OscamMiniManager::statusLocked() {
    Json::Value result;
    result["config_dir"] = kConfigDir;
    result["binary_exists"] = fs::exists(kBinary);

    int rc = 0;
    const std::string active = trim(run({"systemctl", "is-active", kService}, &rc));
    const bool serviceActive = active == "active";
    result["service_active"] = serviceActive;
    result["service_state"] = active.empty() ? "unknown" : active;

    Json::Value devices(Json::arrayValue);
    for (const auto& device : ttyDevices()) {
        devices.append(device);
    }
    result["devices"] = devices;

    const std::string process = trim(run({"ps", "-C", "oscam-mini", "-o", "pid=,rss=,vsz=,%cpu=,cmd="}));
    result["process"] = process;

    std::string pid;
    if (serviceActive) {
        pid = trim(run({"systemctl", "show", "-p", "MainPID", "--value", kService}));
        if (pid == "0") {
            pid.clear();
        }
    }

    std::vector<std::string> logCommand{"journalctl", "-u", kService, "-n", "120", "--no-pager", "-o", "cat"};
    if (!pid.empty()) {
        // The PID is only ever taken from `systemctl show -p MainPID`, but it is
        // validated anyway so a malformed value can never become an argument
        // that journalctl would interpret as a match specification.
        const bool pidIsNumeric = std::all_of(pid.begin(), pid.end(),
            [](unsigned char ch) { return std::isdigit(ch) != 0; });
        if (pidIsNumeric) {
            logCommand = {"journalctl", "-u", kService, "_PID=" + pid, "-n", "240",
                          "--no-pager", "-o", "cat"};
        }
    }
    std::string log = run(logCommand);
    if (log.size() > 32000) {
        log = log.substr(log.size() - 32000);
    }
    result["log"] = log;

    const Settings settings = loadLocked();
    const std::string established = serviceActive ? run({"ss", "-Htan", "state", "established"}) : std::string();

    Json::Value userActivity(Json::objectValue);
    for (const auto& user : settings.users) {
        Json::Value item;
        std::string state = "down";
        std::string detail = "OSCam-mini stopped";

        if (serviceActive) {
            const std::string portToken = ":" + std::to_string(user.port);
            if (established.find(portToken) != std::string::npos) {
                state = "active";
                detail = "Newcamd client connected";
            } else {
                state = "idle";
                detail = "Waiting for connection";
            }
        }

        item["state"] = state;
        item["detail"] = detail;
        item["port"] = user.port;
        userActivity[user.user] = item;
    }
    result["user_activity"] = userActivity;

    Json::Value readerActivity(Json::objectValue);
    for (const auto& reader : settings.readers) {
        Json::Value item;
        std::string state = "down";
        std::string detail = "OSCam-mini stopped";

        if (!reader.enabled) {
            state = "disabled";
            detail = "Reader disabled";
        } else if (serviceActive) {
            const std::string readyNeedle = reader.label + " [irdeto] ready for requests";
            const std::string readyMouseNeedle = reader.label + " [mouse] ready for requests";
            const bool ready = log.find(readyNeedle) != std::string::npos
                || log.find(readyMouseNeedle) != std::string::npos;
            const bool initializing =
                log.find(reader.label + " [mouse] card detected") != std::string::npos
                || log.find(reader.label + " [mouse] detect irdeto card") != std::string::npos
                || log.find(reader.label + " [mouse] found card system") != std::string::npos
                || log.find(reader.label + " [irdeto] THIS WAS A SUCCESSFUL START ATTEMPT") != std::string::npos;
            const bool error =
                log.find(reader.label + " [mouse] Error activating card") != std::string::npos
                || log.find(reader.label + " [mouse] ERROR") != std::string::npos
                || log.find(reader.label + " [irdeto] ERROR") != std::string::npos
                || log.find(reader.label + " [mouse] card initializing error") != std::string::npos;

            if (ready) {
                state = "active";
                detail = "Card ready";
            } else if (error) {
                state = "down";
                detail = "Reader error";
            } else if (initializing) {
                state = "idle";
                detail = "Initializing card";
            } else {
                state = "idle";
                detail = "Waiting for card";
            }
        }

        item["state"] = state;
        item["detail"] = detail;
        item["device"] = reader.device;
        readerActivity[reader.label] = item;
    }
    result["reader_activity"] = readerActivity;
    return result;
}

std::string OscamMiniManager::statusJson() {
    std::lock_guard<std::mutex> lock(mutex_);
    return json(statusLocked());
}

std::string OscamMiniManager::settingsJson() {
    std::lock_guard<std::mutex> lock(mutex_);
    const Settings settings = loadLocked();

    Json::Value result;
    result["bind_ip"] = settings.bindIp;
    result["key"] = settings.key;
    result["keepalive"] = settings.keepalive;

    Json::Value users(Json::arrayValue);
    for (const auto& user : settings.users) {
        Json::Value value;
        value["user"] = user.user;
        value["password"] = user.password;
        value["port"] = user.port;
        value["caid"] = user.caid;
        value["provider"] = user.provider;
        value["groups"] = user.groups;
        value["au"] = user.au;
        users.append(value);
    }
    result["users"] = users;

    Json::Value readersJson(Json::arrayValue);
    for (const auto& reader : settings.readers) {
        Json::Value value;
        value["label"] = reader.label;
        value["protocol"] = reader.protocol;
        value["device"] = reader.device;
        value["caid"] = reader.caid;
        value["detect"] = reader.detect;
        value["mhz"] = reader.mhz;
        value["cardmhz"] = reader.cardmhz;
        value["group"] = reader.group;
        value["ident"] = reader.ident;
        value["boxkey"] = reader.boxkey;
        value["rsakey"] = reader.rsakey;
        value["auprovid"] = reader.auprovid;
        value["emmcache"] = reader.emmcache;
        value["enabled"] = reader.enabled;
        readersJson.append(value);
    }
    result["readers"] = readersJson;
    return json(result);
}

std::string OscamMiniManager::saveSettingsJson(const std::string& body) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string error;
    Json::Value input = parse(body, error);
    Json::Value output;

    if (!error.empty()) {
        output["ok"] = false;
        output["error"] = error;
        return json(output);
    }

    Settings settings;
    settings.bindIp = input.get("bind_ip", "127.0.0.1").asString();
    settings.key = input.get("key", "0102030405060708091011121314").asString();
    settings.keepalive = input.get("keepalive", true).asBool();

    if (input["users"].isArray()) {
        for (const auto& item : input["users"]) {
            NewcamdUser user;
            user.user = item.get("user", "tvstreamer").asString();
            user.password = item.get("password", "tvstreamer").asString();
            user.port = item.get("port", 11220).asInt();
            user.caid = item.get("caid", "0652").asString();
            user.provider = item.get("provider", "000000").asString();
            user.groups = item.get("groups", "1").asString();
            user.au = item.get("au", true).asBool();
            settings.users.push_back(user);
        }
    }

    if (input["readers"].isArray()) {
        for (const auto& item : input["readers"]) {
            Reader reader;
            reader.label = item.get("label", "Reader").asString();
            reader.protocol = item.get("protocol", "mouse").asString();
            reader.device = item.get("device", "").asString();
            reader.caid = item.get("caid", "0652").asString();
            reader.detect = item.get("detect", "cd").asString();
            reader.mhz = item.get("mhz", 600).asInt();
            reader.cardmhz = item.get("cardmhz", 600).asInt();
            reader.group = item.get("group", 1).asInt();
            reader.ident = item.get("ident", "").asString();
            reader.boxkey = item.get("boxkey", "").asString();
            reader.rsakey = item.get("rsakey", "").asString();
            reader.auprovid = item.get("auprovid", "").asString();
            reader.emmcache = item.get("emmcache", "1,3,2,0").asString();
            reader.enabled = item.get("enabled", true).asBool();
            settings.readers.push_back(reader);
        }
    }

    if (!saveLocked(settings, error)) {
        output["ok"] = false;
        output["error"] = error;
        return json(output);
    }

    int rc = 0;
    const std::string restartOutput = run({"systemctl", "restart", kService}, &rc);
    output["ok"] = rc == 0;
    if (rc != 0) {
        output["error"] = "Configuration saved, but OSCam-mini could not be restarted: " + trim(restartOutput);
    }
    output["status"] = statusLocked();
    return json(output);
}

std::string OscamMiniManager::serviceActionJson(const std::string& body) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string error;
    const Json::Value input = parse(body, error);
    Json::Value output;

    const std::string action = input.get("action", "").asString();
    if (action != "start" && action != "stop" && action != "restart") {
        output["ok"] = false;
        output["error"] = "unsupported action";
        return json(output);
    }

    int rc = 0;
    const std::string commandOutput = run({"systemctl", action, kService}, &rc);
    output["ok"] = rc == 0;
    if (rc != 0) {
        output["error"] = trim(commandOutput);
    }
    output["status"] = statusLocked();
    return json(output);
}

std::string OscamMiniManager::renderPage() {
    return R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>OSCam-mini</title>
<link rel="stylesheet" href="/preview/adminlte-3.2.0.min.css">
<style>
body{font-family:Arial,sans-serif;background:#0f1218;color:#eee;margin:0}.w{max-width:1320px;margin:auto;padding:16px}.c{background:#161b25;border:1px solid #2a3241;border-radius:18px;padding:20px;margin:14px 0}.g{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:12px}label{display:flex;flex-direction:column;gap:6px;font-size:14px;color:#c6d1e3}input,select{background:#0e131c;border:1px solid #334058;color:#fff;border-radius:10px;padding:11px;font-size:15px}button,a.btn{border:0;border-radius:10px;padding:11px 16px;background:#218cff;color:white;cursor:pointer;text-decoration:none;font-size:15px}.alt{background:#30394a!important}.danger{background:#56313a!important}.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}.grow{flex:1}.status-ok{color:#77e79d}.status-bad{color:#ff8b8b}.msg{margin-left:10px}.error{color:#ff8b8b}.success{color:#77e79d}pre{white-space:pre-wrap;background:#0b0f16;padding:12px;border-radius:10px;max-height:280px;overflow:auto}h2,h3{margin-top:0}.hint{color:#9ba9bd;font-size:13px;margin-top:8px}
.compact-item{border:1px solid #2c3546;border-radius:12px;margin-top:10px;overflow:hidden;background:#111722}.compact-head{display:grid;grid-template-columns:18px minmax(130px,1.2fr) minmax(120px,1fr) minmax(135px,1fr) minmax(90px,.7fr) 24px;gap:10px;align-items:center;width:100%;box-sizing:border-box;padding:11px 13px;background:#171e2a;color:#eef4ff;border:0;border-radius:0;text-align:left;cursor:pointer}.compact-head:hover{background:#1b2432}.compact-main{font-weight:700;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.compact-sub{color:#aebbd0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:13px}.compact-status{font-size:13px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.chev{font-size:18px;color:#91a0b7;transition:transform .15s ease}.compact-item.open .chev{transform:rotate(90deg)}.compact-body{display:none;padding:14px;border-top:1px solid #2c3546}.compact-item.open .compact-body{display:block}.dot{width:10px;height:10px;border-radius:50%;display:inline-block;box-shadow:0 0 0 2px rgba(255,255,255,.08)}.dot-active{background:#49d17d}.dot-idle{background:#e8b844}.dot-down{background:#e25d66}.dot-disabled{background:#798394}.activity-active{color:#65df92}.activity-idle{color:#e8c45d}.activity-down{color:#ef7b83}.activity-disabled{color:#929cac}@media(max-width:760px){.compact-head{grid-template-columns:18px 1fr 24px}.compact-head .hide-small{display:none}}

/* AdminLTE-based dashboard: custom dark theme and responsive layout. */
body{font-family:system-ui,-apple-system,"Segoe UI",sans-serif;background:#101827;color:#e5edf8}
.w{max-width:1440px;padding:28px 32px}.w>.row:first-child{background:#19263a;border:1px solid #33445d;border-radius:16px;padding:18px 22px;box-shadow:0 10px 26px #0003}
.c{background:#1a273b;border:1px solid #30415a;border-radius:14px;padding:22px;box-shadow:0 6px 18px #0002}
.c h3{font-size:1.1rem;font-weight:700;letter-spacing:.01em}.c p,.hint{color:#afbed2}.g{gap:16px}
.row{display:flex;gap:10px;margin:0;align-items:center}.row>*{padding:0}label{margin:0;color:#bdcbe0;font-weight:500}
input,select{background:#111d2f;border:1px solid #40536c;color:#f6f9ff;min-width:0}input:focus,select:focus{outline:2px solid #60a5fa;outline-offset:1px}
input[type=checkbox]{min-width:auto;accent-color:#3b82f6}button,a.btn{background:#2563eb;border-radius:8px;padding:10px 15px;font-weight:600;line-height:1.3}button:hover,a.btn:hover{filter:brightness(1.16);color:white}button:focus-visible,a:focus-visible{outline:3px solid #93c5fd;outline-offset:2px}.alt{background:#334155!important}.danger{background:#9f394b!important}
.compact-item{background:#132034;border-color:#34455e}.compact-head{background:#203047}.compact-head:hover{background:#2b405a}.compact-body{border-color:#34455e}pre{background:#0c1523;color:#d1dfef}.language-label{display:flex;flex-direction:row;align-items:center;gap:8px}.language-label select{padding:7px 10px}
@media(max-width:760px){.w{padding:16px}.w>.row:first-child{padding:16px}.c{padding:16px}.language-label{margin-left:auto}}
</style>
</head>
<body><div class="w">
<div class="row"><a class="btn alt" href="/">← TVStreammerSAT5</a><h2 style="margin:0">OSCam-mini</h2><span id="state" role="status"></span><span class="grow"></span><label class="language-label">Language <select id="locale" aria-label="Language / Idioma"><option value="en">English</option><option value="es">Español</option></select></label></div>
<div class="c"><div class="row"><button onclick="act('start')">Start</button><button class="alt" onclick="act('restart')">Restart</button><button class="alt" onclick="act('stop')">Stop</button><button class="alt" onclick="loadAll()">Refresh</button></div><p>Configuration: /opt/TVStreammerSAT5/oscam-mini/config</p></div>

<div class="c">
  <h3>Newcamd</h3>
  <div class="g"><label>Bind IP<input id="bind_ip"></label><label>DES key<input id="key" type="password" autocomplete="off"></label></div>
  <label style="flex-direction:row;align-items:center;margin-top:12px"><input id="keepalive" type="checkbox"> Keepalive</label>
  <div class="row" style="margin-top:16px"><h3 style="margin:0">Users / ports</h3><button onclick="addUser()">+ User</button></div>
  <div class="hint">Users are collapsed by default. Select a row to edit its settings.</div>
  <div id="users"></div>
</div>

<div class="c"><div class="row"><h3 style="margin:0">Phoenix / Smartmouse</h3><button onclick="addReader()">+ Reader</button></div><div class="hint">Green: card ready; amber: initializing or waiting; red: stopped or error.</div><div id="readers"></div></div>
<div class="c"><button onclick="save()">Save and restart</button><span id="msg" class="msg"></span></div>
<div class="c"><h3>Process / log</h3><pre id="proc"></pre><pre id="log"></pre></div>
</div>
<script>
let devices=[];
let lastStatus={};

const languageKey = 'tvstreammersat5-language';
let locale = localStorage.getItem(languageKey) === 'es' ? 'es' : 'en';
const spanish = new Map(Object.entries({
  'Language':'Idioma', 'Keepalive':'Mantener conexión', 'Provider':'Proveedor', 'Group':'Grupo', 'Bind IP':'IP de escucha', 'Start':'Iniciar', 'Restart':'Reiniciar', 'Stop':'Detener', 'Refresh':'Actualizar',
  'Configuration:':'Configuración:', 'Users / ports':'Usuarios / puertos', '+ User':'+ Usuario',
  'Users are collapsed by default. Select a row to edit its settings.':'Los usuarios aparecen contraídos. Seleccione una fila para editarla.',
  '+ Reader':'+ Lector', 'Green: card ready; amber: initializing or waiting; red: stopped or error.':'Verde: tarjeta lista; amarillo: iniciando o en espera; rojo: detenido o error.',
  'Save and restart':'Guardar y reiniciar', 'Process / log':'Proceso / registro', 'Username':'Usuario', 'Password':'Contraseña',
  'Port':'Puerto', 'Groups':'Grupos', 'Remove user':'Eliminar usuario', 'No device selected':'Ningún dispositivo seleccionado',
  'Name':'Nombre', 'Device / PC/SC index':'Dispositivo / índice PC/SC', 'Protocol':'Protocolo',
  'Remove reader':'Eliminar lector', 'Enabled':'Habilitado', 'Inactive':'Inactivo', 'Running':'En ejecución', 'Stopped':'Detenido',
  'Process not running':'Proceso detenido', 'Saving…':'Guardando…', 'Saved and restarted':'Guardado y reiniciado', 'Error':'Error',
  'OSCam-mini stopped':'OSCam-mini detenido', 'Newcamd client connected':'Cliente Newcamd conectado',
  'Waiting for connection':'Esperando conexión', 'Reader disabled':'Lector deshabilitado', 'Card ready':'Tarjeta lista',
  'Reader error':'Error del lector', 'Initializing card':'Inicializando tarjeta', 'Waiting for card':'Esperando tarjeta',
  'Connection error':'Error de conexión', 'active':'activo', 'inactive':'inactivo', 'failed':'fallido',
  'DES key must contain exactly 28 hexadecimal characters':'La clave DES debe contener exactamente 28 caracteres hexadecimales',
  'Add at least one Newcamd user':'Agregue al menos un usuario Newcamd', 'Reader name is required':'El nombre del lector es obligatorio',
  '/dev/ttyUSB0 or 0':'/dev/ttyUSB0 o 0'
}));
function tr(value) {
  const text = String(value ?? '');
  if (locale !== 'es') return text;
  if (spanish.has(text)) return spanish.get(text);
  const replacements = [
    ['Unable to create temporary file ', 'No se pudo crear el archivo temporal '], ['Failed to write ', 'Error al escribir '],
    ['Invalid username: ', 'Nombre de usuario inválido: '], ['Invalid password for user ', 'Contraseña inválida para el usuario '],
    ['Invalid port for user ', 'Puerto inválido para el usuario '], ['Invalid groups for user ', 'Grupos inválidos para el usuario '],
    ['Specify a PC/SC reader index (0, 1, ...): ', 'Indique el índice del lector PC/SC (0, 1, ...): '],
    ['Invalid device for reader ', 'Dispositivo inválido para el lector '], ['Invalid group for reader ', 'Grupo inválido para el lector '],
    ['Invalid frequency for reader ', 'Frecuencia inválida para el lector '], ['Supported protocols are mouse/phoenix/pcsc: ', 'Protocolos admitidos: mouse/phoenix/pcsc: '],
    ['Configuration saved, but OSCam-mini could not be restarted: ', 'Configuración guardada, pero no se pudo reiniciar OSCam-mini: '],
    [' must contain 4 hexadecimal characters', ' debe contener 4 caracteres hexadecimales'],
    [' must contain 6 hexadecimal characters', ' debe contener 6 caracteres hexadecimales'],
    [' must contain 16 hexadecimal characters', ' debe contener 16 caracteres hexadecimales'],
    [' must contain an even-length hexadecimal string', ' debe contener una cadena hexadecimal de longitud par'],
    ['CAID for user ', 'CAID del usuario '], ['Provider for user ', 'Proveedor del usuario '],
    ['CAID for reader ', 'CAID del lector '], ['BoxKey for reader ', 'BoxKey del lector '],
    ['RSAKey for reader ', 'RSAKey del lector '], ['AU Provider for reader ', 'Proveedor AU del lector '],
    [' is used more than once', ' se usa más de una vez'], [' is listed more than once', ' aparece más de una vez'],
    ['Port ', 'Puerto '], ['User ', 'Usuario ']
  ];
  return replacements.reduce((result, [from,to]) => result.replace(from,to), text);
}
const originals = new WeakMap();
function localizeNode(node) {
  if (node.nodeType !== Node.TEXT_NODE || !node.parentElement || ['SCRIPT','STYLE','PRE'].includes(node.parentElement.tagName) || node.parentElement.closest('.user-summary-name,.reader-summary-label,.reader-summary-device')) return;
  const value = node.nodeValue || '';
  const m = value.match(/^(\s*)(.*?)(\s*)$/s); if (!m || !m[2]) return;
  let entry = originals.get(node);
  if (!entry || (m[2] !== entry.last && m[2] !== entry.source)) entry = {source:m[2], last:m[2]};
  entry.last = tr(entry.source); originals.set(node, entry);
  const next = m[1] + entry.last + m[3]; if (value !== next) node.nodeValue = next;
}
function localize(root) {
  if (root.nodeType === Node.TEXT_NODE) return localizeNode(root);
  const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT); let n;
  while ((n = walker.nextNode())) localizeNode(n);
  root.querySelectorAll?.('[placeholder]').forEach(el => {
    if (!el.dataset.originalPlaceholder) el.dataset.originalPlaceholder = el.placeholder;
    el.placeholder = tr(el.dataset.originalPlaceholder);
  });
}
const observer = new MutationObserver(records => records.forEach(record => {
  if (record.type === 'characterData') localizeNode(record.target);
  else record.addedNodes.forEach(localize);
}));
observer.observe(document.body, {subtree:true, childList:true, characterData:true});
function setLocale(value) {
  locale = value === 'es' ? 'es' : 'en';
  localStorage.setItem(languageKey, locale);
  document.documentElement.lang = locale;
  document.getElementById('locale').value = locale;
  localize(document.body);
}
document.getElementById('locale').addEventListener('change', e => setLocale(e.target.value));
setLocale(locale);

function esc(v){return String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
async function api(url,opt){const r=await fetch(url,opt);const text=await r.text();let data;try{data=JSON.parse(text)}catch(e){throw new Error(text||('HTTP '+r.status))}if(!r.ok)throw new Error(data.error||('HTTP '+r.status));return data}
function stateClass(s){return ['active','idle','down','disabled'].includes(s)?s:'down'}
function activityDot(s){s=stateClass(s);return `<span class="dot dot-${s}"></span>`}
function toggleItem(head){const open=head.closest('.compact-item').classList.toggle('open');head.setAttribute('aria-expanded',String(open))}

function userHtml(u={},i=0){const name=u.user||('user'+(i+1));return `<div class="compact-item user" data-user="${esc(name)}">
<button type="button" class="compact-head" aria-expanded="false" onclick="toggleItem(this)">
<span class="activity-dot">${activityDot('down')}</span>
<span class="compact-main user-summary-name">${esc(name)}</span>
<span class="compact-sub hide-small user-summary-endpoint">${esc(u.port||11220+i)} · ${esc(u.caid||'0652')}:${esc(u.provider||'000000')}</span>
<span class="compact-sub hide-small user-summary-groups">group ${esc(u.groups||'1')}</span>
<span class="compact-status hide-small activity-text">Inactive</span>
<span class="chev">›</span>
</button>
<div class="compact-body"><div class="g">
<label>Username<input class="user-name" value="${esc(name)}"></label>
<label>Password<input class="user-pass" type="password" value="${esc(u.password||'')}"></label>
<label>Port<input class="user-port" type="number" min="1" max="65535" value="${esc(u.port||11220+i)}"></label>
<label>CAID<input class="user-caid" maxlength="4" value="${esc(u.caid||'0652')}"></label>
<label>Provider<input class="user-provider" maxlength="6" value="${esc(u.provider||'000000')}"></label>
<label>Groups<input class="user-groups" value="${esc(u.groups||'1')}" placeholder="6,12"></label>
</div><div class="row" style="margin-top:10px"><label style="flex-direction:row;align-items:center"><input class="user-au" type="checkbox" ${u.au!==false?'checked':''}> AU/EMM</label><button class="danger" onclick="this.closest('.user').remove()">Remove user</button></div></div></div>`}
function addUser(u={}){const box=document.createElement('div');box.innerHTML=userHtml(u,document.querySelectorAll('.user').length);users.append(...box.childNodes);applyActivity(lastStatus)}

function readerHtml(r={},i=0){const label=r.label||('Reader'+(i+1));return `<div class="compact-item reader" data-reader="${esc(label)}" data-detect="${esc(r.detect||'cd')}">
<button type="button" class="compact-head" aria-expanded="false" onclick="toggleItem(this)">
<span class="activity-dot">${activityDot('down')}</span>
<span class="compact-main reader-summary-label">${esc(label)}</span>
<span class="compact-sub hide-small reader-summary-device">${esc(r.device||'No device selected')}</span>
<span class="compact-sub hide-small reader-summary-ca">CAID ${esc(r.caid||'0652')} · group ${esc(r.group||1)}</span>
<span class="compact-status hide-small activity-text">Inactive</span>
<span class="chev">›</span>
</button>
<div class="compact-body"><div class="g">
<label>Name<input class="reader-label" value="${esc(label)}"></label>
<label>Device / PC/SC index<input class="reader-device" value="${esc(r.device||'')}" placeholder="/dev/ttyUSB0 or 0"></label>
<label>CAID<input class="reader-caid" maxlength="4" value="${esc(r.caid||'0652')}"></label>
<label>Protocol<select class="reader-protocol"><option value="mouse" ${r.protocol==='mouse'?'selected':''}>mouse</option><option value="phoenix" ${r.protocol==='phoenix'?'selected':''}>phoenix</option><option value="pcsc" ${r.protocol==='pcsc'?'selected':''}>PC/SC (OMNIKEY)</option></select></label>
<label>MHz (Phoenix)<input class="reader-mhz" type="number" value="${esc(r.mhz||600)}"></label>
<label>Card MHz<input class="reader-cardmhz" type="number" value="${esc(r.cardmhz||600)}"></label>
<label>Group<input class="reader-group" type="number" min="1" max="64" value="${esc(r.group||1)}"></label>
<label>Ident<input class="reader-ident" value="${esc(r.ident||'')}"></label>
<label>BoxKey<input class="reader-boxkey" autocomplete="off" value="${esc(r.boxkey||'')}" placeholder="16 HEX"></label>
<label>AU Provider<input class="reader-auprovid" maxlength="6" value="${esc(r.auprovid||'')}" placeholder="000652"></label>
<label>EMM cache<input class="reader-emmcache" value="${esc(r.emmcache||'1,3,2,0')}" placeholder="1,3,2,0"></label>
<label style="grid-column:1/-1">RSAKey<input class="reader-rsakey" autocomplete="off" value="${esc(r.rsakey||'')}" placeholder="HEX RSA key"></label>
</div><div class="row" style="margin-top:10px"><label style="flex-direction:row;align-items:center"><input class="reader-enabled" type="checkbox" ${r.enabled!==false?'checked':''}> Enabled</label><button class="danger" onclick="this.closest('.reader').remove()">Remove reader</button></div></div></div>`}
function addReader(r={}){const box=document.createElement('div');box.innerHTML=readerHtml(r,document.querySelectorAll('.reader').length);readers.append(...box.childNodes);applyActivity(lastStatus)}

function setActivity(el,info){if(!el)return;const s=stateClass(info?.state||'down');const dot=el.querySelector('.activity-dot');const txt=el.querySelector('.activity-text');if(dot)dot.innerHTML=activityDot(s);if(txt){txt.textContent=info?.detail||'Inactive';txt.className=`compact-status hide-small activity-text activity-${s}`}}
function applyActivity(st){lastStatus=st||{};document.querySelectorAll('.user').forEach(el=>setActivity(el,(st.user_activity||{})[el.dataset.user]));document.querySelectorAll('.reader').forEach(el=>setActivity(el,(st.reader_activity||{})[el.dataset.reader]))}
async function refreshStatus(){try{const st=await api('/api/oscam-mini/status');lastStatus=st;state.textContent=st.service_active?'● Running':'● '+(st.service_state||'Stopped');state.className=st.service_active?'status-ok':'status-bad';proc.textContent=st.process||'Process not running';log.textContent=st.log||'';applyActivity(st)}catch(e){state.textContent='● Connection error';state.className='status-bad';}}

async function loadAll(){try{const st=await api('/api/oscam-mini/status');lastStatus=st;devices=st.devices||[];state.textContent=st.service_active?'● Running':'● '+(st.service_state||'Stopped');state.className=st.service_active?'status-ok':'status-bad';proc.textContent=st.process||'Process not running';log.textContent=st.log||'';const s=await api('/api/oscam-mini/settings');bind_ip.value=s.bind_ip||'127.0.0.1';key.value=s.key||'';keepalive.checked=s.keepalive!==false;users.innerHTML='';(s.users||[]).forEach(addUser);readers.innerHTML='';(s.readers||[]).forEach(addReader);applyActivity(st)}catch(e){msg.textContent=e.message;msg.className='msg error'}}

async function save(){const userRows=[...document.querySelectorAll('.user')].map(e=>({user:e.querySelector('.user-name').value,password:e.querySelector('.user-pass').value,port:+e.querySelector('.user-port').value,caid:e.querySelector('.user-caid').value.trim(),provider:e.querySelector('.user-provider').value.trim(),groups:e.querySelector('.user-groups').value.trim(),au:e.querySelector('.user-au').checked}));const readerRows=[...document.querySelectorAll('.reader')].map(e=>({label:e.querySelector('.reader-label').value,device:e.querySelector('.reader-device').value,caid:e.querySelector('.reader-caid').value.trim(),protocol:e.querySelector('.reader-protocol').value,detect:e.dataset.detect||'cd',mhz:+e.querySelector('.reader-mhz').value,cardmhz:+e.querySelector('.reader-cardmhz').value,group:+e.querySelector('.reader-group').value,ident:e.querySelector('.reader-ident').value.trim(),boxkey:e.querySelector('.reader-boxkey').value.trim(),rsakey:e.querySelector('.reader-rsakey').value.trim(),auprovid:e.querySelector('.reader-auprovid').value.trim(),emmcache:e.querySelector('.reader-emmcache').value.trim(),enabled:e.querySelector('.reader-enabled').checked}));const body={bind_ip:bind_ip.value.trim(),key:key.value.trim(),keepalive:keepalive.checked,users:userRows,readers:readerRows};msg.textContent='Saving…';msg.className='msg';try{const x=await api('/api/oscam-mini/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});msg.textContent=x.ok?'Saved and restarted':(x.error||'Error');msg.className=x.ok?'msg success':'msg error';if(x.ok)await loadAll()}catch(e){msg.textContent=e.message;msg.className='msg error'}}

async function act(action){try{const x=await api('/api/oscam-mini/action',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action})});if(!x.ok){msg.textContent=x.error||'Error';msg.className='msg error'}await refreshStatus()}catch(e){msg.textContent=e.message;msg.className='msg error'}}
loadAll();
setInterval(refreshStatus,3000);
</script>
</body></html>)HTML";
}