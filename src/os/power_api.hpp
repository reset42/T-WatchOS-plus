#pragma once

class PowerService;
class ApiBus;

// Einzige Deklaration – die Definition ist in src/os/power_api.cpp
void bindPowerApi(PowerService& svc, ApiBus& api);
