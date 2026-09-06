#include "Parser.h"
#include <x86intrin.h>

namespace itch {

Parser::Parser(concurrency::SpscRingBuffer<NormalizedEvent, 1048576>& ring_buffer)
    : ring_buffer_(ring_buffer) {}

inline uint64_t Parser::parse_timestamp(const uint8_t ts[6]) const {
    return (static_cast<uint64_t>(ts[0]) << 40) |
           (static_cast<uint64_t>(ts[1]) << 32) |
           (static_cast<uint64_t>(ts[2]) << 24) |
           (static_cast<uint64_t>(ts[3]) << 16) |
           (static_cast<uint64_t>(ts[4]) << 8)  |
           (static_cast<uint64_t>(ts[5]));
}

void Parser::parse_udp_payload(const network::PacketData& packet) {
    if (packet.length < sizeof(MoldUdp64Header)) return;

    const uint8_t* ptr = packet.payload;
    const auto* mold_hdr = reinterpret_cast<const MoldUdp64Header*>(ptr);
    ptr += sizeof(MoldUdp64Header);
    
    uint16_t message_count = swap16(mold_hdr->message_count);
    size_t remaining_length = packet.length - sizeof(MoldUdp64Header);

    for (uint16_t i = 0; i < message_count; ++i) {
        if (remaining_length < 2) break; 
        
        uint16_t msg_length = swap16(*reinterpret_cast<const uint16_t*>(ptr));
        ptr += 2;
        remaining_length -= 2;

        if (remaining_length < msg_length) break;

        uint8_t message_type = *ptr;
        switch (message_type) {
            case 'A': {
                if (msg_length == sizeof(AddOrderMessage)) {
                    handle_add_order(reinterpret_cast<const AddOrderMessage*>(ptr));
                }
                break;
            }
            case 'E': {
                if (msg_length == sizeof(OrderExecutedMessage)) {
                    handle_order_executed(reinterpret_cast<const OrderExecutedMessage*>(ptr));
                }
                break;
            }
            case 'X': {
                if (msg_length == sizeof(OrderCanceledMessage)) {
                    handle_order_canceled(reinterpret_cast<const OrderCanceledMessage*>(ptr));
                }
                break;
            }
            default: break;
        }

        ptr += msg_length;
        remaining_length -= msg_length;
    }
}

void Parser::handle_add_order(const AddOrderMessage* msg) {
    NormalizedEvent event;
    event.type = EventType::Add;
    event.timestamp_ns = parse_timestamp(msg->header.timestamp);
    event.order_ref = swap64(msg->order_reference_number);
    event.add.shares = swap32(msg->shares);
    event.add.price = swap32(msg->price);
    
    while (!ring_buffer_.push(event)) {
        _mm_pause();
    }
}

void Parser::handle_order_executed(const OrderExecutedMessage* msg) {
    NormalizedEvent event;
    event.type = EventType::Execute;
    event.timestamp_ns = parse_timestamp(msg->header.timestamp);
    event.order_ref = swap64(msg->order_reference_number);
    event.exec.executed_shares = swap32(msg->executed_shares);
    
    while (!ring_buffer_.push(event)) { _mm_pause(); }
}

void Parser::handle_order_canceled(const OrderCanceledMessage* msg) {
    NormalizedEvent event;
    event.type = EventType::Cancel;
    event.timestamp_ns = parse_timestamp(msg->header.timestamp);
    event.order_ref = swap64(msg->order_reference_number);
    event.cancel.canceled_shares = swap32(msg->canceled_shares);
    
    while (!ring_buffer_.push(event)) { _mm_pause(); }
}

}