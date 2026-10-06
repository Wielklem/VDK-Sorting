#!/usr/bin/env bash
# Read-only GigE host check (P20.40). Usage: tools/check_gige_host.sh <nic> [camera-ip]
set -uo pipefail
nic="${1:?usage: $0 <nic> [camera-ip]}"
cam="${2:-}"
rc=0
ok()  { echo "  OK   $*"; }
fix() { echo "  FIX  $*"; rc=1; }

[ -d "/sys/class/net/$nic" ] || { echo "no such interface: $nic"; exit 2; }

speed=$(cat "/sys/class/net/$nic/speed" 2>/dev/null || echo 0)
[ "$speed" -ge 1000 ] && ok "link $speed Mbit/s" || fix "link $speed Mbit/s (need 1000)"

mtu=$(cat "/sys/class/net/$nic/mtu")
[ "$mtu" -ge 9000 ] && ok "MTU $mtu" || fix "MTU $mtu (want 9000)"

if command -v ethtool >/dev/null; then
  rx_max=$(ethtool -g "$nic" 2>/dev/null | awk '/Pre-set/{p=1} /Current/{p=0} p&&/^RX:/{print $2; exit}')
  rx_cur=$(ethtool -g "$nic" 2>/dev/null | awk '/Current/{c=1} c&&/^RX:/{print $2; exit}')
  if [[ "${rx_max:-}" =~ ^[0-9]+$ && "${rx_cur:-}" =~ ^[0-9]+$ ]]; then
    [ "$rx_cur" -ge "$rx_max" ] && ok "RX ring $rx_cur" || fix "RX ring $rx_cur, max $rx_max"
  else
    echo "  --   RX ring not readable on this NIC"
  fi
else
  fix "ethtool not installed (sudo apt install ethtool)"
fi

rmem=$(sysctl -n net.core.rmem_max)
[ "$rmem" -ge 33554432 ] && ok "rmem_max $rmem" || fix "net.core.rmem_max $rmem (want >= 33554432)"
backlog=$(sysctl -n net.core.netdev_max_backlog)
[ "$backlog" -ge 10000 ] && ok "netdev_max_backlog $backlog" || fix "netdev_max_backlog $backlog (want >= 10000)"

gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unknown)
[ "$gov" = "performance" ] && ok "CPU governor $gov" || fix "CPU governor $gov (want performance)"

if [ -n "$cam" ]; then
  if ping -c 2 -W 1 -M do -s 8972 -I "$nic" "$cam" >/dev/null 2>&1; then
    ok "jumbo ping to $cam (8972 bytes, no fragmentation)"
  else
    fix "jumbo ping to $cam failed (camera, switch or MTU)"
  fi
fi
exit $rc
