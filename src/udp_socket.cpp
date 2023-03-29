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

unsigned long
sendUDPPacket(int socket, const UserState &state) {
    std::string hostname{"192.168.1.239"};
    uint16_t port = 5005;

    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(port);
    destination.sin_addr.s_addr = inet_addr(hostname.c_str());

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
