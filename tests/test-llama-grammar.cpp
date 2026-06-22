#ifdef NDEBUG
#undef NDEBUG
#endif

#include "llama.h"

#include "../src/llama-grammar.h"

#include <cassert>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

static bool accepts(llama_grammar * grammar, const std::string & input) {
    llama_grammar * clone = llama_grammar_clone_impl(*grammar);

    for (unsigned char c : input) {
        llama_grammar_accept(clone, c);
        if (llama_grammar_get_stacks(clone).empty()) {
            llama_grammar_free_impl(clone);
            return false;
        }
    }

    bool complete = false;
    for (const auto & stack : llama_grammar_get_stacks(clone)) {
        complete = complete || stack.empty();
    }

    llama_grammar_free_impl(clone);
    return complete;
}

int main() {
    llama_grammar_parser parsed_grammar;

    std::vector<std::pair<std::string, uint32_t>> expected = {
        {"expr", 2},
        {"expr_6", 6},
        {"expr_7", 7},
        {"ident", 8},
        {"ident_10", 10},
        {"num", 9},
        {"num_11", 11},
        {"root", 0},
        {"root_1", 1},
        {"root_5", 5},
        {"term", 4},
        {"ws", 3},
        {"ws_12", 12},
    };

    std::vector<std::vector<llama_grammar_element>> expected_rules = {
        {{LLAMA_GRETYPE_RULE_REF, 5}, {LLAMA_GRETYPE_END, 0}},
        {
            {LLAMA_GRETYPE_RULE_REF, 2},
            {LLAMA_GRETYPE_CHAR, 61},
            {LLAMA_GRETYPE_RULE_REF, 3},
            {LLAMA_GRETYPE_RULE_REF, 4},
            {LLAMA_GRETYPE_CHAR, 10},
            {LLAMA_GRETYPE_END, 0},
        },
        {{LLAMA_GRETYPE_RULE_REF, 4}, {LLAMA_GRETYPE_RULE_REF, 7}, {LLAMA_GRETYPE_END, 0}},
        {{LLAMA_GRETYPE_RULE_REF, 12}, {LLAMA_GRETYPE_END, 0}},
        {
            {LLAMA_GRETYPE_RULE_REF, 8},
            {LLAMA_GRETYPE_ALT, 0},
            {LLAMA_GRETYPE_RULE_REF, 9},
            {LLAMA_GRETYPE_ALT, 0},
            {LLAMA_GRETYPE_CHAR, 40},
            {LLAMA_GRETYPE_RULE_REF, 3},
            {LLAMA_GRETYPE_RULE_REF, 2},
            {LLAMA_GRETYPE_CHAR, 41},
            {LLAMA_GRETYPE_RULE_REF, 3},
            {LLAMA_GRETYPE_END, 0},
        },
        {{LLAMA_GRETYPE_RULE_REF, 1}, {LLAMA_GRETYPE_RULE_REF, 5}, {LLAMA_GRETYPE_ALT, 0}, {LLAMA_GRETYPE_RULE_REF, 1}, {LLAMA_GRETYPE_END, 0}},
        {
            {LLAMA_GRETYPE_CHAR, 45},
            {LLAMA_GRETYPE_CHAR_ALT, 43},
            {LLAMA_GRETYPE_CHAR_ALT, 42},
            {LLAMA_GRETYPE_CHAR_ALT, 47},
            {LLAMA_GRETYPE_RULE_REF, 4},
            {LLAMA_GRETYPE_END, 0},
        },
        {{LLAMA_GRETYPE_RULE_REF, 6}, {LLAMA_GRETYPE_RULE_REF, 7}, {LLAMA_GRETYPE_ALT, 0}, {LLAMA_GRETYPE_END, 0}},
        {
            {LLAMA_GRETYPE_CHAR, 97},
            {LLAMA_GRETYPE_CHAR_RNG_UPPER, 122},
            {LLAMA_GRETYPE_RULE_REF, 10},
            {LLAMA_GRETYPE_RULE_REF, 3},
            {LLAMA_GRETYPE_END, 0},
        },
        {{LLAMA_GRETYPE_RULE_REF, 11}, {LLAMA_GRETYPE_RULE_REF, 3}, {LLAMA_GRETYPE_END, 0}},
        {
            {LLAMA_GRETYPE_CHAR, 97},
            {LLAMA_GRETYPE_CHAR_RNG_UPPER, 122},
            {LLAMA_GRETYPE_CHAR_ALT, 48},
            {LLAMA_GRETYPE_CHAR_RNG_UPPER, 57},
            {LLAMA_GRETYPE_CHAR_ALT, 95},
            {LLAMA_GRETYPE_RULE_REF, 10},
            {LLAMA_GRETYPE_ALT, 0},
            {LLAMA_GRETYPE_END, 0},
        },
        {
            {LLAMA_GRETYPE_CHAR, 48},
            {LLAMA_GRETYPE_CHAR_RNG_UPPER, 57},
            {LLAMA_GRETYPE_RULE_REF, 11},
            {LLAMA_GRETYPE_ALT, 0},
            {LLAMA_GRETYPE_CHAR, 48},
            {LLAMA_GRETYPE_CHAR_RNG_UPPER, 57},
            {LLAMA_GRETYPE_END, 0},
        },
        {
            {LLAMA_GRETYPE_CHAR, 32},
            {LLAMA_GRETYPE_CHAR_ALT, 9},
            {LLAMA_GRETYPE_CHAR_ALT, 10},
            {LLAMA_GRETYPE_RULE_REF, 12},
            {LLAMA_GRETYPE_ALT, 0},
            {LLAMA_GRETYPE_END, 0},
        },
    };

    for (auto pair : expected) {
        parsed_grammar.symbol_ids[pair.first] = pair.second;
    }

    for (auto rule : expected_rules) {
        parsed_grammar.rules.emplace_back();
        for (auto element : rule) {
            parsed_grammar.rules.back().push_back(element);
        }
    }

    std::vector<const llama_grammar_element *> grammar_rules(parsed_grammar.c_rules());

    llama_grammar * grammar = llama_grammar_init_impl(nullptr, grammar_rules.data(), grammar_rules.size(), parsed_grammar.symbol_ids.at("root"));
    if (grammar == nullptr) {
        throw std::runtime_error("Failed to initialize llama_grammar");
    }

    bool expects_lparen = false;
    bool expects_digit  = false;
    bool expects_ident  = false;
    for (const llama_grammar_stack & stack : llama_grammar_get_stacks(grammar)) {
        assert(!stack.empty());
        const llama_grammar_element * element = stack.back();
        if (element->type == LLAMA_GRETYPE_CHAR && element->value == 40) {
            expects_lparen = true;
        } else if (element->type == LLAMA_GRETYPE_CHAR && element->value == 48) {
            expects_digit = true;
        } else if (element->type == LLAMA_GRETYPE_CHAR && element->value == 97) {
            expects_ident = true;
        }
    }
    assert(expects_lparen);
    assert(expects_digit);
    assert(expects_ident);

    assert(accepts(grammar, "a=1\n"));
    assert(accepts(grammar, "abc=42\n"));
    assert(accepts(grammar, "a+1=2\n"));
    assert(!accepts(grammar, "=1\n"));
    assert(!accepts(grammar, "a=\n"));
    assert(!accepts(grammar, "a=1"));

    llama_grammar_free_impl(grammar);

    return 0;
}
