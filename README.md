# IPv6 P2P File Sharing

A peer-to-peer file sharing application written in **C** using **IPv6**, **TCP sockets**, **POSIX threads**, and a centralized tracker.

The tracker is responsible only for resource discovery and seeder management. Actual file data is transferred **directly between peers**, so once a peer downloads a resource it can register itself as a new seeder and serve the file to other clients.

## Features

- IPv6/TCP communication
- centralized tracker for resource discovery
- direct peer-to-peer file transfer
- concurrent tracker request handling with `pthread`
- background peer server running alongside the interactive client
- multiple seeders identified by IPv6 address and listening port
- automatic seeder registration after a successful download
- fallback to another seeder when a peer cannot provide the requested resource
- thread-safe local resource registry
- torrent-style metadata containing resource information and piece hashes
- CMake-based builds for both tracker and client

## Architecture

```text
                         +---------------------+
                         |       Tracker       |
                         |      [::1]:8080     |
                         +----------+----------+
                                    |
                         metadata / seeder list
                                    |
                  +-----------------+-----------------+
                  |                                   |
          +-------+-------+                   +-------+-------+
          |    Peer A     |   direct TCP      |    Peer B     |
          |  [::1]:9001   | <---------------> |  [::1]:9002   |
          +---------------+    file transfer  +---------------+
```

The tracker does **not** relay resource contents. It stores resource metadata and a list of available seeders. File contents are exchanged directly between peers.

## Requirements

The project targets a POSIX environment, such as Linux.

Required tools:

- C compiler with C11 support, e.g. GCC
- CMake 3.16 or newer
- POSIX Threads (`pthread`)
- IPv6 support

## Building

### Tracker

```bash
cd tracker
cmake -S . -B build
cmake --build build
```

The executable is created as:

```text
tracker/build/tracker
```

### Client

```bash
cd client
cmake -S . -B build
cmake --build build
```

The executable is created as:

```text
client/build/client
```

## Running the application

For a local test, use one terminal for the tracker and separate terminals for each peer.

### 1. Start the tracker

From the `tracker` directory:

```bash
./build/tracker '[::1]:8080' database_orig.txt
```

Arguments:

```text
./tracker [IPv6]:port database_file
```

The database file contains the initial resources, piece hashes, original owner and additional seeders.

Example endpoint format:

```text
[::1]:9001
```

### 2. Start Peer A

From the `client` directory:

```bash
./build/client '[::1]:8080' 9001
```

The first argument is the tracker endpoint. The second argument is the TCP port on which this peer accepts direct P2P connections.

### 3. Start Peer B

In another terminal:

```bash
./build/client '[::1]:8080' 9002
```

Both peers may use the same IPv6 address during local testing because the tracker identifies them by the combination of **IPv6 address + listening port**.

## Client menu

```text
1. Share resource
2. Download resource
3. Request file's metadata only
4. Remove this peer from seeders
0. Exit
```

### Share a resource

Select option `1` and provide:

```text
Resource name: sample
Resource file path: ../_sample_data/sample.txt
Torrent metadata path: ../_sample_data/sample.torrent
```

The client sends the metadata to the tracker and stores the local resource path in its in-memory registry so that its peer server can serve the file to other clients.

### Download a resource

On another peer, select option `2`:

```text
Resource name: sample
Output file path: downloaded_sample.txt
Output metadata path [default: download_info.txt]:
```

The client then:

1. requests resource metadata from the tracker,
2. reads the available seeder endpoints,
3. connects directly to a seeder,
4. downloads the resource over TCP,
5. stores it locally,
6. registers the downloaded copy in its local resource registry,
7. informs the tracker that it is now a seeder.

If one listed seeder cannot provide the resource, the client tries the next available endpoint.

## Communication protocols

### Client ↔ Tracker

Every tracker request begins with:

```text
+----------------+----------------------+
| 1 byte command | 2 bytes peer port    |
|                | network byte order   |
+----------------+----------------------+
```

Commands:

| Command | Meaning |
|---|---|
| `'1'` | Share/register resource metadata |
| `'2'` | Request resource metadata |
| `'3'` | Add peer to the resource's seeder list |
| `'4'` | Remove peer from the resource's seeder list |

For resource registration, the metadata size is transmitted as a 32-bit unsigned integer in network byte order followed by the metadata bytes.

### Peer ↔ Peer

A downloading peer sends the resource name as a newline-terminated text line:

```text
resource_name\n
```

The seeder responds with a one-byte status:

| Status | Meaning |
|---|---|
| `'1'` | Resource found |
| `'0'` | Resource not found |

When the resource exists, the response continues with:

```text
+----------+----------------------+--------------------+
| status   | uint32_t file size   | file data          |
| 1 byte   | network byte order   | N bytes            |
+----------+----------------------+--------------------+
```

The receiver first writes the download to a temporary `.part` file and renames it only after the complete transfer succeeds.

## Tracker database format

The initial tracker database uses entries such as:

```text
4:name:sample
5:owner:[::1]:9001
6:pieces:
:11111111111111111111111111111111
:22222222222222222222222222222222
7:seeds:
[::1]:9002
;
```

Each resource contains:

- resource name,
- original owner endpoint,
- piece hashes,
- optional additional seeders.

## Torrent-style metadata

A sample metadata file is included in `_sample_data/sample.torrent`.

The tracker reads the resource name and piece hashes from this file when a resource is registered. Metadata is later returned to downloading peers together with currently known seeder endpoints.

## Current scope

The project implements tracker-based discovery and complete-file P2P transfer. Torrent metadata contains piece hashes, but the current downloader transfers a complete resource from one seeder rather than downloading individual pieces from multiple peers.

Possible extensions include:

- piece-by-piece downloads,
- hash verification after receiving pieces,
- downloading different pieces from multiple seeders concurrently,
- persistent peer resource registries,
- graceful peer-server shutdown,
- automated integration tests.

## Technologies

- C11
- POSIX sockets
- IPv6
- TCP
- pthreads
- CMake
- linked lists
- mutex-based synchronization

## Purpose

This project was created as a networking exercise focused on implementing a BitTorrent-inspired peer-to-peer architecture, application-level protocols, concurrent TCP servers, tracker-based peer discovery, and direct file exchange between peers.
