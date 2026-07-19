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
