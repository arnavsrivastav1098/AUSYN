# Local data and database

Ausyn stores data per Windows user and keeps monitoring independent from optional cloud AI. It does not upload the history database.

## SQLite history

The database file is named `ausyn-history.sqlite3` under Qt's per-user local application-data directory. **History & reports** shows the actual path and current database size. The database uses SQLite WAL mode and schema migrations; the current source supports migrations through schema version 9.

Aggregate system readings are stored about every ten seconds. Connected fixed-volume capacity samples are recorded at most every five minutes per drive. Configured retention is 7 to 90 days (30 days by default) for telemetry, volume samples, and resolved findings; active findings remain while the rule matches. High-frequency per-process readings, startup inventory rows, and raw Windows event records are not written to telemetry history.

The schema stores normalized system readings, volume capacity samples, and finding/incident records, including optional recommendation feedback and before/after CPU or memory verification. A recommendation rating is a user report, not proof that an action caused a change.

## Preferences and conversations

Settings, app-inventory baselines, creator-easter-egg state, and bounded hardware/software change timelines live in the same per-user local application-data area but are separate from the telemetry database. Conversation persistence is off by default; when enabled, the most recent 100 messages are saved locally. An API key is protected with Windows DPAPI, not stored in plaintext.

## Export, backup, restore, and recovery

Use **History & reports** to export CSV or a PDF/HTML report, or to clear telemetry history. Clearing history also removes saved finding feedback and checkpoints, but it does not delete preferences or optional conversation history. Conversation history has a separate clear control in Settings.

History & reports can create a standalone SQLite backup using SQLite's online `VACUUM INTO` snapshot. Ausyn checks the snapshot's SQLite integrity and expected history tables before writing it to the chosen location. To restore, choose the backup and confirm replacement. Ausyn checks integrity, schema version, and required tables before touching the current database, stages the backup beside the database, checkpoints and closes the live connection, preserves the current file during the swap, and reopens the restored database. If opening the restored database fails, Ausyn attempts to put the previous database back automatically. Chats, settings, and app-inventory baselines are not included in this history backup. Keep backups in a separate drive or secure location if protection against device loss is needed.

If the database cannot be opened or written, Ausyn continues live monitoring, reports the local-history problem, retries initialization, and temporarily buffers a bounded number of normalized samples in memory. That buffer is lost if Ausyn exits before storage recovers and cannot guarantee a complete record after a long outage. Creating a verified backup before replacing or clearing important history is recommended.
