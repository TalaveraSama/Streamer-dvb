#include "AtomicFile.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <csignal>
#include <unistd.h>

int main() {
    char pattern[] = "/tmp/tvs-atomic-test-XXXXXX";
    char* dir = ::mkdtemp(pattern);
    if (!dir) return 1;
    std::filesystem::path root(dir), file = root / "config.json";
    { std::ofstream out(file); out << "old"; }
    bool success = tvs::writePrivateAtomic(file, "new config");
    struct stat st{};
    std::ifstream in(file);
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    bool good = success && data == "new config" && ::stat(file.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600;
    // Simulate a write failure after a partial write without risking the old file.
    const pid_t child = ::fork();
    if (child == 0) {
        std::signal(SIGXFSZ, SIG_IGN);
        const struct rlimit limit{1, 1};
        if (::setrlimit(RLIMIT_FSIZE, &limit) != 0) _exit(2);
        _exit(tvs::writePrivateAtomic(file, "replacement too large") ? 3 : 0);
    }
    int status = 0;
    if (child < 0 || ::waitpid(child, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) good = false;
    std::ifstream afterFailure(file);
    std::string unchanged((std::istreambuf_iterator<char>(afterFailure)), std::istreambuf_iterator<char>());
    good = good && unchanged == "new config";
    // rename to a directory must fail, preserve the directory and clean the temp.
    const auto existingDir = root / "existing-directory";
    std::filesystem::create_directory(existingDir);
    good = good && !tvs::writePrivateAtomic(existingDir, "invalid") && std::filesystem::is_directory(existingDir);
    for (const auto& item : std::filesystem::directory_iterator(root)) {
        if (item.path().filename().string().find(".tmp.") != std::string::npos) good = false;
    }
    std::filesystem::remove_all(root);
    if (!good) std::cerr << "Atomic private file replacement failed\n";
    return good ? 0 : 1;
}
