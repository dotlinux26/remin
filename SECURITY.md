# Security

## Supported versions
| Version | Supported |
|---------|-----------|
| 1.0.0 (tag v1.0.0rc) | Yes |
| < 1.0.0 / pre-release | No |

## How to report a vulnerability
- **Do not open a public issue** for security vulnerabilities.
- Report privately via GitHub **Private vulnerability reporting** (Security tab →
  "Report a vulnerability"). Reports are handled in the private advisory.
- Terminal/ANSI parsing is delegated to VTE; upstream issues should be reported
  to the VTE tracker.

## What to include
- Remin version + installation method (deb / AppImage / source) + distro
- Minimal reproduction steps (commands, files, logs, screenshots)
- Estimated impact (privilege escalation, RCE, data leak, etc.)
- Proposed fix if available

## Expected response

Security reports are reviewed as soon as reasonably possible.
The reporter will be contacted through the private advisory if
additional information is required.

## Disclosure policy
- Coordinated disclosure: details are published after a fix is released.
- Reporter credited in release notes if desired.