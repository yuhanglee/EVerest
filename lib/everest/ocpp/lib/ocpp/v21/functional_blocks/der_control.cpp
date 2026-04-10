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

} // anonymous namespace

DERControl::DERControl(const v2::FunctionalBlockContext& context) : context(context) {
}

DERControl::~DERControl() = default;

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
        return req.limitMaxDischarge.has_value();
    case DERControlEnum::FreqDroop:
        return req.freqDroop.has_value();
    case DERControlEnum::EnterService:
        return req.enterService.has_value();
    case DERControlEnum::Gradients:
        return req.gradient.has_value();
    // All curve-based types require the curve field
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
        return req.curve.has_value();
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

void DERControl::send_report(int32_t request_id, const std::vector<std::string>& control_jsons) {
    // Build a ReportDERControlRequest from the stored control JSONs
    // R04.FR.31: Set requestId from GetDERControl request
    ReportDERControlRequest report;
    report.requestId = request_id;

    // For now, send a single report message (R04.FR.32: tbc=false for last/only message)
    // TODO: Handle multi-message reports for large result sets

    // Dispatch the report as a CALL (CS→CSMS)
    ocpp::Call<ReportDERControlRequest> report_call(report);
    this->context.message_dispatcher.dispatch_call(report_call, false);
}

} // namespace ocpp::v21
