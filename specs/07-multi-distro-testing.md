# Part 07 — Multi-distro testing (QEMU/KVM full-GUI matrix)

**Read `specs/00-overview.md` first.** This part extends the project's
testing beyond the single reference toolchain (Ubuntu 24.04, wayfire from
source — the existing `Dockerfile` + `ci.yml`) to a **matrix of real
distros**, each in its own VM, verified by a **full real-GUI session**.
It reuses the existing `install.sh`, `e2e/run.sh`, and `wayfire-plugin.json`
(WPM) rather than reinventing them.

## Goal

Bring up **Fedora**, **Arch**, and **Debian** as disposable QEMU/KVM VMs;
on each, install the **distro's packaged wayfire**, build + install
`emeraldwf` **from source against that wayfire**, and land in a **visible
wayfire+emeraldwf session** so the `e2e/CHECKLIST.md` can be run **by eye**.
Everything up to "a working session is on screen" is scripted; the visual
QA is manual. A results table records, per distro: wayfire version,
`install.sh`, WPM, headless-e2e, and manual-checklist outcomes.

## 1. What varies per distro (the mental model)

`emeraldwf` is a **plugin inside wayfire**, not a standalone WM. The
per-distro procedure is always: *install wayfire (≥0.11) → build emeraldwf
against it → install the plugin/engines → enable `emeraldwf` in
`core/plugins`.* No separate "emerald WM" is installed.

| Variable | Why it matters |
|---|---|
| **wayfire version** (the big one) | Plugin needs the ≥0.11 scene API. Fedora/Arch/Debian *package* it at different versions; a distro shipping <0.11 must build wayfire from source (as `ci.yml` does for Ubuntu) — record which. |
| GTK3 / cairo / pango / glib / gdk-pixbuf | The plugin + vendored emerald engines link against these. |
| Official package (rpm/deb) install path | Only exercised by a real OS, not a container. |
| Fontconfig / fonts | Affects golden-PNG screenshots (headless e2e only; by-eye QA is unaffected). |

**Hard constraint:** the plugin must **build from source on each VM, against
that distro's wayfire.** The prebuilt `dist/` `.so`s are ABI-locked to the
wayfire they were compiled against and are **not portable** across distros.

## 2. Global decisions (pinned)

| # | Decision |
|---|----------|
| T1 | Hypervisor: **QEMU/KVM** (`-enable-kvm`). The host has `/dev/kvm`. VirtualBox is *not* used (slower, emulated); QEMU is near-native and fully scriptable. |
| T2 | Target distros (VMs): **Fedora, Arch, Debian**. **Gentoo** is the local dev box (tested in-place, no VM). **Ubuntu 24.04** is already covered by the existing `Dockerfile`/`ci.yml` (wayfire from source) — no new work. |
| T3 | Test depth: **full real-GUI QA** — a real OS, real display/seat, a visible wayfire+emeraldwf session, `e2e/CHECKLIST.md` run **by eye**. |
| T4 | Install: **from source on each VM**. Primary = `./install.sh` (builds + gtest + atomic install + seeds `~/.emerald`). Secondary (validated as a data point) = **WPM** `wayfire-plugin install` (the canonical wayfire plugin path; self-contained user prefix, `-Dtests=false`, no root). |
| T5 | Automation: **local scripts only** — no CI. A human runs `matrix.sh` and does the by-eye QA. |
| T6 | Source delivery to the VM: **scp a tarball (or `git bundle`) of the current tree over SSH** after cloud-init brings up SSH. Always tests the *current* working tree, not a published snapshot. |
| T7 | VMs are **disposable**: each run uses a qcow2 **overlay** on a cached base cloud image; `destroy` deletes the overlay. |
| T8 | The visible session is reached via a **systemd autologin unit** that execs `wayfire -c ~/.config/wayfire.ini` on tty1 (replacing getty@tty1) — distro-agnostic (systemd), avoids per-distro display-manager packaging differences. |

## 3. Install strategy (T4)

Both paths compile from source against the local wayfire (meson resolves
`dependency('wayfire', >=0.11)` via the distro's `pkg-config`):

- **`./install.sh` (primary, every VM):** the project's blessed installer —
  configure (`-Dprefix`/`-Dlibdir` from the distro's `wayfire.pc`), build,
  **run the gtest suite**, atomic `.so` placement, seed `~/.emerald`, install
  WCM metadata/icon + themer. Validates the installer itself per distro and
  yields the gtest signal. Needs the source tree + root (both present in the VM).
- **WPM `wayfire-plugin install <url-or-path>` (secondary, recorded per distro):**
  the "wayfire way." WPM fetches source, runs the manifest recipe
  (`meson setup … --prefix <managed> -Dtests=false`), compiles, and installs
  into `~/.local/share/wayfire/plugin-manager/install` (self-contained: plugin
  + 7 engines + `libemeraldengine` + WCM metadata; engine dir matches the
  runtime dlopen path). No root, no gtest. **Caveat:** WPM is designed to fetch
  a URL; to test the *current unpushed tree*, verify the local WPM build accepts
  a **local path** (`wayfire-plugin install /path/to/tree`), else push a branch
  and point WPM at it — or use WPM only against the published URL as a
  "does the canonical path work here?" check.

`provision.sh` runs `install.sh` **always**, and WPM **only** under a
`--wpm` flag; each result is a separate matrix column.

## 4. Deliverables

All new files live under `e2e/distros/` (the existing `e2e/` keeps
`run.sh`/`wayfire.ini`/`golden/`/`CHECKLIST.md` untouched):

| File | Content |
|------|---------|
| `e2e/distros/distros.conf` | One row per distro: name, cloud-image URL + **SHA256**, disk size, default user, wayfire source (`pkg`/`source`), notes. URLs/SHAs are pinned per current release (they change; re-pin on bump). |
| `e2e/distros/cloud-init/<distro>/user-data` | NoCloud `#cloud-config`: hostname, SSH authorized key, root access enabled. Kept minimal — all provisioning logic lives in `provision.sh`. (`meta-data` is a one-line instance-id file.) |
| `e2e/distros/provision.sh` | Guest-side, distro-aware (`case` on `/etc/os-release` `ID`): install wayfire + build deps + e2e deps (per-distro list, §5) → record `wayfire --version` → run `./install.sh` (and WPM if `--wpm`) → write `~/.config/wayfire.ini` (`[core] plugins = emeraldwf`, drop `decoration`) → install the autologin systemd unit (§6) → (optional) run headless `./e2e/run.sh` → write `/root/distros-result.json`. |
| `e2e/distros/run-kvm.sh` | Host-side per-distro driver: fetch+cache base image (verify SHA) → create overlay qcow2 → build NoCloud seed ISO → launch QEMU (KVM, `-display gtk`, hostforwarded SSH) → poll SSH → scp tree + `provision.sh` → run it (streamed) → read result JSON → **leave the VM up** for by-eye QA. Subcommand `destroy <distro>` shuts down + deletes the overlay. |
| `e2e/distros/matrix.sh` | Loops `distros.conf`, calls `run-kvm.sh <distro>` for each (each left running), then prints the summary table (§7) + a "watch the N windows, run the CHECKLIST" hint. `--teardown` destroys all. |
| `e2e/distros/README.md` | Howto: host prereqs, `matrix.sh` usage, per-distro wayfire-version expectations, the by-eye QA loop, teardown. |
| `e2e/distros/results/` | Per-run `distros-result.json` + an aggregate `SUMMARY.md` (distro × wayfire-version × install.sh × wpm × headless-e2e × manual-checklist). |

## 5. Per-distro provisioning (`provision.sh` package lists)

Wayfire **from the distro's packages** where ≥0.11; build deps + e2e deps so
`install.sh` (meson/ninja/gtest) and the optional headless `e2e/run.sh`
(`xvfb`, `x11-apps`/`xterm`, `xdotool`, `x11-utils`/`xwininfo`, `weston`
→ `weston-terminal`, `imagemagick`) both work. Record `wayfire --version`
first; if the packaged wayfire is **<0.11**, fall back to the from-source
recipe in `ci.yml` (wayland 1.24 / wayland-protocols 1.47 / libdrm /
xkbcommon / pixman / wayfire v0.11.0) and mark the row `wayfire=source`.

- **Fedora** (`dnf`): `wayfire git meson ninja-build pkgconf-pkg-config
  gtk3-devel gdk-pixbuf-devel cairo-devel glib2-devel libX11-devel
  pango-devel libgtest-devel xorg-x11-server-Xvfb xorg-x11-apps xdotool
  xorg-x11-utils weston ImageMagick`
- **Arch** (`pacman -S --noconfirm`): `wayfire git meson ninja pkgconf gtk3
  gdk-pixbuf2 cairo glib2 libx11 pango gtest xorg-server xorg-apps xdotool
  xorg-util-image weston imagemagick`
- **Debian** (`apt-get install -y`): `wayfire git meson ninja-build
  pkg-config libgtk-3-dev libgdk-pixbuf-2.0-dev libcairo2-dev libglib2.0-dev
  libx11-dev libpango1.0-dev libgtest-dev xvfb x11-apps xdotool x11-utils
  weston imagemagick`

Default cloud users differ (Fedora `fedora`, Debian `admin`, Arch `arch`) —
the SSH step and the autologin unit target the right user; root is used for
`install.sh`.

## 6. Reaching the visible session (T8)

`provision.sh` installs a systemd unit, distro-agnostic:

```
# /etc/systemd/system/emeraldwf-session.service
[Unit]
Description=emeraldwf wayfire session (autologin, tty1)
After=systemd-logind.service

[Service]
User=<test-user>
Environment=HOME=/home/<test-user>
ExecStart=/usr/bin/wayfire -c /home/<test-user>/.config/wayfire.ini
Restart=no

[Install]
WantedBy=multi-user.target
```

and disables `getty@tty1` (`systemctl disable getty@tty1 && systemctl
enable emeraldwf-session`). After the VM's display comes up, the guest lands
directly in a wayfire session with `emeraldwf` active — the state the by-eye
CHECKLIST expects. (A full display manager is intentionally *not* used: it
adds per-distro packaging variance with no testing benefit for a plugin.)

## 7. Results / reporting

Each run writes `e2e/distros/results/<distro>.json`:
`{distro, wayfire_version, wayfire_source: pkg|source, install_sh:
pass|fail, wpm: pass|fail|skipped, headless_e2e: pass|fail|skipped,
errors: [...]}`. `matrix.sh` aggregates into `SUMMARY.md`:

| distro | wayfire (ver, pkg/src) | install.sh | WPM | headless e2e | manual checklist |
|--------|------------------------|-----------|-----|-------------|------------------|
| Fedora | x.y.z (pkg) | pass | pass | pass | pass |
| Arch   | x.y.z (pkg) | pass | …   | …           | …                |
| Debian | x.y.z (pkg/src) | …  | …   | …           | …                |

The **manual checklist** column is filled by hand after the by-eye QA
(`e2e/CHECKLIST.md`: frames, buttons, resize, shade, CSD, live-reload, themer).

## 8. QA model (automated vs by-eye)

- **Automated (scripts):** boot → install distro wayfire → build+install
  emeraldwf from source → enable → autologin to a visible session. Optional
  structural signal: headless `./e2e/run.sh` over SSH (plugin loads, window
  maps, close works, CSD frameless, vendor SHA) — machine PASS/FAIL so the
  eyes only judge the visual/feel items.
- **By eye (human):** `e2e/CHECKLIST.md` in the QEMU `-display gtk` window.
  This is the oracle for "does it look/feel right."

## 9. Host prerequisites (one-time, on Gentoo)

- `emerge sys-emulators/qemu` (enable `kvm` + a display backend: `gtk` or `sdl`)
  — `qemu-system-x86_64` is currently **absent**.
- `emerge app-cds/xorriso` (or `genisoimage`) to build the NoCloud seed ISO
  (`cloud-localds` is absent). Seed recipe: a dir with `user-data`+`meta-data`,
  then `xorriso -as iso9660 -use -iso_level 1 -full_iso9660 -volid cidata
  user-data meta-data -o seed.iso` (label **must** be `cidata`).
- Verify KVM accel actually engages (no "KVM not available" fallback): a
  trivial `-enable-kvm -display none` boot.
- (Optional, native e2e on Gentoo) install the missing e2e deps: `xvfb`,
  `xdotool`, `xorg-apps` (xwininfo), `weston` (weston-terminal),
  `media-gfx/imagemagick`.

## 10. Phases / execution order

1. **Host prep:** emerge `qemu` + `xorriso`; confirm KVM accel works.
2. **Canary — one distro end-to-end** (pick **Debian** or **Fedora**):
   `distros.conf` (1 row) + `cloud-init/<distro>/` + `provision.sh` (that
   branch) + `run-kvm.sh`. Boot it; confirm full OS up, distro wayfire
   installed, `install.sh` green, autologin lands in a **visible**
   wayfire+emeraldwf session. Run the CHECKLIST by eye. *De-risks the whole
   pipeline before generalizing.*
3. **Generalize to Fedora + Arch + Debian:** add the other `provision.sh`
   branches + `distros.conf` rows; `matrix.sh` brings up all three; QA each by
   eye; record the summary table.
4. **(Optional) reduce the by-eye load:** enable the in-guest headless
   `e2e/run.sh` (structural PASS/FAIL) and/or a guest ydotool recipe to click
   through the CHECKLIST.
5. **(Optional) fast daily check on Gentoo:** install the native e2e deps and
   run `./e2e/run.sh` locally (no VM) between full-GUI sessions.

## 11. Tests / verification

- Canary VM boots to a **visible** wayfire+emeraldwf session (Phase 2 gate).
- `install.sh` succeeds on each target distro (build + gtest + install + seed).
- WPM install succeeds (secondary) on each/one distro; recorded per row.
- Headless `e2e/run.sh` (when enabled) passes its structural steps in-guest.
- `e2e/CHECKLIST.md` passes **by eye** on each target distro.
- `destroy` fully cleans up (no leftover overlay / process / port).

## 12. Acceptance criteria

1. A script brings up a Fedora/Arch/Debian VM, installs the **distro's**
   wayfire, builds + installs `emeraldwf` **from source**, and lands in a
   **visible** wayfire+emeraldwf session with no manual steps beyond QA.
2. The `e2e/CHECKLIST.md` passes by eye on each target distro.
3. `e2e/distros/results/SUMMARY.md` records distro × wayfire-version ×
   install.sh × WPM × headless-e2e × manual-checklist.
4. VMs are disposable (overlay per run; `destroy` is clean and idempotent).

## 13. Out of scope / known gotchas

- **Real 3D GPU** — the VM uses a virtual display device (`virtio-gpu`/`qxl`);
  wayfire runs the software/raster path. We test the full *OS* (systemd, seat,
  login, fontconfig, official package), not a real GPU.
- **Prebuilt `dist/` portability** — ABI-locked; always build from source (T-constraint).
- **CI** — local scripts only (T5); no workflow changes.
- **Alpine (musl), NixOS, openSUSE** — not in the core matrix; optional later
  (Alpine expects extra GTK3/vendored-C friction on musl).
- **ydotool host calibration does not transfer** — the guest input stack
  (QEMU virtio-input + guest libinput) has a different scaling factor than the
  host's "÷2.05"; re-measure in-guest if the automated-clicks route is taken.
- **Golden-PNG cross-distro font diffs** — relevant only to the headless
  `e2e/run.sh` (fontconfig/hinting differ); by-eye QA is unaffected. Treat
  goldens as a reference-distro regression baseline, not a hard cross-distro gate.
- **Packaged wayfire < 0.11** — build from source (the `ci.yml` recipe) and
  record `wayfire=source`; a <0.11 package is a *finding*, not a failure.
- **Cloud image URLs/SHAs are release-specific** — pin in `distros.conf`,
  re-pin on release bump (Arch is rolling/dated).
