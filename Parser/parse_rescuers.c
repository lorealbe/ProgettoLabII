#define _GNU_SOURCE
#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include "../Types/emergency_types.h"
#include "../Types/rescuers.h"        
#include "../logging.h"

/**
 * Funzione di parsing in due passate.
 * 1. Legge il file per contare il numero di tipi e il numero totale di digital twin.
 * 2. Alloca tutta la memoria necessaria in un unico blocco.
 * 3. Rilegge il file per popolare gli array, stabilendo i collegamenti in sicurezza.
 */
int parse_rescuer_type(const char* path, rescuer_type_t** rescuer_types, rescuer_digital_twin_t** out_rescuer_twins) {
    
    FILE* file = fopen(path, "r");
    if (!file) {
        LOG_FILE_PARSING("PARSE-RESCUER-TYPES-ERROR", "Errore apertura file '%s'", path);
        perror("Errore nell'apertura del file");
        return -1;
    }

    char* line = NULL;                                                              // puntatore alla linea letta
    size_t len = 0;                                                                 // lunghezza del buffer per getline
    ssize_t read;                                                                   // valore di ritorno di getline
    
    size_t type_count = 0;                                                          // Numero di tipi di soccorritori
    size_t total_twin_count = 0;                                                    // Numero totale di gemelli digitali

    LOG_FILE_PARSING("PARSE-RESCUER-TYPES", "Inizio parsing tipi di soccorritori da '%s'", path);
    // -----------------------------------------------------------------
    // --- PASSATA 1: Conta gli elementi per l'allocazione ---
    // -----------------------------------------------------------------
    while ((read = getline(&line, &len, file)) != -1) {
        
        char* saveptr;
        char* tok_name = strtok_r(line, "][", &saveptr);                            // Nome del tipo di soccorritore
        char* tok_num = strtok_r(NULL, "][", &saveptr);                             // Numero di gemelli digitali
        char* tok_speed = strtok_r(NULL, "][", &saveptr);                           // Velocità del soccorritore
        char* tok_x = strtok_r(NULL, "[;", &saveptr);                               // Coordinata X del gemello digitale
        char* tok_y = strtok_r(NULL, "]\n", &saveptr);                              // Coordinata Y del gemello digitale
        
        // Contiamo solo le righe ben formate
        if(tok_name && tok_num && tok_speed && tok_x && tok_y) {
            LOG_FILE_PARSING("PARSE-RESCUER-TYPES", "Trovato tipo di soccorritore '%s' con %s gemelli da creare", tok_name, tok_num);
            type_count++;
            total_twin_count += atoi(tok_num);
        }
    }

    // Se il file è vuoto o malformato, esci
    if (type_count == 0) {
        LOG_FILE_PARSING("PARSE-RESCUER-TYPES-WARNING", "Nessun tipo di soccorritore trovato in '%s'", path);
        *rescuer_types = NULL;
        *out_rescuer_twins = NULL;
        free(line);
        fclose(file);
        return 0;
    }

    // ------------------------------------------------------------------------------
    // --- Allocazione Memoria per i tipi di soccorritori e gemelli digitali ---
    // ------------------------------------------------------------------------------
    LOG_FILE_PARSING("PARSE-RESCUER-TYPES", "Allocazione memoria per %zu tipi di soccorritori e %zu gemelli digitali", type_count, total_twin_count);
    
    // Alloca spazio per tutti i tipi + 1 per il terminatore NULL
    // Calloc serve per impostare a NULL tutti i campi iniziali, incluso il terminatore
    *rescuer_types = calloc(type_count + 1, sizeof(rescuer_type_t));
    if (!*rescuer_types) {
        LOG_FILE_PARSING("PARSE-RESCUER-TYPES-ERROR", "Errore di allocazione memoria per i tipi di soccorritori in '%s'", path);
        perror("Errore di allocazione per rescuer_types");
        free(line);
        fclose(file);
        return -1;
    }

    // Alloca spazio per il numero esatto di "gemelli" + 1 per il terminatore NULL
    *out_rescuer_twins = calloc(total_twin_count + 1, sizeof(rescuer_digital_twin_t));
    if (!*out_rescuer_twins) {
        LOG_FILE_PARSING("PARSE-RESCUER-TYPES-ERROR", "Errore di allocazione memoria per i gemelli digitali in '%s'", path);
        perror("Errore di allocazione per out_rescuer_twins");
        free(*rescuer_types); // Libera la memoria già allocata
        free(line);
        fclose(file);
        return -1;
    }
 
    // ------------------------------------------------------------------------------
    // --- PASSATA 2: Popola gli array dei soccorritori e dei gemelli digitali ---
    // ------------------------------------------------------------------------------
    
    LOG_FILE_PARSING("PARSE-RESCUER-TYPES", "Inizio popolamento array tipi di soccorritori e gemelli digitali da '%s'", path);

    rewind(file); // Torna all'inizio del file
 
    size_t current_type_idx = 0; // Indice corrente per i tipi di soccorritori
    size_t current_twin_idx = 0; // Indice corrente per i gemelli digitali
 
    while ((read = getline(&line, &len, file)) != -1) {
        char* saveptr;
        char* tok_name = strtok_r(line, "][", &saveptr);                           // Nome del tipo di soccorritore
        char* tok_num = strtok_r(NULL, "][", &saveptr);                            // Numero di gemelli digitali da creare
        char* tok_speed = strtok_r(NULL, "][", &saveptr);                          // Velocità del soccorritore
        char* tok_x = strtok_r(NULL, "[;", &saveptr);                              // Coordinata X della base
        char* tok_y = strtok_r(NULL, "]\n", &saveptr);                             // Coordinata Y della base
         
        if(tok_name && tok_num && tok_speed && tok_x && tok_y) {
            LOG_FILE_PARSING("PARSE-RESCUER-TYPES", "Popolamento tipo di soccorritore '%s' con %s gemelli", tok_name, tok_num);
             
             // Ottieni un puntatore alla struct tipo corrente (già allocata)
             rescuer_type_t *current_type_ptr = &((*rescuer_types)[current_type_idx]);
             current_type_ptr->rescuer_type_name = strdup(tok_name);
             current_type_ptr->speed = atoi(tok_speed);
             current_type_ptr->x = atoi(tok_x);
             current_type_ptr->y = atoi(tok_y);
             
             // Gestisci i "gemelli"
             int num_twins_for_this_rescuer = atoi(tok_num);
             for (int i = 0; i < num_twins_for_this_rescuer; i++) {
                 
                 // Salva il puntatore al gemello corrente 
                 rescuer_digital_twin_t *current_twin_ptr = &((*out_rescuer_twins)[current_twin_idx]);
                 current_twin_ptr->id = current_twin_idx + 1; // ID univoco (1-based)
                 current_twin_ptr->x = current_type_ptr->x;
                 current_twin_ptr->y = current_type_ptr->y;
                 current_twin_ptr->status = IDLE;
                 
                 current_twin_ptr->type = current_type_ptr;
                 
                 current_twin_idx++; 
             }
             
             current_type_idx++; 
         }
     }
 
    // Imposta il terminatore per il campo .type dei gemelli digitali a NULL
    if (current_twin_idx <= total_twin_count) {
        (*out_rescuer_twins)[current_twin_idx].type = NULL;
    }
     LOG_FILE_PARSING("PARSE-RESCUER-TYPES", "Popolamento completato con successo");
     // -----------------------------------------------------------------
     // --- Pulizia ---
     // -----------------------------------------------------------------
 
     free(line); // Libera la memoria allocata da getline
     fclose(file);
     
     return total_twin_count; // Restituisce il numero di digital twin creati
}