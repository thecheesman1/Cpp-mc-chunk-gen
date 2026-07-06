#!/usr/bin/env bash
#===============================================================================
# benchmark.sh — Run chunkgen_offline N times and average the results
#
# Usage:
#   ./benchmark.sh [--times N] [chunkgen_offline args...]
#
# Examples:
#   ./benchmark.sh --radius 64 --threads 4
#   ./benchmark.sh --times 5 --radius 128 --threads 4 --vulkan
#   ./benchmark.sh --times 3 --seed 12345 --radius 32 --threads 2
#===============================================================================
set -euo pipefail

BIN="./chunkgen_offline"
TIMES=4
BENCH_ARGS=()

# ---- Parse --times before forwarding rest to chunkgen_offline ----
while [[ $# -gt 0 ]]; do
    case "$1" in
        --times)
            if [[ -z "${2:-}" || ! "$2" =~ ^[0-9]+$ ]]; then
                echo "[BENCH] ERROR: --times requires a number"
                exit 1
            fi
            TIMES="$2"
            shift 2
            ;;
        --times=*)
            TIMES="${1#*=}"
            if [[ ! "$TIMES" =~ ^[0-9]+$ ]]; then
                echo "[BENCH] ERROR: --times requires a number"
                exit 1
            fi
            shift
            ;;
        *)
            # Don't forward --quiet — we need the Done! line for parsing
            if [[ "$1" != "--quiet" ]]; then
                BENCH_ARGS+=("$1")
            fi
            shift
            ;;
    esac
done

# ---- Check binary exists ----
if [[ ! -x "$BIN" ]]; then
    echo "[BENCH] ERROR: $BIN not found. Run 'make chunkgen_offline' first."
    exit 1
fi

# ---- Extract key params for display ----
SEED="?"
RADIUS="?"
THREADS="?"
VULKAN=""
for ((i = 0; i < ${#BENCH_ARGS[@]}; i++)); do
    case "${BENCH_ARGS[$i]}" in
        --seed)   SEED="${BENCH_ARGS[$((i+1))]:-?}" ;;
        --radius) RADIUS="${BENCH_ARGS[$((i+1))]:-?}" ;;
        --threads) THREADS="${BENCH_ARGS[$((i+1))]:-?}" ;;
        --vulkan) VULKAN=" + Vulkan" ;;
    esac
done

echo "=============================================="
echo "  McChunkGen Benchmark"
echo "  Runs  : $TIMES"
echo "  Args  :${VULKAN} --radius $RADIUS --seed $SEED --threads $THREADS"
echo "=============================================="
echo ""

RESULTS=()
TEMPDIR_BASE="/tmp/mcchunkgen_bench"

for ((i = 1; i <= TIMES; i++)); do
    WORLDDIR="${TEMPDIR_BASE}/run_${i}/world"
    mkdir -p "${WORLDDIR}/region"

    # Run the benchmark, capture everything
    OUTPUT=$("$BIN" --world "$WORLDDIR" "${BENCH_ARGS[@]}" 2>&1 || true)

    # Parse "Done!" line — format: [McChunkGen] Done! N chunks in X.Xs (Y CPS)
    CPS=$(echo "$OUTPUT" | grep "Done!" | grep -oP '\(\K[0-9]+(?= CPS)' || echo "")
    TIME_S=$(echo "$OUTPUT" | grep "Done!" | grep -oP 'in \K[0-9.]+(?=s\s*\()' || echo "")

    if [[ -z "$CPS" ]]; then
        echo "[BENCH] Run $i: FAILED — couldn't parse CPS"
        echo "$OUTPUT" | tail -5
        RESULTS+=("")
        continue
    fi

    RESULTS+=("$CPS")
    printf "  [Run %2d/%d]  %s CPS  (%s seconds)\n" "$i" "$TIMES" "$CPS" "$TIME_S"

    # Clean up — delete region files (not the whole dir, reuse structure is fine)
    rm -rf "$WORLDDIR"
done

echo ""
echo "----------------------------------------------"

# ---- Compute stats ----
VALID=()
for cps in "${RESULTS[@]}"; do
    if [[ -n "$cps" ]]; then
        VALID+=("$cps")
    fi
done

if [[ ${#VALID[@]} -eq 0 ]]; then
    echo "[BENCH] No valid runs to average."
    exit 1
fi

# Sort for min/max/median (convert to lines for sorting)
SORTED_TMP=""
for cps in "${VALID[@]}"; do
    SORTED_TMP="$SORTED_TMP$cps"$'\n'
done
SORTED=()
while IFS= read -r line; do
    [[ -n "$line" ]] && SORTED+=("$line")
done < <(sort -n <<< "$SORTED_TMP")

MIN=${SORTED[0]}
MAX=${SORTED[${#SORTED[@]}-1]}

# Sum
SUM=0
for cps in "${VALID[@]}"; do
    SUM=$((SUM + cps))
done
AVG=$((SUM / ${#VALID[@]}))

# Median
if (( ${#SORTED[@]} % 2 == 1 )); then
    MEDIAN=${SORTED[${#SORTED[@]}/2]}
else
    MID=${#SORTED[@]}/2
    MEDIAN=$(( (${SORTED[$MID-1]} + ${SORTED[$MID]}) / 2 ))
fi

# Stddev
DEVSUM=0
for cps in "${VALID[@]}"; do
    D=$((cps - AVG))
    DEVSUM=$((DEVSUM + D * D))
done
STDDEV=$(echo "scale=0; sqrt($DEVSUM / ${#VALID[@]})" | bc -l 2>/dev/null || echo "?")

printf "  Runs     : %s\n" "${#VALID[@]}"
printf "  Avg      : %s CPS\n" "$AVG"
printf "  Median   : %s CPS\n" "$MEDIAN"
printf "  Min      : %s CPS\n" "$MIN"
printf "  Max      : %s CPS\n" "$MAX"
[[ "$STDDEV" != "?" ]] && printf "  Stddev   : ±%s CPS\n" "$STDDEV" || printf "  Stddev   : ? (install bc)\n"
printf "  Range    : %s - %s CPS\n" "$MIN" "$MAX"
echo "----------------------------------------------"