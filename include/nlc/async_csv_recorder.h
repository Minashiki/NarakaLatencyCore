#pragma once

#include "nlc/direction.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nlc {

struct CsvSchedulingRecord {
    std::uint64_t sequence{0};
    Direction direction{Direction::Inbound};
    std::int64_t captureTimeQpc{0};
    std::int64_t dueTimeQpc{0};
    std::int64_t actualSendTimeQpc{0};
    std::int64_t configuredDelayUs{0};
    double actualDelayUs{0.0};
    double schedulingErrorUs{0.0};
    bool sendResult{false};
};

class AsyncCsvRecorder final {
public:
    explicit AsyncCsvRecorder(std::string path, std::size_t capacity = 16'384);
    ~AsyncCsvRecorder();

    AsyncCsvRecorder(const AsyncCsvRecorder&) = delete;
    AsyncCsvRecorder& operator=(const AsyncCsvRecorder&) = delete;

    [[nodiscard]] bool TryRecord(const CsvSchedulingRecord& record) noexcept;
    void Stop() noexcept;

private:
    void WriterMain() noexcept;

    std::string path_;
    std::vector<CsvSchedulingRecord> queue_;
    std::size_t head_{0};
    std::size_t tail_{0};
    std::size_t size_{0};
    std::mutex mutex_;
    std::condition_variable condition_;
    bool stopping_{false};
    std::thread writer_;
};

}  // namespace nlc

