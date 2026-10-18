#!/system/bin/sh
# Dump the stock DT's TLMM pin states: group|state|function|pins(hex bytes)
TL=/proc/device-tree/soc/pinctrl@400000
for g in $TL/*/; do
	for d in $g*/; do
		[ -f "$d/pins" ] || continue
		f=$(cat "$d/function" 2>/dev/null | tr -d '\000')
		p=$(od -An -tx1 "$d/pins" 2>/dev/null | tr -s ' ' '\n' | grep -v '^$' | tr '\n' ' ')
		echo "$(basename $g)|$(basename $d)|$f|$p"
	done
done
