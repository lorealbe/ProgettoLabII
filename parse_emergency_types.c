#define _GNU_SOURCE
#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include "emergency_types.h"
#include "rescuers.h"

// Includi o copia qui la funzione 'find_rescuer_type_by_name'
static rescuer_type_t* find_rescuer_type_by_name(const char* name, rescuer_type_t* types_list) {
    if (!name || !types_list) return NULL;
    for (size_t i = 0; types_list[i].rescuer_type_name; i++) {
        if (strcmp(types_list[i].rescuer_type_name, name) == 0) {
            return &types_list[i];
        }
    }
    return NULL;
}


/**
 * Parsing in 2 passate per i tipi di emergenza.
 *
 * @param path File da parsare
 * @param out_emergency_types Puntatore doppio per l'array di output (terminato con NULL)
 * @param all_rescuer_types L'array (già parsato) di tipi di soccorritori 
 * per collegare le richieste.
 */
int parse_emergency_type(const char* path, 
                         emergency_type_t** out_emergency_types, 
                         rescuer_type_t* all_rescuer_types) 
{
    FILE* file = fopen(path, "r");
    if (!file) {
        perror("Errore nell'apertura del file");
        exit(1);
    }

    char* line = NULL;
    size_t len = 0;
    
    size_t emergency_count = 0;
    // Array che memorizza quanti sub-request ha ogni emergenza
    size_t* request_counts_per_emergency = NULL;

    // -----------------------------------------------------------------
    // --- PASSATA 1: Contare ---
    // -----------------------------------------------------------------
    while (getline(&line, &len, file) != -1) {
        char* saveptr_line;
        char* tok_name = strtok_r(line, "][", &saveptr_line);
        char* tok_priority = strtok_r(NULL, "][\n", &saveptr_line);
        if(tok_name && tok_priority) {
            // Trovata un'emergenza valida, rialloca l'array dei contatori
            request_counts_per_emergency = realloc(request_counts_per_emergency, (emergency_count + 1) * sizeof(size_t));
            if (!request_counts_per_emergency) {
                perror("Errore realloc contatori pass 1"); exit(1);
            }
            
            size_t current_request_count = 0;
            
            // Loop per contare le sub-richieste
            char* saveptr_req;
            char* tok_name_resc = strtok_r(saveptr_line, ":;", &saveptr_req); // Cerca il primo nome
            
            while(tok_name_resc) {
                char* tok_required_count = strtok_r(NULL, ",", &saveptr_req);
                char* tok_time_to_manage = strtok_r(NULL, ";", &saveptr_req);

                if (tok_required_count && tok_time_to_manage) {
                    current_request_count++;
                }
                
                // Cerca il prossimo nome (dopo il ';') o esci se finita
                tok_name_resc = strtok_r(NULL, ":;", &saveptr_req);
            }
            
            request_counts_per_emergency[emergency_count] = current_request_count;
            emergency_count++;
        }
    }
    
    if (emergency_count == 0) { /* File vuoto o malformato */ }

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

        printf("DEBUG: Parsing Emergency Type: '%s' with priority '%s'\n", tok_name ? tok_name : "NULL", tok_priority ? tok_priority : "NULL");

        if(tok_name && tok_priority) {
            // Prendi il puntatore all'emergenza corrente
            emergency_type_t* current_emergency = &((*out_emergency_types)[current_emergency_idx]);

            current_emergency->emergency_name = strdup(tok_name);
            current_emergency->priority = atoi(tok_priority);

            
            size_t num_requests = request_counts_per_emergency[current_emergency_idx];
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
    free(request_counts_per_emergency); // Libera l'array dei contatori
    fclose(file);
    
    return emergency_count;
}