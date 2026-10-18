#!/system/bin/sh
# Dump the stock DT's TLMM pin states with config values.
TL=/proc/device-tree/soc/pinctrl@400000
for g in $TL/*/; do
	gn=$(basename "$g")
	for d in $g*/; do
		dn=$(basename "$d")
		[ -f "$d/pins" ] || continue
		f=$(cat "$d/function" 2>/dev/null | tr -d '\000')
		p=$(od -An -tx1 "$d/pins" 2>/dev/null | tr -s ' ' '\n' | grep -v '^$' | tr '\n' ' ')
		# collect config booleans/numeric props
		c=""
		for prop in "$d"/*; do
			[ -f "$prop" ] || continue
			bn=$(basename "$prop")
			case "$bn" in
			pins|function) continue ;;
			esac
			# numeric properties have 4-byte cells; booleans are empty files
			if [ -s "$prop" ]; then
				v=$(od -An -td4 "$prop" 2>/dev/null | tr -s ' ' | sed 's/^ *//')
				c="${c}${bn}=${v},"
			else
				c="${c}${bn},"
			fi
		done
		echo "${gn}|${dn}|${f}|${p}|${c%,}"
	done
done
