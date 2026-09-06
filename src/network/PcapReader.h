#pragma once
#include <cstdint>
#include <string>

namespace network {

struct PacketData {
    const uint8_t* payload;
    uint32_t length;
};

class PcapReader {
    public:
        explicit PcapReader(const std::string& file_path);
        ~PcapReader();

        PcapReader(const PcapReader&) = delete;
        PcapReader& operator=(const PcapReader&) = delete;
        PcapReader(PcapReader&&) = delete;
        PcapReader& operator=(PcapReader&&) = delete;

        bool next_udp_payload(PacketData& outpacket);
    
    private:
        int fd_;
        size_t file_size_;
        const uint8_t* mapped_buffer_;
        const uint8_t* current_ptr_;
        const uint8_t* end_ptr_;
};

}