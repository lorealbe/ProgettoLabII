#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "logging.h"

#define QUEUE_NAME "/emergenze676878"

int main(int argc, char *argv[]) {

    if (argc == 2 && strcmp(argv[1], "exit") == 0) {
        mqd_t mq = mq_open(QUEUE_NAME, O_WRONLY);
        if (mq == (mqd_t)-1) {
            perror("Errore nell'apertura della coda");
            exit(1);
        }
        
        // Invia il messaggio "exit" (lunghezza 5 per includere \0)
        if (mq_send(mq, "exit", 5, 0) == -1) {
            perror("Errore nell'invio del messaggio di exit");
            mq_close(mq);
            exit(1);
        }

        printf("Comando di exit inviato al server.\n");
        mq_close(mq);
        return 0;
    }
    
    // Controllo dei parametri
    if (!((argc == 5) || (argc == 3 && strcmp(argv[1], "-f") == 0))) {
        fprintf(stderr, "Uso: %s <nomeEmergenza> <x> <y> <delay>\nOppure\n%s -f <file>\n", argv[0], argv[0]);
        exit(1);
    }

    if(argc == 5) {
        // Inserimento diretto dei parametri passati nella coda
        char *nomeEmergenza = argv[1];
        int x = atoi(argv[2]);
        int y = atoi(argv[3]);
        int delay = atoi(argv[4]);

        char messaggio[256];
        snprintf(messaggio, sizeof(messaggio), "%s %d %d %ld", nomeEmergenza, x, y, time(NULL));

        // Attesa del delay specificato
        sleep(delay);

        mqd_t mq = mq_open(QUEUE_NAME, O_WRONLY);
        if (mq == (mqd_t)-1) {
            perror("Errore nell'apertura della coda");
            exit(1);
        }
        
        // Invia il messaggio alla coda
        if (mq_send(mq, messaggio, strlen(messaggio) + 1, 0) == -1) {
            perror("Errore nell'invio del messaggio alla coda");
            mq_close(mq);
            exit(1);
        }

        printf("Messaggio inviato: %s\n", messaggio);
        mq_close(mq);

    } else if(argc == 3 && strcmp(argv[1], "-f") == 0) {
        // Lettura dei parametri da file

        char *filename = argv[2];
        FILE *file = fopen(filename, "r");
        if (!file) {
            perror("Errore nell'apertura del file");
            exit(1);
        }

        char line[256];
        char messaggio[256];

        // Apertura della coda
        mqd_t mq = mq_open(QUEUE_NAME, O_WRONLY);
        if (mq == (mqd_t)-1) {
            perror("Errore nell'apertura della coda");
            fclose(file);
            exit(1);
        }
        while (fgets(line, sizeof(line), file)) {
            line[strcspn(line, "\n")] = '\0';

            // Estrazione dei dati dalla riga
            char nomeEmergenza[128];
            int x, y, delay;
            if (sscanf(line, "%127s %d %d %d", nomeEmergenza, &x, &y, &delay) != 4) {
                fprintf(stderr, "Formato riga non valido: %s\n", line);
                continue;
            }

            // Attesa del delay specificato
            sleep(delay);

            snprintf(messaggio, sizeof(messaggio), "%s %d %d %ld", nomeEmergenza, x, y, time(NULL));

            // Invia il messaggio alla coda
            if (mq_send(mq, messaggio, strlen(messaggio) + 1, 0) == -1) {
                perror("Errore nell'invio del messaggio alla coda");
                mq_close(mq);
                fclose(file);
                exit(1);
            }
            printf("Messaggio inviato: %s\n", messaggio);
        }
        mq_close(mq);
        fclose(file);
    }
    return 0;
}