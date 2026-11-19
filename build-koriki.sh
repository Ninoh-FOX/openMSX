#!/bin/sh
# Compila openMSX 17.0 para Koriki (Miyoo Mini) y copia el binario a ../openmsx-koriki-dist
set -e
cd "$(dirname "$0")"
K=$(pwd)
DIST=$K/../openmsx-koriki-dist/App/openmsx

export PATH=$K/derived/koriki-deps/bin:/opt/miyoomini-toolchain/bin:$PATH
export TCL_CONFIG=$K/derived/koriki-deps/install/lib

make -j$(nproc) \
	OPENMSX_TARGET_CPU=arm OPENMSX_TARGET_OS=linux OPENMSX_FLAVOUR=opt \
	CXX=arm-linux-gnueabihf-g++ LINK_MODE=SYS_DYN_KORIKI \
	LDFLAGS="-lmi_ao -lmi_common -lmi_sys" \
	CXXFLAGS="-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -DOPENMSX_AUDIO_S16=1 -DOPENMSX_KORIKI=1" \
	"$@"

cp derived/arm-linux-opt/bin/openmsx "$DIST/openmsx"
echo "Binario copiado a $DIST/openmsx"
