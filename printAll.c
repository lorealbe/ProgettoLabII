/*
#include <stdio.h>
#include <stdlib.h>
#include "parse_env.h" 
#include "parse_rescuers.h"
#include "parse_emergency_types.h"
#include "rescuers.h"
#include "emergency_types.h"

int main() {  
    /*
    environment_variable_t env_vars;
    parse_environment_variables("environment.txt", &env_vars);
    // printf("Environment Variables:\n");
    // printf("%-12s %-8s %-8s\n", "Queue", "Height", "Width");
    // printf("%-12s %-8d %-8d\n", env_vars.queue, env_vars.height, env_vars.width);
    

    rescuer_type_t* rescuer_types = NULL;
    rescuer_digital_twin_t* rescuer_twins = NULL;
    parse_rescuer_type("rescuers.txt", &rescuer_types, &rescuer_twins);
    printf("Parsed Rescuer Types:\n");
    printf("%-20s %-8s %-6s %-6s\n", "Rescuer Type", "Speed", "X", "Y");
    for (size_t i = 0; rescuer_types && rescuer_types[i].rescuer_type_name; i++) {
        printf("%-20s %-8d %-6d %-6d\n",
               rescuer_types[i].rescuer_type_name,
               rescuer_types[i].speed,
               rescuer_types[i].x,
               rescuer_types[i].y);
    }
    
    
    emergency_type_t* emergency_types = NULL;
    parse_emergency_type("emergency.txt", &emergency_types, rescuer_types); 
    
    printf("Parsed Emergency Types:\n");
    printf("%-22s %-8s %-20s %-16s %-14s\n", "EMERGENCY TYPE", "PRIORITY", "RESCUER TYPE", "REQUIRED COUNT", "TIME TO MANAGE");
    printf("%-22s %-8s %-20s %-16s %-14s\n", "--------------", "--------", "--------------------", "----------------", "--------------");
    for (size_t i = 0; emergency_types && emergency_types[i].emergency_name != NULL; i++) {
        int first = 1;
        for (int j = 0; j < emergency_types[i].rescuers_req_number; j++) {
            printf("%-22s %-8d %-20s %-16d %-14d\n",
                   first ? emergency_types[i].emergency_name : "",
                   first ? emergency_types[i].priority : 0,
                   emergency_types[i].rescuer_requests[j].type->rescuer_type_name,
                   emergency_types[i].rescuer_requests[j].required_count,
                   emergency_types[i].rescuer_requests[j].time_to_manage
            );
            first = 0;
        }
        if (emergency_types[i].rescuers_req_number == 0) {
            printf("%-22s %-8d %-20s %-16s %-14s\n",
                   emergency_types[i].emergency_name,
                   emergency_types[i].priority,
                   "-", "-", "-"
            );
        }
    }
    return 0;
}

*/