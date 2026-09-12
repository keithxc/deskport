# DeskPort 0.1.6

Released 2026-09-12. This release fixes a build defect that made DeskPort report
a version it was not, and adds a tray entry that restarts the application so a
remote viewer can move onto a newly installed build.

## Changes

- The version is now carried in a generated `version.h` instead of a compiler
  define. A define lives only in the Makefile, so bumping `app/version.txt` left
  every already-compiled object on its old string: the shipped 0.1.5 binary
  reported 0.1.4 in the window and announced itself as 0.1.2 to the update
  check, while `Info.plist` — regenerated at qmake time — correctly said 0.1.5.
  qmake rewrites the header only when the version really changes, so ordinary
  incremental builds are unaffected.
- The tray menu gained **Restart DeskPort**. Both service managers DeskPort
  installs deliberately leave a clean exit alone (launchd's
  `KeepAlive.SuccessfulExit=false`, systemd's `Restart=on-failure`), so the
  restart waits for this process to exit — releasing the single instance lock
  and the host's ports — and only then starts the binary that is on disk now.
  On macOS it prefers `launchctl kickstart` so the new process stays
  launchd-managed, falling back to `open -n -a`; on Linux it prefers
  `systemctl --user restart`, falling back to the executable itself.

## Validation

The macOS package built and linked on mm4, and the previously stale objects
(`systemproperties.o`, `autoupdatechecker.o`) recompiled to the new version, so
the binary now carries a single version string. The Linux branch of the restart
path was type-checked separately; pk4 was unreachable during this release, so
its Nix build and the service and UI suites did not run for 0.1.6.
