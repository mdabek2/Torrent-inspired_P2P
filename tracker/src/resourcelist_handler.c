/**
 * @file resourcelist_handler.c
 * @brief Thread-safe in-memory database of shared resources and seeders.
 */

#include "tracker_header.h"

static Resource* findResource(const char* resourcename);
static Resource* createResource(const char* resourcename);
static Piece* findPiece(Resource* resource, const char* hash);
static Seed* findSeeder(Resource* resource, const char* ip, uint16_t port);
static bool addPiece(Resource* resource, const char* hash);
static bool addSeeder(Resource* resource, const char* ip, uint16_t port, bool isOriginal);
static void removeResource(Resource* resource, Resource* previous);
static void freePieces(Piece* piece);
static void freeSeeders(Seed* seed);
static char* getMetadataValue(const char* line, int mode);
static char* copyString(const char* source, size_t maxLength);
static void trimLine(char* text);

/**
 * Loads the initial tracker database from a file.
 *
 * Resource names, piece hashes, original owners and additional seeders are
 * parsed and inserted into the in-memory linked-list database while holding
 * the database mutex.
 *
 * @param filepath Path to the initial database.
 */
void loadInitialDatabase(const char* filepath)
{
    if (filepath == NULL)
        return;

    FILE* fp = fopen(filepath, "r");
    if (fp == NULL)
    {
        fprintf(stderr, "Warning: unable to open initial database file '%s'. Starting with an empty database.\n", filepath);
        return;
    }

    char* line = NULL;
    size_t capacity = 0;
    char* resourcename = NULL;
    char ownerIP[P2P_IPV6_TEXT_MAX + 1] = {0};
    uint16_t ownerPort = 0;
    bool inPieces = false;
    bool inSeeds = false;

    pthread_mutex_lock(&lock);

    while (getline(&line, &capacity, fp) > 0)
    {
        trimLine(line);

        if (strncmp(line, "4:name", 6) == 0)
        {
            free(resourcename);
            resourcename = getMetadataValue(line, METADATA_VALUE_RESOURCENAME);
            ownerIP[0] = '\0';
            ownerPort = 0;
            inPieces = false;
            inSeeds = false;
            continue;
        }

        if (strncmp(line, "5:owner", 7) == 0)
        {
            char* endpoint = getMetadataValue(line, METADATA_VALUE_OWNER_ENDPOINT);
            if (endpoint != NULL)
            {
                if (!parseIPv6Endpoint(endpoint, ownerIP, sizeof(ownerIP), &ownerPort))
                {
                    ownerIP[0] = '\0';
                    ownerPort = 0;
                }
                free(endpoint);
            }
            continue;
        }

        if (strcmp(line, "6:pieces:") == 0 || strcmp(line, "6:pieces") == 0)
        {
            inPieces = true;
            inSeeds = false;
            continue;
        }

        if (strcmp(line, "7:seeds:") == 0 || strcmp(line, "7:seeds") == 0)
        {
            inPieces = false;
            inSeeds = true;
            continue;
        }

        if (strcmp(line, ";") == 0)
        {
            inPieces = false;
            inSeeds = false;
            continue;
        }

        if (resourcename == NULL)
            continue;

        Resource* resource = findResource(resourcename);
        if (resource == NULL)
            resource = createResource(resourcename);
        if (resource == NULL)
            continue;

        if (ownerIP[0] != '\0' && ownerPort != 0)
            addSeeder(resource, ownerIP, ownerPort, true);

        if (inPieces && line[0] == ':')
            addPiece(resource, line + 1);
        else if (inSeeds && line[0] == '[')
        {
            char seedIP[P2P_IPV6_TEXT_MAX + 1];
            uint16_t seedPort;
            if (parseIPv6Endpoint(line, seedIP, sizeof(seedIP), &seedPort))
                addSeeder(resource, seedIP, seedPort, false);
        }
    }

    pthread_mutex_unlock(&lock);
    free(resourcename);
    free(line);
    fclose(fp);
}

/**
 * Registers torrent metadata received from a peer in the tracker database.
 *
 * If the resource already exists, only the peer is added as another seeder.
 * Otherwise a new resource entry is created with its piece hashes and owner.
 *
 * @param filepath Path to the received metadata file.
 * @param clientIP IPv6 address of the registering peer.
 * @param clientPort Listening TCP port of the registering peer.
 * @return RegisterStatus describing whether a resource/seeder was added or an error occurred.
 */
RegisterStatus registerResourceFromMetadata(const char* filepath, const char* clientIP, uint16_t clientPort)
{
    if (filepath == NULL || clientIP == NULL)
        return REGISTER_ERROR;

    FILE* fp = fopen(filepath, "r");
    if (fp == NULL)
        return REGISTER_ERROR;

    char* line = NULL;
    size_t capacity = 0;
    char* resourcename = NULL;
    bool inPieces = false;
    char** pieces = NULL;
    size_t pieceCount = 0;
    size_t pieceCapacity = 0;

    while (getline(&line, &capacity, fp) > 0)
    {
        trimLine(line);

        if (strncmp(line, "4:name", 6) == 0)
        {
            free(resourcename);
            resourcename = getMetadataValue(line, METADATA_VALUE_RESOURCENAME);
            continue;
        }

        if (strncmp(line, "6:pieces", 8) == 0)
        {
            inPieces = true;
            continue;
        }

        if (!inPieces)
            continue;

        if (line[0] == 'e' || line[0] == ';' || strncmp(line, "7:seeds", 7) == 0)
            break;

        if (line[0] == ':')
        {
            const char* hash = line + 1;
            size_t hashLength = strlen(hash);
            if (hashLength == 0 || hashLength > PIECE_HASH_LENGTH)
                continue;

            if (pieceCount == pieceCapacity)
            {
                size_t newCapacity = pieceCapacity == 0 ? 4 : pieceCapacity * 2;
                char** newPieces = realloc(pieces, newCapacity * sizeof(*newPieces));
                if (newPieces == NULL)
                    break;
                pieces = newPieces;
                pieceCapacity = newCapacity;
            }

            pieces[pieceCount] = strdup(hash);
            if (pieces[pieceCount] != NULL)
                pieceCount++;
        }
    }

    fclose(fp);
    free(line);

    if (resourcename == NULL || pieceCount == 0)
    {
        for (size_t i = 0; i < pieceCount; ++i)
            free(pieces[i]);
        free(pieces);
        free(resourcename);
        return REGISTER_ERROR;
    }

    pthread_mutex_lock(&lock);

    Resource* resource = findResource(resourcename);
    RegisterStatus status;

    if (resource != NULL)
    {
        if (findSeeder(resource, clientIP, clientPort) != NULL)
            status = REGISTER_SEEDER_ALREADY_PRESENT;
        else
        {
            status = addSeeder(resource, clientIP, clientPort, false)
                ? REGISTER_SEEDER_ADDED
                : REGISTER_ERROR;
        }
    }
    else
    {
        resource = createResource(resourcename);
        if (resource == NULL)
            status = REGISTER_ERROR;
        else
        {
            bool ok = true;
            for (size_t i = 0; i < pieceCount; ++i)
                ok = addPiece(resource, pieces[i]) && ok;
            ok = addSeeder(resource, clientIP, clientPort, true) && ok;
            status = ok ? REGISTER_RESOURCE_CREATED : REGISTER_ERROR;
        }
    }

    pthread_mutex_unlock(&lock);

    for (size_t i = 0; i < pieceCount; ++i)
        free(pieces[i]);
    free(pieces);
    free(resourcename);
    return status;
}

/**
 * Writes metadata for a resource to a temporary file.
 *
 * @param resourcename Name of the resource stored in the tracker database.
 * @param filepath Destination metadata file path.
 * @return true if the resource exists and the file was written, otherwise false.
 */
bool writeMetadata(const char* resourcename, const char* filepath)
{
    if (resourcename == NULL || filepath == NULL)
        return false;

    pthread_mutex_lock(&lock);
    Resource* resource = findResource(resourcename);
    if (resource == NULL)
    {
        pthread_mutex_unlock(&lock);
        return false;
    }

    FILE* fp = fopen(filepath, "w");
    if (fp == NULL)
    {
        pthread_mutex_unlock(&lock);
        return false;
    }

    fputs("pieces:\n", fp);
    for (Piece* piece = resource->piece_head; piece != NULL; piece = piece->next)
        fprintf(fp, "%s\n", piece->pieceHash);

    fputs("seeds:\n", fp);
    for (Seed* seed = resource->seed_head; seed != NULL; seed = seed->next)
        fprintf(fp, "[%s]:%u\n", seed->seedIP, seed->seedPort);

    fclose(fp);
    pthread_mutex_unlock(&lock);
    return true;
}

/**
 * Removes one peer from a resource's seeder list.
 *
 * If the removed peer was the original owner, another remaining seeder is
 * promoted. A resource with no remaining seeders is removed from the database.
 *
 * @param resourcename Resource name.
 * @param clientIP IPv6 address of the peer to remove.
 * @param clientPort TCP port of the peer to remove.
 * @return SeederStatus describing the result of the operation.
 */
SeederStatus removeResourceSeeder(const char* resourcename, const char* clientIP, uint16_t clientPort)
{
    if (resourcename == NULL || clientIP == NULL)
        return SEEDER_ERROR;

    pthread_mutex_lock(&lock);

    Resource* resource = head;
    Resource* previousResource = NULL;
    while (resource != NULL && strcmp(resource->resourcename, resourcename) != 0)
    {
        previousResource = resource;
        resource = resource->next;
    }

    if (resource == NULL)
    {
        pthread_mutex_unlock(&lock);
        return SEEDER_RESOURCE_NOT_FOUND;
    }

    Seed* seed = resource->seed_head;
    Seed* previousSeed = NULL;
    while (seed != NULL && (seed->seedPort != clientPort || strcmp(seed->seedIP, clientIP) != 0))
    {
        previousSeed = seed;
        seed = seed->next;
    }

    if (seed == NULL)
    {
        pthread_mutex_unlock(&lock);
        return SEEDER_UNCHANGED;
    }

    bool wasOriginal = seed->isOriginal;
    if (previousSeed == NULL)
        resource->seed_head = seed->next;
    else
        previousSeed->next = seed->next;
    free(seed->seedIP);
    free(seed);

    if (resource->seed_head == NULL)
        removeResource(resource, previousResource);
    else if (wasOriginal)
        resource->seed_head->isOriginal = true;

    pthread_mutex_unlock(&lock);
    return SEEDER_UPDATED;
}

/**
 * Adds a peer to the seeder list of an existing resource.
 *
 * @param resourcename Resource name.
 * @param clientIP IPv6 address of the peer to add.
 * @param clientPort TCP port of the peer to add.
 * @return SeederStatus describing the result of the operation.
 */
SeederStatus addResourceSeeder(const char* resourcename, const char* clientIP, uint16_t clientPort)
{
    if (resourcename == NULL || clientIP == NULL)
        return SEEDER_ERROR;

    pthread_mutex_lock(&lock);
    Resource* resource = findResource(resourcename);
    if (resource == NULL)
    {
        pthread_mutex_unlock(&lock);
        return SEEDER_RESOURCE_NOT_FOUND;
    }

    if (findSeeder(resource, clientIP, clientPort) != NULL)
    {
        pthread_mutex_unlock(&lock);
        return SEEDER_UNCHANGED;
    }

    bool ok = addSeeder(resource, clientIP, clientPort, false);
    pthread_mutex_unlock(&lock);
    return ok ? SEEDER_UPDATED : SEEDER_ERROR;
}

/**
 * Prints the complete in-memory tracker database to standard output.
 * The database mutex is held while resources, piece hashes and seeders are read.
 */
void printDatabase()
{
    pthread_mutex_lock(&lock);

    if (head == NULL)
        printf("  (empty)\n");

    for (Resource* resource = head; resource != NULL; resource = resource->next)
    {
        printf("\n%s:\n", resource->resourcename);
        printf(" pieces:\n");
        for (Piece* piece = resource->piece_head; piece != NULL; piece = piece->next)
            printf("\t%s\n", piece->pieceHash);

        printf(" seeders:\n");
        for (Seed* seed = resource->seed_head; seed != NULL; seed = seed->next)
            printf("\t[%s]:%u%s\n", seed->seedIP, seed->seedPort, seed->isOriginal ? " (original)" : "");
    }

    pthread_mutex_unlock(&lock);
}

/**
 * Frees all resources, pieces and seeders stored in the tracker database.
 */
void freeDatabase(void)
{
    pthread_mutex_lock(&lock);
    while (head != NULL)
    {
        Resource* next = head->next;
        freePieces(head->piece_head);
        freeSeeders(head->seed_head);
        free(head->resourcename);
        free(head);
        head = next;
    }
    pthread_mutex_unlock(&lock);
}

/**
 * Finds a resource by name without locking the database mutex.
 *
 * The caller must already hold `lock` when thread safety is required.
 *
 * @param resourcename Resource name to find.
 * @return Pointer to the matching Resource entry, otherwise NULL.
 */
static Resource* findResource(const char* resourcename)
{
    if (resourcename == NULL)
        return NULL;
    for (Resource* current = head; current != NULL; current = current->next)
        if (strcmp(current->resourcename, resourcename) == 0)
            return current;
    return NULL;
}

/**
 * Creates and prepends a resource entry without locking the database mutex.
 *
 * @param resourcename Name copied into the new resource entry.
 * @return Pointer to the new Resource entry, otherwise NULL.
 */
static Resource* createResource(const char* resourcename)
{
    char* nameCopy = copyString(resourcename, RESOURCEN_LENGTH);
    if (nameCopy == NULL)
        return NULL;

    Resource* resource = calloc(1, sizeof(*resource));
    if (resource == NULL)
    {
        free(nameCopy);
        return NULL;
    }

    resource->resourcename = nameCopy;
    resource->next = head;
    head = resource;
    return resource;
}

/**
 * Searches one resource for a piece hash without locking the database mutex.
 *
 * @param resource Resource whose piece list is searched.
 * @param hash Piece hash to find.
 * @return Pointer to the matching Piece, otherwise NULL.
 */
static Piece* findPiece(Resource* resource, const char* hash)
{
    if (resource == NULL || hash == NULL)
        return NULL;
    for (Piece* piece = resource->piece_head; piece != NULL; piece = piece->next)
        if (strcmp(piece->pieceHash, hash) == 0)
            return piece;
    return NULL;
}

/**
 * Searches one resource for a seeder endpoint without locking the database mutex.
 *
 * @param resource Resource whose seeder list is searched.
 * @param ip Seeder IPv6 address.
 * @param port Seeder TCP port.
 * @return Pointer to the matching Seed, otherwise NULL.
 */
static Seed* findSeeder(Resource* resource, const char* ip, uint16_t port)
{
    if (resource == NULL || ip == NULL)
        return NULL;
    for (Seed* seed = resource->seed_head; seed != NULL; seed = seed->next)
        if (seed->seedPort == port && strcmp(seed->seedIP, ip) == 0)
            return seed;
    return NULL;
}

/**
 * Adds a piece hash to a resource without locking the database mutex.
 *
 * Existing hashes are treated as success and are not duplicated.
 *
 * @param resource Resource to update.
 * @param hash Piece hash to add.
 * @return true if the hash is present after the call, otherwise false.
 */
static bool addPiece(Resource* resource, const char* hash)
{
    if (resource == NULL || hash == NULL || *hash == '\0')
        return false;
    if (findPiece(resource, hash) != NULL)
        return true;

    char* hashCopy = copyString(hash, PIECE_HASH_LENGTH);
    Piece* piece = calloc(1, sizeof(*piece));
    if (hashCopy == NULL || piece == NULL)
    {
        free(hashCopy);
        free(piece);
        return false;
    }

    piece->pieceHash = hashCopy;
    piece->next = resource->piece_head;
    resource->piece_head = piece;
    return true;
}

/**
 * Adds a seeder endpoint to a resource without locking the database mutex.
 *
 * If the endpoint already exists, its original-owner flag is upgraded when
 * necessary instead of adding a duplicate.
 *
 * @param resource Resource to update.
 * @param ip Seeder IPv6 address.
 * @param port Seeder TCP port.
 * @param isOriginal Whether this peer should be marked as the original owner.
 * @return true if the seeder is present after the call, otherwise false.
 */
static bool addSeeder(Resource* resource, const char* ip, uint16_t port, bool isOriginal)
{
    if (resource == NULL || ip == NULL)
        return false;

    Seed* existing = findSeeder(resource, ip, port);
    if (existing != NULL)
    {
        existing->isOriginal = existing->isOriginal || isOriginal;
        return true;
    }

    char* ipCopy = copyString(ip, P2P_IPV6_TEXT_MAX);
    Seed* seed = calloc(1, sizeof(*seed));
    if (ipCopy == NULL || seed == NULL)
    {
        free(ipCopy);
        free(seed);
        return false;
    }

    seed->seedIP = ipCopy;
    seed->seedPort = port;
    seed->isOriginal = isOriginal;
    seed->next = resource->seed_head;
    resource->seed_head = seed;
    return true;
}

/**
 * Removes and frees a resource entry without locking the database mutex.
 *
 * @param resource Resource entry to remove.
 * @param previous Previous list node, or NULL when removing the head.
 */
static void removeResource(Resource* resource, Resource* previous)
{
    if (previous == NULL)
        head = resource->next;
    else
        previous->next = resource->next;

    freePieces(resource->piece_head);
    freeSeeders(resource->seed_head);
    free(resource->resourcename);
    free(resource);
}

/**
 * Frees a linked list of Piece nodes.
 *
 * @param piece First node to free; NULL is accepted.
 */
static void freePieces(Piece* piece)
{
    while (piece != NULL)
    {
        Piece* next = piece->next;
        free(piece->pieceHash);
        free(piece);
        piece = next;
    }
}

/**
 * Frees a linked list of Seed nodes.
 *
 * @param seed First node to free; NULL is accepted.
 */
static void freeSeeders(Seed* seed)
{
    while (seed != NULL)
    {
        Seed* next = seed->next;
        free(seed->seedIP);
        free(seed);
        seed = next;
    }
}

/**
 * Extracts a value from a tracker metadata line.
 *
 * @param line Metadata line to parse.
 * @param mode METADATA_VALUE_RESOURCENAME or METADATA_VALUE_OWNER_ENDPOINT.
 * @return Dynamically allocated value on success, otherwise NULL.
 */
static char* getMetadataValue(const char* line, int mode)
{
    if (line == NULL)
        return NULL;

    size_t maxLength;
    if (mode == METADATA_VALUE_RESOURCENAME)
        maxLength = RESOURCEN_LENGTH;
    else if (mode == METADATA_VALUE_OWNER_ENDPOINT)
        maxLength = P2P_ENDPOINT_MAX;
    else
        return NULL;

    const char* firstColon = strchr(line, ':');
    if (firstColon == NULL)
        return NULL;

    const char* secondColon = strchr(firstColon + 1, ':');
    if (secondColon == NULL)
        return NULL;

    const char* value = secondColon + 1;
    size_t length = strcspn(value, "\r\n");
    if (length == 0 || length > maxLength)
        return NULL;

    char* result = malloc(length + 1);
    if (result == NULL)
        return NULL;

    memcpy(result, value, length);
    result[length] = '\0';
    return result;
}


/**
 * Creates a length-checked dynamic copy of a string.
 *
 * @param source Source string.
 * @param maxLength Maximum accepted string length.
 * @return Newly allocated copy on success, otherwise NULL.
 */
static char* copyString(const char* source, size_t maxLength)
{
    if (source == NULL)
        return NULL;

    size_t length = strlen(source);
    if (length == 0 || length > maxLength)
        return NULL;

    char* copy = malloc(length + 1);
    if (copy == NULL)
        return NULL;
    memcpy(copy, source, length + 1);
    return copy;
}

/**
 * Removes the first CR or LF terminator from a text line in place.
 *
 * @param text Mutable line buffer; NULL is accepted.
 */
static void trimLine(char* text)
{
    if (text != NULL)
        text[strcspn(text, "\r\n")] = '\0';
}
