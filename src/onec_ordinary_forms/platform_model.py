"""Platform object-model vocabulary used by the ordinary-form codec.

This module is the Python counterpart of ``metadata-configuration.xsd``. It is
not a dump format; it is a small, explicit catalog of platform concepts that
the ListIn/ListOut layer can use while mapping binary form streams to named XML.
"""

from __future__ import annotations

from dataclasses import dataclass
import json
from importlib import resources


@dataclass(frozen=True)
class PlatformSchemaResource:
    source: str
    schema: str
    namespace: str


@dataclass(frozen=True)
class PlatformSerializer:
    name: str
    direction: str
    role: str


@dataclass(frozen=True)
class PlatformTypeDomainCode:
    code: str
    type_name: str
    kind: str


def load_platform_model_catalog() -> dict[str, object]:
    data = resources.files(__package__).joinpath("platform_model_catalog.json").read_text(encoding="utf-8")
    return json.loads(data)


PLATFORM_MODEL_CATALOG = load_platform_model_catalog()


def schema_namespace(schema_name: str) -> str:
    if "managed-application_logform_layouter" in schema_name:
        return "http://v8.1c.ru/8.2/managed-application/logform/layouter"
    if "managed-application_logform.xsd" in schema_name:
        return "http://v8.1c.ru/8.2/managed-application/logform"
    if "uobjects" in schema_name:
        return "http://v8.1c.ru/8.2/uobjects"
    if "xdto_root" in schema_name and schema_name.endswith("_core.xsd"):
        return "http://v8.1c.ru/8.1/data/core"
    if "xdto_root" in schema_name and schema_name.endswith("_ui.xsd"):
        return "http://v8.1c.ru/8.1/data/ui"
    if schema_name.endswith("_geo.xsd"):
        return "http://v8.1c.ru/8.2/data/geo"
    if schema_name.endswith("_graphscheme.xsd"):
        return "http://v8.1c.ru/8.2/data/graphscheme"
    if schema_name.endswith("_txtedt.xsd"):
        return "http://v8.1c.ru/8.1/data/txtedt"
    return ""


def schema_short_name(schema_name: str) -> str:
    if "logform_layouter" in schema_name:
        return "logform_layouter.xsd"
    if "logform" in schema_name:
        return "logform.xsd"
    if "uobjects" in schema_name:
        return "uobjects.xsd"
    return schema_name.rsplit("_", 1)[-1]


_schema_resources = []
_schema_resource_keys = set()
for _item in PLATFORM_MODEL_CATALOG["schemaResources"]:
    _schema = str(_item["schema"])
    _namespace = schema_namespace(_schema)
    if not _namespace:
        continue
    _resource = PlatformSchemaResource(str(_item["source"]), schema_short_name(_schema), _namespace)
    _key = (_resource.source, _resource.schema, _resource.namespace)
    if _key not in _schema_resource_keys:
        _schema_resources.append(_resource)
        _schema_resource_keys.add(_key)

PLATFORM_SCHEMA_RESOURCES = tuple(_schema_resources)


PLATFORM_SERIALIZERS = tuple(
    PlatformSerializer(str(item["name"]), str(item["direction"]), str(item["role"]))
    for item in PLATFORM_MODEL_CATALOG["serializers"]
)


PLATFORM_TYPE_DOMAIN_CODES = tuple(
    PlatformTypeDomainCode(str(item["code"]), str(item["typeName"]), str(item["kind"]))
    for item in PLATFORM_MODEL_CATALOG["typeDomainCodes"]
)


PLATFORM_TYPE_DOMAIN_CODE_NAMES = {item.code: item.type_name for item in PLATFORM_TYPE_DOMAIN_CODES}


PLATFORM_METADATA_OBJECT_KINDS = tuple(str(item["name"]) for item in PLATFORM_MODEL_CATALOG["metadataObjectKinds"])


PLATFORM_TYPE_TREE_KINDS = tuple(str(item["name"]) for item in PLATFORM_MODEL_CATALOG["typeTreeKinds"])


_edt_plugins = PLATFORM_MODEL_CATALOG.get("edtModel", {}).get("plugins", [])
PLATFORM_EDT_METADATA_CLASSES = tuple(_edt_plugins[0].get("createdTypes", ())) if len(_edt_plugins) > 0 else ()


PLATFORM_EDT_MCORE_CLASSES = tuple(_edt_plugins[1].get("createdTypes", ())) if len(_edt_plugins) > 1 else ()
