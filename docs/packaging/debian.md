# Packaging — Debian / Ubuntu

A `.deb` is built from the CMake install rules plus the packaging helper
scripts. Installed layout:

```text
/usr/bin/remin
/usr/share/remin/resources/           styles, schemas, logo
/usr/share/applications/remin.desktop
/usr/share/icons/hicolor/index.theme
/usr/share/icons/hicolor/scalable/apps/remin.svg
/usr/share/icons/hicolor/scalable/apps/remin-terminal.svg
/usr/share/icons/hicolor/scalable/apps/remin-note.svg
```

## Dependencies

Declared as Debian package dependencies so `apt` resolves them:

```text
libgtkmm-4.0, libvte-2.91 (patched), libgtksourceview-5,
libadwaita-1, librsvg2, libmd4c, libglib2.0, pango, openssl
```

If the distro ships an unpatched system VTE, the package depends on the Remin
patched VTE (see the `--with-own-vte` convention below); each runtime must then
run with `LD_LIBRARY_PATH` pointing at the bundled libvteterminal.

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