#include "status.h"
#include "../../mq_consumer.h"
#include "../../logging.h"
#include "../../Types/emergency_types.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#define MAX_WORKER_THREADS 16

// Dichiarazione anticipata delle funzioni thread
void* worker_thread(void* arg);
void* timeout_thread(void* arg);

/*
* ---------------------------------------------------------------------------------------------------
*                             Funzioni per la gestione delle emergenze
* ---------------------------------------------------------------------------------------------------
*/

// Trova un'emergenza dato un soccorritore 
emergency_t* find_emergency_by_rescuer(rescuer_digital_twin_t* rescuer, emergency_record_t** emergency_array, int length){
    LOG_SYSTEM("emergency_types", "Ricerca dell'emergenza assegnata al soccorritore: %s", rescuer->type->rescuer_type_name);
    if(!rescuer || !emergency_array){
        LOG_SYSTEM("emergency_types", "Parametri non validi per la ricerca dell'emergenza");
        return NULL;
    }
    for(int i = 0; i < length; ++i){
        emergency_record_t* record = emergency_array[i];
        for(int j = 0; j < record->assigned_rescuers_count; ++j){
            if(rescuer->id == record->assigned_rescuers[j].id){
                LOG_SYSTEM("emergency_types", "Emergenza trovata per il soccorritore: %s", rescuer->type->rescuer_type_name);
                return &record->emergency;
            }
        }
    }
    LOG_SYSTEM("emergency_types", "Nessuna emergenza trovata per il soccorritore: %s", rescuer->type->rescuer_type_name);
    return NULL;
}


// Calcola la distanza di Manhattan tra due punti
static int manhattan_distance(int x1, int y1, int x2, int y2) {
    return abs(x1 - x2) + abs(y1 - y2);
}

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

// Stima la posizione attuale del soccorritore in base al tempo trascorso dall'inizio dell'emergenza
static void estimate_rescuer_position(rescuer_digital_twin_t* rescuer, emergency_t* current_emergency, int* est_x, int* est_y) {
    // Il soccorritore si muove in linea retta verso la coordinata x dell'emergenza, poi verso y
    int em_x = current_emergency->x;
    int em_y = current_emergency->y;
    int init_res_x = rescuer->x;
    int init_res_y = rescuer->y;
    
    int speed = rescuer->type->speed > 0 ? rescuer->type->speed : 1; // Garantisce che non ci siano velocità nulle o negative
    int distance = manhattan_distance(init_res_x, init_res_y, em_x, em_y);
    time_t time_elapsed = time(NULL) - current_emergency->time;
    int distance_covered = speed * time_elapsed;
    if (distance_covered >= distance) { // Il soccorritore ha raggiunto l'emergenza
        *est_x = em_x;
        *est_y = em_y;
    } else {
        // Calcola la posizione stimata lungo il percorso
        int total_x_diff = em_x - init_res_x;
        int total_y_diff = em_y - init_res_y;
        if(distance_covered >= abs(total_x_diff)){
            *est_x = em_x;
            *est_y = init_res_y + (distance_covered - abs(total_x_diff)) * (total_y_diff > 0 ? 1 : -1);
            return;
        } else {
            *est_y = init_res_y;
            *est_x = init_res_x + distance_covered * (total_x_diff > 0 ? 1 : -1);
        }
    }
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

// Rimuove un soccorritore da una coda e ne aggiorna la dimensione
static rescuer_digital_twin_t* remove_rescuer_from_general_queue(void*** array, size_t* count, size_t index) {
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

// Prepara il record dell'emergenza ricevuta
static int prepare_emergency_record(state_t* state, emergency_record_t** out_record, emergency_request_t* request, emergency_type_t* emergency_types, size_t emergency_types_count) {
    if(!state || !out_record || !request || !emergency_types) {
        return -1; // Errore: parametri non validi
    }
    LOG_SYSTEM("status", "Preparazione del record per l'emergenza: %s", request->emergency_name);
    emergency_type_t* type = find_emergency_type_by_name(request->emergency_name, emergency_types);
    if(!type) {
        LOG_SYSTEM("status", "Preparazione fallita: %s", request->emergency_name);
        return -1; // Errore: tipo di emergenza non trovato
    }

    emergency_record_t* emergency_record = calloc(1, sizeof(emergency_record_t));
    if(!emergency_record) {
        LOG_SYSTEM("status", "Preparazione fallita: %s", request->emergency_name);
        return -1; // Errore di allocazione
    }

    emergency_record->emergency.type = *type;                        // Copia il tipo di emergenza
    emergency_record->emergency.status = WAITING;                    // Stato iniziale
    emergency_record->emergency.x = request->x;                      // Coordinate X
    emergency_record->emergency.y = request->y;                      // Coordinate Y
    emergency_record->emergency.time = request->timestamp;           // Timestamp in cui è stata ricevuta l'emergenza

    // Calcola il numero totale di soccorritori richiesti sommando required_count di ogni requisito
    int total_required = 0;
    if (type->rescuer_requests != NULL) {
        for (size_t r = 0; r < (size_t)type->rescuers_req_number; r++) {
            total_required += type->rescuer_requests[r].required_count;
        }
    }
    emergency_record->emergency.rescuers_count = total_required;                  // Numero totale di soccorritori richiesti
    emergency_record->emergency.assigned_rescuers = NULL;                         // Inizialmente nessun soccorritore assegnato
    emergency_record->current_priority = type->priority;                          // Priorità iniziale
    emergency_record->total_time_to_manage = management_time(emergency_record);   // Tempo totale di gestione
    emergency_record->time_remaining = emergency_record->total_time_to_manage;    // Tempo rimanente

    emergency_record->starting_time = 0;                                          // Tempo di inizio gestione, 0 = non iniziato         

    emergency_record->preempted = false;                                          // Flag di preemption     

    LOG_SYSTEM("status", "Record per l'emergenza %s preparato correttamente", request->emergency_name);

    *out_record = emergency_record;
    return 0; // Successo
}

// Mette in pausa un'emergenza
static bool pause_emergency(state_t* state, emergency_t* emergency){
    state->emergencies_paused_count++;
    LOG_SYSTEM("status", "Mette in pausa l'emergenza: %s", emergency->type.emergency_name);
    ensure_capacity((void***)&state->emergencies_paused, &state->emergencies_paused_capacity, state->emergencies_paused_count);

    emergency->status = PAUSED;
    // Trova l'indirizzo dell'emergenza nell'array delle emergenze in corso e la rimuove
    size_t idx = find_idx((void**)state->emergencies_in_progress, state->emergencies_in_progress_count, (void*)emergency);
    if(idx == (size_t)-1){
        return false; // Emergenza non trovata nell'array delle emergenze in corso
    }
    LOG_SYSTEM("status", "Emergenza %s rimossa dall'array delle emergenze in corso", emergency->type.emergency_name);
    
    state->emergencies_in_progress[idx]->preempted = true;

    remove_emergency_from_general_queue((void**)state->emergencies_in_progress, 
                              &state->emergencies_in_progress_count, 
                              idx);
    // Inserisce l'emergenza tra quelle in pausa
    insert_into_general_queue((void***)&state->emergencies_paused, 
                               &state->emergencies_paused_count, 
                               &state->emergencies_paused_capacity, 
                               (void*)emergency);
    LOG_SYSTEM("status", "Emergenza %s inserita nell'array delle emergenze in pausa", emergency->type.emergency_name);
    return true;
}

// Termina un'emergenza per timeout
static bool timeout_emergency(state_t* state, emergency_t* emergency){
    if(!state || !emergency) return false; // Errore nei parametri
    emergency->status = TIMEOUT;
    // Trova l'indirizzo dell'emergenza nell'array delle emergenze in corso e la rimuove
    size_t idx = find_idx((void**)state->emergencies_in_progress, state->emergencies_in_progress_count, (void*)emergency);
    if(idx == (size_t)-1){
        return false; // Emergenza non trovata nell'array delle emergenze in corso
    }

    remove_emergency_from_general_queue((void**)state->emergencies_in_progress, 
                              &state->emergencies_in_progress_count, 
                              idx);
    LOG_SYSTEM("status", "Emergenza %s terminata per timeout", emergency->type.emergency_name);
    // L'emergenza non è stata risolta in tempo
    return true;
}

// Trova il miglior soccorritore IDLE per un'emergenza
static rescuer_digital_twin_t* find_best_idle_rescuer(state_t* state, emergency_record_t* record){
    if(!state || !record) return NULL; // Errore nei parametri
    
    LOG_SYSTEM("status", "Ricerca del miglior soccorritore IDLE per l'emergenza: %s", record->emergency.type.emergency_name);

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
    LOG_SYSTEM("status", "Miglior soccorritore IDLE trovato: %s", best ? best->type->rescuer_type_name : "Nessuno");
    return best;
}

// Trova il miglior soccorritore impegnato in un'emergenza di priorità inferiore
static rescuer_digital_twin_t* find_best_rescuer_lower_priority(state_t* state, emergency_record_t* record){
    if(!state || !record) return NULL; // Errore nei parametri

    LOG_SYSTEM("status", "Ricerca del miglior soccorritore impegnato in un'emergenza di priorità inferiore per l'emergenza: %s", record->emergency.type.emergency_name);

    emergency_t* emergency = &record->emergency; // Emergenza da risolvere
    emergency_t* rescuer_emergency = NULL;       // Emergenza da cui viene prelevato il soccorritore
    rescuer_digital_twin_t* best = NULL;         // Soccorritore che arriva prima all'emergenza
    time_t best_time = LONG_MAX;      
    int idx = -1;                                // Indice del soccorritore trovato nell'array dei soccorritori in uso           


    // Scorre tutti i soccorritori richiesti per l'emergenza
    for(size_t j = 0; j < (size_t)emergency->type.rescuers_req_number; ++j){
        rescuer_request_t* req = &emergency->type.rescuer_requests[j];
        
        // Scorre tutti i soccorritori attualmente in uso in cerca di uno di tipo corrispondente
        for(size_t i = 0; i < state->rescuers_in_use_count; ++i){
            rescuer_digital_twin_t* rescuer = state->rescuers_in_use[i]; // rescuer_in_use contiene solo soccorritori non IDLE
            
            if(strcmp(rescuer->type->rescuer_type_name, req->type->rescuer_type_name) == 0){
                
                if(rescuer->status != EN_ROUTE_TO_SCENE && rescuer->status != ON_SCENE) continue; // Ignora i soccorritori RETURNING_TO_BASE
                
                rescuer_emergency = find_emergency_by_rescuer(best, state->emergencies_in_progress, state->emergencies_in_progress_count);
                if(!rescuer_emergency){
                    // L'emergenza non esiste
                    // Il soccorritore viene rimosso dall'array rescuers_in_use
                    memmove(&state->rescuers_in_use[i], &state->rescuers_in_use[i+1], (state->rescuers_in_use_count - i - 1) * sizeof(rescuer_digital_twin_t*));
                    state->rescuers_in_use_count--;
                    i--; // Decrementa per controllare il nuovo rescuer_in_use[i]
                }
                if(rescuer_emergency->type.priority >= emergency->type.priority) continue; // Il soccoritore si trova in un emergenza di priorità pari o superiore e viene quindi ignorato
                int est_x, est_y;
                estimate_rescuer_position(rescuer, rescuer_emergency, &est_x, &est_y);
                int distance = manhattan_distance(est_x, est_y, emergency->x, emergency->y);
                int speed = rescuer->type->speed > 0 ? rescuer->type->speed : 1; // Garantisce che non ci siano velocità nulle o negative
                time_t time_to_scene = (distance + speed - 1) / speed; // Calcola il tempo stimato per arrivare sulla scena approssimando per eccesso
                if(time_to_scene < best_time && arrive_in_time(time_to_scene, emergency->type.priority) ){
                    best = rescuer;
                    best_time = time_to_scene; 
                    idx = find_idx((void**)state->rescuers_in_use, state->rescuers_in_use_count, (void*)best);
                }
            }
        }
    }

    if(best != NULL){  
        LOG_SYSTEM("status", "Miglior soccorritore impegnato in un'emergenza di priorità inferiore trovato: %s", best->type->rescuer_type_name);
        rescuer_emergency->rescuers_count--;
        
        // Inserisce il soccorritore nell'array dei soccorritori assegnati all'emergenza da risolvere
        emergency->rescuers_count++;
        insert_into_general_queue((void***)&emergency->assigned_rescuers, 
                               (size_t*)&emergency->rescuers_count, 
                               (size_t*)&emergency->rescuers_count, 
                               (void*)best);
        // Rimuove il soccorritore dall'emergenza in cui si trovava
        remove_rescuer_from_general_queue((void***)&rescuer_emergency->assigned_rescuers, 
                                  (size_t*)&rescuer_emergency->rescuers_count, 
                                  idx);
    } else {
        LOG_SYSTEM("status", "Nessun soccorritore impegnato in un'emergenza di priorità inferiore trovato");
    }
    return best;
}

// Prova ad allocare i soccorritori per un'emergenza non preemptata
static bool try_allocate_rescuers(state_t* state, emergency_record_t* record){
    if(!state || !record) return false; // Errore nei parametri

    LOG_SYSTEM("status", "Tentativo di allocazione soccorritori per emergenza: %s", record->emergency.type.emergency_name);
    
    int total_rescuers_needed = record->emergency.rescuers_count;

    // Alloca la memoria necessaria per l'array di soccorritori richiesti dall'emergenza
    ensure_capacity((void***)&record->assigned_rescuers, &record->assigned_rescuers_count + 1, total_rescuers_needed);

    record->assigned_rescuers_count = 0;
    for (size_t i = 0; i < total_rescuers_needed; i++) {
        rescuer_request_t req = record->emergency.type.rescuer_requests[i];
        for (size_t j = 0; j < req.required_count; j++) {
            rescuer_digital_twin_t* best_rescuer = find_best_idle_rescuer(state, record);
            if (best_rescuer != NULL) {
                int idx = find_idx((void**)state->rescuer_available, state->rescuer_available_count, best_rescuer);
                // copia il gemello digitale nell'array degli assegnati
                if (idx < (size_t)record->emergency.rescuers_count) {
                    record->assigned_rescuers[idx++] = *best_rescuer;
                    record->assigned_rescuers[idx-1].status = EN_ROUTE_TO_SCENE;
                    record->assigned_rescuers_count++;

                    state->rescuer_available_count--;
                    state->rescuers_in_use[state->rescuers_in_use_count++] = best_rescuer;
                }
            } else if(record->emergency.type.priority != 0) {
                // Non è stato possibile trovare un soccorritore tra quelli IDLE
                // Ricerca del soccorritore tra le emergenze di priorità più bassa
                best_rescuer = find_best_rescuer_lower_priority(state, record);
                if (best_rescuer != NULL) {
                    int idx = find_idx((void**)state->rescuer_available, state->rescuer_available_count, best_rescuer);
                    // copia il gemello digitale nell'array degli assegnati
                    if (idx < (size_t)record->emergency.rescuers_count) {
                        record->assigned_rescuers[idx++] = *best_rescuer;
                        record->assigned_rescuers[idx-1].status = EN_ROUTE_TO_SCENE;
                        record->assigned_rescuers_count++;

                        state->rescuer_available_count--;
                        state->rescuers_in_use[state->rescuers_in_use_count++] = best_rescuer;
                    }
                } else {
                    // Non è stato possibile trovare un soccorritore tra quelli di priorità più bassa
                    // Rilascia i soccorritori già assegnati per questa emergenza
                    for (size_t k = 0; k < record->assigned_rescuers_count; k++) {
                        record->assigned_rescuers[k].status = IDLE;
                    }
                    free(record->assigned_rescuers);
                    record->assigned_rescuers = NULL;
                    record->assigned_rescuers_count = 0;
                    // Rimette l'emergenza nella coda di attesa
                    insert_into_general_queue((void***)&state->emergencies_waiting, 
                                           (size_t*)&state->emergencies_waiting_count, 
                                           (size_t*)&state->emergencies_waiting_capacity, 
                                           (void*)&record->emergency);
                    LOG_SYSTEM("status", "Allocazione soccorritori per emergenza %s fallita", record->emergency.type.emergency_name);
                    return false; // Allocazione fallita
                }
            }
        }
    }
    LOG_SYSTEM("status", "Allocazione soccorritori per emergenza %s riuscita", record->emergency.type.emergency_name);
    return true;
}

// Prova ad allocare i soccorritori per un'emergenza preemptata
static bool try_allocate_rescuers_for_preempted(state_t* state, emergency_record_t* record){
    if(!state || !record) return false; // Errore nei parametri
    if(!record->preempted) return false; // L'emergenza non è preemptata
    
    LOG_SYSTEM("status", "Tentativo di allocazione soccorritori per emergenza preemptata: %s", record->emergency.type.emergency_name);

    int total_rescuers_needed = record->emergency.rescuers_count;
    if(record->assigned_rescuers_count == total_rescuers_needed){
        LOG_SYSTEM("status", "Tutti i soccorritori già assegnati per l'emergenza preemptata: %s", record->emergency.type.emergency_name);
        // Tutti i soccorritori sono già assegnati
        record->preempted = false;
        LOG_SYSTEM("status", "Rimozione del flag di preemption per l'emergenza: %s", record->emergency.type.emergency_name);
        return true;
    }

    for (size_t i = 0; i < total_rescuers_needed; i++) {
        rescuer_request_t req = record->emergency.type.rescuer_requests[i];
        for (size_t j = 0; j < req.required_count; j++) {
            // Controlla se il soccorritore è già assegnato
            bool already_assigned = false;
            for(size_t k = 0; k < record->assigned_rescuers_count; ++k){
                if(strcmp(record->assigned_rescuers[k].type->rescuer_type_name, req.type->rescuer_type_name) == 0){
                    already_assigned = true;
                    break;
                }
            }
            if(already_assigned) continue; // Passa al prossimo soccorritore

            rescuer_digital_twin_t* best_rescuer = find_best_idle_rescuer(state, record);
            if (best_rescuer != NULL) {
                int idx = find_idx((void**)state->rescuer_available, state->rescuer_available_count, best_rescuer);
                // copia il gemello digitale nell'array degli assegnati
                if (idx < (size_t)record->emergency.rescuers_count) {
                    record->assigned_rescuers[idx++] = *best_rescuer;
                    record->assigned_rescuers[idx-1].status = EN_ROUTE_TO_SCENE;
                    record->assigned_rescuers_count++;

                    state->rescuer_available_count--;
                    state->rescuers_in_use[state->rescuers_in_use_count++] = best_rescuer;
                }
            } else {
                best_rescuer = find_best_rescuer_lower_priority(state, record);
                if (best_rescuer != NULL) {
                    int idx = find_idx((void**)state->rescuer_available, state->rescuer_available_count, best_rescuer);
                    // copia il gemello digitale nell'array degli assegnati
                    if (idx < (size_t)record->emergency.rescuers_count) {
                        record->assigned_rescuers[idx++] = *best_rescuer;
                        record->assigned_rescuers[idx-1].status = EN_ROUTE_TO_SCENE;
                        record->assigned_rescuers_count++;

                        state->rescuer_available_count--;
                        state->rescuers_in_use[state->rescuers_in_use_count++] = best_rescuer;
                    }
                } else {
                    LOG_SYSTEM("status", "Allocazione soccorritori per emergenza preemptata %s fallita", record->emergency.type.emergency_name);
                    return false; // Non è stato possibile trovare un soccorritore disponibile
                }
            }
        }
    }
    LOG_SYSTEM("status", "Allocazione soccorritori per emergenza preemptata %s riuscita", record->emergency.type.emergency_name);
    record->preempted = false;
    return true;
}

// Inizia la gestione di un'emergenza
static bool start_emergency_management(state_t* state, emergency_record_t* record){
    if(!state || !record) return false; // Errore nei parametri
    LOG_SYSTEM("status", "Inizio della gestione dell'emergenza: %s", record->emergency.type.emergency_name);
    record->starting_time = (unsigned int)time(NULL);
    record->emergency.status = ASSIGNED;
    // Rimuove l'emergenza dalla coda di attesa
    size_t idx = find_idx((void**)state->emergencies_waiting, state->emergencies_waiting_count, (void*)&record->emergency);
    if(idx == (size_t)-1){
        return false; // Emergenza non trovata nell'array delle emergenze in attesa
    }
    remove_emergency_from_general_queue((void**)state->emergencies_waiting, 
                              &state->emergencies_waiting_count, 
                              idx);
    // Inserisce l'emergenza tra quelle in corso
    insert_into_general_queue((void***)&state->emergencies_in_progress, 
                               (size_t*)&state->emergencies_in_progress_count, 
                               (size_t*)&state->emergencies_in_progress_capacity, 
                               (void*)&record->emergency);
    LOG_SYSTEM("status", "Gestione dell'emergenza %s iniziata correttamente", record->emergency.type.emergency_name);
    return true;
}

// Calcola il tempo massimo per arrivare sulla scena dell'emergenza
static unsigned int highest_time_to_scene(state_t* state, emergency_record_t* record){
    if(!record) return 0; // Errore nei parametri
    LOG_SYSTEM("status", "Calcolo del tempo massimo per arrivare sulla scena dell'emergenza: %s", record->emergency.type.emergency_name);
    unsigned int max_time = 0;
    for(size_t i = 0; i < record->assigned_rescuers_count; ++i){
        rescuer_digital_twin_t* rescuer = &record->assigned_rescuers[i];
        int distance = 0;
        if( rescuer->status == IDLE) distance = manhattan_distance(rescuer->x, rescuer->y, record->emergency.x, record->emergency.y);
        else { // Il soccorritore è impegnato in un'emergenza
            int est_x, est_y;
            emergency_t* rescuer_emergency = find_emergency_by_rescuer(rescuer, state->emergencies_in_progress, state->emergencies_in_progress_count);
            estimate_rescuer_position(rescuer, rescuer_emergency, &est_x, &est_y);
            distance = manhattan_distance(est_x, est_y, record->emergency.x, record->emergency.y);
        }
        int speed = rescuer->type->speed > 0 ? rescuer->type->speed : 1; // Garantisce che non ci siano velocità nulle o negative
        time_t time_to_scene = (distance + speed - 1) / speed; // Calcola il tempo stimato per arrivare sulla scena approssimando per eccesso
        if(time_to_scene > max_time){
            max_time = time_to_scene;
        }
    }
    LOG_SYSTEM("status", "Tempo massimo per arrivare sulla scena dell'emergenza %s calcolato: %u", record->emergency.type.emergency_name, max_time);
    return max_time;
}

// Verifica se tutti i soccorritori richiesti da un'emergenza sono ancora assegnati
static bool check_all_rescuers_still_assigned(emergency_record_t* record){
    if(!record) return false; // Errore nei parametri
    return record->assigned_rescuers_count == record->emergency.rescuers_count;
}

// Incrementa il timeout di un'emergenza e verifica se ha raggiunto la soglia
static void increment_emergency_timeout(emergency_record_t* record){
    if(!record) return; // Errore nei parametri
    if(record->emergency.status == IN_PROGRESS) return; // Non incrementa il timeout se l'emergenza è in corso
    LOG_SYSTEM("status", "Incremento del timeout per l'emergenza: %s", record->emergency.type.emergency_name);
    record->timeout++;
    const unsigned int TIMEOUT_THRESHOLD = (unsigned int)(record->emergency.type.priority == 2 ? 10 : (record->emergency.type.priority == 1 ? 30 : UINT_MAX));
    if(record->timeout >= TIMEOUT_THRESHOLD){
        LOG_SYSTEM("status", "Timeout raggiunto per l'emergenza: %s", record->emergency.type.emergency_name);
        record->emergency.status = TIMEOUT;
    }
}

// Restituisce l'emergenza da risolvere con la priorità più alta e la sposta nelle emergenze in corso
static emergency_record_t* get_highest_priority_emergency(state_t* state){
    if(!state) return NULL; // Errore nei parametri
    LOG_SYSTEM("status", "Ricerca dell'emergenza da risolvere con la priorità più alta");
    emergency_record_t* emergency = NULL;
    emergency_record_t* highest = NULL;
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
    remove_emergency_from_general_queue((void**)state->emergencies_waiting, 
                              &state->emergencies_waiting_count, 
                              idx);
    insert_into_general_queue((void***)&state->emergencies_in_progress, 
                              (size_t*)&state->emergencies_in_progress_count, 
                              (size_t*)&state->emergencies_in_progress_capacity, 
                              (void*)highest);
    LOG_SYSTEM("status", "Emergenza da risolvere con la priorità più alta trovata: %s, priorità %d", highest->emergency.type.emergency_name, highest->current_priority);
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
        state->rescuer_available = calloc(rescuer_twins_count, sizeof(rescuer_digital_twin_t)); 
        if(!state->rescuer_available) { // Errore di allocazione
            LOG_SYSTEM("status", "Errore di allocazione per l'array dei soccorritori disponibili");
            pthread_cond_destroy(&state->rescuer_available_cond);
            pthread_cond_destroy(&state->emergency_available_cond);
            pthread_mutex_destroy(&state->mutex);
            return -1;
        }

        for (size_t i = 0; i < rescuer_twins_count; ++i) {
            state->rescuer_available[i] = &rescuer_twins[i];
            state->rescuer_available[i]->status = IDLE;
        }
        state->rescuer_available_count = rescuer_twins_count;
        LOG_SYSTEM("status", "Array dei soccorritori disponibili inizializzato con successo");
        return 0;
    }
    return -1;
}

// Distrugge lo stato dell'applicazione
void status_destroy(state_t* state, mq_consumer_t* consumer) {
    if(!state) {
        return; // Stato non valido
    }
    LOG_SYSTEM("status", "Distruzione dello stato");

    // Chiudi la message queue
    LOG_SYSTEM("status", "Chiusura della message queue");
    shutdown_mq(consumer);

    LOG_SYSTEM("status", "Chiusura del mutex e delle condition variable");
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

// Assegna una nuova richiesta di emergenza
int status_add_waiting(state_t* state, emergency_request_t* request, emergency_type_t* emergency_types, size_t emergency_types_count){
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
    emergency_type_t* type = find_emergency_type_by_name(request->emergency_name, emergency_types);
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
    insert_into_general_queue((void***)&state->emergencies_waiting, 
                           (size_t*)&state->emergencies_waiting_count, 
                           (size_t*)&state->emergencies_waiting_capacity, 
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

// Thread worker per la gestione delle emergenze
void* worker_thread(void* arg){
    state_t* state = (state_t*)arg;
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
            if(!try_allocate_rescuers_for_preempted(state, record)){
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
            remove_emergency_from_general_queue((void**)state->emergencies_paused, 
                                      &state->emergencies_paused_count, 
                                      find_idx((void**)state->emergencies_paused, state->emergencies_paused_count, (void*)&record->emergency));
            insert_into_general_queue((void***)&state->emergencies_in_progress, 
                                   (size_t*)&state->emergencies_in_progress_count, 
                                   (size_t*)&state->emergencies_in_progress_capacity, 
                                   (void*)&record->emergency);
        } else {
            while(!state->shutdown_flag && state->emergencies_waiting_count == 0){
                // Attesa di una nuova emergenza
                pthread_cond_wait(&state->emergency_available_cond, &state->mutex);
                continue;
            }
            record = get_highest_priority_emergency(state);
            if(!record){
                // Nessun record di emergenza disponibile
                pthread_mutex_unlock(&state->mutex);
                continue;
            }
            if(!try_allocate_rescuers(state, record)){
                pthread_mutex_lock(&state->mutex);
                insert_into_general_queue((void***)&state->emergencies_waiting, 
                                    (size_t*)&state->emergencies_waiting_count, 
                                    (size_t*)&state->emergencies_waiting_capacity, 
                                    (void*)&record->emergency);
                pthread_mutex_unlock(&state->mutex);
                continue;
            }
            if(!start_emergency_management(state, record) && !record->preempted){
                for(size_t i = 0; i < record->assigned_rescuers_count; ++i){
                    record->assigned_rescuers[i].status = IDLE;
                }
                free(record->assigned_rescuers);
                record->assigned_rescuers = NULL;
                record->assigned_rescuers_count = 0;

                insert_into_general_queue((void***)&state->emergencies_waiting, 
                                    (size_t*)&state->emergencies_waiting_count, 
                                    (size_t*)&state->emergencies_waiting_capacity, 
                                    (void*)&record->emergency);
                pthread_mutex_unlock(&state->mutex);
                continue;
            }
        }

        pthread_mutex_unlock(&state->mutex);
        sleep(highest_time_to_scene(state, record)); // Simula l'arrivo dei soccorritori sulla scena

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
                record->assigned_rescuers[i].status = IDLE;
                state->rescuer_available[state->rescuer_available_count++] = &record->assigned_rescuers[i];
            }
            record->assigned_rescuers_count = 0;
            free(record->assigned_rescuers);
            record->assigned_rescuers = NULL;

            // Rimuove l'emergenza dall'array delle emergenze in corso
            size_t idx = find_idx((void**)state->emergencies_in_progress, state->emergencies_in_progress_count, (void*)&record->emergency);
            if(idx != (size_t)-1){
                remove_emergency_from_general_queue((void**)state->emergencies_in_progress, 
                                          (size_t*)&state->emergencies_in_progress_count, 
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

// Thread worker per la gestione del timeout delle emergenze
void* timeout_thread(void* arg){
    state_t* state = (state_t*)arg;
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
            record->current_priority = (float)record->emergency.type.priority + (float)(cbrt(((float)(record->timeout/9))));
        }
        for(size_t i = 0; i < state->emergencies_waiting_count; ++i){
            emergency_record_t* record = state->emergencies_waiting[i];
            increment_emergency_timeout(record);
            // Aggiorna la priorità corrente in base alla formula: priorità_base + radice cubica di (timeout/9)
            record->current_priority = (float)record->emergency.type.priority + (float)(cbrt(((float)(record->timeout/9)))); 
            if(record->emergency.status == TIMEOUT){
                timeout_emergency(state, &record->emergency);
                // Rimuove l'emergenza dall'array delle emergenze in attesa
                size_t idx = find_idx((void**)state->emergencies_waiting, state->emergencies_waiting_count, (void*)&record->emergency);
                if(idx != (size_t)-1){
                    remove_emergency_from_general_queue((void**)state->emergencies_waiting, 
                                              (size_t*)&state->emergencies_waiting_count, 
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