#pragma once
#include <stddef.h>

typedef struct rescuer_type_t  {
    char* rescuer_type_name;
    int speed; // cells per second
    int x; 
    int y;
} rescuer_type_t;


typedef enum rescuer_status_t  {
    IDLE, EN_ROUTE_TO_SCENE, ON_SCENE, RETURNING_TO_BASE
} rescuer_status_t;

typedef struct rescuer_digital_twin_t {
    int id;
    int x;
    int y;
    rescuer_type_t* type;
    rescuer_status_t status;
} rescuer_digital_twin_t;
 