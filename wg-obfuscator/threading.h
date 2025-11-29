#ifndef _THREADING_H_
#define _THREADING_H_

#include <stdint.h>
#include <pthread.h>
#include "wg-obfuscator.h"

#define QUEUE_SIZE 512
#define MAX_WORKER_THREADS 16

typedef enum {
    THREAD_MODE_SINGLE = 0,
    THREAD_MODE_DUAL = 1,
    THREAD_MODE_MULTI = 2
} thread_mode_t;

typedef struct {
    uint8_t buffer[BUFFER_SIZE];
    int length;
    struct sockaddr_in addr;
    socklen_t addr_len;
    int is_from_client;
    client_entry_t *client;
} packet_job_t;

typedef struct {
    packet_job_t jobs[QUEUE_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile int shutdown;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} packet_queue_t;

typedef struct {
    pthread_t thread_id;
    int worker_index;
    packet_queue_t *queue;
    int listen_sock;
    obfuscator_config_t *config;
    char *xor_key;
    int key_length;
    struct sockaddr_in *forward_addr;
    volatile int running;
} worker_thread_t;

typedef struct {
    thread_mode_t mode;
    int num_cores;
    int num_workers;
    worker_thread_t workers[MAX_WORKER_THREADS];
    packet_queue_t queue;
    pthread_t rx_thread;
    volatile int running;
} threading_context_t;

int detect_cpu_cores(void);
int threading_init(threading_context_t *ctx, obfuscator_config_t *config);
int threading_start(threading_context_t *ctx, int listen_sock, obfuscator_config_t *config,
                    char *xor_key, int key_length, struct sockaddr_in *forward_addr);
void threading_shutdown(threading_context_t *ctx);
int queue_push(packet_queue_t *queue, packet_job_t *job);
int queue_pop(packet_queue_t *queue, packet_job_t *job);

#endif
