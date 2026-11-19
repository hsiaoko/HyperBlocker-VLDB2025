#ifndef HYPERDEDUCE_BASE_STATUS_H_
#define HYPERDEDUCE_BASE_STATUS_H_

#include <optional>
#include <string>
#include <utility>

namespace hyperdeduce {

// A lightweight error carrier. Google style avoids exceptions, so functions
// that can fail on bad input return Status or StatusOr<T>. An ok Status carries
// no message; an error Status carries a human-readable reason.
class Status {
public:
  Status() = default;

  static Status Ok() { return Status(); }

  static Status Error(std::string message) {
    Status status;
    status.ok_ = false;
    status.message_ = std::move(message);
    return status;
  }

  bool ok() const { return ok_; }

  const std::string &message() const { return message_; }

private:
  bool ok_ = true;
  std::string message_;
};

// Holds either a value or an error Status. Callers check ok() before value().
template <typename T>
class StatusOr {
public:
  StatusOr(T value) : status_(Status::Ok()), value_(std::move(value)) {}

  StatusOr(Status status) : status_(std::move(status)) {}

  bool ok() const { return status_.ok(); }

  const Status &status() const { return status_; }

  const T &value() const & { return *value_; }

  T &&value() && { return std::move(*value_); }

private:
  Status status_;
  std::optional<T> value_;
};

} // namespace hyperdeduce

#endif // HYPERDEDUCE_BASE_STATUS_H_