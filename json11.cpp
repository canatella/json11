/* Copyright (c) 2013 Dropbox, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "json11.hpp"
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <stack>

namespace json11 {

static const int max_depth = 200;

using std::string;
using std::vector;
using std::map;
using std::make_shared;
using std::initializer_list;
using std::move;

/* * * * * * * * * * * * * * * * * * * *
 * Serialization
 */

static void dump(std::nullptr_t, string &out) {
    out += "null";
}

static void dump(double value, string &out) {
    if (std::isfinite(value)) {
        char buf[32];
        snprintf(buf, sizeof buf, "%.17g", value);
        out += buf;
    } else {
        out += "null";
    }
}

static void dump(int value, string &out) {
    char buf[32];
    snprintf(buf, sizeof buf, "%d", value);
    out += buf;
}

static void dump(bool value, string &out) {
    out += value ? "true" : "false";
}

static void dump(const string &value, string &out) {
    out += '"';
    for (size_t i = 0; i < value.length(); i++) {
        const char ch = value[i];
        if (ch == '\\') {
            out += "\\\\";
        } else if (ch == '"') {
            out += "\\\"";
        } else if (ch == '\b') {
            out += "\\b";
        } else if (ch == '\f') {
            out += "\\f";
        } else if (ch == '\n') {
            out += "\\n";
        } else if (ch == '\r') {
            out += "\\r";
        } else if (ch == '\t') {
            out += "\\t";
        } else if (static_cast<uint8_t>(ch) <= 0x1f) {
            char buf[8];
            snprintf(buf, sizeof buf, "\\u%04x", ch);
            out += buf;
        } else if (static_cast<uint8_t>(ch) == 0xe2 && static_cast<uint8_t>(value[i+1]) == 0x80
                   && static_cast<uint8_t>(value[i+2]) == 0xa8) {
            out += "\\u2028";
            i += 2;
        } else if (static_cast<uint8_t>(ch) == 0xe2 && static_cast<uint8_t>(value[i+1]) == 0x80
                   && static_cast<uint8_t>(value[i+2]) == 0xa9) {
            out += "\\u2029";
            i += 2;
        } else {
            out += ch;
        }
    }
    out += '"';
}

static void dump(const Json::array &values, string &out) {
    bool first = true;
    out += "[";
    for (const auto &value : values) {
        if (!first)
            out += ", ";
        value.dump(out);
        first = false;
    }
    out += "]";
}

static void dump(const Json::object &values, string &out) {
    bool first = true;
    out += "{";
    for (const auto &kv : values) {
        if (!first)
            out += ", ";
        dump(kv.first, out);
        out += ": ";
        kv.second.dump(out);
        first = false;
    }
    out += "}";
}

void Json::dump(string &out) const {
    m_ptr->dump(out);
}

/* * * * * * * * * * * * * * * * * * * *
 * Value wrappers
 */

template <Json::Type tag, typename T>
class Value : public JsonValue {
protected:

    // Constructors
    explicit Value(const T &value) : m_value(value) {}
    explicit Value(T &&value)      : m_value(move(value)) {}

    // Get type tag
    Json::Type type() const override {
        return tag;
    }

    // Comparisons
    bool equals(const JsonValue * other) const override {
        return m_value == static_cast<const Value<tag, T> *>(other)->m_value;
    }
    bool less(const JsonValue * other) const override {
        return m_value < static_cast<const Value<tag, T> *>(other)->m_value;
    }

    const T m_value;
    void dump(string &out) const override { json11::dump(m_value, out); }
};

class JsonDouble final : public Value<Json::NUMBER, double> {
    double number_value() const override { return m_value; }
    int int_value() const override { return static_cast<int>(m_value); }
    bool equals(const JsonValue * other) const override { return m_value == other->number_value(); }
    bool less(const JsonValue * other)   const override { return m_value <  other->number_value(); }
public:
    explicit JsonDouble(double value) : Value(value) {}
};

class JsonInt final : public Value<Json::NUMBER, int> {
    double number_value() const override { return m_value; }
    int int_value() const override { return m_value; }
    bool equals(const JsonValue * other) const override { return m_value == other->number_value(); }
    bool less(const JsonValue * other)   const override { return m_value <  other->number_value(); }
public:
    explicit JsonInt(int value) : Value(value) {}
};

class JsonBoolean final : public Value<Json::BOOL, bool> {
    bool bool_value() const override { return m_value; }
public:
    explicit JsonBoolean(bool value) : Value(value) {}
};

class JsonString final : public Value<Json::STRING, string> {
    const string &string_value() const override { return m_value; }
public:
    explicit JsonString(const string &value) : Value(value) {}
    explicit JsonString(string &&value)      : Value(move(value)) {}
};

class JsonArray final : public Value<Json::ARRAY, Json::array> {
    const Json::array &array_items() const override { return m_value; }
    const Json & operator[](size_t i) const override;
public:
    explicit JsonArray(const Json::array &value) : Value(value) {}
    explicit JsonArray(Json::array &&value)      : Value(move(value)) {}
};

class JsonObject final : public Value<Json::OBJECT, Json::object> {
    const Json::object &object_items() const override { return m_value; }
    const Json & operator[](const string &key) const override;
public:
    explicit JsonObject(const Json::object &value) : Value(value) {}
    explicit JsonObject(Json::object &&value)      : Value(move(value)) {}
};

class JsonNull final : public Value<Json::NUL, std::nullptr_t> {
public:
    JsonNull() : Value(nullptr) {}
};

/* * * * * * * * * * * * * * * * * * * *
 * Static globals - static-init-safe
 */
struct Statics {
    const std::shared_ptr<JsonValue> null = make_shared<JsonNull>();
    const std::shared_ptr<JsonValue> t = make_shared<JsonBoolean>(true);
    const std::shared_ptr<JsonValue> f = make_shared<JsonBoolean>(false);
    const string empty_string;
    const vector<Json> empty_vector;
    const map<string, Json> empty_map;
    Statics() {}
};

static const Statics & statics() {
    static const Statics s {};
    return s;
}

static const Json & static_null() {
    // This has to be separate, not in Statics, because Json() accesses statics().null.
    static const Json json_null;
    return json_null;
}

/* * * * * * * * * * * * * * * * * * * *
 * Constructors
 */

Json::Json() noexcept                  : m_ptr(statics().null) {}
Json::Json(std::nullptr_t) noexcept    : m_ptr(statics().null) {}
Json::Json(double value)               : m_ptr(make_shared<JsonDouble>(value)) {}
Json::Json(int value)                  : m_ptr(make_shared<JsonInt>(value)) {}
Json::Json(bool value)                 : m_ptr(value ? statics().t : statics().f) {}
Json::Json(const string &value)        : m_ptr(make_shared<JsonString>(value)) {}
Json::Json(string &&value)             : m_ptr(make_shared<JsonString>(move(value))) {}
Json::Json(const char * value)         : m_ptr(make_shared<JsonString>(value)) {}
Json::Json(const Json::array &values)  : m_ptr(make_shared<JsonArray>(values)) {}
Json::Json(Json::array &&values)       : m_ptr(make_shared<JsonArray>(move(values))) {}
Json::Json(const Json::object &values) : m_ptr(make_shared<JsonObject>(values)) {}
Json::Json(Json::object &&values)      : m_ptr(make_shared<JsonObject>(move(values))) {}

/* * * * * * * * * * * * * * * * * * * *
 * Accessors
 */

Json::Type Json::type()                           const { return m_ptr->type();         }
double Json::number_value()                       const { return m_ptr->number_value(); }
int Json::int_value()                             const { return m_ptr->int_value();    }
bool Json::bool_value()                           const { return m_ptr->bool_value();   }
const string & Json::string_value()               const { return m_ptr->string_value(); }
const vector<Json> & Json::array_items()          const { return m_ptr->array_items();  }
const map<string, Json> & Json::object_items()    const { return m_ptr->object_items(); }
const Json & Json::operator[] (size_t i)          const { return (*m_ptr)[i];           }
const Json & Json::operator[] (const string &key) const { return (*m_ptr)[key];         }

double                    JsonValue::number_value()              const { return 0; }
int                       JsonValue::int_value()                 const { return 0; }
bool                      JsonValue::bool_value()                const { return false; }
const string &            JsonValue::string_value()              const { return statics().empty_string; }
const vector<Json> &      JsonValue::array_items()               const { return statics().empty_vector; }
const map<string, Json> & JsonValue::object_items()              const { return statics().empty_map; }
const Json &              JsonValue::operator[] (size_t)         const { return static_null(); }
const Json &              JsonValue::operator[] (const string &) const { return static_null(); }

const Json & JsonObject::operator[] (const string &key) const {
    auto iter = m_value.find(key);
    return (iter == m_value.end()) ? static_null() : iter->second;
}
const Json & JsonArray::operator[] (size_t i) const {
    if (i >= m_value.size()) return static_null();
    else return m_value[i];
}

/* * * * * * * * * * * * * * * * * * * *
 * Comparison
 */

bool Json::operator== (const Json &other) const {
    if (m_ptr->type() != other.m_ptr->type())
        return false;

    return m_ptr->equals(other.m_ptr.get());
}

bool Json::operator< (const Json &other) const {
    if (m_ptr->type() != other.m_ptr->type())
        return m_ptr->type() < other.m_ptr->type();

    return m_ptr->less(other.m_ptr.get());
}

/* * * * * * * * * * * * * * * * * * * *
 * Parsing
 */

/* esc(c)
 *
 * Format char c suitable for printing in an error message.
 */
static inline string esc(char c) {
    char buf[12];
    if (static_cast<uint8_t>(c) >= 0x20 && static_cast<uint8_t>(c) <= 0x7f) {
        snprintf(buf, sizeof buf, "'%c' (%d)", c, c);
    } else {
        snprintf(buf, sizeof buf, "(%d)", c);
    }
    return string(buf);
}

static inline bool in_range(long x, long lower, long upper) {
    return (x >= lower && x <= upper);
}

/* JsonParserPriv
 *
 * Object that tracks all state of an in-progress parse.
 */
struct JsonParserPriv final {

    /* State
     */
    string str;
    size_t i = 0;
    string &err;
    bool failed = false;
    bool need_data = false;
    bool eof = false;
    const JsonParse strategy;
    std::stack<Json> values;
    size_t last_position;

    enum State {
        EXPECT_VALUE,
        VALUE_STRING,
        VALUE_NUMBER,
        VALUE_TRUE,
        VALUE_FALSE,
        VALUE_NULL,
        VALUE_COMMENT,
        VALUE_OBJECT,
        OBJECT_KEY_OR_END,
        OBJECT_COMMA_OR_END,
        OBJECT_KEY,
        OBJECT_COLON,
        OBJECT_VALUE,
        VALUE_ARRAY,
        ARRAY_VALUE_OR_END,
        ARRAY_COMMA_OR_END,
        ARRAY_VALUE
    };
    std::stack<State> states;

    JsonParserPriv(string str, string &err, const JsonParse strategy):
        str(str), err(err), strategy(strategy) {
        push_state(EXPECT_VALUE);
    }

    /* fail(msg, err_ret = Json())
     *
     * Mark this parse as failed.
     */
    void fail(string &&msg) {
        fail(move(msg), Json());
    }

    template <typename T>
    void fail(string &&msg, const T err_ret) {
        if (!failed) {
            pop_state();
            err = std::move(msg);
        }

        /* if we are at end of file, push the error value anyway */
        if (!failed || (need_data && eof))
            values.push(err_ret);

        failed = true;
        assert(states.size() > 0);
    }

    /* stop(msg, err_ret = Json())
     *
     * Mark this parse as needing more data.
     */
    void stop(string &&msg) {
        if (!failed && !need_data)
            err = std::move(msg);
        if (eof) {
            values.push(Json());
            failed = true;
        }
        else
            i = last_position;

        need_data = true;
    }

    /* eos()
     *
     * Return true if we are at the end of the parsing string
     */
    bool eos() {
        return i == str.size();
    }

    /* set_state()
     *
     * Set current parsing state.
     */
    void set_state(State state) {
        last_position = i;
        states.pop();
        states.push(state);
    }

    /* push_state()
     *
     * push new current parsing state.
     */
    void push_state(State state) {
        last_position = i;
        states.push(state);
    }

    /* pop_state()
     *
     * Set current parsing state.
     */
    void pop_state() {
        last_position = i;
        states.pop();
    }

#define assert_state(S) assert(states.top() == S)

    /* consume_whitespace()
     *
     * Advance until the current character is non-whitespace.
     */
    void consume_whitespace() {
        while (str[i] == ' ' || str[i] == '\r' || str[i] == '\n' || str[i] == '\t')
            i++;
    }

    /* consume_comment()
     *
     * Advance comments (c-style inline and multiline).
     */
    void consume_comment() {
      assert_state(VALUE_COMMENT);
      bool comment_found = false;
      if (str[i] == '/') {
        i++;
        if (eos())
            return stop("unexpected end of input inside comment");
        if (str[i] == '/') { // inline comment
          i++;
          if (eos())
            return stop("unexpected end of input inside inline comment");
          // advance until next line
          while (str[i] != '\n') {
            i++;
            if (eos()) {
              if (eof)
                break;
              else
                return stop("unexpected end of input inside inline comment");
            }
          }
          comment_found = true;
        }
        else if (str[i] == '*') { // multiline comment
          i++;
          if (i > str.size()-2)
            return stop("unexpected end of input inside multi-line comment");
          // advance until closing tokens
          while (!(str[i] == '*' && str[i+1] == '/')) {
            i++;
            if (i > str.size()-2)
              return stop(
                "unexpected end of input inside multi-line comment");
          }
          i += 2;
          if (eos())
            return stop(
              "unexpected end of input inside multi-line comment");
          comment_found = true;
        }
        else
          return fail("malformed comment", false);
      }
      pop_state();
      values.push(comment_found);
    }

    /* consume_garbage()
     *
     * Advance until the current character is non-whitespace and non-comment.
     */
    void consume_garbage() {
      consume_whitespace();
      if(strategy == JsonParse::COMMENTS) {
        bool comment_found = false;
        do {
          push_state(VALUE_COMMENT);
          consume_comment();
          if (need_data)
              break;

          comment_found = values.top().bool_value();
          values.pop();
          consume_whitespace();
        }
        while(comment_found);
      }
    }

    /* get_next_token()
     *
     * Return the next non-whitespace character. If the end of the input is reached,
     * flag an error and return 0.
     */
    char get_next_token() {
        consume_garbage();
        if (need_data) {
            return '\0';
        } else if (eos()) {
            stop("unexpected end of input");
            return '\0';
        }

        return str[i++];
    }

    /* encode_utf8(pt, out)
     *
     * Encode pt as UTF-8 and add it to out.
     */
    void encode_utf8(long pt, string & out) {
        if (pt < 0)
            return;

        if (pt < 0x80) {
            out += static_cast<char>(pt);
        } else if (pt < 0x800) {
            out += static_cast<char>((pt >> 6) | 0xC0);
            out += static_cast<char>((pt & 0x3F) | 0x80);
        } else if (pt < 0x10000) {
            out += static_cast<char>((pt >> 12) | 0xE0);
            out += static_cast<char>(((pt >> 6) & 0x3F) | 0x80);
            out += static_cast<char>((pt & 0x3F) | 0x80);
        } else {
            out += static_cast<char>((pt >> 18) | 0xF0);
            out += static_cast<char>(((pt >> 12) & 0x3F) | 0x80);
            out += static_cast<char>(((pt >> 6) & 0x3F) | 0x80);
            out += static_cast<char>((pt & 0x3F) | 0x80);
        }
    }

    /* parse_comment()
     *
     * Parse json comments.
     */
    void parse_comment() {
        assert_state(VALUE_COMMENT);

        bool comment_found = false;

        consume_comment();
        if (need_data)
            return;

        comment_found = values.top().bool_value();
        values.pop();
        consume_whitespace();

        if (comment_found)
            consume_garbage();
    }

    /* parse_string()
     *
     * Parse a string, starting at the current position.
     */
    void parse_string() {
        assert_state(VALUE_STRING);

        string out;
        long last_escaped_codepoint = -1;
        while (true) {
            if (eos())
                return stop("unexpected end of input in string");

            char ch = str[i++];

            if (ch == '"') {
                encode_utf8(last_escaped_codepoint, out);
                pop_state();
                values.push(out);
                return;
            }

            if (in_range(ch, 0, 0x1f))
                return fail("unescaped " + esc(ch) + " in string", "");

            // The usual case: non-escaped characters
            if (ch != '\\') {
                encode_utf8(last_escaped_codepoint, out);
                last_escaped_codepoint = -1;
                out += ch;
                continue;
            }

            // Handle escapes
            if (eos())
                return stop("unexpected end of input in string");

            ch = str[i++];

            if (ch == 'u') {
                // Extract 4-byte escape sequence
                string esc = str.substr(i, 4);
                // Explicitly check length of the substring. The following loop
                // relies on std::string returning the terminating NUL when
                // accessing str[length]. Checking here reduces brittleness.
                if (esc.length() < 4) {
                    return fail("bad \\u escape: " + esc, "");
                }
                for (size_t j = 0; j < 4; j++) {
                    if (!in_range(esc[j], 'a', 'f') && !in_range(esc[j], 'A', 'F')
                            && !in_range(esc[j], '0', '9'))
                        return fail("bad \\u escape: " + esc, "");
                }

                long codepoint = strtol(esc.data(), nullptr, 16);

                // JSON specifies that characters outside the BMP shall be encoded as a pair
                // of 4-hex-digit \u escapes encoding their surrogate pair components. Check
                // whether we're in the middle of such a beast: the previous codepoint was an
                // escaped lead (high) surrogate, and this is a trail (low) surrogate.
                if (in_range(last_escaped_codepoint, 0xD800, 0xDBFF)
                        && in_range(codepoint, 0xDC00, 0xDFFF)) {
                    // Reassemble the two surrogate pairs into one astral-plane character, per
                    // the UTF-16 algorithm.
                    encode_utf8((((last_escaped_codepoint - 0xD800) << 10)
                                 | (codepoint - 0xDC00)) + 0x10000, out);
                    last_escaped_codepoint = -1;
                } else {
                    encode_utf8(last_escaped_codepoint, out);
                    last_escaped_codepoint = codepoint;
                }

                i += 4;
                continue;
            }

            encode_utf8(last_escaped_codepoint, out);
            last_escaped_codepoint = -1;

            if (ch == 'b') {
                out += '\b';
            } else if (ch == 'f') {
                out += '\f';
            } else if (ch == 'n') {
                out += '\n';
            } else if (ch == 'r') {
                out += '\r';
            } else if (ch == 't') {
                out += '\t';
            } else if (ch == '"' || ch == '\\' || ch == '/') {
                out += ch;
            } else {
                return fail("invalid escape character " + esc(ch), "");
            }
        }
    }

    /* parse_number()
     *
     * Parse a double.
     */
    void parse_number() {
        assert_state(VALUE_NUMBER);
        size_t start_pos = i;

        if (str[i] == '-')
            i++;

        // Integer part
        if (str[i] == '0') {
            i++;
            if (eos())
                return stop("end of input while parsing number");
            if (in_range(str[i], '0', '9'))
                return fail("leading 0s not permitted in numbers");
        } else if (in_range(str[i], '1', '9')) {
            i++;
            if (eos())
                return stop("end of input while parsing number");
            while (in_range(str[i], '0', '9')) {
                i++;
                if (eos()) {
                    if (eof)
                        break;
                    else
                        return stop("end of input while parsing number");
                }
            }

        } else {
            return fail("invalid " + esc(str[i]) + " in number");
        }

        if (str[i] != '.' && str[i] != 'e' && str[i] != 'E'
                && (i - start_pos) <= static_cast<size_t>(std::numeric_limits<int>::digits10)) {
            pop_state();
            return values.push(std::atoi(str.c_str() + start_pos));
        }

        // Decimal part
        if (str[i] == '.') {
            i++;
            if (eos())
                return stop("end of input while parsing number");

            if (!in_range(str[i], '0', '9'))
                return fail("at least one digit required in fractional part");

            while (in_range(str[i], '0', '9')) {
                i++;
                if (eos()) {
                    if (eof)
                        break;
                    else
                        return stop("end of input while parsing number");
                }
            }
        }

        // Exponent part
        if (str[i] == 'e' || str[i] == 'E') {
            i++;
            if (eos())
                return stop("end of input while parsing number");

            if (str[i] == '+' || str[i] == '-') {
                i++;
                if (eos())
                    return stop("end of input while parsing number");
            }

            if (!in_range(str[i], '0', '9'))
                return fail("at least one digit required in exponent");

            while (in_range(str[i], '0', '9')) {
                i++;
                if (eos()) {
                    if (eof)
                        break;
                    else
                        return stop("end of input while parsing number");
                }
            }
        }

        pop_state();
        return values.push(std::strtod(str.c_str() + start_pos, nullptr));
    }

    /* expect(str, res)
     *
     * Expect that 'str' starts at the character that was just read. If it does, advance
     * the input and return res. If not, flag an error.
     */
    void expect(const string &expected, Json res) {
        assert(i != 0);
        i--;

        if (str.length() - i < expected.length())
            return stop("end of input, while parsing " + expected);

        if (str.compare(i, expected.length(), expected) == 0) {
            i += expected.length();
            pop_state();
            return values.push(res);
        } else {
            return fail("parse error: expected " + expected + ", got " + str.substr(i, expected.length()));
        }
    }

    /* parse_true()
     *
     * Parse a json true value.
     */
    void parse_true() {
        assert_state(VALUE_TRUE);
        expect("true", true);
    }

    /* parse_false()
     *
     * Parse a json false value.
     */
    void parse_false() {
        assert_state(VALUE_FALSE);
        expect("false", false);
    }

    /* parse_null()
     *
     * Parse a json null value.
     */
    void parse_null() {
        assert_state(VALUE_NULL);
        expect("null", Json());
    }

    /* parse_object()
     *
     * Parse a json object value.
     */
    void parse_object() {
        assert(states.top() >= VALUE_OBJECT && states.top() <= OBJECT_VALUE);

        switch (states.top()) {
        case VALUE_OBJECT:
            values.push(map<string, Json>());
            set_state(OBJECT_KEY_OR_END);
            break;

        case OBJECT_KEY_OR_END: {
            char ch = get_next_token();

            if (need_data)
                break;

            if (ch == '}')
                return pop_state();

            if (ch != '"') {
                values.pop();
                fail("expected '\"' in object, got " + esc(ch));
                return;
            }

            set_state(OBJECT_COLON);
            push_state(VALUE_STRING);

            break;
        }
        case OBJECT_COMMA_OR_END: {
            char ch = get_next_token();

            if (need_data)
                break;

            if (ch == '}') {
                pop_state();
                return;
            }

            if (ch != ',') {
                values.pop();
                fail("expected ',' or '}' in object, got " + esc(ch));
                return;
            }

            set_state(OBJECT_KEY);

            break;
        }
        case OBJECT_KEY: {
            char ch = get_next_token();

            if (need_data)
                break;

            if (ch != '"') {
                values.pop();
                return fail("expected '\"' in object, got " + esc(ch));
            }

            set_state(OBJECT_COLON);
            push_state(VALUE_STRING);

            break;
        }
        case OBJECT_COLON: {
            char ch = get_next_token();
            if (need_data)
                return;

            if (ch != ':') {
                values.pop();
                return fail("expected ':' in object, got " + esc(ch));
            }

            set_state(OBJECT_VALUE);
            push_state(EXPECT_VALUE);
            break;
        }
        case OBJECT_VALUE: {
            Json value = values.top();
            values.pop();
            string key = values.top().string_value();
            values.pop();
            map<string, Json> data = values.top().object_items();
            data[std::move(key)] = value;
            values.top() = data;

            set_state(OBJECT_COMMA_OR_END);
            break;
        }
        default:
            assert(false);
        }
    }

    /* parse_array()
     *
     * Parse a json array value.
     */
    void parse_array() {
        assert(states.top() >= VALUE_ARRAY && states.top() <= ARRAY_VALUE);

        switch (states.top()) {
        case VALUE_ARRAY:
            values.push(vector<Json>());
            set_state(ARRAY_VALUE_OR_END);
            break;

        case ARRAY_VALUE_OR_END: {
            char ch = get_next_token();

            if (need_data)
                return;

            if (ch == ']')
                return pop_state();

            i--;

            set_state(ARRAY_VALUE);
            push_state(EXPECT_VALUE);

            break;
        }
        case ARRAY_COMMA_OR_END: {
            char ch = get_next_token();
            if (need_data)
                return;

            if (ch == ']') {
                pop_state();
                return;
            }
            if (ch != ',') {
                values.pop();
                fail("expected ',' in list, got " + esc(ch));
                return;
            }

            set_state(ARRAY_VALUE_OR_END);

            break;
        }
        case ARRAY_VALUE: {
            Json value = values.top();
            values.pop();

            vector<Json> data = values.top().array_items();
            data.push_back(value);
            values.top() = data;

            set_state(ARRAY_COMMA_OR_END);

            break;
        }
        default:
            assert(false);
        }
    }

    /* parse_json()
     *
     * Parse any JSON value.
     */
    void parse_json() {
        assert_state(EXPECT_VALUE);

        if (values.size() > max_depth) {
            return fail("exceeded maximum nesting depth");
        }

        char ch = get_next_token();
        if (need_data)
            return;

        if (ch == '-' || (ch >= '0' && ch <= '9')) {
            i--;
            return set_state(VALUE_NUMBER);
        }

        if (ch == 't')
            return set_state(VALUE_TRUE);

        if (ch == 'f')
            return set_state(VALUE_FALSE);

        if (ch == 'n')
            return set_state(VALUE_NULL);

        if (ch == '"')
            return set_state(VALUE_STRING);

        if (ch == '{')
            return set_state(VALUE_OBJECT);

        if (ch == '[')
            return set_state(VALUE_ARRAY);

        return fail("expected value, got " + esc(ch));
    }

    void consume(const std::string &in = std::string()) {
        need_data = false;
        str += in;

        /* try to parse as much as possible */
        while (!states.empty()) {
            switch (states.top()) {
            case EXPECT_VALUE:
                parse_json();
                break;
            case VALUE_OBJECT:
            case OBJECT_KEY_OR_END:
            case OBJECT_COMMA_OR_END:
            case OBJECT_KEY:
            case OBJECT_COLON:
            case OBJECT_VALUE:
                parse_object();
                break;
            case VALUE_ARRAY:
            case ARRAY_VALUE_OR_END:
            case ARRAY_COMMA_OR_END:
            case ARRAY_VALUE:
                parse_array();
                break;
            case VALUE_STRING:
                parse_string();
                break;
            case VALUE_NUMBER:
                parse_number();
                break;
            case VALUE_TRUE:
                parse_true();
                break;
            case VALUE_FALSE:
                parse_false();
                break;
            case VALUE_NULL:
                parse_null();
                break;
            case VALUE_COMMENT:
                parse_comment();
                break;
            }

            if (failed || need_data)
                break;
        }
    }

};

JsonParser::JsonParser():
    parser(new JsonParserPriv("", error, JsonParse::STANDARD)) {
}

JsonParser::JsonParser(JsonParse strategy):
    parser(new JsonParserPriv("", error, strategy)) {
}

JsonParser::~JsonParser() {
}

void JsonParser::consume(const std::string &in) {
    parser->consume(in);
}

Json JsonParser::json() {
    parser->eof = true;
    parser->consume();
    if (!parser->failed && !parser->need_data)
        error.clear();
    return parser->values.top();
}

void JsonParser::reset() {
    parser.reset(new JsonParserPriv("", error, parser->strategy));
}

Json Json::parse(const string &in, string &err, JsonParse strategy) {
    JsonParserPriv parser { in, err, strategy };
    assert(parser.states.size() == 1);
    parser.eof = true;
    parser.consume();

    // Check for any trailing garbage
    parser.consume_garbage();
    if (parser.i != in.size()) {
        err = "unexpected trailing " + esc(in[parser.i]);
        return Json();
    }

#ifndef NDEBUG
    if (!parser.failed) {
        assert(parser.values.size() == 1);
        assert(parser.states.empty());
    }
#endif
    return parser.values.top();
}

// Documented in json11.hpp
vector<Json> Json::parse_multi(const string &in,
                               std::string::size_type &parser_stop_pos,
                               string &err,
                               JsonParse strategy) {
    JsonParserPriv parser { in, err, strategy };
    parser.eof = true;
    parser_stop_pos = 0;
    vector<Json> json_vec;
    while (parser.i != in.size() && !parser.failed && !parser.need_data) {
        parser.consume();
#ifndef NDEBUG
        if (!parser.failed)
            assert(parser.values.size() == 1);
#endif
        json_vec.push_back(parser.values.top());
        parser.values.pop();
        // Check for another object
        parser.consume_garbage();
        if (!parser.failed && !parser.need_data) {
            assert(parser.states.empty());
            parser.push_state(JsonParserPriv::EXPECT_VALUE);
            parser_stop_pos = parser.i;
        }
    }
    return json_vec;
}

/* * * * * * * * * * * * * * * * * * * *
 * Shape-checking
 */

bool Json::has_shape(const shape & types, string & err) const {
    if (!is_object()) {
        err = "expected JSON object, got " + dump();
        return false;
    }

    for (auto & item : types) {
        if ((*this)[item.first].type() != item.second) {
            err = "bad type for " + item.first + " in " + dump();
            return false;
        }
    }

    return true;
}

} // namespace json11
