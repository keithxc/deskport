# DeskPort

A remote desktop workspace built on Moonlight and Sunshine.

**Goal:** keep your remote desktop ready in the background, bring it onto your
current workspace with one action, and tuck it away without reconnecting.

The intended product is one application per computer with both viewer and optional
host roles: pair devices once, then open their desktops from a shared device list.
Dedicated displays that follow the viewer window size are also planned. See the
[architecture](docs/ARCHITECTURE.md); the unified mutual-pairing flow and automatic resizing are not yet implemented.

**Status: development preview.** The macOS package now combines the Moonlight-based
viewer, bundled Sunshine host and a native virtual display in one application.
The sharing page manages startup, display presets, permissions and incoming PIN
pairing. See [macOS packaging](docs/MACOS_PACKAGE.md) for installation and limits.
The Linux build currently provides the viewer. Windows packaging, seamless live
resolution changes, bidirectional clipboard and one-action persistent-viewer recall
remain unfinished. Each direction is paired separately in this preview.

## Build and run on Linux

With Nix and flakes enabled:

```sh
git clone https://github.com/keithxc/deskport.git
cd deskport
nix build
./result/bin/deskport
```

The pinned Nix build supplies upstream submodule dependencies automatically.
`nix run . -- --help` prints the inherited command-line interface. A host still
needs Sunshine and a separate pairing with DeskPort. No personal host or pairing
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
- No upstream Moonlight update prompts for this independent development build.
- A locked Nix environment and a Linux build workflow.

The host list and pairing UI are still inherited from Moonlight. The close button
still ends the stream; close-to-hide is part of the next milestone, not available
behavior. Some inherited wording/artwork remains during the initial port.

## Platform scope

| Platform | Current scope |
| --- | --- |
| Linux x86-64 | Initial build and CLI smoke-check target; KDE Wayland / AMD is the first live-use target |
| Linux ARM64 | Nix package definition only; native build and runtime not yet verified |
| macOS | Apple Silicon / macOS 26 all-in-one development package; native viewer, host and virtual display |
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
is installed separately on the host.

GPL-3.0-or-later; see [LICENSE](LICENSE), retained source notices and each
submodule's license. Original documentation is preserved in
[README.upstream.md](README.upstream.md).
