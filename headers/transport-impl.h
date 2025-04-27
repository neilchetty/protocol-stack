#ifndef TRANSPORT_IMPL_H
#define TRANSPORT_IMPL_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t length;
    uint16_t checksum;
} simple_udp_header_t;
#define UDP_PROTOCOL_NUMBER 17
void handle_network_to_transport(void* network_payload);
int handle_application_to_transport(const unsigned char* app_data, size_t app_data_length, uint16_t src_port, uint16_t dest_port);

#endif
