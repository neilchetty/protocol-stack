#include "headers/application-impl.h"
#include "headers/transport-impl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

extern bool DEBUG_ENABLED;

void handle_transport_to_application(void* transport_payload) {
    if(transport_payload == NULL) {
        fprintf(stderr, "APP Error: Received NULL data pointer from transport layer.\n");
        return;
    }
    unsigned char* data = (unsigned char*)transport_payload;
    size_t data_len = strlen((char*)data);
    if(DEBUG_ENABLED) printf("APP: Received data from transport layer (Size: %zu - assumed string).\n", data_len);
    printf("APP: Received Message: %s\n", (char*)data);
    free(transport_payload);
    if(DEBUG_ENABLED) printf("APP: Finished processing transport layer data.\n");
}

int send_application_data(const char* message, uint16_t src_port, uint16_t dest_port) {
    if(message == NULL) {
        fprintf(stderr, "APP Error: Attempted to send NULL message.\n");
        return -1;
    }
    size_t message_len = strlen(message);
    if(DEBUG_ENABLED) printf("APP: Sending message: \"%s\" (Length: %zu) from Port %u to Port %u\n", message, message_len, src_port, dest_port);
    if(handle_application_to_transport((const unsigned char*)message, message_len, src_port, dest_port) != 0) {
        fprintf(stderr, "APP Error: Transport layer failed to send message.\n");
        return -1;
    }
    if(DEBUG_ENABLED) printf("APP: Message successfully passed to transport layer.\n");
    return 0;
}