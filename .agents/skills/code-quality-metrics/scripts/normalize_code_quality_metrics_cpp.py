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
_MASKED_TOKENS = (b"__restrict", b"XM_CALLCONV", b"CONSTEXPR", b"INLINE", b"INIT", b"STD",
                  b"WINAPI", b"CALLBACK", b"VKAPI_ATTR", b"VKAPI_CALL", b"IMGUI_IMPL_API")
_SAL_ANNOTATIONS = (b"_Ret_range_", b"_Out_writes_to_", b"_Ret_notnull_", b"_Post_writable_byte_size_",
                    b"_Ret_maybenull_", b"_Success_", b"_In_", b"_In_opt_")
_EXPRESSION_PREFIXES = (b"alignof", b"co_await", b"co_return", b"co_yield", b"delete", b"do", b"else",
                        b"for", b"if", b"new", b"noexcept", b"return", b"sizeof", b"switch", b"throw",
                        b"typeid", b"while")


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


def _previous_code(source: bytes, kinds: bytearray, index: int) -> int | None:
    index -= 1
    while index >= 0:
        if kinds[index] == 0 and source[index] not in _HORIZONTAL + b"\r\n":
            return index
        index -= 1
    return None


def _next_code(source: bytes, kinds: bytearray, index: int) -> int | None:
    while index < len(source):
        if kinds[index] == 0 and source[index] not in _HORIZONTAL + b"\r\n":
            return index
        index += 1
    return None


def _matching_delimiter(source: bytes, kinds: bytearray, opening: int, opening_byte: int, closing_byte: int) -> int | None:
    depth = 1
    for index in range(opening + 1, len(source)):
        if kinds[index] != 0:
            continue
        if source[index] == opening_byte:
            depth += 1
        elif source[index] == closing_byte:
            depth -= 1
            if depth == 0:
                return index
    return None


def _matching_opening(source: bytes, kinds: bytearray, closing: int, opening_byte: int, closing_byte: int) -> int | None:
    depth = 1
    for index in range(closing - 1, -1, -1):
        if kinds[index] != 0:
            continue
        if source[index] == closing_byte:
            depth += 1
        elif source[index] == opening_byte:
            depth -= 1
            if depth == 0:
                return index
    return None


def _identifier_before(source: bytes, kinds: bytearray, index: int) -> tuple[bytes, int, int] | None:
    end = _previous_code(source, kinds, index)
    if end is None or source[end] not in _IDENT:
        return None
    start = end
    while start > 0 and kinds[start - 1] == 0 and source[start - 1] in _IDENT:
        start -= 1
    return source[start:end + 1], start, end + 1


def _enclosing_aggregate_name(source: bytes, kinds: bytearray, position: int) -> bytes | None:
    scopes: list[bytes | None] = []
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
            aggregate = next((header[item + 1] for item in range(len(header) - 2, -1, -1)
                              if header[item] in (b"class", b"struct")), None)
            scopes.append(aggregate)
            header.clear()
        elif byte == ord("}"):
            if scopes:
                scopes.pop()
            header.clear()
        elif byte == ord(";"):
            header.clear()
        index += 1
    return None if not scopes else scopes[-1]


def _declaration_prefix(source: bytes, kinds: bytearray, index: int) -> bool:
    token = _identifier_before(source, kinds, index)
    if token is not None:
        return token[0] not in _EXPRESSION_PREFIXES
    previous = _previous_code(source, kinds, index)
    while previous is not None and source[previous] in b"*&":
        previous = _previous_code(source, kinds, previous)
    if previous is None or source[previous] not in _IDENT:
        return False
    token = _identifier_before(source, kinds, previous + 1)
    return token is not None and token[0] not in _EXPRESSION_PREFIXES


def _qualified_declaration_prefix(source: bytes, kinds: bytearray, index: int) -> bool:
    previous = _previous_code(source, kinds, index)
    if previous is None or source[previous] != ord(":"):
        return _declaration_prefix(source, kinds, index)
    if previous == 0 or source[previous - 1] != ord(":") or kinds[previous - 1] != 0:
        return False
    qualifier = _identifier_before(source, kinds, previous - 1)
    return qualifier is not None and _qualified_declaration_prefix(source, kinds, qualifier[1])


def _declarator_id_before(source: bytes, kinds: bytearray, opening: int) -> tuple[bytes, int] | None:
    name = _identifier_before(source, kinds, opening)
    if name is not None:
        return name[0], name[1]
    closing = _previous_code(source, kinds, opening)
    if closing is None or source[closing] != ord(")"):
        return None
    operator_opening = _matching_opening(source, kinds, closing, ord("("), ord(")"))
    if operator_opening is None or _next_code(source, kinds, operator_opening + 1) != closing:
        return None
    operator = _identifier_before(source, kinds, operator_opening)
    if operator is None or operator[0] != b"operator":
        return None
    return operator[0], operator[1]


def _function_declarator_context(source: bytes, kinds: bytearray, opening: int) -> bool:
    name = _declarator_id_before(source, kinds, opening)
    if name is None:
        return False
    if _enclosing_aggregate_name(source, kinds, opening) == name[0]:
        return True
    previous = _previous_code(source, kinds, name[1])
    if previous is None:
        return False
    if source[previous] in b".>":
        return False
    if source[previous] != ord(":"):
        return _declaration_prefix(source, kinds, name[1])
    if previous == 0 or source[previous - 1] != ord(":") or kinds[previous - 1] != 0:
        return False
    qualifier = _identifier_before(source, kinds, previous - 1)
    if qualifier is None:
        return False
    return qualifier[0] == name[0] or _qualified_declaration_prefix(source, kinds, qualifier[1])


def _function_parameter_context(source: bytes, kinds: bytearray, equals: int) -> bool:
    openings: list[int] = []
    for index in range(equals):
        if kinds[index] == 0:
            if source[index] == ord("("):
                openings.append(index)
            elif source[index] == ord(")") and openings:
                openings.pop()
    if not openings:
        return False
    opening = openings[-1]
    closing = _matching_delimiter(source, kinds, opening, ord("("), ord(")"))
    if closing is None or closing <= equals or not _function_declarator_context(source, kinds, opening):
        return False
    terminal = _next_code(source, kinds, closing + 1)
    while terminal is not None and source[terminal] not in b"{;=":
        if source[terminal] in b",)":
            return False
        terminal = _next_code(source, kinds, terminal + 1)
    return terminal is not None


def _template_parameter_context(source: bytes, kinds: bytearray, equals: int) -> bool:
    openings: list[int] = []
    for index in range(equals):
        if kinds[index] == 0:
            if source[index] == ord("<"):
                openings.append(index)
            elif source[index] == ord(">") and openings:
                openings.pop()
    if not openings:
        return False
    token = _identifier_before(source, kinds, openings[-1])
    return token is not None and token[0] == b"template"


def _default_argument_empty_brace_span(source: bytes, kinds: bytearray, equals: int) -> tuple[int, int] | None:
    """Return an empty-brace default in a function or template parameter list."""
    index = equals + 1
    while index < len(source) and source[index] in _HORIZONTAL:
        index += 1
    if index >= len(source) or kinds[index] != 0 or source[index] != ord("{"):
        return None
    index += 1
    while index < len(source) and source[index] in _HORIZONTAL:
        index += 1
    if index >= len(source) or kinds[index] != 0 or source[index] != ord("}"):
        return None
    mask_end = index + 1
    end = _next_code(source, kinds, mask_end)
    if end is None or source[end] not in b",)>":
        return None
    if not (_function_parameter_context(source, kinds, equals) or _template_parameter_context(source, kinds, equals)):
        return None
    return equals, mask_end


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


def _directive_at(source: bytes, kinds: bytearray, line_start: int) -> tuple[int, int, bytes, bytes] | None:
    end, content_end = _line_end(source, line_start)
    first = _next_code_token(source, kinds, line_start, content_end)
    if first is None or first[0] != b"#":
        return None
    directive_end = end
    next_start = end + 1
    while _continued(source, content_end, line_start) and end < len(source):
        line_start = next_start
        end, content_end = _line_end(source, line_start)
        directive_end = end
        next_start = end + 1
    name = _next_code_token(source, kinds, first[2], directive_end)
    return directive_end, next_start, b"" if name is None else name[0], source[name[2] if name is not None else directive_end:directive_end]


def _bt_engine_condition(payload: bytes) -> bool | None:
    compact = b"".join(payload.split())
    if compact in (b"defined(BT_ENGINE)", b"BT_ENGINE"):
        return True
    if compact in (b"!defined(BT_ENGINE)", b"!BT_ENGINE"):
        return False
    return None


def _mask_bt_engine_inactive_payload(source: bytes, kinds: bytearray, result: bytearray) -> None:
    """Select direct BT_ENGINE branches while retaining unrelated conditional payloads."""
    stack: list[dict[str, bool | None]] = []
    active = True
    line_start = 0
    while line_start < len(source):
        directive = _directive_at(source, kinds, line_start)
        if directive is None:
            end, _ = _line_end(source, line_start)
            if not active:
                _mask_non_newlines(result, line_start, end)
            line_start = end + 1
            continue
        directive_end, next_start, name, payload = directive
        _mask_non_newlines(result, line_start, directive_end)
        condition = _bt_engine_condition(payload)
        if name in (b"if", b"ifdef", b"ifndef"):
            if name == b"ifdef":
                condition = True if b"".join(payload.split()) == b"BT_ENGINE" else None
            elif name == b"ifndef":
                condition = False if b"".join(payload.split()) == b"BT_ENGINE" else None
            parent = active
            active = parent and (condition if condition is not None else True)
            stack.append({"parent": parent, "known": condition, "selected": condition})
        elif name == b"elif" and stack:
            frame = stack[-1]
            if frame["known"] is not None:
                if frame["selected"]:
                    active = False
                elif condition is not None:
                    active = bool(frame["parent"]) and condition
                    frame["selected"] = condition
                else:
                    active = bool(frame["parent"])
                    frame["selected"] = None
            else:
                active = bool(frame["parent"])
        elif name == b"else" and stack:
            frame = stack[-1]
            active = bool(frame["parent"]) and not bool(frame["selected"]) if frame["known"] is not None else bool(frame["parent"])
            frame["selected"] = True
        elif name == b"endif" and stack:
            active = bool(stack.pop()["parent"])
        line_start = next_start


def _mask_annotation(source: bytes, kinds: bytearray, result: bytearray, start: int, token: bytes) -> None:
    end = start + len(token)
    _mask_non_newlines(result, start, end)
    opening = _next_code(source, kinds, end)
    if opening is not None and source[opening] == ord("("):
        closing = _matching_delimiter(source, kinds, opening, ord("("), ord(")"))
        if closing is not None:
            _mask_non_newlines(result, opening, closing + 1)


def _mask_nested_aggregate_designators(source: bytes, kinds: bytearray, result: bytearray) -> None:
    brace_depth = 0
    index = 0
    while index < len(source):
        if kinds[index] != 0:
            index += 1
            continue
        if source[index] == ord("{"):
            brace_depth += 1
        elif source[index] == ord("}"):
            brace_depth = max(0, brace_depth - 1)
        elif source[index] == ord(".") and brace_depth >= 1:
            name = _next_code(source, kinds, index + 1)
            if name is not None and source[name] in _IDENT:
                end = name + 1
                while end < len(source) and kinds[end] == 0 and source[end] in _IDENT:
                    end += 1
                opening = _next_code(source, kinds, end)
                if opening is not None and source[opening] == ord("="):
                    opening = _next_code(source, kinds, opening + 1)
                previous = _previous_code(source, kinds, index)
                if opening is not None and source[opening] == ord("{") and previous is not None and source[previous] in b"{,":
                    _mask_non_newlines(result, index, opening)
                    index = opening
        index += 1


def _assignment_operator_at(source: bytes, kinds: bytearray, index: int) -> bool:
    if source[index] != ord("=") or kinds[index] != 0:
        return False
    if index >= 2 and kinds[index - 2:index] == bytes(2) and source[index - 2:index] in (b"<<", b">>"):
        return True
    if index > 0 and kinds[index - 1] == 0 and source[index - 1] in b"+-*/%&|^":
        return True
    return ((index == 0 or kinds[index - 1] != 0 or source[index - 1] not in b"=!<>") and
            (index + 1 == len(source) or kinds[index + 1] != 0 or source[index + 1] != ord("=")))


def _contains_assignment(source: bytes, kinds: bytearray, start: int, end: int) -> bool:
    return any(_assignment_operator_at(source, kinds, index) for index in range(start, end))


def _assignment_comma_operand(source: bytes, kinds: bytearray, start: int, end: int) -> bool:
    commas: list[int] = []
    depth = 0
    for index in range(start, end):
        if kinds[index] != 0:
            continue
        if source[index] in b"([{":
            depth += 1
        elif source[index] in b")]}":
            depth = max(0, depth - 1)
        elif depth == 0 and source[index] == ord(","):
            commas.append(index)
    bounds = [start, *commas, end]
    return any(_contains_assignment(source, kinds, bounds[index], bounds[index + 1]) and
               _contains_assignment(source, kinds, bounds[index + 1] + 1, bounds[index + 2])
               for index in range(len(bounds) - 2))


def _mask_comma_folds(source: bytes, kinds: bytearray, result: bytearray) -> None:
    search = 0
    while True:
        ellipsis = source.find(b"...", search)
        if ellipsis < 0:
            return
        search = ellipsis + 3
        if any(kinds[index] != 0 for index in range(ellipsis, ellipsis + 3)):
            continue
        openings: list[int] = []
        for index in range(ellipsis):
            if kinds[index] == 0:
                if source[index] == ord("("):
                    openings.append(index)
                elif source[index] == ord(")") and openings:
                    openings.pop()
        if not openings:
            continue
        opening = openings[-1]
        closing = _matching_delimiter(source, kinds, opening, ord("("), ord(")"))
        if closing is None or closing < ellipsis:
            continue
        comma = _previous_code(source, kinds, ellipsis)
        if comma is None or source[comma] != ord(","):
            continue
        payload_start = _next_code(source, kinds, opening + 1)
        payload_end = _previous_code(source, kinds, comma)
        if payload_start is None or payload_end is None or source[payload_start] != ord("("):
            continue
        if _matching_delimiter(source, kinds, payload_start, ord("("), ord(")")) != payload_end:
            continue
        if _assignment_comma_operand(source, kinds, payload_start + 1, payload_end):
            _mask_non_newlines(result, opening, closing + 1)


def _mask_namespace_declaration(source: bytes, kinds: bytearray, result: bytearray, start: int, first: bytes) -> None:
    line_end = _logical_end(source, start)
    second = _next_code_token(source, kinds, start + len(first), line_end)
    third = None if second is None else _next_code_token(source, kinds, second[2], line_end)
    if first == b"template" and second is not None and second[0] in (b"class", b"struct"):
        declaration = start
    elif first == b"extern" and second is not None and second[0] == b"template" and third is not None and third[0] in (b"class", b"struct"):
        declaration = start
    else:
        return
    semicolon = next((index for index in range(start, line_end) if kinds[index] == 0 and source[index] == ord(";")), None)
    if semicolon is None or any(kinds[index] == 0 and source[index] in b"{}" for index in range(start, semicolon)):
        return
    if any(kinds[index] == 0 and source[index] not in _HORIZONTAL + b"\r\n" for index in range(semicolon + 1, line_end)):
        return
    _mask_non_newlines(result, declaration, semicolon + 1)


def normalize_cpp_bytes(source: bytes) -> bytes:
    """Mask locked-parser gaps while preserving raw source coordinates exactly."""
    kinds = _lex_kinds(source)
    result = bytearray(source)
    _mask_bt_engine_inactive_payload(source, kinds, result)

    line_start = 0
    while line_start < len(source):
        end, _ = _line_end(source, line_start)
        logical_end = _logical_end(source, line_start)
        first = _next_code_token(source, kinds, line_start, logical_end)
        if first is not None and _namespace_scope_at(source, kinds, first[1]):
            _mask_namespace_declaration(source, kinds, result, first[1], first[0])
        line_start = end + 1

    for token in _MASKED_TOKENS:
        start = 0
        while True:
            index = source.find(token, start)
            if index < 0:
                break
            end = index + len(token)
            if kinds[index:end] == bytes(len(token)) and (index == 0 or source[index - 1] not in _IDENT) and (end == len(source) or source[end] not in _IDENT):
                _mask_non_newlines(result, index, end)
            start = end

    for token in _SAL_ANNOTATIONS:
        start = 0
        while True:
            index = source.find(token, start)
            if index < 0:
                break
            end = index + len(token)
            if kinds[index:end] == bytes(len(token)) and (index == 0 or source[index - 1] not in _IDENT) and (end == len(source) or source[end] not in _IDENT):
                _mask_annotation(source, kinds, result, index, token)
            start = end

    for index, byte in enumerate(source):
        if byte == ord("=") and kinds[index] == 0:
            span = _default_argument_empty_brace_span(source, kinds, index)
            if span is not None:
                _mask_non_newlines(result, *span)
        if byte == ord("=") and kinds[index] == 0:
            token = _next_code_token(source, kinds, index + 1, len(source))
            if token is not None and token[0] == b"delete":
                semicolon = _next_code(source, kinds, token[2])
                if semicolon is not None and source[semicolon] == ord(";"):
                    _mask_non_newlines(result, index, token[2])
    _mask_nested_aggregate_designators(source, kinds, result)
    _mask_comma_folds(source, kinds, result)
    return bytes(result)


if __name__ == "__main__":
    sys.stdout.buffer.write(normalize_cpp_bytes(sys.stdin.buffer.read()))
