#include "cli.hpp"

#include "analyzer.hpp"
#include "config.hpp"
#include "discover.hpp"
#include "exporter.hpp"
#include "prober/prober.hpp"
#include "storage/index.hpp"
#include "storage/seal.hpp"
#include "util/log.hpp"
#include "util/time.hpp"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace pt {

namespace {

std::atomic<bool> g_stop{false};

BOOL WINAPI ctrl_handler(DWORD type) {
    switch (type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            g_stop.store(true);
            return TRUE;
        default:
            return FALSE;
    }
}

void print_usage() {
    std::puts(
        "pingtrace - continuous reachability prober\n"
        "\n"
        "Usage:\n"
        "  pingtrace run [--config <path>]\n"
        "      Continuous ICMP/TCP probing (default config.json).\n"
        "\n"
        "  pingtrace discover <dest> [--add] [--config <path>]\n"
        "      Traceroute to <dest>; with --add, append the hops as ladder targets.\n"
        "\n"
        "  pingtrace analyze [--from <t>] [--to <t>] [--target <name|id>] [--by-hour] [--heatmap] [--ladder]\n"
        "      Offline report: per-target summary, outage list, loss-by-hour, heatmap, path ladders.\n"
        "\n"
        "  pingtrace export --from <t> [--to <t>] [--target <name|id>] [--out <file.csv>]\n"
        "      Dump raw records in a window to CSV (stdout if no --out).\n"
        "\n"
        "  pingtrace verify [--config <path>]\n"
        "      Seal any dangling .part files and rebuild the segment index.\n"
        "\n"
        "Time args (--from/--to): 'YYYY-MM-DD[ HH:MM[:SS]]' (local), '<N>d|h|m' ago, or 'now'.\n");
}

// Returns the value following `flag` in args, if present.
std::optional<std::string> flag_value(const std::vector<std::string>& args,
                                      const std::string& flag) {
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == flag) return args[i + 1];
    }
    return std::nullopt;
}

bool has_flag(const std::vector<std::string>& args, const std::string& flag) {
    for (const auto& a : args) if (a == flag) return true;
    return false;
}

// Loads the config named by --config (default config.json) or logs and returns false.
bool load_cfg(const std::vector<std::string>& args, Config& cfg) {
    const std::string path = flag_value(args, "--config").value_or("config.json");
    try {
        cfg = load_config(path);
        return true;
    } catch (const std::exception& e) {
        log_error(std::string("config: ") + e.what());
        return false;
    }
}

int cmd_run(const std::vector<std::string>& args) {
    const std::string config_path = flag_value(args, "--config").value_or("config.json");

    Config cfg;
    try {
        cfg = load_config(config_path);
    } catch (const std::exception& e) {
        log_error(std::string("config: ") + e.what());
        return 1;
    }
    log_info("loaded config '" + config_path + "' with " +
             std::to_string(cfg.targets.size()) + " target(s)");

    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    // Optional self-stop after N seconds (testing / bounded runs).
    std::thread timer;
    if (auto d = flag_value(args, "--duration")) {
        const int secs = std::atoi(d->c_str());
        if (secs > 0) {
            log_info("will stop after " + std::to_string(secs) + " s");
            timer = std::thread([secs] {
                for (int i = 0; i < secs * 10 && !g_stop.load(); ++i) Sleep(100);
                g_stop.store(true);
            });
        }
    }
    log_info("press Ctrl+C to stop");

    uint32_t sim_gap = 0;
    if (auto s = flag_value(args, "--simulate-gap-ms")) sim_gap = static_cast<uint32_t>(std::atoi(s->c_str()));
    const int rc = run_prober(cfg, &g_stop, sim_gap);
    g_stop.store(true);
    if (timer.joinable()) timer.join();
    return rc;
}

int cmd_verify(const std::vector<std::string>& args) {
    const std::string config_path = flag_value(args, "--config").value_or("config.json");

    Config cfg;
    try {
        cfg = load_config(config_path);
    } catch (const std::exception& e) {
        log_error(std::string("config: ") + e.what());
        return 1;
    }

    Index index(cfg.storage.dir);
    const size_t recovered = recover_dangling_parts(cfg.storage.dir, index);
    if (recovered > 0) log_info("sealed " + std::to_string(recovered) + " dangling segment(s)");
    const size_t n = index.rebuild();
    log_info("index rebuilt: " + std::to_string(n) + " segment(s) -> " + index.path());
    return 0;
}

}  // namespace

int cmd_export(const std::vector<std::string>& args) {
    Config cfg;
    if (!load_cfg(args, cfg)) return 1;

    const uint64_t now = now_utc_ms();
    uint64_t from = 0, to = now;
    if (auto s = flag_value(args, "--from")) {
        if (!parse_time_arg(*s, now, from)) { log_error("bad --from '" + *s + "'"); return 1; }
    } else {
        log_error("export requires --from");
        return 1;
    }
    if (auto s = flag_value(args, "--to")) {
        if (!parse_time_arg(*s, now, to)) { log_error("bad --to '" + *s + "'"); return 1; }
    }
    const std::string target = flag_value(args, "--target").value_or("");
    const std::string out    = flag_value(args, "--out").value_or("");
    return run_export(cfg, from, to, target, out);
}

int cmd_analyze(const std::vector<std::string>& args) {
    Config cfg;
    if (!load_cfg(args, cfg)) return 1;

    const uint64_t now = now_utc_ms();
    AnalyzeOptions opt;
    opt.to_utc = now;
    opt.from_utc = (now > 30ULL * 86400000ULL) ? now - 30ULL * 86400000ULL : 0;  // default 30d
    if (auto s = flag_value(args, "--from")) {
        if (!parse_time_arg(*s, now, opt.from_utc)) { log_error("bad --from '" + *s + "'"); return 1; }
    }
    if (auto s = flag_value(args, "--to")) {
        if (!parse_time_arg(*s, now, opt.to_utc)) { log_error("bad --to '" + *s + "'"); return 1; }
    }
    opt.target_filter = flag_value(args, "--target").value_or("");
    opt.by_hour = has_flag(args, "--by-hour");
    opt.heatmap = has_flag(args, "--heatmap");
    opt.ladder  = has_flag(args, "--ladder");
    return run_analyze(cfg, opt);
}

int run_cli(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

    if (args.empty() || args[0] == "-h" || args[0] == "--help" || args[0] == "help") {
        print_usage();
        return args.empty() ? 1 : 0;
    }

    const std::string& cmd = args[0];
    const std::vector<std::string> rest(args.begin() + 1, args.end());

    if (cmd == "run") return cmd_run(rest);
    if (cmd == "verify") return cmd_verify(rest);
    if (cmd == "analyze") return cmd_analyze(rest);
    if (cmd == "export") return cmd_export(rest);

    if (cmd == "discover") {
        if (rest.empty() || rest[0].rfind("--", 0) == 0) {
            log_error("discover requires a destination, e.g. 'discover api.bybit.com'");
            return 2;
        }
        const std::string config_path = flag_value(rest, "--config").value_or("config.json");
        return run_discover(rest[0], has_flag(rest, "--add"), config_path);
    }

    log_error("unknown command '" + cmd + "'");
    print_usage();
    return 2;
}

}  // namespace pt
