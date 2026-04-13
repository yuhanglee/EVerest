// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v21/functional_blocks/der_control.hpp>

#include <ocpp/common/call_types.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/database_handler.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/evse_manager.hpp>
#include <ocpp/v2/ocpp_enums.hpp>

#include <everest/logging.hpp>
#include <nlohmann/json.hpp>

namespace ocpp::v21 {

using namespace v2;

namespace {

std::string der_control_enum_to_string(DERControlEnum e) {
    return v2::conversions::dercontrol_enum_to_string(e);
}

/// Count how many control-specific fields are populated in the request
int count_populated_control_fields(const SetDERControlRequest& req) {
    int count = 0;
    if (req.curve.has_value())
        count++;
    if (req.enterService.has_value())
        count++;
    if (req.fixedPFAbsorb.has_value())
        count++;
    if (req.fixedPFInject.has_value())
        count++;
    if (req.fixedVar.has_value())
        count++;
    if (req.freqDroop.has_value())
        count++;
    if (req.gradient.has_value())
        count++;
    if (req.limitMaxDischarge.has_value())
        count++;
    return count;
}

/// Get the priority from whichever control field is populated
int32_t get_priority_from_request(const SetDERControlRequest& req) {
    if (req.freqDroop.has_value())
        return req.freqDroop->priority;
    if (req.curve.has_value())
        return req.curve->priority;
    if (req.enterService.has_value())
        return req.enterService->priority;
    if (req.fixedPFAbsorb.has_value())
        return req.fixedPFAbsorb->priority;
    if (req.fixedPFInject.has_value())
        return req.fixedPFInject->priority;
    if (req.fixedVar.has_value())
        return req.fixedVar->priority;
    if (req.gradient.has_value())
        return req.gradient->priority;
    if (req.limitMaxDischarge.has_value())
        return req.limitMaxDischarge->priority;
    return 0;
}

/// Get optional startTime from whichever control field is populated
std::optional<std::string> get_start_time_from_request(const SetDERControlRequest& req) {
    if (req.freqDroop.has_value() && req.freqDroop->startTime.has_value())
        return req.freqDroop->startTime->to_rfc3339();
    if (req.curve.has_value() && req.curve->startTime.has_value())
        return req.curve->startTime->to_rfc3339();
    if (req.fixedPFAbsorb.has_value() && req.fixedPFAbsorb->startTime.has_value())
        return req.fixedPFAbsorb->startTime->to_rfc3339();
    if (req.fixedPFInject.has_value() && req.fixedPFInject->startTime.has_value())
        return req.fixedPFInject->startTime->to_rfc3339();
    if (req.fixedVar.has_value() && req.fixedVar->startTime.has_value())
        return req.fixedVar->startTime->to_rfc3339();
    if (req.limitMaxDischarge.has_value() && req.limitMaxDischarge->startTime.has_value())
        return req.limitMaxDischarge->startTime->to_rfc3339();
    // EnterService and Gradient don't have startTime
    return std::nullopt;
}

/// Get optional duration from whichever control field is populated
std::optional<float> get_duration_from_request(const SetDERControlRequest& req) {
    if (req.freqDroop.has_value() && req.freqDroop->duration.has_value())
        return req.freqDroop->duration;
    if (req.curve.has_value() && req.curve->duration.has_value())
        return req.curve->duration;
    if (req.fixedPFAbsorb.has_value() && req.fixedPFAbsorb->duration.has_value())
        return req.fixedPFAbsorb->duration;
    if (req.fixedPFInject.has_value() && req.fixedPFInject->duration.has_value())
        return req.fixedPFInject->duration;
    if (req.fixedVar.has_value() && req.fixedVar->duration.has_value())
        return req.fixedVar->duration;
    if (req.limitMaxDischarge.has_value() && req.limitMaxDischarge->duration.has_value())
        return req.limitMaxDischarge->duration;
    return std::nullopt;
}

/// Check if startTime or duration is set in any control field
bool has_start_time_or_duration(const SetDERControlRequest& req) {
    return get_start_time_from_request(req).has_value() || get_duration_from_request(req).has_value();
}

/// R04.FR.50-56: Validate yUnit for curve-based controlTypes
bool validate_yunit(DERControlEnum control_type, const DERCurve& curve) {
    auto unit = curve.yUnit;
    switch (control_type) {
    // R04.FR.50: FreqWatt → PctMaxW or PctWAvail
    case DERControlEnum::FreqWatt:
        return unit == DERUnitEnum::PctMaxW || unit == DERUnitEnum::PctWAvail;

    // R04.FR.51: Trip curves → Not_Applicable
    case DERControlEnum::HFMustTrip:
    case DERControlEnum::HFMayTrip:
    case DERControlEnum::HVMustTrip:
    case DERControlEnum::HVMomCess:
    case DERControlEnum::HVMayTrip:
    case DERControlEnum::LFMustTrip:
    case DERControlEnum::LVMustTrip:
    case DERControlEnum::LVMomCess:
    case DERControlEnum::LVMayTrip:
    case DERControlEnum::PowerMonitoringMustTrip:
        return unit == DERUnitEnum::Not_Applicable;

    // R04.FR.52: VoltVar → PctMaxVar or PctVarAvail
    case DERControlEnum::VoltVar:
        return unit == DERUnitEnum::PctMaxVar || unit == DERUnitEnum::PctVarAvail;

    // R04.FR.53: VoltWatt → PctMaxW or PctWAvail
    case DERControlEnum::VoltWatt:
        return unit == DERUnitEnum::PctMaxW || unit == DERUnitEnum::PctWAvail;

    // R04.FR.54: WattPF → Not_Applicable
    case DERControlEnum::WattPF:
        return unit == DERUnitEnum::Not_Applicable;

    // R04.FR.55: WattVar → PctMaxVar or PctVarAvail
    case DERControlEnum::WattVar:
        return unit == DERUnitEnum::PctMaxVar || unit == DERUnitEnum::PctVarAvail;

    // Non-curve types — no yUnit validation needed
    case DERControlEnum::EnterService:
    case DERControlEnum::FreqDroop:
    case DERControlEnum::FixedPFAbsorb:
    case DERControlEnum::FixedPFInject:
    case DERControlEnum::FixedVar:
    case DERControlEnum::Gradients:
    case DERControlEnum::LimitMaxDischarge:
        return true;
    }
    return true;
}

} // anonymous namespace

DERControl::DERControl(const v2::FunctionalBlockContext& context) : context(context) {
    // Start periodic check for expired scheduled controls (every 30 seconds)
    this->scheduled_control_timer.interval([this]() { this->check_scheduled_controls(); },
                                            std::chrono::seconds(30));
}

DERControl::~DERControl() {
    this->scheduled_control_timer.stop();
}

void DERControl::handle_message(const ocpp::EnhancedMessage<v2::MessageType>& message) {
    const auto& json_message = message.message;
    if (message.messageType == v2::MessageType::SetDERControl) {
        this->handle_set_der_control(json_message);
    } else if (message.messageType == v2::MessageType::GetDERControl) {
        this->handle_get_der_control(json_message);
    } else if (message.messageType == v2::MessageType::ClearDERControl) {
        this->handle_clear_der_control(json_message);
    } else {
        throw v2::MessageTypeNotImplementedException(message.messageType);
    }
}

bool DERControl::is_control_type_supported(v2::DERControlEnum control_type) const {
    const auto control_type_str = der_control_enum_to_string(control_type);
    const auto& evse_manager = this->context.evse_manager;

    for (int32_t evse_id = 1; evse_id <= static_cast<int32_t>(evse_manager.get_number_of_evses()); evse_id++) {
        // Check DC DER controller
        auto dc_modes_cv = DERComponentVariables::get_dc_component_variable(evse_id, DERComponentVariables::ModesSupported);
        auto dc_modes = this->context.device_model.get_optional_value<std::string>(dc_modes_cv);
        if (dc_modes.has_value()) {
            // Parse comma-separated list
            auto modes_str = dc_modes.value();
            if (modes_str.find(control_type_str) != std::string::npos) {
                return true;
            }
        }

        // Check AC DER controller
        auto ac_modes_cv = DERComponentVariables::get_ac_component_variable(evse_id, DERComponentVariables::ModesSupported);
        auto ac_modes = this->context.device_model.get_optional_value<std::string>(ac_modes_cv);
        if (ac_modes.has_value()) {
            auto modes_str = ac_modes.value();
            if (modes_str.find(control_type_str) != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

bool DERControl::validate_control_fields(const SetDERControlRequest& req) const {
    // R04.FR.17: Reject if multiple control fields are set
    if (count_populated_control_fields(req) != 1) {
        return false;
    }

    // R04.FR.16: Validate that the correct field is set for the controlType
    switch (req.controlType) {
    case DERControlEnum::FixedPFAbsorb:
        return req.fixedPFAbsorb.has_value();
    case DERControlEnum::FixedPFInject:
        return req.fixedPFInject.has_value();
    case DERControlEnum::FixedVar:
        return req.fixedVar.has_value();
    case DERControlEnum::LimitMaxDischarge:
        // R04.FR.56: If powerMonitoringMustTrip curve is present, its yUnit must be Not_Applicable
        if (req.limitMaxDischarge.has_value() && req.limitMaxDischarge->powerMonitoringMustTrip.has_value()) {
            if (!validate_yunit(DERControlEnum::PowerMonitoringMustTrip,
                                req.limitMaxDischarge->powerMonitoringMustTrip.value())) {
                return false;
            }
        }
        return req.limitMaxDischarge.has_value();
    case DERControlEnum::FreqDroop:
        return req.freqDroop.has_value();
    case DERControlEnum::EnterService:
        return req.enterService.has_value();
    case DERControlEnum::Gradients:
        return req.gradient.has_value();
    // All curve-based types require the curve field + R04.FR.50-55 yUnit validation
    case DERControlEnum::FreqWatt:
    case DERControlEnum::HFMustTrip:
    case DERControlEnum::HFMayTrip:
    case DERControlEnum::HVMustTrip:
    case DERControlEnum::HVMomCess:
    case DERControlEnum::HVMayTrip:
    case DERControlEnum::LFMustTrip:
    case DERControlEnum::LVMustTrip:
    case DERControlEnum::LVMomCess:
    case DERControlEnum::LVMayTrip:
    case DERControlEnum::PowerMonitoringMustTrip:
    case DERControlEnum::VoltVar:
    case DERControlEnum::VoltWatt:
    case DERControlEnum::WattPF:
    case DERControlEnum::WattVar:
        return req.curve.has_value() && validate_yunit(req.controlType, req.curve.value());
    }
    return false;
}

void DERControl::handle_set_der_control(ocpp::Call<SetDERControlRequest> call) {
    const auto& request = call.msg;
    SetDERControlResponse response;
    const auto control_type_str = der_control_enum_to_string(request.controlType);

    // R04.FR.01: Check if controlType is supported
    if (!this->is_control_type_supported(request.controlType)) {
        response.status = DERControlStatusEnum::NotSupported;
        ocpp::CallResult<SetDERControlResponse> call_result(response, call.uniqueId);
        this->context.message_dispatcher.dispatch_call_result(call_result);
        return;
    }

    // R04.FR.16-17: Validate control fields match controlType
    if (!this->validate_control_fields(request)) {
        response.status = DERControlStatusEnum::Rejected;
        ocpp::CallResult<SetDERControlResponse> call_result(response, call.uniqueId);
        this->context.message_dispatcher.dispatch_call_result(call_result);
        return;
    }

    // R04.FR.13: Default controls cannot have startTime or duration
    if (request.isDefault && has_start_time_or_duration(request)) {
        response.status = DERControlStatusEnum::Rejected;
        ocpp::CallResult<SetDERControlResponse> call_result(response, call.uniqueId);
        this->context.message_dispatcher.dispatch_call_result(call_result);
        return;
    }

    // R04.FR.15: EnterService and Gradients can only be default (not scheduled)
    if (!request.isDefault &&
        (request.controlType == DERControlEnum::EnterService || request.controlType == DERControlEnum::Gradients)) {
        response.status = DERControlStatusEnum::Rejected;
        ocpp::CallResult<SetDERControlResponse> call_result(response, call.uniqueId);
        this->context.message_dispatcher.dispatch_call_result(call_result);
        return;
    }

    const int32_t priority = get_priority_from_request(request);
    const auto start_time = get_start_time_from_request(request);
    const auto duration = get_duration_from_request(request);

    // Query existing controls of the same type and isDefault
    auto existing_jsons = this->context.database_handler.get_der_controls_matching_criteria(
        std::optional<bool>(request.isDefault), std::optional<std::string>(control_type_str), std::nullopt);

    std::vector<CiString<36>> superseded_ids;

    // R04.FR.02-08: Handle priority-based superseding for existing controls
    for (const auto& existing_json : existing_jsons) {
        try {
            auto existing = json::parse(existing_json);
            auto existing_id = existing.at("controlId").get<std::string>();
            auto existing_priority = existing.at("priority").get<int32_t>();

            // Lower priority value = higher priority (R04.FR.03, R04.FR.06-08)
            if (priority <= existing_priority && existing_id != request.controlId.get()) {
                // New control supersedes existing
                this->context.database_handler.update_der_control_superseded(existing_id, true);
                superseded_ids.emplace_back(existing_id);
            }
        } catch (const json::exception& e) {
            EVLOG_warning << "Failed to parse existing DER control JSON: " << e.what();
        }
    }

    // Serialize the full request as the control JSON for storage
    json control_json;
    control_json["controlId"] = request.controlId.get();
    control_json["controlType"] = control_type_str;
    control_json["isDefault"] = request.isDefault;
    control_json["priority"] = priority;
    if (start_time.has_value()) {
        control_json["startTime"] = start_time.value();
    }
    if (duration.has_value()) {
        control_json["duration"] = duration.value();
    }
    // Store the original request payload
    control_json["request"] = json(request);

    // Persist to database
    this->context.database_handler.insert_or_update_der_control(
        request.controlId.get(), request.isDefault, control_type_str, false, priority, start_time, duration,
        control_json.dump());

    response.status = DERControlStatusEnum::Accepted;
    if (!superseded_ids.empty()) {
        response.supersededIds = superseded_ids;
    }

    ocpp::CallResult<SetDERControlResponse> call_result(response, call.uniqueId);
    this->context.message_dispatcher.dispatch_call_result(call_result);

    // R04.FR.20: If this is a scheduled control and startTime <= now, notify immediate start
    if (!request.isDefault && start_time.has_value()) {
        auto now = ocpp::DateTime();
        auto control_start = ocpp::DateTime(start_time.value());
        if (control_start <= now) {
            std::optional<std::vector<CiString<36>>> superseded_opt;
            if (!superseded_ids.empty()) {
                superseded_opt = superseded_ids;
            }
            this->send_notify_start_stop(request.controlId, true, now, superseded_opt);
        }
    }
}

void DERControl::handle_get_der_control(ocpp::Call<GetDERControlRequest> call) {
    const auto& request = call.msg;
    GetDERControlResponse response;

    // R04.FR.36: If controlType specified and not supported → NotSupported
    if (request.controlType.has_value() && !this->is_control_type_supported(request.controlType.value())) {
        response.status = DERControlStatusEnum::NotSupported;
        ocpp::CallResult<GetDERControlResponse> call_result(response, call.uniqueId);
        this->context.message_dispatcher.dispatch_call_result(call_result);
        return;
    }

    // Build filter criteria from request
    std::optional<std::string> control_type_filter;
    if (request.controlType.has_value()) {
        control_type_filter = der_control_enum_to_string(request.controlType.value());
    }
    std::optional<std::string> control_id_filter;
    if (request.controlId.has_value()) {
        control_id_filter = request.controlId->get();
    }

    auto matching = this->context.database_handler.get_der_controls_matching_criteria(
        request.isDefault, control_type_filter, control_id_filter);

    // R04.FR.30: No matching controls → NotFound
    if (matching.empty()) {
        response.status = DERControlStatusEnum::NotFound;
        ocpp::CallResult<GetDERControlResponse> call_result(response, call.uniqueId);
        this->context.message_dispatcher.dispatch_call_result(call_result);
        return;
    }

    // R04.FR.33-35, R04.FR.37: Return Accepted and send ReportDERControl
    response.status = DERControlStatusEnum::Accepted;
    ocpp::CallResult<GetDERControlResponse> call_result(response, call.uniqueId);
    this->context.message_dispatcher.dispatch_call_result(call_result);

    // Send report with matching controls
    this->send_report(request.requestId, matching);
}

void DERControl::handle_clear_der_control(ocpp::Call<ClearDERControlRequest> call) {
    const auto& request = call.msg;
    ClearDERControlResponse response;

    // R04.FR.46: If controlId is specified, clear that specific control
    if (request.controlId.has_value()) {
        bool deleted = this->context.database_handler.delete_der_control(request.controlId->get());
        // R04.FR.42: Not found
        response.status = deleted ? DERControlStatusEnum::Accepted : DERControlStatusEnum::NotFound;
        ocpp::CallResult<ClearDERControlResponse> call_result(response, call.uniqueId);
        this->context.message_dispatcher.dispatch_call_result(call_result);
        return;
    }

    // R04.FR.43: If controlType specified and not supported (no controlId) → NotSupported
    if (request.controlType.has_value() && !this->is_control_type_supported(request.controlType.value())) {
        response.status = DERControlStatusEnum::NotSupported;
        ocpp::CallResult<ClearDERControlResponse> call_result(response, call.uniqueId);
        this->context.message_dispatcher.dispatch_call_result(call_result);
        return;
    }

    // R04.FR.44: No controlType, no controlId → clear all matching isDefault
    // R04.FR.45: controlType set, no controlId → clear by type and isDefault
    std::optional<std::string> control_type_filter;
    if (request.controlType.has_value()) {
        control_type_filter = der_control_enum_to_string(request.controlType.value());
    }

    int deleted_count =
        this->context.database_handler.delete_der_controls_matching_criteria(request.isDefault, control_type_filter);

    // R04.FR.41: Nothing found to delete
    response.status = (deleted_count > 0) ? DERControlStatusEnum::Accepted : DERControlStatusEnum::NotFound;

    ocpp::CallResult<ClearDERControlResponse> call_result(response, call.uniqueId);
    this->context.message_dispatcher.dispatch_call_result(call_result);
}

void DERControl::send_notify_start_stop(const CiString<36>& control_id, bool started,
                                         const ocpp::DateTime& timestamp,
                                         const std::optional<std::vector<CiString<36>>>& superseded_ids) {
    NotifyDERStartStopRequest req;
    req.controlId = control_id;
    req.started = started;
    req.timestamp = timestamp;
    req.supersededIds = superseded_ids;

    ocpp::Call<NotifyDERStartStopRequest> call(req);
    this->context.message_dispatcher.dispatch_call(call, false);
}

void DERControl::check_scheduled_controls() {
    // Get all scheduled (non-default) controls
    auto controls = this->context.database_handler.get_der_controls_matching_criteria(
        std::optional<bool>(false), std::nullopt, std::nullopt);

    auto now = ocpp::DateTime();

    for (const auto& control_json_str : controls) {
        try {
            auto control = json::parse(control_json_str);
            auto control_id = control.at("controlId").get<std::string>();
            bool is_superseded = control.value("isSuperseded", false);

            if (is_superseded) {
                continue; // Skip superseded controls
            }

            if (!control.contains("startTime") || !control.contains("duration")) {
                continue; // No expiry possible without both fields
            }

            auto start_str = control.at("startTime").get<std::string>();
            auto duration_seconds = control.at("duration").get<float>();
            auto start_time = ocpp::DateTime(start_str);

            // Calculate expiry: startTime + duration
            auto start_tp = start_time.to_time_point();
            auto expiry_tp = start_tp + std::chrono::seconds(static_cast<int>(duration_seconds));
            auto expiry = ocpp::DateTime(expiry_tp);

            // R04.FR.10: Delete controls past startTime + duration
            // R04.FR.22: Send NotifyDERStartStop with started=false
            if (expiry <= now) {
                this->send_notify_start_stop(CiString<36>(control_id), false, now, std::nullopt);
                this->context.database_handler.delete_der_control(control_id);
                EVLOG_info << "DER control " << control_id << " expired, deleted from database";
            }
        } catch (const json::exception& e) {
            EVLOG_warning << "Failed to parse DER control JSON during scheduled check: " << e.what();
        }
    }
}

namespace {

/// Maximum number of controls per ReportDERControl message. If more controls are
/// present, the report is split into multiple messages using the tbc flag.
constexpr size_t MAX_CONTROLS_PER_REPORT_MESSAGE = 10;

/// Build a ReportDERControlRequest populated with the given controls.
/// Each control JSON is parsed and sorted into the appropriate *Get vector.
ReportDERControlRequest build_report(int32_t request_id, const std::vector<std::string>& control_jsons,
                                      bool tbc) {
    ReportDERControlRequest report;
    report.requestId = request_id;

    std::vector<DERCurveGet> curves;
    std::vector<EnterServiceGet> enter_services;
    std::vector<FixedPFGet> fixed_pf_absorbs;
    std::vector<FixedPFGet> fixed_pf_injects;
    std::vector<FixedVarGet> fixed_vars;
    std::vector<FreqDroopGet> freq_droops;
    std::vector<GradientGet> gradients;
    std::vector<LimitMaxDischargeGet> limit_max_discharges;

    for (const auto& control_json_str : control_jsons) {
        try {
            auto stored = json::parse(control_json_str);
            auto control_id = stored.at("controlId").get<std::string>();
            auto control_type_str = stored.at("controlType").get<std::string>();
            bool is_default = stored.value("isDefault", false);
            bool is_superseded = stored.value("isSuperseded", false);
            auto control_type = v2::conversions::string_to_dercontrol_enum(control_type_str);
            const auto& request_json = stored.at("request");

            // Route each control into the appropriate *Get vector based on type
            switch (control_type) {
            case DERControlEnum::FixedPFAbsorb: {
                FixedPFGet get;
                get.fixedPF = request_json.at("fixedPFAbsorb").get<FixedPF>();
                get.id = control_id;
                get.isDefault = is_default;
                get.isSuperseded = is_superseded;
                fixed_pf_absorbs.push_back(get);
                break;
            }
            case DERControlEnum::FixedPFInject: {
                FixedPFGet get;
                get.fixedPF = request_json.at("fixedPFInject").get<FixedPF>();
                get.id = control_id;
                get.isDefault = is_default;
                get.isSuperseded = is_superseded;
                fixed_pf_injects.push_back(get);
                break;
            }
            case DERControlEnum::FixedVar: {
                FixedVarGet get;
                get.fixedVar = request_json.at("fixedVar").get<FixedVar>();
                get.id = control_id;
                get.isDefault = is_default;
                get.isSuperseded = is_superseded;
                fixed_vars.push_back(get);
                break;
            }
            case DERControlEnum::FreqDroop: {
                FreqDroopGet get;
                get.freqDroop = request_json.at("freqDroop").get<FreqDroop>();
                get.id = control_id;
                get.isDefault = is_default;
                get.isSuperseded = is_superseded;
                freq_droops.push_back(get);
                break;
            }
            case DERControlEnum::EnterService: {
                EnterServiceGet get;
                get.enterService = request_json.at("enterService").get<EnterService>();
                get.id = control_id;
                enter_services.push_back(get);
                break;
            }
            case DERControlEnum::Gradients: {
                GradientGet get;
                get.gradient = request_json.at("gradient").get<Gradient>();
                get.id = control_id;
                gradients.push_back(get);
                break;
            }
            case DERControlEnum::LimitMaxDischarge: {
                LimitMaxDischargeGet get;
                get.limitMaxDischarge = request_json.at("limitMaxDischarge").get<LimitMaxDischarge>();
                get.id = control_id;
                get.isDefault = is_default;
                get.isSuperseded = is_superseded;
                limit_max_discharges.push_back(get);
                break;
            }
            // All curve-based types
            case DERControlEnum::FreqWatt:
            case DERControlEnum::HFMustTrip:
            case DERControlEnum::HFMayTrip:
            case DERControlEnum::HVMustTrip:
            case DERControlEnum::HVMomCess:
            case DERControlEnum::HVMayTrip:
            case DERControlEnum::LFMustTrip:
            case DERControlEnum::LVMustTrip:
            case DERControlEnum::LVMomCess:
            case DERControlEnum::LVMayTrip:
            case DERControlEnum::PowerMonitoringMustTrip:
            case DERControlEnum::VoltVar:
            case DERControlEnum::VoltWatt:
            case DERControlEnum::WattPF:
            case DERControlEnum::WattVar: {
                DERCurveGet get;
                get.curve = request_json.at("curve").get<DERCurve>();
                get.id = control_id;
                get.curveType = control_type;
                get.isDefault = is_default;
                get.isSuperseded = is_superseded;
                curves.push_back(get);
                break;
            }
            }
        } catch (const std::exception& e) {
            EVLOG_warning << "Failed to parse stored DER control for report: " << e.what();
        }
    }

    if (!curves.empty()) {
        report.curve = curves;
    }
    if (!enter_services.empty()) {
        report.enterService = enter_services;
    }
    if (!fixed_pf_absorbs.empty()) {
        report.fixedPFAbsorb = fixed_pf_absorbs;
    }
    if (!fixed_pf_injects.empty()) {
        report.fixedPFInject = fixed_pf_injects;
    }
    if (!fixed_vars.empty()) {
        report.fixedVar = fixed_vars;
    }
    if (!freq_droops.empty()) {
        report.freqDroop = freq_droops;
    }
    if (!gradients.empty()) {
        report.gradient = gradients;
    }
    if (!limit_max_discharges.empty()) {
        report.limitMaxDischarge = limit_max_discharges;
    }

    // R04.FR.32: set tbc=true for all but the last message in a multi-message report
    if (tbc) {
        report.tbc = true;
    }

    return report;
}

} // anonymous namespace

void DERControl::send_report(int32_t request_id, const std::vector<std::string>& control_jsons) {
    // R04.FR.32: Split into multiple messages if we have more than MAX controls
    const size_t total = control_jsons.size();
    if (total == 0) {
        return;
    }

    size_t index = 0;
    while (index < total) {
        size_t chunk_end = std::min(index + MAX_CONTROLS_PER_REPORT_MESSAGE, total);
        bool is_last = (chunk_end == total);

        std::vector<std::string> chunk(control_jsons.begin() + static_cast<long>(index),
                                        control_jsons.begin() + static_cast<long>(chunk_end));

        // R04.FR.31: requestId set in every message; R04.FR.32: tbc=true except for last
        auto report = build_report(request_id, chunk, !is_last);

        ocpp::Call<ReportDERControlRequest> report_call(report);
        this->context.message_dispatcher.dispatch_call(report_call, false);

        index = chunk_end;
    }
}

} // namespace ocpp::v21
