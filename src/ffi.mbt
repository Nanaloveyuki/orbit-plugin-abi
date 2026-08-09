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

///|
extern "C" fn native_executor_start(
  abi_version : UInt,
  create_address : UInt64,
  invoke_address : UInt64,
  destroy_address : UInt64,
  wakeup_callback : FuncRef[() -> Unit],
) -> UInt64 = "orbit_plugin_executor_start"

///|
#borrow(command, request)
extern "C" fn native_executor_submit(
  executor : UInt64,
  invocation_id : UInt64,
  command : Bytes,
  request : Bytes,
) -> Int = "orbit_plugin_executor_submit"

///|
extern "C" fn native_executor_cancel(
  executor : UInt64,
  invocation_id : UInt64,
) -> Int = "orbit_plugin_executor_cancel"

///|
extern "C" fn native_executor_poll_event(executor : UInt64) -> Bytes = "orbit_plugin_executor_poll_event"

///|
#borrow(response)
extern "C" fn native_executor_complete_host_request(
  executor : UInt64,
  request_id : UInt64,
  response : Bytes,
) -> Int = "orbit_plugin_executor_complete_host_request"

///|
extern "C" fn native_executor_cancel_host_request(
  executor : UInt64,
  request_id : UInt64,
  status : Int,
) -> Int = "orbit_plugin_executor_cancel_host_request"

///|
extern "C" fn native_executor_begin_shutdown(executor : UInt64) -> Unit = "orbit_plugin_executor_begin_shutdown"

///|
extern "C" fn native_executor_join_stopped(executor : UInt64) -> Int = "orbit_plugin_executor_join_stopped"

///|
extern "C" fn native_executor_join(executor : UInt64) -> Int = "orbit_plugin_executor_join"
