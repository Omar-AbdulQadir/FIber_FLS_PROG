/**
  ******************************************************************************
  * @file    command.h
  * @brief   Command dispatcher + handlers for the flash-programmer server.
  *
  *          Table-driven: each opcode maps to a handler. Adding a command is
  *          one table row + one handler function. Handlers build their own
  *          response(s) via Protocol_SendResponse().
  *
  *          Holds the stream-session state shared between the START and DATA
  *          frames of WRITE_STREAM and READ.
  ******************************************************************************
  */

#ifndef COMMAND_H
#define COMMAND_H

#ifdef __cplusplus
extern "C" {
#endif

#include "protocol/protocol.h"

/* Dispatch a parsed request to its handler. Called by the protocol layer
 * once a valid frame has been assembled and CRC-checked. */
void Command_Dispatch(const proto_request_t *req);

#ifdef __cplusplus
}
#endif

#endif /* COMMAND_H */
