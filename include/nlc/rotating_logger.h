#pragma once

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string_view>

namespace nlc {

class RotatingLogger final {
public:
    RotatingLogger(
        std::filesystem::path directory,
        std::size_t maximumBytes = 5 * 1024 * 1024,
        std::size_t retainedFiles = 5);

    void Write(std::string_view event, std::string_view message) noexcept;
    [[nodiscard]] const std::filesystem::path& Directory() const noexcept;

private:
    void RotateIfNeeded();

    std::filesystem::path directory_;
    std::filesystem::path activePath_;
    std::size_t maximumBytes_;
    std::size_t retainedFiles_;
    std::mutex mutex_;
};

[[nodiscard]] std::filesystem::path DefaultLogDirectory();

}  // namespace nlc

