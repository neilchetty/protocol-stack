#ifndef NETWORK_IMPL_H
#define NETWORK_IMPL_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment_offset;
    uint8_t protocol;
    uint16_t header_checksum;
    // uint32_t src_ip;
    // uint32_t dest_ip;
} simple_ip_header_t;

#define IP_FLAG_MF 0x2000
#define IP_FLAG_DF 0x4000
#define IP_OFFSET_MASK 0x1FFF
void handle_data_link_to_network(void* dl_payload);
void network_layer_init();
void network_layer_shutdown();
int handle_transport_to_network(const unsigned char* transport_data, size_t transport_data_length, uint8_t protocol_type);

#endif
