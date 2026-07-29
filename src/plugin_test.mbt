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
  inspect(supported_abi_version(), content="1")
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
    Err(IncompatibleAbi(2)) => ()
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
  instance.destroy()
  inspect(instance.is_destroyed(), content="true")
  match instance.invoke("echo", b"") {
    Err(Destroyed) => ()
    _ => abort("destroyed instance accepted an invocation")
  }
  instance.destroy()
}
