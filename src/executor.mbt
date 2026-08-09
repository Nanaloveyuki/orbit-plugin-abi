///|
const EXECUTOR_INVALID_INPUT : Int = -2147483448

///|
const EXECUTOR_QUEUE_FULL : Int = -2147483447

///|
const EXECUTOR_CLOSED : Int = -2147483446

///|
const EXECUTOR_DUPLICATE_ID : Int = -2147483443

///|
const EXECUTOR_NOT_READY : Int = -2147483444

///|
const EXECUTOR_WOULD_BLOCK : Int = -2147483442

///|
const EXECUTOR_EVENT_READY : UInt = 1

///|
const EXECUTOR_EVENT_INVOCATION_FINISHED : UInt = 2

///|
const EXECUTOR_EVENT_HOST_REQUEST : UInt = 3

///|
const EXECUTOR_EVENT_HOST_REQUEST_CANCELLED : UInt = 4

///|
const EXECUTOR_EVENT_STOPPED : UInt = 5

///|
/// One host IPC request made synchronously by a v2 plugin invocation.
///
/// The request and command are copied out of native storage while polling on
/// the host thread. `timeout_ms == 0` means inherit the outer invocation
/// deadline.
pub(all) struct HostRequest {
  request_id : UInt64
  invocation_id : UInt64
  command : String
  request : Bytes
  timeout_ms : UInt
} derive(Eq, Debug)

///|
/// Events produced by a native plugin executor and drained on the host thread.
///
/// Status zero means success. A non-zero invocation status is either the
/// plugin's own status or a documented executor bridge status.
pub(all) enum ExecutorEvent {
  Ready(Int)
  InvocationFinished(UInt64, Int, Bytes)
  HostRequest(HostRequest)
  HostRequestCancelled(UInt64, UInt64, Int)
  Stopped
} derive(Eq, Debug)

///|
/// A v2 plugin instance and its dedicated native worker.
///
/// All five plugin ABI functions execute on that worker. The worker only
/// stores native-owned copies and invokes the provided no-capture wakeup
/// function. Poll events only from the host/UI thread.
pub struct PluginExecutor {
  priv mut handle : UInt64
}

///|
fn executor_status(status : Int) -> Result[Unit, AbiError] {
  match status {
    0 => Ok(())
    EXECUTOR_INVALID_INPUT => Err(ExecutorStatus(status))
    EXECUTOR_QUEUE_FULL => Err(ExecutorQueueFull)
    EXECUTOR_CLOSED => Err(ExecutorClosed)
    EXECUTOR_DUPLICATE_ID => Err(DuplicateInvocation)
    EXECUTOR_NOT_READY => Err(InvocationUnavailable)
    EXECUTOR_WOULD_BLOCK => Err(ExecutorUnavailable)
    status => Err(ExecutorStatus(status))
  }
}

///|
/// Starts a dedicated executor for a v2 plugin.
///
/// `wakeup_callback` must be a no-capture native wake primitive. It may be
/// invoked by the executor thread and must not access MoonBit-managed values.
pub fn Plugin::start_executor(
  self : Plugin,
  wakeup_callback : FuncRef[() -> Unit],
) -> Result[PluginExecutor, AbiError] {
  if self.abi_version != V2 {
    return Err(ExecutorUnsupported)
  }
  let handle = native_executor_start(
    ABI_VERSION_V2,
    self.addresses.create_address,
    self.addresses.invoke_address,
    self.addresses.destroy_address,
    wakeup_callback,
  )
  if handle == 0UL {
    Err(ExecutorUnavailable)
  } else {
    Ok({ handle, })
  }
}

///|
/// Submits a copied invocation to the executor's bounded FIFO queue.
pub fn PluginExecutor::submit(
  self : PluginExecutor,
  invocation_id : UInt64,
  command : String,
  request : Bytes,
) -> Result[Unit, AbiError] {
  if self.handle == 0UL {
    return Err(ExecutorClosed)
  }
  if invocation_id == 0UL || request.length() > MAX_REQUEST_BYTES {
    return Err(ExecutorStatus(EXECUTOR_INVALID_INPUT))
  }
  let command_bytes = @utf8.encode(command, bom=false)
  if !command_is_valid(command, command_bytes) {
    return Err(InvalidCommand)
  }
  executor_status(
    native_executor_submit(self.handle, invocation_id, command_bytes, request),
  )
}

///|
/// Cancels a queued invocation or marks the running invocation cooperatively
/// cancelled. Arbitrary native plugin code cannot be preempted safely.
pub fn PluginExecutor::cancel(
  self : PluginExecutor,
  invocation_id : UInt64,
) -> Result[Unit, AbiError] {
  if self.handle == 0UL {
    return Err(ExecutorClosed)
  }
  executor_status(native_executor_cancel(self.handle, invocation_id))
}

///|
fn parse_executor_event(frame : Bytes) -> Result[ExecutorEvent, AbiError] {
  if frame.length() < 40 {
    return Err(MalformedEvent)
  }
  let (kind, status, id, parent_id, timeout_ms, command_length, payload_length) = match
    frame {
    [
      u32le(kind),
      u32le(status),
      u64le(id),
      u64le(parent_id),
      u32le(timeout_ms),
      u32le(command_length),
      u32le(payload_length),
      _,
      _,
      _,
      _,
      ..,
    ] =>
      (
        kind,
        status.reinterpret_as_int(),
        id,
        parent_id,
        timeout_ms,
        command_length.reinterpret_as_int(),
        payload_length.reinterpret_as_int(),
      )
    _ => return Err(MalformedEvent)
  }
  if command_length < 0 ||
    payload_length < 0 ||
    command_length > frame.length() - 40 ||
    payload_length != frame.length() - 40 - command_length {
    return Err(MalformedEvent)
  }
  let command_bytes = frame.view(start=40, end=40 + command_length).to_owned()
  let payload = frame.view(start=40 + command_length).to_owned()
  match kind {
    EXECUTOR_EVENT_READY if command_length == 0 && payload_length == 0 =>
      Ok(Ready(status))
    EXECUTOR_EVENT_INVOCATION_FINISHED if command_length == 0 =>
      Ok(InvocationFinished(id, status, payload))
    EXECUTOR_EVENT_HOST_REQUEST => {
      let command = @utf8.decode(command_bytes) catch {
        _ => return Err(MalformedEvent)
      }
      if command.is_empty() || id == 0UL || parent_id == 0UL {
        return Err(MalformedEvent)
      }
      Ok(
        HostRequest({
          request_id: id,
          invocation_id: parent_id,
          command,
          request: payload,
          timeout_ms,
        }),
      )
    }
    EXECUTOR_EVENT_HOST_REQUEST_CANCELLED if command_length == 0 &&
      payload_length == 0 => Ok(HostRequestCancelled(id, parent_id, status))
    EXECUTOR_EVENT_STOPPED if command_length == 0 &&
      payload_length == 0 &&
      status == 0 => Ok(Stopped)
    _ => Err(MalformedEvent)
  }
}

///|
/// Polls one native event. `None` means the completion queue is empty.
pub fn PluginExecutor::poll_event(
  self : PluginExecutor,
) -> Result[ExecutorEvent?, AbiError] {
  if self.handle == 0UL {
    return Err(ExecutorClosed)
  }
  let frame = native_executor_poll_event(self.handle)
  if frame.is_empty() {
    Ok(None)
  } else {
    parse_executor_event(frame).map(event => Some(event))
  }
}

///|
/// Completes a pending host request with a copied IPC response envelope.
pub fn PluginExecutor::complete_host_request(
  self : PluginExecutor,
  request_id : UInt64,
  response : Bytes,
) -> Result[Unit, AbiError] {
  if self.handle == 0UL {
    return Err(ExecutorClosed)
  }
  executor_status(
    native_executor_complete_host_request(self.handle, request_id, response),
  )
}

///|
/// Completes a pending host request with a non-zero bridge status.
pub fn PluginExecutor::cancel_host_request(
  self : PluginExecutor,
  request_id : UInt64,
  status : Int,
) -> Result[Unit, AbiError] {
  if self.handle == 0UL {
    return Err(ExecutorClosed)
  }
  if status == 0 {
    return Err(ExecutorStatus(EXECUTOR_INVALID_INPUT))
  }
  executor_status(
    native_executor_cancel_host_request(self.handle, request_id, status),
  )
}

///|
/// Rejects new work and cooperatively cancels all queued/running work.
pub fn PluginExecutor::begin_shutdown(self : PluginExecutor) -> Unit {
  if self.handle != 0UL {
    native_executor_begin_shutdown(self.handle)
  }
}

///|
/// Joins and releases an executor only after its `Stopped` event was observed.
pub fn PluginExecutor::join_stopped(
  self : PluginExecutor,
) -> Result[Unit, AbiError] {
  if self.handle == 0UL {
    return Ok(())
  }
  match executor_status(native_executor_join_stopped(self.handle)) {
    Ok(_) => {
      self.handle = 0UL
      Ok(())
    }
    Err(error) => Err(error)
  }
}

///|
/// Blocks until the executor worker has destroyed the plugin, then releases
/// all native executor state. Call `begin_shutdown` first. Arbitrary plugin
/// code remains trusted and can delay this join indefinitely if it ignores
/// cooperative cancellation.
pub fn PluginExecutor::join(self : PluginExecutor) -> Result[Unit, AbiError] {
  if self.handle == 0UL {
    return Ok(())
  }
  match executor_status(native_executor_join(self.handle)) {
    Ok(_) => {
      self.handle = 0UL
      Ok(())
    }
    Err(error) => Err(error)
  }
}

///|
pub fn PluginExecutor::is_closed(self : PluginExecutor) -> Bool {
  self.handle == 0UL
}
