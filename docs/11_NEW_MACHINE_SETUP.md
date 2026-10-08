# 11 — Starting from scratch on a new computer (AI runbook)

> 中文版：[zh_TW/11_NEW_MACHINE_SETUP.md](zh_TW/11_NEW_MACHINE_SETUP.md)

> What this page is for: **hand it to an AI**, and it can clone TOMS onto a new computer, install
> what is needed, build it and run it. The toolchain details are in
> [`02_INSTALL_WINDOWS.md`](02_INSTALL_WINDOWS.md); this page adds **what gets missed when you
> change computers**, especially the git settings (measured on this machine, not general rules).

---

## 0. In one sentence

**Easiest: double-click [`setup_new_pc.bat`](../setup_new_pc.bat)** (you can download just this one
file onto the new computer:
`https://raw.githubusercontent.com/WSLHermesAI/TOMS/main/setup_new_pc.bat`).
It uses winget to install whatever is missing of Git / Visual Studio 2026 (winget
`Microsoft.VisualStudio.Community`; the C++ workload, CMake tools, Windows SDK), turns on long paths,
clones, optionally makes an SSH key and walks you through adding it to GitHub, then builds and tests.
**Steps that need admin rights show a reminder dialog first**; when Windows asks "Do you want to
allow this app to make changes to your device?", answer **Yes**.
All admin work happens inside **one** UAC prompt; cloning and the key run as the normal user (to
avoid git's *dubious ownership* error).
Logs: `%TEMP%\TOMS_setup.log`, `%TEMP%\TOMS_setup_admin.log`.

On the way it asks about the **AI art tools (ComfyUI, can be skipped)**: (1) install ComfyUI here and
download the models (about 13 GB; needs an NVIDIA card with 8 GB+ VRAM), (2) use a ComfyUI server on
another machine (type its URL; it checks the connection and which models the server lacks), or
(3) skip. You can run `tools\setup_ai_art.cmd` again any time. The setting is stored in the user
environment variables `COMFYUI_URL` / `COMFYUI_SERVER` (AIGamestyle's scripts read the latter).
No admin rights needed.

By hand:

```bat
git clone --recurse-submodules git@github.com:WSLHermesAI/TOMS.git
cd TOMS
git config core.longpaths true
tools\check_env.cmd
tools\build.cmd windows-release
tools\test.cmd
```

Running `Build\dist\TOMS-windows\toms_game.exe` (or the one in `Build\windows-release\bin\`) means
it worked. You do **not** need Git LFS, symlink handling or worrying about case clashes; line endings
are already handled by `.gitattributes`.

---

## 1. Instructions for the AI (copy and paste as is)

Paste this whole block to the AI on the new computer:

```
You are to build TOMS on a brand-new Windows computer and report the result.

Project: git@github.com:WSLHermesAI/TOMS.git (without an SSH key, use https://github.com/WSLHermesAI/TOMS.git)
Reference docs: docs/02_INSTALL_WINDOWS.md (toolchain), docs/11_NEW_MACHINE_SETUP.md (this process), docs/05_TROUBLESHOOTING.md

Do these in order. Actually run every step and paste the real output; never write "should work" instead:

1. clone (with submodules)
   git clone --recurse-submodules <repo-url> TOMS
   → accept when: git -C TOMS submodule status prints something; git -C TOMS status -sb is clean (nothing Modified/Untracked)
2. git settings
   git -C TOMS config core.longpaths true
   → why: the build folders Build\...\_deps\... are deep; without long paths Windows fails during the build
3. environment check
   tools\check_env.cmd          (opens a dialog listing what is missing, with download links)
   → accept when: every required item is [ OK ]; typical gaps: Visual Studio 2026 + Desktop development with C++, CMake, Ninja, Git for Windows
4. configure + build (the first time downloads and compiles bgfx / SDL3 / Dear ImGui / glm; several minutes)
   tools\build.cmd windows-release
   → accept when: exit code 0, and Build\windows-release\bin\ contains toms_game.exe
5. tests
   tools\test.cmd                     (everything, about 45 seconds)
   tools\test.cmd windows-release -L unit   (unit tests only, under a second)
   → accept when: you paste ctest's pass/fail summary
6. run the game
   run Build\windows-release\bin\toms_game.exe (or Build\dist\TOMS-windows\toms_game.exe made by build_windows.bat)
   → accept when: a window opens and shows the title screen. If it fails, paste the full error; do not guess.

When reporting, keep these three apart; do not blur them:
  (a) the build succeeded (it compiled)  (b) the program runs (it really opens)  (c) player-visible behaviour (screen / input / saves) was checked
Say "not verified yet" for anything you did not do.
```

---

## 2. Git settings (the main point of this page)

### 2.1 Clone and the submodule

```bash
git clone --recurse-submodules git@github.com:WSLHermesAI/TOMS.git
# already cloned without the flag:
git submodule update --init --recursive
```

- `.gitmodules` has one submodule: `AIGamestyle` →
  `https://github.com/fatmingwang/AIGamestyle.git`.
- A plain clone does **not** download it; `AIGamestyle/` is then an **empty folder**.
- **It is not needed to build:** checked that `CMakeLists.txt`, `cmake/` and every
  `src/*/CMakeLists.txt` **never reference** it; it is not initialised on this machine either, and
  the engine builds fine.
  → `--recurse-submodules` is the safe choice, but leaving it out does not affect the build.

### 2.2 Things you do not need (measured; saves time)

| Item | Result | Basis |
|---|---|---|
| **Git LFS** | not needed | `.gitattributes` has no lfs rules; the largest tracked file is `src/third_party/miniaudio/miniaudio.h` (3.9 MB) |
| **symlink setup** (`core.symlinks`) | not needed | symlinks in the index = **0** |
| **case clashes** | none | case-insensitive duplicate paths = **0** (safe between Windows and Linux) |
| **line endings** | handled by `.gitattributes` | `*.cmd`/`*.bat`/`*.ps1` → `eol=crlf`; `*.sh` → `eol=lf` (so `.cmd` scripts survive a clone on any platform) |

> Note: do **not** set `core.autocrlf` to `false`/`input` expecting it to override
> `.gitattributes`: attributes win, not the other way round. Git for Windows' defaults are fine.

### 2.3 Recommended settings

```bash
git config --global core.longpaths true   # required on Windows; the build tree is deep
```

If needed:

```bash
git config --global user.name  "Your Name"
git config --global user.email "your@email"
```

### 2.4 Authentication (measured on this machine)

> How keys work, the three ways on another computer, the two separate `~/.ssh` of Windows and WSL,
> and troubleshooting: see [`12_SSH_KEY_SETUP.md`](12_SSH_KEY_SETUP.md).

**This machine uses an SSH key, not a password or a token:**

| Item | Measured |
|---|---|
| remote | `git@github.com:WSLHermesAI/TOMS.git` (SSH) |
| key | `~/.ssh/id_ed25519` (ED25519, mode `600`) + `~/.ssh/id_ed25519.pub` (`644`) |
| fingerprint | `SHA256:0AhSQKxzlEc4C+1+uP8z8wmeWOmh3SKnVAjubwNbFvg` |
| GitHub account | **WSLHermesAI** (`ssh -T git@github.com` answers `Hi WSLHermesAI!`) |
| credential helper | not set; `~/.git-credentials` does not exist → **no password or PAT is stored** |
| `~/.ssh/config` | none (default identity and host settings) |

How to check (**note**: GitHub provides no shell, so SSH exits non-zero even on success; read the
output text):

```bash
ssh -T git@github.com
# expect: "Hi WSLHermesAI! You've successfully authenticated, but GitHub does not provide shell access."
```

**Important: this repo is public** (an anonymous HTTPS read works). So on a new computer:

- **cloning needs no authentication at all** (anonymous HTTPS):
  `git clone --recurse-submodules https://github.com/WSLHermesAI/TOMS.git`
- **only pushing needs authentication.** Three ways:
  1. **Make a new key on that machine** (recommended; one key per machine):
     `ssh-keygen -t ed25519 -C "<your email>"`, paste the contents of `~/.ssh/id_ed25519.pub` into
     GitHub → Settings → SSH and GPG keys → New SSH key; use the SSH URL as the remote.
  2. **Copy the existing key** (fastest, but it shares one identity; the private key must be mode `600`).
  3. **HTTPS + Personal Access Token**: `git remote set-url origin https://github.com/WSLHermesAI/TOMS.git`,
     set `git config --global credential.helper manager` (Windows), then type the PAT as the
     password on the first push.

This machine's key authenticates without any interaction (`BatchMode`, no passphrase prompt), so
**treat `~/.ssh/id_ed25519` as a secret**: never commit it, paste it to an AI or a chat, or put it
in a shared folder.

### 2.5 Size and completeness (measured)

- `size-pack` is about **20.12 MiB** → cloning is fast (the 205 MB `.git` here is old loose objects,
  not what gets downloaded; `git gc` slims it).
- **479** tracked files; all **243 files under `assets/` are in git**. The only untracked one is
  `assets/wqy-zenhei.ttc`, and **no code references it** (only an old progress report mentions it).
  → **A fresh clone on a new computer has all the source and content needed to build and run.**

---

## 3. Toolchain (not in git; install separately)

| For | Needs | Notes |
|---|---|---|
| The game (required) | Windows 10/11 x64, Visual Studio 2026 + *Desktop development with C++*, CMake 3.24+, Ninja, Git for Windows | see `docs/02_INSTALL_WINDOWS.md` §1 |
| The editor `toms_editor` | **Qt 6.5+ (6.8 LTS recommended, MSVC 2022 64-bit kit)** | without Qt the game still builds, just not the editor |
| Web build | Emscripten SDK | see `docs/06_BUILD_WEB.md` |
| Android | JDK 17 + Android SDK/NDK 27 | see `docs/07_BUILD_ANDROID.md` |
| Renderer | Vulkan runtime or D3D11/12 | the default is fine; `--renderer=vulkan` needs driver support |

Disk: about **5 GB** free.

---

## 4. Build and run (Windows)

| Command | Output |
|---|---|
| `tools\check_env.cmd` | environment check (`-NoGui` for plain text) |
| `tools\build.cmd windows-release` | `Build\windows-release\bin\` (`toms_game.exe`) |
| `tools\build.cmd windows-debug` | the same, Debug |
| `build_windows.bat` | the shipping package `Build\dist\TOMS-windows\` (with `assets\` and the MSVC runtime; the whole folder can be copied anywhere) |
| `build_web.bat` / `publish_web.bat` | `Build\dist\TOMS-web\` / publish to GitHub Pages |
| `tools\test.cmd [preset] [-L unit]` | CTest (Visual Studio's Test Explorer shows the same tests) |

CMake preset names (`CMakePresets.json`):
`windows-debug`, `windows-release`, `windows-shipping`, `ci-windows`,
`web-debug`/`web-release` (Linux/WSL2), `web-*-windows` (Windows),
`android-debug`, `android-x86_64-debug`, `android-release`.

Opening the folder directly in Visual Studio works too: VS reads `CMakePresets.json`.

---

## 5. Acceptance checklist (what counts as "it works")

Check in order, and **say so when one fails**:

1. `git status` is clean (building never makes it dirty: all output is under the ignored `Build/`).
2. `tools\check_env.cmd`: every required item OK.
3. `tools\build.cmd windows-release` → `exit 0` and `Build\windows-release\bin\toms_game.exe` exists.
4. `tools\test.cmd` → CTest all green.
5. Run `toms_game.exe` → the title screen appears (it only counts once a **player can see** it).
6. Do something (click to move, open the inventory) → input works; it is not a still picture.

---

## 6. Common traps (including ones hit while measuring)

### Windows
- **Paths too long** → `core.longpaths true` (see 2.3). The build tree `Build\*\_deps\*` is deep.
- **Qt not found** → the editor is not built (everything else is). Check that the Qt kit is
  **MSVC 2022 64-bit**.
- **git not on PATH** → the first configure must clone bgfx/SDL3/ImGui/glm and fails at once.
- **The first configure needs github.com**; offline machines must build once on a connected
  machine first.

### Linux / WSL2 (no root)
- **`ccache` set as the compiler:** if `CMAKE_CXX_COMPILER` or the `CC`/`CXX` environment variables
  point at `ccache`, Qt's autogen fails (`ccache: invalid option -- 't'`). Set
  `-DCMAKE_CXX_COMPILER=/usr/bin/g++` explicitly.
- **The editor SIGSEGVs when run headless:** when bgfx picks Vulkan automatically and the system's
  Vulkan ICD list has an unsuitable driver (here `asahi_icd.json`), it crashes in
  `SwapChainVK::createSurface()`. Fix: `TOMS_RENDERER=opengl` (the editor's renderer choices:
  `auto|d3d11|d3d12|vulkan|opengl|gles`).
- **Installing dependencies without root:** `apt-get download` + `dpkg-deb -x ~/opt/x11root`, but
  **absolute symlinks inside a `.deb` break under a user prefix**, and Ubuntu 24.04 renamed some
  packages (`libasound2t64`, `pkgconf-bin`, `libsource-highlight4t64`, …). Check with
  `readlink -f <lib>` that it really resolves.
- **Generating shaders needs a host shaderc**; the desktop shader profiles branch on `WIN32`
  (Linux has no `s_5_0`, which would need shaderc's D3D4Linux support).

### Always
- **Never** commit `Build/` (builds, packages, AI art experiments), `*.deb` or `.spv`.
- This project has **no** LFS, no symlinks and no case clashes: when one of those seems to be the
  problem, suspect the environment first, not the repo.

---

## 7. Where the facts on this page come from

Every number here came from running the commands in `/home/fatming/Desktop/TOMS` (`main`, `7968c16`):
`git submodule status`, `git count-objects -vH`, `git ls-files` (479 files),
`git ls-files -s | awk '$1=="120000"'` (symlinks = 0),
a case-duplicate check (0), `git ls-tree -r -l HEAD` (largest 3.9 MB),
`git ls-files assets | wc -l` (243) against 244 on disk,
the preset names in `CMakePresets.json`, and the contents of `.gitattributes`.
