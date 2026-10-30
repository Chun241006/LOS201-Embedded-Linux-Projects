#!/bin/sh

MODULE="/lib/modules/5.15.0/asgn1_driver.ko"
DEVICE="/dev/asgn1"
TEST_PROGRAM="/usr/bin/test_ioctl"

echo "======================================"
echo " ASM1 CHARACTER DRIVER TEST"
echo "======================================"

# Step 1: Load module if it is not loaded
echo "[1] Checking kernel module..."

if ! grep -q '^asgn1_driver ' /proc/modules; then
    if ! insmod "$MODULE"; then
        echo "FAIL: Cannot load asgn1_driver"
        exit 1
    fi
fi

echo "PASS: Kernel module loaded"

# Step 2: Check device node
echo "[2] Checking device node..."

if [ ! -c "$DEVICE" ]; then
    mknod "$DEVICE" c 241 0 || exit 1
fi

chmod 666 "$DEVICE"

echo "PASS: Device node exists"

# Step 3: Run ioctl tests
echo "[3] Running IOCTL tests..."

if [ ! -x "$TEST_PROGRAM" ]; then
    echo "FAIL: test_ioctl not found"
    exit 1
fi

"$TEST_PROGRAM" || exit 1

# Step 4: Check procfs
echo "[4] Checking Procfs..."

if [ -f /proc/asgn1_info ]; then
    cat /proc/asgn1_info
    echo "PASS: Procfs available"
else
    echo "FAIL: Procfs not found"
    exit 1
fi

# Step 5: Check sysfs
echo "[5] Checking Sysfs..."

SYSFS="/sys/class/asgn1_class/asgn1"

for attribute in buffer_len open_count last_data; do
    if [ ! -f "$SYSFS/$attribute" ]; then
        echo "FAIL: Missing $attribute"
        exit 1
    fi

    echo "$attribute:"
    cat "$SYSFS/$attribute"
done

echo "PASS: Sysfs attributes available"

# Step 6: Unload module
echo "[6] Unloading module..."

if ! rmmod asgn1_driver; then
    echo "FAIL: Cannot unload module"
    exit 1
fi

if grep -q '^asgn1_driver ' /proc/modules; then
    echo "FAIL: Module is still loaded"
    exit 1
fi

echo "PASS: Module unloaded"

echo "======================================"
echo " ASM1 TEST SCRIPT FINISHED"
echo "======================================"
