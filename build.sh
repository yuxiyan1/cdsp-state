#!/bin/sh
# Cross-compile cdsp_state for arm64 Android with the NDK, then push it to the device.
#   ./build.sh                       build + push
#   ./build.sh run [args]            build + push, then run cdsp_state on the device with args
#                                    (e.g. ./build.sh run sleep, ./build.sh run --watch -i 5 -t 30)
#   ./build.sh --ndk PATH --serial SN run ...   same, with the NDK and device given on the command line
#   NDK        Android NDK path (or --ndk PATH). If unset it is looked for, in this order:
#              ANDROID_NDK_HOME/ROOT, ANDROID_SDK_ROOT/ndk, ndk-build in PATH, the cache of the last
#              search, then a search of NDK_SEARCH (default: all of /, at most 2 minutes)
#   SERIAL     adb device serial, needed only when more than one device is connected
set -e
DEST=/data/local/tmp/cdsp_state
DIR=$(cd "$(dirname "$0")" && pwd)
COMPILER=aarch64-linux-android31-clang++
CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/cdsp_state_ndk

# Options in front of "run" override the NDK / SERIAL environment variables
while [ $# -gt 0 ]; do
	case $1 in
	--ndk) NDK=${2:?--ndk needs a path}; shift 2 ;;
	--serial) SERIAL=${2:?--serial needs a device serial}; shift 2 ;;
	*) break ;;
	esac
done

# $1 is an NDK if it has the compiler
is_ndk() {
	ls "$1"/toolchains/llvm/prebuilt/*/bin/$COMPILER >/dev/null 2>&1
}

# Print the newest NDK of the directories on stdin
newest_ndk() {
	while read -r d; do
		is_ndk "$d" && echo "$d"
	done | sort -V | tail -n 1
}

find_ndk() {
	d=$( { echo "$ANDROID_NDK_HOME"; echo "$ANDROID_NDK_ROOT"
	       ls -d "$ANDROID_SDK_ROOT"/ndk/* "$ANDROID_HOME"/ndk/* 2>/dev/null
	       command -v ndk-build >/dev/null 2>&1 && dirname "$(command -v ndk-build)"
	       cat "$CACHE" 2>/dev/null; } | newest_ndk)
	if [ -n "$d" ]; then
		echo "$d"
		return
	fi
	echo "searching for an NDK, can take a while..." >&2
	# skip pseudo file systems and the big source trees
	for root in ${NDK_SEARCH:-/}; do
		timeout 120 find "$root" -maxdepth 8 \
			\( -path /proc -o -path /sys -o -path /dev -o -path /run -o -name .git -o -name .repo -o -name out \) -prune -o \
			-type d -name 'android-ndk*' -print 2>/dev/null || true
	done | newest_ndk
}

if [ -z "$NDK" ]; then
	NDK=$(find_ndk)
	if [ -n "$NDK" ]; then
		mkdir -p "$(dirname "$CACHE")" && echo "$NDK" > "$CACHE"
	fi
fi
[ -n "$NDK" ] && is_ndk "$NDK" || {
	echo "no NDK with $COMPILER found, set NDK=<path> (or NDK_SEARCH=\"<dirs to search>\")" >&2
	exit 1
}
echo "NDK $NDK"
CXX=$(ls "$NDK"/toolchains/llvm/prebuilt/*/bin/$COMPILER | head -n 1)

if [ -z "$SERIAL" ]; then
	devices=$(adb devices | awk 'NR > 1 && $2 == "device" { print $1 }')
	case $(echo "$devices" | wc -w) in
	1) SERIAL=$devices ;;
	0) echo "no adb device connected" >&2; exit 1 ;;
	*) echo "more than one adb device, set SERIAL to one of:" >&2; echo "$devices" >&2; exit 1 ;;
	esac
fi

"$CXX" -std=c++17 -O2 -Wall -static-libstdc++ -I"$DIR/src" -I"$DIR/kernel" \
	"$DIR/src/cdsp_state.cpp" -llog -o "$DIR/cdsp_state"
echo "built $DIR/cdsp_state"

echo "device $SERIAL"
adb -s "$SERIAL" root
adb -s "$SERIAL" push "$DIR/cdsp_state" "$DEST"
adb -s "$SERIAL" shell chmod +x "$DEST"

if [ "$1" = run ]; then
	shift
	adb -s "$SERIAL" shell "$DEST" "$@"
fi
