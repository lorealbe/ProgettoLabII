#include "status.h"
#include "../mq_consumer.h"
#include "../logging.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_WORKER_THREADS 16

// Dichiarazione anticipata delle funzioni thread
void* worker_thread(void* arg);
void* timeout_thread(void* arg);

/*
* ---------------------------------------------------------------------------------------------------
*                             Funzioni per la gestione delle emergenze
* ---------------------------------------------------------------------------------------------------
*/

// Restituisce l'indice di un elemento
static size_t find_idx(void** array, size_t count, void* element) {
    if(array == NULL || count == 0 || element == NULL) {
        return (size_t)-1; // Parametri non validi
    }
    for(size_t i = 0; i < count; i++) {
        if(array[i] == element) {
            return i; // Elemento trovato
        }
    }
    return (size_t)-1; // Elemento non trovato
}

// Rimuove un elemento da una coda e ne aggiorna la dimensione
static emergency_t* remove_emergency_from_general_queue(void** array, size_t* count, size_t index) {
    if(array == NULL || count == NULL || *count == 0 || index >= *count) {
        return NULL; // Parametri non validi
    }
    LOG_SYSTEM("status", "Rimozione di un'emergenza dalla coda "); // ??????????????
    emergency_t* emergency = (emergency_t*)array[index];
    size_t remaining = *count - index - 1;
    if(remaining > 0) {
        memmove(&array[index], &array[index + 1], remaining * sizeof(void*));
    }
    (*count)--;
    array[*count] = NULL;
    LOG_SYSTEM("status", "Emergenza rimossa correttamente");
    return emergency;
}

static rescuer_digital_twin_t* remove_rescuer_from_general_queue(void** array, size_t* count, size_t index) {
    if(array == NULL || count == NULL || *count == 0 || index >= *count) {
        return NULL; // Parametri non validi
    }
    LOG_SYSTEM("status", "Rimozione di un soccorritore dalla coda "); // ??????????????
    rescuer_digital_twin_t* rescuer = (rescuer_digital_twin_t*)array[index];
    size_t remaining = *count - index - 1;
    if(remaining > 0) {
        memmove(&array[index], &array[index + 1], remaining * sizeof(void*));
    }
    (*count)--;
    array[*count] = NULL;
    LOG_SYSTEM("status", "Soccorritore rimosso correttamente");
    return rescuer;
} 

// Garantisce che la capacità di un array sia sufficiente, raddoppiandola se necessario
static bool ensure_capacity(void*** array, size_t* capacity, size_t required) { 
    if(array == NULL || capacity == NULL) {
        return false; // Parametri non validi
    }
    if(*capacity >= required) {
        return true; // Capacità sufficiente
    }
    size_t new_capacity = *capacity == 0 ? 4 : *capacity;
    while(new_capacity < required) {
        new_capacity *= 2;
    }
    void** temp = realloc(*array, new_capacity * sizeof(void*));
    if(temp == NULL) {
        LOG_SYSTEM("status", "Errore di allocazione della memoria");
        return false; // Errore di allocazione
    }
    *array = temp;
    *capacity = new_capacity;
    return true;
}

// Inserisce un elemento in una coda, espandendo la capacità se necessario
static bool insert_into_general_queue(void*** array, size_t* count, size_t* capacity, void* element) { // Inserisce un elemento in una coda, espandendo la capacità se necessario
    if(array == NULL || count == NULL || capacity == NULL || element == NULL) {
        return false; // Parametri non validi
    }
    if(!ensure_capacity(array, capacity, *count + 1)) {
        return false; // Errore di allocazione
    }
    LOG_SYSTEM("status", "Allocazione della memoria necessaria per il nuovo elemento riuscita");
    (*array)[*count] = element;
    LOG_SYSTEM("status", "Elemento inserito correttamente");
    (*count)++;
    return true;
}

// Calcola la distanza di Manhattan tra due punti
static int manhattan_distance(int x1, int y1, int x2, int y2) {
    return abs(x1 - x2) + abs(y1 - y2);
}

// Verifica se un soccorritore può arrivare in tempo all'emergenza in base alla priorità
static bool arrive_in_time(time_t time_to_scene, short priority) {
    // Definisce i tempi massimi per ogni priorità
    const time_t max_times[] = {30, 10}; // priorità 1 -> 30s, priorità 2 -> 10s

    if(priority < 0 || priority > 2) {
        return false; // Priorità non valida
    }
    // Se la priorità è 0 (bassa), non ci sono vincoli di tempo per raggiungere la scena
    return (priority == 0)?true:time_to_scene <= max_times[priority-1];
}

// Calcola il tempo di gestione totale di un'emergenza in base ai tipi di soccorritori richiesti
static int management_time(emergency_record_t* record) {
    if(!record) {
        return -1; // Errore: tipo di emergenza non valido
    }
    // Calcola il tempo massimo di gestione tra tutti i tipi di soccorritori richiesti
    int max_time = 0;
    for(size_t i = 0; i < (size_t)record->emergency.type.rescuers_req_number; ++i) {
        int req_time = record->emergency.type.rescuer_requests[i].time_to_manage;
        if(req_time > max_time) {
            max_time = req_time;
        }
    }
    return max_time;
    
}

static int prepare_emergency_record(state_t* state, emergency_record_t** out_record, emergency_requests_t* request, emergency_type_t* emergency_types, size_t emergency_types_count) {
    if(!state || !out_record || !request || !emergency_types) {
        return -1; // Errore: parametri non validi
    }

    emergency_type_t* type = find_emergency_type_by_name(request->emergency_name, emergency_types, emergency_types_count);
    if(!type) {
        return -1; // Errore: tipo di emergenza non trovato
    }

    emergency_record_t* emergency_record = calloc(1, sizeof(emergency_record_t));
    if(!emergency_record) {
        return -1; // Errore di allocazione
    }

    emergency_record->emergency.type = *type;
    emergency_record->emergency.status = WAITING;
    emergency_record->emergency.x = request->x;
    emergency_record->emergency.y = request->y;
    emergency_record->emergency.time = request->timestamp;

    // Calcola il numero totale di soccorritori richiesti sommando required_count di ogni requisito
    int total_required = 0;
    if (type->rescuer_requests != NULL) {
        for (size_t r = 0; r < (size_t)type->rescuers_req_number; r++) {
            total_required += type->rescuer_requests[r].required_count;
        }
    }
    emergency_record->emergency.rescuers_count = total_required;
    emergency_record->emergency.assigned_rescuers = NULL; // Inizialmente nessun soccorritore assegnato
    emergency_record->current_priority = type->priority;
    emergency_record->total_time_to_manage = management_time(emergency_record);
    emergency_record->time_remaining = emergency_record->total_time_to_manage;

    emergency_record->starting_time = 0;

    emergency_record->preempted = false;

    *out_record = emergency_record;
    return 0; // Successo
}


static bool pause_emergency(state_t* state, emergency_t* emergency){
    state->emergencies_paused_count++;

    ensure_capacity((void***)&state->emergencies_paused, &state->emergencies_paused_capacity, state->emergencies_paused_count);

    emergency->status = PAUSED;
    // Trova l'indirizzo dell'emergenza nell'array delle emergenze in corso e la rimuove
    size_t idx = find_idx((void**)state->emergencies_in_progress, state->emergencies_in_progress_count, (void*)emergency);
    if(idx == (size_t)-1){
        return false; // Emergenza non trovata nell'array delle emergenze in corso
    }

    emergency->preempted = true;
    remove_from_general_queue(state->emergencies_in_progress, 
                              &state->emergencies_in_progress_count, 
                              idx);
    // Inserisce l'emergenza tra quelle in pausa
    insert_into_general_queue((void***)&state->emergencies_paused, 
                               &state->emergencies_paused_count, 
                               &state->emergencies_paused_capacity, 
                               (void*)emergency);
    return true;
}

static bool timeout_emergency(state_t* state, emergency_t* emergency){
    if(!state || !emergency) return false; // Errore nei parametri

    emergency->status = TIMEOUT;
    // Trova l'indirizzo dell'emergenza nell'array delle emergenze in corso e la rimuove
    size_t idx = find_idx((void**)state->emergencies_in_progress, state->emergencies_in_progress_count, (void*)emergency);
    if(idx == (size_t)-1){
        return false; // Emergenza non trovata nell'array delle emergenze in corso
    }

    remove_from_general_queue(state->emergencies_in_progress, 
                              &state->emergencies_in_progress_count, 
                              idx);
    // Log timeout 
    // L'emergenza non è stata risolta in tempo
    return true;
}

static rescuer_digital_twin_t* find_best_idle_rescuer(state_t* state, emergency_record_t* record){
    if(!state || !record) return NULL; // Errore nei parametri
    if(state->rescuer_available_count == 0) return NULL; // Nessun soccorritore disponibile
    emergency_t* emergency = &record->emergency;
    rescuer_digital_twin_t* best = NULL;
    time_t best_time = LONG_MAX;
    for(size_t i = 0; i < state->rescuer_available_count; ++i){
        rescuer_digital_twin_t* rescuer = state->rescuer_available[i]; // Rescuer_available contiene solo soccorritori IDLE
        
        for(size_t j = 0; j < (size_t)emergency->type.rescuers_req_number; ++j){
            rescuer_request_t* req = &emergency->type.rescuer_requests[j];
            if(strcmp(rescuer->type->rescuer_type_name, req->type->rescuer_type_name) == 0){
                int distance = manhattan_distance(rescuer->x, rescuer->y, emergency->x, emergency->y);
                int speed = rescuer->type->speed > 0 ? rescuer->type->speed : 1; // Garantisce che non ci siano velocità nulle o negative
                time_t time_to_scene = (distance + speed - 1) / speed; // Calcola il tempo stimato per arrivare sulla scena approssimando per eccesso
                if(time_to_scene < best_time && arrive_in_time(time_to_scene, emergency->type.priority)){
                    best = rescuer;
                    best_time = time_to_scene;
                }
            }
        }
    }
}

static rescuer_digital_twin_t* find_best_rescuer_lower_priority(state_t* state, emergency_record_t* record){
    if(!state || !record) return NULL; // Errore nei parametri

    emergency_t* emergency = &record->emergency; // Emergenza da risolvere
    emergency_t* rescuer_emergency = NULL;       // Emergenza da cui viene prelevato il soccorritore
    rescuer_digital_twin_t* best = NULL;         // Soccorritore che arriva prima all'emergenza
    time_t best_time = LONG_MAX;      
    int idx = -1;                            // Indice del soccorritore trovato nell'array dei soccorritori in uso           

    for(size_t i = 0; i < state->rescuer_in_use_count; ++i){
        rescuer_digital_twin_t* rescuer = state->rescuers_in_use[i]; // Rescuer_in_use contiene solo soccorritori non IDLE
        if(rescuer->status != EN_ROUTE_TO_SCENE && rescuer->status != ON_SCENE){
            continue; // Ignora i soccorritori RETURNING_TO_BASE
        }
        rescuer_emergency = find_emergency_by_rescuer(best, state->emergencies_in_progress, state->emergencies_in_progress_count);
        if(!rescuer_emergency){
            // L'emergenza non esiste
            // Il soccorritore viene rimosso dall'array rescuers_in_use
            memmove(&state->rescuers_in_use[i], &state->rescuers_in_use[i+1], (state->rescuers_in_use_count - i - 1) * sizeof(rescuer_digital_twin_t*));
            state->rescuers_in_use_count--;
            i--; // Decrementa per controllare il nuovo rescuer_in_use[i]
        }
        if(rescuer_emergency->type.priority >= emergency->type.priority) continue; // Il soccoritore si trova in un emergenza di priorità pari o superiore e viene quindi ignorato
        
        // Abbiamo il soccorritore in un'emergenza di priorità inferiore
        // emergency -> quella da risolvere
        // rescuer_emergency -> quella in cui si trova attualmente il soccorritore



        // Cerco il tipo di soccorritore richiesto dall'emergenza --> servono N tipi di soccorritori
        // cerco tra i soccoritori in uso se c'è quel tipo        --> scorre fino ad N * la sua lunghezza (M)
        // controllo se posso rimuverlo e se arriva in tempo      --> scorro N volte

        // eccetto all'inizio N < M

        // prendo in ordine tutti i soccorritori in uso                   --> scorro M soccorritori
        // controllo se posso rimuoverlo dall'emergenza in cui si trova   --> controllo M volte 
        // controllo se è del tipo richiesto dall'emergenza               --> scorro N tipi di soccorritori
        // calcolo il tempo per arrivare all'emergenza


        for(size_t j = 0; j < (size_t)emergency->type.rescuers_req_number; ++j){
            rescuer_request_t* req = &emergency->type.rescuer_requests[j];
            if(strcmp(rescuer->type->rescuer_type_name, req->type->rescuer_type_name) == 0){
                int distance = manhattan_distance(rescuer->x, rescuer->y, emergency->x, emergency->y);
                int speed = rescuer->type->speed > 0 ? rescuer->type->speed : 1; // Garantisce che non ci siano velocità nulle o negative
                time_t time_to_scene = (distance + speed - 1) / speed; // Calcola il tempo stimato per arrivare sulla scena approssimando per eccesso
                if(time_to_scene < best_time && arrive_in_time(time_to_scene, emergency->type.priority) ){
                    best = rescuer;
                    best_time = time_to_scene; 
                    idx = find_idx((void**)state->rescuers_in_use, state->rescuer_in_use_count, (void*)best);
                    // TODO AGGIUNGI UN IDX PER IL SOCCORRITORE TROVATO PER POI RIMUOVERLO DALL'ARRAY DELL'EMERGENZA IN CUI SI TROVA
                }
            }
        }
    }      
    


    if(best != NULL){  
        rescuer_emergency->rescuers_count--;
        // Lo aggiungo ad emergency 
        emergency->rescuers_count++;
        insert_into_general_queue((void***)&emergency->assigned_rescuers, 
                               &emergency->rescuers_count, 
                               &emergency->rescuers_capacity, 
                               (void*)best);

        remove_rescuer_from_general_queue((void***)&rescuer_emergency->assigned_rescuers, 
                                  &rescuer_emergency->rescuers_count, 
                                  idx);
    }
    return best;
}

static bool try_allocate_rescuers(state_t* state, emergency_record_t* record){
    if(!state || !record) return false // Errore nei parametri
    emergency_types_t* type = &record->emergency.type;
    int total_rescuers_needed = record->emergency.rescuer_count;

    // Alloca la memoria necessaria per l'array di soccorritori richiesti dall'emergenza
    ensure_capacity((void***)&record->assigned_rescuers, &record->assigned_rescuers_capacity, total_rescuers_needed);

    record->assigned_rescuers_count = 0;
    for (size_t i = 0; i < total_rescuers_needed; i++) {
        rescuer_request_t req = record->emergency->type.rescuer_requests[i];
        for (size_t j = 0; j < req.required_count; j++) {
            rescuer_digital_twin_t* best_rescuer = find_best_idle_rescuer(state, record);
            if (best_rescuer != NULL) {
                // copia il gemello digitale nell'array degli assegnati
                if (idx < (size_t)record->emergency.rescuers_count) {
                    record->assigned_rescuers[idx++] = *best_rescuer;
                    record->assigned_rescuers[idx-1] = EN_ROUTE_TO_SCENE;
                    record->assigned_rescuers_count++;

                    state->rescuer_available_count--;
                    state->rescuer_in_use[state->rescuer_in_use_count++] = *best_rescuer;
                }
            } else if(record->emergency.type.priority != 0) {
                // Non è stato possibile trovare un soccorritore tra quelli IDLE
                // Ricerca del soccorritore tra le emergenze di priorità più bassa
                best_rescuer = find_best_rescuer_lower_priority(state, record);
                if (best_rescuer != NULL) {
                    // copia il gemello digitale nell'array degli assegnati
                    if (idx < (size_t)record->emergency.rescuers_count) {
                        record->assigned_rescuers[idx++] = *best_rescuer;
                        record->assigned_rescuers[idx-1] = EN_ROUTE_TO_SCENE;
                        record->assigned_rescuers_count++;

                        state->rescuer_available_count--;
                        state->rescuer_in_use[state->rescuer_in_use_count++] = *best_rescuer;
                    }
                } else {
                    // Non è stato possibile trovare un soccorritore tra quelli di priorità più bassa
                    // Rilascia i soccorritori già assegnati per questa emergenza
                    for (size_t k = 0; k < record->assigned_rescuers_count; k++) {
                        record->assigned_rescuers[k]->status = IDLE;
                    }
                    free(record->assigned_rescuers);
                    record->assigned_rescuers = NULL;
                    record->assigned_rescuers_count = 0;
                    // Rimette l'emergenza nella coda di attesa
                    insert_into_general_queue((void***)&state->emergencies_waiting, 
                                           &state->emergencies_waiting_count, 
                                           &state->emergencies_waiting_capacity, 
                                           (void*)&record->emergency);
                    return false; // Allocazione fallita
                }
            }
        }
    }
    return true;
}

// TODO COMMENTA

static bool try_allocate_rescuers_for_preempted(state_t* state, emergency_record_t* record){
    if(!state || !record) return false; // Errore nei parametri
    if(!record->preempted) return false; // L'emergenza non è preemptata
    // controlla quali ci sono
    // contando per ogni tipo di soccorritore quanti ne servono ancora
    emergency_types_t* type = &record->emergency.type;
    int total_rescuers_needed = record->emergency.rescuer_count;
    if(record->assigned_rescuers_count == total_rescuers_needed){
        // Tutti i soccorritori sono già assegnati
        record->preempted = false;
        return true;
    }

    for (size_t i = 0; i < total_rescuers_needed; i++) {
        rescuer_request_t req = record->emergency->type.rescuer_requests[i];
        for (size_t j = 0; j < req.required_count; j++) {
            // Controlla se il soccorritore è già assegnato
            bool already_assigned = false;
            for(size_t k = 0; k < record->assigned_rescuers_count; ++k){
                if(strcmp(record->assigned_rescuers[k]->type->rescuer_type_name, req.type->rescuer_type_name) == 0){
                    already_assigned = true;
                    break;
                }
            }
            if(already_assigned) continue; // Passa al prossimo soccorritore

            rescuer_digital_twin_t* best_rescuer = find_best_idle_rescuer(state, record);
            if (best_rescuer != NULL) {
                // copia il gemello digitale nell'array degli assegnati
                if (idx < (size_t)record->emergency.rescuers_count) {
                    record->assigned_rescuers[idx++] = *best_rescuer;
                    record->assigned_rescuers[idx-1] = EN_ROUTE_TO_SCENE;
                    record->assigned_rescuers_count++;

                    state->rescuer_available_count--;
                    state->rescuer_in_use[state->rescuer_in_use_count++] = *best_rescuer;
                }
            } else {
                best_rescuer = find_best_rescuer_lower_priority(state, record);
                if (best_rescuer != NULL) {
                    // copia il gemello digitale nell'array degli assegnati
                    if (idx < (size_t)record->emergency.rescuers_count) {
                        record->assigned_rescuers[idx++] = *best_rescuer;
                        record->assigned_rescuers[idx-1] = EN_ROUTE_TO_SCENE;
                        record->assigned_rescuers_count++;

                        state->rescuer_available_count--;
                        state->rescuer_in_use[state->rescuer_in_use_count++] = *best_rescuer;
                    }
                } else {
                    return false; // Non è stato possibile trovare un soccorritore disponibile
                }
            }
        }
    }
    return true;
}


static bool start_emergency_management(state_t* state, emergency_record_t* record){
    if(!state || !record) return false; // Errore nei parametri
    record->starting_time = (unsigned int)time(NULL);
    record->emergency.status = ASSIGNED;
    // Rimuove l'emergenza dalla coda di attesa
    size_t idx = find_idx((void**)state->emergencies_waiting, state->emergencies_waiting_count, (void*)&record->emergency);
    if(idx == (size_t)-1){
        return false; // Emergenza non trovata nell'array delle emergenze in attesa
    }
    remove_from_general_queue(state->emergencies_waiting, 
                              &state->emergencies_waiting_count, 
                              idx);
    // Inserisce l'emergenza tra quelle in corso
    insert_into_general_queue((void***)&state->emergencies_in_progress, 
                               &state->emergencies_in_progress_count, 
                               &state->emergencies_in_progress_capacity, 
                               (void*)&record->emergency);
    return true;
}

static unsigned int highest_time_to_scene(emergency_record_t* record){
    if(!record) return 0; // Errore nei parametri
    unsigned int max_time = 0;
    for(size_t i = 0; i < record->assigned_rescuers_count; ++i){
        rescuer_digital_twin_t* rescuer = record->assigned_rescuers[i];
        int distance = manhattan_distance(rescuer->x, rescuer->y, record->emergency.x, record->emergency.y);
        int speed = rescuer->type->speed > 0 ? rescuer->type->speed : 1; // Garantisce che non ci siano velocità nulle o negative
        time_t time_to_scene = (distance + speed - 1) / speed; // Calcola il tempo stimato per arrivare sulla scena approssimando per eccesso
        if(time_to_scene > max_time){
            max_time = time_to_scene;
        }
    }
    return max_time;
}

static bool check_all_rescuers_still_assigned(emergency_record_t* record){
    if(!record) return false; // Errore nei parametri
    return record->assigned_rescuers_count == record->emergency.rescuers_count;
}

static void increment_emergency_timeout(emergency_record_t* record){
    if(!record) return; // Errore nei parametri
    if(record->emergency.status == IN_PROGRESS) return; // Non incrementa il timeout se l'emergenza è in corso
    
    record->timeout++;
    const unsigned int TIMEOUT_THRESHOLD = (unsigned int)(record->emergency.type.priority == 2 ? 10 : (record->emergency.type.priority == 1 ? 30 : UINT_MAX));
    if(record->timeout >= TIMEOUT_THRESHOLD){
        record->emergency.status = TIMEOUT;
    }
}

// Restituisce l'emergenza da risolvere con la priorità più alta e la sposta nelle emergenze in corso
static emergency_t* get_highest_priority_emergency(state_t* state){
    if(!state) return NULL; // Errore nei parametri
    LOG_SYSTEM("status", "Ricerca dell'emergenza da risolvere con la priorità più alta");
    emergency_t* emergency = NULL;
    emergency_t* highest = NULL;
    for(size_t i = 0; i < state->emergencies_in_progress_count; ++i){
        emergency = state->emergencies_in_progress[i];
        if(!highest || emergency->current_priority > highest->current_priority){
            highest = emergency;
        }
    }
    if(!highest) {
        LOG_SYSTEM("status", "Nessuna emergenza da risolvere trovata");
        return NULL; // Nessuna emergenza in corso
    }
    size_t idx = find_idx((void**)state->emergencies_in_progress, state->emergencies_in_progress_count, (void*)highest);
    remove_from_general_queue(state->emergencies_waiting, 
                              &state->emergencies_waiting_count, 
                              idx);
    insert_into_general_queue((void***)&state->emergencies_in_progress, 
                              &state->emergencies_in_progress_count, 
                              &state->emergencies_in_progress_capacity, 
                              (void*)highest);
    return highest;
}

/*
* ---------------------------------------------------------------------------------------------------
*                        Funzioni per la gestione dello stato del programma
* ---------------------------------------------------------------------------------------------------
*/

// Pulizia della struttura di emergenza
static void emergency_record_cleanup(emergency_record_t* record){
    if(!record) {
        return;
    }
    LOG_SYSTEM("status", "Pulizia della struttura di emergenza per l'emergenza di tipo %s", record->emergency.type.emergency_name);
    free(record->emergency.assigned_rescuers);
    record->emergency.assigned_rescuers = NULL;
    record->emergency.rescuers_count = 0;
    record->current_priority = 0;
    record->total_time_to_manage = 0;
    record->time_remaining = 0;
    record->preempted = false;
    LOG_SYSTEM("status", "Struttura pulita con successo");
    free(record);
}

// Inizializza lo stato dell'applicazione
int status_init(state_t* state, rescuer_digital_twin_t* rescuer_twins, size_t rescuer_twins_count) {
    if(!state) {
        return -1; // Errore: stato non valido
    }
    LOG_SYSTEM("status", "Inizializzazione dello stato");
    *state = (state_t){0}; // Inizializza tutti i campi a zero/NULL

    // Inizializza mutex
    if(pthread_mutex_init(&state->mutex, NULL) != 0) { // Errore nell'inizializzazione del mutex
        LOG_SYSTEM("status", "Errore nell'inizializzazione del mutex");
        perror("Errore nell'inizializzazione del mutex");
        return -1;
    }

    // Inizializza condition variable
    if(pthread_cond_init(&state->emergency_available_cond, NULL) != 0) { // Errore nell'inizializzazione della condition variable
        LOG_SYSTEM("status", "Errore nell'inizializzazione della condition variable emergency_available");
        pthread_mutex_destroy(&state->mutex);
        return -1;
    }

    if(pthread_cond_init(&state->rescuer_available_cond, NULL) != 0) {// Errore nell'inizializzazione della condition variable
        LOG_SYSTEM("status", "Errore nell'inizializzazione della condition variable rescuer_available");
        pthread_cond_destroy(&state->emergency_available_cond);
        pthread_mutex_destroy(&state->mutex);
        return -1;
    }

    // Inizializza l'array dei soccorritori disponibili
    if(rescuer_twins_count > 0) {
        LOG_SYSTEM("status", "Inizializzazione dell'array dei soccorritori disponibili");
        state->rescuer_available = calloc(rescuer_twins_count, sizeof(*rescuer_digital_twin_t)); 
        if(!state->rescuer_available) { // Errore di allocazione
            LOG_SYSTEM("status", "Errore di allocazione per l'array dei soccorritori disponibili");
            pthread_cond_destroy(&state->rescuer_available_cond);
            pthread_cond_destroy(&state->emergency_available_cond);
            pthread_mutex_destroy(&state->mutex);
            return -1;
        }

        for (size_t i = 0; i < rescuer_twins_count; ++i) {
            state->rescuer_available[i] = &rescuer_twins[i];
            state->rescuer_available[i].status = IDLE
        }
        state->rescuer_available_count = rescuer_twins_count;
        LOG_SYSTEM("status", "Array dei soccorritori disponibili inizializzato con successo");
        return 0;
    }
}

// Distrugge lo stato dell'applicazione
void status_destroy(state_t* state) {
    if(!state) {
        return; // Stato non valido
    }
    LOG_SYSTEM("status", "Distruzione dello stato");

    // !!!  shutdown_mq(state);
    
    // Distruggi mutex e condition variable
    pthread_cond_destroy(&state->emergency_available_cond);
    pthread_cond_destroy(&state->rescuer_available_cond);
    pthread_mutex_destroy(&state->mutex);

    LOG_SYSTEM("status", "Libera memoria allocata per gli array di soccorritori e worker threads");
    // Libera memoria allocata per gli array    
    free(state->rescuer_available);
    free(state->worker_threads);

    LOG_SYSTEM("status", "Libera memoria per le emergenze");
    // Libera memoria per le emergenze (se necessario)
    for(size_t i = 0; i < state->emergencies_waiting_count; ++i) {
        free(state->emergencies_waiting[i]->assigned_rescuers);
        free(state->emergencies_waiting[i]);
    }
    free(state->emergencies_waiting);

    for(size_t i = 0; i < state->emergencies_in_progress_count; ++i) {
        free(state->emergencies_in_progress[i]->assigned_rescuers);
        free(state->emergencies_in_progress[i]);
    }
    free(state->emergencies_in_progress);

    for(size_t i = 0; i < state->emergencies_paused_count; ++i) {
        free(state->emergencies_paused[i]->assigned_rescuers);
        free(state->emergencies_paused[i]);
    }
    free(state->emergencies_paused);
    LOG_SYSTEM("status", "Stato distrutto con successo");
}

// Richiede lo shutdown dello stato
void status_request_shutdown(state_t* state) {
    if(!state) {
        return; 
    }
    LOG_SYSTEM("status", "Richiesta di shutdown dello stato");
    pthread_mutex_lock(&state->mutex);
    *(state->shutdown_flag) = 1; // Imposta il flag di shutdown
    pthread_cond_broadcast(&state->emergency_available_cond); // Sveglia tutti i thread in attesa
    pthread_cond_broadcast(&state->rescuer_available_cond); // Sveglia tutti i thread in attesa
    pthread_mutex_unlock(&state->mutex);
}

// Attende la terminazione dei worker threads
void status_join_worker_threads(state_t* state) {
    if(!state || !state->worker_threads) {
        return; 
    }
    LOG_SYSTEM("status", "Attesa della terminazione dei worker threads");
    for(size_t i = 0; i < state->worker_threads_count; ++i) {
        pthread_join(state->worker_threads[i], NULL);
    }
}


// !!!!!!!!!!!!!11
// Avvia i worker threads
int status_start_worker_threads(state_t* state, size_t worker_threads_count) {
    if(!state) {
        return -1; 
    }
    if(worker_threads_count == 0) {
        worker_threads_count = MAX_WORKER_THREADS; 
    }
    LOG_SYSTEM("status", "Inizializzazione dei worker threads");
    state->worker_threads = malloc(worker_threads_count * sizeof(pthread_t));
    if(!state->worker_threads) {
        return -1; // Errore di allocazione
    }
    state->worker_threads_count = worker_threads_count;

    for(size_t i = 0; i < worker_threads_count; ++i) {
        if(pthread_create(&state->worker_threads[i], NULL, worker_thread, (void*)state) != 0) {
            LOG_SYSTEM("status", "Errore nella creazione del worker thread %zu, liberazione risorse in corso", i);
            status_request_shutdown(state); // Richiedi lo shutdown in caso di errore
            status_join_worker_threads(state); // Attendi la terminazione dei thread creati
            free(state->worker_threads);
            state->worker_threads = NULL;
            state->worker_threads_count = 0;
            return -1; // Errore nella creazione del thread
        }
    }
    LOG_SYSTEM("status", "Worker threads creati con successo");


    // Crea il thread per la gestione dei timeout delle emergenze
    if(pthread_create(&state->timeout_thread, NULL, timeout_thread, (void*)state) != 0) {
        LOG_SYSTEM("status", "Errore nella creazione del timeout thread, liberazione risorse in corso");
        status_request_shutdown(state); // Richiedi lo shutdown in caso di errore
        status_join_worker_threads(state); // Attendi la terminazione dei thread creati
        free(state->worker_threads);
        state->worker_threads = NULL;
        state->worker_threads_count = 0;
        return -1; // Errore nella creazione del thread
    }
    LOG_SYSTEM("status", "Timeout thread creato con successo");

    return 0;   
}

// Assegna una nuova richiesta di emergenza
int status_add_waiting(state_t* state, emergency_requests_t* request, emergency_type_t* emergency_types, size_t emergency_types_count){
    if(!state || !request || !emergency_types) {
        return -1; // Errore: parametri non validi
    }
    LOG_SYSTEM("status", "Assegnazione di una nuova richiesta di emergenza");
    pthread_mutex_lock(&state->mutex);

    if(*(state->shutdown_flag)) {
        LOG_SYSTEM("status", "Stato in shutdown, impossibile assegnare nuove richieste");
        pthread_mutex_unlock(&state->mutex);
        return -1; // Errore: stato in shutdown
    }
    emergency_type_t* type = find_emergency_type_by_name(request->emergency_name, emergency_types, emergency_types_count);
    if(!type) {
        pthread_mutex_unlock(&state->mutex);
        return -1; // Errore: tipo di emergenza non trovato
    }
    emergency_record_t* emergency_record = calloc(1, sizeof(emergency_record_t));
    if(!emergency_record) {
        LOG_SYSTEM("status", "Errore di allocazione per il record di emergenza");
        pthread_mutex_unlock(&state->mutex);
        return -1; // Errore di allocazione
    }

    if(prepare_emergency_record(state, &emergency_record, request, emergency_types, emergency_types_count) != 0) {
        LOG_SYSTEM("status", "Errore nella preparazione del record di emergenza");
        emergency_record_cleanup(emergency_record);
        pthread_mutex_unlock(&state->mutex);
        return -1; // Errore nella preparazione del record di emergenza
    }

    // Inserisce l'emergenza creata nella waiting queue
    insert_in_general_queue((void***)&state->emergencies_waiting, 
                           &state->emergencies_waiting_count, 
                           &state->emergencies_waiting_capacity, 
                           (void*)&emergency_record->emergency);
    LOG_SYSTEM("status", "Nuova emergenza inserita nella waiting queue, notifica i worker thread");
    pthread_cond_signal(&state->emergency_available_cond); // Notifica i worker thread dell'arrivo di una nuova emergenza
    pthread_mutex_unlock(&state->mutex); // Sblocca il mutex per i worker appena notificati
    return 0; 
}


/*
* ---------------------------------------------------------------------------------------------------
*                                             Funzioni thread
* ---------------------------------------------------------------------------------------------------
*/
void* worker_thread(void* arg){
    status_t* state = (status_t*)arg;
    if(!state) {
        return NULL;
    } // Errore nello stato

    emergency_record_t* record = NULL;
    
    while(true){
        if(state->shutdown_flag) { // Controlla il flag di shutdown
            pthread_mutex_unlock(&state->mutex);
            break;
        }
        pthread_mutex_lock(&state->mutex);
        if(record && record->preempted){
            if(!try_allocate_rescuers_for_preempted(&state, record)){
                // Non è stato possibile riallocare i soccorritori, continua ad essere preemptata
                pthread_mutex_unlock(&state->mutex);

                if(record->emergency.status == TIMEOUT){
                    pthread_mutex_lock(&state->mutex);
                    timeout_emergency(state, &record->emergency);
                    pthread_mutex_unlock(&state->mutex);
                    break;
                }   
                continue;
            }
            record->preempted = false; 
            remove_from_general_queue(state->emergencies_paused, 
                                      &state->emergencies_paused_count, 
                                      find_idx((void**)state->emergencies_paused, state->emergencies_paused_count, (void*)&record->emergency));
            insert_into_general_queue((void***)&state->emergencies_in_progress, 
                                   &state->emergencies_in_progress_count, 
                                   &state->emergencies_in_progress_capacity, 
                                   (void*)&record->emergency);
        } else {
            while(!&state->shutdown_flag && state->emergencies_waiting_count == 0){
                // Attesa di una nuova emergenza
                pthread_cond_wait(&state->emergency_available_cond, &state->mutex);
                continue;
            }
            record = get_highest_priority_emergency_record(state);
            if(!record){
                // Nessun record di emergenza disponibile
                pthread_mutex_unlock(&state->mutex);
                continue;
            }
            if(!try_allocate_rescuers(&state, record)){
                pthread_mutex_lock(&state->mutex);
                insert_into_general_queue((void***)&state->emergencies_waiting, 
                                    &state->emergencies_waiting_count, 
                                    &state->emergencies_waiting_capacity, 
                                    (void*)&record->emergency);
                pthread_mutex_unlock(&state->mutex);
                continue;
            }
            if(!start_emergency_management(&state, record) && !record->preempted){
                for(size_t i = 0; i < record->assigned_rescuers_count; ++i){
                    record->assigned_rescuers[i]->status = IDLE;
                }
                free(record->assigned_rescuers);
                record->assigned_rescuers = NULL;
                record->assigned_rescuers_count = 0;

                insert_into_general_queue((void***)&state->emergencies_waiting, 
                                    &state->emergencies_waiting_count, 
                                    &state->emergencies_waiting_capacity, 
                                    (void*)&record->emergency);
                pthread_mutex_unlock(&state->mutex);
                continue;
            }
        }

        pthread_mutex_unlock(&state->mutex);
        sleep(highest_time_to_scene(record)); // Simula l'arrivo dei soccorritori sulla scena

        pthread_mutex_lock(&state->mutex);
        if(!check_all_rescuers_still_assigned(record)){
            record->preempted = true;
        } else record->emergency.status = IN_PROGRESS;

        
        // Tutti i soccorritori sono arrivati sulla scena
        while(record->time_remaining > 0 && !record->preempted && !state->shutdown_flag){
            pthread_mutex_unlock(&state->mutex);
            sleep(1); // Simula la gestione dell'emergenza
            pthread_mutex_lock(&state->mutex);
            record->time_remaining--;
            if(!check_all_rescuers_still_assigned(record)){
                record->preempted = true;
            } 
        }
        if(record->time_remaining == 0){
            // Emergenza risolta
            record->emergency.status = COMPLETED;
            // Rilascia i soccorritori assegnati
            for(size_t i = 0; i < record->assigned_rescuers_count; ++i){
                record->assigned_rescuers[i]->status = IDLE;
                state->rescuer_available[state->rescuer_available_count++] = record->assigned_rescuers[i];
            }
            record->assigned_rescuers_count = 0;
            free(record->assigned_rescuers);
            record->assigned_rescuers = NULL;

            // Rimuove l'emergenza dall'array delle emergenze in corso
            size_t idx = find_idx((void**)state->emergencies_in_progress, state->emergencies_in_progress_count, (void*)&record->emergency);
            if(idx != (size_t)-1){
                remove_from_general_queue(state->emergencies_in_progress, 
                                          &state->emergencies_in_progress_count, 
                                          idx);
            }
            emergency_record_cleanup(record);
            break;
        } else if(record->preempted){
            // L'emergenza è stata preemptata
            pause_emergency(state, &record->emergency);
            pthread_mutex_unlock(&state->mutex);
        }
    }
    pthread_mutex_unlock(&state->mutex);
    state->worker_threads_count--;
    return NULL;
}

void* timeout_thread(void* arg){
    status_t* state = (status_t*)arg;
    if(!state) {
        return NULL;
    } // Errore nello stato

    while(true){
        pthread_mutex_lock(&state->mutex);
        if(state->shutdown_flag) { // Controlla il flag di shutdown
            pthread_mutex_unlock(&state->mutex);
            continue;
        }

        for(size_t i = 0; i < state->emergencies_paused_count; ++i){
            emergency_record_t* record = state->emergencies_paused[i];
            increment_emergency_timeout(record);
            // Aggiorna la priorità corrente in base alla formula: priorità_base + radice cubica di (timeout/9)
            // Dopo 9 secondi la priorità aumenta di 1, garantendo che le emergenze di priorità 0 siano gestite in modo prioritario rispetto a quelle di priorità base 1
            // Dopo 72 secondi, una emergenza di priorità base 0 raggiunge la priorità 2
            // Le emergenze di priorià base 0 però non avranno precedenza su quelle di priorità base 2 quando queste sono in timeout da 1 secondo o più
            record->emergency.current_priority = (float)record->emergency.type.priority + (float)(cbrt(((float)(record->timeout/9)))); // Aumenta la priorità in base al timeout
        }
        for(size_t i = 0; i < state->emergencies_waiting_count; ++i){
            emergency_record_t* record = state->emergencies_waiting[i];
            increment_emergency_timeout(record);
            // Aggiorna la priorità corrente in base alla formula: priorità_base + radice cubica di (timeout/9)
            record->emergency.current_priority = (float)record->emergency.type.priority + (float)(cbrt(((float)(record->timeout/9)))); // Aumenta la priorità in base al timeout
            if(record->emergency.status == TIMEOUT){
                timeout_emergency(state, &record->emergency);
                // Rimuove l'emergenza dall'array delle emergenze in attesa
                size_t idx = find_idx((void**)state->emergencies_waiting, state->emergencies_waiting_count, (void*)&record->emergency);
                if(idx != (size_t)-1){
                    remove_from_general_queue(state->emergencies_waiting, 
                                              &state->emergencies_waiting_count, 
                                              idx);
                    emergency_record_cleanup(record);
                    i--; // Decrementa l'indice per controllare il nuovo elemento in questa posizione
                }
            }
        }
        pthread_mutex_unlock(&state->mutex);
        sleep(1); // Attende un secondo prima di controllare nuovamente
    }
    return NULL;
}