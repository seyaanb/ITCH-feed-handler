#!/usr/bin/env python3
import struct
import sys
import os

def create_synthetic_itch_pcap(filename: str, total_messages: int = 1_000_000):
    os.makedirs(os.path.dirname(filename), exist_ok=True)
    
    with open(filename, "wb") as f:
        f.write(struct.pack("<IHHiIII", 0xa1b2c3d4, 2, 4, 0, 0, 65535, 1))

        msgs_per_packet = 10
        total_packets = total_messages // msgs_per_packet
        seq_num = 1

        for pkt_idx in range(total_packets):
            payload = bytearray()
            
            payload.extend(struct.pack(">10sQH", b"NASDAQ0001", seq_num, msgs_per_packet))
            seq_num += msgs_per_packet

            for msg_idx in range(msgs_per_packet):
                cycle = (pkt_idx * msgs_per_packet + msg_idx) % 3
                ts_nanos = 34200000000000 + (pkt_idx * msgs_per_packet + msg_idx) * 1000
                ts_bytes = ts_nanos.to_bytes(6, byteorder="big")

                if cycle == 0:
                    # 'A' - Add Order: 36 bytes payload + 2 bytes length = 38 bytes
                    msg_body = struct.pack(
                        ">cHH6sQcI8sI",
                        b'A', 1, 0, ts_bytes,
                        1000000 + msg_idx,
                        b'B',
                        100,
                        b"AAPL    ",
                        1855000
                    )
                elif cycle == 1:
                    # 'E' - Order Executed: 31 bytes payload + 2 bytes length = 33 bytes
                    msg_body = struct.pack(
                        ">cHH6sQI8s",
                        b'E', 1, 0, ts_bytes,
                        1000000 + msg_idx,
                        50,
                        b"MATCH123"
                    )
                else:
                    # 'X' - Order Cancel: 23 bytes payload + 2 bytes length = 25 bytes
                    msg_body = struct.pack(
                        ">cHH6sQI",
                        b'X', 1, 0, ts_bytes,
                        1000000 + msg_idx,
                        50
                    )

                payload.extend(struct.pack(">H", len(msg_body)))
                payload.extend(msg_body)

            udp_len = 8 + len(payload)
            udp_hdr = struct.pack(">HHHH", 9876, 9876, udp_len, 0)

            ip_tot_len = 20 + udp_len
            ip_hdr = struct.pack(
                ">BBHHHBBH4s4s",
                0x45, 0, ip_tot_len, 54321, 0, 64, 17, 0,
                bytes([192, 168, 1, 1]), bytes([233, 54, 12, 1])
            )

            eth_hdr = struct.pack(">6s6sH", b"\x00" * 6, b"\x11" * 6, 0x0800)

            packet_bytes = eth_hdr + ip_hdr + udp_hdr + payload

            pcap_pkt_hdr = struct.pack("<IIII", 1700000000, pkt_idx, len(packet_bytes), len(packet_bytes))

            f.write(pcap_pkt_hdr)
            f.write(packet_bytes)

    print(f"Generated {total_messages} ITCH messages in {filename}")

if __name__ == "__main__":
    create_synthetic_itch_pcap("data/sample.pcap", 1_000_000)