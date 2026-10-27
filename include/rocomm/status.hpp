#ifndef ROCOMM_STATUS_HPP_
#define ROCOMM_STATUS_HPP_

#include <memory>
#include <string>
#include <vector>

namespace rocomm {

enum class StatusCode {
    Ok,
    InvalidHandle,
    InvalidState,
    InvalidArgument,
    InvalidContext,
    Unsupported,
    UnsupportedConfiguration,
    UnsupportedServiceKind,
    Timeout,
    Cancelled,
    ResourceExhausted,
    PartialFailure,
    AggregateFailure,
    ProtocolError,
    MiddlewareError,
    InternalError,
    RuntimeStopped,
};

struct TargetDiagnostic {
    std::string target;
    StatusCode code = StatusCode::Ok;
    std::string message;
};

struct Diagnostic {
    std::string message;
    std::vector<TargetDiagnostic> targets;
};

class Status {
public:
    Status() noexcept = default;

    explicit Status(StatusCode code) noexcept : code_(code) {}

    StatusCode code() const noexcept { return code_; }

    bool ok() const noexcept { return code_ == StatusCode::Ok; }

    const Diagnostic* diagnostic() const noexcept { return diagnostic_.get(); }

private:
    StatusCode code_ = StatusCode::Ok;
    std::shared_ptr<const Diagnostic> diagnostic_;
};

}  // namespace rocomm

#endif  // ROCOMM_STATUS_HPP_
