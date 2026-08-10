# Build host

| | |
|--|--|
| CPU | 2× Xeon E5-2680 v4 @ 2.4GHz → **56 threads** |
| RAM | **60 GiB** (+8G swap) |
| Disk | ~240G free |
| GPU | RTX 5060 Ti (host) |
| Build | `-j56`, ccache 20G |

This machine can chew full kernel trees quickly. Builds are host-only (no phone flash).
