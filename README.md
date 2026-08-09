# orbit-plugin-abi

Nanaloveyuki/orbit-plugin-abi is Orbit's native-only, fixed-signature C ABI
bridge. It consumes symbol addresses returned by Nanaloveyuki/dynlib and never
exposes a generic call-by-address API.

## Fixed ABI versions

Both ABI versions export exactly these five C functions:

~~~c
uint32_t orbit_plugin_abi_version(void);
const char *orbit_plugin_manifest_json(void);
/* The host argument is OrbitHostV1 for ABI 1 and OrbitHostV2 for ABI 2. */
int32_t orbit_plugin_create(const void *host, void **out_instance);
int32_t orbit_plugin_invoke(
    void *instance,
    const char *command,
    const uint8_t *request,
    uint32_t request_len,
    OrbitBuffer *out_response);
void orbit_plugin_destroy(void *instance);
~~~

orbit_plugin_manifest_json returns a static, NUL-terminated UTF-8 string.
This package copies and validates its UTF-8 but deliberately does not parse
JSON. `PluginAddresses::from_symbols` obtains five addresses from dynlib
`Symbol` values; `PluginAddresses::new` is also available for a host that
already holds validated addresses. `Plugin::open` accepts ABI 1 and ABI 2 and
records the exact reported version.

ABI 1 remains the synchronous compatibility contract. ABI 2 preserves the
complete `OrbitHostV1` layout prefix and adds `struct_size`, feature flags, a
host context, a synchronous host-request callback, and cooperative invocation
cancellation. The five plugin export names and the invoke/destroy signatures
do not change.

## ABI 2 executor

ABI 2 plugins must be created with `Plugin::start_executor`. Each executor
owns one native worker and calls `create`, every `invoke`, and `destroy` on
that same worker in strict FIFO order. MoonBit commands and requests are copied
into a bounded native queue before the submit FFI call returns. Invocation IDs
remain reserved until their completion event is polled. The outstanding count
and native event bytes are also bounded, so an unresponsive host applies
backpressure instead of growing the queue without limit. Completion and host-
request events remain native-owned until `PluginExecutor::poll_event` copies
them on the host thread.

The wakeup argument must be a no-capture `FuncRef[() -> Unit]` that calls only
a foreign-thread-safe native wake primitive. It must not inspect MoonBit heap
state. The intended integration is to wake a host event loop, poll executor
events on the UI/main thread, and signal the async task waiting for that
invocation from there.

A v2 plugin may call `OrbitHostV2::request` only synchronously from the
executor worker while one of its `orbit_plugin_invoke` calls is active. Plugin-
created threads must not call either v2 callback or retain `host_context` for
later callback use. The request call copies the command and JSON request into
the host event queue and blocks the plugin worker until the host completes,
cancels, or shuts down that request. A successful response buffer is allocated
by `host.alloc`; the plugin owns it and must eventually pass it to `host.free`
(or transfer it as its invocation response). `timeout_ms == 0` means inherit
the outer invocation deadline.

Cancellation is cooperative. `OrbitHostV2::invocation_cancelled` observes the
current invocation's native cancellation flag, and cancellation unblocks a
pending host request. Orbit cannot safely preempt arbitrary plugin code. Begin
shutdown first, continue polling until `Stopped`, then call `join_stopped`
before unloading the dynamic library.

## Ownership and lifetime

ABI 1 `create` receives a process-static `const OrbitHostV1 *`. ABI 2 receives
an executor-owned `const OrbitHostV2 *`. A plugin may retain its host pointer
until the matching destroy; it must not free or mutate it. `host.alloc` and
`host.free` define the only supported allocator boundary. Successful invoke
responses must be allocated through the host allocator. The bridge copies
`OrbitBuffer.data` into MoonBit memory and calls `host.free` exactly once.

An empty request is passed as a zero-length request buffer. Commands are
UTF-8, non-empty, contain no embedded NUL, and are capped at 1024 bytes.
Requests and responses are limited to 16 MiB; manifests are limited to 1 MiB.
ABI 1 surfaces non-zero plugin statuses as `AbiError::PluginStatus`. ABI 2
reports them in `ExecutorEvent::InvocationFinished`.

ABI 1 destroy is idempotent on the MoonBit side. Keep the originating dynlib
library open until every v1 instance is destroyed and every v2 executor has
reported `Stopped` and been joined.

## Safety boundary

This package protects the host from accidental ABI mismatch, null addresses,
oversized buffers, invalid command strings, queue overflow, stale completion,
and malformed response buffers. It cannot make arbitrary native code safe: a
plugin is trusted native code. Plugins must not throw C++ exceptions, panic
across the C ABI, retain request or command pointers after invoke returns, or
return memory allocated by an allocator other than `host.alloc`.

## Development

~~~sh
moon fmt --check
moon check --target native --deny-warn --warn-list +73
moon test --target native --deny-warn
moon info --target native
~~~
