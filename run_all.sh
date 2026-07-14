#!/usr/bin/env bash
# Build every test for the QEMU target and run the whole suite.
# Test 11 passes by PANICKING (it verifies stack-overflow detection), so its
# expected output differs; everything else must print TEST PASS.

cd "$(dirname "$0")" || exit 1

echo "=== building (TARGET=qemu) ==="
make tests -j"$(nproc 2>/dev/null || echo 4)" >/dev/null || {
    echo "BUILD FAILED"; exit 1; }

pass=0
fail=0
failed_names=()

for elf in build/qemu/test_*.elf; do
    name=$(basename "$elf" .elf)
    expect="TEST PASS"
    case "$name" in
        test_11*) expect="stack overflow in task" ;;
    esac

    out=$(scripts/run_qemu.sh "$elf" 30 2>&1)
    if grep -q "$expect" <<<"$out" && ! grep -q "TEST FAIL" <<<"$out" \
       && ! grep -q "ASSERT" <<<"$out"; then
        printf "PASS  %s\n" "$name"
        pass=$((pass + 1))
    else
        printf "FAIL  %s\n" "$name"
        sed 's/^/      | /' <<<"$out" | tail -8
        fail=$((fail + 1))
        failed_names+=("$name")
    fi
done

echo "===================="
echo "$pass/$((pass + fail)) tests passed"
if [ "$fail" -ne 0 ]; then
    echo "failed: ${failed_names[*]}"
    exit 1
fi
