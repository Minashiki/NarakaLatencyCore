#include "nlc/async_csv_recorder.h"

#include <stdexcept>
#include <utility>

namespace nlc {

AsyncCsvRecorder::AsyncCsvRecorder(std::string path, std::size_t capacity)
    : path_(std::move(path)), queue_(capacity) {
    if (path_.empty() || capacity == 0) {
        throw std::invalid_argument("CSV path and queue capacity are required");
    }
    writer_ = std::thread(&AsyncCsvRecorder::WriterMain, this);
}

AsyncCsvRecorder::~AsyncCsvRecorder() {
    Stop();
}

bool AsyncCsvRecorder::TryRecord(const CsvSchedulingRecord& record) noexcept {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock() || stopping_ || size_ == queue_.size()) {
        return false;
    }
    queue_[tail_] = record;
    tail_ = (tail_ + 1) % queue_.size();
    ++size_;
    lock.unlock();
    condition_.notify_one();
    return true;
}

void AsyncCsvRecorder::Stop() noexcept {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    condition_.notify_all();
    if (writer_.joinable()) {
        writer_.join();
    }
}

void AsyncCsvRecorder::WriterMain() noexcept {
    try {
        std::ofstream output(path_, std::ios::out | std::ios::trunc);
        if (!output) {
            return;
        }
        output << "sequence,direction,captureTimeQpc,dueTimeQpc,actualSendTimeQpc,"
                  "configuredDelayUs,actualDelayUs,schedulingErrorUs,sendResult\n";
        std::size_t recordsSinceFlush = 0;
        for (;;) {
            CsvSchedulingRecord record;
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, [this] { return stopping_ || size_ != 0; });
                if (size_ == 0 && stopping_) {
                    break;
                }
                record = queue_[head_];
                head_ = (head_ + 1) % queue_.size();
                --size_;
            }
            output << record.sequence << ',' << DirectionName(record.direction) << ','
                   << record.captureTimeQpc << ',' << record.dueTimeQpc << ','
                   << record.actualSendTimeQpc << ',' << record.configuredDelayUs << ','
                   << record.actualDelayUs << ',' << record.schedulingErrorUs << ','
                   << (record.sendResult ? 1 : 0) << '\n';
            if (++recordsSinceFlush == 256) {
                output.flush();
                recordsSinceFlush = 0;
            }
        }
        output.flush();
    } catch (...) {
    }
}

}  // namespace nlc

