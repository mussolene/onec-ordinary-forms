"""Platform-derived ordinary form constants.

The ordinary form designer imports the wbase ``cf_form_controls8`` family in
platform data-object/list-stream paths. Those values identify platform formats
for ordinary-control payloads. The class IDs below are decoded from forms
produced by that runtime and are the stable bridge between list-stream nodes and
the editable XML object model.
"""

from __future__ import annotations

from dataclasses import dataclass
from struct import pack, unpack_from


CF_FORM_CONTROLS8_FORMAT_ID = 0x2500
CF_FORM_CONTROLS_POSITION8_FORMAT_ID = 0x5500
CF_FORM_CONTROLS_INFO8_FORMAT_ID = 0x9D00

PLATFORM_TRANSFER_COUNT_SIZE = 4
PLATFORM_TRANSFER_RECORD_SIZE = 0x10


@dataclass(frozen=True)
class PlatformTransferRecord:
    """One 16-byte entry from platform cf_form_controls_info8/position8 data."""

    word0: int
    word1: int
    word2: int
    word3: int

    def as_tuple(self) -> tuple[int, int, int, int]:
        return (self.word0, self.word1, self.word2, self.word3)


ORDINARY_CONTROL_CLASS_BY_GUID = {
    "09ccdc77-ea1a-4a6d-ab1c-3435eada2433": "Panel",
    "0fc7e20d-f241-460c-bdf4-5ad88e5474a5": "Label",
    "151ef23e-6bb2-4681-83d0-35bc2217230c": "Image",
    "6ff79819-710e-4145-97cd-1618da79e3e2": "Button",
    "381ed624-9217-4e63-85db-c4c3cb87daae": "InputField",
    "e69bf21d-97b2-4f37-86db-675aea9ec2cb": "CommandBar",
    "35af3d93-d7c7-4a2e-a8eb-bac87a1a3f26": "CheckBox",
    "ea83fe3a-ac3c-4cce-8045-3dddf35b28b1": "Table",
    "64483e7f-3833-48e2-8c75-2c31aac49f6e": "ChoiceField",
    "236a17b3-7f44-46d9-a907-75f9cdc61ab5": "SpreadsheetDocumentField",
    "90db814a-c75f-4b54-bc96-df62e554d67d": "GroupBox",
    "782e569a-79a7-4a4f-a936-b48d013936ec": "RadioButton",
    "36e52348-5d60-4770-8e89-a16ed50a2006": "Splitter",
    "a8b97779-1a4b-4059-b09c-807f86d2a461": "Chart",
    "19f8b798-314e-4b4e-8121-905b2a7a03f5": "ListBox",
    "d92a805c-98ae-4750-9158-d9ce7cec2f20": "HTMLDocumentField",
}

ORDINARY_CONTROL_GUID_BY_TYPE = {value: key for key, value in ORDINARY_CONTROL_CLASS_BY_GUID.items()}


def ordinary_control_type(class_id: object) -> str:
    return ORDINARY_CONTROL_CLASS_BY_GUID.get(str(class_id).lower(), "")


def unpack_platform_transfer_records(payload: bytes) -> list[PlatformTransferRecord]:
    """Decode platform transfer payloads shaped as ``uint32 count + 0x10 * count``.

    1C 8.2 writes both ``cf_form_controls_position8`` and
    ``cf_form_controls_info8`` this way. In 8.5 decompile the info payload is
    copied as two 64-bit words, but the external byte layout remains 16 bytes
    per record, so the four-word representation keeps the contract explicit.
    """

    if len(payload) < PLATFORM_TRANSFER_COUNT_SIZE:
        raise ValueError("Platform transfer payload is too short for count")
    count = unpack_from("<I", payload, 0)[0]
    expected = PLATFORM_TRANSFER_COUNT_SIZE + count * PLATFORM_TRANSFER_RECORD_SIZE
    if len(payload) != expected:
        raise ValueError(f"Platform transfer payload has {len(payload)} bytes, expected {expected}")
    records: list[PlatformTransferRecord] = []
    offset = PLATFORM_TRANSFER_COUNT_SIZE
    for _ in range(count):
        records.append(PlatformTransferRecord(*unpack_from("<IIII", payload, offset)))
        offset += PLATFORM_TRANSFER_RECORD_SIZE
    return records


def pack_platform_transfer_records(records: list[PlatformTransferRecord]) -> bytes:
    """Encode ``cf_form_controls_info8``/``position8`` transfer records."""

    result = bytearray(pack("<I", len(records)))
    for record in records:
        result.extend(pack("<IIII", *record.as_tuple()))
    return bytes(result)
