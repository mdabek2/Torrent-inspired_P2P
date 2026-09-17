#include "utils.h"

/**
 * Parses a peer or tracker endpoint in the form `[IPv6]:port`.
 *
 * @param endpoint Endpoint string to parse.
 * @param ipBuffer Destination buffer for the IPv6 address without brackets.
 * @param ipBufferSize Size of the destination IP buffer.
 * @param port Destination variable for the parsed TCP port.
 * @return true if the endpoint is valid, otherwise false.
 */
bool parseIPv6Endpoint(const char* endpoint, char* ipBuffer, size_t ipBufferSize, uint16_t* port)
{
    if (endpoint == NULL || ipBuffer == NULL || ipBufferSize == 0 || port == NULL || endpoint[0] != '[')
        return false;

    const char* closingBracket = strchr(endpoint, ']');
    if (closingBracket == NULL || closingBracket[1] != ':')
        return false;

    size_t ipLength = (size_t)(closingBracket - (endpoint + 1));
    if (ipLength == 0 || ipLength >= ipBufferSize)
        return false;

    memcpy(ipBuffer, endpoint + 1, ipLength);
    ipBuffer[ipLength] = '\0';

    struct in6_addr parsedAddress;
    if (inet_pton(AF_INET6, ipBuffer, &parsedAddress) != 1)
        return false;

    const char* portText = closingBracket + 2;
    if (*portText == '\0')
        return false;

    errno = 0;
    char* endPointer = NULL;
    long parsedPort = strtol(portText, &endPointer, 10);

    if (errno != 0 || parsedPort < 1 || parsedPort > 65535)
        return false;

    while (*endPointer != '\0' && isspace((unsigned char)*endPointer))
        endPointer++;

    if (*endPointer != '\0')
        return false;

    *port = (uint16_t)parsedPort;
    return true;
}

/**
 * Sends the complete data buffer through a TCP socket.
 *
 * Because send() may transmit fewer bytes than requested, the operation is
 * repeated until all bytes are sent or an error occurs.
 *
 * @param socketDescriptor Connected socket descriptor.
 * @param data Pointer to data to send.
 * @param length Number of bytes to send.
 * @return true if the complete buffer was sent, otherwise false.
 */
bool socketSendAll(int socketDescriptor, const void* data, size_t length)
{
    if (data == NULL && length > 0)
        return false;

    const char* buffer = (const char*)data;
    size_t totalSent = 0;

    while (totalSent < length)
    {
        ssize_t sent = send(socketDescriptor, buffer + totalSent, length - totalSent, 0);
        if (sent < 0)
        {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (sent == 0)
            return false;
        totalSent += (size_t)sent;
    }

    return true;
}

/**
 * Sends a resourcename terminated with a newline character.
 *
 * The resourcename is sent as a single text line so that the receiving peer
 * can read it using a line-based receive function.
 *
 * @param socketDescriptor Connected socket descriptor.
 * @param resourcename Resourcename or resource name to send.
 * @return true if the complete resourcename was sent successfully, otherwise false.
 */
bool socketSendResourcename(int socketDescriptor, const char* resourcename)
{
    if (resourcename == NULL || *resourcename == '\0')
        return false;

    char buffer[INPUT_BUFFER_SIZE + 2];

    int length = snprintf(buffer, sizeof(buffer), "%s\n", resourcename);

    if (length < 0 || (size_t)length >= sizeof(buffer))
    {
        fprintf(stderr, "Resourcename is too long.\n");
        return false;
    }

    return socketSendAll(socketDescriptor, buffer, (size_t)length);
}

/**
 * Sends a single status character through a socket.
 *
 * @param socketDescriptor Connected socket descriptor.
 * @param status Status character to send.
 * @return true if the status was sent successfully, otherwise false.
 */
bool socketSendStatus(int socketDescriptor, char status)
{
    ssize_t sent;

    do
        sent = send(socketDescriptor, &status, 1, 0);
    while (sent < 0 && errno == EINTR);

    if (sent != 1)
    {
        if (sent < 0)
            perror("send() error");

        return false;
    }

    return true;
}

/**
 * Receives exactly the requested number of bytes from a TCP socket.
 *
 * @param socketDescriptor Connected socket descriptor.
 * @param data Destination buffer.
 * @param length Number of bytes that must be received.
 * @return true if all bytes were received, otherwise false.
 */
bool socketReceiveBytes(int socketDescriptor, void* data, size_t length)
{
    if (data == NULL && length > 0)
        return false;

    char* buffer = (char*)data;
    size_t totalReceived = 0;

    while (totalReceived < length)
    {
        ssize_t received = recv(socketDescriptor, buffer + totalReceived, length - totalReceived, 0);
        if (received < 0)
        {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (received == 0)
            return false;
        totalReceived += (size_t)received;
    }

    return true;
}

/**
 * Receives one newline-terminated text line from a socket.
 *
 * @param socketDescriptor Connected socket descriptor.
 * @param buffer Destination buffer.
 * @param bufferSize Size of the destination buffer.
 * @return Number of bytes stored, 0 on clean close, or -1 on error/invalid input.
 */
ssize_t socketReceiveLine(int socketDescriptor, char* buffer, size_t bufferSize)
{
    if (buffer == NULL || bufferSize < 2)
        return -1;

    size_t position = 0;

    while (position < bufferSize - 1)
    {
        char character;
        ssize_t received = recv(socketDescriptor, &character, 1, 0);

        if (received < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (received == 0)
            break;

        buffer[position++] = character;
        if (character == '\n')
            break;
    }

    buffer[position] = '\0';
    return (ssize_t)position;
}

/**
 * Creates and connects an IPv6 TCP socket.
 *
 * @param ip IPv6 address of the remote endpoint.
 * @param port TCP port of the remote endpoint.
 * @return Connected socket descriptor on success, otherwise -1.
 */
int connectIPv6Socket(const char* ip, uint16_t port)
{
    if (ip == NULL)
        return -1;

    int socketDescriptor = socket(AF_INET6, SOCK_STREAM, 0);
    if (socketDescriptor < 0)
        return -1;

    struct sockaddr_in6 address;
    memset(&address, 0, sizeof(address));
    address.sin6_family = AF_INET6;
    address.sin6_port = htons(port);

    if (inet_pton(AF_INET6, ip, &address.sin6_addr) != 1 ||
        connect(socketDescriptor, (struct sockaddr*)&address, sizeof(address)) < 0)
    {
        close(socketDescriptor);
        return -1;
    }

    return socketDescriptor;
}
