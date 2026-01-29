#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <signal.h>

#include "Parser/parse_env.h"
#include "Parser/parse_emergency_types.h"
#include "Parser/parse_rescuers.h"
#include "src/runtime/status.h"
#include "mq_consumer.h"
#include "logging.h"


#define QUEUE_NAME "/emergenze676878"

void handler_sigusr1(int sig){ ; }

int main(){
    // -----------------------------------
    // Parsing dei file di configurazione
    // -----------------------------------

    // Ambiente
    environment_variable_t env_vars;
    parse_environment_variables("./Data/environment.conf", &env_vars);

    // Tipi di soccorritori e loro digital twin
    rescuer_type_t* rescuer_types = NULL;
    rescuer_digital_twin_t* rescuer_twins = NULL;
    size_t dt_count = parse_rescuer_type("./Data/rescuers.conf", &rescuer_types, &rescuer_twins);

    // Tipi di emergenze
    emergency_type_t* emergency_types = NULL;
    size_t em_count = parse_emergency_type("./Data/emergency.conf", &emergency_types, rescuer_types); 

    // ------------------------------------------------------
    // Inizializzazione dello stato dell'applicazione
    // ------------------------------------------------------

    state_t state;
    if(status_init(&state, rescuer_twins, dt_count) != 0){
        LOG_SYSTEM("main", "Errore nell'inizializzazione dello stato dell'applicazione");
        goto cleanup;
    }

    // --------------------------------------------
    // Inizializzazione della message queue
    // --------------------------------------------
    mq_consumer_t consumer;
    
    consumer.state = &state;

    if(start_mq(&consumer, &env_vars, emergency_types, em_count) != 0){
        LOG_SYSTEM("main", "Errore nell'inizializzazione della message queue");
        goto cleanup;
    }

    if(status_start_worker_threads(&state, MAX_WORKER_THREADS) != 0){
        LOG_SYSTEM("main", "Errore nell'avvio dei worker threads");
        goto cleanup;
    }

    signal(SIGUSR1, handler_sigusr1);

    while(!*(state.shutdown_flag)){
        pause(); // Attende un segnale per terminare
    }


    // ------------------------------------------------------
    // Shutdown dell'applicazione e pulizia della memoria
    // ------------------------------------------------------
cleanup:
    LOG_SYSTEM("main", "Inizio shutdown dell'applicazione");
    shutdown_mq(&consumer);
    status_request_shutdown(&state);
    status_join_worker_threads(&state);
    status_destroy(&state, &consumer);
    free(env_vars.queue);
    free(rescuer_types);
    free(rescuer_twins);
    free(emergency_types);
    LOG_SYSTEM("main", "Applicazione terminata con successo");
    LOG_SYSTEM("main", "Emergenze risolte: %d", state.emergencies_solved);
    LOG_SYSTEM("main", "Emergenze non risolte: %d", state.emergencies_not_solved);
    return 0;
}