#include <moonbit.h>

#include "../include/orbit_plugin_abi.h"

#include <stdint.h>
#include <string.h>

typedef struct FixtureInstance {
  const OrbitHostV1 *host;
} FixtureInstance;

static uint32_t fixture_abi_version(void) {
  return ORBIT_PLUGIN_ABI_VERSION_V1;
}

static uint32_t fixture_incompatible_abi_version(void) {
  return ORBIT_PLUGIN_ABI_VERSION_V1 + 1;
}

static const char *fixture_manifest_json(void) {
  return "{\"name\":\"fixture\",\"commands\":[\"echo\"]}";
}

static int32_t fixture_create(const OrbitHostV1 *host, void **out_instance) {
  if (host == NULL || out_instance == NULL ||
      host->abi_version != ORBIT_PLUGIN_ABI_VERSION_V1) {
    return -10;
  }
  FixtureInstance *instance = (FixtureInstance *)host->alloc(sizeof(FixtureInstance));
  if (instance == NULL) return -11;
  instance->host = host;
  *out_instance = instance;
  return 0;
}

static int32_t fixture_invoke(
    void *raw_instance,
    const char *command,
    const uint8_t *request,
    uint32_t request_len,
    OrbitBuffer *out_response) {
  FixtureInstance *instance = (FixtureInstance *)raw_instance;
  if (instance == NULL || command == NULL || out_response == NULL) return -20;
  if (strcmp(command, "fail") == 0) return -42;
  if (strcmp(command, "malformed") == 0) {
    out_response->data = NULL;
    out_response->len = 1;
    return 0;
  }
  if (strcmp(command, "oversized") == 0) {
    out_response->data = NULL;
    out_response->len = UINT32_C(16) * UINT32_C(1024) * UINT32_C(1024) + 1;
    return 0;
  }
  if (strcmp(command, "echo") != 0) return -21;
  if (request_len > 0 && request == NULL) return -22;
  uint8_t *response = NULL;
  if (request_len > 0) {
    response = (uint8_t *)instance->host->alloc(request_len);
    if (response == NULL) return -23;
    memcpy(response, request, request_len);
  }
  out_response->data = response;
  out_response->len = request_len;
  return 0;
}

static void fixture_destroy(void *raw_instance) {
  FixtureInstance *instance = (FixtureInstance *)raw_instance;
  if (instance != NULL) instance->host->free(instance);
}

MOONBIT_FFI_EXPORT uint64_t orbit_plugin_fixture_abi_version_address(void) {
  return (uint64_t)(uintptr_t)&fixture_abi_version;
}

MOONBIT_FFI_EXPORT uint64_t orbit_plugin_fixture_manifest_address(void) {
  return (uint64_t)(uintptr_t)&fixture_manifest_json;
}

MOONBIT_FFI_EXPORT uint64_t orbit_plugin_fixture_create_address(void) {
  return (uint64_t)(uintptr_t)&fixture_create;
}

MOONBIT_FFI_EXPORT uint64_t orbit_plugin_fixture_invoke_address(void) {
  return (uint64_t)(uintptr_t)&fixture_invoke;
}

MOONBIT_FFI_EXPORT uint64_t orbit_plugin_fixture_destroy_address(void) {
  return (uint64_t)(uintptr_t)&fixture_destroy;
}

MOONBIT_FFI_EXPORT uint64_t orbit_plugin_fixture_incompatible_abi_address(void) {
  return (uint64_t)(uintptr_t)&fixture_incompatible_abi_version;
}
