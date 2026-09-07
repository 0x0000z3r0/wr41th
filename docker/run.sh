#!/bin/sh
set -e

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
IMG=${WR_IMG:-wr41th-build}
TGT=${1:-all}
KVER=$(uname -r)

docker build -t "$IMG" "$ROOT/docker"

set -- docker run --rm -v "$ROOT:/src" -w /src

if [ -d "/lib/modules/$KVER/build" ]; then
	set -- "$@" -v "/lib/modules/$KVER:/lib/modules/$KVER:ro"
	if [ -d /usr/src ]; then
		set -- "$@" -v /usr/src:/usr/src:ro
	fi
	set -- "$@" -e "KDIR=/lib/modules/$KVER/build"
fi

if command -v pkg-config >/dev/null && pkg-config --exists r_core; then
	set -- "$@" -e "R2LIBDIR=$(pkg-config --variable=libdir r_core)"
fi

set -- "$@" "$IMG" make "in-$TGT"
exec "$@"
