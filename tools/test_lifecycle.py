#!/usr/bin/env python3
"""End-to-end lifecycle test for improv-wifi-busware.

Verifies the spec-mandated 120 s post-boot window:
  1. Right after reset:           GET_DEVICE_INFO must succeed.
  2. ~70 s after reset:            GET_DEVICE_INFO must still succeed
                                   (proves the window is still open).
  3. ~135 s after reset:           GET_DEVICE_INFO must NOT receive a
                                   response (proves hard shutdown).
  4. After expiry:                 the test firmware's "?\\n" probe must
                                   answer "STATUS armed=0 ..." -- proves
                                   the application is still alive, just the
                                   Improv lib is silent.

Total runtime ~150 s. The window length is hard-coded to 120 s in the
firmware so we can't shorten it for tests.
"""

import argparse
import os
import sys
import time
import serial

# Reuse the proven Improv frame helpers from the standalone client.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import improv_client  # noqa: E402

# Pass --port explicitly. There is no useful default because auto-detect on
# multi-board lab hosts grabs the wrong device frequently; better to fail
# loud than to flash/probe an unrelated board.
DEFAULT_PORT = None

WINDOW_MS    = 120_000
PROBE_AT_MS  = (5_000, 70_000, 135_000)


_LIB_FRAME_TYPES = {
    improv_client.ImprovClient.TYPE_CURRENT_STATE,   # 0x01
    improv_client.ImprovClient.TYPE_ERROR,           # 0x02
    improv_client.ImprovClient.TYPE_RPC_RESPONSE,    # 0x04
}
# Type 0x03 (TYPE_RPC) is the host-to-device direction; if an application
# echoes serial bytes back to the host after the window has closed, the
# echoed RPC frames will still parse as a valid IMPROV packet but cannot
# possibly originate from the library. Treat such frames as noise.


def _read_lib_frame(client: improv_client.ImprovClient, timeout: float):
    """Read packets until one originates from the library (or timeout)."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        remaining = deadline - time.time()
        if remaining <= 0:
            break
        pkt = client.read_packet(timeout=remaining)
        if not pkt:
            return None
        ptype, _ = pkt
        if ptype in _LIB_FRAME_TYPES:
            return pkt
        # Otherwise: echoed RPC frame, application banner garbage, etc. Ignore.
    return None


def send_info(client: improv_client.ImprovClient, label: str, expect_response: bool):
    print(f"\n[{label}] sending GET_DEVICE_INFO; expecting {'response' if expect_response else 'silence'}")
    client.buffer.clear()
    client.send_command(client.CMD_GET_DEVICE_INFO)
    pkt = _read_lib_frame(client, timeout=5.0)
    if expect_response:
        if not pkt:
            print(f"[{label}] FAIL -- no library-side response within 5 s")
            return False
        ptype, data = pkt
        print(f"[{label}] OK  -- response type=0x{ptype:02X}, {len(data)} bytes")
        return True
    if pkt:
        ptype, data = pkt
        print(f"[{label}] FAIL -- got library frame type=0x{ptype:02X}, {len(data)} bytes (should be silent)")
        return False
    print(f"[{label}] OK  -- no library-side response (lib silent)")
    return True


def probe_status(client: improv_client.ImprovClient) -> str:
    """Drain serial, send the test firmware's '?\\n' probe, return STATUS line."""
    client.buffer.clear()
    while client.ser.in_waiting:
        client.ser.read(client.ser.in_waiting)
    client.ser.write(b"?\n")
    deadline = time.time() + 2.0
    accumulated = bytearray()
    while time.time() < deadline:
        chunk = client.ser.read(128)
        if chunk:
            accumulated.extend(chunk)
            if b"STATUS" in accumulated and b"\n" in accumulated:
                # Extract the STATUS line.
                idx = accumulated.find(b"STATUS")
                end = accumulated.find(b"\n", idx)
                if end > idx:
                    return accumulated[idx:end].decode("utf-8", errors="ignore")
        time.sleep(0.05)
    return ""


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", default=DEFAULT_PORT, required=DEFAULT_PORT is None,
                   help="Serial device, e.g. /dev/serial/by-id/usb-Espressif_USB_JTAG_...-if00")
    p.add_argument("--baud", type=int, default=115200)
    args = p.parse_args()

    print(f"[lifecycle] target: {args.port}")
    print(f"[lifecycle] firmware window: {WINDOW_MS} ms")
    print(f"[lifecycle] probe schedule (ms since reset): {PROBE_AT_MS}")

    client = improv_client.ImprovClient(args.port, args.baud)
    if not client.connect(reset_device=True):
        return 2

    boot_t0 = time.time()
    # Settle period after reset; firmware boots in well under 1 s.
    time.sleep(0.5)

    results = []

    def elapsed_ms() -> int:
        return int((time.time() - boot_t0) * 1000)

    try:
        # Probe 1: very early (well within window).
        target = PROBE_AT_MS[0]
        if elapsed_ms() < target:
            time.sleep(max(0.0, (target - elapsed_ms()) / 1000.0))
        results.append(("early@5s", send_info(client, f"early t={elapsed_ms()}ms", expect_response=True)))

        # Probe 2: middle of the window.
        target = PROBE_AT_MS[1]
        wait = (target - elapsed_ms()) / 1000.0
        if wait > 0:
            print(f"\n[lifecycle] waiting {wait:.1f}s until middle-of-window probe")
            time.sleep(wait)
        results.append(("mid@70s", send_info(client, f"mid t={elapsed_ms()}ms", expect_response=True)))

        # Probe 3: well after expiry (window is 120 s, probe at 135 s).
        target = PROBE_AT_MS[2]
        wait = (target - elapsed_ms()) / 1000.0
        if wait > 0:
            print(f"\n[lifecycle] waiting {wait:.1f}s until post-expiry probe")
            time.sleep(wait)
        results.append(("post@135s", send_info(client, f"post t={elapsed_ms()}ms", expect_response=False)))

        # Status probe -- application must still be alive.
        status = probe_status(client)
        ok_status = bool(status) and "armed=0" in status
        print(f"\n[status] reply: {status!r}")
        print(f"[status] {'OK' if ok_status else 'FAIL'} -- expected 'armed=0' in reply")
        results.append(("status armed=0", ok_status))

    finally:
        client.close()

    print("\n[lifecycle] summary:")
    all_ok = True
    for name, ok in results:
        print(f"  {'PASS' if ok else 'FAIL'}  {name}")
        all_ok = all_ok and ok
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
