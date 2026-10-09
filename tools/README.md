# tools
CLI tools (record, replay, export) and Python offline tools (training, analysis).


## vsort_record (P20.90)
Records raw frames to `<out>/<YYYYMMDDTHHMMSS>/` (`session.json` plus one `camNN.vrec` per camera).
Built with `linux-galaxy-debug` (needs `GALAXY_SDK_ROOT`).

    vsort_record --camera 0=<serial> [--camera 1=<serial> ...] [--root <dir>] [--out <dir>]
                 [--label <text>] [--frames <n> | --duration <s>] [--trigger hardware|freerun]
                 [--exposure-us <us>] [--gain-db <db>] [--queue-depth <n>]

`--root <dir>` (P20.95): the same root as `vsort_service --root`. The tool applies the saved
`camera_settings` of that root per camera ID (exposure, gain, trigger, edge, ROI) and records to
`<root>/data/recordings` unless `--out` is given. `--exposure-us`, `--gain-db` and `--trigger`
override the saved values for all cameras. Stop the service first. Without `--root`: exposure
10000 us, gain 0 dB, hardware trigger, full sensor.

Ctrl-C stops cleanly. Exit code 3 means frames were dropped or missing: do not use that session
as a reference dataset.