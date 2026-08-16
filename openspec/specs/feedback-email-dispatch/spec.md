# feedback-email-dispatch Specification

## Purpose

Defines how feedback submitted from inside the application is delivered onward by
email, and — more to the point — how the dispatcher accounts for itself when it
delivers nothing. Silence has several causes (nothing queued, a daily limit
reached, a refused configuration, a failed send) and treating them alike leaves
an administrator unable to tell a quiet day from a broken one. Covers those
distinct outcomes, rejecting a configuration that could never deliver before it
is used rather than after, the status and on-demand dispatch an administrator can
reach, and the audited record of every attempt with its actor and result.

## Requirements
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

### Requirement: Configuration rejection names every problem it found
Delivery configuration validation SHALL report the complete set of problems it
found as stable, machine-readable identifiers, rather than a single failure
signal. Validation SHALL NOT stop at the first problem.

#### Scenario: Several settings are wrong at once
- **WHEN** the sender address, the dashboard URL, and the recipient are each
  invalid
- **THEN** the reported problems name all three

#### Scenario: The recipient is not the configured administrator
- **WHEN** the notification recipient differs from the single configured
  administrator address
- **THEN** the problems name that mismatch and no email is sent

### Requirement: A sender that can never deliver is rejected before sending
Validation SHALL reject a sender address whose domain cannot be onboarded to the
email provider — the reserved example domains, the reserved `.invalid`, `.test`,
and `.local` suffixes, `localhost`, and any `workers.dev` host. A rejected
sender SHALL produce the `not_configured` outcome rather than an attempted send.

#### Scenario: The sender is left at the configuration template value
- **WHEN** the sender address is on `example.com`
- **THEN** validation reports `sender_domain_not_sendable` and no send is
  attempted

#### Scenario: The sender is the service's own workers.dev host
- **WHEN** the sender address is on a `workers.dev` host
- **THEN** validation reports `sender_domain_not_sendable`

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

### Requirement: An administrator can dispatch queued feedback on demand
The service SHALL expose an authenticated administrative route that runs a
dispatch immediately and returns its result. The manual dispatch SHALL use the
same code path as the scheduled dispatch, SHALL send only feedback that is
genuinely queued, and SHALL be subject to the configured daily email limit. The
route SHALL NOT provide a means of exceeding that limit.

#### Scenario: Queued feedback is dispatched on demand
- **WHEN** an authorized administrator triggers a dispatch with valid
  configuration and queued reports
- **THEN** an email is sent, the dispatched reports leave the queue, and the
  response reports how many were sent

#### Scenario: A dispatch is triggered after the daily limit is reached
- **WHEN** an authorized administrator triggers a dispatch after the configured
  number of emails have been sent for the current UTC day
- **THEN** no email is sent and the response reports the limit was reached

#### Scenario: A dispatch is triggered while delivery is misconfigured
- **WHEN** an authorized administrator triggers a dispatch and the configuration
  is rejected
- **THEN** no email is sent, the response names the configuration problems, and
  the queued reports remain queued

### Requirement: Every dispatch attempt is recorded with its actor and outcome
The service SHALL record each dispatch attempt — scheduled or manual, and
including attempts that send nothing — with the actor that caused it, the
outcome, and the time it completed. Scheduled attempts SHALL be attributable to
the schedule; manual attempts SHALL be attributable to the administrator
identity that made the request. Records SHALL be subject to the existing audit
retention period.

#### Scenario: A scheduled run finds nothing to send
- **WHEN** a scheduled dispatch runs against an empty queue
- **THEN** an attempt recording the `nothing_pending` outcome is stored against
  the schedule as the actor

#### Scenario: An administrator triggers a dispatch
- **WHEN** an authorized administrator triggers a dispatch
- **THEN** the stored attempt names that administrator's identity as the actor

#### Scenario: Audit retention runs
- **WHEN** retention runs against dispatch attempts older than the audit
  retention period
- **THEN** those attempts are removed

### Requirement: The daily email limit is configuration, and the schema permits it
The maximum number of feedback emails per UTC day SHALL be read from the
service's environment, defaulting to three when unset or invalid. The database
SHALL NOT constrain the daily send count to a narrower range than the
configuration permits.

#### Scenario: A higher limit is configured
- **WHEN** the limit is configured above the previous fixed maximum and that
  many batches are dispatched in one UTC day
- **THEN** every dispatch succeeds and none is rejected by a database constraint

#### Scenario: The limit is unset or invalid
- **WHEN** the limit is absent, non-numeric, zero, or negative
- **THEN** the dispatcher uses three

#### Scenario: The configured limit is reported
- **WHEN** an administrator reads the delivery status
- **THEN** the response reports the limit the service is actually applying

