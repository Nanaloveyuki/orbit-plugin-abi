///|
extern "C" fn fixture_abi_version_address() -> UInt64 = "orbit_plugin_fixture_abi_version_address"

///|
extern "C" fn fixture_manifest_address() -> UInt64 = "orbit_plugin_fixture_manifest_address"

///|
extern "C" fn fixture_create_address() -> UInt64 = "orbit_plugin_fixture_create_address"

///|
extern "C" fn fixture_invoke_address() -> UInt64 = "orbit_plugin_fixture_invoke_address"

///|
extern "C" fn fixture_destroy_address() -> UInt64 = "orbit_plugin_fixture_destroy_address"

///|
extern "C" fn fixture_incompatible_abi_address() -> UInt64 = "orbit_plugin_fixture_incompatible_abi_address"

///|
extern "C" fn fixture_v2_abi_version_address() -> UInt64 = "orbit_plugin_fixture_v2_abi_version_address"

///|
extern "C" fn fixture_v2_create_address() -> UInt64 = "orbit_plugin_fixture_v2_create_address"

///|
extern "C" fn fixture_v2_create_failure_address() -> UInt64 = "orbit_plugin_fixture_v2_create_failure_address"

///|
extern "C" fn fixture_v2_invoke_address() -> UInt64 = "orbit_plugin_fixture_v2_invoke_address"

///|
extern "C" fn fixture_v2_destroy_address() -> UInt64 = "orbit_plugin_fixture_v2_destroy_address"

///|
extern "C" fn fixture_v2_thread_mismatch() -> UInt = "orbit_plugin_fixture_v2_thread_mismatch"

///|
extern "C" fn fixture_v2_foreign_request() -> Int = "orbit_plugin_fixture_v2_foreign_request"

///|
extern "C" fn fixture_sleep() -> Unit = "orbit_plugin_fixture_sleep"

///|
extern "C" fn fixture_wakeup() -> Unit = "orbit_plugin_fixture_wakeup"

///|
extern "C" fn fixture_v1_compat_version_address() -> UInt64 = "orbit_plugin_fixture_v1_compat_version_address"

///|
extern "C" fn fixture_v1_compat_manifest_address() -> UInt64 = "orbit_plugin_fixture_v1_compat_manifest_address"

///|
extern "C" fn fixture_v1_compat_create_address() -> UInt64 = "orbit_plugin_fixture_v1_compat_create_address"

///|
extern "C" fn fixture_v1_compat_invoke_address() -> UInt64 = "orbit_plugin_fixture_v1_compat_invoke_address"

///|
extern "C" fn fixture_v1_compat_destroy_address() -> UInt64 = "orbit_plugin_fixture_v1_compat_destroy_address"

///|
fn fixture_wakeup_callback() -> FuncRef[() -> Unit] {
  () => fixture_wakeup()
}

///|
fn fixture_addresses() -> PluginAddresses {
  PluginAddresses::new(
    fixture_abi_version_address(),
    fixture_manifest_address(),
    fixture_create_address(),
    fixture_invoke_address(),
    fixture_destroy_address(),
  ).unwrap()
}

///|
fn fixture_v2_addresses() -> PluginAddresses {
  PluginAddresses::new(
    fixture_v2_abi_version_address(),
    fixture_manifest_address(),
    fixture_v2_create_address(),
    fixture_v2_invoke_address(),
    fixture_v2_destroy_address(),
  ).unwrap()
}

///|
fn fixture_v1_compat_addresses() -> PluginAddresses {
  PluginAddresses::new(
    fixture_v1_compat_version_address(),
    fixture_v1_compat_manifest_address(),
    fixture_v1_compat_create_address(),
    fixture_v1_compat_invoke_address(),
    fixture_v1_compat_destroy_address(),
  ).unwrap()
}

///|
fn fixture_v2_create_failure_addresses() -> PluginAddresses {
  PluginAddresses::new(
    fixture_v2_abi_version_address(),
    fixture_manifest_address(),
    fixture_v2_create_failure_address(),
    fixture_v2_invoke_address(),
    fixture_v2_destroy_address(),
  ).unwrap()
}

///|
fn next_executor_event(executor : PluginExecutor) -> ExecutorEvent {
  for attempt = 0; attempt < 5000; attempt = attempt + 1 {
    match executor.poll_event().unwrap() {
      Some(event) => return event
      None => fixture_sleep()
    }
  }
  abort("executor event timed out")
}

///|
fn start_fixture_v2_executor() -> PluginExecutor {
  let plugin = Plugin::open(fixture_v2_addresses()).unwrap()
  if plugin.abi_version() != V2 {
    abort("v2 fixture reported the wrong ABI")
  }
  match plugin.create() {
    Err(ExecutorRequired) => ()
    _ => abort("v2 plugin accepted synchronous create")
  }
  let executor = plugin.start_executor(fixture_wakeup_callback()).unwrap()
  match next_executor_event(executor) {
    Ready(0) => executor
    _ => abort("v2 executor did not become ready")
  }
}

///|
fn stop_fixture_v2_executor(executor : PluginExecutor) -> Unit {
  executor.begin_shutdown()
  match next_executor_event(executor) {
    Stopped => ()
    _ => abort("v2 executor did not stop")
  }
  executor.join_stopped().unwrap()
  if !executor.is_closed() {
    abort("joined executor remained open")
  }
}

///|
test "address constructor rejects an invalid address without a native call" {
  match
    PluginAddresses::new(
      0UL,
      fixture_manifest_address(),
      fixture_create_address(),
      fixture_invoke_address(),
      fixture_destroy_address(),
    ) {
    Err(InvalidAddress) => ()
    _ => abort("invalid address was accepted")
  }
}

///|
test "open copies and validates the ABI manifest" {
  let plugin = Plugin::open(fixture_addresses()).unwrap()
  inspect(
    plugin.manifest_json(),
    content="{\"name\":\"fixture\",\"commands\":[\"echo\"]}",
  )
  inspect(supported_abi_version(), content="2")
  assert_eq(supported_abi_versions(), [1, 2])
}

///|
test "open rejects an incompatible ABI version" {
  let addresses = PluginAddresses::new(
    fixture_incompatible_abi_address(),
    fixture_manifest_address(),
    fixture_create_address(),
    fixture_invoke_address(),
    fixture_destroy_address(),
  ).unwrap()
  match Plugin::open(addresses) {
    Err(IncompatibleAbi(3)) => ()
    _ => abort("incompatible ABI was accepted")
  }
}

///|
test "create invoke destroy follows the fixed ABI" {
  let plugin = Plugin::open(fixture_addresses()).unwrap()
  let instance = plugin.create().unwrap()
  inspect(
    instance.invoke("echo", b"request").unwrap() == b"request",
    content="true",
  )
  inspect(instance.invoke("echo", b"").unwrap() == b"", content="true")
  match instance.invoke("fail", b"") {
    Err(PluginStatus(-42)) => ()
    _ => abort("plugin error status was not propagated")
  }
  match instance.invoke("malformed", b"") {
    Err(InvalidResponse) => ()
    _ => abort("null response data with a non-zero length was accepted")
  }
  match instance.invoke("oversized", b"") {
    Err(InvalidResponse) => ()
    _ => abort("oversized response was accepted")
  }
  instance.destroy()
  inspect(instance.is_destroyed(), content="true")
  match instance.invoke("echo", b"") {
    Err(Destroyed) => ()
    _ => abort("destroyed instance accepted an invocation")
  }
  instance.destroy()
}

///|
test "v1 plugin compiled from the 0.1.0 declarations remains compatible" {
  let plugin = Plugin::open(fixture_v1_compat_addresses()).unwrap()
  assert_eq(plugin.abi_version(), V1)
  let instance = plugin.create().unwrap()
  assert_eq(instance.invoke("echo", b"legacy").unwrap(), b"legacy")
  instance.destroy()
}

///|
test "v2 executor preserves worker affinity and copies invocation results" {
  let executor = start_fixture_v2_executor()
  executor.submit(1UL, "echo", b"request").unwrap()
  match next_executor_event(executor) {
    InvocationFinished(1UL, 0, response) =>
      inspect(response == b"request", content="true")
    _ => abort("unexpected v2 completion")
  }
  stop_fixture_v2_executor(executor)
  inspect(fixture_v2_thread_mismatch(), content="0")
}

///|
test "v2 create failure completes work queued before ready" {
  let plugin = Plugin::open(fixture_v2_create_failure_addresses()).unwrap()
  let executor = plugin.start_executor(fixture_wakeup_callback()).unwrap()
  executor.submit(20UL, "echo", b"queued").unwrap()
  match next_executor_event(executor) {
    Ready(-120) => ()
    _ => abort("create failure was not reported")
  }
  match next_executor_event(executor) {
    InvocationFinished(20UL, -120, response) => assert_true(response.is_empty())
    _ => abort("queued invocation did not receive create failure")
  }
  match next_executor_event(executor) {
    Stopped => ()
    _ => abort("failed executor did not stop")
  }
  executor.join_stopped().unwrap()
}

///|
test "v2 host request round trips through the host event queue" {
  let executor = start_fixture_v2_executor()
  executor.submit(2UL, "host", b"request").unwrap()
  let request_id = match next_executor_event(executor) {
    HostRequest(request) => {
      inspect(request.invocation_id, content="2")
      inspect(request.command, content="app:echo")
      inspect(request.request == b"request", content="true")
      inspect(request.timeout_ms, content="250")
      request.request_id
    }
    _ => abort("unexpected host request event")
  }
  executor.complete_host_request(request_id, b"response").unwrap()
  match next_executor_event(executor) {
    InvocationFinished(2UL, 0, response) =>
      inspect(response == b"response", content="true")
    _ => abort("unexpected host request completion")
  }
  stop_fixture_v2_executor(executor)
}

///|
test "v2 running invocation observes cooperative cancellation" {
  let executor = start_fixture_v2_executor()
  executor.submit(3UL, "cancel", b"").unwrap()
  fixture_sleep()
  executor.cancel(3UL).unwrap()
  match next_executor_event(executor) {
    InvocationFinished(3UL, status, response) => {
      inspect(status != 0, content="true")
      inspect(response.is_empty(), content="true")
    }
    _ => abort("unexpected cancellation completion")
  }
  stop_fixture_v2_executor(executor)
}

///|
test "v2 malformed response is rejected on the native boundary" {
  let executor = start_fixture_v2_executor()
  executor.submit(4UL, "malformed", b"").unwrap()
  match next_executor_event(executor) {
    InvocationFinished(4UL, status, response) => {
      assert_true(status != 0)
      assert_true(response.is_empty())
    }
    _ => abort("unexpected malformed-response completion")
  }
  stop_fixture_v2_executor(executor)
}

///|
test "v2 cancellation terminates a pending host request once" {
  let executor = start_fixture_v2_executor()
  executor.submit(5UL, "host", b"request").unwrap()
  let request_id = match next_executor_event(executor) {
    HostRequest(request) => request.request_id
    _ => abort("host request was not emitted")
  }
  executor.cancel(5UL).unwrap()
  match next_executor_event(executor) {
    HostRequestCancelled(id, 5UL, status) => {
      assert_eq(id, request_id)
      assert_true(status != 0)
    }
    _ => abort("host request cancellation was not emitted")
  }
  match executor.complete_host_request(request_id, b"late") {
    Err(InvocationUnavailable) => ()
    _ => abort("late host response was accepted")
  }
  match next_executor_event(executor) {
    InvocationFinished(5UL, status, response) => {
      assert_true(status != 0)
      assert_true(response.is_empty())
    }
    _ => abort("cancelled host invocation did not finish")
  }
  stop_fixture_v2_executor(executor)
}

///|
test "v2 executor enforces duplicate IDs and bounded queue capacity" {
  let executor = start_fixture_v2_executor()
  executor.submit(10UL, "cancel", b"").unwrap()
  fixture_sleep()
  match executor.submit(10UL, "echo", b"") {
    Err(DuplicateInvocation) => ()
    _ => abort("duplicate running invocation ID was accepted")
  }
  for offset = 0; offset < 63; offset = offset + 1 {
    executor.submit(100UL + offset.to_uint64(), "echo", b"").unwrap()
  }
  match executor.submit(163UL, "echo", b"") {
    Err(ExecutorQueueFull) => ()
    _ => abort("executor queue limit was not enforced")
  }
  executor.begin_shutdown()
  let mut stopped = false
  for attempt = 0; attempt < 5000 && !stopped; attempt = attempt + 1 {
    match executor.poll_event().unwrap() {
      Some(Stopped) => stopped = true
      Some(_) => ()
      None => fixture_sleep()
    }
  }
  assert_true(stopped)
  executor.join_stopped().unwrap()
}

///|
test "v2 invocation ID remains reserved until completion is polled" {
  let executor = start_fixture_v2_executor()
  executor.submit(30UL, "echo", b"first").unwrap()
  for attempt = 0; attempt < 20; attempt = attempt + 1 {
    fixture_sleep()
  }
  match executor.submit(30UL, "echo", b"duplicate") {
    Err(DuplicateInvocation) => ()
    _ => abort("unpolled completion released its invocation ID")
  }
  match next_executor_event(executor) {
    InvocationFinished(30UL, 0, response) => assert_eq(response, b"first")
    _ => abort("first completion was unavailable")
  }
  executor.submit(30UL, "echo", b"second").unwrap()
  match next_executor_event(executor) {
    InvocationFinished(30UL, 0, response) => assert_eq(response, b"second")
    _ => abort("polled invocation ID was not reusable")
  }
  stop_fixture_v2_executor(executor)
}

///|
test "v2 host callbacks reject calls outside the executor worker" {
  let executor = start_fixture_v2_executor()
  executor.submit(40UL, "cancel", b"").unwrap()
  fixture_sleep()
  assert_eq(fixture_v2_foreign_request(), -2147483548)
  executor.cancel(40UL).unwrap()
  match next_executor_event(executor) {
    InvocationFinished(40UL, status, _) => assert_true(status != 0)
    _ => abort("foreign callback test invocation did not finish")
  }
  stop_fixture_v2_executor(executor)
}

///|
test "v2 blocking join requires shutdown and releases without polling stopped" {
  let executor = start_fixture_v2_executor()
  match executor.join() {
    Err(ExecutorStatus(_)) => ()
    _ => abort("executor joined before shutdown")
  }
  executor.submit(50UL, "cancel", b"").unwrap()
  fixture_sleep()
  executor.begin_shutdown()
  executor.join().unwrap()
  assert_true(executor.is_closed())
}
