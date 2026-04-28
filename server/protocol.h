#pragma once
// Application-Layer Protocol Constants
// Request Format:  <COMMAND> [args...]\n
// Response Format: <STATUS> [data...]\n
//
// Commands (Client -> Server):
//   LOGIN <username> <password>
//   LOGOUT
//   QUERY <course_code>
//   SEARCH_INSTRUCTOR <name>
//   SEARCH_TIME <day> <time>
//   LIST_ALL [semester]
//   ADD <code|title|section|instructor|day|time|duration|classroom|semester>
//   UPDATE <code> <section> <field> <value>
//   DELETE <code> <section>
//   HELP
//   QUIT
//
// Responses (Server -> Client):
//   WELCOME ...
//   SUCCESS ...
//   FAILURE ...
//   RESULT BEGIN / RESULT <data> / RESULT END
//   RESULT NONE ...
//   OK ...
//   ERROR ...
//   BYE
//   HELP ...

#define PROTO_VERSION "1.0"
#define SERVER_PORT   8888
