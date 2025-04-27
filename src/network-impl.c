#include "headers/network-impl.h"
#include "headers/transport-impl.h"
#include "headers/data-link-impl.h"
#include "headers/thread-pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>

extern bool DEBUG_ENABLED;
extern threadpool thpool;

static uint16_t calculate_internet_checksum(const void* buffer, size_t len) {
    const uint16_t* buf = (const uint16_t*)buffer;
    uint32_t sum = 0;
    while (len > 1) {
        sum += *buf++;
        len -= 2;
    }
    if(len == 1) {
        sum += *(const uint8_t*)buf;
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

typedef struct {
    uint16_t id;
    size_t total_payload_size;
    size_t received_size;
    unsigned char* buffer;
    time_t last_fragment_time;
    bool in_use;
    uint8_t protocol;
} reassembly_buffer_t;

#define REASSEMBLY_TIMEOUT 30
static reassembly_buffer_t current_reassembly = { .in_use = false };
static uint16_t next_packet_id = 0;

void network_layer_init() {
    if(DEBUG_ENABLED) printf("NW: Initializing Network Layer...\n");
    srand(time(NULL));
    next_packet_id = rand() % 65535;
    current_reassembly.in_use = false;
    current_reassembly.buffer = NULL;
    if(DEBUG_ENABLED) printf("NW: Network Layer Initialized.\n");
}

void network_layer_shutdown() {
    if(DEBUG_ENABLED) printf("NW: Shutting down Network Layer...\n");
    if(current_reassembly.in_use) {
        free(current_reassembly.buffer);
        current_reassembly.in_use = false;
        if(DEBUG_ENABLED) printf("NW: Cleared active reassembly buffer.\n");
    }
    if(DEBUG_ENABLED) printf("NW: Network Layer Shutdown complete.\n");
}

void clear_reassembly_buffer() {
    if(current_reassembly.in_use) {
        free(current_reassembly.buffer);
        current_reassembly.buffer = NULL;
        current_reassembly.in_use = false;
        if(DEBUG_ENABLED) printf("NW: Reassembly buffer cleared.\n");
    }
}

void handle_data_link_to_network(void* dl_payload) {
    if(dl_payload == NULL) {
        fprintf(stderr, "NW Error: Received NULL data pointer from data link layer.\n");
        return;
    }
    unsigned char* data = (unsigned char*)dl_payload;
    size_t header_size = sizeof(simple_ip_header_t);
    unsigned char* network_pdu = data + PROTOCOL_SIZE;
    simple_ip_header_t* ip_header = (simple_ip_header_t*)network_pdu;
    uint16_t received_checksum = ip_header->header_checksum;
    ip_header->header_checksum = 0;
    uint16_t calculated_checksum = calculate_internet_checksum(ip_header, header_size);
    ip_header->header_checksum = received_checksum;
    if(calculated_checksum != received_checksum) {
        if(DEBUG_ENABLED) fprintf(stderr, "NW Error: IP Header Checksum mismatch! Received=0x%04X, Calculated=0x%04X. Discarding fragment.\n", received_checksum, calculated_checksum);
        free(dl_payload);
        return;
    }
    if(DEBUG_ENABLED) printf("NW: IP Header Checksum OK (0x%04X).\n", received_checksum);
    size_t fragment_total_length_from_header = ip_header->total_length;
    size_t fragment_payload_size;
    if(fragment_total_length_from_header >= header_size) fragment_payload_size = fragment_total_length_from_header - header_size;
    else {
        if(DEBUG_ENABLED) fprintf(stderr, "NW Error: Fragment IP header total_length (%zu) < header size (%zu). Discarding fragment.\n", fragment_total_length_from_header, header_size);
        free(dl_payload);
        return;
    }
    uint16_t identification = ip_header->identification;
    uint16_t flags_offset = ip_header->flags_fragment_offset;
    uint16_t fragment_offset_bytes = (flags_offset & IP_OFFSET_MASK) * 8;
    bool more_fragments = (flags_offset & IP_FLAG_MF) != 0;
    uint8_t ip_protocol = ip_header->protocol;
    unsigned char* fragment_data = network_pdu + header_size;
    if(DEBUG_ENABLED) printf("NW: Processing fragment. ID: %u, Offset: %u bytes, MF: %s, Proto: %d, FragPayloadSize: %zu\n", identification, fragment_offset_bytes, more_fragments ? "Yes" : "No", ip_protocol, fragment_payload_size);
    if(current_reassembly.in_use && (time(NULL) - current_reassembly.last_fragment_time > REASSEMBLY_TIMEOUT)) {
        if(DEBUG_ENABLED) printf("NW: Reassembly timeout for ID %u. Discarding.\n", current_reassembly.id);
        clear_reassembly_buffer();
    }
    if(fragment_offset_bytes == 0) {
        if(current_reassembly.in_use) {
            if(current_reassembly.id != identification && DEBUG_ENABLED) printf("NW: Received new first fragment ID %u while assembling ID %u. Discarding old.\n", identification, current_reassembly.id);
            else if(current_reassembly.id == identification && DEBUG_ENABLED) printf("NW Warning: Received duplicate first fragment for ID %u. Resetting assembly.\n", identification);
            clear_reassembly_buffer();
        }
        if(DEBUG_ENABLED) printf("NW: First fragment for ID %u detected.\n", identification);
        if(!more_fragments) current_reassembly.total_payload_size = fragment_payload_size;
        else {
            if(DEBUG_ENABLED) printf("NW Error: First fragment has MF=1, simple reassembly logic cannot handle fragmentation. Discarding.\n");
            free(dl_payload);
            return;
        }
        if(current_reassembly.total_payload_size > 66000) {
            fprintf(stderr, "NW Error: Unrealistic total payload size %zu estimated from first fragment.\n", current_reassembly.total_payload_size);
            free(dl_payload);
            return;
        }
        if(current_reassembly.total_payload_size == 0) current_reassembly.buffer = NULL;
        else {
            current_reassembly.buffer = (unsigned char*)malloc(current_reassembly.total_payload_size);
            if(current_reassembly.buffer == NULL) {
                fprintf(stderr, "NW Error: Failed to allocate reassembly buffer (size %zu).\n", current_reassembly.total_payload_size);
                free(dl_payload);
                return;
            }
            memset(current_reassembly.buffer, 0, current_reassembly.total_payload_size);
        }
        current_reassembly.id = identification;
        current_reassembly.protocol = ip_protocol;
        current_reassembly.received_size = 0;
        current_reassembly.last_fragment_time = time(NULL);
        current_reassembly.in_use = true;
        if(fragment_payload_size > 0) {
            if(current_reassembly.buffer != NULL && fragment_payload_size <= current_reassembly.total_payload_size) {
                memcpy(current_reassembly.buffer + fragment_offset_bytes, fragment_data, fragment_payload_size);
                current_reassembly.received_size += fragment_payload_size;
                if(DEBUG_ENABLED) printf("NW: Copied first fragment data. Received: %zu / %zu\n", current_reassembly.received_size, current_reassembly.total_payload_size);
            }
            else {
                if(DEBUG_ENABLED) fprintf(stderr, "NW Error: Buffer/size mismatch when copying first fragment data (%p, %zu <= %zu).\n", current_reassembly.buffer, fragment_payload_size, current_reassembly.total_payload_size);
                clear_reassembly_buffer();
                free(dl_payload);
                return;
            }
        }
        else {
                current_reassembly.received_size = 0;
                if(DEBUG_ENABLED) printf("NW: First fragment has 0 payload size. Received: %zu / %zu\n", current_reassembly.received_size, current_reassembly.total_payload_size);
        }
    }
    else {
        if(DEBUG_ENABLED) printf("NW Error: Received subsequent fragment (Offset > 0), but simple reassembly logic cannot handle fragmentation. Discarding fragment ID %u.\n", identification);
        free(dl_payload);
        return;
    }
    if(current_reassembly.in_use && current_reassembly.id == identification) {
        if(!more_fragments && current_reassembly.received_size >= current_reassembly.total_payload_size) {
            if(DEBUG_ENABLED) printf("NW: Reassembly complete for non-fragmented packet ID %u. Total Payload Size: %zu\n", identification, current_reassembly.total_payload_size);
            unsigned char* transport_payload = NULL;
            if(current_reassembly.total_payload_size > 0) {
                transport_payload = (unsigned char*)malloc(current_reassembly.total_payload_size);
                if(transport_payload) memcpy(transport_payload, current_reassembly.buffer, current_reassembly.total_payload_size);
                else {
                    fprintf(stderr, "NW Error: Failed to allocate memory for final transport payload.\n");
                    clear_reassembly_buffer();
                    free(dl_payload);
                    return;
                }
            }
            else {
                transport_payload = NULL;
                if(DEBUG_ENABLED) printf("NW: Reassembled datagram has 0 payload size.\n");
            }
            if(thpool != NULL) {
                if(thpool_add_work(thpool, (void (*)(void*))handle_network_to_transport, transport_payload) != 0) {
                    fprintf(stderr, "NW Error: Failed to add task to thread pool for Transport Layer.\n");
                    free(transport_payload);
                } else if(DEBUG_ENABLED) printf("NW: Reassembled datagram payload (size %zu) passed to thread pool for TP processing.\n", current_reassembly.total_payload_size);
            }
            else {
                fprintf(stderr, "NW Error: Thread pool is NULL when trying to add TP work.\n");
                free(transport_payload);
            }
            clear_reassembly_buffer();
        }
    }
    free(dl_payload);
}

int handle_transport_to_network(const unsigned char* transport_data, size_t transport_data_length, uint8_t protocol_type) {
    if(transport_data == NULL && transport_data_length > 0) {
        fprintf(stderr, "NW Error: Send request from transport with NULL data but positive length (%zu).\n", transport_data_length);
        return -1;
    }
    if(DEBUG_ENABLED) printf("NW: Received %zu bytes from Transport layer (Proto: %d) for sending.\n", transport_data_length, protocol_type);
    size_t ip_header_size = sizeof(simple_ip_header_t);
    size_t max_payload_per_fragment = MAX_INFO_SIZE - ip_header_size;
    if(max_payload_per_fragment % 8 != 0 && max_payload_per_fragment >= 8) max_payload_per_fragment -= (max_payload_per_fragment % 8);
    else if(max_payload_per_fragment < 8) {
        if(transport_data_length > 0) {
            fprintf(stderr, "NW Error: Data Link MTU too small to fit any payload fragment (max_payload_per_fragment=%zu).\n", max_payload_per_fragment);
            return -1;
        }
        max_payload_per_fragment = 0;
    }
    if(max_payload_per_fragment == 0 && transport_data_length > 0) {
        fprintf(stderr, "NW Error: Network header size >= Data Link MTU. Cannot fragment or send payload.\n");
        return -1;
    }
    uint16_t current_packet_id = next_packet_id++;
    bool needs_fragmentation = (transport_data_length > max_payload_per_fragment);
    if(transport_data_length == 0) needs_fragmentation = false;
    if(DEBUG_ENABLED) printf("NW: Sending Packet ID: %u. Needs Fragmentation: %s. Max payload/frag: %zu\n", current_packet_id, needs_fragmentation ? "Yes" : "No", max_payload_per_fragment);
    size_t bytes_sent = 0;
    uint16_t fragment_offset_units = 0;
    do {
        size_t current_payload_size = transport_data_length - bytes_sent;
        if(current_payload_size > max_payload_per_fragment) current_payload_size = max_payload_per_fragment;
        bool is_last_fragment = (bytes_sent + current_payload_size == transport_data_length);
        size_t fragment_total_size = ip_header_size + current_payload_size;
        unsigned char* fragment_buffer = (unsigned char*)malloc(fragment_total_size);
        if(!fragment_buffer) {
            fprintf(stderr, "NW Error: Failed to allocate memory for fragment buffer.\n");
            return -1;
        }
        simple_ip_header_t* ip_header = (simple_ip_header_t*)fragment_buffer;
        ip_header->total_length = fragment_total_size;
        ip_header->identification = current_packet_id;
        ip_header->protocol = protocol_type;
        uint16_t flags_offset_field = fragment_offset_units;
        if(needs_fragmentation && !is_last_fragment) flags_offset_field |= IP_FLAG_MF;
        ip_header->flags_fragment_offset = flags_offset_field;
        ip_header->header_checksum = 0;
        ip_header->header_checksum = calculate_internet_checksum(ip_header, ip_header_size);
        if(current_payload_size > 0) memcpy(fragment_buffer + ip_header_size, transport_data + bytes_sent, current_payload_size);
        if(DEBUG_ENABLED) printf("NW: Sending Fragment: ID=%u, Offset=%u (bytes), Hdr+Payload Size=%zu, MF=%s, Checksum=0x%04X\n", current_packet_id, fragment_offset_units * 8, fragment_total_size, (flags_offset_field & IP_FLAG_MF) ? "Yes" : "No", ip_header->header_checksum);
        uint16_t dl_protocol = 0x0800;
        if(handle_data_link_to_physical(dl_protocol, fragment_buffer, fragment_total_size) != 0) {
            fprintf(stderr, "NW Error: Data link layer failed to send fragment.\n");
            free(fragment_buffer);
            return -1;
        }
        free(fragment_buffer);
        bytes_sent += current_payload_size;
        if(current_payload_size > 0) fragment_offset_units += (current_payload_size / 8);
    } while (bytes_sent < transport_data_length);
    if(DEBUG_ENABLED) printf("NW: Finished sending all fragments for Packet ID %u.\n", current_packet_id);
    return 0;
}
