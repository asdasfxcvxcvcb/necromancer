#!/bin/bash
set -e
export MSYS2_ARG_CONV_EXCL="*"
MSVCBIN="D:/vis/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64"
VER=10.0.26100.0
export INCLUDE='D:\vis\VC\Tools\MSVC\14.44.35207\include;C:\Program Files (x86)\Windows Kits\10\Include\'$VER'\ucrt;C:\Program Files (x86)\Windows Kits\10\Include\'$VER'\um;C:\Program Files (x86)\Windows Kits\10\Include\'$VER'\shared'
export LIB='D:\vis\VC\Tools\MSVC\14.44.35207\lib\x64;C:\Program Files (x86)\Windows Kits\10\Lib\'$VER'\ucrt\x64;C:\Program Files (x86)\Windows Kits\10\Lib\'$VER'\um\x64'
mkdir -p obj
DEFS="-DOPUS_BUILD -DUSE_ALLOCA -DHAVE_STDINT_H -DHAVE_SYS_TYPES_H -D_CRT_SECURE_NO_WARNINGS"
SRCS="src/analysis.c src/extensions.c src/mapping_matrix.c src/mlp.c src/mlp_data.c src/opus.c src/opus_decoder.c src/opus_encoder.c src/opus_multistream.c src/opus_multistream_decoder.c src/opus_multistream_encoder.c src/opus_projection_decoder.c src/opus_projection_encoder.c src/repacketizer.c $(ls celt/*.c) $(ls silk/*.c) $(ls silk/float/*.c)"
for f in $SRCS; do
  base=$(basename "$f" .c)
  if [ ! -f "obj/$base.obj" ]; then
    "$MSVCBIN/cl.exe" -c -nologo -MT -O2 -GS- -fp:fast $DEFS -Iinclude -Icelt -Isilk -Isilk/float -Isrc -Fo"obj/$base.obj" "$f" || exit 1
  fi
done
rm -f opus.lib
"$MSVCBIN/lib.exe" -nologo -OUT:opus.lib obj/*.obj
echo BUILD_DONE
ls -la opus.lib
