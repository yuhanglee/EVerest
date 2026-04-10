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
#include <ocpp/v2/ocpp_types.hpp>

#include "component_state_manager_mock.hpp"
#include "connectivity_manager_mock.hpp"
#include "device_model_test_helper.hpp"
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
    DeviceModelTestHelper device_model_test_helper;
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
        device_model_test_helper(),
        mock_dispatcher(),
        device_model(device_model_test_helper.get_device_model()),
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

    // DCDERCtrlr values for EVSE 1 are configured in
    // tests/config/v2/resources/component_config/custom/DCDERCtrlr_1.json
    // Available=true, ModesSupported includes FreqDroop,VoltWatt,LimitMaxDischarge,etc.

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

    SetDERControlRequest make_freq_droop_request(const std::string& control_id, bool is_default, int32_t priority) {
        SetDERControlRequest req;
        req.isDefault = is_default;
        req.controlId = control_id;
        req.controlType = DERControlEnum::FreqDroop;
        FreqDroop fd;
        fd.priority = priority;
        fd.overFreq = 61.0f;
        fd.underFreq = 59.0f;
        fd.overDroop = 5.0f;
        fd.underDroop = 5.0f;
        fd.responseTime = 3.0f;
        req.freqDroop = fd;
        return req;
    }

    SetDERControlRequest make_volt_watt_curve_request(const std::string& control_id, bool is_default,
                                                       int32_t priority) {
        SetDERControlRequest req;
        req.isDefault = is_default;
        req.controlId = control_id;
        req.controlType = DERControlEnum::VoltWatt;
        DERCurve curve;
        curve.priority = priority;
        curve.yUnit = DERUnitEnum::PctMaxW;
        DERCurvePoints p1;
        p1.x = 0.97f;
        p1.y = 100.0f;
        DERCurvePoints p2;
        p2.x = 1.03f;
        p2.y = 0.0f;
        curve.curveData = {p1, p2};
        req.curve = curve;
        return req;
    }
};

// --- Message dispatch tests ---

TEST_F(DERControlTest, HandleMessage_UnknownType_Throws) {
    DERControl der_control(functional_block_context);
    ocpp::EnhancedMessage<MessageType> msg;
    msg.messageType = MessageType::Authorize;
    EXPECT_THROW(der_control.handle_message(msg), MessageTypeNotImplementedException);
}

// --- R04.FR.01: Unsupported controlType returns NotSupported ---

TEST_F(DERControlTest, SetDERControl_UnsupportedType_ReturnsNotSupported) {
    DERControl der_control(functional_block_context);

    SetDERControlRequest req;
    req.isDefault = true;
    req.controlId = "ctrl-unsup";
    req.controlType = DERControlEnum::HFMustTrip; // Not in ModesSupported
    DERCurve curve;
    curve.priority = 0;
    curve.yUnit = DERUnitEnum::Not_Applicable;
    DERCurvePoints p1;
    p1.x = 62.0f;
    p1.y = 1.0f;
    curve.curveData = {p1};
    req.curve = curve;

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::NotSupported);
    }));

    der_control.handle_message(msg);
}

// --- R04.FR.02: New default control accepted ---

TEST_F(DERControlTest, SetDERControl_NewDefault_Accepted) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-default-1", true, 0);
    auto msg = make_set_der_control_msg(req);

    // Expect DB query for existing controls of same type
    EXPECT_CALL(database_handler_mock,
                get_der_controls_matching_criteria(std::optional<bool>(true), std::optional<std::string>("FreqDroop"),
                                                   std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(std::vector<std::string>{})); // No existing

    // Expect DB insert
    EXPECT_CALL(database_handler_mock, insert_or_update_der_control("ctrl-default-1", true, "FreqDroop", false, 0, _,
                                                                     _, _))
        .Times(1);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    der_control.handle_message(msg);
}

// --- R04.FR.13: Default with startTime rejected ---

TEST_F(DERControlTest, SetDERControl_DefaultWithStartTime_Rejected) {
    DERControl der_control(functional_block_context);

    SetDERControlRequest req = make_freq_droop_request("ctrl-bad-default", true, 0);
    req.freqDroop.value().startTime = ocpp::DateTime();

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// --- R04.FR.15: Scheduled EnterService rejected ---

TEST_F(DERControlTest, SetDERControl_ScheduledEnterService_Rejected) {
    DERControl der_control(functional_block_context);

    SetDERControlRequest req;
    req.isDefault = false;
    req.controlId = "ctrl-enter-sched";
    req.controlType = DERControlEnum::EnterService;
    EnterService es;
    es.priority = 0;
    es.highVoltage = 264.0f;
    es.lowVoltage = 211.0f;
    es.highFreq = 62.0f;
    es.lowFreq = 58.0f;
    req.enterService = es;

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// --- R04.FR.15: Scheduled Gradients rejected ---

TEST_F(DERControlTest, SetDERControl_ScheduledGradients_Rejected) {
    DERControl der_control(functional_block_context);

    SetDERControlRequest req;
    req.isDefault = false;
    req.controlId = "ctrl-grad-sched";
    req.controlType = DERControlEnum::Gradients;
    Gradient grad;
    grad.priority = 0;
    grad.gradient = 10.0f;
    grad.softGradient = 5.0f;
    req.gradient = grad;

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// --- R04.FR.16-17: Wrong control field for type → Rejected ---

TEST_F(DERControlTest, SetDERControl_WrongControlField_Rejected) {
    DERControl der_control(functional_block_context);

    // FreqDroop type but providing curve instead of freqDroop field
    SetDERControlRequest req;
    req.isDefault = true;
    req.controlId = "ctrl-wrong-field";
    req.controlType = DERControlEnum::FreqDroop;
    DERCurve curve;
    curve.priority = 0;
    curve.yUnit = DERUnitEnum::PctMaxW;
    DERCurvePoints p1;
    p1.x = 1.0f;
    p1.y = 100.0f;
    curve.curveData = {p1};
    req.curve = curve; // Wrong — should be freqDroop

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// --- R04.FR.16-17: Multiple control fields → Rejected ---

TEST_F(DERControlTest, SetDERControl_MultipleControlFields_Rejected) {
    DERControl der_control(functional_block_context);

    SetDERControlRequest req = make_freq_droop_request("ctrl-multi", true, 0);
    // Also set a curve — two fields populated
    DERCurve curve;
    curve.priority = 0;
    curve.yUnit = DERUnitEnum::PctMaxW;
    DERCurvePoints p1;
    p1.x = 1.0f;
    p1.y = 100.0f;
    curve.curveData = {p1};
    req.curve = curve;

    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Rejected);
    }));

    der_control.handle_message(msg);
}

// --- R04.FR.05: New scheduled control accepted ---

TEST_F(DERControlTest, SetDERControl_NewScheduled_Accepted) {
    DERControl der_control(functional_block_context);

    auto req = make_freq_droop_request("ctrl-sched-1", false, 0);
    req.freqDroop.value().startTime = ocpp::DateTime();
    req.freqDroop.value().duration = 3600.0f;
    auto msg = make_set_der_control_msg(req);

    // No existing scheduled controls
    EXPECT_CALL(database_handler_mock,
                get_der_controls_matching_criteria(std::optional<bool>(false), std::optional<std::string>("FreqDroop"),
                                                   std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(std::vector<std::string>{}));

    EXPECT_CALL(database_handler_mock, insert_or_update_der_control("ctrl-sched-1", false, "FreqDroop", false, 0, _, _,
                                                                     _))
        .Times(1);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    der_control.handle_message(msg);
}

// --- Persistence: verify DB insert is called ---

TEST_F(DERControlTest, SetDERControl_PersistsToDatabase) {
    DERControl der_control(functional_block_context);

    auto req = make_volt_watt_curve_request("ctrl-persist", true, 0);
    auto msg = make_set_der_control_msg(req);

    EXPECT_CALL(database_handler_mock,
                get_der_controls_matching_criteria(std::optional<bool>(true), std::optional<std::string>("VoltWatt"),
                                                   std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(std::vector<std::string>{}));

    // The key assertion: DB insert must be called
    EXPECT_CALL(database_handler_mock,
                insert_or_update_der_control("ctrl-persist", true, "VoltWatt", false, 0, _, _, _))
        .Times(1);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<SetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    der_control.handle_message(msg);
}

// =============================================================================
// GetDERControl tests (R04.FR.30-37)
// =============================================================================

// R04.FR.30 - No matching controls → NotFound
TEST_F(DERControlTest, GetDERControl_NoControls_NotFound) {
    DERControl der_control(functional_block_context);

    GetDERControlRequest req;
    req.requestId = 1;
    req.controlType = DERControlEnum::FreqDroop;

    auto msg = make_get_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{}));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<GetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::NotFound);
    }));

    der_control.handle_message(msg);
}

// R04.FR.36 - Unsupported controlType → NotSupported
TEST_F(DERControlTest, GetDERControl_UnsupportedType_NotSupported) {
    DERControl der_control(functional_block_context);

    GetDERControlRequest req;
    req.requestId = 2;
    req.controlType = DERControlEnum::HFMustTrip; // Not in ModesSupported

    auto msg = make_get_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<GetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::NotSupported);
    }));

    der_control.handle_message(msg);
}

// R04.FR.33 - No filters → returns all, sends ReportDERControl
TEST_F(DERControlTest, GetDERControl_NoFilters_ReturnsAll) {
    DERControl der_control(functional_block_context);

    GetDERControlRequest req;
    req.requestId = 3;

    auto msg = make_get_der_control_msg(req);

    std::string control_json = R"({"controlId":"ctrl-1","controlType":"FreqDroop","isDefault":true,"priority":0,"request":{}})";
    EXPECT_CALL(database_handler_mock, get_der_controls_matching_criteria(_, _, _))
        .WillOnce(Return(std::vector<std::string>{control_json}));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<GetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    // Expect ReportDERControl to be dispatched
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(1);

    der_control.handle_message(msg);
}

// R04.FR.34 - Filter by controlType
TEST_F(DERControlTest, GetDERControl_ByType_ReportsMatching) {
    DERControl der_control(functional_block_context);

    GetDERControlRequest req;
    req.requestId = 4;
    req.controlType = DERControlEnum::FreqDroop;

    auto msg = make_get_der_control_msg(req);

    std::string control_json = R"({"controlId":"ctrl-fd","controlType":"FreqDroop","isDefault":true,"priority":0,"request":{}})";
    EXPECT_CALL(database_handler_mock,
                get_der_controls_matching_criteria(_, std::optional<std::string>("FreqDroop"), _))
        .WillOnce(Return(std::vector<std::string>{control_json}));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<GetDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(1);

    der_control.handle_message(msg);
}

// =============================================================================
// ClearDERControl tests (R04.FR.40-46)
// =============================================================================

// R04.FR.41 - controlType not found → NotFound
TEST_F(DERControlTest, ClearDERControl_TypeNotFound_NotFound) {
    DERControl der_control(functional_block_context);

    ClearDERControlRequest req;
    req.isDefault = true;
    req.controlType = DERControlEnum::FreqDroop;

    auto msg = make_clear_der_control_msg(req);

    EXPECT_CALL(database_handler_mock,
                delete_der_controls_matching_criteria(true, std::optional<std::string>("FreqDroop")))
        .WillOnce(Return(0));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<ClearDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::NotFound);
    }));

    der_control.handle_message(msg);
}

// R04.FR.42 - controlId not found → NotFound
TEST_F(DERControlTest, ClearDERControl_ControlIdNotFound_NotFound) {
    DERControl der_control(functional_block_context);

    ClearDERControlRequest req;
    req.isDefault = true;
    req.controlId = "nonexistent-id";

    auto msg = make_clear_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, delete_der_control("nonexistent-id"))
        .WillOnce(Return(false));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<ClearDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::NotFound);
    }));

    der_control.handle_message(msg);
}

// R04.FR.43 - Unsupported controlType, no controlId → NotSupported
TEST_F(DERControlTest, ClearDERControl_UnsupportedType_NotSupported) {
    DERControl der_control(functional_block_context);

    ClearDERControlRequest req;
    req.isDefault = true;
    req.controlType = DERControlEnum::HFMustTrip; // Not in ModesSupported

    auto msg = make_clear_der_control_msg(req);

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<ClearDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::NotSupported);
    }));

    der_control.handle_message(msg);
}

// R04.FR.44 - No controlType, no controlId → clear all by isDefault
TEST_F(DERControlTest, ClearDERControl_AllByDefault_Accepted) {
    DERControl der_control(functional_block_context);

    ClearDERControlRequest req;
    req.isDefault = true;

    auto msg = make_clear_der_control_msg(req);

    EXPECT_CALL(database_handler_mock,
                delete_der_controls_matching_criteria(true, std::optional<std::string>(std::nullopt)))
        .WillOnce(Return(3));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<ClearDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    der_control.handle_message(msg);
}

// R04.FR.45 - controlType set, no controlId → clear by type and isDefault
TEST_F(DERControlTest, ClearDERControl_ByType_Accepted) {
    DERControl der_control(functional_block_context);

    ClearDERControlRequest req;
    req.isDefault = false;
    req.controlType = DERControlEnum::VoltWatt;

    auto msg = make_clear_der_control_msg(req);

    EXPECT_CALL(database_handler_mock,
                delete_der_controls_matching_criteria(false, std::optional<std::string>("VoltWatt")))
        .WillOnce(Return(2));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<ClearDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    der_control.handle_message(msg);
}

// R04.FR.46 - controlId set → clear specific control
TEST_F(DERControlTest, ClearDERControl_ByControlId_Accepted) {
    DERControl der_control(functional_block_context);

    ClearDERControlRequest req;
    req.isDefault = true;
    req.controlId = "ctrl-to-delete";

    auto msg = make_clear_der_control_msg(req);

    EXPECT_CALL(database_handler_mock, delete_der_control("ctrl-to-delete"))
        .WillOnce(Return(true));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<ClearDERControlResponse>();
        EXPECT_EQ(response.status, DERControlStatusEnum::Accepted);
    }));

    der_control.handle_message(msg);
}
