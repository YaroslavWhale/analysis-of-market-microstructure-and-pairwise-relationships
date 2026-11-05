#include "app/TradingSystem.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

namespace {

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

void print_usage(const char* prog) {
    std::cout
        << "Usage:\n"
        << "  " << prog << " -sym <SYMBOL> [-depth <N>]\n"
        << "  " << prog << " -sym <SYMBOL> -sym2 <SYMBOL> [-depth <N>] [-window <N>]\n"
        << "            [-entry <Z>] [-exit <Z>] [-csv <FILE>]\n"
        << "  " << prog << " -demo [-window <N>] [-entry <Z>] [-exit <Z>]\n\n"
        << "  -sym     first / single symbol (e.g. BTCUSDT)\n"
        << "  -sym2    second symbol; enables pair (cointegration) analysis\n"
        << "  -depth   order-book depth (default 10)\n"
        << "  -window  rolling window for spread statistics, in samples (default 200)\n"
        << "  -entry   z-score entry threshold (default 2.0)\n"
        << "  -exit    z-score exit threshold  (default 0.5)\n"
        << "  -csv     optional file to log the per-sample analysis as CSV\n"
        << "  -demo    run the offline synthetic cointegration self-test\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string sym;
    std::string sym2;
    std::string csv_path;
    int depth = 10;
    std::size_t window = 200;
    double entry_z = 2.0;
    double exit_z = 0.5;
    bool demo = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-sym" && i + 1 < argc) {
            sym = to_upper(argv[++i]);
        } else if (arg == "-sym2" && i + 1 < argc) {
            sym2 = to_upper(argv[++i]);
        } else if (arg == "-depth" && i + 1 < argc) {
            depth = std::stoi(argv[++i]);
        } else if (arg == "-window" && i + 1 < argc) {
            window = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else if (arg == "-entry" && i + 1 < argc) {
            entry_z = std::stod(argv[++i]);
        } else if (arg == "-exit" && i + 1 < argc) {
            exit_z = std::stod(argv[++i]);
        } else if (arg == "-csv" && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (arg == "-demo") {
            demo = true;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }

    try {
        if (demo) {
            run_demo(window, entry_z, exit_z);
        } else if (!sym.empty() && !sym2.empty()) {
            run_pair(sym, sym2, static_cast<uint64_t>(depth), window, csv_path,
                     entry_z, exit_z);
        } else if (!sym.empty()) {
            run(sym, static_cast<uint64_t>(depth));
        } else {
            print_usage(argv[0]);
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

