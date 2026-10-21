#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Host tests of the video modules (no hardware): the JPEG encoder against an independent decoder.
#   sh tests/run_video_tests.sh
set -e
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p "$OUT"
CCBIN="${CC:-cc}"
fail=0
echo "== vjpeg: the video core's JPEG encoder"
$CCBIN -O2 -Wall -Wno-unused-function -o "$OUT/vjpeg_test" tests/vjpeg_test.c || fail=1
"$OUT/vjpeg_test" "$OUT" || fail=1
"${PYTHON:-python3}" tests/vjpeg_check.py "$OUT" || fail=1
echo "== vjpeg under ASan + UBSan (no read past a frame, no overflow)"
$CCBIN -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wno-unused-function -o "$OUT/vjpeg_asan" tests/vjpeg_test.c || fail=1
"$OUT/vjpeg_asan" "$OUT" >/dev/null || fail=1
echo "== vrender: the webcam's pictures (a synthetic mix through every style; the sheet: build/host/vrender_sheet.png)"
mkdir -p build/gen && "${PYTHON:-python3}" tools/gen_mirror.py build/gen/vmirror.h >/dev/null || fail=1
$CCBIN -O2 -Wall -Wno-unused-function -Ibuild/gen -o "$OUT/vrender_test" tests/vrender_test.c || fail=1
"$OUT/vrender_test" "$OUT" || fail=1
"${PYTHON:-python3}" tests/vrender_sheet.py "$OUT" || fail=1
$CCBIN -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wno-unused-function -Ibuild/gen -o "$OUT/vrender_asan" tests/vrender_test.c || fail=1
"$OUT/vrender_asan" "$OUT" >/dev/null || fail=1
echo "== uvc: the USB video function's descriptors, probe block and packetizer"
mkdir -p build/gen
"${PYTHON:-python3}" tools/gen_testcard.py build/gen/uvc_testcard.h || fail=1
$CCBIN -O1 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o "$OUT/uvc_test" tests/uvc_test.c || fail=1
"$OUT/uvc_test" || fail=1
$CCBIN -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=all -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o "$OUT/uvc_asan" tests/uvc_test.c || fail=1
"$OUT/uvc_asan" >/dev/null || fail=1
echo "== uvc + uac: the combined build (audio input on EP3, video on EP4)"
$CCBIN -O1 -w -DT_UAC=1 -DHALF_FRAMES=256 -Ibuild/gen -Ifirmware/src -Ifirmware/hal -o "$OUT/uvc_uac_test" tests/uvc_test.c || fail=1
"$OUT/uvc_uac_test" || fail=1
[ "$fail" = 0 ] && echo "VIDEO HOST TESTS PASSED" || { echo "VIDEO HOST TESTS FAILED"; exit 1; }
