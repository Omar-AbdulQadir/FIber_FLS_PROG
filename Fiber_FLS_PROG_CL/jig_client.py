"""JigClient — one connection instance per active programmer jig.

Each jig is a TCP command server (see the firmware's protocol/command layers).
A :class:`JigClient` owns a single socket to one jig and drives the full
program cycle: connect -> read CHIP_ID -> chip-erase -> stream the image ->
verify -> close. All configuration is fed in at construction; nothing is
hard-coded inside, so the orchestration layer (main) just instantiates one
client per device and runs it (one thread per instance for the fleet).

The instance carries full progress state (phase, bytes_written, chip_id,
error, timing) so it can be queried mid-run and emit its own result summary.
"""

from __future__ import annotations

import logging
import socket
import struct
import time
from dataclasses import dataclass
from enum import Enum, auto

import protocol
from protocol import Cmd, Erase, ProtocolError, Status


class Phase(Enum):
    """Where the client is in the program cycle."""

    IDLE = auto()
    CONNECTED = auto()
    READ_ID = auto()
    ERASING = auto()
    WRITING = auto()
    VERIFYING = auto()
    DONE = auto()
    FAILED = auto()


class JigError(Exception):
    """A jig returned a non-OK status, or the exchange desynced."""

    def __init__(self, message: str, status: int | None = None):
        super().__init__(message)
        self.status = status


@dataclass
class Result:
    """Flat, printable summary of one device's run."""

    ip: str
    device_id: int | None
    passed: bool
    phase: str
    chip_id: str | None
    bytes_written: int
    total_bytes: int
    elapsed: float
    error: str | None


class JigClient:
    """A single connection to one programmer jig."""

    def __init__(
        self,
        ip: str,
        *,
        port: int = 5000,
        chip: int = 0,
        image: bytes = b"",
        device_id: int | None = None,
        connect_timeout: float = 5.0,
        op_timeout: float = 10.0,
        erase_timeout: float = 60.0,
        progress_interval: float = 2.0,
        logger: logging.Logger | None = None,
    ):
        # --- configuration (all supplied at construction) ---
        self.ip = ip
        self.port = port
        self.chip = chip
        self.image = image
        self.device_id = device_id
        self.connect_timeout = connect_timeout
        self.op_timeout = op_timeout
        self.erase_timeout = erase_timeout
        self.progress_interval = progress_interval
        self.log = logger or logging.getLogger(f"jig[{ip}]")

        # --- progress state (queryable any time) ---
        self.sock: socket.socket | None = None
        self.phase: Phase = Phase.IDLE
        self.chip_id: bytes | None = None
        self.total_bytes: int = len(image)
        self.bytes_written: int = 0
        self.error: str | None = None
        self.passed: bool = False
        self.started_at: float | None = None
        self.elapsed: float = 0.0

    # ------------------------------------------------------------------ #
    # Connection lifecycle                                               #
    # ------------------------------------------------------------------ #

    def connect(self) -> None:
        """Open the TCP connection to the jig."""
        self.sock = socket.create_connection((self.ip, self.port), timeout=self.connect_timeout)
        self.sock.settimeout(self.op_timeout)
        self.phase = Phase.CONNECTED
        self.log.info("connected to %s:%d", self.ip, self.port)

    def close(self) -> None:
        """Close the socket if open (idempotent)."""
        if self.sock is not None:
            try:
                self.sock.close()
            finally:
                self.sock = None

    def __enter__(self) -> "JigClient":
        self.connect()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    # ------------------------------------------------------------------ #
    # One request/response transaction (lock-step)                       #
    # ------------------------------------------------------------------ #

    def _txn(self, cmd: Cmd, payload: bytes = b"", *, timeout: float | None = None) -> bytes:
        """Send one request, read one response, return its payload.

        Asserts the response opcode echoes the request and STATUS is OK; raises
        :class:`JigError` otherwise. ``timeout`` overrides the socket timeout for
        this one transaction (used for the slow chip-erase).
        """
        if self.sock is None:
            raise JigError("not connected")

        if timeout is not None:
            self.sock.settimeout(timeout)
        try:
            self.sock.sendall(protocol.build_request(self.chip, cmd, payload))
            r_chip, r_cmd, status, resp = protocol.read_response(self.sock)
        finally:
            if timeout is not None:
                self.sock.settimeout(self.op_timeout)

        if r_cmd != int(cmd):
            raise JigError(
                f"response opcode 0x{r_cmd:02X} does not match request 0x{int(cmd):02X}"
            )
        if status != Status.OK:
            name = Status(status).name if status in Status._value2member_map_ else f"0x{status:02X}"
            raise JigError(f"{cmd.name} -> {name}", status=status)
        return resp

    # ------------------------------------------------------------------ #
    # Commands                                                           #
    # ------------------------------------------------------------------ #

    def read_id(self) -> bytes:
        """Read the chip id (4-byte placeholder DE AD BE EF for now)."""
        self.phase = Phase.READ_ID
        self.chip_id = self._txn(Cmd.READ_ID)
        self.log.info("chip id = %s", self.chip_id.hex().upper())
        return self.chip_id

    def erase_chip(self) -> None:
        """Whole-chip erase. addr is ignored by the server for chip scope."""
        self.phase = Phase.ERASING
        self.log.info("erasing chip ...")
        payload = struct.pack(">B", Erase.CHIP) + _u24(0)
        self._txn(Cmd.ERASE, payload, timeout=self.erase_timeout)
        self.log.info("erase complete")

    def write_stream(self) -> None:
        """Stream the image to flash via WRITE_STREAM_START + WRITE_STREAM_DATA.

        Address advances implicitly server-side; each ACK reports running
        bytes_done, which we assert against our own offset to catch any desync.
        """
        self.phase = Phase.WRITING
        total = len(self.image)
        self.total_bytes = total
        self.bytes_written = 0

        # START: tell the server start address (0) and total length.
        self._txn(Cmd.WRITE_STREAM_START, _u24(0) + _u24(total))
        self.log.info("streaming %d bytes ...", total)

        last_log = self.started_at or time.monotonic()
        offset = 0
        while offset < total:
            chunk = self.image[offset:offset + protocol.MAX_DATA]
            resp = self._txn(Cmd.WRITE_STREAM_DATA, chunk)

            if len(resp) != protocol.TOTAL_LEN:
                raise JigError(
                    f"WRITE_STREAM_DATA ACK has {len(resp)} bytes, expected {protocol.TOTAL_LEN}"
                )
            done = _rd_u24(resp)
            offset += len(chunk)
            if done != offset:
                raise JigError(f"progress desync: server={done} client={offset}")
            self.bytes_written = offset

            now = time.monotonic()
            if now - last_log >= self.progress_interval:
                self.log.info("  %d/%d (%.1f%%)", offset, total, 100.0 * offset / total)
                last_log = now

        self.log.info("stream complete: %d bytes", self.bytes_written)

    def verify(self, expected_crc: int = 0x0000) -> None:
        """Whole-chip verify. Server compares its CRC to expected_crc.

        Default 0x0000 matches the firmware's dummy CRC so this passes today;
        pass a real value once both sides compute a real CRC-16.
        """
        self.phase = Phase.VERIFYING
        self.log.info("verifying ...")
        self._txn(Cmd.VERIFY, struct.pack(">H", expected_crc & 0xFFFF))
        self.log.info("verify OK")

    # ------------------------------------------------------------------ #
    # Full cycle                                                         #
    # ------------------------------------------------------------------ #

    def run(self, expected_crc: int = 0x0000) -> bool:
        """Drive the full program cycle. Never raises; records outcome in state.

        Returns True on success. Safe to call from a worker thread.
        """
        self.started_at = time.monotonic()
        self.passed = False
        self.error = None
        try:
            self.connect()
            self.read_id()
            self.erase_chip()
            self.write_stream()
            self.verify(expected_crc)
            self.phase = Phase.DONE
            self.passed = True
        except (JigError, ProtocolError, OSError) as exc:
            self.phase = Phase.FAILED
            self.error = f"{type(exc).__name__}: {exc}"
            self.log.error("failed in %s: %s", self.phase.name, self.error)
        finally:
            self.elapsed = time.monotonic() - self.started_at
            self.close()
        return self.passed

    def result(self) -> Result:
        """Snapshot the instance state as a flat, printable Result."""
        return Result(
            ip=self.ip,
            device_id=self.device_id,
            passed=self.passed,
            phase=self.phase.name,
            chip_id=self.chip_id.hex().upper() if self.chip_id else None,
            bytes_written=self.bytes_written,
            total_bytes=self.total_bytes,
            elapsed=self.elapsed,
            error=self.error,
        )


# ---------------------------------------------------------------------- #
# 24-bit big-endian helpers (match the firmware's wr_u24 / rd_u24)       #
# ---------------------------------------------------------------------- #

def _u24(value: int) -> bytes:
    """Encode a 24-bit value, MSB-first."""
    return struct.pack(">I", value & 0xFFFFFF)[1:]


def _rd_u24(data: bytes) -> int:
    """Decode a 3-byte MSB-first value."""
    return (data[0] << 16) | (data[1] << 8) | data[2]