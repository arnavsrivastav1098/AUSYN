# Ausyn 0.3 interface

The sidebar has five destinations. Each page remembers its Simple/Details slider independently.

| Page | Everyday tools | Additional tools in Details |
| --- | --- | --- |
| Main dashboard | Current assessment, next step, CPU/memory/storage/graphics cards, Running apps, recent activity chart, inline system check, proactive advice, guided help, pause/resume | Battery/network cards, measured performance index and coverage, findings, recommendations, forecasts |
| Performance | Live activity chart, searchable CPU/memory process list, active app, gaming profiles/readiness, battery and power tools | Process IDs, connections and I/O, core load, selected-process evidence and chart, hardware profile, network details |
| Ask Ausyn | Written local answers, quick questions, optional configured cloud AI | Measurement notes in replies |
| Logs | Ausyn session activity, history/charts/reports/backups, Windows events | Startup and installed-app inventory, session-log evidence |
| Settings | Monitoring, background behavior, appearance, startup, assistant style, alerts, privacy/provider controls, security and update status | Sensor coverage, Ausyn overhead, background scan toggles and cadence |

An insight's Review button opens its related tool in the appropriate page. It automatically enables Details when the destination needs it. Check my system displays its local result directly on the dashboard. Ask Ausyn is available when a conversation is useful.

## Separate resource windows

Click a CPU, memory, graphics, storage, battery or network card to open that resource in its own window. Keyboard users can focus a card and press Enter or Space. Running apps opens a searchable process window. Each inspector offers Simple/Details, the latest readings and capture time, resource advice, a bounded chart where relevant, and Windows evidence. CPU and memory inspectors list the busiest readable processes. Missing sensors remain unavailable. Closing an inspector leaves monitoring running; reopening a resource reuses its existing window. Inspectors reuse the main monitor and start no additional collection timer.

Ausyn has its own minimize, maximize/restore and close buttons. Drag the title bar to move a window, double-click it to maximize/restore, and drag a window edge or corner to resize. The main close button keeps Ausyn in the notification area by default. Quit Ausyn on the tray menu exits.

## Useful background behavior

The dashboard shows sustained findings, immediate workload context, and a next step without waiting for a chat question. The activity log records completed checks, new findings, cleared conditions, detected games, monitoring changes and attempted desktop alerts. It keeps the latest 100 entries in memory for the current session. It does not invent improvements or claim to have changed the PC when it only observed it.

Desktop alerts are configurable and independent of dashboard advice. Ausyn cards are the default; they do not depend on Windows toast delivery. Cards show the finding, evidence and a safe next step, with See details and Quiet for 15 min buttons. The card does not take keyboard focus and dismisses after 14 seconds (22 for critical advice). Monitoring continues while cards are snoozed.

By default, desktop cards are suppressed only while you are actively using an Ausyn window. Ausyn remaining visible behind Edge or another application does not suppress them. Advice remains in the dashboard while a popup is blocked; sustained warnings are retried after the restriction ends. The Windows notification channel is available in Settings, but Windows controls its final delivery. Settings reports alert readiness and includes Test my alert settings; the test bypasses foreground suppression but respects paused monitoring, quiet hours, snooze, the master switch and category controls.

At balanced sensitivity, a workload warning uses repeated CPU/memory observations over at least 20 seconds. Memory at or above 95% for at least five seconds across repeated fresh readings receives earlier advice; at 97% or higher with at most 256 MiB available, it is critical. One spike alone is not called sustained pressure. Cards recommend saving work and reviewing unused tabs/apps or a demanding workload; they never close apps automatically. Process context is a possible contributor, rather than proof of cause. Logs record measured recovery when the rule clears.

Per-condition and repeat-alert cooldowns still apply. Nearby reports for the same resource are coalesced for 30 seconds, with escalation allowed to interrupt. Quiet hours and category controls apply to critical advice too. A suppressed card is never recorded as successfully shown. These are observations and guidance, not a guarantee against every PC problem.

Pausing monitoring stops periodic telemetry and background event/security checks. An already-running collection may finish. Saved readings retain their capture times and manual inventory/tools remain available. Closing the window keeps monitoring in the notification area by default; Quit Ausyn exits. Settings can make closing exit instead, choose the initial page, or start with the window hidden.

## Workload companion (0.4.0)

The dashboard's **Keep your work moving** panel reads the current foreground app and reuses existing process/CPU/RAM samples. It does not read window titles, URLs or input. Continued use for at least 35 seconds after a pressure episode is a clue that the app may matter; it is never permission to change a process. Choose a running app and click **Keep this app** to confirm the goal. This context lasts for the app session and can be cleared with **End session** or disabled in Settings.

Advice tries to keep the selected task open: preserve the active browser tab, review unused tabs or other apps, adjust optional video/rendering quality, or review a competing CPU workload. It does not identify individual tabs or claim an app caused a system problem. Memory pressure cannot be fixed by increasing CPU priority.

**Compare my change** captures a reading before a change you make and compares it with at least four later valid readings spanning 20 seconds, after 30 seconds have elapsed. A sampling gap clears the comparison. Results describe observed differences and do not claim causation.

**Review background relief** requires a confirmed session goal and a fresh sampled background app using at least 5% CPU. Ausyn previews the process, PID, path, effect and limitations. With approval, it validates executable identity, creation time, account ownership, non-system/non-critical status, foreground state and Normal priority, then changes only that process to Below Normal. It keeps a handle to that process instance and verifies restoration after ten minutes, on normal exit/end session/pause, or when collection observes the app moving to the foreground. It preserves an externally changed priority. If Ausyn crashes, restarting the affected app resets its process priority. No app is closed and no RAM is freed. The same process is excluded when it is the kept app.

Notifications track meaningful changes rather than random alternate wording. A displayed card refreshes its current readings without extending its timer. Sustained critical memory pressure still receives urgent advice during a kept-app session. Episode updates are bounded; repeated unchanged samples remain quiet. Alert channel, focus, quiet-hours, category and snooze settings still apply.

Ask Ausyn accepts natural questions. Short follow-ups such as **why?**, **what should I do?**, or **how do you know?** use the preceding local topic. Broader AI answers use the configured provider; Settings has separate opt-ins for sharing the recent conversation and reasoning about local PC questions. Each online request previews the selected evidence and shared context. Up to eight bounded recent messages may be included; older replies are context, not current telemetry. No online request runs automatically from a notification.

## Resource use

Version 0.5.0 adds **Your background companion** to the normal dashboard. Stable foreground use selects guidance context automatically. The manual **Keep your work moving** controls are in Details mode. **Settings → Background decisions & standing rules** configures optional local learning, the attention budget, battery session length, and reviewed exact executable permissions. **Logs → Ausyn activity → Details** shows the full bounded companion journal. Right-click the tray icon for a briefing, pause/resume, quiet mode and undo. See [background companion](background-companion.md).

Live charts retain at most 120 readings and ten minutes of data, use actual timestamps, and break lines at missing readings or gaps over 30 seconds. They do not run a repaint timer. Switch/ring animations run briefly on changes and can be disabled. Hidden pages and closed inspectors skip expensive rendering, visible telemetry updates are capped to once per two seconds, briefing cards are reused, and full history refreshes run about every 30 seconds rather than every persistence write. Popup dismissal uses a single-shot timer. Windows collection stays on a worker thread. These changes reduce work; actual overhead and sensor support still depend on the PC and workload.

Presets set sampling, workload sensitivity, periodic check cadence and notification spacing: Quiet & economical uses 10-second sampling; Balanced uses 5 seconds; More responsive uses 2 seconds. Each value can be changed separately.

## Developer validation

Use `ausyn.exe --ui-check <output-folder>` to validate page routes, detail modes, inline checks, local chat and layout rendering using explicit fixtures. It renders a hidden window, disables collectors and cloud calls, and does not write preferences. Use the Windows Qt platform plugin for native fonts. Use `--collector-check <output-folder>` for three real Windows collections with a UI heartbeat and a JSON report, without using the history database. These are smoke checks; they do not establish long-term stability on every PC.
