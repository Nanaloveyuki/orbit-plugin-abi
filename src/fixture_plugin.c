#include <moonbit.h>

#include "../include/orbit_plugin_abi.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static uint64_t fixture_thread_id(void) { return (uint64_t)GetCurrentThreadId(); }
static void fixture_sleep(void) { Sleep(1); }
#else
#include <pthread.h>
#include <time.h>
static uint64_t fixture_thread_id(void) {
  return (uint64_t)(uintptr_t)pthread_self();
}
static void fixture_sleep(void) {
  struct timespec delay = {0, 1000000};
  (void)nanosleep(&delay, NULL);
}
#endif

typedef struct FixtureInstance {
  const OrbitHostV1 *host;
} FixtureInstance;

static uint32_t fixture_abi_version(void) {
  return ORBIT_PLUGIN_ABI_VERSION_V1;
}

static uint32_t fixture_incompatible_abi_version(void) {
  return ORBIT_PLUGIN_ABI_VERSION_V2 + 1;
}

static uint32_t fixture_v2_abi_version(void) {
  return ORBIT_PLUGIN_ABI_VERSION_V2;
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

typedef struct FixtureV2Instance {
  const OrbitHostV2 *host;
  uint64_t worker_thread;
} FixtureV2Instance;

static volatile uint32_t fixture_v2_thread_mismatch = 0;
static const OrbitHostV2 *fixture_v2_foreign_test_host = NULL;

static int32_t fixture_v2_create(
    const OrbitHostV2 *host,
    void **out_instance) {
  if (host == NULL || out_instance == NULL ||
      host->abi_version != ORBIT_PLUGIN_ABI_VERSION_V2 ||
      host->struct_size < sizeof(OrbitHostV2) ||
      (host->flags & ORBIT_HOST_V2_FLAG_REQUEST) == 0 ||
      host->request == NULL || host->invocation_cancelled == NULL) {
    return -100;
  }
  FixtureV2Instance *instance =
      (FixtureV2Instance *)host->alloc(sizeof(FixtureV2Instance));
  if (instance == NULL) return -101;
  instance->host = host;
  instance->worker_thread = fixture_thread_id();
  fixture_v2_foreign_test_host = host;
  *out_instance = instance;
  return 0;
}

static int32_t fixture_v2_create_failure(
    const OrbitHostV2 *host,
    void **out_instance) {
  if (host == NULL || out_instance == NULL) return -119;
  for (uint32_t attempt = 0; attempt < 20; attempt++) fixture_sleep();
  *out_instance = NULL;
  return -120;
}

static int32_t fixture_v2_invoke(
    void *raw_instance,
    const char *command,
    const uint8_t *request,
    uint32_t request_len,
    OrbitBuffer *out_response) {
  FixtureV2Instance *instance = (FixtureV2Instance *)raw_instance;
  if (instance == NULL || command == NULL || out_response == NULL) return -110;
  if (fixture_thread_id() != instance->worker_thread) {
    fixture_v2_thread_mismatch++;
    return -111;
  }
  if (strcmp(command, "echo") == 0) {
    uint8_t *copy = (uint8_t *)instance->host->alloc(request_len);
    if (request_len > 0 && copy == NULL) return -112;
    if (request_len > 0) memcpy(copy, request, request_len);
    out_response->data = copy;
    out_response->len = request_len;
    return 0;
  }
  if (strcmp(command, "host") == 0) {
    static const uint8_t host_command[] = "app:echo";
    return instance->host->request(
        instance->host->host_context,
        host_command,
        (uint32_t)(sizeof(host_command) - 1),
        request,
        request_len,
        250,
        out_response);
  }
  if (strcmp(command, "cancel") == 0) {
    while (!instance->host->invocation_cancelled(
        instance->host->host_context)) {
      fixture_sleep();
    }
    return -113;
  }
  if (strcmp(command, "malformed") == 0) {
    out_response->data = NULL;
    out_response->len = 1;
    return 0;
  }
  return -114;
}

static void fixture_v2_destroy(void *raw_instance) {
  FixtureV2Instance *instance = (FixtureV2Instance *)raw_instance;
  if (instance == NULL) return;
  if (fixture_thread_id() != instance->worker_thread) {
    fixture_v2_thread_mismatch++;
  }
  fixture_v2_foreign_test_host = NULL;
  instance->host->free(instance);
}

static void fixture_wakeup(void) {}

MOONBIT_FFI_EXPORT void orbit_plugin_fixture_sleep(void) {
  fixture_sleep();
}

MOONBIT_FFI_EXPORT void orbit_plugin_fixture_wakeup(void) {
  fixture_wakeup();
}

MOONBIT_FFI_EXPORT uint32_t orbit_plugin_fixture_v2_thread_mismatch(void) {
  return fixture_v2_thread_mismatch;
}

MOONBIT_FFI_EXPORT int32_t orbit_plugin_fixture_v2_foreign_request(void) {
  const OrbitHostV2 *host = fixture_v2_foreign_test_host;
  if (host == NULL) return -300;
  static const uint8_t command[] = "app:echo";
  OrbitBuffer response = {NULL, 0};
  int32_t status = host->request(
      host->host_context,
      command,
      (uint32_t)(sizeof(command) - 1),
      NULL,
      0,
      0,
      &response);
  if (response.data != NULL) host->free(response.data);
  return status;
}

MOONBIT_FFI_EXPORT uint64_t orbit_plugin_fixture_v2_abi_version_address(void) {
  return (uint64_t)(uintptr_t)&fixture_v2_abi_version;
}

MOONBIT_FFI_EXPORT uint64_t orbit_plugin_fixture_v2_create_address(void) {
  return (uint64_t)(uintptr_t)&fixture_v2_create;
}

MOONBIT_FFI_EXPORT uint64_t orbit_plugin_fixture_v2_create_failure_address(void) {
  return (uint64_t)(uintptr_t)&fixture_v2_create_failure;
}

MOONBIT_FFI_EXPORT uint64_t orbit_plugin_fixture_v2_invoke_address(void) {
  return (uint64_t)(uintptr_t)&fixture_v2_invoke;
}

MOONBIT_FFI_EXPORT uint64_t orbit_plugin_fixture_v2_destroy_address(void) {
  return (uint64_t)(uintptr_t)&fixture_v2_destroy;
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
