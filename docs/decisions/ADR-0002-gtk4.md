# ADR-0002: GTK4

## Decision
Use GTK4 for the GUI, via the official gtkmm C++ bindings.

## Reason
GTK4 is the current GTK direction; gtkmm-4.0 provides a first-class C++ API.

## Rejected
- GTK3 — legacy.
- Qt / SDL / Electron — not the Linux-native GNOME ecosystem stack.
