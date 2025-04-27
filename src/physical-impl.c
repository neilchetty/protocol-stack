#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/select.h>
#include "headers/physical-impl.h"
#include "headers/data-link-impl.h"
#include "headers/thread-pool.h"

int physical_shm_fd = -1;
char physical_sem_name[50];
sem_t *physical_sem = SEM_FAILED;
void* physical_shm_ptr = MAP_FAILED;
extern bool DEBUG_ENABLED;
extern char source_mac_address[20];
extern char destination_mac_address[20];
extern threadpool thpool;
pthread_t receiver_tid = 0;
int receiver_pipe[2] = {-1, -1};

int physical_layer_init() {
    if(pipe(receiver_pipe) == -1) {
        perror("PHY Error: pipe failed");
        return -1;
    }
    // Set the read end of the pipe to non-blocking for select check
    int flags = fcntl(receiver_pipe[0], F_GETFL, 0);
    if(flags == -1 || fcntl(receiver_pipe[0], F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("PHY Error: fcntl failed to set pipe non-blocking");
        close(receiver_pipe[0]);
        close(receiver_pipe[1]);
        return -1;
    }
    snprintf(physical_sem_name, sizeof(physical_sem_name), "/sem_%s", source_mac_address);
    sem_unlink(physical_sem_name);
    shm_unlink(source_mac_address);
    if(DEBUG_ENABLED) {
        printf("PHY: Initializing Physical Layer (Listening on %s)...\n", source_mac_address);
        printf("PHY: Shared Memory Name: %s\n", source_mac_address);
        printf("PHY: Semaphore Name: %s\n", physical_sem_name);
    }
    physical_shm_fd = shm_open(source_mac_address, O_CREAT | O_RDWR, 0666);
    if(physical_shm_fd == -1) {
        perror("PHY Error: shm_open failed");
        close(receiver_pipe[0]);
        close(receiver_pipe[1]);
        return -1;
    }
    if(ftruncate(physical_shm_fd, SHARED_MEM_SIZE) == -1) {
        perror("PHY Error: ftruncate failed");
        close(physical_shm_fd);
        shm_unlink(source_mac_address);
        close(receiver_pipe[0]);
        close(receiver_pipe[1]);
        return -1;
    }
    physical_shm_ptr = mmap(NULL, SHARED_MEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, physical_shm_fd, 0);
    if(physical_shm_ptr == MAP_FAILED) {
        perror("PHY Error: mmap failed");
        close(physical_shm_fd);
        shm_unlink(source_mac_address);
        close(receiver_pipe[0]);
        close(receiver_pipe[1]);
        return -1;
    }
    memset(physical_shm_ptr, 0, SHARED_MEM_SIZE);
    physical_sem = sem_open(physical_sem_name, O_CREAT, 0666, 0);
    if(physical_sem == SEM_FAILED) {
        perror("PHY Error: sem_open (creating) failed");
        munmap(physical_shm_ptr, SHARED_MEM_SIZE);
        close(physical_shm_fd);
        shm_unlink(source_mac_address);
        close(receiver_pipe[0]);
        close(receiver_pipe[1]);
        return -1;
    }
    if(DEBUG_ENABLED) printf("PHY: Listening shared memory and semaphore initialized successfully.\n");
    if(start_physical_receiver_thread() != 0) {
        physical_layer_shutdown();
        return -1;
    }
    return 0;
}

void physical_layer_shutdown() {
    if(DEBUG_ENABLED) printf("PHY: Shutting down Physical Layer (Listening on %s)...\n", source_mac_address);
    if(receiver_pipe[1] != -1) {
        char exit_signal = 'X';
        if(write(receiver_pipe[1], &exit_signal, 1) == -1) {
            if(errno != EPIPE) perror("PHY Warning: Failed to write exit signal to pipe");
        }
        close(receiver_pipe[1]);
        receiver_pipe[1] = -1;
    }
    if(receiver_tid != 0) {
        if(pthread_join(receiver_tid, NULL) != 0) perror("PHY Warning: Failed to join receiver thread");
        receiver_tid = 0;
    }
    if(receiver_pipe[0] != -1) {
        close(receiver_pipe[0]);
        receiver_pipe[0] = -1;
    }
    if(physical_shm_ptr != MAP_FAILED) {
        if(munmap(physical_shm_ptr, SHARED_MEM_SIZE) == -1) perror("PHY Warning: munmap failed during shutdown");
        physical_shm_ptr = MAP_FAILED;
    }
    if(physical_shm_fd != -1) {
        if(close(physical_shm_fd) == -1) perror("PHY Warning: close failed during shutdown");
        if(shm_unlink(source_mac_address) == -1 && errno != ENOENT) perror("PHY Warning: shm_unlink failed for source");
        physical_shm_fd = -1;
    }
    if(physical_sem != SEM_FAILED) {
        if(sem_close(physical_sem) == -1) perror("PHY Warning: sem_close failed during shutdown");
        if(sem_unlink(physical_sem_name) == -1 && errno != ENOENT) perror("PHY Warning: sem_unlink failed for source");
        physical_sem = SEM_FAILED;
    }
    if(DEBUG_ENABLED) printf("PHY: Physical Layer shutdown complete.\n");
}

int start_physical_receiver_thread() {
    if(thpool == NULL) {
        fprintf(stderr, "PHY Error: Thread pool not initialized before starting receiver.\n");
        return -1;
    }
    if(pthread_create(&receiver_tid, NULL, receive_frame_thread, NULL) != 0) {
        perror("PHY Error: Failed to create receiver thread");
        return -1;
    }
    if(DEBUG_ENABLED) printf("PHY: Receiver thread started (Listening on %s).\n", source_mac_address);
    return 0;
}

void* receive_frame_thread(void* param) {
    (void)param;
    fd_set read_fds;
    int pipe_read_fd = receiver_pipe[0];
    struct timespec sleep_duration = {0, 100000000L}; // 100 milliseconds
    if(physical_shm_ptr == MAP_FAILED || physical_sem == SEM_FAILED || pipe_read_fd == -1) {
        fprintf(stderr, "PHY Error: Receiver thread started with uninitialized resources.\n");
        return NULL;
    }
    if(DEBUG_ENABLED) printf("PHY: Receiver thread waiting for data on %s (using polling)...\n", source_mac_address);
    while (true) {
        int sem_result = sem_trywait(physical_sem);
        if(sem_result == 0) {
            if(DEBUG_ENABLED) {
                printf("PHY: Receiver (%s) got semaphore. Reading frame...\n", source_mac_address);
                printf("PHY: Shared Mem (%s) start: [", source_mac_address);
                for(int i=0; i<32 && i<SHARED_MEM_SIZE; ++i){
                    char c = ((char*)physical_shm_ptr)[i];
                    if(isprint(c)) printf("%c", c); else printf(".");
                }
                printf("]\n");
            }
            unsigned char* received_data_copy = (unsigned char*)malloc(SHARED_MEM_SIZE);
            if(received_data_copy == NULL) {
                fprintf(stderr, "PHY Error: Failed to allocate memory for received data copy.\n");
                continue;
            }
            memcpy(received_data_copy, physical_shm_ptr, SHARED_MEM_SIZE);
            if(thpool != NULL) {
                if(thpool_add_work(thpool, (void (*)(void*))handle_physical_to_data_link, received_data_copy) != 0) {
                    fprintf(stderr, "PHY Error: Failed to add task to thread pool.\n");
                    free(received_data_copy);
                } else if(DEBUG_ENABLED) printf("PHY: Frame data from %s passed to thread pool.\n", source_mac_address);
            }
            else {
                fprintf(stderr, "PHY Error: Thread pool is NULL when trying to add work.\n");
                free(received_data_copy);
            }
        }
        else if(errno == EAGAIN) nanosleep(&sleep_duration, NULL);
        else if(errno == EINTR && DEBUG_ENABLED) printf("PHY: sem_trywait interrupted, checking pipe...\n");
        else {
            perror("PHY Error: sem_trywait failed");
            break;
        }
        FD_ZERO(&read_fds);
        FD_SET(pipe_read_fd, &read_fds);
        struct timeval tv = {0, 0}; // Timeout of 0 for non-blocking check
        int activity = select(pipe_read_fd + 1, &read_fds, NULL, NULL, &tv);
        if(activity < 0) {
            if(errno != EINTR) {
                perror("PHY Error: select on pipe failed");
                break;
            }
            if(DEBUG_ENABLED) printf("PHY: select on pipe interrupted, looping again...\n");
        }
        else if(activity > 0 && FD_ISSET(pipe_read_fd, &read_fds)) {
            if(DEBUG_ENABLED) printf("PHY: Receiver thread received shutdown signal via pipe.\n");
            break;
        }
    }
    if(DEBUG_ENABLED) printf("PHY: Receiver thread (%s) exiting.\n", source_mac_address);
    return NULL;
}

int physical_layer_send(const unsigned char* frame_data, size_t frame_length) {
    if(destination_mac_address[0] == '\0') {
        fprintf(stderr, "PHY Send Error: Destination MAC address not set.\n");
        return -1;
    }
    if(strcmp(source_mac_address, destination_mac_address) == 0) {
        fprintf(stderr, "PHY Send Error: Attempted to send to self using physical_layer_send. Loopback should occur naturally if needed.\n");
        return -1;
    }
    if(frame_data == NULL) {
        fprintf(stderr, "PHY Send Error: frame_data is NULL for sending to %s.\n", destination_mac_address);
        return -1;
    }
    if(frame_length == 0) fprintf(stderr, "PHY Send Info: Attempted to send zero-length frame to %s. Sending anyway.\n", destination_mac_address);
    if(frame_length > SHARED_MEM_SIZE) {
        fprintf(stderr, "PHY Send Error: Frame length (%zu) exceeds shared memory size (%d) for sending to %s.\n", frame_length, SHARED_MEM_SIZE, destination_mac_address);
        return -1;
    }
    if(DEBUG_ENABLED) printf("PHY: Sending frame of length %zu to %s...\n", frame_length, destination_mac_address);
    char dest_shm_name[50];
    char dest_sem_name[50];
    int dest_shm_fd = -1;
    sem_t* dest_sem = SEM_FAILED;
    void* dest_shm_ptr = MAP_FAILED;
    int result = -1;
    snprintf(dest_shm_name, sizeof(dest_shm_name), "%s", destination_mac_address);
    snprintf(dest_sem_name, sizeof(dest_sem_name), "/sem_%s", destination_mac_address);
    dest_sem = sem_open(dest_sem_name, 0);
    if(dest_sem == SEM_FAILED) {
        if(DEBUG_ENABLED || errno != ENOENT) fprintf(stderr, "PHY Send Info/Error: sem_open ('%s') failed: %s. Is destination '%s' running?\n", dest_sem_name, strerror(errno), dest_shm_name);
        goto cleanup_send;
    }
    dest_shm_fd = shm_open(dest_shm_name, O_RDWR, 0666);
    if(dest_shm_fd == -1) {
        if(DEBUG_ENABLED || errno != ENOENT) fprintf(stderr, "PHY Send Info/Error: shm_open ('%s') failed: %s. Is destination running and initialized?\n", dest_shm_name, strerror(errno));
        goto cleanup_send;
    }
    dest_shm_ptr = mmap(NULL, SHARED_MEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, dest_shm_fd, 0);
    if(dest_shm_ptr == MAP_FAILED) {
        perror("PHY Send Error: mmap failed for destination");
        goto cleanup_send;
    }
    memcpy(dest_shm_ptr, frame_data, frame_length);
    if(DEBUG_ENABLED) printf("PHY: Frame data written to destination shared memory %s.\n", dest_shm_name);
    if(sem_post(dest_sem) == -1) {
        perror("PHY Send Error: sem_post failed for destination");
        goto cleanup_send;
    }
    if(DEBUG_ENABLED) {
        printf("PHY: Destination semaphore %s posted.\n", dest_sem_name);
        printf("PHY: Send to %s successful.\n", destination_mac_address);
    }
    result = 0;
cleanup_send:
    if(dest_shm_ptr != MAP_FAILED && munmap(dest_shm_ptr, SHARED_MEM_SIZE) == -1) perror("PHY Send Warning: munmap for destination failed");
    if(dest_shm_fd != -1 && close(dest_shm_fd) == -1) perror("PHY Send Warning: close for destination shm fd failed");
    if(dest_sem != SEM_FAILED && sem_close(dest_sem) == -1) perror("PHY Send Warning: sem_close for destination failed");
    return result;
}
