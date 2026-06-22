#ifdef NDEBUG
#undef NDEBUG
#endif

#include "../src/llama-grammar.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

struct token_piece {
    llama_token token;
    std::string piece;
};

struct corpus_case {
    const char * name;
    const char * grammar;
    std::vector<token_piece> input;
};

template <typename T, typename = void>
struct has_chart : std::false_type {};

template <typename T>
struct has_chart<T, std::void_t<decltype(std::declval<T>().chart)>> : std::true_type {};

template <typename T, typename = void>
struct has_stacks : std::false_type {};

template <typename T>
struct has_stacks<T, std::void_t<decltype(std::declval<T>().stacks)>> : std::true_type {};

static bool is_end_of_sequence(const llama_grammar_element * pos) {
    return pos->type == LLAMA_GRETYPE_END || pos->type == LLAMA_GRETYPE_ALT;
}

static llama_grammar * build_grammar(const char * grammar_str) {
    llama_grammar * grammar = llama_grammar_init_impl(nullptr, grammar_str, "root", false, nullptr, 0, nullptr, 0);
    assert(grammar != nullptr);
    return grammar;
}

template <typename Grammar>
static bool grammar_has_items(const Grammar * grammar) {
    if constexpr (has_chart<Grammar>::value) {
        return !grammar->chart.back().items.empty();
    } else {
        static_assert(has_stacks<Grammar>::value, "unknown grammar storage");
        return !grammar->stacks.empty();
    }
}

template <typename Grammar>
static bool grammar_is_complete(const Grammar * grammar) {
    if constexpr (has_chart<Grammar>::value) {
        using item_type = typename std::decay_t<decltype(grammar->chart.back().items)>::value_type;
        for (const item_type & item : grammar->chart.back().items) {
            if (item.rule == grammar->start_rule_index && item.origin == 0 &&
                    is_end_of_sequence(&grammar->rules[item.rule][item.dot])) {
                return true;
            }
        }
        return false;
    } else {
        static_assert(has_stacks<Grammar>::value, "unknown grammar storage");
        for (const llama_grammar_stack & stack : grammar->stacks) {
            if (stack.empty()) {
                return true;
            }
        }
        return false;
    }
}

static bool accept_token_piece(llama_grammar * grammar, const token_piece & piece, bool * complete) {
    try {
        llama_grammar_accept_token(*grammar, piece.token, piece.piece);
    } catch (const std::runtime_error &) {
        *complete = false;
        return false;
    }

    const bool accepted = grammar_has_items(grammar);
    *complete = accepted && grammar_is_complete(grammar);
    return accepted;
}

static void hash_byte(uint64_t * hash, uint8_t byte) {
    *hash ^= byte;
    *hash *= 1099511628211ull;
}

static void hash_string(uint64_t * hash, const std::string & value) {
    for (unsigned char c : value) {
        hash_byte(hash, c);
    }
    hash_byte(hash, 0xff);
}

static void hash_bool(uint64_t * hash, bool value) {
    hash_byte(hash, value ? 1 : 0);
}

static std::string hex_piece(const std::string & value) {
    static const char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * 2);
    for (unsigned char c : value) {
        result.push_back(hex[c >> 4]);
        result.push_back(hex[c & 0x0f]);
    }
    return result;
}

static std::vector<token_piece> repeat_piece(size_t n, llama_token token, const std::string & piece) {
    std::vector<token_piece> result;
    result.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        result.push_back({ token, piece });
    }
    return result;
}

static std::vector<corpus_case> build_corpus() {
    std::vector<corpus_case> corpus;

    corpus.push_back({
        "astar-cross-boundaries",
        R"""(root ::= "a"*)""",
        repeat_piece(2048, 0, "a"),
    });

    std::vector<token_piece> token_case = {{10, "<[10]>"}};
    for (size_t i = 0; i < 128; ++i) {
        token_case.push_back({0, "x"});
    }
    token_case.push_back({11, "<[11]>"});
    corpus.push_back({
        "token-terminal-cross-boundaries",
        R"""(root ::= <[10]> "x"* <[11]>)""",
        token_case,
    });

    std::vector<token_piece> token_not_case = {{10, "<[10]>"}};
    for (llama_token tok = 20; tok < 148; ++tok) {
        token_not_case.push_back({tok, "<[" + std::to_string(tok) + "]>"});
    }
    token_not_case.push_back({11, "<[11]>"});
    corpus.push_back({
        "token-not-cross-boundaries",
        R"""(root ::= <[10]> (!<[11]>)* <[11]>)""",
        token_not_case,
    });

    corpus.push_back({
        "split-utf8-2-byte",
        R"""(root ::= "\u00E9")""",
        {{100, std::string("\xC3", 1)}, {101, std::string("\xA9", 1)}},
    });

    corpus.push_back({
        "split-utf8-3-byte",
        R"""(root ::= "\u20AC")""",
        {{100, std::string("\xE2", 1)}, {101, std::string("\x82", 1)}, {102, std::string("\xAC", 1)}},
    });

    corpus.push_back({
        "split-utf8-4-byte",
        R"""(root ::= "\U0001F600")""",
        {{100, std::string("\xF0", 1)}, {101, std::string("\x9F", 1)}, {102, std::string("\x98", 1)}, {103, std::string("\x80", 1)}},
    });

    corpus.push_back({
        "invalid-continuation-rejects",
        R"""(root ::= "\u00E9")""",
        {{100, std::string("\xC3", 1)}, {101, std::string("\x28", 1)}},
    });

    corpus.push_back({
        "balanced-small",
        R"""(root ::= "a" root "b" | "")""",
        {{0, "a"}, {0, "a"}, {0, "a"}, {0, "b"}, {0, "b"}, {0, "b"}},
    });

    return corpus;
}

int main(int argc, char ** argv) {
    const bool trace = argc > 1 && std::string(argv[1]) == "--trace";
    uint64_t hash = 1469598103934665603ull;
    size_t decisions = 0;

    for (const corpus_case & test_case : build_corpus()) {
        llama_grammar * grammar = build_grammar(test_case.grammar);
        hash_string(&hash, test_case.name);

        for (size_t step = 0; step < test_case.input.size(); ++step) {
            llama_grammar * clone = llama_grammar_clone_impl(*grammar);

            bool clone_complete = false;
            const bool clone_accept = accept_token_piece(clone, test_case.input[step], &clone_complete);

            bool commit_complete = false;
            const bool commit_accept = accept_token_piece(grammar, test_case.input[step], &commit_complete);

            assert(clone_accept == commit_accept);
            assert(clone_complete == commit_complete);

            hash_string(&hash, test_case.input[step].piece);
            hash_bool(&hash, clone_accept);
            hash_bool(&hash, clone_complete);
            hash_bool(&hash, commit_accept);
            hash_bool(&hash, commit_complete);
            ++decisions;

            if (trace) {
                std::printf(
                        "trace case=%s step=%zu token=%d piece_hex=%s clone_accept=%d clone_complete=%d commit_accept=%d commit_complete=%d\n",
                        test_case.name,
                        step,
                        test_case.input[step].token,
                        hex_piece(test_case.input[step].piece).c_str(),
                        clone_accept ? 1 : 0,
                        clone_complete ? 1 : 0,
                        commit_accept ? 1 : 0,
                        commit_complete ? 1 : 0);
            }

            llama_grammar_free_impl(clone);
            if (!commit_accept) {
                break;
            }
        }

        llama_grammar_free_impl(grammar);
    }

    std::printf("grammar-differential decisions=%zu fnv64=%016llx\n", decisions, static_cast<unsigned long long>(hash));
    return 0;
}
