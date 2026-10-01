# Blueprint alignment and V1 boundary

The source blueprint describes both a Windows desktop assistant and longer-term concepts. Ausyn implements a practical local-first V1, using the blueprint's principles of measured evidence, visible uncertainty, privacy, low overhead, and user control. The former Sentinel name is replaced by Ausyn. Speech is omitted by design, as requested; Ausyn's assistant is written.

## Implemented in the current V1

- Native C++20/Qt Windows app, Program Files installation, optional current-user sign-in launch, accessible everyday and specialist navigation, dark/light appearance, and a first-run privacy guide.
- Live Windows CPU, memory, graphics-engine, fixed-volume, battery, network, process, system/hardware, storage-reliability, security-status, startup/software inventory, and selected event-log data, with per-sample freshness and quality handling.
- Local SQLite history, retention, charts, incident records, CSV and privacy-selectable PDF/HTML reports, hardware/software change timelines, integrity-checked full-database backups and guarded restore with rollback, and recovery behavior that keeps live monitoring running during history outages.
- A partial evidence-backed health score, repeated-pattern diagnostics, confidence bases, user-led recommendations, feedback, and optional before/after readings.
- Incident replay with finding timelines and nearby Windows event groups, clearly labeled as time context rather than proof of cause.
- On-demand sleep-blocker inspection and user-confirmed switching among Windows-enumerated power plans, with active-session restore and verification.
- User-managed local game requirement profiles with optional source notes and saved dates; profiles can bind a validated executable filename for post-launch detection.
- Conservative storage/memory/battery estimates only where the corresponding evidence supports them; forecasts can remain unavailable. Game readiness compares user-entered RAM, VRAM, and drive-space requirements and does not promise FPS or compatibility.
- Proactive written dashboard briefing, local notifications with category controls and quiet hours, symptom-led troubleshooting, and an offline deterministic assistant. An optional reviewed OpenAI-compatible provider can add natural-language generation.
- Privacy controls, local-only defaults, DPAPI-protected provider credentials, administrator-approved Program Files installer, portable ZIP, and uninstaller cleanup of the optional sign-in entry.

## Deliberate limits and future work

These are not represented as completed features:

- Automatic optimization, arbitrary system commands, file cleanup, process termination, broad configuration rollback, and system repair. One predefined power-plan change is available only after user confirmation and has an undo limited to the current app session.
- A built-in offline large language model. The local assistant is deterministic and rule-based; stronger generative answers need an explicitly configured provider or a future local-model integration.
- Spoken responses, cloud synchronization, accounts, multi-device/enterprise management, mobile apps, plugins, or automatic application updates.
- A complete game catalog or automatic catalog refresh, guaranteed FPS/compatibility predictions, exact hardware lifespan/failure dates, battery wear forecasts, or CPU/GPU die temperatures where Windows does not expose those sensors. Launch detection covers the small bundled list plus executable filenames explicitly assigned to saved profiles; it runs after the process starts and cannot identify every game automatically.
- Scheduled or off-device backup. Full local-history backup and guarded restore are available from History & reports; users still need to store a copy separately to protect against device loss.
- A formal automated test suite and broad real-device validation. Build/package success does not prove every sensor path on every Windows machine.

The feature boundary follows the blueprint's own V1 direction: observe, validate, analyze, explain, forecast cautiously, and recommend. Later automation should be added only as separate predefined actions with preview, explicit approval, verification, and rollback where supported.
