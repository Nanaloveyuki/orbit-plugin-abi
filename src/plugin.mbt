///|
const ABI_VERSION_V1 : UInt = 1

///|
const ABI_VERSION_V2 : UInt = 2

///|
const MAX_MANIFEST_BYTES : Int = 1024 * 1024

///|
const MAX_COMMAND_BYTES : Int = 1024

///|
const MAX_REQUEST_BYTES : Int = 16 * 1024 * 1024

///|
const MAX_RESPONSE_BYTES : Int = 16 * 1024 * 1024

///|
const WRAPPER_MALFORMED_RESPONSE : Int = -2147483648

///|
const WRAPPER_INVALID_INPUT : Int = -2147483647

///|
/// A failure reported by the fixed Orbit plugin ABI bridge.
pub(all) enum AbiError {
  InvalidAddress
  IncompatibleAbi(UInt)
  ManifestUnavailable
  InvalidManifestUtf8
  InvalidCommand
  RequestTooLarge(Int)
  InvalidResponse
  PluginStatus(Int)
  InstanceUnavailable
  Destroyed
  ExecutorRequired
  ExecutorUnsupported
  ExecutorUnavailable
  ExecutorQueueFull
  DuplicateInvocation
  ExecutorClosed
  InvocationUnavailable
  MalformedEvent
  ExecutorStatus(Int)
} derive(Eq, Debug)

///|
pub(all) enum AbiVersion {
  V1
  V2
} derive(Eq, Debug)

///|
/// The five required addresses obtained from Nanaloveyuki/dynlib symbols.
///
/// Every address is validated before a native call is attempted. The owning
/// dynamic library must remain open until every PluginInstance is destroyed.
pub struct PluginAddresses {
  priv abi_version_address : UInt64
  priv manifest_address : UInt64
  priv create_address : UInt64
  priv invoke_address : UInt64
  priv destroy_address : UInt64
}

///|
/// A validated plugin ABI descriptor with its copied manifest.
pub struct Plugin {
  priv addresses : PluginAddresses
  priv manifest : String
  priv abi_version : AbiVersion
}

///|
/// A plugin instance created through the fixed v1 ABI.
///
/// Destroying is idempotent. Do not close the source dynlib Library before
/// calling destroy.
pub struct PluginInstance {
  priv invoke_address : UInt64
  priv destroy_address : UInt64
  priv mut handle : UInt64
}

///|
/// Returns the supported plugin ABI version.
pub fn supported_abi_version() -> UInt {
  ABI_VERSION_V2
}

///|
/// Returns every ABI version accepted by this host bridge.
pub fn supported_abi_versions() -> Array[UInt] {
  [ABI_VERSION_V1, ABI_VERSION_V2]
}

///|
/// Constructs the complete v1 symbol set.
///
/// Use Symbol::address from the published dynlib package for each address.
pub fn PluginAddresses::new(
  abi_version_address : UInt64,
  manifest_address : UInt64,
  create_address : UInt64,
  invoke_address : UInt64,
  destroy_address : UInt64,
) -> Result[PluginAddresses, AbiError] {
  if abi_version_address == 0UL ||
    manifest_address == 0UL ||
    create_address == 0UL ||
    invoke_address == 0UL ||
    destroy_address == 0UL {
    return Err(InvalidAddress)
  }
  Ok({
    abi_version_address,
    manifest_address,
    create_address,
    invoke_address,
    destroy_address,
  })
}

///|
/// Obtains the five fixed ABI addresses from published dynlib symbols.
///
/// The backing Library must remain open until every PluginInstance is
/// destroyed. A closed source symbol is rejected before any ABI call.
pub fn PluginAddresses::from_symbols(
  abi_version : @dynlib.Symbol,
  manifest : @dynlib.Symbol,
  create : @dynlib.Symbol,
  invoke : @dynlib.Symbol,
  destroy : @dynlib.Symbol,
) -> Result[PluginAddresses, AbiError] {
  let abi_version_address = match abi_version.address() {
    Ok(address) => address
    Err(_) => return Err(InvalidAddress)
  }
  let manifest_address = match manifest.address() {
    Ok(address) => address
    Err(_) => return Err(InvalidAddress)
  }
  let create_address = match create.address() {
    Ok(address) => address
    Err(_) => return Err(InvalidAddress)
  }
  let invoke_address = match invoke.address() {
    Ok(address) => address
    Err(_) => return Err(InvalidAddress)
  }
  let destroy_address = match destroy.address() {
    Ok(address) => address
    Err(_) => return Err(InvalidAddress)
  }
  PluginAddresses::new(
    abi_version_address, manifest_address, create_address, invoke_address, destroy_address,
  )
}

///|
/// Calls only the fixed ABI-version and manifest signatures to validate a
/// plugin. It copies the manifest and verifies its UTF-8, but does not parse
/// JSON.
pub fn Plugin::open(addresses : PluginAddresses) -> Result[Plugin, AbiError] {
  let reported_version = native_abi_version(addresses.abi_version_address)
  let abi_version = match reported_version {
    1 => V1
    2 => V2
    version => return Err(IncompatibleAbi(version))
  }
  let manifest_bytes = native_manifest(addresses.manifest_address)
  if manifest_bytes.is_empty() || manifest_bytes.length() > MAX_MANIFEST_BYTES {
    return Err(ManifestUnavailable)
  }
  let manifest = @utf8.decode(manifest_bytes) catch {
    _ => return Err(InvalidManifestUtf8)
  }
  Ok({ addresses, manifest, abi_version })
}

///|
/// Returns the exact ABI version reported by the plugin.
pub fn Plugin::abi_version(self : Plugin) -> AbiVersion {
  self.abi_version
}

///|
/// Returns the static plugin manifest copied during open.
pub fn Plugin::manifest_json(self : Plugin) -> String {
  self.manifest
}

///|
/// Creates one plugin instance with a process-static HostV1. The plugin may
/// retain that host pointer until destroy is called.
pub fn Plugin::create(self : Plugin) -> Result[PluginInstance, AbiError] {
  if self.abi_version != V1 {
    return Err(ExecutorRequired)
  }
  let frame = native_create(self.addresses.create_address)
  if frame.length() != 16 {
    return Err(InvalidResponse)
  }
  let status = match frame {
    [u32le(value), ..] => value.reinterpret_as_int()
    _ => return Err(InvalidResponse)
  }
  if status != 0 {
    return Err(PluginStatus(status))
  }
  let handle = match frame {
    [_, _, _, _, _, _, _, _, u64le(value)] => value
    _ => return Err(InvalidResponse)
  }
  if handle == 0UL {
    return Err(InstanceUnavailable)
  }
  Ok({
    invoke_address: self.addresses.invoke_address,
    destroy_address: self.addresses.destroy_address,
    handle,
  })
}

///|
fn command_is_valid(command : String, encoded : Bytes) -> Bool {
  !command.is_empty() &&
  !command.contains_code_unit(0) &&
  encoded.length() <= MAX_COMMAND_BYTES
}

///|
/// Invokes the fixed v1 plugin command signature.
///
/// Command is UTF-8 without embedded NUL. Empty requests are passed as a
/// non-null zero-length byte buffer. Successful responses are copied before
/// the host allocator frees the plugin-owned OrbitBuffer.
pub fn PluginInstance::invoke(
  self : PluginInstance,
  command : String,
  request : Bytes,
) -> Result[Bytes, AbiError] {
  if self.handle == 0UL {
    return Err(Destroyed)
  }
  if request.length() > MAX_REQUEST_BYTES {
    return Err(RequestTooLarge(request.length()))
  }
  let command_bytes = @utf8.encode(command, bom=false)
  if !command_is_valid(command, command_bytes) {
    return Err(InvalidCommand)
  }
  let frame = native_invoke(
    self.invoke_address,
    self.handle,
    command_bytes,
    request,
  )
  if frame.length() < 4 || frame.length() - 4 > MAX_RESPONSE_BYTES {
    return Err(InvalidResponse)
  }
  let status = match frame {
    [u32le(value), ..] => value.reinterpret_as_int()
    _ => return Err(InvalidResponse)
  }
  if status == WRAPPER_MALFORMED_RESPONSE || status == WRAPPER_INVALID_INPUT {
    return Err(InvalidResponse)
  }
  if status != 0 {
    return Err(PluginStatus(status))
  }
  Ok(frame.view(start=4).to_owned())
}

///|
/// Destroys the native plugin instance once. Repeated calls are no-ops.
pub fn PluginInstance::destroy(self : PluginInstance) -> Unit {
  if self.handle != 0UL {
    native_destroy(self.destroy_address, self.handle)
    self.handle = 0UL
  }
}

///|
/// Returns whether the instance has already been destroyed.
pub fn PluginInstance::is_destroyed(self : PluginInstance) -> Bool {
  self.handle == 0UL
}
