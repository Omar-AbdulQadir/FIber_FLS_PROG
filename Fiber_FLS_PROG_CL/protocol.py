"""Wire protocol for the Fiber FLS flash-programmer test jig.

Mirrors the firmware's protocol layer (``_MiddleWare_/Services/protocol/protocol.h``).
All multi-byte fields are big-endian (MSB-first).

    Request : A5 | CHIP(1) | CMD(1) | LEN(2) | PAYLOAD(LEN) | CRC16(2)
    Response: 5A | CHIP(1) | CMD(1) | STATUS(1) | LEN(2) | RESP(LEN) | CRC16(2)

The exchange is lock-step: the client sends one request and waits for its
response (ACK) before sending the next.

CRC-16 is currently a DUMMY on the firmware side: ``FW_Crc16`` always returns
0x0000, and the server only accepts a frame whose CRC equals that. So
:func:`crc16` returns 0x0000 today. When the firmware grows a real CRC-16,
replace the body of :func:`crc16` here (and nothing else) to match.
"""

from __future__ import annotations

import logging
import socket
import struct
from enum import IntEnum

# Frame logger. Every request built and response read is logged here, decoded
# field-by-field, so the on-wire protocol is observable in the real client (not
# just the tester). It is silent by default (a NullHandler) — attach a handler
# (e.g. FLS_PROG_CL.py wires a terminal + file handler) to see the frames.
frame_log = logging.getLogger("frames")
frame_log.addHandler(logging.NullHandler())

# Start-of-frame markers (reversed between directions so direction is obvious).
SOF_REQ = 0xA5
SOF_RESP = 0x5A

# Fixed field sizes (bytes), from protocol.h.
ADDR_LEN = 3        # 24-bit address, MSB-first
TOTAL_LEN = 3       # 24-bit total length, MSB-first
CRC_LEN = 2         # CRC-16, MSB-first
MAX_DATA = 32       # max DATA bytes carried in one frame (PROTO_MAX_DATA)

# Header sizes.
REQ_HDR_LEN = 5     # SOF + CHIP + CMD + LEN(2)
RESP_HDR_LEN = 6    # SOF + CHIP + CMD + STATUS + LEN(2)


class Cmd(IntEnum):
    """Command opcodes (``proto_cmd_t``)."""

    READ_ID = 0x01
    READ_START = 0x02            # client: start_addr(3) + total_len(3)
    READ_DATA = 0x03            # server -> client data chunk; client ACKs
    ERASE = 0x04               # scope(1) + addr(3)
    WRITE_DATA = 0x05          # addr(3) + data(<=32)
    WRITE_STREAM_START = 0x06  # start_addr(3) + total_len(3)
    WRITE_STREAM_DATA = 0x07   # data(<=32); ACK reports progress(3)
    VERIFY = 0x08              # expected_crc16(2); whole-chip


class Status(IntEnum):
    """Response STATUS codes (``proto_status_t``)."""

    OK = 0x00
    ERR_CRC = 0x01
    ERR_CMD = 0x02
    ERR_LEN = 0x03
    ERR_ADDR = 0x04
    ERR_SEQ = 0x05
    ERR_BUSY = 0x06
    ERR_FLASH = 0x07
    ERR_VERIFY = 0x08


class Erase(IntEnum):
    """Erase scope selector (``fw_erase_scope_t``)."""

    SECTOR = 0x00   # 4 KB
    BLOCK = 0x01    # 64 KB
    CHIP = 0x02     # whole chip


class ProtocolError(Exception):
    """Raised on a malformed or unreadable frame (bad SOF, short read, timeout)."""


# --------------------------------------------------------------------------- #
# Frame logging helpers                                                       #
# --------------------------------------------------------------------------- #

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


def _log_request(raw: bytes, chip: int, cmd: int, payload: bytes, crc: int) -> None:
    """Log one outgoing request frame, decoded field-by-field."""
    if not frame_log.isEnabledFor(logging.INFO):
        return
    frame_log.info(
        "TX REQ  -> %s | SOF=0x%02X CHIP=%d CMD=%s LEN=%d PAYLOAD=[%s] CRC=0x%04X",
        _hex(raw), SOF_REQ, chip, _cmd_name(cmd), len(payload), _hex(payload), crc,
    )


def _log_response(chip: int, cmd: int, status: int, payload: bytes) -> None:
    """Log one incoming response frame, decoded field-by-field."""
    if not frame_log.isEnabledFor(logging.INFO):
        return
    frame_log.info(
        "RX RESP <- SOF=0x%02X CHIP=%d CMD=%s STATUS=%s LEN=%d RESP=[%s]",
        SOF_RESP, chip, _cmd_name(cmd), _status_name(status), len(payload), _hex(payload),
    )


def crc16(data: bytes) -> int:
    """CRC-16 over ``data``.

    DUMMY for now: the firmware's ``FW_Crc16`` always returns 0x0000 and accepts
    only frames whose CRC matches, so we send 0x0000. Replace this body with the
    real CRC-16 once the firmware has one — it is the single point of change.
    """
    return 0x0000


def build_request(chip: int, cmd: int, payload: bytes = b"") -> bytes:
    """Build a request frame: ``A5 CHIP CMD LEN(2) PAYLOAD CRC16(2)`` (big-endian)."""
    if len(payload) > 0xFFFF:
        raise ValueError("payload too large for 16-bit LEN field")

    body = struct.pack(">BBH", chip & 0xFF, int(cmd) & 0xFF, len(payload)) + payload
    crc = crc16(body)
    frame = bytes([SOF_REQ]) + body + struct.pack(">H", crc)
    _log_request(frame, chip & 0xFF, int(cmd) & 0xFF, payload, crc)
    return frame


def _recv_exact(sock: socket.socket, n: int) -> bytes:
    """Read exactly ``n`` bytes from ``sock`` (recv may return partial segments)."""
    chunks = []
    remaining = n
    while remaining > 0:
        chunk = sock.recv(remaining)
        if not chunk:
            raise ProtocolError(
                f"connection closed mid-frame ({n - remaining}/{n} bytes read)"
            )
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def read_response(sock: socket.socket) -> tuple[int, int, int, bytes]:
    """Read one response frame from ``sock``.

    Returns ``(chip, cmd, status, payload)``. Raises :class:`ProtocolError` on a
    bad SOF, oversized LEN, or a short/closed connection. ``socket.timeout``
    propagates so callers can treat a stalled jig as a failure.
    """
    sof = _recv_exact(sock, 1)[0]
    if sof != SOF_RESP:
        raise ProtocolError(f"bad response SOF 0x{sof:02X} (expected 0x{SOF_RESP:02X})")

    # CHIP + CMD + STATUS + LEN(2) — the rest of the 6-byte header after SOF.
    chip, cmd, status, length = struct.unpack(">BBBH", _recv_exact(sock, RESP_HDR_LEN - 1))
    if length > MAX_DATA:
        raise ProtocolError(f"response LEN {length} exceeds MAX_DATA {MAX_DATA}")

    payload = _recv_exact(sock, length) if length else b""
    _recv_exact(sock, CRC_LEN)  # CRC present on the wire; dummy, not validated here

    _log_response(chip, cmd, status, payload)
    return chip, cmd, status, payload
