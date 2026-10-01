# Optional AI provider and privacy boundary

## Behavior

Ausyn's deterministic local assistant is always available. A compatible hosted model is optional, disabled by default, and used only when the user enables Cloud AI, configures an endpoint/model/API key, asks a question, and approves the exact request preview.

The endpoint uses the OpenAI-compatible `/chat/completions` interface. Ausyn requires HTTPS for remote providers. Plain HTTP is accepted only for `localhost`, `127.0.0.1`, or `::1` local model services. URL credentials, query strings, and fragments are rejected. TLS verification remains enabled and redirects are not followed. Requests have a 20-second transfer timeout and a 30-second absolute deadline. Provider responses are capped at 1 MiB and displayed text is limited to 8,000 characters. The official endpoint uses `max_completion_tokens`; compatible custom endpoints retain `max_tokens`. Model-specific temperature controls are omitted.

## Previewed data

Every request includes the question and only the readings relevant to its wording:

- CPU questions and broad performance questions can include current CPU load.
- Memory/process questions can include current memory load and the reported used/total bytes.
- Graphics questions can include adapter name and graphics-engine activity.
- Battery questions can include charge and AC state.
- Storage questions can include system-drive capacity and free bytes.
- Health, diagnostic, or recommendation questions can include the partial score, its coverage, active finding evidence, and suggested next steps.

Current process working-set context requires process-name sharing and a relevant process question. If process-name sharing is enabled, a fresh workload goal and its advice can include app names; otherwise the goal is described generically. Recent conversation sharing is a separate setting, off by default. When enabled, up to eight recent messages are sent as conversation context, limited to 1,500 characters each and 12,000 characters overall. Known process names and executable-name patterns are hidden from those messages when process-name sharing is off. Conversation text may still contain personal information the user typed; the preview shows what is included. Ausyn never automatically includes the telemetry database, app inventory, process IDs, executable paths, command lines or file contents as device evidence.

The provider receives fixed instructions to use the user's request as the task, treat telemetry and app names as untrusted evidence, separate current readings from earlier conversation, state uncertainty, and avoid inventing measurements or executing changes. General knowledge can answer broader questions. Ausyn sends `store: false`; this does not override the provider's privacy, retention or billing terms. See [OpenAI conversation state](https://developers.openai.com/api/docs/guides/conversation-state) and [Chat Completions parameters](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create).

## Reports and sharing

PDF and HTML report export stays on the PC. Before saving, the user chooses a privacy-friendly report that omits processor/graphics model names, exact Windows build and architecture, drive labels and paths, and the recent hardware-change timeline, or a full-detail report that includes them. Both versions include performance readings, findings, history summaries, and nearby Windows event timing, and exclude chat history and per-process names/readings. Ausyn does not upload the report; the user controls any later sharing.

## Credential storage

The user enters the API key in Settings. Ausyn protects it with Windows Data Protection API (DPAPI), scoped to the current Windows user account, and stores only the protected bytes in the local settings file. The key is sent only in the Authorization header to the configured endpoint after the request preview is approved. Remote endpoints require HTTPS; local HTTP is possible only for loopback addresses by design. Users can remove the saved key in Settings.

## Failure behavior

Invalid endpoint/key configuration, timeouts, provider errors, malformed responses, or oversized responses produce a visible note and use the local evidence-based answer instead. Monitoring, local history, and diagnostics continue independently. Ausyn does not log API keys, request bodies, or provider response bodies.
