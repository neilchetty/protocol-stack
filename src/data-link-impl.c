#include "headers/data-link-impl.h"
#include "headers/network-impl.h"
#include "headers/physical-impl.h"
#include "headers/thread-pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

extern bool DEBUG_ENABLED;
extern threadpool thpool;

void handle_physical_to_data_link(void* data) {
    if(data == NULL) {
        fprintf(stderr, "DL Error: Received NULL data pointer from physical layer.\n");
        return;
    }
    unsigned char* raw_data = (unsigned char*)data;
    size_t data_length = SHARED_MEM_SIZE;
    if(DEBUG_ENABLED) printf("DL: Processing %zu bytes received from Physical Layer...\n", data_length);
    bool is_in_frame = false;
    unsigned char frame_buffer[MAX_FRAME_CONTENT_SIZE];
    size_t buffer_index = 0;
    bool next_byte_is_escaped = false;
    for(size_t i = 0; i < data_length; i++) {
        unsigned char current_byte = raw_data[i];
        if(next_byte_is_escaped) {
            unsigned char unescaped_byte;
            if(current_byte == (ESC_BYTE ^ XOR_BYTE)) unescaped_byte = ESC_BYTE;
            else if(current_byte == (FLAG_BYTE ^ XOR_BYTE)) unescaped_byte = FLAG_BYTE;
            else {
                if(DEBUG_ENABLED) fprintf(stderr, "DL Error: Invalid byte 0x%02X after ESC. Discarding frame.\n", current_byte);
                is_in_frame = false;
                next_byte_is_escaped = false;
                continue;
            }
            if(buffer_index < MAX_FRAME_CONTENT_SIZE) frame_buffer[buffer_index++] = unescaped_byte;
            else {
                if(DEBUG_ENABLED) fprintf(stderr, "DL Error: Frame buffer overflow during destuffing. Discarding frame.\n");
                is_in_frame = false;
            }
            next_byte_is_escaped = false;
        }
        else if(current_byte == FLAG_BYTE) {
            if(is_in_frame) {
                if(DEBUG_ENABLED) printf("DL: End flag found. Buffer index: %zu.\n", buffer_index);
                if(buffer_index >= (PROTOCOL_SIZE + CHECKSUM_SIZE)) {
                    uint8_t received_checksum = frame_buffer[buffer_index - 1];
                    uint16_t checksum_calc = 0;
                    for(size_t j = 0; j < buffer_index - CHECKSUM_SIZE; j++) checksum_calc += frame_buffer[j];
                    uint8_t calculated_checksum = (uint8_t)(checksum_calc & 0xFF);
                    if(DEBUG_ENABLED) printf("DL: Received Checksum: 0x%02X, Calculated Checksum: 0x%02X\n", received_checksum, calculated_checksum);
                    if(calculated_checksum == received_checksum) {
                        size_t network_payload_size = buffer_index - CHECKSUM_SIZE;
                        unsigned char* network_payload = (unsigned char*)malloc(network_payload_size);
                        if(network_payload != NULL) {
                            memcpy(network_payload, frame_buffer, network_payload_size);
                            if(thpool != NULL) {
                                if(thpool_add_work(thpool, (void (*)(void*))handle_data_link_to_network, network_payload) != 0) {
                                    fprintf(stderr, "DL Error: Failed to add task to thread pool for Network Layer.\n");
                                    free(network_payload);
                                }
                                else if(DEBUG_ENABLED) printf("DL: Valid frame (Payload size: %zu) passed to thread pool for NW processing.\n", network_payload_size);
                            }
                            else {
                                fprintf(stderr, "DL Error: Thread pool is NULL when trying to add NW work.\n");
                                free(network_payload);
                            }
                        }
                        else fprintf(stderr, "DL Error: Failed to allocate memory for network payload.\n");
                    }
                    else if(DEBUG_ENABLED) fprintf(stderr, "DL Error: Checksum mismatch. Discarding frame.\n");
                }
                else if(DEBUG_ENABLED) fprintf(stderr, "DL Error: Frame content too short (%zu bytes). Discarding frame.\n", buffer_index);
                is_in_frame = false;
                buffer_index = 0;
                next_byte_is_escaped = false;
            }
            else {
                if(DEBUG_ENABLED) printf("DL: Start flag found at index %zu.\n", i);
                is_in_frame = true;
                buffer_index = 0;
                next_byte_is_escaped = false;
            }
        }
        else if(current_byte == ESC_BYTE) {
            if(is_in_frame) {
                if(DEBUG_ENABLED) printf("DL: Escape byte found at index %zu.\n", i);
                next_byte_is_escaped = true;
            }
            else if(DEBUG_ENABLED) printf("DL: Ignoring ESC byte outside frame at index %zu.\n", i);
        }
        else {
            if(is_in_frame) {
                if(buffer_index < MAX_FRAME_CONTENT_SIZE) frame_buffer[buffer_index++] = current_byte;
                else {
                    if(DEBUG_ENABLED) fprintf(stderr, "DL Error: Frame buffer overflow while receiving data. Discarding frame.\n");
                    is_in_frame = false;
                }
            }
        }
    }
    if(is_in_frame && DEBUG_ENABLED) printf("DL: Processing finished, but frame was incomplete (no end flag found).\n");
    free(raw_data);
    if(DEBUG_ENABLED) printf("DL: Finished processing physical layer data block.\n");
}

int handle_data_link_to_physical(uint16_t protocol, const unsigned char* payload, size_t payload_length) {
    if(payload == NULL && payload_length > 0) {
        fprintf(stderr, "DL Error: Send request with NULL payload but positive length (%zu).\n", payload_length);
        return -1;
    }
    if(payload_length > MAX_INFO_SIZE) {
        fprintf(stderr, "DL Error: Payload length (%zu) exceeds maximum info size (%d).\n", payload_length, MAX_INFO_SIZE);
        return -1;
    }
    if(DEBUG_ENABLED) printf("DL: Preparing to send payload of size %zu with protocol 0x%04X.\n", payload_length, protocol);
    size_t content_length = PROTOCOL_SIZE + payload_length + CHECKSUM_SIZE;
    unsigned char frame_content[MAX_FRAME_CONTENT_SIZE];
    frame_content[0] = (protocol >> 8) & 0xFF; // Big Endian
    frame_content[1] = protocol & 0xFF; // Big Endian
    if(payload_length > 0) memcpy(&frame_content[PROTOCOL_SIZE], payload, payload_length);
    uint16_t checksum_calc = 0;
    for (size_t i = 0; i < PROTOCOL_SIZE + payload_length; ++i) checksum_calc += frame_content[i];
    uint8_t checksum = (uint8_t)(checksum_calc & 0xFF);
    frame_content[content_length - 1] = checksum;
    if(DEBUG_ENABLED) printf("DL: Calculated Checksum: 0x%02X for content length %zu.\n", checksum, content_length);
    unsigned char stuffed_frame[MAX_STUFFED_FRAME_SIZE];
    size_t stuffed_index = 0;
    stuffed_frame[stuffed_index++] = FLAG_BYTE;
    for (size_t i = 0; i < content_length; ++i) {
        unsigned char byte = frame_content[i];
        if(byte == FLAG_BYTE || byte == ESC_BYTE) {
            if(stuffed_index + 1 >= MAX_STUFFED_FRAME_SIZE) {
                fprintf(stderr, "DL Error: Stuffed frame buffer overflow during stuffing.\n");
                return -1;
            }
            stuffed_frame[stuffed_index++] = ESC_BYTE;
            stuffed_frame[stuffed_index++] = byte ^ XOR_BYTE;
        }
        else {
            if(stuffed_index >= MAX_STUFFED_FRAME_SIZE) {
                fprintf(stderr, "DL Error: Stuffed frame buffer overflow.\n");
                return -1;
            }
            stuffed_frame[stuffed_index++] = byte;
        }
    }
    if(stuffed_index >= MAX_STUFFED_FRAME_SIZE) {
        fprintf(stderr, "DL Error: Stuffed frame buffer overflow before adding end flag.\n");
        return -1;
    }
    stuffed_frame[stuffed_index++] = FLAG_BYTE;
    if(DEBUG_ENABLED) {
        printf("DL: Frame content (len %zu) stuffed into final frame (len %zu).\n", content_length, stuffed_index);
        printf("DL: Stuffed Frame Hex: ");
        for(size_t k=0; k < stuffed_index; ++k) printf("%02X ", stuffed_frame[k]);
        printf("\n");
    }
    if(physical_layer_send(stuffed_frame, stuffed_index) != 0) {
        fprintf(stderr, "DL Error: Physical layer send failed.\n");
        return -1;
    }
    if(DEBUG_ENABLED) printf("DL: Frame successfully sent to physical layer.\n");
    return 0;
}