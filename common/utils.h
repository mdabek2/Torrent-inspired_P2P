#pragma once

#include <unistd.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

#define INPUT_BUFFER_SIZE   256
#define IP_BUFFER_SIZE      (P2P_IPV6_TEXT_MAX + 1)

bool parseIPv6Endpoint(const char* endpoint, char* ipBuffer, size_t ipBufferSize, uint16_t* port);
bool socketSendAll(int socketDescriptor, const void* data, size_t length);
bool socketSendResourcename(int trackerSocket, const char* resourcename);
bool socketSendStatus(int trackerSocket, char status);
bool socketReceiveBytes(int socketDescriptor, void* data, size_t length);
ssize_t socketReceiveLine(int socketDescriptor, char* buffer, size_t bufferSize);
int connectIPv6Socket(const char* ip, uint16_t port);


