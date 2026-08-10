## 1. Classify

- [x] 1.1 Add `DispatchFailureReason` — `provider_rejected`, `provider_unavailable`, `provider_rate_limited`, `unknown` — and a classifier that reads the raw message and returns only a code
- [x] 1.2 Replace `DispatchResult.error` with `failureReason`; delete `reportableError`
- [x] 1.3 Carry `failureReason` through `DispatchAttempt` and the status payload
- [x] 1.4 Confirm the raw message still reaches `console.error` in full, and reaches nothing else

## 2. The administrative response

- [x] 2.1 `dispatchFailure` returns a fixed sentence per reason instead of the provider's text

## 3. Tests

- [x] 3.1 Each reason is produced by a representative provider message
- [x] 3.2 A message carrying a stack frame or a file path yields a code and leaves no trace of either in the result or the stored attempt
- [x] 3.3 An unrecognised message yields `unknown` rather than being passed through
- [x] 3.4 A thrown non-`Error` is classified rather than stringified into the response
- [x] 3.5 The 503 body carries the fixed sentence, and the status route reports the reason
- [x] 3.6 `npm run check && npm run test` from `src/services`

## 4. Documentation

- [x] 4.1 Replace the failure-message column in `EMAIL_DISPATCH.md` with the reason table, and say where the provider's sentence went
- [x] 4.2 Update the route description in `src/services/feedback-intake/README.md`

## 5. Close the alert

- [x] 5.1 Confirm no exception-derived value reaches a response
- [ ] 5.2 After merge, confirm CodeQL alert 8 resolves on the next scan of `main`
