# Worker database fidelity

### Requirement: Worker tests execute SQL against a real database engine
Automated tests for `src/services/feedback-intake` SHALL execute the worker's SQL
statements against a real SQLite engine rather than a fake that inspects
statement text. No test-only database substitute may decide its result by
matching substrings of the SQL it is given.

#### Scenario: Query contains invalid SQL
- **WHEN** a query in `src/services/feedback-intake/src` is changed to contain a
  SQL syntax error
- **THEN** the Vitest suite covering the route that issues the query fails

#### Scenario: Query references a column that does not exist
- **WHEN** a query selects or filters on a column name that the migrations do
  not define
- **THEN** the Vitest suite covering that query fails

### Requirement: Test schema is built from the project's migration files
The test database SHALL be created by applying every file in
`src/services/feedback-intake/migrations/` in ascending filename order. Tests
SHALL NOT define the schema by any other means, including a hand-maintained
schema file or programmatic table creation.

#### Scenario: Migration adds a column the code depends on
- **WHEN** a new migration file adds a column and a query is updated to read it
- **THEN** the test suite exercises that query against a schema containing the
  new column, with no test-side schema change required

#### Scenario: Code depends on a column no migration creates
- **WHEN** a query reads a column that exists in no migration file
- **THEN** the test suite fails rather than passing against an assumed schema

### Requirement: The migration set is verified to apply cleanly
The system SHALL include a test that applies the complete migration set to an
empty database and fails if any migration raises an error, and that fails if
no migration files are discovered.

#### Scenario: A migration contains invalid SQL
- **WHEN** any file under `migrations/` contains a statement SQLite rejects
- **THEN** the migration-integrity test fails and names the offending file

#### Scenario: Migrations are discovered and applied in order
- **WHEN** the migration-integrity test runs against the current migration set
- **THEN** every file is applied in ascending filename order and the test passes

### Requirement: Database-backed suites share one test database helper
The database-backed Vitest suites (`index.test.ts`, `admin.test.ts`, and
`notifications.test.ts`) SHALL obtain their database from a single shared
helper. Per-suite hand-written database fakes SHALL NOT remain in the
repository.

#### Scenario: A new database-backed test is added
- **WHEN** a contributor adds a test that exercises a new query
- **THEN** the test obtains a database from the shared helper and requires no
  new fake behaviour to be hand-written for the query to run

#### Scenario: Existing suites keep their assertions
- **WHEN** the suites are ported onto the shared helper
- **THEN** each suite's existing assertions remain in place and pass

### Requirement: Database constraints are exercised by tests
Behaviour that depends on a database constraint SHALL be verified against the
constraint as the migrations define it, not against a simulated error.

#### Scenario: Duplicate submission is rejected by the unique constraint
- **WHEN** a submission is stored twice with the same idempotency identity
- **THEN** the real `UNIQUE` constraint from
  `0005_idempotent_submissions.sql` raises the conflict, the worker's
  duplicate-detection path handles it, and the caller receives the original
  receipt

#### Scenario: Statement batches are atomic
- **WHEN** a batched write fails partway through
- **THEN** no partial rows from that batch remain in the database
