#include "rescuers.h"
#include <stdlib.h>
#include <string.h>
#include "emergency_types.h"
#include "../logging.h"
#include "../src/runtime/status.h"

// funzione per cercare un tipo di emergenza dato il suo nome
emergency_type_t* find_emergency_type_by_name(const char* name, emergency_type_t* emergency_types) {
    LOG_SYSTEM("emergency_types", "Ricerca del tipo di emergenza: %s", name);
    if (name == NULL || emergency_types == NULL) {
        LOG_SYSTEM("emergency_types", "Nome o lista di tipi di emergenza non validi");
        return NULL;
    }

    for (size_t i = 0; emergency_types[i].emergency_name != NULL; i++) {
        if (strcmp(emergency_types[i].emergency_name, name) == 0) {
            LOG_SYSTEM("emergency_types", "Tipo di emergenza trovato: %s", name);
            return &emergency_types[i];
        }
    }
    LOG_SYSTEM("emergency_types", "Tipo di emergenza non trovato: %s", name);
    return NULL; // Non trovato
}

emergency_t* find_emergency_by_rescuer(rescuer_digital_twin_t* rescuer, emergency_record_t** emergency_array, int length){
    
}
