//
// Copyright (c) 2019-2020 Ivan Baidakou (basiliscos) (the dot dmol at gmail dot com)
//
// Distributed under the MIT Software License
//

#include "rotor/wx/supervisor_wx.h"
#include <wx/timer.h>

using namespace rotor::wx;
using namespace rotor;

namespace rotor::wx {
namespace {
namespace to {
struct on_timer_trigger {};
} // namespace to
} // namespace
} // namespace rotor::wx

namespace rotor {
template <>
inline auto rotor::actor_base_t::access<to::on_timer_trigger, request_id_t, bool>(request_id_t request_id,
                                                                                  bool cancelled) noexcept {
    on_timer_trigger(request_id, cancelled);
}
} // namespace rotor

supervisor_wx_t::timer_t::timer_t(timer_handler_base_t *handler_, supervisor_ptr_t &&sup_)
    : handler{handler_}, sup{std::move(sup_)} {}

void supervisor_wx_t::timer_t::Notify() noexcept {
    auto timer_id = handler->request_id;
    auto *supervisor = sup.get();
    auto &timers_map = sup->timers_map;
    auto it = timers_map.find(timer_id);
    if (it != timers_map.end()) {
        auto actor_ptr = handler->owner;
        actor_ptr->access<to::on_timer_trigger, request_id_t, bool>(timer_id, false);
        timers_map.erase(it);
        supervisor->do_process();
    }
}

supervisor_wx_t::supervisor_wx_t(supervisor_config_wx_t &config_) : supervisor_t{config_}, handler{config_.handler} {}

void supervisor_wx_t::start() noexcept {
    supervisor_ptr_t self{this};
    handler->CallAfter([self = std::move(self)]() {
        auto &sup = *self;
        sup.do_process();
    });
}

void supervisor_wx_t::shutdown(const std::error_code &ec) noexcept {
    supervisor_ptr_t self{this};
    handler->CallAfter([self = std::move(self), ec = ec]() {
        auto &sup = *self;
        sup.do_shutdown(ec);
        sup.do_process();
    });
}

void supervisor_wx_t::enqueue(message_ptr_t message) noexcept {
    supervisor_ptr_t self{this};
    handler->CallAfter([self = std::move(self), message = std::move(message)]() {
        auto &sup = *self;
        sup.put(std::move(message));
        sup.do_process();
    });
}

void supervisor_wx_t::do_start_timer(const pt::time_duration &interval, timer_handler_base_t &handler) noexcept {
    auto self = timer_t::supervisor_ptr_t(this);
    auto timer = std::make_unique<timer_t>(&handler, std::move(self));
    auto timeout_ms = static_cast<int>(interval.total_milliseconds());
    timer->StartOnce(timeout_ms);
    timers_map.emplace(handler.request_id, std::move(timer));
}

void supervisor_wx_t::do_cancel_timer(request_id_t timer_id) noexcept {
    auto it = timers_map.find(timer_id);
    if (it != timers_map.end()) {
        auto &timer = timers_map.at(timer_id);
        timer->Stop();
        auto actor_ptr = it->second->handler->owner;
        actor_ptr->access<to::on_timer_trigger, request_id_t, bool>(timer_id, true);
        timers_map.erase(it);
    }
}
