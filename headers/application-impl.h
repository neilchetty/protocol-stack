#ifndef APPLICATION_IMPL_H
#define APPLICATION_IMPL_H

#include <stddef.h>
#include <stdint.h>
#include "headers/colors.h"

void handle_transport_to_application(void* transport_payload);
int send_application_data(const char* message, uint16_t src_port, uint16_t dest_port);

#endif
