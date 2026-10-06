# tools
CLI tools (record, replay, export) and Python offline tools (training, analysis).


## vsort_record (P20.90)
Records raw frames to `<out>/<YYYYMMDDTHHMMSS>/` (`session.json` plus one `camNN.vrec` per camera).
Built with `linux-galaxy-debug` (needs `GALAXY_SDK_ROOT`).

    vsort_record --camera 0=<serial> [--camera 1=<serial> ...] [--out <dir>] [--label <text>]
                 [--frames <n> | --duration <s>] [--trigger hardware|freerun]
                 [--exposure-us <us>] [--gain-db <db>] [--queue-depth <n>]

Ctrl-C stops cleanly. Exit code 3 means frames were dropped or missing: do not use that session
as a reference dataset. The sensor is recorded at full size (no ROI option yet).
