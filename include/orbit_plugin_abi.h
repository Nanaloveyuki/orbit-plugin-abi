#ifndef ORBIT_PLUGIN_ABI_H
#define ORBIT_PLUGIN_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIT_PLUGIN_ABI_VERSION_V1 UINT32_C(1)

typedef void (*orbit_plugin_log_fn)(
    uint32_t level,
    const uint8_t *message,
    uint32_t message_len);
typedef void *(*orbit_plugin_alloc_fn)(uint32_t size);
typedef void (*orbit_plugin_free_fn)(void *pointer);

typedef struct OrbitHostV1 {
  uint32_t abi_version;
  orbit_plugin_log_fn log;
  orbit_plugin_alloc_fn alloc;
  orbit_plugin_free_fn free;
} OrbitHostV1;

typedef struct OrbitBuffer {
  uint8_t *data;
  uint32_t len;
} OrbitBuffer;

typedef uint32_t (*orbit_plugin_abi_version_fn)(void);
typedef const char *(*orbit_plugin_manifest_json_fn)(void);
typedef int32_t (*orbit_plugin_create_fn)(
    const OrbitHostV1 *host,
    void **out_instance);
typedef int32_t (*orbit_plugin_invoke_fn)(
    void *instance,
    const char *command,
    const uint8_t *request,
    uint32_t request_len,
    OrbitBuffer *out_response);
typedef void (*orbit_plugin_destroy_fn)(void *instance);

#ifdef __cplusplus
}
#endif

#endif
