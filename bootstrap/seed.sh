# The bootstrap seed: a released kflatc, as C, and the komp that drives the
# builds, both pinned by bootstrap/stage0.toml. Sourced by bootstrap/build.sh
# and scripts/*.sh.
#
#   seed_build DIR    leaves DIR/komp0, the driver, and DIR/kflatc, the seed
#
# The driver is the seed's own komp.c when it ships one, as the releases made
# before komp had a repository of its own do; otherwise the komp binary that
# `komp_url` names, checked against `komp_sha256`.
#
# The pinned release is fetched once into the cache and verified against its
# sha256 on every use. `KFLAT_SEED=<path to a seed tarball>` uses that one
# instead, unverified: that is how a seed built by `bootstrap/build.sh
# --seed-out` is tried before it is released.

SEED_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SEED_CACHE="${KFLAT_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/kflat}/seeds"

seed_field() {
    sed -n "s/^$1 *= *\"\(.*\)\"/\1/p" "$SEED_ROOT/bootstrap/stage0.toml"
}

seed_sha256() {
    if command -v sha256sum > /dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

# Prints the path of a verified seed tarball, downloading it if needed.
seed_tarball() {
    if [ -n "${KFLAT_SEED:-}" ]; then
        [ -f "$KFLAT_SEED" ] || { echo "FAIL: KFLAT_SEED names no file: $KFLAT_SEED" >&2; return 1; }
        echo "NOTE: using the seed at KFLAT_SEED, not the pinned release" >&2
        echo "$KFLAT_SEED"
        return 0
    fi
    seed_version="$(seed_field version)"
    seed_url="$(seed_field url)"
    seed_want="$(seed_field sha256)"
    seed_file="$SEED_CACHE/kflat-seed-$seed_version.tar.gz"
    if [ ! -f "$seed_file" ] || [ "$(seed_sha256 "$seed_file")" != "$seed_want" ]; then
        mkdir -p "$SEED_CACHE"
        echo "fetching the seed, kflat $seed_version" >&2
        if command -v curl > /dev/null 2>&1; then
            curl -fsSL -o "$seed_file.part" "$seed_url" || { echo "FAIL: could not download $seed_url" >&2; return 1; }
        else
            wget -q -O "$seed_file.part" "$seed_url" || { echo "FAIL: could not download $seed_url" >&2; return 1; }
        fi
        mv -f "$seed_file.part" "$seed_file"
    fi
    seed_got="$(seed_sha256 "$seed_file")"
    if [ "$seed_got" != "$seed_want" ]; then
        echo "FAIL: the seed does not match bootstrap/stage0.toml" >&2
        echo "  $seed_file" >&2
        echo "  sha256 $seed_got, expected $seed_want" >&2
        return 1
    fi
    echo "$seed_file"
}

# Downloads `$1` to `$2` unless it is there with sha256 `$3`, then checks it.
fetch_verified() {
    if [ ! -f "$2" ] || [ "$(seed_sha256 "$2")" != "$3" ]; then
        mkdir -p "$(dirname "$2")"
        if command -v curl > /dev/null 2>&1; then
            curl -fsSL -o "$2.part" "$1" || { echo "FAIL: could not download $1" >&2; return 1; }
        else
            wget -q -O "$2.part" "$1" || { echo "FAIL: could not download $1" >&2; return 1; }
        fi
        mv -f "$2.part" "$2"
    fi
    if [ "$(seed_sha256 "$2")" != "$3" ]; then
        echo "FAIL: $2 does not match bootstrap/stage0.toml" >&2
        echo "  sha256 $(seed_sha256 "$2"), expected $3" >&2
        return 1
    fi
}

# Compiles the seed's kflatc into DIR, beside the driver komp, which runs the
# kflatc beside it.
seed_build() {
    seed_file="$(seed_tarball)" || return 1
    mkdir -p "$1/seed"
    tar -xzf "$seed_file" -C "$1/seed" --strip-components=1 || return 1
    # Compile, then link: ccache caches a `-c` compile, never one that also links.
    # From inside the directory, so its random name stays out of the hashed command line.
    (cd "$1/seed" && "${CC:-cc}" ${CFLAGS:--O2} -c -o kflatc.o kflatc.c) || return 1
    "${CC:-cc}" ${CFLAGS:--O2} -o "$1/kflatc" "$1/seed/kflatc.o" || return 1
    if [ -f "$1/seed/komp.c" ]; then
        (cd "$1/seed" && "${CC:-cc}" ${CFLAGS:--O2} -c -o komp.o komp.c) || return 1
        "${CC:-cc}" ${CFLAGS:--O2} -o "$1/komp0" "$1/seed/komp.o" || return 1
        return 0
    fi
    komp_url="$(seed_field komp_url)"
    [ -n "$komp_url" ] || { echo "FAIL: the seed ships no komp.c and stage0.toml names no komp_url" >&2; return 1; }
    komp_archive="$SEED_CACHE/$(basename "$komp_url")"
    fetch_verified "$komp_url" "$komp_archive" "$(seed_field komp_sha256)" || return 1
    mkdir -p "$1/driver"
    tar -xzf "$komp_archive" -C "$1/driver" --strip-components=1 || return 1
    cp "$1/driver/bin/komp" "$1/komp0" || { echo "FAIL: $komp_url holds no bin/komp" >&2; return 1; }
}
