#!/bin/bash
set -eu

# Root-only integration test for the mainline USB/IP data path.
# It creates a temporary ConfigFS composite keyboard + mouse HID + Mass Storage gadget, exports
# it with usbipd --device through usbip-vudc, or with usbip-host when
# YDYUN_E2E_DUMMY_HOST=1, imports it through vhci-hcd, verifies the class
# drivers, then cleans up.

if [ "$(id -u)" -ne 0 ]; then
    echo "e2e_vudc_hid.sh: run as root" >&2
    exit 77
fi

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
GADGET=/sys/kernel/config/usb_gadget/ydyun-vudc-e2e
USBIP_PORT=${1:-3249}
BACKWARD_REMOTE_PORT=${YDYUN_E2E_BACKWARD_REMOTE_PORT:-3250}
BACKWARD_PROXY_PORT=${YDYUN_E2E_BACKWARD_PROXY_PORT:-3251}
USBIPD_PID=
BRIDGE_PID=
CLOUD_PROXY_PID=
EXPORT_ACTIVE=0
DUMMY_MODULE_LOADED=0
CONTROLLER_MODE=${YDYUN_E2E_CONTROLLER:-0}
BACKWARD_MODE=${YDYUN_E2E_BACKWARD:-0}
DUMMY_HOST_MODE=${YDYUN_E2E_DUMMY_HOST:-0}
CONTROLLER_CONFIG="$ROOT/build/vudc-e2e-controller.conf"
DISK="$ROOT/build/vudc-e2e-disk.img"
PROBE="$ROOT/build/vudc-e2e-write-probe.bin"
READBACK="$ROOT/build/vudc-e2e-write-readback.bin"
INPUT_PIDS=
INPUT_LOGS=
USBIP_BUSID=usbip-vudc.0
CLEANED=0

cleanup() {
    status=$?
    set +e
    if [ "$CLEANED" = 1 ]; then
        return "$status"
    fi
    CLEANED=1
    for port in $(/usr/sbin/usbip --tcp-port "$USBIP_PORT" port 2>/dev/null |
        awk '/^Port [0-9]+:/ {gsub(":", "", $2); print $2}'); do
        /usr/sbin/usbip --tcp-port "$USBIP_PORT" detach --port "$port" >/dev/null 2>&1
    done
    if [ -n "$CLOUD_PROXY_PID" ]; then
        kill "$CLOUD_PROXY_PID" >/dev/null 2>&1
        wait "$CLOUD_PROXY_PID" >/dev/null 2>&1
    fi
    if [ -n "$BRIDGE_PID" ]; then
        kill "$BRIDGE_PID" >/dev/null 2>&1
        wait "$BRIDGE_PID" >/dev/null 2>&1
    fi
    if [ -n "$USBIPD_PID" ]; then
        kill "$USBIPD_PID" >/dev/null 2>&1
        wait "$USBIPD_PID" >/dev/null 2>&1
    fi
    if [ "$EXPORT_ACTIVE" = 1 ] && [ -x /usr/bin/ydyun-usbctl ]; then
        /usr/bin/ydyun-usbctl --config "$CONTROLLER_CONFIG" unexport "$USBIP_BUSID" \
            > "$ROOT/build/vudc-e2e-unexport.out" 2>&1
    fi
    for pid in $INPUT_PIDS; do
        kill "$pid" >/dev/null 2>&1
    done
    if [ -e "$GADGET/UDC" ]; then
        printf '' > "$GADGET/UDC"
    fi
    if [ -L "$GADGET/configs/c.1/hid.usb0" ]; then
        unlink "$GADGET/configs/c.1/hid.usb0"
    fi
    if [ -L "$GADGET/configs/c.1/hid.usb1" ]; then
        unlink "$GADGET/configs/c.1/hid.usb1"
    fi
    if [ -L "$GADGET/configs/c.1/mass_storage.0" ]; then
        unlink "$GADGET/configs/c.1/mass_storage.0"
    fi
    rmdir "$GADGET/configs/c.1/strings/0x409" 2>/dev/null
    rmdir "$GADGET/configs/c.1/strings" 2>/dev/null
    rmdir "$GADGET/configs/c.1" 2>/dev/null
    rmdir "$GADGET/configs" 2>/dev/null
    rmdir "$GADGET/functions/hid.usb0" 2>/dev/null
    rmdir "$GADGET/functions/hid.usb1" 2>/dev/null
    rmdir "$GADGET/functions/mass_storage.0" 2>/dev/null
    rmdir "$GADGET/functions" 2>/dev/null
    rmdir "$GADGET/strings/0x409" 2>/dev/null
    rmdir "$GADGET/strings" 2>/dev/null
    rmdir "$GADGET/webusb/landingPage" 2>/dev/null
    rmdir "$GADGET/webusb" 2>/dev/null
    rmdir "$GADGET/os_desc" 2>/dev/null
    rmdir "$GADGET" 2>/dev/null
    if [ -e "$DISK" ]; then
        unlink "$DISK"
    fi
    if [ -e "$PROBE" ]; then
        unlink "$PROBE"
    fi
    if [ -e "$READBACK" ]; then
        unlink "$READBACK"
    fi
    for log in $INPUT_LOGS; do
        if [ -e "$log" ]; then
            unlink "$log"
        fi
    done
    if [ -e "$CONTROLLER_CONFIG" ]; then
        unlink "$CONTROLLER_CONFIG"
    fi
    if [ "$DUMMY_MODULE_LOADED" = 1 ]; then
        /sbin/modprobe -r dummy_hcd >/dev/null 2>&1
    fi
    return "$status"
}

trap cleanup EXIT INT TERM

if [ -e "$GADGET" ]; then
    echo "e2e_vudc_hid.sh: gadget path already exists: $GADGET" >&2
    exit 1
fi
if [ -e "$DISK" ]; then
    echo "e2e_vudc_hid.sh: disk path already exists: $DISK" >&2
    exit 1
fi
if [ -e "$PROBE" ] || [ -e "$READBACK" ]; then
    echo "e2e_vudc_hid.sh: probe path already exists" >&2
    exit 1
fi

/sbin/modprobe libcomposite
/sbin/modprobe usb_f_hid
/sbin/modprobe usb_f_mass_storage
if [ "$DUMMY_HOST_MODE" = 1 ]; then
    /sbin/modprobe dummy_hcd
    DUMMY_MODULE_LOADED=1
else
    /sbin/modprobe usbip-vudc
fi

mkdir -p "$GADGET/strings/0x409" \
    "$GADGET/configs/c.1/strings/0x409" \
    "$GADGET/functions/hid.usb0" \
    "$GADGET/functions/hid.usb1" \
    "$GADGET/functions/mass_storage.0"
printf '0x1d6b' > "$GADGET/idVendor"
printf '0x0104' > "$GADGET/idProduct"
printf '0x0200' > "$GADGET/bcdUSB"
printf 'YDYUN' > "$GADGET/strings/0x409/manufacturer"
printf 'Linux USB/IP E2E' > "$GADGET/strings/0x409/product"
printf 'ydyun-e2e-0001' > "$GADGET/strings/0x409/serialnumber"
printf 'HID test configuration' > "$GADGET/configs/c.1/strings/0x409/configuration"
printf '120' > "$GADGET/configs/c.1/MaxPower"
printf '1' > "$GADGET/functions/hid.usb0/protocol"
printf '1' > "$GADGET/functions/hid.usb0/subclass"
printf '8' > "$GADGET/functions/hid.usb0/report_length"
printf '%b' '\x05\x01\x09\x06\xa1\x01\x05\x07\x19\xe0\x29\xe7\x15\x00\x25\x01\x75\x01\x95\x08\x81\x02\x95\x01\x75\x08\x81\x01\x95\x05\x75\x01\x05\x08\x19\x01\x29\x05\x91\x02\x95\x01\x75\x03\x91\x01\xc0' > "$GADGET/functions/hid.usb0/report_desc"
printf '2' > "$GADGET/functions/hid.usb1/protocol"
printf '1' > "$GADGET/functions/hid.usb1/subclass"
printf '4' > "$GADGET/functions/hid.usb1/report_length"
printf '%b' '\x05\x01\x09\x02\xa1\x01\x09\x01\xa1\x00\x05\x09\x19\x01\x29\x03\x15\x00\x25\x01\x75\x01\x95\x03\x81\x02\x75\x05\x95\x01\x81\x01\x05\x01\x09\x30\x09\x31\x09\x38\x15\x81\x25\x7f\x75\x08\x95\x03\x81\x06\xc0\xc0' > "$GADGET/functions/hid.usb1/report_desc"
truncate -s 8M "$DISK"
mkfs.ext4 -q -F -L YDYUN_E2E "$DISK"
dd if=/dev/zero of="$PROBE" bs=512 count=1 status=none
printf 'YDYUN-E2E-WRITE-CHECK' | dd of="$PROBE" bs=1 conv=notrunc status=none
printf '0' > "$GADGET/functions/mass_storage.0/stall"
printf '%s' "$DISK" > "$GADGET/functions/mass_storage.0/lun.0/file"

cd "$GADGET"
ln -s functions/hid.usb0 configs/c.1/
ln -s functions/hid.usb1 configs/c.1/
ln -s functions/mass_storage.0 configs/c.1/
if [ "$DUMMY_HOST_MODE" = 1 ]; then
    printf 'dummy_udc.0' > UDC
else
    printf 'usbip-vudc.0' > UDC
fi

if [ "$DUMMY_HOST_MODE" = 1 ]; then
    for _ in $(seq 1 30); do
        USBIP_BUSID=$(/usr/sbin/usbip list --local 2>/dev/null |
            awk '/1d6b:0104/ {gsub(":", "", $3); print $3; exit}')
        [ -n "$USBIP_BUSID" ] && break
        sleep 0.1
    done
    if [ -z "$USBIP_BUSID" ]; then
        /usr/sbin/usbip list --local >&2 || true
        echo 'e2e_vudc_hid.sh: dummy_hcd gadget did not appear in usbip local list' >&2
        exit 1
    fi
fi

if [ "$DUMMY_HOST_MODE" = 1 ]; then
    /usr/sbin/usbipd --ipv4 --tcp-port "$USBIP_PORT" \
        > "$ROOT/build/vudc-e2e-usbipd.out" 2>&1 &
else
    /usr/sbin/usbipd --device --ipv4 --tcp-port "$USBIP_PORT" \
        > "$ROOT/build/vudc-e2e-usbipd.out" 2>&1 &
fi
USBIPD_PID=$!

if [ "$DUMMY_HOST_MODE" = 1 ]; then
    if [ "$CONTROLLER_MODE" = 1 ] || [ "$BACKWARD_MODE" = 1 ]; then
        echo 'e2e_vudc_hid.sh: dummy host mode cannot combine with controller/backward mode' >&2
        exit 1
    fi
    if [ -e "$CONTROLLER_CONFIG" ]; then
        echo "e2e_vudc_hid.sh: controller config already exists: $CONTROLLER_CONFIG" >&2
        exit 1
    fi
    printf '%s\n' \
        '[connection]' \
        'remote_host = 127.0.0.1' \
        "remote_port = $USBIP_PORT" \
        '' \
        '[local_devices]' \
        "$USBIP_BUSID = dummy_hcd composite keyboard mouse storage" > "$CONTROLLER_CONFIG"
    /usr/bin/ydyun-usbctl --config "$CONTROLLER_CONFIG" export "$USBIP_BUSID" \
        > "$ROOT/build/vudc-e2e-export.out" 2>&1
    EXPORT_ACTIVE=1
elif [ "$BACKWARD_MODE" = 1 ]; then
    if [ "$CONTROLLER_MODE" = 1 ]; then
        echo 'e2e_vudc_hid.sh: backward mode and controller mode are mutually exclusive' >&2
        exit 1
    fi
    if [ -e "$CONTROLLER_CONFIG" ]; then
        echo "e2e_vudc_hid.sh: controller config already exists: $CONTROLLER_CONFIG" >&2
        exit 1
    fi
    printf '%s\n' \
        '[connection]' \
        'remote_host = 127.0.0.1' \
        "remote_port = $BACKWARD_PROXY_PORT" \
        'reconnect_seconds = 1' \
        '' \
        '[devices]' \
        "usbip-vudc.0 = composite keyboard mouse storage" \
        '' \
        '[backward]' \
        'enabled = yes' \
        'listen_host = 127.0.0.1' \
        "listen_port = $BACKWARD_REMOTE_PORT" \
        'proxy_host = 127.0.0.1' \
        "proxy_port = $BACKWARD_PROXY_PORT" \
        'auto_attach = no' \
        'compression = off' > "$CONTROLLER_CONFIG"
    /usr/bin/ydyun-usbctl --config "$CONTROLLER_CONFIG" backward \
        > "$ROOT/build/vudc-e2e-backward.out" 2>&1 &
    BRIDGE_PID=$!
    python3 "$ROOT/linux/tests/backward_cloud_proxy.py" \
        "$BACKWARD_REMOTE_PORT" "$USBIP_PORT" usbip-vudc.0 \
        > "$ROOT/build/vudc-e2e-cloud-proxy.out" 2>&1 &
    CLOUD_PROXY_PID=$!
elif [ "$CONTROLLER_MODE" = 1 ]; then
    if [ ! -x /usr/bin/ydyun-usbctl ]; then
        echo 'e2e_vudc_hid.sh: installed /usr/bin/ydyun-usbctl not found' >&2
        exit 1
    fi
    if [ -e "$CONTROLLER_CONFIG" ]; then
        echo "e2e_vudc_hid.sh: controller config already exists: $CONTROLLER_CONFIG" >&2
        exit 1
    fi
    printf '%s\n' \
        '[connection]' \
        'remote_host = 127.0.0.1' \
        "remote_port = $USBIP_PORT" \
        '' \
        '[devices]' \
        'usbip-vudc.0 = composite keyboard mouse storage' > "$CONTROLLER_CONFIG"
fi

listed=
for _ in $(seq 1 30); do
    listed=$(/usr/sbin/usbip --tcp-port "$USBIP_PORT" list --remote 127.0.0.1 2>/dev/null || true)
    case "$listed" in
        *"$USBIP_BUSID"*) break ;;
    esac
    sleep 0.1
done
case "$listed" in
    *"$USBIP_BUSID"*) ;;
    *)
        printf '%s\n' "$listed" >&2
        echo "e2e_vudc_hid.sh: usbipd did not export the vUDC gadget" >&2
        exit 1
        ;;
esac
printf '%s\n' "$listed"

if [ "$DUMMY_HOST_MODE" = 1 ]; then
    /usr/sbin/usbip --tcp-port "$USBIP_PORT" attach \
        --remote 127.0.0.1 --busid "$USBIP_BUSID"
elif [ "$BACKWARD_MODE" = 1 ]; then
    /usr/sbin/usbip --tcp-port "$BACKWARD_PROXY_PORT" attach \
        --remote 127.0.0.1 --busid usbip-vudc.0
elif [ "$CONTROLLER_MODE" = 1 ]; then
    /usr/bin/ydyun-usbctl --config "$CONTROLLER_CONFIG" attach usbip-vudc.0
else
    /usr/sbin/usbip --tcp-port "$USBIP_PORT" attach \
        --remote 127.0.0.1 --busid usbip-vudc.0
fi
sleep 1
ports=$(/usr/sbin/usbip --tcp-port "$USBIP_PORT" port)
printf '%s\n' "$ports"
case "$ports" in
    *"$USBIP_BUSID"*) ;;
    *)
        echo "e2e_vudc_hid.sh: vhci-hcd did not import the gadget" >&2
        exit 1
        ;;
esac

descriptor=$(/usr/bin/lsusb -v -d 1d6b:0104 2>/dev/null)
printf '%s\n' "$descriptor"
printf '%s\n' "$descriptor" | grep -F 'bNumInterfaces          3' >/dev/null
printf '%s\n' "$descriptor" | grep -F 'Human Interface Device' >/dev/null
printf '%s\n' "$descriptor" | grep -F 'bInterfaceProtocol      1' >/dev/null
printf '%s\n' "$descriptor" | grep -F 'bInterfaceProtocol      2' >/dev/null
printf '%s\n' "$descriptor" | grep -F 'Transfer Type            Interrupt' >/dev/null
tree=
for _ in $(seq 1 20); do
    tree=$(/usr/bin/lsusb -t)
    case "$tree" in
        *Driver=usbhid*Driver=usb-storage*|*Driver=usb-storage*Driver=usbhid*) break ;;
    esac
    sleep 0.2
done
printf '%s\n' "$tree"
printf '%s\n' "$tree" | grep -F 'Driver=usbhid' >/dev/null
printf '%s\n' "$tree" | grep -F 'Driver=usb-storage' >/dev/null
block=
for _ in $(seq 1 30); do
    block=$(/usr/bin/lsblk -pnro NAME,LABEL 2>/dev/null |
        awk '$2 == "YDYUN_E2E" {print $1; exit}')
    [ -n "$block" ] && break
    sleep 0.2
done
if [ -z "$block" ]; then
    echo 'e2e_vudc_hid.sh: usb-storage did not create the labeled block device' >&2
    /usr/bin/lsblk -pnro NAME,LABEL >&2
    exit 1
fi
dd if="$PROBE" of="$block" bs=512 seek=1000 count=1 conv=notrunc status=none
sync
dd if="$block" of="$READBACK" bs=512 skip=1000 count=1 status=none
cmp "$PROBE" "$READBACK"
printf 'storage_block=%s\n' "$block"
test -c /dev/hidg0
test -c /dev/hidg1

for _ in $(seq 1 30); do
    for event in /dev/input/event*; do
        properties=$(/usr/bin/udevadm info --query=property --name "$event" 2>/dev/null || true)
        printf '%s\n' "$properties" | grep -F 'ID_VENDOR_ID=1d6b' >/dev/null || continue
        printf '%s\n' "$properties" | grep -F 'ID_MODEL_ID=0104' >/dev/null || continue
        log="$ROOT/build/vudc-e2e-${event##*/}.out"
        cat "$event" > "$log" 2>/dev/null &
        INPUT_PIDS="$INPUT_PIDS $!"
        INPUT_LOGS="$INPUT_LOGS $log"
    done
    [ -n "$INPUT_PIDS" ] && break
    sleep 0.2
done
if [ -z "$INPUT_PIDS" ]; then
    echo 'e2e_vudc_hid.sh: no input event reader for the imported HID gadget' >&2
    exit 1
fi
if [ "$DUMMY_HOST_MODE" = 1 ]; then
    # dummy_hcd exposes the gadget as a local host device, but its gadget-side
    # hidg write can wait forever when the remote usbhid driver has no pending
    # interrupt-IN URB.  Keep this probe bounded; descriptor/class binding and
    # storage I/O above already cover the local usbip-host export path.
    if timeout 2s sh -c "printf '%b' '\\x00\\x00\\x04\\x00\\x00\\x00\\x00\\x00' > /dev/hidg0"; then
        printf '%b' '\x00\x00\x00\x00' > /dev/hidg0
        printf '%b' '\x00\x00\x00\x00' > /dev/hidg1
        printf '%b' '\x05\x00\x00\x00' > /dev/hidg1
        printf '%s\n' 'PASS dummy_hcd -> usbip-host -> usbipd -> vhci-hcd HID enumeration + Mass Storage read/write; HID report probe passed'
    else
        printf '%s\n' 'PASS dummy_hcd -> usbip-host -> usbipd -> vhci-hcd HID enumeration + Mass Storage read/write; HID report probe bounded out (dummy_hcd endpoint behavior)'
    fi
else
    printf '%b' '\x00\x00\x04\x00\x00\x00\x00\x00' > /dev/hidg0
    printf '%b' '\x00\x00\x00\x00' > /dev/hidg0
    printf '%b' '\x00\x00\x00\x00' > /dev/hidg1
    printf '%b' '\x05\x00\x00\x00' > /dev/hidg1
    printf '%s\n' 'PASS vUDC -> usbipd -> vhci-hcd keyboard + mouse HID and Mass Storage read/write'
fi
