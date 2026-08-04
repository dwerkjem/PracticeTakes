# Performance Event Capture Safety Review

`RealTimeEventCapture` is a fixed-capacity single-producer/single-consumer channel. The audio
callback is the sole producer and a non-real-time collector is the sole consumer.

## Callback Path

The `record()` path is bounded and `noexcept`. It performs:

- lock-free atomic loads, one fixed-array assignment, arithmetic, and one atomic store;
- one additional lock-free atomic increment only when storage is full;
- no allocation, deallocation, lock acquisition, logging, file access, or system call;
- no retry loop or wait for the consumer.

The implementation rejects unsupported targets at compile time with `is_always_lock_free`
assertions for both atomic types used by the callback. `AudioCallbackEvent` is required to remain
trivially copyable. A full channel drops the newest event and increments a bounded diagnostic
counter instead of blocking or overwriting unread data.

## Consumer And Lifecycle Constraints

- Exactly one audio callback thread may call `record()`.
- Exactly one non-real-time thread may call `drain()`.
- `reset()` may only run while producer and consumer are stopped.
- The consumer owns all aggregation, logging, persistence, and GUI notification.

## Verification

`RealTimeEventCaptureTests` covers field fidelity, wraparound, full-channel behavior, and a
500,000-attempt concurrent stress run. The stress test verifies bounded occupancy, monotonic
accepted sequence values, and conservation of accepted plus dropped events.

This review establishes source-level and automated evidence. Instrumentation overhead on reference
audio hardware remains part of the hardware calibration protocol and cannot be established by CI.