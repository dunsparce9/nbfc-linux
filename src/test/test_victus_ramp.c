// No hardware access: exercise the real sensor, filter and fan code with files.
// Example (from repository root):
// cc -DSYSCONFDIR='"/etc"' -DDATADIR='"/usr/share"' \
//   -DLIBDIR='"/usr/lib"' -DRUNSTATEDIR='"/var/run"' -DVERSION='"0.5.4"' -I/usr/include/lua5.4 \
//   src/test/test_victus_ramp.c -o /tmp/test-victus-ramp -lm -llua5.4 -ldl
// Run /tmp/test-victus-ramp with the three profile paths.
#define main nbfc_service_main
#include "../build.c"
#undef main
#include <assert.h>

static void write_temperature(const char* path, int value) {
  FILE* f = fopen(path, "w");
  assert(f);
  fprintf(f, "%d\n", value * 1000);
  assert(fclose(f) == 0);
}

static void sample(FanTemperatureControl* ftc) {
  Error e = FanTemperatureControl_UpdateFanTemperature(ftc);
  assert(!e);
  Fan_SetTemperature(&ftc->Fan, ftc->Temperature);
}

int main(int argc, char** argv) {
  assert(argc > 1);
  char cpu_path[] = "/tmp/nbfc-cpu-XXXXXX";
  char gpu_path[] = "/tmp/nbfc-gpu-XXXXXX";
  int cpu_fd = mkstemp(cpu_path), gpu_fd = mkstemp(gpu_path);
  assert(cpu_fd >= 0 && gpu_fd >= 0);
  close(cpu_fd);
  close(gpu_fd);
  FS_TemperatureSource sources[] = {
    {.name = "k10temp", .file = cpu_path, .multiplier = 0.001f, .type = FS_TemperatureSource_File},
    {.name = "nvidia-ml", .file = gpu_path, .multiplier = 0.001f, .type = FS_TemperatureSource_File},
  };
  FS_Sensors_Sources.data = sources;
  FS_Sensors_Sources.size = 2;

  FILE* state = fopen(gpu_path, "w");
  assert(state);
  fputs("suspended\n", state);
  assert(fclose(state) == 0);
  float unused;
  Error suspended = Nvidia_GetTemperature(gpu_path, &unused);
  assert(suspended && !Nvidia_DlHandle && !Nvidia_Initialized);

  for (int arg = 1; arg < argc; ++arg) {
    ModelConfig config = {0};
    Error e = ModelConfig_FromFile(&config, argv[arg]);
    assert(!e);
    Trace trace = {0};
    e = ModelConfig_Validate(&trace, &config);
    assert(!e);
    array_of(FanTemperatureControl) fans = {0};
    fans.size = config.FanConfigurations.size;
    fans.data = Mem_Calloc(fans.size, sizeof(*fans.data));
    for (array_size_t i = 0; i < fans.size; ++i) {
      e = Fan_Init(&fans.data[i].Fan, &config.FanConfigurations.data[i], &config);
      assert(!e);
    }
    ServiceConfig service_config = {0};
    e = FanTemperatureControls_Init(&fans, &service_config, &config);
    assert(!e);

    for (array_size_t i = 0; i < fans.size; ++i) {
      FanTemperatureControl* ftc = &fans.data[i];
      write_temperature(cpu_path, 60);
      write_temperature(gpu_path, 40);
      for (int n = 0; n < 20; ++n) sample(ftc);
      write_temperature(cpu_path, 90);
      for (int n = 0; n < 5; ++n) {
        sample(ftc);
        assert(Fan_GetTargetSpeed(&ftc->Fan) < 100);
      }
      printf("%s fan %zd: 5-second 90 C burst -> %.0f%% at %.1f C filtered\n",
        argv[arg], i, Fan_GetTargetSpeed(&ftc->Fan), ftc->Temperature);

      write_temperature(cpu_path, 94);
      for (int n = 0; n < 20; ++n) sample(ftc);
      assert(Fan_GetTargetSpeed(&ftc->Fan) == 100);

      write_temperature(cpu_path, 40);
      for (int n = 0; n < 20; ++n) sample(ftc);
      // Even averaging with a cool GPU must not conceal an emergency.
      ftc->TemperatureAlgorithmType = TemperatureAlgorithmType_Average;
      write_temperature(cpu_path, 96);
      sample(ftc);
      assert(ftc->Temperature >= 96);
      assert(ftc->Fan.isCritical && Fan_GetTargetSpeed(&ftc->Fan) == 100);

      write_temperature(cpu_path, 40);
      for (int n = 0; n < 20; ++n) sample(ftc);
      if (ftc->TemperatureSources.size > 1) {
        write_temperature(gpu_path, 96);
        sample(ftc);
        assert(ftc->Temperature >= 96);
        assert(ftc->Fan.isCritical && Fan_GetTargetSpeed(&ftc->Fan) == 100);
      }
      write_temperature(gpu_path, 40);
      TemperatureFilter_Close(&ftc->TemperatureFilter);
    }
    FanTemperatureControls_Free(&fans);
    ModelConfig_Free(&config);
  }
  unlink(cpu_path);
  unlink(gpu_path);
  return 0;
}
