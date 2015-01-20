======================
LCAP exchange protocol
======================

-------------
Core concepts
-------------

Architecture
============

LCAP uses a regular client/server architecture to redistribute lustre changelog
records to numerous consumers. Exchanges are done over ZeroMQ sockets.

The server is composed of a broker and one or several worker threads. There is
one worker thread per exposed MDT. The broker is responsible for bootstraping
the whole server and for dispatching requests/replies between connected
components. It uses and exposes a ZMQ_ROUTER socket.

The workers read records and enqueue them locally. They receive and process
client requests, routed to them by the broker. Similarly replies are identified
so that the broker can send them back to the clients. The workers use ZMQ_DEALER
sockets.

The clients which use the NULL-channel do nothing but calling the regular
changelog consumption functions as exposed by the lustreapi. The proxified ones
exploit the LCAP exchange protocol to get batches of records and consume them as
quickly as possible.

            READER_mdt0000         READER_mdt0001         READER_mdt0002
                  |                      |                      |
                  |______________________|______________________|
                                         |
                                    lcapd broker
                                         |
                        _________________|_________________
                       |           |         |             |
                    Client0    Client1       ...       ClientN


Running as threads of a same process, the readers are guaranteed to be either
all present or absent.


Supported operations
====================

The exchanges between clients and server are strictly synchronous, and initiated
client-side.

Operations must occur in the following order:
    - changelog_start
    - changelog_recv
    - changelog_clear
    - changelog_stop

**changelog_start** makes the client visible to the server. This first request
contains the targetted MDT and as such, will be routed accordingly. The reader
will create a context for this client.

**changelog_recv** is a request for records. The server will send as much
records as possible, i.e. min(available, max_batch_size).

**changelog_clear** becomes a two-steps operations with lcap. Clients can
cheaply acknowledge every consumed records locally, and the current state will
be regularly pushed to the server, for upstream acknowledgement.

**changelog_stop** is used to notify the server that this client is about to
leave. All contexts will be cleared past this call and the client must re-issue
a **changelog_start** request to start receiving records again.


Wire format
===========

LCAP RPCs leverage ZMQ multi-framing capabilities, and are composed of the
following elements:

0: empty frame (invisible to ZMQ_REQ)
1: mdt name (for request routing)
2: empty frame (envelope delimiter)
3: RPC from lcap_idl.h

Note that the RPC itself can be a multi-frame message, depending on how it was
sent. It is up to the receiver to aggregate it properly.
