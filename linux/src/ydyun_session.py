#!/usr/bin/env python3
"""Offline validator for a China Mobile Cloud PC vendor-session response.

The official client obtains this object from ``/terminal/cc/getFirmAuth/v1``
before starting its private JWAE/SCG and SPICE layers.  This tool deliberately
does not log in, decrypt a response, connect to a vendor host, or print
passwords/tokens. It validates and redacts a response that the operator already
obtained through an authorised client session, and can optionally write a single
owner-only public-SDK loader config without echoing its secrets.
"""

from __future__ import annotations

import argparse
import ipaddress
import json
import os
from dataclasses import dataclass
from pathlib import Path
import sys
from typing import Any, Mapping


class SessionFormatError(ValueError):
    """The supplied session object is malformed or incomplete."""


SECRET_KEYS = frozenset(
    {
        "authCode",
        "auth_code",
        "bizCode",
        "biz_code",
        "vmPassword",
        "vm_password",
        "password",
        "token",
        "sohoToken",
        "sessionToken",
    }
)


def _first(mapping: Mapping[str, Any], *keys: str) -> Any:
    for key in keys:
        value = mapping.get(key)
        if value not in (None, ""):
            return value
    return None


def _text(value: Any, field: str) -> str | None:
    if value in (None, ""):
        return None
    if not isinstance(value, (str, int)):
        raise SessionFormatError(f"{field} must be a string or integer")
    text = str(value).strip()
    if not text or any(ch.isspace() for ch in text):
        raise SessionFormatError(f"{field} must not contain whitespace")
    return text


def _host(value: Any, field: str) -> str | None:
    value = _text(value, field)
    if value is None:
        return None
    try:
        ipaddress.ip_address(value)
    except ValueError:
        if any(ch in value for ch in "/\\:;|&`$\"'"):
            raise SessionFormatError(f"{field} is not a hostname or IP address")
    return value


def _port(value: Any, field: str) -> int | None:
    if value in (None, ""):
        return None
    try:
        port = int(value)
    except (TypeError, ValueError) as exc:
        raise SessionFormatError(f"{field} must be an integer") from exc
    if not 1 <= port <= 65535:
        raise SessionFormatError(f"{field} is outside 1..65535")
    return port


def _unwrap(raw: Mapping[str, Any]) -> Mapping[str, Any]:
    data = raw.get("data")
    if isinstance(data, Mapping):
        return data
    if isinstance(data, str):
        raise SessionFormatError(
            "data is still encrypted/base64; supply the already-decoded client response"
        )
    return raw


@dataclass(frozen=True)
class SessionDescriptor:
    vm_id: str
    spu_code: str | None
    vmc_host: str | None
    vmc_port: int | None
    cag_host: str | None
    cag_ipv6: str | None
    cag_port: int | None
    scg_host: str | None
    scg_tcp_port: int | None
    scg_udp_port: int | None
    session_id: str | None
    user_name: str | None
    auth_code: str | None
    biz_code: str | None
    has_user: bool
    has_password: bool
    has_auth_code: bool
    has_token: bool

    @property
    def route_count(self) -> int:
        return sum(
            bool(host and port)
            for host, port in (
                (self.vmc_host, self.vmc_port),
                (self.cag_ipv6 or self.cag_host, self.cag_port),
                (self.scg_host, self.scg_tcp_port),
            )
        )

    def redacted(self) -> dict[str, Any]:
        return {
            "vm_id": self.vm_id,
            "spu_code": self.spu_code,
            "vmc": {"host": self.vmc_host, "port": self.vmc_port},
            "cag": {"host": self.cag_host, "port": self.cag_port},
            "cag_ipv6": self.cag_ipv6,
            "scg": {
                "host": self.scg_host,
                "tcp_port": self.scg_tcp_port,
                "udp_port": self.scg_udp_port,
            },
            "session_id": self.session_id,
            "secret_presence": {
                "user": self.has_user,
                "password": self.has_password,
                "auth_code": self.has_auth_code,
                "token": self.has_token,
            },
            "route_count": self.route_count,
        }

    def loader_config(
        self,
        *,
        library: str,
        server_ip: str,
        server_port: int,
        terminal_sn: str | None = None,
        unit_type: str | None = None,
    ) -> str:
        """Build the opt-in public SDK loader config without logging secrets."""
        if self.user_name is None or self.auth_code is None or self.biz_code is None:
            raise SessionFormatError(
                "decoded session lacks vmUserName/authCode/bizCode for the public loader"
            )
        checked_server = _host(server_ip, "server_ip")
        checked_port = _port(server_port, "server_port")
        if checked_server is None or checked_port is None:
            raise SessionFormatError("server_ip and server_port are required")
        values = {
            "library": library,
            "server_ip": checked_server,
            "server_port": str(checked_port),
            "terminal_sn": terminal_sn,
            "unit_type": unit_type,
            "vm_id": self.vm_id,
            "username": self.user_name,
            "auth_code": self.auth_code,
            "biz_code": self.biz_code,
        }
        for key, value in values.items():
            if value is not None and ("\n" in value or "\r" in value):
                raise SessionFormatError(f"{key} contains a newline")
        return "".join(
            f"{key} = {value}\n"
            for key, value in values.items()
            if value is not None
        )


def write_private_config(path: Path, content: str) -> None:
    """Create a loader config once, with owner-only permissions."""
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    descriptor = os.open(path, flags, 0o600)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            descriptor = -1
            output.write(content)
    finally:
        if descriptor != -1:
            os.close(descriptor)


def parse_session(raw: Mapping[str, Any]) -> SessionDescriptor:
    data = _unwrap(raw)
    vm_id = _text(_first(data, "vmId", "vm_id", "vmid"), "vmId")
    if vm_id is None:
        raise SessionFormatError("session response is missing vmId")

    descriptor = SessionDescriptor(
        vm_id=vm_id,
        spu_code=_text(_first(data, "spuCode", "spu_code"), "spuCode"),
        vmc_host=_host(_first(data, "vmcIp", "vmcIP", "vmc_ip"), "vmcIp"),
        vmc_port=_port(_first(data, "vmcPort", "vmc_port"), "vmcPort"),
        cag_host=_host(_first(data, "cagIp", "cagIP", "cag_ip"), "cagIp"),
        cag_ipv6=_host(_first(data, "cagIpv6", "cagIPv6", "cag_ipv6"), "cagIpv6"),
        cag_port=_port(_first(data, "cagPort", "cag_port"), "cagPort"),
        scg_host=_host(_first(data, "scgIp", "scgIP", "scg_ip"), "scgIp"),
        scg_tcp_port=_port(
            _first(data, "scgTcpPort", "scg_tcp_port"), "scgTcpPort"
        ),
        scg_udp_port=_port(
            _first(data, "scgUdpPort", "scg_udp_port"), "scgUdpPort"
        ),
        session_id=_text(_first(data, "sessionId", "session_id"), "sessionId"),
        user_name=_text(
            _first(data, "vmUserName", "vm_user_name", "userName"), "vmUserName"
        ),
        auth_code=_text(_first(data, "authCode", "auth_code"), "authCode"),
        biz_code=_text(_first(data, "bizCode", "biz_code"), "bizCode"),
        has_user=_first(data, "vmUserName", "vm_user_name", "userName") is not None,
        has_password=_first(data, "vmPassword", "vm_password", "password") is not None,
        has_auth_code=_first(data, "authCode", "auth_code") is not None,
        has_token=_first(data, "token", "sohoToken", "sessionToken") is not None,
    )
    return descriptor


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", help="JSON file, or '-' for stdin")
    parser.add_argument("--emit-loader-config", metavar="PATH")
    parser.add_argument("--library", help="authorized libchuanyun.so path")
    parser.add_argument("--server-ip", help="public SDK control server host")
    parser.add_argument("--server-port", type=int, help="public SDK control server port")
    parser.add_argument("--terminal-sn")
    parser.add_argument("--unit-type")
    args = parser.parse_args(argv)
    try:
        raw_text = sys.stdin.read() if args.input == "-" else Path(args.input).read_text()
        raw = json.loads(raw_text)
        if not isinstance(raw, Mapping):
            raise SessionFormatError("top-level JSON value must be an object")
        descriptor = parse_session(raw)
        if args.emit_loader_config is not None:
            if args.library is None or args.server_ip is None or args.server_port is None:
                raise SessionFormatError(
                    "--emit-loader-config requires --library, --server-ip and --server-port"
                )
            config = descriptor.loader_config(
                library=args.library,
                server_ip=args.server_ip,
                server_port=args.server_port,
                terminal_sn=args.terminal_sn,
                unit_type=args.unit_type,
            )
            write_private_config(Path(args.emit_loader_config), config)
            print("ydyun-session-probe: private loader config created", file=sys.stderr)
        print(json.dumps(descriptor.redacted(), ensure_ascii=False, sort_keys=True))
        if descriptor.route_count == 0:
            print("ydyun-session-probe: no complete VMC/CAG/SCG route", file=sys.stderr)
            return 2
        return 0
    except (OSError, json.JSONDecodeError, SessionFormatError) as exc:
        print(f"ydyun-session-probe: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
