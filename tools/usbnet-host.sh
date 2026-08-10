#!/usr/bin/env bash
# Host side for rain USB RNDIS: 192.168.7.1 ↔ phone 192.168.7.2
set -euo pipefail
IF=${1:-}
if [ -z "$IF" ]; then
  for c in /sys/class/net/*/device/uevent; do
    [ -f "$c" ] || continue
    if grep -q 'DRIVER=rndis_host\|PRODUCT=1d6b/104' "$c" 2>/dev/null || \
       grep -qi rndis "$(dirname "$c")/../uevent" 2>/dev/null; then
      IF=$(basename "$(dirname "$(dirname "$c")")")
      break
    fi
  done
fi
# fallback: newest usb0/enx*
if [ -z "${IF:-}" ]; then
  IF=$(ls /sys/class/net | grep -E '^(usb|enx)' | head -1 || true)
fi
[ -n "${IF:-}" ] || { echo "no usb rndis iface (modprobe rndis_host?)"; ip -br link; exit 1; }

echo "using $IF"
sudo ip link set "$IF" up
sudo ip addr flush dev "$IF" 2>/dev/null || true
sudo ip addr add 192.168.7.1/24 dev "$IF"
echo "ping phone..."
ping -c 3 -W 2 192.168.7.2 || true
echo "optional NAT (share host net): sudo $0 nat $IF"
if [ "${1:-}" = "nat" ] || [ "${2:-}" = "nat" ]; then
  DEV=${IF}
  [ "$1" = "nat" ] && DEV=${2:-$IF}
  WAN=$(ip route | awk '/default/ {print $5; exit}')
  echo "MASQUERADE via $WAN"
  sudo sysctl -w net.ipv4.ip_forward=1 >/dev/null
  sudo iptables -t nat -C POSTROUTING -s 192.168.7.0/24 -o "$WAN" -j MASQUERADE 2>/dev/null || \
    sudo iptables -t nat -A POSTROUTING -s 192.168.7.0/24 -o "$WAN" -j MASQUERADE
  echo "on phone: busybox ip route add default via 192.168.7.1; echo nameserver 1.1.1.1 >/etc/resolv.conf"
fi
