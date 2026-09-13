#include "nlc/rotating_logger.h"

#include <windows.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace nlc {

RotatingLogger::RotatingLogger(
    std::filesystem::path directory,
    std::size_t maximumBytes,
    std::size_t retainedFiles)
    : directory_(std::move(directory)),
      activePath_(directory_ / L"NarakaLatencyController.log"),
      maximumBytes_(maximumBytes),
      retainedFiles_((std::max<std::size_t>)(1, retainedFiles)) {
}

void RotatingLogger::Write(std::string_view event, std::string_view message) noexcept {
    try {
        std::lock_guard lock(mutex_);
        std::filesystem::create_directories(directory_);
        RotateIfNeeded();
        SYSTEMTIME time{};
        GetSystemTime(&time);
        std::ofstream output(activePath_, std::ios::out | std::ios::app);
        output << '{' << "\"utc\":\""
               << std::setfill('0') << std::setw(4) << time.wYear << '-'
               << std::setw(2) << time.wMonth << '-' << std::setw(2) << time.wDay
               << 'T' << std::setw(2) << time.wHour << ':' << std::setw(2) << time.wMinute
               << ':' << std::setw(2) << time.wSecond << '.' << std::setw(3)
               << time.wMilliseconds << "Z\",\"event\":\"" << event
               << "\",\"message\":\"" << message << "\"}\n";
    } catch (...) {
        // Logging must never affect engine lifecycle or packet processing.
    }
}

const std::filesystem::path& RotatingLogger::Directory() const noexcept {
    return directory_;
}

void RotatingLogger::RotateIfNeeded() {
    if (!std::filesystem::exists(activePath_) ||
        std::filesystem::file_size(activePath_) < maximumBytes_) {
        return;
    }
    for (std::size_t index = retainedFiles_; index > 1; --index) {
        const auto destination = directory_ /
            (L"NarakaLatencyController.log." + std::to_wstring(index));
        const auto source = directory_ /
            (L"NarakaLatencyController.log." + std::to_wstring(index - 1));
        std::error_code ignored;
        std::filesystem::remove(destination, ignored);
        if (std::filesystem::exists(source)) {
            std::filesystem::rename(source, destination, ignored);
        }
    }
    const auto first = directory_ / L"NarakaLatencyController.log.1";
    std::error_code ignored;
    std::filesystem::remove(first, ignored);
    std::filesystem::rename(activePath_, first, ignored);
}

std::filesystem::path DefaultLogDirectory() {
    wchar_t buffer[32'768]{};
    const auto length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    if (length > 0 && length < std::size(buffer)) {
        return std::filesystem::path(buffer) / L"NarakaLatencyController" / L"logs";
    }
    return std::filesystem::temp_directory_path() /
        L"NarakaLatencyController" / L"logs";
}

}  // namespace nlc
