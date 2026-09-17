#pragma once

#include <pthread.h>

#include "protocol.h"
#include "utils.h"

/**
 * Represents one local resource shared/downloaded by the local peer.
 */
typedef struct LocalResource
{
    char name[INPUT_BUFFER_SIZE];
    char path[INPUT_BUFFER_SIZE];
    struct LocalResource* next;
} LocalResource;

/* tracker_client.c */

bool shareTorrent(const char* trackerIP, uint16_t trackerPort, uint16_t peerPort, const char* torrentPath);
bool requestMetadata(const char* trackerIP, uint16_t trackerPort, uint16_t peerPort, const char* resourcename, const char* outputPath);
bool updateSeeders(const char* trackerIP, uint16_t trackerPort, uint16_t peerPort, char command, const char* resourcename);

/* peer_client.c */

bool downloadResource(const char* metadataPath, const char* resourcename, const char* outputPath);

/* peer_server.c */

void* runPeerServer(void* arguments);

/* shared_files.c */

bool registerLocalResource(const char* resourceName, const char* filepath);
bool getLocalResourcePath(const char* resourceName, char* pathBuffer, size_t pathBufferSize);
void clearLocalResources();
