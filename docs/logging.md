# RoComm Logging V1 设计

**Project:** RoComm — Robotics Communication Runtime  
**Module:** `rocomm::logging`  
**Language:** C++17  
**Architecture Status:** **FROZEN — V1 Design Baseline**  
**Semantic Contract:** **FROZEN**  
**Concurrency Model:** **FROZEN**  
**Backend SPI Semantic Model:** **FROZEN**  
**Exact Public C++ Headers:** **HEADER/API FINALIZATION REQUIRED**

本文档定义 RoComm Logging V1 的公共接口模型、Runtime-local 生命周期、Category 与过滤语义、同步/异步执行模型、有界队列、Backend SPI、spdlog 集成、文本输出、RawLog、错误隔离、Runtime 集成、工程结构、测试标准以及 V2 NativeBackend 的演进边界。

Logging 已从《RoComm V1 架构与接口设计》中拆分为独立模块。主架构只保留 Logging Integration Contract；本文是 Logging 子系统自身的正式设计基线。

---

# 1. 模块定位、设计原则与总体架构

## 1.1 模块定位

RoComm Logging 是 RoComm Runtime 的基础可观测性组件。

它负责：

```text
LogLevel
Category
SourceLocation
LogRecord

Logger frontend

compile-time filtering
runtime category filtering

LoggingContext

synchronous dispatch
asynchronous dispatch

bounded queue
overflow policy

Backend lifecycle

Flush
Shutdown

statistics

Raw emergency logging
```

它不负责：

```text
Metrics
Tracing
Distributed tracing
Telemetry pipeline

日志采集 Agent
网络日志服务器

业务事件总线
Audit framework
Crash dump framework
```

这些能力未来如果需要，应分别设计独立模块，而不能继续向 Logging 内部堆叠。

核心原则：

> **Logging 是 RoComm Runtime 的基础 primitive，而不是通用 Observability Framework。**

---

## 1.2 与 RoComm 主架构的边界

RoComm 主架构只依赖以下 Logging Integration Contract：

```text
1. Logging 是 Runtime-local 的。

2. Core 与 Middleware 使用统一 Logger abstraction。

3. Logging 支持 Category / Severity filtering。

4. disabled logging 不执行日志 message expression。

5. Public API 不泄漏第三方 logging 类型。

6. Logging failure 不修改 Topic / Service operation 的 Status。

7. 默认异步 Logging 不在通信线程执行 Sink / output I/O。

8. Runtime::Stop() 统一管理 Logging lifecycle。

9. fatal / recursion / logging subsystem failure
   必须存在独立 RawLog fallback。

10. Logging shutdown timeout 不通过 unsafe thread termination
    强制终止 Backend 或调用线程。

11. Runtime shutdown 时 Logging 最后关闭，
    Middleware / Executor shutdown 期间仍可正常记录日志。
```

以下实现和接口细节全部属于本文：

```text
Logger
LogRecord

LoggingConfig
LoggingContext

CategoryLevels
CategoryEntry

AsyncQueue
Overflow

Backend SPI

TextFormatter
TextBackendConfig

SpdlogBackend

RawLog

Logging tests
V2 NativeBackend
```

---

## 1.3 V1 / V2 总体路线

Logging 采用：

> **RoComm-owned frontend + Runtime-local logging runtime + narrow replaceable backend。**

V1：

```text
Logger
LogRecord

LoggingConfig
LoggingContext

Category policy

Sync dispatch
AsyncQueue
Async writer

RawLog
TextFormatter

LoggingBackend SPI

Test Backends

SpdlogBackend
```

V2：

```text
NativeBackend
├── Console output
├── File output
└── Rotating file output
```

V2 必须直接复用 V1：

```text
Logger

LogRecord

LoggingConfig
LoggingContext

Category filtering

Sync / Async dispatch

AsyncQueue
Overflow

Flush
Shutdown
Timeout

RawLog

TextFormatter
TextBackendConfig

LoggingBackend SPI

Backend Contract Tests
```

因此：

> **V2 的主要新增内容是 NativeBackend，而不是第二套 Logging Runtime。**

---

## 1.4 总体架构

```text
Application / RoComm Core / Middleware
                 │
                 ▼
               Logger
                 │
        compile-time filtering
                 │
                 ▼
       Runtime Category filtering
                 │
                 ▼
          message construction
                 │
                 ▼
              LogRecord
                 │
                 ▼
          LoggingContext
          ┌──────┴──────┐
          │             │
          ▼             ▼
     Synchronous     Asynchronous
          │             │
          │         AsyncQueue
          │             │
          │         writer thread
          │             │
          └──────┬──────┘
                 │
                 ▼
       Backend invocation guard
                 │
                 ▼
          LoggingBackend
       ┌─────────┼─────────┐
       ▼         ▼         ▼
    Spdlog    Native     Custom
      V1        V2       Backend
```

Emergency path：

```text
logging recursion
backend failure
allocation failure
late teardown
fatal invariant
        │
        ▼
      RawLog
        │
        ▼
low-level stderr output
```

---

## 1.5 四层职责

Logging V1 划分为四个明确层次。

### Logger Frontend

负责：

```text
Category identity

Enabled()

SourceLocation

Log()

LogLazy()

logging macros
```

不负责：

```text
queue
writer
Backend lifecycle
console
file
rotation
```

---

### LoggingContext

负责：

```text
Runtime-local ownership

lifecycle/admission state

CategoryLevels
CategoryEntry registry

global filtering

sync / async dispatch

AsyncQueue

writer lifecycle

overflow policy

Backend invocation serialization

Backend recursion boundary

Flush

Shutdown

timeout

statistics
```

它是 Logging module 的 Runtime 核心。

---

### LoggingBackend

负责：

```text
消费 LogRecord

backend-specific rendering

backend-specific routing

secondary output filtering

console / file / rotation

backend-specific I/O

backend-specific stats
```

Backend 不拥有：

```text
RoComm global category policy

RoComm async queue

RoComm overflow semantics

RoComm Runtime Stop semantics
```

---

### RawLog

RawLog 完全独立于前三层。

不得依赖：

```text
Logger

LoggingContext

LoggingBackend

AsyncQueue

spdlog

普通 logging mutex

heap allocation
```

---

## 1.6 为什么 AsyncQueue 属于 LoggingContext

以下语义是 RoComm Logging 的公共契约：

```text
Synchronous
Asynchronous

DropNewest
DropOldest
Block

Flush barrier

Shutdown

timeout

statistics
```

如果 queue/worker 属于：

```text
SpdlogBackend
NativeBackend
CustomBackend
```

那么不同 Backend 将拥有不同：

```text
queue
overflow
worker
flush
shutdown
```

实现。

这会导致：

```text
LoggingMode
LogOverflowPolicy
Flush
Shutdown
```

实际上失去统一语义。

因此正式冻结：

> **Async dispatch、queue、overflow、worker lifecycle 属于 LoggingContext，而不是 LoggingBackend。**

从 LoggingContext 看：

> **LoggingBackend 是同步、串行调用的 LogRecord consumer。**

即便某个 Custom Backend 内部自行 buffering，它也只能把那部分机制视作自己的私有实现，不能改变 RoComm LoggingContext 已经承诺的公共语义。

---

## 1.7 为什么 V1 不使用 spdlog async runtime

spdlog 自身已经拥有成熟的 asynchronous logging 和多种 overflow policy。

因此 RoComm 不使用 `spdlog::async_logger` 并不是因为 spdlog 功能不足。

真正原因是：

```text
RoComm 必须自己拥有 Runtime-local queue。

RoComm 必须统一定义 Overflow contract。

RoComm 必须统一统计 queue/drop metrics。

RoComm 必须统一 Flush / Shutdown / timeout。

SpdlogBackend / NativeBackend / CustomBackend
必须表现出相同的 async semantics。

不能让第三方 thread-pool ownership
反向决定 RoComm Runtime lifecycle。
```

因此 V1 使用：

```text
LoggingContext asynchronous runtime

        +

synchronous SpdlogBackend
```

---

## 1.8 Runtime-local 原则

每一个 RoComm Runtime 拥有自己的 LoggingContext。

禁止依赖：

```text
global mutable logger singleton

process-global mutable category registry

process-global mutable log level

process-global mutable Backend
```

两个 Runtime 可以独立拥有：

```text
不同 default level

不同 category override

不同 Backend

不同文件

不同 queue capacity

不同 overflow policy

不同测试 CaptureBackend
```

关闭其中一个 Runtime：

```text
不得影响另一个 Runtime
```

---

## 1.9 热路径原则

disabled logging：

```text
不执行 message expression

不分配 heap

不访问 Backend

不执行 Sink I/O

不获取 ordinary mutex
```

Async enabled logging：

```text
允许 caller-side message construction

允许 caller-side message allocation

允许 queue synchronization

不执行 Backend Sink I/O
```

这一点必须明确：

> **Async enabled path 不承诺 caller-side zero allocation。**

因为：

```text
message construction
std::string materialization
```

本身可能需要 allocation。

RoComm V1 的性能承诺是：

> **不把 output I/O 放在异步日志调用线程，而不是宣称所有日志路径都 zero-allocation。**

---

## 1.10 Sync 模式的明确代价

Synchronous 模式：

```text
caller thread
    ↓
LoggingContext serialization
    ↓
Backend::Write()
    ↓
output I/O
```

因此：

```text
Backend mutex contention
filesystem delay
terminal delay
```

会直接传播到调用点。

这是 Sync 模式的明确定义，不属于 Logging bug。

因此：

> **Sync mode 不是通信 hot-path optimization mode。**

需要低干扰路径时应使用：

```text
Asynchronous
+
非 Block overflow policy
```

---

## 1.11 参考项目与设计取舍

RoComm Logging 不复刻某一个日志库，而是分别借鉴高质量 C++ 项目的不同设计点。

| 参考项目 | RoComm 主要借鉴 |
|---|---|
| Abseil Logging | 窄前端、窄消费接口、Raw logging、recursion 处理 |
| Boost.Log | 全局 filtering 与 output-specific filtering 分层 |
| spdlog | 成熟 console/file/rotation 输出实现 |
| Folly XLOG | disabled hot path、lazy evaluation、compile-time filtering |
| brpc | 项目级 logging abstraction 与二次 filtering |
| Qt `QLoggingCategory` | runtime category level 调整 |
| Quill | frontend/backend 分离、未来 low-latency 优化方向 |
| ClickHouse | Backend 内部多 output routing 思路 |

明确不照搬：

```text
Abseil process-global logger registry

Boost.Log 大型 attributes/filter subsystem

spdlog global registry
spdlog global shutdown
spdlog async runtime

Quill V1 deferred argument encoding

Qt arbitrary CategoryFilter callback

ClickHouse route/channel subsystem
```

RoComm 的目标始终保持：

> **窄 API、明确生命周期、低依赖、Runtime-local、可替换 Backend。**

---

# 2. Public API、数据模型、Category 与配置

## 2.1 Namespace

公共：

```cpp
namespace rocomm::logging {
}
```

Backend-specific factory：

```cpp
namespace rocomm::logging::spdlog {
}
```

未来：

```cpp
namespace rocomm::logging::native {
}
```

Logging 内部不使用：

```text
rocomm::logging::detail
rocomm::logging::internal
rocomm::logging::impl
```

private 源码已经通过：

```text
src/logging/
```

形成物理边界。

---

## 2.2 LogLevel

```cpp
enum class LogLevel : std::uint8_t {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Critical = 5,
    Off = 6,
};
```

severity 顺序：

```text
Trace
  <
Debug
  <
Info
  <
Warn
  <
Error
  <
Critical
  <
Off
```

例如 effective level：

```text
Warn
```

则：

```text
Trace    disabled
Debug    disabled
Info     disabled

Warn     enabled
Error    enabled
Critical enabled
```

`Off` 只用于：

```text
threshold
configuration
compile-time filtering
```

不允许创建：

```text
LogRecord(level = Off)
```

---

## 2.3 Critical 不等于 Fatal

V1 不定义：

```text
Fatal
```

为普通 LogLevel。

因为：

> **日志严重程度和进程控制是两个不同概念。**

`Critical`：

```text
最高普通 severity
```

不会自动：

```text
abort
exit
terminate
```

真正 fail-fast 路径：

```text
best-effort Critical log
        或
      RawLog
        ↓
std::terminate()
```

由调用点显式执行。

---

## 2.4 SourceLocation

C++17 不使用：

```cpp
std::source_location
```

V1：

```cpp
class SourceLocation {
public:
    constexpr SourceLocation() noexcept = default;

    static constexpr SourceLocation
    FromStatic(
        const char* file,
        const char* function,
        std::uint32_t line) noexcept {
        return SourceLocation{
            file,
            function,
            line};
    }

    constexpr const char*
    file() const noexcept {
        return file_;
    }

    constexpr const char*
    function() const noexcept {
        return function_;
    }

    constexpr std::uint32_t
    line() const noexcept {
        return line_;
    }

private:
    constexpr SourceLocation(
        const char* file,
        const char* function,
        std::uint32_t line) noexcept
        : file_(file),
          function_(function),
          line_(line) {}

    const char* file_ = nullptr;
    const char* function_ = nullptr;
    std::uint32_t line_ = 0;
};
```

统一宏：

```cpp
#define ROCOMM_SOURCE_LOCATION \
    ::rocomm::logging::SourceLocation::FromStatic( \
        __FILE__, \
        __func__, \
        __LINE__)
```

`FromStatic()` 表达的契约是：

> `file` 和 `function` 必须具有至少覆盖该 LogRecord 生命周期的稳定存储。

框架宏提供的编译器字符串满足这一要求。

---

## 2.5 LogRecord

LogRecord 表示：

> **message 已构造完成、caller metadata 已捕获，但尚未完成 output rendering 的逻辑日志记录。**

概念 Public API：

```cpp
class LogRecord {
public:
    using TimePoint =
        std::chrono::system_clock::time_point;

    LogRecord(
        const LogRecord&) = delete;

    LogRecord&
    operator=(
        const LogRecord&) = delete;

    LogRecord(
        LogRecord&&) noexcept;

    LogRecord&
    operator=(
        LogRecord&&) noexcept;

    TimePoint
    timestamp() const noexcept;

    LogLevel
    level() const noexcept;

    std::string_view
    category() const noexcept;

    std::string_view
    message() const noexcept;

    SourceLocation
    source() const noexcept;

    std::thread::id
    thread_id() const noexcept;

private:
    // constructed only by logging runtime
};
```

用户不能直接构造一个正常 LogRecord。

---

## 2.6 LogRecord message ownership

LogRecord 必须独立拥有：

```text
message content
```

例如：

```cpp
logger.Log(
    LogLevel::Info,
    temporary_string);
```

返回以后：

```text
temporary_string
```

可以立即销毁。

Async queue 和 Backend 不依赖原 caller 字符串。

---

## 2.7 Category storage lifetime

Category 不要求每条 LogRecord 单独复制：

```text
"rocomm.fastdds.discovery"
```

LoggingContext 可以 intern category 并建立：

```text
CategoryEntry
```

LogRecord 保存：

```text
stable CategoryEntry reference/handle
```

关键生命周期契约：

> **Interned category storage 由 self-owned Logging State 持有。**

其生命周期必须至少覆盖：

```text
所有 queued LogRecord

所有正在 Backend::Write() 中的 LogRecord

所有 shutdown timeout 后仍 in-flight 的 LogRecord
```

因此即使：

```text
LoggingContext wrapper 已销毁
+
shutdown 已 Timeout
```

只要 Backend 仍处理某 LogRecord：

```text
record.category()
```

仍然有效。

---

## 2.8 Backend 对 LogRecord 只有 borrowed access

```cpp
virtual void
Write(
    const LogRecord& record) noexcept = 0;
```

Backend 不得在 `Write()` 返回后保存：

```text
&record

record.message().data()

record.category().data()

SourceLocation 中的 pointer
```

如果 Custom Backend 需要延迟保存：

> **Backend 必须自行复制所需字段。**

LoggingBackend SPI 本身是：

```text
synchronous consumer contract
```

---

## 2.9 两级格式化模型

V1 明确区分：

```text
Message Construction
```

与：

```text
Record Rendering
```

---

### 2.9.1 Message Construction

发生在：

```text
Logger frontend
caller thread
```

例如：

```text
"Created publisher '/robot/state'"
```

成为：

```text
LogRecord::message()
```

这一步允许：

```text
std::string allocation
```

---

### 2.9.2 Record Rendering

发生在：

```text
Backend dispatch side
```

把：

```text
timestamp
level
category
thread_id
source
message
```

渲染成：

```text
2026-10-26T14:35:20.123456+09:00 [INFO] [rocomm.fastdds.discovery] [tid=18342] Participant matched [discovery.cpp:217]
```

---

## 2.10 为什么采用两级格式化

这样可以同时满足：

```text
Public header 无 fmt/spdlog 依赖

caller metadata 真实

Async 模式不让 Backend 重新生成错误 thread ID

Backend 可以自行决定最终文本样式
```

---

## 2.11 Logger

Logger 是：

> **轻量、copyable、Runtime-bound handle。**

概念：

```cpp
class Logger {
public:
    Logger() noexcept = default;

    bool Enabled(
        LogLevel level) const noexcept;

    void Log(
        LogLevel level,
        std::string_view message,
        SourceLocation source = {}) const noexcept;

    template <typename Builder>
    void LogLazy(
        LogLevel level,
        Builder&& builder,
        SourceLocation source = {}) const noexcept;

    std::string_view
    category() const noexcept;
};
```

---

## 2.12 默认构造 Logger

正式冻结：

```cpp
Logger logger;
```

得到：

```text
disabled Logger
```

其行为：

```text
logger.Enabled(...)
→ false

logger.Log(...)
→ safe no-op

logger.LogLazy(...)
→ builder 不执行
```

这与：

```text
LoggingContext 已 Stopped 后的已有 Logger
```

行为一致。

---

## 2.13 Logger copy / move 性能

Logger copy：

```text
O(1)

no heap allocation
```

Logger move：

```text
O(1)

no heap allocation
```

实现可以选择：

```text
std::shared_ptr

intrusive refcount

specialized state handle
```

但具体机制不是 V1 semantic contract。

因此：

> **V1 不冻结 `std::shared_ptr`。**

如果 profiling 证明 shared_ptr atomic refcount 成本明显，可以替换内部 representation。

---

## 2.14 Logger lifetime

Logger 内部必须安全保持所需 Logging State 生命周期。

LoggingContext wrapper 销毁后：

```text
Logger memory-safe
```

但 Logging State 已进入：

```text
Stopping / Stopped
```

因此：

```text
Logger::Enabled()
→ false

Logger::Log()
→ safe no-op
```

Logger 引用不能让 Logging pipeline 永远保持 Running。

---

## 2.15 Logger::Enabled()

必须：

```text
noexcept

O(1)

zero heap allocation

no Backend I/O

no ordinary mutex
```

逻辑至少等价于：

```cpp
if (!state_) {
    return false;
}

if (state_->lifecycle() != Running) {
    return false;
}

return IsEnabled(
    level,
    entry_->effective_level());
```

因此通常包含：

```text
lifecycle/admission atomic load

+

effective-level atomic load
```

---

## 2.16 Enabled() 的原子表示

V1 不要求直接使用：

```cpp
std::atomic<LogLevel>
```

推荐内部使用明确 scalar representation：

```text
uint8 / uint32
```

并在目标平台验证：

```text
lock-free atomic support
```

冻结的是：

```text
scalar atomic read

no ordinary mutex

O(1)
```

而不是具体 C++ storage type。

---

## 2.17 Shutdown 正确性不能只依赖 CategoryEntry=Off

Shutdown 的真正 admission 线性化点是：

```text
Running
   ↓ atomic transition
Stopping
```

这一状态转换一旦成功：

```text
所有新的正常 log admission 都必须失败
```

之后可以把现有 CategoryEntry：

```text
effective_level → Off
```

作为优化，减少已持有 Logger 的后续检查成本。

但：

> **批量设置 CategoryEntry=Off 不是 Shutdown correctness mechanism。**

因为 O(n) 更新本身存在时间窗口。

---

## 2.18 Logger::Log()

```cpp
void Log(
    LogLevel level,
    std::string_view message,
    SourceLocation source = {}) const noexcept;
```

正常语义：

```text
recursion check
↓
state / level check
↓
caller timestamp
↓
caller thread ID
↓
message materialization
↓
LogRecord construction
↓
LoggingContext admission
```

返回前 caller-owned message 已经完成 materialization。

---

## 2.19 Logger::LogLazy()

目的：

> **disabled logging 时 message expression 完全不执行。**

正常路径：

```text
recursion check

↓

Enabled(level)

↓

builder()

↓

message materialization

↓

Log()
```

---

## 2.20 LogLazy Builder contract

V1 不使用模糊的：

```text
message-like type
```

正式限制为：

Builder 返回下列之一：

```text
std::string

std::string_view

const char*
```

Header Finalization 可以通过 traits 给出清晰 static_assert。

不在 V1 支持：

```text
arbitrary streamable type
fmt object
custom formatter object
```

---

## 2.21 Log() 与 LogLazy() 的区别

直接：

```cpp
logger.Log(
    LogLevel::Debug,
    BuildMessage());
```

调用方会先执行：

```text
BuildMessage()
```

然后才能进入 `Log()`。

所以：

```text
Log()
```

不能阻止 caller-side expression construction。

而：

```cpp
ROCOMM_LOG_DEBUG(
    logger,
    BuildMessage());
```

通过 `LogLazy()`：

```text
Enabled == false
→ BuildMessage() 不执行
```

因此 hot-path 推荐：

```text
logging macro
或
LogLazy()
```

而不是直接 `Log(BuildMessage())`。

---

## 2.22 Recursion check 必须早于 Enabled()

如果 Backend invocation guard 已经 active：

```text
Logger::Log()
Logger::LogLazy()
```

不能先检查：

```text
Enabled()
```

因为 Backend Initialize/Shutdown 期间 LoggingContext 可能尚未处于普通 Running 状态。

正式顺序：

```text
recursion guard
    ↓
normal Enabled check
```

---

## 2.23 Recursive Log()

如果：

```text
BackendInvocationGuard active
```

且调用：

```cpp
logger.Log(
    level,
    message,
    source);
```

则：

```text
不进入 LoggingContext

不检查普通 Enabled

best-effort RawLog

recursive_fallbacks++
```

---

## 2.24 Recursive LogLazy()

如果 guard active：

```text
进入 emergency lazy path
```

该路径：

```text
只执行 builder 一次

catch all

bounded materialization

RawLog
```

同时设置：

```text
RawFallbackGuard
```

如果 builder 内再次触发 Logger：

```text
直接 suppress nested recursive logging
```

不得形成：

```text
Raw fallback
→ builder
→ Logger
→ Raw fallback
→ ...
```

无限递归。

---

## 2.25 V1 不提供 fmt-style Core API

C++17 没有：

```cpp
std::format
```

而公共头文件不能强制依赖：

```text
fmt
spdlog
```

因此 V1 Core 不冻结：

```cpp
logger.Info(
    "publisher {} created",
    name);
```

这种 typed format API。

推荐：

```cpp
ROCOMM_LOG_INFO(
    logger,
    BuildMessage(name));
```

未来可以单独新增：

```text
rocomm::logging::fmt
```

adapter target。

该 adapter 不属于 Logging Core dependency。

---

## 2.26 Logging macros

至少提供：

```text
ROCOMM_LOG_TRACE
ROCOMM_LOG_DEBUG
ROCOMM_LOG_INFO
ROCOMM_LOG_WARN
ROCOMM_LOG_ERROR
ROCOMM_LOG_CRITICAL
```

概念：

```cpp
#define ROCOMM_LOG_INFO(logger_expr, message_expr) \
    do { \
        auto&& _rocomm_logger = (logger_expr); \
        _rocomm_logger.LogLazy( \
            ::rocomm::logging::LogLevel::Info, \
            [&]() -> decltype(auto) { \
                return (message_expr); \
            }, \
            ROCOMM_SOURCE_LOCATION); \
    } while (false)
```

要求：

```text
logger_expr
→ 最多求值一次

message_expr
→ 仅 Enabled 时求值
```

---

## 2.27 Compile-time filtering

预处理级别固定为：

```cpp
#define ROCOMM_LOG_LEVEL_TRACE     0
#define ROCOMM_LOG_LEVEL_DEBUG     1
#define ROCOMM_LOG_LEVEL_INFO      2
#define ROCOMM_LOG_LEVEL_WARN      3
#define ROCOMM_LOG_LEVEL_ERROR     4
#define ROCOMM_LOG_LEVEL_CRITICAL  5
#define ROCOMM_LOG_LEVEL_OFF       6
```

必须与：

```cpp
LogLevel
```

underlying ordering 保持一致。

---

## 2.28 `ROCOMM_LOGGING_COMPILED_LEVEL`

```text
ROCOMM_LOGGING_COMPILED_LEVEL
```

取值必须为：

```text
0..6
```

或者上述对应 macro。

默认：

```text
ROCOMM_LOG_LEVEL_TRACE
```

原因：

> 默认 build 不应在编译期永久移除 Debug/Trace，从而仍允许运行时动态启用。

Release / deployment 可以显式配置更高 compiled level。

---

## 2.29 Compile-time macro 语义

例如：

```text
ROCOMM_LOGGING_COMPILED_LEVEL
=
ROCOMM_LOG_LEVEL_INFO
```

则：

```text
TRACE macro
DEBUG macro
```

必须完全编译为空操作。

并且：

```text
logger_expr 不求值

message_expr 不求值
```

直接调用：

```cpp
logger.Log(...)
```

不属于 compile-time stripping contract。

---

## 2.30 Category model

Category 使用：

```text
dot-separated hierarchy
```

正式官方 category 至少：

```text
rocomm

rocomm.runtime

rocomm.topic
rocomm.service

rocomm.execution
rocomm.async

rocomm.transport
rocomm.type
rocomm.middleware

rocomm.fastdds
rocomm.ros2
rocomm.zenoh
rocomm.grpc
```

例如：

```text
rocomm.fastdds.discovery

rocomm.fastdds.writer

rocomm.service.client

rocomm.service.server
```

---

## 2.31 Category namespace

```text
rocomm
rocomm.*
```

保留给 RoComm 官方模块。

Application 推荐：

```text
app.*

robot.*

company.*
```

例如：

```text
robot.control

robot.perception

app.task
```

---

## 2.32 Category grammar

Category 语法：

```text
segment(.segment)*
```

segment 允许：

```text
ASCII letter
digit
_
-
```

禁止：

```text
空字符串

前导 .

尾随 .

连续 ..
```

Category：

> **case-sensitive。**

官方 RoComm category 必须 lowercase。

Application 也强烈推荐 lowercase，以避免：

```text
robot.Control
robot.control
```

被误认为同一 category。

---

## 2.33 GetLogger()

正式冻结：

```cpp
Result<Logger>
GetLogger(
    std::string_view category);
```

成功：

```text
返回绑定该 CategoryEntry 的 Logger
```

失败：

```text
invalid category
→ InvalidArgument

category allocation failure
→ ResourceExhausted

LoggingContext stopping/stopped
→ RuntimeStopped
```

如果：

```text
LoggingConfig.enabled == false
```

但 Context 本身仍处于 Running：

```text
GetLogger()
→ success

Logger::Enabled()
→ false
```

这样 Core / Middleware 不需要因为关闭 Logging 改变初始化流程。

---

## 2.34 Hierarchical filtering

规则：

> **longest matching dot-segment prefix wins。**

配置：

```text
default:
Info

rocomm:
Warn

rocomm.fastdds:
Debug

rocomm.fastdds.discovery:
Trace
```

则：

```text
rocomm.fastdds.discovery.participant
→ Trace

rocomm.fastdds.writer
→ Debug

rocomm.service
→ Warn
```

---

## 2.35 普通字符串 prefix 不成立

以下：

```text
rule:
rocomm.fast

category:
rocomm.fastdds
```

不得匹配。

语义应等价于：

```text
rocomm.fastdds.discovery
        ↓
rocomm.fastdds
        ↓
rocomm
        ↓
default
```

而不是：

```text
starts_with("rocomm.fast")
```

---

## 2.36 CategoryLevels

内部：

```cpp
class CategoryLevels {
public:
    void SetDefault(
        LogLevel level);

    void Set(
        std::string category,
        LogLevel level);

    void Erase(
        std::string_view category);

    LogLevel Resolve(
        std::string_view category) const;
};
```

文件：

```text
src/logging/category_levels.hpp
```

namespace：

```text
rocomm::logging
```

---

## 2.37 CategoryLevels thread-safety

正式冻结：

> **CategoryLevels 自身不保证 thread-safe。**

所有同步责任属于：

```text
LoggingContext
```

因此：

```text
CategoryLevels
```

可以保持简单数据结构。

禁止为了让内部辅助类“自己线程安全”而重复构造第二套 locking layer。

---

## 2.38 Dynamic category update

LoggingContext 提供：

```cpp
Status SetDefaultLevel(
    LogLevel level) noexcept;

Status SetCategoryLevel(
    std::string_view category,
    LogLevel level) noexcept;

Status ClearCategoryLevel(
    std::string_view category) noexcept;
```

---

## 2.39 Dynamic update 错误语义

非法 category：

```text
InvalidArgument
```

allocation failure：

```text
ResourceExhausted
```

Context stopped：

```text
RuntimeStopped
```

---

## 2.40 Dynamic update strong semantic guarantee

例如：

```cpp
SetCategoryLevel(
    "rocomm.fastdds",
    LogLevel::Debug);
```

必须满足：

```text
成功
→ 新配置完整 commit

失败
→ 原 category configuration 保持不变
```

实现应先准备：

```text
new key/value
```

成功以后再 commit。

不能出现：

```text
CategoryLevels 已改变
但部分 CategoryEntry 未更新
同时 API 返回 failure
```

的半更新状态。

---

## 2.41 Dynamic update 复杂度

V1 允许：

```text
O(number_of_existing_categories)
```

更新已有 Logger 的 effective level。

这是：

```text
control-plane operation
```

不是 hot path。

推荐维护：

```text
CategoryEntry registry
```

更新规则以后遍历已有 CategoryEntry。

不为这一控制操作引入：

```text
复杂 prefix tree

RCU

generation propagation system
```

如果未来确实需要优化，可改成：

```text
generation + lazy recomputation
```

而不修改 Public API。

---

## 2.42 两层 Filtering

Filtering 分为：

```text
Layer 1:
LoggingContext global category/level filtering

Layer 2:
Backend / output-specific filtering
```

Layer 1：

```text
发生在 message construction 之前
```

是真正 hot-path filter。

Layer 2：

```text
发生在 LogRecord 已创建之后
```

用于：

```text
console Info+

file Debug+
```

Backend：

> **不得重新启用 LoggingContext 已丢弃的 record。**

---

## 2.43 LoggingMode

```cpp
enum class LoggingMode :
    std::uint8_t {
    Synchronous,
    Asynchronous,
};
```

---

## 2.44 LogOverflowPolicy

```cpp
enum class LogOverflowPolicy :
    std::uint8_t {
    DropNewest,
    DropOldest,
    Block,
};
```

语义：

```text
DropNewest
→ queue 满时丢当前新记录

DropOldest
→ queue 满时丢最旧 queued record，
  当前新记录入队

Block
→ caller 等待 capacity
```

---

## 2.45 LoggingConfig

概念：

```cpp
class LoggingConfig {
public:
    LoggingConfig&
    SetEnabled(
        bool enabled) noexcept;

    LoggingConfig&
    SetDefaultLevel(
        LogLevel level) noexcept;

    LoggingConfig&
    SetCategoryLevel(
        std::string category,
        LogLevel level);

    LoggingConfig&
    SetMode(
        LoggingMode mode) noexcept;

    LoggingConfig&
    SetQueueCapacity(
        std::size_t capacity) noexcept;

    LoggingConfig&
    SetOverflowPolicy(
        LogOverflowPolicy policy) noexcept;

    LoggingConfig&
    SetFlushOnLevel(
        LogLevel level) noexcept;

    LoggingConfig&
    SetBackendFactory(
        LoggingBackendFactory factory);
};
```

---

## 2.46 LoggingConfig setter 异常语义

正式冻结：

> **LoggingConfig 是普通 configuration builder。**

涉及 allocation 的 setter：

```text
SetCategoryLevel

SetBackendFactory
```

允许：

```text
std::bad_alloc
```

等配置期 C++ exception。

不把所有 config builder 改造成：

```text
Result-returning setter
```

这与 RoComm `RuntimeConfig` / Options 的 builder 风格一致。

---

## 2.47 Queue capacity validation

`SetQueueCapacity()`：

```text
只存储配置
```

真正 validation 在：

```text
LoggingContext::Create()
```

Async mode 下必须：

```text
capacity >= 1
```

否则：

```text
InvalidArgument
```

Sync mode 中 queue capacity 不参与运行，但仍建议进行统一合法性检查，避免一个 Config 在 mode 切换后变成潜在非法状态。

---

## 2.48 默认配置

V1 推荐默认：

```text
enabled:
true

default level:
Info

mode:
Asynchronous

queue capacity:
8192

overflow:
DropNewest

flush-on-level:
Error

category override:
none

backend:
build default
```

---

## 2.49 默认 Async 的理由

RoComm 第一定位是：

```text
communication runtime
```

默认不应让：

```text
console I/O

file I/O

rotation

Backend Flush
```

阻塞通信线程。

因此默认：

```text
Asynchronous
```

---

## 2.50 默认 DropNewest 的理由

默认目标：

```text
bounded memory

+

不阻塞通信线程
```

因此不能默认：

```text
Block
```

选择：

```text
DropNewest
```

会优先保存：

```text
已经进入 queue 的历史
```

需要优先看到最新日志的应用可以显式选择：

```text
DropOldest
```

---

## 2.51 Flush-on-level

如果：

```text
record.level >= flush_on_level
```

Backend 在该 record 处理以后：

```text
Flush
```

Async：

```text
writer/control side 执行
```

Sync：

```text
control path 按本文第 3 章的 timeout-safe 规则执行
```

`Off`：

```text
关闭 automatic flush-on-level
```

---

## 2.52 默认 Error flush 的权衡

默认：

```text
flush_on_level = Error
```

偏向：

```text
故障诊断
crash 前尽快落盘
```

而不是 maximum throughput。

如果应用可能高频产生 Error：

```text
Critical
```

或者：

```text
Off
```

可能更适合。

---

## 2.53 Text Backend 配置

SpdlogBackend 和未来 NativeBackend 都需要：

```text
console

file

rotation

text formatting

output-specific level
```

这些是 Backend-neutral text logging semantics。

因此 V1 定义：

```text
TextFormatConfig
TextBackendConfig
```

---

## 2.54 TextFormatConfig

```cpp
enum class LogTimeZone :
    std::uint8_t {
    Local,
    UTC,
};

struct TextFormatConfig {
    LogTimeZone timezone =
        LogTimeZone::Local;

    bool include_timestamp = true;
    bool include_level = true;
    bool include_category = true;
    bool include_thread_id = true;
    bool include_source_location = true;
    bool include_function = false;
};
```

V1 不提供：

```text
custom pattern language
```

避免重新实现一套复杂 pattern parser。

---

## 2.55 ConsoleOutputConfig

```cpp
enum class ConsoleStream :
    std::uint8_t {
    Stdout,
    Stderr,
};

enum class ColorMode :
    std::uint8_t {
    Auto,
    Always,
    Never,
};

struct ConsoleOutputConfig {
    ConsoleStream stream =
        ConsoleStream::Stderr;

    ColorMode color =
        ColorMode::Auto;

    LogLevel min_level =
        LogLevel::Trace;
};
```

---

## 2.56 FileOutputConfig

```cpp
struct FileOutputConfig {
    std::string path;

    bool truncate = false;

    LogLevel min_level =
        LogLevel::Trace;
};
```

非法：

```text
path empty
```

Backend factory：

```text
InvalidArgument
```

---

## 2.57 RotatingFileOutputConfig

```cpp
struct RotatingFileOutputConfig {
    std::string path;

    std::uint64_t
        max_file_size_bytes =
            64 * 1024 * 1024;

    std::size_t
        max_archived_files = 4;

    LogLevel min_level =
        LogLevel::Trace;
};
```

`max_archived_files`：

```text
不包括 active file
```

---

## 2.58 TextOutputConfig

```cpp
using TextOutputConfig =
    std::variant<
        ConsoleOutputConfig,
        FileOutputConfig,
        RotatingFileOutputConfig>;
```

---

## 2.59 TextBackendConfig

```cpp
struct TextBackendConfig {
    TextFormatConfig format;

    std::vector<TextOutputConfig>
        outputs;
};
```

---

## 2.60 空 outputs 的语义

正式冻结：

```text
outputs.empty()
→ InvalidArgument
```

不把它解释为：

```text
隐式 NullBackend
```

原因：

```text
空 outputs 更可能意味着用户配置遗漏
```

如果用户希望完全关闭 Logging：

```cpp
config.SetEnabled(false);
```

如果未来需要显式 discard Backend：

```text
应定义明确 NullBackend
```

而不是利用空 vector 隐式表达。

---

# 3. LoggingContext、异步调度、并发与生命周期

## 3.1 LoggingContext

LoggingContext 是 Logging Runtime 的 owning facade。

概念：

```cpp
class LoggingContext {
public:
    LoggingContext(
        const LoggingContext&) = delete;

    LoggingContext&
    operator=(
        const LoggingContext&) = delete;

    LoggingContext(
        LoggingContext&&) noexcept;

    LoggingContext&
    operator=(
        LoggingContext&&) noexcept;

    static Result<LoggingContext>
    Create(
        LoggingConfig config);

    Result<Logger>
    GetLogger(
        std::string_view category);

    Status SetDefaultLevel(
        LogLevel level) noexcept;

    Status SetCategoryLevel(
        std::string_view category,
        LogLevel level) noexcept;

    Status ClearCategoryLevel(
        std::string_view category) noexcept;

    Status Flush(
        Duration timeout) noexcept;

    Status Shutdown(
        Duration timeout) noexcept;

    LoggingStats
    GetStats() const noexcept;

    ~LoggingContext() noexcept;

private:
    struct State;
    std::shared_ptr<State> state_;
};
```

---

## 3.2 Lifecycle 与 quiescence 分离

Lifecycle：

```text
Created
   ↓
Initialized
   ↓
Running
   ↓
Stopping
   ↓
Stopped
```

另外独立存在：

```text
shutdown completion / quiescence
```

因此允许：

```text
logical state = Stopped

但

Backend cleanup 尚未完成
```

这与 RoComm Runtime 的：

```text
logical Stopped
vs
quiescent
```

模型保持一致。

---

## 3.3 Create 顺序

```text
validate LoggingConfig
        ↓
prepare category policy
        ↓
如果 enabled=false
    跳过 Backend factory
        ↓
否则 invoke BackendFactory
        ↓
validate factory result
        ↓
Backend::Initialize()
        ↓
allocate bounded queue if Async
        ↓
prepare worker/control state
        ↓
start async writer if Async
        ↓
publish Running
```

---

## 3.4 Create rollback

任何阶段失败都必须 rollback。

例如 Backend 已初始化，但 queue allocation 失败：

```text
Backend::Shutdown()
↓
destroy Backend
↓
return failure
```

writer 已经成功启动，但后续初始化失败：

```text
signal worker stop
↓
join / complete worker shutdown
↓
Backend::Shutdown()
↓
destroy backing
↓
return failure
```

不得：

```text
泄漏 worker
泄漏 Backend
返回半初始化 Context
```

---

## 3.5 BackendFactory

公共：

```cpp
using LoggingBackendFactory =
    std::function<
        Result<
            std::unique_ptr<LoggingBackend>>()>;
```

LoggingConfig 保存：

```text
factory
```

不是：

```text
shared Backend instance
```

从而每个 LoggingContext 都拥有：

```text
独立 Backend
独立 state
独立 lifecycle
```

---

## 3.6 Factory failure Result

Factory：

```cpp
return Result<
    std::unique_ptr<LoggingBackend>>{
        error_status};
```

时：

> **原 Status 直接传播。**

LoggingContext 不重新映射一个已经明确表达失败原因的 Status。

---

## 3.7 Factory success + nullptr

以下是 Backend factory contract violation：

```text
Result success
+
unique_ptr == nullptr
```

LoggingContext：

```text
InternalError
```

Diagnostic 建议：

```text
Logging backend factory returned a null backend.
```

---

## 3.8 BackendFactory exception boundary

Factory callable 可以抛异常。

`LoggingContext::Create()` 必须捕获。

```text
std::bad_alloc
→ ResourceExhausted

其他 std::exception
→ InternalError

unknown exception
→ InternalError
```

Diagnostic 可以保留：

```text
exception.what()
```

但不得把 exception 穿越 LoggingContext::Create() 边界。

Factory invocation 是：

```text
control-plane exception boundary
```

---

## 3.9 Logging disabled 时不调用 factory

如果：

```cpp
config.SetEnabled(false);
```

则：

```text
BackendFactory 不执行

Backend 不创建

AsyncQueue 不创建

writer 不启动
```

这样避免：

```text
无意义 side effect
无意义 file open
无意义 third-party initialization
```

所有 Logger：

```text
Enabled() == false
```

---

## 3.10 Default Backend

用户没有设置 BackendFactory 时：

```text
使用 build default
```

V1：

```text
ROCOMM_HAS_SPDLOG_BACKEND
→ SpdlogBackend
```

如果：

```text
logging enabled
+
build 无默认 Backend
```

则：

```text
UnsupportedConfiguration
```

不得 silent fallback。

---

## 3.11 不暴露 BackendKind

V1 不定义：

```cpp
enum class BackendKind {
    Auto,
    Spdlog,
    Native,
};
```

原因：

```text
不为未来 Feature 预埋枚举

不让 Auto priority 变成稳定 API

BackendFactory 已足够表达显式选择
```

---

## 3.12 AsyncQueue

Async mode 使用：

> **bounded MPSC / single-consumer queue。**

V1 推荐实现：

```text
preallocated ring buffer

+

mutex

+

condition_variable
```

不在 V1 一开始实现 lock-free queue。

原因：

```text
语义清晰

overflow 精确

Block 容易正确实现

Shutdown race 易验证

TSAN 友好
```

---

## 3.13 Queue allocation

Async Context 初始化时预分配：

```text
capacity
```

个 queue slot。

Running 后：

```text
queue container 不增长
不 resize
```

保证：

```text
queue storage bounded
```

不保证：

```text
Logger::Log zero allocation
```

---

## 3.14 EnqueueResult

内部：

```cpp
enum class EnqueueResult {
    Accepted,
    DroppedNewest,
    DroppedOldest,
    Stopped,
};
```

不暴露给 Application。

用于：

```text
stats
overflow fallback
shutdown race
```

---

## 3.15 DropNewest

queue full：

```text
当前新 LogRecord
→ 不入 queue

旧 queue
→ 不变

dropped_newest++
```

如果新 record：

```text
Error
Critical
```

则：

```text
RawLog minimal fallback
```

但仍计为：

```text
dropped_newest
```

不是正常 dispatch。

---

## 3.16 DropOldest

queue full：

```text
删除最旧 queued LogRecord

dropped_oldest++

当前 LogRecord 入队
```

如果被删除的旧 record：

```text
Error
Critical
```

则：

```text
RawLog minimal fallback
```

但仍然必须删除它。

禁止：

```text
因为 severity 高
改为寻找其他低 severity record 删除
```

原则：

> **Overflow policy 不按照 severity 重写。Severity 只影响 emergency fallback。**

---

## 3.17 Control event 不允许被 DropOldest 删除

如果内部使用：

```text
Flush barrier

Shutdown control event
```

这些控制信息不得参与普通 LogRecord overflow。

实现可以：

```text
control path 独立于 queue capacity
```

或者使用：

```text
不可丢弃 barrier representation
```

---

## 3.18 Block

queue full：

```text
producer 等待 capacity
```

直到：

```text
consumer pop

或

Shutdown 开始
```

Shutdown 时：

```text
notify all blocked producers
```

producer 返回内部：

```text
Stopped
```

不能形成：

```text
Shutdown 等 producer

producer 等 queue

writer 已停止
```

的死锁。

---

## 3.19 Async writer

每个 Async LoggingContext：

```text
一个 writer thread
```

V1 不提供：

```text
multiple logging workers
```

原因：

```text
保持 accepted order

Backend 不需要并发 Write

Flush 更简单

Shutdown 更简单
```

---

## 3.20 Backend invocation 串行化

LoggingContext 保证：

```text
Initialize
Write
Flush
Shutdown
```

不会并发进入同一个 Backend。

Async：

```text
writer/control path
```

串行。

Sync：

```text
caller thread
↓
backend invocation mutex
↓
Backend
```

串行。

因此：

> **Custom Backend 不要求 Write 本身 thread-safe。**

---

## 3.21 Sync mode

正常：

```text
Logger::Log
↓
caller thread
↓
backend invocation mutex
↓
Backend::Write
```

因此 Sync mode 允许：

```text
mutex contention
+
Backend I/O delay
```

传播给调用点。

V1 不提供：

```text
SingleThreadedSync
```

等特殊模式。

---

## 3.22 日志顺序

Async mode 的 Backend dispatch order：

```text
accepted queue linearization order
```

同一个 producer：

```text
A
B
C
```

正常保持：

```text
A
B
C
```

除非某条记录明确被 overflow policy 丢弃。

不同 producer：

```text
不保证 timestamp total order
```

不做跨线程：

```text
timestamp merge sort
```

---

## 3.23 BackendInvocationGuard

Backend invocation boundary 必须建立：

```text
thread_local recursion guard
```

逻辑：

```text
LoggingContext
↓
BackendInvocationGuard
↓
Backend callback
```

guard 设置发生在：

> **实际执行 Backend callback 的线程。**

Async：

```text
writer/control thread
```

Sync：

```text
caller/control thread
```

---

## 3.24 Guard 覆盖全部 Backend callback

以下都必须受 guard 保护：

```text
Backend::Initialize

Backend::Write

Backend::Flush

Backend::Shutdown
```

因此 Backend 在任意 lifecycle callback 内错误地使用 RoComm Logger：

```text
都会进入 RawLog fallback
```

而不是重新进入 normal pipeline。

---

## 3.25 Flush 不是简单 accepted-counter 比较

V1 不把 Flush correctness 定义为：

```text
读取 accepted counter
然后等待 dispatched == accepted
```

因为：

```text
DropOldest

in-flight Write

concurrent producers
```

会让简单计数不足以表达 prefix completion。

---

## 3.26 Flush barrier 语义

```cpp
Status Flush(
    Duration timeout) noexcept;
```

Flush 必须建立：

> **与 log admission 线性化的不可丢弃 barrier。**

概念：

```text
Producer A:
R1 ─ R2 ─────────────┐

Producer B:
────── R3 ───────────┼── [FLUSH BARRIER] ── later records
                     │
                     ▼
                   writer
```

barrier 之前已经成功 admission 的记录必须最终成为：

```text
Backend 已 dispatch

或者

已按明确 overflow policy terminalize
```

之后：

```text
Backend::Flush()
```

完成，Flush 才返回：

```text
Ok
```

---

## 3.27 Flush 线性化点

正式定义：

> **LoggingContext 通过与 log admission 相同的同步域，成功发布 Flush barrier 的时刻。**

因此：

```text
barrier 之前
→ 属于本次 Flush

barrier 之后
→ 不要求本次 Flush 等待
```

具体实现可以采用：

```text
non-droppable control barrier

或

monotonic sequence + completion frontier

或其他语义等价机制
```

不冻结内部算法。

---

## 3.28 Flush barrier 不受 overflow 影响

任何：

```text
DropNewest
DropOldest
Block
```

都不能：

```text
丢弃 Flush barrier
```

否则 Flush 将无法形成可靠 prefix barrier。

---

## 3.29 Flush timeout

如果 deadline 到达而 barrier 尚未完成：

```text
Flush()
→ Timeout
```

此时调用方不能假设：

```text
barrier 前全部记录已经 dispatch/flush
```

后台仍允许：

```text
继续推进该 Flush operation
```

但已经返回的：

```text
Timeout Status
```

不再被修改。

---

## 3.30 Concurrent Flush

多个线程同时 Flush：

```text
每个调用拥有自己的 barrier semantic
```

实现允许：

```text
合并兼容 barrier
```

但只有在不改变：

```text
各自 prefix guarantee
```

的情况下才能合并。

不要求 V1 暴露 Flush ID。

---

## 3.31 Flush during Stopping

如果：

```text
Shutdown 已开始
```

则新的 Flush 不再插入普通运行期 barrier。

它应：

```text
join 当前 shutdown completion
```

在 timeout 内如果 shutdown 达到：

```text
Backend Flush / Shutdown completion
```

返回：

```text
Ok
```

否则：

```text
Timeout
```

---

## 3.32 为什么 Backend::Flush() 不接收 timeout

Backend SPI：

```cpp
Status Flush() noexcept;
```

自身不承担 deadline。

timeout 属于：

```text
LoggingContext control operation
```

这样：

```text
Custom Backend
```

无需实现第二套 timeout mechanism。

LoggingContext 负责：

```text
在独立可延迟存活的执行上下文中调用 Backend control operation
+
等待 completion 到用户 deadline
```

---

## 3.33 Sync mode 的 Flush timeout

这是 V1 必须明确解决的语义。

Sync normal log：

```text
caller thread
→ Backend::Write
```

但：

```text
Flush(timeout)
```

不能直接在调用 `Flush()` 的 caller thread 上执行可能无限阻塞的：

```text
Backend::Flush()
```

否则 timeout 无法兑现。

因此正式冻结：

> **Sync mode 只规定正常 `Write()` 在 caller thread 执行；Flush/Shutdown 等带 timeout 的 control operation 可以在 self-owned control execution context 中执行。**

---

## 3.34 Sync control execution

Sync Flush：

```text
caller
  ↓
publish Flush barrier / close prefix
  ↓
self-owned control task/thread
  ↓
acquire backend invocation serialization
  ↓
Backend::Flush()
  ↓
completion
```

caller：

```text
wait_until(deadline)
```

超时：

```text
return Timeout
```

control operation 继续安全执行。

---

## 3.35 Sync control execution implementation

可以使用：

```text
临时 self-owned control thread

共享 Logging control worker

其他 self-owned execution mechanism
```

但：

```text
不能依赖用户 Executor

不能依赖 process-global thread pool

不能让 caller 因 Backend 永久阻塞而永远无法返回 timeout
```

具体 mechanism 不冻结。

---

## 3.36 Lifecycle Shutdown

```cpp
Status Shutdown(
    Duration timeout) noexcept;
```

正常 shutdown：

```text
1. Running → Stopping linearization

2. reject new normal admission

3. wake Block producers

4. establish shutdown prefix

5. drain/terminalize accepted records

6. Backend::Flush()

7. Backend::Shutdown()

8. worker/control path exits

9. logical lifecycle → Stopped

10. shutdown completion → quiescent
```

---

## 3.37 Shutdown admission 线性化

真正的 shutdown admission cut 是：

```text
Running → Stopping
```

atomic transition。

如果 Log admission 先赢：

```text
record accepted
```

Shutdown 必须对它负责。

如果 Shutdown 先赢：

```text
new normal log
→ rejected
```

---

## 3.38 CategoryEntry Off 只是优化

进入 Stopping 后可以：

```text
遍历 CategoryEntry
→ effective_level = Off
```

使已有 Logger 更快返回。

但即使 O(n) 更新尚未完成：

```text
lifecycle == Stopping
```

已经足以禁止 admission。

---

## 3.39 Shutdown 与 Log 并发

Log 先赢：

```text
record accepted

→ 必须 dispatch
或
按已经冻结的 overflow semantic terminalize
```

Shutdown 先赢：

```text
direct Log attempt
→ rejected_after_stop++

LogLazy / macro
通常会因 Enabled=false
根本不进入 builder
```

---

## 3.40 Shutdown completion

除 lifecycle 外，内部必须维护：

```text
shutdown completion state
```

例如：

```text
Pending

Complete
```

或者等价 representation。

原因是：

```text
logical Stopped
```

并不总等于：

```text
Backend 已 quiescent
```

---

## 3.41 Shutdown timeout

如果 deadline 到达时：

```text
Backend::Write
Backend::Flush
Backend::Shutdown
control worker
```

仍未完成：

```text
logical lifecycle
→ Stopped

shutdown completion
→ Pending

Shutdown()
→ Timeout
```

---

## 3.42 Repeated Shutdown

如果已经：

```text
Stopped
+
shutdown completion == Complete
```

再次：

```cpp
Shutdown(timeout);
```

返回：

```text
Ok
```

如果：

```text
Stopped
+
shutdown completion == Pending
```

则新的 Shutdown：

> **join 同一个 shutdown completion。**

如果 cleanup 在新 timeout 内完成：

```text
Ok
```

如果仍未完成：

```text
Timeout
```

因此：

> **Repeated Shutdown joins shutdown completion，而不是只观察 logical lifecycle state。**

---

## 3.43 Concurrent Shutdown

多个线程同时 Shutdown：

```text
只有一个 shutdown initiator
```

其他 caller：

```text
join same completion
```

各 caller 使用自己的 timeout 等待。

可以出现：

```text
Caller A:
Timeout

Caller B:
稍后等待更久
→ Ok
```

这是合法的。

---

## 3.44 Shutdown timeout 后的 worker

超时以后：

```text
后台 worker/control task
继续完成清理
```

但：

```text
不修改已经返回给 caller 的 Status
```

它只继续更新：

```text
内部 shutdown completion
stats
```

---

## 3.45 Timeout 后 Backend 生命周期

如果 Shutdown timeout：

```text
LoggingContext wrapper
可以继续存在或销毁

logical lifecycle
= Stopped
```

但：

```text
Backend

queue backing

category storage

worker/control state
```

可能仍被 in-flight execution 持有。

因此：

> **LoggingContext wrapper 析构不等于 Backend 对象立即析构。**

---

## 3.46 Self-owned worker state

实现必须采用安全 lifetime 机制，例如：

```text
shared WorkerState

self-owned control state

deferred destruction
```

参考概念：

```text
LoggingContext wrapper
      ↓ strong
LoggingState

worker/control thread
      ↓ strong
same LoggingState

LoggingState
      ↓ owns
Backend
Category storage
Queue
```

wrapper 可以销毁，但 worker 仍保持必要 backing。

---

## 3.47 Custom Backend 永久阻塞

如果 Custom Backend：

```cpp
Write()
```

永久不返回：

```text
shutdown completion
可能永远 Pending
```

Backend state 可能保留到：

```text
进程退出
```

这是 C++ 无法安全强制解决的限制。

Backend contract 因此明确：

> **Initialize/Write/Flush/Shutdown 不得无限阻塞。**

违反这一条属于：

```text
Custom Backend contract violation
```

RoComm 仍必须保证：

```text
no UAF

no double-free

no std::thread destructor terminate
```

---

## 3.48 SpdlogBackend 的阻塞边界

SpdlogBackend 使用普通同步 sink。

它不引入：

```text
第三方 async worker join

全局 async pool wait
```

但：

```text
filesystem

terminal

OS write
```

本身仍可能异常长时间阻塞。

因此：

> **RoComm 不为普通文件/终端 I/O 提供严格最大阻塞时间保证。**

这也是 LoggingContext timeout contract 必须存在的原因。

---

## 3.49 LoggingContext destructor

```cpp
~LoggingContext() noexcept;
```

正式语义：

> **析构触发 safe shutdown initiation，但不阻塞等待可能永久阻塞的 Backend 完成。**

因此：

```text
析构
≠
完整 Flush guarantee
```

需要明确保证日志落盘时：

```cpp
Shutdown(timeout);
```

必须由调用方显式调用。

RoComm Runtime 自己必须在：

```text
Runtime::Stop()
```

中显式执行 Logging shutdown。

---

## 3.50 LoggingStats

```cpp
struct LoggingStats {
    std::uint64_t accepted = 0;

    std::uint64_t dispatched = 0;

    std::uint64_t dropped_newest = 0;

    std::uint64_t dropped_oldest = 0;

    std::uint64_t rejected_after_stop = 0;

    std::uint64_t frontend_failures = 0;

    std::uint64_t recursive_fallbacks = 0;

    std::uint64_t queue_high_watermark = 0;

    LoggingBackendStats backend;
};
```

---

## 3.51 `accepted`

表示：

```text
通过 global filter

成功 materialize LogRecord

成功进入 LoggingContext admission
```

DropNewest：

```text
新记录未 accepted
```

因此：

```text
dropped_newest
```

不包含在 `accepted` 中。

---

## 3.52 `dispatched`

表示：

```text
Backend::Write(record)
```

已经开始执行。

不意味着：

```text
文件一定写成功
```

实际 Backend failure 看：

```text
backend.write_errors
```

---

## 3.53 `dropped_oldest`

DropOldest 删除的是：

```text
之前已经 accepted
```

的记录。

因此稳定状态下：

```text
accepted
≈
dispatched
+
dropped_oldest
+
queued
+
in-flight
```

由于 stats 是 eventually consistent：

```text
不要求瞬时严格等式
```

---

## 3.54 `accepted - dispatched` 不能直接当 backlog

因为：

```text
dropped_oldest
```

已经从 queue 移除。

更合理的近似：

```text
accepted
-
dispatched
-
dropped_oldest
```

约等于：

```text
queued + in-flight
```

但由于不同 counter 并非单原子 snapshot：

> 仍然只适合作为诊断指标，而非精确同步条件。

---

## 3.55 queue_high_watermark

`queue_high_watermark` 是：

```text
eventually consistent diagnostic statistic
```

不要求每次 producer Push：

```text
atomic CAS max
```

---

## 3.56 High-watermark 采样策略

V1 推荐：

```text
producer/thread-local 轻量采样

或

writer 每处理 K 条记录时采样

或

每 N 次 push 批量发布
```

允许近似。

不能让精确 high-watermark 统计成为所有 producer 的显著 hot-path 负担。

---

## 3.57 GetStats()

```cpp
LoggingStats
GetStats() const noexcept;
```

返回：

```text
eventually consistent snapshot
```

不保证所有 counter 属于完全相同的时间点。

---

# 4. Backend SPI、SpdlogBackend、TextFormatter 与 RawLog

## 4.1 LoggingBackend 是唯一 Public Backend SPI

V1 不同时公开：

```text
Custom Backend SPI

+

Custom Sink SPI
```

唯一正式扩展点：

```text
LoggingBackend
```

Backend 内部可以自行建立：

```text
Sink
Output
Channel
Router
```

这些不进入 RoComm Core Public API。

---

## 4.2 LoggingBackend

```cpp
struct LoggingBackendStats {
    std::uint64_t write_calls = 0;
    std::uint64_t write_errors = 0;
    std::uint64_t flush_errors = 0;
    std::uint64_t shutdown_errors = 0;
};

class LoggingBackend {
public:
    virtual ~LoggingBackend() = default;

    virtual std::string_view
    Name() const noexcept = 0;

    virtual Status
    Initialize() noexcept = 0;

    virtual void
    Write(
        const LogRecord& record) noexcept = 0;

    virtual Status
    Flush() noexcept = 0;

    virtual Status
    Shutdown() noexcept = 0;

    virtual LoggingBackendStats
    GetStats() const noexcept {
        return {};
    }
};
```

---

## 4.3 V1 不引入 LoggingBackendInitContext

V1 正式保持：

```cpp
Initialize() noexcept
```

无参数。

原因：

Backend creation-time dependency 可以由：

```text
LoggingBackendFactory
```

capture。

例如：

```cpp
config.SetBackendFactory(
    [backend_config,
     runtime_name]() mutable {
        return logging::spdlog::
            CreateBackend(
                BuildConfig(
                    backend_config,
                    runtime_name));
    });
```

在没有明确存在“必须由 LoggingContext 在 Initialize 时注入、且 factory 无法合理提供”的资源之前：

> **不提前引入新的 InitContext abstraction。**

---

## 4.4 Backend 生命周期

```text
Constructed
    ↓
Initialize()
    ↓
Initialized
    ↓
Write / Flush
    ↓
Shutdown()
    ↓
Stopped
```

`Initialize()`：

```text
最多成功一次
```

---

## 4.5 Initialize failure

Initialize 失败后：

```text
Backend 仍必须可安全析构
```

LoggingContext 可以：

```text
best-effort Backend::Shutdown()
```

Backend::Shutdown 必须能够：

```text
清理已经部分初始化的资源
```

不能假设：

```text
Initialize 一定全部成功
```

---

## 4.6 Backend API 异常规则

以下均：

```text
noexcept
```

```cpp
Initialize()
Write()
Flush()
Shutdown()
GetStats()
```

Backend 内部所有异常必须转换或吞入其正式错误路径。

---

## 4.7 Write error

`Write()` 返回：

```text
void
```

因为 logging data-plane output failure：

> 不允许反向修改原 communication Status。

发生输出错误：

```text
BackendStats::write_errors++
```

必要时：

```text
RawLog
```

---

## 4.8 Initialize / Flush / Shutdown error

这些是：

```text
Logging control-plane operation
```

因此返回：

```text
Status
```

推荐：

```text
invalid config
→ InvalidArgument

unsupported
→ UnsupportedConfiguration

resource failure
→ ResourceExhausted

internal/backend initialization failure
→ InternalError
```

---

## 4.9 Backend 不负责 timeout

Backend：

```cpp
Flush()
Shutdown()
```

不接收 deadline。

timeout 属于：

```text
LoggingContext
```

这样 Custom Backend 不必重复实现 timeout framework。

LoggingContext 通过：

```text
self-owned control execution
+
completion wait
```

提供 timeout。

---

## 4.10 Backend invocation serialization

LoggingContext 保证：

```text
同一个 Backend instance
```

的：

```text
Initialize
Write
Flush
Shutdown
```

不会并发。

因此 Backend 不要求：

```text
Write thread-safe
```

---

## 4.11 GetStats concurrency

`GetStats()`：

```text
允许 LoggingContext 运行期间查询
```

因此 Backend 必须：

```text
安全读取 stats
```

推荐：

```text
atomic counters
```

或者等价同步。

---

## 4.12 Backend 不由生产代码直接驱动

正常 Application 不应：

```cpp
backend->Write(...);
```

Backend lifecycle 应由：

```text
LoggingContext
```

驱动。

这是 API contract 和 code-review rule。

---

## 4.13 Custom Backend 单元测试

测试代码允许：

```text
直接构造 Backend

直接调用：
Initialize
Write
Flush
Shutdown
```

用于 Backend 自身 unit test。

但 Logging integration test：

> **必须通过 LoggingContext 驱动。**

---

## 4.14 Backend Contract Test Harness

RoComm repository 提供非安装测试辅助：

```text
tests/support/logging/backend_contract.hpp
```

例如：

```cpp
RunBackendContractTests(
    BackendFactory factory);
```

用于统一检查第三方或内置 Backend。

该测试 helper：

```text
不是 Public Runtime API
```

---

## 4.15 TextFormatter

private：

```cpp
class TextFormatter {
public:
    explicit TextFormatter(
        TextFormatConfig config);

    bool Format(
        const LogRecord& record,
        std::string& output) noexcept;
};
```

---

## 4.16 TextFormatter output contract

`Format()` 开始时：

```text
output.clear()
```

成功：

```text
return true

output
→ 一条完整日志行
```

失败：

```text
return false
```

此时：

```text
output 内容不得被 caller 当作合法日志使用
```

可以为空，也可以包含中间 partial data。

---

## 4.17 TextFormatter failure

例如：

```text
allocation failure

time conversion failure

unexpected rendering failure
```

导致：

```text
Backend formatting/error stats++
RawLog minimal fallback
```

不抛异常。

---

## 4.18 Default text format

推荐：

```text
2026-10-26T14:35:20.123456+09:00 [INFO] [rocomm.fastdds.discovery] [tid=18342] Participant matched [discovery.cpp:217]
```

V1 不冻结具体空格数量等微观格式作为兼容 ABI。

冻结的是：

```text
TextFormatConfig 中启用的字段
必须按配置存在
```

---

## 4.19 Output-specific filtering

每个 output 可以设置：

```text
min_level
```

例如：

```text
console:
Info+

file:
Debug+
```

属于：

```text
第二层 filtering
```

不能重新产生：

```text
LoggingContext 已经在 global filter 阶段丢弃的 Debug
```

---

## 4.20 SpdlogBackend

V1 production Backend：

```text
SpdlogBackend
```

但：

> **spdlog 只是实现细节。**

Public Core headers 不出现：

```text
spdlog::*
fmt::*
```

---

## 4.21 SpdlogBackend Public Surface

不公开具体 Backend class。

只公开 factory：

```cpp
namespace rocomm::logging::spdlog {

Result<std::unique_ptr<LoggingBackend>>
CreateBackend(
    TextBackendConfig config);

}
```

头文件：

```text
include/rocomm/logging/backends/spdlog.hpp
```

该头文件自身也不能 include spdlog。

---

## 4.22 SpdlogBackend 配置 validation

至少验证：

```text
outputs 非空

File path 非空

rotation size > 0

rotation count 合法

所有 LogLevel 合法
```

失败：

```text
InvalidArgument
```

---

## 4.23 SpdlogBackend 内部约束

正式禁止：

```text
spdlog::async_logger

spdlog global async thread pool

spdlog global logger registry

spdlog global set_level

spdlog global shutdown

process-global mutable spdlog configuration
```

---

## 4.24 SpdlogBackend 使用同步 logger

内部使用：

```text
synchronous spdlog::logger
```

因为 RoComm 已经在 LoggingContext 层提供：

```text
async queue
writer
overflow
shutdown
```

---

## 4.25 Caller metadata

Async 时调用 spdlog 的线程是：

```text
RoComm writer/control thread
```

因此不能让 spdlog 自己生成：

```text
caller timestamp
caller thread ID
caller source
```

正确路径：

```text
caller
↓
LogRecord captures metadata
↓
TextFormatter
↓
final line
↓
spdlog output
```

---

## 4.26 spdlog pattern

内部建议：

```text
%v
```

有 color 时可以：

```text
%^%v%$
```

只负责：

```text
输出 RoComm 已渲染 line
```

---

## 4.27 Spdlog error handler

必须安装 backend-local error handling。

发生：

```text
spdlog output failure
```

时：

```text
write_errors++
```

必要时：

```text
throttled RawLog
```

禁止：

```text
通过普通 RoComm Logger 报告
```

否则可能形成 recursion。

---

## 4.28 Spdlog global API CI 检查

Linux CI 对：

```text
rocomm_logging_spdlog
```

执行：

```text
nm
objdump
或等价符号扫描
```

禁止引用明确列出的 global APIs，例如：

```text
spdlog::shutdown

spdlog::register_logger

spdlog::set_level
```

以及已列入禁止表的 global async 初始化符号。

Windows / macOS：

```text
可以使用平台等价工具
```

如果 CI 暂无等价能力：

```text
允许暂时只在 Linux 执行符号扫描
```

但 runtime multi-context test 仍必须在支持平台运行。

---

## 4.29 RawLog 定位

RawLog 用于：

```text
Logging 初始化失败

Backend recursion

Backend severe failure

frontend allocation failure

late teardown

user callable exception terminate path

internal invariant fail-fast
```

不是 Application 普通 logging API。

---

## 4.30 RawLog 约束

尽可能满足：

```text
no heap allocation

no LoggingContext

no LoggingBackend

no AsyncQueue

no spdlog

no normal logging mutex

fixed-size stack buffer

oversized message truncation

low-level stderr write
```

---

## 4.31 Linux RawLog

推荐：

```text
fixed stack buffer
↓
bounded formatting
↓
write(STDERR_FILENO, ...)
```

---

## 4.32 EINTR

POSIX `write()`：

```text
如果返回 EINTR
→ retry
```

直到：

```text
成功写入

发生非 EINTR error

或完成允许的 bounded retry path
```

RawLog 不因 EINTR 直接静默失败。

---

## 4.33 RawLog 的 portable boundary

不同平台允许使用不同：

```text
low-level stderr writer
```

V1 不宣称：

```text
完全 async-signal-safe
```

只保证：

> **比正常 Logging pipeline 更低层，并可在正常 pipeline 不可用时 best-effort 输出。**

---

## 4.34 Recursion behavior

Backend invocation thread：

```text
BackendInvocationGuard active
```

如果 Backend 内再次调用 Logger：

```text
跳过 normal LoggingContext

RawLog

recursive_fallbacks++
```

不得：

```text
重新 enqueue

重新调用 Backend

重新 Block
```

尤其避免：

```text
writer
→ Backend
→ Logger
→ Block queue
→ writer 等自己
```

---

## 4.35 Frontend allocation failure

Logger message materialization 可能：

```text
std::bad_alloc
```

Logger API：

```text
noexcept
```

所以：

```text
frontend_failures++
```

Error / Critical：

```text
RawLog fallback
```

Trace / Debug / Info / Warn：

```text
safe drop
```

不能把 allocation exception 传播给通信 Runtime。

---

# 5. Runtime 集成、工程结构与 V1/V2 演进

## 5.1 Runtime ownership

```text
RuntimeState
    ↓ owns
LoggingContext
    ↓ owns
LoggingState
    ├── lifecycle/admission state
    ├── CategoryLevels
    ├── CategoryEntry registry
    ├── AsyncQueue
    ├── worker/control state
    └── LoggingBackend
```

Middleware 不获得：

```text
LoggingContext*
```

只获得：

```text
Logger
```

---

## 5.2 Middleware integration

Middleware 初始化时：

```cpp
auto logger_result =
    context.GetLogger(
        "rocomm.fastdds");
```

或者 MiddlewareInitContext 已经提供对应 Logger。

如果：

```text
GetLogger
→ failure
```

例如官方 category 配置错误或 allocation failure：

```text
Middleware initialization
→ failure
```

不能默默得到一个无效 Logger。

---

## 5.3 Middleware 权限边界

Middleware 不能：

```text
切换 Backend

修改 Logging queue

关闭 LoggingContext

直接管理 writer

调用 Runtime-wide Shutdown
```

Middleware 只能：

```text
使用自己的 Logger
```

---

## 5.4 Runtime Create 顺序

推荐：

```text
Runtime::Create()
      ↓
validate RuntimeConfig
      ↓
LoggingContext::Create()
      ↓
Executor initialization
      ↓
Middleware Initialize
      ↓
Route / Type validation
      ↓
Middleware Start
      ↓
Runtime Running
```

这样：

```text
Executor 初始化
Middleware 初始化
Route/type 初始化
```

都可以使用正常 Logging。

---

## 5.5 Logging 初始化失败

如果用户显式配置：

```text
Custom Backend

SpdlogBackend file output

其他 Backend 参数
```

而初始化失败：

```text
Runtime::Create()
→ failure
```

不得：

```text
silent fallback to console

silent fallback to Noop
```

---

## 5.6 Runtime Stop 中 Logging 的位置

正常 Runtime shutdown：

```text
1. reject new communication operation

2. stop new receive dispatch

3. terminalize pending operation

4. stop/drain user execution

5. shutdown Middleware

6. complete Executor shutdown

7. LoggingContext::Shutdown(
       remaining deadline)

8. Runtime logical Stopped
```

---

## 5.7 Middleware/Executor shutdown 日志

正式冻结：

> **Middleware shutdown 和 Executor shutdown 期间 LoggingContext 仍处于正常可用状态。**

因此：

```text
Middleware shutdown error

Executor shutdown warning

resource cleanup diagnostic
```

仍进入正常 Logging pipeline。

只有这些阶段结束后才关闭 Logging。

---

## 5.8 Shutdown 期间大量日志

如果 Middleware shutdown 产生大量日志：

```text
AsyncQueue 仍按正常 overflow policy
```

处理。

不会为了 shutdown：

```text
临时切换 Block
临时改 queue capacity
```

因此仍可能：

```text
DropNewest
DropOldest
```

对应 stats 和 RawLog fallback。

---

## 5.9 Runtime timeout 下 late log

如果 Runtime shutdown timeout 时仍有：

```text
in-flight user callback
```

而 LoggingContext 已经：

```text
Stopping / Stopped
```

旧 Logger：

```text
Enabled()
→ false

Log()
→ safe no-op
```

这些 timeout 后 late logs：

```text
不保证持久化
```

Fatal/invariant 仍可：

```text
RawLog
```

---

## 5.10 Runtime Stop 与 Logging timeout

如果 Logging shutdown 未在 Runtime 剩余 deadline 内完成：

```text
Runtime::Stop()
→ Timeout
```

Diagnostic 可以包含：

```text
logging shutdown did not reach quiescence
```

Runtime：

```text
logical Stopped
```

Logging Backend backing：

```text
可以 deferred destruction
```

---

## 5.11 User callable exception

如果：

```text
Subscriber callback

Server Handler

ResponseHandler

Future continuation
```

抛异常：

```text
normal Logger 可用
→ best-effort Critical

否则
→ RawLog

最终
→ std::terminate()
```

Logging failure 不得阻止 terminate。

---

## 5.12 Public files

建议：

```text
include/rocomm/logging/
├── log_level.hpp
├── source_location.hpp
├── log_record.hpp
├── logger.hpp
├── logging_config.hpp
├── logging_context.hpp
├── logging_backend.hpp
├── logging_stats.hpp
├── macros.hpp
│
├── text/
│   ├── text_format_config.hpp
│   └── text_backend_config.hpp
│
└── backends/
    └── spdlog.hpp
```

---

## 5.13 Public header dependency

上述 Public Core headers：

```text
不得 include spdlog

不得 include fmt

不得 include Quill

不得 include Boost.Log
```

`backends/spdlog.hpp` 也只声明：

```text
RoComm-owned factory
```

不得暴露第三方类型。

---

## 5.14 Private source layout

```text
src/logging/
├── logging_context.cpp
├── logging_state.hpp
│
├── category_levels.hpp
├── category_levels.cpp
│
├── async_queue.hpp
├── async_queue.cpp
│
├── control_operation.hpp
├── control_operation.cpp
│
├── backend_invocation_guard.hpp
│
├── text_formatter.hpp
├── text_formatter.cpp
│
├── raw_log.hpp
├── raw_log.cpp
│
├── backend_factory.hpp
├── backend_factory.cpp
│
└── spdlog/
    ├── spdlog_backend.hpp
    └── spdlog_backend.cpp
```

不创建：

```text
src/logging/detail/

src/logging/internal/

src/logging/impl/
```

---

## 5.15 CMake targets

建议：

```text
rocomm::logging

rocomm::logging_spdlog
```

`rocomm::logging`：

```text
无第三方 logging dependency
```

包含：

```text
Logger

LoggingContext

Category

AsyncQueue

Backend SPI

RawLog

TextFormatter
```

`rocomm::logging_spdlog`：

```text
depends:
rocomm::logging
spdlog
```

---

## 5.16 CMake options

V1：

```text
ROCOMM_BUILD_LOGGING

ROCOMM_BUILD_LOGGING_SPDLOG

ROCOMM_LOGGING_COMPILED_LEVEL
```

Feature macro：

```text
ROCOMM_HAS_SPDLOG_BACKEND
```

V1 不定义：

```text
ROCOMM_HAS_NATIVE_BACKEND
```

直到 NativeBackend 真正存在。

---

## 5.17 Default Backend 是 build policy

V1 build default：

```text
SpdlogBackend
```

V2 可以改成：

```text
NativeBackend
```

但：

> **默认 Backend 具体选择不是稳定 Public API contract。**

需要确定 Backend 的用户必须：

```cpp
SetBackendFactory(...)
```

---

## 5.18 V2 NativeBackend

V2 新增：

```text
include/rocomm/logging/backends/native.hpp

src/logging/native/
├── native_backend.hpp
├── native_backend.cpp
├── console_output.cpp
├── file_output.cpp
└── rotating_file_output.cpp
```

---

## 5.19 NativeBackend 复用边界

V2 必须复用：

```text
LoggingContext

AsyncQueue

CategoryLevels

Flush

Shutdown

RawLog

TextFormatter

TextBackendConfig

Backend SPI

Contract tests
```

不能建立：

```text
NativeAsyncQueue
NativeLogger
NativeLoggingContext
```

等第二套 runtime。

---

## 5.20 V2 与 TextBackendConfig

V1 正式冻结以后：

```text
TextFormatConfig

TextBackendConfig

ConsoleOutputConfig

FileOutputConfig

RotatingFileOutputConfig
```

不能为了 NativeBackend 实现方便随意改变语义。

如果 NativeBackend 需要：

```text
fsync mode

platform file permission

native console flags

特殊 rotation strategy
```

应定义：

```cpp
struct NativeBackendConfig {
    TextBackendConfig text;

    // Native-specific settings
};
```

原则：

> **NativeBackend 适配 V1 通用 Text contract，而不是反向修改 V1 contract。**

---

## 5.21 V2 不改变的 V1 contract

V2 不改变：

```text
LogLevel

SourceLocation

LogRecord

Logger

LogLazy

Macros

Category

LoggingConfig core semantics

LoggingContext

LoggingMode

LogOverflowPolicy

LoggingBackend

LoggingBackendFactory

LoggingStats

TextFormatConfig

TextBackendConfig

Runtime-local ownership

Flush

Shutdown

Timeout

RawLog
```

---

## 5.22 Quill 路线边界

Quill 的核心低延迟路线是：

```text
caller
→ encode arguments
→ per-thread queue
→ backend
→ delayed formatting
```

而 RoComm V1：

```text
caller-side message construction

+

backend-side record rendering
```

二者不是同一 frontend contract。

因此 V1 不加入：

```text
argument encoding

format codec

per-thread frontend queue

deferred format metadata
```

如果未来 benchmark 证明：

```text
caller-side message construction
```

成为明显瓶颈，再单独设计：

```text
EncodedLogRecord

DeferredFormattingFrontend
```

而不是现在提前承担复杂度。

---

## 5.23 V1 实施阶段

### L0 — Public Header Finalization

最终冻结：

```text
LogLevel

SourceLocation

LogRecord

Logger

LoggingConfig

LoggingContext

LoggingBackend

LoggingBackendFactory

LoggingStats

TextFormatConfig

TextBackendConfig

Macros

Duration 使用方式
```

---

### L1 — Backend-independent Core

实现：

```text
LoggingState

CategoryLevels

CategoryEntry registry

Logger

LogLazy

AsyncQueue

sync dispatch

async writer

control-operation execution

BackendInvocationGuard

Flush barriers

Shutdown completion

Stats

RawLog

TextFormatter
```

不接入 spdlog。

---

### L2 — Test Backends

实现：

```text
MemoryBackend

BlockingBackend

FailingBackend

RecursiveBackend

PartiallyInitializedBackend
```

先验证：

```text
LoggingContext contracts

Backend SPI contracts

timeout

deferred destruction

recursion
```

---

### L3 — SpdlogBackend

实现：

```text
Backend factory

console

file

rotation

per-output filtering

TextFormatter integration

spdlog error handler

global API isolation
```

---

### L4 — Runtime Integration

接入：

```text
RuntimeConfig::SetLogging()

Runtime::Create()

MiddlewareInitContext

Runtime::Stop()

user callable fail-fast
```

---

### L5 — Verification / Performance

执行：

```text
ASAN

UBSAN

TSAN

queue stress

shutdown race

multi-Runtime isolation

symbol scan

benchmark
```

---

# 6. 验证标准、性能契约与冻结边界

## 6.1 Public Header Dependency Test

独立 TU：

```cpp
#include <rocomm/logging/logger.hpp>
#include <rocomm/logging/logging_context.hpp>
#include <rocomm/logging/logging_backend.hpp>
```

在没有：

```text
spdlog

fmt
```

的环境必须成功：

```text
compile
```

这是 Backend independence 的硬验收项。

---

## 6.2 Default Logger tests

```text
Logger{}
→ Enabled false

Log()
→ safe no-op

LogLazy()
→ builder 不执行
```

---

## 6.3 Logger lazy evaluation tests

Enabled false：

```text
builder count == 0
```

Enabled true：

```text
builder count == 1
```

---

## 6.4 Logger copy tests

必须验证：

```text
copy O(1)

copy no heap allocation

move no heap allocation
```

可以使用：

```text
test allocator
operator new counter
```

验证。

---

## 6.5 Logger post-stop tests

已有 Logger：

```text
LoggingContext Shutdown
↓
Logger Enabled false
Logger Log safe no-op
```

不得：

```text
UAF
access destroyed CategoryEntry
```

---

## 6.6 Compile-time filtering tests

构建：

```text
ROCOMM_LOGGING_COMPILED_LEVEL
=
ROCOMM_LOG_LEVEL_INFO
```

验证：

```text
TRACE:
logger_expr 不求值
message_expr 不求值

DEBUG:
logger_expr 不求值
message_expr 不求值

INFO:
正常执行
```

---

## 6.7 Category grammar tests

覆盖：

```text
rocomm
rocomm.fastdds
robot.control
```

合法。

以下非法：

```text
""

".rocomm"

"rocomm."

"rocomm..fastdds"
```

---

## 6.8 Category case tests

验证：

```text
robot.Control

!=

robot.control
```

Category 是：

```text
case-sensitive
```

---

## 6.9 Hierarchy tests

覆盖：

```text
default

exact

parent

longest dot-prefix

普通 string prefix 不匹配
```

例如：

```text
rule rocomm.fast

不能匹配

rocomm.fastdds
```

---

## 6.10 GetLogger tests

覆盖：

```text
valid category
→ success

invalid category
→ InvalidArgument

allocation failure
→ ResourceExhausted

Context stopped
→ RuntimeStopped

Logging disabled but Context Running
→ success disabled Logger
```

---

## 6.11 Category storage lifetime tests

构造：

```text
queued LogRecord
+
Shutdown timeout
+
LoggingContext wrapper destruction
```

Backend 后续读取：

```text
record.category()
```

仍必须有效。

使用：

```text
ASAN
```

验证。

---

## 6.12 Dynamic category update tests

覆盖：

```text
SetDefaultLevel

SetCategoryLevel

ClearCategoryLevel

existing Logger update

new Logger uses latest level
```

---

## 6.13 Strong update guarantee tests

Fault injection：

```text
SetCategoryLevel
→ allocation failure
```

要求：

```text
ResourceExhausted

原配置保持不变

已有 Logger effective level 保持原值
```

---

## 6.14 Concurrent category update

```text
producer threads
+
SetCategoryLevel
```

验证：

```text
no data race

TSAN pass

已有 Logger 最终观察新 level
```

不要求：

```text
瞬间 global cutover
```

---

## 6.15 Logging disabled tests

```text
enabled=false
```

要求：

```text
BackendFactory 调用次数 == 0

no queue

no writer

GetLogger succeeds

Enabled false
```

---

## 6.16 Queue capacity validation

Async：

```text
capacity = 0
→ InvalidArgument
```

---

## 6.17 Empty outputs validation

```text
TextBackendConfig.outputs.empty()
```

创建 SpdlogBackend：

```text
InvalidArgument
```

不得隐式变成 NullBackend。

---

## 6.18 Backend factory tests

覆盖：

```text
factory success

factory failure Result

factory success + nullptr

throws bad_alloc

throws std::runtime_error

throws unknown
```

结果：

```text
failure Result
→ original Status propagated

success + nullptr
→ InternalError

bad_alloc
→ ResourceExhausted

other exception
→ InternalError
```

---

## 6.19 Create rollback tests

分别在：

```text
Backend Initialize

queue allocation

writer creation
```

注入失败。

验证：

```text
已创建资源 rollback

Backend partial Shutdown safe

无 leaked thread

无 leaked Backend

无 half-created Context
```

---

## 6.20 AsyncQueue tests

覆盖：

```text
empty

single producer

multiple producer

single consumer

wrap around

bounded capacity

DropNewest

DropOldest

Block

Stop wakes Block producer

writer wakeup
```

---

## 6.21 DropNewest severity fallback

当新：

```text
Error / Critical
```

因 queue full 被丢弃：

```text
dropped_newest++

RawLog fallback
```

---

## 6.22 DropOldest severity fallback

当最旧 queued record：

```text
Error / Critical
```

被 DropOldest 删除：

```text
dropped_oldest++

RawLog fallback
```

并仍必须：

```text
删除该 oldest
```

不能改变 overflow policy。

---

## 6.23 Block shutdown test

producer：

```text
queue full
→ blocked
```

Shutdown：

```text
wake producer
```

要求：

```text
producer exits wait

no deadlock
```

---

## 6.24 Sync serialization tests

多个 caller 同时：

```text
Log()
```

验证：

```text
Backend::Write never concurrent

backend mutex serialized

no data race
```

---

## 6.25 Sync blocking documentation/benchmark

使用 BlockingBackend：

```text
Write sleeps
```

确认：

```text
Sync Log caller 同步阻塞
```

这是预期行为。

---

## 6.26 Async ordering tests

单 producer：

```text
A
B
C
```

Backend 正常看到：

```text
A
B
C
```

除非对应 record 被 overflow 丢弃。

多 producer：

```text
只验证 queue linearization
```

不验证 timestamp total ordering。

---

## 6.27 Recursion tests

Backend：

```text
Initialize
Write
Flush
Shutdown
```

分别调用 Logger。

要求：

```text
Backend invocation thread guard active

normal pipeline 不递归

RawLog fallback

recursive_fallbacks++

no deadlock

no stack overflow
```

---

## 6.28 Nested recursive builder test

Backend：

```text
调用 ROCOMM_LOG_*
```

该 builder 内再次调用：

```text
ROCOMM_LOG_*
```

要求：

```text
RawFallbackGuard 阻止无限递归
```

---

## 6.29 Flush barrier tests

必须验证：

```text
barrier 前 accepted record

全部 dispatch 或明确 dropped

Backend::Flush 在 prefix terminalize 后执行
```

---

## 6.30 Flush + concurrent producer

producer 在 Flush barrier 前后的记录：

```text
R1
R2
[Flush barrier]
R3
R4
```

Flush 只必须等待：

```text
R1
R2
```

R3/R4 不属于该 barrier guarantee。

---

## 6.31 Flush + DropOldest

构造：

```text
barrier 前存在 record
随后部分 record 被 DropOldest
```

Flush 仍必须：

```text
确认这些 record 已明确 terminalized
```

不能因计数混乱永久等待。

---

## 6.32 Flush barrier 不可丢弃

在：

```text
DropNewest
DropOldest
```

高压下：

```text
Flush barrier
```

仍必须可靠完成或 timeout。

不能被 overflow 丢掉。

---

## 6.33 Flush timeout

BlockingBackend：

```text
Backend::Flush()
阻塞
```

调用：

```text
Flush(short_timeout)
```

要求：

```text
Timeout

caller 能按 deadline 返回

后台 control operation 可以继续
```

---

## 6.34 Sync Flush timeout

特别验证：

```text
LoggingMode::Synchronous
```

Backend::Flush 永久/长时间阻塞。

调用线程：

```text
不能永久卡死
```

必须：

```text
按 timeout 返回
```

证明 control operation 确实不直接在 caller 上执行阻塞 Backend control call。

---

## 6.35 Normal Shutdown tests

覆盖：

```text
Running
→ Stopping
→ Stopped
→ quiescent
```

结果：

```text
Ok
```

---

## 6.36 Shutdown admission cut test

构造并发：

```text
Log admission
vs
Shutdown transition
```

验证：

```text
Log wins
→ accepted and owned by shutdown

Shutdown wins
→ rejected
```

不能出现：

```text
已 accepted
但 shutdown 看不到
```

---

## 6.37 Shutdown CategoryEntry optimization test

即使：

```text
部分 CategoryEntry 尚未更新 Off
```

只要 lifecycle：

```text
Stopping
```

`Enabled()` 也必须：

```text
false
```

证明 CategoryEntry Off 不是 correctness mechanism。

---

## 6.38 Repeated Shutdown tests

情况一：

```text
第一次 Shutdown
→ Ok
```

第二次：

```text
Ok
```

情况二：

```text
第一次
→ Timeout

cleanup 后续完成

第二次等待
→ Ok
```

情况三：

```text
第一次
→ Timeout

cleanup 仍未完成

第二次短 timeout
→ Timeout
```

---

## 6.39 Concurrent Shutdown tests

多个 caller：

```text
同时 Shutdown
```

要求：

```text
一个 initiator

其他 join same completion

无 duplicate Backend::Shutdown
```

---

## 6.40 Shutdown timeout lifetime test

BlockingBackend：

```text
Write 阻塞
```

Shutdown：

```text
Timeout
```

随后：

```text
Context wrapper destruct
```

要求：

```text
Backend remains alive

Category storage remains alive

no UAF

writer eventually returns
→ deferred destruction complete
```

---

## 6.41 Permanent blocking test

模拟永不返回 Backend。

验证至少：

```text
caller Shutdown 能 Timeout

wrapper 能析构

no std::terminate from thread destruction

no UAF
```

测试进程可以使用受控方式结束，不等待永久 worker。

---

## 6.42 Backend Contract Tests

统一验证：

```text
Name non-empty

Initialize success

Initialize failure

partial initialization cleanup

Write noexcept

Write stats

write error isolation

Flush

Flush failure

Shutdown

Shutdown idempotence

Stats concurrent read

borrowed LogRecord lifetime
```

---

## 6.43 Custom Backend test harness

提供：

```text
tests/support/logging/backend_contract.hpp
```

不安装为正式 Runtime API。

---

## 6.44 TextFormatter tests

覆盖：

```text
timestamp on/off

level on/off

category on/off

thread ID on/off

source on/off

function on/off

Local

UTC
```

---

## 6.45 TextFormatter output clearing

输入：

```text
output = "old"
```

成功 Format 后：

```text
output
只包含新完整日志行
```

不能：

```text
append old content
```

---

## 6.46 SpdlogBackend tests

覆盖：

```text
sync spdlog logger

no async_logger

no global registry

no global set_level

no global shutdown

no global async pool

console

file

rotation

per-output threshold

caller timestamp retained

caller thread ID retained

caller source retained
```

---

## 6.47 Multi-SpdlogBackend isolation

创建：

```text
Context A
Context B
```

使用不同 Backend。

验证：

```text
A shutdown
不影响 B

A error handler
不影响 B

A category
不影响 B
```

---

## 6.48 Spdlog symbol scan

Linux CI 检查禁止 global API dependency。

这是：

```text
architecture regression guard
```

不是普通功能测试替代品。

---

## 6.49 RawLog tests

覆盖：

```text
normal output

long message truncation

EINTR retry

logging unavailable

recursion

backend failure

late teardown
```

---

## 6.50 RawLog no-allocation test

支持的平台：

```text
operator new instrumentation
```

验证 RawLog 不触发 heap allocation。

---

## 6.51 Allocation failure tests

fault injection：

```text
message materialization

LogRecord creation

Category intern

SetCategoryLevel

BackendFactory

TextFormatter
```

分别验证：

```text
Logger 不抛异常

Context control API 返回 Status

Config builder 仍允许 config-time exception
```

---

## 6.52 Multi-Runtime tests

```text
Runtime A:
Debug
file A

Runtime B:
Error
file B
```

验证：

```text
A category update
不影响 B

A Backend failure
不影响 B

A Shutdown
不影响 B

A spdlog teardown
不影响 B
```

---

## 6.53 Runtime integration tests

必须验证：

```text
Logging 先于 Middleware 初始化

Middleware 获得 Logger

Middleware shutdown 日志正常

Executor shutdown 日志正常

Logging 最后 shutdown

Logging timeout
→ Runtime Stop Timeout

late Logger
→ safe no-op
```

---

## 6.54 queue_high_watermark tests

不要求严格精确。

压力测试使 queue 明显达到：

```text
>= 500
```

最终 high-watermark：

```text
应反映明显高负载
```

不能长期仍然：

```text
10
```

但不要求：

```text
精确 == 503
```

---

## 6.55 Benchmark

至少：

```text
BM_Logger_Enabled_Disabled

BM_Logger_Enabled_Enabled

BM_CompileTimeDisabled

BM_Log_Sync

BM_Log_Async_1Producer

BM_Log_Async_NProducer

BM_Async_DropNewest

BM_Async_DropOldest

BM_Async_Block

BM_GetLogger

BM_SetCategoryLevel

BM_TextFormatter

BM_Flush_Async

BM_Flush_Sync
```

---

## 6.56 V1 性能契约

正式冻结：

```text
Compile-time disabled macro:
  logger expression 不求值
  message expression 不求值

Runtime disabled:
  builder 不执行

Logger::Enabled:
  O(1)
  no allocation
  no Backend I/O
  no ordinary mutex

Logger copy:
  O(1)
  no heap allocation

Async enabled path:
  caller-side allocation allowed
  no Sink/output I/O in caller

AsyncQueue:
  bounded storage

Backend:
  serialized invocation
```

---

## 6.57 不冻结的性能实现

不冻结：

```text
具体 ns latency

mutex vs lock-free queue

shared_ptr vs intrusive refcount

CategoryEntry container

queue memory layout

control-thread implementation

exact throughput
```

这些通过 benchmark 决定。

---

## 6.58 不宣称 hard-real-time safe

V1 Logging 可能：

```text
构造 std::string

heap allocation

queue mutex

atomic refcount

Block waiting

Sync Backend I/O
```

因此：

> **RoComm Logging V1 不是 hard-real-time-safe API。**

严格实时路径应该：

```text
关闭对应 category
```

或者：

```text
compile-time strip
```

未来需要严格实时日志时：

> **单独设计 fixed-size RT event / trace primitive。**

不能继续把普通 Logger 扩张成实时 tracing system。

---

## 6.59 V1 明确不做

V1 不实现：

```text
NativeBackend

Public Sink SPI

structured key/value logging

JSON logging

network logging sink

Runtime Backend hot swap

arbitrary CategoryFilter callback

Quill-style deferred formatting

encoded formatting arguments

per-thread frontend queue

hard-real-time Logger

distributed tracing

process-global logger registry

custom pattern language

implicit NullBackend from empty outputs
```

---

## 6.60 V1 正式冻结的 Semantic Contract

以下正式冻结：

```text
Logging Runtime-local ownership

四层职责：
Logger
LoggingContext
LoggingBackend
RawLog

LogLevel

Critical != Fatal

SourceLocation static-lifetime model

LogRecord move-only logical model

message ownership

category interned lifetime

caller metadata capture

Logger default-disabled semantics

Logger O(1) no-allocation copy

Logger::Enabled O(1)

lifecycle atomic + effective-level atomic model

Log()

LogLazy()

recursion check before Enabled

Builder supported return types

logging macros

compile-time level mapping

compile-time filtering

Category grammar

Category case-sensitive

reserved rocomm.* namespace

longest dot-segment prefix

GetLogger → Result<Logger>

invalid category → InvalidArgument

CategoryLevels synchronization belongs to LoggingContext

dynamic category update

strong update failure guarantee

two-layer filtering

LoggingMode

DropNewest

DropOldest

Block

severity RawLog fallback without changing overflow policy

queue capacity >= 1

LoggingConfig builder exception model

default Async

default DropNewest

default flush-on-level Error

TextBackend common configuration

empty outputs → InvalidArgument

AsyncQueue belongs to LoggingContext

bounded MPSC / single-consumer semantics

Backend invocation serialized

Sync Write caller-thread execution

Sync control operation timeout-safe execution

BackendInvocationGuard on actual Backend thread

Flush prefix barrier

barrier is non-droppable

Flush timeout semantics

Shutdown admission linearization

logical Stopped vs shutdown completion

Repeated Shutdown joins same completion

Shutdown timeout deferred destruction

Custom Backend non-infinite-blocking contract

LoggingStats semantics

eventually consistent high-watermark

LoggingBackend is sole Public Backend SPI

BackendFactory per-context ownership

Factory failure propagation

Factory null success → InternalError

Factory exception containment

Backend Initialize() no-argument V1 contract

Backend control APIs noexcept + Status

Backend timeout owned by LoggingContext

TextFormatter full-line semantics

SpdlogBackend global-state isolation

RawLog requirement

RawLog EINTR handling

Runtime closes Logging after Middleware/Executor

V2 NativeBackend reuses V1 runtime

V2 native-specific config cannot mutate V1 Text contract
```

---

## 6.61 仍属于 Header/API Finalization 的事项

经过本轮收敛后，剩余问题已经不再涉及架构方向，主要是 exact C++ 形状：

```text
1. RoComm 公共 Duration 的最终类型名和 header。

2. LoggingBackendFactory 的最终 typedef 所在 header。

3. LoggingConfig::SetBackendFactory
   是按值接收还是 rvalue reference 接收。

4. LoggingConfig move/copy exact noexcept contract。

5. LoggingContext move assignment
   对目标已有 state 的 release-before-acquire exact 行为。

6. LogRecord 内部 message storage
   是 std::string 还是 small-buffer representation。

7. CategoryEntry internal reference
   使用 shared_ptr / intrusive / stable registry slot 的具体方案。

8. LogLazy traits/static_assert 的 exact diagnostic 文本。

9. Flush barrier 的具体内部实现：
   control record
   还是 sequence/frontier。

10. Sync control operation
    使用临时 self-owned thread
    还是持久 control worker。

11. LoggingStats counter 的 exact atomic width/storage。

12. Backend contract test helper 的具体测试框架接口。
```

这些属于：

> **Header / implementation finalization。**

不再重新打开：

```text
模块职责
AsyncQueue 归属
Backend SPI 边界
Flush/Shutdown 语义
Runtime-local ownership
```

---

## 6.62 最终架构图

```text
                     RoComm / Application
                              │
                              ▼
                            Logger
                              │
              ┌───────────────┴──────────────┐
              │                              │
     compile-time filtering          Runtime filtering
                                             │
                                       CategoryEntry
                                             │
                                             ▼
                                    message construction
                                             │
                                             ▼
                                         LogRecord
                                             │
                                             ▼
                                      LoggingContext
                              ┌──────────────┴──────────────┐
                              │                             │
                             Sync                          Async
                              │                             │
                       backend mutex                   AsyncQueue
                              │                             │
                              │                         writer
                              │                             │
                              └──────────────┬──────────────┘
                                             │
                                    BackendInvocationGuard
                                             │
                                             ▼
                                      LoggingBackend
                                 ┌───────────┼───────────┐
                                 │           │           │
                                 ▼           ▼           ▼
                           SpdlogBackend  NativeBackend CustomBackend
                                V1            V2
```

Control path：

```text
Flush / Shutdown
       │
       ▼
LoggingContext control operation
       │
       ├── Async mode
       │      ↓
       │   writer/control path
       │
       └── Sync mode
              ↓
       self-owned control execution
              ↓
      backend serialization
              ↓
      Flush / Shutdown
```

Emergency path：

```text
Backend recursion
logging unavailable
allocation failure
fatal invariant
late teardown
       │
       ▼
     RawLog
       │
       ▼
low-level stderr
```

---

## 6.63 最终设计结论

RoComm Logging V1 正式采用：

> **RoComm-owned Logger frontend + Runtime-local LoggingContext + unified bounded dispatch + narrow LoggingBackend SPI + SpdlogBackend。**

其中：

> **Logger 只负责 Category、Enabled、message admission 和 SourceLocation。**

> **LoggingContext 统一负责 lifecycle、Category policy、sync/async dispatch、queue、overflow、Backend serialization、Flush、Shutdown、timeout 和 statistics。**

> **AsyncQueue 不属于 SpdlogBackend，也不属于未来 NativeBackend。**

> **LoggingBackend 是唯一 Public Backend extension point；V1 不建立第二套 Public Sink SPI。**

> **Backend invocation 在 LoggingContext 层串行化，因此 Custom Backend 不需要自行支持并发 Write。**

> **Sync mode 的普通 Write 在 caller thread 执行，Backend 阻塞会传播到 call site；它不是 hot-path optimization mode。**

> **带 timeout 的 Flush/Shutdown control operation 即使在 Sync mode 下，也不能直接把可能无限阻塞的 Backend control call 放在用户 caller thread 上执行，而应使用 self-owned control execution，使 timeout contract 可兑现。**

> **Recursion guard 在真实 Backend invocation thread 设置。Async 模式下是 writer/control thread，Sync 模式下是 caller/control thread。**

> **Logger 在 recursion path 中先检查 guard，再检查普通 Enabled；Backend Initialize/Flush/Shutdown 内产生的日志因此会进入 RawLog，而不会因为 Runtime state 暂时不可用被静默吞掉。**

> **Flush 使用与 log admission 线性化的不可丢弃 prefix barrier，不依赖简单 accepted/dispatched counter 推断完成状态。**

> **Shutdown 的 correctness 由原子 Running→Stopping admission cut 保证；批量将 CategoryEntry 设置为 Off 只是优化。**

> **Logical Stopped 与 shutdown quiescence 分离。第一次 Shutdown Timeout 后，再次 Shutdown 必须继续等待同一个 shutdown completion，而不是仅因 lifecycle 已 Stopped 就无条件返回 Ok。**

> **Shutdown timeout 后 Backend 和 worker backing 可以延迟析构；RoComm 不通过 unsafe thread termination 回收永久阻塞的 Custom Backend。**

> **DropNewest 和 DropOldest 都严格保持自身 overflow 语义；Error/Critical 只额外触发 RawLog fallback，不改变究竟丢弃哪条记录。**

> **BackendFactory 是 control-plane exception boundary；failure Result 原样传播，success+nullptr 为 InternalError，bad_alloc 映射 ResourceExhausted，其余异常映射 InternalError。**

> **LoggingConfig 是普通 configuration builder，允许配置期 allocation exception；已经创建的 LoggingContext control API 则通过 Status 表达资源失败并保证失败时不留下半更新配置。**

> **GetLogger 正式返回 `Result<Logger>`；invalid category 返回 InvalidArgument；默认构造 Logger 永远是安全 disabled handle。**

> **Category 是 case-sensitive 的 dot hierarchy；CategoryLevels 自身不负责线程安全，统一由 LoggingContext 同步。**

> **Category intern storage 属于 self-owned Logging State，其生命周期必须覆盖 shutdown timeout 后的所有 in-flight LogRecord。**

> **Logger copy 必须 O(1) 且不分配内存，但 V1 不冻结 shared_ptr 等具体引用机制。**

> **`queue_high_watermark` 是 eventually-consistent diagnostic statistic，不为了绝对精确在每次 producer Push 上付出高成本 atomic-max。**

> **SpdlogBackend 必须隔离 spdlog process-global registry、global level、global shutdown 和 global async runtime，并通过运行时测试和符号扫描防止架构回退。**

> **`TextBackendConfig.outputs.empty()` 是 InvalidArgument，不隐式转换为 NullBackend。**

> **Runtime shutdown 时 Logging 最后关闭；Middleware 和 Executor shutdown 阶段仍使用完整正常 Logging pipeline。**

> **V2 NativeBackend 必须适配 V1 已冻结的 TextBackend contract；Native-specific 能力使用独立配置，不反向污染 V1 公共结构。**

> **Logging failure 永远不能修改 Topic / Service operation 的通信 Status。**

> **V1 Logging 不宣称 hard-real-time safe；严格实时事件记录如果未来需要，应单独设计 fixed-size RT event / trace primitive。**

本文档可以作为：

```text
docs/logging.md

RoComm Logging V1 Design Baseline
```

进入下一阶段：

```text
Public Header / API Finalization
        ↓
Backend-independent Logging Core
        ↓
TestBackend Contract Validation
        ↓
SpdlogBackend
        ↓
Runtime Integration
        ↓
Concurrency / Performance Verification
```