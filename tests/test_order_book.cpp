#include "lob/order_book.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

using lob::Fill;
using lob::OrderBook;
using lob::OrderSpec;
using lob::Side;

TEST_CASE("empty add cancel restores empty", "[book]") {
    OrderBook book(1024);
    REQUIRE(book.order_count() == 0);
    REQUIRE(book.rest(OrderSpec{1, Side::Bid, 100, 10}));
    REQUIRE(book.order_count() == 1);
    REQUIRE(book.cancel(1));
    REQUIRE(book.order_count() == 0);
    const auto top = book.top();
    REQUIRE_FALSE(top.has_bid);
    REQUIRE_FALSE(top.has_ask);
}

TEST_CASE("FIFO: first resting order fills first", "[book][fifo]") {
    OrderBook book(1024);
    REQUIRE(book.rest(OrderSpec{1, Side::Ask, 100, 5}));
    REQUIRE(book.rest(OrderSpec{2, Side::Ask, 100, 5}));

    std::vector<Fill> fills;
    REQUIRE(book.add_limit(OrderSpec{10, Side::Bid, 100, 5}, &fills));

    REQUIRE(fills.size() == 1);
    REQUIRE(fills[0].resting_id == 1);
    REQUIRE(fills[0].qty == 5);
    REQUIRE(book.find(1) == nullptr);
    REQUIRE(book.find(2) != nullptr);
    REQUIRE(book.find(2)->qty == 5);
}

TEST_CASE("partial fill leaves remainder at head", "[book]") {
    OrderBook book(1024);
    REQUIRE(book.rest(OrderSpec{1, Side::Ask, 100, 10}));

    std::vector<Fill> fills;
    REQUIRE(book.add_limit(OrderSpec{10, Side::Bid, 100, 4}, &fills));

    REQUIRE(fills.size() == 1);
    REQUIRE(fills[0].qty == 4);
    REQUIRE(book.find(1) != nullptr);
    REQUIRE(book.find(1)->qty == 6);
    REQUIRE(book.level_volume(Side::Ask, 100) == 6);
}

TEST_CASE("cancel middle of DLL keeps neighbors linked", "[book]") {
    OrderBook book(1024);
    REQUIRE(book.rest(OrderSpec{1, Side::Bid, 50, 1}));
    REQUIRE(book.rest(OrderSpec{2, Side::Bid, 50, 1}));
    REQUIRE(book.rest(OrderSpec{3, Side::Bid, 50, 1}));

    REQUIRE(book.cancel(2));
    REQUIRE(book.find(2) == nullptr);
    REQUIRE(book.find(1)->next == book.find(3));
    REQUIRE(book.find(3)->prev == book.find(1));
    REQUIRE(book.level_volume(Side::Bid, 50) == 2);
    REQUIRE(book.sum_order_qty_at(Side::Bid, 50) == 2);
}

TEST_CASE("level volume equals sum of order qtys", "[book]") {
    OrderBook book(1024);
    REQUIRE(book.rest(OrderSpec{1, Side::Ask, 101, 3}));
    REQUIRE(book.rest(OrderSpec{2, Side::Ask, 101, 7}));
    REQUIRE(book.rest(OrderSpec{3, Side::Ask, 102, 4}));
    REQUIRE(book.level_volume(Side::Ask, 101) == book.sum_order_qty_at(Side::Ask, 101));
    REQUIRE(book.level_volume(Side::Ask, 101) == 10);
}

TEST_CASE("replay execute reduces resting size", "[book][replay]") {
    OrderBook book(1024);
    REQUIRE(book.rest(OrderSpec{1, Side::Bid, 99, 10}));
    REQUIRE(book.execute(1, 4));
    REQUIRE(book.find(1)->qty == 6);
    REQUIRE(book.execute(1, 6));
    REQUIRE(book.find(1) == nullptr);
}

TEST_CASE("replay book stays uncrossed with sane events", "[book][replay]") {
    OrderBook book(1024);
    REQUIRE(book.rest(OrderSpec{1, Side::Bid, 99, 10}));
    REQUIRE(book.rest(OrderSpec{2, Side::Ask, 101, 10}));
    REQUIRE_FALSE(book.crossed());
    REQUIRE(book.rest(OrderSpec{3, Side::Bid, 100, 5}));
    REQUIRE_FALSE(book.crossed());
    const auto top = book.top();
    REQUIRE(top.bid_price == 100);
    REQUIRE(top.ask_price == 101);
}

TEST_CASE("match walks multiple levels", "[book][match]") {
    OrderBook book(1024);
    REQUIRE(book.rest(OrderSpec{1, Side::Ask, 100, 3}));
    REQUIRE(book.rest(OrderSpec{2, Side::Ask, 101, 3}));

    std::vector<Fill> fills;
    REQUIRE(book.add_limit(OrderSpec{9, Side::Bid, 101, 5}, &fills));

    REQUIRE(fills.size() == 2);
    REQUIRE(fills[0].resting_id == 1);
    REQUIRE(fills[0].qty == 3);
    REQUIRE(fills[1].resting_id == 2);
    REQUIRE(fills[1].qty == 2);
    REQUIRE(book.find(2)->qty == 1);
    REQUIRE(book.find(9) == nullptr);  // fully filled, nothing rests
}

TEST_CASE("partial cancel by qty", "[book]") {
    OrderBook book(1024);
    REQUIRE(book.rest(OrderSpec{1, Side::Bid, 50, 10}));
    REQUIRE(book.cancel(1, 4));
    REQUIRE(book.find(1)->qty == 6);
}

TEST_CASE("cancel microbenchmark", "[bench]") {
    constexpr std::size_t N = 100000;
    OrderBook book(N + 16);

    for (std::size_t i = 0; i < N; ++i) {
        // Spread orders across prices so map stays realistic
        const int64_t px = 1000 + static_cast<int64_t>(i % 500);
        REQUIRE(book.rest(OrderSpec{i + 1, Side::Bid, px, 1}));
    }

    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < N; ++i) {
        book.cancel(i + 1);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double ns =
        std::chrono::duration<double, std::nano>(t1 - t0).count();
    const double ops_per_s = (N * 1e9) / ns;
    const double ns_per_op = ns / N;

    WARN("cancel benchmark: " << N << " cancels, " << ns_per_op << " ns/op, "
                               << (ops_per_s / 1e6) << " Mops/s (single-threaded)");
    REQUIRE(book.order_count() == 0);
    REQUIRE(ops_per_s > 0);
}
