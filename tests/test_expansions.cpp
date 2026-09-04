#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>
#include <vector>

#include "local_expansion.hpp"
#include "multipole_expansion.hpp"

using Catch::Approx;

namespace {

double directPotential(const std::vector<fmm::Source>& sources, const Complex& target) {
    Complex sum{0.0, 0.0};
    for (const auto& s : sources) {
        sum += s.q * std::log(target - s.position);
    }
    return sum.real();
}

Complex directForce(const std::vector<fmm::Source>& sources, const Complex& target) {
    Complex total{0.0, 0.0};
    for (const auto& s : sources) {
        const double dx = target.real() - s.position.real();
        const double dy = target.imag() - s.position.imag();
        const double r2 = dx * dx + dy * dy;
        total += Complex{-s.q * dx / r2, -s.q * dy / r2};
    }
    return total;
}

}  // namespace

TEST_CASE("Multipole expansion approximates direct field far from sources", "[expansion][multipole]") {
    std::vector<fmm::Source> sources{{1.5, -0.75, 2.0}, {-1.0, 0.25, 1.5}};

    const Complex center{0.0, 0.0};
    constexpr int order = 12;
    fmm::MultipoleExpansion me(center, order, sources.begin(), sources.end());

    const Complex target{20.0, 15.0};
    const double exact_potential = directPotential(sources, target);
    const Complex exact_force = directForce(sources, target);

    const double approx_potential = me.evaluatePotential(target);
    const Complex approx_force = me.evaluateForce(target);

    REQUIRE(approx_potential == Approx(exact_potential).epsilon(1e-6));
    REQUIRE(approx_force.real() == Approx(exact_force.real()).epsilon(1e-6));
    REQUIRE(approx_force.imag() == Approx(exact_force.imag()).epsilon(1e-6));
}

TEST_CASE("Multipole expansion shift preserves evaluation", "[expansion][multipole]") {
    std::vector<fmm::Source> sources{{-2.0, 1.0, 1.0}, {0.5, -1.5, 3.0}};

    constexpr int order = 10;
    fmm::MultipoleExpansion me_a({0.0, 0.0}, order, sources.begin(), sources.end());

    std::array<const fmm::MultipoleExpansion*, 1> in{&me_a};
    fmm::MultipoleExpansion me_b({2.0, -1.0}, std::span<const fmm::MultipoleExpansion* const>(in.data(), in.size()));

    const Complex target{25.0, -18.0};
    REQUIRE(me_a.evaluatePotential(target) == Approx(me_b.evaluatePotential(target)).epsilon(1e-8));

    const Complex f_a = me_a.evaluateForce(target);
    const Complex f_b = me_b.evaluateForce(target);
    REQUIRE(f_a.real() == Approx(f_b.real()).epsilon(1e-8));
    REQUIRE(f_a.imag() == Approx(f_b.imag()).epsilon(1e-8));
}

TEST_CASE("Local expansion from far sources approximates direct field near center", "[expansion][local]") {
    std::vector<fmm::Source> far_sources{{18.0, 15.0, 2.0}, {-20.0, 12.0, 1.0}};

    constexpr int order = 12;
    const Complex local_center{0.0, 0.0};
    fmm::LocalExpansion local(local_center, order, far_sources.begin(), far_sources.end());

    const Complex target{0.5, -0.4};
    const double exact_potential = directPotential(far_sources, target);
    const Complex exact_force = directForce(far_sources, target);

    REQUIRE(local.evaluatePotential(target) == Approx(exact_potential).epsilon(1e-6));

    const Complex approx_force = local.evaluateForce(target);
    REQUIRE(approx_force.real() == Approx(exact_force.real()).epsilon(1e-6));
    REQUIRE(approx_force.imag() == Approx(exact_force.imag()).epsilon(1e-6));
}
