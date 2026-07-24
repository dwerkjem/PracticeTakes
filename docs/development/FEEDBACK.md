# In-app feedback client

The feedback form uses the version 1 contract in
`contracts/feedback/v1.schema.json`. Production builds default to
`https://practice-takes-feedback-intake.derekrneilson.workers.dev`. Override
the intake service at CMake configure time when testing another deployment:

```sh
cmake -S . -B build \
  -DPRACTICE_TAKES_FEEDBACK_ENDPOINT=https://feedback.example.com
```

The value must be the HTTPS origin without a trailing slash. The client first
requests `/v1/authorizations`, then sends the feedback to `/v1/submissions`.

The production `workers.dev` endpoint keeps those intake routes and the data-free health probe
public. Only the dashboard assets and administrative API paths are protected by a path-scoped
Cloudflare Access application. The Worker also validates the signed Access JWT against one exact
administrator address before returning any private data.

Accepted reports are stored in D1 immediately. Three scheduled runs per UTC day email every queued
report to the administrator, combining reports only when needed to stay within the three-email
limit. The queue balances the current backlog across the remaining daily sends and retries failed
batches; email availability never determines whether the client receives a submission receipt.

The Tuner and Spectrogram each provide **Give feedback on this tool**. That action opens the same
form with an editable context field containing only the tool name and application version. Clearing
that field removes all context from the submission; imported music, audio samples, screenshots,
and practice content are never added to it.

After three completed tool uses, the application may show one feedback invitation once all tool
windows have closed. It never appears during an active analysis session. Dismissal is persisted,
and invitations can be disabled or re-enabled from the Help menu without removing the manual
feedback commands.

When no endpoint is configured, or authorization temporarily fails, the form
reports the submission as locally queued and retains the complete draft in the
per-user application settings. A non-success response from the submission
route is reported as failed and also preserves the draft.

The six user-facing feedback types map onto the four wire categories. The
specific user-facing type is included in the message, so information is not
lost:

- Bug, Audio problem, and Notation/MIDI problem ⇾ `bug`
- Usability problem ⇾ `usability`
- Feature request ⇾ `idea`
- Other ⇾ `other`
