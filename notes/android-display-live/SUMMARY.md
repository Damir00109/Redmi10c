# Live Android display dump (Motregen 4.19 CIP, rain/fog, Magisk root)
# Device: 220333QNY / rain, lcmtype=dsi_panel_c3q_43_03_0b_fhdp_video_display

## Confirmed from live DRM sysfs
- modes: **720x1650x60x89954vid**
- connector: enabled, connected, dpms=Off (display was OFF when dumped)
- panel: ft8006s video mode dsi xinli panel
- type: **dsi_video_mode** (no GRAM — continuous HS video stream required)
- topology: lm=1, intf=1
- bl: bl_ctrl_dcs, max=0x7ff, min=1, bl_move_high_8b + bl_inverted_dbv
- HBM on DCS: 51 ff 0e ; HBM off: 51 cc 0a
- traffic: non_burst_sync_event, h-sync-pulse=0, bpp=24, lane_map_0123, 4 lanes
- lp11-init present, tx-eot-append, bllp power modes
- reset-sequence: 1,5,0,5,1,35 (ms style Android)
- GPIOs: reset=82, te=81, enp=98, enn=101
- porches: H FP=26 BP=25 PW=16 ; V FP=135 BP=110 PW=10 ; 60 Hz
- clock: 89954 kHz (= (720+26+16+25)*(1650+135+10+110)*60/1000)
- phy-timings: 00 13 05 04 13 1e 05 05 06 02 04 00 12 0a
- PLL: 7nm DSI PLL registered (dsi_pll_clock_register_7nm)
- sde hw rev: 0x600a0000
- backlight sysfs max_brightness=2047
