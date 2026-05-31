"""Internal ordinary-form platform object graph.

This module connects the public ordinary-form object model to the platform
runtime graph discovered in ``dsgnfrm``. It is intentionally internal: public
``Form.xml`` stays typed and object-oriented, while this graph is the staging
area for platform ListOutStream persistence work.
"""

from __future__ import annotations

from dataclasses import dataclass

from onec_ordinary_forms.ordinary_model import OrdinaryControl, OrdinaryFormModel
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
