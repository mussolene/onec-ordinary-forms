import pytest

from onec_ordinary_forms.value_codec import (
    TypeDomainPatternItem,
    clean_atom,
    dump_type_domain_pattern,
    localized_text_from_record,
    localized_text_record,
    parse_composite_id,
    parse_type_domain_pattern,
    quote_atom,
    value_from_string_internal,
    value_to_string_internal,
)


def test_quote_and_clean_atoms_use_1c_double_quote_style() -> None:
    atom = quote_atom('A "quoted" value')
    assert atom == '"A ""quoted"" value"'
    assert clean_atom(atom) == 'A "quoted" value'


def test_type_domain_pattern_roundtrip_primitives_and_reference() -> None:
    pattern = ['"S"', '"#"', "01234567-89ab-cdef-0123-456789abcdef", '"B"']
    items = parse_type_domain_pattern(pattern, {"01234567-89ab-cdef-0123-456789abcdef": "cfg:Document.Ref"})

    assert items == [
        TypeDomainPatternItem(code="S", type_name="xs:string", kind="primitive"),
        TypeDomainPatternItem(
            code="#",
            uuid="01234567-89ab-cdef-0123-456789abcdef",
            type_name="cfg:Document.Ref",
            kind="reference",
        ),
        TypeDomainPatternItem(code="B", type_name="xs:boolean", kind="primitive"),
    ]
    assert dump_type_domain_pattern(items) == pattern


def test_composite_id_validation_matches_platform_schema_shape() -> None:
    assert parse_composite_id("-1") == "-1"
    assert parse_composite_id("12:01234567-89ab-cdef-0123-456789abcdef").startswith("12:")
    with pytest.raises(ValueError):
        parse_composite_id("not-an-id")


def test_value_string_internal_confirmed_scalar_subset_roundtrips() -> None:
    for value in [None, True, False, 42, 3.5, "text"]:
        assert value_from_string_internal(value_to_string_internal(value)) == value


def test_confirmed_internal_pipeline_is_bidirectional_for_typed_values() -> None:
    source_pattern = ['"S"', '"B"']
    typed_model = parse_type_domain_pattern(source_pattern)
    public_items = [
        {"code": item.code, "typeName": item.type_name, "kind": item.kind}
        for item in typed_model
    ]
    rebuilt_model = [
        TypeDomainPatternItem(code=item["code"], type_name=item["typeName"], kind=item["kind"])
        for item in public_items
    ]

    assert dump_type_domain_pattern(rebuilt_model) == source_pattern


def test_localized_text_record_roundtrip() -> None:
    record = localized_text_record("Заголовок")
    assert record == ["1", "1", ['"ru"', '"Заголовок"']]
    assert localized_text_from_record(record) == "Заголовок"
