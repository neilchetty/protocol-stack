#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "headers/variables.h"
#include "headers/thread-pool.h"
#include "headers/physical-impl.h"
#include "headers/network-impl.h"
#include "headers/application-impl.h"

bool DEBUG_ENABLED = true;
char source_mac_address[20];
char destination_mac_address[20];
threadpool thpool = NULL;
volatile sig_atomic_t shutdown_flag = 0;

void handle_sigint(int sig) {
    (void)sig;
    printf("\nMAIN: SIGINT received, initiating shutdown...\n");
    shutdown_flag = 1;
}

int main(int argc, char *argv[]) {
    if(argc != 3) {
        fprintf(stderr, "Usage: %s <source_mac> <destination_mac>\n", argv[0]);
        fprintf(stderr, "  <source_mac>      : Identifier for this instance's shared memory.\n");
        fprintf(stderr, "  <destination_mac> : Identifier of the instance to send messages to.\n");
        return 1;
    }
    strncpy(source_mac_address, argv[1], sizeof(source_mac_address) - 1);
    source_mac_address[sizeof(source_mac_address) - 1] = '\0';
    strncpy(destination_mac_address, argv[2], sizeof(destination_mac_address) - 1);
    destination_mac_address[sizeof(destination_mac_address) - 1] = '\0';
    printf("MAIN: Source MAC (Listening ID): %s\n", source_mac_address);
    printf("MAIN: Destination MAC (Sending Target ID): %s\n", destination_mac_address);
    signal(SIGINT, handle_sigint);
    int num_threads = 4;
    thpool = thpool_init(num_threads);
    if(thpool == NULL) {
        fprintf(stderr, "MAIN Error: Failed to initialize thread pool.\n");
        return 1;
    }
    printf("MAIN: Thread pool initialized with %d threads.\n", num_threads);
    network_layer_init();
    if(physical_layer_init() != 0) {
        fprintf(stderr, "MAIN Error: Failed to initialize physical layer.\n");
        thpool_destroy(thpool);
        network_layer_shutdown();
        return 1;
    }
    printf("MAIN: Physical layer initialized and receiver started.\n");
    printf("MAIN: Setup complete. Ready to send/receive.\n");
    printf("MAIN: Press Ctrl+C to exit gracefully.\n");
    int message_count = 0;
    while (!shutdown_flag) {
        sleep(10);
        if(shutdown_flag) break;
        message_count++;
        char message_buffer[100];
        snprintf(message_buffer, sizeof(message_buffer), "Message %d from %s to %s!",
                message_count, source_mac_address, destination_mac_address);
        const char* message_to_send = message_buffer;
        uint16_t source_port = 12345;
        uint16_t destination_port = 54321;
        printf("\nMAIN: Attempting to send application message (%d)...\n", message_count);
        if(send_application_data(message_to_send, source_port, destination_port) != 0) fprintf(stderr, "MAIN Error: Failed attempt to send application message (%d).\n", message_count);
    }
    printf("MAIN: Shutting down...\n");
    physical_layer_shutdown();
    network_layer_shutdown();
    if(thpool) {
        thpool_destroy(thpool);
        printf("MAIN: Thread pool destroyed.\n");
    }
    printf("MAIN: Shutdown complete.\n");
    return 0;
}
