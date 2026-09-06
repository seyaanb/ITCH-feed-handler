#pragma once
#include <cstdint>

namespace itch {

// NASDAQ ITCH feeds over UDP are encapsulated in MoldUDP64 packets.
struct __attribute__((packed)) MoldUdp64Header {
    uint8_t  session[10];
    uint64_t sequence_number;
    uint16_t message_count;
};

// Base ITCH 5.0 Header included in every message type
struct __attribute__((packed)) ItchHeader {
    uint8_t  message_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint8_t  timestamp[6];
};

// Message Type 'A' - Add Order (No MPID)
struct __attribute__((packed)) AddOrderMessage {
    ItchHeader header;
    uint64_t order_reference_number;
    uint8_t  buy_sell_indicator;
    uint32_t shares;
    uint8_t  stock[8];
    uint32_t price;
};

// Message Type 'E' - Order Executed
struct __attribute__((packed)) OrderExecutedMessage {
    ItchHeader header;
    uint64_t order_reference_number;
    uint32_t executed_shares;
    uint64_t match_number;
};

// Message Type 'X' - Order Canceled
struct __attribute__((packed)) OrderCanceledMessage {
    ItchHeader header;
    uint64_t order_reference_number;
    uint32_t canceled_shares;
};

}