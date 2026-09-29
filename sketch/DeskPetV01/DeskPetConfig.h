#pragma once

// Optional per-installation settings. This file must remain untracked.
#if __has_include("DeskPetConfig.local.h")
#include "DeskPetConfig.local.h"
#endif

// Hostname only, without scheme, port or path. Use your own HTTPS server.
// The public snapshot never connects to the original owner's private service.
#ifndef DESKPET_MESSAGE_HOST
#define DESKPET_MESSAGE_HOST "esp.example.com"
#endif
