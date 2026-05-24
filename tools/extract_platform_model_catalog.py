#!/usr/bin/env python3
"""Extract platform metadata/type vocabulary evidence into a small catalog.

The catalog is generated from local 1C platform binaries/resources plus the
embedded XSD extraction report. It intentionally contains only vocabulary and
evidence source names, not platform binaries or private configuration data.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import zipfile
from pathlib import Path


METADATA_CANDIDATES = {
    "Configuration": ("Configuration",),
    "Common": ("Общие",),
    "Constant": ("Константы",),
    "Catalog": ("Справочники", "CatalogRef", "Справочник"),
    "Document": ("Документы", "DocumentRef", "Документ"),
    "DocumentJournal": ("Журналы документов",),
    "Enumeration": ("Перечисления", "EnumRef"),
    "Report": ("Отчеты",),
    "DataProcessor": ("Обработки",),
    "ChartOfCharacteristicTypes": ("Планы видов характеристик", "ChartOfCharacteristicTypes"),
    "ChartOfAccounts": ("Планы счетов",),
    "ChartOfCalculationTypes": ("Планы видов расчета",),
    "InformationRegister": ("Регистры сведений",),
    "AccumulationRegister": ("Регистры накопления",),
    "AccountingRegister": ("Регистры бухгалтерии",),
    "CalculationRegister": ("Регистры расчета",),
    "BusinessProcess": ("Бизнес-процессы",),
    "Task": ("Задачи",),
    "ExternalDataSource": ("Внешние источники данных", "ExternalDataSource", "ВнешнийИсточникДанных"),
    "ExternalDataProcessorObject": ("ExternalDataProcessorObject",),
    "ExchangePlan": ("Планы обмена",),
    "FilterCriterion": ("Критерии отбора",),
    "CommonModule": ("Общие модули",),
    "CommonForm": ("Общие формы",),
}


TYPE_TREE_CANDIDATES = {
    "Number": ("Число",),
    "String": ("Строка",),
    "Date": ("Дата",),
    "Boolean": ("Булево",),
    "ValueList": ("СписокЗначений",),
    "CatalogRef": ("СправочникСсылка", "CatalogRef"),
    "DocumentRef": ("ДокументСсылка", "DocumentRef"),
    "EnumerationRef": ("ПеречислениеСсылка", "EnumRef"),
    "ChartOfCharacteristicTypesRef": ("ПланВидовХарактеристикСсылка",),
    "ChartOfAccountsRef": ("ПланСчетовСсылка",),
    "ChartOfCalculationTypesRef": ("ПланВидовРасчетаСсылка",),
    "BusinessProcessRef": ("БизнесПроцессСсылка",),
    "BusinessProcessRoutePointRef": ("ТочкаМаршрутаБизнесПроцессаСсылка",),
    "TaskRef": ("ЗадачаСсылка",),
    "ExchangePlanRef": ("ПланОбменаСсылка",),
    "Characteristic": ("Характеристика",),
    "AnyRef": ("ЛюбаяСсылка",),
    "TypeDescription": ("ОписаниеТипов", "TypeDescription"),
    "Color": ("Цвет",),
    "Font": ("Шрифт",),
    "StandardBeginningDate": ("СтандартнаяДатаНачала", "StandardBeginningDate"),
    "StandardPeriod": ("СтандартныйПериод", "StandardPeriod"),
    "Filter": ("Отбор",),
    "Order": ("Порядок",),
    "ConditionalAppearance": ("УсловноеОформление",),
    "AppearanceSettings": ("НастройкаОформления",),
    "FormattingField": ("ОбластьОформления",),
    "SystemEnumeration": ("Системные перечисления",),
}


TYPE_DOMAIN_CODES = [
    {"code": "S", "typeName": "xs:string", "kind": "primitive", "source": "TypeDomainPattern"},
    {"code": "N", "typeName": "xs:decimal", "kind": "primitive", "source": "TypeDomainPattern"},
    {"code": "B", "typeName": "xs:boolean", "kind": "primitive", "source": "TypeDomainPattern"},
    {"code": "D", "typeName": "xs:dateTime", "kind": "primitive", "source": "TypeDomainPattern"},
    {"code": "U", "typeName": "xs:anyType", "kind": "primitive", "source": "TypeDomainPattern"},
    {"code": "#", "typeName": "cfg:uuid", "kind": "reference", "source": "TypeDomainPattern"},
]


def encoded_variants(text: str) -> tuple[bytes, ...]:
    return (
        text.encode("utf-8", errors="ignore"),
        text.encode("utf-16le", errors="ignore"),
        text.encode("cp1251", errors="ignore"),
    )


def scan_candidates(root: Path, candidates: dict[str, tuple[str, ...]]) -> list[dict[str, object]]:
    result: list[dict[str, object]] = []
    files = [
        path
        for path in sorted(root.iterdir())
        if path.is_file() and path.suffix.lower() in {".dll", ".exe", ".res", ".hbk"}
    ]
    for name, tokens in candidates.items():
        sources: list[str] = []
        for path in files:
            data = path.read_bytes()
            if any(variant and variant in data for token in tokens for variant in encoded_variants(token)):
                sources.append(path.name)
        if sources:
            result.append({"name": name, "tokens": list(tokens), "sources": sources[:16], "sourceCount": len(sources)})
    return result


def schema_resources(report_path: Path) -> list[dict[str, str]]:
    report = json.loads(report_path.read_text(encoding="utf-8"))
    selected = []
    for item in report.get("resources", []):
        file_name = item.get("file", "")
        if any(key in file_name for key in ("logform", "uobjects", "core", "ui", "geo", "graphscheme", "txtedt")):
            selected.append(
                {
                    "source": item.get("source", ""),
                    "schema": file_name,
                    "sha256": item.get("sha256", ""),
                }
            )
    return selected


def serializer_evidence(decompile_path: Path) -> list[dict[str, str]]:
    data = json.loads(decompile_path.read_text(encoding="utf-8"))
    body = "\n".join(str(item.get("body", "")) for item in data.get("functions", []))
    serializers = []
    for name, direction, role in [
        ("ListInStream", "read", "platform bracket/list-stream reader"),
        ("ListOutStream", "write", "platform bracket/list-stream writer"),
        ("CompositeID::deserialize", "read", "platform metadata object id reader"),
        ("CompositeID::serialize", "write", "platform metadata object id writer"),
        ("TypeDomainPattern::deserialize", "read", "platform type-domain reader"),
        ("TypeDomainPattern::serialize", "write", "platform type-domain writer"),
        ("ValueFromStringInternal", "read", "typed scalar value reader"),
        ("ValueToStringInternal", "write", "typed scalar value writer"),
    ]:
        token = name.split("::", 1)[0]
        if token in body or name.startswith("Value"):
            serializers.append({"name": name, "direction": direction, "role": role})
    return serializers


def run_javap(jar_path: Path, class_name: str) -> str:
    result = subprocess.run(
        ["javap", "-classpath", str(jar_path), "-public", class_name],
        check=True,
        text=True,
        capture_output=True,
    )
    return result.stdout


def find_plugin_jar(plugins_root: Path, prefix: str) -> Path | None:
    jars = sorted(plugins_root.glob(f"{prefix}_*.jar"))
    return jars[-1] if jars else None


def camel_to_words(name: str) -> str:
    return re.sub(r"(?<!^)(?=[A-Z])", " ", name)


def factory_created_types(jar_path: Path, factory_class: str, package_prefix: str) -> list[str]:
    output = run_javap(jar_path, factory_class)
    names: list[str] = []
    pattern = re.compile(rf"abstract {re.escape(package_prefix)}\.([A-Za-z0-9_]+) create([A-Za-z0-9_]+)\(")
    for line in output.splitlines():
        match = pattern.search(line)
        if not match:
            continue
        return_type, created_name = match.groups()
        if return_type == created_name and created_name not in names:
            names.append(created_name)
    return names


def class_feature_names(jar_path: Path, package_class: str) -> dict[str, list[str]]:
    output = run_javap(jar_path, package_class)
    features: dict[str, list[str]] = {}
    for line in output.splitlines():
        match = re.search(r"static final int ([A-Z][A-Z0-9_]+)__([A-Z][A-Z0-9_]+);", line)
        if not match:
            continue
        class_name, feature_name = match.groups()
        features.setdefault(class_name, []).append(feature_name)
    return features


def edt_model_catalog(plugins_root: Path | None) -> dict[str, object]:
    if not plugins_root:
        return {}

    metadata_jar = find_plugin_jar(plugins_root, "com._1c.g5.v8.dt.metadata")
    mcore_jar = find_plugin_jar(plugins_root, "com._1c.g5.v8.dt.mcore")
    if not metadata_jar or not mcore_jar:
        return {}

    metadata_types = factory_created_types(
        metadata_jar,
        "com._1c.g5.v8.dt.metadata.mdclass.MdClassFactory",
        "com._1c.g5.v8.dt.metadata.mdclass",
    )
    mcore_types = factory_created_types(
        mcore_jar,
        "com._1c.g5.v8.dt.mcore.McoreFactory",
        "com._1c.g5.v8.dt.mcore",
    )
    metadata_features = class_feature_names(metadata_jar, "com._1c.g5.v8.dt.metadata.mdclass.MdClassPackage")
    mcore_features = class_feature_names(mcore_jar, "com._1c.g5.v8.dt.mcore.McorePackage")

    with zipfile.ZipFile(metadata_jar) as archive:
        metadata_entries = [name for name in archive.namelist() if name.startswith("com/_1c/g5/v8/dt/metadata/mdclass/")]
    with zipfile.ZipFile(mcore_jar) as archive:
        mcore_entries = [name for name in archive.namelist() if name.startswith("com/_1c/g5/v8/dt/mcore/")]

    return {
        "source": "1C EDT EMF model jars",
        "plugins": [
            {"id": metadata_jar.name, "model": "metadata.mdclass", "createdTypes": metadata_types},
            {"id": mcore_jar.name, "model": "mcore", "createdTypes": mcore_types},
        ],
        "metadataClassFeatures": metadata_features,
        "mcoreClassFeatures": mcore_features,
        "entryCounts": {
            "metadata.mdclass": len(metadata_entries),
            "mcore": len(mcore_entries),
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--platform-root", required=True)
    parser.add_argument("--resources-json", required=True)
    parser.add_argument("--decompile-json", required=True)
    parser.add_argument("--edt-plugins-root")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    root = Path(args.platform_root)
    catalog = {
        "source": "1C 8.2 platform binaries/resources",
        "schemaResources": schema_resources(Path(args.resources_json)),
        "serializers": serializer_evidence(Path(args.decompile_json)),
        "metadataObjectKinds": scan_candidates(root, METADATA_CANDIDATES),
        "typeTreeKinds": scan_candidates(root, TYPE_TREE_CANDIDATES),
        "typeDomainCodes": TYPE_DOMAIN_CODES,
        "edtModel": edt_model_catalog(Path(args.edt_plugins_root) if args.edt_plugins_root else None),
    }
    Path(args.out).write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"out": args.out, "metadata": len(catalog["metadataObjectKinds"]), "types": len(catalog["typeTreeKinds"])}, ensure_ascii=False))


if __name__ == "__main__":
    main()
