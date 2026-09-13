#include "nlc/public_api.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

std::atomic<bool> stopRequested{false};

BOOL WINAPI ConsoleControlHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT) {
        stopRequested.store(true, std::memory_order_release);
        return TRUE;
    }
    return FALSE;
}

struct Arguments {
    bool inboundEnabled{true};
    bool outboundEnabled{true};
    std::int64_t inboundDelayUs{0};
    std::int64_t outboundDelayUs{0};
    int durationSeconds{10};
    bool showStats{false};
    bool applyDelay{false};
    std::wstring csvPath{};
};

std::string RequireValue(int& index, int argc, char** argv) {
    if (++index >= argc) {
        throw std::invalid_argument(std::string("missing value after ") + argv[index - 1]);
    }
    return argv[index];
}

bool ParseBool(const std::string& value) {
    if (value == "true") return true;
    if (value == "false") return false;
    throw std::invalid_argument("boolean value must be true or false");
}

void ApplyPreset(Arguments& args, const std::string& name) {
    int rttMs = 0;
    if (name == "rtt-10") rttMs = 10;
    else if (name == "rtt-15") rttMs = 15;
    else if (name == "rtt-20") rttMs = 20;
    else if (name == "rtt-25") rttMs = 25;
    else if (name == "rtt-30") rttMs = 30;
    else throw std::invalid_argument("unknown preset: " + name);
    args.inboundEnabled = true;
    args.outboundEnabled = true;
    args.inboundDelayUs = static_cast<std::int64_t>(rttMs) * 500;
    args.outboundDelayUs = static_cast<std::int64_t>(rttMs) * 500;
}

Arguments ParseArguments(int argc, char** argv) {
    Arguments args;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--inbound-enabled") {
            args.inboundEnabled = ParseBool(RequireValue(index, argc, argv));
        } else if (option == "--outbound-enabled") {
            args.outboundEnabled = ParseBool(RequireValue(index, argc, argv));
        } else if (option == "--inbound-delay-us") {
            args.inboundDelayUs = std::stoll(RequireValue(index, argc, argv));
        } else if (option == "--outbound-delay-us") {
            args.outboundDelayUs = std::stoll(RequireValue(index, argc, argv));
        } else if (option == "--duration") {
            args.durationSeconds = std::stoi(RequireValue(index, argc, argv));
        } else if (option == "--show-stats") {
            args.showStats = true;
        } else if (option == "--apply-delay") {
            args.applyDelay = true;
        } else if (option == "--csv") {
            const auto value = RequireValue(index, argc, argv);
            args.csvPath.assign(value.begin(), value.end());
        } else if (option == "--preset") {
            ApplyPreset(args, RequireValue(index, argc, argv));
        } else if (option == "--help") {
            std::cout
                << "Naraka Latency Controller CLI\n\n"
                << "  --inbound-enabled <true|false>\n"
                << "  --outbound-enabled <true|false>\n"
                << "  --inbound-delay-us <0..100000>\n"
                << "  --outbound-delay-us <0..100000>\n"
                << "  --duration <seconds; 0 waits for Ctrl+C>\n"
                << "  --show-stats\n  --csv <path>\n  --apply-delay\n"
                << "  --preset <rtt-10|rtt-15|rtt-20|rtt-25|rtt-30>\n\n"
                << "No interception handle is opened unless --apply-delay is present.\n";
            std::exit(0);
        } else {
            throw std::invalid_argument("unknown option: " + option);
        }
    }
    if (args.inboundDelayUs < 0 || args.inboundDelayUs > 100'000 ||
        args.outboundDelayUs < 0 || args.outboundDelayUs > 100'000 ||
        args.durationSeconds < 0) {
        throw std::invalid_argument("delay must be 0..100000 us and duration non-negative");
    }
    return args;
}

void PrintDirection(const char* name, const NL_DirectionMetrics& metrics) {
    std::cout << name << " enabled=" << static_cast<int>(metrics.enabled)
              << " delayUs=" << metrics.configuredDelayUs
              << " captured=" << metrics.capturedPackets
              << " scheduled=" << metrics.scheduledPackets
              << " injected=" << metrics.injectedPackets
              << " bypass=" << metrics.bypassPackets
              << " dropped=" << metrics.droppedPackets
              << " queue=" << metrics.queueDepth
              << " maxQueue=" << metrics.maximumQueueDepth
              << " avgDelayUs=" << std::fixed << std::setprecision(1)
              << metrics.averageActualDelayUs
              << " p95ErrorUs=" << metrics.p95SchedulingErrorUs
              << " p99ErrorUs=" << metrics.p99SchedulingErrorUs << '\n';
}

void PrintMetrics() {
    NL_MetricsSnapshot metrics{};
    metrics.structSize = sizeof(metrics);
    if (nl_get_metrics(&metrics) != NL_OK) {
        return;
    }
    std::cout << "state=" << static_cast<int>(metrics.engineState)
              << " uptimeMs=" << metrics.engineUptimeMs
              << " pool=" << metrics.currentPoolUsage << '/' << metrics.maximumPoolUsage
              << " csvDropped=" << metrics.csvRecordsDropped << '\n';
    PrintDirection("inbound ", metrics.inbound);
    PrintDirection("outbound", metrics.outbound);
}

std::string LastErrorText() {
    wchar_t buffer[1024]{};
    if (nl_get_last_error(buffer, static_cast<std::uint32_t>(std::size(buffer))) != NL_OK) {
        return "native error text unavailable";
    }
    const std::wstring wide(buffer);
    if (wide.empty()) return {};
    const auto length = WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
        nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
        utf8.data(), length, nullptr, nullptr);
    return utf8;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto args = ParseArguments(argc, argv);
        if (!args.applyDelay) {
            std::cout << "Configuration valid. No interception opened; pass --apply-delay to start.\n";
            return 0;
        }
        if ((!args.inboundEnabled || args.inboundDelayUs == 0) &&
            (!args.outboundEnabled || args.outboundDelayUs == 0)) {
            throw std::invalid_argument(
                "at least one enabled direction must have a non-zero delay");
        }
        SetConsoleCtrlHandler(ConsoleControlHandler, TRUE);
        if (nl_initialize() != NL_OK) {
            throw std::runtime_error("native initialization failed");
        }
        if (!args.csvPath.empty() && nl_set_csv_path(args.csvPath.c_str()) != NL_OK) {
            throw std::runtime_error("CSV configuration failed");
        }
        NL_DelaySettings settings{};
        settings.structSize = sizeof(settings);
        settings.inboundEnabled = args.inboundEnabled ? 1 : 0;
        settings.outboundEnabled = args.outboundEnabled ? 1 : 0;
        settings.inboundDelayUs = args.inboundDelayUs;
        settings.outboundDelayUs = args.outboundDelayUs;
        const auto startResult = nl_start(&settings);
        if (startResult != NL_OK) {
            throw std::runtime_error(
                "start failed (code " + std::to_string(startResult) + "): " + LastErrorText());
        }

        const auto started = std::chrono::steady_clock::now();
        while (!stopRequested.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (args.showStats) {
                PrintMetrics();
            }
            if (args.durationSeconds != 0 &&
                std::chrono::steady_clock::now() - started >=
                    std::chrono::seconds(args.durationSeconds)) {
                break;
            }
        }
        const auto stopResult = nl_stop_and_flush(10'000);
        PrintMetrics();
        nl_shutdown();
        if (stopResult != NL_OK) {
            std::cerr << "safe stop did not complete within the requested timeout\n";
            return 2;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        nl_shutdown();
        return 1;
    }
}
