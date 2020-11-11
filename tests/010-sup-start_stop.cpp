//
// Copyright (c) 2019-2020 Ivan Baidakou (basiliscos) (the dot dmol at gmail dot com)
//
// Distributed under the MIT Software License
//

#include "catch.hpp"
#include "rotor.hpp"
#include "supervisor_test.h"
#include "actor_test.h"
#include "access.h"

namespace r = rotor;
namespace rt = rotor::test;

static std::uint32_t destroyed = 0;

struct init_shutdown_plugin_t;

namespace payload {
struct sample_payload_t {};
} // namespace payload

namespace message {
using sample_payload_t = r::message_t<payload::sample_payload_t>;
}

struct sample_sup_t : public rt::supervisor_test_t {
    using sup_base_t = rt::supervisor_test_t;

    using plugins_list_t = std::tuple<r::plugin::address_maker_plugin_t, r::plugin::locality_plugin_t,
                                      r::plugin::delivery_plugin_t<r::plugin::local_delivery_t>,
                                      r::plugin::lifetime_plugin_t, init_shutdown_plugin_t, /* use custom */
                                      r::plugin::foreigners_support_plugin_t, r::plugin::child_manager_plugin_t,
                                      r::plugin::starter_plugin_t>;

    std::uint32_t initialized = 0;
    std::uint32_t init_invoked = 0;
    std::uint32_t shutdown_started = 0;
    std::uint32_t shutdown_finished = 0;
    std::uint32_t shutdown_conf_invoked = 0;
    r::address_ptr_t shutdown_addr;

    using rt::supervisor_test_t::supervisor_test_t;

    ~sample_sup_t() override { ++destroyed; }

    void do_initialize(r::system_context_t *ctx) noexcept override {
        ++initialized;
        sup_base_t::do_initialize(ctx);
    }

    void shutdown_finish() noexcept override {
        ++shutdown_finished;
        rt::supervisor_test_t::shutdown_finish();
    }
};

struct init_shutdown_plugin_t : r::plugin::init_shutdown_plugin_t {
    using parent_t = r::plugin::init_shutdown_plugin_t;

    void deactivate() noexcept override { parent_t::deactivate(); }

    bool handle_shutdown(r::message::shutdown_request_t *message) noexcept override {
        auto sup = static_cast<sample_sup_t *>(actor);
        sup->shutdown_started++;
        return parent_t::handle_shutdown(message);
    }

    bool handle_init(r::message::init_request_t *message) noexcept override {
        auto sup = static_cast<sample_sup_t *>(actor);
        sup->init_invoked++;
        return parent_t::handle_init(message);
    }
};

struct sample_sup2_t : public rt::supervisor_test_t {
    using sup_base_t = rt::supervisor_test_t;

    std::uint32_t initialized = 0;
    std::uint32_t init_invoked = 0;
    std::uint32_t shutdown_finished = 0;
    std::uint32_t shutdown_conf_invoked = 0;
    r::address_ptr_t shutdown_addr;
    actor_base_t *init_child = nullptr;
    actor_base_t *shutdown_child = nullptr;
    std::error_code init_ec;
    std::error_code shutdown_ec;

    using rt::supervisor_test_t::supervisor_test_t;

    ~sample_sup2_t() override { ++destroyed; }

    void do_initialize(r::system_context_t *ctx) noexcept override {
        ++initialized;
        sup_base_t::do_initialize(ctx);
    }

    void init_finish() noexcept override {
        ++init_invoked;
        sup_base_t::init_finish();
    }

    virtual void shutdown_finish() noexcept override {
        ++shutdown_finished;
        rt::supervisor_test_t::shutdown_finish();
    }

    void on_child_init(actor_base_t *actor, const std::error_code &ec) noexcept override {
        init_child = actor;
        init_ec = ec;
    }

    void on_child_shutdown(actor_base_t *actor, const std::error_code &ec) noexcept override {
        shutdown_child = actor;
        shutdown_ec = ec;
    }
};

struct sample_sup3_t : public rt::supervisor_test_t {
    using sup_base_t = rt::supervisor_test_t;
    using rt::supervisor_test_t::supervisor_test_t;
    std::uint32_t received = 0;

    void make_subscription() noexcept {
        subscribe(&sample_sup3_t::on_sample);
        send<payload::sample_payload_t>(address);
    }

    void on_sample(message::sample_payload_t &) noexcept { ++received; }
};

struct unsubscriber_sup_t : public rt::supervisor_test_t {
    using sup_base_t = rt::supervisor_test_t;
    using rt::supervisor_test_t::supervisor_test_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        plugin.with_casted<r::plugin::starter_plugin_t>(
            [](auto &p) { p.subscribe_actor(&unsubscriber_sup_t::on_sample); });
    }

    void on_start() noexcept override {
        rt::supervisor_test_t::on_start();
        unsubscribe(&unsubscriber_sup_t::on_sample);
    }

    void on_sample(message::sample_payload_t &) noexcept {}
};

struct sample_actor_t : public r::actor_base_t {
    using r::actor_base_t::actor_base_t;
};

struct sample_actor2_t : public rt::actor_test_t {
    using rt::actor_test_t::actor_test_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { alternative = p.create_address(); });
        plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
            p.subscribe_actor(&sample_actor2_t::on_link, alternative);
            send<payload::sample_payload_t>(alternative);
        });
    }

    void on_link(message::sample_payload_t &) noexcept { ++received; }

    r::address_ptr_t alternative;
    int received = 0;
};

struct sample_actor3_t : public rt::actor_test_t {
    using rt::actor_test_t::actor_test_t;

    void shutdown_start() noexcept override {
        rt::actor_test_t::shutdown_start();
        resources->acquire();
    }
};

TEST_CASE("on_initialize, on_start, simple on_shutdown (handled by plugin)", "[supervisor]") {
    destroyed = 0;
    r::system_context_t *system_context = new r::system_context_t{};
    auto sup = system_context->create_supervisor<sample_sup_t>().timeout(rt::default_timeout).finish();

    REQUIRE(&sup->get_supervisor() == sup.get());
    REQUIRE(sup->initialized == 1);

    sup->do_process();
    CHECK(sup->init_invoked == 1);
    CHECK(sup->shutdown_started == 0);
    CHECK(sup->shutdown_conf_invoked == 0);
    CHECK(sup->active_timers.size() == 0);
    CHECK(sup->get_state() == r::state_t::OPERATIONAL);

    sup->do_shutdown(r::make_error_code(r::shutdown_code_t::normal));
    sup->do_process();
    REQUIRE(sup->shutdown_started == 1);
    REQUIRE(sup->shutdown_finished == 1);
    REQUIRE(sup->active_timers.size() == 0);

    REQUIRE(sup->get_state() == r::state_t::SHUT_DOWN);
    REQUIRE(sup->get_leader_queue().size() == 0);
    REQUIRE(sup->get_points().size() == 0);
    CHECK(rt::empty(sup->get_subscription()));

    REQUIRE(destroyed == 0);
    delete system_context;

    sup->shutdown_addr.reset();
    sup.reset();
    REQUIRE(destroyed == 1);
}

TEST_CASE("on_initialize, on_start, simple on_shutdown", "[supervisor]") {
    destroyed = 0;
    r::system_context_t *system_context = new r::system_context_t{};
    auto sup = system_context->create_supervisor<sample_sup2_t>().timeout(rt::default_timeout).finish();

    REQUIRE(&sup->get_supervisor() == sup.get());
    REQUIRE(sup->initialized == 1);
    REQUIRE(sup->init_child == nullptr);

    sup->do_process();
    REQUIRE(sup->init_invoked == 1);
    REQUIRE(sup->shutdown_conf_invoked == 0);
    REQUIRE(sup->active_timers.size() == 0);
    REQUIRE(sup->get_state() == r::state_t::OPERATIONAL);

    sup->do_shutdown(r::make_error_code(r::shutdown_code_t::normal));
    sup->do_process();
    REQUIRE(sup->shutdown_finished == 1);
    REQUIRE(sup->active_timers.size() == 0);

    REQUIRE(sup->get_state() == r::state_t::SHUT_DOWN);
    REQUIRE(sup->get_leader_queue().size() == 0);
    REQUIRE(sup->get_points().size() == 0);
    CHECK(rt::empty(sup->get_subscription()));
    REQUIRE(sup->shutdown_child == nullptr);

    REQUIRE(destroyed == 0);
    delete system_context;

    sup->shutdown_addr.reset();
    sup.reset();
    REQUIRE(destroyed == 1);
}

TEST_CASE("start/shutdown 1 child & 1 supervisor", "[supervisor]") {
    r::system_context_ptr_t system_context = new r::system_context_t();
    auto sup = system_context->create_supervisor<sample_sup2_t>().timeout(rt::default_timeout).finish();
    auto act = sup->create_actor<sample_actor_t>().timeout(rt::default_timeout).finish();

    /* for better coverage */
    auto &last = sup->access<rt::to::last_req_id>();
    sup->access<rt::to::request_map>()[last + 1] = r::request_curry_t();
    sup->do_process();

    CHECK(sup->access<rt::to::last_req_id>() > 1);

    CHECK(sup->get_state() == r::state_t::OPERATIONAL);
    CHECK(act->access<rt::to::state>() == r::state_t::OPERATIONAL);
    CHECK(act->access<rt::to::resources>()->has() == 0);
    CHECK(sup->init_child == act.get());
    CHECK(!sup->init_ec);
    CHECK(sup->shutdown_child == nullptr);

    sup->do_shutdown(r::make_error_code(r::shutdown_code_t::normal));
    sup->do_process();
    CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
    CHECK(act->access<rt::to::state>() == r::state_t::SHUT_DOWN);
    CHECK(sup->shutdown_child == act.get());
    CHECK(!sup->shutdown_ec);
}

TEST_CASE("custom subscription", "[supervisor]") {
    r::system_context_ptr_t system_context = new r::system_context_t();
    auto sup = system_context->create_supervisor<sample_sup3_t>().timeout(rt::default_timeout).finish();
    sup->do_process();
    CHECK(sup->get_state() == r::state_t::OPERATIONAL);

    sup->make_subscription();
    sup->do_process();
    CHECK(sup->received == 1);

    sup->do_shutdown(r::make_error_code(r::shutdown_code_t::normal));
    sup->do_process();
    CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
}

TEST_CASE("shutdown immediately", "[supervisor]") {
    r::system_context_ptr_t system_context = new r::system_context_t();
    auto sup = system_context->create_supervisor<sample_sup3_t>().timeout(rt::default_timeout).finish();
    sup->do_shutdown(r::make_error_code(r::shutdown_code_t::normal));
    sup->do_process();
    CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
}

TEST_CASE("self unsubscriber", "[actor]") {
    r::system_context_ptr_t system_context = new r::system_context_t();
    auto sup = system_context->create_supervisor<unsubscriber_sup_t>().timeout(rt::default_timeout).finish();
    sup->do_process();
    CHECK(sup->get_state() == r::state_t::OPERATIONAL);

    sup->do_shutdown(r::make_error_code(r::shutdown_code_t::normal));
    sup->do_process();
    CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
}

TEST_CASE("alternative address subscriber", "[actor]") {
    r::system_context_ptr_t system_context = new r::system_context_t();
    auto sup = system_context->create_supervisor<rt::supervisor_test_t>().timeout(rt::default_timeout).finish();
    auto act = sup->create_actor<sample_actor2_t>().timeout(rt::default_timeout).finish();
    sup->do_process();
    CHECK(sup->get_state() == r::state_t::OPERATIONAL);
    CHECK(act->get_state() == r::state_t::OPERATIONAL);
    CHECK(act->received == 1);

    sup->do_shutdown(r::make_error_code(r::shutdown_code_t::normal));
    sup->do_process();
    CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
    CHECK(act->get_state() == r::state_t::SHUT_DOWN);
}

TEST_CASE("acquire resources on shutdown start", "[actor]") {
    r::system_context_ptr_t system_context = new r::system_context_t();
    auto sup = system_context->create_supervisor<rt::supervisor_test_t>().timeout(rt::default_timeout).finish();
    auto act = sup->create_actor<sample_actor3_t>().timeout(rt::default_timeout).finish();
    sup->do_process();
    CHECK(sup->get_state() == r::state_t::OPERATIONAL);

    sup->do_shutdown(r::make_error_code(r::shutdown_code_t::normal));
    sup->do_process();
    CHECK(act->get_state() == r::state_t::SHUTTING_DOWN);

    act->access<rt::to::resources>()->release();
    sup->do_process();
    CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
    CHECK(act->get_state() == r::state_t::SHUT_DOWN);
}
