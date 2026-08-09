#ifndef ORBIT_PLUGIN_ABI_H
#define ORBIT_PLUGIN_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ORBIT_PLUGIN_ABI_VERSION_V1 UINT32_C(1)
#define ORBIT_PLUGIN_ABI_VERSION_V2 UINT32_C(2)

#define ORBIT_HOST_V2_FLAG_REQUEST UINT32_C(1)

#define ORBIT_HOST_REQUEST_OK INT32_C(0)
#define ORBIT_HOST_REQUEST_UNAVAILABLE INT32_C(-2147483548)
#define ORBIT_HOST_REQUEST_CANCELLED INT32_C(-2147483547)
#define ORBIT_HOST_REQUEST_CLOSED INT32_C(-2147483546)
#define ORBIT_HOST_REQUEST_INVALID_INPUT INT32_C(-2147483545)

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

/*
 * request and invocation_cancelled are valid only on the executor worker,
 * synchronously inside orbit_plugin_invoke. Plugin-created threads must not
 * call them or retain host_context beyond that call stack.
 */
typedef int32_t (*orbit_plugin_host_request_v2_fn)(
    void *host_context,
    const uint8_t *command,
    uint32_t command_len,
    const uint8_t *request_json,
    uint32_t request_len,
    uint32_t timeout_ms,
    OrbitBuffer *out_response_json);
typedef uint32_t (*orbit_plugin_invocation_cancelled_v2_fn)(
    void *host_context);

typedef struct OrbitHostV2 {
  /* The first four fields preserve the exact OrbitHostV1 layout. */
  uint32_t abi_version;
  orbit_plugin_log_fn log;
  orbit_plugin_alloc_fn alloc;
  orbit_plugin_free_fn free;
  uint32_t struct_size;
  uint32_t flags;
  void *host_context;
  orbit_plugin_host_request_v2_fn request;
  orbit_plugin_invocation_cancelled_v2_fn invocation_cancelled;
} OrbitHostV2;

typedef uint32_t (*orbit_plugin_abi_version_fn)(void);
typedef const char *(*orbit_plugin_manifest_json_fn)(void);
typedef int32_t (*orbit_plugin_create_v1_fn)(
    const OrbitHostV1 *host,
    void **out_instance);
typedef orbit_plugin_create_v1_fn orbit_plugin_create_fn;
typedef int32_t (*orbit_plugin_create_v2_fn)(
    const OrbitHostV2 *host,
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
