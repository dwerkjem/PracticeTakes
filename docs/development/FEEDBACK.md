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

The production `workers.dev` endpoint exposes only those public intake routes and the data-free
health probe. Hosted administration remains disabled until a custom hostname is protected by
Cloudflare Access; use the locally authenticated Docker dashboard in the meantime.

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
