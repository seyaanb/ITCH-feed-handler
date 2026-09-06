#pragma once
#include <cstdint>

namespace network {

struct __attribute__((packed)) PcapGlobalHeader {
    uint32_t magic_number;
    uint16_t version_major;
    uint16_t version_minor;
    int32_t thiszone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;
};

struct __attribute__((packed)) PcapPacketHeader {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
};

struct __attribute__((packed)) EthernetHeader {
    uint8_t dest_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype;
};

struct __attribute__((packed)) Ipv4Header {
    uint8_t  version_ihl;
    uint8_t  tos;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment_offset;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t header_checksum;
    uint32_t source_ip;
    uint32_t dest_ip;
};

struct __attribute__((packed)) UdpHeader {
    uint16_t source_port;
    uint16_t dest_port;
    uint16_t length;
    uint16_t checksum;
};

}