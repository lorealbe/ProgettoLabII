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

#define MAX_WORKER_THREADS 16


static void add_emergency_request(emergency_request_t** requests, size_t* current_size, const char* name, int x, int y, time_t timestamp) {
    emergency_request_t new_request;
    strncpy(new_request.emergency_name, name, EMERGENCY_NAME_LENGTH);
    new_request.x = x;
    new_request.y = y;
    new_request.timestamp = timestamp;

    *requests = realloc(*requests, (*current_size + 1) * sizeof(emergency_request_t));
    if (*requests == NULL) {
        perror("Errore nella reallocazione della memoria");
        exit(EXIT_FAILURE);
    }
    (*requests)[*current_size] = new_request;
    (*current_size)++;
}

static emergency_t* find_emergency_requirements(char* name, emergency_type_t* emergency_types, emergency_request_t* request, 
                                         emergency_t* emergency) {
    for (size_t i = 0; emergency_types[i].emergency_name != NULL; i++) {
        if (strcmp(emergency_types[i].emergency_name, name) == 0) {
            emergency->type = emergency_types[i];
            emergency->status = WAITING;
            emergency->x = request->x;
            emergency->y = request->y;
            emergency->time = request->timestamp;
            // Calcola il numero totale di soccorritori richiesti sommando required_count di ogni requisito
            int total_required = 0;
            if (emergency_types[i].rescuer_requests != NULL) {
                for (size_t r = 0; r < (size_t)emergency_types[i].rescuers_req_number; r++) {
                    total_required += emergency_types[i].rescuer_requests[r].required_count;
                }
            }
            emergency->rescuers_count = total_required;
            emergency->assigned_rescuers = NULL; // Inizialmente nessun soccorritore assegnato
            return emergency;
        }
    }
    return NULL; // Non trovato

}

static int arrive_in_time(time_t time_required_to_scene, int priority) {
    switch(priority) {
        case 2: // alta
            return time_required_to_scene <= 10; // in secondi
        case 1: // media
            return time_required_to_scene <= 30; 
        default:
            return 1; // Nessun vincolo di tempo per bassa priorità
    }
}


// Ricerca dell'emergency corrente a cui è assegnato il soccorritore
static emergency_t* find_emergency_by_rescuer(rescuer_digital_twin_t* rescuer, emergency_t* emergencies, size_t emergency_count) {
    for (size_t i = 0; i < emergency_count; i++) {
        emergency_t* emergency = &emergencies[i];
        for (size_t j = 0; j < (size_t)emergency->rescuers_count; j++) {
            if (&emergency->assigned_rescuers[j] == rescuer) {
                return emergency;
            }
        }
    }
    return NULL; // Non trovato
}

static int manhattan_distance(int x1, int y1, int x2, int y2) {
    return abs(x1 - x2) + abs(y1 - y2);
}


static void estimate_rescuer_position(rescuer_digital_twin_t* rescuer, emergency_t* current_emergency, int* est_x, int* est_y) {
    
    // Il soccorritore si muove in linea retta verso la coordinata x dell'emergenza, poi verso y
    int em_x = current_emergency->x;
    int em_y = current_emergency->y;
    int init_res_x = rescuer->x;
    int init_res_y = rescuer->y;
    int speed = rescuer->type->speed > 0 ? rescuer->type->speed : 1;
    int distance = manhattan_distance(init_res_x, init_res_y, em_x, em_y);
    time_t time_elapsed = time(NULL) - current_emergency->time;
    int distance_covered = speed * time_elapsed;
    if (distance_covered >= distance) { // Emergency reached
        *est_x = em_x;
        *est_y = em_y;
    } else {
        // Calcola la posizione stimata lungo il percorso
        int remaining_distance = distance - distance_covered; // distanza rimanente
        int total_x_diff = em_x - init_res_x;
        int total_y_diff = em_y - init_res_y;
        if(distance_covered > total_x_diff){
            *est_x = em_x;
            *est_y = init_res_y + (distance_covered - abs(total_x_diff)) * (total_y_diff > 0 ? 1 : -1);
            return;
        }
        /*
        float ratio = (float)(distance_covered) / distance;   
        *est_x = init_res_x + (int)((em_x - init_res_x) * ratio);
        *est_y = init_res_y + (int)((em_y - init_res_y) * ratio);
        */
    }
}

// !!! Controlla se è corretta !!!
static void remove_rescuer_from_emergency(rescuer_digital_twin_t* rescuer, emergency_t* emergency) {
    if (emergency == NULL || emergency->assigned_rescuers == NULL) return;
    size_t idx = -1;
    for (size_t i = 0; i < (size_t)emergency->rescuers_count; i++) {
        if (&emergency->assigned_rescuers[i] == rescuer) {
            idx = i;
            break;
        }
    }
    if (idx != (size_t)-1) {
        emergency->assigned_rescuers[idx] = NULL; // Rimuovi il soccorritore
        emergency->status = WAITING; // Aggiorna lo stato dell'emergenza se necessario
    }
}


// Cerca e restituisce il miglior soccorritore 

// !!! DA AGGIUNGERE CONTROLLI SE CI SONO SOCCORRITORI OCCUPATI IN EMERGENZE CON PRIORITÀ MINORE !!!
static rescuer_digital_twin_t* find_best_rescuer(emergency_t* emergency, rescuer_request_t* req, rescuer_digital_twin_t* rescuer_twins, emergency_t* emergencies, size_t emergency_count) {
    if (rescuer_twins == NULL || req == NULL || emergency == NULL) return NULL;
    time_t best_time = LONG_MAX;
    rescuer_digital_twin_t* best = NULL;

    for (size_t k = 0; ; k++) {
        // iterazione sicura: verifica che il campo type sia inizializzato; si assume che il parser termini la lista con .type == NULL
        if (rescuer_twins[k].type == NULL) break;
        rescuer_digital_twin_t* rescuer = &rescuer_twins[k];
        if (rescuer == NULL || rescuer->type == NULL || req->type == NULL) continue;
        if (strcmp(rescuer->type->rescuer_type_name, req->type->rescuer_type_name) == 0) {

            int distance = manhattan_distance(rescuer->x, rescuer->y, emergency->x, emergency->y);
            int speed = rescuer->type->speed > 0 ? rescuer->type->speed : 1;
            time_t time_to_scene = (distance + speed - 1) / speed;
            if(rescuer->status == IDLE) {
                if (arrive_in_time(time_to_scene, emergency->type.priority) && time_to_scene < best_time) {
                    best = rescuer;
                    best_time = time_to_scene;
                } 
            } else if(rescuer->status == EN_ROUTE_TO_SCENE || rescuer->status == ON_SCENE) {
                emergency_t* current_emergency = find_emergency_by_rescuer(rescuer, emergencies, emergency_count);
                
                // Rimuove il soccorritore dall'altra emergenza solo se la priorità è minore rispetto a quella corrente
                if(emergency->type.priority > 0 && current_emergency->type.priority < emergency->type.priority) { 
                    if(rescuer->status == ON_SCENE){
                        // Controlla il tempo rimanente per terminare l'emergenza

                        time_t time_remaining; // TODO: calcolare il tempo rimanente effettivo
                        // ??? Creare una funzione per calcolare il tempo rimanente considerando anche il ritorno alla base
                        // time_remaining = calculate_time_remaining(current_emergency, rescuer);

                        time_t total_time_to_scene = time_to_scene + time_remaining;
                        if(arrive_in_time(total_time_to_scene, emergency->type.priority) && total_time_to_scene < best_time) {
                            best = rescuer;
                            best_time = time_to_scene;
                        } else {
                            if(arrive_in_time(time_to_scene, emergency->type.priority) && time_to_scene < best_time) {
                                best = rescuer;
                                best_time = time_to_scene;
                            }
                        }
                    } else {
                        // Stima la posizione attuale del soccorritore
                        int est_x, est_y;
                        estimate_rescuer_position(rescuer, current_emergency, &est_x, &est_y);
                        distance = manhattan_distance(est_x, est_y, emergency->x, emergency->y);
                        time_to_scene = (distance + speed - 1) / speed;
                        if(arrive_in_time(time_to_scene, emergency->type.priority) && time_to_scene < best_time) {
                            best = rescuer;
                            best_time = time_to_scene;
                        }
                    }
                }
            }
        } 
        // TODO: Rimuovi il soccorritore dall'altra emergenza
        if(best != NULL) {
            remove_rescuer_from_emergency(best, current_emergency);
            // TODO: Aggiorna lo stato del soccorritore
            best->status = EN_ROUTE_TO_SCENE;
        }
    }
    return best;
}



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
    time_t timestamp;
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
        if(bytes_received <= 0){
            if(errno != ETIMEDOUT || errno != EAGAIN) {
                LOG_SYSTEM("mq_consumer", "Errore nella ricezione del messaggio dalla coda");
                perror("Errore nella ricezione del messaggio dalla coda");
                // Nessun messaggio disponibile entro il timeout
                continue;
            } else if( errno == EINTR) {
                LOG_SYSTEM("mq_consumer", "Ricezione del messaggio interrotta da segnale, riprovo");
                continue; // Interrupted by signal, retry
            } 
            continue;
        }

        // Crea tutti i thread necessari per raggiungere il cap MAX_WORKER_THREADS
        pthread_mutex_lock(&consumer->state.mutex);
        size_t current_workers = consumer->state.worker_threads_count;
        if(current_workers < MAX_WORKER_THREADS) {
            size_t threads_to_create = MAX_WORKER_THREADS - current_workers;
            for(size_t i = 0; i < threads_to_create; ++i) {
                pthread_t new_thread;
                if(pthread_create(&new_thread, NULL, worker_thread, (void*)&consumer->state) == 0) {
                    // Aggiungi il nuovo thread all'array dei worker threads
                    consumer->state.worker_threads = realloc(consumer->state.worker_threads, (consumer->state.worker_threads_count + 1) * sizeof(pthread_t));
                    if(consumer->state.worker_threads != NULL) {
                        consumer->state.worker_threads[consumer->state.worker_threads_count] = new_thread;
                        consumer->state.worker_threads_count++;
                        LOG_SYSTEM("mq_consumer", "Nuovo worker thread creato. Totale worker threads: %zu", consumer->state.worker_threads_count);
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
        pthread_mutex_unlock(&consumer->state.mutex);
        

        buffer[bytes_received] = '\0'; // Termina il messaggio
        LOG_SYSTEM("mq_consumer", "Messaggio ricevuto: %s", buffer);
        if(mq_parse_message(consumer, buffer, &request)) {
            LOG_SYSTEM("mq_consumer", "Richiesta di emergenza analizzata: %s %d %d %ld", request.emergency_name, request.x, request.y, request.timestamp);
            // Processa la richiesta di emergenza
            if(consumer->running){
                if(status_add_waiting(&consumer->state, &request, consumer->emergency_types, consumer->emergency_types_count)) {
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
    if(!args) {
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

int start_mq(mq_consumer_t* consumer, environment_t* environment, emergency_type_t* emergency_types, size_t emergency_types_count) {
    if(!consumer || !environment || !emergency_types || !consumer->mq) {
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
    attr.mq_maxmsg = 32;
    attr.mq_msgsize = consumer->message_size;
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
