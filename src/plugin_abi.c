#include <moonbit.h>

#include "../include/orbit_plugin_abi.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ORBIT_PLUGIN_MAX_MANIFEST_BYTES (1024U * 1024U)
#define ORBIT_PLUGIN_MAX_COMMAND_BYTES 1024U
#define ORBIT_PLUGIN_MAX_REQUEST_BYTES (16U * 1024U * 1024U)
#define ORBIT_PLUGIN_MAX_RESPONSE_BYTES (16U * 1024U * 1024U)

#define ORBIT_PLUGIN_WRAPPER_MALFORMED_RESPONSE INT32_MIN
#define ORBIT_PLUGIN_WRAPPER_INVALID_INPUT (INT32_MIN + 1)

static void orbit_plugin_host_log(
    uint32_t level,
    const uint8_t *message,
    uint32_t message_len) {
  (void)level;
  if (message == NULL || message_len == 0) return;
  (void)fwrite(message, 1, message_len, stderr);
  (void)fputc('\n', stderr);
}

static void *orbit_plugin_host_alloc(uint32_t size) {
  size_t requested = size == 0 ? 1 : (size_t)size;
  return malloc(requested);
}

static void orbit_plugin_host_free(void *pointer) {
  free(pointer);
}

/*
 * This object has static storage duration. Plugins may retain its pointer
 * from create until their matching destroy call.
 */
static const OrbitHostV1 ORBIT_PLUGIN_HOST_V1 = {
    ORBIT_PLUGIN_ABI_VERSION_V1,
    orbit_plugin_host_log,
    orbit_plugin_host_alloc,
    orbit_plugin_host_free,
};

static moonbit_bytes_t orbit_plugin_empty_bytes(void) {
  return moonbit_make_bytes(0, 0);
}

static void orbit_plugin_write_u32_le(uint8_t *out, uint32_t value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
  out[2] = (uint8_t)(value >> 16);
  out[3] = (uint8_t)(value >> 24);
}

static void orbit_plugin_write_u64_le(uint8_t *out, uint64_t value) {
  for (uint32_t index = 0; index < 8; index++) {
    out[index] = (uint8_t)(value >> (index * 8));
  }
}

static moonbit_bytes_t orbit_plugin_result(
    int32_t status,
    const uint8_t *payload,
    uint32_t payload_len) {
  if (payload_len > INT32_MAX - 4) return orbit_plugin_empty_bytes();
  moonbit_bytes_t result = moonbit_make_bytes((int32_t)payload_len + 4, 0);
  if (result == NULL) return orbit_plugin_empty_bytes();
  orbit_plugin_write_u32_le(result, (uint32_t)status);
  if (payload_len > 0 && payload != NULL) {
    memcpy(result + 4, payload, payload_len);
  }
  return result;
}

static size_t orbit_plugin_bounded_strlen(const char *value, size_t limit) {
  if (value == NULL) return 0;
  for (size_t index = 0; index < limit; index++) {
    if (value[index] == '\0') return index;
  }
  return limit;
}

static char *orbit_plugin_copy_command(moonbit_bytes_t value) {
  if (value == NULL) return NULL;
  int32_t length = Moonbit_array_length(value);
  if (length <= 0 || (uint32_t)length > ORBIT_PLUGIN_MAX_COMMAND_BYTES) {
    return NULL;
  }
  if (memchr(value, 0, (size_t)length) != NULL) return NULL;
  char *copy = (char *)malloc((size_t)length + 1);
  if (copy == NULL) return NULL;
  memcpy(copy, value, (size_t)length);
  copy[length] = '\0';
  return copy;
}

MOONBIT_FFI_EXPORT uint32_t orbit_plugin_abi_call_version(uint64_t address) {
  if (address == 0) return 0;
  orbit_plugin_abi_version_fn function =
      (orbit_plugin_abi_version_fn)(uintptr_t)address;
  return function();
}

MOONBIT_FFI_EXPORT moonbit_bytes_t orbit_plugin_abi_call_manifest(
    uint64_t address) {
  if (address == 0) return orbit_plugin_empty_bytes();
  orbit_plugin_manifest_json_fn function =
      (orbit_plugin_manifest_json_fn)(uintptr_t)address;
  const char *manifest = function();
  size_t length = orbit_plugin_bounded_strlen(
      manifest, (size_t)ORBIT_PLUGIN_MAX_MANIFEST_BYTES + 1);
  if (manifest == NULL || length == 0 ||
      length > ORBIT_PLUGIN_MAX_MANIFEST_BYTES || length > INT32_MAX) {
    return orbit_plugin_empty_bytes();
  }
  moonbit_bytes_t copy = moonbit_make_bytes((int32_t)length, 0);
  if (copy == NULL) return orbit_plugin_empty_bytes();
  memcpy(copy, manifest, length);
  return copy;
}

MOONBIT_FFI_EXPORT moonbit_bytes_t orbit_plugin_abi_call_create(
    uint64_t address) {
  if (address == 0) {
    moonbit_bytes_t invalid = moonbit_make_bytes(16, 0);
    if (invalid == NULL) return orbit_plugin_empty_bytes();
    orbit_plugin_write_u32_le(
        invalid, (uint32_t)ORBIT_PLUGIN_WRAPPER_INVALID_INPUT);
    return invalid;
  }
  orbit_plugin_create_fn function = (orbit_plugin_create_fn)(uintptr_t)address;
  void *instance = NULL;
  int32_t status = function(&ORBIT_PLUGIN_HOST_V1, &instance);
  moonbit_bytes_t result = moonbit_make_bytes(16, 0);
  if (result == NULL) return orbit_plugin_empty_bytes();
  orbit_plugin_write_u32_le(result, (uint32_t)status);
  orbit_plugin_write_u64_le(result + 8, (uint64_t)(uintptr_t)instance);
  return result;
}

MOONBIT_FFI_EXPORT moonbit_bytes_t orbit_plugin_abi_call_invoke(
    uint64_t address,
    uint64_t instance_address,
    moonbit_bytes_t command,
    moonbit_bytes_t request) {
  if (address == 0 || instance_address == 0 || request == NULL) {
    return orbit_plugin_result(ORBIT_PLUGIN_WRAPPER_INVALID_INPUT, NULL, 0);
  }
  int32_t request_length = Moonbit_array_length(request);
  if (request_length < 0 ||
      (uint32_t)request_length > ORBIT_PLUGIN_MAX_REQUEST_BYTES) {
    return orbit_plugin_result(ORBIT_PLUGIN_WRAPPER_INVALID_INPUT, NULL, 0);
  }
  char *command_copy = orbit_plugin_copy_command(command);
  if (command_copy == NULL) {
    return orbit_plugin_result(ORBIT_PLUGIN_WRAPPER_INVALID_INPUT, NULL, 0);
  }
  orbit_plugin_invoke_fn function = (orbit_plugin_invoke_fn)(uintptr_t)address;
  OrbitBuffer response = {NULL, 0};
  int32_t status = function(
      (void *)(uintptr_t)instance_address,
      command_copy,
      request,
      (uint32_t)request_length,
      &response);
  free(command_copy);
  if (status != 0) return orbit_plugin_result(status, NULL, 0);
  if (response.len > ORBIT_PLUGIN_MAX_RESPONSE_BYTES ||
      (response.len > 0 && response.data == NULL)) {
    if (response.data != NULL) ORBIT_PLUGIN_HOST_V1.free(response.data);
    return orbit_plugin_result(ORBIT_PLUGIN_WRAPPER_MALFORMED_RESPONSE, NULL, 0);
  }
  moonbit_bytes_t result =
      orbit_plugin_result(0, response.data, response.len);
  if (response.data != NULL) ORBIT_PLUGIN_HOST_V1.free(response.data);
  return result;
}

MOONBIT_FFI_EXPORT void orbit_plugin_abi_call_destroy(
    uint64_t address,
    uint64_t instance_address) {
  if (address == 0 || instance_address == 0) return;
  orbit_plugin_destroy_fn function = (orbit_plugin_destroy_fn)(uintptr_t)address;
  function((void *)(uintptr_t)instance_address);
}
