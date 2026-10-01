// Per-frame guest cost of a cartridge on the PRG32-QT core (ESP32-C6 model).
// perf file.prg32 warmup frames [frame:mask ...]  -> average instructions and
// modelled cycles per frame over the measured window.
#include "Cartridge.h"
#include "Runtime.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <vector>
int main(int argc, char** argv) {
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
    std::string e;
    auto c = prg32::Cartridge::parse(b, e);
    if (!c) { std::cerr << e << "\n"; return 3; }
    prg32::Runtime r;
    if (!r.load(*c, e) || !r.init(e)) { std::cerr << e << "\n"; return 4; }
    int warm = std::stoi(argv[2]), frames = std::stoi(argv[3]);
    std::map<int, uint32_t> script;
    for (int i = 4; i < argc; i++) {
        std::string v = argv[i];
        auto s = v.find(':');
        script[std::stoi(v.substr(0, s))] = uint32_t(std::stoul(v.substr(s + 1)));
    }
    uint32_t in = 0;
    const uint64_t period = 5'280'000;          // 33 ms at 160 MHz
    uint64_t ins = 0, late_ins = 0, late_cyc = 0;
    for (int i = 0; i < warm + frames; i++) {
        if (auto it = script.find(i); it != script.end()) in = it->second;
        // Re-anchor the frame schedule: a frame that overruns then reports its
        // own true cycle count; a faster frame is padded to one period.
        r.setPerformanceMode(prg32::PerformanceMode::Esp32C6Accurate);
        uint64_t i0 = r.retiredInstructions(), c0 = r.virtualCycles();
        if (!r.frame(in, e)) { std::cerr << "frame " << i << ": " << e << "\n"; return 5; }
        if (i >= warm) {
            uint64_t d = r.virtualCycles() - c0, n = r.retiredInstructions() - i0;
            ins += n;
            if (d > period) { late_ins += n; late_cyc += d; }
        }
    }
    // Cycles per instruction: measured on overrunning frames when there are
    // any, otherwise the ratio observed for this code base (ALU/load/mul mix).
    double cpi = late_ins ? double(late_cyc) / double(late_ins) : 1.95;
    double cycles = double(ins) / frames * cpi;
    std::cout << "instr/frame=" << ins / frames << " cycles/frame=" << uint64_t(cycles) << " ("
              << cycles / 160000.0 << " ms modelled)\n";
    return 0;
}
