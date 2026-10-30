// Diamond Dogs Together launcher core: a small JSON value (RFC 8259) for the
// tooling's records (friend-install.json, play-session.json, manifests,
// link-status.json). Objects keep their key order, so fields this program
// does not know are written back unchanged. Project-authored.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ddt::core::json {

enum class Type { null, boolean, number, string, array, object };

class Value {
public:
    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : type_(Type::boolean), bool_(b) {}
    Value(int v) : type_(Type::number), int_(v), is_int_(true) {}
    Value(unsigned v) : type_(Type::number), int_(v), is_int_(true) {}
    Value(std::int64_t v) : type_(Type::number), int_(v), is_int_(true) {}
    Value(std::uint64_t v) : type_(Type::number), int_(static_cast<std::int64_t>(v)), is_int_(true) {}
    Value(double v) : type_(Type::number), double_(v), is_int_(false) {}
    Value(const char* s) : type_(Type::string), string_(s ? s : "") {}
    Value(std::string s) : type_(Type::string), string_(std::move(s)) {}
    static Value Array() { Value v; v.type_ = Type::array; return v; }
    static Value Object() { Value v; v.type_ = Type::object; return v; }

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::null; }
    bool is_object() const { return type_ == Type::object; }
    bool is_array() const { return type_ == Type::array; }
    bool is_string() const { return type_ == Type::string; }
    bool is_number() const { return type_ == Type::number; }
    bool is_bool() const { return type_ == Type::boolean; }
    bool is_integer() const { return type_ == Type::number && is_int_; }   // a number without fraction or exponent

    // Lenient readers in the PowerShell tools' manner ([int] / [string] casts).
    bool AsBool(bool fallback = false) const;
    std::int64_t AsInt(std::int64_t fallback = 0) const;
    double AsDouble(double fallback = 0.0) const;
    // A string as is; a number as decimal text; true/false as "True"/"False".
    std::string AsText(const std::string& fallback = "") const;
    const std::string& StringRef() const { return string_; }

    // Objects. Find returns nullptr when absent (or this is no object).
    const Value* Find(const std::string& key) const;
    Value* Find(const std::string& key);
    // Inserts null when absent (turns a null value into an object first).
    Value& operator[](const std::string& key);
    void Set(const std::string& key, Value value) { (*this)[key] = std::move(value); }
    bool Remove(const std::string& key);
    const std::vector<std::pair<std::string, Value>>& Items() const { return members_; }

    // Arrays.
    std::vector<Value>& Elements() { return elements_; }
    const std::vector<Value>& Elements() const { return elements_; }
    void Push(Value value);
    std::size_t size() const { return type_ == Type::array ? elements_.size() : type_ == Type::object ? members_.size() : 0; }

    // Typed member access with a fallback.
    std::string Str(const std::string& key, const std::string& fallback = "") const;
    std::int64_t Int(const std::string& key, std::int64_t fallback = 0) const;
    bool Bool(const std::string& key, bool fallback = false) const;

private:
    Type type_ = Type::null;
    bool bool_ = false;
    std::int64_t int_ = 0;
    double double_ = 0.0;
    bool is_int_ = true;
    std::string string_;
    std::vector<Value> elements_;
    std::vector<std::pair<std::string, Value>> members_;
    friend void DumpTo(const Value& v, std::string& out, int indent, bool pretty);
};

// Parses a whole document (a UTF-8 BOM is skipped). Depth is limited to 64.
bool Parse(const std::string& text, Value& out, std::string* error = nullptr);
// Pretty: two-space indentation, "key": value; LF line ends, no final newline.
std::string Dump(const Value& value, bool pretty = true);

}  // namespace ddt::core::json
