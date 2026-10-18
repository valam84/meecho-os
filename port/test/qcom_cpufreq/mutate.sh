#!/bin/sh
# Мутации службы qcom_cpufreq: каждая - правдоподобная ошибка в копии её
# файлов.  Стенд (run.sh) обязан её поймать: нарушением в модели (L у PLL,
# от которого идут ядра; мультиплексор на незахваченный PLL; частота выше
# того, что держит уставка; точка от 872 мВ; запись мимо мультиплексора и
# PLL), расхождением с cpu-volt.py или не тем итогом сценария.  Мутация,
# которую стенд не поймал, - дыра в стенде, а не в службе.  Мутация, которая
# не собралась или не применилась, ничего не доказывает и считается отказом
# самого списка.
#
#	sh mutate.sh
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
SRC=$TREE/minix/drivers/power/qcom_cpufreq
T=${TMPDIR:-/tmp}/qcom-cpufreq-mutate
rc=0 n=0 caught=0

# файл@выражение sed@что это за ошибка
while IFS='@' read -r file expr what; do
	[ -z "$file" ] && continue
	case "$file" in \#*) continue ;; esac
	n=$((n + 1))
	rm -rf "$T"; mkdir -p "$T"
	cp "$SRC"/*.c "$SRC"/*.h "$T"/
	sed -i "$expr" "$T/$file"
	if cmp -s "$SRC/$file" "$T/$file"; then
		echo "NOT APPLIED  $what"
		rc=1
		continue
	fi
	if SRC=$T OUT=$T sh "$HERE/run.sh" > "$T/out" 2>&1; then
		echo "MISSED       $what"
		rc=1
	elif grep -q 'error:' "$T/out"; then
		echo "NO BUILD     $what: $(grep -m1 'error:' "$T/out" | cut -c1-100)"
		rc=1
	elif ! grep -q 'FAIL' "$T/out"; then
		echo "CRASHED      $what: $(tail -1 "$T/out" | cut -c1-100)"
		rc=1
	else
		caught=$((caught + 1))
		echo "caught       $what: $(grep -m1 'FAIL: ' "$T/out" | sed 's/^ *FAIL: //' | cut -c1-90)"
	fi
done <<'EOF'
# The sequence (cf_clk.c).
cf_clk.c@s/^\tif (st.on_pll \&\& (r = mux(cl, CFG_AUX)) != OK)$/\tif (0 \&\& (r = mux(cl, CFG_AUX)) != OK)/@the cores not moved to aux first (the guard stops it)
cf_clk.c@s/^\tif (st.on_pll \&\& (r = mux(cl, CFG_AUX)) != OK)$/\tif (0 \&\& (r = mux(cl, CFG_AUX)) != OK)/;s/^\tif (CFG_SRC(cfg) != SRC_AUX ||$/\tif (0 \&\&/@the cores not moved to aux, and no guard
cf_clk.c@s/^\t\tif (cf_rd(\&cl->pll, PLL_STATUS) \& STATUS_LOCK)$/\t\tif (1)/@the output on without waiting for the lock
cf_clk.c@s/^\tcf_delay_us(BYPASS_US);$//@no 10 us in bypass before the reset is released
cf_clk.c@s/^#define MODE_RUN\t0x6/#define MODE_RUN\t0x4/@the reset released without BYPASSNL
cf_clk.c@0,/^\tcf_wr(\&cl->pll, PLL_MODE, MODE_OFF);$/s///@L written without the PLL off first
cf_clk.c@s/^#define CFG_EARLY\t0x501/#define CFG_EARLY\t0x301/@the cores put on the PLL's main output
cf_clk.c@s/if (!(cf_rd(\&cl->glb, RCG_CMD) \& CMD_UPDATE))/if (1)/@the mux's switch not waited for
cf_clk.c@s/^\tcf_wr(\&cl->glb, RCG_CMD, cf_rd(\&cl->glb, RCG_CMD) | CMD_UPDATE);$//@the mux's CFG written without UPDATE
cf_clk.c@s/st->khz = st->l \* CF_XO_KHZ;/st->khz = (st->l + 1) * CF_XO_KHZ;/@the PLL's rate misread
cf_clk.c@s/return l >= PLL_MIN_L \&\& l <= PLL_MAX_L ? l : 0;/return l >= 1 \&\& l <= PLL_MAX_L ? l : 0;/@an L below PLL_MIN_LVAL accepted
# The guards (cf_clk.c).
cf_clk.c@s/if (\*uv >= CF_APM_UV)/if (*uv > CF_APM_UV)/@a point at exactly 872 mV taken
cf_clk.c@s/if (\*uv > (int32_t)mv \* 1000)/if (*uv > (int32_t)mv * 1000 + 4000)/@4 mV more than the set point taken
cf_clk.c@s/if (\*uv > (int32_t)mv \* 1000)/if (*uv >= (int32_t)mv * 1000)/@a point at exactly the set point refused
cf_clk.c@s/^\tif ((r = cf_held(cf, cl, khz, 0)) != OK) {$/\tif (0) {/@the rail not read again before the switch
cf_clk.c@s/^\t\t    mux(cl, CFG_AUX) == OK)$/\t\t    0)/@no retreat to aux when the rail no longer holds the rate
cf_clk.c@s/^\tif (khz == 0)$/\tif (0)/@aux with its root off taken as a rate
cf_clk.c@s/held_at(cl, cl->c.khz\[i\], mv, \&uv) == NULL) {/(held_at(cl, cl->c.khz[i], mv, \&uv) == NULL || 1)) {/@max ignores the rail
# The arithmetic (cf_volt.c), against cpu-volt.py.
cf_volt.c@s/(raw \& (1u << (FUSE_BITS - 1))) ?/(raw \& (1u << FUSE_BITS)) ?/@the fuse's sign bit misplaced
cf_volt.c@s/^\tif (v % STEP_UV > 0)$/\tif (0)/@rounding down instead of up
cf_volt.c@s/^\t\tfv\[i\] += t->adjust_uv\[i\];$//@the tree's adjustment left out
cf_volt.c@s/^\t\tfv\[i\] = max64(fv\[i\], fv\[i - 1\]);$//@a fuse corner below the one before it kept
# Not here: interpolating up from the lower point (y1 + ...) instead of the
# kernel's down from the upper (y2 - ...).  The two differ by one uV when the
# division is not exact, and never by a 4 mV step: fuse voltages are whole
# mV, and a corner sits r/21 or r/23 of the way between two fuse corners, so
# the remainder is at least 43 uV past a whole mV.  An equivalent mutant
# (a search of 400000 random chips found none where it shows).
cf_volt.c@s/return y2 - (x2 - x) \* (y2 - y1) \/ (x2 - x1);/return y2 - (x - x1) * (y2 - y1) \/ (x2 - x1);/@interpolated the wrong way round
cf_volt.c@s/round_up(CF_APM_UV) - round_up(APM_HYST_UV));/round_up(CF_APM_UV) - round_up(APM_HYST_UV) - STEP_UV);/@the APM floor one step low
cf_volt.c@s/t->range_mv\[i\] \* 1000ll/t->range_mv[0] * 1000ll/@one floor-to-ceiling range for every corner
cf_volt.c@s/^\t\tif (flo\[i\] < flo\[i - 1\]) {$/\t\tif (0) {/@floors left decreasing
cf_volt.c@s/static const struct seg speed_bin = { 38, 29, 31 };/static const struct seg speed_bin = { 38, 28, 30 };/@the speed bin read one bit off
cf_volt.c@s/\.fmax = { 2, 3, 4, 6, 7 },/.fmax = { 2, 3, 5, 6, 7 },/@gold's fuse corners mapped to the wrong corners
cf_volt.c@s/\.adjust_uv = { -4000, 4000, 7000, 19000, -8000 },/.adjust_uv = { -4000, 4000, 7000, 19000, 8000 },/@a sign in silver's adjustment
# The rails (cf_rail.c, cf_find.c).
cf_rail.c@s/^#define REG_VSET\t0x40/#define REG_VSET\t0x68/@the upper limit read for the set point
cf_rail.c@s/if (i == 0 || v < lo)/if (i == 0 || v > lo)/@gold's set point the higher of S2 and S3
cf_rail.c@s/if (!(en \& EN_CTL_ENABLE)) {/if (0) {/@a disabled regulator believed
cf_rail.c@s/if (t\[0\] != FTS426_TYPE || t\[1\] != FTS426_SUBTYPE) {/if (0) {/@a regulator of another type read as an FTS426
cf_rail.c@s/spmi_read(s, r->sid, r->base + REG_EN_CTL, \&en, 1)/spmi_write(s, r->sid, r->base + REG_EN_CTL, \&en, 1)/@a write to the PMIC
cf_find.c@s/^\tcl->nrail = 2;/\tcl->nrail = 1;/@gold's second phase not read
cf_find.c@s/(struct cf_rail){ "S2", PM660_SID_REGS, 0x1700 }/(struct cf_rail){ "S2", PM660_SID_REGS, 0x1400 }/@gold's rail read at S1
# The blocks (cf_find.c).
cf_find.c@s/map_named(\&cf->cl\[CF_SILVER\].pll, \&lk, "silver-pll")/map_named(\&cf->cl[CF_SILVER].pll, \&lk, "gold-pll")/@silver's PLL taken for gold's
cf_find.c@s/^#define FUSE_ROWS_OFF\t\t0x4000/#define FUSE_ROWS_OFF\t\t0x4008/@the fuse rows read one row off
cf_find.c@s/cf->aux_khz = d <= 1 ? GPLL0_KHZ : GPLL0_KHZ \* 2 \/ (d + 1);/cf->aux_khz = d <= 1 ? GPLL0_KHZ : GPLL0_KHZ;/@aux's divider ignored
cf_find.c@s/if ((cmd \& RCG_ROOT_OFF) || ((cfg >> 8) \& 7) != GCC_SRC_GPLL0) {/if (((cfg >> 8) \& 7) != GCC_SRC_GPLL0) {/@aux's root not looked at
# The stages (cf_run.c).
cf_run.c@s/if (cl->c.khz\[i\] > from \&\& cl->c.khz\[i\] < khz \&\&/if (0 \&\&/@no steps on the way up
cf_run.c@s/(!has_frac || c->khz\[i\] % 1000 \/ 100 == frac)/(!has_frac || frac < 10)/@the decimal of a target ignored
cf_run.c@s/if (khz != 0 \&\& khz != from \&\& (r = cf_held(cf, cl, khz, 0)) != OK)/if (0)/@a target the rail does not hold climbed towards
cf_run.c@s/^\tif (stage == CF_PROBE)$/\tif (0)/@probe goes on to set
EOF
echo "$caught of $n caught"
exit $rc
