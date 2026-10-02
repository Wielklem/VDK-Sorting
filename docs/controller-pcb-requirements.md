# Controller PCB — Requirements Specification

Version 0.1 · 2026-10-01 · Belongs to: Vision Sorting Platform (controller functions C10–C90, protocol M45)

---

## 0. How to read this document

- **[TBD]** marks an assumed value that must be confirmed before layout. The assumed value is used for sizing.
- Part numbers are **reference choices**. Check stock and lifecycle in Flux / your distributor; pin-compatible alternates are listed where possible.
- Numbering steps by 10 so requirements can be inserted later. Requirement IDs are `R<section>.<nn>`.

---

## 10. Purpose and design rules

The board is a **dumb, deterministic I/O executor** for the vision PC.

| ID | Rule |
|---|---|
| R10.01 | The board stores **no machine settings**. All timing, IO mapping, pulse widths and trigger plans come from the PC over Ethernet and live in RAM only. |
| R10.02 | Allowed non-volatile data: firmware, board serial nr, MAC address, network identity (IP via DHCP or DIP switch). Nothing else. |
| R10.03 | All time-critical outputs are fired by **hardware timer compare on the encoder count**, never by the PC or polling loops. |
| R10.04 | On power-up, reset, firmware hang or PC heartbeat loss, all outputs go to a **safe state** within a defined time (see 90). |
| R10.05 | The board is **not safety-rated**. The emergency stop is a separate hardwired circuit that removes field power (24 V OUT) independently. |

---

## 20. System sizing assumptions [TBD]

| Parameter | Assumed value | Used for |
|---|---|---|
| Lanes | up to 12 | input/output count |
| Objects per lane | up to 10 /s | event and queue sizing |
| Total objects | up to 120 /s | — |
| Tipper outputs (onboard) | 16 | driver count |
| Tipper outputs (with expansion) | up to 48 (2 × 16 extra) | expansion connectors |
| Camera trigger outputs | 8 | fast outputs |
| Light strobe outputs | 4 | fast outputs |
| Digital inputs | 8 | sensors, status |
| Encoder inputs | 2 (1 used, 1 spare) | timers |
| Max camera-to-tipper travel time | 3 s | queue depth |
| Tipper solenoid current | ≤ 1.0 A continuous, ≤ 2.0 A peak per channel | driver choice |
| Encoder resolution × speed | ≤ 500 kHz edge rate (after ×4 decoding: ≤ 2 M counts/s) | input stage, timer |

Confirm all of these against the actual machine. Driver choice (section 60) depends directly on solenoid current.

---

## 30. Block overview

```
                     ┌──────────────────────────── ISOLATION BARRIER ─────────────────────────┐
 24V LOGIC IN ─► Power (logic) ─► 5V ─► 3V3 ─► MCU (STM32H723) ─┬─ RMII ─► PHY ─► RJ45 (PC)    │
                                         │                      ├─ TIM2 enc ◄─ Encoder rx ◄── iso ◄─ ENC1 (RS-422/HTL)
                                         │                      ├─ TIM23 enc ◄─ Encoder rx ◄── iso ◄─ ENC2
                                         │                      ├─ GPIO fast ─► iso ─► Fast out ×12 ─► cameras / lights
                                         │                      ├─ GPIO ─► iso high-side ×16 ─► tippers (24V OUT)
                                         │                      ├─ SPI + LATCH + OE ─► iso ─► Expansion ×2
                                         │                      ├─ Input capture ◄─ iso ◄─ DI ×8 (24V)
                                         │                      ├─ I2C ─► EEPROM (MAC)
 24V OUT (field, via E-stop) ─────────────────────────────────── └─ Ext. watchdog ─► OUTPUT_ENABLE
```

Two separate 24 V supplies enter the board:
- **24 V LOGIC** powers the MCU and network. It stays on during an e-stop.
- **24 V OUT** is field power for tippers, triggers and lights. It is switched off by the e-stop circuit.

---

## 40. MCU

| ID | Requirement | Value |
|---|---|---|
| R40.01 | Part | **STM32H723ZGT6** (LQFP144). Alt: STM32H733ZG, STM32H743ZI (pin-compatible family, check timer mapping) |
| R40.02 | Core / clock | Cortex-M7, 550 MHz, FPU |
| R40.03 | Flash | 1 MB internal (firmware ≤ 256 KB, bootloader 64 KB) |
| R40.04 | RAM | 564 KB internal (needed ≤ 200 KB, see 120) |
| R40.05 | Timers needed | 2 × 32-bit encoder-capable (TIM2, TIM23), 1 × 32-bit µs timebase (TIM5), 1 × 32-bit pulse-end scheduler (TIM24), input capture for DI |
| R40.06 | Ethernet | Internal MAC, RMII to external PHY |
| R40.07 | Clock | HSE 25 MHz crystal ±20 ppm (shared with PHY via MCO, or separate PHY crystal) |
| R40.08 | Debug | SWD (Tag-Connect TC2050-IDC-NL footprint) + UART debug header (3-pin, 3.3 V) |
| R40.09 | Boot | BOOT0 jumper/pad, NRST button |
| R40.10 | Recovery | USB-C (device, FS) for DFU firmware recovery and service console. ESD-protected (USBLC6-2SC6) |
| R40.11 | Decoupling | 100 nF per VDD pin + 4.7 µF bulk; VCAP pins per datasheet (2 × 2.2 µF); VDDA filtered (ferrite + 1 µF) |

### 40.10 Computation budget

| Load | Rate | Cost | CPU |
|---|---|---|---|
| Compare-match ISR (trigger, strobe, eject on) | ≤ 5,000 /s | ≤ 1 µs | < 0.5 % |
| Pulse-end ISR | ≤ 5,000 /s | ≤ 0.5 µs | < 0.3 % |
| Trigger log + event stream | ≤ 10,000 events/s | ≤ 0.3 µs | < 0.3 % |
| Ethernet / UDP (lwIP) | ≤ 2,000 packets/s | ≤ 20 µs | < 4 % |
| Encoder status to PC | 1 kHz | ≤ 5 µs | < 0.5 % |

Total is below 10 %. The MCU is chosen for **interrupt latency and timer count**, not for raw compute.

### 40.20 Timing performance (board level)

| ID | Requirement | Value |
|---|---|---|
| R40.21 | Encoder count → output GPIO edge | ≤ 2 µs, jitter ≤ 0.5 µs (ISR at highest priority, ITCM code, BSRR writes) |
| R40.22 | Simultaneous events at the same count | Fired within the same ISR, skew ≤ 1 µs |
| R40.23 | Pulse width range / resolution | 10 µs – 2 s / 1 µs |
| R40.24 | Timestamp resolution | 1 µs, 32-bit (wraps every 71 min; 64-bit extended in software) |
| R40.25 | Total output accuracy vs encoder | ≤ 1 encoder count + 2 µs + output driver delay (constant, calibrated on the PC) |

---

## 50. Encoder inputs (×2)

| ID | Requirement | Value |
|---|---|---|
| R50.01 | Signals | A, B, Z (index), quadrature |
| R50.02 | Input standards | RS-422 differential (5 V TTL line-driver encoders) **and** HTL 24 V push-pull/single-ended, selectable per channel by jumper |
| R50.03 | Max input frequency | 1 MHz per channel (RS-422), 200 kHz (HTL) |
| R50.04 | RS-422 receiver | AM26LV32E (or MAX3097E), 120 Ω termination switchable by jumper |
| R50.05 | HTL receiver | Comparator or ISO1211-style input with ≤ 1 µs delay. **No slow optocouplers.** |
| R50.06 | Isolation | Digital isolator after receiver (ISO7731 / Si8630, ≥ 50 Mbps), field side powered by isolated DC-DC |
| R50.07 | Encoder supply | 5 V and 24 V selectable output to encoder, 200 mA, short-circuit protected (PTC or eFuse) |
| R50.08 | Decoding | MCU timer in encoder mode ×4, 32-bit counter, digital input filter enabled |
| R50.09 | Connector | 8-pin pluggable terminal (A+, A−, B+, B−, Z+, Z−, V+, 0V) or M12 8-pin A-coded |
| R50.10 | Protection | TVS on each line (SM712 for RS-422), series resistors 10–47 Ω |

---

## 60. Tipper outputs (×16 onboard)

| ID | Requirement | Value |
|---|---|---|
| R60.01 | Type | 24 V high-side (PNP/sourcing), galvanically isolated |
| R60.02 | Current | ≥ 1.0 A continuous, ≥ 2 A peak per channel [TBD per solenoid] |
| R60.03 | Reference driver | **ST ISO8200BQ** (8-ch isolated, parallel input, 0.7 A). If > 0.7 A is needed: **ST IPS4260L** / **Infineon BTT6030-2ERA** behind a digital isolator |
| R60.04 | Input mode | **Parallel** (direct GPIO per channel). No SPI in the timing path for onboard outputs. |
| R60.05 | Inductive load | Internal active clamp of the driver. Add an external freewheel diode option footprint per channel for fast-release vs slow-release choice. |
| R60.06 | Propagation delay | Datasheet value ≤ 100 µs and stable. The constant part is compensated in G120. |
| R60.07 | Diagnostics | Fault/overtemp line per driver chip to MCU input |
| R60.08 | Default state | OFF at power-up and reset: pull-downs on all driver inputs, plus global OUTPUT_ENABLE (see 90) |
| R60.09 | Indication | LED per channel on field side |
| R60.10 | Field supply | 24 V OUT, fuse per group of 8 (resettable eFuse TPS1663 or blade fuse), reverse polarity protection, TVS (SMBJ33A) |
| R60.11 | Connector | Pluggable terminal blocks 5.08 mm, output + 0 V per channel (2-pin per channel or 16+GND blocks) |

---

## 70. Fast outputs (×12): camera triggers (8) + light strobes (4)

| ID | Requirement | Value |
|---|---|---|
| R70.01 | Type | Isolated push-pull, level selectable per group: 5 V or 24 V (jumper) |
| R70.02 | Rise/fall time | ≤ 1 µs into 10 mA / 1 m cable |
| R70.03 | Propagation delay | ≤ 1 µs (digital isolator ≤ 50 ns + driver) |
| R70.04 | Current | ≥ 50 mA per channel, short-circuit protected |
| R70.05 | Reference parts | Digital isolator ISO7741 / Si8641 → push-pull driver (e.g. TC4427 at 24 V, 74LVC1G125 at 5 V) |
| R70.06 | Camera compatibility | Must drive the Daheng opto-isolated trigger input (Line0). **Verify voltage and current range in the camera datasheet [TBD].** |
| R70.07 | Light compatibility | Trigger input of an external strobe/light controller. The board does **not** drive LED current. |
| R70.08 | Default state | Low at reset |
| R70.09 | Connector | Pluggable terminal 3.81 mm, signal + 0 V per channel |

---

## 80. Digital inputs (×8)

| ID | Requirement | Value |
|---|---|---|
| R80.01 | Type | 24 V sinking inputs, IEC 61131-2 Type 1/3 |
| R80.02 | Reference part | **TI ISO1211** (1 ch) or **ISO1212** (2 ch), isolated, ≤ 1 µs delay |
| R80.03 | Timestamping | Routed to timer input-capture pins (or EXTI + TIM5 timestamp), ≤ 2 µs accuracy |
| R80.04 | Use | Product sensors, air pressure OK, e-stop status (read-only), door switches |
| R80.05 | Indication | LED per input |
| R80.06 | Connector | Pluggable terminal 3.81 mm |

---

## 90. Safe state and watchdog

| ID | Requirement | Value |
|---|---|---|
| R90.01 | Global OUTPUT_ENABLE | Hardware AND of: MCU OE pin, external watchdog OK, 24 V OUT present. Gates all tipper and fast outputs. |
| R90.02 | External watchdog | TPS3430 (window watchdog), timeout ≈ 50 ms, fed by the firmware main loop |
| R90.03 | MCU watchdog | IWDG enabled, ≈ 100 ms |
| R90.04 | PC heartbeat loss | Firmware: after timeout (set by PC, default 200 ms) → apply safe-state action from PC (all off / all reject outputs on) |
| R90.05 | Brown-out | MCU BOR level 3; supervisor on 3.3 V |
| R90.06 | Power-up | All outputs off until the PC has sent a valid config and enabled RUN |

---

## 100. Ethernet

| ID | Requirement | Value |
|---|---|---|
| R100.01 | Interface | 10/100BASE-TX, RMII |
| R100.02 | PHY | **Microchip LAN8742A** (alt: TI DP83848, DP83825I) |
| R100.03 | Connector | RJ45 with integrated magnetics and LEDs (e.g. Würth 7499010211A / HanRun HR911105A) |
| R100.04 | Protection | ESD array on PHY side, 1500 V magnetics isolation, Bob Smith termination to chassis via 1 nF/2 kV |
| R100.05 | Layout | RMII traces length-matched ±5 mm, 50 Ω single-ended; MDI pairs 100 Ω differential |
| R100.06 | MAC address | EEPROM with EUI-48 (**Microchip 24AA02E48**) on I2C |
| R100.07 | Protocol | UDP, M45 binary protocol; firmware with lwIP (bare-metal or FreeRTOS) |
| R100.08 | IP | DHCP or static from 4-position DIP switch (last octet offset) |
| R100.09 | Bandwidth | ≤ 2 Mbit/s expected; 100 Mbit/s link is ample |

---

## 110. Expansion (×2 connectors)

| ID | Requirement | Value |
|---|---|---|
| R110.01 | Purpose | Up to 2 × 16 extra tipper outputs on separate expansion boards (later) |
| R110.02 | Interface | SPI (≤ 20 MHz) + LATCH + OE + FAULT + 3.3 V/5 V + GND, via 10-pin IDC, digitally isolated on main board |
| R110.03 | Timing | Shift data in ahead of time; fire with the LATCH edge at compare-match → same ≤ 2 µs accuracy as onboard |
| R110.04 | Cable | ≤ 0.5 m, inside cabinet |

---

## 120. Firmware memory sizing

| Buffer | Size | RAM |
|---|---|---|
| Eject queue | 120 obj/s × 3 s × 2 margin = 720 entries × 16 B | 12 KB |
| Trigger plan | 64 entries × 16 B | 1 KB |
| Trigger log ring | 4,096 × 12 B | 48 KB |
| Event stream ring | 8,192 × 16 B | 128 KB (DTCM/AXI) |
| IO mapping + timing config | — | < 4 KB |
| lwIP + buffers | — | ≈ 40 KB |
| Stack / misc | — | ≈ 20 KB |
| **Total** | | **≈ 255 KB of 564 KB** |

Time-critical ISRs and their data go in ITCM/DTCM.

---

## 130. Power

| ID | Requirement | Value |
|---|---|---|
| R130.01 | 24 V LOGIC input | 19.2–30 V, ≤ 0.5 A; reverse polarity (P-FET), TVS SMBJ33A, PTC fuse |
| R130.02 | 24 V → 5 V | Buck, 1.5 A (e.g. TI LMR36015 / TPS54202) |
| R130.03 | 5 V → 3.3 V | Buck or LDO, 800 mA (e.g. TLV62569 / AP7361C) |
| R130.04 | Isolated field-side logic | 5 V/3.3 V isolated DC-DC (e.g. Murata NXE1S0505MC or iso transformer driver SN6505B) for encoder/fast output isolator sides |
| R130.05 | 24 V OUT input | ≥ 20 A total [TBD by tipper count × current], separate 2-pin high-current terminal (7.62 mm), separate ground return |
| R130.06 | Grounding | Logic GND, field 0 V and chassis kept separate; chassis via DIN-rail clip / mounting hole |
| R130.07 | Power-good | Monitored by MCU; LEDs for 3.3 V, 24 V LOGIC, 24 V OUT |

---

## 140. Mechanical and PCB

| ID | Requirement | Value |
|---|---|---|
| R140.01 | Form factor | ≈ 160 × 100 mm, DIN-rail enclosure (e.g. Phoenix Contact UCS / ICS series) [TBD] |
| R140.02 | Layers | 4: Signal / GND / Power / Signal |
| R140.03 | Thickness / copper | 1.6 mm, outer 2 oz (output currents), inner 1 oz |
| R140.04 | Isolation barrier | Continuous keep-out slot/gap between logic and field side; creepage ≥ 3 mm (functional insulation, 24 V field + transients) |
| R140.05 | Current paths | 24 V OUT traces sized for 2 A per channel and 8 A per group (polygons) |
| R140.06 | Connectors | All field connectors on one long board edge; Ethernet, USB and SWD on the opposite edge |
| R140.07 | Test points | All supply rails, encoder A/B after isolator, OUTPUT_ENABLE, 1 TP per output group |
| R140.08 | Silkscreen | Channel numbers matching IO mapping names (TIP01–TIP16, TRG01–TRG08, STR01–STR04, DI01–DI08, ENC1/2) |
| R140.09 | Finish | ENIG, conformal coating (moist and wash-down environment); mask coating off connectors and test points |

---

## 150. Environment and compliance

| ID | Requirement | Value |
|---|---|---|
| R150.01 | Operating temperature | 0 … +55 °C (in cabinet); components rated −40 … +85 °C |
| R150.02 | Humidity | 5–95 % non-condensing (conformal coating) |
| R150.03 | EMC immunity | EN IEC 61000-6-2 (industrial): ESD 4/8 kV, burst 2 kV on supply/IO, surge 1 kV |
| R150.04 | EMC emission | EN IEC 61000-6-4 |
| R150.05 | Standards reference | IEC 61131-2 for IO behaviour; CE as part of the machine |

---

## 160. Firmware requirements (summary)

| ID | Function | Implementation |
|---|---|---|
| C10 | Encoder counting | TIM2/TIM23 encoder mode; position to PC at 1 kHz |
| C20 | Camera triggers | Compare-match on encoder counter → GPIO, pulse end via TIM24 |
| C30 | Trigger log | Trigger nr + encoder count + µs timestamp → ring buffer → PC |
| C40 | Light strobes | Same as C20, offset and width from PC |
| C50 | Eject queue | Sorted queue per tipper; next event loaded into compare register; late-command detection |
| C60 | IO mapping | RAM table: logical function → physical channel, from PC |
| C70 | Watchdog & safe state | IWDG + external WD + PC heartbeat → safe-state action |
| C80 | Event stream | All IO edges with µs timestamp, batched UDP to PC |
| C90 | Protocol | M45 over UDP: sequence numbers, acks, protocol version check |
| C100 | Bootloader | Firmware update over Ethernet (UDP/TFTP) + USB DFU fallback |
| C110 | Self-test | Output test only on PC command, never automatic |

Stack: STM32CubeH7 HAL/LL, lwIP, optional FreeRTOS (ISRs never blocked by RTOS; timer ISRs above `configMAX_SYSCALL_INTERRUPT_PRIORITY`).

---

## 170. Reference BOM (key parts)

| Function | Part | Qty |
|---|---|---|
| MCU | STM32H723ZGT6 | 1 |
| Ethernet PHY | LAN8742A-CZ | 1 |
| RJ45 + magnetics | HR911105A | 1 |
| MAC EEPROM | 24AA02E48T | 1 |
| Encoder receiver | AM26LV32EIDR | 2 |
| Digital isolators | ISO7741 / ISO7731 | ≈ 6 |
| Tipper drivers | ISO8200BQ (≤ 0.7 A) or IPS4260L + isolator (> 0.7 A) | 2 / 4 |
| Fast output drivers | TC4427 / 74LVC1G125 | ≈ 6 / 12 |
| Digital inputs | ISO1212 | 4 |
| External watchdog | TPS3430 | 1 |
| 24 → 5 V buck | LMR36015 | 1 |
| 5 → 3.3 V | TLV62569 | 1 |
| Isolated DC-DC | SN6505B + transformer, or NXE1S0505MC | 2 |
| eFuse 24 V OUT | TPS1663 | 2 |
| USB ESD | USBLC6-2SC6 | 1 |
| TVS supply | SMBJ33A | 3 |
| Crystal | 25 MHz, ±20 ppm | 1 |
| Terminals | Phoenix/Würth pluggable 5.08 / 3.81 mm | per IO |

---

## 180. Flux AI starting prompt

```
Design a 4-layer industrial controller PCB, 160x100 mm, DIN-rail enclosure.
MCU: STM32H723ZGT6 (LQFP144) with 25 MHz crystal, SWD (Tag-Connect TC2050),
USB-C device (DFU) with USBLC6-2SC6, BOOT0 jumper, reset button.
Ethernet: LAN8742A PHY via RMII, RJ45 with integrated magnetics, 24AA02E48 MAC EEPROM on I2C.
Power: 24V LOGIC input (19.2-30V) with reverse-polarity P-FET, SMBJ33A TVS, PTC;
LMR36015 buck to 5V 1.5A; TLV62569 to 3.3V. Separate 24V OUT field supply input
with 2x TPS1663 eFuse (one per 8 outputs).
Isolation barrier (>=3 mm creepage) between logic side and field side.
2x encoder inputs: AM26LV32 RS-422 receivers with switchable 120R termination,
alternative HTL 24V input via jumper, isolated with ISO7731 to MCU TIM2 and TIM23
encoder-mode pins; switchable 5V/24V encoder supply, short-circuit protected.
16x tipper outputs: 2x ISO8200BQ isolated high-side drivers, parallel input from MCU GPIO,
LED per channel, OUTPUT_ENABLE gating.
12x fast outputs (8 camera triggers, 4 light strobes): ISO7741 isolators + push-pull drivers,
5V/24V selectable by jumper, <1 us propagation.
8x 24V digital inputs: 4x ISO1212 to MCU timer input-capture pins, LED per input.
TPS3430 external window watchdog; OUTPUT_ENABLE = AND(MCU OE, watchdog OK, 24V OUT present).
2x 10-pin IDC expansion connectors: isolated SPI + LATCH + OE + FAULT.
4-pos DIP switch for IP, status LEDs (power, link, run, fault).
All field connectors (pluggable terminal blocks 5.08/3.81 mm) on one long edge;
Ethernet, USB, SWD on opposite edge. 2 oz outer copper, ENIG.
```

---

## 190. Open points

| # | Question | Impacts |
|---|---|---|
| 1 | Solenoid/valve current and type (24 V DC? peak-and-hold?) | R60 driver choice, R130.05 |
| 2 | Number of lanes and tippers per lane | R20, expansion need |
| 3 | Encoder type (RS-422 or HTL), PPR, max belt speed | R50 |
| 4 | Daheng trigger input voltage/current range (exact camera model) | R70.06 |
| 5 | Light controller trigger input specification | R70.07 |
| 6 | Enclosure choice | R140.01 |
| 7 | One board per machine, or one per line? | R20, cost |
