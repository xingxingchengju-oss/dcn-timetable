#pragma once
// Protocol v2.4 - see docs/protocol.md for the full specification.
// Request:  COMMAND|param1|param2|...\n
// Response: STATUS|field1|field2|...\n  (or RESULT_BEGIN/RESULT_END block)
// v2.3 added server-initiated NOTIFY|<op>|<code>|<section> broadcasts.
// v2.4 added server-side query-result cache + cache_hits/cache_misses in STATUS.

#define PROTO_VERSION "2.4"
#define SERVER_PORT   50000
#define XOR_KEY       "DCN2026TimetableKey"
