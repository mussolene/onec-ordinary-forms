"""Internal ordinary-form platform object graph.

This module connects the public ordinary-form object model to the platform
runtime graph discovered in ``dsgnfrm``. It is intentionally internal: public
``Form.xml`` stays typed and object-oriented, while this graph is the staging
area for platform ListOutStream persistence work.
"""

from __future__ import annotations

import copy
from dataclasses import dataclass

from onec_ordinary_forms.liststream import dumps_list_out_stream, parse_list_stream_document
from onec_ordinary_forms.ordinary_model import (
    OrdinaryControl,
    OrdinaryFormModel,
    parse_ordinary_form_model,
)
from onec_ordinary_forms.ordinary_platform import (
    CF_FORM_CONTROLS8_FORMAT_ID,
    CF_FORM_CONTROLS_INFO8_FORMAT_ID,
    CF_FORM_CONTROLS_POSITION8_FORMAT_ID,
    ORDINARY_CONTROL_CLASS_BY_GUID,
)
from onec_ordinary_forms.ordinary_platform_mappings import platform_property_xml_name
from onec_ordinary_forms.ordinary_stream import CONTROL_INFO_SLOT_DESCRIPTORS
from onec_ordinary_forms.platform_model import (
    PLATFORM_RUNTIME_CALL_EDGES,
    PLATFORM_RUNTIME_EDGES,
    PLATFORM_RUNTIME_NODES,
    PlatformRuntimeCallEdge,
)
from onec_ordinary_forms.value_codec import clean_atom, quote_atom


@dataclass(frozen=True)
class PlatformObjectNode:
    id: str
    name: str
    layer: str
    role: str
    control_type: str = ""
    evidence: str = ""


@dataclass(frozen=True)
class PlatformObjectEdge:
    source: str
    target: str
    kind: str
    evidence: str = ""


@dataclass(frozen=True)
class PlatformPersistenceRecord:
    owner: str
    family: str
    format_id: int
    role: str
    evidence: str = ""


@dataclass(frozen=True)
class PlatformObjectDiagnostic:
    severity: str
    code: str
    message: str
    node_id: str = ""


class UnsupportedPlatformObjectOperation(ValueError):
    """Raised when an object-level edit has no verified platform accessor yet."""


@dataclass(frozen=True)
class OrdinaryPlatformControlObject:
    node_id: str
    object_id: str
    name: str
    control_type: str
    class_id: str
    title: str
    info_kind: str
    declared_child_count: int
    actual_child_count: int
    state_count: int
    state_names: tuple[str, ...]
    position_record_count: int
    persistence_records: tuple[PlatformPersistenceRecord, ...]
    children: tuple["OrdinaryPlatformControlObject", ...] = ()

    def persistence_by_family(self, family: str) -> tuple[PlatformPersistenceRecord, ...]:
        return tuple(record for record in self.persistence_records if record.family == family)

    def supports_platform_format(self, format_id: int) -> bool:
        return any(record.format_id == format_id for record in self.persistence_records)

    def child(self, name: str) -> "OrdinaryPlatformControlObject":
        for item in self.children:
            if item.name == name:
                return item
        raise KeyError(f"Unknown child control {name!r} for {self.node_id}")


@dataclass(frozen=True)
class OrdinaryPlatformObject:
    root: object
    model: OrdinaryFormModel
    graph: "OrdinaryFormPlatformGraph"
    controls: tuple[OrdinaryPlatformControlObject, ...]
    diagnostics: tuple[PlatformObjectDiagnostic, ...] = ()

    @property
    def can_use_object_model(self) -> bool:
        return not any(item.severity == "error" for item in self.diagnostics)

    def flatten_controls(self) -> tuple[OrdinaryPlatformControlObject, ...]:
        result: list[OrdinaryPlatformControlObject] = []
        for control in self.controls:
            result.extend(_flatten_platform_control(control))
        return tuple(result)

    def control(self, node_id: str) -> OrdinaryPlatformControlObject:
        for item in self.flatten_controls():
            if item.node_id == node_id:
                return item
        raise KeyError(f"Unknown platform object control: {node_id}")

    def control_by_name(self, name: str) -> OrdinaryPlatformControlObject:
        for item in self.flatten_controls():
            if item.name == name:
                return item
        raise KeyError(f"Unknown platform object control name: {name}")

    def persistence_by_family(self, family: str) -> tuple[PlatformPersistenceRecord, ...]:
        return self.graph.persistence_by_family(family)

    def to_list_stream_root(self) -> object:
        return copy.deepcopy(self.root)

    def to_list_stream_text(self, *, include_bom: bool = False) -> str:
        text = dumps_list_out_stream(self.root)
        return "\ufeff" + text if include_bom else text

    def with_control_updates(
        self,
        node_id: str,
        *,
        name: str | None = None,
        title: str | None = None,
    ) -> "OrdinaryPlatformObject":
        if name is None and title is None:
            return self
        control = self.control(node_id)
        root = copy.deepcopy(self.root)
        raw = _find_control_node(root, control)
        if raw is None:
            raise KeyError(f"Cannot find raw list-stream node for {node_id}")
        if name is not None:
            _set_control_metadata_name(raw, name)
        if title is not None and not _set_control_title(raw, control.control_type, title):
            raise ValueError(f"Cannot find localized title record for {node_id}")
        return platform_object_from_list_stream_root(root)

    def with_control_name(self, node_id: str, name: str) -> "OrdinaryPlatformObject":
        return self.with_control_updates(node_id, name=name)

    def with_control_title(self, node_id: str, title: str) -> "OrdinaryPlatformObject":
        return self.with_control_updates(node_id, title=title)

    def control_property(self, node_id: str, property_name: str) -> str:
        control = self.control(node_id)
        property_key = _control_property_key(control.control_type, property_name)
        if property_key == "Name":
            return control.name
        if property_key == "Title":
            return control.title
        raw = _find_control_node(self.root, control)
        if raw is None:
            raise KeyError(f"Cannot find raw list-stream node for {node_id}")
        return _control_property_value(raw, control.control_type, property_key)

    def with_control_property(
        self,
        node_id: str,
        property_name: str,
        value: object,
    ) -> "OrdinaryPlatformObject":
        control = self.control(node_id)
        property_key = _control_property_key(control.control_type, property_name)
        if property_key == "Name":
            return self.with_control_name(node_id, str(value))
        if property_key == "Title":
            return self.with_control_title(node_id, str(value))

        root = copy.deepcopy(self.root)
        raw = _find_control_node(root, control)
        if raw is None:
            raise KeyError(f"Cannot find raw list-stream node for {node_id}")
        _set_control_property(raw, control.control_type, property_key, value)
        return platform_object_from_list_stream_root(root)


@dataclass(frozen=True)
class OrdinaryFormPlatformGraph:
    nodes: tuple[PlatformObjectNode, ...]
    edges: tuple[PlatformObjectEdge, ...]
    persistence_records: tuple[PlatformPersistenceRecord, ...]
    call_edges: tuple[PlatformRuntimeCallEdge, ...]

    def node(self, node_id: str) -> PlatformObjectNode:
        for item in self.nodes:
            if item.id == node_id:
                return item
        raise KeyError(f"Unknown platform graph node: {node_id}")

    def nodes_by_layer(self, layer: str) -> tuple[PlatformObjectNode, ...]:
        return tuple(item for item in self.nodes if item.layer == layer)

    def control_nodes(self) -> tuple[PlatformObjectNode, ...]:
        return self.nodes_by_layer("object")

    def has_edge(self, source: str, target: str, kind: str) -> bool:
        return any(
            edge.source == source and edge.target == target and edge.kind == kind
            for edge in self.edges
        )

    def persistence_by_family(self, family: str) -> tuple[PlatformPersistenceRecord, ...]:
        return tuple(record for record in self.persistence_records if record.family == family)


def runtime_skeleton_graph() -> OrdinaryFormPlatformGraph:
    return OrdinaryFormPlatformGraph(
        nodes=tuple(
            PlatformObjectNode(
                id=node.name,
                name=node.name,
                layer=node.layer,
                role=node.role,
                evidence=node.evidence,
            )
            for node in PLATFORM_RUNTIME_NODES
        ),
        edges=tuple(
            PlatformObjectEdge(
                source=edge.source,
                target=edge.target,
                kind=edge.kind,
                evidence=edge.evidence,
            )
            for edge in PLATFORM_RUNTIME_EDGES
        ),
        persistence_records=(),
        call_edges=PLATFORM_RUNTIME_CALL_EDGES,
    )


def platform_graph_from_model(model: OrdinaryFormModel) -> OrdinaryFormPlatformGraph:
    skeleton = runtime_skeleton_graph()
    nodes = list(skeleton.nodes)
    edges = list(skeleton.edges)
    persistence_records: list[PlatformPersistenceRecord] = []

    for index, control in enumerate(model.flatten(), start=1):
        node_id = _control_node_id(control, index)
        nodes.append(
            PlatformObjectNode(
                id=node_id,
                name=control.name or node_id,
                layer="object",
                role="ordinary-control",
                control_type=control.type,
                evidence="OrdinaryFormModel control node",
            )
        )
        edges.append(
            PlatformObjectEdge(
                source="ControlSite",
                target=node_id,
                kind="contains-control",
                evidence="ControlSite bridges platform view to ordinary controls",
            )
        )
        persistence_records.extend(_control_persistence_records(control, node_id))

    return OrdinaryFormPlatformGraph(
        nodes=tuple(nodes),
        edges=tuple(edges),
        persistence_records=tuple(persistence_records),
        call_edges=skeleton.call_edges,
    )


def platform_object_from_model(root: object, model: OrdinaryFormModel) -> OrdinaryPlatformObject:
    graph = platform_graph_from_model(model)
    flat = model.flatten()
    node_ids = {
        id(control): _control_node_id(control, index)
        for index, control in enumerate(flat, start=1)
    }
    records_by_owner: dict[str, list[PlatformPersistenceRecord]] = {}
    for record in graph.persistence_records:
        records_by_owner.setdefault(record.owner, []).append(record)
    controls = tuple(
        _platform_control_object(control, node_ids, records_by_owner)
        for control in model.controls
    )
    return OrdinaryPlatformObject(
        root=root,
        model=model,
        graph=graph,
        controls=controls,
        diagnostics=_platform_object_diagnostics(controls),
    )


def platform_object_from_list_stream_root(root: object) -> OrdinaryPlatformObject:
    return platform_object_from_model(root, parse_ordinary_form_model(root))


def platform_object_from_list_stream_text(text: str) -> OrdinaryPlatformObject:
    document = parse_list_stream_document(text, allow_trailing=True)
    return platform_object_from_list_stream_root(document.value)


def platform_graph_from_list_stream_root(root: object) -> OrdinaryFormPlatformGraph:
    return platform_graph_from_model(parse_ordinary_form_model(root))


def platform_graph_from_list_stream_text(text: str) -> OrdinaryFormPlatformGraph:
    document = parse_list_stream_document(text, allow_trailing=True)
    return platform_graph_from_list_stream_root(document.value)


def all_controls_skeleton_graph() -> OrdinaryFormPlatformGraph:
    controls = [
        OrdinaryControl(
            class_id=guid,
            object_id=str(index),
            name=control_type,
            type=control_type,
            title="",
            raw=[],
            info_kind="skeleton",
            position_record_count=1,
        )
        for index, (guid, control_type) in enumerate(
            sorted(ORDINARY_CONTROL_CLASS_BY_GUID.items(), key=lambda item: item[1]),
            start=1,
        )
    ]
    return platform_graph_from_model(OrdinaryFormModel(controls))


def _control_node_id(control: OrdinaryControl, index: int) -> str:
    if control.object_id:
        return f"control:{control.object_id}"
    if control.name:
        return f"control:{control.name}"
    return f"control:{index}"


def _find_control_node(root: object, control: OrdinaryPlatformControlObject) -> list[object] | None:
    for node in _walk_lists(root):
        if (
            len(node) >= 2
            and clean_atom(node[0]) == control.class_id
            and clean_atom(node[1]) == control.object_id
            and _control_metadata_name(node) == control.name
        ):
            return node
    return None


def _set_control_metadata_name(node: list[object], name: str) -> None:
    metadata = _control_metadata_record(node)
    if metadata is None or len(metadata) < 2:
        raise ValueError("Control list-stream node has no metadata name record")
    metadata[1] = quote_atom(name)


def _set_control_title(node: list[object], control_type: str, title: str) -> bool:
    info = _control_info_slot_container(node, control_type)
    title_slot = _control_title_slot(control_type)
    if title_slot is not None and info is not None and len(info) > title_slot:
        return _set_localized_text_record(info[title_slot], title)
    return _set_first_localized_text_record(node, title)


def _control_property_key(control_type: str, property_name: str) -> str:
    if property_name in {"Name", "Имя"}:
        return "Name"
    if property_name in {"Title", "Заголовок"}:
        return "Title"
    mapped = platform_property_xml_name(control_type, property_name)
    return mapped or property_name


def _control_property_value(node: list[object], control_type: str, property_key: str) -> str:
    if control_type == "InputField" and property_key == "ReadOnly":
        record = _input_field_object_info_record(node)
        if record is not None and len(record) > 12:
            return "true" if clean_atom(record[12]) == "1" else "false"
    raise UnsupportedPlatformObjectOperation(
        f"No verified object accessor for {control_type}.{property_key}"
    )


def _set_control_property(
    node: list[object],
    control_type: str,
    property_key: str,
    value: object,
) -> None:
    if control_type == "InputField" and property_key == "ReadOnly":
        record = _input_field_object_info_record(node)
        if record is not None and len(record) > 12:
            record[12] = _platform_bool_atom(value)
            return
    raise UnsupportedPlatformObjectOperation(
        f"No verified object writer for {control_type}.{property_key}"
    )


def _input_field_object_info_record(node: list[object]) -> list[object] | None:
    info = _control_info_record(node)
    if (
        not info
        or clean_atom(info[0]) != "9"
        or len(info) <= 2
        or not isinstance(info[2], list)
    ):
        return None
    for candidate in info[2]:
        if isinstance(candidate, list) and candidate and isinstance(candidate[0], list):
            return candidate
    return None


def _platform_bool_atom(value: object) -> str:
    if isinstance(value, bool):
        return "1" if value else "0"
    text = str(value).strip().lower()
    if text in {"1", "true", "yes", "да"}:
        return "1"
    if text in {"0", "false", "no", "нет", ""}:
        return "0"
    raise ValueError(f"Cannot encode platform boolean value: {value!r}")


def _set_first_localized_text_record(value: object, title: str) -> bool:
    if _is_localized_text_record(value):
        _set_localized_text_record(value, title)
        return True
    if isinstance(value, list):
        for item in value:
            if _set_first_localized_text_record(item, title):
                return True
    return False


def _set_localized_text_record(value: object, text: str) -> bool:
    if not _is_localized_text_record(value):
        return False
    assert isinstance(value, list)
    assert isinstance(value[2], list)
    value[2][1] = quote_atom(text)
    return True


def _is_localized_text_record(value: object) -> bool:
    return (
        isinstance(value, list)
        and len(value) >= 3
        and clean_atom(value[0]) == "1"
        and clean_atom(value[1]) == "1"
        and isinstance(value[2], list)
        and len(value[2]) >= 2
        and clean_atom(value[2][0]) in {"#", "ru"}
    )


def _control_metadata_name(node: list[object]) -> str:
    metadata = _control_metadata_record(node)
    if metadata is None or len(metadata) < 2:
        return ""
    return clean_atom(metadata[1])


def _control_metadata_record(node: list[object]) -> list[object] | None:
    for child in _walk_lists(node):
        if len(child) >= 2 and clean_atom(child[0]) == "14":
            return child
    return None


def _control_info_record(node: list[object]) -> list[object] | None:
    if len(node) > 2 and isinstance(node[2], list):
        return node[2]
    return None


def _control_info_slot_container(node: list[object], control_type: str) -> list[object] | None:
    info = _control_info_record(node)
    descriptor = CONTROL_INFO_SLOT_DESCRIPTORS.get(control_type)
    if (
        info is not None
        and descriptor is not None
        and len(info) > 1
        and clean_atom(info[0]) == descriptor.info_kind
        and isinstance(info[1], list)
    ):
        return info[1]
    return info


def _control_title_slot(control_type: str) -> int | None:
    descriptor = CONTROL_INFO_SLOT_DESCRIPTORS.get(control_type)
    if descriptor is None:
        return None
    try:
        return descriptor.slot_index("Title")
    except KeyError:
        return None


def _walk_lists(value: object) -> list[list[object]]:
    result: list[list[object]] = []
    if isinstance(value, list):
        result.append(value)
        for item in value:
            result.extend(_walk_lists(item))
    return result


def _platform_control_object(
    control: OrdinaryControl,
    node_ids: dict[int, str],
    records_by_owner: dict[str, list[PlatformPersistenceRecord]],
) -> OrdinaryPlatformControlObject:
    node_id = node_ids[id(control)]
    children = tuple(
        _platform_control_object(child, node_ids, records_by_owner)
        for child in control.children
    )
    return OrdinaryPlatformControlObject(
        node_id=node_id,
        object_id=control.object_id,
        name=control.name,
        control_type=control.type,
        class_id=control.class_id,
        title=control.title,
        info_kind=control.info_kind,
        declared_child_count=control.declared_child_count,
        actual_child_count=control.actual_child_count,
        state_count=control.state_count,
        state_names=tuple(control.state_names),
        position_record_count=control.position_record_count,
        persistence_records=tuple(records_by_owner.get(node_id, ())),
        children=children,
    )


def _platform_object_diagnostics(
    controls: tuple[OrdinaryPlatformControlObject, ...],
) -> tuple[PlatformObjectDiagnostic, ...]:
    diagnostics: list[PlatformObjectDiagnostic] = []
    for control in _flatten_many_platform_controls(controls):
        if control.declared_child_count != control.actual_child_count:
            diagnostics.append(
                PlatformObjectDiagnostic(
                    severity="error",
                    code="child-count-mismatch",
                    message=(
                        f"{control.node_id} declares {control.declared_child_count} children "
                        f"but object model contains {control.actual_child_count}"
                    ),
                    node_id=control.node_id,
                )
            )
    return tuple(diagnostics)


def _flatten_many_platform_controls(
    controls: tuple[OrdinaryPlatformControlObject, ...],
) -> tuple[OrdinaryPlatformControlObject, ...]:
    result: list[OrdinaryPlatformControlObject] = []
    for control in controls:
        result.extend(_flatten_platform_control(control))
    return tuple(result)


def _flatten_platform_control(control: OrdinaryPlatformControlObject) -> list[OrdinaryPlatformControlObject]:
    result = [control]
    for child in control.children:
        result.extend(_flatten_platform_control(child))
    return result


def _control_persistence_records(
    control: OrdinaryControl,
    node_id: str,
) -> tuple[PlatformPersistenceRecord, ...]:
    records = [
        PlatformPersistenceRecord(
            owner=node_id,
            family="controls",
            format_id=CF_FORM_CONTROLS8_FORMAT_ID,
            role="control-payload",
            evidence="wbase::cf_form_controls8",
        )
    ]
    if control.position_record_count:
        records.append(
            PlatformPersistenceRecord(
                owner=node_id,
                family="position",
                format_id=CF_FORM_CONTROLS_POSITION8_FORMAT_ID,
                role="control-position",
                evidence="wbase::cf_form_controls_position8",
            )
        )
    if control.info_kind:
        records.append(
            PlatformPersistenceRecord(
                owner=node_id,
                family="info",
                format_id=CF_FORM_CONTROLS_INFO8_FORMAT_ID,
                role="control-info",
                evidence="wbase::cf_form_controls_info8",
            )
        )
    return tuple(records)
