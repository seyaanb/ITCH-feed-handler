#pragma once
#include "../network/PcapReader.h"
#include "ItchMessages.h"
#include "../concurrency/SpscRingBuffer.h"

namespace itch {

enum class EventType : uint8_t { Add, Execute, Cancel };

struct alignas(32) NormalizedEvent {
    EventType type;
    uint64_t timestamp_ns;
    uint64_t order_ref;
    union {
        struct { uint32_t shares; uint32_t price; } add;
        struct { uint32_t executed_shares; } exec;
        struct { uint32_t canceled_shares; } cancel;
    };
};

class Parser {
public:
    explicit Parser(concurrency::SpscRingBuffer<NormalizedEvent, 1048576>& ring_buffer);

    void parse_udp_payload(const network::PacketData& packet);

private:
    concurrency::SpscRingBuffer<NormalizedEvent, 1048576>& ring_buffer_;

    inline uint64_t swap64(uint64_t val) const { return __builtin_bswap64(val); }
    inline uint32_t swap32(uint32_t val) const { return __builtin_bswap32(val); }
    inline uint16_t swap16(uint16_t val) const { return __builtin_bswap16(val); }
    inline uint64_t parse_timestamp(const uint8_t ts[6]) const;

    void handle_add_order(const AddOrderMessage* msg);
    void handle_order_executed(const OrderExecutedMessage* msg);
    void handle_order_canceled(const OrderCanceledMessage* msg);
};

}