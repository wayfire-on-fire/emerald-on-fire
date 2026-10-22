# emeraldwf — multi-distro testing (QEMU/KVM full-GUI matrix)

Spec: [`specs/07-multi-distro-testing.md`](../../specs/07-multi-distro-testing.md).

Brings up **Arch / openSUSE Tumbleweed** as disposable QEMU/KVM VMs, installs each
distro's **packaged wayfire**, builds + installs **emeraldwf from source**
against it, and lands in a **visible wayfire+emeraldwf session** so you can run
[`e2e/CHECKLIST.md`](../CHECKLIST.md) **by eye**. Everything up to "a working
session is on screen" is scripted; the visual QA is manual.

> `emeraldwf` is a *plugin inside wayfire*, not a standalone WM. The per-distro
> procedure is always: install wayfire (≥0.11) → build emeraldwf against it →
> install the plugin/engines → enable `emeraldwf` in `core/plugins`. The plugin
> **must build from source on each VM** — the prebuilt `dist/` `.so`s are
> ABI-locked to the wayfire they were built against and do not port across
> distros.

## Layout

```
e2e/distros/
├── distros.conf            # name / image_url / sha256 / disk_gb / session_user / wf_source
├── cloud-init/<distro>/    # NoCloud seed: user-data (__PUBKEY__ placeholder) + meta-data
├── provision.sh            # guest-side: deps → wayfire → install.sh → (WPM) → session → (e2e)
├── run-kvm.sh              # host-side: image → overlay → seed → QEMU → ssh → provision
├── matrix.sh               # all distros in parallel + summary table
├── images/                 # cached base cloud images (gitignored)
├── run/                    # per-run overlays, seed ISOs, serial/qemu logs (gitignored)
└── results/                # per-run <distro>.json + SUMMARY.md (gitignored)
```

## Host prerequisites (one-time)

On this Gentoo host:

```sh
sudo emerge sys-emulators/qemu     # enable: kvm, and a display backend (gtk or sdl)
sudo emerge app-cdr/cdrtools       # mkisofs — builds the NoCloud seed ISO
```

- `/dev/kvm` must exist **and** your user must be in the `kvm` group — otherwise
  QEMU's `-enable-kvm` dies with `Could not access KVM kernel module: Permission
  denied`. `/dev/kvm` is `root:kvm` mode `660`, so non-root users need the group:
  ```sh
  sudo usermod -aG kvm "$USER"    # then log out/in, or `newgrp kvm`, to apply
  ```
  Verify with `id` (should list `kvm`).
- **Seed-ISO tool:** `run-kvm.sh` builds the cloud-init NoCloud seed ISO with
  `genisoimage`/`mkisofs` (`mkisofs` from `app-cdr/cdrtools` — `cdrkit` is no
  longer in the tree) when present, else `xorriso`. Caveat: some
  `dev-libs/libisoburn` builds (which ship `xorriso`) link libisofs but are
  compiled **without** the `-as iso9660` frontend — `xorriso` "exists" yet
  can't create an ISO. `mkisofs` avoids that entirely.
- A **local display** is required for the `-display gtk` window (the by-eye part).
  If you drive the host over a remote session, change `-display gtk` in
  `run-kvm.sh` to `-vnc :N` and connect with a VNC client.
- An SSH keypair for the host is auto-created on first run if none exists
  (`~/.ssh/id_ed25519`).

## Pinning the cloud images

`distros.conf` lists a **release-specific** `image_url` + `sha256` per distro
(currently pinned to an Arch `latest` cloudimg and an openSUSE Tumbleweed `-Cloud`
qcow2 — both verified HTTP 200 + SHA on 2026-10-20). Only distros that ship
wayfire ≥ 0.11 in their main repos are targeted (the plugin builds against the
0.11 scene-graph API). Notes:

- **Arch** uses the stable unversioned `…/images/latest/Arch-Linux-x86_64-cloudimg.qcow2`
  (arch-boxes); the SHA is from the mirror's `.SHA256`.
- **openSUSE Tumbleweed** uses the stable `…/tumbleweed/appliances/openSUSE-Tumbleweed-Minimal-VM.x86_64-Cloud.qcow2`
  (the NoCloud variant); the SHA is from its `.sha256` file.

## Usage

```sh
# One distro (canary):
./run-kvm.sh arch                   # boot + provision, leave running
./run-kvm.sh opensuse --wpm         # also validate the WPM install path
./run-kvm.sh arch --headless-e2e    # also run the in-guest headless e2e

# Both at once:
./matrix.sh                          # parallel bring-up + summary table
./matrix.sh --wpm --headless-e2e
./matrix.sh --summary                # re-print the table
./matrix.sh --teardown               # destroy all

# Per-VM:
./run-kvm.sh status
./run-kvm.sh destroy opensuse
```

After a bring-up, a QEMU window titled `emeraldwf-<distro>` shows the VM's
display. Provisioning runs over SSH (a few minutes: package install +
`./install.sh` build). When it finishes, the VM **autologins into a wayfire
session with `emeraldwf` enabled** — that is the state to QA.

## The QA loop (by eye)

With the session on screen, work through [`e2e/CHECKLIST.md`](../CHECKLIST.md):
SSD decoration (active/inactive/maximized), buttons + titlebar actions, resize,
CSD (no frame / `always_decorate`), shade, live reload, and the themer. The
structural assertions (plugin loads, window maps, close works, CSD frameless,
vendor SHA) are also covered by the optional in-guest headless `./e2e/run.sh`
(`--headless-e2e`) if you want a machine PASS/FAIL under the visual QA.

## Results

Each run writes `results/<distro>.json`:
`{distro, wayfire_version, wayfire_source: pkg|source, install_sh,
wpm, headless_e2e, session_user, timestamp}`. `matrix.sh --summary` renders:

| distro | wayfire | install.sh | wpm | headless_e2e | manual-checklist |
|--------|---------|-----------|-----|-------------|------------------|
| arch     | x.y.z (pkg) | pass | pass | pass | (by eye) |
| opensuse | x.y.z (pkg) | pass | …    | …    | (by eye) |

`install_sh` is `pass`, `pass_no_tests` (gtest packaging quirk, e.g. openSUSE —
the install still lands; gtest is CI-covered), or `fail`. `wayfire_source` is
`source` only when the distro's packaged wayfire was < 0.11 and the ci.yml
from-source recipe was used.

## Troubleshooting

- **SSH never comes up** → read `run/<distro>/serial.log` (the console). Common
  causes: wrong `image_url` (404 / not a cloud image), KVM not enabled.
- **Guest dracut "UUID does not exist" / emergency mode** → the qcow2
  overlay's virtual size must match the base image's (a smaller overlay
  confuses the kernel's partition reader). `run-kvm.sh` queries the base's
  actual size via `qemu-img info` and creates the overlay to match. If you
  create the overlay manually, use the base's virtual size (not an arbitrary
  `disk_gb`).
- **Session doesn't appear in the QEMU window** → the autologin unit
  (`emeraldwf-session.service`) runs wayfire as the session user on the virtual
  GPU. Check `journalctl -u emeraldwf-session` in-guest; if the distro's
  wayfire-as-service has quirks, the fallback is a full display manager.
- **The host's own window manager obscures the QEMU window** (e.g. the host is
  itself a wayfire/LXQt desktop showing a window switcher) → capture the *guest*
  framebuffer directly from the QEMU monitor instead of screenshotting the host:
  `python3 -c 'import socket,time; s=socket.socket(socket.AF_UNIX,
  socket.SOCK_STREAM); s.connect("run/<distro>/mon.sock"); time.sleep(0.3);
  s.recv(1024); s.sendall(b"screendump /tmp/g.ppm\n"); time.sleep(2);
  s.recv(4096); s.close()'` then `magick /tmp/g.ppm out.png`. This bypasses the
  host display entirely.
- **`install.sh` FATALs on a non-writable system dir** → the VM runs as root,
  so this shouldn't happen; if it does, the distro's wayfire package didn't
  create `$(datadir)/wayfire/metadata`.
- **Golden-PNG mismatches in the headless e2e** → expected cross-distro (font
  rendering differs); the structural steps are the signal, not the pixel goldens.
- **Stale VM / port in use** → `./run-kvm.sh destroy <distro>` (deletes the
  overlay; the base image in `images/` is kept).

## Out of scope

Real 3D GPU (virtual display only — software/raster path); Fedora/Debian/Ubuntu
(no wayfire 0.11 in their main repos), Alpine/musl, NixOS (0.10), and **Gentoo**
(ships wayfire 0.11 but with no binaries — the full LLVM/clang/mesa/wlroots/wayfire
chain must compile from source in-VM, ~3–4 h on a 4 vCPU/4G VM with OOM risk in
the LLVM build; not viable for a practical e2e harness). The full-GUI path (a
*visible* session) is **local-only** — CI
(`.github/workflows/ci.yml`) covers **compilation only** (per-distro
`meson setup` + `ninja`, no session). See the spec's
"Out of scope / known gotchas".
