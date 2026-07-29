#!/usr/bin/env python3
"""Byte-preserving compatibility masking for Broken Engine C++ analysis captures."""
from __future__ import annotations

import sys


_HORIZONTAL = b" \t"
_IDENT = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_"
_DECIMAL_DIGITS = b"0123456789"
_OCTAL_DIGITS = b"01234567"
_BINARY_DIGITS = b"01"
_HEXADECIMAL_DIGITS = b"0123456789ABCDEFabcdef"
_NUMERIC_PREPROCESSING_TOKEN = _IDENT + b".'"
_MASKED_TOKENS = (b"__restrict", b"XM_CALLCONV")


def _mark(kinds: bytearray, start: int, end: int, kind: int) -> None:
    kinds[start:end] = bytes((kind,)) * (end - start)


def _raw_end(source: bytes, start: int) -> int | None:
    """Return the exclusive raw-literal end, or None for a malformed introducer."""
    delimiter_end = start + 2
    while delimiter_end < len(source) and source[delimiter_end] != ord("("):
        if delimiter_end - (start + 2) == 16 or source[delimiter_end] in b" ()\\\t\v\f\r\n":
            return None
        delimiter_end += 1
    if delimiter_end == len(source):
        return None
    terminator = b")" + source[start + 2:delimiter_end] + b'"'
    closing = source.find(terminator, delimiter_end + 1)
    return None if closing < 0 else closing + len(terminator)


def _digit_separator(source: bytes, index: int) -> bool:
    if index == 0 or index + 1 == len(source):
        return False
    start = index
    while start > 0 and source[start - 1] in _NUMERIC_PREPROCESSING_TOKEN:
        start -= 1
    if start > 1 and source[start - 1] in b"+-" and source[start - 2] in b"eEpP":
        exponent_start = start - 2
        while exponent_start > 0 and source[exponent_start - 1] in _NUMERIC_PREPROCESSING_TOKEN:
            exponent_start -= 1
        if (source[exponent_start:exponent_start + 1] in _DECIMAL_DIGITS or
                (source[exponent_start:exponent_start + 1] == b"." and
                 source[exponent_start + 1:exponent_start + 2] in _DECIMAL_DIGITS)):
            start = exponent_start
    if start > 0 and source[start - 1] in _IDENT:
        return False
    end = index + 1
    while end < len(source) and source[end] in _NUMERIC_PREPROCESSING_TOKEN:
        end += 1
    token = source[start:end]
    prefix = source[start:start + 2]
    if prefix in (b"0x", b"0X"):
        digits = _HEXADECIMAL_DIGITS
    elif prefix in (b"0b", b"0B"):
        digits = _BINARY_DIGITS
    elif source[start:start + 1] == b"0" and b"." not in token and b"e" not in token and b"E" not in token:
        digits = _OCTAL_DIGITS
    elif source[start:start + 1] in _DECIMAL_DIGITS or (source[start:start + 1] == b"." and
                                                        source[start + 1:start + 2] in _DECIMAL_DIGITS):
        digits = _DECIMAL_DIGITS
    else:
        return False
    return source[index - 1] in digits and source[index + 1] in digits


def _lex_kinds(source: bytes) -> bytearray:
    """Classify code (0), comments (1), and string/character literals (2)."""
    kinds = bytearray(len(source))
    index = 0
    while index < len(source):
        byte = source[index]
        if byte == ord("/") and index + 1 < len(source):
            if source[index + 1] == ord("/"):
                end = source.find(b"\n", index)
                end = len(source) if end < 0 else end
                _mark(kinds, index, end, 1)
                index = end
                continue
            if source[index + 1] == ord("*"):
                end = source.find(b"*/", index + 2)
                end = len(source) if end < 0 else end + 2
                _mark(kinds, index, end, 1)
                index = end
                continue
        if byte == ord("R") and index + 1 < len(source) and source[index + 1] == ord('"'):
            end = _raw_end(source, index)
            end = len(source) if end is None else end
            _mark(kinds, index, end, 2)
            index = end
            continue
        if byte in (ord('"'), ord("'")) and not (byte == ord("'") and _digit_separator(source, index)):
            quote = byte
            end = index + 1
            while end < len(source):
                if source[end] == ord("\\"):
                    end += 2
                    continue
                end += 1
                if source[end - 1] == quote:
                    break
            _mark(kinds, index, min(end, len(source)), 2)
            index = min(end, len(source))
            continue
        index += 1
    return kinds


def _line_end(source: bytes, start: int) -> tuple[int, int]:
    newline = source.find(b"\n", start)
    end = len(source) if newline < 0 else newline
    content_end = end - 1 if end > start and source[end - 1] == ord("\r") else end
    return end, content_end


def _continued(source: bytes, content_end: int, start: int) -> bool:
    return content_end > start and source[content_end - 1] == ord("\\")


def _mask_non_newlines(result: bytearray, start: int, end: int) -> None:
    for index in range(start, end):
        if result[index] not in (ord("\r"), ord("\n")):
            result[index] = ord(" ")


def _next_code_token(source: bytes, kinds: bytearray, start: int, end: int) -> tuple[bytes, int, int] | None:
    index = start
    while index < end:
        if kinds[index] != 0 or source[index] in _HORIZONTAL + b"\r\n":
            index += 1
            continue
        token_start = index
        if source[index] in _IDENT:
            index += 1
            while index < end and kinds[index] == 0 and source[index] in _IDENT:
                index += 1
            return source[token_start:index], token_start, index
        return source[index:index + 1], index, index + 1
    return None


def _logical_end(source: bytes, start: int) -> int:
    physical_start = start
    while True:
        end, content_end = _line_end(source, physical_start)
        if not _continued(source, content_end, physical_start) or end == len(source):
            return content_end
        physical_start = end + 1


def _namespace_scope_at(source: bytes, kinds: bytearray, position: int) -> bool:
    """Explicit instantiations are valid only at namespace scope."""
    scopes: list[bool] = []
    header: list[bytes] = []
    index = 0
    while index < position:
        if kinds[index] != 0:
            index += 1
            continue
        byte = source[index]
        if byte in _IDENT:
            start = index
            index += 1
            while index < position and kinds[index] == 0 and source[index] in _IDENT:
                index += 1
            header.append(source[start:index])
            continue
        if byte == ord("{"):
            scopes.append(b"namespace" in header)
            header.clear()
        elif byte == ord("}"):
            if scopes:
                scopes.pop()
            header.clear()
        elif byte == ord(";"):
            header.clear()
        index += 1
    return all(scopes)


def normalize_cpp_bytes(source: bytes) -> bytes:
    """Mask the narrow Broken Engine extensions that the locked C++ parser rejects."""
    kinds = _lex_kinds(source)
    result = bytearray(source)

    line_start = 0
    while line_start < len(source):
        end, content_end = _line_end(source, line_start)
        first_code = None
        blocked_by_literal = False
        prefix_end = 3 if line_start == 0 and source.startswith(b"\xef\xbb\xbf") else line_start
        for index in range(prefix_end, content_end):
            if kinds[index] == 2 and source[index] not in _HORIZONTAL:
                blocked_by_literal = True
            if kinds[index] == 0 and source[index] not in _HORIZONTAL:
                first_code = index
                break
        if not blocked_by_literal and first_code is not None and source[first_code] == ord("#"):
            directive_start = line_start
            directive_end = end
            next_start = end + 1
            while _continued(source, content_end, line_start) and end < len(source):
                line_start = next_start
                end, content_end = _line_end(source, line_start)
                directive_end = end
                next_start = end + 1
            _mask_non_newlines(result, directive_start, directive_end)
        line_start = end + 1

    line_start = 0
    while line_start < len(source):
        end, _ = _line_end(source, line_start)
        logical_end = _logical_end(source, line_start)
        first = _next_code_token(source, kinds, line_start, logical_end)
        second = None if first is None else _next_code_token(source, kinds, first[2], logical_end)
        if (first is not None and second is not None and _namespace_scope_at(source, kinds, first[1]) and
                first[0] == b"template" and second[0] in (b"class", b"struct")):
            semicolon = next((index for index in range(second[2], logical_end) if kinds[index] == 0 and source[index] == ord(";")), None)
            if semicolon is not None:
                has_body = any(kinds[index] == 0 and source[index] in b"{}" for index in range(first[1], semicolon))
                trailing_code = any(kinds[index] == 0 and source[index] not in _HORIZONTAL + b"\r\n" for index in range(semicolon + 1, logical_end))
                if not has_body and not trailing_code:
                    for index in range(first[1], semicolon + 1):
                        if kinds[index] == 0 and result[index] not in (ord("\r"), ord("\n")):
                            result[index] = ord(" ")
        line_start = end + 1

    for token in _MASKED_TOKENS:
        start = 0
        while True:
            index = source.find(token, start)
            if index < 0:
                break
            end = index + len(token)
            if (kinds[index:end] == bytes(len(token)) and
                    (index == 0 or source[index - 1] not in _IDENT) and
                    (end == len(source) or source[end] not in _IDENT)):
                result[index:end] = b" " * len(token)
            start = end
    return bytes(result)


if __name__ == "__main__":
    sys.stdout.buffer.write(normalize_cpp_bytes(sys.stdin.buffer.read()))
