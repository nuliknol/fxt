#property strict

#define ORDER_COMMENT_PREFIX "Harvester|G="

bool StrategyValidateInputs();
void StrategyResetState();
void StrategyOnTickPrelude();
void StrategyUpdateSignalState();
bool StrategyShouldHarvest();
void StrategyOnHarvestSuccess();
void StrategyAppendComment(string &text);
void StrategyOnDeinit(const int reason);
string StrategyModeName();

#include "harvester/core_common.mqh"
#include "harvester/modes/profit_spike_trail.mqh"
