// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v21/functional_blocks/der_control.hpp>

#include <ocpp/common/call_types.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/ocpp_enums.hpp>

#include <everest/logging.hpp>
#include <nlohmann/json.hpp>

namespace ocpp::v21 {

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

bool DERControl::is_control_type_supported(v2::DERControlEnum /*control_type*/) const {
    // TODO: Query DCDERCtrlr.ModesSupported and ACDERCtrlr.ModesSupported
    return false;
}

bool DERControl::validate_control_fields(const SetDERControlRequest& /*req*/) const {
    // TODO: Implement R04.FR.16-17 field validation
    return true;
}

void DERControl::handle_set_der_control(ocpp::Call<SetDERControlRequest> call) {
    const auto& request = call.msg;

    SetDERControlResponse response;
    // TODO: Implement R04.FR.01-17
    response.status = v2::DERControlStatusEnum::NotSupported;

    ocpp::CallResult<SetDERControlResponse> call_result(response, call.uniqueId);
    this->context.message_dispatcher.dispatch_call_result(call_result);
}

void DERControl::handle_get_der_control(ocpp::Call<GetDERControlRequest> call) {
    const auto& request = call.msg;

    GetDERControlResponse response;
    // TODO: Implement R04.FR.30-37
    response.status = v2::DERControlStatusEnum::NotFound;

    ocpp::CallResult<GetDERControlResponse> call_result(response, call.uniqueId);
    this->context.message_dispatcher.dispatch_call_result(call_result);
}

void DERControl::handle_clear_der_control(ocpp::Call<ClearDERControlRequest> call) {
    const auto& request = call.msg;

    ClearDERControlResponse response;
    // TODO: Implement R04.FR.40-46
    response.status = v2::DERControlStatusEnum::NotFound;

    ocpp::CallResult<ClearDERControlResponse> call_result(response, call.uniqueId);
    this->context.message_dispatcher.dispatch_call_result(call_result);
}

void DERControl::send_notify_start_stop(const CiString<36>& /*control_id*/, bool /*started*/,
                                         const ocpp::DateTime& /*timestamp*/,
                                         const std::optional<std::vector<CiString<36>>>& /*superseded_ids*/) {
    // TODO: Implement in Task 6
}

void DERControl::send_report(int32_t /*request_id*/, const std::vector<std::string>& /*control_jsons*/) {
    // TODO: Implement in Task 5
}

} // namespace ocpp::v21
