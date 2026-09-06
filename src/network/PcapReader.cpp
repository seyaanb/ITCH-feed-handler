#include "PcapReader.h"
#include "Headers.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdexcept>
#include <arpa/inet.h>

namespace network {

PcapReader::PcapReader(const std::string& file_path)
    : fd_{ -1 }, file_size_{ 0 }, mapped_buffer_{ nullptr }, current_ptr_{ nullptr }, end_ptr_{ nullptr } {
        fd_ = open(file_path.c_str(), O_RDONLY);
        if (fd_ < 0) {
            throw std::runtime_error("Failed to open PCAP file: " + file_path);
        }

        struct stat st{};
        if (fstat(fd_, &st) < 0) {
            close(fd_);
            throw std::runtime_error("Failed to stat PCAP file: " + file_path);
        }
        file_size_ = st.st_size;

        mapped_buffer_ = static_cast<const uint8_t*>(
            mmap(nullptr, file_size_, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd_, 0)
        );

        if (mapped_buffer_ == MAP_FAILED) {
            close(fd_);
            throw std::runtime_error("Failed to mmap PCAP file");
        }

        if (file_size_ < sizeof(PcapGlobalHeader)) {
            throw std::runtime_error("File too small to contain PCAP global header");
        }

        const auto* global_hdr = reinterpret_cast<const PcapGlobalHeader*>(mapped_buffer_);

        if (global_hdr->magic_number != 0xa1b2c3d4 && global_hdr->magic_number != 0xa1b23c4d) {
            throw std::runtime_error("Invalid or unsupported PCAP magic number");
        }

        current_ptr_ = mapped_buffer_ + sizeof(PcapGlobalHeader);
        end_ptr_ = mapped_buffer_ + file_size_;
    }

PcapReader::~PcapReader() {
    if (mapped_buffer_ != MAP_FAILED && mapped_buffer_ != nullptr) {
        munmap(const_cast<uint8_t*>(mapped_buffer_), file_size_);
    }
    if (fd_ >= 0) {
        close(fd_);
    }
}

bool PcapReader::next_udp_payload(PacketData& out_packet) {
    while (current_ptr_ + sizeof(PcapPacketHeader) <= end_ptr_) {
        const auto* pcap_hdr = reinterpret_cast<const PcapPacketHeader*>(current_ptr_);
        current_ptr_ += sizeof(PcapPacketHeader);

        if (current_ptr_ + pcap_hdr->incl_len > end_ptr_) {
            break;
        }

        const uint8_t* packet_start = current_ptr_;
        current_ptr_ += pcap_hdr->incl_len;

        if (pcap_hdr->incl_len < sizeof(EthernetHeader)) continue;
        const auto* eth_hdr = reinterpret_cast<const EthernetHeader*>(packet_start);
        
        if (ntohs(eth_hdr->ethertype) != 0x0800) continue;

        const uint8_t* ip_start = packet_start + sizeof(EthernetHeader);
        size_t remaining_len = pcap_hdr->incl_len - sizeof(EthernetHeader);

        if (remaining_len < sizeof(Ipv4Header)) continue;
        const auto* ip_hdr = reinterpret_cast<const Ipv4Header*>(ip_start);
        if (ip_hdr->protocol != 17) continue;

        uint8_t ihl = (ip_hdr->version_ihl & 0x0F) * 4;
        if (remaining_len < ihl) continue;

        const uint8_t* udp_start = ip_start + ihl;
        remaining_len -= ihl;

        if (remaining_len < sizeof(UdpHeader)) continue;
        const auto* udp_hdr = reinterpret_cast<const UdpHeader*>(udp_start);
        
        uint16_t udp_len = ntohs(udp_hdr->length);
        if (udp_len < sizeof(UdpHeader) || remaining_len < udp_len) continue;

        out_packet.payload = udp_start + sizeof(UdpHeader);
        out_packet.length = udp_len - sizeof(UdpHeader);
        
        return true;
    }
    return false;
}
    
}