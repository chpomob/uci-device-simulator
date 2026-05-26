#!/usr/bin/env python3
"""
Emulates Qorvo Cherry SDK's twr_app ranging flow against our UCI chardev simulator.

twr_app flow (simplified):
  RESET -> DEVICE_INFO -> SESSION_INIT -> SET_APP_CONFIGS -> SESSION_START ->
  [RANGE_DATA_NTF...] -> SESSION_STOP
"""
import subprocess, os, sys, time, struct, termios, fcntl, select, pty

# ── Simulator management ────────────────────────────────────

def start_sim(scenario="ranging_stream"):
    """Start chardev simulator, return (process, pty_path)."""
    master_fd, slave_fd = pty.openpty()
    proc = subprocess.Popen(
        ["./build/uci-device-sim-chardev", scenario],
        stdout=subprocess.DEVNULL, stderr=slave_fd,
        cwd="/media/chpo/HDD-papa/gemini_test/uci_device_simulator",
        start_new_session=True)
    os.close(slave_fd)
    buf = b""
    while b"slave=" not in buf: buf += os.read(master_fd, 256)
    path = buf.split(b"slave=")[1].split(b"\r")[0].strip().decode()
    return proc, path, master_fd

def open_slave(path):
    fd = os.open(path, os.O_RDWR | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    for m in [termios.IGNBRK, termios.BRKINT, termios.PARMRK, termios.ISTRIP,
              termios.INLCR, termios.IGNCR, termios.ICRNL, termios.IXON]: a[0] &= ~m
    a[1] &= ~termios.OPOST
    a[2] = (a[2] & ~(termios.CSIZE | termios.PARENB)) | termios.CS8
    for m in [termios.ECHO, termios.ECHONL, termios.ICANON, termios.ISIG, termios.IEXTEN]: a[3] &= ~m
    a[6][termios.VMIN] = 1; a[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, a)
    return fd

# ── Packet I/O ───────────────────────────────────────────────

def rw(fd, cmd=None, label="", timeout=3.0):
    """Write optional command, read all response packets."""
    if cmd: os.write(fd, bytes(cmd))
    time.sleep(0.02)
    buf = b""
    dl = time.time() + timeout
    while time.time() < dl:
        r, _, _ = select.select([fd], [], [], 0.3)
        if r:
            try: chunk = os.read(fd, 4096)
            except BlockingIOError: continue
            if chunk: buf += chunk; continue
            else: break
        elif buf: break
    if not buf: return []
    pkts = []
    off = 0
    while off + 4 <= len(buf):
        mt = (buf[off] >> 5) & 0x03
        plen = (buf[off+2] | (buf[off+3] << 8)) if mt == 0 else buf[off+3]
        total = 4 + plen
        if off + total > len(buf): break
        pkts.append((buf[off:off+total], mt, buf[off] & 0x0F, buf[off+1] & 0x3F))
        off += total
    return pkts

def status_name(s): return {0x00:"OK",0x01:"REJECTED",0x06:"INVALID_MSG_SIZE",
    0x08:"UNKNOWN_OID",0x09:"INVALID_PARAM",0x0A:"UNKNOWN_GID",0x11:"INVALID_RANGE"}.get(s,f"0x{s:02X}")

def show(pkts, label):
    mt_n = {1:"CMD",2:"RSP",3:"NTF",0:"DATA"}
    gid_n = {0:"CORE",1:"SESSION_CFG",2:"SESSION_CTL"}
    for raw,mt,gid,oid in pkts:
        st = status_name(raw[4]) if len(raw) > 4 else "N/A"
        print(f"  {label}: MT={mt_n.get(mt,mt)} GID={gid_n.get(gid,gid)} OID=0x{oid:02X} status={st} ({len(raw)}B)")

# ── UCI helpers ──────────────────────────────────────────────

def make_set_app_config(session_id, configs):
    """Session id LE u32 + list of (config_id, value_bytes)."""
    payload = struct.pack("<I", session_id)
    payload += bytes([len(configs)])  # num TLVs
    for cid, val in configs:
        payload += bytes([cid, len(val)]) + bytes(val)
    hdr = bytes([0x21, 0x03])  # MT=CMD, SESSION_CONFIG, SET_APP_CONFIG
    # Payload len in byte[3] for non-DATA
    hdr += bytes([0x00, len(payload)])
    return hdr + payload

def make_session_init(session_id, session_type=0x00):
    """UCI_MT_COMMAND | SESSION_CONFIG | SESSION_INIT"""
    payload = struct.pack("<I", session_id) + bytes([session_type])
    hdr = bytes([0x21, 0x00, 0x00, len(payload)])
    return hdr + payload

def make_session_control(oid, session_id):
    hdr = bytes([0x22, oid, 0x00, 4])
    return hdr + struct.pack("<I", session_id)

# ── Main test ────────────────────────────────────────────────

def test_ranging_stream():
    print("═══ Qorvo twr_app ranging flow ═══\n")

    proc, pty_path, stderr_m = start_sim("ranging_stream")
    print(f"PTY: {pty_path}\n")
    fd = open_slave(pty_path)
    SID = 0x12345678

    try:
        # 1. RESET
        print("1. DEVICE_RESET")
        pkts = rw(fd, bytes([0x20,0x00,0x00,0x01,0x00]), "RESET")
        assert len(pkts) >= 2 and pkts[0][3] == 0x00, "RESET failed"

        # 2. DEVICE_INFO
        print("2. DEVICE_INFO")
        pkts = rw(fd, bytes([0x20,0x02,0x00,0x00]), "DEV_INFO")
        assert pkts and pkts[0][3] == 0x02, "DEVICE_INFO failed"

        # 3. SESSION_INIT (RANGING type)
        print("3. SESSION_INIT")
        pkts = rw(fd, make_session_init(SID, 0x00), "SESS_INIT")
        show(pkts, "SESS_INIT")
        assert any(p[3]==0x00 and p[1]==2 for p in pkts), "SESSION_INIT rsp missing"

        # 4. SET_APP_CONFIG — required for START
        #    DEVICE_ROLE=RESPONDER(0x01), DEVICE_TYPE=CONTROLEE(0x00),
        #    RANGING_INTERVAL=100ms, SLOTS_PER_RR=4
        print("4. SET_APP_CONFIG")
        configs = [
            (0x11, [0x00]),   # DEVICE_ROLE = RESPONDER (0x00, not 0x01!)
            (0x00, [0x00]),   # DEVICE_TYPE = CONTROLEE
            (0x09, [100,0,0,0]), # RANGING_INTERVAL = 100ms LE
            (0x1B, [4]),      # SLOTS_PER_RR = 4 (uint8, 1 byte)
        ]
        pkts = rw(fd, make_set_app_config(SID, configs), "SET_CFG")
        show(pkts, "SET_CFG")
        assert any(p[3]==0x03 and status_name(p[0][4])=="OK" for p in pkts), "SET_APP_CONFIG failed"

        # 5. SESSION_START — range data may arrive in same read batch
        print("5. SESSION_START")
        pkts = rw(fd, make_session_control(0x00, SID), "START")
        show(pkts, "START")
        assert any(p[3]==0x00 and status_name(p[0][4])=="OK" for p in pkts), "SESSION_START failed"
        # Count any range data that arrived during START
        start_range = sum(1 for raw,mt,_,oid in pkts if mt==3 and oid==0x00 and len(raw) > 25)
        if start_range:
            print(f"  (got {start_range} RANGE_DATA with START response)")

        # 6. Collect remaining RANGE_DATA_NTF
        print("6. Waiting for more RANGE_DATA_NTF...")
        range_count = start_range
        buf = b""
        dl = time.time() + 5.0
        while time.time() < dl and range_count < 5:
            r, _, _ = select.select([fd], [], [], 1.0)
            if r:
                try: chunk = os.read(fd, 4096)
                except BlockingIOError: continue
                if chunk: buf += chunk
            off = 0
            while off + 4 <= len(buf):
                mt = (buf[off] >> 5) & 0x03
                plen = (buf[off+2] | (buf[off+3] << 8)) if mt == 0 else buf[off+3]
                total = 4 + plen
                if off + total > len(buf): break
                if mt == 3 and buf[off+1] & 0x3F == 0x00 and total > 25:
                    range_count += 1
                    seq = buf[off+4] | (buf[off+5] << 8) | (buf[off+6] << 16) | (buf[off+7] << 24)
                    if range_count <= 5:
                        print(f"  RANGE_DATA #{range_count}: seq={seq} ({total}B)")
                off += total
        print(f"  Total: {range_count} RANGE_DATA_NTF received")

        # 7. SESSION_STOP
        print("7. SESSION_STOP")
        pkts = rw(fd, make_session_control(0x01, SID), "STOP")
        show(pkts, "STOP")
        assert any(p[3]==0x01 and status_name(p[0][4])=="OK" for p in pkts), "SESSION_STOP failed"

        print(f"\n✅ SUCCESS — twr_app ranging flow: {range_count} measurements")

    finally:
        os.close(fd)
        os.close(stderr_m)
        proc.terminate()
        proc.wait()

if __name__ == "__main__":
    test_ranging_stream()
