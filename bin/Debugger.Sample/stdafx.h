// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "targetver.h"

#include "DebugProtocolHandler.h"
#include "DebugService.h"
#include "ErrorHelpers.h"

#include <stdio.h>
#include <tchar.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

// HACK - trick it into not loading the Windows APIs
#define _CHAKRACOMMONWINDOWS_H_
#include <ChakraCore.h>
#include <ChakraDebugProtocolHandler.h>
#include <ChakraDebugService.h>
