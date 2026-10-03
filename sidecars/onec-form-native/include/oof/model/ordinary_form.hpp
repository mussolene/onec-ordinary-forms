#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace oof::model {

class ObjectId {
public:
    constexpr ObjectId() noexcept = default;
    explicit constexpr ObjectId(std::uint64_t value) noexcept : value_(value) {}

    [[nodiscard]] constexpr std::uint64_t value() const noexcept {
        return value_;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return value_ != 0;
    }

    friend constexpr auto operator<=>(ObjectId, ObjectId) noexcept = default;

private:
    std::uint64_t value_ = 0;
};

struct ObjectIdHash {
    [[nodiscard]] std::size_t operator()(ObjectId id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value());
    }
};

class PropertyId {
public:
    constexpr PropertyId() noexcept = default;

    [[nodiscard]] static constexpr PropertyId from_name(std::string_view name) noexcept {
        std::uint64_t hash = 14695981039346656037ULL;
        for (const unsigned char byte : name) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
        return PropertyId(hash == 0 ? 1 : hash);
    }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept {
        return value_;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return value_ != 0;
    }

    friend constexpr auto operator<=>(PropertyId, PropertyId) noexcept = default;

private:
    explicit constexpr PropertyId(std::uint64_t value) noexcept : value_(value) {}

    std::uint64_t value_ = 0;
};

struct PropertyIdHash {
    [[nodiscard]] std::size_t operator()(PropertyId id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value());
    }
};

enum class PropertyState : std::uint8_t {
    implicit_default,
    explicit_value,
};

template <typename T>
class Property {
public:
    Property() = default;

    explicit Property(T implicit_default)
        : default_(std::move(implicit_default)), value_(default_) {}

    [[nodiscard]] static Property explicit_value(T value) {
        Property property;
        property.set(std::move(value));
        return property;
    }

    [[nodiscard]] static Property explicit_value(T value, T implicit_default) {
        Property property(std::move(implicit_default));
        property.set(std::move(value));
        return property;
    }

    [[nodiscard]] PropertyState state() const noexcept {
        return state_;
    }

    [[nodiscard]] bool is_explicit() const noexcept {
        return state_ == PropertyState::explicit_value;
    }

    [[nodiscard]] const T& value() const noexcept {
        return value_;
    }

    [[nodiscard]] const T& default_value() const noexcept {
        return default_;
    }

    void set(T value) {
        value_ = std::move(value);
        state_ = PropertyState::explicit_value;
    }

    void reset() {
        value_ = default_;
        state_ = PropertyState::implicit_default;
    }

private:
    T default_{};
    T value_{};
    PropertyState state_ = PropertyState::implicit_default;
};

struct Form;
struct ControlNode;
struct Page;
struct Attribute;
struct Command;
struct Event;
struct PictureAsset;

template <typename Target>
class Reference {
public:
    constexpr Reference() noexcept = default;
    explicit constexpr Reference(ObjectId id) noexcept : id_(id) {}

    [[nodiscard]] constexpr ObjectId id() const noexcept {
        return id_;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return static_cast<bool>(id_);
    }

    friend constexpr auto operator<=>(Reference, Reference) noexcept = default;

private:
    ObjectId id_{};
};

using FormRef = Reference<Form>;
using ControlRef = Reference<ControlNode>;
using PageRef = Reference<Page>;
using AttributeRef = Reference<Attribute>;
using CommandRef = Reference<Command>;
using EventRef = Reference<Event>;
using PictureAssetRef = Reference<PictureAsset>;
using ChildItemRef = std::variant<ControlRef, PageRef>;

struct UndefinedValue {
    friend bool operator==(UndefinedValue, UndefinedValue) = default;
};

struct UuidValue {
    std::string canonical = "00000000-0000-0000-0000-000000000000";

    friend bool operator==(const UuidValue&, const UuidValue&) = default;
};

struct LocalizedStringItem {
    std::string language;
    std::string text;

    friend bool operator==(const LocalizedStringItem&, const LocalizedStringItem&) = default;
};

struct LocalizedStringValue {
    std::vector<LocalizedStringItem> items;

    friend bool operator==(const LocalizedStringValue&, const LocalizedStringValue&) = default;
};

struct FormattedStringValue {
    LocalizedStringValue value;
    bool formatted = false;

    friend bool operator==(const FormattedStringValue&, const FormattedStringValue&) = default;
};

struct DecimalValue {
    std::string canonical;

    friend bool operator==(const DecimalValue&, const DecimalValue&) = default;
};

struct DateValue {
    std::string canonical;

    friend bool operator==(const DateValue&, const DateValue&) = default;
};

struct EnumerationValue {
    std::string type_name;
    std::string member;

    friend bool operator==(const EnumerationValue&, const EnumerationValue&) = default;
};

struct CompositeIdValue {
    std::int64_t object_id = 0;
    UuidValue uuid;
    bool is_null = true;

    friend bool operator==(const CompositeIdValue&, const CompositeIdValue&) = default;
};

enum class TypeDomainTerm : std::uint8_t {
    unknown,
    list,
    boolean,
    binary,
    date,
    numeric,
    reference,
    string,
    type,
};

struct NumericQualifiers {
    std::uint32_t length = 0;
    std::uint32_t precision = 0;
    bool non_negative = false;

    friend bool operator==(const NumericQualifiers&, const NumericQualifiers&) = default;
};

struct LengthQualifiers {
    std::uint32_t length = 0;
    bool variable = true;

    friend bool operator==(const LengthQualifiers&, const LengthQualifiers&) = default;
};

struct DateQualifiers {
    bool date = true;
    bool time = true;

    friend bool operator==(const DateQualifiers&, const DateQualifiers&) = default;
};

struct TypeDomainEntry {
    TypeDomainTerm term = TypeDomainTerm::unknown;
    std::optional<UuidValue> type_uuid;
    NumericQualifiers numeric;
    LengthQualifiers string;
    LengthQualifiers binary;
    DateQualifiers date;

    friend bool operator==(const TypeDomainEntry&, const TypeDomainEntry&) = default;
};

struct TypeDomainPatternValue {
    std::vector<TypeDomainEntry> entries;

    friend bool operator==(const TypeDomainPatternValue&, const TypeDomainPatternValue&) = default;
};

struct QualifiedName {
    std::string value;

    friend bool operator==(const QualifiedName&, const QualifiedName&) = default;
};

using StyleReference = std::variant<std::monostate, CompositeIdValue, QualifiedName>;

enum class ColorKind : std::uint8_t {
    absolute,
    automatic,
    style_reference,
};

struct ColorValue {
    ColorKind kind = ColorKind::automatic;
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 255;
    StyleReference style;

    friend bool operator==(const ColorValue&, const ColorValue&) = default;
};

enum class FontKind : std::uint8_t {
    absolute,
    windows_font,
    style_reference,
    automatic,
};

struct FontValue {
    FontKind kind = FontKind::automatic;
    std::uint32_t mask = 0;
    StyleReference style;
    std::string face_name;
    double height = 0.0;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikeout = false;

    friend bool operator==(const FontValue&, const FontValue&) = default;
};

struct DataPath {
    AttributeRef attribute;
    std::vector<std::string> members;

    friend bool operator==(const DataPath&, const DataPath&) = default;
};

enum class PictureFormat : std::uint8_t {
    gif,
    png,
    jpeg,
    bmp,
};

struct PictureAsset {
    ObjectId id{};
    std::string relative_path;
    PictureFormat format = PictureFormat::gif;
};

struct PictureRef {
    PictureAssetRef asset;

    friend bool operator==(const PictureRef&, const PictureRef&) = default;
};

using PropertyValue = std::variant<
    UndefinedValue,
    bool,
    std::int64_t,
    DecimalValue,
    std::string,
    LocalizedStringValue,
    FormattedStringValue,
    DateValue,
    UuidValue,
    CompositeIdValue,
    TypeDomainPatternValue,
    EnumerationValue,
    ColorValue,
    FontValue,
    PictureRef,
    ControlRef,
    AttributeRef,
    CommandRef>;

struct PropertyEntry {
    PropertyId id{};
    PropertyState state = PropertyState::explicit_value;
    PropertyValue value{false};

    friend bool operator==(const PropertyEntry&, const PropertyEntry&) = default;
};

class PropertySet {
public:
    [[nodiscard]] const PropertyEntry* find(PropertyId id) const noexcept;
    [[nodiscard]] PropertyEntry* find(PropertyId id) noexcept;
    [[nodiscard]] bool contains(PropertyId id) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

    void set_explicit(PropertyId id, PropertyValue value);
    bool unset(PropertyId id);
    void clear() noexcept;

    template <typename Visitor>
    void for_each_explicit(Visitor&& visitor) const {
        for (const auto& [id, entry] : entries_) {
            static_cast<void>(id);
            std::invoke(visitor, entry);
        }
    }

private:
    std::unordered_map<PropertyId, PropertyEntry, PropertyIdHash> entries_;
};

enum class BindingCoordinate : std::uint8_t {
    left,
    top,
    right,
    bottom,
    vertical_center,
    horizontal_center,
};

enum class BindingDimension : std::uint8_t {
    width,
    height,
    minimum_width,
    minimum_height,
    stretch,
};

struct AnchorBindingTarget {
    // An absent target denotes the current Form.
    std::optional<ControlRef> target;
    BindingCoordinate coordinate = BindingCoordinate::left;
    Property<std::int32_t> offset{0};
};

struct AnchorBinding {
    BindingCoordinate coordinate = BindingCoordinate::left;
    // An absent target denotes the current Form.
    std::optional<ControlRef> target;
    Property<std::int32_t> offset{0};
    BindingCoordinate target_coordinate = BindingCoordinate::left;
    std::optional<AnchorBindingTarget> proportional;
};

struct DimensionBinding {
    BindingDimension dimension = BindingDimension::width;
    Property<std::int32_t> value{0};
};

struct Bindings {
    std::vector<AnchorBinding> anchors;
    std::vector<DimensionBinding> dimensions;
    Property<bool> manual_horizontal{false};
    Property<bool> manual_vertical{false};
};

struct Position {
    Property<std::optional<bool>> default_control{std::nullopt};
    Property<std::int32_t> left{0};
    Property<std::int32_t> top{0};
    Property<std::int32_t> width{0};
    Property<std::int32_t> height{0};
    Property<bool> visible{true};
    Property<std::optional<std::int32_t>> tab_order{std::nullopt};
    Property<std::optional<std::int32_t>> z_order{std::nullopt};
    Property<std::optional<EnumerationValue>> collapse{std::nullopt};
    Bindings bindings;
};

enum class AttributeType : std::uint8_t {
    boolean,
    number,
    string,
    date,
    picture,
    object,
};

struct Attribute {
    ObjectId id{};
    std::string name;
    TypeDomainPatternValue type;
    Property<bool> main{false};
    Property<bool> stored_data{false};
};

struct Command {
    ObjectId id{};
    std::string name;
    std::string handler;
    Property<LocalizedStringValue> title{LocalizedStringValue{}};
    Property<bool> changes_data{false};
    Property<std::optional<PictureRef>> picture{std::nullopt};
};

using EventOwner = std::variant<FormRef, ControlRef>;

struct Event {
    ObjectId id{};
    std::string name;
    std::string handler;
    EventOwner owner{FormRef{}};
};

enum class ControlKind : std::uint8_t {
    panel,
    command_bar,
    button,
    picture_decoration,
    check_box,
    choice_field,
    radio_button,
    input_field,
    usual_group,
    splitter,
    chart,
    pivot_chart,
    gantt_chart,
    dendrogram,
    html_document_field,
    list_box,
    progress_bar,
    track_bar,
    calendar_field,
    text_document_field,
    geographical_schema_field,
    graphical_schema_field,
    table,
    spreadsheet_document_field,
    label_decoration,
    active_x_control,
    count,
};

inline constexpr std::size_t control_kind_count =
    static_cast<std::size_t>(ControlKind::count);

template <ControlKind Kind>
struct TypedControlPayload {
    static constexpr ControlKind kind = Kind;
    PropertySet properties;
};

struct PanelPayload final : TypedControlPayload<ControlKind::panel> {};
struct CommandBarPayload final : TypedControlPayload<ControlKind::command_bar> {};
struct ButtonPayload final : TypedControlPayload<ControlKind::button> {};
struct PictureDecorationPayload final
    : TypedControlPayload<ControlKind::picture_decoration> {};
struct CheckBoxPayload final : TypedControlPayload<ControlKind::check_box> {};
struct ChoiceFieldPayload final : TypedControlPayload<ControlKind::choice_field> {};
struct RadioButtonPayload final : TypedControlPayload<ControlKind::radio_button> {};
struct InputFieldPayload final : TypedControlPayload<ControlKind::input_field> {};
struct UsualGroupPayload final : TypedControlPayload<ControlKind::usual_group> {};
struct SplitterPayload final : TypedControlPayload<ControlKind::splitter> {};
struct ChartPayload final : TypedControlPayload<ControlKind::chart> {};
struct PivotChartPayload final : TypedControlPayload<ControlKind::pivot_chart> {};
struct GanttChartPayload final : TypedControlPayload<ControlKind::gantt_chart> {};
struct DendrogramPayload final : TypedControlPayload<ControlKind::dendrogram> {};
struct HtmlDocumentFieldPayload final
    : TypedControlPayload<ControlKind::html_document_field> {};
struct ListBoxPayload final : TypedControlPayload<ControlKind::list_box> {};
struct ProgressBarPayload final : TypedControlPayload<ControlKind::progress_bar> {};
struct TrackBarPayload final : TypedControlPayload<ControlKind::track_bar> {};
struct CalendarFieldPayload final : TypedControlPayload<ControlKind::calendar_field> {};
struct TextDocumentFieldPayload final
    : TypedControlPayload<ControlKind::text_document_field> {};
struct GeographicalSchemaFieldPayload final
    : TypedControlPayload<ControlKind::geographical_schema_field> {};
struct GraphicalSchemaFieldPayload final
    : TypedControlPayload<ControlKind::graphical_schema_field> {};
struct TablePayload final : TypedControlPayload<ControlKind::table> {};
struct SpreadsheetDocumentFieldPayload final
    : TypedControlPayload<ControlKind::spreadsheet_document_field> {};
struct LabelDecorationPayload final
    : TypedControlPayload<ControlKind::label_decoration> {};
struct ActiveXControlPayload final
    : TypedControlPayload<ControlKind::active_x_control> {};

using ControlPayload = std::variant<
    PanelPayload,
    CommandBarPayload,
    ButtonPayload,
    PictureDecorationPayload,
    CheckBoxPayload,
    ChoiceFieldPayload,
    RadioButtonPayload,
    InputFieldPayload,
    UsualGroupPayload,
    SplitterPayload,
    ChartPayload,
    PivotChartPayload,
    GanttChartPayload,
    DendrogramPayload,
    HtmlDocumentFieldPayload,
    ListBoxPayload,
    ProgressBarPayload,
    TrackBarPayload,
    CalendarFieldPayload,
    TextDocumentFieldPayload,
    GeographicalSchemaFieldPayload,
    GraphicalSchemaFieldPayload,
    TablePayload,
    SpreadsheetDocumentFieldPayload,
    LabelDecorationPayload,
    ActiveXControlPayload>;

static_assert(std::variant_size_v<ControlPayload> == control_kind_count);

[[nodiscard]] ControlKind payload_kind(const ControlPayload& payload) noexcept;
[[nodiscard]] PropertySet& payload_properties(ControlPayload& payload) noexcept;
[[nodiscard]] const PropertySet& payload_properties(const ControlPayload& payload) noexcept;

struct ControlNode {
    ControlNode() = default;
    ControlNode(ObjectId object_id, std::string object_name, ControlPayload control_payload);

    [[nodiscard]] ControlKind kind() const noexcept;
    [[nodiscard]] PropertySet& properties() noexcept;
    [[nodiscard]] const PropertySet& properties() const noexcept;

    ObjectId id{};
    std::string name;
    std::optional<DataPath> data_path;
    PropertySet extension_properties;
    Position position;
    std::vector<EventRef> events;
    std::vector<ChildItemRef> children;
    ControlPayload payload{PanelPayload{}};
};

struct Page {
    ObjectId id{};
    std::string name;
    Property<LocalizedStringValue> title{LocalizedStringValue{}};
    Property<bool> visible{true};
    Property<bool> enabled{true};
    Property<Position> position{Position{}};
    std::vector<ChildItemRef> children;
};

struct Form {
    ObjectId id{};
    std::string name;
    PropertySet properties;
    std::vector<EventRef> events;
    std::vector<ChildItemRef> children;
};

struct FormModule {
    std::string text;
};

struct ObjectCollections {
    std::vector<ControlNode> controls;
    std::vector<Page> pages;
    std::vector<Attribute> attributes;
    std::vector<Command> commands;
    std::vector<Event> events;
};

enum class ObjectCategory : std::uint8_t {
    form,
    control,
    page,
    attribute,
    command,
    event,
    picture_asset,
};

enum class InvariantCode : std::uint8_t {
    invalid_id,
    duplicate_id,
    dangling_reference,
    cycle,
    illegal_children,
    multiple_parents,
    orphan,
    invalid_property,
};

struct InvariantViolation {
    InvariantCode code = InvariantCode::invalid_id;
    ObjectId source{};
    ObjectId target{};
    std::string message;
};

struct ValidationReport {
    std::vector<InvariantViolation> violations;

    [[nodiscard]] bool ok() const noexcept {
        return violations.empty();
    }

    [[nodiscard]] bool has(InvariantCode code) const noexcept;
};

class InvariantError : public std::logic_error {
public:
    explicit InvariantError(ValidationReport report);

    [[nodiscard]] const ValidationReport& report() const noexcept {
        return report_;
    }

private:
    ValidationReport report_;
};

class OrdinaryFormDocument {
public:
    using ObjectView = std::variant<
        std::reference_wrapper<const Form>,
        std::reference_wrapper<const ControlNode>,
        std::reference_wrapper<const Page>,
        std::reference_wrapper<const Attribute>,
        std::reference_wrapper<const Command>,
        std::reference_wrapper<const Event>,
        std::reference_wrapper<const PictureAsset>>;

    OrdinaryFormDocument();
    explicit OrdinaryFormDocument(Form form);

    [[nodiscard]] const Form& form() const noexcept {
        return form_;
    }

    [[nodiscard]] const FormModule& module() const noexcept {
        return module_;
    }

    [[nodiscard]] const std::vector<PictureAsset>& assets() const noexcept {
        return assets_;
    }

    [[nodiscard]] const ObjectCollections& collections() const noexcept {
        return collections_;
    }

    void set_form(Form form);
    void set_module(FormModule module);
    void add_asset(PictureAsset asset);
    void add_control(ControlNode control);
    void add_page(Page page);
    void add_attribute(Attribute attribute);
    void add_command(Command command);
    void add_event(Event event);

    [[nodiscard]] std::optional<ObjectView> find(ObjectId id) const;
    [[nodiscard]] const ControlNode* find_control(ObjectId id) const noexcept;
    [[nodiscard]] const Page* find_page(ObjectId id) const noexcept;
    [[nodiscard]] const Attribute* find_attribute(ObjectId id) const noexcept;
    [[nodiscard]] const Command* find_command(ObjectId id) const noexcept;
    [[nodiscard]] const Event* find_event(ObjectId id) const noexcept;
    [[nodiscard]] const PictureAsset* find_asset(ObjectId id) const noexcept;
    [[nodiscard]] std::size_t indexed_id_count() const noexcept;

    [[nodiscard]] ValidationReport validate() const;
    void validate_or_throw() const;

private:
    struct ObjectKey {
        ObjectCategory category = ObjectCategory::form;
        ObjectId id{};

        friend bool operator==(const ObjectKey&, const ObjectKey&) = default;
    };

    struct ObjectKeyHash {
        [[nodiscard]] std::size_t operator()(const ObjectKey& key) const noexcept {
            const std::size_t category_hash = std::hash<std::uint8_t>{}(
                static_cast<std::uint8_t>(key.category));
            const std::size_t id_hash = ObjectIdHash{}(key.id);
            return category_hash ^ (id_hash + 0x9e3779b9U + (category_hash << 6U) + (category_hash >> 2U));
        }
    };

    struct ObjectLocation {
        ObjectCategory category = ObjectCategory::form;
        std::size_t index = 0;
    };

    void rebuild_index();
    void index_first(ObjectId id, ObjectCategory category, std::size_t index);
    [[nodiscard]] std::optional<ObjectLocation> find_location(
        ObjectCategory category,
        ObjectId id) const noexcept;

    Form form_{};
    FormModule module_{};
    std::vector<PictureAsset> assets_;
    ObjectCollections collections_;
    std::unordered_map<ObjectKey, ObjectLocation, ObjectKeyHash> index_;
};

}  // namespace oof::model
