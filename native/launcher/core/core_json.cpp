// Diamond Dogs Together launcher core: JSON value, parser and writer.
#include "core_json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ddt::core::json {

bool Value::AsBool(bool fallback) const {
    switch (type_) {
    case Type::boolean: return bool_;
    case Type::number: return is_int_ ? int_ != 0 : double_ != 0.0;
    case Type::string: return string_ == "1" || string_ == "true" || string_ == "True";
    default: return fallback;
    }
}

std::int64_t Value::AsInt(std::int64_t fallback) const {
    switch (type_) {
    case Type::boolean: return bool_ ? 1 : 0;
    case Type::number: return is_int_ ? int_ : static_cast<std::int64_t>(std::llround(double_));
    case Type::string: {
        if (string_.empty()) return fallback;
        char* end = nullptr;
        const long long v = std::strtoll(string_.c_str(), &end, 10);
        return end && *end == 0 ? static_cast<std::int64_t>(v) : fallback;
    }
    default: return fallback;
    }
}

double Value::AsDouble(double fallback) const {
    if (type_ == Type::number) return is_int_ ? static_cast<double>(int_) : double_;
    if (type_ == Type::string && !string_.empty()) {
        char* end = nullptr;
        const double v = std::strtod(string_.c_str(), &end);
        return end && *end == 0 ? v : fallback;
    }
    return fallback;
}

std::string Value::AsText(const std::string& fallback) const {
    switch (type_) {
    case Type::string: return string_;
    case Type::boolean: return bool_ ? "True" : "False";
    case Type::number: {
        if (is_int_) return std::to_string(int_);
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%.17g", double_);
        return buf;
    }
    default: return fallback;
    }
}

const Value* Value::Find(const std::string& key) const {
    if (type_ != Type::object) return nullptr;
    for (const auto& m : members_)
        if (m.first == key) return &m.second;
    return nullptr;
}

Value* Value::Find(const std::string& key) {
    if (type_ != Type::object) return nullptr;
    for (auto& m : members_)
        if (m.first == key) return &m.second;
    return nullptr;
}

Value& Value::operator[](const std::string& key) {
    if (type_ != Type::object) {
        *this = Object();
    }
    for (auto& m : members_)
        if (m.first == key) return m.second;
    members_.emplace_back(key, Value());
    return members_.back().second;
}

bool Value::Remove(const std::string& key) {
    for (auto it = members_.begin(); it != members_.end(); ++it) {
        if (it->first == key) {
            members_.erase(it);
            return true;
        }
    }
    return false;
}

void Value::Push(Value value) {
    if (type_ != Type::array) *this = Array();
    elements_.push_back(std::move(value));
}

std::string Value::Str(const std::string& key, const std::string& fallback) const {
    const Value* v = Find(key);
    return v && !v->is_null() ? v->AsText(fallback) : fallback;
}

std::int64_t Value::Int(const std::string& key, std::int64_t fallback) const {
    const Value* v = Find(key);
    return v && !v->is_null() ? v->AsInt(fallback) : fallback;
}

bool Value::Bool(const std::string& key, bool fallback) const {
    const Value* v = Find(key);
    return v && !v->is_null() ? v->AsBool(fallback) : fallback;
}

// ---- Parser ----------------------------------------------------------------
namespace {
class Parser {
public:
    explicit Parser(const std::string& t) : s_(t) {}
    bool Document(Value& out, std::string* error) {
        if (s_.size() >= 3 && static_cast<unsigned char>(s_[0]) == 0xEF && static_cast<unsigned char>(s_[1]) == 0xBB &&
            static_cast<unsigned char>(s_[2]) == 0xBF)
            p_ = 3;
        Skip();
        if (!ParseValue(out, 0)) {
            if (error) *error = error_.empty() ? "invalid JSON" : error_;
            return false;
        }
        Skip();
        if (p_ != s_.size()) {
            if (error) *error = "text after the JSON value";
            return false;
        }
        return true;
    }

private:
    void Skip() {
        while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\t' || s_[p_] == '\r' || s_[p_] == '\n')) ++p_;
    }
    bool Fail(const char* what) {
        if (error_.empty()) error_ = what;
        return false;
    }
    bool Literal(const char* word) {
        std::size_t n = 0;
        while (word[n]) ++n;
        if (s_.compare(p_, n, word) != 0) return Fail("invalid literal");
        p_ += n;
        return true;
    }
    static void AppendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    bool Hex4(unsigned& out) {
        if (p_ + 4 > s_.size()) return Fail("short \\u escape");
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[p_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else return Fail("bad \\u escape");
        }
        out = v;
        return true;
    }
    bool String(std::string& out) {
        ++p_;   // opening quote
        while (p_ < s_.size()) {
            const char c = s_[p_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return Fail("control character in a string");
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (p_ >= s_.size()) break;
            const char e = s_[p_++];
            switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                unsigned cp = 0;
                if (!Hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF && p_ + 1 < s_.size() && s_[p_] == '\\' && s_[p_ + 1] == 'u') {
                    const std::size_t save = p_;
                    p_ += 2;
                    unsigned low = 0;
                    if (!Hex4(low)) return false;
                    if (low >= 0xDC00 && low <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    else p_ = save;
                }
                AppendUtf8(out, cp);
                break;
            }
            default: return Fail("bad escape");
            }
        }
        return Fail("unterminated string");
    }
    bool Number(Value& out) {
        const std::size_t start = p_;
        if (s_[p_] == '-') ++p_;
        bool integral = true;
        while (p_ < s_.size()) {
            const char c = s_[p_];
            if (c >= '0' && c <= '9') ++p_;
            else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                integral = false;
                ++p_;
            } else break;
        }
        const std::string text = s_.substr(start, p_ - start);
        if (text.empty() || text == "-") return Fail("bad number");
        char* end = nullptr;
        if (integral && text.size() <= 18) {
            const long long v = std::strtoll(text.c_str(), &end, 10);
            if (end && *end == 0) {
                out = Value(static_cast<std::int64_t>(v));
                return true;
            }
        }
        const double d = std::strtod(text.c_str(), &end);
        if (!end || *end != 0) return Fail("bad number");
        if (integral && std::fabs(d) < 9.2e18) out = Value(static_cast<std::int64_t>(std::strtoll(text.c_str(), nullptr, 10)));
        else out = Value(d);
        return true;
    }
    bool ParseValue(Value& out, int depth) {
        if (depth > 64) return Fail("nested too deeply");
        if (p_ >= s_.size()) return Fail("unexpected end");
        const char c = s_[p_];
        if (c == '{') {
            ++p_;
            out = Value::Object();
            Skip();
            if (p_ < s_.size() && s_[p_] == '}') {
                ++p_;
                return true;
            }
            for (;;) {
                Skip();
                if (p_ >= s_.size() || s_[p_] != '"') return Fail("expected a key");
                std::string key;
                if (!String(key)) return false;
                Skip();
                if (p_ >= s_.size() || s_[p_] != ':') return Fail("expected ':'");
                ++p_;
                Skip();
                Value member;
                if (!ParseValue(member, depth + 1)) return false;
                out[key] = std::move(member);
                Skip();
                if (p_ < s_.size() && s_[p_] == ',') {
                    ++p_;
                    continue;
                }
                if (p_ < s_.size() && s_[p_] == '}') {
                    ++p_;
                    return true;
                }
                return Fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++p_;
            out = Value::Array();
            Skip();
            if (p_ < s_.size() && s_[p_] == ']') {
                ++p_;
                return true;
            }
            for (;;) {
                Skip();
                Value element;
                if (!ParseValue(element, depth + 1)) return false;
                out.Push(std::move(element));
                Skip();
                if (p_ < s_.size() && s_[p_] == ',') {
                    ++p_;
                    continue;
                }
                if (p_ < s_.size() && s_[p_] == ']') {
                    ++p_;
                    return true;
                }
                return Fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            std::string text;
            if (!String(text)) return false;
            out = Value(std::move(text));
            return true;
        }
        if (c == 't') {
            if (!Literal("true")) return false;
            out = Value(true);
            return true;
        }
        if (c == 'f') {
            if (!Literal("false")) return false;
            out = Value(false);
            return true;
        }
        if (c == 'n') {
            if (!Literal("null")) return false;
            out = Value();
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) return Number(out);
        return Fail("unexpected character");
    }

    const std::string& s_;
    std::size_t p_ = 0;
    std::string error_;
};

void Escape(const std::string& s, std::string& out) {
    out.push_back('"');
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out.push_back(ch);
            }
        }
    }
    out.push_back('"');
}
}  // namespace

void DumpTo(const Value& v, std::string& out, int indent, bool pretty) {
    auto newline = [&](int level) {
        if (!pretty) return;
        out.push_back('\n');
        out.append(static_cast<std::size_t>(level) * 2, ' ');
    };
    switch (v.type_) {
    case Type::null: out += "null"; break;
    case Type::boolean: out += v.bool_ ? "true" : "false"; break;
    case Type::number: out += v.AsText(); break;
    case Type::string: Escape(v.string_, out); break;
    case Type::array:
        if (v.elements_.empty()) {
            out += "[]";
            break;
        }
        out.push_back('[');
        for (std::size_t i = 0; i < v.elements_.size(); ++i) {
            if (i) out.push_back(',');
            newline(indent + 1);
            DumpTo(v.elements_[i], out, indent + 1, pretty);
        }
        newline(indent);
        out.push_back(']');
        break;
    case Type::object:
        if (v.members_.empty()) {
            out += "{}";
            break;
        }
        out.push_back('{');
        for (std::size_t i = 0; i < v.members_.size(); ++i) {
            if (i) out.push_back(',');
            newline(indent + 1);
            Escape(v.members_[i].first, out);
            out += pretty ? ": " : ":";
            DumpTo(v.members_[i].second, out, indent + 1, pretty);
        }
        newline(indent);
        out.push_back('}');
        break;
    }
}

bool Parse(const std::string& text, Value& out, std::string* error) {
    Parser p(text);
    Value v;
    if (!p.Document(v, error)) return false;
    out = std::move(v);
    return true;
}

std::string Dump(const Value& value, bool pretty) {
    std::string out;
    DumpTo(value, out, 0, pretty);
    return out;
}

}  // namespace ddt::core::json
