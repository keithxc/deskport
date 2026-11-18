# PC task integration

## Development consolidation — 2026-11-18

The current candidate incorporates the remaining desktop development chains on
top of main `37fabe160dcafcb5f2f35fe9560b8ee4a9df3b5b`. The earlier "Retain branch"
decisions below describe the October checkpoint and no longer determine source
integration. Existing main behavior is retained where an older implementation has
already been superseded; overlapping session, input and encoder changes are
adapted to the current owners. The reviewed source is consolidated without
publishing local development notes from the old branch histories.

The candidate includes background recall, power recovery, desktop typography,
browser recovery, live encoder rate, HDR negotiation, Windows lifecycle and macOS
lifecycle work. GNOME production changes already present on main are retained.
Seamless sources and the standalone lab remain experimental, outside the default
desktop transport and the admitted M3 product contract.

Current integration checks cover native macOS session/binding contracts (22
passing cases), native clipboard demand, shutdown sanitizers with meaningful
negative controls, capture/color fixtures, generated translations and 71 isolated
Chromium UI checks. These are local contract and fixture results. Package builds,
platform-specific hardware acceptance and manual activation are separate gates;
the historical physical results below are not promoted to this candidate.

## Historical integration — 2026-10-18

Seven tested implementation/documentation slices are eligible for this batch.
This does not declare every platform, GPU, input device or installed build accepted.
The integration preserves the activated macOS/iPad checkpoint on main and changes
no installed application, system permission, release asset or mynix configuration.
Seamless remains deferred.

| Task | Integration decision | Verified scope and remaining gate |
| --- | --- | --- |
| 1. Windows installer/service lifecycle | Retain branch | Regression, cross-build and ordinary-user fixtures pass; first install, upgrade/rollback, uninstall/reinstall and full native lifecycle remain pending. |
| 2. Core PC interoperability | Retain branch | Native macOS clipboard fix and isolated tools pass; the 76-case four-direction physical matrix is incomplete. |
| 3. GNOME policies | Retain branch | Topology/recovery and Nix pass, but intermittent capture startup timeout and PipeWire/DisplayConfig recovery failure block acceptance. |
| 4. Hyprland policies | Integrate `559c5dcb` | IPC, HostManager, Nix and Lua/legacy WLR/H.264 probes pass. Physical topology and end-to-end client media/input remain separate. |
| 5. X11 workspace | Integrate `72eb11a4` | Preconfigured inactive dummy outputs, 1x extension, packaged capture and recovery pass. No general GPU virtual output, 2x or topology-policy promise. |
| 6. Wayland clipboard | Integrate `4a8f83e3` | Portal/wlr fallbacks, demand/cancel tests and isolated GNOME/Hyprland paths pass. Physical file-manager paste and wlr-v1-only remain pending. |
| 7. Local IME | Integrate through `abbe167e` | Native macOS/Windows Pinyin and Linux X11 CJK fixtures, builds and regressions pass. Default off; top-left candidates; native Wayland, HiDPI/fullscreen and remote insertion pending. |
| 8. Client power recovery | Retain branch | Platform builds and isolated lifecycle/input/worker checks pass; real sleep/lock/wake, physical focus/input and route recovery remain pending. |
| 9. Background/recall | Retain branch | Nix and native macOS/Windows synthetic windows pass; Linux GUI and real stream resource/audio/input acceptance remain pending. |
| 10. Live encoder bitrate | Retain branch | Builds, regressions and VideoToolbox hardware switches pass; NVENC/AMF/VAAPI hardware and network/stream acceptance remain pending. |
| 11. HDR/codec negotiation | Retain branch | Codec/VAAPI/native capture fixtures pass; complete macOS build and physical HDR/end-to-end validation remain pending. |
| 12. Web localization | Integrate `8e01e234` | Shared 126-key/22-locale resources, Chromium/RTC/HTTPS and Nix pass. Native-speaker and broader physical/browser checks remain pending. |
| 13. Windows Web/Safari | Retain branch | Build/Linux/Windows loopback fixtures and later 12 native Safari checks pass; Windows-host Web end-to-end remains incomplete. |
| 14. macOS lifecycle | Retain branch | Topology/lifecycle/binding/sanitizer and Nix checks pass. The separate activated iPad takeover checkpoint is on main; physical multiscreen, upgrade/rollback and permission/recovery-helper lifecycle remain pending. |
| 15. Linux packages | Integrate through `f6084121` | Pinned VA runtime/launcher repair, Ubuntu candidate control, Nix, synthetic AV and 28 VM teardown checks pass. Historical old-DEB rollback and GNOME AppImage failures remain explicit. |
| 16. Fonts/RTL | Retain branch | Native macOS page/font and UI checks pass; required Linux/Nix and Windows checks have not run. Native-speaker review remains pending. |
| 17. Support evidence | Integrate through `edf745b2` | Versioned provenance, references, CI scope and isolated build/CLI checks pass. Refresh the snapshot for this batch without relabeling historical passes. |

Hyprland and X11 both extend HostManager capability detection. Their integration
retains both states, keeps X11 policy omission/rejection, and preserves the
Hyprland legacy-helper fallback. The lifecycle fixture resets both backend test
environments so one case cannot contaminate another.

The IME and Web branches had independent shared-core descendants. Core
`f34fc7b1524ce436185c4b085a903e6af4314790` merges their catalogs and IME contract;
Git, Nix revision and NAR pins advance together. Qt/Web resources must still
match the combined catalog. Individual-branch tests are historical evidence;
integration build and regression results are recorded separately after execution.

## Additional requested UI change

Integrate desktop `codex/square-device-cards` at `1c146302`: device, group and
add cards track the grid cell width, including folder cards, with Material
padding explicitly cleared. The branch passed its macOS build and 27 UI checks;
the combined Linux UI suite and build are checked separately for this batch.
The private mobile repository and its same-named branch are outside this desktop
integration.

## Integration verification

The combined core/regression snapshot is
`49cd7a1a0134c6b428b99026750a812a5be2c88b`. Thirteen targeted groups passed:
shared core and Qt workspace, source/compiled/browser translations, device
preferences, host lifecycle (45), binding (172), Hyprland IPC, clipboard demand
(8, with three non-Linux cases skipped), private-bus portal (19), local IME,
Linux VA runtime and support-evidence validation.

The later square-card snapshot is
`ddf6e6eaacb806245ade99abdb425d4e2cbfab95`. Its only additional production change
is `PcView.qml`; the combined Linux UI suite passed 27 cases. The full native
Linux Nix build, including Sunshine, and the packaged CLI version check passed.
The output is `/nix/store/05cqvvapqls095wlx4c7zpa3x58nx1pg-deskport-0.7.2`.

Acceptance-only preBuild hooks constrain both actual Sunshine and client builders
to CPU 0–3, nice 15 and at most four jobs. The initial build was cancelled when
the dependency was observed outside those limits; its logs are retained separately.
The successful retry preserves production sources and changes only those resource
hooks in the exported flake. Actual compiler restrictions were recorded for both
stages. No prior passing regression group was repeated for that hook change.

Source verification covers all 1,611 tracked application blobs, with the explicitly
hashed resource-only flake exception, and the matching shared-core NAR. Test/build
processes exited and the task's canonical build lock was released. These results
do not activate a package or extend the native acceptance scopes above.
