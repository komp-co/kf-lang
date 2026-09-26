#!/usr/bin/env sh
# kflat_version(), the release a tree becomes. The file is found by what it
# defines, so moving it breaks no release.
#
#   scripts/release-version.sh                  the working tree's
#   scripts/release-version.sh REF              a commit's; nothing, and exit 0,
#                                               for one that predates it
#   scripts/release-version.sh --next PART      the working tree's, with PART
#                                               (major, minor, patch) raised
#   scripts/release-version.sh --set X.Y.Z      rewrite the working tree's
set -eu

cd "$(dirname "$0")/.."

defining_file() {
    git grep -l "fun kflat_version" ${1:+"$1"} -- compiler | head -1
}

version_in() {
    sed -n 's/^ *return "\(.*\)"$/\1/p'
}

case "${1:-}" in
    "")
        version_in < "$(defining_file)"
        ;;
    --next)
        current=$(version_in < "$(defining_file)")
        major=${current%%.*}
        rest=${current#*.}
        minor=${rest%%.*}
        patch=${rest#*.}
        case "${2:-}" in
            major) echo "$((major + 1)).0.0" ;;
            minor) echo "$major.$((minor + 1)).0" ;;
            patch) echo "$major.$minor.$((patch + 1))" ;;
            *) echo "usage: $0 --next major|minor|patch" >&2; exit 2 ;;
        esac
        ;;
    --set)
        case "${2:-}" in
            [0-9]*.[0-9]*.[0-9]*) ;;
            *) echo "usage: $0 --set X.Y.Z" >&2; exit 2 ;;
        esac
        file=$(defining_file)
        sed "s/^\( *return \"\).*\"$/\1$2\"/" "$file" > "$file.tmp"
        mv "$file.tmp" "$file"
        ;;
    *)
        file=$(defining_file "$1")
        if [ -n "$file" ]; then git show "$file" | version_in; fi
        ;;
esac
