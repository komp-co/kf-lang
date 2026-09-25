#!/usr/bin/env sh
# What a change touches, so CI can pick which jobs it needs:
#
#   docs       only Markdown
#   comments   only Markdown, plus whole-line `//` comments or blank lines in .kf
#   code       anything else
#
#   scripts/ci-classify.sh BASE HEAD
#
# A `//` line inside a `"""` string is string content, not a comment, and
# makes the change `code`.
set -eu

files=$(git diff --name-only "$1" "$2")
if [ -z "$files" ]; then echo code; exit 0; fi

kind=docs
for f in $files; do
    case "$f" in
        *.md) ;;
        *.kf) kind=comments ;;
        *) echo code; exit 0 ;;
    esac
done

# Exits non-zero when a hunk of `$3` touches a line that the BASE version has
# inside a triple-quoted string.
strings_untouched() {
    in_string=$(mktemp)
    git show "$1:$3" 2>/dev/null | awk '
        { if (!open && $0 ~ /^[ \t]*\/\//) next
          n = gsub(/"""/, "&")
          if (open) print NR
          if (n % 2 == 1) { open = !open; if (open) print NR } }' > "$in_string" || true
    git diff -U0 "$1" "$2" -- "$3" | awk -v lines="$in_string" '
        BEGIN { while ((getline l < lines) > 0) s[l] = 1 }
        /^@@ / {
            split($2, r, ",")
            start = substr(r[1], 2) + 0
            count = (r[2] == "") ? 1 : r[2] + 0
            if (count == 0 && s[start] && s[start + 1]) hit = 1
            for (i = start; i < start + count; i++) if (s[i]) hit = 1
        }
        END { exit hit }'
    status=$?
    rm -f "$in_string"
    return $status
}

if [ "$kind" = comments ]; then
    if ! git diff -U0 "$1" "$2" -- '*.kf' | awk '
        /^(\+\+\+|---) / { next }
        /^[+-]/ { if (substr($0, 2) !~ /^[ \t]*(\/\/.*)?$/) bad = 1 }
        END { exit bad }'; then
        echo code
        exit 0
    fi
    for f in $files; do
        case "$f" in
            *.kf) if ! strings_untouched "$1" "$2" "$f"; then echo code; exit 0; fi ;;
        esac
    done
fi
echo "$kind"
