# Multi-Pattern Matching Engine for Network Intrusion Detection

A high-throughput, streaming multi-pattern matcher in C++20 for Linux x86-64 —
the core scanning primitive a NIDS (Snort/Suricata/Hyperscan-class) uses to test
network traffic against thousands of signatures at once.

The engine is an **Aho-Corasick** automaton with a **SIMD byte-set prefilter**,
fed by a **lock-free SPSC ring** from a **bounds-checked packet reassembler**.
Correctness is enforced by differential testing against a naive reference
matcher, streaming-equivalence testing, sanitizers, and a fuzzer.

> Everything here builds and runs in a Linux container; every performance number
> below was measured on this machine, not estimated. See
> [Reproducing the numbers](#reproducing-the-numbers).

## Components

| Component | Files | What it does |
|-----------|-------|--------------|
| Aho-Corasick engine | `include/mpm/aho_corasick.hpp`, `src/aho_corasick.cpp` | Deterministic goto table (one lookup/byte), flattened outputs, streaming `State` threaded across chunks |
| Naive reference | `include/mpm/naive_matcher.hpp` | Obviously-correct oracle for differential testing |
| SIMD prefilter | `include/mpm/prefilter.hpp`, `src/simd/prefilter_{scalar,sse42,avx2}.cpp` | Exact arbitrary-256-set membership scan; scalar / SSE4.2 / AVX2 with **runtime CPUID dispatch** |
| Lock-free SPSC ring | `include/mpm/spsc_ring.hpp` | Wait-free single-producer/single-consumer queue; every memory ordering justified ([docs/memory-model.md](docs/memory-model.md)) |
| Packet parse + reassembly | `include/mpm/packet.hpp`, `src/packet.cpp` | Framing + out-of-order stream reassembly, bounded against hostile input |
| Benchmark harness | `bench/bench.cpp` | Measures throughput, emits JSON |

### SIMD discipline

The binary is **never** compiled with `-march=native`. Only the two SIMD
translation units get architecture flags, applied per-file in CMake:

```cmake
set_source_files_properties(src/simd/prefilter_sse42.cpp PROPERTIES COMPILE_OPTIONS "-msse4.2")
set_source_files_properties(src/simd/prefilter_avx2.cpp  PROPERTIES COMPILE_OPTIONS "-mavx2")
```

The scalar TU carries no SIMD and does the runtime dispatch (`__builtin_cpu_supports`),
so the AVX2 code path is only ever *called* on a CPU that has AVX2.

## Building & testing

Everything runs in a reproducible toolchain container (GCC 13 + Clang 17):

```bash
docker build -f docker/Dockerfile.dev -t nids-dev .

# build + full test suite with one compiler (gcc | clang)
scripts/dev.sh scripts/ci.sh gcc
scripts/dev.sh scripts/ci.sh clang
```

Both compilers build clean under `-Wall -Wextra -Wpedantic -Werror`.

### Correctness gates

| Gate | How | Scale |
|------|-----|-------|
| Differential vs naive | `test_differential` | **1,000,000** randomized (pattern-set, input) cases; match sets must be identical |
| Streaming equivalence | `test_streaming` | whole-buffer vs split at *every* boundary (incl. 1 byte at a time) must match |
| SIMD vs scalar | `test_prefilter` | every compiled tier vs scalar reference, **1,000,000** cases |
| SPSC ring | `test_spsc` | 5,000,000 items through a tiny ring; order + no loss/dup |
| Packet robustness | `test_packet` | unit cases + 200k random/corrupted inputs |

Sanitizers (Clang ASan + UBSan) — the full suite passes clean:

```bash
scripts/dev.sh bash -c 'CC=clang-17 CXX=clang++-17 cmake -S . -B build/asan -G Ninja -DMPM_SANITIZE=ON && cmake --build build/asan -j && ctest --test-dir build/asan --output-on-failure'
```

Fuzzing (libFuzzer + ASan on the attacker-controlled parser):

```bash
scripts/dev.sh bash -c 'CC=clang-17 CXX=clang++-17 cmake -S . -B build/fuzz -G Ninja -DMPM_FUZZER=ON && cmake --build build/fuzz --target fuzz_packet -j && ./build/fuzz/fuzz_packet -max_total_time=60'
```
Latest run: ~955k executions in 60 s, no crashes / OOB / hangs.

## Benchmark results

Measured on a **13th Gen Intel Core i7-1360P**, inside the Linux container,
64 MiB haystack, best of 5 passes. Raw JSON in `bench/results/`.

| Metric | GCC 13 | Clang 17 |
|--------|-------:|---------:|
| Aho-Corasick scan, 2000 patterns (26,561 states) | 44.1 MiB/s | 41.0 MiB/s |
| Aho-Corasick scan, 200 patterns (2,870 states)   | 86.9 MiB/s | — |
| Prefilter scan, scalar | 1,004.9 MiB/s | 1,236.1 MiB/s |
| Prefilter scan, **AVX2 (dispatched)** | **5,749.6 MiB/s** | **6,550.7 MiB/s** |
| Naive reference scan | 0.1 MiB/s | 0.1 MiB/s |

```bash
scripts/dev.sh ./build/gcc/bench bench/results/gcc.json --mb 64 --patterns 2000
```

### Honest reading of these numbers

- **The AVX2 prefilter is ~5–6× the scalar scan** (5.7 GiB/s vs 1.0 GiB/s), and
  the differential test proves it returns byte-identical results.
- **Aho-Corasick throughput is memory-bound, not compute-bound.** A 2000-pattern
  automaton has a ~27 MB goto table; every input byte is an effectively random
  access into it, so it runs at DRAM-latency speed (~44 MiB/s). Shrinking the
  automaton to 200 patterns (its table fits in cache) roughly doubles throughput
  to ~87 MiB/s. This is the classic large-DFA cache-miss wall, not a code defect.
- The intended production shape follows directly: use the fast SIMD prefilter to
  skip the vast majority of traffic and only pay the DFA cost on candidate
  regions. The pieces for that are all here; wiring the prefilter into the AC hot
  path as an automatic fast-path is the next optimization (and would itself be
  gated by the differential test).
- **Naive at 0.1 MiB/s** is why the automaton exists — it is ~400× slower and is
  used only as the correctness oracle.

## Repository layout

```
include/mpm/     public headers (engine, matcher, prefilter, ring, packet, gen)
src/             library sources; src/simd/ holds the per-arch SIMD TUs
tests/           differential, streaming, SIMD, SPSC, packet tests + harness
bench/           benchmark harness; bench/results/ holds measured JSON
fuzz/            libFuzzer target for the packet parser
docs/            memory-model.md (SPSC ordering rationale)
docker/          GCC 13 + Clang 17 toolchain image
scripts/         dev.sh (run in container), ci.sh (configure/build/test)
```

## Reproducing the numbers

Nothing in this README is hand-entered from memory. Re-run
`scripts/dev.sh ./build/<compiler>/bench` and compare against
`bench/results/*.json`. If a measurement here ever disagrees with a fresh run on
your hardware, the JSON is the source of truth.
