# The lake protocol
The lake protocol is designed to be asynchronous, flexible and non-restrictive both to clients and servers ensuring backwards and forwards compatibility where possible.

## Data structures
Unless otherwise specified all integers are considered **BIG ENDIAN**
### Request
```c
typedef struct {
    uint32_t protocol_id;
    uint32_t func_id;
    uint32_t packet_id;
    uint32_t packet_len;
    // char packet_data[packet_len];
} Request;
```
- protocol_id - refers to the [Protocol](#Protocol) we want to make our request to
- func_id - refers to the [Function](#Function) we want to run on this protocol.
- packet_id - is an identifier to identify our request in the responses (checkout packet_id in [Response](#Response))
- packet_len - length of the subsequent data of our Request in bytes.

### Response
```c
typedef struct {
    uint32_t packet_id;
    uint32_t opcode;
    uint32_t packet_len;
    // char packet_data[packet_len];
} Response;
```
- packet_id - the identifier given in the [Request](#Request)'s packet_id field.
- opcode - used for general error handling 0 for OK, Non 0 for Error.
- packet_len - length of the subsequent data of our Response in bytes.

## Protocol
A protocol is simply a set of functions (identified by an id) that the server supports.
Examples for protocols include - authentication, getting user information, notifications, etc.

In order for the client to know what protocols the server supports, protocol_id 0 func_id 0 is reserved for:
"GetAvailableProtocols". 

the server responds with a chain of responses (ending with a response with length zero to indicate "END") that look something like this:
```c
// this is packet_data of GetAvailableProtocols
struct {
    uint32_t protocol_id;
    char name[];
};
```

## Postscript

If you're interested in specifics of supported protocols you can check out server/src/protocols where you can find most of the requests, their structures and their responses.
For client side implementations please checkout client/src/on*

## Asynchronous Design

The protocol is made with asynchronicity in mind, which means the server can accept and process requests in any order it likes.
In order to identify any given request, you as the client can give a unique "packet_id" to it and the server is required to send you back a response with the same packet_id

## Terminology
When referring to:
- [Request](#Request) - packet sent to perform Client to Server communication.
- [Response](#Response) - packet sent to perform Server to Client communication.
- [Protocol](#Protocol) - refers to a server supported set of functionality.
- Function - a subset of a Protocol, implements specific functionality of the Protocol.
