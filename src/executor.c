#include <moonbit.h>

#include "../include/orbit_plugin_abi.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
typedef CRITICAL_SECTION orbit_mutex_t;
typedef CONDITION_VARIABLE orbit_cond_t;
typedef HANDLE orbit_thread_t;
#define ORBIT_THREAD_RESULT unsigned __stdcall
static void orbit_mutex_init(orbit_mutex_t *value) { InitializeCriticalSection(value); }
static void orbit_mutex_destroy(orbit_mutex_t *value) { DeleteCriticalSection(value); }
static void orbit_mutex_lock(orbit_mutex_t *value) { EnterCriticalSection(value); }
static void orbit_mutex_unlock(orbit_mutex_t *value) { LeaveCriticalSection(value); }
static void orbit_cond_init(orbit_cond_t *value) { InitializeConditionVariable(value); }
static void orbit_cond_destroy(orbit_cond_t *value) { (void)value; }
static void orbit_cond_wait(orbit_cond_t *value, orbit_mutex_t *mutex) {
  (void)SleepConditionVariableCS(value, mutex, INFINITE);
}
static void orbit_cond_signal(orbit_cond_t *value) { WakeConditionVariable(value); }
static void orbit_cond_broadcast(orbit_cond_t *value) { WakeAllConditionVariable(value); }
#else
#include <pthread.h>
typedef pthread_mutex_t orbit_mutex_t;
typedef pthread_cond_t orbit_cond_t;
typedef pthread_t orbit_thread_t;
#define ORBIT_THREAD_RESULT void *
static void orbit_mutex_init(orbit_mutex_t *value) { (void)pthread_mutex_init(value, NULL); }
static void orbit_mutex_destroy(orbit_mutex_t *value) { (void)pthread_mutex_destroy(value); }
static void orbit_mutex_lock(orbit_mutex_t *value) { (void)pthread_mutex_lock(value); }
static void orbit_mutex_unlock(orbit_mutex_t *value) { (void)pthread_mutex_unlock(value); }
static void orbit_cond_init(orbit_cond_t *value) { (void)pthread_cond_init(value, NULL); }
static void orbit_cond_destroy(orbit_cond_t *value) { (void)pthread_cond_destroy(value); }
static void orbit_cond_wait(orbit_cond_t *value, orbit_mutex_t *mutex) {
  (void)pthread_cond_wait(value, mutex);
}
static void orbit_cond_signal(orbit_cond_t *value) { (void)pthread_cond_signal(value); }
static void orbit_cond_broadcast(orbit_cond_t *value) { (void)pthread_cond_broadcast(value); }
#endif

#define ORBIT_EXECUTOR_MAX_QUEUED_INVOCATIONS 64U
#define ORBIT_EXECUTOR_MAX_QUEUED_BYTES (64U * 1024U * 1024U)
#define ORBIT_EXECUTOR_MAX_EVENT_BYTES (64U * 1024U * 1024U)
#define ORBIT_EXECUTOR_MAX_COMMAND_BYTES 1024U
#define ORBIT_EXECUTOR_MAX_REQUEST_BYTES (16U * 1024U * 1024U)
#define ORBIT_EXECUTOR_MAX_RESPONSE_BYTES (16U * 1024U * 1024U)

#define ORBIT_EXECUTOR_OK INT32_C(0)
#define ORBIT_EXECUTOR_INVALID_INPUT INT32_C(-2147483448)
#define ORBIT_EXECUTOR_QUEUE_FULL INT32_C(-2147483447)
#define ORBIT_EXECUTOR_CLOSED INT32_C(-2147483446)
#define ORBIT_EXECUTOR_CANCELLED INT32_C(-2147483445)
#define ORBIT_EXECUTOR_NOT_READY INT32_C(-2147483444)
#define ORBIT_EXECUTOR_DUPLICATE_ID INT32_C(-2147483443)
#define ORBIT_EXECUTOR_WOULD_BLOCK INT32_C(-2147483442)
#define ORBIT_EXECUTOR_MALFORMED_RESPONSE INT32_C(-2147483441)

#define ORBIT_EVENT_READY UINT32_C(1)
#define ORBIT_EVENT_INVOCATION_FINISHED UINT32_C(2)
#define ORBIT_EVENT_HOST_REQUEST UINT32_C(3)
#define ORBIT_EVENT_HOST_REQUEST_CANCELLED UINT32_C(4)
#define ORBIT_EVENT_STOPPED UINT32_C(5)

#define ORBIT_STATIC_ASSERT(name, condition) \
  typedef char name[(condition) ? 1 : -1]
ORBIT_STATIC_ASSERT(
    orbit_host_v2_abi_version_prefix,
    offsetof(OrbitHostV2, abi_version) == offsetof(OrbitHostV1, abi_version));
ORBIT_STATIC_ASSERT(
    orbit_host_v2_log_prefix,
    offsetof(OrbitHostV2, log) == offsetof(OrbitHostV1, log));
ORBIT_STATIC_ASSERT(
    orbit_host_v2_alloc_prefix,
    offsetof(OrbitHostV2, alloc) == offsetof(OrbitHostV1, alloc));
ORBIT_STATIC_ASSERT(
    orbit_host_v2_free_prefix,
    offsetof(OrbitHostV2, free) == offsetof(OrbitHostV1, free));
ORBIT_STATIC_ASSERT(
    orbit_host_v2_extension_offset,
    offsetof(OrbitHostV2, struct_size) == sizeof(OrbitHostV1));

typedef struct OrbitExecutorEvent OrbitExecutorEvent;
typedef struct OrbitExecutorJob OrbitExecutorJob;
typedef struct OrbitHostRequestWait OrbitHostRequestWait;

struct OrbitExecutorEvent {
  uint32_t kind;
  uint64_t id;
  uint64_t parent_id;
  int32_t status;
  uint32_t timeout_ms;
  uint8_t *command;
  uint32_t command_len;
  uint8_t *payload;
  uint32_t payload_len;
  OrbitExecutorEvent *next;
};

struct OrbitHostRequestWait {
  uint64_t id;
  int done;
  int32_t status;
  uint8_t *response;
  uint32_t response_len;
  OrbitExecutorEvent *cancel_event;
};

struct OrbitExecutorJob {
  uint64_t id;
  char *command;
  uint32_t command_len;
  uint8_t *request;
  uint32_t request_len;
  int cancelled;
  OrbitHostRequestWait *host_request;
  OrbitExecutorEvent *completion_event;
  OrbitExecutorJob *next;
};

typedef struct OrbitPluginExecutor {
  orbit_mutex_t lock;
  orbit_cond_t work_available;
  orbit_cond_t host_request_completed;
  orbit_cond_t event_space_available;
  orbit_thread_t thread;
  int thread_started;
  int closing;
  int stopped;
  int joined;
  int ready;
#ifdef _WIN32
  unsigned worker_thread_id;
#else
  pthread_t worker_thread_id;
#endif
  int worker_identity_ready;
  uint64_t next_host_request_id;
  uint32_t queued_count;
  uint64_t queued_bytes;
  uint32_t outstanding_count;
  uint64_t event_bytes;
  OrbitExecutorJob *jobs_head;
  OrbitExecutorJob *jobs_tail;
  OrbitExecutorJob *current_job;
  OrbitExecutorEvent *events_head;
  OrbitExecutorEvent *events_tail;
  OrbitExecutorEvent *ready_event;
  OrbitExecutorEvent *stopped_event;
  uint64_t create_address;
  uint64_t invoke_address;
  uint64_t destroy_address;
  void *instance;
  void (*wakeup)(void);
  OrbitHostV2 host;
} OrbitPluginExecutor;

static void orbit_executor_write_u32(uint8_t *out, uint32_t value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
  out[2] = (uint8_t)(value >> 16);
  out[3] = (uint8_t)(value >> 24);
}

static void orbit_executor_write_u64(uint8_t *out, uint64_t value) {
  for (uint32_t index = 0; index < 8; index++) {
    out[index] = (uint8_t)(value >> (index * 8));
  }
}

static void *orbit_executor_alloc(uint32_t size) {
  return malloc(size == 0 ? 1 : (size_t)size);
}

static void orbit_executor_free(void *pointer) { free(pointer); }

static void orbit_executor_log(
    uint32_t level,
    const uint8_t *message,
    uint32_t message_len) {
  (void)level;
  (void)message;
  (void)message_len;
}

static uint8_t *orbit_executor_copy(const uint8_t *source, uint32_t length) {
  if (length == 0) return NULL;
  if (source == NULL) return NULL;
  uint8_t *copy = (uint8_t *)malloc((size_t)length);
  if (copy != NULL) memcpy(copy, source, (size_t)length);
  return copy;
}

static void orbit_executor_free_event(OrbitExecutorEvent *event) {
  if (event == NULL) return;
  free(event->command);
  free(event->payload);
  free(event);
}

static void orbit_executor_free_job(OrbitExecutorJob *job) {
  if (job == NULL) return;
  free(job->command);
  free(job->request);
  orbit_executor_free_event(job->completion_event);
  free(job);
}

static uint64_t orbit_executor_event_bytes(OrbitExecutorEvent *event) {
  return event == NULL
             ? 0
             : (uint64_t)event->command_len + (uint64_t)event->payload_len;
}

static OrbitExecutorEvent *orbit_executor_new_event(
    uint32_t kind,
    uint64_t id,
    uint64_t parent_id,
    int32_t status,
    uint32_t timeout_ms,
    const uint8_t *command,
    uint32_t command_len,
    const uint8_t *payload,
    uint32_t payload_len) {
  OrbitExecutorEvent *event =
      (OrbitExecutorEvent *)calloc(1, sizeof(OrbitExecutorEvent));
  if (event == NULL) return NULL;
  event->kind = kind;
  event->id = id;
  event->parent_id = parent_id;
  event->status = status;
  event->timeout_ms = timeout_ms;
  event->command_len = command_len;
  event->payload_len = payload_len;
  if (command_len > 0) {
    event->command = orbit_executor_copy(command, command_len);
    if (event->command == NULL) {
      orbit_executor_free_event(event);
      return NULL;
    }
  }
  if (payload_len > 0) {
    event->payload = orbit_executor_copy(payload, payload_len);
    if (event->payload == NULL) {
      orbit_executor_free_event(event);
      return NULL;
    }
  }
  return event;
}

static void orbit_executor_queue_event_locked(
    OrbitPluginExecutor *executor,
    OrbitExecutorEvent *event) {
  if (event == NULL) return;
  if (executor->events_tail == NULL) {
    executor->events_head = event;
  } else {
    executor->events_tail->next = event;
  }
  executor->events_tail = event;
  executor->event_bytes += orbit_executor_event_bytes(event);
}

static void orbit_executor_publish_event(
    OrbitPluginExecutor *executor,
    OrbitExecutorEvent *event) {
  if (event == NULL) return;
  orbit_mutex_lock(&executor->lock);
  uint64_t event_bytes = orbit_executor_event_bytes(event);
  while (executor->event_bytes + event_bytes >
         ORBIT_EXECUTOR_MAX_EVENT_BYTES) {
    orbit_cond_wait(&executor->event_space_available, &executor->lock);
  }
  orbit_executor_queue_event_locked(executor, event);
  orbit_mutex_unlock(&executor->lock);
  executor->wakeup();
}

static int orbit_executor_on_worker_thread(OrbitPluginExecutor *executor) {
  if (!executor->worker_identity_ready) return 0;
#ifdef _WIN32
  return GetCurrentThreadId() == executor->worker_thread_id;
#else
  return pthread_equal(pthread_self(), executor->worker_thread_id) != 0;
#endif
}

static int orbit_executor_command_valid(
    const uint8_t *command,
    uint32_t command_len) {
  return command != NULL && command_len > 0 &&
         command_len <= ORBIT_EXECUTOR_MAX_COMMAND_BYTES &&
         memchr(command, 0, command_len) == NULL;
}

static int32_t orbit_executor_host_request(
    void *host_context,
    const uint8_t *command,
    uint32_t command_len,
    const uint8_t *request,
    uint32_t request_len,
    uint32_t timeout_ms,
    OrbitBuffer *out_response) {
  OrbitPluginExecutor *executor = (OrbitPluginExecutor *)host_context;
  if (out_response != NULL) {
    out_response->data = NULL;
    out_response->len = 0;
  }
  if (executor == NULL || out_response == NULL ||
      !orbit_executor_command_valid(command, command_len) ||
      request_len > ORBIT_EXECUTOR_MAX_REQUEST_BYTES ||
      (request_len > 0 && request == NULL)) {
    return ORBIT_HOST_REQUEST_INVALID_INPUT;
  }
  if (!orbit_executor_on_worker_thread(executor)) {
    return ORBIT_HOST_REQUEST_UNAVAILABLE;
  }

  OrbitExecutorEvent *event = orbit_executor_new_event(
      ORBIT_EVENT_HOST_REQUEST,
      0,
      0,
      ORBIT_HOST_REQUEST_OK,
      timeout_ms,
      command,
      command_len,
      request,
      request_len);
  if (event == NULL) return ORBIT_HOST_REQUEST_UNAVAILABLE;
  OrbitExecutorEvent *cancel_event = orbit_executor_new_event(
      ORBIT_EVENT_HOST_REQUEST_CANCELLED,
      0,
      0,
      ORBIT_HOST_REQUEST_CANCELLED,
      0,
      NULL,
      0,
      NULL,
      0);
  if (cancel_event == NULL) {
    orbit_executor_free_event(event);
    return ORBIT_HOST_REQUEST_UNAVAILABLE;
  }

  OrbitHostRequestWait wait = {
      0, 0, ORBIT_HOST_REQUEST_UNAVAILABLE, NULL, 0, cancel_event};
  orbit_mutex_lock(&executor->lock);
  OrbitExecutorJob *job = executor->current_job;
  if (executor->closing || job == NULL || job->cancelled ||
      job->host_request != NULL) {
    int32_t rejection = executor->closing ? ORBIT_HOST_REQUEST_CLOSED
                                          : ORBIT_HOST_REQUEST_UNAVAILABLE;
    orbit_mutex_unlock(&executor->lock);
    orbit_executor_free_event(event);
    orbit_executor_free_event(cancel_event);
    return rejection;
  }
  uint64_t event_bytes = orbit_executor_event_bytes(event);
  while (!executor->closing && !job->cancelled &&
         executor->event_bytes + event_bytes >
             ORBIT_EXECUTOR_MAX_EVENT_BYTES) {
    orbit_cond_wait(&executor->event_space_available, &executor->lock);
  }
  if (executor->closing || job->cancelled) {
    int32_t rejection = executor->closing ? ORBIT_HOST_REQUEST_CLOSED
                                          : ORBIT_HOST_REQUEST_CANCELLED;
    orbit_mutex_unlock(&executor->lock);
    orbit_executor_free_event(event);
    orbit_executor_free_event(cancel_event);
    return rejection;
  }
  wait.id = ++executor->next_host_request_id;
  if (wait.id == 0) wait.id = ++executor->next_host_request_id;
  job->host_request = &wait;
  event->id = wait.id;
  event->parent_id = job->id;
  orbit_executor_queue_event_locked(executor, event);
  orbit_mutex_unlock(&executor->lock);
  executor->wakeup();

  orbit_mutex_lock(&executor->lock);
  while (!wait.done && !job->cancelled && !executor->closing) {
    orbit_cond_wait(&executor->host_request_completed, &executor->lock);
  }
  if (!wait.done) {
    wait.status = executor->closing ? ORBIT_HOST_REQUEST_CLOSED
                                    : ORBIT_HOST_REQUEST_CANCELLED;
  }
  job->host_request = NULL;
  orbit_mutex_unlock(&executor->lock);

  orbit_executor_free_event(wait.cancel_event);

  if (wait.status == ORBIT_HOST_REQUEST_OK) {
    out_response->data = wait.response;
    out_response->len = wait.response_len;
  } else {
    free(wait.response);
  }
  return wait.status;
}

static uint32_t orbit_executor_invocation_cancelled(void *host_context) {
  OrbitPluginExecutor *executor = (OrbitPluginExecutor *)host_context;
  if (executor == NULL || !orbit_executor_on_worker_thread(executor)) return 1;
  orbit_mutex_lock(&executor->lock);
  uint32_t cancelled =
      executor->closing || executor->current_job == NULL ||
              executor->current_job->cancelled
          ? 1U
          : 0U;
  orbit_mutex_unlock(&executor->lock);
  return cancelled;
}

static void orbit_executor_publish_completion(
    OrbitPluginExecutor *executor,
    OrbitExecutorJob *job,
    int32_t status,
    uint8_t *response,
    uint32_t response_len) {
  OrbitExecutorEvent *event = job->completion_event;
  job->completion_event = NULL;
  event->id = job->id;
  event->status = status;
  event->payload = response;
  event->payload_len = response_len;
  orbit_mutex_lock(&executor->lock);
  uint64_t event_bytes = orbit_executor_event_bytes(event);
  while (executor->event_bytes + event_bytes >
         ORBIT_EXECUTOR_MAX_EVENT_BYTES) {
    orbit_cond_wait(&executor->event_space_available, &executor->lock);
  }
  if (executor->current_job == job &&
      (job->cancelled || executor->closing)) {
    free(event->payload);
    event->payload = NULL;
    event->payload_len = 0;
    event->status = ORBIT_EXECUTOR_CANCELLED;
  }
  orbit_executor_queue_event_locked(executor, event);
  orbit_mutex_unlock(&executor->lock);
  executor->wakeup();
}

static ORBIT_THREAD_RESULT orbit_executor_worker(void *raw_executor) {
  OrbitPluginExecutor *executor = (OrbitPluginExecutor *)raw_executor;
  orbit_mutex_lock(&executor->lock);
#ifdef _WIN32
  executor->worker_thread_id = GetCurrentThreadId();
#else
  executor->worker_thread_id = pthread_self();
#endif
  executor->worker_identity_ready = 1;
  orbit_mutex_unlock(&executor->lock);
  orbit_plugin_create_v2_fn create =
      (orbit_plugin_create_v2_fn)(uintptr_t)executor->create_address;
  orbit_plugin_invoke_fn invoke =
      (orbit_plugin_invoke_fn)(uintptr_t)executor->invoke_address;
  orbit_plugin_destroy_fn destroy =
      (orbit_plugin_destroy_fn)(uintptr_t)executor->destroy_address;

  int32_t create_status = create(&executor->host, &executor->instance);
  if (create_status == 0 && executor->instance == NULL) {
    create_status = ORBIT_EXECUTOR_NOT_READY;
  }
  orbit_mutex_lock(&executor->lock);
  executor->ready = create_status == 0;
  if (create_status != 0) executor->closing = 1;
  orbit_mutex_unlock(&executor->lock);
  OrbitExecutorEvent *ready_event = executor->ready_event;
  executor->ready_event = NULL;
  ready_event->status = create_status;
  orbit_executor_publish_event(executor, ready_event);

  if (create_status != 0) {
    orbit_mutex_lock(&executor->lock);
    OrbitExecutorJob *failed_job = executor->jobs_head;
    executor->jobs_head = NULL;
    executor->jobs_tail = NULL;
    executor->queued_count = 0;
    executor->queued_bytes = 0;
    orbit_mutex_unlock(&executor->lock);
    while (failed_job != NULL) {
      OrbitExecutorJob *next = failed_job->next;
      orbit_executor_publish_completion(
          executor, failed_job, create_status, NULL, 0);
      orbit_executor_free_job(failed_job);
      failed_job = next;
    }
  }

  while (create_status == 0) {
    orbit_mutex_lock(&executor->lock);
    while (executor->jobs_head == NULL && !executor->closing) {
      orbit_cond_wait(&executor->work_available, &executor->lock);
    }
    if (executor->jobs_head == NULL && executor->closing) {
      orbit_mutex_unlock(&executor->lock);
      break;
    }
    OrbitExecutorJob *job = executor->jobs_head;
    executor->jobs_head = job->next;
    if (executor->jobs_head == NULL) executor->jobs_tail = NULL;
    executor->queued_count--;
    executor->queued_bytes -=
        (uint64_t)job->command_len + (uint64_t)job->request_len;
    job->next = NULL;
    executor->current_job = job;
    int cancelled_before_start = job->cancelled || executor->closing;
    orbit_mutex_unlock(&executor->lock);

    if (cancelled_before_start) {
      orbit_executor_publish_completion(
          executor, job, ORBIT_EXECUTOR_CANCELLED, NULL, 0);
    } else {
      OrbitBuffer response = {NULL, 0};
      int32_t status = invoke(
          executor->instance,
          job->command,
          job->request,
          job->request_len,
          &response);
      orbit_mutex_lock(&executor->lock);
      int cancelled = job->cancelled || executor->closing;
      orbit_mutex_unlock(&executor->lock);
      if (cancelled) {
        if (response.data != NULL) executor->host.free(response.data);
        orbit_executor_publish_completion(
            executor, job, ORBIT_EXECUTOR_CANCELLED, NULL, 0);
      } else if (status != 0) {
        if (response.data != NULL) executor->host.free(response.data);
        orbit_executor_publish_completion(executor, job, status, NULL, 0);
      } else if (response.len > ORBIT_EXECUTOR_MAX_RESPONSE_BYTES ||
                 (response.len > 0 && response.data == NULL)) {
        if (response.data != NULL) executor->host.free(response.data);
        orbit_executor_publish_completion(
            executor, job, ORBIT_EXECUTOR_MALFORMED_RESPONSE, NULL, 0);
      } else {
        orbit_executor_publish_completion(
            executor, job, ORBIT_EXECUTOR_OK, response.data, response.len);
      }
    }

    orbit_mutex_lock(&executor->lock);
    executor->current_job = NULL;
    orbit_mutex_unlock(&executor->lock);
    orbit_executor_free_job(job);
  }

  if (executor->instance != NULL) {
    destroy(executor->instance);
    executor->instance = NULL;
  }
  orbit_mutex_lock(&executor->lock);
  executor->ready = 0;
  OrbitExecutorEvent *stopped_event = executor->stopped_event;
  executor->stopped_event = NULL;
  orbit_executor_queue_event_locked(executor, stopped_event);
  executor->stopped = 1;
  orbit_mutex_unlock(&executor->lock);
  executor->wakeup();
#ifdef _WIN32
  return 0;
#else
  return NULL;
#endif
}

MOONBIT_FFI_EXPORT uint64_t orbit_plugin_executor_start(
    uint32_t abi_version,
    uint64_t create_address,
    uint64_t invoke_address,
    uint64_t destroy_address,
    void (*wakeup)(void)) {
  if (abi_version != ORBIT_PLUGIN_ABI_VERSION_V2 || create_address == 0 ||
      invoke_address == 0 || destroy_address == 0 || wakeup == NULL) {
    return 0;
  }
  OrbitPluginExecutor *executor =
      (OrbitPluginExecutor *)calloc(1, sizeof(OrbitPluginExecutor));
  if (executor == NULL) return 0;
  orbit_mutex_init(&executor->lock);
  orbit_cond_init(&executor->work_available);
  orbit_cond_init(&executor->host_request_completed);
  orbit_cond_init(&executor->event_space_available);
  executor->ready_event = orbit_executor_new_event(
      ORBIT_EVENT_READY, 0, 0, ORBIT_EXECUTOR_OK, 0, NULL, 0, NULL, 0);
  executor->stopped_event = orbit_executor_new_event(
      ORBIT_EVENT_STOPPED, 0, 0, ORBIT_EXECUTOR_OK, 0, NULL, 0, NULL, 0);
  if (executor->ready_event == NULL || executor->stopped_event == NULL) {
    orbit_executor_free_event(executor->ready_event);
    orbit_executor_free_event(executor->stopped_event);
    orbit_cond_destroy(&executor->event_space_available);
    orbit_cond_destroy(&executor->host_request_completed);
    orbit_cond_destroy(&executor->work_available);
    orbit_mutex_destroy(&executor->lock);
    free(executor);
    return 0;
  }
  executor->create_address = create_address;
  executor->invoke_address = invoke_address;
  executor->destroy_address = destroy_address;
  executor->wakeup = wakeup;
  executor->host.abi_version = ORBIT_PLUGIN_ABI_VERSION_V2;
  executor->host.log = orbit_executor_log;
  executor->host.alloc = orbit_executor_alloc;
  executor->host.free = orbit_executor_free;
  executor->host.struct_size = (uint32_t)sizeof(OrbitHostV2);
  executor->host.flags = ORBIT_HOST_V2_FLAG_REQUEST;
  executor->host.host_context = executor;
  executor->host.request = orbit_executor_host_request;
  executor->host.invocation_cancelled = orbit_executor_invocation_cancelled;

#ifdef _WIN32
  executor->thread = (HANDLE)_beginthreadex(
      NULL,
      0,
      orbit_executor_worker,
      executor,
      0,
      &executor->worker_thread_id);
  executor->thread_started = executor->thread != NULL;
#else
  executor->thread_started =
      pthread_create(&executor->thread, NULL, orbit_executor_worker, executor) == 0;
#endif
  if (!executor->thread_started) {
    orbit_executor_free_event(executor->ready_event);
    orbit_executor_free_event(executor->stopped_event);
    orbit_cond_destroy(&executor->event_space_available);
    orbit_cond_destroy(&executor->host_request_completed);
    orbit_cond_destroy(&executor->work_available);
    orbit_mutex_destroy(&executor->lock);
    free(executor);
    return 0;
  }
  return (uint64_t)(uintptr_t)executor;
}

MOONBIT_FFI_EXPORT int32_t orbit_plugin_executor_submit(
    uint64_t executor_address,
    uint64_t invocation_id,
    moonbit_bytes_t command,
    moonbit_bytes_t request) {
  OrbitPluginExecutor *executor =
      (OrbitPluginExecutor *)(uintptr_t)executor_address;
  if (executor == NULL || invocation_id == 0 || command == NULL ||
      request == NULL) {
    return ORBIT_EXECUTOR_INVALID_INPUT;
  }
  int32_t command_length = Moonbit_array_length(command);
  int32_t request_length = Moonbit_array_length(request);
  if (command_length <= 0 || request_length < 0 ||
      !orbit_executor_command_valid(command, (uint32_t)command_length) ||
      (uint32_t)request_length > ORBIT_EXECUTOR_MAX_REQUEST_BYTES) {
    return ORBIT_EXECUTOR_INVALID_INPUT;
  }
  OrbitExecutorJob *job =
      (OrbitExecutorJob *)calloc(1, sizeof(OrbitExecutorJob));
  if (job == NULL) return ORBIT_EXECUTOR_QUEUE_FULL;
  job->completion_event = orbit_executor_new_event(
      ORBIT_EVENT_INVOCATION_FINISHED,
      invocation_id,
      0,
      ORBIT_EXECUTOR_OK,
      0,
      NULL,
      0,
      NULL,
      0);
  if (job->completion_event == NULL) {
    orbit_executor_free_job(job);
    return ORBIT_EXECUTOR_QUEUE_FULL;
  }
  job->id = invocation_id;
  job->command_len = (uint32_t)command_length;
  job->request_len = (uint32_t)request_length;
  job->command = (char *)malloc((size_t)job->command_len + 1);
  job->request = orbit_executor_copy(request, job->request_len);
  if (job->command == NULL || (job->request_len > 0 && job->request == NULL)) {
    orbit_executor_free_job(job);
    return ORBIT_EXECUTOR_QUEUE_FULL;
  }
  memcpy(job->command, command, job->command_len);
  job->command[job->command_len] = '\0';

  orbit_mutex_lock(&executor->lock);
  if (executor->closing || executor->stopped) {
    orbit_mutex_unlock(&executor->lock);
    orbit_executor_free_job(job);
    return ORBIT_EXECUTOR_CLOSED;
  }
  if (executor->current_job != NULL &&
      executor->current_job->id == invocation_id) {
    orbit_mutex_unlock(&executor->lock);
    orbit_executor_free_job(job);
    return ORBIT_EXECUTOR_DUPLICATE_ID;
  }
  for (OrbitExecutorJob *item = executor->jobs_head; item != NULL;
       item = item->next) {
    if (item->id == invocation_id) {
      orbit_mutex_unlock(&executor->lock);
      orbit_executor_free_job(job);
      return ORBIT_EXECUTOR_DUPLICATE_ID;
    }
  }
  for (OrbitExecutorEvent *event = executor->events_head; event != NULL;
       event = event->next) {
    if (event->kind == ORBIT_EVENT_INVOCATION_FINISHED &&
        event->id == invocation_id) {
      orbit_mutex_unlock(&executor->lock);
      orbit_executor_free_job(job);
      return ORBIT_EXECUTOR_DUPLICATE_ID;
    }
  }
  uint64_t job_bytes = (uint64_t)job->command_len + job->request_len;
  if (executor->outstanding_count >= ORBIT_EXECUTOR_MAX_QUEUED_INVOCATIONS ||
      executor->queued_count >= ORBIT_EXECUTOR_MAX_QUEUED_INVOCATIONS ||
      executor->queued_bytes + job_bytes > ORBIT_EXECUTOR_MAX_QUEUED_BYTES) {
    orbit_mutex_unlock(&executor->lock);
    orbit_executor_free_job(job);
    return ORBIT_EXECUTOR_QUEUE_FULL;
  }
  if (executor->jobs_tail == NULL) {
    executor->jobs_head = job;
  } else {
    executor->jobs_tail->next = job;
  }
  executor->jobs_tail = job;
  executor->queued_count++;
  executor->outstanding_count++;
  executor->queued_bytes += job_bytes;
  orbit_cond_signal(&executor->work_available);
  orbit_mutex_unlock(&executor->lock);
  return ORBIT_EXECUTOR_OK;
}

MOONBIT_FFI_EXPORT int32_t orbit_plugin_executor_cancel(
    uint64_t executor_address,
    uint64_t invocation_id) {
  OrbitPluginExecutor *executor =
      (OrbitPluginExecutor *)(uintptr_t)executor_address;
  if (executor == NULL || invocation_id == 0) {
    return ORBIT_EXECUTOR_INVALID_INPUT;
  }
  OrbitExecutorEvent *completion = NULL;
  OrbitExecutorEvent *host_cancelled = NULL;
  orbit_mutex_lock(&executor->lock);
  OrbitExecutorJob *previous = NULL;
  OrbitExecutorJob *job = executor->jobs_head;
  while (job != NULL && job->id != invocation_id) {
    previous = job;
    job = job->next;
  }
  if (job != NULL) {
    if (previous == NULL) executor->jobs_head = job->next;
    else previous->next = job->next;
    if (executor->jobs_tail == job) executor->jobs_tail = previous;
    executor->queued_count--;
    executor->queued_bytes -=
        (uint64_t)job->command_len + (uint64_t)job->request_len;
    completion = job->completion_event;
    job->completion_event = NULL;
    completion->status = ORBIT_EXECUTOR_CANCELLED;
  } else if (executor->current_job != NULL &&
             executor->current_job->id == invocation_id) {
    executor->current_job->cancelled = 1;
    OrbitHostRequestWait *wait = executor->current_job->host_request;
    if (wait != NULL && !wait->done) {
      wait->done = 1;
      wait->status = ORBIT_HOST_REQUEST_CANCELLED;
      host_cancelled = wait->cancel_event;
      wait->cancel_event = NULL;
      host_cancelled->id = wait->id;
      host_cancelled->parent_id = invocation_id;
      host_cancelled->status = ORBIT_HOST_REQUEST_CANCELLED;
      orbit_cond_broadcast(&executor->host_request_completed);
    }
  } else {
    orbit_mutex_unlock(&executor->lock);
    return ORBIT_EXECUTOR_NOT_READY;
  }
  orbit_executor_queue_event_locked(executor, completion);
  orbit_executor_queue_event_locked(executor, host_cancelled);
  orbit_cond_broadcast(&executor->event_space_available);
  orbit_mutex_unlock(&executor->lock);
  if (completion != NULL || host_cancelled != NULL) executor->wakeup();
  if (job != NULL) orbit_executor_free_job(job);
  return ORBIT_EXECUTOR_OK;
}

MOONBIT_FFI_EXPORT moonbit_bytes_t orbit_plugin_executor_poll_event(
    uint64_t executor_address) {
  OrbitPluginExecutor *executor =
      (OrbitPluginExecutor *)(uintptr_t)executor_address;
  if (executor == NULL) return moonbit_make_bytes(0, 0);
  orbit_mutex_lock(&executor->lock);
  OrbitExecutorEvent *event = executor->events_head;
  if (event == NULL) {
    orbit_mutex_unlock(&executor->lock);
    return moonbit_make_bytes(0, 0);
  }
  uint64_t total = UINT64_C(40) + event->command_len + event->payload_len;
  if (total > INT32_MAX) {
    orbit_mutex_unlock(&executor->lock);
    return moonbit_make_bytes(0, 0);
  }
  moonbit_bytes_t frame = moonbit_make_bytes((int32_t)total, 0);
  if (frame == NULL) {
    orbit_mutex_unlock(&executor->lock);
    return moonbit_make_bytes(0, 0);
  }
  executor->events_head = event->next;
  if (executor->events_head == NULL) executor->events_tail = NULL;
  executor->event_bytes -= orbit_executor_event_bytes(event);
  if (event->kind == ORBIT_EVENT_INVOCATION_FINISHED &&
      executor->outstanding_count > 0) {
    executor->outstanding_count--;
  }
  orbit_cond_broadcast(&executor->event_space_available);
  orbit_mutex_unlock(&executor->lock);
  orbit_executor_write_u32(frame, event->kind);
  orbit_executor_write_u32(frame + 4, (uint32_t)event->status);
  orbit_executor_write_u64(frame + 8, event->id);
  orbit_executor_write_u64(frame + 16, event->parent_id);
  orbit_executor_write_u32(frame + 24, event->timeout_ms);
  orbit_executor_write_u32(frame + 28, event->command_len);
  orbit_executor_write_u32(frame + 32, event->payload_len);
  if (event->command_len > 0) {
    memcpy(frame + 40, event->command, event->command_len);
  }
  if (event->payload_len > 0) {
    memcpy(
        frame + 40 + event->command_len,
        event->payload,
        event->payload_len);
  }
  orbit_executor_free_event(event);
  return frame;
}

MOONBIT_FFI_EXPORT int32_t orbit_plugin_executor_complete_host_request(
    uint64_t executor_address,
    uint64_t request_id,
    moonbit_bytes_t response) {
  OrbitPluginExecutor *executor =
      (OrbitPluginExecutor *)(uintptr_t)executor_address;
  if (executor == NULL || request_id == 0 || response == NULL) {
    return ORBIT_EXECUTOR_INVALID_INPUT;
  }
  int32_t response_length = Moonbit_array_length(response);
  if (response_length < 0 ||
      (uint32_t)response_length > ORBIT_EXECUTOR_MAX_RESPONSE_BYTES) {
    return ORBIT_EXECUTOR_INVALID_INPUT;
  }
  uint8_t *copy = orbit_executor_copy(response, (uint32_t)response_length);
  if (response_length > 0 && copy == NULL) return ORBIT_EXECUTOR_QUEUE_FULL;
  orbit_mutex_lock(&executor->lock);
  OrbitHostRequestWait *wait = executor->current_job == NULL
                                   ? NULL
                                   : executor->current_job->host_request;
  if (wait == NULL || wait->id != request_id || wait->done) {
    orbit_mutex_unlock(&executor->lock);
    free(copy);
    return ORBIT_EXECUTOR_NOT_READY;
  }
  wait->response = copy;
  wait->response_len = (uint32_t)response_length;
  wait->status = ORBIT_HOST_REQUEST_OK;
  wait->done = 1;
  orbit_cond_broadcast(&executor->host_request_completed);
  orbit_mutex_unlock(&executor->lock);
  return ORBIT_EXECUTOR_OK;
}

MOONBIT_FFI_EXPORT int32_t orbit_plugin_executor_cancel_host_request(
    uint64_t executor_address,
    uint64_t request_id,
    int32_t status) {
  OrbitPluginExecutor *executor =
      (OrbitPluginExecutor *)(uintptr_t)executor_address;
  if (executor == NULL || request_id == 0 || status == 0) {
    return ORBIT_EXECUTOR_INVALID_INPUT;
  }
  orbit_mutex_lock(&executor->lock);
  OrbitHostRequestWait *wait = executor->current_job == NULL
                                   ? NULL
                                   : executor->current_job->host_request;
  if (wait == NULL || wait->id != request_id || wait->done) {
    orbit_mutex_unlock(&executor->lock);
    return ORBIT_EXECUTOR_NOT_READY;
  }
  wait->status = status;
  wait->done = 1;
  orbit_cond_broadcast(&executor->host_request_completed);
  orbit_mutex_unlock(&executor->lock);
  return ORBIT_EXECUTOR_OK;
}

MOONBIT_FFI_EXPORT void orbit_plugin_executor_begin_shutdown(
    uint64_t executor_address) {
  OrbitPluginExecutor *executor =
      (OrbitPluginExecutor *)(uintptr_t)executor_address;
  if (executor == NULL) return;
  OrbitExecutorEvent *cancelled_head = NULL;
  OrbitExecutorEvent *cancelled_tail = NULL;
  orbit_mutex_lock(&executor->lock);
  if (!executor->closing) executor->closing = 1;
  OrbitExecutorJob *job = executor->jobs_head;
  executor->jobs_head = NULL;
  executor->jobs_tail = NULL;
  executor->queued_count = 0;
  executor->queued_bytes = 0;
  while (job != NULL) {
    OrbitExecutorJob *next = job->next;
    OrbitExecutorEvent *event = job->completion_event;
    job->completion_event = NULL;
    event->status = ORBIT_EXECUTOR_CANCELLED;
    if (cancelled_tail == NULL) cancelled_head = event;
    else cancelled_tail->next = event;
    cancelled_tail = event;
    orbit_executor_free_job(job);
    job = next;
  }
  if (executor->current_job != NULL) {
    executor->current_job->cancelled = 1;
    OrbitHostRequestWait *wait = executor->current_job->host_request;
    if (wait != NULL && !wait->done) {
      wait->done = 1;
      wait->status = ORBIT_HOST_REQUEST_CLOSED;
      OrbitExecutorEvent *event = wait->cancel_event;
      wait->cancel_event = NULL;
      event->id = wait->id;
      event->parent_id = executor->current_job->id;
      event->status = ORBIT_HOST_REQUEST_CLOSED;
      orbit_executor_queue_event_locked(executor, event);
    }
  }
  while (cancelled_head != NULL) {
    OrbitExecutorEvent *next = cancelled_head->next;
    cancelled_head->next = NULL;
    orbit_executor_queue_event_locked(executor, cancelled_head);
    cancelled_head = next;
  }
  orbit_cond_broadcast(&executor->host_request_completed);
  orbit_cond_broadcast(&executor->work_available);
  orbit_cond_broadcast(&executor->event_space_available);
  orbit_mutex_unlock(&executor->lock);
  executor->wakeup();
}

MOONBIT_FFI_EXPORT int32_t orbit_plugin_executor_join_stopped(
    uint64_t executor_address) {
  OrbitPluginExecutor *executor =
      (OrbitPluginExecutor *)(uintptr_t)executor_address;
  if (executor == NULL) return ORBIT_EXECUTOR_INVALID_INPUT;
  orbit_mutex_lock(&executor->lock);
  int stopped = executor->stopped;
  orbit_mutex_unlock(&executor->lock);
  if (!stopped) return ORBIT_EXECUTOR_WOULD_BLOCK;
  if (!executor->joined) {
#ifdef _WIN32
    (void)WaitForSingleObject(executor->thread, INFINITE);
    (void)CloseHandle(executor->thread);
#else
    (void)pthread_join(executor->thread, NULL);
#endif
    executor->joined = 1;
  }
  OrbitExecutorJob *job = executor->jobs_head;
  while (job != NULL) {
    OrbitExecutorJob *next = job->next;
    orbit_executor_free_job(job);
    job = next;
  }
  OrbitExecutorEvent *event = executor->events_head;
  while (event != NULL) {
    OrbitExecutorEvent *next = event->next;
    orbit_executor_free_event(event);
    event = next;
  }
  orbit_executor_free_event(executor->ready_event);
  orbit_executor_free_event(executor->stopped_event);
  orbit_cond_destroy(&executor->event_space_available);
  orbit_cond_destroy(&executor->host_request_completed);
  orbit_cond_destroy(&executor->work_available);
  orbit_mutex_destroy(&executor->lock);
  free(executor);
  return ORBIT_EXECUTOR_OK;
}
