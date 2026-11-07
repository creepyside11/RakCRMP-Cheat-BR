// RakNet sync packet patching (pure logic, unit-testable).
#pragma once

namespace rcpkt {

// All functions take the raw RakNet payload with its leading packet id and
// an explicit length; they never write outside [0, len).
void playerSync(char* d, int len);
void vehicleSync(char* d, int len);
void passengerSync(char* d, int len);

// drop the accumulated speed-hack offset (e.g. when the feature is toggled off)
void resetMotion();

} // namespace rcpkt
