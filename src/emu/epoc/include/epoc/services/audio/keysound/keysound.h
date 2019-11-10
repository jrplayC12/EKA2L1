/*
 * Copyright (c) 2019 EKA2L1 Team
 * 
 * This file is part of EKA2L1 project.
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <epoc/services/audio/keysound/context.h>
#include <epoc/services/framework.h>

#include <stack>

namespace eka2l1 {
    class keysound_session: public service::typical_session {
    private:
        service::uid app_uid_;                              ///< The UID3 of the app opening this session
        std::stack<epoc::keysound::context> contexts_;      ///< Context stack describes sound to play when key action trigger.

    public:
        explicit keysound_session(service::typical_server *svr, service::uid client_ss_uid);
        void fetch(service::ipc_context *ctx) override;

        void init(service::ipc_context *ctx);
        void push_context(service::ipc_context *ctx);
        void pop_context(service::ipc_context *ctx);
    };

    class keysound_server: public service::typical_server {
    public:
        explicit keysound_server(system *sys);
        void connect(service::ipc_context &context) override;
    };
}