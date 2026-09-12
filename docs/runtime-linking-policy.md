# Remin Runtime & Linking Policy

**Status:** official policy for the 1.0 release series.
**Scope:** determines how official Remin binaries are linked and packaged,
and therefore what a user must have installed to run them.

---

## 1. Goal

> A user installs Remin and it runs. Maximum independence from any dynamic
> library that happens to be present on the system.

Achieving that does **not** mean static-linking the whole stack. For a
GTK/VTE desktop application the sane approach is **dynamic linking plus a
privately bundled runtime** for the parts Remin must control.

The single governing rule:

> Static-link the small, application-owned dependencies; bundle the runtime
> that needs to be reproducible; let the OS manage system integration; and
> never let the distro's VTE replace Remin's patched VTE.

## 2. The only irreplaceable dependency: Remin-patched VTE

Remin does not "use VTE 0.76". It uses:

```text
VTE 0.76.0
        +
Remin VTE patch series (0001..0004)
        =
Remin terminal backend (snapshot/restore API)
```

The snapshot/restore API does **not exist** in upstream VTE. If a package
declares a bare system dependency such as `Depends: libvte-2.91-gtk4-0`, the
package manager may hand Remin any ABI-compatible implementation - without the
snapshot API. That is an architectural bug, not a packaging detail.

**Invariant (verified in CI, never assumed):**

```text
OFFICIAL REMIN BINARY
        |
        +-- must link against --> Remin-patched VTE 0.76 (0001..0004)
```

Every official distribution ships its own private copy of the patched VTE.

## 3. Distribution matrix

| Component            | `.deb` (Debian/Ubuntu) | AppImage                  | macOS `.app` / `.dmg` |
|----------------------|------------------------|---------------------------|-----------------------|
| Remin executable     | dynamic                | dynamic                   | dynamic               |
| GTK 4.x              | distro dynamic         | bundled                   | bundled               |
| gtkmm 4.x            | distro dynamic         | bundled                   | bundled               |
| **VTE 0.76 patched** | **private bundled `.so`** | **bundled `.so`**      | **bundled `.dylib`**  |
| VTE patch series     | applied at build time  | applied at build time     | applied at build time |
| md4c                 | static (`.a`)          | static (`.a`)             | static (`.a`)         |
| Cairo / Pango        | distro dynamic         | bundled                   | bundled               |
| GLib / GObject / GIO | distro dynamic         | bundled                   | bundled               |
| GDK-Pixbuf / HarfBuzz / FreeType | distro dynamic | bundled if required | bundled if required |
| Wayland / X11        | host system            | host system               | -                     |
| Quartz               | -                      | -                         | system macOS          |
| libc                 | system                 | system                    | system                |
| kernel / GPU drivers | host                   | host                      | host                  |

Design rationale by column:

- **`.deb`** - use the distribution's own libraries for standard system
  integration (GTK/gtkmm/GLib/Cairo/Pango), because GTK is officially
  distributed and versioned by the distro. The **patched VTE is the single
  exception** and is installed privately.
- **AppImage** - bundle the complete application-level runtime (GTK stack +
  patched VTE) so "download and run" actually holds on any modern 64-bit
  Linux. Never bundle libc, the kernel, a compositor, an X server, or GPU
  drivers - those stay on the host.
- **macOS** - bundle all dylibs (GTK stack + patched VTE) inside the `.app`.
  Homebrew is a **build environment only**, never a runtime dependency.

## 4. `.deb`: hybrid packaging

Private bundled VTE, system everything else:

```text
/usr/bin/remin
/usr/lib/remin/libvte-2.91-gtk4.so.0        <- Remin-patched 0.76
/usr/share/remin/...
/usr/share/applications/remin.desktop
/usr/share/icons/hicolor/...
/usr/share/metainfo/remin.metainfo.xml
```

The binary resolves its private VTE via a relative RUNPATH, not an absolute
build path:

```text
RUNPATH: $ORIGIN/../lib/remin
```

- Debian Policy permits `/usr/lib/<package>` as the location for runtime/support
  files used by a single package - exactly this private-VTE model.
- RPATH is generally discouraged in Debian, but a **relative RUNPATH to a
  package-private directory** is the accepted mechanism for private libraries;
  hard-coded absolute build paths are forbidden.
- The package declares dependencies for the standard GTK stack normally. It
  must **not** declare a system `libvte` dependency that could shadow the
  patched copy.

### ABI baseline

Remin is built against a pinned baseline (currently gtkmm 4.10.0 / GTK 4.14.5
/ VTE 0.76 patched / md4c 0.4.8). Running on a distro with a different GTK may
work, but real ABI compatibility must be tested - never assumed. The release
test matrix:

```text
Debian stable
Ubuntu LTS
Fedora (stretch goal)
```

No blanket claim of "works on every Linux".

## 5. AppImage: fully bundled runtime

```text
Remin.AppImage
|-- AppRun
|-- usr/bin/remin
|-- usr/lib/
|   |-- libgtk-4.so
|   |-- libgtkmm-4.0.so
|   |-- libvte-2.91-gtk4.so        <- Remin-patched
|   |-- libglib-2.0.so
|   |-- libcairo.so
|   |-- libpango.so
|   '-- ...
'-- usr/share/...
```

Everything excluded above (libc, kernel, compositor, X/GPU stack) stays on
the host. The patched VTE is resolved from inside the image so a system VTE
never shadows it.

## 6. macOS: `.app` with bundled frameworks

```text
Remin.app/
'-- Contents/
    |-- MacOS/remin
    |-- Frameworks/
    |   |-- libgtk-4.1.dylib
    |   |-- libgtkmm-4.0.dylib
    |   |-- libvte-2.91-gtk4.dylib   <- Remin-patched
    |   |-- libglib-2.0.dylib
    |   |-- libcairo.dylib
    |   |-- libpango.dylib
    |   '-- ...
    '-- Resources/
        |-- icons/
        |-- styles/
        '-- Info.plist
```

- GTK4 has a native Quartz backend on macOS; distro-managed installs are not
  needed ([GTK on macOS](https://www.gtk.org/docs/installations/macos)).
- `gtk-mac-bundler` populates the bundle with the executable and its
  dependencies and rewrites installed library names to point inside the bundle.
- **Homebrew is a build environment**, not a runtime dependency. Users never
  run `brew install gtk4 gtkmm4 vte`.

### Signing and distribution

Apps distributed outside the Mac App Store must be signed with a Developer ID,
use the hardened runtime, and pass Apple notarization.

### Architecture

Ship separate builds first:

```text
Remin-1.0.0-macos-arm64.dmg
Remin-1.0.0-macos-x86_64.dmg
```

A universal2 build (every dylib in both architectures, incl. GTK stack and
patched VTE) complicates the pipeline significantly - revisit only after the
single-architecture pipeline is stable.

### VTE on macOS

The patched VTE is a build dependency that must be reproducible:

```text
VTE source (0.76.0)
    |-- apply patches/vte-0.76.0/*.patch
    |-- build for macOS
    |-- bundle patched libvte
    '-- build Remin against that exact VTE
```

"Developer machine already has a patched VTE" is never an acceptable premise.

## 7. Static linking policy

Static linking is used **selectively**, only for dependencies that are small,
application-owned, and not system integration:

- **md4c** - static. No reason for users to carry `libmd4c.so` for an
  internal parser; it is embedded as `libmd4c.a`.

Explicitly **not** static: GTK, GLib, Pango, Cairo, GDK-Pixbuf, HarfBuzz,
FreeType, VTE. Static-linking the full chain brings build/ABI/upgrade/debug
complexity **plus** LGPL obligations without gaining portability beyond what
bundling already provides (GTK/gtkmm are LGPL-2.1-or-later; VTE is
LGPL-3.0-or-later - bundling shared libraries is cleanly compliant, and any
artifact must satisfy the corresponding license conditions).

## 8. CI verification of the VTE invariant

`ldd` / `readelf` on the produced binary and libraries is a **verification
gate**, not an assumption:

```bash
ldd build/src/app/remin
readelf -d build/src/app/remin          # check RUNPATH: $ORIGIN/../lib/remin
strings libvte-2.91-gtk4.so | grep -i snapshot
```

plus a formal runtime/build probe that resolves and exercises the snapshot
symbols. If any gate fails, the release is not built from that tree.

## 9. README wording

The rendered policy for the README:

> ### Runtime dependency policy
>
> Remin relies on a modified VTE 0.76 runtime that provides the
> snapshot/restore API required for persistent terminal state.
>
> Official binary distributions therefore never rely on an arbitrary
> system VTE implementation.
>
> - Debian packages ship the Remin-patched VTE runtime privately.
> - AppImage bundles the complete GTK/VTE runtime.
> - macOS application bundles include the patched VTE dylib.
> - System Wayland/X11 integration remains provided by the host system.
> - Development dependencies may be installed through the platform's
>   package manager; they are not runtime requirements for official
>   self-contained releases.

## 10. One-sentence summary

> **Static-link the small, application-owned dependencies; bundle the runtime
> that needs reproducibility; let the OS manage system integration; and never
> let the distro's VTE replace Remin's patched VTE.**