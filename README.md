# Zero-Copy NASDAQ ITCH 5.0 Feed Handler

C++20 feed handler for reading NASDAQ ITCH 5.0 traffic from a PCAP file and passing parsed messages through a lock-free SPSC ring buffer.

The main idea is pretty simple: map the PCAP into memory, walk the packets in place, decode the fields we care about, and avoid allocating or copying messages along the way.

This is Linux/x86-oriented code. It is deliberately not trying to be a portable packet parsing library.

## How it works

The packet path is:

```text
Ethernet
  -> IPv4
    -> UDP
      -> MoldUDP64
        -> ITCH
```

The PCAP is opened with `mmap` and `MAP_POPULATE`. There is no `ifstream` read loop in the main parser; once the file is mapped, the parser walks the capture directly.

The Ethernet/IP/UDP/Mold headers are represented as packed structs and read from the packet buffer. Network-order integers are converted with the GCC/Clang byte-swap builtins.

At the moment there are three ITCH message types:

* `A` — Add
* `E` — Execute
* `X` — Cancel

Decoded messages are written to a fixed-size SPSC ring buffer. The producer and consumer positions are kept on separate cache lines, and the ring uses acquire/release atomics for synchronisation.

## Build

Requirements:

* C++20
* GCC 10+ or Clang 11+
* CMake 3.15+
* Linux

The Linux requirement is intentional. The implementation uses POSIX/Linux APIs such as `mmap` and `MAP_POPULATE`.

Python 3 is only needed for the synthetic PCAP generator.

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

## Run

Generate the sample capture first:

```bash
python3 scripts/generate_synthetic_itch.py
```

This creates:

```text
data/sample.pcap
```

with 1,000,000 synthetic ITCH messages using only `A`, `E`, and `X`.

Then run the two benchmarks:

```bash
./naive_bench ../data/sample.pcap
./feed_handler_bench ../data/sample.pcap
```

`naive_bench` is there as a baseline. It uses `ifstream`, a mutex, and shift-based byte swapping instead of the approach used by the main parser.

For hardware counters:

```bash
perf stat -e cycles,instructions,cache-misses,L1-dcache-load-misses,context-switches,page-faults \
  ./naive_bench ../data/sample.pcap

perf stat -e cycles,instructions,cache-misses,L1-dcache-load-misses,context-switches,page-faults \
  ./feed_handler_bench ../data/sample.pcap
```

## Benchmark

The current benchmark uses the synthetic one-million-message capture described above.

### Normal run

Running the binaries directly gives:

| Metric             |      `naive_bench` | `feed_handler_bench` |
| ------------------ | -----------------: | -------------------: |
| Time elapsed       |         0.180467 s |          0.0176612 s |
| Messages processed |          1,000,000 |            1,000,000 |
| Throughput         | 5.54119 M msgs/sec |   56.6213 M msgs/sec |

On this run, the feed handler processes the capture about **10.2x faster** than the naive baseline and achieves about **10.2x the throughput**.

Both implementations process the full one-million-message capture in this benchmark.

### `perf` run

The same binaries were also run under `perf` with the following counters:

```text
cycles
instructions
cache-misses
L1-dcache-load-misses
context-switches
page-faults
```

The results were:

| Metric                 |      `naive_bench` | `feed_handler_bench` |
| ---------------------- | -----------------: | -------------------: |
| Time elapsed           |          0.11662 s |          0.0300819 s |
| Messages processed     |          1,000,000 |            1,000,000 |
| Throughput             | 8.57484 M msgs/sec |   33.2426 M msgs/sec |
| Cycles                 |        862,468,204 |          271,174,981 |
| Instructions           |        933,171,758 |          148,181,635 |
| Instructions per cycle |               1.08 |                 0.55 |
| Cache misses           |          1,340,596 |            1,783,819 |
| L1 D-cache load misses |          8,586,083 |            3,419,305 |
| Context switches       |             10,429 |                    4 |
| Page faults            |              4,009 |                7,952 |

Under `perf`, the feed handler uses about **3.18x fewer CPU cycles** and executes about **6.30x fewer instructions** than the naive implementation.

It also has substantially fewer L1 D-cache load misses and context switches, although total cache misses are slightly higher in this particular run.

The `perf` runs should not be compared directly with the normal runs for absolute throughput. Instrumentation changes execution time, and the two measurement modes therefore serve different purposes: the normal run is the primary throughput comparison, while the `perf` run is useful for understanding CPU and memory behaviour.

The benchmark is intentionally small and synthetic. It is mainly useful for comparing the two implementations on the same machine; it is not meant to predict production exchange-feed performance.

## Some implementation details

The project is built with `-fno-strict-aliasing` in `CMakeLists.txt`.

The packet parser uses packed structs so the on-wire headers can be treated as structures instead of being copied into temporary objects. That keeps the parsing code fairly small, but it also means the code is making assumptions about the compiler and target architecture.

The other obvious issue is alignment. Some of these fields are not naturally aligned once they are packed. Unaligned loads are fine on x86, although they are not necessarily free. This would need more work on architectures where unaligned access is restricted.

The SPSC ring is fixed-size. If the consumer falls behind and the ring fills up, `push` spins using `_mm_pause()` until a slot becomes available.

## Usage

The basic flow looks like this:

```cpp
#include "src/network/PcapReader.h"
#include "src/itch/Parser.h"
#include "src/concurrency/SpscRingBuffer.h"

concurrency::SpscRingBuffer<itch::NormalizedEvent, 1048576> ring_buffer;
network::PcapReader reader("data/sample.pcap");
itch::Parser parser(ring_buffer);
network::PacketData packet;

while (reader.next_udp_payload(packet)) {
    parser.parse_udp_payload(packet);
}

// consumer thread
itch::NormalizedEvent event;
while (ring_buffer.pop(event)) {
    // handle event.type:
    // EventType::Add, EventType::Execute, EventType::Cancel
}
```

## Current limitations

The packet parser expects unfragmented IPv4 packets and assumes the UDP packet fits within a standard 1500-byte Ethernet MTU.

There is no IPv6 support and no IP fragment reassembly.

On the ITCH side, only `A`, `E`, and `X` are currently decoded. `R` (Replace) and `D` (Delete) are not implemented, so this is not enough by itself to maintain a complete order book from a general ITCH feed.

The packet parsing code also assumes the packet layout used by the capture and the packed-struct layout described above. In practice that means this should be treated as Linux/x86 code rather than something expected to build and behave the same way everywhere.

The benchmark has the same limitation. One million synthetic messages with three message types is useful for the comparison here, but it is a long way from a real exchange capture.

## Next things to try

The obvious missing pieces are:

* IP fragment reassembly
* ITCH `R` (Replace) and `D` (Delete)
* a real exchange capture with a less convenient message mix
* runs on more than one machine/configuration

If the mmap-based parser actually becomes the bottleneck, the next experiment would probably be DPDK or `ef_vi`.

For now, the code is intentionally fairly small. The interesting part of the project is the packet-to-message path and the cost of moving those messages through the pipeline, rather than trying to build a full market-data stack.
