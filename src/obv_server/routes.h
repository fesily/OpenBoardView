#pragma once

#include "board_registry.h"

#include "httplib.h"

namespace obv_server {

// Registers /api/v1/boards* routes (list/upload/get/meta/delete + overlay/annotation CRUD).
void RegisterBoardRoutes(httplib::Server &svr, BoardRegistry &registry);

} // namespace obv_server
