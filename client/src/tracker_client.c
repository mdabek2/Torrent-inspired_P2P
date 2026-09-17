/**
 * @file tracker_client.c
 * @brief Requests send by client to tracker.
 */

#include "client_header.h"

static bool socketSendRequestHeader(int trackerSocket, char command, uint16_t peerPort);
static bool socketGetFinalResponse(int trackerSocket);

/**
 * Sends torrent metadata to the tracker and registers a shared resource.
 *
 * @param trackerIP IPv6 address of the tracker.
 * @param trackerPort TCP port of the tracker.
 * @param peerPort TCP port on which this peer accepts P2P requests.
 * @param torrentPath Path to the torrent metadata file.
 * @return true if the tracker confirms the request, otherwise false.
 */
bool shareTorrent(const char* trackerIP, uint16_t trackerPort, uint16_t peerPort, const char* torrentPath)
{
    if (trackerIP == NULL || torrentPath == NULL || *torrentPath == '\0')
        return false;

    FILE* fp = fopen(torrentPath, "rb");
    if (fp == NULL)
    {
        perror("Unable to open torrent file");
        return false;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        perror("fseek() error");
        fclose(fp);
        return false;
    }

    long size = ftell(fp);
    if (size <= 0 || (unsigned long)size > P2P_MAX_TORRENT_SIZE)
    {
        fprintf(stderr, "Invalid torrent size: %ld bytes.\n", size);
        fclose(fp);
        return false;
    }
    rewind(fp);

    int trackerSocket = connectIPv6Socket(trackerIP, trackerPort);
    if (trackerSocket < 0)
    {
        perror("Unable to connect to tracker");
        fclose(fp);
        return false;
    }

    if (!socketSendRequestHeader(trackerSocket, TRACKER_CMD_SHARE_FILE, peerPort))
    {
        fclose(fp);
        close(trackerSocket);
        return false;
    }

    char response[512];
    if (socketReceiveLine(trackerSocket, response, sizeof(response)) <= 0)
    {
        fprintf(stderr, "Tracker did not confirm registration request.\n");
        fclose(fp);
        close(trackerSocket);
        return false;
    }
    printf("Tracker: %s", response);

    uint32_t networkFileSize = htonl((uint32_t)size);
    if (!socketSendAll(trackerSocket, &networkFileSize, sizeof(networkFileSize)))
    {
        fclose(fp);
        close(trackerSocket);
        return false;
    }

    char buffer[P2P_DATA_BUFFER_SIZE];
    size_t bytesRead;
    while ((bytesRead = fread(buffer, 1, sizeof(buffer), fp)) > 0)
    {
        if (!socketSendAll(trackerSocket, buffer, bytesRead))
        {
            fclose(fp);
            close(trackerSocket);
            return false;
        }
    }

    bool readOk = !ferror(fp);
    fclose(fp);

    if (!readOk)
    {
        fprintf(stderr, "Error while reading torrent file.\n");
        close(trackerSocket);
        return false;
    }

    bool ok = socketGetFinalResponse(trackerSocket);
    close(trackerSocket);
    return ok;
}

/**
 * Requests metadata of a resource from the tracker.
 *
 * @param trackerIP IPv6 address of the tracker.
 * @param trackerPort TCP port of the tracker.
 * @param peerPort TCP port of the requesting peer.
 * @param resourcename Name of the requested resource.
 * @param outputPath Path where received metadata will be saved.
 * @return true if metadata was received and stored successfully, otherwise false.
 */
bool requestMetadata(const char* trackerIP, uint16_t trackerPort, uint16_t peerPort, const char* resourcename, const char* outputPath)
{
    if (trackerIP == NULL || resourcename == NULL || *resourcename == '\0' || outputPath == NULL || *outputPath == '\0')
        return false;

    int trackerSocket = connectIPv6Socket(trackerIP, trackerPort);
    if (trackerSocket < 0)
    {
        perror("Unable to connect to tracker");
        return false;
    }

    if (!socketSendRequestHeader(trackerSocket, TRACKER_CMD_DOWNLOAD_FILE, peerPort))
    {
        close(trackerSocket);
        return false;
    }

    char response[512];
    if (socketReceiveLine(trackerSocket, response, sizeof(response)) <= 0)
    {
        fprintf(stderr, "Tracker did not request a resourcename.\n");
        close(trackerSocket);
        return false;
    }
    printf("Tracker: %s", response);

    if (!socketSendResourcename(trackerSocket, resourcename))
    {
        close(trackerSocket);
        return false;
    }

    if (socketReceiveLine(trackerSocket, response, sizeof(response)) <= 0)
    {
        fprintf(stderr, "Tracker did not respond to download request.\n");
        close(trackerSocket);
        return false;
    }
    printf("Tracker: %s", response);

    if (strncmp(response, "Resource metadata have been sent.", 33) != 0)
    {
        close(trackerSocket);
        return false;
    }

    FILE* output = fopen(outputPath, "wb");
    if (output == NULL)
    {
        perror("Unable to create metadata file");
        close(trackerSocket);
        return false;
    }

    bool ok = true;
    char buffer[P2P_DATA_BUFFER_SIZE];
    while (1)
    {
        ssize_t received = recv(trackerSocket, buffer, sizeof(buffer), 0);
        if (received < 0)
        {
            if (errno == EINTR)
                continue;
            perror("recv() error");
            ok = false;
            break;
        }
        if (received == 0)
            break;

        if (fwrite(buffer, 1, (size_t)received, output) != (size_t)received)
        {
            fprintf(stderr, "Unable to save metadata.\n");
            ok = false;
            break;
        }
    }

    fclose(output);
    close(trackerSocket);

    if (!ok)
    {
        remove(outputPath);
        return false;
    }

    printf("Requested metadata saved to: %s\n", outputPath);
    return true;
}

/**
 * Performs a request that requires updating tracker's seeders list. 
 *
 * @param trackerIP IPv6 address of the tracker.
 * @param trackerPort TCP port of the tracker.
 * @param peerPort TCP port of the requesting peer.
 * @param command Tracker protocol command.
 * @param resourcename Resource name associated with the request.
 * @return true if the tracker reports success, otherwise false.
 */
bool updateSeeders(const char* trackerIP, uint16_t trackerPort, uint16_t peerPort, char command, const char* resourcename)
{
    if (trackerIP == NULL || resourcename == NULL || *resourcename == '\0')
        return false;

    int trackerSocket = connectIPv6Socket(trackerIP, trackerPort);
    if (trackerSocket < 0)
    {
        perror("Unable to connect to tracker");
        return false;
    }

    if (!socketSendRequestHeader(trackerSocket, command, peerPort))
    {
        close(trackerSocket);
        return false;
    }

    char response[512];
    if (socketReceiveLine(trackerSocket, response, sizeof(response)) <= 0)
    {
        fprintf(stderr, "Tracker did not request a resourcename.\n");
        close(trackerSocket);
        return false;
    }
    printf("Tracker: %s", response);

    if (!socketSendResourcename(trackerSocket, resourcename)) 
    {
        close(trackerSocket);
        return false;
    }

    bool ok = socketGetFinalResponse(trackerSocket);
    close(trackerSocket);
    return ok;
}

/**
 * Sends the common header used by every tracker request.
 *
 * @param trackerSocket Connected tracker socket.
 * @param command Tracker protocol command.
 * @param peerPort TCP port on which this peer accepts P2P requests.
 * @return true if the complete header was sent, otherwise false.
 */
static bool socketSendRequestHeader(int trackerSocket, char command, uint16_t peerPort)
{
    uint16_t networkPeerPort = htons(peerPort);
    return socketSendAll(trackerSocket, &command, sizeof(command)) && socketSendAll(trackerSocket, &networkPeerPort, sizeof(networkPeerPort));
}

/**
 * Receives, prints and classifies the final text response from the tracker.
 *
 * @param trackerSocket Connected tracker socket.
 * @return true when a non-error response is received, otherwise false.
 */
static bool socketGetFinalResponse(int trackerSocket)
{
    char response[512];
    if (socketReceiveLine(trackerSocket, response, sizeof(response)) <= 0)
    {
        fprintf(stderr, "No response received from tracker.\n");
        return false;
    }

    printf("Tracker: %s", response);
    return strncmp(response, "An error", 8) != 0 && strncmp(response, "I don't have", 12) != 0;
}
