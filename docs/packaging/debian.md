# Packaging — Debian / Ubuntu

A `.deb` is built from the CMake install rules plus the packaging helper
scripts. Installed layout (hybrid packaging per the
[Runtime & Linking Policy](../runtime-linking-policy.md)):

```text
/usr/bin/remin
/usr/lib/remin/libvte-2.91-gtk4.so.0      PRIVATE Remin-patched VTE
/usr/share/remin/resources/               styles, schemas, logo
/usr/share/applications/remin.desktop
/usr/share/icons/hicolor/index.theme
/usr/share/icons/hicolor/scalable/apps/remin.svg
/usr/share/icons/hicolor/scalable/apps/remin-terminal.svg
/usr/share/icons/hicolor/scalable/apps/remin-note.svg
/usr/share/metainfo/remin.metainfo.xml
```

## Dependency model

The patched VTE is a **private runtime**. The binary resolves it through a
relative RUNPATH (`$ORIGIN/../lib/remin`), not through the system loader, so a
distro `libvte-2.91-gtk4.so` can never shadow it. The package must not declare
a system `libvte` dependency.

Other stack pieces (GTK/gtkmm/GLib/Cairo/Pango, gtksourceview, adwaita) come
from the distro as ordinary declared dependencies:

```text
libgtkmm-4.0, libgtksourceview-5, libadwaita-1, librsvg2,
libglib2.0, pango, openssl
```

md4c is statically linked into Remin (see the policy).

## Building the package

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --install build --prefix build/stage/usr
dpkg-deb --build build/stage remin_1.0.0_amd64.deb
```

Final artifact: `remin_1.0.0_amd64.deb` with `control`, `rules`, and
post-install scripts kept in this repository (packaging conventions follow
Debian Policy).

## Local install alternative

```bash
cmake --install build --prefix /usr/local
```

This installs the same binary + desktop + icons without producing a package.