"""Platform-derived ordinary form constants.

The 8.2 ordinary form designer imports these wbase/core entry points:
cf_form_controls8, cf_form_controls_position8, cf_form_controls_info8,
ListInStream/ListOutStream, CompositeID, and TypeDomainPattern serializers.
The class IDs below are decoded from forms produced by that runtime and are the
stable bridge between list-stream nodes and the editable XML object model.
"""

from __future__ import annotations


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


def ordinary_control_type(class_id: object) -> str:
    return ORDINARY_CONTROL_CLASS_BY_GUID.get(str(class_id).lower(), "")
