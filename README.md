# DeskPort

A remote desktop workspace built on Moonlight and Sunshine.

**Goal:** keep your remote desktop ready in the background, bring it onto your
current workspace with one action, and tuck it away without reconnecting.

**Version: 0.1.2 — input defaults update.** DeskPort combines a viewer and optional host
in one application, with a shared device list, mutual binding and permission
controls. The macOS package includes Sunshine and a native virtual display;
the Linux Nix package includes a Sunshine host for the existing desktop.

The dedicated macOS workspace follows the client window at 1× host scaling,
using the client's logical dimensions to avoid a 2× supersampled video stream.
Resizing briefly reconnects video while retaining the client window and showing
a loading animation. It is not seamless encoder reconfiguration.

See the [release notes](docs/RELEASE_0.1.2.md),
[architecture](docs/ARCHITECTURE.md) and
[macOS installation guide](docs/MACOS_PACKAGE.md).
Persistent hide/show without reconnecting, automatic physical-display topology
management, image clipboard sharing and Windows packaging remain unfinished.
Bidirectional text sharing is available between opted-in bound DeskPort devices.

## Build and run on Linux

With Nix and flakes enabled:

```sh
git clone https://github.com/keithxc/deskport.git
cd deskport
nix build
./result/bin/deskport
```

The pinned Nix build supplies upstream submodule dependencies automatically.
`nix run . -- --help` prints the inherited command-line interface. Start Sharing on the host and bind the devices before connecting. Legacy
Sunshine PIN pairing is also available. No personal host or pairing
credential is included or imported from Moonlight.
New manual addresses default to DeskPort's port `48989`. Include the port shown
on the host's sharing page if different, or use `host:47989` to connect explicitly
to a default standalone Sunshine installation. Saved/discovered endpoints retain
their own ports.

For an editable native build:

```sh
git submodule update --init --recursive app/SDL_GameControllerDB \
  moonlight-common-c/moonlight-common-c qmdnsengine/qmdnsengine \
  soundio/libsoundio h264bitstream/h264bitstream
nix develop
mkdir -p build
cd build
qmake ../moonlight-qt.pro CONFIG+=disable-prebuilts
make -j4
./app/deskport
```

The upstream project filenames remain unchanged to keep the fork reviewable.

## What is different today?

- Separate `deskport` executable, `DeskPort` Qt settings namespace and
  `io.github.keithxc.DeskPort` Linux application ID.
- Windowed streaming and absolute-pointer control by default.
- Mute on focus loss; game optimization, gamepad mouse, multi-controller mode,
  background gamepad input and Discord presence disabled by default.
- No upstream Moonlight update prompts for this independent application.
- A locked Nix environment and a Linux build workflow.

The desktop interface provides device, sharing and settings pages, with language
selection and separate host permissions. The close button still ends the stream;
close-to-hide is part of the next milestone. Some inherited wording remains.

## Platform scope

| Platform | Current scope |
| --- | --- |
| Linux x86-64 | Initial build and CLI smoke-check target; KDE Wayland / AMD is the first live-use target |
| Linux ARM64 | Nix package definition only; native build and runtime not yet verified |
| macOS | Apple Silicon / macOS 26 all-in-one package; native viewer, host and virtual display |
| Windows | Inherited native source; DeskPort build and packaging not yet verified |

The first development workflow is Linux → macOS through Sunshine. Client platform
support and host support are separate: a Mac host does not require a DeskPort Mac
client. Hardware decode, live input, image quality and recall latency need real
session testing; a successful build does not establish them.

## Next milestone

Keep one session connected across **50 hide/show cycles**, show the window on the
current workspace, and reliably return local input. Measure fresh-frame latency
and background resource use separately from window appearance.

See [the roadmap](docs/ROADMAP.md) for acceptance criteria and deferred features,
and [upstream notes](docs/UPSTREAM.md) for provenance and maintenance boundaries.

## Validation

```sh
nix build
python3 scripts/deskport-smoke.py ./result
```

The smoke check uses temporary XDG configuration/cache directories and an offscreen
Qt platform. It does not pair with a host, start a stream or inject input.

## License and attribution

DeskPort is an independent derivative of [Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt),
initially based on v6.1.0. It is not an official Moonlight or Sunshine release.
Moonlight provides the streaming foundation; [Sunshine](https://github.com/LizardByte/Sunshine)
is bundled in the macOS package and supplied by the Linux Nix package.
Separately installed Sunshine services are kept independent.

GPL-3.0-or-later; see [LICENSE](LICENSE), retained source notices and each
submodule's license. Original documentation is preserved in
[README.upstream.md](README.upstream.md).


### Daily desktop controls

Closing a window keeps DeskPort running in the tray/menu bar. Use **Open DeskPort**
to recall it, **Disconnect viewer** to end only the current connection, or
**Quit DeskPort** to exit the service. Local sharing continues when a viewer closes.

Plain-text clipboard sharing and system-shortcut capture are enabled by default on
both bound devices. Setting changes apply after reconnecting. Only new copies are
shared, up to 1 MiB; images and files are not transferred. In desktop pointer
mode, keyboard routing follows the pointer inside the focused video.
**Ctrl+Alt+Shift+Z** releases input;
**Ctrl+Alt+Shift+Q** disconnects the viewer. Click inside to regain input after
explicit release. OS-reserved shortcuts depend on the desktop compositor.

Login startup and recovery require an active graphical login session. See
[acceptance checks and limitations](docs/INPUT_SERVICE_ACCEPTANCE.md) before relying
on a computer for unattended access.
