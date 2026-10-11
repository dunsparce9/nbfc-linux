#ifndef NBFC_NVIDIA_H_
#define NBFC_NVIDIA_H_

#include "error.h"

#include <stdbool.h>

enum Nvidia_Error {
  Nvidia_Error_OK     = 0,
  Nvidia_Error_DlOpen = -1,
  Nvidia_Error_API    = -2
};
typedef enum Nvidia_Error Nvidia_Error;

Nvidia_Error Nvidia_Init(void);
Nvidia_Error Nvidia_Load(void);
void         Nvidia_Close(void);
Error        Nvidia_GetTemperature(const char*, float*);
bool         Nvidia_IsRuntimeSuspended(const char*);

#endif
