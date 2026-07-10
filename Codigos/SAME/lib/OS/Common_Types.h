#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <stdint.h>

/* Auto-generated — edite SAME.yaml, não este arquivo */


typedef enum __attribute__((packed)) {

    FUNC_START,


    FUNC_STOP,


    FUNC_RESET,


    FUNC_SENSOR


} func_type;


typedef enum __attribute__((packed)) {

    INSTR_READ,


    INSTR_WRITE,


    INSTR_NONE,


    INSTR_DEFAULT


} instruct_type;


typedef enum __attribute__((packed)) {

    TARGET_CURRENT_A,


    TARGET_CURRENT_B,


    TARGET_CURRENT_C,


    TARGET_VOLTAGE_AB,


    TARGET_VOLTAGE_BC,


    TARGET_POLES_PAIRS,


    TARGET_RESISTANCE,


    TARGET_INDUCTANCE,


    TARGET_KV_RATING,


    TARGET_GAMMA1,


    TARGET_GAMMA2,


    TARGET_GAMMAT,


    TARGET_BEMF_LPF_FREQUENCY,


    TARGET_TORQUE_LPF_FREQUENCY,


    TARGET_TS_ADC,


    TARGET_TS_UDP,


    TARGET_DEFAULT


} target_type;


typedef enum {

    DEFAULT_ACTION_NONE,


    DEFAULT_ACTION_SENSE_LINEAR_COEF,


    DEFAULT_ACTION_SENSE_OFFSET_COEF,


    DEFAULT_ACTION_SENSE_RESET_DEFAULT,


    DEFAULT_ACTION_SENSE_MAX_VALID,


    DEFAULT_ACTION_SENSE_MIN_VALID,


    DEFAULT_ACTION_SET_VALUE,


    DEFAULT_ACTION_VALUE_RESET_DEFAULT


} default_action;


typedef enum {

    LOG_VALID,


    LOG_INVALID,


    LOG_OVERFLOW,


    LOG_CRITICAL_ALARM,


    LOG_DENIED_ACTION


} log_status;


typedef enum {

    STATUS_OK,


    STATUS_ERROR,


    STATUS_NONE


} status_type;


#endif /* COMMON_TYPES_H */