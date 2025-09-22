#pragma once
#include "radio.h"

#define BUFFER_SIZE 21

bool program_flash(Radio &driver);
bool check_recovery_bytes(void);