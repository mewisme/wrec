#pragma once

#include <string>
#include <utility>
#include <variant>

template <typename T> class Result {
public:
  static Result ok(T value) { return Result(std::move(value)); }
  static Result fail(std::string error) { return Result(std::move(error)); }

  bool isOk() const { return std::holds_alternative<T>(data_); }
  const T &value() const { return std::get<T>(data_); }
  T &value() { return std::get<T>(data_); }
  const std::string &error() const { return std::get<std::string>(data_); }

private:
  explicit Result(T value) : data_(std::move(value)) {}
  explicit Result(std::string error) : data_(std::move(error)) {}

  std::variant<T, std::string> data_;
};

class Status {
public:
  static Status ok() { return Status(true, {}); }
  static Status fail(std::string error) {
    return Status(false, std::move(error));
  }

  bool isOk() const { return ok_; }
  const std::string &error() const { return error_; }

private:
  Status(bool ok, std::string error) : ok_(ok), error_(std::move(error)) {}

  bool ok_;
  std::string error_;
};
