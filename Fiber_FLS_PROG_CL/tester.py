"""Component tester: emulate the jig server, exercise the real JigClient.

This is a host-side, no-hardware component test. It stands up a faithful
in-process TCP server that speaks the exact wire protocol the firmware speaks
(``protocol.h`` / ``command.c``), backs it with a simulated W25Qxx flash array,
then runs the *real* :class:`jig_client.JigClient` against it and asserts both
the client's externally observable behavior and the resulting flash contents.

The mock server is configurable to inject faults (drop the link, return error
statuses, lie about progress, corrupt a byte) so the client's failure handling
is tested too.

Every request and response frame is logged — decoded field-by-field — to a
fresh log file created each run (``tester_<timestamp>.log`` by default; see
``--log``). The full suite emits thousands of frames, so by default they go only
to that file and the terminal stays a clean PASS/FAIL summary; ``--frames`` and
``-v`` also echo frames to the terminal.

Run:  python tester.py            # suite; frames -> log file, PASS/FAIL on screen
      python tester.py -v         # also echo frames + client logs to the terminal
      python tester.py --frames   # one clean single-cycle frame transcript on screen
      python tester.py --log x.log # choose the frame log file path
"""

from __future__ import annotations

import argparse
import logging
import socket
import struct
import threading
import time
from datetime import datetime
from pathlib import Path

import protocol
from protocol import Cmd, Erase, Status
from jig_client import JigClient


# ---------------------------------------------------------------------- #
# Mock server — emulates one programmer jig                              #
# ---------------------------------------------------------------------- #

ERASED_BYTE = 0xFF  # NOR flash erases to all-ones

# Frame tracing: when enabled, the mock server logs every request it decodes and
# every response it sends, byte-for-byte, so the wire protocol can be validated.
# Output goes through the dedicated 'frames' logger (terminal + per-run file).
TRACE_FRAMES = False
frame_log = logging.getLogger("frames")


def _cmd_name(cmd: int) -> str:
    try:
        return Cmd(cmd).name
    except ValueError:
        return f"0x{cmd:02X}"


def _status_name(status: int) -> str:
    try:
        return Status(status).name
    except ValueError:
        return f"0x{status:02X}"


def _hex(b: bytes) -> str:
    return b.hex(" ").upper() if b else "<none>"


def _trace_request(raw: bytes, cmd: int, payload: bytes) -> None:
    """Log one request frame as the *server* received it (A5 ... CRC16).

    The client side is already logged by ``protocol.py`` (``TX REQ``); this
    ``SRV RX`` line confirms the server saw the same bytes off the wire.
    """
    if not TRACE_FRAMES:
        return
    sof, chip, _cmd, length = struct.unpack(">BBBH", raw[:protocol.REQ_HDR_LEN])
    crc = (raw[-2] << 8) | raw[-1]
    frame_log.info(
        "SRV RX  <- %s | SOF=0x%02X CHIP=%d CMD=%s LEN=%d PAYLOAD=[%s] CRC=0x%04X",
        _hex(raw), sof, chip, _cmd_name(cmd), length, _hex(payload), crc,
    )


def _trace_response(raw: bytes, cmd: int, status: int, resp: bytes) -> None:
    """Log one response frame as the *server* sent it (5A ... CRC16).

    Pairs with ``protocol.py``'s ``RX RESP`` (client view); this ``SRV TX``
    line shows the exact bytes the server put on the wire, CRC included.
    """
    if not TRACE_FRAMES:
        return
    sof, chip, _cmd, st, length = struct.unpack(">BBBBH", raw[:protocol.RESP_HDR_LEN])
    crc = (raw[-2] << 8) | raw[-1]
    frame_log.info(
        "SRV TX  -> %s | SOF=0x%02X CHIP=%d CMD=%s STATUS=%s LEN=%d RESP=[%s] CRC=0x%04X",
        _hex(raw), sof, chip, _cmd_name(cmd), _status_name(status), length,
        _hex(resp), crc,
    )


class MockJigServer:
    """A single-client TCP server emulating the firmware command server.

    Implements the same request/response framing and the command semantics in
    ``command.c``: READ_ID, ERASE (sector/block/chip), WRITE_STREAM_START/DATA
    (implicit advancing address, ACK carries running bytes_done), and VERIFY.
    Backs writes with a ``bytearray`` "flash" so tests can verify contents.

    Fault-injection knobs (all default off):
      fail_cmd      : Cmd -> Status      return this status for the given command
      lie_progress  : bool               WRITE_STREAM_DATA ACK reports a wrong count
      drop_after    : int | None         close the socket after N requests served
      corrupt_write : bool               flip one bit of the first written byte
    """

    def __init__(
        self,
        *,
        chip_id: bytes = b"\xDE\xAD\xBE\xEF",
        capacity: int = 2 * 1024 * 1024,
        chip: int = 0,
        fail_cmd: dict[int, int] | None = None,
        lie_progress: bool = False,
        drop_after: int | None = None,
        corrupt_write: bool = False,
        verify_crc: int = 0x0000,
    ):
        self.chip_id = chip_id
        self.capacity = capacity
        self.chip = chip
        self.fail_cmd = fail_cmd or {}
        self.lie_progress = lie_progress
        self.drop_after = drop_after
        self.corrupt_write = corrupt_write
        self.verify_crc = verify_crc

        self.flash = bytearray([ERASED_BYTE]) * capacity
        self.requests_served = 0

        # Stream session state (mirrors command.c g_sess).
        self._sess_start = 0
        self._sess_total = 0
        self._sess_done = 0

        self._srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._srv.bind(("127.0.0.1", 0))
        self._srv.listen(1)
        self.port = self._srv.getsockname()[1]
        self._thread = threading.Thread(target=self._serve, daemon=True)

    # -- lifecycle -- #

    def __enter__(self) -> "MockJigServer":
        self._thread.start()
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def close(self) -> None:
        try:
            self._srv.close()
        except OSError:
            pass
        self._thread.join(timeout=2.0)

    # -- wire helpers -- #

    @staticmethod
    def _recv_exact(conn: socket.socket, n: int) -> bytes | None:
        buf = b""
        while len(buf) < n:
            chunk = conn.recv(n - len(buf))
            if not chunk:
                return None
            buf += chunk
        return buf

    def _read_request(self, conn: socket.socket):
        """Return (cmd, payload) or None if the connection closed."""
        hdr = self._recv_exact(conn, protocol.REQ_HDR_LEN)
        if hdr is None:
            return None
        sof, chip, cmd, length = struct.unpack(">BBBH", hdr)
        assert sof == protocol.SOF_REQ, f"bad request SOF 0x{sof:02X}"
        rest = self._recv_exact(conn, length + protocol.CRC_LEN)
        if rest is None:
            return None
        _trace_request(hdr + rest, cmd, rest[:length])
        return cmd, rest[:length]

    def _send(self, conn: socket.socket, cmd: int, status: int, resp: bytes = b"") -> None:
        frame = bytes([protocol.SOF_RESP, self.chip, cmd, status])
        frame += struct.pack(">H", len(resp)) + resp + b"\x00\x00"
        _trace_response(frame, cmd, status, resp)
        conn.sendall(frame)

    # -- main loop -- #

    def _serve(self) -> None:
        try:
            conn, _ = self._srv.accept()
        except OSError:
            return
        with conn:
            while True:
                req = self._read_request(conn)
                if req is None:
                    return
                cmd, payload = req
                self.requests_served += 1

                if self.drop_after is not None and self.requests_served > self.drop_after:
                    return  # simulate the link dying mid-session

                forced = self.fail_cmd.get(cmd)
                if forced is not None:
                    self._send(conn, cmd, forced)
                    continue

                self._dispatch(conn, cmd, payload)

    def _dispatch(self, conn: socket.socket, cmd: int, payload: bytes) -> None:
        if cmd == Cmd.READ_ID:
            self._send(conn, cmd, Status.OK, self.chip_id)

        elif cmd == Cmd.ERASE:
            scope = payload[0]
            if scope == Erase.CHIP:
                for i in range(len(self.flash)):
                    self.flash[i] = ERASED_BYTE
            self._send(conn, cmd, Status.OK)

        elif cmd == Cmd.WRITE_STREAM_START:
            self._sess_start = _rd_u24(payload[0:3])
            self._sess_total = _rd_u24(payload[3:6])
            self._sess_done = 0
            self._send(conn, cmd, Status.OK)

        elif cmd == Cmd.WRITE_STREAM_DATA:
            addr = self._sess_start + self._sess_done
            self.flash[addr:addr + len(payload)] = payload
            if self.corrupt_write and self._sess_done == 0 and payload:
                self.flash[addr] ^= 0x01  # silent corruption (VERIFY would catch w/ real CRC)
            self._sess_done += len(payload)
            reported = self._sess_done + (7 if self.lie_progress else 0)
            self._send(conn, cmd, Status.OK, _u24(reported))

        elif cmd == Cmd.VERIFY:
            expected = struct.unpack(">H", payload)[0]
            status = Status.OK if expected == self.verify_crc else Status.ERR_VERIFY
            self._send(conn, cmd, status)

        else:
            self._send(conn, cmd, Status.ERR_CMD)


def _u24(v: int) -> bytes:
    return struct.pack(">I", v & 0xFFFFFF)[1:]


def _rd_u24(b: bytes) -> int:
    return (b[0] << 16) | (b[1] << 8) | b[2]


# ---------------------------------------------------------------------- #
# Test harness                                                           #
# ---------------------------------------------------------------------- #

class TestRunner:
    def __init__(self) -> None:
        self.passed = 0
        self.failed = 0

    def run(self, name: str, fn) -> None:
        if TRACE_FRAMES:
            frame_log.info("===== %s =====", name)
        try:
            fn()
        except AssertionError as exc:
            self.failed += 1
            print(f"  FAIL  {name}\n          {exc}")
        except Exception as exc:  # noqa: BLE001 — surface anything unexpected
            self.failed += 1
            print(f"  ERROR {name}\n          {type(exc).__name__}: {exc}")
        else:
            self.passed += 1
            print(f"  PASS  {name}")

    def summary(self) -> int:
        total = self.passed + self.failed
        print(f"\n{self.passed}/{total} passed, {self.failed} failed")
        return 0 if self.failed == 0 else 1


def make_image(n: int) -> bytes:
    """A deterministic, non-uniform image (so a wrong/short write is detectable)."""
    return bytes((i * 31 + 7) & 0xFF for i in range(n))


# Small capacity keeps the simulated flash + erase loop fast in tests.
CAP = 64 * 1024


# ---------------------------------------------------------------------- #
# Tests                                                                  #
# ---------------------------------------------------------------------- #

def test_full_program_cycle() -> None:
    """Happy path: read_id, erase, stream, verify; flash matches the image."""
    image = make_image(2000)  # 62 full 32B frames + a 16B tail
    with MockJigServer(capacity=CAP) as srv:
        cli = JigClient("127.0.0.1", port=srv.port, image=image, device_id=0,
                        connect_timeout=2.0, op_timeout=2.0, erase_timeout=5.0)
        ok = cli.run()
        r = cli.result()
        assert ok and r.passed, f"run failed: {r.error}"
        assert r.phase == "DONE", r.phase
        assert r.chip_id == "DEADBEEF", r.chip_id
        assert r.bytes_written == len(image), r.bytes_written
        assert bytes(srv.flash[:len(image)]) == image, "flash content mismatch"
        # bytes past the image stay erased
        assert all(b == ERASED_BYTE for b in srv.flash[len(image):]), "tail not erased"


def test_exact_page_multiple() -> None:
    """An image that is an exact multiple of MAX_DATA streams with no tail frame."""
    image = make_image(protocol.MAX_DATA * 10)
    with MockJigServer(capacity=CAP) as srv:
        cli = JigClient("127.0.0.1", port=srv.port, image=image,
                        connect_timeout=2.0, op_timeout=2.0, erase_timeout=5.0)
        assert cli.run(), cli.error
        assert bytes(srv.flash[:len(image)]) == image


def test_empty_image() -> None:
    """A zero-length image: START says total=0, no DATA frames, still verifies."""
    with MockJigServer(capacity=CAP) as srv:
        cli = JigClient("127.0.0.1", port=srv.port, image=b"",
                        connect_timeout=2.0, op_timeout=2.0, erase_timeout=5.0)
        assert cli.run(), cli.error
        assert cli.result().bytes_written == 0


def test_chip_id_reported() -> None:
    """Client surfaces the server's chip id verbatim."""
    with MockJigServer(capacity=CAP, chip_id=b"\x01\x23\x45\x67") as srv:
        cli = JigClient("127.0.0.1", port=srv.port, image=make_image(64),
                        connect_timeout=2.0, op_timeout=2.0, erase_timeout=5.0)
        assert cli.run(), cli.error
        assert cli.result().chip_id == "01234567"


def test_erase_failure_caught() -> None:
    """Server returns ERR_FLASH on ERASE -> client fails in ERASING, no raise."""
    with MockJigServer(capacity=CAP, fail_cmd={Cmd.ERASE: Status.ERR_FLASH}) as srv:
        cli = JigClient("127.0.0.1", port=srv.port, image=make_image(64),
                        connect_timeout=2.0, op_timeout=2.0, erase_timeout=5.0)
        assert not cli.run()
        assert cli.phase.name == "FAILED" and "ERR_FLASH" in cli.error, cli.error


def test_verify_failure_caught() -> None:
    """Server's CRC disagrees -> VERIFY returns ERR_VERIFY -> client fails."""
    with MockJigServer(capacity=CAP, verify_crc=0xBEEF) as srv:  # client sends 0x0000
        cli = JigClient("127.0.0.1", port=srv.port, image=make_image(64),
                        connect_timeout=2.0, op_timeout=2.0, erase_timeout=5.0)
        assert not cli.run()
        assert cli.phase.name == "FAILED" and "ERR_VERIFY" in cli.error, cli.error


def test_progress_desync_caught() -> None:
    """Server lies about bytes_done -> client detects desync and aborts."""
    with MockJigServer(capacity=CAP, lie_progress=True) as srv:
        cli = JigClient("127.0.0.1", port=srv.port, image=make_image(64),
                        connect_timeout=2.0, op_timeout=2.0, erase_timeout=5.0)
        assert not cli.run()
        assert "desync" in cli.error, cli.error


def test_link_drop_caught() -> None:
    """Link dies mid-stream -> client fails cleanly (no hang, no raise)."""
    with MockJigServer(capacity=CAP, drop_after=3) as srv:  # die early in the stream
        cli = JigClient("127.0.0.1", port=srv.port, image=make_image(4000),
                        connect_timeout=2.0, op_timeout=2.0, erase_timeout=5.0)
        assert not cli.run()
        assert cli.phase.name == "FAILED" and cli.error, cli.error


def test_connection_refused() -> None:
    """No server on the port -> connect fails, run() returns False."""
    cli = JigClient("127.0.0.1", port=1, image=make_image(16), connect_timeout=1.0)
    assert not cli.run()
    assert cli.error, "expected an error string"


def test_fleet_parallel() -> None:
    """Several clients drive several servers concurrently (fleet emulation).

    Frame tracing is suppressed here: 4 concurrent connections would interleave
    frames in the log unreadably. The single-connection tests already trace.
    """
    global TRACE_FRAMES
    saved_trace, TRACE_FRAMES = TRACE_FRAMES, False
    image = make_image(3000)
    servers = [MockJigServer(capacity=CAP) for _ in range(4)]
    for s in servers:
        s.__enter__()
    try:
        clients = [
            JigClient("127.0.0.1", port=s.port, image=image, device_id=i,
                      connect_timeout=2.0, op_timeout=2.0, erase_timeout=5.0)
            for i, s in enumerate(servers)
        ]
        threads = [threading.Thread(target=c.run) for c in clients]
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=30.0)
        assert all(c.result().passed for c in clients), \
            [c.result().error for c in clients]
        assert all(bytes(s.flash[:len(image)]) == image for s in servers)
    finally:
        for s in servers:
            s.close()
        TRACE_FRAMES = saved_trace


TESTS = [
    ("full_program_cycle", test_full_program_cycle),
    ("exact_page_multiple", test_exact_page_multiple),
    ("empty_image", test_empty_image),
    ("chip_id_reported", test_chip_id_reported),
    ("erase_failure_caught", test_erase_failure_caught),
    ("verify_failure_caught", test_verify_failure_caught),
    ("progress_desync_caught", test_progress_desync_caught),
    ("link_drop_caught", test_link_drop_caught),
    ("connection_refused", test_connection_refused),
    ("fleet_parallel", test_fleet_parallel),
]


def trace_demo() -> None:
    """Run one full program cycle on a tiny image with every frame logged.

    Validates the wire protocol: each request and response is decoded
    field-by-field (SOF/CHIP/CMD/LEN/PAYLOAD/CRC). Uses a small image so the
    transcript stays short and the partial tail frame is visible.
    """
    image = bytes(range(40))  # 1 full 32B frame + an 8B tail frame
    frame_log.info("===== trace_demo: full cycle, image = %d bytes (MAX_DATA = %d) =====",
                   len(image), protocol.MAX_DATA)
    with MockJigServer(capacity=4096) as srv:
        cli = JigClient("127.0.0.1", port=srv.port, image=image, device_id=0,
                        connect_timeout=2.0, op_timeout=2.0, erase_timeout=5.0)
        ok = cli.run()
    r = cli.result()
    print(f"\nResult: {'PASS' if ok else 'FAIL'}  phase={r.phase} "
          f"chip_id={r.chip_id} bytes={r.bytes_written}/{r.total_bytes} "
          f"error={r.error}")
    print(f"Flash matches image: {bytes(srv.flash[:len(image)]) == image}")


def setup_frame_logging(log_path: Path, *, console: bool) -> None:
    """Wire the 'frames' logger to a per-run log file (always) and, if
    ``console`` is set, also to the terminal.

    The full suite emits thousands of frames, so by default they go only to the
    file and the terminal stays a clean PASS/FAIL summary; the file always has
    the complete transcript. ``--frames`` (short demo) and ``-v`` turn the
    terminal stream on.
    """
    frame_log.setLevel(logging.INFO)
    frame_log.propagate = False  # don't double-print through the root logger
    fmt = logging.Formatter("%(asctime)s  %(message)s", datefmt="%H:%M:%S")

    if console:
        stream = logging.StreamHandler()
        stream.setFormatter(fmt)
        frame_log.addHandler(stream)

    file_handler = logging.FileHandler(log_path, mode="w", encoding="utf-8")
    file_handler.setFormatter(fmt)
    frame_log.addHandler(file_handler)


def main() -> int:
    global TRACE_FRAMES

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="also show the client's high-level logs (connect/erase/...)")
    ap.add_argument("--frames", action="store_true",
                    help="log one clean single-cycle frame transcript instead of "
                         "the full test suite")
    ap.add_argument("--log", metavar="PATH", default=None,
                    help="frame log file path (default: tester_<timestamp>.log "
                         "beside this script)")
    args = ap.parse_args()

    # Client logs (logger 'jig[...]') show only with -v. Without it, mute them to
    # CRITICAL so the negative tests' expected errors don't clutter the terminal.
    # The 'frames' logger has its own handlers (propagate=False) and is unaffected.
    logging.basicConfig(level=logging.INFO if args.verbose else logging.CRITICAL,
                        format="%(name)s: %(message)s")

    # Frame tracing is always on; route it to terminal + a fresh file each run.
    TRACE_FRAMES = True
    if args.log:
        log_path = Path(args.log)
    else:
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        log_path = Path(__file__).with_name(f"tester_{stamp}.log")
    # Frames always go to the file. Echo to the terminal for the short --frames
    # demo or when -v is requested; the full suite would otherwise flood it.
    setup_frame_logging(log_path, console=args.frames or args.verbose)
    frame_log.info("frame log -> %s", log_path)

    if args.frames:
        # A single sequential cycle: frames from concurrent connections would
        # interleave unreadably, so this runs one focused full cycle.
        trace_demo()
        print(f"\nframe log written to {log_path}")
        return 0

    print("Component test: JigClient vs. emulated jig server\n")
    runner = TestRunner()
    for name, fn in TESTS:
        runner.run(name, fn)
    rc = runner.summary()
    print(f"frame log written to {log_path}")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())