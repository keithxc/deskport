# Mutual binding preview

Enter the other computer's IP/domain in **Bind device** and choose **Request
binding**. The other computer accepts one prompt authorizing desktop viewing and
control in both directions. Both applications save the remote device automatically;
no PIN or second reverse pairing is required. Desktop sharing starts (or briefly
restarts) on both computers. Each OS still controls capture and input permissions. Sharing resumes when DeskPort
is reopened; choosing Stop sharing clears that launch preference. Starting at OS
login remains a separate opt-in setting.

The binding endpoint is TCP 48991, separate from the selected Sunshine video port
family. An occupied binding port fails visibly without stopping other services.
Both applications must be open and reachable. This preview has no internet relay;
local/Tailscale addressing is the intended path. Discovery still filters self hosts.

## Trust and state

The binding socket uses TLS 1.2 or newer and the application's existing viewer
certificate and private key. Both peers prove possession of their certificate key.
The requestor authorizes the entered endpoint; the recipient explicitly approves
its displayed peer name/address and key fingerprint. This is trust on first use:
a name or domain is not an independently verified real-world identity. First-use
approval on a hostile network without checking the fingerprint cannot exclude
an active intermediary. Subsequent known-address key changes are rejected, and
request order, transaction IDs, message sizes and timeouts are checked.

Only public certificates and host identity/port metadata cross the binding socket.
No private key, host admin password or PIN is sent. Approval on each endpoint
provisions the peer viewer certificate into that endpoint's own Sunshine state.
The host process is stopped before updating `root.named_devices`; the DeskPort
instance lock is held during an atomic write. Unrelated clients and state fields
are preserved. An existing entry with the same certificate is replaced rather
than duplicated, so later local revocation removes that device's access.

This state adapter follows the pinned Sunshine versions: macOS 2026.906.222525 and
Linux 2026.516.143833 from the locked Nix package set. Revalidate the named-device
schema on upstream upgrades. Existing independently installed Sunshine instances
and their state are never touched.

Bindings are stored in DeskPort's private application-data `binding/peers.json`,
with owner-only file permissions. Host/client certificates remain pinned in the
viewer device list. OS keychain migration for the inherited viewer private-key
storage remains separate work; this preview does not claim keychain-backed storage.

Binding is complete only after both endpoints report their local grant persisted.
Interrupted approval may leave a locally approved, incomplete binding, shown in
Saved bindings. It is never silently promoted to mutual success. The local access
removal button removes the peer from this computer's host and briefly restarts it.
Remove the binding at the other computer too to revoke both directions; this
preview does not claim offline remote revocation or distributed atomic commits.

## Linux host scope

The Nix package references its own pinned Sunshine runtime under `libexec`.
It uses independent state and the same collision-tested DeskPort port family.
KDE sessions use KWin capture; other desktops use portal capture. The existing
Linux desktop is shared; a private Linux virtual display is not implemented.
The GUI session must provide capture permission, an encoder and `/dev/uinput`
access for remote input. No root service or broad device permission is installed.

## Validation

`python3 scripts/test-host-lifecycle.py --binding` creates disposable certificates,
loopback TLS peers and fake host children. It checks approval gating, both-direction
persistence, restart recovery, stale confirmation, rejection, disconnect, malformed
and oversized messages, replay, changed endpoint keys, unrelated-state preservation,
certificate deduplication and local revocation. It never captures a real desktop or
injects input. The existing lifecycle suite and Linux package build remain required.

Live acceptance: initiate one request, approve once, verify both device lists and
both-direction picture/input, restart without another approval, then verify local
revocation. Pairing status alone does not establish working capture or input.
