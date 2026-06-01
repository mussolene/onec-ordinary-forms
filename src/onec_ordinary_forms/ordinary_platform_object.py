"""Object-oriented facade over the ordinary-form platform object graph.

The facade is the editing API for ordinary forms. It keeps public XML and raw
list-stream details out of callers: edits are named object operations applied
to an existing ``OrdinaryPlatformObject`` and then serialized through the
internal platform-compatible ListOutStream boundary.
"""

from __future__ import annotations

from dataclasses import dataclass

from onec_ordinary_forms.ordinary_platform_graph import (
    OrdinaryPlatformControlObject,
    OrdinaryPlatformObject,
    UnsupportedPlatformObjectOperation,
    platform_object_from_list_stream_text,
)


@dataclass(frozen=True)
class PlatformControlObject:
    """Stable object-level view of one ordinary form control."""

    node_id: str
    object_id: str
    name: str
    control_type: str
    title: str
    class_id: str
    child_count: int

    @classmethod
    def from_platform_control(cls, control: OrdinaryPlatformControlObject) -> "PlatformControlObject":
        return cls(
            node_id=control.node_id,
            object_id=control.object_id,
            name=control.name,
            control_type=control.control_type,
            title=control.title,
            class_id=control.class_id,
            child_count=control.actual_child_count,
        )


@dataclass(frozen=True)
class PlatformFormObject:
    """Editable object model for an ordinary form backed by platform streams."""

    platform_object: OrdinaryPlatformObject

    @classmethod
    def from_list_stream_text(cls, text: str) -> "PlatformFormObject":
        return cls(platform_object_from_list_stream_text(text))

    def to_platform_object(self) -> OrdinaryPlatformObject:
        return self.platform_object

    def to_list_stream_text(self, *, include_bom: bool = False) -> str:
        return self.platform_object.to_list_stream_text(include_bom=include_bom)

    def controls(self) -> tuple[PlatformControlObject, ...]:
        return tuple(
            PlatformControlObject.from_platform_control(control)
            for control in self.platform_object.flatten_controls()
        )

    def control(self, ref: str) -> PlatformControlObject:
        return PlatformControlObject.from_platform_control(self._resolve_control(ref))

    def get_control_property(self, control_ref: str, property_name: str) -> str:
        control = self._resolve_control(control_ref)
        return self.platform_object.control_property(control.node_id, property_name)

    def rename_control(
        self,
        control_ref: str,
        name: str,
        *,
        title: str | None = None,
    ) -> "PlatformFormObject":
        control = self._resolve_control(control_ref)
        updated = self.platform_object.with_control_updates(
            control.node_id,
            name=name,
            title=title,
        )
        return PlatformFormObject(updated)

    def set_control_title(self, control_ref: str, title: str) -> "PlatformFormObject":
        control = self._resolve_control(control_ref)
        return PlatformFormObject(self.platform_object.with_control_title(control.node_id, title))

    def set_control_property(
        self,
        control_ref: str,
        property_name: str,
        value: object,
    ) -> "PlatformFormObject":
        control = self._resolve_control(control_ref)
        updated = self.platform_object.with_control_property(control.node_id, property_name, value)
        return PlatformFormObject(updated)

    def _resolve_control(self, ref: str) -> OrdinaryPlatformControlObject:
        if ref.startswith("control:"):
            return self.platform_object.control(ref)
        return self.platform_object.control_by_name(ref)


__all__ = [
    "PlatformControlObject",
    "PlatformFormObject",
    "UnsupportedPlatformObjectOperation",
]
