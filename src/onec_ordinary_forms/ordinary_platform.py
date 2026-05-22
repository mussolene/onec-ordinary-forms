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
}


def ordinary_control_type(class_id: object) -> str:
    return ORDINARY_CONTROL_CLASS_BY_GUID.get(str(class_id).lower(), "")
