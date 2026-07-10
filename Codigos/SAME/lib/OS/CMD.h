#ifndef CMD_H
#define CMD_H

#include "Common_Types.h"
#include <stdint.h>

/* Auto-generated — edite SAME.yaml, não este arquivo */

typedef struct __attribute__((packed)) {

    func_type function;

    instruct_type instruction;

    target_type target_id;

    float value;

    int8_t action_or_status;

} CMD;

#endif /* CMD_H */