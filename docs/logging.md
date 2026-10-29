# RoComm Logging V1 设计

**Project:** RoComm — Robotics Communication Runtime  
**Module:** `rocomm::logging`  
**Language:** C++17  

**Architecture:** **FROZEN**
**Semantic Contract:** **FROZEN**  
**Concurrency Model:** **FROZEN**  
**Backend SPI Semantics:** **FROZEN**
**Runtime Integration:** **FROZEN**
**Public Header/API Contract:** **FROZEN — V1 Implementation Baseline**
**Internal Data Structures / Algorithms:** **OPEN**

本文档定义 RoComm Logging V1 的公共 API、Runtime-local 生命周期、Category 与过滤模型、同步/异步执行语义、有界队列及 Overflow、Admission、Flush/Shutdown、Backend SPI、spdlog 集成、文本输出、RawLog、Runtime 集成、工程结构、测试标准以及 V2 NativeBackend 的演进边界。

Logging 已从《RoComm V1 架构与接口设计》中拆分为独立模块。RoComm 主架构只保留 Logging Integration Contract；本文是 Logging 子系统的正式 V1 Implementation Baseline。

## API naming alignment

本文早期 code block 中出现的 `Create`、`GetLogger`、`SetCategoryLevel`、`LogLazy`
等 PascalCase 名称，只表示对应的 V1 semantic operation。项目采用
`docs/cpp_coding_guidelines.md` 后，所有手写函数和成员函数统一以 snake_case
交付：`create`、`get_logger`、`set_category_level`、`log_lazy` 等。该迁移只改变
public spelling，不引入 compatibility wrapper，也不改变本文冻结的 admission、
lifecycle、overflow、flush、shutdown、timeout 或 Backend SPI 语义。

---

# 1. 模块定位、设计原则与总体架构

## 1.1 模块定位

RoComm Logging 是 RoComm Runtime 的基础可观测性 primitive。

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

log admission
synchronous dispatch
asynchronous dispatch

bounded queue
overflow policy

Flush / Shutdown
timeout / quiescence

LoggingBackend SPI

SpdlogBackend

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

未来如果需要 Metrics、Tracing 或实时事件记录，应分别定义独立 primitive，而不是继续扩张普通 Logging。

核心原则：

> **Logging 是 RoComm Runtime 的基础 primitive，而不是通用 Observability Framework。**

---

## 1.2 与 RoComm 主架构的边界

RoComm 主架构只依赖以下 Logging Integration Contract：

```text
1. Logging 是 Runtime-local 的。

2. Core 与 Middleware 使用统一 Logger abstraction。

3. Logging 支持 Category / Severity filtering。

4. disabled logging 不执行 message expression。

5. Logging Public API 不泄漏第三方 logging 类型。

6. Logging failure 不修改 Topic / Service operation 的 Status。

7. 默认异步 Logging 的正常 dispatch 不在通信线程执行 Backend output I/O；
   emergency RawLog 允许在 caller 直接输出 stderr，且不承诺最大阻塞时长。

8. Runtime::Stop() 统一管理 Logging lifecycle。

9. fatal / recursion / logging subsystem failure
   必须存在独立 emergency 边界；普通故障使用 RawLog，
   必须 fail-fast 的路径仅可选 nonblocking diagnostic（§5.12）。

10. Logging shutdown timeout 不通过 unsafe thread termination
    强制结束 Backend 或调用线程。

11. Runtime shutdown 时 Logging 最后关闭。
    Middleware / Executor shutdown 期间仍可以正常记录日志。

12. Logging V1 不宣称 hard-real-time safe。

13. Logger::Enabled() 是 advisory fast path，
    生命周期 correctness 最终由 LoggingContext admission 决定。

14. 成功 log admission 与 Running → Stopping
    必须存在唯一可定义的线性化关系。
```

以下内容全部由本文定义，而不再进入主架构：

```text
Logger exact API
LogRecord

LoggingConfig
LoggingContext

CategoryLevels
CategoryEntry

Admission semantics

AsyncQueue
Overflow semantics

Flush barrier
Shutdown completion

LoggingBackend SPI

TextFormatter
TextBackendConfig

SpdlogBackend

RawLog

Logging test matrix

V2 NativeBackend boundary
```

---

## 1.3 设计路线

RoComm Logging 采用：

> **RoComm-owned frontend + Runtime-local logging runtime + narrow replaceable backend。**

V1：

```text
Logger
LogRecord

LoggingConfig
LoggingContext

Category policy

Admission gate

Synchronous dispatch

AsyncQueue
Async writer

Flush / Shutdown control path

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
Admission semantics

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

> **V2 的核心新增内容是 NativeBackend，而不是第二套 Logging Runtime。**

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
        LoggingContext Admission
                 │
                 ▼
       owned message / LogRecord
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
       BackendInvocationGuard
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
Backend recursion
logging unavailable
allocation failure
Backend failure
late teardown
ordinary invariant diagnostic
        │
        ▼
      RawLog
        │
        ▼
 low-level stderr output
```

---

## 1.5 四层职责

### 1.5.1 Logger Frontend

Logger 负责：

```text
Category identity

Enabled()

SourceLocation

Log()

LogLazy()

logging macros
```

Logger 不负责：

```text
Runtime lifecycle ownership

Admission linearization

queue

worker

Backend lifecycle

file / console

rotation

Flush ownership

Shutdown ownership
```

---

### 1.5.2 LoggingContext

LoggingContext 负责：

```text
Runtime-local ownership

lifecycle / admission state

CategoryLevels
CategoryEntry registry

global filtering

admission linearization

sync dispatch
async dispatch

AsyncQueue

writer lifecycle

overflow policy

Backend invocation serialization

Backend recursion boundary

Flush barrier

Shutdown completion

timeout

statistics
```

LoggingContext 是整个 Logging module 的 Runtime 核心。

---

### 1.5.3 LoggingBackend

LoggingBackend 负责：

```text
消费 LogRecord

backend-specific rendering

backend-specific output routing

secondary output filtering

console / file / rotation

backend-specific I/O

backend-specific statistics
```

LoggingBackend 不负责：

```text
RoComm global category policy

RoComm lifecycle admission

RoComm async queue

RoComm overflow semantics

RoComm Runtime Stop semantics

RoComm public Logger API
```

---

### 1.5.4 RawLog

RawLog 完全独立于前三层。

RawLog 不得依赖：

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

## 1.6 AsyncQueue 为什么属于 LoggingContext

以下语义是 RoComm Logging contract：

```text
Synchronous
Asynchronous

DropNewest
DropOldest
Block

Flush

Shutdown

timeout

statistics
```

如果 Queue/Worker 属于：

```text
SpdlogBackend
NativeBackend
CustomBackend
```

那么不同 Backend 很容易分别实现不同的：

```text
queue

overflow

worker

flush

shutdown
```

此时：

```text
LoggingMode
LogOverflowPolicy
Flush
Shutdown
```

就不再是真正统一的 RoComm 语义。

因此正式冻结：

> **Async dispatch、Queue、Overflow、Worker lifecycle 属于 LoggingContext，而不是 LoggingBackend。**

从 LoggingContext 的角度：

> **LoggingBackend 是同步、串行调用的 LogRecord consumer。**

Custom Backend 即使内部再次 buffering，那也是 Backend 私有实现，不能改变 LoggingContext 已经承诺的公共语义。

---

## 1.7 为什么 V1 不使用 spdlog async runtime

spdlog 已经拥有成熟的 async logger 和 overflow policy。

RoComm 不使用 `spdlog::async_logger` 不是因为第三方库缺少能力，而是因为：

```text
RoComm 必须自己拥有 Runtime-local Queue。

RoComm 必须自己定义统一 Overflow contract。

RoComm 必须自己统计 queue/drop metrics。

RoComm 必须统一 Flush / Shutdown / timeout。

SpdlogBackend / NativeBackend / CustomBackend
必须具有完全相同的 async semantics。

不能让第三方 thread-pool ownership
决定 RoComm Runtime lifecycle。
```

因此 V1：

```text
LoggingContext async runtime

        +

synchronous SpdlogBackend
```

---

## 1.8 Runtime-local 原则

每一个 Runtime 拥有自己的 LoggingContext。

禁止依赖：

```text
global mutable logger singleton

process-global mutable category registry

process-global mutable log level

process-global mutable Backend
```

两个 Runtime 可以拥有：

```text
不同 default level

不同 category overrides

不同 Backend

不同日志文件

不同 queue capacity

不同 overflow policy

不同测试 CaptureBackend
```

关闭 Runtime A：

```text
不得影响 Runtime B
```

---

## 1.9 Hot path 与 correctness boundary 必须分离

`Logger::Enabled()` 是：

```text
fast-path advisory check
```

它用于：

```text
调用方显式 if (logger.Enabled(...))

LogLazy()

logging macros
```

从而避免：

```text
无意义 builder 执行

无意义 message construction
```

但：

> **Enabled() 不是生命周期 correctness 的最终线性化边界。**

原因是：

```text
Enabled() sees Running
        ↓
另一个线程 Shutdown wins
        ↓
当前线程继续向 Log()/admission 前进
```

这是合法并发。

因此真正保证：

```text
Shutdown 后不接受新记录
```

的边界必须在：

```text
LoggingContext admission
```

内部。

正式原则：

> **Enabled 是 advisory；Admission 是 authoritative。**

---

## 1.10 热路径原则

Runtime-disabled logging：

```text
message builder 不执行

不访问 Backend

不执行 output I/O

不获取 ordinary mutex
```

Compile-time disabled logging进一步要求：

```text
logger expression 不执行

message expression 不执行
```

Async enabled logging：

```text
允许 caller-side message construction

允许 caller-side string allocation

允许 queue synchronization

不执行 Backend output I/O
```

因此 V1 明确不承诺：

```text
enabled async log
= zero allocation
```

RoComm 的目标是：

> **把 output I/O 移出通信调用线程，而不是伪造一个所有普通日志路径均 zero-allocation 的实时契约。**

---

## 1.11 Sync 模式的明确代价

Synchronous mode：

```text
caller thread
    ↓
LoggingContext Backend serialization
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

都会传播到调用点。

而且：

> **一个慢 Backend/Sink 不只阻塞当前 Sync Logger，也会通过 Backend invocation serialization 阻塞其他并发 Sync Logger。**

因此 Sync mode 不是 communication hot-path optimization mode。

如果关注调用线程延迟，应使用：

```text
Asynchronous

+

DropNewest / DropOldest
```

而不是 `Block` 或 Sync I/O。

---

## 1.12 不宣称 hard-real-time safe

V1 Logging 可能：

```text
构造 std::string

heap allocation

queue mutex

condition_variable

atomic reference count

Block waiting

Sync Backend I/O
```

所以：

> **RoComm Logging V1 不是 hard-real-time-safe API。**

严格实时线程应该：

```text
关闭对应 category
```

或者：

```text
compile-time strip
```

未来如果需要严格 RT 事件记录，应新增：

```text
fixed-size RT event / trace primitive
```

而不是把普通 Logger 演化成复杂 realtime tracing framework。

---

## 1.13 参考项目与采用边界

RoComm Logging 不复刻单一日志库，而是分别借鉴高质量 C++ 项目的不同设计点。

| 参考项目 | RoComm 主要借鉴 |
|---|---|
| Abseil Logging | 窄前端、窄消费边界、Raw logging、递归防护 |
| Boost.Log | 全局 filtering 与 output-specific filtering 分层 |
| spdlog | 成熟 console/file/rotation output |
| Folly XLOG | disabled hot path、lazy evaluation、compile-time filtering |
| brpc | 项目级 logging abstraction 与二次 filtering |
| Qt `QLoggingCategory` | runtime category level 调整 |
| Quill | frontend/backend 分离、未来 low-latency 路线 |
| ClickHouse | Backend 内部 multi-output routing 思路 |

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

最终原则：

> **窄 API、低依赖、Runtime-local、明确生命周期、Backend 可替换。**

---

# 2. Public API、数据模型、Category 与配置

## 2.1 Namespace

公共：

```cpp
namespace rocomm::logging {
}
```

spdlog Backend factory：

```cpp
namespace rocomm::logging::spdlog {
}
```

未来 NativeBackend：

```cpp
namespace rocomm::logging::native {
}
```

禁止：

```text
rocomm::logging::detail

rocomm::logging::internal

rocomm::logging::impl
```

private 实现由：

```text
src/logging/
```

本身形成边界。

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

severity：

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

如果 effective level：

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

`Off` 只作为：

```text
threshold
configuration
compile-time filtering
```

不得构造：

```text
LogRecord(level = Off)
```

---

## 2.3 Critical 不等于 Fatal

V1 不定义：

```text
Fatal
```

普通 LogLevel。

因为：

> **Logging severity 与 process control 是两个概念。**

`Critical`：

```text
只表示最高普通日志严重级别
```

不会自动：

```text
abort
exit
terminate
```

真正 fail-fast 由调用路径显式控制：仅可选尝试已证明 nonblocking 的 emergency diagnostic，然后 `std::terminate()`。不得先等待普通 Critical Log 或可能阻塞的 RawLog，具体边界见 §5.12。

---

## 2.4 SourceLocation

RoComm 使用 C++17，不依赖 `std::source_location`。

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

标准宏：

```cpp
#define ROCOMM_SOURCE_LOCATION \
    ::rocomm::logging::SourceLocation::FromStatic( \
        __FILE__, \
        __func__, \
        static_cast<std::uint32_t>(__LINE__))
```

显式 cast 避免启用 `-Wconversion` 时产生无意义窄化告警。

---

## 2.5 SourceLocation lifetime

`FromStatic()` 表达：

> `file` 与 `function` 必须具有覆盖 LogRecord 生命周期的稳定存储。

框架宏中的：

```text
__FILE__
__func__
```

满足该约束。

Application 不应把临时字符串的：

```text
c_str()
```

传给 `FromStatic()`。

---

## 2.6 LogRecord

LogRecord 表示：

> **message 已完成 materialization、caller metadata 已捕获，但尚未完成 Backend output rendering 的逻辑日志记录。**

Public read-only model：

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
    // Only logging runtime may construct a normal LogRecord.
};
```

LogRecord 是：

```text
move-only
```

对象。

---

## 2.7 LogRecord message ownership

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

返回后：

```text
temporary_string
```

可以立即销毁。

Async queue 与 Backend 不依赖 caller-owned message。

---

## 2.8 Category storage lifetime

Category 不要求每个 LogRecord 重复复制：

```text
"rocomm.fastdds.discovery"
```

LoggingContext 可以建立：

```text
interned CategoryEntry
```

LogRecord 保存稳定 reference/handle。

Category backing 由：

```text
self-owned LoggingState
```

持有。

其生命周期必须覆盖：

```text
queued LogRecord

in-flight Backend::Write()

shutdown timeout 后仍在执行的 Backend::Write()
```

即使已经发生：

```text
LoggingContext wrapper 已销毁

所有 Logger 已销毁

writer/control execution 仍未完成
```

只要 in-flight LogRecord 还存在：

```cpp
record.category()
```

仍必须有效。

因此 worker/control state 必须安全保持：

```text
LoggingState
```

而 LoggingState 持有：

```text
CategoryEntry registry
```

---

## 2.9 Backend 对 LogRecord 是 borrowed access

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

SourceLocation 中的 borrowed pointer
```

Custom Backend 如果需要延迟处理：

> **必须自己 materialize/copy 所需字段。**

LoggingBackend SPI 本身是 synchronous consumer contract。

---

## 2.10 两级格式化模型

V1 严格区分：

```text
Message Construction
```

与：

```text
Record Rendering
```

---

### 2.10.1 Message Construction

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

这一阶段允许：

```text
std::string allocation
```

---

### 2.10.2 Record Rendering

发生在：

```text
Backend dispatch side
```

把：

```text
timestamp

level

category

caller thread ID

source

message
```

渲染成：

```text
2026-10-26T14:35:20.123456+08:00 [INFO] [rocomm.fastdds.discovery] [tid=18342] Participant matched [discovery.cpp:217]
```

---

## 2.11 两级格式化的意义

这种设计同时满足：

```text
Public Core header 无 fmt/spdlog dependency

caller timestamp 正确

caller thread ID 正确

Async worker 不覆盖 caller metadata

Backend 可以决定最终 output representation
```

---

## 2.12 Logger

Logger 是：

> **轻量、copyable、Runtime-bound handle。**

Public API：

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

## 2.13 默认 Logger

```cpp
Logger logger;
```

得到 disabled Logger。

行为：

```text
logger.Enabled(...)
→ false

logger.Log(...)
→ safe no-op

logger.LogLazy(...)
→ builder 不执行

logger.category()
→ empty string_view
```

---

## 2.14 Logger copy / move

正式性能契约：

```text
copy:
O(1)
no heap allocation

move:
O(1)
no heap allocation
```

内部允许：

```text
std::shared_ptr

intrusive refcount

stable state handle

其他等价机制
```

但具体引用机制：

```text
不是 V1 API contract
```

V1 明确接受：

```text
Logger::Enabled()
```

执行 lifecycle 和 effective-level 两次 O(1) scalar atomic read。

不为了减少一次 atomic read引入额外复杂度。

---

## 2.15 Logger lifetime

Logger 必须安全保持其所需 Logging State。

LoggingContext wrapper 销毁后：

```text
Logger 仍 memory-safe
```

但对应 Logging State 已：

```text
Stopping / Stopped
```

因此：

```text
Enabled()
→ false

Log()
→ safe no-op 或 lifecycle admission reject
```

对于已经完成 Stop 且调用 `Log()` 的情况：

```text
最终不会进入 Backend
```

Logger 不允许让 worker 因 wrapper 消失而永久保持 Running。

---

## 2.16 Logger::Enabled()

`Logger::Enabled()` 是面向调用方和 LogLazy 的 advisory fast-path predicate。

必须满足 `noexcept`、O(1)、zero heap allocation、no Backend I/O、no ordinary mutex。

逻辑等价于：

```cpp
if (!state_ || !state_->logging_enabled()) {
    return false;
}
if (state_->lifecycle() != Running) {
    return false;
}
return IsEnabled(level, entry_->effective_level());
```

`logging_enabled()` 是 Create 时固定的配置，不会由动态 level API 改变；它可以用等价的 disabled handle representation 表达。通常需要 lifecycle 与 effective-level 两次 scalar atomic read；不可变 enabled 配置不增加 mutable atomic state。

`Off` 不是合法 record severity，`Enabled(Off)` 返回 false。

---

## 2.17 Enabled() 不是 Admission

`Enabled()` 只表示：

> **在该读取瞬间，这条日志值得继续构造。**

它不保证：

```text
稍后一定能够 admission
```

因为：

```text
Thread A:
Enabled() → true

Thread B:
Running → Stopping

Thread A:
继续构造 message
继续调用 Log/admission
```

是合法并发。

因此：

> **Enabled() 不参与 Shutdown 的最终 correctness 线性化。**

真正 lifecycle admission 决策必须发生在 LoggingContext admission boundary。

---

## 2.18 Enabled atomic representation

V1 不要求内部直接写成：

```cpp
std::atomic<LogLevel>
```

推荐明确的 scalar representation：

```text
std::uint8_t
或
std::uint32_t
```

实现阶段必须验证目标平台相应 atomic。

冻结的是：

```text
O(1)

scalar atomic reads

no ordinary mutex
```

不冻结：

```text
所有平台都必须 is_always_lock_free
```

某个平台如果 64-bit/某种标量 atomic 不是 lock-free：

```text
功能语义必须保持正确

性能差异通过 benchmark 暴露
```

---

## 2.19 Logger::Log()

```cpp
void Log(
    LogLevel level,
    std::string_view message,
    SourceLocation source = {}) const noexcept;
```

正常顺序冻结为：

```text
recursion check（默认 / config-disabled Logger 保持 no-op）
↓
valid state / immutable logging-enabled check
↓
validate record severity（Off / 非法值不提交 record）
↓
Running 时执行 effective-level check
    // 非 Running 时交给 admission 拒绝
    // 因 level 过滤返回前，再读 lifecycle；仍为 Running 才返回
↓
LoggingContext admission
    // 与 Running → Stopping 线性化
    // 失败：rejected_after_stop++，return
↓
capture caller timestamp / caller thread ID
↓
materialize owned message
↓
construct LogRecord
↓
sync dispatch ownership / async queue ownership transfer
    // 未转交前的任何失败或取消必须释放 admission ownership
```

valid state check 只检查 Logger 是否持有有效 state handle；默认或 moved-from Logger 是 empty handle，安全 no-op。持有已停止 state 的 Logger 仍是有效 handle，不在这里因 lifecycle 提前返回。`enabled=false` 是独立的不可变 no-op gate，即使 state 已停止也不到 admission、不改变数据面 counters。

`Log()` 不调用完整 `Enabled()` 作为 lifecycle gate。Running 时因 level 返回的操作，以最后一次观察 Running 的检查作为过滤决定；之后发生 Shutdown 不追溯计为 rejection。若观察到非 Running，必须继续到 authoritative admission。Shutdown 不修改 effective-level cache（§3.51）。

非法 severity 不产生 LogRecord，也不到 admission；对有效且 enabled 的 Logger 计 `frontend_failures++`，不能把 `Off` 当作普通 severity 输出。

---

## 2.20 为什么 direct Log() 仍检查 effective level

直接调用 `logger.Log(level, message)` 也不能绕过运行期 Category filtering。

```text
Enabled():
  immutable enabled gate + lifecycle/level advisory check

Direct Log / LogLazy 的内部提交路径:
  immutable enabled gate
  + Running 时的 effective-level check
  + authoritative admission lifecycle check
```

非 Running 时跳过 level filtering，由 admission 统一拒绝并计数；Running 时的过滤决定遵循 §2.19 的 lifecycle 复查规则。默认 / config-disabled Logger 始终是 no-op。

过滤、admission ownership 与 `accepted` 是不同概念：获得 admission 后仍可能 materialization 失败、overflow 或取消，`accepted` 的转交计数点见 §3.68。

---

## 2.21 Admission boundary

LoggingContext 内部必须存在语义等价于：

```text
TryBeginAdmission()
```

的机制。

它不是 Public API。

语义：

```text
if lifecycle == Running:
    成功获得本次 log admission ownership

if lifecycle == Stopping / Stopped:
    reject
```

成功 admission 与：

```text
Running → Stopping
```

必须存在唯一线性化顺序。

---

## 2.22 Admission 与 Shutdown 线性化

若 Log admission 先赢：

```text
Log:
TryBeginAdmission()
→ success

Shutdown:
Running → Stopping
```

则：

> **Shutdown 必须把该 log 视为已经进入 Logging subsystem 的 operation。**

它后续必须：

```text
成功 dispatch

或
按已冻结 overflow policy terminalize

或
在后续 frontend/materialization failure 中安全释放 admission ownership

或
Block producer 因 Shutdown 取消入队并 terminalize（§3.28）
```

不能出现：

```text
admission 成功
但 Shutdown 不知道它存在
```

---

若 Shutdown 先赢：

```text
Shutdown:
Running → Stopping

Log:
TryBeginAdmission()
→ reject
```

则：

```text
normal Backend 不再接收该记录
```

---

## 2.23 Admission 内部实现不冻结

允许使用：

```text
atomic lifecycle + in-flight admission count

mutex-protected admission gate

combined atomic state word

其他可证明线性化的机制
```

不引入：

```text
Public AdmissionToken
Public AdmissionLease
```

之类新 API。

冻结的是：

> **Admission 与 Running→Stopping 必须线性化。**

---

## 2.24 Logger::LogLazy()

disabled logging 时 builder 不执行。正常路径：

```text
recursion check（默认 / config-disabled Logger 保持 no-op）
↓
Enabled(level)
↓
builder()，最多一次；异常 / null const char* 按 frontend failure 处理
↓
内部提交路径（与 direct Log 共用过滤与 admission 规则）
↓
admission success
↓
capture caller metadata
↓
move std::string result / copy borrowed result into owned message
↓
construct LogRecord
↓
sync dispatch / async queue ownership transfer
```

`std::string` 返回值由 builder 在 admission 前构造；`std::string_view` / `const char*` 的 owned copy 在 admission 成功后执行。不得声称所有 builder allocation 都发生在 admission 后。

内部提交路径必须接受 owned string 的移动，不能强制调用 public `Log(std::string_view)` 后再次复制。内部桥接声明可以放入 Logger 的 private 部分，installed header 不依赖 private source headers。

Enabled 成功不保证 admission：若 builder 正常返回、后续提交观察到 Shutdown 并被 admission 拒绝，则 builder 已执行，`rejected_after_stop++`，record 不进入 Backend。若 builder 自身失败或记录在 Running 时被新的 category 配置过滤，则不到 admission，不增加该 rejection counter。

---

## 2.25 LogLazy Builder contract

V1 Builder 只允许返回：

```text
std::string

std::string_view

const char*
```

实际 trait 判断必须对返回类型执行 decay。

概念：

```cpp
template <typename Builder>
using LogLazyRawResult =
    std::invoke_result_t<Builder&>;

template <typename Builder>
using LogLazyResult =
    std::decay_t<
        LogLazyRawResult<Builder>>;
```

因此字符串字面量：

```cpp
ROCOMM_LOG_INFO(
    logger,
    "started");
```

即使 lambda 原始返回类型：

```text
const char (&)[N]
```

decay 后：

```text
const char*
```

因此合法。

---

## 2.26 LogLazy compile-time diagnostic

Header 必须提供清晰 static assertion。

语义等价于：

```cpp
static_assert(
    std::is_same_v<Result, std::string> ||
    std::is_same_v<Result, std::string_view> ||
    std::is_same_v<Result, const char*>,
    "LogLazy builder must return "
    "std::string, std::string_view, or const char*.");
```

不要求输出复杂 template type name。

---

## 2.27 LogLazy materialization

如果 Builder 返回：

```text
std::string
```

必须允许：

```text
move into owned LogRecord message
```

避免：

```text
string
→ string_view
→ 再次 copy
```

如果返回：

```text
std::string_view
```

则：

```text
copy into owned message
```

如果返回：

```text
const char*
```

则：

```text
copy into owned message
```

---

## 2.28 Null `const char*`

如果 LogLazy Builder 返回：

```text
nullptr
```

形式的 `const char*`：

```text
不得构造 invalid string_view
不得产生 UB
```

处理为 frontend failure：

```text
frontend_failures++
```

如果 level：

```text
Error / Critical
```

则：

```text
RawLog minimal diagnostic
```

较低 level：

```text
safe drop
```

---

## 2.29 Builder exception

Builder 抛异常：

```text
不得逃出 Logger::LogLazy()
```

处理：

```text
frontend_failures++
```

如果：

```text
Error / Critical
```

则：

```text
RawLog minimal diagnostic
```

否则：

```text
safe drop
```

V1 不把 Builder exception 转成应用 communication Status。

---

## 2.30 Recursion check 必须先于 Enabled()

对持有有效且 config-enabled state 的 Logger，Backend invocation guard active 时，必须先处理 recursion，再进行普通 `Enabled()` 检查。Backend Initialize/Flush/Shutdown 期间 state 可能非 Running，不能因此静默吞掉递归诊断。

默认、moved-from 或 config-disabled Logger 始终 no-op；其 LogLazy builder 不执行，数据面 counters 不变。这一 immutable handle/config 检查不属于 lifecycle rejection。

```text
empty / config-disabled handle → no-op
otherwise:
  Backend recursion check
  ↓
  normal lifecycle / level / admission path
```

---

## 2.31 Recursive `Log()`

Backend invocation guard active 时：

```cpp
logger.Log(...)
```

行为：

```text
不进入 normal LoggingContext

不做普通 Category filtering

best-effort RawLog

recursive_fallbacks++
```

---

## 2.32 Recursive `LogLazy()`

Backend invocation guard active 时进入 emergency lazy path。

该路径：

```text
builder 最多执行一次

catch everything

bounded materialization

RawLog
```

同时使用：

```text
thread_local RawFallbackGuard
```

如果 Builder 自身再次调用 Logger：

```text
nested recursive logging
→ suppress
```

防止：

```text
Raw fallback
→ builder
→ Logger
→ Raw fallback
→ ...
```

---

## 2.33 BackendInvocationGuard 与 RawFallbackGuard 区分

两者职责不同。

`BackendInvocationGuard`：

```text
由 LoggingContext 在真正调用 Backend callback 的线程设置
```

用于检测：

```text
Backend → Logger
```

递归。

`RawFallbackGuard`：

```text
只在 emergency fallback formatting/path 内设置
```

用于防止：

```text
Raw fallback 本身再次递归
```

普通 LogLazy Builder exception：

```text
不等价于 Backend recursion
```

不得错误设置或长期保留 BackendInvocationGuard。

---

## 2.34 V1 不提供 fmt-style Core API

C++17 没有：

```cpp
std::format
```

Public Core headers 也不能强制依赖：

```text
fmt
spdlog
```

因此 V1 不冻结：

```cpp
logger.Info(
    "publisher {} created",
    name);
```

这样的 typed format API。

推荐：

```cpp
ROCOMM_LOG_INFO(
    logger,
    BuildMessage(name));
```

未来可以新增独立：

```text
rocomm::logging::fmt
```

adapter target，但不是 Core requirement。

---

## 2.35 Logging macros

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
→ at most once

message_expr
→ 仅 runtime-enabled 时求值，
   但 Enabled 后仍可能在 admission race 中输给 Shutdown
```

---

## 2.36 Compile-time level

预处理常量：

```cpp
#define ROCOMM_LOG_LEVEL_TRACE     0
#define ROCOMM_LOG_LEVEL_DEBUG     1
#define ROCOMM_LOG_LEVEL_INFO      2
#define ROCOMM_LOG_LEVEL_WARN      3
#define ROCOMM_LOG_LEVEL_ERROR     4
#define ROCOMM_LOG_LEVEL_CRITICAL  5
#define ROCOMM_LOG_LEVEL_OFF       6
```

顺序必须与：

```cpp
LogLevel
```

一致。

---

## 2.37 `ROCOMM_LOGGING_COMPILED_LEVEL`

允许：

```text
0..6
```

默认：

```text
ROCOMM_LOG_LEVEL_TRACE
```

原因：

> 默认构建不能永久删除 Debug/Trace，否则 Runtime category 动态配置失去意义。

Deployment build 可以主动提高 compiled level。

---

## 2.38 Compile-time stripping

如果：

```text
ROCOMM_LOGGING_COMPILED_LEVEL
=
ROCOMM_LOG_LEVEL_INFO
```

那么：

```text
ROCOMM_LOG_TRACE
ROCOMM_LOG_DEBUG
```

必须完全编译为 no-op。

并且：

```text
logger_expr 不求值

message_expr 不求值
```

直接调用：

```cpp
logger.Log(...)
```

不受 compile-time macro stripping 约束。

---

## 2.39 Category model

Category 使用：

```text
dot-separated hierarchy
```

官方 category 至少包括：

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

扩展示例：

```text
rocomm.fastdds.discovery

rocomm.fastdds.writer

rocomm.service.client

rocomm.service.server
```

---

## 2.40 Category namespace

以下 namespace 保留：

```text
rocomm
rocomm.*
```

Application 推荐：

```text
app.*
robot.*
company.*
```

---

## 2.41 Category grammar

Category：

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

RoComm 官方 category 必须 lowercase。

Application 也强烈推荐 lowercase。

---

## 2.42 GetLogger()

正式 API：

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

category creation/allocation failure
→ ResourceExhausted

valid Context but lifecycle Stopping/Stopped
→ RuntimeStopped

moved-from / invalid LoggingContext wrapper
→ InvalidState
```

---

## 2.43 Logging disabled 下的 GetLogger

如果：

```text
LoggingConfig.enabled == false
```

但 LoggingContext 仍是：

```text
Running
```

则：

```text
GetLogger(valid category)
→ success
```

返回 disabled Logger：

```text
Enabled() == false
```

这样 Core/Middleware 不需要因为关闭日志而改变初始化控制流。

---

## 2.44 Hierarchical filtering

规则：

> **longest matching dot-segment prefix wins。**

例如：

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

## 2.45 普通字符串 prefix 不属于 category inheritance

以下：

```text
rule:
rocomm.fast

category:
rocomm.fastdds
```

不得匹配。

解析应等价于：

```text
rocomm.fastdds.discovery
        ↓
rocomm.fastdds
        ↓
rocomm
        ↓
default
```

而不是任意 `starts_with()`。

---

## 2.46 CategoryLevels

private：

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

位于：

```text
src/logging/category_levels.hpp
```

---

## 2.47 CategoryLevels thread-safety

正式冻结：

> **CategoryLevels 自身不保证 thread-safe。**

同步责任统一属于：

```text
LoggingContext
```

不为了一个 private helper 再建立第二套内部 locking architecture。

---

## 2.48 Dynamic Category API

```cpp
Status SetDefaultLevel(
    LogLevel level) noexcept;

Status SetCategoryLevel(
    std::string_view category,
    LogLevel level) noexcept;

Status ClearCategoryLevel(
    std::string_view category) noexcept;
```

非法参数：

```text
InvalidArgument
```

allocation failure：

```text
ResourceExhausted
```

valid Context but stopped：

```text
RuntimeStopped
```

moved-from Context：

```text
InvalidState
```

---

## 2.49 `noexcept` 与 allocation

这些 control API 虽然：

```text
noexcept
```

但内部可能因为：

```text
std::string
map/hash insertion
temporary state
```

产生 allocation。

实现必须捕获：

```text
std::bad_alloc
```

并返回：

```text
ResourceExhausted
```

不得因为 `noexcept` 导致：

```text
std::terminate()
```

---

## 2.50 Category update strong failure guarantee

正式冻结：

```text
成功
→ 新配置完整 commit

失败
→ 原配置保持不变
```

所有可能失败的步骤必须发生在 commit 之前。

推荐流程：

```text
validate
↓
完成必要 allocation
↓
准备新 configuration mutation
↓
完成所有可能失败的计算
↓
commit CategoryLevels
↓
以不会失败的 atomic store
传播 effective-level cache
```

一旦进入 cache propagation：

```text
不得再存在 failure path
```

---

## 2.51 Category update 可见性

强 failure guarantee 不等于：

```text
所有 Logger 同一个 CPU 指令瞬间切换 level
```

更新多个 CategoryEntry 时允许出现短暂：

```text
部分 Logger 已观察新值

部分 Logger 仍观察旧值
```

最终必须：

```text
所有受影响 Logger 收敛到新值
```

V1 不承诺 global atomic cutover。

---

## 2.52 SetDefaultLevel / ClearCategoryLevel

`SetDefaultLevel()` 和 `ClearCategoryLevel()` 具有与 `SetCategoryLevel()` 相同的：

```text
failure strong guarantee

bad_alloc → ResourceExhausted

commit 后 propagation 不失败
```

语义。

---

## 2.53 Runtime 重复 SetCategoryLevel

同一 category 重复设置：

```text
last-write-wins
```

例如：

```text
rocomm.fastdds = Info

随后：

rocomm.fastdds = Debug
```

最终：

```text
Debug
```

不执行 merge。

---

## 2.54 LoggingConfig

Public API：

```cpp
class LoggingConfig {
public:
    LoggingConfig() = default;

    LoggingConfig(
        const LoggingConfig&) = default;

    LoggingConfig&
    operator=(
        const LoggingConfig&) = default;

    LoggingConfig(
        LoggingConfig&&) noexcept = default;

    LoggingConfig&
    operator=(
        LoggingConfig&&) noexcept = default;

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

## 2.55 LoggingConfig::SetCategoryLevel 累积语义

Configuration builder 的 Category override 语义正式冻结为：

```text
不同 category
→ 累积

同一 category 重复设置
→ last-write-wins
```

例如：

```cpp
LoggingConfig config;

config
    .SetCategoryLevel(
        "rocomm.fastdds",
        LogLevel::Debug)
    .SetCategoryLevel(
        "rocomm.service",
        LogLevel::Warn)
    .SetCategoryLevel(
        "rocomm.fastdds",
        LogLevel::Trace);
```

最终配置等价于：

```text
rocomm.fastdds
→ Trace

rocomm.service
→ Warn
```

即：

```text
不同 key 累积

同 key 替换
```

不执行：

```text
merge
multi-value
first-write-wins
```

---

## 2.56 Config API 与 Runtime API 的同名语义一致

以下两个 API：

```cpp
LoggingConfig::SetCategoryLevel(...)

LoggingContext::SetCategoryLevel(...)
```

对于同一 category 的重复操作都遵循：

```text
last-write-wins
```

差别仅在于：

```text
LoggingConfig
→ 构建未来 Context 的静态配置

LoggingContext
→ 修改正在运行 Context 的 control-plane state
```

不能让两个同名 API 拥有不同重复设置语义。

---

## 2.57 LoggingConfig category 内部存储不冻结

可以使用：

```text
std::vector<CategoryOverride>

std::map<std::string, LogLevel>

std::unordered_map<std::string, LogLevel>

其他等价结构
```

只要保证：

```text
不同 category 累积

同 category last-write-wins

copy/move contract 成立
```

---

## 2.58 LoggingConfig copy/move

Copy 允许 allocation 与 `std::bad_alloc`，不标 noexcept。Move 是 V1 `noexcept` contract，实现必须选择支持该行为的数据布局。

CI 同时验证公共声明：

```cpp
static_assert(std::is_nothrow_move_constructible_v<LoggingConfig>);
static_assert(std::is_nothrow_move_assignable_v<LoggingConfig>);
```

以及内部实际 storage 的移动属性：在未强制声明外层 noexcept 的成员聚合 / storage 类型上检查相同 traits，或逐项验证实际被移动的成员。自定义 move body 还必须审查其所有执行步骤；这些步骤不得 allocation、调用可能抛异常的用户代码或执行其他可失败工作。

显式 `noexcept = default` 会使外层 trait 为 true，即使成员 move 可能抛异常，因此仅检查 LoggingConfig 自身不能证明内部移动安全。新增成员时必须同步 storage 检查；优先调整 representation，不能静默移除公共 noexcept。

---

## 2.59 SetBackendFactory

正式使用：

```cpp
LoggingConfig&
SetBackendFactory(
    LoggingBackendFactory factory);
```

即：

```text
by value + move
```

这样新 factory 在进入函数前已经成功构造。

setter 内部再 move 到配置状态。

应提供：

> **strong exception guarantee。**

如果新 factory construction/copy 在调用前失败：

```text
旧 factory 不变
```

---

## 2.60 LoggingConfig builder exception model

LoggingConfig 是：

```text
普通 configuration builder
```

涉及 allocation 的 builder：

```text
SetCategoryLevel
SetBackendFactory
```

允许配置期 C++ exception。

这与已经创建的 LoggingContext control API 不同：

```text
LoggingContext control API
→ noexcept + Status
```

---

## 2.61 Dynamic update 复杂度

这是：

```text
control-plane operation
```

V1 允许：

```text
O(number_of_existing_categories)
```

遍历并更新 CategoryEntry。

不为低频控制操作引入：

```text
复杂 prefix tree

RCU

generation propagation framework
```

未来可以用：

```text
generation + lazy recomputation
```

优化，而无需改变 API。

---

## 2.62 两层 Filtering

过滤分成：

```text
Layer 1:
LoggingContext global Category/Level filter

Layer 2:
Backend / output-specific filter
```

Layer 1：

```text
发生在 message builder 前
```

Layer 2：

```text
发生在 LogRecord 已创建以后
```

例如：

```text
Global:
Debug+

Console:
Info+

File:
Debug+
```

Backend：

> **不能重新启用 LoggingContext 已经过滤掉的记录。**

---

## 2.63 LoggingMode

```cpp
enum class LoggingMode : std::uint8_t {
    Synchronous,
    Asynchronous,
};
```

---

## 2.64 LogOverflowPolicy

```cpp
enum class LogOverflowPolicy : std::uint8_t {
    DropNewest,
    DropOldest,
    Block,
};
```

定义：

```text
DropNewest
→ queue 满时丢弃当前新记录

DropOldest
→ queue 满时删除最旧 queued record，
  当前新记录进入 queue

Block
→ caller 等待 capacity
```

---

## 2.65 Queue capacity

`SetQueueCapacity()` 只设置配置。

真正 validation：

```text
LoggingContext::Create()
```

Async mode 必须：

```text
capacity >= 1
```

否则：

```text
InvalidArgument
```

V1 不设置任意固定最大值。

如果用户设置：

```text
SIZE_MAX
```

或者其他无法分配的巨大值：

```text
size computation overflow
或
allocation failure
```

返回：

```text
ResourceExhausted
```

不把经验性的：

```text
8192
65536
```

误写成公共上限。

---

## 2.66 默认配置

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

category overrides:
none

backend:
build default
```

---

## 2.67 默认 Async

RoComm 的主要定位是：

```text
communication runtime
```

所以默认不应让：

```text
console I/O
file I/O
rotation
flush
```

运行在通信调用线程。

默认：

```text
Asynchronous
```

---

## 2.68 默认 DropNewest

默认目标：

```text
bounded memory

+

不阻塞 communication producer
```

因此不默认：

```text
Block
```

使用：

```text
DropNewest
```

保留已经进入 queue 的历史。

需要最新日志优先的应用可以选择：

```text
DropOldest
```

---

## 2.69 Flush-on-level

如果：

```text
record.level >= flush_on_level
```

正常 record output 后：

```text
触发 Backend Flush
```

`Off`：

```text
关闭 automatic flush-on-level
```

---

## 2.70 默认 Error flush

默认：

```text
flush_on_level = Error
```

偏向：

```text
crash diagnosability
故障日志尽快落盘
```

而不是 maximum throughput。

高吞吐系统如果 Error 很频繁，应主动设置：

```text
Critical
```

或：

```text
Off
```

README 应显式说明这一权衡。

---

## 2.71 Text Backend configuration

SpdlogBackend 和未来 NativeBackend 共享以下通用语义：

```text
console

file

rotation

text rendering

output-specific severity
```

因此 V1 定义：

```text
TextFormatConfig

TextBackendConfig
```

---

## 2.72 TextFormatConfig

```cpp
enum class LogTimeZone : std::uint8_t {
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

---

## 2.73 ConsoleOutputConfig

```cpp
enum class ConsoleStream : std::uint8_t {
    Stdout,
    Stderr,
};

enum class ColorMode : std::uint8_t {
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

## 2.74 FileOutputConfig

```cpp
struct FileOutputConfig {
    std::string path;

    bool truncate = false;

    LogLevel min_level =
        LogLevel::Trace;
};
```

空 path：

```text
InvalidArgument
```

---

## 2.75 RotatingFileOutputConfig

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

必须：

```text
path 非空

max_file_size_bytes > 0

max_archived_files > 0
```

否则：

```text
InvalidArgument
```

`max_archived_files` 不包括 active file。

---

## 2.76 TextOutputConfig

```cpp
using TextOutputConfig =
    std::variant<
        ConsoleOutputConfig,
        FileOutputConfig,
        RotatingFileOutputConfig>;
```

---

## 2.77 TextBackendConfig

```cpp
struct TextBackendConfig {
    TextFormatConfig format;

    std::vector<TextOutputConfig>
        outputs;
};
```

---

## 2.78 Empty outputs

正式冻结：

```text
outputs.empty()
→ InvalidArgument
```

不能把空 outputs 偷偷解释成：

```text
NullBackend
```

如果用户希望关闭 logging：

```cpp
config.SetEnabled(false);
```

如果未来确实需要：

```text
discard all records
but preserve pipeline
```

应显式定义：

```text
NullBackend
```

V1 不做。

---

# 3. LoggingContext、Admission、异步调度、并发与生命周期

## 3.1 Duration

Logging 不定义新的 Duration 类型。

所有 timeout API 复用：

```text
RoComm Core 公共 Duration
```

即本文中的：

```cpp
rocomm::Duration
```

具体基础表示由 RoComm Core 统一定义。Logging timeout 统一使用单调时钟计算 elapsed/deadline；system_clock 只用于 LogRecord timestamp。

负 timeout 返回 InvalidArgument，不发起 operation；零 timeout 仍允许建立请求/关闭 admission，但不等待尚未完成的工作，Pending 返回 Timeout，已完成返回保存结果。default/moved-from Shutdown 的无条件 Ok、disabled Context 的无 Backend control no-op 优先于 timeout validation。转换或 deadline 加法不得溢出，超大正 duration 使用饱和 deadline。

Logging 不建立第二个：

```text
LoggingDuration
```

类型。

---

## 3.2 LoggingContext

Public API：

```cpp
class LoggingContext {
public:
    LoggingContext(
        const LoggingContext&) = delete;

    LoggingContext&
    operator=(
        const LoggingContext&) = delete;

    LoggingContext(
        LoggingContext&& other) noexcept;

    LoggingContext&
    operator=(
        LoggingContext&& other) noexcept;

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
        rocomm::Duration timeout) noexcept;

    Status Shutdown(
        rocomm::Duration timeout) noexcept;

    LoggingStats
    GetStats() const noexcept;

    ~LoggingContext() noexcept;

private:
    struct State;
    StateHandle state_;
};
```

`StateHandle` 为 private representation，不冻结具体实现。

---

## 3.3 Lifecycle 与 quiescence

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

另外独立维护：

```text
shutdown completion
```

因此允许：

```text
lifecycle = Stopped

但

Backend cleanup / blocked invocation
仍未 quiescent
```

这与 RoComm Runtime 的：

```text
logical Stopped
vs
true quiescence
```

模型一致。

---

## 3.4 Admission 是生命周期正确性的唯一正式边界

`Logger::Enabled()` 可以观察：

```text
Running
```

然后在其返回之后 lifecycle 立即发生变化。

因此最终决定：

```text
当前 Log 是否真正进入 Logging subsystem
```

的必须是 LoggingContext admission。

语义等价于：

```text
TryBeginAdmission()
```

但不对 Public API 暴露。

---

## 3.5 Admission 成功的语义

成功 admission 表示本次 logging operation 在线性化意义上先于 Shutdown admission cut，并已进入 shutdown responsibility。

责任必须连续保持到以下任一情况：

```text
sync dispatch 接管（包括等待 Backend serialization 与完成 Write）
async queue 接管（包括排队、in-flight Write 与 overflow terminalization）
DropNewest terminalization
frontend/materialization failure terminalization
Block producer 因 Shutdown 取消入队并 terminalize
```

queue/dispatch 接管只是 ownership 转移，不是整条 record 已完成。Shutdown 必须继续追踪接管后的 queued/in-flight 工作直到终态。

Admission ownership 不等于 `LoggingStats::accepted`；后者只在 §3.68 的成功转交点递增。

---

## 3.6 Admission 失败

如果 lifecycle 已：

```text
Stopping
Stopped
```

则 admission：

```text
reject
```

direct `Log()` 情况：

```text
rejected_after_stop++
```

不进入 normal Backend。

---

## 3.7 Admission ownership release

admission 成功后的 message allocation、LogRecord construction、queue admission 失败，以及 Block 因 Shutdown 取消入队，都必须安全 terminalize，并释放 in-flight admission ownership。

采用内部 RAII 或等价机制，保证每次成功 admission 恰好一次转交或终结；ownership 转交后由 queue/dispatch 的 completion tracking 负责最终完成，不能在两者之间出现 Shutdown 看不见的空窗。

frontend / 意外 queue 转交 failure 计 `frontend_failures++`；DropNewest 计 `dropped_newest++`；Block 因 Shutdown 取消按 §3.28 处理。它们都不是 admission rejection。所有终态必须推进其对应的 Flush / Shutdown prefix completion，包括尚未产生 LogRecord 的 frontend failure。

具体 lease/count/frontier representation 保持 private。

---

## 3.8 Create 顺序

```text
validate LoggingConfig
↓
prepare CategoryLevels / immutable enabled gate
↓
if enabled=false:
    不创建 Backend、queue、writer 或 Backend control runner
else:
    invoke BackendFactory / validate result
    ↓
    Backend::Initialize()
    ↓
    allocate bounded queue if Async
    ↓
    prepare shutdown completion + guaranteed cleanup execution resources
    ↓
    start writer / prepare required control execution
↓
publish Running
```

成功 Create 直接返回 Running Context。发布 Running 前必须保证：后续 Shutdown initiation、析构和 move assignment 的旧 state release，不需要再 allocation 或创建线程即可发起并保持 cleanup progress。

可以复用已经存在的 writer/control runner，或预备等价执行资源；具体布局不冻结。无法准备这些必要资源时 Create 返回 ResourceExhausted 或相应错误，并 rollback，不返回半初始化 Context。

---

## 3.9 Create rollback

任何阶段失败都必须 rollback。

例如 Backend 已成功 Initialize，但 queue allocation 失败：

```text
best-effort Backend::Shutdown()
↓
destroy Backend
↓
return original failure
```

writer 已启动后出现后续失败：

```text
request worker stop
↓
complete safe worker cleanup
↓
Backend::Shutdown()
↓
destroy state
↓
return original failure
```

不得：

```text
泄漏 worker
泄漏 Backend
返回半初始化 Context
```

---

## 3.10 Rollback failure

Rollback 自己也可能失败，例如：

```text
Backend::Shutdown()
→ failure
```

规则：

> **Create 最终仍返回导致 Create 失败的原始 Status。**

Rollback failure：

```text
不得覆盖 primary failure
```

如果 normal Logging 尚不可用：

```text
RawLog best effort
```

报告 rollback secondary failure。

---

## 3.11 LoggingBackendFactory 所在 Header

`LoggingBackendFactory` 正式定义于：

```text
include/rocomm/logging/logging_backend.hpp
```

因为它直接描述：

```text
LoggingBackend creation contract
```

而不是 LoggingConfig 专属类型。

定义：

```cpp
using LoggingBackendFactory =
    std::function<
        Result<
            std::unique_ptr<LoggingBackend>>()>;
```

---

## 3.12 Factory failure Result

Factory 返回失败：

```text
原 Status 直接传播
```

LoggingContext 不二次重映射已经明确表达的 Backend creation failure。

---

## 3.13 Factory success + nullptr

以下是 contract violation：

```text
Result success
+
unique_ptr == nullptr
```

LoggingContext 返回：

```text
InternalError
```

Diagnostic：

```text
Logging backend factory returned a null backend.
```

---

## 3.14 BackendFactory exception boundary

Factory callable 可能抛异常。

`LoggingContext::Create()` 必须捕获。

```text
std::bad_alloc
→ ResourceExhausted

其他 std::exception
→ InternalError

unknown exception
→ InternalError
```

`std::exception::what()` 可进入 Diagnostic。

Factory exception 不得穿过：

```text
LoggingContext::Create()
```

边界。

---

## 3.15 Logging disabled

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

但 Context 仍正常进入：

```text
Running
```

因此这是：

```text
一个真实 lifecycle Context
```

而不是空 wrapper。

---

## 3.16 Disabled Context control behavior

`enabled=false` 创建真实 Running Context，但不创建 Backend、AsyncQueue、writer 或 Backend control runner。

```text
GetLogger(valid category) → success disabled Logger
Enabled(any level) → false
Log / LogLazy → no-op，builder 不执行
Flush → Ok
Shutdown → Running → Stopping → Stopped，Ok
GetStats → 数据面 counters 与 backend counters 全 0
```

SetDefaultLevel / SetCategoryLevel / ClearCategoryLevel 在 Running 时仍可验证、更新 category 配置并返回通常的 Status，但不能改变 immutable enabled gate、创建 Backend 或启用日志。停止后这些 API 返回 RuntimeStopped。

disabled Logger 在 Shutdown 前后均不到 admission，也不执行 emergency builder 或增加 recursive/rejection counters；默认 / moved-from Logger 采用相同 no-op 原则。

---

## 3.17 Default Backend

用户未提供 Factory 时：

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
enabled=true

且 build 无 default Backend
```

则：

```text
UnsupportedConfiguration
```

不得 silent fallback。

---

## 3.18 不暴露 BackendKind

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
不提前为未来 Feature 预留枚举

不让 Auto 优先级变成稳定 API

BackendFactory 已能表达显式选择
```

---

## 3.19 LoggingContext move constructor

Move constructor：

```text
转移 state handle
```

moved-from Context：

```text
becomes empty/invalid wrapper
```

不会触发原 state shutdown，因为 ownership 已被转移。

---

## 3.20 LoggingContext move assignment

Move assignment 采用：

> **release-before-acquire semantics。**

如果 destination 原来持有一个 state：

```text
先对旧 state 发起 safe shutdown initiation
↓
释放 destination wrapper 对旧 state 的 ownership
↓
接收 source state
↓
source becomes moved-from
```

不得因为 move assignment：

```text
无界阻塞等待旧 Backend
```

旧 state 如果仍需要 cleanup：

```text
通过 self-owned state deferred cleanup
```

继续完成。

---

## 3.21 Moved-from LoggingContext

moved-from wrapper 表示：

```text
invalid / empty wrapper
```

而不是 RuntimeStopped Context。

正式行为：

```text
Shutdown(...)
→ Ok
  // safe idempotent release operation

Flush(...)
→ InvalidState

GetLogger(...)
→ InvalidState

SetDefaultLevel(...)
→ InvalidState

SetCategoryLevel(...)
→ InvalidState

ClearCategoryLevel(...)
→ InvalidState

GetStats()
→ zero snapshot
```

---

## 3.22 为什么 moved-from 的不同 API 返回不同结果

这些行为是有意设计，而不是不一致。

### Shutdown

`Shutdown()` 属于：

```text
resource release / cleanup operation
```

对 empty wrapper：

```text
安全幂等 no-op
→ Ok
```

方便：

```text
scope cleanup
error cleanup
generic teardown
```

无需额外判断 wrapper 是否 moved-from。

---

### Flush / GetLogger / Set*Level

这些属于：

```text
state-dependent control operation
```

需要有效 LoggingState。

moved-from wrapper：

```text
没有 state
```

因此：

```text
InvalidState
```

---

### GetStats

`GetStats()` 属于：

```text
diagnostic query
```

为了让：

```text
监控
错误处理
析构诊断
```

代码可以无条件读取：

```text
moved-from
→ zero snapshot
```

避免诊断路径再次引入 wrapper-validity 分支。

---

## 3.23 AsyncQueue

Async mode 使用：

> **bounded MPSC / single-consumer queue。**

V1 推荐首版实现：

```text
preallocated ring buffer

+

mutex

+

condition_variable
```

不提前实现 lock-free queue。

原因：

```text
语义容易验证

overflow 精确

Block 容易实现

Shutdown race 容易审计

TSAN 友好
```

---

## 3.24 Queue allocation

Async Context 初始化时预分配固定：

```text
capacity
```

Running 后 queue container：

```text
不自动 grow

不 resize
```

保证：

```text
queue storage bounded
```

但不承诺：

```text
整个 Log operation zero allocation
```

---

## 3.25 EnqueueResult

private：

```cpp
enum class EnqueueResult {
    Accepted,
    DroppedNewest,
    DroppedOldest,
    Stopped,
};
```

用于：

```text
overflow accounting

RawLog fallback

shutdown race
```

不进入 Public API。

---

## 3.26 DropNewest

queue full：

```text
当前新 LogRecord 不入 queue

旧 queue 不变

dropped_newest++
```

如果当前新 record：

```text
Error / Critical
```

则：

```text
RawLog minimal fallback
```

但该 record 不计 accepted，仍然属于：

```text
dropped_newest
```

不是正常 dispatch。

---

## 3.27 DropOldest

queue full：

```text
删除最旧 queued LogRecord

dropped_oldest++

当前 LogRecord 入 queue
```

如果被删除 record：

```text
Error / Critical
```

则：

```text
RawLog minimal fallback
```

但不得：

```text
为了保护高 severity
改删别的低 severity record
```

原则：

> **Overflow policy 不根据 severity 改写；severity 只影响 emergency fallback。**

---

## 3.28 Block

queue full 时 producer 等待 capacity，直到 consumer pop 或 Shutdown 开始。Shutdown 必须唤醒所有 blocked producers。

admission 已成功、但尚未获得 queue ownership 的 producer，在观察到 Stopping/Stopped 后取消入队：

```text
不调用 Backend
不增加 accepted
不增加 rejected_after_stop（admission 已成功）
不增加 dropped_newest / dropped_oldest / frontend_failures
内部 terminalize，并释放 admission ownership
推进 Flush / Shutdown 对应 prefix completion
Error/Critical → best-effort RawLog minimal fallback
```

这是一种明确的 shutdown cancellation，公共 V1 stats 不单独计数；它不改写任何 overflow policy。若 queue ownership 已在 shutdown cut 前成功转交，则该 record 属于正常 drain/overflow responsibility。

不得形成 Shutdown 等 producer、producer 等 queue、writer 已停止的死锁。

---

## 3.29 Control signaling 不占普通 LogRecord capacity

Flush / Shutdown control progress：

> **不能因为普通 LogRecord queue 已满而无法建立。**

因此 control signaling：

```text
不消耗普通 LogRecord capacity

不受 DropNewest 影响

不受 DropOldest 影响
```

实现可以是：

```text
独立 control state

side channel

sequence/frontier

non-droppable internal representation
```

具体算法不冻结。

---

## 3.30 Async writer

每个 Async LoggingContext：

```text
一个 normal logging writer
```

V1 不提供多个 logging workers。

这样可以：

```text
保持 queue order

简化 Backend serialization

简化 Flush

简化 Shutdown
```

---

## 3.31 Backend invocation serialization

同一 Backend：

```text
Initialize
Write
Flush
Shutdown
```

不能并发执行。

Async：

```text
writer/control execution
```

负责 serialization。

Sync：

```text
caller/control execution
+
Backend invocation mutex
```

负责 serialization。

因此 Custom Backend：

```text
不要求 Write() thread-safe
```

---

## 3.32 Sync normal Write

Synchronous mode：

```text
Logger::Log
↓
caller thread
↓
Backend invocation serialization
↓
Backend::Write
```

Backend 慢：

```text
当前 caller 被阻塞
```

并且：

```text
其他并发 Sync caller
也可能阻塞在 Backend serialization
```

这是正式语义。

---

## 3.33 日志顺序

Async mode 的 Backend dispatch 顺序：

```text
accepted queue linearization order
```

同一 producer：

```text
A
B
C
```

正常 dispatch：

```text
A
B
C
```

除非 record 明确被 overflow 丢弃。

多 producer：

```text
不保证 timestamp total order
```

不做跨线程 timestamp merge。

---

## 3.34 BackendInvocationGuard

LoggingContext 在实际调用 Backend callback 的线程设置：

```text
thread_local BackendInvocationGuard
```

保护：

```text
Initialize

Write

Flush

Shutdown
```

Backend 内部再次调用 RoComm Logger：

```text
normal pipeline 不允许递归
→ RawLog fallback
```

---

## 3.35 Guard thread ownership

Async：

```text
guard 设置在 writer/control execution thread
```

Sync：

```text
normal Write guard 设置在 caller thread

Flush/Shutdown guard 设置在 control execution thread
```

绝不能只在：

```text
原 Logger caller thread
```

设置 guard。

---

## 3.36 Flush

Public API：

```cpp
Status Flush(
    rocomm::Duration timeout) noexcept;
```

Flush 使用：

> **与 log admission 线性化的、不可丢弃的 prefix barrier。**

---

## 3.37 Flush barrier 语义

Flush 为与 admission 线性化的不可丢弃 prefix barrier。

boundary 前成功 admission 的 operation 必须先达到终态：

```text
Backend::Write 已返回
或 DropNewest / DropOldest terminalized
或 frontend/materialization failure terminalized
或 Block 因 Shutdown 取消入队并 terminalized
```

这包括 boundary 前已获得 admission、但尚未完成 owned message / LogRecord 构造的 operation。Backend Write 返回即满足 dispatch completion；其 output error 由 BackendStats 表示，不追溯转成该 Flush 的逐记录错误。

prefix 全部完成后执行 Backend::Flush；其返回 Status 是该运行期 Flush 的结果。Backend Flush 失败不能伪装为 Ok，也不撤销已经完成的 prefix。boundary 后的 operation 不属于本次等待要求。

若 Flush 与 Shutdown 合并，则遵循 §3.42–§3.43 的共享 shutdown result。

---

## 3.38 Flush linearization point

定义为：

> **LoggingContext 在与 admission 相同的同步域中，成功建立该 Flush prefix boundary 的时刻。**

因此：

```text
boundary 之前成功 admission
→ 属于该 Flush guarantee，即使尚未计 accepted

boundary 之后成功 admission
→ 不要求该 Flush 等待
```

---

## 3.39 Flush barrier algorithm 不冻结

允许：

```text
control barrier

sequence + completion frontier

completion bitmap

其他语义等价实现
```

V1 不冻结某个具体内部算法。

尤其不能简单把：

```text
accepted == dispatched
```

当作 Flush correctness，因为：

```text
DropOldest

in-flight Write

concurrent producer
```

都会破坏这种简单推断。

---

## 3.40 Flush barrier 不可丢弃

任何：

```text
DropNewest
DropOldest
Block
```

都不能丢掉 Flush control progress。

---

## 3.41 Flush timeout 覆盖范围

timeout 从：

```text
Flush() 调用开始
```

计算，并覆盖完整过程：

```text
建立 prefix boundary

等待之前记录 terminalize

等待 Backend invocation serialization

执行 / 等待 Backend::Flush completion
```

如果任一阶段超过 deadline：

```text
Flush()
→ Timeout
```

调用方不得假设：

```text
prefix 已经完整 flush
```

后台 control operation 可以继续安全推进。

---

## 3.42 Concurrent Flush

每个 Flush 有自己的 prefix guarantee 和从调用开始计算的 deadline；可以合并兼容的内部 control state，但不得削弱任意 caller 的 prefix。

Shutdown cut 前已建立的 Flush 可以先完成，或被 shutdown prefix 吸收。被吸收时由 shutdown 的最终 Flush/Shutdown 完成它，并观察共享 final Status。尚未建立 boundary 就观察到非 Running 的调用直接 join shutdown completion。

所有 pending / timed-out control execution 都必须受同一 control ordering 管理：Backend::Shutdown 开始前完成其应执行的 Backend Flush，或将其合并/取消为 join shutdown；Backend::Shutdown 开始后不得再执行 Write/Flush/Initialize。

per-operation thread 不得仅靠取得 Backend mutex 就盲目执行旧请求；它必须核对 control state。shutdown completion Complete 还要求所有旧 control execution 已退出或安全解除执行责任。

---

## 3.43 Flush during Stopping / Stopped

有效 Context 已非 Running 时，新的 Flush 不建立运行期 barrier，而是 join 同一个 shutdown completion。

```text
在本次 deadline 内完成 → 返回保存的 shutdown final Status
到 deadline 仍 Pending → Timeout
已经 Complete → 立即返回保存的 final Status
```

完成不等于成功：Backend Flush/Shutdown 的错误按 §3.56 传播。moved-from Context 返回 InvalidState；config-disabled Context 没有 Backend，返回 Ok。

---

## 3.44 Backend timeout 不属于 Backend SPI

Backend：

```cpp
Status Flush() noexcept;
Status Shutdown() noexcept;
```

不接受 timeout。

deadline 是：

```text
LoggingContext control-plane semantic
```

这样 Custom Backend 不需要重新实现 timeout framework。

---

## 3.45 Sync mode 下的 Flush/Shutdown timeout

Synchronous mode 只意味着：

> **正常 `Backend::Write()` 在 Logger caller thread 执行。**

不意味着：

```text
Backend::Flush()
Backend::Shutdown()
```

必须直接运行在调用 `Flush()` / `Shutdown()` 的用户线程。

否则 Backend 永久阻塞时：

```text
timeout contract 无法兑现
```

---

## 3.46 Self-owned control execution

带 timeout 的 Backend control operation 必须通过：

```text
self-owned control execution state
```

执行。

概念：

```text
caller
  ↓
request control operation
  ↓
self-owned execution
  ↓
wait Backend serialization
  ↓
Backend::Flush / Shutdown
  ↓
completion
```

caller：

```text
只等待到 deadline
```

control/admission synchronization 不得跨 Backend callback、RawLog I/O、builder 或其他用户代码持锁；overflow emergency 输出也必须移到这些锁外。所有可能等待的同步步骤纳入 deadline，不能用无期限获取普通 mutex 破坏 timeout。

---

## 3.47 Control execution 实现保持开放

允许 persistent worker、lazy worker、per-operation self-owned thread 或等价策略，但都必须满足 §3.8 的 shutdown 资源预备和 §3.42 的 control ordering。

persistent worker 线程数稳定；lazy/per-operation execution 有创建成本。任何策略下，同一 Backend 的 Initialize/Write/Flush/Shutdown 都必须串行。

因此一个永久阻塞的 Backend callback 会阻止该 Backend 后续所有 callback 推进。per-operation thread 只能隔离 caller 的等待和执行状态，不能绕过 Backend serialization；也不能借此宣称后续 Shutdown 可以完成。

caller 仍必须能按各自 timeout 返回，backing state 必须保留。Header Finalization / implementation review 应记录选定策略、资源预备方式和阻塞边界，不改变上述公共语义。

---

## 3.48 Control execution 创建失败

Flush 为单次请求准备 control state / execution 时，若发生 `std::bad_alloc` 或资源类 `std::system_error`，返回 ResourceExhausted；无法分类的执行创建错误返回 InternalError，不得逃出 noexcept。

请求发布前完成可失败准备。准备失败不留下无人执行的 barrier、completion 或 ownership，也不改变 Context lifecycle；之后 Flush / Shutdown 可以正常重试。

Shutdown 所需 completion 与保证 cleanup 能被执行的资源，在 Create 成功前准备完毕。Shutdown cut、析构 initiation 和 move assignment 旧 state release 不得依赖新的 allocation / thread creation；它们不能因控制资源创建失败而留下无人推进的 Pending。若某种 lazy/per-operation 策略需要这些资源，必须提供已经准备好的 cleanup 执行路径。

错误 Status 必须支持不带 Diagnostic 的 no-allocation 构造；错误报告时 Diagnostic allocation 失败只能省略 Diagnostic，不得导致 noexcept terminate。Backend control API 与动态 category API 的错误路径遵循相同规则。

---

## 3.49 Lifecycle Shutdown

```cpp
Status Shutdown(
    rocomm::Duration timeout) noexcept;
```

正常阶段：

```text
1. Running → Stopping linearization

2. reject new normal admission

3. wake Block producers

4. establish shutdown prefix

5. 等待所有成功 admission 的 operation 转交或 terminalize，
   drain/terminalize queue/dispatch 接管的 records

6. Backend::Flush()

7. Backend::Shutdown()

8. worker/control path exits

9. lifecycle → Stopped

10. 保存 final Status，shutdown completion → Complete
```

---

## 3.50 Shutdown admission cut

正确线性化点：

```text
Running → Stopping
```

如果 Log admission 先赢：

```text
record admitted
```

Shutdown 必须负责该 operation。

如果 Shutdown transition 先赢：

```text
新的 admission
→ reject
```

---

## 3.51 Shutdown 不修改 Category effective level

V1 Shutdown 只通过 Running → Stopping admission cut 关闭接受边界，不批量把 CategoryEntry effective level 改成 Off。

Category cache 始终表达已提交的 category 配置；停止状态由独立 lifecycle 表达。Enabled 看到非 Running 即返回 false；direct Log 按 §2.19 跳过停止后的 level filtering，到 admission 统一拒绝。

这样不会因为 shutdown cache 改写而把 lifecycle rejection 混入 category filtering。未来若希望加入等价优化，必须证明不改变 filtering / rejection counters / race 契约，不能直接恢复 Off 覆盖。

---

## 3.52 Shutdown 后 direct Log()

对有效、config-enabled Logger，以合法 severity 直接调用 Log，若提交路径观察到 Stopping/Stopped，则跳过 effective-level filtering，立即由 admission 拒绝：

```text
rejected_after_stop++
不复制 owned message
不构造 LogRecord
不调用 normal Backend
```

不要求 level 高于停止前的 category threshold；停止后的拒绝优先于 category filtering。caller 在进入 Log 前已经完成的参数表达式求值不受此保证影响。

与 Shutdown 并发时，仍可能在最后一次观察 Running 时因 level 被过滤；这属于 §2.19 的合法 filtering outcome。默认 / moved-from / config-disabled Logger 是 no-op，不计 rejection。

---

## 3.53 Shutdown 后 LogLazy / Macro

如果 LogLazy 开始执行时已经：

```text
lifecycle != Running
```

则：

```text
Enabled() == false

builder 不执行

不到 admission

rejected_after_stop 不增加
```

因此：

> **因 Enabled() 预检查直接被过滤的 LogLazy，不计入 `rejected_after_stop`。**

---

## 3.54 LogLazy 与 Shutdown 的 race

合法交错：

```text
Thread A: Enabled → true
Thread B: Running → Stopping
Thread A: builder 正常返回
Thread A: 内部提交观察非 Running → admission reject
```

该操作 builder 已执行，`rejected_after_stop++`，normal Backend 不被调用。

若 builder 抛异常 / 返回 null const char*，则先按 frontend failure 结束，不到 admission；若在仍 Running 时被并发 category 更新过滤，也不到 admission。只有真正到达 admission 且被 lifecycle 拒绝的操作增加 rejection counter。

---

## 3.55 `rejected_after_stop` 正式定义

表示到达 LoggingContext admission boundary、因 lifecycle 非 Running 被拒绝的 logging operation 数量。

包括有效且 config-enabled Logger 在停止后的合法 direct Log，以及 builder 正常返回后输掉 admission race 的 LogLazy。

不包括 Enabled 已拒绝的 lazy/macros、Running 时的 category filtering、default/moved-from/config-disabled Logger、admission 前 builder failure、非法 severity，以及 admission 已成功后发生的 frontend failure / overflow / Block shutdown cancellation。

每次 admission rejection 恰好计一次。

---

## 3.56 Shutdown completion 与 final Status

内部必须独立保存 lifecycle、shutdown completion（Pending / Complete）和不可变 final Status。

Complete 最低要求：

```text
所有 admission / queue / dispatch ownership 已终结
Backend::Flush 与 Backend::Shutdown 已按序返回（enabled Context）
所有 Logging-owned worker/control invocation 已退出或完成
没有仍需 LoggingState 支撑的内部工作
```

最终 Flush 失败后仍必须尝试 Backend::Shutdown，以释放资源；不得因 Flush 错误直接跳过 cleanup。

结果规则：

```text
Flush 非 Ok → final Status 为该 Flush 原 Status
否则 Shutdown 非 Ok → final Status 为该 Shutdown 原 Status
否则 → Ok
```

两个都失败时保留 Flush 为 primary，Shutdown 为 secondary，通过 BackendStats / best-effort Diagnostic 报告，不覆盖 primary。Diagnostic 不能引入 allocation failure。

Complete 只表示 quiescent，不保证 output 成功。所有 joiner、重复 Shutdown 和 join shutdown 的 Flush 观察同一 final Status；caller 的 Timeout 不覆盖最终结果。disabled Context 无 Backend，final Status 为 Ok。

---

## 3.57 Shutdown timeout

deadline 到达而 cleanup 未完成：

```text
lifecycle
→ Stopped

shutdown completion
→ Pending

Shutdown()
→ Timeout
```

logical Stopped 不表示 Backend 已经 quiescent。

---

## 3.58 Repeated Shutdown

Stopped + Complete 时立即返回保存的 shutdown final Status，可能是 Ok，也可能是 Backend control failure；不重新调用 Backend::Shutdown，也不把历史失败改成 Ok。

Stopped / Stopping + Pending 时 join 同一个 completion，每次从自己的调用开始重新计算 timeout。

```text
first Shutdown(100 ms) → Timeout
cleanup 随后成功
second Shutdown(5 s) → Ok

first Shutdown → Backend control failure，Complete
second Shutdown → 同一个 failure

first Shutdown → Timeout
second Shutdown 时仍 Pending 且本次 deadline 到达 → Timeout
```

每个 caller 的 Timeout 仅描述本次等待，不写入共享 final Status。

---

## 3.59 Concurrent Shutdown

多个 caller：

```text
只有一个 initiator
```

其余：

```text
join same shutdown completion
```

每个 caller 使用自己的 timeout。

不得多次执行：

```text
Backend::Shutdown()
```

---

## 3.60 Timeout 后 Backend lifetime

如果 Shutdown Timeout：

```text
LoggingContext wrapper
可以继续存在或销毁

lifecycle = Stopped
```

但：

```text
Backend

Category registry

Queue

worker/control state
```

可能仍被 in-flight execution 持有。

因此：

> **LoggingContext wrapper 析构不等于 Backend 立即析构。**

---

## 3.61 Self-owned LoggingState

概念 ownership：

```text
LoggingContext wrapper
      ↓ strong
LoggingState

Logger
      ↓ state handle
LoggingState

worker/control execution
      ↓ strong
LoggingState

LoggingState
      ↓ owns
Backend
Category registry
Queue
```

Shutdown transition 决定：

```text
是否继续接受 logging
```

引用计数只决定：

```text
内存何时可以安全回收
```

两者不能混淆。

---

## 3.62 Custom Backend 永久阻塞

如果 Custom Backend：

```cpp
Write()
```

永久不返回：

```text
shutdown completion
可能永远 Pending
```

Backend state 可能保留到进程结束。

这是 C++ 无法安全强制修复的边界。

因此 Backend contract：

> **Initialize/Write/Flush/Shutdown 不得无限阻塞。**

违反该要求属于 Backend contract violation。

RoComm 不使用：

```text
pthread_cancel

unsafe detach + free backing

TerminateThread
```

等方案强制回收。

---

## 3.63 SpdlogBackend 阻塞边界

SpdlogBackend 不引入：

```text
spdlog async pool wait

spdlog worker join
```

但底层：

```text
filesystem

terminal

OS write
```

仍可能异常长时间阻塞。

因此：

> **RoComm 不承诺普通文件/终端 I/O 的严格最大阻塞时长。**

---

## 3.64 LoggingContext destructor

```cpp
~LoggingContext() noexcept;
```

析构：

> **发起 safe shutdown initiation，但不执行可能无界阻塞的等待。**

不要求“绝对从不 join”。

如果 worker 已经：

```text
完全退出
```

可以同步 reclaim/join。

如果 worker/control invocation 仍未完成：

```text
self-owned state deferred cleanup
```

析构不能因为坏 Backend：

```text
无限卡住
```

---

## 3.65 完整 Flush guarantee

Destructor：

```text
不是 persistence guarantee
```

需要确认日志已经 flush 的调用方必须显式：

```cpp
Shutdown(timeout);
```

Runtime 自己必须在：

```text
Runtime::Stop()
```

中显式执行 Logging shutdown。

---

## 3.66 LoggingStats

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

## 3.67 Stats counter storage

Public semantic width：

```text
std::uint64_t
```

内部通常可使用：

```text
std::atomic<std::uint64_t>
```

但：

> **64-bit atomic 必须 lock-free 不是跨平台 Public API contract。**

在主目标平台应验证性能。

如果未来某个平台不是 lock-free：

```text
功能正确性仍必须成立

性能差异通过 benchmark / platform notes 暴露
```

---

## 3.68 `accepted`

accepted 统计完成 global filtering、owned frontend materialization 和 authoritative admission 后，成功转交给 dispatch/queue 的 record 数量。

精确计数点：

```text
Sync:
  record 转交给受 Shutdown 跟踪的 dispatch ownership 时 +1
  可以早于 Backend serialization / Write，Write 在 caller 执行

Async:
  record 成功取得 queue ownership 的线性化点 +1
```

每条 record 只计一次。DropNewest 未入队、frontend failure、Block shutdown cancellation、admission rejection 都不计 accepted。DropOldest 删除已 accepted 的旧记录时不回减 accepted，增加 dropped_oldest；新记录成功入队时 accepted +1。

admission ownership 负责 lifecycle / prefix correctness，accepted 只是 record 转交统计，不能用作 Flush 同步条件。admission 成功不立即增加 accepted。

---

## 3.69 `dispatched`

正式定义：

> **`Backend::Write(record)` 已经正常返回的记录数。**

不是：

```text
Write 刚开始调用的次数
```

即使 Backend 内部遇到 I/O failure，但：

```text
Write() 按 contract 正常返回
```

仍：

```text
dispatched++
```

同时：

```text
backend.write_errors++
```

---

## 3.70 Backlog diagnostic

```text
accepted - dispatched - dropped_oldest
```

在 counters 未溢出、工作静止且读取稳定快照后，等于仍被 queue / sync dispatch / in-flight Write ownership 持有的 record 数。全部 record 已终结时应为 0，包括 Backend Write 返回但报告 output error 的情况。

DropNewest、frontend failure 和 Block shutdown cancellation 不计 accepted，因此不会留下永久差额。

并发 GetStats 是 eventually-consistent snapshot，字段可能来自不同时刻；即时差值仅供诊断，读取方应避免 uint64 下溢（例如采用饱和差值），不得据此判断 Flush/Shutdown 完成。

---

## 3.71 queue_high_watermark

定义为：

```text
eventually consistent diagnostic statistic
```

V1 不要求 producer 每次 Push：

```text
atomic CAS max
```

也不向用户暴露：

```text
sampling interval configuration
```

具体采样方式：

```text
implementation-open
```

可以在 benchmark 后调整。

---

## 3.72 GetStats()

```cpp
LoggingStats
GetStats() const noexcept;
```

返回 eventually-consistent snapshot。

不要求所有字段属于同一个严格时间点。

---

# 4. Backend SPI、SpdlogBackend、TextFormatter 与 RawLog

## 4.1 LoggingBackend 是唯一 Public Backend SPI

V1 不同时暴露：

```text
Custom Backend SPI

+

Custom Sink SPI
```

唯一正式 extension point：

```text
LoggingBackend
```

Backend 内部可以自己建立：

```text
Sink

Output

Channel

Router
```

但这些不成为 RoComm public abstraction。

---

## 4.2 LoggingBackend

Public：

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

保持：

```cpp
Initialize() noexcept
```

无参数。

Backend creation-time dependencies 通过：

```text
LoggingBackendFactory capture
```

传入。

只有未来出现：

```text
factory 无法合理提供、
必须由 LoggingContext 在 initialization 时注入
```

的明确 dependency，才重新设计职责明确的 Init contract。

不提前增加通用：

```text
LoggingBackendInitContext
```

容器。

---

## 4.4 Backend lifecycle

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

Shutdown 必须能处理 Constructed、partial-initialized、Initialized 状态。第一次调用完成 cleanup 后进入 Stopped，即使返回非 Ok，也不得再接受 Write/Flush。重复 Shutdown 不再次执行 cleanup，返回保存的第一次 Shutdown Status（正常无资源 cleanup 为 Ok）。

LoggingContext 正常只调用一次 Backend::Shutdown；幂等要求用于 Backend contract tests 和安全 rollback。Backend 析构不得执行无界阻塞的隐式 cleanup，也不得抛异常。

---

## 4.5 Initialize 只能成功一次

第一次 Initialize 成功后进入 Initialized；第二次及后续 Initialize 返回 InvalidState，保持原 Initialized 状态与资源不变，Write/Flush/Shutdown 仍正常可用。

Initialize 不是幂等 API。Initialize 已失败或 Backend 已 Stopped 后不得重试初始化；后续 Initialize 返回 InvalidState。部分初始化失败仍必须支持安全 Shutdown 和析构。

---

## 4.6 Partial initialization

如果 Initialize 部分完成后失败，例如：

```text
file 已打开

但后续 output 创建失败
```

Backend 仍必须：

```text
可安全析构

可安全调用 Shutdown()
```

`Shutdown()` 必须能清理：

```text
partial initialization resources
```

不能假定 Initialize 成功完成。

---

## 4.7 Backend noexcept contract

以下全部：

```text
noexcept
```

```text
Initialize
Write
Flush
Shutdown
GetStats
```

如果第三方库内部抛异常：

### Initialize / Flush / Shutdown

捕获后按 §4.9 映射 Status（资源不足为 ResourceExhausted，无法分类的异常为 InternalError），并更新适用的对应 stats；Initialize 没有独立公共 error counter。返回错误不得依赖 Diagnostic allocation。

### Write

捕获后：

```text
write_errors++
```

必要时：

```text
RawLog
```

不能让异常穿过 Backend SPI。

---

## 4.8 Backend Write failure

`Write()` 返回：

```text
void
```

因为数据面日志输出失败：

> **不能反向改变原 communication operation 的 Status。**

例如：

```text
Publisher::Publish()
→ Ok

同时日志写文件失败
```

Publish 仍然：

```text
Ok
```

Logging failure 通过：

```text
LoggingStats

BackendStats

RawLog
```

观察。

---

## 4.9 Backend control Status

以下是 Logging control-plane：

```text
Initialize

Flush

Shutdown
```

因此返回：

```text
Status
```

映射：

```text
invalid backend configuration
→ InvalidArgument

unsupported capability
→ UnsupportedConfiguration

resource failure
→ ResourceExhausted

unexpected third-party/backend failure
→ InternalError
```

---

## 4.10 Backend 不处理 timeout

Backend SPI 不接受：

```text
Duration
deadline
```

timeout 全部由：

```text
LoggingContext
```

控制。

这样 Backend 不需要重复实现 deadline framework。

---

## 4.11 Backend serialized invocation

LoggingContext 保证同一个 Backend instance 的：

```text
Initialize

Write

Flush

Shutdown
```

不并发执行。

Backend author 因此不需要让：

```text
Write()
```

本身支持多线程并发。

---

## 4.12 Backend GetStats()

`GetStats()`：

```text
noexcept

不得分配内存

不得执行 I/O
```

Backend stats 应来自：

```text
atomic counters

或其他 no-allocation state
```

不得在 `GetStats()` 内动态构造需要 heap allocation 的诊断结构。

---

## 4.13 Backend 不由生产代码直接驱动

正常生产代码不得：

```cpp
backend->Write(...);
```

Backend lifecycle 调用权属于：

```text
LoggingContext
```

Backend interface 是：

```text
implementation extension contract
```

而不是普通业务 API。

---

## 4.14 Custom Backend 单元测试

测试代码允许：

```text
直接构造 Backend

直接调用 Initialize

构造测试 LogRecord

调用 Write

调用 Flush

调用 Shutdown
```

用于验证 Backend 自身。

LoggingContext integration test 必须：

```text
通过 LoggingContext 驱动 Backend
```

---

## 4.15 Backend Contract Test Harness

仓库提供非安装测试 helper：

```text
tests/support/logging/backend_contract.hpp
```

接口形状：

```cpp
void RunBackendContractTests(
    LoggingBackendFactory factory);
```

具体测试框架适配可以另包一层。

该 helper：

```text
不属于 installed Runtime API
```

---

## 4.16 LoggingBackendStats 语义

`write_calls`：

```text
Backend::Write() 被调用次数
```

`write_errors`：

```text
Write 内部遇到 output/rendering failure 次数
```

`flush_errors`：

```text
Backend::Flush() 返回非 Ok 次数
```

`shutdown_errors`：

```text
Backend::Shutdown() 返回非 Ok 次数
```

同一次 Write 可以：

```text
write_calls++
write_errors++
```

---

## 4.17 LoggingBackendStats 内部原子实现

Public：

```text
std::uint64_t
```

snapshot。

Backend 内部通常：

```text
std::atomic<std::uint64_t>
```

但：

> **是否 `is_always_lock_free` 不属于 Public contract。**

主目标平台通过 benchmark / CI 确认性能即可。

---

## 4.18 TextFormatter

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

## 4.19 TextFormatter output contract

每次 Format 开始：

```text
output.clear()
```

成功：

```text
return true

output
→ 一条完整 rendered log line
```

失败：

```text
return false
```

此时：

> **caller 必须完全忽略 output 内容。**

它可能：

```text
为空

或

含 partial intermediate data
```

但不能继续输出。

---

## 4.20 TextFormatter exception containment

Formatting 可能遇到：

```text
allocation failure

time conversion failure

unexpected formatting failure
```

必须：

```text
catch internally

return false
```

Backend：

```text
更新 error stats

RawLog best effort
```

不能抛异常。

---

## 4.21 Local time implementation

`LogTimeZone::Local`：

POSIX：

```text
localtime_r
```

Windows：

```text
localtime_s
```

禁止使用：

```text
localtime()
```

这种共享静态状态接口。

---

## 4.22 Default text format

推荐：

```text
2026-10-26T14:35:20.123456+08:00 [INFO] [rocomm.fastdds.discovery] [tid=18342] Participant matched [discovery.cpp:217]
```

V1 不把：

```text
精确空格数量

括号风格
```

冻结成兼容 ABI。

冻结的是：

```text
TextFormatConfig 启用字段
必须按配置出现在 rendered output
```

---

## 4.23 Output-specific filtering

每个 output 可以：

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

这是第二层 filtering。

它不能重新启用 global filtering 已经丢弃的 record。

---

## 4.24 SpdlogBackend

V1 production Backend：

```text
SpdlogBackend
```

但：

> **spdlog 是实现细节，不是 RoComm Logging API。**

Public Core header 不出现：

```text
spdlog::*

fmt::*
```

---

## 4.25 SpdlogBackend Public API

不公开具体 class。

只公开：

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

该 header：

```text
也不得 include spdlog headers
```

---

## 4.26 SpdlogBackend validation

`CreateBackend()` 验证：

```text
outputs 非空

File path 非空

Rotating path 非空

rotation size > 0

rotation archive count > 0

LogLevel values 合法
```

非法：

```text
InvalidArgument
```

资源创建失败：

```text
ResourceExhausted
或
InternalError
```

根据实际失败类型映射。

---

## 4.27 SpdlogBackend 内部约束

正式禁止：

```text
spdlog::async_logger

spdlog global logger registry

spdlog::register_logger()

spdlog::set_level()

spdlog::shutdown()

spdlog::init_thread_pool()

spdlog global async thread pool

其他 process-global mutable logging configuration
```

---

## 4.28 SpdlogBackend 使用 synchronous logger

内部使用：

```text
synchronous spdlog::logger
```

因为 RoComm 已经负责：

```text
queue

writer

overflow

flush

shutdown
```

---

## 4.29 Caller metadata

Async 时调用 spdlog 的线程：

```text
RoComm logging worker/control execution
```

不是原 caller。

因此 timestamp/thread/source 必须：

```text
在 caller 产生 LogRecord 时保存
```

不能由 spdlog output 时重新推导。

---

## 4.30 spdlog pattern

内部使用类似：

```text
%v
```

color：

```text
%^%v%$
```

只输出 RoComm TextFormatter 已生成的完整 line。

---

## 4.31 Spdlog error handler

必须使用 backend-local error handling。

发生第三方输出失败：

```text
write_errors++
```

必要时：

```text
throttled RawLog
```

不得调用：

```text
普通 RoComm Logger
```

否则可能 Backend recursion。

---

## 4.32 Spdlog symbol scan

Linux CI 对：

```text
rocomm_logging_spdlog
```

进行：

```text
nm
objdump
或等价工具
```

检查禁止 global API dependency。

至少覆盖：

```text
spdlog::shutdown

spdlog::register_logger

spdlog::set_level

spdlog::init_thread_pool
```

该检查属于：

```text
architecture regression guard
```

不能替代 runtime isolation test。

---

## 4.33 RawLog 定位

RawLog 用于：

```text
Logging 初始化失败

Backend recursion

Backend severe failure

frontend allocation failure

late teardown

user callable exception terminate path（仅限已证明 nonblocking 的 emergency 路径，见 §5.12）

internal invariant diagnostic（必须 fail-fast 时遵循 §5.12）
```

不是 Application 常规日志 API。

---

## 4.34 RawLog 约束

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

## 4.35 Linux RawLog

推荐：

```text
fixed stack buffer
↓
bounded snprintf-style formatting
↓
write(STDERR_FILENO, ...)
```

禁止：

```text
std::string

iostream

normal Logger

spdlog
```

---

## 4.36 EINTR

POSIX `write()` 返回：

```text
EINTR
```

时必须允许 retry。

但 retry：

> **必须有限。**

禁止：

```text
while (errno == EINTR) forever
```

exact retry count / time budget：

```text
implementation constant
```

不进入 Public API。

首版实现可以采用小的固定次数。

持续失败后：

```text
best-effort abandon
```

不能 panic 或递归报告。

---

## 4.37 RawLog portable boundary

不同平台可以使用不同：

```text
low-level stderr writer
```

V1 不宣称 RawLog：

```text
完整 async-signal-safe
```

只冻结：

> **RawLog 比正常 pipeline 更低层，并能在普通 Logging 已不可用时 best-effort 输出。**

普通 RawLog 的 OS write 仍可能阻塞；固定 buffer 和有限 EINTR retry 只限制格式化空间与重试次数，不保证总调用时间。fail-fast 不得依赖这个可能阻塞的路径，见 §5.12。

---

## 4.38 Backend recursion

Backend invocation thread：

```text
BackendInvocationGuard active
```

Backend 再调用 Logger：

```text
normal pipeline 不递归

RawLog fallback

recursive_fallbacks++
```

禁止：

```text
重新 enqueue

重新调用同一 Backend

Block queue
```

---

## 4.39 Frontend allocation failure

Logger message materialization：

```text
可能 bad_alloc
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
RawLog minimal fallback
```

Trace / Debug / Info / Warn：

```text
safe drop
```

不得传播异常给通信 Runtime。

---

# 5. Runtime 集成、工程结构与 V1/V2 演进

## 5.1 Runtime ownership

```text
RuntimeState
    ↓ owns
LoggingContext
    ↓ owns/controls
LoggingState
    ├── lifecycle/admission state
    ├── CategoryLevels
    ├── CategoryEntry registry
    ├── AsyncQueue
    ├── worker/control state
    └── LoggingBackend
```

Logger 和 worker/control execution 可以继续：

```text
保持 LoggingState lifetime
```

但不能改变其 lifecycle。

即：

> **引用可以延长内存寿命，但不能延长 Running 生命周期。**

---

## 5.2 Middleware integration

Middleware 应在：

```text
Initialize
```

阶段取得其整个生命周期所需要的 Logger。

例如：

```cpp
auto logger_result =
    logging_context.GetLogger(
        "rocomm.fastdds");
```

成功后：

```text
Middleware 保存 Logger
```

---

## 5.3 Middleware shutdown 不再 GetLogger

Middleware shutdown 阶段：

```text
不得临时向 LoggingContext 请求新的 Logger
```

而应复用：

```text
Initialize 阶段已经持有的 Logger
```

Runtime Stop 顺序确保：

```text
Middleware Shutdown
发生在
LoggingContext Shutdown
之前
```

所以已持有 Logger 正常可用。

---

## 5.4 Middleware 权限边界

Middleware 只能：

```text
使用 Logger
```

不能：

```text
更换 Backend

修改 Queue

关闭 LoggingContext

管理 Logging worker

执行 Runtime-wide Logging shutdown
```

---

## 5.5 Runtime Create 顺序

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

因此：

```text
Executor initialization

Middleware initialization

Route/type initialization
```

都可以记录正常日志。

---

## 5.6 Logging 初始化失败

用户显式配置 Backend：

```text
Custom Backend

Spdlog file Backend
```

初始化失败：

```text
Runtime::Create()
→ failure
```

不得：

```text
silent fallback to console

silent fallback to Noop
```

显式用户意图必须被尊重。

---

## 5.7 Runtime Stop 顺序

```text
1. reject new communication operation

2. stop new receive dispatch

3. terminalize pending communication operation

4. stop/drain user execution

5. shutdown Middleware

6. complete Executor shutdown

7. 始终发起 LoggingContext::Shutdown(
       remaining deadline，包括 remaining=0)

8. Runtime logical Stopped
```

---

## 5.8 Middleware / Executor shutdown logging

正式冻结：

> **Middleware 和 Executor shutdown 期间 LoggingContext 仍处于正常 Running 状态。**

因此：

```text
shutdown warning

resource cleanup error

Middleware stop diagnostics
```

仍进入普通 pipeline。

---

## 5.9 Shutdown 阶段大量日志

Middleware/Executor shutdown 产生大量日志时：

```text
仍按正常 queue capacity 和 overflow policy
```

处理。

不会自动：

```text
扩大 queue

切换 Block

修改 overflow policy
```

如果 Error/Critical 因 overflow 被 DropNewest 或 DropOldest：

```text
RawLog fallback
```

仍然生效。

---

## 5.10 Runtime timeout 下 late Logger

如果 Runtime shutdown 已超时，而仍有 in-flight user callable：

```text
LoggingContext
可能已经 Stopping / Stopped
```

已有 Logger：

```text
Enabled() → false
```

direct `Log()`：

```text
有效且 config-enabled Logger，合法 severity
→ 跳过停止后的 level filtering，最终 admission reject
→ safe no-op from Backend perspective
```

这些 late normal logs：

```text
不保证持久化
```

Fatal/invariant 若必须 fail-fast，遵循 §5.12：仅可选使用已证明 nonblocking 的 emergency diagnostic，不能等待普通 RawLog。其他 late diagnostics 可使用普通 RawLog best effort 输出。

---

## 5.11 Runtime Stop 与 Logging timeout

Logging shutdown 未在剩余 Runtime deadline 内 quiesce：

```text
Runtime::Stop()
→ Timeout
```

Diagnostic 可包含：

```text
logging shutdown did not reach quiescence
```

Runtime：

```text
logical Stopped
```

Logging backing：

```text
允许 deferred cleanup
```

即使此前 Middleware/Executor cleanup 已耗尽 deadline，Runtime 仍须无等待地发起 Logging shutdown，关闭新 admission；不能因无剩余时间而让 Logging 永久 Running。在 deadline 内完成时，若无更早的 Runtime cleanup error，Stop 传播 Logging final Status；已有 primary error 时保留它，将 Logging failure 作为 secondary diagnostic。未 quiescent 时返回 Timeout。Diagnostic allocation 失败可省略文字，不得把错误改成 Ok；本次 Timeout 不覆盖 Logging 保存的最终结果。

---

## 5.12 User callable exception

Subscriber callback、Server Handler、ResponseHandler、Future continuation 抛异常时，必须执行 fail-fast；日志输出不能成为到达 `std::terminate()` 的前置阻塞条件。

该路径不调用普通 Logger（包括 Critical），不等待 Backend、queue capacity、Flush 或 Shutdown。Sync Logger 与 Async + Block 都可能无限等待，Enabled 不能证明它们适合 fail-fast。

可以尝试固定大小、无 allocation、无普通 logging mutex 的 emergency diagnostic，但仅限目标平台已经证明不会阻塞的输出路径。不允许修改进程级 stderr flags 来临时制造 nonblocking；若无法保证本次 emergency write 不阻塞，则省略输出并直接 terminate。

```text
user callable exception
↓
optional proven nonblocking emergency diagnostic
↓
std::terminate()
```

普通 RawLog 的 EINTR retry 有界，不代表单次 OS write 有界，因此不能无条件复用为该路径。此要求不把普通 Logger / RawLog 宣称为 hard-real-time safe。

---

## 5.13 Public files

```text
include/rocomm/logging/
├── log_level.hpp
├── source_location.hpp
├── log_record.hpp
├── logger.hpp
├── logging_backend.hpp
├── logging_config.hpp
├── logging_context.hpp
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

## 5.14 Public dependency boundary

Core installed headers：

```text
不得 include spdlog

不得 include fmt

不得 include Quill

不得 include Boost.Log
```

`backends/spdlog.hpp`：

```text
也不得暴露 spdlog 类型
```

未来：

```text
backends/native.hpp
```

同样只能暴露 RoComm-owned types，不得引入不必要第三方 public dependency。

---

## 5.15 Private source layout

```text
src/logging/
├── logging_context.cpp
├── logging_state.hpp
│
├── category_levels.hpp
├── category_levels.cpp
│
├── admission.hpp
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

这里的：

```text
admission.hpp
```

只是一个可能的职责文件名。

实现也可以把 admission state 放入：

```text
logging_state.hpp
```

只要不因此创建不必要的小文件。

不使用：

```text
src/logging/detail/

src/logging/internal/

src/logging/impl/
```

---

## 5.16 CMake targets

```text
rocomm::logging

rocomm::logging_spdlog
```

`rocomm::logging`：

```text
零第三方 logging dependency
```

包含：

```text
Logger

LoggingContext

Category

Admission

Queue

Backend SPI

RawLog

TextFormatter
```

`rocomm::logging_spdlog`：

```text
包含：与 rocomm::logging 相同的 Core source + SpdlogBackend source
依赖：rocomm_core + 固定版本 spdlog
```

这是选择 SpdlogBackend 作为默认 Backend 的**完整静态库变体**，不是同时链接
`rocomm::logging` 和 `rocomm::logging_spdlog` 的增量插件。由于 V1 的默认
Backend factory 在 `LoggingContext::create()` 中按构建策略静态选择，两个
target 都包含同一份 LoggingContext/Core 实现源码，但各自只编译和链接自身所需
的 Backend 依赖。最终 binary **只能选择其中一个**，不得同时链接两个变体导致
重复符号。它们的 Public API、Logging 语义与 Backend SPI 完全相同。

这种编译期互斥实现避免了全局可变 Backend registry，也确保纯
`rocomm::logging` 二进制完全不依赖 spdlog。将来若引入新的默认 Backend，
应重新审查构建策略，不能依赖隐式全局注册。

---

## 5.17 CMake options

```text
ROCOMM_BUILD_LOGGING

ROCOMM_BUILD_LOGGING_SPDLOG

ROCOMM_LOGGING_COMPILED_LEVEL
```

启用 `ROCOMM_BUILD_LOGGING_SPDLOG` 时，CMake 只使用固定在
`third_party/spdlog` 的 Git submodule，不依赖或静默探测系统安装包。首次 clone
仓库后必须执行：

```bash
git submodule update --init --recursive
```

feature：

```text
ROCOMM_HAS_SPDLOG_BACKEND
```

V1 不预定义：

```text
ROCOMM_HAS_NATIVE_BACKEND
```

直到真正实现 NativeBackend。

---

## 5.18 Default Backend 是 build policy

V1 default 可以：

```text
SpdlogBackend
```

V2 可以：

```text
NativeBackend
```

但：

> **default Backend identity 不属于稳定 Public API semantic contract。**

需要精确选择的用户必须使用：

```text
SetBackendFactory()
```

---

## 5.19 V2 NativeBackend

未来新增：

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

## 5.20 NativeBackend 复用 V1 Runtime

V2 必须直接复用：

```text
LoggingContext

Admission semantics

CategoryLevels

AsyncQueue

Flush

Shutdown

Timeout

RawLog

TextFormatter

TextBackendConfig

LoggingBackend SPI

Backend Contract Tests
```

禁止创建：

```text
NativeLogger

NativeLoggingContext

NativeAsyncQueue
```

第二套 logging runtime。

---

## 5.21 V2 与 TextBackendConfig

V1 冻结：

```text
TextFormatConfig

ConsoleOutputConfig

FileOutputConfig

RotatingFileOutputConfig

TextBackendConfig
```

V2 不能为了 NativeBackend 实现方便修改 V1 语义。

如果 NativeBackend 需要：

```text
fsync mode

native file permissions

console native flags

特殊 rotation strategy
```

使用：

```cpp
struct NativeBackendConfig {
    TextBackendConfig text;

    // Native-specific fields
};
```

原则：

> **NativeBackend 适配 V1 Text contract，而不是修改 V1 contract 迁就 NativeBackend。**

---

## 5.22 TextBackendConfig 的 V2 完整性边界

V1 TextBackendConfig 只表达：

```text
通用文本输出需求
```

并不尝试提前包含：

```text
fsync

file permission

platform console mode

平台特有文件打开 flags
```

这些本就属于 Backend-specific configuration。

因此其当前字段已经足以作为：

```text
V1/V2 共用通用语义层
```

未来缺失的 Native-specific 字段：

```text
不得反向加入 V1 common config
```

除非它事实上是所有 Text Backend 都具有的通用语义，并通过新的版本化设计评审。

---

## 5.23 Quill / deferred formatting future note

V1 LogRecord 是：

```text
owned message model
```

未来如果 profiling 证明：

```text
caller-side message construction
```

是重大瓶颈，可以新增：

```text
EncodedLogRecord

DeferredFormattingFrontend
```

等新 primitive。

原则：

> **未来 deferred formatting 不通过破坏 V1 LogRecord 语义实现。**

V1 LogRecord 继续保持其已冻结的 owned-message contract。

---

## 5.24 Implementation phases

### L0 — Public Header Finalization

本文件已经冻结语义和 Header 形状。

实现以下 headers：

```text
LogLevel

SourceLocation

LogRecord

Logger

LoggingBackend
LoggingBackendFactory

LoggingConfig

LoggingContext

LoggingStats

TextFormatConfig
TextBackendConfig

Macros
```

---

### L1 — Backend-independent Core

实现：

```text
LoggingState

Admission gate/state

CategoryLevels

CategoryEntry registry

Logger

LogLazy

AsyncQueue

sync dispatch

async writer

control-operation execution

BackendInvocationGuard

RawFallbackGuard

Flush prefix semantics

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

Admission/Shutdown race

Backend SPI

Flush

Shutdown

Timeout

deferred lifetime

recursion
```

证明 architecture 不被 spdlog 反向塑造。

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

spdlog-local error handler

global API isolation
```

---

### L4 — Runtime Integration

接入：

```text
RuntimeConfig::SetLogging()

Runtime::Create()

Middleware initialization

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

admission/shutdown race

shutdown race

multi-Runtime tests

symbol scan

benchmarks
```

---

# 6. 验证标准、性能契约与冻结边界

## 6.1 Public Header Dependency Test

独立 translation unit：

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

的环境必须成功编译。

这是 Backend independence 的硬验收条件。

---

## 6.2 Default Logger

测试：

```text
Logger{}
→ Enabled false

Log()
→ no normal Backend operation

LogLazy()
→ builder not evaluated

category()
→ empty
```

---

## 6.3 Lazy evaluation

Runtime disabled：

```text
builder count == 0
```

Enabled：

```text
builder exactly once
```

---

## 6.4 String literal LogLazy

验证：

```cpp
ROCOMM_LOG_INFO(
    logger,
    "started");
```

正常编译并输出。

证明：

```text
decay-based trait
```

正确支持 string literal。

---

## 6.5 `std::string` move path

Builder：

```cpp
[]() -> std::string {
    return BuildLargeString();
}
```

验证实现不会无意义：

```text
额外 copy owned message
```

可通过 instrumentation / move-counting test 检查。

---

## 6.6 Null `const char*`

Builder 返回：

```text
static_cast<const char*>(nullptr)
```

要求：

```text
no UB

frontend_failures++

Error/Critical → RawLog

lower level → safe drop
```

---

## 6.7 Logger copy

验证：

```text
copy O(1)

copy no heap allocation

move O(1)

move no heap allocation
```

测试可使用：

```text
operator new counter

或 test allocation hook
```

---

## 6.8 Enabled hot path

Benchmark / instrument：

```text
no ordinary mutex

no heap allocation

no Backend I/O

O(1)
```

允许：

```text
lifecycle scalar atomic read

effective-level scalar atomic read
```

---

## 6.9 Enabled 是 advisory test

用确定性同步构造 Enabled true → Shutdown cut → 正常 builder 返回 / 合法 direct submission。验证 admission reject、rejected_after_stop 恰好 +1、无 Backend Write。

另测 builder failure 与 Running 时 category filtering：它们在 admission 前结束，不增加 rejected_after_stop。

---

## 6.10 Post-stop Logger

已有 Logger：

```text
Context Shutdown
↓
Enabled false
```

direct `Log()`：

```text
最终 admission reject
```

不得：

```text
UAF

dangling CategoryEntry

Backend Write
```

---

## 6.11 Compile-time filtering

构建：

```text
ROCOMM_LOGGING_COMPILED_LEVEL
=
ROCOMM_LOG_LEVEL_INFO
```

验证：

```text
TRACE:
logger_expr 不执行
message_expr 不执行

DEBUG:
logger_expr 不执行
message_expr 不执行

INFO:
正常执行
```

---

## 6.12 Category grammar

合法：

```text
rocomm

rocomm.fastdds

robot.control
```

非法：

```text
""

".rocomm"

"rocomm."

"rocomm..fastdds"
```

---

## 6.13 Category case sensitivity

验证：

```text
robot.Control

!=

robot.control
```

---

## 6.14 Category hierarchy

覆盖：

```text
default

exact match

parent match

longest segment prefix

plain string prefix must not match
```

---

## 6.15 GetLogger

覆盖：

```text
valid category
→ success

invalid category
→ InvalidArgument

allocation failure
→ ResourceExhausted

Context Stopping/Stopped
→ RuntimeStopped

moved-from Context
→ InvalidState

enabled=false but Running
→ success disabled Logger
```

---

## 6.16 Category lifetime after wrapper destruction

构造：

```text
queued record

Shutdown Timeout

LoggingContext wrapper destroyed

all Logger wrappers destroyed

worker still alive
```

Backend 后续：

```cpp
record.category()
```

仍必须合法。

ASAN 验证。

---

## 6.17 LoggingConfig category accumulation

```cpp
config
    .SetCategoryLevel(
        "rocomm.fastdds",
        LogLevel::Debug)
    .SetCategoryLevel(
        "rocomm.service",
        LogLevel::Warn);
```

验证：

```text
两个 category 都存在
```

不能第二次操作覆盖整个 override 集合。

---

## 6.18 LoggingConfig duplicate category

```cpp
config
    .SetCategoryLevel(
        "rocomm.fastdds",
        LogLevel::Info)
    .SetCategoryLevel(
        "rocomm.fastdds",
        LogLevel::Trace);
```

最终：

```text
rocomm.fastdds → Trace
```

验证 last-write-wins。

---

## 6.19 Dynamic Category update

覆盖：

```text
SetDefaultLevel

SetCategoryLevel

ClearCategoryLevel

existing Logger eventually updates

new Logger sees new level
```

---

## 6.20 Runtime duplicate SetCategoryLevel

```text
same category
set A
set B
```

最终：

```text
B
```

与 LoggingConfig 行为一致。

---

## 6.21 Dynamic update allocation failure

fault injection：

```text
SetCategoryLevel
```

在 commit 前 allocation 失败：

```text
ResourceExhausted
```

要求：

```text
CategoryLevels unchanged

existing Logger cached levels unchanged
```

---

## 6.22 SetDefaultLevel strong failure guarantee

同样 fault injection 验证：

```text
失败时原 default level 保持
```

不得半更新。

---

## 6.23 Concurrent category update

Producer threads：

```text
Logging
```

Control thread：

```text
SetCategoryLevel
```

要求：

```text
no data race

TSAN pass

Logger eventually converges
```

不要求：

```text
global instantaneous cutover
```

---

## 6.24 LoggingConfig SetBackendFactory strong exception guarantee

构造一个 copy 时抛异常的 test callable。

验证：

```text
new factory construction fails
→ old factory remains
```

---

## 6.25 LoggingConfig move noexcept

编译期验证 LoggingConfig 公共 move traits，并验证未被外层显式 noexcept 掩盖的实际 storage / 各成员 move traits。

增加 compile-fail fixture：向被检查的 storage 注入 noexcept(false) move 成员，storage 断言必须失败。不能仅断言一个已经显式 noexcept 的 wrapper。

审查自定义 move body 不包含 allocation 或潜在 throwing 调用。

---

## 6.26 Queue capacity

验证：

```text
capacity = 0
→ InvalidArgument
```

以及：

```text
capacity = SIZE_MAX
```

如果 sizing overflow / allocation impossible：

```text
ResourceExhausted
```

不得 overflow 成小 allocation。

---

## 6.27 Logging disabled

enabled=false 验证 BackendFactory 不执行、queue/writer/control runner absent、Context Running、GetLogger success disabled、Flush/Shutdown Ok。

对 Running Context 调用 SetDefaultLevel / SetCategoryLevel / ClearCategoryLevel 后，Logger 仍 disabled、LogLazy builder 不执行；停止后 Set* 返回 RuntimeStopped。

停止前后 direct Log / LogLazy、递归环境中的 disabled Logger 均 safe no-op，全部数据面与 Backend counters 为零。

---

## 6.28 Empty outputs

```text
TextBackendConfig.outputs.empty()
```

创建 SpdlogBackend：

```text
InvalidArgument
```

不得隐式构造 NullBackend。

---

## 6.29 Rotation validation

覆盖：

```text
path empty

size == 0

archive count == 0
```

均：

```text
InvalidArgument
```

---

## 6.30 BackendFactory

覆盖：

```text
success

failure Result

success + nullptr

throws bad_alloc

throws std::runtime_error

throws unknown
```

期望：

```text
failure Result
→ original Status

success + nullptr
→ InternalError

bad_alloc
→ ResourceExhausted

other exception
→ InternalError
```

---

## 6.31 Create rollback

分别在：

```text
Backend Initialize

Queue allocation

writer startup

control execution setup
```

注入失败。

验证：

```text
partial resources cleaned

Backend partial Shutdown safe

no leaked joinable thread

no half-created Context
```

---

## 6.32 Rollback secondary failure

让 primary failure 后：

```text
Backend::Shutdown()
```

也失败。

要求：

```text
Create 返回 original primary Status

secondary rollback error
→ RawLog best effort
```

---

## 6.33 Admission basic race test

构造：

```text
Thread A:
direct Log reaches admission

Thread B:
Shutdown attempts Running→Stopping
```

验证只有两种合法结果：

```text
A admission wins
→ record belongs to shutdown responsibility

B Shutdown wins
→ admission rejects
```

不得出现第三种：

```text
record 被认为 accepted
但 Shutdown 完全遗漏它
```

---

## 6.34 Admission ownership failure-release test

确定性暂停在 admission 成功后、owned message / record 构造前，分别注入 materialization / construction failure；也覆盖 queue 转交前 failure 与 Block shutdown cancellation。

同时建立 Flush 或发起 Shutdown，验证每次 admission 恰好一次转交或终结、prefix completion 无洞、Shutdown 不永久等待、未转交的 record 不计 accepted。

LogLazy 的 builder exception 在 admission 前，必须另测，不伪装成 admission 后 allocation failure。

---

## 6.35 AsyncQueue basic

覆盖：

```text
empty

single producer

multiple producers

single consumer

wrap-around

bounded capacity

writer wakeup
```

---

## 6.36 DropNewest

queue full：

```text
old queue unchanged

new record dropped

dropped_newest++
```

---

## 6.37 DropNewest Error/Critical

新 Error/Critical 被丢：

```text
dropped_newest++

RawLog fallback
```

---

## 6.38 DropOldest

queue full：

```text
oldest removed

new record accepted

dropped_oldest++
```

---

## 6.39 DropOldest Error/Critical

被移除 oldest 为 Error/Critical：

```text
dropped_oldest++

RawLog fallback
```

并确认：

```text
仍然是 oldest 被删除
```

---

## 6.40 Block

queue full：

```text
producer waits
```

consumer 腾出 slot：

```text
producer resumes
```

---

## 6.41 Block + Shutdown

固定 queue 满且 producer 已成功 admission、尚未取得 queue ownership。Shutdown 唤醒 producer，验证取消入队、内部 terminalization 与 ownership release。

该 producer 不增加 accepted / rejected_after_stop / dropped_newest / dropped_oldest / frontend_failures；Error/Critical 执行 best-effort RawLog fallback。此前已入队 record 继续 drain。

建立覆盖该 admission 的 Flush，验证取消推进 prefix completion，不 deadlock。

---

## 6.42 Sync serialization

多个 caller：

```text
同时 Log()
```

验证：

```text
Backend::Write 不并发

Backend invocation serialized
```

---

## 6.43 Sync slow Backend propagation

一个 Sync `Write()` 人为长时间阻塞。

验证：

```text
当前 caller 被阻塞

其他 Sync caller
在 backend serialization 上阻塞
```

与文档契约一致。

---

## 6.44 Async ordering

单 producer：

```text
A
B
C
```

正常：

```text
A
B
C
```

除非明确 overflow。

多 producer：

```text
只验证 queue linearization
```

不验证 timestamp total ordering。

---

## 6.45 Recursion — Initialize

Backend::Initialize 内调用 Logger：

```text
RawLog fallback

normal pipeline not entered
```

---

## 6.46 Recursion — Write

Backend::Write 内调用 Logger：

```text
RawLog

no re-enqueue

no deadlock
```

---

## 6.47 Recursion — Flush / Shutdown

同样验证：

```text
BackendInvocationGuard
```

覆盖这两个 callback。

---

## 6.48 Nested Raw fallback

recursive `LogLazy()` builder 再次调用 Logger。

要求：

```text
RawFallbackGuard
→ suppress nested recursion
```

不能 stack overflow。

---

## 6.49 Flush basic barrier

构造 R1/R2 → Flush boundary → R3。验证 R1/R2 达到终态且 Backend Flush 返回后才能完成；R3 不属于等待要求。

另外暂停 boundary 前已 admission、尚未构造 record 的 producer，分别让它成功、frontend failure、Block shutdown cancellation，验证 barrier 正确推进。

Backend Flush 返回非 Ok 时，Context Flush 传播原 Status，不能返回 Ok。

---

## 6.50 Flush + concurrent producer

验证 prefix boundary 在并发 producer 下仍可线性化。

---

## 6.51 Flush + DropOldest

让 barrier 前某些 accepted record：

```text
随后被 DropOldest terminalize
```

Flush 仍必须正确完成。

不能由于：

```text
completion hole / count mismatch
```

永久等待。

---

## 6.52 Flush control path queue-full independence

让 normal queue 满。

调用 Flush：

```text
Flush control progress
仍能建立
```

证明 control signaling：

```text
不消耗普通 LogRecord capacity
```

---

## 6.53 Flush timeout

同时覆盖负 timeout InvalidArgument、零 timeout initiation/poll、超大 duration 饱和、单调时钟 deadline；Shutdown(0) 必须关闭 admission 并让 cleanup 继续推进。control/admission mutex 不得跨 RawLog 或 Backend I/O 持有，阻塞 emergency output 时其他 caller 仍能按 deadline 返回。

BlockingBackend::Flush：

```text
阻塞超过 timeout
```

要求：

```text
caller 按 timeout 返回

Status == Timeout

control state remains safe
```

---

## 6.54 Flush timeout 包含 backend serialization wait

让另一个 Sync Write 持有 Backend serialization。

随后调用 Flush。

如果等待 serialization 本身超过 timeout：

```text
Flush → Timeout
```

不能在拿到 mutex 后重新开始完整 timeout。

---

## 6.55 Sync Flush timeout

Synchronous mode：

```text
Backend::Flush 永久阻塞
```

调用：

```text
Flush(short timeout)
```

caller 必须：

```text
按 deadline 返回
```

证明 control operation 不直接不可控地运行在 caller thread。

---

## 6.56 Control execution creation failure

模拟 Flush control state / thread resource creation failure：返回 ResourceExhausted 或无法分类时 InternalError，无 terminate、无遗留 barrier，随后 Flush 和 Shutdown 能正常完成。

Create 阶段对 shutdown completion / guaranteed cleanup execution resource preparation 注入失败，验证 Create rollback。

Create 成功后禁止后续 allocation / thread creation，验证 Shutdown initiation、wrapper 析构和 move assignment 的旧 state release 仍能关闭 admission 并推进 cleanup；不能留下没有执行者的 Pending。

---

## 6.57 Normal Shutdown

验证：

```text
Running
→ Stopping
→ Stopped
→ completion Complete
```

结果：

```text
Ok
```

---

## 6.58 Shutdown admission race

并发：

```text
Log admission
vs
Running→Stopping
```

必须：

```text
Log wins
→ admitted and owned by shutdown

Shutdown wins
→ rejected
```

不得出现：

```text
admission success
但 shutdown 不知道该 operation
```

---

## 6.59 Shutdown 与 Category cache

设置非 Off category threshold，执行 Shutdown，验证 category effective-level cache 不因停止被覆盖成 Off，Enabled 仍因 lifecycle 返回 false。

确定性交错 direct Log 的 Running load、level load、lifecycle 复查和 shutdown cut：Running 时合法过滤不计 rejection；观察到非 Running 则到 admission 拒绝。

这一测试不依赖 O(n) Off 更新窗口。

---

## 6.60 rejected_after_stop — direct Log

先完成 shutdown cut，再对有效且 config-enabled Logger，以合法 severity direct Log。分别使用原 category threshold 允许和不允许的 severity。

均验证：跳过 level filtering、admission rejects、rejected_after_stop 恰好 +1、accepted 不变、没有 owned message allocation / LogRecord construction / Backend Write。

分别覆盖 Stopping、Timeout 后 logical Stopped、Complete 后 Stopped。default / config-disabled Logger 不计 rejection。

---

## 6.61 rejected_after_stop — LogLazy already sees Stopping

先进入：

```text
Stopping
```

再执行：

```cpp
ROCOMM_LOG_INFO(
    logger,
    side_effect());
```

要求：

```text
Enabled() == false

side_effect() 不执行

不到 admission

rejected_after_stop 不增加
```

---

## 6.62 rejected_after_stop — LogLazy race

用测试同步点确保 Enabled 已返回 true，再让 Shutdown 完成 admission cut，随后恢复 builder 正常返回和内部提交。

验证 builder 恰好一次、admission rejection 恰好 +1、accepted 不变、Backend Write 不调用。不能在 LogLazy 之前做一次无关 Enabled 调用代替实际 race。

另测 builder 抛异常 / 返回 null：frontend_failures +1，不到 admission，rejected_after_stop 不变。

---

## 6.63 Repeated Shutdown / control final Status

覆盖：

```text
first Ok → second Ok
first Timeout → cleanup 成功 → second Ok
first Timeout → cleanup 仍 Pending → second 按自身 deadline Timeout
Backend final Flush failure → 仍调用 Backend Shutdown → 保存 Flush failure
Backend Shutdown failure → 保存 Shutdown failure
两者都失败 → Flush primary，Shutdown secondary
Complete failure → repeated Shutdown 返回相同 failure，不再调用 Backend
Flush 在 Stopping / Stopped join → 返回相同 final Status 或本次 Timeout
```

并发 joiners 使用各自 deadline，Timeout 不覆盖共享 final Status。

---

## 6.64 Concurrent Shutdown

还必须交错 timed-out Flush、pending per-operation control execution 与 Shutdown；验证旧 Flush 先完成或合并到 shutdown，Backend::Shutdown 开始后无 Write/Flush，Complete 后无遗留 control invocation。

验证：

```text
single initiator

others join same completion

Backend::Shutdown called once
```

---

## 6.65 Shutdown timeout lifetime

BlockingBackend：

```text
Write blocked
```

Shutdown：

```text
Timeout
```

随后：

```text
destroy LoggingContext wrapper
destroy Logger wrappers
```

要求：

```text
Backend alive

Category storage alive

no UAF
```

Backend 最终返回后：

```text
deferred state cleanup
```

正常完成。

---

## 6.66 Permanent blocking Backend

通过子进程或隔离测试进程构造永久阻塞 Backend。

验证：

```text
Shutdown caller 能 Timeout

LoggingContext wrapper 可以析构

没有 joinable std::thread destructor terminate

没有 UAF
```

测试进程本身通过 harness timeout / `_exit` 等受控手段结束。

---

## 6.67 Moved-from LoggingContext

验证：

```text
Shutdown → Ok

Flush → InvalidState

GetLogger → InvalidState

Set* → InvalidState

GetStats → zero
```

并确认其原因分别是：

```text
Shutdown
→ release operation

Flush/GetLogger/Set*
→ state-dependent control operation

GetStats
→ diagnostic query
```

---

## 6.68 Backend Contract Test

所有 Backend 必须统一验证：

```text
Name non-empty

Initialize success

second Initialize → InvalidState，原 Initialized 状态不变

second Initialize 后 Write/Flush/Shutdown 仍可用

Initialize partial failure

partial Shutdown cleanup

Write noexcept

Write call accounting

Write failure isolation

Flush

Flush failure

Shutdown

Shutdown idempotence：重复返回第一次结果，不重复 cleanup

Shutdown failure 后仍为 Stopped，不再 Write/Flush

GetStats no allocation

Stats concurrent read

borrowed LogRecord lifetime
```

---

## 6.69 Backend `GetStats()` no-allocation

使用 allocation instrumentation：

```text
GetStats()
```

不得分配 heap。

---

## 6.70 TextFormatter

覆盖：

```text
timestamp on/off

level on/off

category on/off

thread ID on/off

source on/off

function on/off

UTC

Local
```

---

## 6.71 Local time concurrency

并发多个 formatter：

```text
Local timezone
```

验证无：

```text
localtime shared-state race
```

TSAN pass。

---

## 6.72 TextFormatter output clearing

输入：

```text
output = "old-content"
```

Format 成功后：

```text
output 只包含新日志
```

Format 失败：

```text
caller ignores output
```

---

## 6.73 SpdlogBackend

覆盖：

```text
synchronous logger

no async_logger

console

file

rotation

per-output threshold

caller timestamp retained

caller thread ID retained

caller source retained
```

---

## 6.74 Spdlog global isolation

验证：

```text
no global registry

no global set_level

no global shutdown

no global async pool

no init_thread_pool
```

---

## 6.75 Multi-SpdlogBackend isolation

创建：

```text
Context A

Context B
```

不同 Backend/config。

验证：

```text
A shutdown 不影响 B

A error handler 不影响 B

A category config 不影响 B
```

---

## 6.76 Spdlog symbol scan

Linux CI 对 Backend object/library 执行符号扫描。

禁止出现预定义 global API dependency。

---

## 6.77 RawLog basic

覆盖：

```text
normal stderr

long message truncation

logging unavailable

Backend recursion

late teardown
```

---

## 6.78 RawLog EINTR

通过 syscall shim / test wrapper 模拟连续：

```text
EINTR
```

验证：

```text
有限 retry

最终不会 infinite loop
```

---

## 6.79 RawLog no allocation

支持平台通过：

```text
operator new instrumentation
```

验证：

```text
RawLog
→ zero heap allocation
```

---

## 6.80 Frontend allocation failure

还必须在完全 quiescent 后验证 accepted - dispatched - dropped_oldest == 0，覆盖 frontend failure、DropNewest 与 Block shutdown cancellation。错误 Status 的 Diagnostic allocation 再次失败时必须保留 code 并安全返回。

fault injection：

```text
message materialization

LogRecord construction
```

要求：

```text
Logger 不抛异常

frontend_failures++

Error/Critical → RawLog

lower level → safe drop

admission ownership if already acquired
must be safely released
```

---

## 6.81 Dynamic config allocation failure

覆盖：

```text
SetDefaultLevel

SetCategoryLevel

ClearCategoryLevel
```

确认：

```text
bad_alloc caught

ResourceExhausted

strong failure guarantee
```

---

## 6.82 Multi-Runtime

Runtime A：

```text
Debug
file A
```

Runtime B：

```text
Error
file B
```

验证：

```text
A category update 不影响 B

A Backend failure 不影响 B

A Shutdown 不影响 B

A spdlog teardown 不影响 B
```

---

## 6.83 Middleware logger lifecycle

Middleware Initialize：

```text
获取 Logger
```

Middleware Shutdown：

```text
不再 GetLogger

复用 existing Logger
```

验证 shutdown 阶段日志正常进入 pipeline。

---

## 6.84 Runtime integration

验证：

```text
Logging 先于 Middleware 初始化

Executor 初始化可记录日志

Middleware shutdown 可记录日志

Executor shutdown 可记录日志

Logging 最后关闭

Logging Timeout
→ Runtime Stop Timeout

late Logger
→ no Backend access

user callable throws with Sync blocked Backend / Async Block full queue
→ fail-fast path skips normal Logger and reaches terminate

emergency writer may block
→ fail-fast skips it; test in isolated subprocess
```

---

## 6.85 queue_high_watermark

压力测试使 queue 明显达到高占用。

要求：

```text
最终 high-watermark
反映实际发生过的明显高负载
```

不要求：

```text
精确等于瞬时最大 queue size
```

sampling algorithm 不作为测试 API。

---

## 6.86 Benchmarks

至少：

```text
BM_Logger_Enabled_Disabled

BM_Logger_Enabled_Enabled

BM_CompileTimeDisabled

BM_Logger_Copy

BM_Log_Direct_DisabledByLevel

BM_Log_AfterStop_AdmissionReject

BM_LogLazy_Disabled

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

## 6.87 V1 性能契约

正式冻结：

```text
Compile-time disabled:
  logger expression 不执行
  message expression 不执行

Runtime disabled:
  LogLazy builder 不执行

Logger::Enabled:
  O(1)
  no allocation
  no Backend I/O
  no ordinary mutex
  two scalar atomic reads acceptable

Logger copy:
  O(1)
  no heap allocation

Direct Log:
  Running 时 effective-level check，非 Running 时到 admission reject
  lifecycle correctness at admission

Async enabled:
  caller-side message allocation allowed
  no normal Backend output I/O in caller
  emergency RawLog may write stderr in caller

AsyncQueue:
  bounded storage

Backend:
  serialized invocation
```

---

## 6.88 Internal implementation choices — OPEN

以下明确不是 Public API contract：

```text
Logger state reference:
  shared_ptr
  intrusive refcount
  stable slot

LogRecord message representation:
  std::string
  future SBO

Admission implementation:
  lifecycle + in-flight counter
  gate mutex
  combined atomic state word

AsyncQueue:
  mutex ring
  lock-free replacement

Flush internal algorithm:
  control barrier
  sequence/frontier
  completion bitmap

Control execution:
  persistent worker
  lazy worker
  per-operation thread

Category registry container

Category effective-level storage width

BackendStats atomic implementation

queue_high_watermark sampling algorithm

RawLog exact EINTR retry count
```

这些实现可以在不修改 V1 semantic contract 的情况下优化。

---

## 6.89 V1 明确不做

V1 不实现：

```text
NativeBackend

Public Sink SPI

structured key/value logging

JSON logging

network logging sink

runtime Backend hot swap

arbitrary CategoryFilter callback

Quill-style deferred formatting

encoded formatting arguments

per-thread frontend queue

hard-real-time Logger

distributed tracing

process-global logger registry

custom pattern language

implicit NullBackend from empty outputs

Public AdmissionToken / AdmissionLease
```

---

## 6.90 V1 冻结契约总表

以下均正式冻结：

```text
Runtime-local Logging

Logger / LoggingContext / LoggingBackend / RawLog 四层职责

AsyncQueue 属于 LoggingContext

LoggingBackend 是唯一 Public Backend SPI

LogLevel

Critical != Fatal

SourceLocation static-storage model

ROCOMM_SOURCE_LOCATION explicit line cast

LogRecord move-only logical model

LogRecord owns message

Category storage follows LoggingState lifetime

Logger default-disabled semantics

Logger copy O(1), no heap

Logger::Enabled O(1)

Enabled contains lifecycle + level advisory check

Enabled is advisory, not authoritative admission

Direct Log does not use Enabled as lifecycle gate

Direct Log checks effective level while Running; stopped submission reaches admission

Lifecycle correctness belongs to LoggingContext admission

Admission and Running→Stopping are linearizable

Admission internal implementation remains private

Log / LogLazy

LogLazy decay-based Builder traits

string literal supported

std::string move materialization

null const char* safe failure

Builder exception containment

Backend recursion check before Enabled

BackendInvocationGuard

RawFallbackGuard

Compile-time logging macros

Compile-time level mapping

Category grammar

Category case-sensitive

rocomm.* reserved

dot-segment hierarchy

GetLogger → Result<Logger>

invalid category → InvalidArgument

disabled Context GetLogger → disabled Logger

CategoryLevels synchronization owned by LoggingContext

Dynamic category update

Dynamic update noexcept + ResourceExhausted

Dynamic update strong failure guarantee

Runtime duplicate SetCategoryLevel → last-write-wins

LoggingConfig different category overrides accumulate

LoggingConfig same category → last-write-wins

Config and Runtime SetCategoryLevel duplicate semantics一致

LoggingConfig move noexcept + public and actual storage move verification

Two-layer filtering

LoggingMode

DropNewest

DropOldest

Block

Error/Critical RawLog overflow fallback

Queue capacity >= 1

No arbitrary queue upper bound

Huge queue allocation failure → ResourceExhausted

LoggingConfig normal builder exception semantics

SetBackendFactory by value + strong exception guarantee

Default Async

Default DropNewest

Default flush-on-level Error

TextBackendConfig

Empty outputs → InvalidArgument

AsyncQueue bounded MPSC/single-consumer semantics

Control signaling not limited by normal queue capacity

Backend invocation serialized

Sync Write caller-thread semantics

Slow Sync Backend blocks concurrent Sync callers

Flush prefix barrier

Flush control is non-droppable

Flush timeout includes all waiting phases

Sync Flush/Shutdown timeout-safe control execution

Control execution implementation remains open

Control execution resource failure mapping

Running→Stopping shutdown admission cut

Shutdown preserves configured CategoryEntry levels

direct Log after stop reaches admission reject

LogLazy that sees Stopping before builder
does not increment rejected_after_stop

LogLazy that wins Enabled but loses admission race
after a successful builder result increments rejected_after_stop

rejected_after_stop counts lifecycle rejection at admission

logical Stopped vs shutdown completion

Repeated Shutdown joins same completion and preserves final Status

Each Shutdown call has its own timeout starting at that call

Timeout deferred destruction

LoggingContext destructor never performs unbounded wait

Moved-from LoggingContext:
  Shutdown Ok
  stateful control API InvalidState
  GetStats zero snapshot

Moved-from result differences have explicit operation-class rationale

Custom Backend must not infinitely block

LoggingStats dispatched means Write returned

Admission ownership is distinct from accepted record transfer accounting

Frontend failure and Block shutdown cancellation advance prefix completion

Shutdown completion stores immutable final Status

Shutdown initiation requires no new allocation or thread creation after Create

Pending Flush is ordered before or merged into Shutdown

Fail-fast never waits on normal Logger or potentially blocking RawLog

Config-disabled Logger remains no-op after level updates and stop

Stats eventually consistent

queue_high_watermark approximate / non-configurable

BackendFactory in logging_backend.hpp

Factory original failure Status propagation

Factory null success → InternalError

Factory exception containment

Backend Initialize no-argument V1 contract

Second Initialize → InvalidState, preserving Initialized state

Partial Initialize must be safely shutdown

Backend methods noexcept

Backend GetStats no allocation

BackendStats lock-free implementation is not cross-platform API contract

Backend timeout owned by LoggingContext

TextFormatter output semantics

Local time thread-safe conversion

SpdlogBackend synchronous-only

Spdlog global state forbidden

spdlog init_thread_pool forbidden

Spdlog symbol scan

RawLog V1 requirement

RawLog no normal pipeline dependency

RawLog bounded EINTR retry

Middleware acquires Logger during Initialize

Middleware Shutdown reuses existing Logger

Logging remains Running through Middleware/Executor shutdown

V2 NativeBackend reuses V1 runtime

V1 TextBackendConfig remains frozen as common text semantics

V2 Native-specific config does not mutate V1 Text contract

Future deferred formatting adds new primitive instead of changing V1 LogRecord
```

---

## 6.91 V1 Implementation Baseline

本文件完成后，以下内容不再属于设计开放问题：

```text
模块职责

Public API semantic model

Category model

LoggingConfig model

LoggingContext lifecycle

Admission semantics

Enabled / Log / LogLazy responsibility boundary

Queue ownership

Overflow semantics

Flush semantics

Shutdown semantics

Timeout model

rejected_after_stop semantics

Backend SPI

SpdlogBackend boundary

RawLog boundary

Runtime integration

V2 evolution boundary
```

后续实现阶段若发现某项无法实现，不应直接在代码中自行改变语义。

必须：

```text
明确指出冲突

重新进行对应设计评审

再修改本文契约
```

---

## 6.92 最终架构

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
                                    Admission Boundary
                                             │
                                             ▼
                                owned message / LogRecord
                                             │
                                             ▼
                                      LoggingContext
                              ┌──────────────┴──────────────┐
                              │                             │
                             Sync                          Async
                              │                             │
                       backend serialization            AsyncQueue
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

Lifecycle / admission：

```text
Enabled()
   │
   ├── lifecycle advisory check
   └── effective-level advisory check
              │
              ▼
        builder() for LogLazy
              │
              ▼
     Log / internal submission
              │
  effective-level check if Running
              │
              ▼
       Admission Boundary
              │
      ┌───────┴────────┐
      │                │
   Running         Stopping/Stopped
      │                │
   accept            reject
      │                │
      │       rejected_after_stop++
      ▼
 owned message / LogRecord
      │
      ▼
 sync/async ownership transfer
```

Control plane：

```text
Flush / Shutdown
       │
       ▼
LoggingContext control state
       │
       ▼
self-owned control execution
       │
       ▼
Backend serialization
       │
       ▼
Backend Flush / Shutdown
```

Emergency：

```text
Backend recursion
logging unavailable
allocation failure
Backend failure
ordinary invariant diagnostic
late teardown
       │
       ▼
     RawLog
       │
       ▼
low-level stderr
```

---

## 6.93 最终设计结论

RoComm Logging V1 正式采用：

> **RoComm-owned Logger frontend + Runtime-local LoggingContext + explicit admission boundary + unified bounded dispatch + narrow LoggingBackend SPI + SpdlogBackend。**

其中：

> **Logger 负责 Category、Enabled、SourceLocation、message construction 前的快速过滤以及日志提交入口。**

> **`Logger::Enabled()` 是 advisory fast path，不是 lifecycle correctness boundary。它包含 lifecycle 和 effective-level 检查，以避免不必要的 builder/message construction。**

> **Direct Log 不把 Enabled 当作 lifecycle gate；Running 时检查 effective level，观察非 Running 时交给 admission 拒绝。Admission 后才构造 owned record，LogLazy builder 仍在 admission 前执行。**

> **Logging admission 与 `Running → Stopping` 必须线性化。Log 先赢则 Shutdown 必须承担该 operation；Shutdown 先赢则 admission 必须拒绝。**

> **不向 Public API 暴露 AdmissionToken/AdmissionLease；admission gate 的 atomic/count/mutex representation 完全属于内部实现。**

> **`rejected_after_stop` 统计真正到达 admission boundary、但因 lifecycle 已非 Running 而被拒绝的 logging operation。**

> **有效且 config-enabled Logger 在停止后以合法 severity direct Log，跳过 level filtering，在 admission 被拒绝并增加 rejected_after_stop；默认 / disabled Logger 始终 no-op。**

> **如果 LogLazy/宏一开始就通过 `Enabled()` 观察到 Stopping/Stopped，它不会执行 builder，也不会增加 `rejected_after_stop`。**

> **LogLazy 先通过 Enabled、builder 正常返回，随后输掉 admission race 时，rejected_after_stop 增加；builder failure 或 admission 前 filtering 不计 rejection。**

> **LoggingContext 统一负责 lifecycle、Admission、Category policy、sync/async dispatch、Queue、Overflow、Backend serialization、Flush、Shutdown、Timeout 与 Statistics。**

> **AsyncQueue 不属于 SpdlogBackend，也不属于未来 NativeBackend。**

> **LoggingBackend 是唯一 Public Backend extension point；V1 不建立第二套 Public Sink SPI。**

> **Backend invocation 在 LoggingContext 层串行化，所以 Custom Backend 不需要自行实现并发 Write。**

> **Synchronous normal Write 在 caller thread 执行；一个慢 Backend 会通过 serialization 阻塞其他并发 Sync caller。**

> **带 timeout 的 Flush/Shutdown control operation 不允许直接让用户 caller thread 被不可控 Backend call 永久阻塞；必须通过 self-owned control execution 提供 timeout。具体 control thread 策略仍属于实现选择。**

> **Recursion guard 在真正调用 Backend 的线程设置；Backend Initialize/Write/Flush/Shutdown 内部再次使用 Logger 都进入 RawLog fallback。**

> **默认、moved-from、config-disabled Logger 始终 no-op，包括递归场景；不可变 disabled gate 不会被动态 level 更新重新开启。**

> **LogLazy 使用 decay 后的 Builder 返回类型判断，因此字符串字面量合法；`std::string` 返回值允许直接 move 到 LogRecord。**

> **Flush 使用与 admission 线性化的不可丢弃 prefix boundary；不依赖简单 accepted/dispatched 计数推断完成状态，也不冻结具体内部 barrier 算法。**

> **Shutdown 的正确性由 Running→Stopping admission cut 保证，不覆盖 CategoryEntry effective level；已提交 category 配置与 lifecycle 分开表达。**

> **Logical Stopped、shutdown completion 和 final Status 分离。重复调用等待同一 completion，使用自身 timeout；Complete 后返回保存的最终结果，不掩盖 Backend control failure。**

> **LoggingContext wrapper 析构不执行可能无限阻塞的 cleanup；必要 backing 通过 self-owned LoggingState 延迟释放。**

> **DropNewest 和 DropOldest 都严格保持各自 Overflow 语义；Error/Critical 只增加 RawLog emergency fallback，不改变究竟丢哪条 record。**

> **LoggingConfig 的 Category override 采用集合式累积语义：不同 category 累积，同一 category 重复设置 last-write-wins。Runtime 动态 SetCategoryLevel 使用相同的重复设置语义。**

> **LoggingConfig 是普通 builder；配置期 allocation exception 可以抛出。已经创建的 LoggingContext control API 则通过 Status 表达失败并提供明确的失败保证。**

> **LoggingConfig move 是 noexcept contract；公共 traits 只验证声明，还必须验证实际 storage / 成员的移动安全并审查自定义 move body。**

> **Dynamic Category update 的所有可失败工作发生在 commit 前；commit 后 effective-level propagation 不再存在 failure path。**

> **GetLogger 正式返回 `Result<Logger>`；invalid category 返回 InvalidArgument；默认构造 Logger 是安全 disabled handle；moved-from LoggingContext 使用 InvalidState，而 RuntimeStopped 只表示有效 state 已经停止。**

> **Moved-from Context 中，Shutdown 返回 Ok 是因为它属于安全幂等 release operation；Flush/GetLogger/Set* 返回 InvalidState 是因为它们需要有效 state；GetStats 返回零快照是为了让诊断路径可以无条件读取。**

> **Category intern storage 属于 self-owned LoggingState，其生命周期覆盖 wrapper、Logger 已销毁但 worker 仍在处理 in-flight LogRecord 的情况。**

> **`LoggingStats::dispatched` 表示 `Backend::Write()` 已经返回，而不是刚开始调用。**

> **BackendStats 内部通常可以使用 atomic uint64，但 atomic 是否 lock-free 不作为跨平台 Public contract。**

> **`queue_high_watermark` 是 eventually-consistent diagnostic statistic，采样算法不属于 API，也不向用户暴露采样周期。**

> **SpdlogBackend 与 spdlog process-global registry、global level、global shutdown、global async pool、`init_thread_pool()` 完全隔离，并通过 runtime tests 与 symbol scan 防止架构回退。**

> **RawLog 使用固定大小低层输出路径，POSIX EINTR retry 必须有界，但 exact retry count 仍属于实现常量。**

> **普通 RawLog 的单次 OS write 可能阻塞；fail-fast 不调用普通 Logger 或可能阻塞的 RawLog，只可选尝试已证明 nonblocking 的 emergency diagnostic。**

> **Middleware 必须在 Initialize 阶段取得长期需要的 Logger；shutdown 阶段复用已有 Logger。正常 shutdown 在 Middleware/Executor cleanup 后关闭 Logging；Runtime deadline 耗尽时也必须发起 Logging shutdown，允许未完成工作安全延迟清理。**

> **V1 `TextBackendConfig` 已经冻结为 Backend-neutral 的公共文本输出契约，不再为了假想 V2 能力重新开放；Native-specific 能力进入独立 NativeBackendConfig。**

> **V2 NativeBackend 适配 V1 已冻结的 TextBackend contract，而不是反向修改 V1 contract。**

> **未来如果需要 Quill 风格 deferred formatting，应增加新的 record/frontend primitive，而不是改变 V1 LogRecord 的 owned-message 语义。**

> **Logging failure 永远不能修改 Topic / Service operation 的通信 Status。**

> **V1 Logging 不宣称 hard-real-time safe。严格实时事件记录属于未来独立 RT event/trace primitive。**

本文档自此可作为：

```text
docs/logging.md

RoComm Logging V1 Implementation Baseline
```

进入实现阶段：

```text
Public Headers
      ↓
Backend-independent Core
      ↓
Admission / Shutdown Linearization Validation
      ↓
TestBackend Contract Validation
      ↓
SpdlogBackend
      ↓
Runtime Integration
      ↓
Concurrency / Sanitizer / Performance Verification
```

除非实现阶段发现明确不可满足的契约冲突，否则不再重新打开 Logging V1 总体架构。
