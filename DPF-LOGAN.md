# DPF-LOGAN: the DPF fork used by this project

This project builds on [DPF](https://github.com/DISTRHO/DPF) through our own fork, not on
DISTRHO/DPF directly. All LOGAN plugins share this fork. This file says what that means
for this project. It is the same file in every project; the master copy lives in the
`dpf-logan` working folder.

## The repositories

| What | Where | Branch |
|---|---|---|
| DPF fork | [Helge666/DPF](https://github.com/Helge666/DPF) | `logan` |
| pugl fork (DPF's `dgl/src/pugl-upstream`) | [Helge666/pugl](https://github.com/Helge666/pugl) | `logan` |
| Upstream | [DISTRHO/DPF](https://github.com/DISTRHO/DPF), [DISTRHO/pugl](https://github.com/DISTRHO/pugl) | `main` |

- `logan` = upstream plus our commits on top. `main` of each fork mirrors upstream and is
  never changed by us.
- Released states are tagged `logan-1`, `logan-2`, ... on both forks. Projects pin a tag.
- The fork's `.gitmodules` already points DPF's pugl submodule at `Helge666/pugl`, so a
  recursive clone gets the right pugl.
- Both forks are public and keep DPF's and pugl's ISC licence files unchanged.

## Current tag: `logan-1`

Base: DISTRHO/DPF `4238e1c` ("Merge branch 'develop'", 23.10.2025), pugl `5e2621d`.

Fixes:

| Change | What it does |
|---|---|
| VST3: 16 MIDI channels | the MIDI event buses report 16 channels; Cubase folded everything onto channel 1 |
| VST3 UI: idle watchdog | the view asks the controller again when a "ready" answer is lost; the panel no longer freezes in Cubase |
| pugl (Windows): stuck expose | a draw that throws, or one that never returns, cannot stop the view from ever redrawing |
| VST3 UI: size rounding | sizes within rounding are accepted, the minimum size is scaled outside macOS; the window no longer shrinks at 125 % in Cubase |
| Windows: file dialog | cancelling a running file dialog no longer closes the host's plugin window |
| VST3: UTF-8 state | state strings are converted between UTF-8 and UTF-16 for real; DPF kept only ASCII |
| quiet state updates | no debug print on every DSP → UI state update |

Additions:

| Change | What it does |
|---|---|
| DSP → UI state for JACK, CLAP, VST3 | `Plugin::updateStateValue()` reaches the UI in every format, not only LV2 (lock-free ring, 16 entries, values up to 16 KiB) |
| LV2: `UI::requestSizeChange()` | public; the UI asks the host for a new size through `ui:resize`; host-driven resizes are no longer echoed back |
| VST3: extra outputs off | with all outputs in port groups, only bus 0 starts active; extra buses stay main buses but off until the user switches them on |
| MIDI output: SysEx, program change | SysEx out for VST3 and CLAP, program change out for VST3 |
| `Plugin::setParameterName()` | renames a parameter at runtime; VST3 and CLAP hosts are told (LV2 keeps the TTL names) |
| parameter groups as folders | VST3 `IUnitInfo` (one unit per port group that holds parameters), CLAP module = group name; `Plugin::setPortGroupName()` renames a group at runtime |

Behaviour change to know about: under CLAP a parameter **without** a group now has an empty
module path; upstream put the parameter's symbol there.

`git log --oneline 4238e1c..logan-1` in a clone of the fork lists the commits. Each commit message
says why the change was made and which project it came from.

## Using the fork in a project

### New project

```bash
git submodule add -b logan https://github.com/Helge666/DPF.git dpf
git -C dpf checkout logan-1
git -C dpf submodule update --init --recursive
git add .gitmodules dpf
```

Use the HTTPS URL in `.gitmodules`, so anyone can clone without an SSH key. To push over
SSH anyway, set once per machine:

```bash
git config --global url."git@github.com:".pushInsteadOf "https://github.com/"
```

### Existing project that still uses DISTRHO/DPF or patches

1. Reset the DPF submodule to its committed state (drop applied patches in DPF and in
   `dgl/src/pugl-upstream`, delete any patch bookkeeping file).
2. `git submodule set-url <path> https://github.com/Helge666/DPF.git`,
   `git submodule set-branch -b logan <path>`, `git submodule sync`.
3. `git -C <path> fetch --tags`, `git -C <path> checkout logan-N`,
   `git -C <path> submodule sync`, `git -C <path> submodule update --init --recursive`.
4. Remove the project's patch folder and the patch step from the build (CMake or Make).
5. With Make: build DGL from clean once (`rm -rf <path>/build*`): DGL's build does not
   notice a change in a file that `pugl.cpp` includes.
6. Build, test, commit the new pointer together with the removed patches.

### Clone

```bash
git clone --recurse-submodules <project URL>
# or, after a clone without it:
git submodule update --init --recursive
```

`git submodule status --recursive` must show a leading space on the DPF and pugl lines. A
`+` or `m` means the submodule is not on the pinned state.

### Windows line endings

With `core.autocrlf=true` in the global Git config, a submodule is checked out with CRLF.
CMake and MSVC do not mind; Make on Linux may. For Make-based projects, before checking
out the submodule:

```bash
git -C <path> config core.autocrlf false
git -C <path>/dgl/src/pugl-upstream config core.autocrlf false
```

## Rules

1. **No DPF patches in projects.** The project's build applies nothing to DPF, and nothing
   in the DPF submodule is edited and left there.
2. **Changes to DPF are made in the fork only,** in the `dpf-logan` working folder (on
   Helge's machine `C:\Users\helge\Apps\dpf-logan`, clones `DPF/` and `pugl/`). There they
   become a commit on `logan` with an English message: what, why, from which project.
3. **A new tag per released state** (`logan-N`, on DPF, and on pugl if it changed). Tags are
   never moved.
4. **Each project moves its pin when it is ready,** by tag, after its own build and tests.
   Projects may be on different tags for a while.
5. **Push, fork, tag and upstream issues only after Helge confirms.** Texts for DISTRHO are
   in English; issues before pull requests.

## How a project asks for a DPF change

1. Note the problem in the project (what fails, in which host and format, how to reproduce).
2. Try the fix in the project's DPF submodule if that is quickest, but only as an
   experiment: `git -C <path> diff > fix.diff`, then reset the submodule.
3. Bring the change to `dpf-logan`: commit it on `logan` there, build and test **every**
   project that uses the fork against it (at least a build), tag it.
4. Move the pin in the project that asked for it; the other projects follow when they want.
5. Record the new tag below and in `dpf-logan/PLAN.md`.

When upstream DPF takes over one of our changes, it drops out of `logan` at the next rebase.

## Open: one install folder for all LOGAN plugins (Windows)

Not done yet; to be settled separately. The idea, agreed in principle on 29.10.2026:

- Development and test builds of all LOGAN plugins go to one folder per format under
  the per-user plugin location: `%LOCALAPPDATA%\Programs\Common\VST3\LOGAN\` (CLAP:
  `%LOCALAPPDATA%\Programs\Common\CLAP\LOGAN\`). Hosts scan it by default, subfolders
  included, and copying there needs no admin rights.
- Today they are scattered: Drumship One in the system folder
  `C:\Program Files\Common Files\VST3\`, Ostrich 220 directly in
  `%LOCALAPPDATA%\Programs\Common\VST3\`. Further candidates in the system folders:
  `Logan Runs.vst3`, `vm2gui.vst3`, `vm2gui PoC.vst3`, `CA-72.vst3`, `CA-72.clap`.
- When moving: close Cubase, **move** (never leave a copy behind: two bundles with the
  same class ID make the host pick one at random), then let the host rescan. Plugin IDs
  stay the same, so projects keep loading them.
- Release installers for other users are a separate question.

## Projects on the fork

| Project | Submodule path | Build | Tag |
|---|---|---|---|
| Drumship One | `dpf` | CMake | `logan-1` |
| Ostrich 220 | `plugin/dpf` | Make | `logan-1` |
