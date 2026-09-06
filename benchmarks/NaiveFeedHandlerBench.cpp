#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstring>
#include <atomic>
#include <arpa/inet.h>
#include "../src/itch/ItchMessages.h"
#include "../src/itch/Parser.h"

static inline uint64_t naive_swap64(uint64_t val) {
    return (((val & 0xFF00000000000000ULL) >> 56) |
            ((val & 0x00FF000000000000ULL) >> 40) |
            ((val & 0x0000FF0000000000ULL) >> 24) |
            ((val & 0x000000FF00000000ULL) >> 8)  |
            ((val & 0x00000000FF000000ULL) << 8)  |
            ((val & 0x0000000000FF0000ULL) << 24) |
            ((val & 0x000000000000FF00ULL) << 40) |
            ((val & 0x00000000000000FFULL) << 56));
}

static inline uint32_t naive_swap32(uint32_t val) {
    return (((val & 0xFF000000) >> 24) |
            ((val & 0x00FF0000) >> 8)  |
            ((val & 0x0000FF00) << 8)  |
            ((val & 0x000000FF) << 24));
}

static inline uint16_t naive_swap16(uint16_t val) {
    return ((val >> 8) | (val << 8));
}

class NaiveBlockingQueue {
public:
    void push(const itch::NormalizedEvent& ev) {
        std::lock_guard<std::mutex> lock(mtx_);
        q_.push(ev);
    }

    bool pop(itch::NormalizedEvent& ev) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (q_.empty()) return false;
        ev = q_.front();
        q_.pop();
        return true;
    }

private:
    std::queue<itch::NormalizedEvent> q_;
    std::mutex mtx_;
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_pcap>\n";
        return 1;
    }

    std::ifstream file(argv[1], std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Cannot open " << argv[1] << "\n";
        return 1;
    }

    file.seekg(24, std::ios::beg);

    NaiveBlockingQueue queue;
    std::atomic<bool> producer_done{false};
    uint64_t total_consumed = 0;

    auto start_time = std::chrono::high_resolution_clock::now();

    std::thread consumer([&]() {
        itch::NormalizedEvent ev;

        for (;;) {
            if (queue.pop(ev)) {
                total_consumed++;
                continue;
            }

            if (producer_done.load(std::memory_order_acquire)) {
                break;
            }

            std::this_thread::yield();
        }
    });

    while (file.good() && !file.eof()) {
        uint32_t ts_sec, ts_usec, incl_len, orig_len;
        file.read(reinterpret_cast<char*>(&ts_sec), 4);
        file.read(reinterpret_cast<char*>(&ts_usec), 4);
        file.read(reinterpret_cast<char*>(&incl_len), 4);
        file.read(reinterpret_cast<char*>(&orig_len), 4);
        if (file.gcount() < 4) break;

        std::vector<uint8_t> packet_data(incl_len);
        file.read(reinterpret_cast<char*>(packet_data.data()), incl_len);
        if (static_cast<uint32_t>(file.gcount()) < incl_len) break;

        if (incl_len <= 42) continue;
        size_t offset = 42;

        if (incl_len - offset < sizeof(itch::MoldUdp64Header)) continue;
        auto* mold = reinterpret_cast<itch::MoldUdp64Header*>(
            packet_data.data() + offset
        );
        uint16_t msg_count = naive_swap16(mold->message_count);
        offset += sizeof(itch::MoldUdp64Header);

        for (uint16_t i = 0; i < msg_count; ++i) {
            if (offset + 2 > incl_len) break;

            uint16_t msg_len = naive_swap16(
                *reinterpret_cast<uint16_t*>(packet_data.data() + offset)
            );
            offset += 2;

            if (offset + msg_len > incl_len) break;
            uint8_t msg_type = packet_data[offset];

            itch::NormalizedEvent ev;

            if (msg_type == 'A' &&
                msg_len == sizeof(itch::AddOrderMessage)) {

                auto* a = reinterpret_cast<itch::AddOrderMessage*>(
                    packet_data.data() + offset
                );

                ev.type = itch::EventType::Add;
                ev.order_ref = naive_swap64(a->order_reference_number);
                ev.add.shares = naive_swap32(a->shares);
                ev.add.price = naive_swap32(a->price);

                queue.push(ev);

            } else if (msg_type == 'E' &&
                       msg_len == sizeof(itch::OrderExecutedMessage)) {

                auto* e = reinterpret_cast<itch::OrderExecutedMessage*>(
                    packet_data.data() + offset
                );

                ev.type = itch::EventType::Execute;
                ev.order_ref = naive_swap64(e->order_reference_number);
                ev.exec.executed_shares = naive_swap32(e->executed_shares);

                queue.push(ev);

            } else if (msg_type == 'X' &&
                       msg_len == sizeof(itch::OrderCanceledMessage)) {

                auto* x = reinterpret_cast<itch::OrderCanceledMessage*>(
                    packet_data.data() + offset
                );

                ev.type = itch::EventType::Cancel;
                ev.order_ref = naive_swap64(x->order_reference_number);
                ev.cancel.canceled_shares = naive_swap32(x->canceled_shares);

                queue.push(ev);
            }

            offset += msg_len;
        }
    }

    producer_done.store(true, std::memory_order_release);
    consumer.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end_time - start_time;

    std::cout << "--- Naive Baseline Benchmark ---\n";
    std::cout << "Time Elapsed   : " << diff.count() << " seconds\n";
    std::cout << "Msgs Processed : " << total_consumed << "\n";
    std::cout << "Throughput     : "
              << (total_consumed / diff.count()) / 1e6
              << " Million Msgs/sec\n";

    return 0;
}