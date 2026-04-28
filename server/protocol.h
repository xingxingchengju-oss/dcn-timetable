#pragma once
// Protocol v2.0 - see docs/protocol.md for the full specification.
// Request:  COMMAND|param1|param2|...\n
// Response: STATUS|field1|field2|...\n  (or RESULT_BEGIN/RESULT_END block)

#define PROTO_VERSION "2.0"
#define SERVER_PORT   50000
