# Ubuntu PC setup: GigE cameras (P20.40)

Example values: NIC `enp3s0`, camera subnet `192.168.10.0/24`. Values are starting points; confirm with the acceptance test at the bottom.

## 1. Hardware
- [ ] One dedicated NIC per camera, or one per switch uplink. Intel NICs (i210/i350/X550) preferred; no USB NICs.
- [ ] Switch (if used) supports jumbo frames (MTU >= 9000) and has enough uplink bandwidth.
- [ ] Cat5e or better, <= 100 m.
- [ ] BIOS: PCIe ASPM off, C-states limited, performance power profile.

## 2. Network (NetworkManager)
- [ ] Static IP, no gateway, no IPv6, jumbo MTU, RX ring at the NIC maximum (`ethtool -g enp3s0`):
```bash
sudo nmcli con add type ethernet ifname enp3s0 con-name cam0 \
  ipv4.method manual ipv4.addresses 192.168.10.1/24 ipv4.never-default yes \
  ipv6.method disabled 802-3-ethernet.mtu 9000 ethtool.ring-rx 4096
sudo nmcli con up cam0
```
- [ ] Each NIC gets its own subnet (192.168.10.x, 192.168.11.x, ...). Set the camera IPs inside it (`vsort_daheng_probe setip ...` or the app).
- [ ] Optional: EEE and flow control off (`ethtool --set-eee enp3s0 eee off`, `ethtool -A enp3s0 rx off tx off`).

## 3. Kernel buffers
```bash
sudo tee /etc/sysctl.d/90-vsort-gige.conf << 'CONF'
net.core.rmem_max = 33554432
net.core.netdev_max_backlog = 10000
net.ipv4.conf.all.rp_filter = 2
CONF
sudo sysctl --system
```
The SDK can only use socket buffers up to `rmem_max`.

## 4. Firewall and CPU
- [ ] Allow all traffic on the camera NICs: `sudo ufw allow in on enp3s0` (stream ports are dynamic).
- [ ] CPU governor `performance`: `sudo apt install linux-tools-generic && sudo cpupower frequency-set -g performance`. Make it persistent (systemd unit or tuned).

## 5. Camera side (set by the adapter on open, see `DahengOptions`)
- Packet size: the optimal size for the path, usually 8192+ with jumbo frames. Check the log line "GigE stream: packet size ...".
- Packet delay (`GevSCPD`): leave 0 with one camera per NIC.
  - Rate per camera = width x height x bytes/pixel x fps. A 1 GbE link carries about 110 MB/s; keep the total <= 80 %.
  - Several cameras on one uplink fire at the same trigger edge, so their bursts collide. If frames arrive incomplete, raise `packetDelay` stepwise until they stop. The unit and range depend on the camera model.
  - Still not enough: lower fps/ROI or add a NIC.
- `acquisitionBuffers`: raise (e.g. 16-32) if drops show up only in bursts.

## 6. Verify
```bash
tools/check_gige_host.sh enp3s0 192.168.10.21   # all lines OK
```
- [ ] Free-run at target fps for 10 min per camera: 0 drops, frame IDs contiguous.
- [ ] All cameras on the external trigger at target rate for 10 min: 0 drops.
- [ ] `nstat -az UdpRcvbufErrors` stays at 0, and `ethtool -S enp3s0 | grep -iE "miss|drop|err"` stays flat.
