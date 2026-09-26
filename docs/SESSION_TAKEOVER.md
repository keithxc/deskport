# Confirmed session takeover

2026-09-19: A new viewer must not bypass an occupied display lease by launching a
second fixed-size Sunshine stream. The wire contract is owned by
`shared/deskport-core/protocol/SESSION_TAKEOVER.md`.

The desktop viewer opts into admission on its retained display TLS connection.
It asks before takeover, defaults the dialog to Cancel, and treats any failure
after capability negotiation as fatal. Video-only resize/reconnect keeps the
admission connection. Losing an admitted connection stops the stream.

PeerManager checks completed binding and TLS identity, issues a connection-bound
30-second single-use challenge, and serializes admission. HostManager calls the
pinned-certificate, Basic-authenticated loopback management API. Sunshine shares a
mutex across snapshot/claim, GameStream launch/resume/cancel and final RTSP
admission. The verified request object owns the certificate mapping; an unrelated
TLS handshake cannot overwrite it. Generation fencing rejects older pending RTSP
negotiations. Joining the old streams and an input task-pool fence precede success.
PeerManager also waits for display recovery and clipboard-helper exit.

The management endpoint does not remove pairings, close desktop applications,
kill Sunshine, or restart sharing. A timeout fails closed. An eviction already
completed cannot be undone if the new client disconnects before receiving success.

## Desktop adjustment

Device settings and the tray menu offer 0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.2, 1.3,
1.4 and 1.5. The final adjustment uses core `dp_workspace_adjust` after the existing
desktop calculation; scale is unchanged and the protocol bounds still apply.
Smaller values enlarge controls; larger values fit more content. The tray action
saves only this device's adjustment key and reconnects video using the retained
admission connection. It does not overwrite other preferences from an old snapshot.

## Isolated acceptance alongside an existing host

Do not run a candidate with the normal `--share` option while testing alongside a
production instance. Use a signed candidate bundle and:

```sh
bash scripts/run-isolated-session-host.sh /absolute/path/to/candidate/DeskPort.app
```

The script creates a private temporary directory, a new identity and portable
settings. The test window requires explicit binding approval and shows the ports.
The full TCP/UDP group is checked on all IPv4 interfaces and a non-default private
port bucket is preferred. The binding listener moves to video base port + 2, so
normal clients can discover it from the video address. A temporary random binding
listener remains an alias. New bindings update the running host's authorization
without restarting it or acquiring a session. Also test binding a second client
while the first has an active stream; failure must preserve that stream.

The test display has a separate product/serial identity. **This parallel test mode
always uses its own extended workspace**, regardless of the client's topology
selection. It neither reads the production display-recovery journal nor mirrors,
disables or restores another display. This mode cannot validate production
mirror/primary-only behavior. Closing its window or the 15-minute deadline stops
only its own child processes. The temporary state directory remains for inspection.
Never reuse the earlier pre-isolation candidates for concurrent-host testing.

Check idle admission, cancel preserving old video/input, confirmed takeover ending
the old actual video, old held-key release, two concurrent contenders, repeat
resize/rotation, 0.5/1.0/1.5 adjustment and reloaded per-device settings. Compilation,
signing and fake-host tests do not establish these physical-device outcomes.

## Binding without session acquisition — 2026-09-26

Completing a binding returns the requesting desktop to Devices. Only an explicit
Connect action starts a stream. Incoming binding completion must not navigate
away from or replace an existing outgoing session.

The bundled host adapter adds `POST /api/deskport/trust`, protected by loopback,
Basic authentication, pinned host TLS, and rejection of browser Origin/Referer
headers. It atomically persists an approved certificate and updates live TLS
authorization under the host's authorization mutex. It does not call session
acquisition, change the admission generation/lease, terminate streams, or restart
the display/host. An unavailable endpoint fails binding without a restart fallback.
An initially stopped host can still be initialized normally. Explicit revocation
retains its existing restart behavior and is separate from granting a binding.
This is a bundled local adapter API; peer binding/session wire messages do not
change and no new shared-core negotiation is introduced.

On Linux, run `scripts/test-host-live-trust.py /absolute/path/to/built/sunshine` for disposable
loopback TLS, admission-lease preservation, persistence and authorization checks.
The test never launches video and does not establish active-stream acceptance.

## Chained sessions and cycles

Current admission is exclusive per host, not a distributed connection graph.
An incoming controller and an outgoing viewer can coexist on one desktop.
Consequently, a chain and a cycle can both pass the current per-host checks.
Chains add another encode/decode/network hop, and nested input depends on focus
and local shortcut handling. A cycle may recursively capture a viewer or feed
input back, depending on display layout and focus; it is not necessarily a
recursive image when separate virtual displays are used.

The sessionTopology 1 extension now enforces acyclic session chains for updated
desktop peers, including pending connections and takeover revalidation. Mutual
binding remains allowed. Desktop peers without the capability are rejected;
existing client-only mobile bindings remain compatible. Unreachable or changing
paths fail closed. See shared core `protocol/SESSION_GRAPH.md` for the contract,
concurrency argument, identity rules and limits. There is no fixed-resolution
fallback after a topology failure.
