# Ausyn architecture

The 0.5.0 `AutonomousCompanion` is a bounded local decision engine alongside `WorkloadCoach`. It consumes fresh snapshots before UI rendering, infers stable task context, maintains optional app aggregates, and emits evidence-labeled decisions. `MainWindow` applies only explicitly approved executable rules through `BackgroundRelief`; native identity/ownership/foreground checks remain the execution boundary. Verification can request undo and suspend ineffective rules. The attention budget governs presentation independently of collection, and local journal persistence is separately opted in. See [background companion](background-companion.md).

Ausyn is a native Windows desktop application organized around explicit boundaries. Qt is used for presentation and application integration; the user interface does not call Windows telemetry APIs directly.

## Planned layers

1. **Presentation:** accessible Qt Widgets screens and notifications.
2. **Application services:** coordinate user requests, refreshes, and workflows.
3. **Intelligence:** validation, health scoring, diagnosis, recommendations, and predictions.
4. **Monitoring adapters:** independently report supported Windows and hardware measurements.
5. **Local data:** SQLite-backed, 30-day telemetry history and active/resolved finding records.
6. **Controlled actions:** preview, approval, execution, verification, and rollback where supported.
7. **AI providers:** optional OpenAI-compatible chat-completions client that receives only minimized, user-approved context; local deterministic answers remain available offline.

## Data flow

`Windows / device APIs → validated snapshot → local history and analysis → evidence-backed insight → UI`

System-changing work follows a separate path: `recommendation → preview → explicit approval → action → result verification → history`.

## Current implementation

The current implementation includes the Qt application shell, Windows telemetry adapters, a background polling service, local SQLite history, rule-based findings, evidence-based storage trend analysis, user-specified game requirement checks, read-only Windows Event Log, application inventory, and security/update screens, a deterministic local assistant, and an optional user-approved OpenAI-compatible provider client. Event, app, and security inventory reads run outside the UI thread and are not persisted or forwarded to the provider. The update cache search is user-triggered and offline. The remote client receives a per-question minimized context and never receives the telemetry database. The system-health score is deliberately limited to the CPU, memory, and system-drive signals currently measured; it does not claim that security state, updates, thermal health, or battery condition have been assessed.

The optional Windows sign-in setting uses only the current user’s standard Run registry key. Its registry change is initiated by the explicit Settings toggle, reports write/verification failures to the user, and is removed by the installer’s uninstaller callback. It does not install a service, elevate Ausyn, or run a separate background agent.
