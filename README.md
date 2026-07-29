# orbit-plugin-abi

Nanaloveyuki/orbit-plugin-abi is Orbit's native-only, fixed-signature C ABI
bridge. It consumes symbol addresses returned by Nanaloveyuki/dynlib and never
exposes a generic call-by-address API.

## Fixed v1 ABI

Plugins export exactly these C functions:

~~~c
uint32_t orbit_plugin_abi_version(void);
const char *orbit_plugin_manifest_json(void);
int32_t orbit_plugin_create(const OrbitHostV1 *host, void **out_instance);
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
JSON. PluginAddresses::from_symbols obtains five addresses from dynlib Symbol
values; PluginAddresses::new is also available for a host that already holds
validated addresses. Plugin::open validates the ABI; PluginInstance::invoke
only uses the fixed invoke signature above.

## Ownership and lifetime

create receives a process-static const OrbitHostV1 *. A plugin may retain that
pointer until its matching destroy; it must not free or mutate it.
OrbitHostV1::alloc and OrbitHostV1::free define the only supported allocator
boundary. Successful invoke responses must be allocated through the host
allocator. The bridge copies OrbitBuffer.data into MoonBit memory and calls
host.free exactly once before returning.

An empty request is passed as a zero-length request buffer. Commands are
UTF-8, non-empty, contain no embedded NUL, and are capped at 1024 bytes.
Requests and responses are limited to 16 MiB; manifests are limited to 1 MiB.
Non-zero plugin statuses are surfaced as AbiError::PluginStatus.

Destroy is idempotent on the MoonBit side. Keep the originating dynlib library
open until every plugin instance has been destroyed.

## Safety boundary

This package protects the host from accidental ABI mismatch, null addresses,
oversized buffers, invalid command strings, and malformed response buffers.
It cannot make arbitrary native code safe: a plugin is trusted native code.
Plugins must not throw C++ exceptions, panic across the C ABI, retain request
or command pointers after invoke returns, or return memory allocated by an
allocator other than host.alloc.

## Development

~~~sh
moon fmt --check
moon check --target native --deny-warn --warn-list +73
moon test --target native --deny-warn
moon info --target native
~~~
