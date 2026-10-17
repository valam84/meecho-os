#!/bin/bash
# Do typectest and otgtest notice when typec.c is wrong?  Each mutation is
# one edit to a copy of the file; the stands must fail on every one of them.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
SRC=$TREE/minix/drivers/power/qcom_pmic
T=${TMPDIR:-/tmp}/pmic-mutate
CLOVER=${CLOVER_DTB:-$HOME/work-clover-flash/sdm660-xiaomi-clover.dtb}
mkdir -p "$T"
SD=$TREE/port/test/sdmmc
TS=$TREE/port/test/qcom_touch/stubs
CC=(cc -std=gnu99 -Wall -Wextra -Wno-unused-parameter -O1 -g
    -I"$HERE/stubs" -I"$TS" -I"$SD/clkstubs" -I"$SD/stubs" -I"$SRC"
    -idirafter "$TREE/minix/include"
    -D__unused="__attribute__((unused))"
    -DREAL_FDT_H="\"$TREE/minix/include/minix/fdt.h\"")

# name|sed expression applied to typec.c
MUTATIONS=(
# Reading the port (typectest).
'vbus-plugin-first|s/if ((s->otg_status \& 0x07) == 0x02 || (s->cmd_otg \& 0x01))/if (0)/'
'ufp-mask-not-byte|s/switch (s->st\[0\]) {/switch (s->st[0] \& 0xe0) {/'
'dfp-whole-byte|s/switch (s->st\[1\] \& TC_DFP_MASK) {/switch (s->st[1]) {/'
'ignore-dfp-bit|s/if (s->st\[3\] \& TC_ST4_DFP) {/if (0) {/'
'cable-not-sink|/case TC_DFP_RD_RA_VCONN:/d'
'unknown-is-none|s/? TC_UNKNOWN : TC_NONE/? TC_NONE : TC_NONE/'
'rp-1p5-as-3a|s/s->st\[0\] == TC_RP_1P5A ? 1500/s->st[0] == TC_RP_1P5A ? 3000/'
'sid-of-child|s/f->c->sid = (unsigned)f->sid\[depth - 1\];/f->c->sid = 2;/'
'write-on-read|s/io->rd(io->ctx, TC_CMD_OTG, \&s->cmd_otg, 1) != 0)/io->rd(io->ctx, TC_CMD_OTG, \&s->cmd_otg, 1) != 0 || io->wr(io->ctx, TC_CMD_OTG, 0) == 0)/'
# The boost (otgtest).
'usbin-awake|s/	if (update(io, TC_USBIN_CMD_IL, TC_USBIN_SUSPEND, TC_USBIN_SUSPEND,/	if (0 \&\& update(io, TC_USBIN_CMD_IL, TC_USBIN_SUSPEND, TC_USBIN_SUSPEND,/'
'no-unlock|s/	if ((off \& 0xff) >= 0xa0 \&\&/	if (0 \&\&/'
'one-step|s/	unsigned i, n = o->pm660 ? sizeof(otg_step_ma) \/ sizeof(otg_step_ma\[0\])/	unsigned i, n = o->pm660 ? 1/'
'limit-at-step|s/			(void)wr1(io, TC_OTG_CL, (uint8_t)otg_code(TC_OTG_MA));/			;/'
'first-sight|s/o->fault || o->sink_seen < 2)/o->fault || o->sink_seen < 1)/'
'overload-retried|/the charger reports it failed or/{n;n;s/			o->fault = 1;/			;/}'
'external-vbus|s/	if ((s->usbin_rt \& TC_USBIN_PLUGIN) || (s->st\[3\] \& TC_ST4_VBUS)) {/	if (0) {/'
'role-not-back|s/		if (o->role_saved \&\& update(io, TC_SW_CTRL, TC_ROLE_MASK,/		if (0 \&\& update(io, TC_SW_CTRL, TC_ROLE_MASK,/'
'off-limit-kept|/^boost_regs_off/,/^}/{/TC_OTG_CL, 0);/d}'
'no-drp|s/		if (update(io, TC_SW_CTRL, TC_ROLE_MASK, TC_ROLE_DRP, \&was) !=/		if (update(io, TC_SW_CTRL, 0, TC_ROLE_DRP, \&was) !=/'
)

caught=0
for m in "${MUTATIONS[@]}"; do
	name=${m%%|*}
	expr=${m#*|}
	sed "$expr" "$SRC/typec.c" > "$T/typec.c"
	if cmp -s "$SRC/typec.c" "$T/typec.c"; then
		echo "mutation $name: did not apply"
		continue
	fi
	if ! "${CC[@]}" -o "$T/typectest" "$HERE/typectest.c" "$T/typec.c" \
	    "$TREE/minix/lib/libsys/fdt.c" 2>"$T/cc.log" ||
	   ! "${CC[@]}" -o "$T/otgtest" "$HERE/otgtest.c" "$T/typec.c" \
	    "$TREE/minix/lib/libsys/fdt.c" 2>>"$T/cc.log"; then
		echo "mutation $name: does not compile"
		continue
	fi
	if "$T/typectest" "$CLOVER" >"$T/$name.log" 2>&1 &&
	   "$T/otgtest" >>"$T/$name.log" 2>&1; then
		echo "mutation $name: NOT caught"
	else
		echo "mutation $name: caught ($(grep -m1 -E 'FAIL|model:' \
		    "$T/$name.log" | sed 's/^ *//' | cut -c1-60))"
		caught=$((caught + 1))
	fi
done
echo "caught $caught of ${#MUTATIONS[@]}"
[ "$caught" -eq "${#MUTATIONS[@]}" ]
