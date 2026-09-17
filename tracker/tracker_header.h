#pragma once

#include <netinet/in.h>
#include <pthread.h>

#include "protocol.h"
#include "utils.h"

#define RESOURCEN_LENGTH   40
#define PIECE_HASH_LENGTH   32

#define METADATA_VALUE_RESOURCENAME     1
#define METADATA_VALUE_OWNER_ENDPOINT   2

/**
 * Represents one piece hash stored for a shared resource. 
 */
typedef struct PieceS
{
    char* pieceHash;
    struct PieceS* next;
} Piece;

/**
 *  Represents one peer currently seeding a resource. 
 */
typedef struct SeedS
{
    bool isOriginal;
    char* seedIP;
    uint16_t seedPort;
    struct SeedS* next;
} Seed;

/** 
 * Represents one resource registered in the tracker's database. 
 */
typedef struct ResourceS
{
    char* resourcename;
    Piece* piece_head;
    Seed* seed_head;
    struct ResourceS* next;
} Resource;

/**
 *  Arguments passed to a client tracker thread. 
 */
typedef struct thread_args
{
    int clientSocket;
    char* clientIP;
} ThreadArgs;

/**
 *  Result codes returned while registering resource metadata. 
 */
typedef enum RegisterStatus
{
    REGISTER_ERROR = -1,
    REGISTER_RESOURCE_CREATED = 1,
    REGISTER_SEEDER_ADDED = 2,
    REGISTER_SEEDER_ALREADY_PRESENT = 3
} RegisterStatus;

/**
 *  Result codes returned while updating resource seeder lists. 
 */
typedef enum SeederStatus
{
    SEEDER_ERROR = -1,
    SEEDER_RESOURCE_NOT_FOUND = 0,
    SEEDER_UPDATED = 1,
    SEEDER_UNCHANGED = 2
} SeederStatus;

/** Head of the tracker's in-memory resource list. */
extern Resource* head;
/** Main tracker listening socket descriptor. */
extern int trackerSocket;
/** Mutex protecting the shared tracker database. */
extern pthread_mutex_t lock;

/* resourcelist_handler.c */

void loadInitialDatabase(const char* filepath);
RegisterStatus registerResourceFromMetadata(const char* filepath, const char* clientIP, uint16_t clientPort);
bool writeMetadata(const char* resourcename, const char* filepath);
SeederStatus removeResourceSeeder(const char* resourcename, const char* clientIP, uint16_t clientPort);
SeederStatus addResourceSeeder(const char* resourcename, const char* clientIP, uint16_t clientPort);
void printDatabase(void);
void freeDatabase(void);

/* tracker_server.c */

bool createSocket(const char* trackerIP, uint16_t trackerPort);
void* serveRequest(void* arguments);
bool socketSendMessage(int clientSocket, const char* message);
bool socketSendMetadata(int clientSocket, FILE* fp);
char* socketReceiveMetadata(int clientSocket, const char* clientIP, uint16_t clientPort);

/* client_service.c */

void registerResource(int clientSocket, const char* clientIP, uint16_t clientPort);
void prepareMetadata(int clientSocket, const char* clientIP, uint16_t clientPort, char* resourcename);
void removeSeeder(int clientSocket, const char* clientIP, uint16_t clientPort, char* resourcename);
void updateSeedersList(int clientSocket, const char* clientIP, uint16_t clientPort, char* resourcename);
