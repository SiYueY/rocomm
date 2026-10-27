#ifndef ROCOMM_RESULT_HPP_
#define ROCOMM_RESULT_HPP_

#include <exception>
#include <optional>
#include <utility>

#include <rocomm/status.hpp>

namespace rocomm {

template <typename T>
class Result {
public:
    Result(T value) : value_(std::move(value)) {}

    Result(Status status) noexcept : status_(std::move(status)) {}

    bool ok() const noexcept { return value_.has_value(); }

    explicit operator bool() const noexcept { return ok(); }

    T& value() & noexcept {
        if (!value_) {
            std::terminate();
        }
        return *value_;
    }

    const T& value() const& noexcept {
        if (!value_) {
            std::terminate();
        }
        return *value_;
    }

    T&& value() && noexcept {
        if (!value_) {
            std::terminate();
        }
        return std::move(*value_);
    }

    const Status& status() const noexcept {
        static const Status kOk;
        return value_ ? kOk : status_;
    }

private:
    std::optional<T> value_;
    Status status_ = Status(StatusCode::InternalError);
};

template <>
class Result<void> {
public:
    Result() noexcept = default;

    Result(Status status) noexcept : status_(std::move(status)) {}

    bool ok() const noexcept { return status_.ok(); }

    explicit operator bool() const noexcept { return ok(); }

    const Status& status() const noexcept { return status_; }

private:
    Status status_;
};

}  // namespace rocomm

#endif  // ROCOMM_RESULT_HPP_
