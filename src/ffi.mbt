///|
extern "C" fn native_abi_version(address : UInt64) -> UInt = "orbit_plugin_abi_call_version"

///|
extern "C" fn native_manifest(address : UInt64) -> Bytes = "orbit_plugin_abi_call_manifest"

///|
extern "C" fn native_create(address : UInt64) -> Bytes = "orbit_plugin_abi_call_create"

///|
#borrow(command, request)
extern "C" fn native_invoke(
  address : UInt64,
  instance : UInt64,
  command : Bytes,
  request : Bytes,
) -> Bytes = "orbit_plugin_abi_call_invoke"

///|
extern "C" fn native_destroy(address : UInt64, instance : UInt64) -> Unit = "orbit_plugin_abi_call_destroy"
