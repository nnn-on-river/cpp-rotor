#include "rotor/asio/supervisor_asio.h"
#include "rotor/asio/forwarder.hpp"

using namespace rotor::asio;

supervisor_asio_t::supervisor_asio_t(system_context_ptr_t system_context_, const supervisor_config_t &config_)
    : system_context{system_context_}, strand{system_context_->io_context},
      shutdown_timer{system_context->io_context}, config{config_} {}

void supervisor_asio_t::start() noexcept { create_forwarder (&supervisor_asio_t::do_start)(); }

void supervisor_asio_t::shutdown() noexcept { create_forwarder (&supervisor_asio_t::do_shutdown)(); }

void supervisor_asio_t::start_shutdown_timer() noexcept {
    shutdown_timer.expires_from_now(config.shutdown_timeout);
    auto fwd = create_forwarder(&supervisor_t::on_shutdown_timer_trigger, &supervisor_asio_t::on_shutdown_timer_error);
    shutdown_timer.async_wait(std::move(fwd));
}

void supervisor_asio_t::on_shutdown_timer_error(const boost::system::error_code &ec) noexcept {
    if (ec != asio::error::operation_aborted) {
        system_context->on_error(ec);
    }
}

void supervisor_asio_t::cancel_shutdown_timer() noexcept {
    boost::system::error_code ec;
    shutdown_timer.cancel(ec);
    if (ec) {
        system_context->on_error(ec);
    }
}

void supervisor_asio_t::enqueue(message_ptr_t message) noexcept {
    auto actor_ptr = supervisor_ptr_t(this);
    asio::defer(strand, [actor = std::move(actor_ptr), message = std::move(message)]() {
        auto &sup = *actor;
        sup.enqueue(std::move(message));
        sup.do_process();
    });
}
