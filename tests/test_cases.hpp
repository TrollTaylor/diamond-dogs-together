// The cases of one test executable, so that CTest can run it as several parallel shards
// (docs/STATUS.md "Developer test loop"). Each case names its measured cost; the command line selects:
//   (nothing)             every case, in table order
//   --case A[,B...]       only these cases (the developer loop)
//   --shard K/N           shard K (1-based) of N: the cases are dealt longest first to the shard with the least
//                         cost so far, so every case runs in exactly one shard of the N (CMake registers K = 1..N,
//                         cmake/test_shards.cmake), and each shard runs its cases in table order
//   --list                the shard of each case for every N up to 8, nothing run
// Each run case prints "case <name>: <ms> ms". An unknown case or a bad shard fails the run (exit 2).
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace mgscoop::test {

struct TestCase {
    const char* name;
    void (*run)();
    unsigned cost_ms;   // measured alone on the developer PC; only balances the shards
};

// Shard of every case (0-based) for N shards: longest first, to the least loaded shard (lowest index on ties).
inline std::vector<unsigned> AssignTestShards(const TestCase* cases, std::size_t count, unsigned shards) {
    std::vector<std::size_t> order(count);
    for (std::size_t i = 0; i < count; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return cases[a].cost_ms > cases[b].cost_ms; });
    std::vector<unsigned long long> load(shards, 0);
    std::vector<unsigned> shard_of(count, 0);
    for (const std::size_t i : order) {
        unsigned best = 0;
        for (unsigned s = 1; s < shards; ++s)
            if (load[s] < load[best]) best = s;
        shard_of[i] = best;
        load[best] += cases[i].cost_ms;
    }
    return shard_of;
}

// Fills `selected` from the command line; false (after printing why) for bad arguments or an unknown case.
// `listed` is set for --list (print only, run nothing).
inline bool SelectTestCases(const TestCase* cases, std::size_t count, int argc, char** argv,
                            std::vector<const TestCase*>& selected, bool& listed) {
    selected.clear();
    listed = false;
    if (argc <= 1) {
        for (std::size_t i = 0; i < count; ++i) selected.push_back(&cases[i]);
        return true;
    }
    const std::string mode = argv[1];
    if (argc == 2 && mode == "--list") {
        listed = true;
        for (std::size_t i = 0; i < count; ++i) {
            std::printf("%s (%u ms):", cases[i].name, cases[i].cost_ms);
            for (unsigned n = 2; n <= 8; ++n) std::printf(" %u/%u", AssignTestShards(cases, count, n)[i] + 1, n);
            std::printf("\n");
        }
        return true;
    }
    if (argc == 3 && mode == "--case") {
        const std::string list = argv[2];
        for (std::size_t at = 0; at <= list.size();) {
            const std::size_t comma = std::min(list.find(',', at), list.size());
            const std::string name = list.substr(at, comma - at);
            at = comma + 1;
            const TestCase* found = nullptr;
            for (std::size_t i = 0; i < count; ++i)
                if (name == cases[i].name) found = &cases[i];
            if (!found) {
                std::fprintf(stderr, "unknown case: %s\n", name.c_str());
                return false;
            }
            selected.push_back(found);
        }
        return true;
    }
    if (argc == 3 && mode == "--shard") {
        const std::string spec = argv[2];
        const std::size_t slash = spec.find('/');
        const unsigned index = slash == std::string::npos ? 0u : static_cast<unsigned>(std::strtoul(spec.substr(0, slash).c_str(), nullptr, 10));
        const unsigned shards = slash == std::string::npos ? 0u : static_cast<unsigned>(std::strtoul(spec.substr(slash + 1).c_str(), nullptr, 10));
        if (shards < 1 || shards > 64 || index < 1 || index > shards || spec != std::to_string(index) + "/" + std::to_string(shards)) {
            std::fprintf(stderr, "bad shard: %s (want K/N with 1 <= K <= N <= 64)\n", spec.c_str());
            return false;
        }
        const std::vector<unsigned> shard_of = AssignTestShards(cases, count, shards);
        for (std::size_t i = 0; i < count; ++i)
            if (shard_of[i] == index - 1) selected.push_back(&cases[i]);
        return true;
    }
    std::fprintf(stderr, "usage: [--case A[,B...] | --shard K/N | --list]\n");
    return false;
}

inline void RunTestCases(const std::vector<const TestCase*>& selected) {
    for (const TestCase* c : selected) {
        const ULONGLONG started = GetTickCount64();
        c->run();
        std::printf("case %s: %llu ms\n", c->name, static_cast<unsigned long long>(GetTickCount64() - started));
        std::fflush(stdout);
    }
}

}  // namespace mgscoop::test
