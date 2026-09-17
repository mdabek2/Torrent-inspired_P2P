/**
 * @file client_service.c
 * @brief Operations requested by clients connected to the tracker.
 */

#include "tracker_header.h"

static char* trimWhitespace(char* str);

/**
 * Registers a resource shared by a connected client.
 *
 * @param clientSocket Socket descriptor of the connected client.
 * @param clientIP IPv6 address of the requesting peer.
 * @param clientPort Listening TCP port of the requesting peer.
 */
void registerResource(int clientSocket, const char* clientIP, uint16_t clientPort)
{
    char* filepath = socketReceiveMetadata(clientSocket, clientIP, clientPort);
    if (filepath == NULL)
    {
        socketSendMessage(clientSocket, "Failed to receive torrent metadata file.\n");
        close(clientSocket);
        return;
    }

    RegisterStatus status = registerResourceFromMetadata(filepath, clientIP, clientPort);
    switch (status)
    {
        case REGISTER_RESOURCE_CREATED:
            socketSendMessage(clientSocket, "Resource was successfully added to database.\n");
            printDatabase();
            break;
        case REGISTER_SEEDER_ADDED:
            socketSendMessage(clientSocket, "Resource already exists. This peer was added as a seeder.\n");
            printDatabase();
            break;
        case REGISTER_SEEDER_ALREADY_PRESENT:
            socketSendMessage(clientSocket, "Resource already exists and this peer is already a seeder.\n");
            break;
        default:
            socketSendMessage(clientSocket, "An error occurred while adding the resource.\n");
            break;
    }

    remove(filepath);
    free(filepath);
    close(clientSocket);
}

/**
 * Prepares and sends metadata required to download a resource.
 *
 * @param clientSocket Socket descriptor of the connected client.
 * @param clientIP IPv6 address of the requesting peer.
 * @param clientPort Listening TCP port of the requesting peer.
 * @param resourcename Requested resource name; surrounding whitespace is removed.
 */
void prepareMetadata(int clientSocket, const char* clientIP, uint16_t clientPort, char* resourcename)
{
    char* trimmedResourcename = trimWhitespace(resourcename);
    char filepath[64];
    snprintf(filepath, sizeof(filepath), "download_info_%d.tmp", clientSocket);

    if (trimmedResourcename == NULL || !writeMetadata(trimmedResourcename, filepath))
    {
        socketSendMessage(clientSocket, "I don't have this resource.\n");
        close(clientSocket);
        return;
    }

    FILE* fp = fopen(filepath, "rb");
    if (fp == NULL)
    {
        socketSendMessage(clientSocket, "An error occurred while preparing resource metadata.\n");
        remove(filepath);
        close(clientSocket);
        return;
    }

    if (socketSendMessage(clientSocket, "Resource metadata have been sent.\n") && !socketSendMetadata(clientSocket, fp))
        fprintf(stderr, "Unable to send metadata for '%s'.\n", trimmedResourcename);

    printf("Metadata for '%s' sent to [%s]:%u.\n", trimmedResourcename, clientIP, clientPort);
    fclose(fp);
    remove(filepath);
    close(clientSocket);
}

/**
 * Removes the requesting peer from a resource's seeder list.
 *
 * @param clientSocket Socket descriptor of the connected client.
 * @param clientIP IPv6 address of the requesting peer.
 * @param clientPort Listening TCP port of the requesting peer.
 * @param resourcename Name of the resource.
 */
void removeSeeder(int clientSocket, const char* clientIP, uint16_t clientPort, char* resourcename)
{
    char* trimmedResourcename = trimWhitespace(resourcename);
    SeederStatus status = removeResourceSeeder(trimmedResourcename, clientIP, clientPort);

    if (status == SEEDER_RESOURCE_NOT_FOUND)
        socketSendMessage(clientSocket, "I don't have this resource.\n");
    else if (status == SEEDER_UPDATED)
    {
        socketSendMessage(clientSocket, "You were successfully removed from this resource's seeders list.\n");
        printDatabase();
    }
    else if (status == SEEDER_UNCHANGED)
        socketSendMessage(clientSocket, "You are not in this resource's seeders list.\n");
    else
        socketSendMessage(clientSocket, "An error occurred while removing the seeder.\n");

    close(clientSocket);
}

/**
 * Registers the requesting peer as a seeder after a successful download.
 *
 * @param clientSocket Socket descriptor of the connected client.
 * @param clientIP IPv6 address of the requesting peer.
 * @param clientPort Listening TCP port of the requesting peer.
 * @param resourcename Name of the downloaded resource.
 */
void updateSeedersList(int clientSocket, const char* clientIP, uint16_t clientPort, char* resourcename)
{
    char* trimmedResourcename = trimWhitespace(resourcename);
    SeederStatus status = addResourceSeeder(trimmedResourcename, clientIP, clientPort);

    if (status == SEEDER_RESOURCE_NOT_FOUND)
        socketSendMessage(clientSocket, "I don't have this resource.\n");
    else if (status == SEEDER_UPDATED)
    {
        socketSendMessage(clientSocket, "You were successfully added to this resource's seeders list.\n");
        printDatabase();
    }
    else if (status == SEEDER_UNCHANGED)
        socketSendMessage(clientSocket, "You already exist on this resource's seeders list.\n");
    else
        socketSendMessage(clientSocket, "An error occurred while updating the seeder list.\n");

    close(clientSocket);
}

/**
 * Removes leading and trailing whitespace from a mutable string in place.
 *
 * @param str String to trim.
 * @return Pointer to the first non-whitespace character, or NULL for NULL input.
 */
static char* trimWhitespace(char* str)
{
    if (str == NULL)
        return NULL;

    while (isspace((unsigned char)*str))
        str++;

    if (*str == '\0')
        return str;

    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
        end--;
    end[1] = '\0';
    return str;
}
