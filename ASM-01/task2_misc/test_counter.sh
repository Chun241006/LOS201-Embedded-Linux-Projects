#!/bin/sh

MODULE="/lib/modules/5.15.0/counter_driver.ko"
DEVICE="/dev/counter"
PROC="/proc/counter_info"

fail() {
    echo "FAIL: $1"
    exit 1
}

echo "======================================"
echo " ASM1 MISC COUNTER DRIVER TEST"
echo "======================================"

echo "[1] Checking kernel module..."

if ! grep -q '^counter_driver ' /proc/modules; then
    insmod "$MODULE" || fail "Cannot load counter_driver"
fi

echo "PASS: Kernel module loaded"

echo "[2] Checking device node..."

[ -c "$DEVICE" ] || fail "/dev/counter not found"

echo "PASS: Device node exists"

echo "[3] Checking Procfs..."

[ -f "$PROC" ] || fail "/proc/counter_info not found"

cat "$PROC"
echo "PASS: Procfs available"

echo "[4] Testing write..."

echo 100 > "$DEVICE" || fail "Cannot write 100"

echo "PASS: Write 100"

echo "[5] Testing read and increment..."

VALUE1=$(cat "$DEVICE") || fail "First read failed"
VALUE2=$(cat "$DEVICE") || fail "Second read failed"

[ "$VALUE1" = "100" ] || fail "Expected 100, got $VALUE1"
[ "$VALUE2" = "101" ] || fail "Expected 101, got $VALUE2"

echo "First read : $VALUE1"
echo "Second read: $VALUE2"
echo "PASS: Counter increments correctly"

echo "[6] Testing invalid input..."

if (echo -5 > "$DEVICE") 2>/dev/null; then
    fail "Negative value accepted"
fi
echo "PASS: Negative value rejected"

if (echo abc > "$DEVICE") 2>/dev/null; then
    fail "Non-numeric value accepted"
fi
echo "PASS: Non-numeric value rejected"

if (echo 2147483648 > "$DEVICE") 2>/dev/null; then
    fail "Out-of-range value accepted"
fi
echo "PASS: Out-of-range value rejected"

echo "[7] Checking final statistics..."

cat "$PROC"

echo "[8] Unloading module..."

rmmod counter_driver || fail "Cannot unload module"

if grep -q '^counter_driver ' /proc/modules; then
    fail "Module still loaded"
fi

[ ! -e "$PROC" ] || fail "Procfs entry still exists"

echo "PASS: Module unloaded"

echo "======================================"
echo " ASM1 COUNTER TEST FINISHED"
echo "======================================"
