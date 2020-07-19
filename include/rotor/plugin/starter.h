#pragma once

//
// Copyright (c) 2019-2020 Ivan Baidakou (basiliscos) (the dot dmol at gmail dot com)
//
// Distributed under the MIT Software License
//

#include "subscriber.h"

namespace rotor::internal {

struct starter_plugin_t : public subscriber_plugin_t {
    using subscriber_plugin_t::subscriber_plugin_t;

    static const void *class_identity;
    const void *identity() const noexcept override;

    void activate(actor_base_t *actor) noexcept override;

    bool handle_init(message::init_request_t *) noexcept override;
    bool handle_start(message::start_trigger_t *message) noexcept override;

    processing_result_t handle_subscription(message::subscription_t &message) noexcept override;

    void on_start(message::start_trigger_t &message) noexcept;

  private:
    bool configured = false;
};

} // namespace rotor::internal
