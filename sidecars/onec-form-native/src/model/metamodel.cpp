#include "oof/model/metamodel.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace oof::model::metamodel {
namespace {

struct ControlIdentity {
    ControlKind kind;
    std::string_view guid;
    std::string_view storage_tag;
    VersionMask version_mask;
    ClassificationStatus classification;
    ChildPolicy child_policy;
};

struct HelpControlName {
    ControlKind kind;
    std::string_view public_name;
    std::string_view api_name;
    std::u8string_view russian_name;
};

constexpr auto all_versions = VersionMask::all_supported;

constexpr std::array<ShortcutKeyDescriptor, 80> shortcut_keys{{
    {"None", 0}, {"BackSpace", 8}, {"Space", 32}, {"PageUp", 33}, {"PageDown", 34},
    {"Home", 36}, {"End", 35}, {"Left", 37}, {"Up", 38}, {"Right", 39}, {"Down", 40},
    {"Ins", 45}, {"Del", 46}, {"Tab", 9}, {"Enter", 13}, {"Esc", 27},
    {"_0", 48}, {"_1", 49}, {"_2", 50}, {"_3", 51}, {"_4", 52}, {"_5", 53},
    {"_6", 54}, {"_7", 55}, {"_8", 56}, {"_9", 57},
    {"A", 65}, {"B", 66}, {"C", 67}, {"D", 68}, {"E", 69}, {"F", 70}, {"G", 71},
    {"H", 72}, {"I", 73}, {"J", 74}, {"K", 75}, {"L", 76}, {"M", 77}, {"N", 78},
    {"O", 79}, {"P", 80}, {"Q", 81}, {"R", 82}, {"S", 83}, {"T", 84}, {"U", 85},
    {"V", 86}, {"W", 87}, {"X", 88}, {"Y", 89}, {"Z", 90},
    {"Num0", 96}, {"Num1", 97}, {"Num2", 98}, {"Num3", 99}, {"Num4", 100},
    {"Num5", 101}, {"Num6", 102}, {"Num7", 103}, {"Num8", 104}, {"Num9", 105},
    {"NumMultiply", 106}, {"NumAdd", 107}, {"NumSubtract", 109}, {"NumDecimal", 110},
    {"NumDivide", 111}, {"F1", 112}, {"F2", 113}, {"F3", 114}, {"F4", 115},
    {"F5", 116}, {"F6", 117}, {"F7", 118}, {"F8", 119}, {"F9", 120}, {"F10", 121},
    {"F11", 122}, {"F12", 123}, {"Break", 3},
}};

ValueCodec initial_value_codec(ValueKind kind, std::u8string_view platform_type) noexcept {
    switch (kind) {
        case ValueKind::boolean:
            return ValueCodec::boolean;
        case ValueKind::number:
            return ValueCodec::decimal;
        case ValueKind::string:
            return ValueCodec::string;
        case ValueKind::date_time:
            return ValueCodec::date;
        case ValueKind::picture:
            return ValueCodec::picture;
        case ValueKind::color:
            return ValueCodec::color;
        case ValueKind::font:
            return ValueCodec::font;
        case ValueKind::shortcut:
            return ValueCodec::shortcut;
        case ValueKind::identifier:
            return ValueCodec::uuid;
        case ValueKind::enumeration:
            return ValueCodec::enumeration;
        case ValueKind::object:
            return platform_type == u8"ОписаниеТипов" ? ValueCodec::type_domain
                                                       : ValueCodec::unclassified;
        case ValueKind::unknown:
        case ValueKind::border:
        case ValueKind::binary:
        case ValueKind::collection:
        case ValueKind::variant:
            return ValueCodec::unclassified;
    }
    return ValueCodec::unclassified;
}

ValueCodec panel_placement_value_codec(
    ValueKind kind,
    std::u8string_view platform_type
) noexcept {
    return kind == ValueKind::number ? ValueCodec::integer32
                                     : initial_value_codec(kind, platform_type);
}

constexpr std::array<ControlIdentity, control_kind_count> control_identities{{
    {ControlKind::panel, "09ccdc77-ea1a-4a6d-ab1c-3435eada2433", "pnl", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::ordered_controls_and_pages},
    {ControlKind::command_bar, "e69bf21d-97b2-4f37-86db-675aea9ec2cb", "cmdb", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::forbidden},
    {ControlKind::button, "6ff79819-710e-4145-97cd-1618da79e3e2", "btn", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::forbidden},
    {ControlKind::picture_decoration, "151ef23e-6bb2-4681-83d0-35bc2217230c", "img", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::forbidden},
    {ControlKind::check_box, "35af3d93-d7c7-4a2e-a8eb-bac87a1a3f26", "chk", all_versions, ClassificationStatus::corpus_xsd_resource_correlated, ChildPolicy::forbidden},
    {ControlKind::choice_field, "64483e7f-3833-48e2-8c75-2c31aac49f6e", "txt", all_versions, ClassificationStatus::corpus_xsd_resource_correlated, ChildPolicy::forbidden},
    {ControlKind::radio_button, "782e569a-79a7-4a4f-a936-b48d013936ec", "rbtn", all_versions, ClassificationStatus::corpus_xsd_resource_correlated, ChildPolicy::forbidden},
    {ControlKind::input_field, "381ed624-9217-4e63-85db-c4c3cb87daae", "txt", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::forbidden},
    {ControlKind::usual_group, "90db814a-c75f-4b54-bc96-df62e554d67d", "grpb", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::ordered_controls},
    {ControlKind::splitter, "36e52348-5d60-4770-8e89-a16ed50a2006", "sep", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::chart, "a8b97779-1a4b-4059-b09c-807f86d2a461", "chrt", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::pivot_chart, "a26da99e-184a-4823-b0d6-62816d38dc4e", "", all_versions, ClassificationStatus::platform_ui_guid_table_backed, ChildPolicy::forbidden},
    {ControlKind::gantt_chart, "e5fdc112-5c84-4a16-9728-72b85692b6e2", "gchrt", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::dendrogram, "984981b1-622d-4ebc-94f7-885f0cdfb59a", "dndrgm", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::html_document_field, "d92a805c-98ae-4750-9158-d9ce7cec2f20", "html", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::forbidden},
    {ControlKind::list_box, "19f8b798-314e-4b4e-8121-905b2a7a03f5", "txt", all_versions, ClassificationStatus::corpus_xsd_resource_correlated, ChildPolicy::forbidden},
    {ControlKind::progress_bar, "b1db1f86-abbb-4cf0-8852-fe6ae21650c2", "prgb", all_versions, ClassificationStatus::corpus_xsd_resource_correlated, ChildPolicy::forbidden},
    {ControlKind::track_bar, "6c06cd5d-8481-4b6f-a90a-7a97a8bb8bef", "trckb", all_versions, ClassificationStatus::corpus_xsd_resource_correlated, ChildPolicy::forbidden},
    {ControlKind::calendar_field, "e3c063d8-ef92-41be-9c89-b70290b5368b", "clndr", all_versions, ClassificationStatus::corpus_xsd_resource_correlated, ChildPolicy::forbidden},
    {ControlKind::text_document_field, "14c4a229-bfc3-42fe-9ce1-2da049fd0109", "txtd", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::geographical_schema_field, "ad37194e-555e-4305-b718-5dca84baf145", "gm", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::graphical_schema_field, "42248403-7748-49da-b782-e4438fd7bff3", "flwchrt", all_versions, ClassificationStatus::corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::table, "ea83fe3a-ac3c-4cce-8045-3dddf35b28b1", "tbl", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::forbidden},
    {ControlKind::spreadsheet_document_field, "236a17b3-7f44-46d9-a907-75f9cdc61ab5", "sprdsht", all_versions, ClassificationStatus::binary_guid_corpus_xsd_correlated, ChildPolicy::forbidden},
    {ControlKind::label_decoration, "0fc7e20d-f241-460c-bdf4-5ad88e5474a5", "lbl", all_versions, ClassificationStatus::platform_resource_backed, ChildPolicy::forbidden},
    {ControlKind::active_x_control, "621e95f1-064f-11d4-9400-008048da11f9", "", all_versions, ClassificationStatus::windows_harness_required, ChildPolicy::forbidden},
}};

constexpr std::array<StandardPictureDescriptor, 294> standard_picture_catalog{{
    {"PictureLib.AccountingRegister", u8"РегистрБухгалтерии", "20175a6d-7510-46a8-9077-75a5d0d97802", 0},
    {"PictureLib.AccumulationRegister", u8"РегистрНакопления", "70f51581-87b6-41cb-a21b-c9dcdcc7fa93", 0},
    {"PictureLib.ActivateTask", u8"АктивироватьЗадачу", "093dd4ed-e03c-4fc6-a95a-01f51379cccf", 0},
    {"PictureLib.ActiveUsers", u8"АктивныеПользователи", "47f01799-7968-4f44-9acc-fe1bdde8beb2", 0},
    {"PictureLib.AddToFavorites", u8"ДобавитьВИзбранное", "1001ae3e-9289-4303-9699-3c0c17e20e61", 0},
    {"PictureLib.AppearanceBoxesEmpty", u8"ОформлениеКвадратыПустые", "879ca050-8650-4135-b799-f3ef9e00a65a", 0},
    {"PictureLib.AppearanceBoxesFilled", u8"ОформлениеКвадратыЗаполненные", "ba592483-bc90-4e26-ba4d-2126359c6529", 0},
    {"PictureLib.AppearanceBoxesOneFilled", u8"ОформлениеКвадратыЗаполненныеОдин", "7026a8e6-f973-4fab-88f3-1047734d787f", 0},
    {"PictureLib.AppearanceBoxesThreeFilled", u8"ОформлениеКвадратыЗаполненныеТри", "8c18ce73-0840-4f64-b76c-89d2356b1a41", 0},
    {"PictureLib.AppearanceBoxesTwoFilled", u8"ОформлениеКвадратыЗаполненныеДва", "f164fa2a-0a20-48df-9816-e2d6cbe0aee9", 0},
    {"PictureLib.AppearanceCheckBox", u8"ОформлениеФлажок", "7a9cd2fd-6372-4342-9a9e-3ebbd754fd83", 0},
    {"PictureLib.AppearanceCheckIcon", u8"ОформлениеЗнакФлажок", "85998f14-805b-4e2b-ba19-9d79b0464042", 0},
    {"PictureLib.AppearanceCircleBlack", u8"ОформлениеКругЧерный", "cf10e497-9779-44a7-82d8-811b88193215", 0},
    {"PictureLib.AppearanceCircleEmpty", u8"ОформлениеКругПустой", "2721abfb-fbff-4a3a-98ac-b7c9eb29cd85", 0},
    {"PictureLib.AppearanceCircleFilled", u8"ОформлениеКругЗаполненный", "788667db-61c9-45f3-9c4f-5f660ecdf3e1", 0},
    {"PictureLib.AppearanceCircleGreen", u8"ОформлениеКругЗеленый", "71cbcb5c-f3f0-4ffd-a4d0-19b802b5ed6b", 0},
    {"PictureLib.AppearanceCircleOneFourthFilled", u8"ОформлениеКругЗаполненныйНаОднуЧетверть", "fc058833-e57f-4f93-ba7a-803992a65c3e", 0},
    {"PictureLib.AppearanceCircleRed", u8"ОформлениеКругКрасный", "c1a61df2-f280-49d0-a8b3-7e5fc6f56ff7", 0},
    {"PictureLib.AppearanceCircleThreeFourthFilled", u8"ОформлениеКругЗаполненныйНаТриЧетверти", "08a03482-420e-4faa-af9d-e0138c53bfff", 0},
    {"PictureLib.AppearanceCircleTwoFourthFilled", u8"ОформлениеКругЗаполненныйНаДвеЧетверти", "dc9b46a9-905b-4baa-9e53-4d824e291a5b", 0},
    {"PictureLib.AppearanceCircleYellow", u8"ОформлениеКругЖелтый", "8d7e5026-9c1c-4542-bec0-2b729c84e139", 0},
    {"PictureLib.AppearanceCross", u8"ОформлениеКрест", "b2202798-23e0-4165-9982-24878f432488", 0},
    {"PictureLib.AppearanceCrossIcon", u8"ОформлениеЗнакКрест", "9ef73565-2250-4a35-9fb3-470bd19ca9ca", 0},
    {"PictureLib.AppearanceDashYellow", u8"ОформлениеДефисЖелтый", "e0ad5543-df7f-423c-8f59-8caf200cca0a", 0},
    {"PictureLib.AppearanceDownArrowGray", u8"ОформлениеСтрелкаВнизСерая", "e3b29b1d-4694-4f56-8d55-922f83afed7a", 0},
    {"PictureLib.AppearanceDownArrowRed", u8"ОформлениеСтрелкаВнизКрасная", "0880808c-5146-42b2-87a3-e5a9693ad256", 0},
    {"PictureLib.AppearanceDownInclineArrowGray", u8"ОформлениеСтрелкаНаклоннаяВнизСерая", "a30ab2ef-6076-457d-9293-44edc7c6767e", 0},
    {"PictureLib.AppearanceDownInclineArrowRed", u8"ОформлениеСтрелкаНаклоннаяВнизКрасная", "80a31eac-8df6-49c0-ba84-b53c4631d09f", 0},
    {"PictureLib.AppearanceDownInclineArrowYellow", u8"ОформлениеСтрелкаНаклоннаяВнизЖелтая", "ce1c3d6b-7825-4cac-a58c-58bac18cefc9", 0},
    {"PictureLib.AppearanceDownTriangleRed", u8"ОформлениеТреугольникВнизКрасный", "7a2a6f5c-7677-45e1-a2d3-ce2444d99a63", 0},
    {"PictureLib.AppearanceExclamationMark", u8"ОформлениеВосклицательныйЗнак", "501b8c1d-8062-408e-bde8-b6549324713e", 0},
    {"PictureLib.AppearanceExclamationMarkIcon", u8"ОформлениеЗнакВосклицательныйЗнак", "b39aa431-a32f-4447-984a-45606474c82d", 0},
    {"PictureLib.AppearanceFlagGreen", u8"ОформлениеФлагЗеленый", "bbd37b6b-2742-48f3-9efa-d7c245f125b0", 0},
    {"PictureLib.AppearanceFlagRed", u8"ОформлениеФлагКрасный", "b0dd988f-2d9f-4364-b1f4-a4d5f45ffb78", 0},
    {"PictureLib.AppearanceFlagYellow", u8"ОформлениеФлагЖелтый", "16e4a14a-f38b-453d-908a-41fb9479c3f6", 0},
    {"PictureLib.AppearanceRightArrowGray", u8"ОформлениеСтрелкаВправоСерая", "20b82e97-5fcc-4c68-8e0d-d01060847520", 0},
    {"PictureLib.AppearanceRightArrowYellow", u8"ОформлениеСтрелкаВправоЖелтая", "697e00c5-4a1d-4367-90e8-ddb734069ad5", 0},
    {"PictureLib.AppearanceStarEmpty", u8"ОформлениеЗвездаПустая", "f8d11b3a-a72b-4ee3-ad44-d401472e4b7c", 0},
    {"PictureLib.AppearanceStarFilled", u8"ОформлениеЗвездаЗаполненная", "3689585c-a3e2-45d0-a302-caeb31b78835", 0},
    {"PictureLib.AppearanceStarHalfFilled", u8"ОформлениеЗвездаЗаполненнаяНаполовину", "26abd244-8dd5-417e-aed0-247a573394ca", 0},
    {"PictureLib.AppearanceUpArrowGray", u8"ОформлениеСтрелкаВверхСерая", "a5d108f7-17d6-42dc-a789-45d71fb50ecd", 0},
    {"PictureLib.AppearanceUpArrowGreen", u8"ОформлениеСтрелкаВверхЗеленая", "87d032df-0956-47e9-bead-4e15330f1983", 0},
    {"PictureLib.AppearanceUpInclineArrowGray", u8"ОформлениеСтрелкаНаклоннаяВверхСерая", "92e24ce1-3917-4ee4-bbde-adce48b6c96b", 0},
    {"PictureLib.AppearanceUpInclineArrowGreen", u8"ОформлениеСтрелкаНаклоннаяВверхЗеленая", "0a688f53-2f13-4833-b340-c604cb1d3eb1", 0},
    {"PictureLib.AppearanceUpInclineArrowYellow", u8"ОформлениеСтрелкаНаклоннаяВверхЖелтая", "09547c21-3f15-4a6d-ba87-e5f289dc1a6d", 0},
    {"PictureLib.AppearanceUpTriangleGreen", u8"ОформлениеТреугольникВверхЗеленый", "8da615a0-65a5-4511-a8d4-00dc156478fb", 0},
    {"PictureLib.Attach", u8"Прикрепить", "f6e88116-03d8-4400-9d88-791895d7031a", 0},
    {"PictureLib.Attribute", u8"Реквизит", "0c1f7756-6143-4903-a94c-8f22c85e44de", 0},
    {"PictureLib.Back", u8"Назад", "3c904ff7-1195-4a7c-9a38-7b1f6ca49cce", 0},
    {"PictureLib.BusinessProcess", u8"БизнесПроцесс", "509c4a7f-6406-4388-bb8c-bc81fb5131aa", 0},
    {"PictureLib.BusinessProcessObject", u8"БизнесПроцессОбъект", "a24cff7f-a1a5-4403-af82-a7b31852cde9", 0},
    {"PictureLib.BusinessProcessStart", u8"СтартБизнесПроцесса", "9fecbaff-2a05-4da6-9ef1-807e754b928d", 0},
    {"PictureLib.CalculationRegister", u8"РегистрРасчета", "f3b8f300-5a54-4eea-8136-5798413a479c", 0},
    {"PictureLib.CalculationType", u8"ВидРасчета", "80837aab-ad67-4811-9d83-88344439c721", 0},
    {"PictureLib.Calculator", u8"Калькулятор", "fada8a16-8b14-4151-87a9-775099f37832", 0},
    {"PictureLib.Calendar", u8"Календарь", "97c5a6d5-47ed-43f9-8c8c-10e9903c23d2", 0},
    {"PictureLib.CancelSearch", u8"ОтменитьПоиск", "9e808d29-787b-4825-863a-13c6844ce91d", 0},
    {"PictureLib.Catalog", u8"Справочник", "069b8324-7c51-4c73-a6a8-c06d4fc383b5", 0},
    {"PictureLib.CatalogObject", u8"СправочникОбъект", "d971ec77-85e4-4e8d-a20a-8b130eedeb29", 0},
    {"PictureLib.Change", u8"Изменить", "97b2cc97-d5c6-45fb-9824-9d6d73db21fe", 0},
    {"PictureLib.Char", u8"Символ", "4781692b-60eb-47e4-acfb-2caf0f49e977", 0},
    {"PictureLib.Chart", u8"Диаграмма", "f3c1376a-d2ee-46c4-9e44-aa2f7dae31c4", 0},
    {"PictureLib.ChartOfAccounts", u8"ПланСчетов", "ccb3d8f7-6da2-4c65-aba6-17b2ffbba78c", 0},
    {"PictureLib.ChartOfAccountsObject", u8"ПланСчетовОбъект", "6d4277ee-b29b-4790-8c3c-c4f868311c96", 0},
    {"PictureLib.ChartOfCalculationTypes", u8"ПланВидовРасчета", "b58ea40e-819b-4d07-bd9f-5fbf42e01f55", 0},
    {"PictureLib.ChartOfCalculationTypesObject", u8"ПланВидовРасчетаОбъект", "64400154-ca4c-4a05-8028-83434fda5bef", 0},
    {"PictureLib.ChartOfCharacteristicTypes", u8"ПланВидовХарактеристик", "2bfafd1e-6d07-4609-8010-6c1c605cefd2", 0},
    {"PictureLib.ChartOfCharacteristicTypesObject", u8"ПланВидовХарактеристикОбъект", "45921467-71f2-4176-97e8-7df1d31d9e87", 0},
    {"PictureLib.CheckAll", u8"УстановитьФлажки", "", -10},
    {"PictureLib.CheckSyntax", u8"СинтаксическийКонтроль", "dcd23a32-5c7c-43f2-9021-80d98128556f", 0},
    {"PictureLib.ChooseTopLevel", u8"ВыбратьВерхнийУровень", "38a5a658-ec2b-4ddf-8088-c6d2169d3181", 0},
    {"PictureLib.ChooseType", u8"ВыбратьТип", "", -1400},
    {"PictureLib.ChooseValue", u8"ВыбратьЗначение", "2f130057-bb2a-4e22-bba5-e108fac26940", 0},
    {"PictureLib.Clear", u8"Очистить", "", -200},
    {"PictureLib.ClearFilter", u8"ОтключитьОтбор", "479470e0-ea0f-4266-8549-e2b1e8c06534", 0},
    {"PictureLib.CloneListItem", u8"СкопироватьЭлементСписка", "448d6f55-d885-496c-870d-d1bd78374745", 0},
    {"PictureLib.CloneObject", u8"СкопироватьОбъект", "f6532868-30b9-44ab-803c-78f0f0b06b02", 0},
    {"PictureLib.Close", u8"Закрыть", "1377931c-5744-4948-bade-cb35117b5f63", 0},
    {"PictureLib.CollaborationSystemExternalUser", u8"ВнешнийПользовательСистемыВзаимодействия", "66c18df5-d7a6-465e-92b5-bcc6c1032504", 0},
    {"PictureLib.CollaborationSystemIntegrationUser", u8"ПользовательИнтеграцииСистемыВзаимодействия", "05b8ccd1-f644-4e2e-87de-9683410c7038", 0},
    {"PictureLib.CollaborationSystemUser", u8"ПользовательСистемыВзаимодействия", "a722bc14-4edb-4eed-84b9-5d9b2b443e04", 0},
    {"PictureLib.CollapseAll", u8"СвернутьВсе", "27ee3053-952c-49e5-8261-9215098e0e9c", 0},
    {"PictureLib.Constant", u8"Константа", "e93f538e-dfaf-4a91-a9b6-c053555bcf60", 0},
    {"PictureLib.Conversations", u8"Обсуждения", "6a248caf-0a7b-46ad-a595-74890ea202f7", 0},
    {"PictureLib.CreateFolder", u8"СоздатьГруппу", "4ab0e87f-7d9b-4aa8-ac4b-680a78522da8", 0},
    {"PictureLib.CreateInitialImage", u8"СоздатьНачальныйОбраз", "4d2570b5-205f-413c-b4cc-b2097f61684f", 0},
    {"PictureLib.CreateListItem", u8"СоздатьЭлементСписка", "977e831a-0e73-4d60-af51-091a6fa8612e", 0},
    {"PictureLib.Credit", u8"Кредит", "55bc1099-a7df-4d0a-b332-a45f0473b368", 0},
    {"PictureLib.CustomExpression", u8"ПроизвольноеВыражение", "f695666a-bad9-49f6-ab7c-5198d7ea4739", 0},
    {"PictureLib.CustomizeForm", u8"ИзменитьФорму", "6511326b-20c3-4bf8-8503-c2c2c9072c6c", 0},
    {"PictureLib.CustomizeList", u8"НастроитьСписок", "f04794cb-c198-4172-86c3-649386013c85", 0},
    {"PictureLib.DataCompositionConditionalAppearance", u8"УсловноеОформлениеКомпоновкиДанных", "984b0a3e-daa2-4a9e-b75c-1f230a6e592a", 0},
    {"PictureLib.DataCompositionConditionalAppearanceDisabled", u8"УсловноеОформлениеКомпоновкиДанныхНедоступное", "5ff5bf50-1f3c-46b6-b4ef-a4973dd610aa", 0},
    {"PictureLib.DataCompositionDataParameters", u8"ПараметрыДанныхКомпоновкиДанных", "a075c3ef-bc4e-4c96-bdad-2245ec09c28e", 0},
    {"PictureLib.DataCompositionFilter", u8"ОтборКомпоновкиДанных", "d90a7482-9a1d-4d3d-ae96-6db440214d96", 0},
    {"PictureLib.DataCompositionFilterDisabled", u8"ОтборКомпоновкиДанныхНедоступный", "d35bd799-1cc3-44d1-8ae3-09755a09d44b", 0},
    {"PictureLib.DataCompositionGroupFields", u8"ПоляГруппировкиКомпоновкиДанных", "ad8cb448-a6bb-43b4-886a-7d6a8367eef2", 0},
    {"PictureLib.DataCompositionGroupFieldsDisabled", u8"ПоляГруппировкиКомпоновкиДанныхНедоступные", "afbaed82-4b8e-4970-8ac6-97a3bff796d3", 0},
    {"PictureLib.DataCompositionNewChart", u8"НоваяДиаграммаКомпоновкиДанных", "732f9dd3-5baf-47ff-af7b-edfe16dac2a1", 0},
    {"PictureLib.DataCompositionNewGroup", u8"НоваяГруппировкаКомпоновкиДанных", "77180b5e-8faa-4712-a788-e9f8903e3419", 0},
    {"PictureLib.DataCompositionNewNestedScheme", u8"НоваяВложеннаяСхемаКомпоновкиДанных", "8ac19694-383a-457a-b050-0a3ee937f5f3", 0},
    {"PictureLib.DataCompositionNewTable", u8"НоваяТаблицаКомпоновкиДанных", "6c2759b1-2b63-4fa5-8d13-75786c7e1e89", 0},
    {"PictureLib.DataCompositionOrder", u8"ПорядокКомпоновкиДанных", "efda7350-6cd7-4416-b188-f5ca9baf66c2", 0},
    {"PictureLib.DataCompositionOrderDisabled", u8"ПорядокКомпоновкиДанныхНедоступный", "c8923bc1-6ea9-456b-829f-3f979dc98420", 0},
    {"PictureLib.DataCompositionOutputParameters", u8"ПараметрыВыводаКомпоновкиДанных", "ee7c4a5b-2d9b-4087-ae3e-947792085f09", 0},
    {"PictureLib.DataCompositionOutputParametersDisabled", u8"ПараметрыВыводаКомпоновкиДанныхНедоступные", "1522a72e-11d1-41e3-ba0e-e0780c4ba543", 0},
    {"PictureLib.DataCompositionSelection", u8"ВыборКомпоновкиДанных", "2c732bfa-f734-48bc-a18b-7554db8a3888", 0},
    {"PictureLib.DataCompositionSelectionDisabled", u8"ВыборКомпоновкиДанныхНедоступный", "a0b879df-db58-4cba-b76e-9bbc4391f3d9", 0},
    {"PictureLib.DataCompositionSettingsWizard", u8"КонструкторНастроекКомпоновкиДанных", "affb1617-24bc-4170-9c84-0902cc3ef206", 0},
    {"PictureLib.DataCompositionStandardSettings", u8"СтандартнаяНастройкаКомпоновкиДанных", "251aaa98-0127-44c3-a163-6f5ab4367ee2", 0},
    {"PictureLib.DataCompositionUserFields", u8"ПользовательскиеПоляКомпоновкиДанных", "b68eb29c-2372-46e1-b84e-13843899ccf6", 0},
    {"PictureLib.DataHistory", u8"ИсторияДанных", "e8a49985-fef7-45a9-b6bb-ddd2b9028172", 0},
    {"PictureLib.DataProcessor", u8"Обработка", "a6cbfd77-fcf0-40f4-a8de-ee0d3e580fe6", 0},
    {"PictureLib.DataSearch", u8"ПоискДанных", "35bc8caa-f7ce-4158-87da-d9bf785afa39", 0},
    {"PictureLib.Debit", u8"Дебет", "196622f7-0941-435b-992b-722f3082adf4", 0},
    {"PictureLib.DebitCredit", u8"ДебетКредит", "ed067d76-b144-4d00-bb36-d1833dd1350c", 0},
    {"PictureLib.Delete", u8"Удалить", "08a45a70-c221-4339-b3b1-9f11cb22147d", 0},
    {"PictureLib.DeleteDirectly", u8"УдалитьНепосредственно", "60643198-e4b2-4c39-9de1-53cca3fff382", 0},
    {"PictureLib.Dendrogram", u8"Дендрограмма", "4bf9fbb5-53c5-4b09-bab2-d69bbfab945b", 0},
    {"PictureLib.DialogExclamation", u8"ДиалогВосклицание", "5289d9a4-b012-4d54-9bce-50473fe29b57", 0},
    {"PictureLib.DialogInformation", u8"ДиалогИнформация", "8bdf1079-8fad-4d21-ad7f-4b2e4ecdce3d", 0},
    {"PictureLib.DialogQuestion", u8"ДиалогВопрос", "ef27ae9e-7040-4374-b93c-0d276de2ea23", 0},
    {"PictureLib.DialogStop", u8"ДиалогСтоп", "83db1f8a-41bd-4016-bdb2-a28e3a8d6dcc", 0},
    {"PictureLib.Dimension", u8"Измерение", "c78c9f3d-e92c-4f38-bb72-d8bd7fa5dbe3", 0},
    {"PictureLib.Document", u8"Документ", "894afc03-9904-465d-b671-f555ffb9b21c", 0},
    {"PictureLib.DocumentJournal", u8"ЖурналДокументов", "e51185a4-d915-45b8-b201-1c46cc2d8104", 0},
    {"PictureLib.DocumentObject", u8"ДокументОбъект", "2209b937-6f41-4153-b1ac-ef7e170ad3a7", 0},
    {"PictureLib.DontDisturb", u8"НеБеспокоить", "a55455d6-be06-4da3-9a4f-62fbefd1aded", 0},
    {"PictureLib.DontNotify", u8"НеОповещать", "517ed1af-55eb-42da-a1d4-a9960ef7c1f3", 0},
    {"PictureLib.EditInDialog", u8"РедактироватьВДиалоге", "021c20a0-071b-4a60-8e44-12487adde0c8", 0},
    {"PictureLib.EndEdit", u8"ЗакончитьРедактирование", "ed0bec43-4633-416c-8c08-0384ca444e32", 0},
    {"PictureLib.Enum", u8"Перечисление", "80d1a756-13a8-4281-86cc-eaaf84da4cf9", 0},
    {"PictureLib.EventLog", u8"ЖурналРегистрации", "723765ab-0b92-4745-a621-1ba0f77c92c9", 0},
    {"PictureLib.EventLogByUser", u8"ЖурналРегистрацииПоПользователю", "4fddea39-5129-4b4c-83fe-4e443cd61940", 0},
    {"PictureLib.ExchangePlan", u8"ПланОбмена", "544fdbe8-5956-4512-bc62-93b4c022d291", 0},
    {"PictureLib.ExchangePlanObject", u8"ПланОбменаОбъект", "743cc773-12e6-4ee6-9c57-5f5800bac427", 0},
    {"PictureLib.ExecuteTask", u8"ВыполнитьЗадачу", "003024ed-fa25-42ac-9f53-f5014e383801", 0},
    {"PictureLib.ExpandAll", u8"РазвернутьВсе", "fb7e9fb5-110b-41cb-adc6-753969ae1c81", 0},
    {"PictureLib.ExternalDataSource", u8"ВнешнийИсточникДанных", "3a32aa35-c679-4c5b-91bd-72ae038e5bb4", 0},
    {"PictureLib.ExternalDataSourceCube", u8"ВнешнийИсточникДанныхКуб", "fad46a2b-2e56-47cc-b90c-3c2d4b061937", 0},
    {"PictureLib.ExternalDataSourceCubeDimensionTable", u8"ВнешнийИсточникДанныхКубТаблицаИзмерения", "32b63e58-c1fa-4f59-b874-bf4a077f816e", 0},
    {"PictureLib.ExternalDataSourceFunction", u8"ВнешнийИсточникДанныхФункция", "2954e819-f3fc-40de-9769-292efce9a355", 0},
    {"PictureLib.ExternalDataSourceTable", u8"ВнешнийИсточникДанныхТаблица", "5b612c21-e223-4997-9e61-86f7a67ec945", 0},
    {"PictureLib.Favorites", u8"Избранное", "c38cc4cf-111d-4bc8-8dcb-4464e2ddfb25", 0},
    {"PictureLib.FilterAndSort", u8"ОтборИСортировка", "73af51dd-6cda-48be-a093-5a7161c60c77", 0},
    {"PictureLib.FilterByCurrentValue", u8"ОтборПоТекущемуЗначению", "b1406535-6cc2-4410-95ea-753556e8460f", 0},
    {"PictureLib.FilterByType", u8"ОтборПоВиду", "0bac63da-5b4e-48af-b593-7c5d29663e83", 0},
    {"PictureLib.FilterCriterion", u8"КритерийОтбора", "2ef82795-06fe-4365-bd0c-44b486264620", 0},
    {"PictureLib.FilterHistory", u8"ИсторияОтборов", "8729a534-9f88-47b0-8d6b-ec213689580d", 0},
    {"PictureLib.Find", u8"Найти", "ffab30f1-da11-44b5-b34c-24da22badcf4", 0},
    {"PictureLib.FindByNumber", u8"НайтиПоНомеру", "4eb938cb-7d69-4f3d-ac17-1f20f1212d49", 0},
    {"PictureLib.FindInList", u8"НайтиВСписке", "c7cdd3c0-3879-436a-b145-5e2615e9b3e1", 0},
    {"PictureLib.FindInTree", u8"НайтиВДереве", "38bbcebe-e456-461b-8457-07c9a72344a3", 0},
    {"PictureLib.FindNext", u8"НайтиСледующий", "05612131-3e11-49c0-9592-07e6d9318ef7", 0},
    {"PictureLib.FindPrevious", u8"НайтиПредыдущий", "e3b38083-0191-4a10-8f5b-51571f2419b4", 0},
    {"PictureLib.FixTable", u8"ЗафиксироватьТаблицу", "5182f57f-e834-4d11-9c9f-4aedc002b6e9", 0},
    {"PictureLib.Form", u8"Форма", "fc34a694-e99b-4d1c-a526-63f5571bdb09", 0},
    {"PictureLib.FormHelp", u8"СправкаФормы", "b7c81c62-d6ad-4eae-9cea-0e203182db67", 0},
    {"PictureLib.FormattedString", u8"ФорматированнаяСтрока", "a636287d-8d6f-4ef3-94bb-a8f986ab52bb", 0},
    {"PictureLib.Forward", u8"Вперед", "f874b0cc-db1d-4577-8c77-d4ba206eb05d", 0},
    {"PictureLib.FunctionMenuCommand", u8"КомандаМенюФункций", "dfcd2d21-24ea-4b27-ab9a-6bf754577536", 0},
    {"PictureLib.GanttChart", u8"ДиаграммаГанта", "fa67cb81-8d56-4534-90bd-b62fb0dbf5f0", 0},
    {"PictureLib.GenerateReport", u8"СформироватьОтчет", "0ce78048-0196-4f80-a781-9829cdb7f43e", 0},
    {"PictureLib.GeographicalSchema", u8"ГеографическаяСхема", "a9152be7-62cf-4523-be34-a23f018f497e", 0},
    {"PictureLib.GetURL", u8"ПолучитьНавигационнуюСсылку", "1a4342a5-fa06-4556-8a85-e8738fc25821", 0},
    {"PictureLib.GoBack", u8"ПерейтиНазад", "892196a9-c94f-4e50-8224-3c0cea4ea6b8", 0},
    {"PictureLib.GoForward", u8"ПерейтиВперед", "7562cef7-0e57-4f63-a754-b61128a4f3ae", 0},
    {"PictureLib.GoToBegin", u8"ПерейтиКНачалу", "85cc7dd0-44fc-41aa-967f-f52f202ee2e6", 0},
    {"PictureLib.GoToEnd", u8"ПерейтиККонцу", "14b24498-e49c-4713-be64-75101d0abfb9", 0},
    {"PictureLib.GotoExternalURL", u8"ПерейтиПоВнешнейНавигационнойСсылке", "31bf709f-3b50-4137-9b51-ebc7fb802a7c", 0},
    {"PictureLib.GotoURL", u8"ПерейтиПоНавигационнойСсылке", "3b2a508b-f36e-4e0b-9dc0-70b2b56276a9", 0},
    {"PictureLib.GraphicalSchema", u8"ГрафическаяСхема", "e96de06b-fa83-48cf-b033-190a249855c9", 0},
    {"PictureLib.GrayedAll", u8"ЗатенитьФлажки", "", -12},
    {"PictureLib.GroupConversation", u8"ГрупповоеОбсуждение", "2a7e58e2-a6c5-4387-a459-5249c441947a", 0},
    {"PictureLib.Help", u8"Справка", "6ecee038-9722-4d80-bb91-7ee7046ec4c7", 0},
    {"PictureLib.HidePassword", u8"СкрытьПароль", "0e2da390-5c04-46a2-a74b-1b7e13a40f2b", 0},
    {"PictureLib.HierarchicalView", u8"ИерархическийПросмотр", "a119150f-6c0c-4a94-97b0-5f08d7ebd6f5", 0},
    {"PictureLib.History", u8"История", "c283cd1c-3187-451d-8ef2-7df55daeef06", 0},
    {"PictureLib.Information", u8"Информация", "4b54770b-d069-4c0e-9b17-5cc2a01134d9", 0},
    {"PictureLib.InformationRegister", u8"РегистрСведений", "5b87ad1b-d8cc-43c1-b5c4-dc43613c518c", 0},
    {"PictureLib.InformationRegisterRecord", u8"РегистрСведенийЗапись", "4128e6b6-e4ef-48d4-a523-99dc6b613054", 0},
    {"PictureLib.InputFieldCalculator", u8"ПолеВводаКалькулятор", "", -6},
    {"PictureLib.InputFieldCalendar", u8"ПолеВводаКалендарь", "", -5},
    {"PictureLib.InputFieldChooseType", u8"ПолеВводаВыбратьТип", "", -14},
    {"PictureLib.InputFieldClear", u8"ПолеВводаОчистить", "", -2},
    {"PictureLib.InputFieldOpen", u8"ПолеВводаОткрыть", "", -7},
    {"PictureLib.InputFieldSelect", u8"ПолеВводаВыбрать", "", -1},
    {"PictureLib.InputOnBasis", u8"ВводНаОсновании", "01ec9d9a-7497-4d88-b93f-066c633a4866", 0},
    {"PictureLib.LevelDown", u8"УровеньВниз", "01743054-d102-4e7c-bf15-5ed7fd84441b", 0},
    {"PictureLib.LevelUp", u8"УровеньВверх", "cb34c423-3d6a-4202-a809-3b3f45fb14ab", 0},
    {"PictureLib.ListSettings", u8"НастройкаСписка", "31b93f03-0ba2-4631-a171-0d3a3d2ecc48", 0},
    {"PictureLib.ListViewMode", u8"РежимПросмотраСписка", "549a2c45-4fce-493f-94ee-9a3a4f426551", 0},
    {"PictureLib.ListViewModeHierarchicalList", u8"РежимПросмотраСпискаИерархическийСписок", "3d4ad3b1-17de-4cf1-a2e4-0c2c83a5b5c2", 0},
    {"PictureLib.ListViewModeList", u8"РежимПросмотраСпискаСписок", "c757209d-a87f-4410-b1a3-76000178f1f0", 0},
    {"PictureLib.ListViewModeTree", u8"РежимПросмотраСпискаДерево", "da9ac044-0ff7-4bcf-a441-3187bd1d951f", 0},
    {"PictureLib.LoadReportSettings", u8"ЗагрузитьНастройкиОтчета", "283ecabd-aaed-41d1-ad46-6cca91c29120", 0},
    {"PictureLib.Magnifier", u8"Лупа", "", -700},
    {"PictureLib.MarkToDelete", u8"ПометитьНаУдаление", "18492a87-2fe4-44af-b218-304897fed020", 0},
    {"PictureLib.Message", u8"Сообщение", "78da2c47-172f-4d57-ab52-a06e40548136", 0},
    {"PictureLib.MessageHistory", u8"ИсторияСообщений", "d08abfb8-44b6-4b0f-bad2-e235c6a718c3", 0},
    {"PictureLib.MoveDown", u8"ПереместитьВниз", "", -4},
    {"PictureLib.MoveItem", u8"ПеренестиЭлемент", "37e91e77-93ce-4c3b-8d30-a9d8cfd3d3b0", 0},
    {"PictureLib.MoveLeft", u8"ПереместитьВлево", "", -8},
    {"PictureLib.MoveRight", u8"ПереместитьВправо", "", -9},
    {"PictureLib.MoveUp", u8"ПереместитьВверх", "", -3},
    {"PictureLib.NestedTable", u8"ВложеннаяТаблица", "52b637e5-f95f-4c70-9a72-2a4b5a9df449", 0},
    {"PictureLib.NewConversation", u8"НовоеОбсуждение", "9c7f3c8c-a4b4-4a0a-84c8-d60d9feeca16", 0},
    {"PictureLib.NewWindow", u8"НовоеОкно", "eb47324b-85f9-4172-9315-bba8015d9970", 0},
    {"PictureLib.Next", u8"Следующий", "9cf611dc-2370-4357-910d-a2b49c7a1ec6", 0},
    {"PictureLib.Notifications", u8"Оповещения", "928075d1-b90b-416c-b0b2-c3104cf084aa", 0},
    {"PictureLib.Notify", u8"Оповещать", "d66b6f73-53b8-49b9-8efc-33c54aa06e3f", 0},
    {"PictureLib.OpenFile", u8"ОткрытьФайл", "785362cb-3756-48ed-87d2-292ded17054a", 0},
    {"PictureLib.OpenFromMainServer", u8"ОткрытьСОсновногоСервера", "b4ea245b-70ce-4777-a321-c7eb09839643", 0},
    {"PictureLib.OpenFromStandaloneServer", u8"ОткрытьСАвтономногоСервера", "3bf7bd14-2232-4f53-b3b3-59a8c683f9b0", 0},
    {"PictureLib.OutputList", u8"ВывестиСписок", "c2e2d966-5b7f-4699-903b-28a6f50d5471", 0},
    {"PictureLib.Parameters", u8"Параметры", "835db646-1531-494b-b7c1-3239b0080bcb", 0},
    {"PictureLib.Picture", u8"Картинка", "64837726-d2a2-4682-a788-737423e80013", 0},
    {"PictureLib.PivotChart", u8"СводнаяДиаграмма", "79bf535f-2256-4ebb-bb8d-c26f3dd1a3de", 0},
    {"PictureLib.Post", u8"Провести", "20ebc47b-f4d9-439c-acd3-fdc624fbac2a", 0},
    {"PictureLib.Previous", u8"Предыдущий", "584b470d-ba34-4b25-9620-8de4066ffeaa", 0},
    {"PictureLib.Print", u8"Печать", "", -13},
    {"PictureLib.PrintImmediately", u8"ПечатьСразу", "0abdab67-5c90-4296-8168-239d22024d11", 0},
    {"PictureLib.Properties", u8"Свойства", "b4c7ab2c-bcda-4468-a28f-5fee93838c4e", 0},
    {"PictureLib.QueryWizard", u8"КонструкторЗапроса", "1f046bc2-d6c5-46a3-a459-b2c0508f86fb", 0},
    {"PictureLib.QueryWizardCreateNestedQuery", u8"КонструкторЗапросаСоздатьВложенныйЗапрос", "18bca3d7-a7a5-41df-a180-4dff9c217f43", 0},
    {"PictureLib.QueryWizardCreateTempTableDescription", u8"КонструкторЗапросаСоздатьОписаниеВременнойТаблицы", "7604cff7-5cc6-4f88-8d16-504f01b92a3c", 0},
    {"PictureLib.QueryWizardCreateTempTableDropQuery", u8"КонструкторЗапросаСоздатьЗапросУничтоженияВременнойТаблицы", "7df3febb-2640-41b7-ad8b-7a23b7ad4aec", 0},
    {"PictureLib.QueryWizardNestedQuery", u8"КонструкторЗапросаВложенныйЗапрос", "c5501b77-0070-46e3-8ebd-f04a61eb9a73", 0},
    {"PictureLib.QueryWizardReplaceTable", u8"КонструкторЗапросаЗаменитьТаблицу", "a9481ba4-dc85-4112-9c50-f9f340a61298", 0},
    {"PictureLib.QueryWizardShowChangesTables", u8"КонструкторЗапросаОтображатьТаблицыИзменений", "270de5f0-f2df-4845-9fde-30b1ec486217", 0},
    {"PictureLib.QueryWizardTableParameters", u8"КонструкторЗапросаПараметрыТаблицы", "fe740df0-d828-4241-a12f-7414e12302e8", 0},
    {"PictureLib.QueryWizardTempTable", u8"КонструкторЗапросаВременнаяТаблица", "64ca52ee-f1a3-468f-8055-311935077515", 0},
    {"PictureLib.QueryWizardTempTableDescription", u8"КонструкторЗапросаОписаниеВременнойТаблицы", "6b322d7c-3fa2-437d-b311-fd70c4c7ed46", 0},
    {"PictureLib.QueryWizardTempTablesGroup", u8"КонструкторЗапросаГруппаВременныхТаблиц", "49369551-39dc-4adb-8795-5ab619c6c2bd", 0},
    {"PictureLib.ReadChanges", u8"ПрочитатьИзменения", "83c8f18d-8701-41f3-bef4-53f88adbb868", 0},
    {"PictureLib.Refresh", u8"Обновить", "fc4f29e0-d168-4fe0-8e64-e982fabf2595", 0},
    {"PictureLib.Rename", u8"Переименовать", "c8a269ff-5b6d-4f42-9fa6-369d7b492aa7", 0},
    {"PictureLib.Replace", u8"Заменить", "6cb69e7f-fe19-4f64-bfb5-1a4fad6c2ef9", 0},
    {"PictureLib.Report", u8"Отчет", "db817ee1-fd28-4e7f-bb4a-53686b2b153c", 0},
    {"PictureLib.ReportSettings", u8"НастройкиОтчета", "942e0303-a3ec-4fe8-887c-5aea8516d424", 0},
    {"PictureLib.Reread", u8"Перечитать", "8f29e0e2-d5e6-41e8-a34d-9a0288156322", 0},
    {"PictureLib.Resource", u8"Ресурс", "d6eefec0-792a-4720-8933-e2a57f9e312c", 0},
    {"PictureLib.RestoreValues", u8"ВосстановитьЗначения", "a7707ed1-39b0-418f-974d-4d500d27a9c6", 0},
    {"PictureLib.RotateClockwise", u8"ПовернутьПоЧасовойСтрелке", "c7f70aa3-b944-4efe-97e3-0fa3bda3cd88", 0},
    {"PictureLib.RotateCounterclockwise", u8"ПовернутьПротивЧасовойСтрелки", "a43fcd1b-ad8d-4318-9d55-fd1ba086e65b", 0},
    {"PictureLib.SaveFile", u8"СохранитьФайл", "818ab7d0-4654-4542-bd5e-fd9d1352b5a1", 0},
    {"PictureLib.SaveReportSettings", u8"СохранитьНастройкиОтчета", "b5a0aaba-3a83-4a71-b6f9-24aae1574681", 0},
    {"PictureLib.SaveValues", u8"СохранитьЗначения", "23f940bf-7381-4c2b-85a1-e541ed428042", 0},
    {"PictureLib.ScheduledJob", u8"РегламентноеЗадание", "1970a480-9b38-405e-9d9e-8209f3fad5f1", 0},
    {"PictureLib.ScheduledJobs", u8"РегламентныеЗадания", "6206a729-16e5-4e32-b53c-122de4e30c8d", 0},
    {"PictureLib.SearchControl", u8"УправлениеПоиском", "fc6a06a8-1308-4385-b1b2-9d302d2054ed", 0},
    {"PictureLib.Select", u8"Выбрать", "", -100},
    {"PictureLib.SendMessage", u8"ОтправитьСообщение", "be23a908-fe1b-44df-be94-d0f6e8353abe", 0},
    {"PictureLib.SetDateInterval", u8"УстановитьИнтервал", "58174855-39be-462e-8723-cb2d95182146", 0},
    {"PictureLib.SetTime", u8"УстановитьВремя", "55ef0776-5ee4-4daf-9a9b-70d63643ab8d", 0},
    {"PictureLib.Setting", u8"Настройка", "caf2e58b-ca3d-4b63-82c9-f21f1c9bc9eb", 0},
    {"PictureLib.SettingsStorage", u8"ХранилищеНастроек", "6b909f65-95a4-4697-8ca0-c8f331227b9a", 0},
    {"PictureLib.ShowData", u8"ПоказатьДанные", "a064544f-6037-48ca-b19f-8ad63e43af23", 0},
    {"PictureLib.ShowInList", u8"ПоказатьВСписке", "9c96aa25-d656-4b3d-ab3e-81d9718da238", 0},
    {"PictureLib.ShowPassword", u8"ПоказатьПароль", "97f87955-b88a-4225-a0d8-03af981ecd86", 0},
    {"PictureLib.Sort", u8"Сортировка", "a594c8a1-7218-420a-860f-7b493c5e65c4", 0},
    {"PictureLib.SortList", u8"СортироватьСписок", "fafe4c1f-c265-4220-a0e1-8f82af26b72e", 0},
    {"PictureLib.SortListAsc", u8"СортироватьСписокПоВозрастанию", "91022b99-b610-48ad-954e-a297848081ce", 0},
    {"PictureLib.SortListDesc", u8"СортироватьСписокПоУбыванию", "1fa32fdb-a180-418f-a6eb-db7516b7a30b", 0},
    {"PictureLib.SortedAsc", u8"ОтсортированоПоВозрастанию", "e4c49dfb-ef8e-4f4a-9d01-9536ba61f944", 0},
    {"PictureLib.SortedDesc", u8"ОтсортированоПоУбыванию", "34c981c1-f0bd-43e0-a0cf-c2a9cd648b91", 0},
    {"PictureLib.SpreadsheetDeleteComment", u8"ТабличныйДокументУдалитьПримечание", "7c75b1df-1fdc-471f-aae6-6b7870318cd4", 0},
    {"PictureLib.SpreadsheetDeletePageBreak", u8"ТабличныйДокументУдалитьРазрывСтраницы", "aa96f4bb-cf28-4dad-bc42-5ed53de95c0c", 0},
    {"PictureLib.SpreadsheetInsertComment", u8"ТабличныйДокументВставитьПримечание", "03665ff1-3a05-41d1-96d3-04bda2d8ede3", 0},
    {"PictureLib.SpreadsheetInsertPageBreak", u8"ТабличныйДокументВставитьРазрывСтраницы", "26518e18-e364-475a-8026-e41134658b2a", 0},
    {"PictureLib.SpreadsheetReadOnly", u8"ТабличныйДокументТолькоПросмотр", "2846af8d-af84-47e3-82b9-01b01f960426", 0},
    {"PictureLib.SpreadsheetShowComments", u8"ТабличныйДокументОтображатьПримечания", "bc61d1e7-2d40-4e93-8630-cce840bdcf99", 0},
    {"PictureLib.SpreadsheetShowGrid", u8"ТабличныйДокументОтображатьСетку", "3646acb9-a91e-4163-bbe1-2006041cd65d", 0},
    {"PictureLib.SpreadsheetShowGroups", u8"ТабличныйДокументОтображатьГруппировки", "702a9e16-0bb6-4efb-af11-10faf1e6ee87", 0},
    {"PictureLib.SpreadsheetShowHeaders", u8"ТабличныйДокументОтображатьЗаголовки", "46598f81-5f95-4485-9b33-bfe4fd1276d0", 0},
    {"PictureLib.StartVideoconference", u8"НачатьВидеоконференцию", "e7f605f9-53d4-4e07-b56a-4715dc3a3e50", 0},
    {"PictureLib.Stop", u8"Остановить", "1cd7b762-ec6a-4e92-ac9a-1832be228ec3", 0},
    {"PictureLib.SwitchActivity", u8"ПереключитьАктивность", "6e3687cf-a8d1-446a-833a-bfaf38516353", 0},
    {"PictureLib.SyncContents", u8"НайтиВСодержании", "3bdc16c8-6a96-4467-9442-a8e4804b3fa2", 0},
    {"PictureLib.Task", u8"Задача", "37cf7cc0-abad-4385-b597-6fd2d8dc085a", 0},
    {"PictureLib.TaskObject", u8"ЗадачаОбъект", "b9e2dae2-b205-4442-aee5-802cd82f5199", 0},
    {"PictureLib.Today", u8"Сегодня", "6ca1af75-2aa5-48a6-820d-efa0ccb8f4f1", 0},
    {"PictureLib.UncheckAll", u8"СнятьФлажки", "", -11},
    {"PictureLib.UndoPosting", u8"ОтменаПроведения", "8ca4ea33-603d-4992-8a41-c7924b5bd40b", 0},
    {"PictureLib.User", u8"Пользователь", "6ff3ddbd-56e3-4ddf-a5bf-048c1e2dfb2f", 0},
    {"PictureLib.UserWithAuthentication", u8"ПользовательСАутентификацией", "75a40cc4-c719-4c3f-91ea-fc5787bc34ca", 0},
    {"PictureLib.UserWithoutNecessaryProperties", u8"ПользовательБезНеобходимыхСвойств", "f16ab927-3ac6-4cdd-bcf1-d8acf005a255", 0},
    {"PictureLib.ViewByOwner", u8"ПросмотрПоВладельцу", "683fc04f-e189-49b7-92a9-f835c36f5fb2", 0},
    {"PictureLib.Write", u8"Записать", "894cf65b-4109-4533-a1d7-c87b1fcc80a3", 0},
    {"PictureLib.WriteAndClose", u8"ЗаписатьИЗакрыть", "e6fc55a0-3d58-4b15-bdd3-717453929598", 0},
    {"PictureLib.WriteChanges", u8"ЗаписатьИзменения", "f62488ee-f90c-47f7-929d-f42ec11a1e63", 0},
    {"PictureLib.Zoom", u8"ИзменитьМасштаб", "b9cb1339-815d-4e11-84d3-aebfa5a58459", 0},
    {"PictureLib.ZoomIn", u8"УвеличитьМасштаб", "", -16},
    {"PictureLib.ZoomOut", u8"УменьшитьМасштаб", "", -15},
}};

static_assert(control_identities.size() == 26);

std::vector<HelpControlName> make_help_control_names() {
    return {
#define OOF_HELP_CONTROL(kind_token, public_name_value, api_name_value, russian_name_value) \
        {ControlKind::kind_token, public_name_value, api_name_value, russian_name_value},
#define OOF_HELP_PROPERTY(...)
#define OOF_HELP_EVENT(...)
#define OOF_HELP_FORM_PROPERTY(...)
#define OOF_HELP_FORM_EVENT(...)
#define OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY(...)
#define OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY(...)
#include "generated_help_catalog.inc"
#undef OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_EVENT
#undef OOF_HELP_FORM_PROPERTY
#undef OOF_HELP_EVENT
#undef OOF_HELP_PROPERTY
#undef OOF_HELP_CONTROL
    };
}

std::vector<PropertyDescriptor> make_control_property_descriptors() {
    return {
#define OOF_HELP_CONTROL(...)
#define OOF_HELP_PROPERTY(kind_token, order_value, xml_name_value, api_name_value, russian_name_value, platform_type_value, value_kind_token, api_access_token, version_token) \
        {PropertyId::from_name(api_name_value), DescriptorOwner::control, PropertySurface::control_payload, ControlKind::kind_token, order_value, xml_name_value, api_name_value, russian_name_value, platform_type_value, ValueKind::value_kind_token, initial_value_codec(ValueKind::value_kind_token, platform_type_value), ApiAccess::api_access_token, VersionMask::version_token, PersistenceClass::unclassified, StorageCodec::unclassified, {}},
#define OOF_HELP_EVENT(...)
#define OOF_HELP_FORM_PROPERTY(...)
#define OOF_HELP_FORM_EVENT(...)
#define OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY(...)
#define OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY(...)
#include "generated_help_catalog.inc"
#undef OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_EVENT
#undef OOF_HELP_FORM_PROPERTY
#undef OOF_HELP_EVENT
#undef OOF_HELP_PROPERTY
#undef OOF_HELP_CONTROL
    };
}

std::vector<EventDescriptor> make_control_event_descriptors() {
    return {
#define OOF_HELP_CONTROL(...)
#define OOF_HELP_PROPERTY(...)
#define OOF_HELP_EVENT(kind_token, order_value, xml_name_value, api_name_value, russian_name_value, version_token) \
        {DescriptorOwner::control, ControlKind::kind_token, order_value, xml_name_value, api_name_value, russian_name_value, VersionMask::version_token, PersistenceClass::unclassified, StorageCodec::unclassified, {}},
#define OOF_HELP_FORM_PROPERTY(...)
#define OOF_HELP_FORM_EVENT(...)
#define OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY(...)
#define OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY(...)
#include "generated_help_catalog.inc"
#undef OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_EVENT
#undef OOF_HELP_FORM_PROPERTY
#undef OOF_HELP_EVENT
#undef OOF_HELP_PROPERTY
#undef OOF_HELP_CONTROL
    };
}

std::vector<PropertyDescriptor> make_form_property_descriptors() {
    return {
#define OOF_HELP_CONTROL(...)
#define OOF_HELP_PROPERTY(...)
#define OOF_HELP_EVENT(...)
#define OOF_HELP_FORM_PROPERTY(order_value, xml_name_value, api_name_value, russian_name_value, platform_type_value, value_kind_token, api_access_token, version_token) \
        {PropertyId::from_name(api_name_value), DescriptorOwner::form, PropertySurface::form, ControlKind::panel, order_value, xml_name_value, api_name_value, russian_name_value, platform_type_value, ValueKind::value_kind_token, initial_value_codec(ValueKind::value_kind_token, platform_type_value), ApiAccess::api_access_token, VersionMask::version_token, PersistenceClass::unclassified, StorageCodec::unclassified, {}},
#define OOF_HELP_FORM_EVENT(...)
#define OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY(...)
#define OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY(...)
#include "generated_help_catalog.inc"
#undef OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_EVENT
#undef OOF_HELP_FORM_PROPERTY
#undef OOF_HELP_EVENT
#undef OOF_HELP_PROPERTY
#undef OOF_HELP_CONTROL
    };
}

std::vector<EventDescriptor> make_form_event_descriptors() {
    return {
#define OOF_HELP_CONTROL(...)
#define OOF_HELP_PROPERTY(...)
#define OOF_HELP_EVENT(...)
#define OOF_HELP_FORM_PROPERTY(...)
#define OOF_HELP_FORM_EVENT(order_value, xml_name_value, api_name_value, russian_name_value, version_token) \
        {DescriptorOwner::form, ControlKind::panel, order_value, xml_name_value, api_name_value, russian_name_value, VersionMask::version_token, PersistenceClass::unclassified, StorageCodec::unclassified, {}},
#define OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY(...)
#define OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY(...)
#include "generated_help_catalog.inc"
#undef OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_EVENT
#undef OOF_HELP_FORM_PROPERTY
#undef OOF_HELP_EVENT
#undef OOF_HELP_PROPERTY
#undef OOF_HELP_CONTROL
    };
}

std::vector<PropertyDescriptor> make_control_extension_property_descriptors() {
    return {
#define OOF_HELP_CONTROL(...)
#define OOF_HELP_PROPERTY(...)
#define OOF_HELP_EVENT(...)
#define OOF_HELP_FORM_PROPERTY(...)
#define OOF_HELP_FORM_EVENT(...)
#define OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY(order_value, xml_name_value, api_name_value, russian_name_value, platform_type_value, value_kind_token, api_access_token, version_token) \
        {PropertyId::from_name(api_name_value), DescriptorOwner::control, PropertySurface::control_extension, ControlKind::panel, order_value, xml_name_value, api_name_value, russian_name_value, platform_type_value, ValueKind::value_kind_token, initial_value_codec(ValueKind::value_kind_token, platform_type_value), ApiAccess::api_access_token, VersionMask::version_token, PersistenceClass::unclassified, StorageCodec::unclassified, {}},
#define OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY(...)
#include "generated_help_catalog.inc"
#undef OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_EVENT
#undef OOF_HELP_FORM_PROPERTY
#undef OOF_HELP_EVENT
#undef OOF_HELP_PROPERTY
#undef OOF_HELP_CONTROL
    };
}

std::vector<PropertyDescriptor> make_panel_placement_property_descriptors() {
    return {
#define OOF_HELP_CONTROL(...)
#define OOF_HELP_PROPERTY(...)
#define OOF_HELP_EVENT(...)
#define OOF_HELP_FORM_PROPERTY(...)
#define OOF_HELP_FORM_EVENT(...)
#define OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY(...)
#define OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY(order_value, xml_name_value, api_name_value, russian_name_value, platform_type_value, value_kind_token, api_access_token, version_token) \
        {PropertyId::from_name(api_name_value), DescriptorOwner::control, PropertySurface::panel_placement, ControlKind::panel, order_value, xml_name_value, api_name_value, russian_name_value, platform_type_value, ValueKind::value_kind_token, panel_placement_value_codec(ValueKind::value_kind_token, platform_type_value), ApiAccess::api_access_token, VersionMask::version_token, PersistenceClass::unclassified, StorageCodec::unclassified, {}},
#include "generated_help_catalog.inc"
#undef OOF_HELP_PANEL_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_CONTROL_EXTENSION_PROPERTY
#undef OOF_HELP_FORM_EVENT
#undef OOF_HELP_FORM_PROPERTY
#undef OOF_HELP_EVENT
#undef OOF_HELP_PROPERTY
#undef OOF_HELP_CONTROL
    };
}

template <typename Descriptor>
void sort_and_validate_order(std::vector<Descriptor>& descriptors, std::string_view owner) {
    std::ranges::sort(descriptors, {}, &Descriptor::order);
    for (std::size_t index = 1; index < descriptors.size(); ++index) {
        if (descriptors[index - 1].order == descriptors[index].order) {
            throw std::logic_error(
                "duplicate ordinary-form help order for " + std::string(owner));
        }
    }
}

bool requires_storage(PersistenceClass classification) noexcept {
    return classification == PersistenceClass::unclassified ||
           classification == PersistenceClass::persisted_editable ||
           classification == PersistenceClass::persisted_readonly ||
           classification == PersistenceClass::version_specific;
}

bool requires_default(const PropertyDescriptor& descriptor) noexcept {
    return requires_storage(descriptor.persistence) &&
           descriptor.api_access != ApiAccess::read_only;
}

void classify_property(
    std::vector<PropertyDescriptor>& descriptors,
    std::string_view name,
    StorageCodec storage_codec,
    DefaultKind default_kind,
    std::string_view default_value) {
    const auto descriptor = std::ranges::find(
        descriptors,
        name,
        &PropertyDescriptor::api_name);
    if (descriptor == descriptors.end()) {
        throw std::logic_error("missing property for proven storage override: " + std::string(name));
    }
    descriptor->persistence = PersistenceClass::persisted_editable;
    descriptor->storage_codec = storage_codec;
    descriptor->default_value = {default_kind, default_value};
}

void apply_proven_storage_overrides(
    std::array<std::vector<PropertyDescriptor>, control_kind_count>& properties,
    std::vector<PropertyDescriptor>& panel_placement_properties,
    std::vector<PropertyDescriptor>& form_properties,
    std::array<std::vector<EventDescriptor>, control_kind_count>& events) {
    auto& usual_group = properties[static_cast<std::size_t>(ControlKind::usual_group)];
    classify_property(usual_group, "Enabled", StorageCodec::control_base, DefaultKind::boolean, "true");
    classify_property(usual_group, "Caption", StorageCodec::control_info, DefaultKind::string, "");
    classify_property(usual_group, "ToolTip", StorageCodec::control_base, DefaultKind::string, "");
    auto& button = properties[static_cast<std::size_t>(ControlKind::button)];
    classify_property(button, "Buttons", StorageCodec::control_info, DefaultKind::none, "");
    std::ranges::find(button, "Buttons", &PropertyDescriptor::api_name)->value_codec = ValueCodec::command_bar_buttons;
    auto& command_bar = properties[static_cast<std::size_t>(ControlKind::command_bar)];
    classify_property(command_bar, "Enabled", StorageCodec::control_base, DefaultKind::boolean, "true");
    classify_property(command_bar, "ToolTip", StorageCodec::control_base, DefaultKind::string, "");
    classify_property(command_bar, "Buttons", StorageCodec::control_info, DefaultKind::none, "");
    std::ranges::find(command_bar, "Buttons", &PropertyDescriptor::api_name)->value_codec = ValueCodec::command_bar_buttons;
    classify_property(
        button,
        "Enabled",
        StorageCodec::control_base,
        DefaultKind::boolean,
        "true");
    classify_property(
        button,
        "Caption",
        StorageCodec::control_info,
        DefaultKind::string,
        "");
    classify_property(
        button,
        "MultiLine",
        StorageCodec::control_info,
        DefaultKind::boolean,
        "false");
    classify_property(
        button,
        "HorizontalAlign",
        StorageCodec::control_info,
        DefaultKind::enumeration,
        "Center");
    classify_property(
        button,
        "VerticalAlign",
        StorageCodec::control_info,
        DefaultKind::enumeration,
        "Center");
    classify_property(
        button,
        "ToolTip",
        StorageCodec::control_base,
        DefaultKind::string,
        "");
    classify_property(
        button,
        "PictureLocation",
        StorageCodec::control_info,
        DefaultKind::enumeration,
        "Left");
    classify_property(
        button,
        "PictureSize",
        StorageCodec::control_info,
        DefaultKind::enumeration,
        "RealSize");
    classify_property(
        button,
        "MenuMode",
        StorageCodec::control_info,
        DefaultKind::enumeration,
        "DontUse");
    classify_property(
        button,
        "Picture",
        StorageCodec::picture_record,
        DefaultKind::none,
        "");
    classify_property(
        button,
        "BorderColor",
        StorageCodec::control_base,
        DefaultKind::color,
        "automatic");
    classify_property(
        button,
        "ButtonTextColor",
        StorageCodec::control_base,
        DefaultKind::color,
        "StyleColors.ButtonTextColor");
    classify_property(
        button,
        "ButtonBackColor",
        StorageCodec::control_base,
        DefaultKind::color,
        "StyleColors.ButtonBackColor");
    classify_property(
        button,
        "Font",
        StorageCodec::control_base,
        DefaultKind::font,
        "automatic");
    classify_property(
        button,
        "Shortcut",
        StorageCodec::control_info,
        DefaultKind::shortcut,
        "None");

    auto& radio_button = properties[static_cast<std::size_t>(ControlKind::radio_button)];
    classify_property(radio_button, "Enabled", StorageCodec::control_base, DefaultKind::boolean, "true");
    classify_property(radio_button, "Caption", StorageCodec::control_info, DefaultKind::string, "");
    classify_property(radio_button, "ToolTip", StorageCodec::control_base, DefaultKind::string, "");

    auto& html_document_field = properties[static_cast<std::size_t>(ControlKind::html_document_field)];
    classify_property(
        html_document_field,
        "Output",
        StorageCodec::control_info,
        DefaultKind::enumeration,
        "Auto");

    auto& picture_decoration = properties[static_cast<std::size_t>(ControlKind::picture_decoration)];
    classify_property(
        picture_decoration,
        "Enabled",
        StorageCodec::control_base,
        DefaultKind::boolean,
        "true");
    classify_property(
        picture_decoration,
        "ToolTip",
        StorageCodec::control_base,
        DefaultKind::string,
        "");
    classify_property(
        picture_decoration,
        "Picture",
        StorageCodec::picture_record,
        DefaultKind::none,
        "");

    auto& splitter = properties[static_cast<std::size_t>(ControlKind::splitter)];
    classify_property(splitter, "Enabled", StorageCodec::control_base, DefaultKind::boolean, "true");
    classify_property(splitter, "Orientation", StorageCodec::control_info, DefaultKind::enumeration, "Auto");
    std::ranges::find(splitter, "Orientation", &PropertyDescriptor::api_name)->value_codec = ValueCodec::enumeration;
    classify_property(splitter, "ToolTip", StorageCodec::control_base, DefaultKind::string, "");
    classify_property(splitter, "BorderColor", StorageCodec::control_base, DefaultKind::color, "automatic");
    classify_property(splitter, "BackColor", StorageCodec::control_base, DefaultKind::color, "automatic");

    auto& label_decoration = properties[static_cast<std::size_t>(ControlKind::label_decoration)];
    classify_property(
        label_decoration,
        "Enabled",
        StorageCodec::control_info,
        DefaultKind::boolean,
        "true");
    classify_property(
        label_decoration,
        "ToolTip",
        StorageCodec::control_info,
        DefaultKind::string,
        "");
    classify_property(
        label_decoration,
        "HorizontalAlign",
        StorageCodec::control_info,
        DefaultKind::none,
        "");

    auto& choice_field = properties[static_cast<std::size_t>(ControlKind::choice_field)];
    classify_property(choice_field, "Enabled", StorageCodec::control_base, DefaultKind::boolean, "true");
    classify_property(choice_field, "ToolTip", StorageCodec::control_base, DefaultKind::string, "");
    if (const auto choice_list = std::ranges::find(choice_field, "ChoiceList", &PropertyDescriptor::api_name);
        choice_list != choice_field.end()) {
        choice_list->persistence = PersistenceClass::runtime_only;
        choice_list->storage_codec = StorageCodec::none;
    }

    auto& check_box = properties[static_cast<std::size_t>(ControlKind::check_box)];
    classify_property(
        check_box,
        "Enabled",
        StorageCodec::control_base,
        DefaultKind::boolean,
        "true");
    classify_property(
        check_box,
        "Caption",
        StorageCodec::control_info,
        DefaultKind::string,
        "");
    classify_property(check_box, "ToolTip", StorageCodec::control_base, DefaultKind::string, "");
    classify_property(check_box, "Font", StorageCodec::control_base, DefaultKind::font, "automatic");

    auto& calendar_field = properties[static_cast<std::size_t>(ControlKind::calendar_field)];
    classify_property(
        calendar_field,
        "Enabled",
        StorageCodec::control_base,
        DefaultKind::boolean,
        "true");
    classify_property(
        calendar_field,
        "BeginOfDisplayPeriod",
        StorageCodec::control_info,
        DefaultKind::undefined,
        "undefined");

    auto& list_box = properties[static_cast<std::size_t>(ControlKind::list_box)];
    classify_property(list_box, "Enabled", StorageCodec::control_base, DefaultKind::boolean, "true");
    classify_property(list_box, "ShowPicture", StorageCodec::control_info, DefaultKind::boolean, "false");
    classify_property(list_box, "ShowCheckBox", StorageCodec::control_info, DefaultKind::boolean, "false");
    classify_property(list_box, "ToolTip", StorageCodec::control_base, DefaultKind::string, "");
    classify_property(list_box, "ReadOnly", StorageCodec::control_info, DefaultKind::boolean, "true");

    auto& text_document_field = properties[static_cast<std::size_t>(ControlKind::text_document_field)];
    classify_property(text_document_field, "Enabled", StorageCodec::control_base, DefaultKind::boolean, "true");
    classify_property(text_document_field, "BorderColor", StorageCodec::control_base, DefaultKind::color, "automatic");
    classify_property(text_document_field, "Font", StorageCodec::control_base, DefaultKind::font, "automatic");

    auto& progress_bar = properties[static_cast<std::size_t>(ControlKind::progress_bar)];
    classify_property(
        progress_bar,
        "Enabled",
        StorageCodec::control_info,
        DefaultKind::boolean,
        "true");
    classify_property(
        progress_bar,
        "ToolTip",
        StorageCodec::control_info,
        DefaultKind::string,
        "");
    classify_property(progress_bar, "MaxValue", StorageCodec::control_info, DefaultKind::integer, "100");
    std::ranges::find(progress_bar, "MaxValue", &PropertyDescriptor::api_name)->value_codec = ValueCodec::integer32;
    classify_property(progress_bar, "MinValue", StorageCodec::control_info, DefaultKind::integer, "0");
    std::ranges::find(progress_bar, "MinValue", &PropertyDescriptor::api_name)->value_codec = ValueCodec::integer32;
    classify_property(progress_bar, "Step", StorageCodec::control_info, DefaultKind::integer, "1");
    std::ranges::find(progress_bar, "Step", &PropertyDescriptor::api_name)->value_codec = ValueCodec::integer32;

    auto& track_bar = properties[static_cast<std::size_t>(ControlKind::track_bar)];
    classify_property(
        track_bar,
        "Enabled",
        StorageCodec::control_info,
        DefaultKind::boolean,
        "true");
    classify_property(
        track_bar,
        "ToolTip",
        StorageCodec::control_info,
        DefaultKind::string,
        "");
    classify_property(track_bar, "MaxValue", StorageCodec::control_info, DefaultKind::integer, "100");
    std::ranges::find(track_bar, "MaxValue", &PropertyDescriptor::api_name)->value_codec = ValueCodec::integer32;
    classify_property(track_bar, "MinValue", StorageCodec::control_info, DefaultKind::integer, "0");
    std::ranges::find(track_bar, "MinValue", &PropertyDescriptor::api_name)->value_codec = ValueCodec::integer32;
    classify_property(track_bar, "Step", StorageCodec::control_info, DefaultKind::integer, "1");
    std::ranges::find(track_bar, "Step", &PropertyDescriptor::api_name)->value_codec = ValueCodec::integer32;

    auto& input_field = properties[static_cast<std::size_t>(ControlKind::input_field)];
    classify_property(input_field, "ToolTip", StorageCodec::control_base, DefaultKind::string, "");
    classify_property(input_field, "Format", StorageCodec::control_info, DefaultKind::string, "");
    classify_property(input_field, "HorizontalAlign", StorageCodec::control_info, DefaultKind::enumeration, "Auto");
    classify_property(input_field, "VerticalAlign", StorageCodec::control_info, DefaultKind::enumeration, "Top");
    classify_property(input_field, "ChoiceListHeight", StorageCodec::control_info, DefaultKind::integer, "0");
    std::ranges::find(input_field, "ChoiceListHeight", &PropertyDescriptor::api_name)->value_codec = ValueCodec::integer32;
    classify_property(
        input_field,
        "AutoChoiceIncomplete",
        StorageCodec::control_info,
        DefaultKind::boolean,
        "false");
    classify_property(input_field, "Wrap", StorageCodec::control_info, DefaultKind::boolean, "true");
    classify_property(input_field, "ChooseType", StorageCodec::control_info, DefaultKind::boolean, "true");
    classify_property(input_field, "MarkNegatives", StorageCodec::control_info, DefaultKind::boolean, "false");
    classify_property(input_field, "ChoiceButton", StorageCodec::control_info, DefaultKind::boolean, "false");
    classify_property(input_field, "OpenButton", StorageCodec::control_info, DefaultKind::boolean, "false");
    classify_property(input_field, "ClearButton", StorageCodec::control_info, DefaultKind::boolean, "false");
    classify_property(input_field, "SpinButton", StorageCodec::control_info, DefaultKind::boolean, "false");
    classify_property(input_field, "ChoiceListButton", StorageCodec::control_info, DefaultKind::boolean, "false");
    classify_property(input_field, "Transparent", StorageCodec::control_base, DefaultKind::boolean, "false");
    classify_property(input_field, "MultiLine", StorageCodec::control_info, DefaultKind::boolean, "false");
    classify_property(input_field, "ExtendedEdit", StorageCodec::control_info, DefaultKind::boolean, "false");
    classify_property(input_field, "PasswordMode", StorageCodec::control_info, DefaultKind::boolean, "false");
    classify_property(
        input_field,
        "AutoMarkIncomplete",
        StorageCodec::control_info,
        DefaultKind::boolean,
        "false");
    for (const auto property_name : {"ChoiceIncomplete", "MarkIncomplete"}) {
        if (const auto property = std::ranges::find(input_field, property_name, &PropertyDescriptor::api_name);
            property != input_field.end()) {
            property->persistence = PersistenceClass::runtime_only;
            property->storage_codec = StorageCodec::none;
        }
    }

    classify_property(
        panel_placement_properties,
        "Left",
        StorageCodec::position_record,
        DefaultKind::integer,
        "0");
    classify_property(
        panel_placement_properties,
        "Top",
        StorageCodec::position_record,
        DefaultKind::integer,
        "0");
    classify_property(
        panel_placement_properties,
        "Width",
        StorageCodec::position_record,
        DefaultKind::integer,
        "0");
    classify_property(
        panel_placement_properties,
        "Height",
        StorageCodec::position_record,
        DefaultKind::integer,
        "0");
    classify_property(
        panel_placement_properties,
        "Visible",
        StorageCodec::position_record,
        DefaultKind::boolean,
        "true");

    classify_property(
        form_properties,
        "Caption",
        StorageCodec::root_record,
        DefaultKind::string,
        "");
    classify_property(
        form_properties,
        "Width",
        StorageCodec::root_record,
        DefaultKind::integer,
        "400");
    classify_property(
        form_properties,
        "Height",
        StorageCodec::root_record,
        DefaultKind::integer,
        "300");

    auto& button_events = events[static_cast<std::size_t>(ControlKind::button)];
    const auto click = std::ranges::find(
        button_events,
        std::string_view{"Click"},
        &EventDescriptor::api_name);
    if (click == button_events.end()) {
        throw std::logic_error("missing Button.Click event for proven storage override");
    }
    click->persistence = PersistenceClass::persisted_editable;
    click->storage_codec = StorageCodec::event_record;
    click->storage_tag = "e1692cc2-605b-4535-84dd-28440238746c";
}

}  // namespace

struct Metamodel::Impl {
    std::array<ControlDescriptor, control_kind_count> controls{};
    std::array<std::vector<PropertyDescriptor>, control_kind_count> properties;
    std::array<std::vector<EventDescriptor>, control_kind_count> events;
    std::vector<PropertyDescriptor> control_extension_properties;
    std::vector<PropertyDescriptor> panel_placement_properties;
    std::vector<PropertyDescriptor> form_properties;
    std::vector<EventDescriptor> form_events;

    std::unordered_map<std::string_view, const ControlDescriptor*> controls_by_guid;
    std::unordered_map<std::string_view, const ControlDescriptor*> controls_by_public_name;
    std::unordered_map<std::string_view, const ControlDescriptor*> controls_by_api_name;
    std::unordered_map<std::u8string_view, const ControlDescriptor*> controls_by_russian_name;
    std::array<std::unordered_map<std::string_view, const PropertyDescriptor*>, control_kind_count>
        properties_by_name;
    std::array<std::unordered_map<PropertyId, const PropertyDescriptor*, PropertyIdHash>, control_kind_count>
        properties_by_id;
    std::array<std::unordered_map<std::string_view, const EventDescriptor*>, control_kind_count>
        events_by_name;
    std::unordered_map<std::string_view, const PropertyDescriptor*> form_properties_by_name;
    std::unordered_map<PropertyId, const PropertyDescriptor*, PropertyIdHash> form_properties_by_id;
    std::unordered_map<std::string_view, const EventDescriptor*> form_events_by_name;
    MetamodelCoverage coverage;

    Impl() {
        const auto help_names = make_help_control_names();
        std::array<bool, control_kind_count> help_name_seen{};
        for (const auto& help : help_names) {
            const auto index = static_cast<std::size_t>(help.kind);
            if (index >= control_kind_count || help_name_seen[index]) {
                throw std::logic_error("duplicate or invalid ordinary-form help control");
            }
            help_name_seen[index] = true;
            const auto& identity = control_identities[index];
            if (identity.kind != help.kind) {
                throw std::logic_error("ordinary-form control identity order is invalid");
            }
            controls[index] = {
                identity.kind,
                identity.guid,
                identity.storage_tag,
                help.public_name,
                help.api_name,
                help.russian_name,
                identity.version_mask,
                identity.classification,
                identity.child_policy,
            };
        }
        if (!std::ranges::all_of(help_name_seen, [](bool seen) { return seen; })) {
            throw std::logic_error("ordinary-form help catalog does not cover all controls");
        }

        for (auto descriptor : make_control_property_descriptors()) {
            properties[static_cast<std::size_t>(descriptor.control_kind)].push_back(descriptor);
        }
        auto& dendrogram_properties = properties[static_cast<std::size_t>(ControlKind::dendrogram)];
        dendrogram_properties.push_back(PropertyDescriptor{
            PropertyId::from_name("Items"), DescriptorOwner::control,
            PropertySurface::control_payload, ControlKind::dendrogram, 998,
            "Items", "Items", u8"Элементы", u8"ЭлементыДендрограммы",
            ValueKind::collection, ValueCodec::dendrogram_items, ApiAccess::read_write,
            VersionMask::platform_8_5, PersistenceClass::persisted_editable,
            StorageCodec::collection_record, {DefaultKind::none, ""}});
        dendrogram_properties.push_back(PropertyDescriptor{
            PropertyId::from_name("Links"), DescriptorOwner::control,
            PropertySurface::control_payload, ControlKind::dendrogram, 999,
            "Links", "Links", u8"Связи", u8"СвязиДендрограммы",
            ValueKind::collection, ValueCodec::dendrogram_links, ApiAccess::read_write,
            VersionMask::platform_8_5, PersistenceClass::persisted_editable,
            StorageCodec::collection_record, {DefaultKind::none, ""}});
        dendrogram_properties.push_back(PropertyDescriptor{
            PropertyId::from_name("Orientation"), DescriptorOwner::control,
            PropertySurface::control_payload, ControlKind::dendrogram, 1000,
            "Orientation", "Orientation", u8"Ориентация", u8"ОриентацияДендрограммы",
            ValueKind::enumeration, ValueCodec::enumeration, ApiAccess::read_write,
            VersionMask::platform_8_5, PersistenceClass::persisted_editable,
            StorageCodec::value_record, {DefaultKind::enumeration, "DendrogramOrientation.Up"}});
        for (auto descriptor : make_control_event_descriptors()) {
            events[static_cast<std::size_t>(descriptor.control_kind)].push_back(descriptor);
        }
        control_extension_properties = make_control_extension_property_descriptors();
        panel_placement_properties = make_panel_placement_property_descriptors();
        form_properties = make_form_property_descriptors();
        form_events = make_form_event_descriptors();

        apply_proven_storage_overrides(
            properties,
            panel_placement_properties,
            form_properties,
            events);
        auto& chart_properties = properties[static_cast<std::size_t>(ControlKind::chart)];
        // Публичное Title отображает путь Диаграмма.ОбластьЗаголовка.Текст, а не отдельное свойство диаграммы.
        chart_properties.push_back({
            PropertyId::from_name("Title"), DescriptorOwner::control,
            PropertySurface::control_payload, ControlKind::chart, chart_properties.size(), "Title", "TitleArea.Text",
            u8"ОбластьЗаголовка.Текст", u8"Строка", ValueKind::string, ValueCodec::string,
            ApiAccess::read_write, VersionMask::platform_8_5,
            PersistenceClass::persisted_editable, StorageCodec::control_info,
            {DefaultKind::string, ""}});

        // Help may repeat an inherited extension property on one concrete control.
        // The executable model keeps the shared extension as the single owner.
        for (auto& control_properties : properties) {
            for (const auto& shared : control_extension_properties) {
                const auto duplicate = std::ranges::find(
                    control_properties,
                    shared.api_name,
                    &PropertyDescriptor::api_name);
                if (duplicate == control_properties.end()) {
                    continue;
                }
                if (duplicate->xml_name != shared.xml_name ||
                    duplicate->russian_name != shared.russian_name ||
                    duplicate->platform_type != shared.platform_type ||
                    duplicate->value_kind != shared.value_kind ||
                    duplicate->value_codec != shared.value_codec ||
                    duplicate->api_access != shared.api_access ||
                    duplicate->version_mask != shared.version_mask) {
                    throw std::logic_error(
                        "conflicting inherited ordinary-form control property");
                }
                control_properties.erase(duplicate);
            }
        }

        for (std::size_t index = 0; index < control_kind_count; ++index) {
            sort_and_validate_order(properties[index], controls[index].public_name);
            sort_and_validate_order(events[index], controls[index].public_name);
        }
        sort_and_validate_order(control_extension_properties, "Form control extension");
        sort_and_validate_order(panel_placement_properties, "Panel control extension");
        sort_and_validate_order(form_properties, "Form properties");
        sort_and_validate_order(form_events, "Form events");

        for (const auto& descriptor : controls) {
            const auto add_unique = [](auto& index, auto key, const ControlDescriptor* value) {
                if (key.empty() || !index.emplace(key, value).second) {
                    throw std::logic_error("ordinary-form control name/GUID is empty or duplicated");
                }
            };
            add_unique(controls_by_guid, descriptor.guid, &descriptor);
            add_unique(controls_by_public_name, descriptor.public_name, &descriptor);
            add_unique(controls_by_api_name, descriptor.api_name, &descriptor);
            add_unique(controls_by_russian_name, descriptor.russian_name, &descriptor);
        }

        std::set<std::string_view> unique_property_names;
        std::set<std::string_view> unique_event_names;
        std::unordered_map<PropertyId, std::string_view, PropertyIdHash> property_ids;

        const auto index_property = []<typename NameIndex, typename IdIndex>(
                                        NameIndex& name_index,
                                        IdIndex& id_index,
                                        const PropertyDescriptor& descriptor) {
            const auto add_alias = [&](std::string_view name) {
                const auto [position, inserted] = name_index.emplace(name, &descriptor);
                if (!inserted && position->second != &descriptor &&
                    (position->second->id != descriptor.id ||
                     position->second->api_name != descriptor.api_name ||
                     position->second->value_kind != descriptor.value_kind)) {
                    throw std::logic_error("conflicting property name for ordinary-form owner");
                }
            };
            add_alias(descriptor.xml_name);
            add_alias(descriptor.api_name);
            const auto [id_position, id_inserted] = id_index.emplace(descriptor.id, &descriptor);
            if (!id_inserted && id_position->second->api_name != descriptor.api_name) {
                throw std::logic_error("conflicting property ID for ordinary-form owner");
            }
        };

        const auto account_property = [&](const PropertyDescriptor& descriptor) {
            const auto [position, inserted] = property_ids.emplace(descriptor.id, descriptor.api_name);
            if (!inserted && position->second != descriptor.api_name) {
                ++coverage.property_id_collisions;
            }
            if (descriptor.persistence == PersistenceClass::unclassified) {
                ++coverage.unclassified_properties;
            }
            if (descriptor.value_kind == ValueKind::unknown) {
                ++coverage.unknown_value_kinds;
            }
            if (descriptor.value_codec == ValueCodec::unclassified) {
                ++coverage.unclassified_value_codecs;
            }
            if (requires_default(descriptor) &&
                descriptor.default_value.kind == DefaultKind::unknown) {
                ++coverage.unknown_defaults;
            }
            if (requires_storage(descriptor.persistence) &&
                descriptor.storage_codec == StorageCodec::unclassified) {
                ++coverage.missing_storage_codecs;
            }
        };

        const auto index_event = [](auto& index, const EventDescriptor& descriptor) {
            const auto add_alias = [&](std::string_view name) {
                const auto [position, inserted] = index.emplace(name, &descriptor);
                if (!inserted && position->second != &descriptor) {
                    throw std::logic_error("duplicate event name for ordinary-form owner");
                }
            };
            add_alias(descriptor.xml_name);
            add_alias(descriptor.api_name);
        };

        const auto account_event = [&](const EventDescriptor& descriptor) {
            if (descriptor.persistence == PersistenceClass::unclassified) {
                ++coverage.unclassified_events;
            }
            if (requires_storage(descriptor.persistence) &&
                descriptor.storage_codec == StorageCodec::unclassified) {
                ++coverage.missing_storage_codecs;
            }
        };

        coverage.control_count = controls.size();
        for (std::size_t index = 0; index < control_kind_count; ++index) {
            coverage.control_property_occurrences += properties[index].size();
            coverage.control_event_occurrences += events[index].size();
            for (const auto& descriptor : properties[index]) {
                index_property(properties_by_name[index], properties_by_id[index], descriptor);
                account_property(descriptor);
                unique_property_names.insert(descriptor.api_name);
            }
            for (const auto& descriptor : events[index]) {
                index_event(events_by_name[index], descriptor);
                account_event(descriptor);
                unique_event_names.insert(descriptor.api_name);
            }
        }
        coverage.unique_control_property_names = unique_property_names.size();
        coverage.unique_control_event_names = unique_event_names.size();
        coverage.control_extension_property_count = control_extension_properties.size();
        coverage.panel_placement_property_count = panel_placement_properties.size();
        coverage.form_property_count = form_properties.size();
        coverage.form_event_count = form_events.size();
        for (const auto& descriptor : control_extension_properties) {
            account_property(descriptor);
        }
        for (const auto& descriptor : panel_placement_properties) {
            account_property(descriptor);
        }
        for (std::size_t index = 0; index < control_kind_count; ++index) {
            for (const auto& descriptor : control_extension_properties) {
                index_property(properties_by_name[index], properties_by_id[index], descriptor);
            }
            for (const auto& descriptor : panel_placement_properties) {
                index_property(properties_by_name[index], properties_by_id[index], descriptor);
            }
        }
        for (const auto& descriptor : form_properties) {
            index_property(form_properties_by_name, form_properties_by_id, descriptor);
            account_property(descriptor);
        }
        for (const auto& descriptor : form_events) {
            index_event(form_events_by_name, descriptor);
            account_event(descriptor);
        }
        coverage.release_ready = coverage.control_count == control_kind_count &&
                                 coverage.property_id_collisions == 0 &&
                                 coverage.unclassified_properties == 0 &&
                                 coverage.unclassified_events == 0 &&
                                 coverage.unknown_value_kinds == 0 &&
                                 coverage.unclassified_value_codecs == 0 &&
                                 coverage.unknown_defaults == 0 &&
                                 coverage.missing_storage_codecs == 0;
    }
};

Metamodel::Metamodel() : impl_(std::make_unique<Impl>()) {}

Metamodel::~Metamodel() = default;

const Metamodel& Metamodel::instance() {
    static const Metamodel metamodel;
    return metamodel;
}

std::span<const ControlDescriptor> Metamodel::controls() const noexcept {
    return impl_->controls;
}

std::span<const PropertyDescriptor> Metamodel::form_properties() const noexcept {
    return impl_->form_properties;
}

std::span<const EventDescriptor> Metamodel::form_events() const noexcept {
    return impl_->form_events;
}

std::span<const PropertyDescriptor> Metamodel::control_extension_properties() const noexcept {
    return impl_->control_extension_properties;
}

std::span<const PropertyDescriptor> Metamodel::panel_placement_properties() const noexcept {
    return impl_->panel_placement_properties;
}

std::span<const PropertyDescriptor> Metamodel::properties_for(ControlKind kind) const noexcept {
    const auto index = static_cast<std::size_t>(kind);
    return index < control_kind_count ? std::span<const PropertyDescriptor>(impl_->properties[index])
                                      : std::span<const PropertyDescriptor>{};
}

std::span<const EventDescriptor> Metamodel::events_for(ControlKind kind) const noexcept {
    const auto index = static_cast<std::size_t>(kind);
    return index < control_kind_count ? std::span<const EventDescriptor>(impl_->events[index])
                                      : std::span<const EventDescriptor>{};
}

const ControlDescriptor& Metamodel::control(ControlKind kind) const {
    const auto index = static_cast<std::size_t>(kind);
    if (index >= control_kind_count) {
        throw std::out_of_range("unknown ordinary-form control kind");
    }
    return impl_->controls[index];
}

const ControlDescriptor* Metamodel::control_by_guid(std::string_view guid) const noexcept {
    const auto found = impl_->controls_by_guid.find(guid);
    return found == impl_->controls_by_guid.end() ? nullptr : found->second;
}

const ControlDescriptor* Metamodel::control_by_public_name(std::string_view name) const noexcept {
    const auto found = impl_->controls_by_public_name.find(name);
    return found == impl_->controls_by_public_name.end() ? nullptr : found->second;
}

const ControlDescriptor* Metamodel::control_by_api_name(std::string_view name) const noexcept {
    const auto found = impl_->controls_by_api_name.find(name);
    return found == impl_->controls_by_api_name.end() ? nullptr : found->second;
}

const ControlDescriptor* Metamodel::control_by_russian_name(std::u8string_view name) const noexcept {
    const auto found = impl_->controls_by_russian_name.find(name);
    return found == impl_->controls_by_russian_name.end() ? nullptr : found->second;
}

const PropertyDescriptor* Metamodel::property(
    ControlKind kind,
    std::string_view name) const noexcept {
    const auto index = static_cast<std::size_t>(kind);
    if (index >= control_kind_count) {
        return nullptr;
    }
    const auto found = impl_->properties_by_name[index].find(name);
    return found == impl_->properties_by_name[index].end() ? nullptr : found->second;
}

const PropertyDescriptor* Metamodel::form_property(std::string_view name) const noexcept {
    const auto found = impl_->form_properties_by_name.find(name);
    return found == impl_->form_properties_by_name.end() ? nullptr : found->second;
}

const PropertyDescriptor* Metamodel::property(ControlKind kind, PropertyId id) const noexcept {
    const auto index = static_cast<std::size_t>(kind);
    if (index >= control_kind_count) {
        return nullptr;
    }
    const auto found = impl_->properties_by_id[index].find(id);
    return found == impl_->properties_by_id[index].end() ? nullptr : found->second;
}

const PropertyDescriptor* Metamodel::form_property(PropertyId id) const noexcept {
    const auto found = impl_->form_properties_by_id.find(id);
    return found == impl_->form_properties_by_id.end() ? nullptr : found->second;
}

const EventDescriptor* Metamodel::event(ControlKind kind, std::string_view name) const noexcept {
    const auto index = static_cast<std::size_t>(kind);
    if (index >= control_kind_count) {
        return nullptr;
    }
    const auto found = impl_->events_by_name[index].find(name);
    return found == impl_->events_by_name[index].end() ? nullptr : found->second;
}

const EventDescriptor* Metamodel::form_event(std::string_view name) const noexcept {
    const auto found = impl_->form_events_by_name.find(name);
    return found == impl_->form_events_by_name.end() ? nullptr : found->second;
}

const MetamodelCoverage& Metamodel::coverage() const noexcept {
    return impl_->coverage;
}

std::span<const ControlDescriptor> control_descriptors() noexcept {
    return Metamodel::instance().controls();
}

std::span<const PropertyDescriptor> form_property_descriptors() noexcept {
    return Metamodel::instance().form_properties();
}

std::span<const EventDescriptor> form_event_descriptors() noexcept {
    return Metamodel::instance().form_events();
}

std::span<const PropertyDescriptor> control_extension_property_descriptors() noexcept {
    return Metamodel::instance().control_extension_properties();
}

std::span<const PropertyDescriptor> panel_placement_property_descriptors() noexcept {
    return Metamodel::instance().panel_placement_properties();
}

std::span<const PropertyDescriptor> property_descriptors(ControlKind kind) noexcept {
    return Metamodel::instance().properties_for(kind);
}

std::span<const EventDescriptor> event_descriptors(ControlKind kind) noexcept {
    return Metamodel::instance().events_for(kind);
}

std::span<const StandardPictureDescriptor> standard_picture_descriptors() noexcept {
    return standard_picture_catalog;
}

const StandardPictureDescriptor* find_standard_picture(std::string_view runtime_name) noexcept {
    const auto found = std::ranges::find(standard_picture_catalog, runtime_name,
        &StandardPictureDescriptor::runtime_name);
    return found == standard_picture_catalog.end() ? nullptr : &*found;
}

const StandardPictureDescriptor* find_standard_picture_by_guid(std::string_view guid) noexcept {
    if (guid.empty()) return nullptr;
    const auto found = std::ranges::find(standard_picture_catalog, guid,
        &StandardPictureDescriptor::guid);
    return found == standard_picture_catalog.end() ? nullptr : &*found;
}

const StandardPictureDescriptor* find_standard_picture_by_storage_id(std::int32_t storage_id) noexcept {
    if (storage_id >= 0) return nullptr;
    const auto found = std::ranges::find(standard_picture_catalog, storage_id,
        &StandardPictureDescriptor::storage_id);
    return found == standard_picture_catalog.end() ? nullptr : &*found;
}

const ControlDescriptor& descriptor_for(ControlKind kind) {
    return Metamodel::instance().control(kind);
}

const ControlDescriptor* find_by_guid(std::string_view guid) noexcept {
    return Metamodel::instance().control_by_guid(guid);
}

const ControlDescriptor* find_by_public_name(std::string_view name) noexcept {
    return Metamodel::instance().control_by_public_name(name);
}

const ControlDescriptor* find_by_api_name(std::string_view name) noexcept {
    return Metamodel::instance().control_by_api_name(name);
}

const ControlDescriptor* find_by_russian_name(std::u8string_view name) noexcept {
    return Metamodel::instance().control_by_russian_name(name);
}

const PropertyDescriptor* find_property(ControlKind kind, std::string_view name) noexcept {
    return Metamodel::instance().property(kind, name);
}

const PropertyDescriptor* find_form_property(std::string_view name) noexcept {
    return Metamodel::instance().form_property(name);
}

const PropertyDescriptor* find_property(ControlKind kind, PropertyId id) noexcept {
    return Metamodel::instance().property(kind, id);
}

const PropertyDescriptor* find_form_property(PropertyId id) noexcept {
    return Metamodel::instance().form_property(id);
}

const EventDescriptor* find_event(ControlKind kind, std::string_view name) noexcept {
    return Metamodel::instance().event(kind, name);
}

const EventDescriptor* find_form_event(std::string_view name) noexcept {
    return Metamodel::instance().form_event(name);
}

const MetamodelCoverage& metamodel_coverage() noexcept {
    return Metamodel::instance().coverage();
}

std::string_view classification_name(ClassificationStatus status) noexcept {
    switch (status) {
        case ClassificationStatus::platform_resource_backed:
            return "platform-resource-backed";
        case ClassificationStatus::corpus_xsd_resource_correlated:
            return "corpus-xsd-resource-correlated";
        case ClassificationStatus::corpus_xsd_correlated:
            return "corpus-xsd-correlated";
        case ClassificationStatus::platform_ui_guid_table_backed:
            return "platform-ui-guid-table-backed";
        case ClassificationStatus::platform_resource_backed_windows_oracle_pending:
            return "platform-resource-backed-windows-oracle-pending";
        case ClassificationStatus::binary_guid_corpus_xsd_correlated:
            return "binary-guid-corpus-xsd-correlated";
        case ClassificationStatus::windows_harness_required:
            return "windows-harness-required";
    }
    return "unknown";
}

std::string_view persistence_name(PersistenceClass classification) noexcept {
    switch (classification) {
        case PersistenceClass::unclassified:
            return "unclassified";
        case PersistenceClass::persisted_editable:
            return "persisted-editable";
        case PersistenceClass::persisted_readonly:
            return "persisted-readonly";
        case PersistenceClass::runtime_only:
            return "runtime-only";
        case PersistenceClass::version_specific:
            return "version-specific";
        case PersistenceClass::unsupported_by_platform:
            return "unsupported-by-platform";
    }
    return "unclassified";
}

std::string_view storage_codec_name(StorageCodec codec) noexcept {
    switch (codec) {
        case StorageCodec::unclassified:
            return "unclassified";
        case StorageCodec::none:
            return "none";
        case StorageCodec::root_record:
            return "root-record";
        case StorageCodec::control_base:
            return "control-base";
        case StorageCodec::control_info:
            return "control-info";
        case StorageCodec::position_record:
            return "position-record";
        case StorageCodec::binding_record:
            return "binding-record";
        case StorageCodec::event_record:
            return "event-record";
        case StorageCodec::value_record:
            return "value-record";
        case StorageCodec::collection_record:
            return "collection-record";
        case StorageCodec::picture_record:
            return "picture-record";
        case StorageCodec::active_x_state:
            return "active-x-state";
    }
    return "unclassified";
}

std::string_view value_kind_name(ValueKind kind) noexcept {
    switch (kind) {
        case ValueKind::unknown:
            return "unknown";
        case ValueKind::boolean:
            return "boolean";
        case ValueKind::number:
            return "number";
        case ValueKind::string:
            return "string";
        case ValueKind::date_time:
            return "date-time";
        case ValueKind::picture:
            return "picture";
        case ValueKind::color:
            return "color";
        case ValueKind::font:
            return "font";
        case ValueKind::border:
            return "border";
        case ValueKind::shortcut:
            return "shortcut";
        case ValueKind::binary:
            return "binary";
        case ValueKind::identifier:
            return "identifier";
        case ValueKind::collection:
            return "collection";
        case ValueKind::enumeration:
            return "enumeration";
        case ValueKind::object:
            return "object";
        case ValueKind::variant:
            return "variant";
    }
    return "unknown";
}

std::string_view value_codec_name(ValueCodec codec) noexcept {
    switch (codec) {
        case ValueCodec::command_bar_buttons:
            return "command-bar-buttons";
        case ValueCodec::dendrogram_items:
            return "dendrogram-items";
        case ValueCodec::dendrogram_links:
            return "dendrogram-links";
        case ValueCodec::unclassified:
            return "unclassified";
        case ValueCodec::boolean:
            return "boolean";
        case ValueCodec::integer:
            return "integer";
        case ValueCodec::integer32:
            return "integer32";
        case ValueCodec::decimal:
            return "decimal";
        case ValueCodec::string:
            return "string";
        case ValueCodec::localized_string:
            return "localized-string";
        case ValueCodec::formatted_string:
            return "formatted-string";
        case ValueCodec::shortcut:
            return "shortcut";
        case ValueCodec::date:
            return "date";
        case ValueCodec::uuid:
            return "uuid";
        case ValueCodec::composite_id:
            return "composite-id";
        case ValueCodec::type_domain:
            return "type-domain";
        case ValueCodec::enumeration:
            return "enumeration";
        case ValueCodec::color:
            return "color";
        case ValueCodec::font:
            return "font";
        case ValueCodec::picture:
            return "picture";
        case ValueCodec::control_reference:
            return "control-reference";
        case ValueCodec::attribute_reference:
            return "attribute-reference";
        case ValueCodec::command_reference:
            return "command-reference";
    }
    return "unclassified";
}

std::span<const ShortcutKeyDescriptor> shortcut_key_descriptors() noexcept {
    return shortcut_keys;
}

const ShortcutKeyDescriptor* find_shortcut_key(std::string_view name) noexcept {
    const auto found = std::ranges::find(shortcut_keys, name, &ShortcutKeyDescriptor::name);
    return found == shortcut_keys.end() ? nullptr : &*found;
}

}  // namespace oof::model::metamodel
