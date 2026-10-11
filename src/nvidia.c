#include "nvidia.h"

#include "file_utils.h"

#include <dirent.h>
#include <dlfcn.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// Type definitions and constants for accessing NVIDIA GPU information via libnvidia-ml
// ============================================================================

typedef struct nvmlDevice_st* nvmlDevice_t;
typedef int nvmlReturn_t;

#define NVML_TEMPERATURE_GPU 0
#define NVML_SUCCESS 0

// ============================================================================
// NVML-related globals used for dynamically accessing GPU temperature:
// ============================================================================

static void *Nvidia_DlHandle = NULL;
static nvmlDevice_t Nvidia_Device;
static bool Nvidia_Initialized = false;

nvmlReturn_t (*Nvidia_nvmlInit_fn)(void);
nvmlReturn_t (*Nvidia_nvmlShutdown_fn)(void);
nvmlReturn_t (*Nvidia_nvmlDeviceGetHandleByIndex_fn)(unsigned int, nvmlDevice_t*);
nvmlReturn_t (*Nvidia_nvmlDeviceGetTemperature_fn)(nvmlDevice_t, unsigned int, unsigned int*);

// ============================================================================
// Nvidia_* Functions
// ============================================================================

Nvidia_Error Nvidia_Load(void) {
  if (! Nvidia_DlHandle) {
    Nvidia_DlHandle = dlopen("libnvidia-ml.so.1", RTLD_LAZY);

    if (! Nvidia_DlHandle)
      return Nvidia_Error_DlOpen;
  }

  Nvidia_nvmlInit_fn = (nvmlReturn_t (*)(void)) dlsym(Nvidia_DlHandle, "nvmlInit");
  if (!Nvidia_nvmlInit_fn)
    return Nvidia_Error_DlOpen;

  Nvidia_nvmlShutdown_fn = (nvmlReturn_t (*)(void)) dlsym(Nvidia_DlHandle, "nvmlShutdown");
  if (!Nvidia_nvmlShutdown_fn)
    return Nvidia_Error_DlOpen;

  Nvidia_nvmlDeviceGetHandleByIndex_fn = (nvmlReturn_t (*)(unsigned int, nvmlDevice_t *)) dlsym(Nvidia_DlHandle, "nvmlDeviceGetHandleByIndex");
  if (!Nvidia_nvmlDeviceGetHandleByIndex_fn)
    return Nvidia_Error_DlOpen;

  Nvidia_nvmlDeviceGetTemperature_fn = (nvmlReturn_t (*)(nvmlDevice_t, unsigned int, unsigned int *)) dlsym(Nvidia_DlHandle, "nvmlDeviceGetTemperature");
  if (!Nvidia_nvmlDeviceGetTemperature_fn)
    return Nvidia_Error_DlOpen;

  return Nvidia_Error_OK;
}

Nvidia_Error Nvidia_Init(void) {
  Nvidia_Error e = Nvidia_Load();
  if (e)
    return e;

  if (Nvidia_Initialized)
    return Nvidia_Error_OK;

  if (Nvidia_nvmlInit_fn() != NVML_SUCCESS)
    return Nvidia_Error_API;

  if (Nvidia_nvmlDeviceGetHandleByIndex_fn(0, &Nvidia_Device) != NVML_SUCCESS)
    return Nvidia_Error_API;

  unsigned int temp; // NOLINT
  if (Nvidia_nvmlDeviceGetTemperature_fn(Nvidia_Device, NVML_TEMPERATURE_GPU, &temp) != NVML_SUCCESS)
    return Nvidia_Error_API;

  Nvidia_Initialized = true;
  return Nvidia_Error_OK;
}

void Nvidia_Close(void) {
  if (Nvidia_Initialized && Nvidia_nvmlShutdown_fn)
    Nvidia_nvmlShutdown_fn();

  if (Nvidia_DlHandle)
    dlclose(Nvidia_DlHandle);

  Nvidia_DlHandle = NULL;
  Nvidia_nvmlShutdown_fn = NULL;
  Nvidia_Initialized = false;
}

bool Nvidia_IsRuntimeSuspended(const char* runtime_status_file) {
  if (! runtime_status_file || ! strcmp(runtime_status_file, "none"))
    return false;

  char buf[32];
  file_op_result res = slurp_file(buf, sizeof(buf), runtime_status_file);
  return res.ok && ! strncmp(buf, "suspended", 9);
}

Error Nvidia_GetTemperature(const char* runtime_status_file, float* out) {
  unsigned int temp; // NOLINT
  Nvidia_Error e;

  if (Nvidia_IsRuntimeSuspended(runtime_status_file))
    return err_string("nvidia-ml: GPU runtime-suspended");

  e = Nvidia_Init();
  if (e == Nvidia_Error_DlOpen)
    return err_string("nvidia-ml: Failed to load NVML");
  if (e == Nvidia_Error_API)
    return err_string("nvidia-ml: Failed to initialize NVML");

  if (Nvidia_nvmlDeviceGetTemperature_fn(Nvidia_Device, NVML_TEMPERATURE_GPU, &temp) == NVML_SUCCESS) {
    *out = (float) temp;
    Nvidia_Close();
  }
  else {
    Nvidia_Close();
    return err_string("nvidia-ml: Failed to get temperature");
  }

  return err_success();
}
