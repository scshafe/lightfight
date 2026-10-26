# lightfight

Real-life laser tag: small ESP32 infrared blasters and worn IR hit sensors,
players' phones as the HUD, and a game server on the tailnet that runs the
matches. Infrared only, eye-safe, no lasers.

- Brief: [docs/brief.md](docs/brief.md). The design (`docs/design.md`) is the
  project agent's first PR.
- [firmware/](firmware/): the gear (blaster and hit sensors).
- [ios/](ios/): the phone HUD.
- [server/](server/): the match server.

## Verify

```sh
./scripts/verify      # the repository tests (standard library unittest)
```

## License

MIT (`LICENSE`).
