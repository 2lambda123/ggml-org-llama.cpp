//  Tests chat handling, including grammar generation and parsing for tool calling, for various templates.
//
//  Also acts as a CLI to generate a Markdown summary of the formats of Jinja templates,
//  e.g. given Minja (http://github.com/google/minja) checked out in parent dir:
//
//    cmake -B build && cmake --build build --parallel && ./build/bin/test-chat ../minja/build/tests/*.jinja 2>/dev/null
//
#include <exception>
#include <iostream>
#include <json.hpp>
#include <string>

#include "chat-parser.h"
#include "common.h"
#include "log.h"
#include "regex-partial.h"

using json = nlohmann::ordered_json;

template <class T>
static void assert_equals(const T & expected, const T & actual) {
    if (expected != actual) {
        std::cerr << "Expected: " << expected << std::endl;
        std::cerr << "Actual: " << actual << std::endl;
        std::cerr << std::flush;
        throw std::runtime_error("Test failed");
    }
}
static void assert_equals(const char * expected, const std::string & actual) {
  return assert_equals<std::string>(expected, actual);
}

template <class T = std::exception>
static void assert_throws(const std::function<void()> & fn, const std::string & expected_exception_pattern = "") {
    try {
        fn();
    } catch (const T & e) {
        if (expected_exception_pattern.empty()) {
          return;
        }
        std::regex expected_exception_regex(expected_exception_pattern);
        std::string actual_message = e.what();
        if (std::regex_search(actual_message, expected_exception_regex)) {
            return;
        }
        throw std::runtime_error("Exception doesn't match expected pattern: " + actual_message + " (pattern: " + expected_exception_pattern + ")");
    }
    throw std::runtime_error("Exception was expected but not thrown");
}

static void test_reasoning() {
  {
    common_chat_msg_parser builder("<tnk>Cogito</tnk>Ergo sum", /* is_partial= */ false, {
        /* .format = */ COMMON_CHAT_FORMAT_CONTENT_ONLY,
        /* .reasoning_format = */ COMMON_REASONING_FORMAT_NONE,
        /* .reasoning_in_content = */ false,
        /* .thinking_forced_open = */ false,
    });
    assert_equals(false, builder.try_parse_reasoning("<tnk>", "</tnk>"));
    assert_equals("<tnk>Cogito</tnk>Ergo sum", builder.consume_rest());
  }
  {
    common_chat_msg_parser builder("<tnk>Cogito</tnk>Ergo sum", /* is_partial= */ false, {
        /* .format = */ COMMON_CHAT_FORMAT_CONTENT_ONLY,
        /* .reasoning_format = */ COMMON_REASONING_FORMAT_DEEPSEEK,
        /* .reasoning_in_content = */ false,
        /* .thinking_forced_open = */ false,
    });
    assert_equals(true, builder.try_parse_reasoning("<tnk>", "</tnk>"));
    assert_equals(std::string("Cogito"), builder.result().reasoning_content);
    assert_equals("Ergo sum", builder.consume_rest());
  }
  {
    common_chat_msg_parser builder("Cogito</tnk>Ergo sum", /* is_partial= */ false, {
        /* .format = */ COMMON_CHAT_FORMAT_CONTENT_ONLY,
        /* .reasoning_format = */ COMMON_REASONING_FORMAT_NONE,
        /* .reasoning_in_content = */ false,
        /* .thinking_forced_open = */ false,
    });
    assert_equals(false, builder.try_parse_reasoning("<tnk>", "</tnk>"));
    assert_equals("Cogito</tnk>Ergo sum", builder.consume_rest());
  }
  {
    common_chat_msg_parser builder("Cogito</tnk>Ergo sum", /* is_partial= */ false, {
        /* .format = */ COMMON_CHAT_FORMAT_CONTENT_ONLY,
        /* .reasoning_format = */ COMMON_REASONING_FORMAT_DEEPSEEK,
        /* .reasoning_in_content = */ false,
        /* .thinking_forced_open = */ true,
    });
    assert_equals(true, builder.try_parse_reasoning("<tnk>", "</tnk>"));
    assert_equals(std::string("Cogito"), builder.result().reasoning_content);
    assert_equals("Ergo sum", builder.consume_rest());
  }
  {
    common_chat_msg_parser builder("Cogito</tnk>Ergo sum", /* is_partial= */ false, {
        /* .format = */ COMMON_CHAT_FORMAT_CONTENT_ONLY,
        /* .reasoning_format = */ COMMON_REASONING_FORMAT_DEEPSEEK,
        /* .reasoning_in_content = */ true,
        /* .thinking_forced_open = */ true,
    });
    assert_equals(true, builder.try_parse_reasoning("<tnk>", "</tnk>"));
    assert_equals("<think>Cogito</think>", builder.result().content);
    assert_equals("Ergo sum", builder.consume_rest());
  }
}

static void test_regex() {
  auto test_throws = [](const std::string & input, const std::string & regex, const std::string & expected_exception_pattern = "") {
    common_chat_msg_parser builder(input, /* is_partial= */ false, {});
    assert_throws([&]() { builder.consume_regex(common_regex(regex)); }, expected_exception_pattern);
  };

  test_throws("Hello, world!", "abc", "^abc$");
  test_throws("Hello, world!", "e", "^e$");

  {
    common_chat_msg_parser builder("Hello, world!", /* is_partial= */ false, {});
    builder.consume_regex(common_regex("Hello"));
    assert_equals(", world!", builder.consume_rest());
  }

  {
    // When in non partial mode, we can say whether the regex was consumed or not.
    common_chat_msg_parser builder("Hello,", /* is_partial= */ false, {});
    assert_equals(false, builder.try_consume_regex(common_regex("Hello, world!")).has_value());
    assert_equals(true, builder.try_consume_regex(common_regex("Hell(o, world!)?")).has_value());
  }
  {
    // But in partial mode, we have a partial final match / can't decide, so we throw a partial exception.
    common_chat_msg_parser builder("Hello,", /* is_partial= */ true, {});
    assert_throws<common_chat_msg_partial_exception>([&]() {
      builder.try_consume_regex(common_regex("Hello, world!"));
    }, "^Hello, world!$");
  }

  // Now regardless of the mode, we can tell these aren't a match.
  for (const auto is_partial : {false, true}) {
    common_chat_msg_parser builder("Hello,", is_partial, {});
    assert_equals(false, builder.try_consume_regex(common_regex("a(b|c)(d|e)f")).has_value());
  }
  for (const auto is_partial : {false, true}) {
    common_chat_msg_parser builder("Hello,", is_partial, {});
    assert_equals(false, builder.try_consume_literal("Oh"));
  }
}

const std::vector<std::string> barely_healable_jsons = {
  "{",
  "{\"",
  "{\"n",
  "{\"name\"",
  "{\"name\":",
  "{\"name\":\"",
  "{\"name\":\"python",
};

static void test(const std::string & input, bool is_partial, const std::vector<std::vector<std::string>> & args_paths, const std::string & expected) {
  common_chat_msg_parser builder(input, is_partial, {});
  auto js = builder.try_consume_json_with_dumped_args(args_paths);
  assert_equals(true, js.has_value());
  assert_equals(is_partial, js->is_partial);
  assert_equals(expected, args_paths.size() == 1 && args_paths[0].empty() ? js->value.get<std::string>() : js->value.dump());
}
static void test_with_args(const std::string & input, const std::string & expected, bool parse_as_partial = true, bool is_partial = true) {
  common_chat_msg_parser builder(input, parse_as_partial, {});
  auto js = builder.try_consume_json_with_dumped_args({{"args"}});
  assert_equals(true, js.has_value());
  assert_equals(is_partial, js->is_partial);
  assert_equals(expected, js->value.dump());
}

static void test_json_with_dumped_args_no_args() {
  // Normal JSON, nothing to heal, nothing to dump
  test("{\"name\": \"python\"}", false, {}, "{\"name\":\"python\"}");
  // Full json is args
  test("{\"name\": \"python\"}", false, {{}}, "{\"name\":\"python\"}");

  // If the arguments are further down, don't heal partial content.
  for (const auto & src : barely_healable_jsons) {
    test(src, true, {{"arguments"}}, "{}");
  }
  // But heal content that isn't partial.
  test("{\"name\": \"python\"", true, {{"arguments"}}, "{\"name\":\"python\"}");
}

static void test_json_with_dumped_args() {
  // If the entire JSON is the arguments, healing it them dumping it produces the same output as the input (just reformatted).
  test("{\"name\": \"python", true, {{}}, "{\"name\":\"python");
  for (const auto & src : barely_healable_jsons) {
    test(src, true, {{}}, src);
  }

  // Full JSON w/ args
  for (auto parse_as_partial : {true, false}) {
    test_with_args(
      R"({"name": "python", "args": {"arg1": 1}})",
      R"({"name":"python","args":"{\"arg1\":1}"})",
      parse_as_partial,
      /* is_partial= */ false
    );
  }

  // Partial JSON w/ partial args
  test_with_args(
    R"({"foo": "bar", "args": {")",
    R"({"foo":"bar","args":"{\""})"
  );
  // Partial args broken in object key
  test_with_args(
    R"({"foo": "bar", "args": {"ar)",
    R"({"foo":"bar","args":"{\"ar"})"
  );
  // Partial args broken after object key
  test_with_args(
    R"({"foo": "bar", "args": {"arg1")",
    R"({"foo":"bar","args":"{\"arg1\""})"
  );
  // Partial args broken before object value
  test_with_args(
    R"({"foo": "bar", "args": {"arg1":)",
    R"({"foo":"bar","args":"{\"arg1\":"})"
  );
  // Partial args broken before object value (space)
  test_with_args(
    R"({"foo": "bar", "args": {"arg1": )",
    R"({"foo":"bar","args":"{\"arg1\":"})"
  );
  // Partial args broken in object value that may not be complete (int)
  test_with_args(
    R"({"foo": "bar", "args": {"arg1": 1)",
    R"({"foo":"bar","args":"{\"arg1\":"})"
  );
  // Partial args broken in object value that is complete (int)
  test_with_args(
    R"({"foo": "bar", "args": {"arg1": 1 )",
    R"({"foo":"bar","args":"{\"arg1\":1"})"
  );
  // Partial args broken in object value that is incomplete (string)
  test_with_args(
    R"({"foo": "bar", "args": {"arg1": ")",
    R"({"foo":"bar","args":"{\"arg1\":\""})"
  );
  // Partial args broken in object value that is complete (string)
  test_with_args(
    R"({"foo": "bar", "args": {"arg1": "1")",
    R"({"foo":"bar","args":"{\"arg1\":\"1\""})"
  );
  // Partial args broken on array opening
  test_with_args(
    R"({"foo": "bar", "args": [)",
    R"({"foo":"bar","args":"["})"
  );
  // Partial args broken on array value that is incomplete (int)
  test_with_args(
    R"({"foo": "bar", "args": [1)",
    R"({"foo":"bar","args":"["})"
  );
  // Partial args broken on array value that is complete (int)
  test_with_args(
    R"({"foo": "bar", "args": [1 )",
    R"({"foo":"bar","args":"[1"})"
  );
  // Partial args broken on array value that is complete (string)
  test_with_args(
    R"({"foo": "bar", "args": ["1")",
    R"({"foo":"bar","args":"[\"1\""})"
  );
  // Partial args broken after array value
  test_with_args(
    R"({"foo": "bar", "args": [1,)",
    R"({"foo":"bar","args":"[1,"})"
  );
  // Partial args broken on nested array
  test_with_args(
    R"({"foo": "bar", "args": {"arg1": [)",
    R"({"foo":"bar","args":"{\"arg1\":["})"
  );
}

int main() {
    test_json_with_dumped_args_no_args();
    test_json_with_dumped_args();
    test_reasoning();
    test_regex();
    std::cout << "All tests passed!\n";
    return 0;
}
