#!/usr/bin/env sh
# Installs the kflat release this script ships in: builds kflatc with cc and
# puts it, with the libraries it compiles against, in
# $KFLAT_HOME/toolchains/<version> ($KFLAT_HOME is ~/.kflat unless set).
# kflatc is linked into $KFLAT_HOME/bin, beside komp, which runs the kflatc
# next to it when a project pins no other toolchain.
#
#   sh install.sh            # CC and CFLAGS pick the C compiler and flags
#
# Installing a version again replaces it. Other versions are left alone.
set -eu

here="$(cd "$(dirname "$0")" && pwd)"
version="$(cat "$here/VERSION")"
home="${KFLAT_HOME:-$HOME/.kflat}"
toolchain="$home/toolchains/$version"
staging="$toolchain.tmp.$$"
CC="${CC:-cc}"
CFLAGS="${CFLAGS:--O2}"

fail() {
    echo "FAIL: $*" >&2
    rm -rf "$staging"
    exit 1
}

command -v "$CC" > /dev/null 2>&1 || fail "no C compiler: install gcc or clang, or name one with CC"
rm -rf "$staging"
mkdir -p "$staging/bin"
echo "building kflatc $version with $CC" >&2
"$CC" $CFLAGS -o "$staging/bin/kflatc" "$here/kflatc.c" || fail "$CC could not build kflatc"
cp -R "$here/libs" "$staging/libs" || fail "could not copy the libraries into $staging"

rm -rf "$toolchain"
mv "$staging" "$toolchain"
mkdir -p "$home/bin"
ln -sf "$toolchain/bin/kflatc" "$home/bin/kflatc"

echo "installed kflat $version in $toolchain"
case ":${PATH:-}:" in
    *":$home/bin:"*) ;;
    *) echo "add $home/bin to PATH, for example in your shell profile:"
       echo "    export PATH=\"$home/bin:\$PATH\"" ;;
esac
