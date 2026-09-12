# Packaging — Desktop Integration

Remin ships standard Linux desktop integration so it looks native in GNOME,
KDE, and friends.

## Launcher entry

`resources/remin.desktop`:

```ini
[Desktop Entry]
Type=Application
Name=Remin
GenericName=Terminal Workspace
Comment=Save, restore and carry your terminal workspace
Exec=remin gui
Icon=remin
Terminal=false
Categories=System;TerminalEmulator;Utility;
StartupWMClass=remin
Keywords=terminal;workspace;remind;save;restore;
```

Installed to `/usr/share/applications/`. `Exec=remin gui` matches the
[entry point](../getting-started.md).

## Icons

Themed scalable icons live in `resources/icons/hicolor/scalable/apps/`:

- `remin.svg` — application/window icon
- `remin-terminal.svg` — terminal tab marker
- `remin-note.svg` — note tab marker

`index.theme` registers the `hicolor` theme so GTK resolves them by name.
16px symbolic icons for buttons come from the system theme (see the UI
principles in [docs/design/ui-principles.md](../design/ui-principles.md)).

## AppStream metadata

Desktop store / software-center integration is provided by AppStream metainfo
(installed to `/usr/share/metainfo/remin.metainfo.xml`). It declares the app
ID (`com.remind.remin`), summary, screenshots (the ones in this repository's
docs), developer name, and release entries. Validate before release:

```bash
appstreamcli validate downstream/usr/share/metainfo/remin.metainfo.xml
```

## Startup

`StartupWMClass=remin` keeps window tiling/grouping correct on both Wayland and
X11; the app works on either windowing system.