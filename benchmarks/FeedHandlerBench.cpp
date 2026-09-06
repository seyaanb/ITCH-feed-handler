#include "../src/network/PcapReader.h"
#include "../src/itch/Parser.h"
#include "../src/concurrency/SpscRingBuffer.h"

#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <pthread.h>
#include <x86intrin.h>

constexpr size_t RING_BUFFER_CAPACITY = 1048576;

using RingBuffer =
    concurrency::SpscRingBuffer<itch::NormalizedEvent, RING_BUFFER_CAPACITY>;

void pin_thread_to_core(pthread_t thread, int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_pcap>\n";
        return 1;
    }

    std::string pcap_path = argv[1];

    auto* ring_buffer = new RingBuffer();

    std::atomic<bool> producer_done{false};
    std::atomic<uint64_t> messages_consumed{0};

    std::thread consumer([&]() {
        pin_thread_to_core(pthread_self(), 2);

        itch::NormalizedEvent event;
        uint64_t local_count = 0;

        for (;;) {
            if (ring_buffer->pop(event)) {
                local_count++;
                continue;
            }

            if (producer_done.load(std::memory_order_acquire)) {
                break;
            }

            _mm_pause();
        }

        messages_consumed.store(
            local_count,
            std::memory_order_release
        );
    });

    pin_thread_to_core(pthread_self(), 1);

    auto start_time = std::chrono::high_resolution_clock::now();

    try {
        network::PcapReader reader(pcap_path);
        itch::Parser parser(*ring_buffer);
        network::PacketData packet;

        while (reader.next_udp_payload(packet)) {
            parser.parse_udp_payload(packet);
        }
    } catch (const std::exception& e) {
        std::cerr << "Engine Fault: " << e.what() << "\n";
    }

    producer_done.store(true, std::memory_order_release);
    consumer.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end_time - start_time;

    uint64_t total_msgs =
        messages_consumed.load(std::memory_order_acquire);

    double msgs_per_sec = total_msgs / diff.count();

    std::cout << "--- HFT Feed Handler Benchmark ---\n";
    std::cout << "Time Elapsed   : " << diff.count() << " seconds\n";
    std::cout << "Msgs Processed : " << total_msgs << "\n";
    std::cout << "Throughput     : "
              << msgs_per_sec / 1e6
              << " Million Msgs/sec\n";

    delete ring_buffer;
    return 0;
}