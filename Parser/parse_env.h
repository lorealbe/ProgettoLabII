#pragma once
#include <stddef.h>

typedef struct environment_variable_t {
    char* queue;
    int height;
    int width;
} environment_variable_t;


int parse_environment_variables(const char* path, environment_variable_t* env_vars);
int is_within_environment(int x, int y, const environment_variable_t* env_vars);