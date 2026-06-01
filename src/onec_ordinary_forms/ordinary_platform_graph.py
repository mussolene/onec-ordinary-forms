"""Internal ordinary-form platform object graph.

This module connects the public ordinary-form object model to the platform
runtime graph discovered in ``dsgnfrm``. It is intentionally internal: public
``Form.xml`` stays typed and object-oriented, while this graph is the staging
area for platform ListOutStream persistence work.
"""

from __future__ import annotations

from dataclasses import dataclass

from onec_ordinary_forms.liststream import parse_list_stream_document
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
from onec_ordinary_forms.platform_model import (
    PLATFORM_RUNTIME_CALL_EDGES,
    PLATFORM_RUNTIME_EDGES,
    PLATFORM_RUNTIME_NODES,
    PlatformRuntimeCallEdge,
)


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
