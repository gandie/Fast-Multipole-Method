#ifndef SIM_OPTIONS_HPP
#define SIM_OPTIONS_HPP

#include <string>
#include <string_view>
#include <vector>

namespace sim {

struct SimulationOptions {
    int rebuild_every = 1;
    int cluster_bodies = 40000;
    int uniform_bodies = 10000;
    int orbit_bodies = 0;
    double black_hole_mass = 0.0;
    std::string scenario_file;
    bool help_requested = false;
};

struct ParseResult {
    bool ok = false;
    SimulationOptions options{};
    std::string error_message;
};

inline bool parseIntValue(const std::string& text, int& out_value) {
    try {
        size_t consumed = 0;
        out_value = std::stoi(text, &consumed);
        return consumed == text.size();
    } catch (...) {
        return false;
    }
}

inline ParseResult parseSimulationArgs(const std::vector<std::string>& args) {
    ParseResult result;
    result.ok = true;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];

        auto parseIntArg = [&](std::string_view arg_name, int& output) -> bool {
            if (i + 1 >= args.size()) {
                result.ok = false;
                result.error_message = "Error: argument requires a value: " + std::string(arg_name);
                return false;
            }
            int parsed = 0;
            if (!parseIntValue(args[i + 1], parsed)) {
                result.ok = false;
                result.error_message = "Error: invalid integer value: " + args[i + 1];
                return false;
            }
            output = parsed;
            ++i;
            return true;
        };

        auto parseStringArg = [&](std::string_view arg_name, std::string& output) -> bool {
            if (i + 1 >= args.size()) {
                result.ok = false;
                result.error_message = "Error: argument requires a value: " + std::string(arg_name);
                return false;
            }
            output = args[i + 1];
            if (output.empty()) {
                result.ok = false;
                result.error_message = "Error: argument value cannot be empty: " + std::string(arg_name);
                return false;
            }
            ++i;
            return true;
        };

        if (arg == "-h" || arg == "--help") {
            result.options.help_requested = true;
            return result;
        }
        if (arg == "-r" || arg == "--rebuild-every") {
            if (!parseIntArg(arg, result.options.rebuild_every)) return result;
            continue;
        }
        if (arg == "-c" || arg == "--cluster-bodies") {
            if (!parseIntArg(arg, result.options.cluster_bodies)) return result;
            continue;
        }
        if (arg == "-u" || arg == "--uniform-bodies") {
            if (!parseIntArg(arg, result.options.uniform_bodies)) return result;
            continue;
        }
        if (arg == "-o" || arg == "--orbit") {
            if (!parseIntArg(arg, result.options.orbit_bodies)) return result;
            continue;
        }
        if (arg == "-b" || arg == "--black-hole") {
            int black_hole_mass_int = 0;
            if (!parseIntArg(arg, black_hole_mass_int)) return result;
            result.options.black_hole_mass = static_cast<double>(black_hole_mass_int);
            continue;
        }
        if (arg == "-s" || arg == "--scenario") {
            if (!parseStringArg(arg, result.options.scenario_file)) return result;
            continue;
        }

        result.ok = false;
        result.error_message = "Error: unknown argument: " + arg;
        return result;
    }

    if (result.options.rebuild_every < 1) {
        result.ok = false;
        result.error_message = "Error: rebuild-every must be >= 1";
        return result;
    }

    if (!result.options.scenario_file.empty()) {
        // Scenario mode ignores generated body controls by policy.
        return result;
    }

    if (result.options.cluster_bodies < 0 || result.options.uniform_bodies < 0 || result.options.orbit_bodies < 0) {
        result.ok = false;
        result.error_message = "Error: body counts cannot be negative";
        return result;
    }
    if (result.options.cluster_bodies + result.options.uniform_bodies + result.options.orbit_bodies == 0) {
        result.ok = false;
        result.error_message = "Error: total body count must be > 0";
        return result;
    }
    if (result.options.black_hole_mass < 0.0) {
        result.ok = false;
        result.error_message = "Error: black hole mass cannot be negative";
        return result;
    }
    if (result.options.orbit_bodies > 0 && result.options.black_hole_mass <= 0.0) {
        result.ok = false;
        result.error_message = "Error: orbit bodies require --black-hole with positive mass";
        return result;
    }

    return result;
}

}  // namespace sim

#endif
