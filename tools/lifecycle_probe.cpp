#include "nlc/public_api.h"

#include <windows.h>

#include <cstdint>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::wstring LastErrorText() {
    wchar_t buffer[1024]{};
    (void)nl_get_last_error(buffer, static_cast<std::uint32_t>(std::size(buffer)));
    return buffer;
}

DWORD HandleCount() {
    DWORD count = 0;
    if (!GetProcessHandleCount(GetCurrentProcess(), &count)) {
        throw std::runtime_error("GetProcessHandleCount failed");
    }
    return count;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        int cycles = 100;
        if (argc == 3 && std::wstring_view(argv[1]) == L"--cycles") {
            cycles = std::stoi(argv[2]);
        } else if (argc != 1) {
            std::wcerr << L"usage: nlc_lifecycle_probe [--cycles N]\n";
            return 64;
        }
        if (cycles <= 0 || cycles > 10'000) {
            throw std::invalid_argument("cycles must be 1..10000");
        }

        if (nl_initialize() != NL_OK) {
            throw std::runtime_error("nl_initialize failed");
        }
        const auto handlesBefore = HandleCount();

        NL_DelaySettings settings{};
        settings.structSize = sizeof(settings);
        settings.inboundEnabled = 1;
        settings.outboundEnabled = 1;
        settings.inboundDelayUs = 1'000;
        settings.outboundDelayUs = 1'000;

        for (int cycle = 0; cycle < cycles; ++cycle) {
            const auto start = nl_start(&settings);
            if (start != NL_OK) {
                std::wcerr << L"start failed at cycle " << cycle << L": "
                           << LastErrorText() << L'\n';
                nl_shutdown();
                return 2;
            }
            const auto stop = nl_stop_and_flush(10'000);
            if (stop != NL_OK) {
                std::wcerr << L"stop failed at cycle " << cycle << L": "
                           << LastErrorText() << L'\n';
                nl_shutdown();
                return 3;
            }
        }

        const auto handlesAfter = HandleCount();
        NL_MetricsSnapshot metrics{};
        metrics.structSize = sizeof(metrics);
        (void)nl_get_metrics(&metrics);
        nl_shutdown();

        std::wcout << L"cycles=" << cycles
                   << L" handlesBefore=" << handlesBefore
                   << L" handlesAfter=" << handlesAfter
                   << L" handleDelta="
                   << static_cast<long long>(handlesAfter) - handlesBefore
                   << L" starts=" << metrics.totalStartCount
                   << L" stops=" << metrics.totalStopCount << L'\n';
        return handlesAfter > handlesBefore + 2 ? 4 : 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        nl_shutdown();
        return 1;
    }
}
