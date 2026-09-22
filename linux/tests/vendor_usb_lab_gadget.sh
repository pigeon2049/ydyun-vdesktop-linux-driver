#!/bin/bash
set -eu

# Root-only lab fixture for vendor usbredirect protocol work.
# It exposes one ConfigFS Mass Storage device through dummy_hcd, then waits.
# No physical USB device is touched and the fixture is never packaged.

if [ "$(id -u)" -ne 0 ]; then
    echo 'vendor_usb_lab_gadget.sh: run as root' >&2
    exit 77
fi

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
GADGET=/sys/kernel/config/usb_gadget/ydyun-vendor-lab
DISK="$ROOT/build/vendor-usb-lab-disk.img"
CLEANED=0

cleanup() {
    status=$?
    set +e
    if [ "$CLEANED" = 1 ]; then
        return "$status"
    fi
    CLEANED=1
    if [ -e "$GADGET/UDC" ]; then
        printf '' > "$GADGET/UDC"
    fi
    unlink "$GADGET/configs/c.1/mass_storage.0" 2>/dev/null || true
    rmdir "$GADGET/configs/c.1/strings/0x409" 2>/dev/null || true
    rmdir "$GADGET/configs/c.1/strings" 2>/dev/null || true
    rmdir "$GADGET/configs/c.1" 2>/dev/null || true
    rmdir "$GADGET/configs" 2>/dev/null || true
    rmdir "$GADGET/functions/mass_storage.0" 2>/dev/null || true
    rmdir "$GADGET/functions" 2>/dev/null || true
    rmdir "$GADGET/strings/0x409" 2>/dev/null || true
    rmdir "$GADGET/strings" 2>/dev/null || true
    rmdir "$GADGET" 2>/dev/null || true
    unlink "$DISK" 2>/dev/null || true
    modprobe -r dummy_hcd 2>/dev/null || true
    return "$status"
}
trap cleanup EXIT INT TERM

if [ -e "$GADGET" ] || [ -e "$DISK" ]; then
    echo 'vendor_usb_lab_gadget.sh: fixture path already exists' >&2
    exit 1
fi

modprobe libcomposite
modprobe usb_f_mass_storage
modprobe dummy_hcd
mkdir -p "$GADGET/strings/0x409" \
    "$GADGET/configs/c.1/strings/0x409" \
    "$GADGET/functions/mass_storage.0"
printf '0x1d6b' > "$GADGET/idVendor"
printf '0x0105' > "$GADGET/idProduct"
printf '0x0200' > "$GADGET/bcdUSB"
printf 'YDYUN' > "$GADGET/strings/0x409/manufacturer"
printf 'Vendor USB protocol lab' > "$GADGET/strings/0x409/product"
printf 'ydyun-vendor-lab-0001' > "$GADGET/strings/0x409/serialnumber"
printf 'Mass Storage protocol lab' > "$GADGET/configs/c.1/strings/0x409/configuration"
printf '120' > "$GADGET/configs/c.1/MaxPower"
printf '0' > "$GADGET/functions/mass_storage.0/stall"
truncate -s 8M "$DISK"
printf '%s' "$DISK" > "$GADGET/functions/mass_storage.0/lun.0/file"
ln -s "$GADGET/functions/mass_storage.0" "$GADGET/configs/c.1/"
printf 'dummy_udc.0' > "$GADGET/UDC"

busid=
for _ in $(seq 1 50); do
    busid=$(usbip list --local 2>/dev/null | awk '/1d6b:0105/ {gsub(":", "", $3); print $3; exit}')
    [ -n "$busid" ] && break
    sleep 0.1
done
if [ -z "$busid" ]; then
    usbip list --local >&2 || true
    echo 'vendor_usb_lab_gadget.sh: dummy_hcd device did not appear' >&2
    exit 1
fi
printf 'vendor_usb_lab busid=%s\n' "$busid"
lsusb -t | grep -E 'dummy_hcd|Mass Storage' || true
while :; do
    sleep 1
done
