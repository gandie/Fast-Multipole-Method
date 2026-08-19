#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "math_utils.hpp"

using Catch::Approx;

TEST_CASE("BinomialTable computes classic values", "[math][binomial]") {
    fmm::BinomialTable table;
    table.init(6);

    REQUIRE(table.max_order == 6);
    REQUIRE(table(0, 0) == Approx(1.0));
    REQUIRE(table(6, 0) == Approx(1.0));
    REQUIRE(table(6, 6) == Approx(1.0));
    REQUIRE(table(6, 1) == Approx(6.0));
    REQUIRE(table(6, 2) == Approx(15.0));
    REQUIRE(table(6, 3) == Approx(20.0));
}

TEST_CASE("BinomialTable keeps data when asked for smaller order", "[math][binomial]") {
    fmm::BinomialTable table;
    table.init(7);
    const auto previous_max = table.max_order;

    table.init(4);

    REQUIRE(table.max_order == previous_max);
    REQUIRE(table(7, 4) == Approx(35.0));
}
