#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h> 
#include "../Types/parse_env.h"
#include <stdio.h>
#include "../logging.h"

int parse_environment_variables(const char* path, environment_variable_t* env_vars) {
    
    LOG_FILE_PARSING("PARSE-ENV", "Inizio parsing variabili d'ambiente da '%s'", path);

    FILE* file = fopen(path, "r");
    if (!file) {
        LOG_FILE_PARSING("PARSE-ENV-ERROR", "Errore apertura file '%s'", path);
        perror("Errore nell'apertura del file");
        return -1;
    }

    char* line = NULL;                                                                 // Puntatore per memorizzare la linea letta
    size_t len = 0;

    while (getline(&line, &len, file) != -1) {
        char* saveptr;
        char* tok_key = strtok_r(line, "=", &saveptr);
        char* tok_value = strtok_r(NULL, "\n", &saveptr);

        if (tok_key && tok_value) {
            if (strcmp(tok_key, "queue") == 0) {                                       // Assegna il nome della coda di messaggi
                env_vars->queue = strdup(tok_value);
            } else if (strcmp(tok_key, "height") == 0) {                               // Assegna l'altezza dell'ambiente
                env_vars->height = atoi(tok_value);
            } else if (strcmp(tok_key, "width") == 0) {                                // Assegna la larghezza dell'ambiente
                env_vars->width = atoi(tok_value);
            }
        }
    }
    if(env_vars->queue == NULL) {
        LOG_FILE_PARSING("PARSE-ENV-WARNING", "'queue' non trovata in '%s'", path);
    } else {
        LOG_FILE_PARSING("PARSE-ENV", "Aggiunta la 'queue' con nome '%s' e dimensioni (%d, %d)", env_vars->queue, env_vars->width, env_vars->height);
    }

    free(line);
    fclose(file);
    return 0;
}


// funzione che verifica se le coordinate appartengono all'ambiente
int is_within_environment(int x, int y, const environment_variable_t* env_vars) {
    LOG_SYSTEM("parse_env", "Verifica coordinate (%d, %d) nell'ambiente di dimensioni (%d, %d)", x, y, env_vars->width, env_vars->height);
    if (x < 0 || x >= env_vars->width || y < 0 || y >= env_vars->height) {
        return 0; // Fuori dai limiti
    }
    return 1; // Dentro i limiti
}