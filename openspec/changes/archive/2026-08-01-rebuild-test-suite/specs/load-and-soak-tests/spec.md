## ADDED Requirements

### Requirement: The sample FIFO is exercised concurrently
`AudioSampleFifo` SHALL have tests that run a producer and a consumer on
separate threads simultaneously, asserting that every sample pushed is received
exactly once and in order, so that its single-producer/single-consumer claim is
verified rather than assumed.

#### Scenario: Concurrent producer and consumer
- **WHEN** one thread pushes a known sequence while another drains it
- **THEN** the consumer observes the sequence in order with no duplicated,
  reordered, or invented samples

#### Scenario: The consumer cannot keep up
- **WHEN** the producer outruns the consumer and the ring fills
- **THEN** the overflow is reported through the FIFO's documented mechanism
  rather than corrupting the buffer or blocking the producer

### Requirement: Saturation is tested rather than assumed
The load suite SHALL drive the capture path at and beyond its designed capacity
— including a full ring, a consumer that stalls, and the maximum supported
number of simultaneous tool consumers — and assert the documented behaviour at
each limit.

#### Scenario: Many simultaneous consumers
- **WHEN** the maximum supported number of tool consumers drain concurrently
- **THEN** each receives its own complete stream and no consumer starves another

#### Scenario: A stalled consumer
- **WHEN** one consumer stops draining while others continue
- **THEN** the stalled consumer's FIFO overflows and the remaining consumers are
  unaffected

### Requirement: Sustained operation is tested
The suite SHALL include a soak test that runs the capture and analysis path
continuously for a configurable duration and asserts that throughput and memory
use remain bounded, so that slow leaks and drift are detectable.

#### Scenario: A long run stays bounded
- **WHEN** the soak test runs for its configured duration
- **THEN** memory use does not grow without bound and throughput does not
  degrade beyond the stated tolerance

#### Scenario: The duration is configurable
- **WHEN** a shorter duration is requested
- **THEN** the soak test honours it, so the same test serves both a quick local
  run and a longer scheduled run

### Requirement: Load tests are opt-in
Load and soak tests SHALL be tagged so they are excluded from the default test
run, matching the existing `[.benchmark]` convention, because they are slow by
construction.

#### Scenario: The default suite is run
- **WHEN** the test binary is invoked with no tag filter
- **THEN** no load or soak test runs

#### Scenario: The load suite is requested
- **WHEN** the test binary is invoked with the load tag
- **THEN** the load and soak tests run

### Requirement: Load tests report rather than merely pass
Each load and soak test SHALL emit the figures it measured — throughput,
observed overflow counts, peak memory, and duration — so a run that passes still
shows whether headroom is shrinking.

#### Scenario: A passing load run
- **WHEN** the load suite passes
- **THEN** the measured figures are reported alongside the pass result
