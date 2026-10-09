# hmi
Qt 6 / QML operator interface (M100, pages G20–G130). Separate process; talks to the service via shared memory + ZeroMQ.

## Design system (P30.40)
- `Theme` (singleton): colours, font sizes, spacing, control height. No hardcoded colours or sizes in pages.
- `VsText` (variants: Body, Caption, Title, Heading), `VsButton` (`primary`), `VsNumberInput` (`from`, `to`, `decimals`, `unit`, `edited`), `VsTable` (`headers`, `model` with `display` role, `currentRow`), `VsDialog`.
- All components build on `QtQuick.Controls.Basic` so they look the same on every OS.
- Gallery: `vsort_hmi --gallery`. Headless check: `vsort_hmi --selftest` (runs in ctest on Linux).
- Navigation: `NavBar` across the top, one button per page plugin (sorted by `order()`); a page's own tabs sit in a column on the left (`Theme.sideNavWidth`).

## Overlay layer (P30.70)
- `OverlayLayer` (in `VsortHmi`): draws ROIs, lane lines and detections over a `VideoItem`. Coordinates are camera-frame pixels; bind `sourceSize` to the video's `sourceSize`.
- Used by the G30.20 ROI editor; later by G50.30 lane ROIs and G60.30 debug overlays. No separate page.

## ROI editor and camera settings (P30.80)
- G30 Cameras has tabs: Live view (G30.10), ROI (G30.20), Camera settings (G30.30).
- ROIs are stored per camera in the config module `rois` as fractions (0..1) of the camera image, so they do not depend on the preview size. The editor shows percent. If the camera's hardware ROI changes, the stored ROIs refer to the new image.
- Draw on the picture, drag to move, drag a corner to resize, or type values. Nothing is sent before Save; Reload discards unsaved edits.
- Camera settings (exposure, gain) go to the camera as soon as you press Apply; the HMI then reads back what the camera accepted.
