#!/bin/sh
# the webcam's rate: frames the device sent against frames the host received in 5 s (ffmpeg, no files)
cd "$(dirname "$0")/.."
a=$(.venv/bin/python tools/fm1_vcdiag.py | awk '/uvc.frames/ {print $2}')
got=$(ffmpeg -hide_banner -loglevel info -f avfoundation -framerate 30 -video_size 320x180 -pixel_format uyvy422 -i Synthesthetic -t 5 -fps_mode passthrough -f null - 2>&1 | grep -oE "frame= *[0-9]+" | tail -1 | grep -oE "[0-9]+")
b=$(.venv/bin/python tools/fm1_vcdiag.py | awk '/uvc.frames/ {print $2}')
echo "sent $((b - a)) frames, host got $got, in ~5 s"
