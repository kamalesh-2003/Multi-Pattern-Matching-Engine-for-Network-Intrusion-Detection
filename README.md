# Multi-Pattern Matching Engine for Network Intrusion Detection

A streaming multi-pattern matcher in C++20 for Linux x86-64 — the scanning core a
NIDS uses to test network traffic against thousands of signatures at once. It
pairs an Aho-Corasick automaton with a SIMD byte-set prefilter, fed through a
lock-free ring from a bounds-checked packet reassembler.

## Design

```
wire bytes ── producer ──► SPSC ring ──► consumer ──► StreamScanner ──► matches
                                                       (parse → reassemble → scan)
```

| Component | Header / source | Notes |
|-----------|-----------------|-------|
| Aho-Corasick engine | `aho_corasick.{hpp,cpp}` | Deterministic goto table, one lookup per byte; streaming state threaded across chunks |
| SIMD prefilter | `prefilter.hpp`, `src/simd/prefilter_{scalar,sse42,avx2}.cpp` | Exact arbitrary byte-set membership; runtime CPUID dispatch |
| Lock-free SPSC ring | `spsc_ring.hpp` | Wait-free queue; orderings documented in [docs/memory-model.md](docs/memory-model.md) |
| Packet reassembly | `packet.{hpp,cpp}` | Out-of-order stream reassembly, bounded against hostile input |
| Stream scanner | `scanner.hpp` | Ties reassembly to the automaton, per-stream state |
| Reference matcher | `naive_matcher.hpp` | Correctness oracle for differential testing |

SIMD is confined to two translation units compiled with per-file `-march` flags
(`-msse4.2`, `-mavx2`); the rest of the binary is built for the baseline ISA and
reaches the vector paths only through runtime CPUID dispatch. `-march=native` is
never used.

## Build & test

Requires GCC 13 or Clang 17. A container image with both is provided:

```sh
docker build -f docker/Dockerfile.dev -t nids-dev .
scripts/dev.sh scripts/ci.sh gcc      # configure, build, ctest
scripts/dev.sh scripts/ci.sh clang
```

Both compilers build clean under `-Wall -Wextra -Wpedantic -Werror`.

Run the detector:

```sh
scripts/dev.sh ./build/gcc/nids --patterns signatures.txt --in traffic.bin --show
scripts/dev.sh ./build/gcc/nids --patterns signatures.txt --demo --show   # synthetic input
```

## Testing

| Test | Coverage |
|------|----------|
| `test_differential` | Engine vs. naive oracle, 1,000,000 randomized cases, identical match sets |
| `test_streaming` | Whole-buffer vs. split-at-every-boundary equivalence |
| `test_pipeline` | End-to-end parse→reassemble→scan under out-of-order, byte-fragmented delivery |
| `test_prefilter` | Every SIMD tier vs. scalar, 1,000,000 cases |
| `test_spsc` | 5,000,000 items through a small ring; order preserved, no loss |
| `test_packet` | Framing/reassembly units + 200k random/corrupted inputs |

Sanitizers and fuzzing:

```sh
# ASan + UBSan across the whole suite
scripts/dev.sh bash -c 'CC=clang-17 CXX=clang++-17 cmake -S . -B build/asan -G Ninja -DMPM_SANITIZE=ON && cmake --build build/asan -j && ctest --test-dir build/asan --output-on-failure'
# libFuzzer on the packet parser
scripts/dev.sh bash -c 'CC=clang-17 CXX=clang++-17 cmake -S . -B build/fuzz -G Ninja -DMPM_FUZZER=ON && cmake --build build/fuzz --target fuzz_packet -j && ./build/fuzz/fuzz_packet -max_total_time=60'
```

## Benchmarks

`bench` emits JSON; regenerate with `scripts/dev.sh ./build/gcc/bench out.json`.
Measured on a 13th Gen Intel Core i7-1360P, 64 MiB input, best of 5 passes:

| Metric | GCC 13 | Clang 17 |
|--------|-------:|---------:|
| Prefilter scan, AVX2 (dispatched) | 5,750 MiB/s | 6,551 MiB/s |
| Prefilter scan, scalar | 1,005 MiB/s | 1,236 MiB/s |
| Aho-Corasick scan, 2000 patterns (26,561 states) | 44 MiB/s | 41 MiB/s |
| Aho-Corasick scan, 200 patterns (2,870 states) | 87 MiB/s | — |
| Naive reference scan | 0.1 MiB/s | 0.1 MiB/s |

The automaton is memory-bound on large dictionaries: a 2000-pattern goto table is
~27 MB, so each byte is effectively a random access at DRAM latency. Shrinking the
dictionary until the table fits in cache roughly doubles throughput. This is why
the prefilter exists — in a production configuration it skips traffic that cannot
begin any signature so the automaton only runs on candidate regions.

## Layout

```
include/mpm/   public headers
src/           library sources; src/simd/ holds the per-arch SIMD units
apps/          nids command-line detector
tests/         test suite + support/check.hpp harness
bench/         benchmark harness
fuzz/          libFuzzer target
docs/          memory-model.md
docker/        toolchain image; scripts/ dev + CI helpers
```
