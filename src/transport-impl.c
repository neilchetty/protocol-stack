#include "headers/transport-impl.h"
#include "headers/application-impl.h"
#include "headers/network-impl.h"
#include "headers/thread-pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

extern bool DEBUG_ENABLED;
extern threadpool thpool;

void handle_network_to_transport(void* network_payload) {
    if(network_payload == NULL) {
        fprintf(stderr, ANSI_COLOR_RESET COLOR_ERR "TRANSPORT Error: Received NULL data pointer from network layer.\n");
        return;
    }
    unsigned char* udp_segment = (unsigned char*)network_payload;
    if(true) {
        simple_udp_header_t* udp_header = (simple_udp_header_t*)udp_segment;
        uint16_t src_port = udp_header->src_port;
        uint16_t dest_port = udp_header->dest_port;
        uint16_t udp_length = udp_header->length;
        uint16_t checksum = udp_header->checksum;
        size_t header_size = sizeof(simple_udp_header_t);
        if(DEBUG_ENABLED) printf(ANSI_COLOR_RESET ANSI_COLOR_BRIGHT_CYAN "TRANSPORT: Received UDP segment. Src Port: %u, Dest Port: %u, Length Field: %u\n", src_port, dest_port, udp_length);
        if(udp_length < header_size) {
            fprintf(stderr, ANSI_COLOR_RESET COLOR_ERR "TRANSPORT Error: UDP header length (%u) < UDP header struct size (%zu). Discarding.\n", udp_length, header_size);
            free(network_payload);
            return;
        }
        if(checksum != 0) { }
        size_t app_payload_size = udp_length - header_size;
        unsigned char* app_payload = (unsigned char*)malloc(app_payload_size);
        if(app_payload) {
            memcpy(app_payload, udp_segment + header_size, app_payload_size);
            if(thpool != NULL) {
                if(thpool_add_work(thpool, (void (*)(void*))handle_transport_to_application, app_payload) != 0) {
                    fprintf(stderr, ANSI_COLOR_RESET COLOR_ERR "TRANSPORT Error: Failed to add task to thread pool for Application Layer.\n");
                    free(app_payload);
                }
                else if(DEBUG_ENABLED) printf(ANSI_COLOR_RESET ANSI_COLOR_BRIGHT_CYAN "TRANSPORT: UDP Payload (Size: %zu) passed to thread pool for APP processing.\n", app_payload_size);
            }
            else {
                fprintf(stderr, ANSI_COLOR_RESET COLOR_ERR "TRANSPORT Error: Thread pool is NULL when trying to add APP work.\n");
                free(app_payload);
            }
        }
        else fprintf(stderr, ANSI_COLOR_RESET COLOR_ERR "TRANSPORT Error: Failed to allocate memory for application payload.\n");
    }
    else if(DEBUG_ENABLED) fprintf(stderr, ANSI_COLOR_RESET COLOR_ERR "TRANSPORT Error: Received data block too small for UDP header.\n");
    free(network_payload);
}

int handle_application_to_transport(const unsigned char* app_data, size_t app_data_length, uint16_t src_port, uint16_t dest_port) {
    if(app_data == NULL && app_data_length > 0) {
        fprintf(stderr, ANSI_COLOR_RESET COLOR_ERR "TRANSPORT Error: Send request from application with NULL data but positive length (%zu).\n", app_data_length);
        return -1;
    }
    size_t udp_header_size = sizeof(simple_udp_header_t);
    size_t udp_segment_length = udp_header_size + app_data_length;
    if(DEBUG_ENABLED) printf(ANSI_COLOR_RESET ANSI_COLOR_BRIGHT_CYAN "TRANSPORT: Sending %zu bytes of app data from Port %u to Port %u.\n", app_data_length, src_port, dest_port);
    unsigned char* udp_segment = (unsigned char*)malloc(udp_segment_length);
    if(!udp_segment) {
        fprintf(stderr, ANSI_COLOR_RESET COLOR_ERR "TRANSPORT Error: Failed to allocate memory for UDP segment.\n");
        return -1;
    }
    simple_udp_header_t* udp_header = (simple_udp_header_t*)udp_segment;
    udp_header->src_port = src_port;
    udp_header->dest_port = dest_port;
    udp_header->length = udp_segment_length;
    udp_header->checksum = 0;
    memcpy(udp_segment + udp_header_size, app_data, app_data_length);
    if(DEBUG_ENABLED) printf(ANSI_COLOR_RESET ANSI_COLOR_BRIGHT_CYAN "TRANSPORT: UDP Segment created (Total Length: %zu).\n", udp_segment_length);
    if(handle_transport_to_network(udp_segment, udp_segment_length, UDP_PROTOCOL_NUMBER) != 0) {
        fprintf(stderr, ANSI_COLOR_RESET COLOR_ERR "TRANSPORT Error: Network layer failed to send UDP segment.\n");
        free(udp_segment);
        return -1;
    }
    free(udp_segment);
    if(DEBUG_ENABLED) printf(ANSI_COLOR_RESET ANSI_COLOR_BRIGHT_CYAN "TRANSPORT: UDP Segment successfully sent to network layer.\n");
    return 0;
}