#pragma once   
#include <stddef.h>
#include "../Types/emergency_types.h"
#include "../Types/rescuers.h"

int parse_emergency_type(const char* path, emergency_type_t** emergency_types, rescuer_type_t* all_rescuer_types);