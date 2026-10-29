# Building Ostrich 220

Everything builds on Linux (developed on Ubuntu 24.04). Windows binaries are cross-compiled
there with mingw-w64.

## 1. Packages

```sh
sudo apt-get install --no-install-recommends build-essential git python3 pkg-config ccache \
    libjack-jackd2-dev libgl1-mesa-dev libx11-dev libxext-dev libxcursor-dev \
    libcairo2-dev libdbus-1-dev g++-mingw-w64-x86-64
```

For MAME (only to work on the driver; see RUNNING.md) add
`libsdl2-dev libsdl2-ttf-dev libfontconfig-dev libpulse-dev qtbase5-dev qtbase5-dev-tools
qtchooser qt5-qmake`. The measurement tools in `tools/` need `python3-numpy python3-scipy`.

`g++-mingw-w64-x86-64` brings two thread models, chosen with `update-alternatives`. Builds
here and in CI use **win32** (the default on Ubuntu 24.04): the binaries then need no mingw
runtime DLL. Objects from the two models must not be mixed, or libstdc++ ends up with two
opinions about `std::mutex`.

## 2. Clone

```sh
git clone --recurse-submodules https://github.com/Helge666/ostrich220.git
```

All three levels are needed: `mame`, `plugin/dpf`, and DPF's own `dgl/src/pugl-upstream`.
After a clone without `--recurse-submodules`, or after a pull that moves a submodule:

```sh
git submodule update --init --recursive
```

`git submodule status --recursive` shows a leading space on every line when all is in place.

## 3. Build

```sh
cd plugin/ostrich
make -j5          # Linux: VST3, CLAP, standalone (JACK)  -> plugin/bin/
make -j5 win      # Windows: VST3, CLAP, standalone       -> plugin/ostrich/bin-win/
```

Each run, in order:

1. builds the emulation core, `plugin/tools/build_u220_core.sh`: MAME's CPU and PCM-chip
   sources, unmodified, against the shim in `plugin/compat/`, plus `plugin/core/`, into
   `plugin/build-u220/libu220core.a` (Windows: `plugin/ostrich/build-win/core/`), and the
   tools `u220_render` and `u220_reverb_run`;
2. links the plugin. A core newer than a binary forces the relink.

The Windows binaries are statically linked and depend on nothing beyond Windows. The
standalone uses DPF's RtAudio backend on Windows and JACK on Linux.

## 4. DPF

`plugin/dpf` is our fork of DPF, [Helge666/DPF](https://github.com/Helge666/DPF), branch
`logan`; DPF's own pugl submodule points at [Helge666/pugl](https://github.com/Helge666/pugl),
branch `logan`. The changes we need are commits there, not patches here, and the submodule
is pinned to a tag (`logan-1`, ...). Nothing is applied at build time: `plugin/dpf` stays
clean, and `git submodule status --recursive` shows a leading space on its lines as well.

What the fork adds to DISTRHO/DPF, as far as Ostrich uses it:

| Change | What it does |
|---|---|
| JACK, CLAP, VST3: DSP → UI state | `updateStateValue()` reaches the UI (the display reaches the panel); under CLAP it was a stub that threw the value away |
| quiet state updates | no debug message on every DSP → UI update |
| LV2: `requestSizeChange()` | asks the host to resize, without pinning the window's minimum |
| VST3: 16 MIDI channels | the MIDI input bus has 16 channels (Cubase folded all onto channel 1) |
| VST3: UI idle watchdog | asks the controller again when an idle answer is lost |
| pugl (Windows) | a draw that throws, or one that never returns, cannot stop the view from redrawing |
| VST3: size rounding | accepts sizes within rounding; the minimum size in physical pixels on Windows |
| VST3: extra outputs off | extra outputs are main buses, off until the host switches them on |
| MIDI output | SysEx (VST3, CLAP) and program change (VST3) |
| Windows: file dialog | closing a running file dialog closes the dialog, not the host's plugin window |
| VST3: UTF-8 state | state strings (file paths, card names) converted between UTF-8 and UTF-16 for real; DPF kept only ASCII |

The fork also carries changes from Drumship One that Ostrich does not use:
`Plugin::setParameterName()`, `Plugin::setPortGroupName()`, and parameter groups as VST3
units and CLAP modules.

A change to DPF is a commit on the fork's `logan` branch and a new tag; then move the pin
here (`git -C plugin/dpf fetch --tags`, `git -C plugin/dpf checkout logan-N`) and commit the
new pointer. After the pin moves, build DGL from clean (`rm -rf plugin/dpf/build*`): its
build does not notice a change in a file that `pugl.cpp` includes, and the old library
would be linked as it was.

## 5. Checking it

`plugin/tools/ostrich_clap_render.c` loads the CLAP without a host or a display, plays a
note, presses panel keys, saves and loads state, and writes all six outputs to a WAV:

```sh
gcc -std=gnu11 -O2 -o ostrich_clap_render plugin/tools/ostrich_clap_render.c \
    -Iplugin/dpf/distrho/src -ldl
OSTRICH_ROMS=/path/to/roms ./ostrich_clap_render plugin/bin/Ostrich220.clap out.wav
```

Its arguments and environment variables are described at the top of the source.
`OSTRICH_DEBUG=1` makes the plugin print what the machine's display shows after each key.

The core can be null-tested against MAME: `u220_render --no-filter` with the MIDI arrival
times from the MAME driver's log renders bit-identical to MAME (±1 LSB rounding) for all
128 timbres and all drum keys.

## 6. Before publishing

A clone can fetch only pushed commits. Check that both submodule pins are on their remotes:

```sh
git -C mame ls-remote origin | grep "$(git rev-parse :mame)"
git -C plugin/dpf ls-remote origin | grep "$(git rev-parse :plugin/dpf)"
```

And that the history holds no ROM, wave, card or manual file:

```sh
git log --all --name-only --format= | sort -u | grep -iE '\.(bin|rom|syx|ic[0-9]+|wav|mid|nv)$'
```

## 7. Where things are

| Path | What |
|---|---|
| `plugin/ostrich/` | the plugin: DSP (`OstrichPlugin.cpp`), panel (`OstrichUI.cpp`), DAC stage, resampler |
| `plugin/core/` | the emulation core `U220Core` and the effect models |
| `plugin/compat/` | the minimal MAME framework the core compiles MAME's devices against |
| `plugin/tools/` | core build script, `u220_render`, `u220_reverb_run`, `ostrich_clap_render` |
| `mame/src/mame/roland/roland_u220.cpp` | the MAME driver; `roland_u220_ic25.h` is shared with the core |
| `tools/` | measurement, A/B and SysEx tools (Python), MAME helper scripts |
| `analysis/` | what is known about the hardware and the firmware |
| `listen/` | notes on listening tests (the audio itself stays out of the repository) |
