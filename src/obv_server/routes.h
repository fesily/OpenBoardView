#pragma once

#include "board_registry.h"

#include "httplib.h"

namespace obv_server {

// Registers /api/v1/boards* and /api/v1/chips* routes (board overlay CRUD + chip library CRUD).
void RegisterBoardRoutes(httplib::Server &svr, BoardRegistry &registry);

} // namespace obv_server
