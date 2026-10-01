# Ausyn background companion — 0.5.0

Ausyn watches the sampled foreground executable and supported Windows readings while you work in other apps. The companion defaults to on, requires monitoring and workload awareness, and selects guidance context after an app remains in front for at least 20 seconds. It does not claim to know thoughts, page contents or whether an executable is performing a particular task.

## New behavior

| Feature | What it does |
| --- | --- |
| Automatic task context | Follows stable foreground use without a Keep button. An explicitly kept app remains protected from priority changes. |
| Activity modes | Recognizes browser, supported game, communication, development, document and creative executables; unknown apps get a generic session. A communication app name does not establish an active call. |
| Local app baselines | With learning enabled, remembers typical whole-system CPU/RAM while an app is foreground. At least 30 valid observations precede a baseline comparison. |
| Unusual session detection | Compares repeated readings with that app's learned average and variance; requires at least four elevated readings spanning 20 seconds. Reports association, not causation. |
| Recurring pressure | Counts transitions into sustained pressure while a task is current; highlights a pattern after three episodes. It does not count every poll as an incident. |
| Short RAM runway | Uses a continuous, capacity-consistent decline with at least six readings over 30 seconds, 75% declining steps and a fit of at least 0.85. Only reports a conditional range within five minutes of 256 MB headroom. Invalid values, gaps or capacity changes invalidate it. |
| Battery session guard | Compares Windows' valid unplugged runtime estimate with your selected 15–240 minute work target and suggests plugging in early. It does not switch power plans automatically. |
| Standing background rules | Exact executable paths you approve can receive temporary Below Normal CPU priority under sustained CPU pressure. No other process-changing action is authorized by this setting. |
| Automatic outcome checks | Compares at least four subsequent valid readings spanning 20 seconds. No CPU relief or worsening RAM requests restoration and a 30-minute cooldown. Two ineffective outcomes suspend the rule until revoked and approved again. Lower system load is an observed association, not proof of responsiveness or cause. |
| Task handoff recaps | Records observed average system CPU and peak RAM for the previous task when you switch to another stable task. No popup is required. |
| Attention budget and digest | Groups ordinary interruptions after the configured 1–6 alerts per ten minutes. Critical alerts, security warnings and warnings at 95%+ RAM bypass this budget; quiet hours, disabled categories and snooze still apply. The digest retains grouped titles and recent evidence. |
| Tray quick controls | Right-click the Ausyn icon for a briefing, pause/resume, quiet mode and undo without opening the main window. |
| Ausyn resource governor | Six costly samples (collector above 20% of cadence, sampled Ausyn CPU over 3%, or working set over 300 MB) select ten-second sampling and disable chart animation. Twelve lower-cost samples restore your chosen cadence. This reduces activity; it is not a hard memory cap. |

## Configure once

Open **Settings → Background decisions & standing rules**. Task following and attention management work immediately. Optional **Remember app patterns** enables persistence. **Approve an executable…** shows the exact path and the full standing permission before approval. Enable **Apply the executable rules I approve below automatically** to activate that set. Without approved paths, no automatic OS priority changes occur.

The rule requires fresh system and process evidence, a stable foreground task, four sustained CPU readings, and a continuously observed busy background target. Ausyn checks the process instance, actual executable path, account owner, creation time, native foreground state, non-system/non-critical status and current Normal priority again immediately before applying. High/Realtime priority is never used. No app is closed; CPU priority does not free RAM. Outcomes are remembered across launches only when local learning is enabled; otherwise rule cooldowns and suspension last for this session.

Only one lease can exist at a time. It expires after ten minutes and restores on observed task/target focus change, missing telemetry, verification failure, rule disable, monitoring pause or normal exit. The native foreground guard also checks once a second. It preserves a priority changed externally. If Windows denies restoration, Ausyn reports that failure; restarting the affected app resets its priority. A crash can leave Below Normal priority until that app restarts.

## Local memory and privacy

The optional `companion.json` under Ausyn's local application-data folder stores up to 64 app profiles (30-day retention), 72 journal entries (7-day retention), and outcomes for at most eight approved paths. It is written atomically at most once a minute and on normal exit. It never stores window titles, page content, screenshots, keystrokes or browsing history. Turning learning off clears in-memory profiles/outcomes and attempts to delete the saved file. **Forget learned patterns & journal** also clears session memory. A denied file removal is reported, and the disabled file is not used.

The companion runs locally. It does not issue background cloud requests, incur model charges or grant a model permission to execute commands. Ask Ausyn's optional online provider remains separately configured and reviewed per request. Codex subscription credits do not configure an API key in this app.

## Interface and verification

Live graphs retain at most 120 actual readings over ten minutes. Shaded bands and glow are visual presentation; hovering selects an actual recorded sample. A finite 480 ms pulse marks incoming data. Hidden charts stop animation, and reduced motion or paused monitoring disables it. Notifications use a bounded body viewport; 120 successive text refreshes are checked for unchanged window geometry and a non-extended dismissal deadline.

The release QA checks pure decision fixtures, learning bounds/expiry, exact-path authorization, action result logic, native priority changes only on a disposable Ausyn helper, layouts and real Windows collection. These checks do not establish long-term stability on every PC or authenticated online-provider behavior.
