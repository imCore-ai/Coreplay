#!/bin/bash
# Makes the test media for the end-to-end scripts (needs ffmpeg and a test build,
# which provides mkexr). Usage: make_media.sh <media-dir>
set -e
M="${1:?media folder}"
HERE="$(cd "$(dirname "$0")" && pwd)"
MKEXR="$HERE/../../build/mkexr.exe"
FONT="C\\:/Windows/Fonts/arial.ttf"   # drawtext needs a font file on Windows
FF="ffmpeg -hide_banner -loglevel error -y -threads 2"
DT="drawtext=fontfile='$FONT':text='%{frame_num}':start_number=1001:fontsize=120:fontcolor=white:x=60:y=60:box=1:boxcolor=black@0.6"
mkdir -p "$M"/{exr_seq,exr_fps,exr4k,tif_seq,png_seq}

$FF -f lavfi -i "testsrc2=size=1920x1080:rate=24000/1001:duration=10" -f lavfi -i "sine=frequency=440:duration=10" \
    -vf "$DT" -c:v libx264 -preset veryfast -pix_fmt yuv420p -c:a aac "$M/h264_23976.mp4"
$FF -f lavfi -i "testsrc2=size=1920x1080:rate=25:duration=6" -f lavfi -i "sine=frequency=660:duration=6" \
    -vf "$DT" -c:v prores_ks -profile:v 3 -c:a pcm_s16le "$M/prores_25.mov"
# 32-bit float EXR, 120 frames (ffmpeg writes framesPerSecond 24/1)
$FF -f lavfi -i "testsrc2=size=1920x1080:rate=24:duration=5" -vf "$DT" -pix_fmt gbrpf32le -c:v exr -compression zip1 \
    -start_number 1001 "$M/exr_seq/shot_v001.%04d.exr"
# 4K half-float EXR, 72 frames
$FF -f lavfi -i "testsrc2=size=4096x2160:rate=24:duration=3" -vf "$DT" -pix_fmt gbrpf32le -c:v exr -compression zip1 \
    -format half -start_number 1001 "$M/exr4k/plate4k.%04d.exr"
# 16-bit TIFF, 48 frames
$FF -f lavfi -i "testsrc2=size=1920x1080:rate=24:duration=2" -vf "$DT" -pix_fmt rgb48le -start_number 1001 \
    "$M/tif_seq/plate.%04d.tif"
# 8-bit PNG, frames 1-48 with 10 and 11 missing
$FF -f lavfi -i "testsrc2=size=1280x720:rate=24:duration=2" -vf "$DT" -start_number 1 "$M/png_seq/comp.%04d.png"
rm -f "$M/png_seq/comp.0010.png" "$M/png_seq/comp.0011.png"
# Half-float PIZ EXR with framesPerSecond 30000/1001 and a data window inside the display window
"$MKEXR" "$M/exr_fps/bar.%04d.exr" 1001 24 30000 1001
# LUT patches: a 2-frame PNG sequence and the same picture as a Rec.709-tagged 4:4:4 movie
mkdir -p "$M/patch_seq"
python -I "$HERE/make_patches.py" "$M/patch_seq/patch.0001.png"
cp "$M/patch_seq/patch.0001.png" "$M/patch_seq/patch.0002.png"
$FF -loop 1 -i "$M/patch_seq/patch.0001.png" -t 2 -r 24 -vf "scale=out_color_matrix=bt709:out_range=tv"     -c:v libx264 -preset veryfast -crf 4 -pix_fmt yuv444p -colorspace bt709 -color_primaries bt709     -color_trc bt709 -color_range tv "$M/patches_709.mp4"
# A 2-point inverting 3D LUT for the LUT tests
printf 'TITLE "invert"
LUT_3D_SIZE 2
1 1 1
0 1 1
1 0 1
0 0 1
1 1 0
0 1 0
1 0 0
0 0 0
' > "$M/invert.cube"
echo "media ready in $M"

# Two 3 s clips, same format, for the gapless playlist test (t19)
for c in a b; do
  $FF -f lavfi -i "testsrc2=size=1280x720:rate=24000/1001:duration=3" -f lavfi       -i "sine=frequency=$([ $c = a ] && echo 440 || echo 660):duration=3:sample_rate=48000"       -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest "$M/gap_$c.mp4"
done

# Solid red and blue clips (sequence + movie) for the A/B compare test (t22)
for c in red:0xC02020 blue:0x2020C0; do
  n=${c%%:*}; v=${c#*:}
  mkdir -p "$M/cmp_$n"
  $FF -f lavfi -i "color=c=$v:s=640x360:r=24:d=1" -start_number 1 "$M/cmp_$n/$n.%04d.png"
  $FF -f lavfi -i "color=c=$v:s=640x360:r=24:d=2" -c:v libx264 -pix_fmt yuv420p "$M/cmp_$n.mp4"
done
