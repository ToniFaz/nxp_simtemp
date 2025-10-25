#!/bin/bash
set -e

echo "=== Starting Demo ==="

MODULE_NAME="nxp_simtemp"
MODULE_FILE="${MODULE_NAME}.ko"
DEVICE_FILE="/dev/simtemp"
SYSFS_DIR="/sys/class/nxp_simtemp/simtemp"
CLI_APP="./simtemp_cli"

echo "--- Loading Kernel Module ---"
sudo insmod "$MODULE_FILE"

# Verificar AMBAS interfaces
echo "--- Checking interfaces ---"

# 1. Verificar dispositivo de caracteres (con sudo)
if sudo test -e "$DEVICE_FILE"; then
    echo " Char device: $DEVICE_FILE"
else
    echo " Missing: $DEVICE_FILE"
fi

# 2. Verificar sysfs (sin sudo normalmente)
if test -d "$SYSFS_DIR"; then
    echo " Sysfs interface: $SYSFS_DIR"
    # Mostrar archivos disponibles
    ls -la "$SYSFS_DIR/"
else
    echo " Missing: $SYSFS_DIR"
fi

echo "--- Running Tests ---"

# Test con /dev/simtemp (datos binarios)
echo "Test 1: Reading binary data"
sudo dd if="$DEVICE_FILE" of=./test.bin bs=16 count=10

echo "----Analyzing test.bin ------"
./decode_samples test.bin

# Test con /sys/class/... (variables/parámetros)
echo "Test 2: Checking sysfs variables"
if [ -f "$SYSFS_DIR/device/threshold_mC" ]; then
    echo "Temperature: $(cat $SYSFS_DIR/device/threshold_mC)"
fi
if [ -f "$SYSFS_DIR/device/sampling_ms" ]; then
    echo "Sampling rate: $(cat $SYSFS_DIR/device/sampling_ms)"
fi

echo "=== Demo completed ==="
