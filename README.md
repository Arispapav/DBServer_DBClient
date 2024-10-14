# DBServer_DBClient
DBServer is a simple database server designed to handle client requests for storing and retrieving data records. It supports both sequential and multi-threaded operation modes. The multi-threaded server spawns a new handler thread for each client, ensuring efficient request processing.
Features
Sequential Server: Single-threaded server mode that handles one client request at a time.
Multi-threaded Server:
The parent server initializes a listening socket and waits for connection requests from clients.
Upon accepting a connection, the server creates a new thread (handler) to process client requests independently.
The server can handle multiple clients concurrently using a pool of handler threads.
Server-Client Communication
The server processes two types of requests from the client:

1. PUT Request
This request contains data records to be stored in the database.
The handler:
Appends the record to the database file.
Sends a SUCCESS message to the client if the operation was successful.
Sends a FAIL message if the operation fails.
2. GET Request
This request contains the ID of the record the client wants to retrieve.
The handler:
Searches the database for a matching record.
Sends a SUCCESS message along with the matching record if found.
Sends a FAIL message if the record does not exist.
Thread Lifecycle
The handler thread continues processing requests until the client closes the connection.
Once the connection is closed, the handler thread terminates.
Message Format
The format of messages exchanged between the server and the client, as well as the structure of records stored in the database, is defined in the file msg.h.

Usage
Start the server by compiling and running the DBServer in either sequential or multi-threaded mode.
Clients can connect to the server, send PUT and GET requests, and receive responses according to the protocol.
For further information on how to run the server and client, and details about the message format, please refer to the documentation in the msg.h file.
