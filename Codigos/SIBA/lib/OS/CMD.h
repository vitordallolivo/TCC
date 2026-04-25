#ifndef COMMANDS_H
#define COMMANDS_H

#include "Common_Types.h"
#include <stdint.h>

/* --- Auto-generated Struct: CMD --- */
typedef struct __attribute__((packed)) {

    func_type function;

    instruct_type instruction;

    int target_id;

    float value;

    status_type status;

} CMD;

#endif // COMMANDS_H