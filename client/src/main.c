/**
 * @file main.c
 * @brief Console interface for the P2P client.
 */

#include "client_header.h"

static void readInput(const char* prompt, char* buffer, size_t bufferSize);
static bool parsePort(const char* text, uint16_t* port);

/**
 * Starts the interactive P2P client application.
 *
 * Parses the tracker endpoint and local peer port, starts the background peer
 * server, and handles the console menu used to share, download and unregister
 * resources.
 *
 * @param argc Number of command-line arguments.
 * @param argv Command-line arguments: tracker endpoint and peer port.
 */
int main(int argc, char** argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s [trackerIPv6]:port peerPort\n", argv[0]);
        return EXIT_FAILURE;
    }

    char trackerIP[IP_BUFFER_SIZE];
    uint16_t trackerPort, peerPort;

    if (!parseIPv6Endpoint(argv[1], trackerIP, sizeof(trackerIP), &trackerPort))
    {
        fprintf(stderr, "Invalid tracker endpoint. Expected format: [IPv6]:port\n");
        return EXIT_FAILURE;
    }

    if (!parsePort(argv[2], &peerPort))
    {
        fprintf(stderr, "Invalid peer port.\n");
        return EXIT_FAILURE;
    }

    pthread_t peerServerThread;
    if (pthread_create(&peerServerThread, NULL, runPeerServer, &peerPort) != 0 || pthread_detach(peerServerThread) != 0)
    {
        fprintf(stderr, "Unable to start peer server.\n");
        return EXIT_FAILURE;
    }

    printf("Tracker: [%s]:%u\nPeer port: %u\n", trackerIP, trackerPort, peerPort);

    char input[INPUT_BUFFER_SIZE];
    char resourcename[INPUT_BUFFER_SIZE]; 
    char resourcePath[INPUT_BUFFER_SIZE], torrentPath[INPUT_BUFFER_SIZE], metadataPath[INPUT_BUFFER_SIZE];

    while (1)
    {
        printf("\nMenu:\n"
               " 1. Share resource\n"
               " 2. Download resource\n"
               " 3. Request file's metadata only\n"
               " 4. Remove this peer from seeders\n"
               " 0. Exit\n"
               "Choice: ");

        if (fgets(input, sizeof(input), stdin) == NULL)
            break;

        switch (atoi(input))
        {
            case 1:
                readInput("Resource name: ", resourcename, sizeof(resourcename));
                readInput("Resource file path: ", resourcePath, sizeof(resourcePath));
                readInput("Torrent metadata path: ", torrentPath, sizeof(torrentPath));

                if (access(resourcePath, R_OK) != 0)
                {
                    perror("Unable to access resource file");
                    break;
                }
                if (!shareTorrent(trackerIP, trackerPort, peerPort, torrentPath))
                {
                    fprintf(stderr, "Torrent registration failed.\n");
                    break;
                }
                if (!registerLocalResource(resourcename, resourcePath))
                    fprintf(stderr, "Warning: resource was registered in tracker but not in local registry.\n");
                break;

            case 2:
                readInput("Resource name: ", resourcename, sizeof(resourcename));
                readInput("Output file path: ", resourcePath, sizeof(resourcePath));
                readInput("Output metadata path [default: download_info.txt]: ", metadataPath, sizeof(metadataPath));
                if (metadataPath[0] == '\0')
                    snprintf(metadataPath, sizeof(metadataPath), "download_info.txt");

                if (!requestMetadata(trackerIP, trackerPort, peerPort, resourcename, metadataPath))
                    break;
                if (!downloadResource(metadataPath, resourcename, resourcePath))
                    break;

                if (!registerLocalResource(resourcename, resourcePath))
                    fprintf(stderr, "Downloaded file could not be added to the local registry.\n");
                else if (!updateSeeders(trackerIP, trackerPort, peerPort, TRACKER_CMD_ADD_SEEDER, resourcename))
                    fprintf(stderr, "Warning: file was downloaded, but the tracker's database was not updated.\n");
                break;

            case 3:
                readInput("Resource name: ", resourcename, sizeof(resourcename));
                readInput("Output metadata path [default: file_metadata.txt]: ", metadataPath, sizeof(metadataPath));
                if (metadataPath[0] == '\0')
                    snprintf(metadataPath, sizeof(metadataPath), "file_metadata.txt");
                requestMetadata(trackerIP, trackerPort, peerPort, resourcename, metadataPath);
                break;

            case 4:
                readInput("Resource name: ", resourcename, sizeof(resourcename));
                if (!updateSeeders(trackerIP, trackerPort, peerPort, TRACKER_CMD_REMOVE_SEEDER, resourcename))
                    fprintf(stderr, "Unable to update tracker's seeders list.\n");
                break;

            case 0:
                clearLocalResources();
                printf("Bye.\n");
                return EXIT_SUCCESS;

            default:
                printf("Unknown option.\n");
                break;
        }
    }

    clearLocalResources();
    return EXIT_SUCCESS;
}

/**
 * Reads one line of text from standard input.
 *
 * @param prompt Text displayed before reading input.
 * @param buffer Destination buffer.
 * @param bufferSize Size of the destination buffer.
 */
static void readInput(const char* prompt, char* buffer, size_t bufferSize)
{
    printf("%s", prompt);
    fflush(stdout);

    if (fgets(buffer, (int)bufferSize, stdin) == NULL)
    {
        buffer[0] = '\0';
        return;
    }

    buffer[strcspn(buffer, "\r\n")] = '\0';
}

/**
 * Parses and validates a TCP port number supplied as text.
 *
 * @param text Null-terminated decimal port number.
 * @param port Destination variable for the parsed port.
 * @return true for a valid port in the range 1-65535, otherwise false.
 */
static bool parsePort(const char* text, uint16_t* port)
{
    if (text == NULL || port == NULL || *text == '\0')
        return false;

    errno = 0;
    char* endPointer = NULL;
    long value = strtol(text, &endPointer, 10);

    if (errno != 0 || *endPointer != '\0' || value < 1 || value > 65535)
        return false;

    *port = (uint16_t)value;
    return true;
}
