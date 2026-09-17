/**
 * @file main.c
 * @brief Entry point of the P2P tracker application.
 */

#include "tracker_header.h"

Resource* head = NULL;
int trackerSocket = -1;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * Starts the tracker server.
 *
 * Loads the initial tracker database, creates the IPv6 listening socket and
 * creates a detached worker thread for every accepted client connection.
 *
 * @param argc Number of command-line arguments.
 * @param argv Command-line arguments: tracker endpoint and initial seeder list.
 * @return EXIT_FAILURE if initialization fails; otherwise the server runs until stopped.
 */
int main(int argc, char** argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s [IPv6]:port database_file\n", argv[0]);
        return EXIT_FAILURE;
    }

    char trackerIP[P2P_IPV6_TEXT_MAX + 1];
    uint16_t trackerPort;
    if (!parseIPv6Endpoint(argv[1], trackerIP, sizeof(trackerIP), &trackerPort))
    {
        fprintf(stderr, "Invalid tracker address. Expected format: [IPv6]:port\n");
        return EXIT_FAILURE;
    }

    loadInitialDatabase(argv[2]);
    printf("Loaded tracker database:\n");
    printDatabase();

    if (!createSocket(trackerIP, trackerPort))
    {
        freeDatabase();
        return EXIT_FAILURE;
    }

    printf("Tracker listening on [%s]:%u\n", trackerIP, trackerPort);

    while (1)
    {
        struct sockaddr_in6 clientAddress;
        socklen_t clientAddressLength = sizeof(clientAddress);
        int clientSocket = accept(trackerSocket, (struct sockaddr*)&clientAddress, &clientAddressLength);
        if (clientSocket < 0)
        {
            if (errno == EINTR)
                continue;
            perror("accept() error");
            continue;
        }

        char clientIP[INET6_ADDRSTRLEN];
        if (inet_ntop(AF_INET6, &clientAddress.sin6_addr, clientIP, sizeof(clientIP)) == NULL)
        {
            perror("inet_ntop() error");
            close(clientSocket);
            continue;
        }

        ThreadArgs* args = calloc(1, sizeof(*args));
        if (args == NULL)
        {
            close(clientSocket);
            continue;
        }

        args->clientSocket = clientSocket;
        args->clientIP = strdup(clientIP);
        if (args->clientIP == NULL)
        {
            free(args);
            close(clientSocket);
            continue;
        }

        pthread_t threadID;
        if (pthread_create(&threadID, NULL, serveRequest, args) != 0)
        {
            free(args->clientIP);
            free(args);
            close(clientSocket);
            continue;
        }
        pthread_detach(threadID);
    }

    close(trackerSocket);
    freeDatabase();
    return EXIT_SUCCESS;
}
