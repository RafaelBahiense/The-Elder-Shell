# Terminal transport

The plugin exposes `TheElderShell-<process ID>` as a local Windows named pipe
when enabled. It uses byte mode, a current-user DACL, remote-client rejection
and one connected client. Connection I/O runs on a worker; the backend schedules
submission on the game thread. It shares the shell's request limits, variables,
transcript and game-session invalidation.

Frames contain a four-byte unsigned little-endian UTF-8 byte count followed by
that many bytes. Requests are bounded to 65536 source bytes plus the operation;
responses to 8 MiB. Oversized responses return an error instead of truncated
results. The connection starts with `hello\n1\nTheElderShell-<id>`.

| Request payload | Response |
| --- | --- |
| `ping` | `pong` |
| `input\n<source>` | `continue` for incomplete shell input, otherwise request states |
| `execute\n<source>` | Request states; incomplete input is rejected |
| `complete\n<cursor>\n<source>` | Request states with a JSON completion result |

Request states contain five newline-separated fields. The fifth field may itself
contain newlines, so split at most four times:

```text
pending or result
ticket ID
generation
status
result text
```

`pending` covers `Queued`, `Compiling` and `Submitted to VM`. `result` covers
`Completed`, `Rejected` and `Abandoned after load`. Errors before submission use
`error\n<reason>` and have no ticket. Pending heartbeats detect disconnection;
they do not extend the client's configured command deadline.

Each connection processes one command to a terminal state before accepting the
next. A retained request survives transcript clearing and trimming until the
connection releases it. Disconnecting releases retention, not Papyrus work.
Calls already submitted may still complete. Save loads abandon their tickets;
late callbacks cannot update the new game session. Neither side retries commands.

Completion uses a decimal UTF-8 byte cursor in the unmodified source. The cursor
must fall on a character boundary. Its result contains `begin` and `end` byte
offsets for replacement, `total` match count, `signature`, `parameter`, and
`items`. Up to 128 items are returned, each with `name`, `description`, insertion
`text` and `cursor`. The item cursor is a byte offset relative to `begin` in the
resulting document; it can cross existing parentheses beyond the inserted text.
Clients must convert offsets when their editor uses another indexing scheme.

Completion reads metadata on the game thread, shares the in-game completion
engine and rejects stale game sessions. It never executes expressions or getters,
creates a command ticket, or adds transcript entries. Completion request states
use ticket zero. An unavailable metadata source returns `Rejected`.

The backend owns poll/release callbacks, allowing pipe tests to run with
synthetic results without Skyrim. Tests cover framing, retention, Windows
connection lifecycle and the PowerShell client. Engine execution and in-game
settings require runtime validation.
