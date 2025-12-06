#define _GNU_SOURCE
#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include "../Types/emergency_types.h"
#include "../Types/rescuers.h"
#include "../logging.h"

// Cerca il rescuer dato il suo nome nella lista di digital twins
static rescuer_type_t* find_rescuer_type_by_name(const char* name, rescuer_type_t* types_list) {
    LOG_SYSTEM("parse_emergency_types", "Ricerca del tipo di soccorritore: %s", name);
    if (!name || !types_list) {
        LOG_SYSTEM("parse_emergency_types", "Nome o lista di tipi di soccorritore non validi");
        return NULL;
    }

    for (size_t i = 0; types_list[i].rescuer_type_name; i++) {
        if (strcmp(types_list[i].rescuer_type_name, name) == 0) {
            LOG_SYSTEM("parse_emergency_types", "Tipo di soccorritore trovato: %s", name);
            return &types_list[i];
        }
    }
    LOG_SYSTEM("parse_emergency_types", "Tipo di soccorritore non trovato: %s", name);
    return NULL;
}


int parse_emergency_type(const char* path, 
                         emergency_type_t** out_emergency_types, 
                         rescuer_type_t* all_rescuer_types) {

    LOG_FILE_PARSING("PARSE-EMERGENCY-TYPES", "Inizio parsing tipi di emergenza da '%s'", path);

    FILE* file = fopen(path, "r");
    if (!file) {
        LOG_FILE_PARSING("PARSE-EMERGENCY-TYPES-ERROR", "Errore apertura file '%s'", path);
        perror("Errore apertura file");
        return -1;
    }

    char* line = NULL;                                                              // puntatore alla linea letta
    size_t len = 0;                                                                 // lunghezza del buffer per getline
    
    size_t emergency_count = 0;                                                     // Numero di tipi di emergenze
    
    size_t* rescuers_required_for_emergency = NULL;                                 // Array che memorizza quanti soccorritori sono richiesti per ogni emergenza


    // -----------------------------------------------------------------
    // --- PASSATA 1: Conta  ---
    // -----------------------------------------------------------------
    while (getline(&line, &len, file) != -1) {
        char* saveptr_line;
        char* tok_name = strtok_r(line, "][", &saveptr_line);
        char* tok_priority = strtok_r(NULL, "][\n", &saveptr_line);
        if(tok_name && tok_priority) {
            // Trovata un'emergenza valida, rialloca l'array dei contatori
            rescuers_required_for_emergency = realloc(rescuers_required_for_emergency, (emergency_count + 1) * sizeof(size_t));
            if (!rescuers_required_for_emergency) {
                LOG_FILE_PARSING("PARSE-EMERGENCY-TYPES-ERROR", "Errore realloc memoria per le emergenze '%s'", path);
                perror("Errore realloc"); exit(1);
            }
            
            size_t current_request_count = 0;
            
            // Loop per contare le richieste di soccorritori
            char* saveptr_req;
            char* tok_name_resc = strtok_r(saveptr_line, ":;", &saveptr_req); // Cerca il primo nome
            
            while(tok_name_resc) {
                LOG_FILE_PARSING("PARSE-EMERGENCY-TYPES", "Trovata sub-richiesta '%s' per emergenza '%s'", tok_name_resc, tok_name);
                char* tok_required_count = strtok_r(NULL, ",", &saveptr_req);
                char* tok_time_to_manage = strtok_r(NULL, ";", &saveptr_req);

                if (tok_required_count && tok_time_to_manage) {
                    current_request_count++;
                }
                
                // Cerca il prossimo nome (dopo il ';') o esci se finita
                tok_name_resc = strtok_r(NULL, ":;", &saveptr_req);
            }
            
            rescuers_required_for_emergency[emergency_count] = current_request_count;
            emergency_count++;
        }
    }
    
    if (emergency_count == 0) { 
        LOG_FILE_PARSING("PARSE-EMERGENCY-TYPES-WARNING", "Nessun tipo di emergenza trovato in '%s'", path);
    }

    // -----------------------------------------------------------------
    // --- Allocazione Unica ---
    // -----------------------------------------------------------------
    
    // Alloca + 1 per il terminatore NULL (calloc azzera tutto)
    *out_emergency_types = calloc(emergency_count + 1, sizeof(emergency_type_t));
    if (!*out_emergency_types) {
        perror("Errore calloc emergency_types"); exit(1);
    }

    // -----------------------------------------------------------------
    // --- PASSATA 2: Popolare ---
    // -----------------------------------------------------------------
    rewind(file);
    size_t current_emergency_idx = 0;

    while (getline(&line, &len, file) != -1) {
        char* saveptr_line;
        char* tok_name = strtok_r(line, "][", &saveptr_line);
        char* tok_priority = strtok_r(NULL, "][", &saveptr_line);

        if(tok_name && tok_priority) {
            // Prendi il puntatore all'emergenza corrente
            emergency_type_t* current_emergency = &((*out_emergency_types)[current_emergency_idx]);

            current_emergency->emergency_name = strdup(tok_name);
            current_emergency->priority = atoi(tok_priority);

            
            size_t num_requests = rescuers_required_for_emergency[current_emergency_idx];
            current_emergency->rescuers_req_number = num_requests;


            if (num_requests > 0) {
                // Alloca spazio per le richieste di *questa* emergenza
                current_emergency->rescuer_requests = calloc(num_requests, sizeof(rescuer_request_t));
                if (!current_emergency->rescuer_requests) {
                    perror("Errore calloc rescuer_requests"); exit(1);
                }
                
                char* saveptr_req;
                char* tok_name_resc = strtok_r(saveptr_line, ":;", &saveptr_req);
                
                for (size_t i = 0; i < num_requests; i++) {
                    char* tok_required_count = strtok_r(NULL, ",", &saveptr_req);
                    char* tok_time_to_manage = strtok_r(NULL, ";", &saveptr_req);

                    rescuer_request_t* current_request = &(current_emergency->rescuer_requests[i]);

                    // --- COLLEGAMENTO CORRETTO ---
                    current_request->type = find_rescuer_type_by_name(tok_name_resc, all_rescuer_types);
                    
                    if (current_request->type == NULL) {
                        fprintf(stderr, "ATTENZIONE: Tipo soccorritore '%s' non trovato!\n", tok_name_resc);
                    }
                    
                    current_request->required_count = atoi(tok_required_count);
                    current_request->time_to_manage = atoi(tok_time_to_manage);

                    // Cerca il prossimo nome
                    tok_name_resc = strtok_r(NULL, ":;", &saveptr_req);
                }
            }
            // (Se num_requests == 0, rescuer_requests rimane NULL grazie a calloc)
            
            current_emergency_idx++;
        }
    }

    // -----------------------------------------------------------------
    // --- Pulizia ---
    // -----------------------------------------------------------------
    
    // Il terminatore (*out_emergency_types)[emergency_count].emergency_name
    // è già NULL grazie a calloc().

    free(line); // Libera il buffer di getline
    free(rescuers_required_for_emergency); // Libera l'array dei contatori
    fclose(file);
    
    return emergency_count;
}