//
// Created by abiel on 8/19/20.
//

#include "MicroDronInterfaceUDP.h"
#include <arpa/inet.h>
#include <cmath>
#include <chrono>
#include <string>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <fmt/format.h>

MicroDronInterfaceUDP::MicroDronInterfaceUDP() {
    mavlink_attitude_t initialAttitude;
    initialAttitude.yaw = 0;
    initialAttitude.pitch = 0;
    initialAttitude.roll = 0;
    attitude.store(initialAttitude);

    std::string addr("192.168.1.46");
    comms = new ESPComms(addr, 14550, 14551, 14552);

//    ret = fcntl(sock, F_SETFL, O_NONBLOCK | FASYNC);
//    if(ret < 0){
//        perror("Nonblocking set failed");
//        close(sock);
//        exit(EXIT_FAILURE);
//    }
    lastHb = std::chrono::high_resolution_clock::now();
    updateThread = std::thread(&MicroDronInterfaceUDP::update, this);
    hbThread = std::thread(&MicroDronInterfaceUDP::hbUpdate, this);
    paramsThread = std::thread(&MicroDronInterfaceUDP::sendAndCheckParams, this);
}

void MicroDronInterfaceUDP::setPitchPid(SimplePID pitchPid) {

}

void MicroDronInterfaceUDP::setRollPid(SimplePID rollPid) {

}

void MicroDronInterfaceUDP::setYawPid(SimplePID yawPid) {

}

void MicroDronInterfaceUDP::setHeightPid(SimplePID heightPid) {

}

void MicroDronInterfaceUDP::sendHeartBeat() {
    //Send hb to drone
    mavlink_message_t msg;
    mavlink_msg_heartbeat_pack(1, MAV_COMP_ID_SYSTEM_CONTROL, &msg, MAV_COMP_ID_AUTOPILOT1, 0, 0, 0,
                               isEmergencyStopped() ? MAV_STATE_EMERGENCY : 0);

    comms->sendMessage(msg);
}

float MicroDronInterfaceUDP::getPitch() const {
    return attitude.load().pitch * 180.0 / M_PI;
}

float MicroDronInterfaceUDP::getRoll() const {
    return attitude.load().roll * 180.0 / M_PI;
}

float MicroDronInterfaceUDP::getYaw() const {
    return attitude.load().yaw * 180.0 / M_PI;
}

float MicroDronInterfaceUDP::getHeight() const {
    return distanceSensor.load().current_distance;
}

int MicroDronInterfaceUDP::getMode() const {
    return 0;
}

float MicroDronInterfaceUDP::getK() const {
    return 0;
}

float MicroDronInterfaceUDP::getMotorOutput1() const {
    return 0;
}

float MicroDronInterfaceUDP::getMotorOutput2() const {
    return 0;
}

float MicroDronInterfaceUDP::getMotorOutput3() const {
    return 0;
}

float MicroDronInterfaceUDP::getMotorOutput4() const {
    return 0;
}

void MicroDronInterfaceUDP::setAllMotorOutput(float output) {

}

void MicroDronInterfaceUDP::setAllMotorOutput(float output1, float output2, float output3, float output4) {

}

void MicroDronInterfaceUDP::setSetpoints(float roll, float pitch, float yaw, float height) {
    std::cout << fmt::format("{}, {}, {}, {}", roll, pitch, yaw, height) << std::endl;
}

void MicroDronInterfaceUDP::setK(float newK) {

}

void MicroDronInterfaceUDP::emergencyStop(bool state) {
    emergencyStopped = state;
    if (emergencyStopped) {
        //std::cout << "E-stopped" << std::endl;
    }
}

bool MicroDronInterfaceUDP::isEmergencyStopped() const {
    return emergencyStopped;
}

bool MicroDronInterfaceUDP::isConnected() const {
    return getHeartbeatTime() < 0.5;
}

float MicroDronInterfaceUDP::getHeartbeatTime() const {
    return std::min(1e3, std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - lastHb).count());
}

SimplePID MicroDronInterfaceUDP::getPitchPid() const {
    return SimplePID();
}

SimplePID MicroDronInterfaceUDP::getRollPid() const {
    return SimplePID();
}

SimplePID MicroDronInterfaceUDP::getYawPid() const {
    return SimplePID();
}

SimplePID MicroDronInterfaceUDP::getHeightPid() const {
    return SimplePID();
}

void MicroDronInterfaceUDP::sendMessage(const mavlink_message_t &msg) {
    comms->sendMessage(msg);
}

void MicroDronInterfaceUDP::requestParamList() {
    mavlink_message_t msg;
    mavlink_msg_param_request_list_pack(201, 2, &msg, 0, 0);
    sendMessage(msg);
}

void MicroDronInterfaceUDP::setParameter(const mavlink_param_set_t &paramSet) {
    auto it = pendingParams.find(paramSet);

    if(it != pendingParams.end()){
        std::unique_lock nextGuard(nextToAccessMutex);
        std::lock_guard guard(updateMutex);

        nextGuard.unlock();

        mavlink_message_t msg;
        mavlink_msg_param_set_encode(201, 2, &msg, &paramSet);
        sendMessage(msg);
        pendingParams[paramSet] = msg;
    }

}

std::map<int, mavlink_param_value_t> &MicroDronInterfaceUDP::getParams() {
    return paramList;
}

//Yaw = R
//Pitch = Y
//Roll = X
//Height = Z
void MicroDronInterfaceUDP::sendJoystickControl(int16_t x, int16_t y, int16_t z, int16_t r) {
    const int16_t maxVal = 1000;
    const int16_t minVal = -1000;

    std::cout << fmt::format("Roll: {}, Pitch: {}, Yaw: {}, Thrust: {}", x, y, r, z) << std::endl;

    mavlink_message_t msg;
    mavlink_msg_manual_control_pack(1, MAV_COMP_ID_SYSTEM_CONTROL, &msg, MAV_COMP_ID_AUTOPILOT1,
                                    x, y,
                                    z, r, 0);

    comms->sendMessage(msg);
}

void MicroDronInterfaceUDP::update() {
    size_t bufLen = MAVLINK_MAX_PACKET_LEN + sizeof(uint64_t);
    uint8_t buf[bufLen];

    while (isRunning) {
        std::lock_guard lowGuard(lowPriorityMutex);
        std::unique_lock nextToAccess(nextToAccessMutex);
        std::lock_guard updateGuard(updateMutex);

        nextToAccess.unlock();


        memset(buf, 0, bufLen);
        auto ret = comms->getMessage();

        if (ret.has_value()) {
            mavlink_message_t &msg(ret.value());
            mavlink_attitude_t new_attitude;
            mavlink_distance_sensor_t new_distanceSensor;
            switch (msg.msgid) {
                case MAVLINK_MSG_ID_ATTITUDE:
                    mavlink_msg_attitude_decode(&msg, &new_attitude);
                    attitude = new_attitude;
                    lastAttUpdateTime = std::chrono::high_resolution_clock::now();
                    break;
                case MAVLINK_MSG_ID_DISTANCE_SENSOR:
                    mavlink_msg_distance_sensor_decode(&msg, &new_distanceSensor);
                    distanceSensor = new_distanceSensor;
                    break;
                case MAVLINK_MSG_ID_HEARTBEAT:
                    lastHb = std::chrono::high_resolution_clock::now();
                    break;
                case MAVLINK_MSG_ID_PARAM_VALUE:
                    mavlink_param_value_t param;
                    mavlink_msg_param_value_decode(&msg, &param);

                    paramList[param.param_index] = param;
                    std::cout << fmt::format("Got param: {} i: {}", param.param_id, param.param_index) << std::endl;
                    break;
                case MAVLINK_MSG_ID_NAMED_VALUE_FLOAT:
                    mavlink_named_value_float_t valueFloat;
                    mavlink_msg_named_value_float_decode(&msg, &valueFloat);

                    bool isMotor; isMotor = false;
                    if(strcasecmp(valueFloat.name, "FrontLeft") == 0){
                        motorValues.frontLeft = valueFloat.value;
                        isMotor = true;
                    } else if(strcasecmp(valueFloat.name, "FrontRight") == 0){
                        motorValues.frontRight = valueFloat.value;
                        isMotor = true;
                    } else if(strcasecmp(valueFloat.name, "BackLeft") == 0){
                        motorValues.backLeft = valueFloat.value;
                        isMotor = true;
                    } else if(strcasecmp(valueFloat.name, "BackRight") == 0){
                        motorValues.backRight = valueFloat.value;
                        isMotor = true;
                    }

                    if(isMotor) lastMotorUpdateTime = std::chrono::high_resolution_clock::now();
                    break;

                default:
                    break;
            }
        }

        //std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

MotorValues MicroDronInterfaceUDP::getMotorValues() const {
    return motorValues;
}

void MicroDronInterfaceUDP::hbUpdate() {
    while (isRunning) {
        sendHeartBeat();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}

void MicroDronInterfaceUDP::sendAndCheckParams() {
    while(isRunning) {


        if (pendingParams.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }else{
            std::unique_lock nextGuard(nextToAccessMutex);
            std::lock_guard guard(updateMutex);

            nextGuard.unlock();

            requestParamList();

            for(const auto& pendingParam : pendingParams){
                for(const auto& param : paramList){
                    if(pendingParam.first.param_id == param.second.param_id ){
                        if(pendingParam.first.param_value != param.second.param_value){
                            sendMessage(pendingParam.second);
                        }else{
                            pendingParams.erase(pendingParam.first);
                        }
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

std::chrono::high_resolution_clock::time_point MicroDronInterfaceUDP::getLastAttUpdateTime() const {
    return lastAttUpdateTime;
}

std::chrono::high_resolution_clock::time_point MicroDronInterfaceUDP::getLastMotorUpdate() const {
    return lastMotorUpdateTime;
}

MicroDronInterfaceUDP::~MicroDronInterfaceUDP() {
    isRunning = false;
    updateThread.join();
}
