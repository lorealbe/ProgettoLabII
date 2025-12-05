#pragma once
#include <stddef.h>
#include "rescuers.h"


#define EMERGENCY_NAME_LENGTH 64


typedef struct rescuer_request_t {
    rescuer_type_t* type;
    int required_count;
    int time_to_manage; // in sec
} rescuer_request_t;

typedef struct emergency_type_t  {
    short priority; // 0 (low) to 2 (high)
    char* emergency_name;
    rescuer_request_t* rescuer_requests;
    int rescuers_req_number;
} emergency_type_t;


// Tipi per la gestione delle emergenze


typedef enum emergency_status_t {
    WAITING,
    ASSIGNED,
    IN_PROGRESS,
    PAUSED,
    COMPLETED,
    CANCELED,
    TIMEOUT
} emergency_status_t;

typedef struct emergency_request_t {
    char emergency_name[EMERGENCY_NAME_LENGTH];
    int x;
    int y;
    time_t timestamp;
} emergency_request_t;

typedef struct emergency_t {
    emergency_type_t type;
    emergency_status_t status;
    int x;
    int y;
    time_t time;
    int rescuers_count;
    rescuer_digital_twin_t* assigned_rescuers;
} emergency_t;

emergency_type_t* find_emergency_type_by_name(const char* name, emergency_type_t* emergency_types); 
