// platform:: implementation for the 3DS port (replaces util/platform.cpp)
#include "util/platform.hpp"

#include <3ds.h>

#include <thread>
#include <chrono>

namespace platform {
    void configure_encoding() {}

    std::string detect_locale() {
        return "en_US";
    }

    void open_folder(const std::filesystem::path&) {}

    void sleep(size_t millis) {
        svcSleepThread(static_cast<s64>(millis) * 1000000);
    }

    int get_process_id() {
        return 0;
    }

    std::filesystem::path get_executable_path() {
        return "sdmc:/3ds/voxelcore/voxelcore3ds.3dsx";
    }

    void new_engine_instance(
        const std::vector<std::string>&, std::filesystem::path
    ) {}

    bool open_url(const std::string&) {
        return false;
    }

    bool stdin_has_data() {
        return false;
    }
}
