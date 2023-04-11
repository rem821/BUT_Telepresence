//
// Created by stand on 29.03.2023.
//
#include "udp_socket.h"

#include <sys/socket.h>
#include <linux/in.h>
#include <sys/endian.h>
#include <arpa/inet.h>
#include <unistd.h>

int createSocket() {
    return socket(AF_INET, SOCK_DGRAM, 0);
}

void closeSocket(int socket) {
    close(socket);
}

std::string buttonToByte(bool pressed, bool touched) {
    std::bitset<8> button_b;
    button_b[7] = touched;
    button_b[6] = pressed;
    return button_b.to_string();
}

unsigned long sendUDPPacket(int socket, const UserState &state) {
    std::string hostname{"192.168.1.239"};
    uint16_t port = 5005;

    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(port);
    destination.sin_addr.s_addr = inet_addr(hostname.c_str());

    {
        // Head UDP Datagram #HXYZW(binary orientation)XX(CRC placeholder)
        std::bitset<sizeof(float) * 8> x_b, y_b, z_b, w_b;
        std::memcpy(&x_b, &state.hmdPose.orientation.x, sizeof(float));
        std::memcpy(&y_b, &state.hmdPose.orientation.y, sizeof(float));
        std::memcpy(&z_b, &state.hmdPose.orientation.z, sizeof(float));
        std::memcpy(&w_b, &state.hmdPose.orientation.w, sizeof(float));
        std::string x_s = x_b.to_string();
        std::string y_s = y_b.to_string();
        std::string z_s = z_b.to_string();
        std::string w_s = w_b.to_string();

        std::string headDatagram = "#H" + x_s + y_s + z_s + w_s + "XX";
        LOG_INFO("%s", headDatagram.c_str());
    }

    {
        static int pass = 0;
        if (pass > 3) {
            pass = 0;

            {
                // LEFT
                std::bitset<sizeof(float) * 8> x_b, y_b, s_b, t_b;
                std::memcpy(&x_b, &state.thumbstickPose[Side::LEFT].x, sizeof(float));
                std::memcpy(&y_b, &state.thumbstickPose[Side::LEFT].y, sizeof(float));
                std::memcpy(&s_b, &state.squeezeValue[Side::LEFT], sizeof(float));
                std::memcpy(&t_b, &state.triggerValue[Side::LEFT], sizeof(float));
                std::string x = x_b.to_string();
                std::string y = y_b.to_string();
                std::string squeeze = s_b.to_string();
                std::string trigger = t_b.to_string();

                std::string button_thumbstick = buttonToByte(state.thumbstickPressed[Side::LEFT], state.thumbstickTouched[Side::LEFT]);
                std::string button_trigger = buttonToByte(state.triggerValue[Side::LEFT] > 0.8, state.triggerTouched[Side::LEFT]);

                std::string button_x = buttonToByte(state.xPressed, state.xTouched);
                std::string button_y = buttonToByte(state.yPressed, state.yTouched);

                std::string leftControllerDatagram =
                        "#L" + x + y + squeeze + trigger + button_thumbstick + button_trigger + button_x + button_y + "XX";
                LOG_INFO("%s", leftControllerDatagram.c_str());
            }


            {
                // RIGHT
                std::bitset<sizeof(float) * 8> x_b, y_b, s_b, t_b;
                std::memcpy(&x_b, &state.thumbstickPose[Side::RIGHT].x, sizeof(float));
                std::memcpy(&y_b, &state.thumbstickPose[Side::RIGHT].y, sizeof(float));
                std::memcpy(&s_b, &state.squeezeValue[Side::RIGHT], sizeof(float));
                std::memcpy(&t_b, &state.triggerValue[Side::RIGHT], sizeof(float));
                std::string x = x_b.to_string();
                std::string y = y_b.to_string();
                std::string squeeze = s_b.to_string();
                std::string trigger = t_b.to_string();

                std::string button_thumbstick = buttonToByte(state.thumbstickPressed[Side::RIGHT], state.thumbstickTouched[Side::RIGHT]);
                std::string button_trigger = buttonToByte(state.triggerValue[Side::RIGHT] > 0.8, state.triggerTouched[Side::RIGHT]);

                std::string button_a = buttonToByte(state.aPressed, state.aTouched);
                std::string button_b = buttonToByte(state.bPressed, state.bTouched);

                std::string leftControllerDatagram =
                        "#R" + x + y + squeeze + trigger + button_thumbstick + button_trigger + button_a + button_b + "XX";
                LOG_INFO("%s", leftControllerDatagram.c_str());
            }
        }
        pass++;
    }

//    LOG_INFO(
//            "Current state:\nHMD Pose: %f,%f,%f,%f\nController Left Pose: %f,%f,%f,%f\nController Right Pose: %f,%f,%f,%f\nThumbstick Left Pose: %f,%f\nThumbstick Right Pose: %f,%f\nThumbstick Left Pressed: %d\nThumbstick Right Pressed: %d\nThumbstick Left Touched: %d\nThumbstick Right Touched: %d\nA Pressed: %d\nA Touched: %d\nB Pressed: %d\nB Touched: %d\nX Pressed: %d\nX Touched: %d\nY Pressed: %d\nY Touched: %d\nSqueeze Left Value: %f\nSqueeze Right Value: %f\nTrigger Left Value: %f\nTrigger Right Value: %f\nTrigger Left Touched: %d\nTrigger Right Touched: %d\n\n\n\n",
//            state.hmdPose.orientation.x, state.hmdPose.orientation.y, state.hmdPose.orientation.z, state.hmdPose.orientation.w,
//            state.controllerPose[Side::LEFT].orientation.x, state.controllerPose[Side::LEFT].orientation.y,
//            state.controllerPose[Side::LEFT].orientation.z, state.controllerPose[Side::LEFT].orientation.w,
//            state.controllerPose[Side::RIGHT].orientation.x, state.controllerPose[Side::RIGHT].orientation.y,
//            state.controllerPose[Side::RIGHT].orientation.z, state.controllerPose[Side::RIGHT].orientation.w,
//            state.thumbstickPose[Side::LEFT].x, state.thumbstickPose[Side::LEFT].y,
//            state.thumbstickPose[Side::RIGHT].x, state.thumbstickPose[Side::RIGHT].y,
//            state.thumbstickPressed[Side::LEFT], state.thumbstickPressed[Side::RIGHT],
//            state.thumbstickTouched[Side::LEFT], state.thumbstickTouched[Side::RIGHT],
//            state.aPressed, state.aTouched,
//            state.bPressed, state.bTouched,
//            state.xPressed, state.xTouched,
//            state.yPressed, state.yTouched,
//            state.squeezeValue[Side::LEFT], state.squeezeValue[Side::RIGHT],
//            state.triggerValue[Side::LEFT], state.triggerValue[Side::RIGHT],
//            state.triggerTouched[Side::LEFT], state.triggerTouched[Side::RIGHT]
//    );
//    LOG_INFO("Head: H:%d, V:%d, Motors: L:%d, R:%d", s1, s0, left, right);
//    std::string message = Fmt("s0:%03d,s1:%03d,m0:%04d,%01d,m1:%04d,%01d\n", abs(s0),
//                                        abs(s1), abs(left),
//                                        leftDir, abs(right), rightDir);
//
//    size_t n_bytes = sendto(socket, message.c_str(), message.length(), 0,
//                            reinterpret_cast<sockaddr *>(&destination), sizeof(destination));
//
//    return n_bytes;
    return 0;
}
