# lightfight agent notes

- Real-life laser tag: ESP32 infrared blasters and worn IR hit sensors
  (`firmware/`), phones as the HUD (`ios/`), and a match server on the
  tailnet (`server/`). `docs/brief.md` is fixed; `docs/design.md` is the
  designer's (the first PR), kept in step with the code. Build nothing past
  the skeleton until the owner accepts the design.
- Safety first: eye-safe IR LEDs only, never a laser; LED current within the
  part's rating; protected batteries with a proper charger (`docs/brief.md`).
- Buying hardware and flashing boards are owner steps that need the owner's
  approval: put what you need in the PR and ask.
- No real names, addresses or player data in the repository, its issues or
  its CI logs. Fixtures use made-up players. Never write the tailnet's name:
  use `<tailnet>`.
- Deploying, tailnet nodes, node shares and runner registration are the
  owner session's work: put what you need in the PR and ask.
- Public, MIT. CI runs on GitHub: wait for it before merging.
