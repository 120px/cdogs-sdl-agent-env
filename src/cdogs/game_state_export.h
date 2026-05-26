#pragma once
#include "../json/json.h"

void EnsureOutputDir(void);
void ResetDeathLog(void);
json_t *GameStateToJSON(const int tick);
void WriteSnapshotToJsonFile(json_t *root);
