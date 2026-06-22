#ifdef NDEBUG
#undef NDEBUG
#endif

#include "../src/llama-grammar.h"

#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <string>

struct chart_stats {
    size_t origins = 0;
    size_t sealed = 0;
    size_t current_items = 0;
    size_t stored_items = 0;
    size_t resume_entries = 0;
};

static bool is_end_of_sequence(const llama_grammar_element * pos) {
    return pos->type == LLAMA_GRETYPE_END || pos->type == LLAMA_GRETYPE_ALT;
}

static llama_grammar * build_grammar(const std::string & grammar_str) {
    llama_grammar * grammar = llama_grammar_init_impl(nullptr, grammar_str.c_str(), "root", false, nullptr, 0, nullptr, 0);
    assert(grammar != nullptr);
    return grammar;
}

static bool grammar_is_complete(const llama_grammar * grammar) {
    for (const llama_grammar_item & item : grammar->chart.back().items) {
        if (item.rule == grammar->start_rule_index && item.origin == 0 &&
                is_end_of_sequence(&grammar->rules[item.rule][item.dot])) {
            return true;
        }
    }

    return false;
}

static bool accept_token_piece(llama_grammar * grammar, llama_token token, const std::string & piece) {
    try {
        llama_grammar_accept_token(*grammar, token, piece);
    } catch (const std::runtime_error &) {
        return false;
    }

    return !grammar->chart.back().items.empty();
}

static bool match_string(const std::string & grammar_str, const std::string & input) {
    llama_grammar * grammar = build_grammar(grammar_str);
    bool matched = true;

    for (unsigned char c : input) {
        const std::string piece(1, static_cast<char>(c));
        if (!accept_token_piece(grammar, 0, piece)) {
            matched = false;
            break;
        }
    }

    matched = matched && grammar_is_complete(grammar);
    llama_grammar_free_impl(grammar);
    return matched;
}

static chart_stats get_chart_stats(const llama_grammar * grammar) {
    chart_stats stats;
    stats.origins = grammar->chart.size();
    stats.current_items = grammar->chart.back().items.size();

    for (const llama_grammar_chart_column & column : grammar->chart) {
        if (column.sealed) {
            ++stats.sealed;
        }
        stats.stored_items += column.items.size();
        for (const auto & resume : column.resume) {
            ++stats.resume_entries;
            stats.stored_items += resume.second.items.size();
        }
    }

    return stats;
}

static void print_stats(const char * label, size_t n, const chart_stats & stats) {
    std::fprintf(
            stdout,
            "%s n=%zu origins=%zu sealed=%zu current_items=%zu stored_items=%zu resume_entries=%zu\n",
            label,
            n,
            stats.origins,
            stats.sealed,
            stats.current_items,
            stats.stored_items,
            stats.resume_entries);
    std::fflush(stdout);
}

static chart_stats run_a_star(size_t n) {
    llama_grammar * grammar = build_grammar(R"""(root ::= "a"*)""");

    for (size_t i = 0; i < n; ++i) {
        assert(accept_token_piece(grammar, 0, "a"));
    }

    assert(grammar_is_complete(grammar));
    const chart_stats stats = get_chart_stats(grammar);
    print_stats("astar", n, stats);
    llama_grammar_free_impl(grammar);
    return stats;
}

static void test_a_star_plateau(size_t n) {
    const chart_stats stats = run_a_star(n);

    assert(stats.origins <= 8);
    assert(stats.current_items <= 8);
    assert(stats.stored_items <= 64);
}

static void test_balanced_exact(size_t max_n) {
    const std::string grammar = R"""(root ::= "a" root "b" | "")""";

    assert(match_string(grammar, ""));
    for (size_t n = 1; n <= max_n; ++n) {
        const std::string a(n, 'a');
        const std::string b(n, 'b');

        assert(match_string(grammar, a + b));
        assert(!match_string(grammar, a + std::string(n - 1, 'b')));
        assert(!match_string(grammar, a + std::string(n + 1, 'b')));
    }

    llama_grammar * grammar_state = build_grammar(grammar);
    for (size_t i = 0; i < max_n; ++i) {
        assert(accept_token_piece(grammar_state, 0, "a"));
    }

    const chart_stats stats = get_chart_stats(grammar_state);
    print_stats("balanced-prefix", max_n, stats);
    assert(stats.origins >= max_n / 2);

    llama_grammar_free_impl(grammar_state);
}

int main(int argc, char ** argv) {
    const bool long_run = argc > 1 && std::string(argv[1]) == "--long";

    test_a_star_plateau(long_run ? 100000 : 1000);
    test_balanced_exact(long_run ? 512 : 64);

    return 0;
}
