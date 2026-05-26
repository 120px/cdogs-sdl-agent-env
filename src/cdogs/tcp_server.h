#pragma once

// Simple TCP server for streaming game state JSON to an external agent.
// Call TcpServerInit() once at mission start, TcpServerSendJSON() each tick,
// and TcpServerTerminate() at mission end.

#include "../json/json.h"

void TcpServerInit(const int port);
void TcpServerSendJSON(json_t *root);
void TcpServerTerminate(void);
