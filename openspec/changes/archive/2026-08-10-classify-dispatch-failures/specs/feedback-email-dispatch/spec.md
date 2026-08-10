## MODIFIED Requirements

### Requirement: The dispatcher distinguishes every reason it sent nothing
The feedback email dispatcher SHALL return a result that names which of the
following occurred: a batch was sent, nothing was queued, the delivery
configuration was rejected, the daily email limit was already reached, or the
send was attempted and failed. These outcomes SHALL be distinguishable by the
caller without inspecting logs. A single sentinel value SHALL NOT stand for more
than one of them.

A failed send SHALL be reported as a classification chosen from a fixed set,
never as text originating from the caught exception. The provider's own message
SHALL be written to the service log and SHALL NOT appear in any response body or
stored record.

#### Scenario: Nothing is queued
- **WHEN** the dispatcher runs with valid configuration and an empty queue
- **THEN** the result reports the `nothing_pending` outcome and zero reports sent

#### Scenario: Delivery is not configured
- **WHEN** the dispatcher runs with a missing or rejected delivery configuration
  and reports waiting in the queue
- **THEN** the result reports the `not_configured` outcome, and the queued
  reports remain queued and unclaimed

#### Scenario: The provider rejects the send
- **WHEN** the configured send fails
- **THEN** the result reports the `send_failed` outcome and a failure reason
  drawn from the fixed set, and the claimed reports and the reserved daily slot
  are both released

#### Scenario: The provider's message would identify internals
- **WHEN** the failure carries a stack frame, a file path, or any other detail
  of the service's implementation
- **THEN** none of it appears in the returned result or in the stored attempt,
  and the full value is written to the service log

#### Scenario: The daily limit is already spent
- **WHEN** the dispatcher runs after the configured number of emails have
  already been sent for the current UTC day
- **THEN** the result reports the `daily_limit_reached` outcome and no email is
  sent

### Requirement: An administrator can read the dispatcher's delivery status
The service SHALL expose an authenticated administrative route that reports
whether delivery is configured, the problems if it is not, the number of queued
reports and the age of the oldest, the emails sent for the current UTC day
against the configured limit, and the most recent dispatch attempt with its
outcome, its failure reason where it failed, and its time. The route SHALL NOT
send email.

#### Scenario: Status is read while delivery is misconfigured
- **WHEN** an authorized administrator reads the status route and the sender
  address is invalid
- **THEN** the response reports that delivery is not configured and names the
  problem, and the queue is left untouched

#### Scenario: Status is read after a dispatch
- **WHEN** an authorized administrator reads the status route after a dispatch
  has run
- **THEN** the response reports that attempt's outcome and the time it completed

#### Scenario: Status is read after a failed dispatch
- **WHEN** an authorized administrator reads the status route after a send
  failed
- **THEN** the response reports the classified failure reason and no text
  originating from the provider

#### Scenario: An unauthenticated caller reads the status route
- **WHEN** the status route is requested without an authorized administrator
  identity
- **THEN** the request is refused and no delivery configuration is disclosed
