#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <signal.h>

#include "../../Types/emergency_types.h"
#include "../../Types/rescuers.h"

#define MAX_WORKER_THREADS 16

typedef struct mq_consumer_t mq_consumer_t; 

typedef struct emergency_record_t{
    emergency_t emergency;
    float current_priority;

    rescuer_digital_twin_t* assigned_rescuers;
    size_t assigned_rescuers_count;

    unsigned int total_time_to_manage;
    unsigned int time_remaining;

    unsigned int starting_time;

    unsigned int timeout;
    
    bool preempted;
} emergency_record_t;


typedef struct state_t {
    pthread_mutex_t mutex;
    pthread_cond_t emergency_available_cond;
    pthread_cond_t rescuer_available_cond;
    
    emergency_record_t** emergencies_waiting;
    size_t emergencies_waiting_count;
    size_t emergencies_waiting_capacity;

    emergency_record_t** emergencies_in_progress;
    size_t emergencies_in_progress_count;
    size_t emergencies_in_progress_capacity;

    emergency_record_t** emergencies_paused;
    size_t emergencies_paused_count;
    size_t emergencies_paused_capacity;

    rescuer_digital_twin_t** rescuer_available;
    size_t rescuer_available_count;

    rescuer_digital_twin_t** rescuers_in_use;
    size_t rescuers_in_use_count;

    pthread_t* worker_threads;
    size_t worker_threads_count;

    sig_atomic_t* shutdown_flag; // 0 = running, 1 = shutdown
} state_t;


emergency_t* find_emergency_by_rescuer(rescuer_digital_twin_t* rescuer, emergency_record_t** emergency_array, int length);


int status_init(state_t* state, rescuer_digital_twin_t* rescuer_twins, size_t rescuer_twins_count);

void status_destroy(state_t* state, mq_consumer_t* consumer);

int status_start_worker_threads(state_t* state, size_t worker_threads_count);
void status_request_shutdown(state_t* state);
void status_join_worker_threads(state_t* state);


int status_add_waiting(state_t* state, emergency_request_t* request, emergency_type_t* emergency_types, size_t emergency_types_count);



void* worker_thread(void* arg);
void* timeout_thread(void* arg);