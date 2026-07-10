#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <stdint.h>

/* Auto-generated — edite SIBA.yaml, não este arquivo */


typedef enum __attribute__((packed)) {

    FUNC_START,


    FUNC_STOP,


    FUNC_RESET,


    FUNC_SENSOR,


    FUNC_ESC,


    FUNC_BATTERY


} func_type;


typedef enum __attribute__((packed)) {

    INSTR_READ,


    INSTR_WRITE,


    INSTR_NONE,


    INSTR_DEFAULT


} instruct_type;


typedef enum {

    TARGET_LOAD_CELL_0_enum,


    TARGET_LOAD_CELL_1_enum,


    TARGET_LOAD_CELL_2_enum,


    TARGET_LOAD_CELL_3_enum,


    TARGET_LOAD_CELL_4_enum,


    TARGET_LOAD_CELL_5_enum,


    TARGET_VOLTAGE_DIVIDOR,


    TARGET_CURRENT_SENSOR,


    TARGET_BATTERY_SOC,


    TARGET_TEMPERATURE_SENSOR,


    TARGET_PITOT_TUBE,


    TARGET_DEFAULT


} target_type;


typedef enum {

    STATUS_OK,


    STATUS_ERROR,


    STATUS_NONE


} status_type;


typedef enum {

    DEFAULT_ACTION_NONE,


    DEFAULT_ACTION_SENSE_LINEAR_COEF,


    DEFAULT_ACTION_SENSE_OFFSET_COEF,


    DEFAULT_ACTION_SENSE_RESET_DEFAULT,


    DEFAULT_ACTION_SENSE_MAX_VALID,


    DEFAULT_ACTION_SENSE_MIN_VALID


} default_action;


typedef enum {

    BAT_ACTION_P0_OVERWRITE,


    BAT_ACTION_P1_OVERWRITE,


    BAT_ACTION_P2_OVERWRITE,


    BAT_ACTION_P3_OVERWRITE,


    BAT_ACTION_P4_OVERWRITE,


    BAT_ACTION_TEMP_LIMIT_OVERWRITE,


    BAT_ACTION_RELAY


} battery_action;


typedef enum {

    LOG_VALID,


    LOG_INVALID,


    LOG_OVERFLOW,


    LOG_CRITICAL_ALARM,


    LOG_DENIED_ACTION


} log_status;


#endif /* COMMON_TYPES_H */