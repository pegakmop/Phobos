#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include "threading.h"
#include "wg-obfuscator.h"
#include "obfuscation.h"
#include "masking.h"

extern client_entry_t *conn_table;

int detect_cpu_cores(void) {
    #ifdef _SC_NPROCESSORS_ONLN
        long cores = sysconf(_SC_NPROCESSORS_ONLN);
        if (cores > 0) {
            return (int)cores;
        }
    #endif
    return 1;
}

static void queue_init(packet_queue_t *queue) {
    memset(queue, 0, sizeof(packet_queue_t));
    queue->head = 0;
    queue->tail = 0;
    queue->shutdown = 0;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);
}

static void queue_destroy(packet_queue_t *queue) {
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
}

int queue_push(packet_queue_t *queue, packet_job_t *job) {
    uint32_t current_head = __sync_fetch_and_add(&queue->head, 0);
    uint32_t next_head = (current_head + 1) % QUEUE_SIZE;

    if (next_head == __sync_fetch_and_add(&queue->tail, 0)) {
        return -1;
    }

    packet_job_t *slot = &queue->jobs[current_head];
    slot->length = job->length;
    slot->addr = job->addr;
    slot->addr_len = job->addr_len;
    slot->is_from_client = job->is_from_client;
    slot->client = job->client;
    memcpy(slot->buffer, job->buffer, job->length);

    __sync_synchronize();
    __sync_lock_test_and_set(&queue->head, next_head);

    return 0;
}

int queue_pop(packet_queue_t *queue, packet_job_t *job) {
    int spin_count = 0;

    while (1) {
        uint32_t current_tail = __sync_fetch_and_add(&queue->tail, 0);
        uint32_t current_head = __sync_fetch_and_add(&queue->head, 0);

        if (queue->shutdown && current_head == current_tail) {
            return -1;
        }

        if (current_head != current_tail) {
            packet_job_t *slot = &queue->jobs[current_tail];
            job->length = slot->length;
            job->addr = slot->addr;
            job->addr_len = slot->addr_len;
            job->is_from_client = slot->is_from_client;
            job->client = slot->client;
            memcpy(job->buffer, slot->buffer, slot->length);

            __sync_synchronize();
            __sync_lock_test_and_set(&queue->tail, (current_tail + 1) % QUEUE_SIZE);

            return 0;
        }

        spin_count++;
        if (spin_count > 100) {
            usleep(1);
            spin_count = 0;
        }
    }
}

static void process_packet_from_client(packet_job_t *job, obfuscator_config_t *config,
                                       char *xor_key, int key_length, int listen_sock,
                                       struct sockaddr_in *forward_addr) {
    uint8_t *buffer = job->buffer;
    int length = job->length;
    struct sockaddr_in *sender_addr = &job->addr;

    struct timespec now_ts;
    clock_gettime(CLOCK_MONOTONIC, &now_ts);
    long now = now_ts.tv_sec * 1000 + now_ts.tv_nsec / 1000000;

    client_entry_t *client_entry = find_client_safe(sender_addr);

    uint8_t obfuscated = length >= 4 && is_obfuscated(buffer);
    masking_handler_t *masking_handler = config->masking_handler;

    if (obfuscated) {
        length = masking_unwrap_from_client(buffer, length, config, client_entry,
                                           listen_sock, sender_addr, forward_addr, &masking_handler);
        if (length <= 0) {
            return;
        }
    }

    if (length < 4) {
        return;
    }

    uint8_t version = client_entry ? client_entry->version : OBFUSCATION_VERSION;

    if (obfuscated) {
        int original_length = length;
        length = decode(buffer, length, xor_key, key_length, &version);
        if (length < 4 || length > original_length) {
            return;
        }
    }

    uint32_t packet_type = WG_TYPE(buffer);

    if (packet_type == WG_TYPE_HANDSHAKE) {
        if (!client_entry) {
            client_entry = new_client_entry(config, sender_addr, forward_addr);
            if (!client_entry) {
                return;
            }
            client_entry->last_activity_time = now;
            client_entry->masking_handler = masking_handler;
        }
        if (!obfuscated) {
            masking_on_handshake_req_from_client(config, client_entry, listen_sock, sender_addr, forward_addr);
        }
        client_entry->handshake_direction = DIR_CLIENT_TO_SERVER;
        client_entry->last_handshake_request_time = now;
    } else if (packet_type == WG_TYPE_HANDSHAKE_RESP) {
        if (!client_entry) {
            return;
        }
        if (now - client_entry->last_handshake_request_time > HANDSHAKE_TIMEOUT) {
            return;
        }
        if (client_entry->handshake_direction != DIR_SERVER_TO_CLIENT) {
            return;
        }
        client_entry->handshaked = 1;
        client_entry->client_obfuscated = obfuscated;
        client_entry->server_obfuscated = !obfuscated;
        client_entry->last_handshake_time = now;
    } else if (!client_entry || !client_entry->handshaked) {
        return;
    }

    if (version < client_entry->version) {
        client_entry->version = version;
    }

    if (!obfuscated && client_entry) {
        length = encode(buffer, length, xor_key, key_length, client_entry->version,
                       config->max_dummy_length_data);
        if (length < 4) {
            return;
        }
        length = masking_data_wrap_to_server(buffer, length, config, client_entry, listen_sock, forward_addr);
    }

    if (client_entry) {
        send(client_entry->server_sock, buffer, length, 0);
        client_entry->last_activity_time = now;
    }
}

static void process_packet_from_server(packet_job_t *job, obfuscator_config_t *config,
                                       char *xor_key, int key_length, int listen_sock,
                                       struct sockaddr_in *forward_addr) {
    uint8_t *buffer = job->buffer;
    int length = job->length;
    client_entry_t *client_entry = job->client;

    if (!client_entry) {
        return;
    }

    struct timespec now_ts;
    clock_gettime(CLOCK_MONOTONIC, &now_ts);
    long now = now_ts.tv_sec * 1000 + now_ts.tv_nsec / 1000000;

    uint8_t obfuscated = length >= 4 && is_obfuscated(buffer);

    if (obfuscated) {
        length = masking_unwrap_from_server(buffer, length, config, client_entry, listen_sock, forward_addr);
        if (length <= 0) {
            return;
        }
    }

    if (length < 4) {
        return;
    }

    uint8_t version = client_entry->version;

    if (obfuscated) {
        int original_length = length;
        length = decode(buffer, length, xor_key, key_length, &version);
        if (length < 4 || length > original_length) {
            return;
        }
    }

    uint32_t packet_type = WG_TYPE(buffer);

    if (packet_type == WG_TYPE_HANDSHAKE) {
        if (!obfuscated) {
            masking_on_handshake_req_from_server(config, client_entry, listen_sock, &client_entry->client_addr, forward_addr);
        }
        client_entry->handshake_direction = DIR_SERVER_TO_CLIENT;
        client_entry->last_handshake_request_time = now;
    } else if (packet_type == WG_TYPE_HANDSHAKE_RESP) {
        if (now - client_entry->last_handshake_request_time > HANDSHAKE_TIMEOUT) {
            return;
        }
        if (client_entry->handshake_direction != DIR_CLIENT_TO_SERVER) {
            return;
        }
        client_entry->handshaked = 1;
        client_entry->client_obfuscated = !obfuscated;
        client_entry->server_obfuscated = obfuscated;
        client_entry->last_handshake_time = now;
    } else if (!client_entry->handshaked) {
        return;
    }

    if (version < client_entry->version) {
        client_entry->version = version;
    }

    if (!obfuscated) {
        length = encode(buffer, length, xor_key, key_length, client_entry->version,
                       config->max_dummy_length_data);
        if (length < 4) {
            return;
        }
        length = masking_data_wrap_to_client(buffer, length, config, client_entry, listen_sock, forward_addr);
    }

    sendto(listen_sock, buffer, length, 0, (struct sockaddr *)&client_entry->client_addr,
           sizeof(client_entry->client_addr));

    client_entry->last_activity_time = now;
}

static void *worker_thread_func(void *arg) {
    worker_thread_t *worker = (worker_thread_t *)arg;
    packet_job_t job;

    log(LL_DEBUG, "Worker thread #%d started", worker->worker_index);

    while (worker->running) {
        if (queue_pop(worker->queue, &job) < 0) {
            break;
        }

        if (job.is_from_client) {
            process_packet_from_client(&job, worker->config, worker->xor_key, worker->key_length,
                                      worker->listen_sock, worker->forward_addr);
        } else {
            process_packet_from_server(&job, worker->config, worker->xor_key, worker->key_length,
                                      worker->listen_sock, worker->forward_addr);
        }
    }

    log(LL_DEBUG, "Worker thread #%d stopped", worker->worker_index);
    return NULL;
}

int threading_init(threading_context_t *ctx, obfuscator_config_t *config) {
    memset(ctx, 0, sizeof(threading_context_t));

    ctx->num_cores = detect_cpu_cores();
    log(LL_INFO, "Detected %d logical CPU(s)", ctx->num_cores);

    if (ctx->num_cores <= 1) {
        log(LL_INFO, "Using single-threaded mode");
        ctx->mode = THREAD_MODE_SINGLE;
        ctx->num_workers = 0;
    } else if (ctx->num_cores == 2) {
        log(LL_INFO, "Using dual-threaded mode (1 main + 1 worker)");
        ctx->mode = THREAD_MODE_DUAL;
        ctx->num_workers = 1;
    } else {
        int workers;
        if (ctx->num_cores <= 4) {
            workers = 1;
            log(LL_INFO, "Using dual-threaded mode (1 main + 1 worker)");
            ctx->mode = THREAD_MODE_DUAL;
        } else {
            workers = (ctx->num_cores + 1) / 2;
            if (workers > MAX_WORKER_THREADS) {
                workers = MAX_WORKER_THREADS;
            }
            log(LL_INFO, "Using multi-threaded mode (1 main + %d workers)", workers);
            ctx->mode = THREAD_MODE_MULTI;
        }
        ctx->num_workers = workers;
    }

    if (ctx->mode != THREAD_MODE_SINGLE) {
        queue_init(&ctx->queue);
    }

    return 0;
}

int threading_start(threading_context_t *ctx, int listen_sock, obfuscator_config_t *config,
                    char *xor_key, int key_length, struct sockaddr_in *forward_addr) {
    if (ctx->mode == THREAD_MODE_SINGLE) {
        return 0;
    }

    ctx->running = 1;

    for (int i = 0; i < ctx->num_workers; i++) {
        worker_thread_t *worker = &ctx->workers[i];
        worker->worker_index = i;
        worker->queue = &ctx->queue;
        worker->listen_sock = listen_sock;
        worker->config = config;
        worker->xor_key = xor_key;
        worker->key_length = key_length;
        worker->forward_addr = forward_addr;
        worker->running = 1;

        if (pthread_create(&worker->thread_id, NULL, worker_thread_func, worker) != 0) {
            log(LL_ERROR, "Failed to create worker thread #%d: %s", i, strerror(errno));
            return -1;
        }
    }

    log(LL_INFO, "Started %d worker thread(s)", ctx->num_workers);
    return 0;
}

void threading_shutdown(threading_context_t *ctx) {
    if (ctx->mode == THREAD_MODE_SINGLE) {
        return;
    }

    log(LL_INFO, "Shutting down threading system...");

    ctx->running = 0;

    pthread_mutex_lock(&ctx->queue.mutex);
    ctx->queue.shutdown = 1;
    pthread_cond_broadcast(&ctx->queue.cond);
    pthread_mutex_unlock(&ctx->queue.mutex);

    for (int i = 0; i < ctx->num_workers; i++) {
        worker_thread_t *worker = &ctx->workers[i];
        worker->running = 0;

        if (worker->thread_id) {
            pthread_join(worker->thread_id, NULL);
            log(LL_DEBUG, "Worker thread #%d joined", i);
        }
    }

    queue_destroy(&ctx->queue);

    log(LL_INFO, "Threading system shut down");
}
