#!/bin/sh
# Test runner for GarudaLang.
# Runs each testdata/*.ga, comparing stdout against a .expected file
# when one exists, and checking that the compiler rejects the
# corresponding *.bad.ga file when it does.
#
# Author: GalangTeam

set -u

GAR=./bin/gar
DATA=testdata
pass=0
fail=0

if [ ! -x "$GAR" ]; then
    echo "error: $GAR not built. Run 'make' first." >&2
    exit 1
fi

green() { printf '\033[32m%s\033[0m\n' "$1"; }
red()   { printf '\033[31m%s\033[0m\n' "$1"; }

for src in "$DATA"/*.ga; do
    [ -e "$src" ] || continue
    case "$src" in *.bad.ga) continue ;; esac

    name=$(basename "$src" .ga)
    bin="/tmp/gar_test_$name"

    if ! "$GAR" build "$src" -o "$bin" >/tmp/gar_build.log 2>&1; then
        red "FAIL (build) $name"
        sed 's/^/      /' /tmp/gar_build.log
        fail=$((fail + 1))
        continue
    fi

    actual=$("$bin" 2>&1)
    expected_file="$DATA/$name.expected"

    if [ -f "$expected_file" ]; then
        expected=$(cat "$expected_file")
        if [ "$actual" = "$expected" ]; then
            green "ok   $name"
            pass=$((pass + 1))
        else
            red "FAIL (output) $name"
            printf '      expected: %s\n' "$(printf '%s' "$expected" | od -c | head -5)"
            printf '      actual:   %s\n' "$(printf '%s' "$actual" | od -c | head -5)"
            fail=$((fail + 1))
        fi
    else
        green "ok   $name (no .expected, build+run only)"
        pass=$((pass + 1))
    fi

    rm -f "$bin"
done

# Negative tests: each *.bad.ga MUST be rejected by the compiler.
for src in "$DATA"/*.bad.ga; do
    [ -e "$src" ] || continue

    name=$(basename "$src" .bad.ga)
    if "$GAR" check "$src" >/tmp/gar_check.log 2>&1; then
        red "FAIL (should be rejected) $name"
        fail=$((fail + 1))
    else
        green "ok   $name (rejected: $(head -1 /tmp/gar_check.log))"
        pass=$((pass + 1))
    fi
done

echo
echo "passed: $pass  failed: $fail"
[ "$fail" -eq 0 ]
