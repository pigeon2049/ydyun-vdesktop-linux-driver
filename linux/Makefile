PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib/ydyun-usb
VENDOR_SESSION ?= ../build/ydyun-chuanyun-session
VENDOR_USB ?= ../build/ydyun-chuanyun-usb
SPICE_VIEWER ?= ../build/ydyun-spice-viewer
DEB ?= ../build/ydyun-usbctl_$(shell dpkg-parsechangelog -l ../debian/changelog -S Version)_amd64.deb

.PHONY: check install package-audit e2e-vudc-hid e2e-vudc-usb e2e-vudc-backward e2e-dummy-host e2e-spice-display

check:
	python3 -m py_compile src/ydyun_usbctl.py src/ydyun_spice.py src/ydyun_ice_probe.py src/ydyun_session.py tests/backward_cloud_proxy.py
	mkdir -p $(dir $(VENDOR_SESSION))
	cc -Wall -Wextra -Werror -O2 ../linux/experimental/ydyun_chuanyun_session.c -ldl -o $(VENDOR_SESSION)
	cc -Wall -Wextra -Werror -O2 ../linux/experimental/ydyun_chuanyun_usb.c -ldl -o $(VENDOR_USB)
	cc -Wall -Wextra -Werror -O2 ../linux/experimental/ydyun_spice_viewer.c \
		$$(pkg-config --cflags --libs spice-client-gtk-3.0) -o $(SPICE_VIEWER)
	cc -Wall -Wextra -Werror -fPIC -shared tests/fixtures/fake_chuanyun.c -o ../build/libfake_chuanyun.so
	cc -Wall -Wextra -Werror -fPIC -shared tests/fixtures/fake_chuanyun_usbip.c -o ../build/libfake_chuanyun_usbip.so
	timeout --signal=INT 2 $(VENDOR_SESSION) tests/fixtures/chuanyun-session-loader.conf > ../build/session-loader.out || test $$? -eq 124
	grep -F 'connection vm=fixture-vm state=connected' ../build/session-loader.out
	grep -F 'display vm=fixture-vm state=first-frame' ../build/session-loader.out
	printf '%s\n' list 'attach 1-2' 'detach 1-2' 'map 1-2' 'unmap 1-2' stop | $(VENDOR_USB) tests/fixtures/chuanyun-usb-loader.conf run >/dev/null
	printf '%s\n' stop | $(VENDOR_USB) tests/fixtures/chuanyun-usb-jwae-loader.conf run > ../build/usb-jwae-loader.out
	grep -F 'jwae state=started' ../build/usb-jwae-loader.out
	grep -F 'usb state=started' ../build/usb-jwae-loader.out

e2e-vudc-hid:
	tests/e2e_vudc_hid.sh

e2e-vudc-usb: e2e-vudc-hid

e2e-vudc-backward:
	YDYUN_E2E_BACKWARD=1 tests/e2e_vudc_hid.sh 3259

e2e-dummy-host:
	YDYUN_E2E_DUMMY_HOST=1 tests/e2e_vudc_hid.sh 3261

e2e-spice-display:
	tests/e2e_spice_display.sh

package-audit:
	tests/audit_deb.sh $(DEB)

install:
	install -Dm0755 src/ydyun_usbctl.py $(DESTDIR)$(BINDIR)/ydyun-usbctl
	install -Dm0755 src/ydyun_ice_probe.py $(DESTDIR)$(BINDIR)/ydyun-ice-probe
	install -Dm0755 src/ydyun_session.py $(DESTDIR)$(BINDIR)/ydyun-session-probe
	install -Dm0644 src/ydyun_spice.py $(DESTDIR)$(LIBDIR)/ydyun_spice.py
	install -Dm0755 $(VENDOR_SESSION) $(DESTDIR)$(BINDIR)/ydyun-chuanyun-session
	install -Dm0755 $(VENDOR_USB) $(DESTDIR)$(BINDIR)/ydyun-chuanyun-usb
	install -Dm0755 $(SPICE_VIEWER) $(DESTDIR)$(BINDIR)/ydyun-spice-viewer
