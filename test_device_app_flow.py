#!/usr/bin/env python3
"""
Emulates Qorvo Cherry SDK's device_app flow against our UCI chardev simulator.

device_app does: reset(optional) -> get_device_info -> get_device_stats
We test: reset -> get_device_info (our simulator doesn't do get_device_stats).
"""
import subprocess, os, sys, time, struct, termios, fcntl, select

def run_simulator():
    """Start the chardev simulator in a PTY so stderr is line-buffered.
    Returns (process, pty_path, master_fd). Keep master_fd open or sim dies on SIGHUP."""
    import pty
    master_fd, slave_fd = pty.openpty()
    proc = subprocess.Popen(
        ["./build/uci-device-sim-chardev", "default"],
        stdout=subprocess.DEVNULL, stderr=slave_fd,
        cwd="/media/chpo/HDD-papa/gemini_test/uci_device_simulator",
        start_new_session=True,  # detach from controlling terminal
    )
    os.close(slave_fd)

    # Read from master fd until we get the PTY path
    stderr_buf = b""
    pty_path = None
    deadline = time.time() + 5.0
    while time.time() < deadline:
        try:
            chunk = os.read(master_fd, 256)
            if chunk:
                stderr_buf += chunk
                if b"slave=" in stderr_buf:
                    for line in stderr_buf.split(b"\n"):
                        if b"slave=" in line:
                            pty_path = line.split(b"slave=")[1].strip().decode()
                            break
            if pty_path:
                break
        except BlockingIOError:
            time.sleep(0.05)
    if not pty_path:
        proc.kill(); os.close(master_fd)
        raise RuntimeError(f"Failed to get PTY path, stderr: {stderr_buf.decode()}")
    # NOTE: keep master_fd open — closing it sends SIGHUP to simulator
    return proc, pty_path, master_fd

def open_pty(path):
    """Open PTY slave, set raw mode manually (cfmakeraw not in Python termios)."""
    fd = os.open(path, os.O_RDWR | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    # cfmakeraw equivalent
    attrs[0] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK | termios.ISTRIP |
                   termios.INLCR | termios.IGNCR | termios.ICRNL | termios.IXON)
    attrs[1] &= ~termios.OPOST
    attrs[2] &= ~(termios.CSIZE | termios.PARENB)
    attrs[2] |= termios.CS8
    attrs[3] &= ~(termios.ECHO | termios.ECHONL | termios.ICANON | termios.ISIG | termios.IEXTEN)
    if hasattr(termios, 'VMIN'):
        attrs[6][termios.VMIN] = 1
        attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd

def rw(fd, cmd_bytes=None, expect_status=0x00, label=""):
    """Write optional command, poll+read ALL available packets.
    Returns list of (raw_bytes, parsed_fields)."""
    if cmd_bytes:
        os.write(fd, bytes(cmd_bytes))
    time.sleep(0.01)  # let PTY propagate

    all_buf = b""
    deadline = time.time() + 3.0
    while time.time() < deadline:
        r, _, _ = select.select([fd], [], [], 0.3)
        if r:
            try:
                chunk = os.read(fd, 4096)
            except BlockingIOError:
                continue
            if chunk:
                all_buf += chunk
                continue  # keep reading while data flows
            else:
                break  # EOF
        elif all_buf:
            break  # no more data after we got something

    if not all_buf:
        print(f"  {label}: TIMEOUT — no data")
        return []

    # Parse all complete packets from the buffer
    packets = []
    offset = 0
    mt_names = {1: "CMD", 2: "RSP", 3: "NTF", 0: "DATA"}  # Qorvo UCI: NT=3=NTF, 0=DATA
    gid_names = {0: "CORE", 1: "SESSION_CONFIG", 2: "SESSION_CONTROL"}
    status_names = {0x00: "OK", 0x01: "REJECTED", 0x06: "INVALID_MSG_SIZE",
                    0x08: "UNKNOWN_OID", 0x0A: "UNKNOWN_GID"}

    while offset + 4 <= len(all_buf):
        mt = (all_buf[offset] >> 5) & 0x03
        # Qorvo UCI: MT=0 (DATA) uses LE u16, all others use byte[3] only
        plen = (all_buf[offset+2] | (all_buf[offset+3] << 8)) if mt == 0 else all_buf[offset+3]
        pkt_total = 4 + plen
        if offset + pkt_total > len(all_buf):
            break  # partial packet
        pkt = all_buf[offset:offset + pkt_total]
        gid = all_buf[offset] & 0x0F
        oid = all_buf[offset+1] & 0x3F
        status = pkt[4] if plen > 0 else None
        print(f"  {label}: MT={mt_names.get(mt, mt)} GID={gid_names.get(gid, gid)} "
              f"OID=0x{oid:02X} status={status_names.get(status, f'0x{status:02X}' if status is not None else 'N/A')} "
              f"({pkt_total} bytes)")
        packets.append((pkt, {"mt": mt, "gid": gid, "oid": oid, "plen": plen, "status": status}))
        offset += pkt_total
    return packets

def test_device_app_flow():
    print("═══ Qorvo device_app flow against chardev simulator ═══\n")

    proc, pty, stderr_master = run_simulator()
    print(f"Simulator PTY: {pty}\n")
    fd = open_pty(pty)

    try:
        # 1. DEVICE_RESET (soft) — matches cherry_reset_device(ctx, false)
        print("1. DEVICE_RESET (soft)")
        # UCI header: MT=1(CMD) PBF=0 GID=0(CORE) OID=0x00(RESET) payload_len=1 reset_type=0x00
        cmd = bytes([0x20, 0x00, 0x00, 0x01, 0x00])
        pkts = rw(fd, cmd, label="  RESET")
        assert len(pkts) >= 2, f"Expected RSP+NTF, got {len(pkts)} packets"
        # First packet: response
        assert pkts[0][1]["status"] == 0x00, f"RESET failed: {pkts[0][1]}"
        # Second packet: DEVICE_STATUS_NTF (READY)
        assert pkts[1][1]["oid"] == 0x01, f"Expected STATUS_NTF, got oid=0x{pkts[1][1]['oid']:02X}"
        assert pkts[1][1]["status"] == 0x01, f"Expected READY ntf, got status={pkts[1][1]['status']}"

        # 2. DEVICE_INFO — matches cherry_get_device_info(ctx)
        print("\n2. DEVICE_INFO")
        cmd = bytes([0x20, 0x02, 0x00, 0x00])
        pkts = rw(fd, cmd, label="  DEVICE_INFO")
        assert len(pkts) >= 1, f"No response"
        p = pkts[0][1]
        assert p["status"] == 0x00, f"DEVICE_INFO failed"
        if p["plen"] >= 10:
            uci_ver = pkts[0][0][5] | (pkts[0][0][6] << 8)
            mac_ver = pkts[0][0][7] | (pkts[0][0][8] << 8)
            phy_ver = pkts[0][0][9] | (pkts[0][0][10] << 8)
            uci_test = pkts[0][0][11] | (pkts[0][0][12] << 8)
            print(f"  UCI={uci_ver}.{mac_ver}  MAC={mac_ver}  PHY={phy_ver}  TEST={uci_test}")

        print(f"\n✅ SUCCESS — device_app flow passed")

    finally:
        os.close(fd)
        os.close(stderr_master)
        proc.terminate()
        proc.wait()

if __name__ == "__main__":
    test_device_app_flow()
