/**
 * @file peer_client.c
 * @brief Downloads resources directly from peer seeders.
 */

#include "client_header.h"

static bool downloadFromPeer(const char* peerIP, uint16_t peerPort, const char* resourceName, const char* outputPath);

/**
 * Downloads a resource from one of the seeders listed in a metadata file.
 *
 * @param metadataPath Path to metadata received from the tracker.
 * @param resourceName Name of the resource requested from the peer.
 * @param outputPath Path where the downloaded resource will be stored.
 * @return true when the resource was downloaded successfully, otherwise false.
 */
bool downloadResource(const char* metadataPath, const char* resourceName, const char* outputPath)
{
    if (metadataPath == NULL || resourceName == NULL || *resourceName == '\0' || outputPath == NULL || *outputPath == '\0')
        return false;

    FILE* fp = fopen(metadataPath, "r");
    if (fp == NULL)
    {
        perror("Unable to open download metadata");
        return false;
    }

    char line[INPUT_BUFFER_SIZE];
    bool seedsSection = false, seederFound = false;
    char peerIP[IP_BUFFER_SIZE];
    uint16_t peerPort;

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        line[strcspn(line, "\r\n")] = '\0';

        if (strcmp(line, "seeds:") == 0)
        {
            seedsSection = true;
            continue;
        }

        if (!seedsSection || line[0] == '\0')
            continue;

        if (!parseIPv6Endpoint(line, peerIP, sizeof(peerIP), &peerPort))
        {
            fprintf(stderr, "Ignoring invalid seeder endpoint: %s\n", line);
            continue;
        }

        seederFound = true;
        printf("Trying seeder [%s]:%u...\n", peerIP, peerPort);

        if (downloadFromPeer(peerIP, peerPort, resourceName, outputPath))
        {
            fclose(fp);
            printf("Resource downloaded to: %s\n", outputPath);
            return true;
        }
    }

    fclose(fp);
    fprintf(stderr, seederFound
        ? "Unable to download resource from available seeders.\n"
        : "No seeders available for this resource.\n");
    return false;
}

/**
 * Downloads one resource directly from a selected peer.

 * @param peerIP IPv6 address of the seeding peer.
 * @param peerPort TCP port of the seeding peer.
 * @param resourceName Resource name sent to the peer server.
 * @param outputPath Final path of the downloaded file.
 * @return true if the complete resource was received and saved, otherwise false.
 */
static bool downloadFromPeer(const char* peerIP, uint16_t peerPort, const char* resourceName, const char* outputPath)
{
    int peerSocket = connectIPv6Socket(peerIP, peerPort);
    if (peerSocket < 0)
        return false;

    size_t nameLength = strlen(resourceName);
    if (nameLength == 0 || !socketSendResourcename(peerSocket, resourceName))
    {
        close(peerSocket);
        return false;
    }

    char status;
    if (!socketReceiveBytes(peerSocket, &status, sizeof(status)) || status != PEER_STATUS_FILE_FOUND)
    {
        close(peerSocket);
        return false;
    }

    uint32_t networkFileSize;
    if (!socketReceiveBytes(peerSocket, &networkFileSize, sizeof(networkFileSize)))
    {
        close(peerSocket);
        return false;
    }

    uint32_t fileSize = ntohl(networkFileSize);
    char temporaryPath[INPUT_BUFFER_SIZE + 6];
    int written = snprintf(temporaryPath, sizeof(temporaryPath), "%s.part", outputPath);
    if (written < 0 || (size_t)written >= sizeof(temporaryPath))
    {
        close(peerSocket);
        return false;
    }

    FILE* output = fopen(temporaryPath, "wb");
    if (output == NULL)
    {
        perror("Unable to create output file");
        close(peerSocket);
        return false;
    }

    bool ok = true;
    uint32_t remainingBytes = fileSize;
    char buffer[P2P_DATA_BUFFER_SIZE];

    while (remainingBytes > 0)
    {
        size_t bytesToReceive = remainingBytes < sizeof(buffer) ? (size_t)remainingBytes : sizeof(buffer);
        ssize_t received;
        do {
            received = recv(peerSocket, buffer, bytesToReceive, 0);
        } while (received < 0 && errno == EINTR);

        if (received <= 0 || fwrite(buffer, 1, (size_t)received, output) != (size_t)received)
        {
            ok = false;
            break;
        }
        remainingBytes -= (uint32_t)received;
    }

    fclose(output);
    close(peerSocket);

    if (!ok || rename(temporaryPath, outputPath) != 0)
    {
        if (ok)
            perror("Unable to finalize downloaded file");
        remove(temporaryPath);
        return false;
    }

    return true;
}
