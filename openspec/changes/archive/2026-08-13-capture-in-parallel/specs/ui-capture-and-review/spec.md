## ADDED Requirements

### Requirement: A run may capture several surfaces at once
The capture pass SHALL be able to photograph more than one surface
concurrently, each on its own private display and in its own instance of the
application, so that the settling waits overlap rather than accumulate.

The number of concurrent workers SHALL be chosen by the operator. A run SHALL
NOT default to a count derived from the machine's processor count, because
several instrumented applications competing for one machine measures the machine
rather than the build.

Captures produced by a parallel run SHALL be indistinguishable from those a
sequential run produces, and SHALL be recorded against one run.

#### Scenario: A full sweep with several workers
- **WHEN** a run captures with more than one worker
- **THEN** every surface in the plan is captured exactly once, recorded against
  one run, and no surface is captured twice

#### Scenario: A worker fails
- **WHEN** one worker's application stops responding mid-run
- **THEN** the surfaces it had not reached are recorded as failures with the
  reason, and the other workers finish their own

#### Scenario: The same run, one worker or many
- **WHEN** the same plan is captured sequentially and in parallel
- **THEN** the two runs contain the same surfaces, at the same geometries, in
  the same palettes

### Requirement: Cross-capture checks keep their reach when a run is split
Two of the pass's checks work by comparing a capture against other captures in
the same run: that no two different surfaces produced byte-identical images at
one resolution, and that a surface's geometries produced different sizes.

Splitting a run across workers SHALL NOT reduce what those checks can see. A
worker SHALL compare against every capture the run has taken, not only its own.

#### Scenario: Two surfaces photograph the same window
- **WHEN** two different surfaces produce byte-identical images at one
  resolution, and they were captured by different workers
- **THEN** the second is recorded as a failure naming the first, exactly as it
  would be within a single worker

#### Scenario: A window that never resized
- **WHEN** a surface's captures at two geometries come back the same size, and
  they were taken by different workers
- **THEN** the second is recorded as a failure naming the geometry it matched
