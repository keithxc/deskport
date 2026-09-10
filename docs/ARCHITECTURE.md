# Unified peer application

Product decision, 2026-09-09. This describes the intended architecture; the macOS preview now bundles viewer, host and native virtual-display components.
The host UI handles incoming PIN pairing; a single mutual-pairing flow remains planned.

## One installation, two roles

Ship one DeskPort application per desktop platform. Every supported installation
can connect to another computer and can enable sharing its own desktop. Hosting
is opt-in and reports platform permission readiness before advertising availability.

Keep the Moonlight-based viewer, Sunshine host and desktop-integration helper as
separate components supervised by the application. They can fail, restart and
update independently. The user sees one device list, pairing flow and settings UI;
they should not need to configure a second remote-access product or its web UI.

The Sunshine component handles capture, encoding and remote input. The helper
handles host capabilities, display management and clipboard operations that are
not covered by the media protocol. Preserve upstream notices and source provenance.

## Pairing and access

One user-visible pairing interaction should establish persistent, mutually verified
device trust. Explicitly show which directions and capabilities are being enabled:
view/control desktop, clipboard, and eventually selected file access. A receive-only
computer need not expose its own desktop. Support device revocation from either end.

Existing GameStream pairing is directional. A unified flow must arrange the required
credentials for each enabled direction; a successful viewer-to-host pairing must
not silently grant the host access to the viewer. Never send a Sunshine administrator
password to peers. Persist credentials in platform-protected storage and bind any
helper channel to the verified device identity, not just its network address.

Initially use reachable local/Tailscale addresses. Pairing does not make an offline
or sleeping computer reachable, and cloud rendezvous/relays are outside the first
milestone. End users should not need to set up SSH keys to use the finished product;
SSH remains a development/diagnostic tool.

## Platform installation

macOS requires a stable bundle identity and signing identity across updates for
privacy permissions. Screen recording and accessibility require initial local user
authorization. Do not bypass OS consent or promise that OS changes can never require
reauthorization. Keep background hosting in the appropriate logged-in GUI session.

The current personal server deployment uses the official signed Sunshine bundle.
Bundling it into DeskPort, service installation, notarization and updates need their
own platform validation. Merely copying two executables does not complete packaging.

## Desktop workspace

Keep the remote session alive while its viewer window is hidden. Give the session
a dedicated host display where supported; resize that display to the viewer's content
area with explicit HiDPI and codec-size handling. Keep the display alive when hidden.
Verify hot reconfiguration before claiming seamless resizing; fall back to presets
when necessary. Avoid changing the physical monitor's working layout.

## Delivery sequence

1. Stabilize the existing Linux-viewer/macOS-host deployment and its permissions.
2. Validate persistent sessions and a dedicated, resizable macOS display.
3. Build unified installation, host status and pairing management for that path.
4. Validate the reverse macOS-to-Linux path before claiming mutual desktop access.
5. Extend the same contract to Windows and qualify each platform independently.

Checkpoint for unified pairing: install on two fresh test profiles, authorize OS
permissions, pair once, connect in each allowed direction, restart both applications,
repeat without pairing, then revoke and verify that new access is rejected. Test
desktop input and clipboard separately. File transfer is a later capability.
