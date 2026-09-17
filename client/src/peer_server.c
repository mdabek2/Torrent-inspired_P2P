/**
 * @file peer_server.c
 * @brief Server side of direct peer-to-peer file transfer.
 */

#include "client_header.h"

static void* servePeer(void* arguments);

/**
 * Runs the peer TCP/IPv6 server in a background thread. The server listens on the peer 
 * port and creates a detached worker thread for each incoming P2P file request.
 *
 * @param arguments Pointer to the uint16_t peer listening port.
 * @return Always NULL when the server thread terminates.
 */
void* runPeerServer(void* arguments)
{
    if (arguments == NULL)
        return NULL;

    uint16_t peerPort = *((uint16_t*)arguments);
    int serverSocket = socket(AF_INET6, SOCK_STREAM, 0);
    if (serverSocket < 0)
    {
        perror("Peer socket() error");
        return NULL;
    }

    int option = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0)
    {
        perror("Peer setsockopt() error");
        close(serverSocket);
        return NULL;
    }

    struct sockaddr_in6 serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin6_family = AF_INET6;
    serverAddress.sin6_port = htons(peerPort);
    serverAddress.sin6_addr = in6addr_any;

    if (bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0 || listen(serverSocket, SOMAXCONN) < 0)
    {
        perror("Unable to start peer server");
        close(serverSocket);
        return NULL;
    }

    printf("Peer server listening on [::]:%u\n", peerPort);

    while (1)
    {
        int peerSocket = accept(serverSocket, NULL, NULL);
        if (peerSocket < 0)
        {
            if (errno == EINTR)
                continue;
            perror("Peer accept() error");
            continue;
        }

        int* socketArgument = malloc(sizeof(*socketArgument));
        if (socketArgument == NULL)
        {
            close(peerSocket);
            continue;
        }
        *socketArgument = peerSocket;

        pthread_t threadID;
        if (pthread_create(&threadID, NULL, servePeer, socketArgument) != 0)
        {
            free(socketArgument);
            close(peerSocket);
            continue;
        }
        pthread_detach(threadID);
    }

    close(serverSocket);
    return NULL;
}

/**
 * Handles one incoming resource request from another peer.
 * 
 * @param arguments Dynamically allocated pointer to the connected socket descriptor.
 * @return Always NULL after the request has been handled.
 */
static void* servePeer(void* arguments)
{
    if (arguments == NULL)
        return NULL;

    int peerSocket = *((int*)arguments);
    free(arguments);

    char resourceName[INPUT_BUFFER_SIZE];
    ssize_t received = socketReceiveLine(peerSocket, resourceName, sizeof(resourceName));
    if (received <= 0)
    {
        close(peerSocket);
        return NULL;
    }
    resourceName[strcspn(resourceName, "\r\n")] = '\0';

    char filepath[INPUT_BUFFER_SIZE];
    if (!getLocalResourcePath(resourceName, filepath, sizeof(filepath)))
    {
        socketSendStatus(peerSocket, PEER_STATUS_FILE_NOT_FOUND);
        close(peerSocket);
        return NULL;
    }

    FILE* fp = fopen(filepath, "rb");
    if (fp == NULL)
    {
        socketSendStatus(peerSocket, PEER_STATUS_FILE_NOT_FOUND);
        close(peerSocket);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        close(peerSocket);
        return NULL;
    }

    long fileSize = ftell(fp);
    if (fileSize < 0 || (unsigned long)fileSize > UINT32_MAX)
    {
        fclose(fp);
        close(peerSocket);
        return NULL;
    }
    rewind(fp);

    uint32_t networkFileSize = htonl((uint32_t)fileSize);
    if (!socketSendStatus(peerSocket, PEER_STATUS_FILE_FOUND) || !socketSendAll(peerSocket, &networkFileSize, sizeof(networkFileSize)))
    {
        fclose(fp);
        close(peerSocket);
        return NULL;
    }

    char buffer[P2P_DATA_BUFFER_SIZE];
    size_t bytesRead;
    while ((bytesRead = fread(buffer, 1, sizeof(buffer), fp)) > 0)
    {
        if (!socketSendAll(peerSocket, buffer, bytesRead))
            break;
    }

    fclose(fp);
    close(peerSocket);
    return NULL;
}
