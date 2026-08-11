## ADDED Requirements

### Requirement: A hub older than the code it serves says so
The hub SHALL report when it is running code older than the suite's source on
disk, on every view rather than only where a run is started.

The hub is a long-lived process that imports its modules once, while the page it
serves is read from disk on every request. A hub left open while the suite is
edited therefore answers a current page with stale code: a control the page has
gained posts to a route the process has never heard of, and data the page has
learned to read in a new shape arrives in the old one. Both present as a broken
feature rather than as a stale process, which is the failure this reports.

The comparison SHALL cover the suite's own modules and SHALL NOT cover the page
assets, which are re-read per request and so cannot fall out of step. A hub that
cannot determine what it started from SHALL say nothing rather than warn, because
a warning that appears on a current hub would teach reviewers to ignore it.

#### Scenario: The suite is edited while the hub is open
- **WHEN** a module of the suite is changed after the hub was started
- **THEN** every view of the hub says it is running older code and names
  restarting as the way out

#### Scenario: A hub started from the current source
- **WHEN** nothing has changed since the hub was started
- **THEN** no such warning appears anywhere in the hub

#### Scenario: Only the page assets change
- **WHEN** a file under the hub's web directory is changed but no module is
- **THEN** no warning appears, because the page is served from disk each time
