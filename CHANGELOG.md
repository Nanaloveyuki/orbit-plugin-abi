# Changelog

## 0.2.2

- Make OS thread join and Windows thread-handle close failures observable so a
  host can keep the plugin library loaded when worker termination is not
  confirmed.

## 0.2.1

- Add an explicit blocking executor join for host teardown after
  `begin_shutdown`; shutdown now releases event-byte backpressure before the
  worker is joined.

## 0.2.0

- Add ABI v2 while preserving the five exported plugin symbols and the complete
  ABI v1 host-table layout.
- Add a dedicated native executor with copied bounded queues, same-thread
  create/invoke/destroy, host-request events, cooperative cancellation and
  explicit shutdown/join lifecycle.
- Reserve invocation IDs until completion consumption, bound native event
  storage, preallocate terminal events, and reject host callbacks from plugin-
  created threads.
- Align private MoonBit/C frames so sanitizer builds do not perform unaligned
  64-bit reads, and use the CRT-aware Windows thread entry point.
- Keep ABI v1 synchronous loading and invocation compatible with plugins built
  from the 0.1.0 declarations.
