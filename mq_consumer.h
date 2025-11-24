#pragma once 
#include <mqueue.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>
#include "Types/emergency_requests.h"
#include "Types/emergency_types.h"
#include "Types/emergencies.h"
#include "Types/rescuers.h"



typedef struct mq_consumer_t {
    // Coda 
    mqd_t mq;                                 
    char* mq_name;                            // Nome della coda di messaggi -> /emergenze676878
    size_t message_size;                      
    int running;                              // 1 = in esecuzione, 0 = fermo

    // Thread
    pthread_t consumer_thread;                
    sig_atomic_t* shutdown_flag;              // 0 = running, 1 = shutdown
    bool thread_created;                      // Indica se il thread è stato creato con successo

    // Dati Environment
    int env_width;
    int env_height;

    // Dati emergenze
    emergency_type_t* emergency_types;
    size_t emergency_types_count;

    // Stato generale
    status_t* state;

} mq_consumer_t;

void initialize_mq(mq_consumer_t* consumer);
void start_mq(mq_consumer_t* consumer, environment_variable_t* env_vars, emergency_type_t* emergency_types, size_t emergency_types_count);
void shutdown_mq(mq_consumer_t* consumer);
void stop_mq(mq_consumer_t* consumer);