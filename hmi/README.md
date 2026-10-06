# hmi
Qt 6 / QML operator interface (M100, pages G20–G130). Separate process; talks to the service via shared memory + ZeroMQ.

Pages are `IPage` plugins (`include/vsort/hmi/ipage.hpp`) loaded from `<app dir>/pages`
(override: `VSORT_HMI_PAGES`). Example: `pages/cameras`.
