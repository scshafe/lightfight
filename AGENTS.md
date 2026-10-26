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

<!-- scshafe-dev:begin landing -->
## Verify and landing

Managed by scshafe-dev: `dev adopt` and `dev update` refresh this section from `dev.toml`; change `dev.toml`, not these lines.

Before finishing, both of these must pass:

```sh
./scripts/verify
dev check .
```

How a change lands:

1. Work on a branch and open a PR.
2. Run the two commands above. If the repository is private, GitHub Actions does not run for it: verify locally and say in the PR what you ran. If it is public, wait for CI to be green.
3. Merge your own PR with a merge commit, one change at a time: `gh pr merge <N> --merge --subject "Merge #<N>: <title>"`. Never squash or rebase (both are off on the repository), and pass `--subject`: `gh pr merge` does not make the `Merge #N: <title>` subject by itself.

The project's agent may merge its own PR and push `main`; there is no approval gate.
<!-- scshafe-dev:end landing -->
