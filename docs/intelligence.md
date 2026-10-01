# Ausyn health and insight rules

Ausyn uses deterministic rules for these findings. The assistant is not allowed to invent a sensor value, and no rule in this layer changes Windows settings or closes an application.

## Measured performance score

The dashboard score is a partial score, not a diagnosis of the whole laptop. It combines CPU pressure (30% weight), memory pressure (35%), and free space on the Windows system drive (35%). Available components are normalized to their combined weight; the explanation reports how many of the three groups contributed. A missing or unsupported sensor is excluded instead of receiving a zero.

CPU and memory scoring use recent stored samples from a five-minute window. Twelve samples are required before those trend components are included. Their thresholds are intentionally broad indicators of resource pressure; a high CPU score penalty can be expected during rendering, compiling, gaming, or other demanding work. Workload context is not inferred yet.

## Current findings

- **Processor busy:** recent average at or above 90%; critical at or above 97%. The finding explains that demanding work can make this expected.
- **Memory pressure:** recent average at or above 90%; critical at or above 97%. The largest observed process working set can be shown as context, without claiming it caused the pressure.
- **Low system-drive space:** at or below 10% free; critical at or below 5%. The recommendation asks the user to review files or applications; Ausyn does not remove anything.
- **Low battery charge:** at or below 15% while Windows reports that the device is not connected to AC power.
- **Drive-reported reliability indicator:** Windows Storage reports Warning/Unhealthy for a physical drive, a wear indicator at 100%, or one or more uncorrected read/write errors. Ausyn names the Windows status or counter, recommends checking backups and the drive maker’s diagnostics, and does not claim imminent failure. Counter coverage and semantics vary by device and driver.

Each active finding is de-duplicated by rule, updated while the condition remains active, and marked resolved when it no longer matches. Resolved findings are retained for 30 days. Recommendations show their evidence and first-seen time.

Ausyn saves optional user feedback about whether a recommendation helped and optional before-and-after CPU/memory readings with the local incident record. Recommendation answers and the proactive dashboard can use aggregated reports from the selected local history window; summaries count all matching outcomes even though the incident list itself is capped at 250 visible rows. A percentage is shown only after five rated reports for the same finding type. Questions about previous recommendation outcomes are handled locally and are not sent to cloud AI. These self-reports and measurements do not prove that a recommendation caused an improvement, and Ausyn does not automatically change the system.

The assistant can summarize the latest software inventory changes and device changes visible in the current snapshot when asked what changed. Software entries are based on periodic installed-app inventory comparisons; a detected change is a lead for investigation, not proof of cause. The user should compare its recorded time with health history and Windows events. No inventory change is made by this explanation.

Slowdown questions are handled locally. Alongside current readings, sustained findings, and nearby Windows events, the answer summarizes saved CPU and memory averages, peaks, endpoints, observed span, and selected history window when enough valid points are available. A missing or short history is called out; trend movement and event timing are context, not proof of root cause.

When a sustained CPU or memory finding has a recorded start time, Ausyn can also show installed-app inventory changes it observed within the preceding 24 hours. This is based on inventory observation time, not installation time, and is presented only as a lead to compare with the user's own timeline—not as a root-cause claim.

Process questions prioritize matching executable names when they appear in the latest process sample and distinguish instances by PID. Questions about app impact compare the current process snapshot with current system readings but do not attribute cause. The Performance page also shows the foreground app Windows reported for the latest process sample, its PID, and available CPU/working-set readings; its matching row is highlighted. This identifies which window was in front, not which process caused system pressure. On Performance, a selected process has up to 30 temporary CPU, working-set, and I/O samples; the profile summarizes averages/peaks and counts co-occurrence between a large working set (at least 15% of installed RAM) and high system memory use (at least 85%). Those paired observations are a short selected-process association, not a prediction or causal test. Working sets can include shared pages. Samples are held in memory and cleared on selection change or app exit; Ausyn does not retain long-term process history.

## What this score does not assess

Battery wear, historical/component-specific temperature trends, drive-reported reliability counters, Windows Defender state, Windows Update state, and device-specific firmware signals are not included in the partial performance score. Ausyn can still report a sustained reading above a Windows ACPI thermal-zone trip point when that signal is available; it does not claim that zone is the CPU/GPU die temperature. The UI names these gaps so the score cannot be mistaken for a complete hardware or security audit. Drive-reliability questions use current local Windows readings and active local findings; they are not sent to a cloud provider.
