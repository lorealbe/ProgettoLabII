#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_ENTRIES 1000
#define MAX_NAME 32
#define NAMES_COUNT 12

const char *rescuer_names[NAMES_COUNT] = {
    "Pompieri",
    "Ambulanza",
    "Polizia",
    "VigiliUrbani",
    "GuardiaCostiera",
    "SoccorsoAlpino",
    "CroceRossa",
    "GDF",
    "Elicottero",
    "Misericordia",
    "RescueDog",
    "ProtezioneCivile"
};

// For each name, maintain a speed (keeps same speed for all entries of that name)
int name_speeds[NAMES_COUNT] = {0};

// Helper: assign or retrieve the speed for a given name index
int get_or_assign_speed(int idx) {
    if (name_speeds[idx] == 0) {
        // Assign random speed 1..30 and remember it
        name_speeds[idx] = (rand() % 30) + 1;
    }
    return name_speeds[idx];
}

int main() {
    FILE *fout = fopen("rescuer.txt", "w");
    if (!fout) {
        perror("rescuer.txt");
        return 1;
    }

    srand((unsigned int)time(NULL));
    int counter = 0;
    // To avoid all pairs being duplicates use a safety for generated coordinates
    int used_coords[MAX_ENTRIES][2];
    int used_count = 0;

    while (counter < MAX_ENTRIES) {
        int idx = rand() % NAMES_COUNT;
        const char* name = rescuer_names[idx];
        int speed = get_or_assign_speed(idx); // same for all of same name
        int number = (rand() % 26) + 5; // [5,30]
        int x, y, unique;
        // Ensure the pair x,y is unique for this name+speed
        do {
            x = rand() % 501;
            y = rand() % 501;
            unique = 1;
            for (int i = 0; i < used_count; ++i) {
                if (used_coords[i][0] == x && used_coords[i][1] == y) {
                    unique = 0;
                    break;
                }
            }
        } while (!unique);
        used_coords[used_count][0] = x;
        used_coords[used_count][1] = y;
        used_count++;

        fprintf(fout, "[%s][%d][%d][%d;%d]\n", name, number, speed, x, y);
        ++counter;
    }

    fclose(fout);
    printf("File rescuer.txt generato con successo (%d entries)\n", counter);
    printf("Nomi usati:\n");
    for (int j = 0; j < NAMES_COUNT; ++j)
        printf("- %s (speed=%d)\n", rescuer_names[j], name_speeds[j]);
    return 0;
}