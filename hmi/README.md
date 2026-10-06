# hmi
Qt 6 / QML operator interface (M100, pages G20–G130). Separate process; talks to the service via shared memory + ZeroMQ.

## Design system (P30.40)
- `Theme` (singleton): colours, font sizes, spacing, control height. No hardcoded colours or sizes in pages.
- `VsText` (variants: Body, Caption, Title, Heading), `VsButton` (`primary`), `VsNumberInput` (`from`, `to`, `decimals`, `unit`, `edited`), `VsTable` (`headers`, `model` with `display` role, `currentRow`), `VsDialog`.
- All components build on `QtQuick.Controls.Basic` so they look the same on every OS.
- Gallery: `vsort_hmi --gallery`. Headless check: `vsort_hmi --selftest` (runs in ctest on Linux).
