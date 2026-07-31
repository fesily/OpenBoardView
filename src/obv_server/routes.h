#pragma once

#include "board_registry.h"

#include "httplib.h"

namespace obv_server {

// Registers /api/v1/boards* routes (list/get/meta/delete + overlay/annotation CRUD + pin/part GET).
void RegisterBoardRoutes(httplib::Server &svr, BoardRegistry &registry);

} // namespace obv_server
