//
// Created by stand on 29.03.2023.
//
#pragma once

#include "pch.h"
#include "log.h"

int createSocket();

void closeSocket(int socket);

unsigned long sendUDPPacket(int socket, const UserState& state);