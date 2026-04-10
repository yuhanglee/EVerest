// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ocpp/v21/functional_blocks/der_control.hpp>

#include <ocpp/common/call_types.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/functional_blocks/functional_block_context.hpp>
#include <ocpp/v2/ocpp_enums.hpp>

#include "component_state_manager_mock.hpp"
#include "connectivity_manager_mock.hpp"
#include "evse_manager_fake.hpp"
#include "evse_security_mock.hpp"
#include "message_dispatcher_mock.hpp"
#include "mocks/database_handler_mock.hpp"

using namespace ocpp::v2;
using namespace ocpp::v21;
using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

class DERControlTest : public ::testing::Test {
protected:
    MockMessageDispatcher mock_dispatcher;
    DeviceModel* device_model;
    ::testing::NiceMock<ConnectivityManagerMock> connectivity_manager;
    ::testing::NiceMock<DatabaseHandlerMock> database_handler_mock;
    ocpp::EvseSecurityMock evse_security;
    EvseManagerFake evse_manager;
    ComponentStateManagerMock component_state_manager;
    std::atomic<ocpp::OcppProtocolVersion> ocpp_version;
    FunctionalBlockContext functional_block_context;

    DERControlTest() :
        mock_dispatcher(),
        device_model(nullptr),
        connectivity_manager(),
        database_handler_mock(),
        evse_security(),
        evse_manager(1),
        component_state_manager(),
        ocpp_version(ocpp::OcppProtocolVersion::v21),
        functional_block_context{this->mock_dispatcher,       *this->device_model, this->connectivity_manager,
                                 this->evse_manager,          this->database_handler_mock, this->evse_security,
                                 this->component_state_manager, this->ocpp_version} {
    }

    ocpp::EnhancedMessage<MessageType> make_set_der_control_msg(const SetDERControlRequest& req) {
        ocpp::Call<SetDERControlRequest> call(req);
        ocpp::EnhancedMessage<MessageType> enhanced_message;
        enhanced_message.messageType = MessageType::SetDERControl;
        enhanced_message.message = call;
        return enhanced_message;
    }

    ocpp::EnhancedMessage<MessageType> make_get_der_control_msg(const GetDERControlRequest& req) {
        ocpp::Call<GetDERControlRequest> call(req);
        ocpp::EnhancedMessage<MessageType> enhanced_message;
        enhanced_message.messageType = MessageType::GetDERControl;
        enhanced_message.message = call;
        return enhanced_message;
    }

    ocpp::EnhancedMessage<MessageType> make_clear_der_control_msg(const ClearDERControlRequest& req) {
        ocpp::Call<ClearDERControlRequest> call(req);
        ocpp::EnhancedMessage<MessageType> enhanced_message;
        enhanced_message.messageType = MessageType::ClearDERControl;
        enhanced_message.message = call;
        return enhanced_message;
    }
};

// Test that an unknown message type throws MessageTypeNotImplementedException
TEST_F(DERControlTest, HandleMessage_UnknownType_Throws) {
    DERControl der_control(functional_block_context);

    ocpp::EnhancedMessage<MessageType> msg;
    msg.messageType = MessageType::Authorize; // Not a DER message

    EXPECT_THROW(der_control.handle_message(msg), MessageTypeNotImplementedException);
}

// R04.FR.01 - Unsupported controlType returns NotSupported
// With the skeleton, all types are "unsupported" since is_control_type_supported() returns false
TEST_F(DERControlTest, SetDERControl_Skeleton_ReturnsNotSupported) {
    DERControl der_control(functional_block_context);

    SetDERControlRequest req;
    req.isDefault = true;
    req.controlId = "ctrl-1";
    req.controlType = DERControlEnum::FreqDroop;

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::NotSupported);
    }));

    der_control.handle_message(msg);
}

// GetDERControl returns NotFound when no controls exist
TEST_F(DERControlTest, GetDERControl_Skeleton_ReturnsNotFound) {
    DERControl der_control(functional_block_context);

    GetDERControlRequest req;
    req.requestId = 1;

    auto msg = make_get_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<GetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::NotFound);
    }));

    der_control.handle_message(msg);
}

// ClearDERControl returns NotFound when no controls exist
TEST_F(DERControlTest, ClearDERControl_Skeleton_ReturnsNotFound) {
    DERControl der_control(functional_block_context);

    ClearDERControlRequest req;
    req.isDefault = true;

    auto msg = make_clear_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<ClearDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::NotFound);
    }));

    der_control.handle_message(msg);
}
