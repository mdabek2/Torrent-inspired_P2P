/**
 * @file shared_files.c
 * @brief Thread-safe local registry of resources shared by one peer.
 */

#include "client_header.h"

static LocalResource* localResources = NULL;
static pthread_mutex_t localResourcesLock = PTHREAD_MUTEX_INITIALIZER;

/**
 * Registers a local resource shared/downloaded by this peer.
 *
 * @param resourceName Name used to identify the resource.
 * @param filepath Path to the local file.
 * @return true if the resource was registered or updated, otherwise false.
 */
bool registerLocalResource(const char* resourceName, const char* filepath)
{
    if (resourceName == NULL || filepath == NULL || *resourceName == '\0' || *filepath == '\0')
        return false;

    pthread_mutex_lock(&localResourcesLock);

    for (LocalResource* current = localResources; current != NULL; current = current->next)
        if (strcmp(current->name, resourceName) == 0)
        {
            snprintf(current->path, sizeof(current->path), "%s", filepath);
            pthread_mutex_unlock(&localResourcesLock);
            return true;
        }

    LocalResource* newResource = calloc(1, sizeof(*newResource));
    if (newResource == NULL)
    {
        pthread_mutex_unlock(&localResourcesLock);
        return false;
    }

    snprintf(newResource->name, sizeof(newResource->name), "%s", resourceName);
    snprintf(newResource->path, sizeof(newResource->path), "%s", filepath);
    newResource->next = localResources;
    localResources = newResource;

    pthread_mutex_unlock(&localResourcesLock);
    return true;
}

/**
 * Gets the local file path associated with a resource.
 *
 * @param resourceName Resource name to search for.
 * @param pathBuffer Destination buffer for the local path.
 * @param pathBufferSize Size of the destination buffer.
 * @return true if the resource exists locally, otherwise false.
 */
bool getLocalResourcePath(const char* resourceName, char* pathBuffer, size_t pathBufferSize)
{
    if (resourceName == NULL || pathBuffer == NULL || pathBufferSize == 0)
        return false;

    pthread_mutex_lock(&localResourcesLock);

    for (LocalResource* current = localResources; current != NULL; current = current->next)
    {
        if (strcmp(current->name, resourceName) == 0)
        {
            snprintf(pathBuffer, pathBufferSize, "%s", current->path);
            pthread_mutex_unlock(&localResourcesLock);
            return true;
        }
    }

    pthread_mutex_unlock(&localResourcesLock);
    return false;
}

/**
 * Removes all entries from the local resource registry.
 */
void clearLocalResources()
{
    pthread_mutex_lock(&localResourcesLock);
    while (localResources != NULL)
    {
        LocalResource* next = localResources->next;
        free(localResources);
        localResources = next;
    }
    pthread_mutex_unlock(&localResourcesLock);
}
