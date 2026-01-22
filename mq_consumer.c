#include "mq_consumer.h"
#include "Parser/parse_env.h"
#include "Parser/parse_emergency_types.h"
#include "Parser/parse_rescuers.h"
#include "src/runtime/status.h"
#include "logging.h"

#include <mqueue.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <string.h>






// --------------------------------------------------------------
// Funzioni di gestione della message queue
// --------------------------------------------------------------


static bool mq_parse_message(mq_consumer_t* consumer, const char* message, emergency_request_t* request) {
    if(!message || !request || !consumer) {
        LOG_SYSTEM("mq_consumer", "Parametri non validi per l'analisi del messaggio");
        return false;
    }
    LOG_SYSTEM("mq_consumer", "Analisi del messaggio: %s", message);

    // Analizza il messaggio e popola la struttura request
    char emergency_name[128];
    int x, y;
    time_t timestamp = time(NULL);
    sscanf(message, "%127s %d %d %ld", emergency_name, &x, &y, &timestamp);

    if(consumer->env_width < x || x < 0) {
        LOG_SYSTEM("mq_consumer", "Coordinate X fuori dall'ambiente: %d", x);
        return false; // ignora il messaggio
    }
    if(consumer->env_height < y || y < 0) {
        LOG_SYSTEM("mq_consumer", "Coordinate Y fuori dall'ambiente: %d", y);
        return false; // ignora il messaggio
    }
    LOG_SYSTEM("mq_consumer", "Messaggio valido: %s %d %d %ld", emergency_name, x, y, timestamp);

    strncpy(request->emergency_name, emergency_name, EMERGENCY_NAME_LENGTH);
    request->x = x;
    request->y = y;
    request->timestamp = timestamp;
    LOG_SYSTEM("mq_consumer", "Richiesta di emergenza creata: %s %d %d %ld", request->emergency_name, request->x, request->y, request->timestamp);
    return true;
}

void* mq_consumer_thread(void* arg) {
    mq_consumer_t* consumer = (mq_consumer_t*)arg;
    if(!consumer) {
        LOG_SYSTEM("mq_consumer", "Argomento thread mq_consumer non valido");
        fprintf(stderr, "Errore: argomento thread mq_consumer non valido\n");
        pthread_exit(NULL);
    }

    char* buffer = malloc(consumer->message_size);
    if(!buffer) {
        LOG_SYSTEM("mq_consumer", "Errore nell'allocazione del buffer del messaggio");
        perror("Errore nell'allocazione del buffer del messaggio");
        pthread_exit(NULL);
    }  
    emergency_request_t request;

    while(consumer->running) {
        struct timespec timeout;
        if(clock_gettime(CLOCK_REALTIME, &timeout) == -1) {
            perror("Errore nel recupero del tempo corrente");
            break;
        }
        timeout.tv_sec += 1; // Attendi al massimo 1 secondo   

        ssize_t bytes_received = mq_timedreceive(consumer->mq, buffer, consumer->message_size, NULL, &timeout);
        if(bytes_received <= 0){ // Gestione errori
            if(errno == ETIMEDOUT || errno == EAGAIN) {
                // Nessun messaggio disponibile entro il timeout
                continue;
            } else if( errno == EINTR) {
                LOG_SYSTEM("mq_consumer", "Ricezione del messaggio interrotta da segnale, riprovo");
                continue; 
            } else if (errno == EBADF) {
                LOG_SYSTEM("mq_consumer", "Coda di messaggi chiusa, terminazione del thread consumatore");
                break;
            } else if (errno == EINVAL) {
                LOG_SYSTEM("mq_consumer", "Coda di messaggi non valida, terminazione del thread consumatore");
                break; 
            } else if (errno == EMSGSIZE) {
                LOG_SYSTEM("mq_consumer", "Messaggio troppo grande per il buffer, ignoro il messaggio");
            }
            continue;
        }

        // Crea tutti i thread necessari per raggiungere il cap MAX_WORKER_THREADS
        pthread_mutex_lock(&consumer->state->mutex);

        size_t current_workers = consumer->state->worker_threads_count;
        if(current_workers < MAX_WORKER_THREADS) {
            LOG_SYSTEM("mq_consumer", "Numero di worker threads (%zu) inferiore al massimo (%d), creazione di nuovi thread", current_workers, MAX_WORKER_THREADS);
            size_t threads_to_create = MAX_WORKER_THREADS - current_workers;
            for(size_t i = 0; i < threads_to_create; ++i) {
                pthread_t new_thread;
                if(pthread_create(&new_thread, NULL, worker_thread, (void*)consumer->state) == 0) {
                    // Aggiunge il nuovo thread all'array dei worker threads
                    if(consumer->state->worker_threads != NULL) {
                        consumer->state->worker_threads[consumer->state->worker_threads_count] = new_thread;
                        consumer->state->worker_threads_count++;
                        LOG_SYSTEM("mq_consumer", "Nuovo worker thread creato. Totale worker threads: %zu", consumer->state->worker_threads_count);
                    } else {
                        LOG_SYSTEM("mq_consumer", "Errore nella reallocazione dell'array dei worker threads");
                        perror("Errore nella reallocazione dell'array dei worker threads");
                    }
                } else {
                    LOG_SYSTEM("mq_consumer", "Errore nella creazione del nuovo worker thread");
                    perror("Errore nella creazione del nuovo worker thread");
                }
            }
        }
        pthread_mutex_unlock(&consumer->state->mutex);
        

        buffer[bytes_received] = '\0'; // Termina il messaggio
        LOG_SYSTEM("mq_consumer", "Messaggio ricevuto: %s", buffer);
        if(mq_parse_message(consumer, buffer, &request)) {
            LOG_SYSTEM("mq_consumer", "Richiesta di emergenza analizzata: %s %d %d %ld", request.emergency_name, request.x, request.y, request.timestamp);
            // Processa la richiesta di emergenza
            if(consumer->running){
                if(status_add_waiting(consumer->state, &request, consumer->emergency_types, consumer->emergency_types_count)) {
                    LOG_SYSTEM("mq_consumer", "Errore nell'assegnazione della richiesta di emergenza");
                }
                
            }
        }

                
    }
    LOG_SYSTEM("mq_consumer", "Terminazione del thread consumatore");

    free(buffer);
    pthread_exit(NULL);
}

void initialize_mq(mq_consumer_t* consumer) {
    if(!consumer) {
        fprintf(stderr, "Errore: argomenti mq_consumer non validi\n");
        exit(1);
    }
    LOG_SYSTEM("mq_consumer", "Inizializzazione della struttura mq_consumer");
    consumer->mq_name = strdup("/emergenze676878"); // Nome della coda di messaggi
    consumer->message_size = 256;                   // Dimensione massima del messaggio
    consumer->shutdown_flag = 0;                    // Flag di shutdown inizializzato a 0
    consumer->thread_created = false;               // Flag per indicare se il thread è stato creato
    consumer->mq = (mqd_t)-1;                       // Coda non ancora aperta
    consumer->env_width = 0;                        // Dimensioni dell'ambiente
    consumer->env_height = 0;                       // Dimensioni dell'ambiente
    consumer->emergency_types = NULL;               // Puntatore ai tipi di emergenza
    consumer->emergency_types_count = 0;            // Numero di tipi di emergenza
    consumer->consumer_thread = 0;                  // Thread non ancora creato
    consumer->running = 0;                          // Flag di esecuzione del thread
    LOG_SYSTEM("mq_consumer", "Struttura mq_consumer inizializzata. Nome coda: %s", consumer->mq_name);
}

int start_mq(mq_consumer_t* consumer, environment_variable_t* environment, emergency_type_t* emergency_types, size_t emergency_types_count) {
    if(!consumer) {
        LOG_SYSTEM("mq_consumer", "Consumer non valido");
        return -1; // Errore: argomenti non validi
    }
    if(!environment) {
        LOG_SYSTEM("mq_consumer", "Variabili d'ambiente non valide");
        return -1; // Errore: variabili d'ambiente non valide
    }
    
    if(!consumer || !environment || !emergency_types) {
        LOG_SYSTEM("mq_consumer", "Argomenti non validi o coda non aperta");
        return -1; // Errore: argomenti non validi o coda non aperta
    }
    LOG_SYSTEM("mq_consumer", "Inizializzazione della message queue");
    initialize_mq(consumer);

    consumer->env_width = environment->width;
    consumer->env_height = environment->height;
    consumer->emergency_types = emergency_types;
    consumer->emergency_types_count = emergency_types_count;

    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = consumer->message_size; // 256
    attr.mq_curmsgs = 0;

    consumer->mq = mq_open(consumer->mq_name, O_RDONLY | O_CREAT, 0644, &attr);
    if(consumer->mq == (mqd_t)-1) {
        LOG_SYSTEM("mq_consumer", "Errore nell'apertura della coda di messaggi");
        perror("Errore nell'apertura della coda di messaggi");
        return -1;
    }
    
    consumer->running = 1;

    int result = pthread_create(&consumer->consumer_thread, NULL, mq_consumer_thread, (void*)consumer);
    if(result != 0) {
        LOG_SYSTEM("mq_consumer", "Errore nella creazione del thread consumatore");
        perror("Errore nella creazione del thread del consumer");
        mq_close(consumer->mq);
        consumer->running = 0;
        return -1;
    }

    consumer->thread_created = true;
    LOG_SYSTEM("mq_consumer", "Message queue inizializzata correttamente");

    return 0;
}

void shutdown_mq(mq_consumer_t* consumer) {
    if (consumer == NULL || !consumer->running) {
        LOG_SYSTEM("mq_consumer", "Shutdown chiamato su consumer non valido o non in esecuzione");
        return;
    }
    LOG_SYSTEM("mq_consumer", "Shutdown della message queue in corso");
    consumer->running = 0;

    // Attendi la terminazione del thread
    if (consumer->thread_created) {
        LOG_SYSTEM("mq_consumer", "Attesa della terminazione del thread consumatore");
        pthread_join(consumer->consumer_thread, NULL);
        consumer->consumer_thread = 0;
        consumer->thread_created = false;
    }

    // Chiudi la coda di messaggi
    if (consumer->mq != (mqd_t)-1) {
        LOG_SYSTEM("mq_consumer", "Chiusura della coda di messaggi");
        mq_close(consumer->mq);
        mq_unlink(consumer->mq_name);
        consumer->mq = (mqd_t)-1;
    }

    // Libera la memoria allocata per il nome della coda
    if (consumer->mq_name != NULL) {
        free(consumer->mq_name);
        consumer->mq_name = NULL;
    }

    consumer->emergency_types = NULL;
    consumer->emergency_types_count = 0;
    LOG_SYSTEM("mq_consumer", "Message queue terminata correttamente");
}

void stop_mq(mq_consumer_t* consumer) {
    if (consumer == NULL || !consumer->running) {
        return;
    }
    LOG_SYSTEM("mq_consumer", "Stop della message queue in corso");
    consumer->running = 0;
}
