# ios

The phone HUD: health, ammo, score and match state, paired with the
player's gear.

Placeholder. Nothing is built here until the owner accepts the design
(`docs/design.md`). Whether Android is supported too is an open question in
[docs/brief.md](../docs/brief.md).

## Package

`Package.swift` holds `LightfightProtocol`, the BLE and IR wire formats, with
no UI and no CoreBluetooth. `swift test` runs its tests against the shared
vectors in `../protocol/`; `scripts/verify` runs it where Swift is installed
(on Linux: `docker run --rm -v "$PWD":/w -w /w/ios swift:6.0 swift test`).
