#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <unordered_map>

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
using AttributeRef = Reference<Attribute>;
using CommandRef = Reference<Command>;
using EventRef = Reference<Event>;
using PictureAssetRef = Reference<PictureAsset>;

struct LocalizedText {
    std::string locale = "ru";
    std::string text;

    friend bool operator==(const LocalizedText&, const LocalizedText&) = default;
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

struct AnchorBinding {
    BindingCoordinate coordinate = BindingCoordinate::left;
    std::optional<ControlRef> target;
    Property<std::int32_t> offset{0};
};

struct DimensionBinding {
    BindingDimension dimension = BindingDimension::width;
    Property<std::int32_t> value{0};
};

struct Bindings {
    std::vector<AnchorBinding> anchors;
    std::vector<DimensionBinding> dimensions;
};

struct Position {
    Property<std::int32_t> left{0};
    Property<std::int32_t> top{0};
    Property<std::int32_t> right{0};
    Property<std::int32_t> bottom{0};
    Bindings bindings;
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
    AttributeType type = AttributeType::string;
    Property<bool> main{false};
};

struct Command {
    ObjectId id{};
    std::string name;
    Property<LocalizedText> title{LocalizedText{}};
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

enum class Orientation : std::uint8_t {
    horizontal,
    vertical,
};

enum class PictureScaleMode : std::uint8_t {
    actual_size,
    fit,
    fill,
};

enum class ChartPresentation : std::uint8_t {
    cartesian,
    pie,
    gauge,
};

struct PanelPayload {
    static constexpr ControlKind kind = ControlKind::panel;
    Property<Orientation> orientation{Orientation::horizontal};
    Property<bool> show_tabs{false};
};

struct CommandBarPayload {
    static constexpr ControlKind kind = ControlKind::command_bar;
    Property<bool> auto_fill{true};
    Property<bool> auxiliary{false};
};

struct ButtonPayload {
    static constexpr ControlKind kind = ControlKind::button;
    Property<std::optional<CommandRef>> command{std::nullopt};
    Property<std::optional<PictureRef>> picture{std::nullopt};
    Property<bool> default_button{false};
};

struct PictureDecorationPayload {
    static constexpr ControlKind kind = ControlKind::picture_decoration;
    Property<std::optional<PictureRef>> picture{std::nullopt};
    Property<PictureScaleMode> scale_mode{PictureScaleMode::actual_size};
};

struct CheckBoxPayload {
    static constexpr ControlKind kind = ControlKind::check_box;
    Property<std::optional<AttributeRef>> data_attribute{std::nullopt};
    Property<bool> three_state{false};
};

struct ChoiceFieldPayload {
    static constexpr ControlKind kind = ControlKind::choice_field;
    Property<std::optional<AttributeRef>> data_attribute{std::nullopt};
    Property<std::int32_t> list_height{0};
    Property<bool> read_only{false};
};

struct RadioButtonPayload {
    static constexpr ControlKind kind = ControlKind::radio_button;
    Property<std::optional<AttributeRef>> data_attribute{std::nullopt};
    Property<std::int32_t> columns{1};
};

struct InputFieldPayload {
    static constexpr ControlKind kind = ControlKind::input_field;
    Property<std::optional<AttributeRef>> data_attribute{std::nullopt};
    Property<bool> read_only{false};
    Property<bool> multiline{false};
    Property<bool> password_mode{false};
};

struct UsualGroupPayload {
    static constexpr ControlKind kind = ControlKind::usual_group;
    Property<bool> show_border{true};
};

struct SplitterPayload {
    static constexpr ControlKind kind = ControlKind::splitter;
    Property<Orientation> orientation{Orientation::vertical};
    Property<std::int32_t> thickness{1};
};

struct ChartPayload {
    static constexpr ControlKind kind = ControlKind::chart;
    Property<std::optional<AttributeRef>> data_attribute{std::nullopt};
    Property<ChartPresentation> presentation{ChartPresentation::cartesian};
};

struct PivotChartPayload {
    static constexpr ControlKind kind = ControlKind::pivot_chart;
    Property<std::optional<AttributeRef>> data_attribute{std::nullopt};
    Property<bool> show_totals{true};
};

struct GanttChartPayload {
    static constexpr ControlKind kind = ControlKind::gantt_chart;
    Property<std::optional<AttributeRef>> data_attribute{std::nullopt};
    Property<bool> show_time_scale{true};
};

struct DendrogramPayload {
    static constexpr ControlKind kind = ControlKind::dendrogram;
    Property<std::optional<AttributeRef>> data_attribute{std::nullopt};
    Property<Orientation> orientation{Orientation::horizontal};
};

struct HtmlDocumentFieldPayload {
    static constexpr ControlKind kind = ControlKind::html_document_field;
    Property<std::optional<AttributeRef>> data_attribute{std::nullopt};
    Property<bool> allow_scripts{false};
};

struct ListBoxPayload {
    static constexpr ControlKind kind = ControlKind::list_box;
    Property<std::optional<AttributeRef>> data_attribute{std::nullopt};
    Property<bool> multiple_selection{false};
};

struct ProgressBarPayload {
    static constexpr ControlKind kind = ControlKind::progress_bar;
    Property<std::optional<AttributeRef>> data_attribute{std::nullopt};
    Property<double> minimum{0.0};
    Property<double> maximum{100.0};
};

struct TrackBarPayload {
    static constexpr ControlKind kind = ControlKind::track_bar;
    Property<std::optional<AttributeRef>> data_attribute{std::nullopt};
    Property<double> minimum{0.0};
    Property<double> maximum{100.0};
    Property<double> step{1.0};
};

struct CalendarFieldPayload {
    static constexpr ControlKind kind = ControlKind::calendar_field;
    Property<std::optional<AttributeRef>> data_attribute{std::nullopt};
    Property<bool> show_current_date{true};
};

struct TextDocumentFieldPayload {
    static constexpr ControlKind kind = ControlKind::text_document_field;
    Property<std::optional<AttributeRef>> data_attribute{std::nullopt};
    Property<bool> read_only{false};
};

struct GeographicalSchemaFieldPayload {
    static constexpr ControlKind kind = ControlKind::geographical_schema_field;
    Property<std::optional<AttributeRef>> data_attribute{std::nullopt};
    Property<bool> show_legend{true};
};

struct GraphicalSchemaFieldPayload {
    static constexpr ControlKind kind = ControlKind::graphical_schema_field;
    Property<std::optional<AttributeRef>> data_attribute{std::nullopt};
    Property<bool> read_only{false};
};

struct TablePayload {
    static constexpr ControlKind kind = ControlKind::table;
    Property<std::optional<AttributeRef>> data_attribute{std::nullopt};
    Property<bool> allow_row_changes{true};
};

struct SpreadsheetDocumentFieldPayload {
    static constexpr ControlKind kind = ControlKind::spreadsheet_document_field;
    Property<std::optional<AttributeRef>> data_attribute{std::nullopt};
    Property<bool> read_only{false};
};

struct LabelDecorationPayload {
    static constexpr ControlKind kind = ControlKind::label_decoration;
    Property<std::optional<PictureRef>> picture{std::nullopt};
    Property<bool> hyperlink{false};
};

struct ActiveXControlPayload {
    static constexpr ControlKind kind = ControlKind::active_x_control;
    Property<std::string> class_id{std::string{}};
};

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

struct ControlNode {
    ControlNode() = default;
    ControlNode(ObjectId object_id, std::string object_name, ControlPayload control_payload);

    [[nodiscard]] ControlKind kind() const noexcept;

    ObjectId id{};
    std::string name;
    Property<LocalizedText> title{LocalizedText{}};
    Property<bool> visible{true};
    Property<bool> enabled{true};
    Position position;
    std::vector<EventRef> events;
    std::vector<ControlRef> children;
    ControlPayload payload{PanelPayload{}};
};

struct Form {
    ObjectId id{};
    std::string name;
    Property<LocalizedText> title{LocalizedText{}};
    Property<std::int32_t> width{0};
    Property<std::int32_t> height{0};
    std::vector<EventRef> events;
    std::vector<ControlRef> children;
};

struct FormModule {
    std::string text;
};

struct ObjectCollections {
    std::vector<ControlNode> controls;
    std::vector<Attribute> attributes;
    std::vector<Command> commands;
    std::vector<Event> events;
};

enum class ObjectCategory : std::uint8_t {
    form,
    control,
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
    void add_attribute(Attribute attribute);
    void add_command(Command command);
    void add_event(Event event);

    [[nodiscard]] std::optional<ObjectView> find(ObjectId id) const;
    [[nodiscard]] const ControlNode* find_control(ObjectId id) const noexcept;
    [[nodiscard]] const Attribute* find_attribute(ObjectId id) const noexcept;
    [[nodiscard]] const Command* find_command(ObjectId id) const noexcept;
    [[nodiscard]] const Event* find_event(ObjectId id) const noexcept;
    [[nodiscard]] const PictureAsset* find_asset(ObjectId id) const noexcept;
    [[nodiscard]] std::size_t indexed_id_count() const noexcept;

    [[nodiscard]] ValidationReport validate() const;
    void validate_or_throw() const;

private:
    struct ObjectLocation {
        ObjectCategory category = ObjectCategory::form;
        std::size_t index = 0;
    };

    void rebuild_index();
    void index_first(ObjectId id, ObjectCategory category, std::size_t index);

    Form form_{};
    FormModule module_{};
    std::vector<PictureAsset> assets_;
    ObjectCollections collections_;
    std::unordered_map<ObjectId, ObjectLocation, ObjectIdHash> index_;
};

}  // namespace oof::model
