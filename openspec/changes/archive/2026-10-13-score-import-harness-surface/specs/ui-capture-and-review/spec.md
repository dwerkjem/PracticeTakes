## ADDED Requirements

### Requirement: An approved state may name a score to import
An approved window state SHALL be able to name a score fixture for the
application to import as part of establishing that state, so a surface that
only exists once a document is loaded can be reached. The name SHALL resolve to
a file committed to the repository; the channel SHALL NOT accept a path chosen
at run time, so the set of surfaces under verification stays a reviewable list
in one file.

#### Scenario: A state that names a score
- **WHEN** the capture pass opens an approved state naming a score fixture
- **THEN** the application imports that fixture and presents the surface that
  depends on it, without the harness supplying a path

#### Scenario: A state that names no score
- **WHEN** the capture pass opens an approved state that names no score fixture
- **THEN** no import is performed and the state is established exactly as it was
  before this capability existed

#### Scenario: An arbitrary path is offered
- **WHEN** something attempts to have the application open a score by a path
  that no approved state names
- **THEN** the channel refuses it, because its vocabulary admits only approved
  names

### Requirement: A state's score is imported through the application's own load path
A state naming a score SHALL import it the way the application imports a score a
user chose — on a background thread, with the result delivered to the message
thread — rather than through a path that exists only for the harness. A surface
that verified a synchronous import would not be a picture of what a user gets.

#### Scenario: The import runs
- **WHEN** a state naming a score is established
- **THEN** the import runs on a background thread and the message thread stays
  responsive while it does

#### Scenario: The capture waits for the result
- **WHEN** a surface depends on an imported score
- **THEN** it declares the settling time the capture needs, so the image is of
  the finished result rather than of the import in progress

### Requirement: A state naming an absent score fixture fails loudly
Where an approved state names a score fixture, the absence of that fixture
SHALL be a failure the test suite reports, not a capture of an empty window. A
reviewer SHALL NOT be shown an image that is blank because its input went
missing.

#### Scenario: The fixture is gone
- **WHEN** an approved state names a score fixture that is not present in the
  repository
- **THEN** the test that checks the approved states fails and names the state
  and the missing fixture
