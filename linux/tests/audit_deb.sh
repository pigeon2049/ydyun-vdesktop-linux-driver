#!/bin/sh
set -eu

if [ "$#" -ne 1 ] || [ ! -f "$1" ]; then
	printf 'usage: %s PACKAGE.deb\n' "$0" >&2
	exit 2
fi

deb=$1
package=$(dpkg-deb -f "$deb" Package)
version=$(dpkg-deb -f "$deb" Version)
arch=$(dpkg-deb -f "$deb" Architecture)
depends=$(dpkg-deb -f "$deb" Depends | tr -d ' ')

[ "$package" = ydyun-usbctl ] || { printf 'audit: unexpected package %s\n' "$package" >&2; exit 1; }
[ "$arch" = amd64 ] || { printf 'audit: unexpected architecture %s\n' "$arch" >&2; exit 1; }
[ "$depends" = python3,usbip,libspice-client-gtk-3.0-5 ] || {
	printf 'audit: unexpected Depends for %s: %s\n' "$version" "$depends" >&2
	exit 1
}

file_list=$(dpkg-deb -c "$deb")
if printf '%s\n' "$file_list" | rg -i '\.(exe|dll|sys|pdb)$|windivert|guard|monitor|qoe|telemetry|trace|security|libjwae|libzime|libzxsecurity'; then
	printf 'audit: forbidden runtime file/path found\n' >&2
	exit 1
fi

for unit in ydyun-usbctl.service ydyun-usbctl-backward.service ydyun-usb-export.service; do
	printf '%s\n' "$file_list" | rg -F -q "./lib/systemd/system/$unit" || {
		printf 'audit: missing systemd unit %s\n' "$unit" >&2
		exit 1
	}
done

printf '%s\n' "$file_list" | rg -F -q './usr/bin/ydyun-spice-viewer' || {
	printf 'audit: missing standard SPICE viewer\n' >&2
	exit 1
}

postinst=$(dpkg-deb --ctrl-tarfile "$deb" | tar -xOf - ./postinst)
if printf '%s\n' "$postinst" | rg -i 'deb-systemd-invoke.*(start|restart|try-restart)'; then
	printf 'audit: package postinst starts a service\n' >&2
	exit 1
fi

printf 'PASS package audit: %s %s %s\n' "$package" "$version" "$arch"
