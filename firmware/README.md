# firmware

The gear's firmware: the ESP32-C3 blaster and the vest hub (ESP-IDF, design
decision 9).

- `lib/`: the portable C library, no ESP-IDF includes: IR codec (`lf_ir`),
  rules engine (`lf_rules`), emitter safety gate (`lf_emitter`), receive path
  (`lf_rx`), the hub core (`lf_hub`), log format (`lf_log`), BLE messages
  and event queue (`lf_ble`). Tested on the host: `make test`.
- `tests/`: the host tests; they read the shared vectors in `../protocol/`.
- `idf/`: the ESP-IDF project. `main/` holds only hardware glue (RMT driver,
  GPIO, tasks); the roles are `sdkconfig.<role>` overlays (`blaster`,
  `blaster-bench`, `hub`).
- `scripts/idf-build.sh`: compiles the roles in the `espressif/idf` container
  (what `scripts/verify` and CI run; nothing is flashed).
- `scripts/idf-flash.sh`: builds and flashes one board: an owner step.

Hardware: [docs/hardware.md](../docs/hardware.md). Bench procedure after the
first boards are built: [docs/bench.md](../docs/bench.md). Flashing a board
needs the owner's approval.
