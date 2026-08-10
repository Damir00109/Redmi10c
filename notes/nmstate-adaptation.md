# Nmstate adaptation for Redmi 10C (rain/fog)

Upstream: https://github.com/nmstate/nmstate (cloned at src/nmstate)

## Why not stock nmstatectl + NetworkManager

Nmstate's only production southbound provider is NetworkManager.
On this board NM and ath10k_snoc+MPSS soft-hang in either order
(even with unmanaged-devices=*). So stock `nmstatectl apply` is unsafe.

Nmstate kernel-only mode (`set_kernel_only(true)`) can set addresses/routes
via nispor but does not do WPA Wi-Fi association.

## What we adapted

Northbound: declarative YAML inspired by nmstate schema
  /etc/rain/network-state.yml

Southbound provider: `qcom` (our scripts), never NM
  rain-netstate show|apply

```bash
sudo rain-netstate apply
sudo rain-netstate show
```

Equivalent low-level:
```bash
sudo qcom-wifi-start.sh
sudo qcom-wifi-connect.sh
```
