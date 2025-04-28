#ifndef NIC_IMPL_H
#define NIC_IMPL_H

#include "headers/variables.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include "headers/colors.h"

#define SHARED_MEM_SIZE 2048
extern int physical_shm_fd;
extern char physical_sem_name[50];
extern sem_t *physical_sem;
extern void* physical_shm_ptr;
int physical_layer_init();
void physical_layer_shutdown();
int start_physical_receiver_thread();
void* receive_frame_thread(void* param);
int physical_layer_send(const unsigned char* frame_data, size_t frame_length);

#endif
