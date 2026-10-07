# IPC design (P30.10)

Service ↔ HMI. Frames via shared memory, messages via ZeroMQ. Portable code only: all OS calls stay behind `ISharedMemory` (M15).

## 1. Overview
| Channel | Tech | Direction | Content |
|---|---|---|---|
| Preview frames | Shared memory ring, one per camera | service → HMI | downscaled frames |
| Commands | ZeroMQ ROUTER (service) / DEALER (HMI) | HMI → service, reply back | requests + responses |
| Events | ZeroMQ PUB (service) / SUB (HMI) | service → HMI | heartbeat, config changes, diagnostics |

- Endpoints: `tcp://127.0.0.1:<port>` (defaults 5555 commands, 5556 events, configurable). TCP loopback is used instead of `ipc://` because it behaves the same on Linux and Windows.
- The service owns all resources. The HMI is only a client, so a crash of either side is harmless to the other.

## 2. Serialization: FlatBuffers (decision)
- Zero-copy reads, fixed-size integer types, little-endian by definition.
- Schema evolution: only append fields, never reorder or remove (`deprecated` instead).
- Schemas live in `common/ipc/schema/*.fbs`; code is generated at build time via vcpkg `flatbuffers`.
- Why not Protobuf: copying on parse, and nothing in our messages needs its extras. Revisit only if debugging text dumps turn out to be needed (FlatBuffers has JSON output via `flatc`/reflection).

Envelope (every command and event):
| Field | Type | Note |
|---|---|---|
| protocol_version | u16 | bumped on breaking change; mismatch → HMI shows "version mismatch" |
| msg_type | u16 | enum, MSG-<module>-<nn> numbering |
| request_id | u64 | 0 for events; reply echoes the id |
| timestamp_ns | u64 | service monotonic clock |
| payload | union | per msg_type |

## 3. Commands (DEALER/ROUTER)
- Async with request_id, timeout 2 s (HMI side), no REQ/REP lockstep (a lost reply must not block the socket).
- Reply = `{request_id, status (u16), error_text (utf-8), payload}`.

| Command | Purpose | Used by |
|---|---|---|
| Hello | version + capabilities + service state | P30.90 |
| GetCameraList | logical ID, serial, state | G30.10 |
| SetPreview | camera_id, fps, max_width, enabled | G30.10 |
| GetCameraSettings / SetCameraSettings | exposure, gain, ROI, trigger mode | G30.30 |
| GetConfig / SetConfig | config store access (JSON string, ConfigVersion) | G20, G30.20 |
| StartBatch / StopBatch | later phases | P80 |
| GetCupSnapshot | last cups of one lane (or all, lane_id 0) with seq and depth | G140.10 (P80.100) |

- Freeze/unfreeze is HMI-local (stop reading the ring). No command needed.
- Commands are idempotent where possible.

## 4. Events (PUB/SUB)
Topic prefix (first frame) for SUB filtering: `hb`, `cfg`, `diag`, `preview`, `cam`, `cups`.
| Event | Message | Rate |
|---|---|---|
| Heartbeat | MSG-10-01 (service state, uptime, seq) | 1 Hz |
| ConfigChanged | MSG-20-01 (config id, new ConfigVersion) | on change |
| DiagEvent | MSG-90-01 | on change, ≤ 10 Hz aggregated |
| PreviewStreamChanged | camera_id, shm name, generation | on (re)create |
| CameraListChanged | MSG-30-02, no payload: a camera appeared, disappeared or changed state; the HMI sends GetCameraList again | on change |
| CupUpdate | MSG-50-02 (topic `cups`): changed cups of one lane, full state per cup, seq per lane | ≤ 10 Hz per lane |

- HMI declares "service offline" after 3 missed heartbeats (3 s); reconnect is automatic (ZeroMQ), then Hello + GetCameraList + re-attach to the rings (P30.90).
- PUB/SUB drops messages for slow subscribers by design. State is always re-fetched after reconnect, never rebuilt from missed events.
- MSG-50-01 ObjectRecord (msg type 5001, table `ObjectRecord`) is in the schema but not published: it stays on the service's message bus until a client needs it. The HMI gets cups via MSG-50-02 (P80.100).
- Product Monitor: subscribe to `cups` first, then send GetCupSnapshot; apply the snapshot and drop CupUpdate events whose seq is not above the snapshot's seq for that lane. Keep the last `depth` cups by cup ID.

## 5. Preview ring buffer
One shared memory object per camera and generation: `vdk_preview_<camera_id>_<generation>` (name rules: lowercase `[a-z0-9_]`, mapped to `/vdk_preview_x` on POSIX and `Local\vdk_preview_x` on Windows inside M15 only).

Single writer (service), multiple readers (HMI, tools), lock-free.

Layout (all little-endian, fixed-size ints, 64-byte aligned):
| Block | Fields |
|---|---|
| Header | magic u32 `0x56444B50`, layout_version u16, slot_count u16 (default 4), slot_size u32 (max payload bytes), generation u32, write_seq `atomic<u64>` (last committed slot count) |
| Slot[i] header | seq `atomic<u64>`, frame_id u64, timestamp_ns u64, width u32, height u32, stride u32, pixel_format u32 (enum: Mono8, RGB8), payload_size u32 |
| Slot[i] payload | pixel data, `slot_size` bytes |

Writer (seqlock per slot): slot = write_seq % slot_count → set slot.seq odd → write header + payload → set slot.seq even (release) → write_seq++ (release).
Reader: s = write_seq (acquire); slot = (s-1) % slot_count; read slot.seq (must be even) → copy header + payload → re-read slot.seq; if changed, retry with the newest slot. Never blocks the writer.

Rules:
- `static_assert(std::atomic<uint64_t>::is_always_lock_free)`.
- Preview is a lossy latest-frame stream: readers only get the newest frame, drops are fine and are not counted as errors.
- Size/fps change (SetPreview): service creates a new ring with generation+1, emits PreviewStreamChanged, keeps the old one for 1 s, then closes it. Readers compare generation and re-attach.
- Service start: remove stale rings of the same name, then create. Service stop: remove. HMI never creates or unlinks.
- Memory per ring = slot_count × (slot header + max_width × max_height × bytes_per_pixel); e.g. 4 × 1280×1024 Mono8 ≈ 5.2 MB.
- Reader polls `write_seq` from a render-timer (display rate). No extra wakeup channel in V1.
- The generation is part of the name because closing a ring removes its name; a same-name successor would disappear with it. The ring is created on the first preview frame after SetPreview(enabled), so shm_name is empty until the PreviewStreamChanged event arrives. In SetPreview, fps = 0 or max_width = 0 means keep the current value.

## 6. `ISharedMemory` (implemented in P10.45)
Minimal surface (sketch):
- `create(name, size) → expected<Region>`, `open(name) → expected<Region>`, `Region::data()/size()`, `remove(name)`.
- RAII, no OS types in the header. Linux: POSIX shm. Windows: file mapping (stub until M15.20).

## 7. Open points
- Preview pixel format: Mono8 vs RGB8 (depends on camera colour mode, open point 1 of the architecture doc).
- Port defaults and authentication: loopback only, no auth in V1.
- Whether `SetConfig` carries full JSON or patches (decide in P30.20).
