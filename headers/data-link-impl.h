#ifndef DATA_LINK_IMPL_H
#define DATA_LINK_IMPL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "headers/colors.h"

#define FLAG_BYTE 0x7E
#define ESC_BYTE  0x7D
#define XOR_BYTE  0x20

#define MAX_INFO_SIZE 1500
#define PROTOCOL_SIZE 2
#define CHECKSUM_SIZE 1
#define MAX_FRAME_CONTENT_SIZE (PROTOCOL_SIZE + MAX_INFO_SIZE + CHECKSUM_SIZE)
#define MAX_STUFFED_FRAME_SIZE ((MAX_FRAME_CONTENT_SIZE * 2) + 2)
void handle_physical_to_data_link(void* data);
int handle_data_link_to_physical(uint16_t protocol, const unsigned char* payload, size_t payload_length);

#endif
