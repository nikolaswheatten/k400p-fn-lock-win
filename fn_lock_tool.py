#!/usr/bin/env python3
"""
Logitech K400+ Fn Lock — universal apply across all HID++ receivers and device slots.

Requires: pip install hidapi
"""

from __future__ import annotations

import argparse
import sys

LOGITECH_VID = 0x046D
HIDPP_PAGE = 65280
HIDPP_USAGE = 1
# Wireless slots 1-6; 0xFF = directly connected device (HID++ convention)
DEVICE_INDICES = tuple(list(range(1, 7)) + [0xFF])

# Legacy Fn Lock: F1-F12 without Fn (upstream / lenisko k400_plus_conf.c)
FN_LOCK = bytes([0x10, 0x01, 0x09, 0x19, 0x00, 0x00, 0x00])

KNOWN_RECEIVERS = {
    0xC52B: "Unifying receiver",
    0xC52F: "Unifying receiver (nano)",
    0xC534: "Unifying receiver",
    0xC548: "Bolt receiver",
    0xC52A: "Logitech receiver",
}


def receiver_label(pid: int) -> str:
    name = KNOWN_RECEIVERS.get(pid)
    return f"{name} (PID {pid:04X})" if name else f"Logitech receiver (PID {pid:04X})"


def hex_bytes(data: bytes) -> str:
    return " ".join(f"{b:02x}" for b in data)


def hidpp_ack(reply: bytes) -> bool:
    """HID++ 1.x short ACK uses subId 0x8F in byte 2."""
    return len(reply) >= 3 and reply[2] == 0x8F


def find_hidpp_interfaces() -> list[dict]:
    import hid

    seen_paths: set[bytes] = set()
    found: list[dict] = []

    for dev in hid.enumerate(LOGITECH_VID, 0):
        if dev.get("usage_page") != HIDPP_PAGE or dev.get("usage") != HIDPP_USAGE:
            continue
        path = dev["path"]
        if path in seen_paths:
            continue
        seen_paths.add(path)
        found.append(dev)

    return found


def send_fn_lock(path: bytes, device_index: int) -> tuple[bytes, bytes]:
    import hid

    packet = bytearray(FN_LOCK)
    packet[1] = device_index & 0xFF
    handle = hid.device()
    handle.open_path(path)
    try:
        written = handle.write(packet)
        if written != len(packet):
            raise OSError(f"hid write returned {written}, expected {len(packet)}")
        reply = handle.read(64, timeout_ms=500) or b""
        return bytes(packet), bytes(reply)
    finally:
        handle.close()


def slot_label(device_index: int) -> str:
    return "direct (FF)" if device_index == 0xFF else str(device_index)


def apply_fn_lock(*, verbose: bool) -> int:
    interfaces = find_hidpp_interfaces()
    if not interfaces:
        if verbose:
            print("ERROR: no Logitech HID++ interface found (usage_page=65280, usage=1).")
            print("Plug in the K400+ USB dongle and turn the keyboard on.")
        return 2

    if verbose:
        print(f"Found {len(interfaces)} HID++ interface(s).")
        print("Sending Fn Lock to every receiver and every device slot 1-6 + direct (FF)...\n")

    writes_ok = 0
    acks = 0

    for dev in interfaces:
        pid = dev.get("product_id", 0)
        path = dev["path"]
        label = receiver_label(pid)

        if verbose:
            print(f"[{label}]")
            print(f"  path: {path!r}")

        for idx in DEVICE_INDICES:
            slot = slot_label(idx)
            try:
                sent, reply = send_fn_lock(path, idx)
            except OSError as exc:
                if verbose:
                    print(f"  slot {slot}: FAILED: {exc}")
                continue

            writes_ok += 1
            if reply and hidpp_ack(reply):
                acks += 1
                if verbose:
                    print(
                        f"  slot {slot}: sent [{hex_bytes(sent)}] "
                        f"reply [{hex_bytes(reply)}] ACK"
                    )
            elif reply:
                if verbose:
                    print(
                        f"  slot {slot}: sent [{hex_bytes(sent)}] "
                        f"reply [{hex_bytes(reply)}] (no ACK)"
                    )
            elif verbose:
                print(f"  slot {slot}: sent [{hex_bytes(sent)}] reply [none]")

        if verbose:
            print()

    if writes_ok == 0:
        if verbose:
            print("ERROR: could not write to any HID++ interface.")
        return 3

    if verbose:
        print(f"Done: {writes_ok} command(s) sent, {acks} ACK(s).")
        print("Test F2 in Explorer on the K400+ keyboard.")
        print("Fn Lock lasts until reboot.")

    return 0


def diagnose() -> int:
    return apply_fn_lock(verbose=True)


def probe() -> int:
    return 0 if find_hidpp_interfaces() else 2


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Apply Logitech K400+ Fn Lock on all HID++ receivers and device slots"
    )
    parser.add_argument("--apply", action="store_true", help="Send Fn Lock commands")
    parser.add_argument("--diagnose", action="store_true", help="Apply with full diagnostic output")
    parser.add_argument(
        "--probe",
        action="store_true",
        help="Exit 0 if a Logitech HID++ interface is present (no commands sent)",
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Minimal output (for autostart; use with --apply)",
    )
    args = parser.parse_args()

    try:
        import hid  # noqa: F401
    except ImportError:
        print("ERROR: hidapi not installed. Run: pip install hidapi")
        return 1

    if args.probe:
        return probe()

    if args.apply:
        return apply_fn_lock(verbose=not args.quiet)

    if args.diagnose:
        return diagnose()

    return diagnose()


if __name__ == "__main__":
    sys.exit(main())
