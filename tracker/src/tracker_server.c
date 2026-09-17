/**
 * @file tracker_server.c
 * @brief Network communication layer of the tracker.
 */

#include "tracker_header.h"

/**
 * Creates and starts the tracker IPv6 TCP server socket.
 *
 * @param trackerIP IPv6 address on which the tracker should listen.
 * @param trackerPort TCP listening port.
 * @return true if the listening socket was created successfully, otherwise false.
 */
bool createSocket(const char* trackerIP, uint16_t trackerPort)
{
    if (trackerIP == NULL)
        return false;

    trackerSocket = socket(AF_INET6, SOCK_STREAM, 0);
    if (trackerSocket < 0)
    {
        perror("socket() error");
        return false;
    }

    int option = 1;
    if (setsockopt(trackerSocket, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0)
    {
        perror("setsockopt() error");
        close(trackerSocket);
        trackerSocket = -1;
        return false;
    }

    struct sockaddr_in6 address;
    memset(&address, 0, sizeof(address));
    address.sin6_family = AF_INET6;
    address.sin6_port = htons(trackerPort);

    if (inet_pton(AF_INET6, trackerIP, &address.sin6_addr) != 1 || bind(trackerSocket, (struct sockaddr*)&address, sizeof(address)) < 0 || listen(trackerSocket, SOMAXCONN) < 0)
    {
        perror("Unable to start tracker socket");
        close(trackerSocket);
        trackerSocket = -1;
        return false;
    }

    return true;
}

/**
 * Sends a complete null-terminated text message to a client.
 *
 * @param clientSocket Connected client socket.
 * @param message Message to send.
 * @return true if the complete message was sent, otherwise false.
 */
bool socketSendMessage(int clientSocket, const char* message)
{
    return message != NULL && socketSendAll(clientSocket, message, strlen(message));
}

/**
 * Handles one tracker request in a worker thread.
 *
 * The function reads the protocol command and peer listening port, dispatches
 * the operation to the appropriate service function, and releases thread data.
 *
 * @param arguments Pointer to ThreadArgs containing the client socket and IPv6 address.
 * @return Always NULL after the request has been handled.
 */
void* serveRequest(void* arguments)
{
    if (arguments == NULL)
        return NULL;

    ThreadArgs* args = (ThreadArgs*)arguments;
    int clientSocket = args->clientSocket;
    char* clientIP = args->clientIP;
    free(args);

    char command;
    uint16_t networkPeerPort;
    if (!socketReceiveBytes(clientSocket, &command, sizeof(command)) ||
        !socketReceiveBytes(clientSocket, &networkPeerPort, sizeof(networkPeerPort)))
    {
        close(clientSocket);
        free(clientIP);
        return NULL;
    }

    uint16_t peerPort = ntohs(networkPeerPort);
    printf("Request %c from [%s]:%u\n", command, clientIP, peerPort);

    char resourcename[RESOURCEN_LENGTH + 2];
    switch (command)
    {
        case TRACKER_CMD_SHARE_FILE:
            if (socketSendMessage(clientSocket, "Adding resource to database.\n"))
                registerResource(clientSocket, clientIP, peerPort);
            else
                close(clientSocket);
            break;

        case TRACKER_CMD_DOWNLOAD_FILE:
            if (!socketSendMessage(clientSocket, "Preparing resource metadata.\n") ||
                socketReceiveLine(clientSocket, resourcename, sizeof(resourcename)) <= 0)
                close(clientSocket);
            else
                prepareMetadata(clientSocket, clientIP, peerPort, resourcename);
            break;

        case TRACKER_CMD_ADD_SEEDER:
            if (!socketSendMessage(clientSocket, "Adding you to resource's seeders list.\n") || socketReceiveLine(clientSocket, resourcename, sizeof(resourcename)) <= 0)
                close(clientSocket);
            else
                updateSeedersList(clientSocket, clientIP, peerPort, resourcename);
            break;

        case TRACKER_CMD_REMOVE_SEEDER:
            if (!socketSendMessage(clientSocket, "Removing you from resource's seeders list.\n") || socketReceiveLine(clientSocket, resourcename, sizeof(resourcename)) <= 0)
                close(clientSocket);
            else
                removeSeeder(clientSocket, clientIP, peerPort, resourcename);
            break;        

        default:
            socketSendMessage(clientSocket, "Unknown command.\n");
            close(clientSocket);
            break;
    }

    free(clientIP);
    return NULL;
}

/**
 * Sends the torret metadata file to a connected client.
 *
 * @param clientSocket Socket connected to the receiving client.
 * @param fp File positioned at the first byte to send.
 * @return true when the whole file was read and sent, otherwise false.
 */
bool socketSendMetadata(int clientSocket, FILE* fp)
{
    if (fp == NULL)
        return false;

    char buffer[P2P_DATA_BUFFER_SIZE];
    size_t bytesRead;
    while ((bytesRead = fread(buffer, 1, sizeof(buffer), fp)) > 0)
    {
        if (!socketSendAll(clientSocket, buffer, bytesRead))
            return false;
    }

    return !ferror(fp);
}

/**
 * Receives a torrent metadata file from a connected peer.
 *
 * The sender first provides a 32-bit file size followed by exactly that many
 * bytes. The received data is stored in a temporary file.
 *
 * @param clientSocket Connected client socket.
 * @param clientIP IPv6 address of the sending peer, used for logging.
 * @param clientPort Listening TCP port of the sending peer, used for logging.
 * @return Dynamically allocated temporary file path on success, otherwise NULL.
 */
char* socketReceiveMetadata(int clientSocket, const char* clientIP, uint16_t clientPort)
{
    if (clientIP == NULL)
        return NULL;

    uint32_t networkFileSize;
    if (!socketReceiveBytes(clientSocket, &networkFileSize, sizeof(networkFileSize)))
        return NULL;

    uint32_t fileSize = ntohl(networkFileSize);
    if (fileSize == 0 || fileSize > P2P_MAX_TORRENT_SIZE)
    {
        fprintf(stderr, "Invalid torrent file size: %u bytes.\n", fileSize);
        return NULL;
    }

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "torrent_%d.tmp", clientSocket);
    FILE* fp = fopen(filepath, "wb");
    if (fp == NULL)
        return NULL;

    bool ok = true;
    uint32_t remaining = fileSize;
    char buffer[P2P_DATA_BUFFER_SIZE];

    while (remaining > 0)
    {
        size_t requested = remaining < sizeof(buffer) ? (size_t)remaining : sizeof(buffer);
        ssize_t received = recv(clientSocket, buffer, requested, 0);
        if (received < 0 && errno == EINTR)
            continue;
        if (received <= 0 || fwrite(buffer, 1, (size_t)received, fp) != (size_t)received)
        {
            ok = false;
            break;
        }
        remaining -= (uint32_t)received;
    }

    fclose(fp);
    if (!ok)
    {
        remove(filepath);
        return NULL;
    }

    char* result = strdup(filepath);
    if (result == NULL)
    {
        remove(filepath);
        return NULL;
    }

    printf("Torrent metadata received from [%s]:%u (%u bytes).\n", clientIP, clientPort, fileSize);
    return result;
}

