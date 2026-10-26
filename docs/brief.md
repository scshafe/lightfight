# lightfight: the owner's brief

Real-life laser tag. Players wear small infrared gear, carry an infrared
blaster, and use their phone as the HUD. A game server on the tailnet runs
the matches.

## Owner decisions (2026-10-26)

- **Hit detection: infrared gear.** Small ESP32 IR blasters and worn IR hit
  sensors, the way commercial laser tag works. Infrared only: no visible
  lasers.
- **Phones are the HUD.** Each player's phone shows health, ammo, score and
  match state, and pairs with that player's gear.
- **Server.** A game server on the tailnet runs matches. It is deployed
  later through the runner lane on the PC.
- **Repository:** public, MIT licence, named lightfight.

## Goals

- **Casual matches first:** friends and family, in a house or a yard.
- **Hits must be reliable.** A hit that lands registers, once, on the right
  player; a miss never registers. Reliability comes before features.

## Safety rules

- **Eye-safe IR LEDs only.** No lasers of any kind, visible or infrared.
  Emitters stay within eye-safe limits at any distance, including point
  blank.
- **Sensible current limits.** LED drive current stays within the part's
  rating, with a series resistor or a constant-current driver, and with a
  pulsed duty cycle that keeps the emitter cool.
- **Battery safety.** Use protected cells or packs with a protection
  circuit (over-charge, over-discharge, short circuit), charge with a proper
  charger IC, fuse or current-limit the pack, and keep cells out of places
  where they can be crushed or punctured when a player falls.

## Rules

- **Owner steps.** Buying hardware and flashing boards are owner steps that
  need the owner's approval. Put what you need in the PR and ask.
- **No player data in git.** No real names, addresses or player data go in
  the repository, its issues or its CI logs. Test fixtures use made-up
  players.
- **Never write the tailnet's name.** Use `<tailnet>` (for example
  `lightfight.<tailnet>.ts.net`).

## Open questions for the design

The designer lists a recommendation for each in the design's "Owner
decisions" section; the owner decides.

1. **Phone platforms:** iOS only (the owner has an iPhone 17 Pro Max), or
   Android too.
2. **Phone-to-gear link:** BLE is proposed. Pairing, reconnection and
   latency.
3. **IR protocol:** compatible with an existing one such as MilesTag II, or
   custom. Encoding, player and team ids, damage, error detection.
4. **How sensors are worn:** a vest, a headband, or both. How many sensors
   and where.
5. **Game modes for v1.**
6. **Offline play** when there is no server or no tailnet: can a match run
   phone-to-phone or gear-only?
7. **Players outside the tailnet:** guests whose phones are not on it.
8. **Hardware bill of materials** and a per-player budget.
9. **Firmware toolchain:** ESP-IDF or Arduino/PlatformIO.
10. **Server stack:** language, storage, and what the server owns versus
    what the gear and phones decide.
11. **Milestones.**

## The designer's job (first PR)

1. Write `docs/design.md`: the gear (blaster and sensors), the IR protocol,
   the phone-to-gear link, the phone app, the server and its deployment on
   the runner lane, how a hit travels from emitter to scoreboard, how hits
   stay reliable, safety, and milestones. List every decision the owner must
   make in an "Owner decisions" section. Don't decide those yourself.
2. Keep `scripts/verify` and `dev check .` passing.
3. Build nothing past the skeleton until the owner accepts the design.
