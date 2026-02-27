// test_spec.cpp - CommonMark spec test harness
//
// Runs all ~652 examples from spec.json in a single GTest, reporting per-example
// results. Using a single test avoids the 652x GTest fixture overhead and keeps
// startup instantaneous. A per-example timeout guards against infinite loops from
// unimplemented features (e.g. reference-style links).

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "html_renderer.hpp"
#include "parser.hpp"
#include "streaming_session.hpp"

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Spec example record
// ---------------------------------------------------------------------------

struct SpecExample {
    std::string markdown;
    std::string expected_html;
    int         example_number;
    std::string section;
    int         start_line;
    int         end_line;
};

// ---------------------------------------------------------------------------
// Load spec.json
// ---------------------------------------------------------------------------

static std::vector<SpecExample> load_spec(const std::string &path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Cannot open spec.json at: " + path);

    json arr;
    file >> arr;

    std::vector<SpecExample> out;
    out.reserve(arr.size());
    for (const auto &item : arr) {
        out.push_back({
            item["markdown"].get<std::string>(),
            item["html"].get<std::string>(),
            item["example"].get<int>(),
            item["section"].get<std::string>(),
            item["start_line"].get<int>(),
            item["end_line"].get<int>(),
        });
    }
    return out;
}

static std::string find_spec_json() {
    for (const char *p : {"md_examples/spec.json",
                          "../md_examples/spec.json",
                          "../../md_examples/spec.json"}) {
        if (std::ifstream(p).good())
            return p;
    }
    throw std::runtime_error("Could not locate spec.json");
}

// ---------------------------------------------------------------------------
// Parse + render one example (run in a thread so we can timeout)
// ---------------------------------------------------------------------------

static std::string parse_and_render(const std::string &markdown) {
    StreamingSession session;
    session.parse(markdown);
    session.finish();
    HtmlRenderer renderer;
    return renderer.render(session.parser().get_root());
}

// Run parse_and_render with a wall-clock timeout.
// Returns nullopt if the call does not complete within the deadline.
static std::optional<std::string>
parse_with_timeout(const std::string &markdown,
                   std::chrono::milliseconds limit =
                       std::chrono::milliseconds(500)) {
    std::optional<std::string> result;
    std::atomic<bool> done{false};

    std::thread worker([&] {
        result = parse_and_render(markdown);
        done.store(true, std::memory_order_release);
    });

    auto deadline = std::chrono::steady_clock::now() + limit;
    while (!done.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            // Detach; the thread will be leaked but the test can continue.
            worker.detach();
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    worker.join();
    return result;
}

// ---------------------------------------------------------------------------
// The single GTest that drives all examples
// ---------------------------------------------------------------------------

TEST(CommonMarkSpec, AllExamples) {
    std::vector<SpecExample> examples = load_spec(find_spec_json());

    int passed        = 0;
    int failed        = 0;
    int timed_out     = 0;

    // Collect failures for a summary at the end
    struct Failure {
        int         number;
        std::string section;
        std::string markdown;
        std::string expected;
        std::string actual;   // empty string means timed out
        bool        timeout;
    };
    std::vector<Failure> failures;

    // Per-section pass/fail counts for the summary table
    std::map<std::string, std::pair<int,int>> section_stats; // {pass, fail}

    for (const auto &ex : examples) {
        auto result = parse_with_timeout(ex.markdown);

        auto &[sec_pass, sec_fail] = section_stats[ex.section];

        if (!result.has_value()) {
            timed_out++;
            failed++;
            sec_fail++;
            failures.push_back({ex.example_number, ex.section,
                                 ex.markdown, ex.expected_html, "", true});
            continue;
        }

        if (*result == ex.expected_html) {
            passed++;
            sec_pass++;
        } else {
            failed++;
            sec_fail++;
            failures.push_back({ex.example_number, ex.section,
                                 ex.markdown, ex.expected_html, *result, false});
        }
    }

    int total = passed + failed;

    // -----------------------------------------------------------------
    // Per-section summary
    // -----------------------------------------------------------------
    std::printf("\n%-45s  %6s  %6s  %6s\n",
                "Section", "Pass", "Fail", "Total");
    std::printf("%s\n", std::string(68, '-').c_str());
    for (const auto &[sec, pf] : section_stats) {
        auto [p, f] = pf;
        std::printf("%-45s  %6d  %6d  %6d\n",
                    sec.c_str(), p, f, p + f);
    }
    std::printf("%s\n", std::string(68, '-').c_str());
    std::printf("%-45s  %6d  %6d  %6d\n",
                "TOTAL", passed, failed, total);
    if (timed_out)
        std::printf("  (%d timed out)\n", timed_out);
    std::printf("\n");

    // -----------------------------------------------------------------
    // Per-failure detail (capped to avoid wall of text)
    // -----------------------------------------------------------------
    constexpr int MAX_DETAIL = 40;
    int shown = 0;
    for (const auto &f : failures) {
        if (shown >= MAX_DETAIL) {
            std::printf("  ... (%zu more failures not shown)\n",
                        failures.size() - MAX_DETAIL);
            break;
        }
        if (f.timeout) {
            std::printf("TIMEOUT  #%d [%s]\n"
                        "  Input:    %s\n\n",
                        f.number, f.section.c_str(),
                        json(f.markdown).dump().c_str());
        } else {
            std::printf("FAIL  #%d [%s]\n"
                        "  Input:    %s\n"
                        "  Expected: %s\n"
                        "  Actual:   %s\n\n",
                        f.number, f.section.c_str(),
                        json(f.markdown).dump().c_str(),
                        json(f.expected).dump().c_str(),
                        json(f.actual).dump().c_str());
        }
        shown++;
    }

    // Fail the test only if anything went wrong
    EXPECT_EQ(failed, 0)
        << passed << " passed, " << failed << " failed"
        << (timed_out ? " (" + std::to_string(timed_out) + " timed out)" : "")
        << " out of " << total << " total.";
}
