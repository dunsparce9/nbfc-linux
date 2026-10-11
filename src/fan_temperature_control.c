#include "fan_temperature_control.h"

#include "nbfc.h"
#include "memory.h"

#include <stdio.h>
#include <string.h>

static Error FanTemperatureControl_SetTemperatureSources(
  FanTemperatureControl* ftc,
  array_of(str)* sensors,
  const char* context)
{
  Error e;

  ftc->TemperatureSources.size = 0;

  for_each_array(const char**, sensor, *sensors) {
    e = FS_TemperatureSources_AddTemperatureSources(&ftc->TemperatureSources, *sensor);
    if (e) {
      Log_Warn("Skipping unavailable temperature source \"%s\" for %s: %s", *sensor, context, err_print_all(e));
      continue;
    }
  }

  if (! ftc->TemperatureSources.size)
    return err_chain_stringf(0, "%s: No temperature sources available", context);

  return err_success();
}

// Set default sensors for FanTemperatureControls.
// That means:
//   - Use "Average" as TemperatureAlgorithmType
//   - Utilize every CPU sensor
static Error FanTemperatureControl_SetDefaults(array_of(FanTemperatureControl)* fans) {
  for_each_array(FanTemperatureControl*, ftc, *fans) {
    ftc->TemperatureAlgorithmType = TemperatureAlgorithmType_Average;
    ftc->TemperatureSources.size = 0;
    FS_TemperatureSources_AddTemperatureSources(&ftc->TemperatureSources, "@CPU");
  }

  return err_success();
}

static Error FanTemperatureControl_SetByModelConfig0(
  FanTemperatureControl* ftc,
  FanConfiguration* fc)
{
  if (fc->isset.TemperatureAlgorithmType)
    ftc->TemperatureAlgorithmType = fc->TemperatureAlgorithmType;

  // Use default sensor names
  if (! fc->Sensors.size)
    return err_success();

  return FanTemperatureControl_SetTemperatureSources(ftc, &fc->Sensors, fc->FanDisplayName);
}

// Set fan temperature sources by model config
static Error FanTemperatureControl_SetByModelConfig(
  array_of(FanTemperatureControl)* fans,
  ModelConfig* model_config)
{
  Error e;

  for_enumerate_array(array_size_t, fan_index, *fans) {
    FanTemperatureControl* ftc = &fans->data[fan_index];
    FanConfiguration* fc = &model_config->FanConfigurations.data[fan_index];

    e = FanTemperatureControl_SetByModelConfig0(ftc, fc);
    if (e)
      return err_chain_stringf(e, "FanConfigurations[%zd] (%s)", fan_index, fc->FanDisplayName);
  }

  return err_success();
}

// Initialize `fans` by `service_config`
static Error FanTemperatureControl_SetByServiceConfig(
  array_of(FanTemperatureControl)* fans,
  ServiceConfig* service_config)
{
  Error e;

  for_each_array(FanTemperatureSourceConfig*, ftsc, service_config->FanTemperatureSources) {
    if (ftsc->FanIndex >= fans->size)
      return err_stringf("Invalid FanIndex in FanTemperatureSources: %d", ftsc->FanIndex);

    FanTemperatureControl* ftc = &fans->data[ftsc->FanIndex];

    if (ftsc->isset.TemperatureAlgorithmType)
      ftc->TemperatureAlgorithmType = ftsc->TemperatureAlgorithmType;

    // If no sensors are given, use the defaults
    if (! ftsc->Sensors.size)
      continue;

    char context[64];
    snprintf(context, sizeof(context), "FanTemperatureSources[%d]", ftsc->FanIndex);
    e = FanTemperatureControl_SetTemperatureSources(ftc, &ftsc->Sensors, context);
    if (e)
      return e;
  }

  return err_success();
}

// Initialize temperature filters in `fans`
static Error FanTemperatureControl_InitializeTemperatureFilters(
  array_of(FanTemperatureControl)* fans,
  ModelConfig* model_config)
{
  Error e;

  for_enumerate_array(size_t, i, *fans) {
    FanTemperatureControl* ftc = &fans->data[i];
    unsigned int timespan = model_config->FanConfigurations.data[i].TemperatureFilterTimespan;
    e = TemperatureFilter_Init(&ftc->TemperatureFilter, model_config->EcPollInterval, timespan);
    if (e)
      return e;
    Log_Info("Fan #%zd temperature smoothing window: %u ms", i, timespan);
  }

  return err_success();
}

// Initialize FanTemperatureControls in `fans`
Error FanTemperatureControls_Init(
  array_of(FanTemperatureControl)* fans,
  ServiceConfig* service_config,
  ModelConfig* model_config)
{
  Error e;

  // Set default TemperatureAlgorithmType and temperature sources.
  e = FanTemperatureControl_SetDefaults(fans);
  if (e)
    return e;

  // Set temperature sources as specified in ModelConfig
  e = FanTemperatureControl_SetByModelConfig(fans, model_config);
  if (e)
    return e;

  // Set the temperature sources as specified in service_config
  e = FanTemperatureControl_SetByServiceConfig(fans, service_config);
  if (e)
    return e;

  // Initialize the temperature filters
  e = FanTemperatureControl_InitializeTemperatureFilters(fans, model_config);
  if (e)
    return e;

  return err_success();
}

Error FanTemperatureControl_UpdateFanTemperature(FanTemperatureControl* ftc) {
  Error e;
  float temp; // NOLINT
  float hottest; // NOLINT

  e = FS_TemperatureSources_GetTemperatureAndMax(
        &ftc->TemperatureSources, ftc->TemperatureAlgorithmType, &temp, &hottest);

  if (e)
    return e;

  ftc->Temperature = TemperatureFilter_FilterTemperature(&ftc->TemperatureFilter, temp);
  // Do not let averaging across sensors or over time hide critical heat.
  if (hottest >= ftc->Fan.criticalTemperature)
    ftc->Temperature = MAX(ftc->Temperature, hottest);
  return err_success();
}

void FanTemperatureControls_Log(const array_of(FanTemperatureControl)* fans, ModelConfig* model_config) {
  for_enumerate_array(array_size_t, fan_index, *fans) {
    FanTemperatureControl* ftc = &fans->data[fan_index];

    for_each_array(array_size_t*, ts_ref, ftc->TemperatureSources) {
      const FS_TemperatureSource* ts = FS_Sensors_Sources_UnRef(*ts_ref);

      Log_Info("Fan #%zd (%s) uses \"%s\" (%s) as temperature source (%s)",
        fan_index,
        model_config->FanConfigurations.data[fan_index].FanDisplayName,
        ts->name,
        ts->file,
        TemperatureAlgorithmType_ToString(ftc->TemperatureAlgorithmType));
    }
  }
}

void FanTemperatureControls_Free(array_of(FanTemperatureControl)* ftcs) {
  for_each_array(FanTemperatureControl*, ftc, *ftcs)
    Mem_Free(ftc->TemperatureSources.data);
  Mem_Free(ftcs->data);
}
