//
// Copyright (c) 2019-2020 Ivan Baidakou (basiliscos) (the dot dmol at gmail dot com)
//
// Distributed under the MIT Software License
//

#include "rotor/asio/supervisor_asio.h"
#include "rotor/asio/forwarder.hpp"

using namespace rotor::asio;
using namespace rotor;

namespace rotor::asio {
namespace {
namespace to {
struct on_timer_trigger {};
} // namespace to
} // namespace
} // namespace rotor::asio

namespace rotor {
template <>
inline auto rotor::actor_base_t::access<to::on_timer_trigger, request_id_t, bool>(request_id_t request_id,
                                                                                  bool cancelled) noexcept {
    on_timer_trigger(request_id, cancelled);
}
} // namespace rotor

supervisor_asio_t::supervisor_asio_t(supervisor_config_asio_t &config_)
    : supervisor_t{config_}, strand{config_.strand} {
    if (config_.guard_context) {
        guard = std::make_unique<guard_t>(asio::make_work_guard(strand->context()));
    }
}

rotor::address_ptr_t supervisor_asio_t::make_address() noexcept { return instantiate_address(strand.get()); }

void supervisor_asio_t::start() noexcept { create_forwarder (&supervisor_asio_t::do_process)(); }

void supervisor_asio_t::shutdown(const std::error_code &ec) noexcept {
    using typed_actor_t = intrusive_ptr_t<supervisor_asio_t>;
    typed_actor_t self(this);
    asio::defer(*strand, [self = std::move(self), this, ec = ec]() {
        (void)self;
        do_shutdown(ec);
        do_process();
    });
}

void supervisor_asio_t::do_start_timer(const pt::time_duration &interval, timer_handler_base_t &handler) noexcept {
    auto timer = std::make_unique<supervisor_asio_t::timer_t>(&handler, strand->context());
    timer->expires_from_now(interval);

    intrusive_ptr_t<supervisor_asio_t> self(this);
    request_id_t timer_id = handler.request_id;
    timer->async_wait([self = std::move(self), timer_id = timer_id](const boost::system::error_code &ec) {
        auto &strand = self->get_strand();
        if (ec) {
            asio::defer(strand, [self = std::move(self), timer_id = timer_id, ec = ec]() {
                auto &sup = *self;
                auto &timers_map = sup.timers_map;
                auto it = timers_map.find(timer_id);
                if (it != timers_map.end()) {
                    bool cancelled = ec == asio::error::operation_aborted;
                    if (cancelled) {
                        auto actor_ptr = it->second->handler->owner;
                        actor_ptr->access<to::on_timer_trigger, request_id_t, bool>(timer_id, true);
                    } else {
                        sup.on_timer_error(timer_id, ec);
                    }
                    timers_map.erase(it);
                    sup.do_process();
                }
            });
        } else {
            asio::defer(strand, [self = std::move(self), timer_id = timer_id]() {
                auto &sup = *self;
                auto &timers_map = sup.timers_map;
                auto it = timers_map.find(timer_id);
                assert(it != timers_map.end());
                auto actor_ptr = it->second->handler->owner;
                actor_ptr->access<to::on_timer_trigger, request_id_t, bool>(timer_id, false);
                timers_map.erase(it);
                sup.do_process();
            });
        }
    });
    timers_map.emplace(timer_id, std::move(timer));
}

void supervisor_asio_t::do_cancel_timer(request_id_t timer_id) noexcept {
    auto it = timers_map.find(timer_id);
    assert(it != timers_map.end());
    auto &timer = it->second;
    boost::system::error_code ec;
    timer->cancel(ec);
    // ignore the possible error, caused the case when timer is not cancelleable
    // if (ec) { ... }
}

void supervisor_asio_t::on_timer_error(request_id_t, const boost::system::error_code &ec) noexcept {
    context->on_error(ec);
}

void supervisor_asio_t::enqueue(rotor::message_ptr_t message) noexcept {
    auto actor_ptr = supervisor_ptr_t(this);
    // std::cout << "deferring on " << this << ", stopped : " << strand.get_io_context().stopped() << "\n";
    asio::defer(get_strand(), [actor = std::move(actor_ptr), message = std::move(message)]() mutable {
        auto &sup = *actor;
        // std::cout << "deferred processing on" << &sup << "\n";
        // sup.enqueue(std::move(message));
        sup.put(std::move(message));
        sup.do_process();
    });
}

void supervisor_asio_t::shutdown_finish() noexcept {
    if (guard)
        guard.reset();
    supervisor_t::shutdown_finish();
}
