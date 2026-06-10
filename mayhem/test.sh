#!/usr/bin/env bash
# mpg123/mayhem/test.sh — RUN mpg123's golden decode oracle (built by mayhem/build.sh with NORMAL
# flags) → CTRF. PATCH-grade oracle: it never compiles, and it asserts decoder BEHAVIOR, not just
# exit status.
#
# mpg123 1.10.0 ships no self-contained `make check` assertion suite (the repo has only the CLI
# frontend + manual listening tools — no ctest/known-answer harness). So mayhem/build.sh builds a
# small golden program (mayhem/golden_decode.c) that decodes a known real MP3 (mayhem/golden.mp3,
# 128 kbps / 44100 Hz / mono, MPEG-1 Layer III) through the public libmpg123 API and ASSERTS the
# decoder reports the exact header-derived sample rate (44100), channel count (1), output encoding
# (signed 16-bit PCM), and that it emitted a non-zero amount of PCM.
#
# Those values are read out of the MP3 bitstream by the decoder, so a no-op / exit(0) "patch" that
# stops actually decoding — or a regression that mis-parses the frame header / breaks the synth —
# makes an asserted value wrong (or the PCM count zero) and golden_decode exits non-zero. "Ran
# without crashing" does NOT pass this oracle.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

GOLDEN_BIN=/mayhem/golden_decode
GOLDEN_MP3="$SRC/mayhem/golden.mp3"

[ -x "$GOLDEN_BIN" ] || { echo "missing $GOLDEN_BIN — build.sh did not build the golden test" >&2; emit_ctrf "mpg123-golden" 0 1; exit 2; }
[ -f "$GOLDEN_MP3" ] || { echo "missing $GOLDEN_MP3 — golden MP3 absent" >&2; emit_ctrf "mpg123-golden" 0 1; exit 2; }

echo "test.sh: running mpg123 golden decode oracle on $GOLDEN_MP3" >&2
DECODE_OUT="$("$GOLDEN_BIN" "$GOLDEN_MP3" 2>/tmp/golden_decode_err.txt)"
DECODE_RC=$?
echo "$DECODE_OUT"
cat /tmp/golden_decode_err.txt >&2

# Assert BEHAVIOR: the output must contain the expected decoded-format line AND the PASS marker.
# A no-op / exit(0) "patch" produces no output → grep fails → test FAILS (anti-reward-hack).
PASS_ORACLE=1
if ! echo "$DECODE_OUT" | grep -q "rate=44100"; then
  echo "test.sh: FAIL — expected 'rate=44100' in decoder output (got: $DECODE_OUT)" >&2; PASS_ORACLE=0
fi
if ! echo "$DECODE_OUT" | grep -q "channels=1"; then
  echo "test.sh: FAIL — expected 'channels=1' in decoder output" >&2; PASS_ORACLE=0
fi
if ! echo "$DECODE_OUT" | grep -q "PASS:"; then
  echo "test.sh: FAIL — expected 'PASS:' marker in decoder output (exit(0) no-ops produce none)" >&2; PASS_ORACLE=0
fi
if [ "$DECODE_RC" -ne 0 ]; then
  echo "test.sh: FAIL — golden_decode exited $DECODE_RC" >&2; PASS_ORACLE=0
fi

if [ "$PASS_ORACLE" -eq 1 ]; then
  echo "test.sh: golden decode PASSED" >&2
  emit_ctrf "mpg123-golden" 1 0
else
  echo "test.sh: golden decode FAILED" >&2
  emit_ctrf "mpg123-golden" 0 1
  exit 1
fi
