// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A minimal stand-in for the few JUCE names that Replicant 303's DSP/Pattern.h and Presets/PresetBank.{h,cpp} use (juce::String,
// juce::ValueTree with juce::var, jlimit, the lock types), so that the ORIGINAL generator can be compiled unchanged on the host and
// serve as the oracle of app/seq/tests/seq_gen_oracle_test.cpp. Only what those three files call is here, with the semantics the
// generator needs; the serialisation functions compile against it but are never run. Test code only: never part of the firmware.

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace juce {

using uint8 = std::uint8_t;
using uint32 = std::uint32_t;

template <typename Type>
Type jlimit(Type lowerLimit, Type upperLimit, Type valueToConstrain) noexcept {
  return valueToConstrain < lowerLimit ? lowerLimit : (upperLimit < valueToConstrain ? upperLimit : valueToConstrain);
}

template <typename Type>
bool isPositiveAndBelow(Type valueToTest, Type upperLimit) noexcept {
  return Type() <= valueToTest && valueToTest < upperLimit;
}

class String {
 public:
  String() = default;
  String(const char* text) : text_(text) {}  // NOLINT: implicit, as JUCE's
  String(int value) : text_(std::to_string(value)) {}

  static String charToString(char c) { return String(std::string(1, c).c_str()); }

  bool isEmpty() const noexcept { return text_.empty(); }
  int length() const noexcept { return static_cast<int>(text_.size()); }
  String trim() const {
    const auto b = text_.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) {
      return String();
    }
    const auto e = text_.find_last_not_of(" \t\r\n");
    return String(text_.substr(b, e - b + 1).c_str());
  }
  String substring(int start, int end) const {
    const auto n = static_cast<int>(text_.size());
    start = jlimit(0, n, start);
    end = jlimit(start, n, end);
    return String(text_.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start)).c_str());
  }
  const char* toRawUTF8() const noexcept { return text_.c_str(); }

  String& operator+=(const String& other) {
    text_ += other.text_;
    return *this;
  }
  friend String operator+(String a, const String& b) {
    a += b;
    return a;
  }
  friend bool operator==(const String& a, const String& b) noexcept { return a.text_ == b.text_; }
  friend bool operator!=(const String& a, const String& b) noexcept { return a.text_ != b.text_; }

 private:
  std::string text_;
};

class var {
 public:
  var() = default;
  var(bool v) : number_(v ? 1.0 : 0.0) {}
  var(int v) : number_(static_cast<double>(v)) {}
  var(double v) : number_(v) {}
  var(const char* v) : text_(v), is_text_(true) {}
  var(const String& v) : text_(v.toRawUTF8()), is_text_(true) {}

  operator int() const { return static_cast<int>(number_); }
  operator bool() const { return number_ != 0.0; }
  operator double() const { return number_; }
  String toString() const { return is_text_ ? String(text_.c_str()) : String(static_cast<int>(number_)); }

 private:
  double number_ = 0.0;
  std::string text_;
  bool is_text_ = false;
};

class ValueTree {
 public:
  ValueTree() = default;
  explicit ValueTree(const char* type) : node_(std::make_shared<Node>()) { node_->type = type; }

  bool isValid() const noexcept { return node_ != nullptr; }
  bool hasType(const char* type) const noexcept { return node_ != nullptr && node_->type == type; }

  ValueTree& setProperty(const String& name, const var& value, void* /*undoManager*/) {
    if (node_ != nullptr) {
      node_->properties[name.toRawUTF8()] = value;
    }
    return *this;
  }
  var getProperty(const String& name, const var& defaultValue = var()) const {
    if (node_ != nullptr) {
      const auto it = node_->properties.find(name.toRawUTF8());
      if (it != node_->properties.end()) {
        return it->second;
      }
    }
    return defaultValue;
  }
  void appendChild(const ValueTree& child, void* /*undoManager*/) {
    if (node_ != nullptr) {
      node_->children.push_back(child);
    }
  }
  int getNumChildren() const noexcept { return node_ != nullptr ? static_cast<int>(node_->children.size()) : 0; }
  ValueTree getChild(int index) const {
    return (node_ != nullptr && index >= 0 && index < getNumChildren()) ? node_->children[static_cast<std::size_t>(index)] : ValueTree();
  }
  ValueTree getChildWithName(const char* type) const {
    if (node_ != nullptr) {
      for (const auto& c : node_->children) {
        if (c.hasType(type)) {
          return c;
        }
      }
    }
    return ValueTree();
  }

 private:
  struct Node {
    std::string type;
    std::map<std::string, var> properties;
    std::vector<ValueTree> children;
  };
  std::shared_ptr<Node> node_;
};

class CriticalSection {};

class ScopedLock {
 public:
  explicit ScopedLock(const CriticalSection&) noexcept {}
};

}  // namespace juce
