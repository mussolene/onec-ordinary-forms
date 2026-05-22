"""Typed ordinary-form model decoded from 1C list-stream nodes."""

from __future__ import annotations

from dataclasses import dataclass, field

from onec_ordinary_forms.ordinary_platform import ordinary_control_type


@dataclass
class OrdinaryControl:
    class_id: str
    object_id: str
    name: str
    type: str
    title: str
    raw: list[object]
    declared_child_count: int = 0
    children: list["OrdinaryControl"] = field(default_factory=list)
    info_kind: str = ""
    metadata_record_type: str = ""
    metadata_owner_id: str = ""
    metadata_flag1: str = ""
    metadata_flag2: str = ""
    metadata_flag3: str = ""
    state_count: int = 0
    state_names: list[str] = field(default_factory=list)
    position_record_count: int = 0

    @property
    def actual_child_count(self) -> int:
        return len(self.children)


@dataclass
class OrdinaryFormModel:
    controls: list[OrdinaryControl]

    def flatten(self) -> list[OrdinaryControl]:
        result: list[OrdinaryControl] = []
        for control in self.controls:
            result.extend(_flatten_control(control))
        return result


def parse_ordinary_form_model(root: object) -> OrdinaryFormModel:
    if not isinstance(root, list):
        return OrdinaryFormModel([])
    controls: list[OrdinaryControl] = []
    for node in _walk_lists(root):
        if not _is_control_node(node):
            continue
        if _has_control_ancestor(root, node):
            continue
        controls.append(_parse_control(node))
    return OrdinaryFormModel(controls)


def _parse_control(node: list[object]) -> OrdinaryControl:
    child_table = _child_table(node)
    children = [_parse_control(child) for child in child_table[1:] if _is_control_node(child)] if child_table else []
    info_table = _control_info_table(node)
    metadata = _metadata_record(node)
    state_names = _state_names(info_table)
    return OrdinaryControl(
        class_id=str(node[0]),
        object_id=str(node[1]) if len(node) > 1 else "",
        name=_metadata_name(node),
        type=ordinary_control_type(node[0]),
        title=_title(node),
        raw=node,
        declared_child_count=_declared_count(child_table),
        children=children,
        info_kind=str(info_table[0]) if info_table else "",
        metadata_record_type=str(metadata[0]) if metadata else "",
        metadata_owner_id=str(metadata[2]) if metadata and len(metadata) > 2 else "",
        metadata_flag1=str(metadata[3]) if metadata and len(metadata) > 3 else "",
        metadata_flag2=str(metadata[4]) if metadata and len(metadata) > 4 else "",
        metadata_flag3=str(metadata[5]) if metadata and len(metadata) > 5 else "",
        state_count=_state_count(_state_table(info_table)),
        state_names=state_names,
        position_record_count=_position_record_count(info_table),
    )


def _is_control_node(node: object) -> bool:
    return (
        isinstance(node, list)
        and len(node) >= 5
        and ordinary_control_type(node[0]) != ""
        and _metadata_name(node) != ""
        and any(_looks_like_geometry(child) for child in node)
    )


def _has_control_ancestor(root: object, target: list[object]) -> bool:
    path = _path_to_identity(root, target)
    if path is None:
        return False
    current = root
    for index in path[:-1]:
        if isinstance(current, list):
            current = current[index]
            if current is not target and _is_control_node(current):
                return True
    return False


def _path_to_identity(value: object, target: list[object], path: tuple[int, ...] = ()) -> tuple[int, ...] | None:
    if value is target:
        return path
    if isinstance(value, list):
        for index, child in enumerate(value):
            found = _path_to_identity(child, target, path + (index,))
            if found is not None:
                return found
    return None


def _walk_lists(value: object) -> list[list[object]]:
    result: list[list[object]] = []
    if isinstance(value, list):
        result.append(value)
        for item in value:
            result.extend(_walk_lists(item))
    return result


def _child_table(node: list[object]) -> list[object] | None:
    for child in node:
        if not isinstance(child, list) or not child or not _is_count_atom(child[0]):
            continue
        expected = int(str(child[0]))
        controls = [item for item in child[1:] if _is_control_node(item)]
        if controls and len(controls) == expected:
            return child
    return None


def _control_info_table(node: list[object]) -> list[object] | None:
    if len(node) > 2 and isinstance(node[2], list):
        return node[2]
    return None


def _state_table(info_table: list[object] | None) -> list[object] | None:
    if not isinstance(info_table, list):
        return None
    best: list[object] | None = None
    for child in _walk_lists(info_table):
        if len(child) >= 2 and str(child[0]) == "1" and _is_count_atom(child[1]):
            states = [item for item in child[2:] if isinstance(item, list) and item and str(item[0]) == "6"]
            if states and len(states) == int(str(child[1])):
                if best is None or len(states) > int(str(best[1])):
                    best = child
    return best


def _state_names(info_table: list[object] | None) -> list[str]:
    state_table = _state_table(info_table)
    if not state_table:
        return []
    names: list[str] = []
    for state in state_table[2:]:
        if isinstance(state, list) and len(state) > 5:
            names.append(_clean(state[5]))
    return names


def _position_record_count(info_table: list[object] | None) -> int:
    if not isinstance(info_table, list):
        return 0
    count = 0
    for child in _walk_lists(info_table):
        if len(child) >= 9 and str(child[0]) == "2":
            try:
                int(str(child[1]))
                int(str(child[2]))
                int(str(child[3]))
                count += 1
            except ValueError:
                continue
    return count


def _declared_count(value: list[object] | None) -> int:
    if value and _is_count_atom(value[0]):
        return int(str(value[0]))
    return 0


def _state_count(value: list[object] | None) -> int:
    if value and len(value) > 1 and _is_count_atom(value[1]):
        return int(str(value[1]))
    return 0


def _metadata_name(node: list[object]) -> str:
    record = _metadata_record(node)
    if record and len(record) >= 2:
        return _clean(record[1])
    return ""


def _metadata_record(node: list[object]) -> list[object] | None:
    for child in _walk_lists(node):
        if len(child) >= 2 and str(child[0]) == "14":
            return child
    return None


def _title(node: object) -> str:
    if isinstance(node, list):
        if len(node) >= 3 and str(node[0]) == "1" and str(node[1]) == "1" and isinstance(node[2], list):
            if len(node[2]) >= 2 and _clean(node[2][0]) == "ru":
                return _clean(node[2][1])
        for child in node:
            found = _title(child)
            if found:
                return found
    return ""


def _looks_like_geometry(value: object) -> bool:
    if not isinstance(value, list) or len(value) < 5:
        return False
    try:
        int(str(value[1]))
        int(str(value[2]))
        int(str(value[3]))
        int(str(value[4]))
    except ValueError:
        return False
    return True


def _is_count_atom(value: object) -> bool:
    return str(value).isdigit()


def _clean(value: object) -> str:
    text = str(value)
    if len(text) >= 2 and text[0] == '"' and text[-1] == '"':
        return text[1:-1].replace('\\"', '"').replace("\\\\", "\\")
    return text


def _flatten_control(control: OrdinaryControl) -> list[OrdinaryControl]:
    result = [control]
    for child in control.children:
        result.extend(_flatten_control(child))
    return result
