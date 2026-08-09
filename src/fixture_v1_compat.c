#include <moonbit.h>

#include <stdint.h>
#include <string.h>

/* Exact public declarations shipped by orbit-plugin-abi 0.1.0. */
#define ORBIT_PLUGIN_ABI_VERSION_V1_COMPAT UINT32_C(1)

typedef void (*orbit_plugin_log_v1_compat_fn)(
    uint32_t level,
    const uint8_t *message,
    uint32_t message_len);
typedef void *(*orbit_plugin_alloc_v1_compat_fn)(uint32_t size);
typedef void (*orbit_plugin_free_v1_compat_fn)(void *pointer);

typedef struct OrbitHostV1Compat {
  uint32_t abi_version;
  orbit_plugin_log_v1_compat_fn log;
  orbit_plugin_alloc_v1_compat_fn alloc;
  orbit_plugin_free_v1_compat_fn free;
} OrbitHostV1Compat;

typedef struct OrbitBufferV1Compat {
  uint8_t *data;
  uint32_t len;
} OrbitBufferV1Compat;

typedef struct FixtureV1CompatInstance {
  const OrbitHostV1Compat *host;
} FixtureV1CompatInstance;

static uint32_t fixture_v1_compat_version(void) {
  return ORBIT_PLUGIN_ABI_VERSION_V1_COMPAT;
}

static const char *fixture_v1_compat_manifest(void) {
  return "{\"name\":\"v1-compat\",\"commands\":[\"echo\"]}";
}

static int32_t fixture_v1_compat_create(
    const OrbitHostV1Compat *host,
    void **out_instance) {
  if (host == NULL || out_instance == NULL ||
      host->abi_version != ORBIT_PLUGIN_ABI_VERSION_V1_COMPAT) {
    return -200;
  }
  FixtureV1CompatInstance *instance =
      (FixtureV1CompatInstance *)host->alloc(sizeof(FixtureV1CompatInstance));
  if (instance == NULL) return -201;
  instance->host = host;
  *out_instance = instance;
  return 0;
}

static int32_t fixture_v1_compat_invoke(
    void *raw_instance,
    const char *command,
    const uint8_t *request,
    uint32_t request_len,
    OrbitBufferV1Compat *out_response) {
  FixtureV1CompatInstance *instance =
      (FixtureV1CompatInstance *)raw_instance;
  if (instance == NULL || command == NULL || out_response == NULL ||
      strcmp(command, "echo") != 0) {
    return -202;
  }
  uint8_t *copy = (uint8_t *)instance->host->alloc(request_len);
  if (request_len > 0 && copy == NULL) return -203;
  if (request_len > 0) memcpy(copy, request, request_len);
  out_response->data = copy;
  out_response->len = request_len;
  return 0;
}

static void fixture_v1_compat_destroy(void *raw_instance) {
  FixtureV1CompatInstance *instance =
      (FixtureV1CompatInstance *)raw_instance;
  if (instance != NULL) instance->host->free(instance);
}

MOONBIT_FFI_EXPORT uint64_t orbit_plugin_fixture_v1_compat_version_address(void) {
  return (uint64_t)(uintptr_t)&fixture_v1_compat_version;
}

MOONBIT_FFI_EXPORT uint64_t orbit_plugin_fixture_v1_compat_manifest_address(void) {
  return (uint64_t)(uintptr_t)&fixture_v1_compat_manifest;
}

MOONBIT_FFI_EXPORT uint64_t orbit_plugin_fixture_v1_compat_create_address(void) {
  return (uint64_t)(uintptr_t)&fixture_v1_compat_create;
}

MOONBIT_FFI_EXPORT uint64_t orbit_plugin_fixture_v1_compat_invoke_address(void) {
  return (uint64_t)(uintptr_t)&fixture_v1_compat_invoke;
}

MOONBIT_FFI_EXPORT uint64_t orbit_plugin_fixture_v1_compat_destroy_address(void) {
  return (uint64_t)(uintptr_t)&fixture_v1_compat_destroy;
}
