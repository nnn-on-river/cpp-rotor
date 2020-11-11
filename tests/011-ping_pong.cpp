//
// Copyright (c) 2019-2020 Ivan Baidakou (basiliscos) (the dot dmol at gmail dot com)
//
// Distributed under the MIT Software License
//

#include "catch.hpp"
#include "rotor.hpp"
#include "access.h"
#include "supervisor_test.h"

namespace r = rotor;
namespace rt = r::test;

struct ping_t {};
struct pong_t {};

static std::uint32_t destroyed = 0;

struct pinger_t : public r::actor_base_t {
    std::uint32_t ping_sent = 0;
    std::uint32_t pong_received = 0;

    using r::actor_base_t::actor_base_t;
    ~pinger_t() override { destroyed += 1; }

    void set_ponger_addr(const r::address_ptr_t &addr) { ponger_addr = addr; }

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        plugin.with_casted<r::plugin::starter_plugin_t>([](auto &p) { p.subscribe_actor(&pinger_t::on_pong); });
    }

    void on_start() noexcept override {
        r::actor_base_t::on_start();
        ++ping_sent;
        send<ping_t>(ponger_addr);
    }

    void on_pong(r::message_t<pong_t> &) noexcept { ++pong_received; }

    r::address_ptr_t ponger_addr;
};

struct ponger_t : public r::actor_base_t {
    std::uint32_t ping_received = 0;
    std::uint32_t pong_sent = 0;

    using r::actor_base_t::actor_base_t;
    ~ponger_t() override { destroyed += 3; }

    void set_pinger_addr(const r::address_ptr_t &addr) { pinger_addr = addr; }

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        plugin.with_casted<r::plugin::starter_plugin_t>([](auto &p) { p.subscribe_actor(&ponger_t::on_ping); });
    }

    void on_ping(r::message_t<ping_t> &) noexcept {
        ++ping_received;
        send<pong_t>(pinger_addr);
        ++pong_sent;
    }

  private:
    r::address_ptr_t pinger_addr;
};

TEST_CASE("ping-pong", "[supervisor]") {
    r::system_context_t system_context;

    auto sup = system_context.create_supervisor<rt::supervisor_test_t>().timeout(rt::default_timeout).finish();
    auto pinger = sup->create_actor<pinger_t>().timeout(rt::default_timeout).finish();
    auto ponger = sup->create_actor<ponger_t>().timeout(rt::default_timeout).finish();

    pinger->set_ponger_addr(ponger->get_address());
    ponger->set_pinger_addr(pinger->get_address());

    sup->do_process();
    REQUIRE(pinger->ping_sent == 1);
    REQUIRE(pinger->pong_received == 1);
    REQUIRE(ponger->pong_sent == 1);
    REQUIRE(ponger->ping_received == 1);

    sup->do_shutdown(r::make_error_code(r::shutdown_code_t::normal));
    sup->do_process();
    REQUIRE(sup->get_state() == r::state_t::SHUT_DOWN);
    REQUIRE(sup->get_leader_queue().size() == 0);
    REQUIRE(sup->get_points().size() == 0);
    CHECK(rt::empty(sup->get_subscription()));

    pinger.reset();
    ponger.reset();
    REQUIRE(destroyed == 4);
}
