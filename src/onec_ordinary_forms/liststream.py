"""Clean-room parser/writer for 1C list-stream bracket text.

The platform libraries expose ListInStream/ListOutStream symbols. This module
implements the textual bracket syntax we observe in exported ordinary forms; it
does not copy platform code.
"""

from __future__ import annotations

from dataclasses import dataclass


class ListStreamParseError(ValueError):
    pass


@dataclass(frozen=True)
class Token:
    kind: str
    value: str
    start: int
    end: int


@dataclass(frozen=True)
class ListStreamDocument:
    value: object
    trailing: str


def parse_list_stream(text: str, *, allow_trailing: bool = False) -> object:
    document = parse_list_stream_document(text, allow_trailing=allow_trailing)
    return document.value


def parse_list_stream_document(text: str, *, allow_trailing: bool = False) -> ListStreamDocument:
    tokens = tokenize(text)
    parser = _Parser(tokens)
    value = parser.parse_value()
    if parser.index != len(tokens) and not allow_trailing:
        raise ListStreamParseError(f"Unexpected trailing token: {tokens[parser.index].value}")
    trailing = text[tokens[parser.index].start :] if parser.index < len(tokens) else ""
    return ListStreamDocument(value=value, trailing=trailing)


def dumps(value: object) -> str:
    if isinstance(value, list):
        return "{" + ",".join(dumps(item) for item in value) + "}"
    return str(value)


def dumps_list_out_stream(value: object) -> str:
    """Serialize using the CRLF-oriented style used by platform ListOutStream.

    The platform writer keeps scalar-only lists compact, but starts nested list
    values on a new CRLF line. Ordinary form streams written as one compact line
    can pass loose container import checks and still fail when Configurator
    opens the form, so build-bin must use this stream style.
    """

    if not isinstance(value, list):
        return str(value)
    if not any(isinstance(item, list) for item in value):
        return "{" + ",".join(dumps_list_out_stream(item) for item in value) + "}"

    parts: list[str] = ["{"]
    for index, item in enumerate(value):
        if index:
            parts.append(",")
        if isinstance(item, list):
            parts.append("\r\n")
            parts.append(dumps_list_out_stream(item))
        else:
            parts.append(dumps_list_out_stream(item))
    parts.append("\r\n}")
    return "".join(parts)


def tokenize(text: str) -> list[Token]:
    tokens: list[Token] = []
    index = 0
    while index < len(text):
        char = text[index]
        if char.isspace():
            index += 1
            continue
        if char in "{}[],":
            tokens.append(Token(char, char, index, index + 1))
            index += 1
            continue
        if char == '"':
            start = index
            index += 1
            escaped = False
            while index < len(text):
                current = text[index]
                index += 1
                if escaped:
                    escaped = False
                elif current == "\\":
                    escaped = True
                elif current == '"':
                    break
            else:
                raise ListStreamParseError("Unterminated string literal")
            tokens.append(Token("atom", text[start:index], start, index))
            continue
        start = index
        while index < len(text) and not text[index].isspace() and text[index] not in "{}[],":
            index += 1
        tokens.append(Token("atom", text[start:index], start, index))
    return tokens


class _Parser:
    def __init__(self, tokens: list[Token]) -> None:
        self.tokens = tokens
        self.index = 0

    def parse_value(self) -> object:
        if self.index >= len(self.tokens):
            raise ListStreamParseError("Unexpected end of input")
        token = self.tokens[self.index]
        if token.kind in ("{", "["):
            return self.parse_list("}" if token.kind == "{" else "]")
        if token.kind == "atom":
            self.index += 1
            return token.value
        raise ListStreamParseError(f"Unexpected token: {token.value}")

    def parse_list(self, close: str) -> list[object]:
        self.index += 1
        result: list[object] = []
        while self.index < len(self.tokens):
            token = self.tokens[self.index]
            if token.kind == close:
                self.index += 1
                return result
            if token.kind == ",":
                self.index += 1
                continue
            result.append(self.parse_value())
        raise ListStreamParseError(f"Missing closing {close}")
