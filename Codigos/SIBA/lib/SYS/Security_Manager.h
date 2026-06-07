#ifndef SECURITY_MANAGER_H
#define SECURITY_MANAGER_H

#include <stdint.h>

void Security_Manager_Init(void);
void Security_Manager_Check(uint32_t sensor_id, float value);
void Security_Manager_Trigger_Emergency(void);

#endif /* SECURITY_MANAGER_H */