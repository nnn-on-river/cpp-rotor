#pragma once

//
// Copyright (c) 2019-2020 Ivan Baidakou (basiliscos) (the dot dmol at gmail dot com)
//
// Distributed under the MIT Software License
//

#include "../subscription.h"
#include "rotor/messages.hpp"

namespace rotor {

struct plugin_t {
    enum processing_result_t { CONSUMED = 0, IGNORED, FINISHED };

    enum reaction_t {
        INIT = 1 << 0,
        SHUTDOWN = 1 << 1,
        SUBSCRIPTION = 1 << 2,
    };

    plugin_t() = default;
    plugin_t(const plugin_t &) = delete;
    virtual ~plugin_t();

    virtual const void *identity() const noexcept = 0;

    virtual void activate(actor_base_t *actor) noexcept;
    virtual void deactivate() noexcept;

    virtual bool handle_init(message::init_request_t *message) noexcept;
    virtual bool handle_shutdown(message::shutdown_request_t *message) noexcept;

    virtual bool handle_unsubscription(const subscription_point_t &point, bool external) noexcept;
    virtual bool forget_subscription(const subscription_point_t &point) noexcept;
    virtual void forget_subscription(const subscription_info_ptr_t &info) noexcept;

    virtual processing_result_t handle_subscription(message::subscription_t &message) noexcept;

    template <typename Handler>
    subscription_info_ptr_t subscribe(Handler &&handler, const address_ptr_t &address) noexcept;
    template <typename Handler> subscription_info_ptr_t subscribe(Handler &&handler) noexcept;

    template <typename Plugin, typename Fn> void with_casted(Fn &&fn) noexcept {
        if (identity() == Plugin::class_identity) {
            auto &final = static_cast<Plugin &>(*this);
            fn(final);
        }
    }

    std::size_t get_reaction() const noexcept { return reaction; }
    actor_base_t *get_actor() const noexcept { return actor; }
    void reaction_on(reaction_t value) noexcept { reaction = reaction | value; }
    void reaction_off(reaction_t value) noexcept { reaction = reaction & ~value; }
    subscription_container_t &get_subscriptions() noexcept { return own_subscriptions; }

  protected:
    actor_base_t *actor;

  private:
    subscription_container_t own_subscriptions;
    std::size_t reaction = 0;
};

} // namespace rotor
