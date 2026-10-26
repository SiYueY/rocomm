# RoComm V1 架构与接口设计

**RoComm — Robotics Communication Runtime**  
**Tagline:** *A lightweight C++17 runtime for heterogeneous robot communication*  
**Language:** C++17  
**Architecture Status:** **FROZEN — V1 Architecture Baseline**  
**Phase 0 Semantic Contract:** **FROZEN**  
**Phase 0 Exact Public C++ Headers:** **FINALIZATION REQUIRED**

本文档是 RoComm V1 的正式架构与接口设计基线，用于约束：

```text
Public API
Core Runtime
Middleware SPI
异步执行
生命周期
并发
日志
类型系统
Capability / Info
测试
性能验证
后续 Middleware Integration
```

本文档冻结的是：

> **用户语义、跨模块契约和架构边界。**

不冻结的是：

> **可替换的内部数据结构、性能调优常量以及尚未最终确定的 exact C++ header signatures。**

---

# 1. 项目定位与总体设计

## 1.1 项目名称

项目正式展示名：

```text
RoComm
```

代码标识符：

```text
rocomm
```

正式全称：

```text
Robotics Communication Runtime
```

正式副标题：

```text
A lightweight C++17 runtime for heterogeneous robot communication
```

推荐首页展示：

```text
RoComm — Robotics Communication Runtime

A lightweight C++17 runtime for heterogeneous robot communication.
```

RoComm 的核心定位为：

```text
Heterogeneous Robot Communication
              +
Asynchronous Execution Runtime
```

其中：

```text
Communication
→ 项目第一定位

Runtime / Executor / Future / Logging
→ 支撑通信生命周期、执行、异步和可观测性
```

RoComm 不是：

```text
通用机器人应用框架
任务编排框架
DDS wrapper
ROS2 wrapper
纯 RPC framework
导航 / 运动规划框架
行为树 Runtime
```

RoComm 首先是：

> **一个面向异构机器人通信的轻量级 C++17 Runtime。**

---

## 1.2 `lightweight` 的工程定义

RoComm 中的 lightweight 不表示：

```text
总代码量必须很少
只能提供几个 API
不能存在 Future / Logging / Type System
```

而表示：

```text
低强制依赖
低运行时侵入
Core 不依赖任何具体 Middleware
Middleware 按需链接和初始化
hot path 成本受控
配置和扩展机制克制
不依赖运行时扫描/反射
不使用功能不产生相应运行时成本
```

V1 至少满足以下原则：

| 维度 | V1 约束 |
|---|---|
| C++ 标准 | C++17 |
| Core 对 Middleware 的强制依赖 | 无 |
| Middleware integration | 独立可选 target |
| Dynamic Plugin ABI | V1 不提供 |
| Context | bounded / zero heap / no RTTI |
| `As<T>()` | no allocation / no RTTI / no I/O |
| Type discovery | compile-time opt-in |
| Logging async queue | bounded |
| Runtime worker | 仅按配置创建 |
| Native integration | 未启用时不初始化 |
| 配置扩展 | scoped，而非万能 property bag |

因此：

> **lightweight 描述的是部署、依赖和运行成本，而不是功能列表长度。**

---

## 1.3 命名规范

| 场景 | 规范 | 示例 |
|---|---|---|
| 正式展示名 | CamelCase | `RoComm` |
| 文档标题 | CamelCase | `RoComm V1 架构与接口设计` |
| 仓库名 | lowercase | `rocomm` |
| C++ namespace | lowercase | `rocomm::` |
| include root | lowercase | `<rocomm/...>` |
| CMake target alias | lowercase | `rocomm::core` |
| CMake option | uppercase | `ROCOMM_BUILD_TESTS` |
| Feature macro | uppercase | `ROCOMM_HAS_FASTDDS` |
| visibility macro | uppercase | `ROCOMM_API` |
| 环境变量 | uppercase | `ROCOMM_CONFIG_PATH` |
| Logger category | lowercase | `rocomm.service` |
| 默认日志前缀 | lowercase | `[rocomm]` |

新代码不再引入：

```text
RoRT
rort
RORT_
RoRT::
```

历史 Git 记录、迁移说明和旧讨论文档可以保留旧名称。

---

## 1.4 总体架构定位

RoComm 不重新实现：

```text
DDS
ROS2
Zenoh
gRPC
```

而是在这些通信系统之上建立统一的机器人通信 Runtime：

```text
                         Application
                              │
                            RoComm
                              │
          ┌───────────────────┼────────────────────┐
          ▼                   ▼                    ▼
       Fast DDS              ROS2                Zenoh
                                                   
                              │
                             gRPC
```

V1 首批重点支持：

```text
Fast DDS
ROS2
Zenoh
gRPC
```

后续可以增加：

```text
Cyclone DDS
Connext DDS
MQTT
NATS
其他 Middleware
```

而不改变 RoComm Core 的基础语义。

核心目标：

> **Unified communication semantics, extensible middleware capabilities, native performance paths, and executor-based asynchronous execution.**

---

## 1.5 V1 基础 Primitive

V1 只冻结两个基础通信 Primitive：

```text
Topic
├── Publisher<T>
└── Subscriber<T>

Service
├── Client<Request, Response>
└── Server<Request, Response>
```

同时提供运行基础设施：

```text
Runtime
Executor
Future / Promise
Timer
Cancellation
Logging
Type / Serialization support
```

V1 不包含：

```text
Action Framework
Streaming RPC
导航
运动规划
行为树
参数服务器
Launch 系统
完整 ROS Graph 替代
稳定第三方 Dynamic Plugin ABI
```

未来 Action 可以建立为：

```text
Action
├── Generic Topic + Service protocol
└── Optional native transport
```

但不属于 V1 基础 Primitive。

---

## 1.6 不定义公共 Endpoint 统称

RoComm V1 不定义：

```cpp
class Endpoint;
class EndpointBase;
class Communicator;
class CommunicationObject;

enum class EndpointKind;

template <typename T>
struct is_endpoint;

template <typename T>
struct is_communication_object;
```

也不把：

```text
Endpoint
Communicator
CommunicationObject
Handle
```

定义成：

```text
Publisher
Subscriber
Client
Server
```

的正式用户可见统称。

公共模型始终是：

```text
                    Runtime
                      │
       ┌──────────────┴──────────────┐
       │                             │
      Topic                        Service
       │                             │
 ┌─────┴─────┐                 ┌─────┴─────┐
 ▼           ▼                 ▼           ▼
Publisher  Subscriber        Client      Server
```

核心原则：

> **共享契约不自动升级为共享概念。**

> **共享实现也不自动泄漏为共享公共 API。**

四类公共类型可以共享：

```text
move-only ownership
Runtime lifecycle
As<T>() model
Capability membership freeze
Runtime Stop 后安全析构
```

但这些共同契约不构成：

```text
公共父类
公共 Kind
公共 membership trait
公共 umbrella type
```

---

## 1.7 文档中的共享描述规则

需要同时描述四类公共类型时，直接写：

> `Publisher`、`Subscriber`、`Client` 和 `Server`……

或者局部使用：

```text
上述四类类型
这四类对象
```

但不正式定义：

```text
通信对象
```

为新的 glossary concept。

这样避免自然语言中的便利称呼最终再次上浮成 API abstraction。

---

## 1.8 Service 的 Call / Request / Response 命名模型

Service 采用三层明确语义。

### Call

`Call` 表示：

> **一次完整 Service Request 到 terminal Response/Error 的生命周期。**

因此：

```text
AsyncCall
CallState
CallCompletion
ClientCallInfo
ServerCallInfo
```

继续使用 `Call`。

---

### Request operation

`Request` 表示：

> **Client 发起请求这一侧的一次 operation。**

因此：

```text
RequestContext
operation_scope::Request
```

---

### Response operation

`Response` 表示：

> **Server 产生 terminal Response 或 terminal Error 这一侧的一次 operation。**

因此：

```text
ResponseContext
ResponseSender
ResponseHandler
operation_scope::Response
```

整体关系：

```text
Call
├── Request operation
│   └── RequestContext
│
└── Response operation
    ├── ResponseContext
    ├── automatic Handler result
    └── explicit ResponseHandler
            ↓
       ResponseSender
```

不再使用：

```text
CallContext
ReplyContext
ReplySender
ReplyHandler
operation_scope::Call
operation_scope::Reply
```

Core Service 统一使用：

```text
Request
Response
```

如果某个 Middleware 的官方原生术语本身使用：

```text
reply
multi-reply
```

则可以仅在该 Middleware-specific 章节中保留 native terminology。

---

## 1.9 Options / Context / As<T>() / Info 四组概念

RoComm 用户层的核心心智模型：

```text
Options
→ 创建什么样的 Publisher / Subscriber / Client / Server

Context
→ 本次 operation 如何执行

As<T>()
→ 已创建对象还提供什么高级运行时能力

Info
→ 一次实际通信产生了什么 metadata
```

整体：

```text
Creation
   ↓
Options

Operation
   ↓
Context

Runtime capability
   ↓
As<T>()

Communication result metadata
   ↓
Info
```

其中：

```text
Options / Context / As<T>()
→ control / extension model

Info
→ data-plane result metadata
```

---

## 1.10 核心设计原则

### Primitives, not Features

Core 只提供支撑异构通信 Runtime 所需要的 primitive。

某个 Middleware 存在一项能力，不意味着：

```text
必须创建新的 Core subsystem
```

---

### Common data plane, extensible control plane

```text
Common data plane
+
Extensible control plane
```

Topic / Service 提供统一主数据路径。

Middleware-specific 能力通过：

```text
Options
Context
Capability
Info
```

扩展。

---

### 常用能力显式化

普通用户的常见配置应使用显式 API：

```cpp
ctx.SetTimeout(...);
ctx.SetPriority(...);
ctx.SetTarget(...);
```

而不是：

```cpp
ctx.Set(TimeoutOption{...});
```

---

### 配置与行为分离

```text
Options
→ creation-time configuration

Context
→ operation-time configuration

Capability
→ runtime behavior

Info
→ communication result metadata
```

---

### Generic programming 围绕能力

RoComm V1 不提供：

```cpp
is_communication_object<T>
```

泛型代码应约束：

```text
需要的表达式
需要的 Capability
```

而不是：

```text
是否属于一个 umbrella membership
```

---

# 2. 用户 API、通信语义与数据模型

## 2.1 Status、Diagnostic 与 Result

### 2.1.1 StatusCode

V1 至少定义：

```cpp
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
```

其中：

```text
ProtocolError
```

表示：

> RoComm 公共协议契约被违反。

例如：

```text
声明 unary Service
却收到多个 response
```

Middleware native error 不直接扩展 Core enum，而是：

```text
native error
    ↓
closest StatusCode
    +
optional Diagnostic
```

---

### 2.1.2 Status

```cpp
class Status {
public:
    Status() noexcept;

    StatusCode code() const noexcept;
    bool ok() const noexcept;

    const Diagnostic* diagnostic() const noexcept;
};
```

成功：

```text
StatusCode::Ok
diagnostic == nullptr
zero diagnostic allocation
```

失败：

```text
code != Ok
optional immutable Diagnostic
```

---

### 2.1.3 Diagnostic

概念结构：

```cpp
struct TargetDiagnostic {
    std::string target;
    StatusCode code;
    std::string message;
};

struct Diagnostic {
    std::string message;
    std::vector<TargetDiagnostic> targets;
};
```

Diagnostic 只在失败路径按需创建。

成功路径不得为了“以后可能报错”预构造：

```text
std::string
std::vector
per-target result container
```

---

### 2.1.4 Status copy lifetime

`Status` copy 共享 immutable Diagnostic backing。

例如：

```cpp
auto s = publisher.Publish(msg);

auto* diagnostic =
    s.diagnostic();

auto s2 = s;
```

只要 `s2` 或其他共享该 Diagnostic 的 Status 仍然存活：

```text
diagnostic
→ valid
```

最后一个共享 Status 销毁后，该 pointer 才失效。

实现可以使用：

```cpp
std::shared_ptr<const Diagnostic>
```

但这属于内部实现。

---

### 2.1.5 Status move / noexcept

Status 的正常移动应保持轻量。

Phase 0 Public Header Finalization 应明确：

```text
copy ctor
copy assignment
move ctor
move assignment
```

的 exact `noexcept` contract。

但冻结语义是：

```text
move 不改变 logical Status value
Diagnostic backing 不被深复制
```

---

### 2.1.6 Result<T>

```cpp
template <typename T>
class Result {
public:
    Result(T value);
    Result(Status status);

    bool ok() const noexcept;
    explicit operator bool() const noexcept;

    T& value() & noexcept;
    const T& value() const& noexcept;
    T&& value() && noexcept;

    const Status& status() const noexcept;
};
```

以及：

```cpp
template <>
class Result<void> {
public:
    Result() noexcept;
    Result(Status status);

    bool ok() const noexcept;
    explicit operator bool() const noexcept;

    const Status& status() const noexcept;
};
```

语义：

```text
Result<T> success
→ 持有 T

Result<T> failure
→ 持有 non-Ok Status

Result<void>()
→ success
```

`Result(Status)` 的前置条件：

```text
status.code() != Ok
```

成功 Result 的 `status()`：

```text
返回 static / logically equivalent Ok Status
```

V1 内部可以先采用：

```cpp
std::variant<T, Status>
```

只有 profiling 证明必要时才优化成 custom tagged union。

---

### 2.1.7 `Result::value()` fail-fast

前置条件：

```text
ok() == true
```

失败 Result 调用：

```cpp
result.value();
```

属于 programmer contract violation。

语义必须是：

```text
不返回
不抛异常
不产生 UB
```

推荐：

```cpp
if (!ok()) {
#ifndef NDEBUG
    assert(false &&
           "Result::value() called on failed Result");
#endif
    std::terminate();
}
```

因此：

```text
Debug:
assert 通常直接 abort，并提供 source location；
若 assert 未终止，仍继续 terminate。

Release:
assert 被编译掉；
直接 std::terminate()。
```

两种构建均 fail-fast。

`T&& value() && noexcept` 只表示：

```text
返回引用本身不抛异常
```

后续调用方真正 move-construct `T` 时是否抛异常，由 `T` 决定。

---

## 2.2 Object Scope 与 Operation Scope

Scope 只表达：

> **compile-time applicability**

不是 Runtime object，不构成继承关系。

---

### 2.2.1 Object Scope

```cpp
namespace rocomm::object_scope {

struct Publisher {};
struct Subscriber {};
struct Client {};
struct Server {};

}
```

例如：

```text
rocomm::Publisher<T>
→ Runtime-created public type

rocomm::object_scope::Publisher
→ compile-time applicability marker
```

二者没有 runtime relation。

---

### 2.2.2 Operation Scope

```cpp
namespace rocomm::operation_scope {

struct Publish {};
struct Request {};
struct Response {};

}
```

含义：

```text
Publish
→ 一次 Topic Publish operation

Request
→ 一次 Service Request operation

Response
→ 一次 Service terminal Response/Error operation
```

不存在：

```text
operation_scope::Call
```

因为：

```text
Call
```

是完整生命周期。

---

### 2.2.3 object_scope_set

```cpp
template <typename... Scopes>
struct object_scope_set;
```

硬约束：

```text
非空
只能包含 object_scope 类型
不允许重复
不能包含 operation_scope 类型
```

并正式提供：

```cpp
template <typename Scope>
inline static constexpr bool contains = ...;
```

概念：

```cpp
template <typename... Scopes>
struct object_scope_set {
    static_assert(
        sizeof...(Scopes) > 0,
        "object_scope_set must not be empty");

    template <typename Scope>
    inline static constexpr bool contains =
        (std::is_same_v<Scope, Scopes> || ...);
};
```

实现必须额外 compile-time 验证：

```text
每一个 Scopes 都是合法 object_scope type
不存在重复
```

这些约束必须由类型系统自身保证。

CI compile-fail tests 只是 regression protection。

---

### 2.2.4 operation_scope_set

未来 Operation Capability 如确有需要：

```cpp
template <typename... Scopes>
struct operation_scope_set;
```

硬约束：

```text
非空
只能包含 operation_scope
不重复
不能包含 object_scope
```

Object Capability 和 Operation Capability 在类型层隔离。

---

## 2.3 MiddlewareId

`MiddlewareId` 是 opaque value type。

公共概念：

```cpp
class MiddlewareId {
public:
    MiddlewareId() noexcept = default;

    explicit operator bool() const noexcept;

    friend bool operator==(
        MiddlewareId,
        MiddlewareId) noexcept;

    friend bool operator!=(
        MiddlewareId,
        MiddlewareId) noexcept;

private:
    // implementation-defined domain / slot / generation identity

    friend class RuntimeConfig;
    friend class Runtime;
};
```

用户不能自行构造一个有效 MiddlewareId。

有效 ID 只能来自：

```cpp
RuntimeConfig::AddMiddleware(...)
```

MiddlewareId 必须能够检测：

```text
配置 domain 不匹配
Runtime domain 不匹配
stale generation
slot reuse
```

因此不能只用：

```text
裸 uint64 slot
```

作为语义全部。

从 Runtime A 获得的 ID 用于 Runtime B：

```text
InvalidArgument
```

无效 ID：

```text
InvalidArgument
```

MiddlewareId 不是：

```text
wire protocol ID
process-global stable ID
Plugin ABI ID
```

---

## 2.4 Scoped Options

创建期配置分为：

```text
PublisherOptions
SubscriberOptions
ClientOptions
ServerOptions
```

---

### 2.4.1 PublisherOptions

PublisherOptions 表示：

> Publisher creation-time configuration。

可包含：

```text
显式 Topic targets
创建期资源/队列配置
Middleware-specific Publisher Options
```

例如：

```cpp
PublisherOptions&
SetTargets(
    std::vector<MiddlewareId> targets);
```

V1 不提供：

```text
PublisherOptions::executor
```

因为：

```text
Publisher
```

没有用户 callback 需要调度。

Middleware 内部发送线程也不由用户 Executor 直接控制。

---

### 2.4.2 SubscriberOptions

可提供：

```cpp
SubscriberOptions&
SetExecutor(
    std::shared_ptr<Executor> executor);
```

用于覆盖 Runtime default Executor。

---

### 2.4.3 ClientOptions

可提供：

```cpp
ClientOptions&
SetTarget(MiddlewareId target) noexcept;

ClientOptions&
SetErrorMode(
    ServiceErrorMode mode) noexcept;
```

---

### 2.4.4 ServerOptions

可提供：

```cpp
ServerOptions&
SetExecutor(
    std::shared_ptr<Executor> executor);

ServerOptions&
SetErrorMode(
    ServiceErrorMode mode) noexcept;
```

---

### 2.4.5 Middleware-specific Options

例如：

```cpp
rocomm::ros2::PublisherOptions ros2;

ros2.qos
    .KeepLast(10)
    .Reliable()
    .TransientLocal();

options.Set(ros2);
```

---

### 2.4.6 `Set<T>()` 粒度

允许：

```cpp
options.Set(
    rocomm::ros2::PublisherOptions{...});
```

不允许把 generic extension container 退化成 property bag：

```cpp
options.Set(Reliable{});
options.Set(KeepLast{10});
options.Set(TransientLocal{});
```

原则：

> **`Set<T>()` 接收当前 scope 下完整的语义配置对象。**

---

### 2.4.7 Options extension trait

默认：

```cpp
template <typename T>
struct option_extension_traits {
    static constexpr bool enabled = false;
};
```

默认未注册 trait 仅提供：

```text
enabled = false
```

不提供：

```text
scope
name
descriptor
```

合法特化必须提供：

```text
enabled == true
scope
name
```

例如：

```cpp
template <>
struct option_extension_traits<
    rocomm::ros2::PublisherOptions> {

    static constexpr bool enabled = true;

    using scope =
        rocomm::object_scope::Publisher;

    static constexpr std::string_view name =
        "rocomm.ros2.publisher-options.v1";
};
```

Options extension：

> **exactly one object scope**

---

### 2.4.8 Malformed trait diagnostic

如果某个 integration 作者写：

```cpp
template <>
struct option_extension_traits<MyOptions> {
    static constexpr bool enabled = true;
    // missing scope
};
```

RoComm 的模板入口应使用 C++17 detection helper 尽量给出：

```text
option_extension_traits<T> has enabled=true
but does not define scope.
```

而不是只产生：

```text
no type named 'scope'
```

同样适用于：

```text
context_extension_traits
capability_traits
info_traits
type_traits
```

V1 不要求 compile-time 自动拼接：

```text
完整 demangled T
T 实际注册 scope
所有合法 extension 列表
```

---

### 2.4.9 Options extension 共享数据

如果配置字段本身可以复用：

```cpp
struct RetryPolicy {
    ...
};
```

允许：

```cpp
struct FooPublisherOptions {
    RetryPolicy retry;
};

struct FooClientOptions {
    RetryPolicy retry;
};
```

但不允许：

```cpp
options.Set(RetryPolicy{});
```

让 `RetryPolicy` 变成跨 scope generic fragment。

---

### 2.4.10 Last-write-wins

同类型：

```cpp
options.Set(a);
options.Set(b);
```

语义：

```text
b 完整替换 a
```

不做隐式 merge。

需要 merge 时由具体 Options type 自己定义：

```cpp
specific_options.Merge(...);
```

---

### 2.4.11 Options 性能契约

默认构造：

```text
O(1)
zero heap allocation
empty/lazy extension storage
```

首次 `Set<T>()` 后允许：

```text
dynamic allocation
RTTI
type erasure
```

因为 Options 不是 operation hot path。

V1 不冻结：

```text
是否使用 std::any
具体 container
单次 Set 的 allocation 数
```

---

## 2.5 Middleware-specific Options 消费规则

用户显式提供的每一项 Middleware-specific config：

> **必须至少被一个最终 resolved target 消费。**

例如 Topic：

```text
robot/state
├── FastDDS
└── Zenoh
```

用户设置：

```text
FastDDS options
Zenoh options
ROS2 options
```

但 route 没有 ROS2：

```text
CreatePublisher()
→ UnsupportedConfiguration
```

Diagnostic 应指出：

```text
rocomm.ros2.publisher-options.v1
was not consumed by any resolved target.

Resolved targets:
- fastdds
- zenoh
```

禁止：

> 用户显式配置，但 Runtime 静默忽略。

没有显式 config 的 target 可以使用该 Middleware 默认值。

---

### 2.5.1 Logical Create all-or-nothing

Topic 可解析为：

```text
Publisher
├── FastDDS
└── Zenoh
```

创建之前尽可能先完成：

```text
route resolution
type validation
Options validation
Options consumption validation
Middleware capability validation
```

如果 native creation：

```text
FastDDS succeeded
Zenoh failed
```

RoComm 必须：

```text
清理 FastDDS 已创建资源
返回 failure
不返回半创建 Publisher
```

因此：

```text
Create*
→ logical all-or-nothing

Publish fan-out
→ non-transactional
```

---

## 2.6 Context

V1 定义：

```text
PublishContext
RequestContext
ResponseContext
```

全部是：

```text
copyable
reusable
bounded
zero heap
no RTTI
```

initiating API 接受：

```cpp
const Context&
```

不消费或修改 caller 的 Context。

---

### 2.6.1 PublishContext

表示：

> 一次 Topic Publish operation 的参数。

常见 common field 可以直接显式暴露。

Middleware-specific per-publish 参数通过：

```cpp
PublishContext::Set<T>()
```

---

### 2.6.2 RequestContext

表示：

> Client 发起一次 Service Request operation 的参数。

至少提供：

```cpp
RequestContext&
SetTimeout(Duration) noexcept;

RequestContext&
SetPriority(Priority) noexcept;

RequestContext&
SetTarget(MiddlewareId target) noexcept;

template <typename T>
RequestContext&
Set(const T&) noexcept;
```

---

### 2.6.3 Request target 是 common field

Service target 是所有 Middleware 都理解的公共路由语义，因此：

```text
RequestContext::SetTarget()
```

是 common field。

它不是 Middleware-specific extension。

---

### 2.6.4 Service routing priority

每次 Call 最终只能解析到一个 target：

```text
1. RequestContext explicit target
2. ClientOptions configured target
3. Service route rule
4. RuntimeConfig default service target
```

Service 不自动 fan-out。

---

### 2.6.5 Topic routing priority

Publisher target resolution：

```text
1. PublisherOptions explicit targets
2. Topic route rule
3. RuntimeConfig default topic targets
```

Topic 可以：

```text
0..N target
```

如果最终无 target：

```text
CreatePublisher()
→ UnsupportedConfiguration
```

---

### 2.6.6 ResponseContext

表示：

> Server 产生 terminal Response 或 terminal Error 时的 per-operation configuration。

例如：

```text
response metadata
compression
transport-specific response attributes
native response flags
```

ResponseContext 不保存：

```text
CallState
ResponseSender state
request ID ownership
completion state
```

这些属于运行时内部 state。

---

### 2.6.7 Context 不保存 runtime state

Context 不允许保存：

```text
request id
native RPC call object
completion
cancel state
response state
```

这些属于：

```text
service::CallState
```

以及必要的 operation internal state。

---

### 2.6.8 Context extension trait

默认：

```cpp
template <typename T>
struct context_extension_traits {
    static constexpr bool enabled = false;
};
```

合法特化必须提供：

```text
enabled
scope
name
descriptor
```

例如：

```cpp
template <>
struct context_extension_traits<
    rocomm::grpc::CallOptions> {

    static constexpr bool enabled = true;

    using scope =
        rocomm::operation_scope::Request;

    static constexpr std::string_view name =
        "rocomm.grpc.call-options.v1";

    inline static constexpr
        ContextExtensionDescriptor descriptor{
            StableHash64(name),
            name
        };
};
```

Middleware native 类型仍然可以叫：

```text
grpc::CallOptions
```

因为 native terminology 可以描述整个 RPC。

但在 RoComm 中它的 applicability 是：

```text
operation_scope::Request
```

---

### 2.6.9 ContextExtensionDescriptor

```cpp
struct ContextExtensionDescriptor {
    std::uint64_t key;
    std::string_view name;
};
```

用于：

```text
bounded Context slot identity
same-type replacement
```

查找方式：

```text
key compare
    ↓
key equal
    ↓
canonical name compare
```

不使用：

```text
typeid
std::type_index
函数地址
本地 static 地址
```

作为跨 integration library 的稳定 identity。

---

### 2.6.10 Context extension 类型约束

必须满足：

```cpp
std::is_trivially_copyable<T>::value
std::is_trivially_destructible<T>::value
std::is_nothrow_copy_constructible<T>::value
```

并要求：

```text
bounded size
alignof(T) <= alignof(std::max_align_t)
```

V1 不为 over-aligned Context extension 分配动态 storage。

---

### 2.6.11 View / Ref

variable-size 数据通过：

```text
ByteView
StringView
MetadataSpan
CredentialsRef
```

表达。

不在 Context extension 中直接拥有：

```text
std::vector
std::string
dynamic map
大型 heap-owned object
```

原则：

> **Context extension 是 bounded descriptor/view，不是 variable-size owner。**

---

## 2.7 Context 状态模型

初始 tuning 建议：

```text
kMaxContextExtensions = 2
kMaxContextExtensionSize = 64 bytes
alignment = alignof(std::max_align_t)
```

这些属于：

```text
implementation tuning constants
```

不是公共 ABI guarantee。

---

### 2.7.1 Sticky invalid

发生：

```text
extension capacity overflow
invalid common field
extension validation failure
```

后：

```text
Context → invalid
```

第一个错误获胜。

进入 invalid 后：

```text
所有 setter no-op
```

包括：

```text
common setter
extension setter
```

---

### 2.7.2 Same-type replacement

如果 Context 已包含同类型 extension：

```cpp
ctx.Set(a);
ctx.Set(b);
```

第二次：

```text
找到原 slot
原地完整替换
last-write-wins
```

即使 Context 已达到：

```text
kMaxContextExtensions
```

也必须允许 same-type replacement。

---

### 2.7.3 Reset

每个 Context 提供：

```cpp
void Reset() noexcept;
```

语义完全等价于：

```cpp
ctx = RequestContext{};
```

或对应默认 Context。

Reset：

```text
common fields → defaults
extensions → empty
invalid state → cleared
first error → cleared
```

---

### 2.7.4 ContextValidation

概念：

```cpp
struct ContextValidation {
    enum class Code : std::uint8_t {
        Ok,
        Invalid,
        ExtensionCapacityExceeded,
    };

    Code code = Code::Ok;
    std::string_view extension_name{};

    explicit operator bool() const noexcept;
};
```

接口：

```cpp
ContextValidation
Validate() const noexcept;
```

`extension_name` 必须指向：

```text
trait 中的 static canonical name
```

保证 lifetime 稳定。

---

### 2.7.5 Operation boundary validation

普通用户不需要主动调用 `Validate()`。

以下边界必须自动验证：

```text
Publish
AsyncCall
AsyncCallWithInfo
ResponseSender::Send
ResponseSender::Fail
```

所有 invalid Context 对外统一：

```text
InvalidContext
```

详细原因进入 Diagnostic。

---

## 2.8 Initiating API ownership contract

以下属于 initiating API：

```text
Publish
AsyncCall
AsyncCallWithInfo
ResponseSender::Send
ResponseSender::Fail
```

返回之前必须完成：

```text
caller-owned borrowed input
        ↓
materialize / acquire independent ownership
        ↓
RoComm-owned state
```

返回以后：

```text
原 message
原 RequestContext
原 ResponseContext
其中 View / Ref
caller stack
```

都可以立即销毁。

RoComm 后续异步工作只能依赖：

```text
transport::MessageFrame
service::CallState
Middleware-owned operation state
其他明确 owned backing
```

这不表示：

```text
所有网络发送必须在 API 返回前完成
```

而表示：

> API 返回后不再依赖 caller-owned 临时数据。

---

## 2.9 Capability 与 `As<T>()`

四类公共对象分别提供自己的：

```cpp
template <typename T>
T* As() noexcept;
```

例如：

```cpp
publisher.As<rocomm::dds::Publisher>();

subscriber.As<rocomm::dds::Subscriber>();

client.As<rocomm::grpc::Client>();

server.As<rocomm::grpc::Server>();
```

不存在：

```cpp
Endpoint::As<T>();
```

---

### 2.9.1 capability_traits

默认：

```cpp
template <typename T>
struct capability_traits {
    static constexpr bool enabled = false;
};
```

合法特化：

```cpp
template <>
struct capability_traits<
    rocomm::dds::Publisher> {

    static constexpr bool enabled = true;

    using scopes =
        object_scope_set<
            object_scope::Publisher>;

    static constexpr std::string_view name =
        "rocomm.dds.publisher.v1";

    inline static constexpr
        CapabilityDescriptor descriptor{
            StableHash64(name),
            name
        };
};
```

---

### 2.9.2 多 object scope Capability

例如：

```cpp
using scopes =
    object_scope_set<
        object_scope::Publisher,
        object_scope::Subscriber>;
```

允许一个 Capability interface 支持多个 object scope。

但：

> **同一个 Capability type 在所有合法 scope 中必须表达同一个 interface contract。**

如果不同 scope 下语义实际上不同：

```text
拆成不同 Capability
```

例如：

```text
PublisherStatistics
SubscriberStatistics
```

比一个含义模糊的 generic Statistics 更合理。

---

### 2.9.3 非法 scope 编译期失败

例如：

```cpp
publisher.As<rocomm::grpc::Client>();
```

如果：

```text
grpc::Client
→ object_scope::Client only
```

则：

```text
compile-time failure
```

概念检查：

```cpp
static_assert(
    capability_traits<T>::scopes
        ::template contains<
            object_scope::Publisher>,
    "Capability T is not valid for Publisher; "
    "check capability_traits<T>::scopes.");
```

---

### 2.9.4 `nullptr` 的唯一语义

只有：

```text
Capability type 在当前 object scope 合法
+
当前实例没有实现该 Capability
```

时：

```cpp
As<T>() == nullptr
```

因此 nullptr 不混合表示：

```text
scope misuse
instance unsupported
```

---

### 2.9.5 CapabilityDescriptor

```cpp
struct CapabilityDescriptor {
    std::uint64_t key;
    std::string_view name;
};
```

Identity：

```text
StableHash64(canonical name)
+
full canonical name
```

查找：

```text
fixed-size key compare
    ↓
key match
    ↓
canonical name compare
```

hash collision 不产生错误 capability match。

---

### 2.9.6 StableHash64

V1 固定算法：

```text
FNV-1a 64-bit
```

接口：

```cpp
constexpr std::uint64_t
StableHash64(
    std::string_view value) noexcept;
```

其用途：

```text
in-process lookup accelerator
```

不是：

```text
wire protocol ID
schema ID
network type ID
future Plugin ABI stable ID
```

未来真正 stable binary plugin ABI 应使用独立 ID 体系。

---

### 2.9.7 Canonical name

例如：

```text
rocomm.dds.publisher.v1
rocomm.grpc.client.v1
rocomm.ros2.subscription-info.v1
```

一个 canonical interface type 对应唯一 canonical name。

多个具体实现可以实现同一个 family Capability：

```text
Fast DDS
Cyclone DDS
Connext DDS
    ↓
rocomm.dds.publisher.v1
```

官方 trait 必须通过 CI 检查 canonical name uniqueness。

---

### 2.9.8 `As<T>()` 性能契约

所有 object-level `As<T>()`：

```text
noexcept
zero allocation
no RTTI
no Middleware I/O
```

Capability membership：

```text
成功 Create* 返回前构造完成
之后冻结
```

Runtime Stop 不改变 membership。

---

### 2.9.9 Capability pointer lifetime

例如：

```cpp
auto* dds =
    publisher.As<rocomm::dds::Publisher>();

auto moved =
    std::move(publisher);
```

如果：

```text
dds != nullptr
```

则该 borrowed pointer 与：

```text
underlying logical object state
```

绑定，而不是与旧 wrapper 地址绑定。

因此 move 后：

```text
dds
```

仍然有效，直到 moved-to owner 的 logical state 被最终销毁。

moved-from wrapper：

```text
operator bool() == false
```

但之前获得的 capability pointer 不因 wrapper move 自动失效。

V1 不提供：

```text
Acquire<T>()
```

或其他 capability owning handle。

---

## 2.10 ResponseSender 不参与 Object Capability

V1：

```text
ResponseSender<Response>
├── Send()
├── Fail()
├── IsCancelled()
└── operator bool()
```

不提供：

```cpp
ResponseSender::As<T>();
```

ResponseSender 是：

> **Server Call 的 Response operation auxiliary object。**

它不是：

```text
Publisher
Subscriber
Client
Server
```

的第五个同级对象。

---

### 2.10.1 ResponseHandler

高级 Server path：

```cpp
using ResponseHandler =
    std::function<
        void(
            const Request&,
            const ServerCallInfo&,
            ResponseSender<Response>)>;
```

`ResponseHandler` 在 Server API 中表示：

> **server-side handler with explicit response control。**

不是：

```text
接收 Response 的 Client callback
```

---

### 2.10.2 Future operation capability

未来如果确实需要：

```text
Response capability
Goal capability
Stream operation capability
Loan operation capability
```

应独立定义：

```text
operation_capability_traits<T>
operation_scope_set
```

不得复用：

```text
capability_traits<T>
object_scope_set
```

V1 不实现 operation-level `As<T>()`。

---

## 2.11 Info 数据平面模型

V1 定义：

```text
MessageInfo
ClientCallInfo
ServerCallInfo
```

不使用：

```text
TopicInfo
ServiceInfo
```

后两者保留给未来 resource/graph introspection。

---

### 2.11.1 InfoKind

```cpp
enum class InfoKind {
    Message,
    ClientCall,
    ServerCall,
};
```

---

### 2.11.2 info_traits

默认：

```cpp
template <typename T>
struct info_traits {
    static constexpr bool enabled = false;
};
```

例如：

```cpp
template <>
struct info_traits<
    rocomm::dds::MessageInfo> {

    static constexpr bool enabled = true;

    static constexpr InfoKind kind =
        InfoKind::Message;

    static constexpr std::string_view name =
        "rocomm.dds.message-info.v1";

    inline static constexpr
        InfoDescriptor descriptor{
            StableHash64(name),
            name,
            InfoKind::Message
        };
};
```

---

### 2.11.3 InfoDescriptor

```cpp
struct InfoDescriptor {
    std::uint64_t key;
    std::string_view name;
    InfoKind kind;
};
```

查询验证：

```text
trait enabled
InfoKind compatible
key match
canonical name match
```

Capability 与 Info 是两套独立系统。

---

### 2.11.4 MessageInfo

```cpp
struct MessageInfo {
    std::optional<Timestamp> source_timestamp;
    Timestamp receive_timestamp;
    std::optional<Identity> source_identity;
    std::optional<bool> is_local;

    template <typename T>
    const T* As() const noexcept;
};
```

`receive_timestamp` mandatory。

定义为：

> **消息第一次进入 RoComm Middleware integration receive boundary 时，由 RoComm 本地时钟记录的时间。**

不是：

```text
callback start time
物理 NIC timestamp
Middleware native receive timestamp
```

native timestamp 如有需要，通过 Middleware-specific Info 暴露。

---

### 2.11.5 Message 与 MessageInfo lifetime

Subscriber callback：

```cpp
subscriber.SetCallback(
    [](const T& message,
       const MessageInfo& info) {
        ...
    });
```

以下均只保证到 callback 返回：

```text
message reference
MessageInfo reference
MessageInfo::As<T>() pointer
其中的 borrowed View / pointer / string_view
native-loan view
```

尤其：

> payload reference 本身也必须明确为 callback-scoped。

如果用户需要长期保存：

```text
显式复制 payload / portable metadata
```

---

### 2.11.6 不提供 universal Info::Copy()

V1 不提供：

```cpp
MessageInfo::Copy();
ServerCallInfo::Copy();
```

也不建立：

```text
OwnedInfo
```

通用抽象。

原因是 Middleware-specific Info 可能包含：

```text
native pointer
loan metadata
opaque transport view
borrowed runtime state
```

无法统一深复制。

portable value field 可以直接复制：

```cpp
auto ts =
    info.receive_timestamp;

auto identity =
    info.source_identity;
```

Middleware-specific Info 自己决定是否提供：

```text
owned copy helper
```

---

### 2.11.7 Receive backing

接收路径：

```text
Native middleware receive
          │
          ├── payload
          │      ↓
          │ MessageFrame / typed backing
          │
          └── native metadata
                 ↓
           MessageInfo backing
                 │
                 └────────────┐
                              ▼
                    DispatchEnvelope
                              ↓
                          Executor
                              ↓
                    callback(msg, info)
```

不能把：

```text
stack-local MessageInfo
```

借给异步 Executor。

---

## 2.12 Topic Publish

公共概念：

```cpp
Status Publish(
    const T& message,
    const PublishContext& context = {});
```

---

### 2.12.1 Publish 成功语义

返回前完成：

```text
同步参数检查
Context validation
borrowed input materialization
local submission acceptance
```

成功 `Ok` 表示：

> **RoComm / target Middleware 已接受本地提交。**

不表示：

```text
远端 Subscriber 已收到
DDS ACK 已完成
broker 已持久化
network send 已完成
```

普通 Publish 不承诺 zero-copy。

native loan / zero-copy 由专门 Capability 提供。

---

### 2.12.2 Topic fan-out

一个 Publisher 可以解析到：

```text
FastDDS
Zenoh
...
```

运行时 Publish 定义为：

> **non-transactional best-effort multi-target submission**

所有 resolved target 都必须尝试。

结果：

```text
all success
→ Ok

at least one success + at least one failure
→ PartialFailure

all failed
→ AggregateFailure
```

无 rollback。

因此：

```text
PartialFailure
```

意味着：

> 消息可能已经通过部分 Middleware 发出。

---

### 2.12.3 Aggregate Diagnostic 顺序

只要 Publish 最终不是 `Ok`：

```text
Diagnostic.targets
```

必须按：

```text
resolved-target order
```

包含所有实际尝试 target，包括成功 target。

例如：

```text
resolved:
1. fastdds
2. zenoh
3. mqtt

result:
fastdds → Ok
zenoh → ResourceExhausted
mqtt → MiddlewareError
```

返回：

```text
PartialFailure

targets[0]:
  fastdds → Ok

targets[1]:
  zenoh → ResourceExhausted

targets[2]:
  mqtt → MiddlewareError
```

全部失败：

```text
AggregateFailure
```

也遵守同样顺序。

成功 Publish：

```text
不创建 Diagnostic
不要求预构造 per-target result vector
```

保持 hot path 轻量。

---

## 2.13 Service Client API

默认：

```cpp
Future<Result<Response>>
AsyncCall(
    const Request& request,
    const RequestContext& context = {});
```

需要 completion metadata：

```cpp
Future<CallCompletion<Response>>
AsyncCallWithInfo(
    const Request& request,
    const RequestContext& context = {});
```

其中：

```cpp
template <typename Response>
struct CallCompletion {
    Result<Response> result;
    ClientCallInfo info;
};
```

名称保持：

```text
CallCompletion
```

因为它代表完整 Call lifecycle 的 terminal completion。

---

### 2.13.1 CallCompletion copy/move

copy/move 能力自然由成员决定。

如果：

```text
Response move-only
```

则：

```text
CallCompletion<Response>
→ move-only
```

如果所有成员 copyable：

```text
CallCompletion
→ copyable
```

不额外人为限制。

---

### 2.13.2 ClientCallInfo

```cpp
enum class CallInfoCompleteness :
    std::uint8_t {
    Complete,
    Partial,
};
```

```cpp
class ClientCallInfo {
public:
    CallInfoCompleteness
    completeness() const noexcept;

    template <typename T>
    const T* As() const noexcept;
};
```

---

### 2.13.3 Complete

正常 terminal path：

```text
应有的 completion metadata 已取得
```

通常：

```text
Complete
```

---

### 2.13.4 Partial

以下情况可产生：

```text
Timeout
Cancelled
RuntimeStopped
Executor rejection
MiddlewareError
其他提前终止
```

此时：

```text
Partial
```

Partial 不代表：

```text
Info 完全为空
```

而表示：

> 只保留终止前实际已经获得的 metadata。

---

### 2.13.5 Partial 与 `As<T>()`

当：

```cpp
info.completeness()
    == CallInfoCompleteness::Partial
```

时：

```cpp
info.As<grpc::ClientCallInfo>()
```

允许返回：

```text
nullptr
```

因此用户需要分别检查：

```text
completeness
As<T>()
具体 optional fields
```

---

### 2.13.6 Future 完成时机

正常：

```text
Call terminal result
+
正常应取得的 completion metadata 已确定
    ↓
Future ready
```

异常：

```text
Timeout
Cancelled
RuntimeStopped
ResourceExhausted
...
    ↓
local terminal state determined
    ↓
Future immediately ready
    ↓
ClientCallInfo may be Partial
```

不无限等待：

```text
remote trailing metadata
```

---

## 2.14 Service Server API

V1 common Service：

```text
Unary Request
      ↓
terminal Response / Error
```

Server 提供两条模型：

```text
Simple Handler
Explicit ResponseHandler
```

---

### 2.14.1 Simple Handler

```cpp
using Handler =
    std::function<
        Result<Response>(
            const Request&,
            const ServerCallInfo&)>;
```

概念：

```text
Request
    ↓
Handler
    ↓
Result<Response>
    ↓
RoComm 自动执行 Response operation
```

Simple Handler 使用默认空 `ResponseContext`。

---

### 2.14.2 ServerCallInfo

`ServerCallInfo`：

> **handler-scoped borrowed view**

以下只保证到 handler invocation 返回：

```text
ServerCallInfo&
ServerCallInfo::As<T>() pointer
其中的 View / pointer / string_view
```

如果 ResponseHandler 将工作异步移交：

```text
必须复制需要长期使用的 request / metadata value
```

---

### 2.14.3 ResponseHandler

```cpp
using ResponseHandler =
    std::function<
        void(
            const Request&,
            const ServerCallInfo&,
            ResponseSender<Response>)>;
```

用于：

```text
异步 response
延迟 response
ResponseContext
显式 terminal error
```

`ResponseHandler` 的定义是：

> **Server-side handler with explicit response control。**

---

### 2.14.4 ResponseSender API

```cpp
template <typename Response>
class ResponseSender {
public:
    ResponseSender(
        const ResponseSender&) = delete;

    ResponseSender&
    operator=(
        const ResponseSender&) = delete;

    ResponseSender(
        ResponseSender&&) noexcept;

    ResponseSender&
    operator=(
        ResponseSender&&) noexcept;

    explicit operator bool()
        const noexcept;

    bool IsCancelled()
        const noexcept;

    Status Send(
        const Response& response,
        const ResponseContext& context = {});

    Status Fail(
        Status error,
        const ResponseContext& context = {});
};
```

---

### 2.14.5 ResponseSender 状态空间

ResponseSender 自身：

```text
Attached
Consumed
MovedFrom
```

CallState：

```text
Pending
Completed
Cancelled
Timeout
MiddlewareError
RuntimeStopped
...
```

两者不是同一个状态空间。

所以合法：

```text
ResponseSender = Attached
CallState = Cancelled
```

此时：

```text
operator bool() == true
IsCancelled() == true
```

---

### 2.14.6 `operator bool()`

表示：

> ResponseSender 当前是否仍持有一个 CallState attachment，且没有因 move 或 successful Send/Fail 被消费。

因此：

```text
moved-from
→ false

successful Send
→ false

successful Fail
→ false
```

但：

```text
Cancelled
Timeout
RuntimeStopped
```

等 external terminal state 不要求立即把 sender 清空。

所以：

```text
operator bool() == true
```

不表示：

```text
当前一定还能成功发送 response
```

---

### 2.14.7 `IsCancelled()`

只表示：

> Client-side cancellation 是否已经成为 Call terminal state。

如果 sender：

```text
Consumed
MovedFrom
```

则：

```text
IsCancelled() == false
```

它不表示：

```text
Timeout
RuntimeStopped
Completed
```

---

### 2.14.8 `Send()`

`Send()` 是 initiating API。

返回前必须完成：

```text
ResponseContext validation
borrowed View/Ref materialization
Response ownership acquisition
local response submission acceptance
```

返回后：

```text
原 Response
原 ResponseContext
其中 borrowed data
```

可以立即销毁。

---

### 2.14.9 Successful Send

```text
Attached
    ↓
local response submission accepted
    ↓
Consumed
```

之后：

```text
operator bool() == false

Send()
→ InvalidState

Fail()
→ InvalidState
```

---

### 2.14.10 Validation / materialization failure

如果：

```text
ResponseContext invalid
Response materialization failed
本地预提交验证失败
```

则：

```text
返回对应 error
ResponseSender remains Attached
```

用户允许修正后重试。

---

### 2.14.11 External terminal state

如果 Call 已：

```text
Cancelled
Timeout
RuntimeStopped
```

此时：

```cpp
sender.Send(...);
sender.Fail(...);
```

分别返回：

```text
Cancelled
Timeout
RuntimeStopped
```

并且：

```text
ResponseSender remains Attached
```

不因为一次失败的发送尝试自动消费。

最终析构看到 Call 已 external-terminal：

```text
no-op
```

---

### 2.14.12 `Fail(Status error, ResponseContext)`

参数：

```text
error
→ 希望成为 Call terminal result 的逻辑错误
```

返回：

```text
Status
→ 本次 error submission 的本地提交结果
```

例如：

```cpp
auto status =
    sender.Fail(
        Status{
            StatusCode::InvalidArgument});
```

若：

```text
status == Ok
```

表示：

> `InvalidArgument` 已成功提交为 remote Call terminal result。

若：

```text
status == RuntimeStopped
```

表示：

> 该 logical error 没有成功提交到 response channel。

---

### 2.14.13 `Fail(Ok)`

```cpp
sender.Fail(Status{});
```

非法。

结果：

```text
InvalidArgument
```

且：

```text
ResponseSender remains Attached
```

---

### 2.14.14 单 terminal response 保证

第一次 successful：

```text
Send
或
Fail
```

终止 response-side sender。

后续：

```text
Send / Fail
→ InvalidState
```

---

### 2.14.15 ResponseSender 与 Executor

`ServerOptions::executor` 只决定：

```text
Server Handler / ResponseHandler 初始 invocation
```

`ResponseSender::Send()` / `Fail()`：

> 在当前调用线程完成 synchronous local submission。

不会自动切回：

```text
Server Executor
```

因此可以：

```cpp
other_executor.Execute(
    [sender = std::move(sender),
     response = std::move(response)]() mutable {

        sender.Send(response);
    });
```

---

### 2.14.16 ResponseSender abandonment

如果：

```text
ResponseSender == Attached
CallState == Pending
sender destructor
```

并且 abandonment 赢得 terminal CAS：

```text
CallState
→ InternalError
```

Diagnostic：

```text
"ResponseSender destroyed without terminal response"
```

如果 Call 已：

```text
Cancelled
Timeout
RuntimeStopped
```

则析构：

```text
no-op
```

不覆盖已有 terminal reason。

Consumed / MovedFrom sender：

```text
destructor no-op
```

---

### 2.14.17 Abandonment 在 Portable 模式下

`ServiceErrorMode::Portable`：

```text
RoComm Client
→ Result<Response>{InternalError}

ClientCallInfo
→ Partial
```

因此：

> **Portable 模式下，abandoned ResponseSender 不允许让 RoComm Client 永久等待。**

---

### 2.14.18 Abandonment 在 NativeCompatible 模式下

如果 native protocol 无独立 error status：

```text
Server local CallState
→ InternalError
```

但 native remote peer 不保证能够立刻收到这个 InternalError。

remote 可能只能观察：

```text
timeout
transport-specific failure
```

因此：

> NativeCompatible 模式必须配置合理 deadline，或者把业务 error 显式编码进 native Response schema。

---

### 2.14.19 Move assignment

```cpp
ResponseSender a = /* call A */;
ResponseSender b = /* call B */;

a = std::move(b);
```

采用：

> **release-before-acquire**

步骤：

```text
1. release A attachment
2. 如果 A 仍 Pending
   → abandonment semantics
3. a acquire B
4. b → MovedFrom
```

---

## 2.15 ServiceErrorMode

```cpp
enum class ServiceErrorMode {
    Portable,
    NativeCompatible,
};
```

---

### 2.15.1 Portable

要求 target integration 能够表达：

```text
successful Response
+
terminal Status error
```

实现可以使用：

```text
native error status
RoComm-specific envelope
Middleware-specific protocol mapping
```

如果某 target 无法支持完整 Portable semantics：

```text
CreateClient / CreateServer
→ UnsupportedConfiguration
```

不得静默降级。

默认：

```text
Portable
```

因为它代表完整 RoComm Service contract。

---

### 2.15.2 NativeCompatible

优先：

```text
native schema interoperability
```

如果 native protocol 没有独立 error channel：

```cpp
sender.Fail(...)
```

返回：

```text
Unsupported
```

并且：

```text
sender remains Attached
```

Simple Handler 返回 failed `Result<Response>` 时：

```text
Server local CallState
→ Unsupported
```

RoComm 不伪造一个成功 Response 去承载错误。

如果应用需要 native peer 感知 domain error：

> **将业务错误编码进 Response schema。**

---

## 2.16 User exception contract

用户提供给 RoComm 的：

```text
Subscriber callback
Server Handler
ResponseHandler
Future continuation
RoComm 对外暴露的 user task
```

不得让 C++ exception 穿过 RoComm invocation boundary。

RoComm 必须统一：

```cpp
try {
    user_callable();
} catch (...) {
    // best-effort Critical log
    std::terminate();
}
```

不做：

```text
exception → Status
```

转换。

原因：

```text
不同 Executor / Middleware 行为一致
减少恢复状态机
提高可预测性
避免异常语义污染数据平面
```

业务错误通过：

```text
Result<Response>
ResponseSender::Fail()
Future result/error channel
```

表达。

---

# 3. Core Runtime、生命周期、异步执行与日志

## 3.1 RuntimeConfig

Phase 0 正式公共语义：

```cpp
class RuntimeConfig {
public:
    Result<MiddlewareId>
    AddMiddleware(MiddlewareSpec spec);

    RuntimeConfig&
    SetDefaultExecutor(
        std::shared_ptr<Executor> executor);

    RuntimeConfig&
    SetDefaultTopicTargets(
        std::vector<MiddlewareId> targets);

    RuntimeConfig&
    SetDefaultServiceTarget(
        MiddlewareId target);

    RuntimeConfig&
    AddTopicRoute(
        TopicRouteRule rule);

    RuntimeConfig&
    AddServiceRoute(
        ServiceRouteRule rule);

    RuntimeConfig&
    SetLogging(
        LoggingConfig config);

    RuntimeConfig&
    SetShutdownTimeout(
        Duration timeout);
};
```

RuntimeConfig 不提供：

```text
RegisterType<T>()
```

或：

```text
initial type list
```

Type 通过：

```text
type_traits<T>
```

compile-time opt-in。

---

## 3.2 MiddlewareSpec

`MiddlewareSpec` 是：

> Middleware instance 的创建描述。

至少逻辑包含：

```text
Middleware logical name
factory / construction function
middleware-specific initial configuration
```

例如：

```cpp
auto fastdds =
    config.AddMiddleware(
        rocomm::fastdds::MakeMiddlewareSpec(
            fastdds_config));

auto grpc =
    config.AddMiddleware(
        rocomm::grpc::MakeMiddlewareSpec(
            grpc_config));
```

同一个 RuntimeConfig 内：

```text
Middleware logical name
```

必须唯一。

---

## 3.3 Routing

V1 Route 只支持：

> **exact resource-name matching**

不在 V1 引入：

```text
regex
glob
route priority language
dynamic script
policy engine
```

---

### 3.3.1 TopicRouteRule

概念：

```cpp
struct TopicRouteRule {
    std::string topic;
    std::vector<MiddlewareId> targets;
};
```

Topic：

```text
0..N targets
```

---

### 3.3.2 ServiceRouteRule

概念：

```cpp
struct ServiceRouteRule {
    std::string service;
    MiddlewareId target;
};
```

Service：

```text
exactly one target
```

---

## 3.4 Default Executor

如果用户未设置：

```text
RuntimeConfig default Executor
```

Runtime 必须创建内部 default Executor。

V1 推荐默认实现：

```text
SingleThreadExecutor
```

关键契约不是具体类型，而是：

> **Middleware I/O thread 不直接调用用户 callback。**

---

## 3.5 Runtime 创建 API

不采用：

```cpp
Runtime runtime(config);
```

作为可能失败的创建 API。

正式模型：

```cpp
class Runtime {
public:
    Runtime(
        const Runtime&) = delete;

    Runtime&
    operator=(
        const Runtime&) = delete;

    Runtime(Runtime&&) noexcept;

    Runtime&
    operator=(Runtime&&) noexcept;

    static Result<Runtime>
    Create(RuntimeConfig config);

    Status Stop() noexcept;

    Logger GetLogger(
        std::string_view category) const;

    ~Runtime() noexcept;
};
```

---

## 3.6 Runtime::Create()

典型：

```cpp
auto runtime_result =
    rocomm::Runtime::Create(
        std::move(config));

if (!runtime_result) {
    return runtime_result.status();
}

auto runtime =
    std::move(runtime_result).value();
```

成功返回：

```text
Runtime == Running
```

内部仍可以经过：

```text
Created
→ Configured
→ Initialized
→ Running
```

任意阶段失败：

```text
rollback 已创建资源
返回 failure
不暴露半初始化 Runtime
```

---

## 3.7 Runtime lifecycle

```text
Created
   ↓
Configured
   ↓
Initialized
   ↓
Running
   ↓
Stopping
   ↓
Stopped
```

成功交给用户的 Runtime 已：

```text
Running
```

因此：

```text
Created
Configured
Initialized
```

主要是内部状态。

---

## 3.8 Runtime::Stop()

概念：

```text
1. linearize Running → Stopping
2. reject new Create*
3. reject new operation
4. terminalize / cancel pending operation
5. stop accepting new receive dispatch
6. drain / cancel queued user work
7. wait for in-flight user invocation
8. shutdown Middleware
9. drain / flush logging
10. transition logical RuntimeState → Stopped
```

---

### 3.8.1 Stop 幂等

已经：

```text
Stopped
```

再次：

```cpp
Stop();
```

返回：

```text
Ok
```

---

### 3.8.2 Concurrent Stop

多个线程同时调用：

```cpp
Stop()
```

必须 join：

```text
same shutdown operation
```

最终观察相同的：

```text
Stopped state
shutdown terminal outcome
```

---

### 3.8.3 Shutdown timeout

`RuntimeConfig::SetShutdownTimeout()` 控制 graceful shutdown deadline。

若超时：

```text
停止继续等待
logical RuntimeState → Stopped
Stop() → Timeout + Diagnostic
```

禁止：

```text
永久停在 Stopping
```

---

## 3.9 Stopped 与 quiescent

C++17 无法安全强制终止正在执行的用户代码。

因此 RoComm 必须区分：

```text
logical Stopped
quiescent
```

---

### 3.9.1 `Stop() == Ok`

表示完整 quiescence：

```text
不接受新 operation
不产生新 callback dispatch
queued work 已 drain/cancel
所有已开始 user invocation 已返回
Middleware shutdown 完成
要求的 logging flush 完成
RuntimeState == Stopped
```

---

### 3.9.2 `Stop() == Timeout`

表示：

```text
Runtime 不再接受新 operation
不再产生新 dispatch
pending call 已 terminalize/cancel
logical RuntimeState == Stopped
```

但仍可能有：

```text
已经运行中的 callback
已经运行中的 Handler / continuation
阻塞中的 custom LogSink
```

尚未返回。

RoComm 不允许：

```text
pthread_cancel
unsafe thread termination
强制杀掉用户线程
提前释放仍被执行路径引用的 state
```

正在执行的 invocation 必须持有必要：

```text
execution lease
shared backing
equivalent lifetime token
```

直到自然返回。

因此：

> **Stopped 是逻辑生命周期状态；quiescent 表示所有 in-flight user work 已退出。**

`Stop() == Timeout` 不承诺 quiescent。

---

## 3.10 Runtime destructor

如果用户没有显式：

```cpp
Stop();
```

则：

```text
~Runtime()
→ 执行 shutdown
```

析构不能返回 Status。

如果 shutdown timeout：

```text
best-effort Error/Critical log
Runtime wrapper 可以销毁
必要 internal state 继续存活
```

直到：

```text
in-flight execution / blocking sink
```

自然返回。

不得 unsafe kill。

---

## 3.11 Publisher / Subscriber / Client / Server ownership

四类公共类型：

```text
move-only RAII logical owner
```

例如：

```cpp
Publisher(
    const Publisher&) = delete;

Publisher&
operator=(
    const Publisher&) = delete;

Publisher(
    Publisher&&) noexcept;

Publisher&
operator=(
    Publisher&&) noexcept;
```

---

### 3.11.1 Moved-from state

moved-from：

```text
operator bool() → false

As<T>()
→ nullptr
  （仅对 compile-time 合法 capability query）

destructor
→ no-op
```

需要 live state 的 operation：

```text
InvalidHandle
```

例如：

```text
Publisher::Publish
→ InvalidHandle

Client::AsyncCall
→ immediately completed error / InvalidHandle semantics
```

exact Future construction 由 Header Finalization 决定。

---

## 3.12 不使用 HandleBase

禁止：

```cpp
class HandleBase;
class EndpointBase;
class CommunicationBase;
```

并让：

```text
Publisher / Subscriber / Client / Server
```

继承它们。

原则：

> **共享实现不自动要求共享基类。**

只有出现真正 runtime polymorphism 职责时才建立职责明确的内部 interface。

---

## 3.13 Internal Runtime state

允许：

```cpp
namespace rocomm::runtime {

class RuntimeState;
class EndpointState;
class EndpointRegistry;

}
```

删除：

```text
RuntimeBinding
```

因为当前没有独立职责。

---

## 3.14 Specific state 与 EndpointState

具体领域：

```text
topic::PublisherState
topic::SubscriberState

service::ClientState
service::ServerState
```

与：

```text
runtime::EndpointState
```

采用：

```text
composition / association
```

概念：

```text
Publisher
    ↓ strong
topic::PublisherState
    ↓ strong
runtime::EndpointState
    ↓ strong
runtime::RuntimeState
```

禁止：

```cpp
class PublisherState
    : public runtime::EndpointState {
};
```

---

## 3.15 RuntimeState ownership

Runtime wrapper：

```text
strong → RuntimeState
```

每个 live EndpointState：

```text
strong → same RuntimeState
```

因此：

```text
Runtime wrapper destroyed
```

以后，只要还有：

```text
Publisher
Subscriber
Client
Server
```

相关 EndpointState：

```text
RuntimeState backing 仍存活
```

但：

```text
RuntimeState == Stopped
```

所以残留对象只保留：

> safe stopped runtime state

不是继续 Running。

---

## 3.16 EndpointRegistry ownership

RuntimeState owns：

```text
EndpointRegistry
```

Registry 不强拥有：

```text
PublisherState
SubscriberState
ClientState
ServerState
```

推荐概念：

```text
RuntimeState
    ↓ owns
EndpointRegistry
    ↓ weak/reference
EndpointState

specific state
    ↓ strong
EndpointState
    ↓ strong
RuntimeState
```

不得形成：

```text
RuntimeState ↔ EndpointState
```

strong cycle。

---

### 3.16.1 Registry weak/reference cleanup

必须定义：

```text
expired entry cleanup
concurrent Endpoint destruction safety
Stop traversal safety
no dangling access
```

具体实现可为：

```text
weak_ptr
generation slot
intrusive weak reference
lock-protected record
```

不冻结。

---

## 3.17 EndpointRegistry concurrency

Registry 必须支持：

```text
并发 Create* registration
并发 Endpoint destruction
Runtime::Stop traversal
diagnostics/accounting
```

---

### 3.17.1 Create* 与 Stop 线性化

若 Create 先赢：

```text
Create 成功返回之前
EndpointState 已完整注册
```

因此后续 Stop：

```text
必须观察并关闭该对象
```

若 Stop 先进入：

```text
Stopping
```

则新：

```text
Create*
→ RuntimeStopped
```

禁止：

```text
Create 返回成功
但 Stop registry 中不可见
```

---

## 3.18 Executor

Middleware I/O 不直接调用用户 callback。

路径：

```text
Middleware I/O
    ↓
receive / operation machinery
    ↓
execution::DispatchEnvelope
    ↓
Executor
    ↓
user callback / Handler / ResponseHandler
```

---

### 3.18.1 Executor interface

概念：

```cpp
class Executor {
public:
    virtual ~Executor() = default;

    virtual Status
    Execute(Task) = 0;

    virtual Status
    ExecuteAt(
        TimePoint,
        Task) = 0;

    virtual bool
    IsCurrentThread()
        const noexcept = 0;
};
```

V1 候选：

```text
InlineExecutor
SingleThreadExecutor
ThreadPoolExecutor
SerialExecutor / Strand
```

---

### 3.18.2 Executor 作用范围

```text
Runtime default Executor
→ default callback / Handler execution

SubscriberOptions executor
→ Subscriber callback override

ServerOptions executor
→ Handler / ResponseHandler override

Future::Then(executor, ...)
→ continuation target
```

Executor 不规定：

```text
Middleware I/O thread
deserialization worker
internal queue drain
MessageFrame construction
```

必须使用同一线程。

---

### 3.18.3 Subscriber Executor rejection

如果：

```text
Execute()
→ ResourceExhausted
```

则：

```text
callback 不执行
MessageInfo 不进入用户 callback
delivery 生命周期结束
subscriber executor_rejected counter++
按日志 policy 记录
```

不反向修改已经完成的发送侧 Publish Status。

---

### 3.18.4 Subscriber statistics

建议使用职责明确 Capability：

```text
SubscriberStatistics
```

而不是含义过宽：

```text
Statistics
```

概念 snapshot：

```cpp
struct SubscriberStatisticsSnapshot {
    std::uint64_t delivered = 0;
    std::uint64_t executor_rejected = 0;
};
```

具体 API 在 Capability Header Finalization 时定稿。

---

### 3.18.5 Server Handler Executor rejection

若：

```text
Server Executor
→ ResourceExhausted
```

则 Call terminal：

```text
ResourceExhausted
```

Diagnostic：

```text
"Executor rejected server handler task"
```

RoComm Client：

```text
Result<Response>
→ ResourceExhausted

ClientCallInfo
→ Partial
```

不能：

```text
silent drop
```

否则 Client 会永久等待。

---

## 3.19 User callable exception boundary

用户提供：

```text
Subscriber callback
Handler
ResponseHandler
Future continuation
RoComm 对外暴露的 user task
```

不得让 exception 穿过 RoComm invocation boundary。

实现语义：

```cpp
try {
    user_callable();
} catch (...) {
    // best-effort Critical log
    std::terminate();
}
```

不：

```text
吞掉异常
转换成 Status
继续执行
```

这是一致性的 fail-fast contract。

---

## 3.20 Future / Promise / Timer

V1 保留基础异步 primitive：

```text
Future
Promise
Timer
CancellationToken
CancellationSource
```

架构层至少要求：

```text
Ready
Wait
Get
Then
Cancel
```

Continuation：

```text
executor-aware
```

RoComm 不使用：

```cpp
std::future
```

作为主 Runtime Future abstraction。

---

### 3.20.1 目前仍待 exact header 冻结的 Future 事项

以下不能由 Phase 1 实现者自行决定：

```text
Future move-only 还是 shared/copyable
Ready() exact signature
Wait() exact signature
WaitFor() exact signature
Get() 是否 consume
多次 Get 是否允许
Then exact template signature
Then return type
Then 是否 flatten Future
continuation error propagation
continuation cancellation
Promise set-once
broken Promise
Executor rejection 对 continuation 的影响
Timer 创建 API
Timer callback/Future API
Timer cancellation race
```

这些属于：

> **Phase 0 Public Header Finalization**

在此之前不开始 Future concrete implementation。

---

### 3.20.2 Service Call cancellation 已冻结部分

CallState：

```text
Created
   ↓
Pending
   ├── Completed
   ├── Timeout
   ├── Cancelled
   ├── MiddlewareError
   └── RuntimeShutdown
```

第一个合法 terminal transition 获胜。

`Future::Cancel()` 针对 Service Call：

```cpp
bool Cancel() noexcept;
```

如果 Cancel CAS 赢：

```text
true
Future immediately ready
local Call terminal = Cancelled
```

`AsyncCall()`：

```text
Result<Response>
→ Cancelled
```

`AsyncCallWithInfo()`：

```text
result → Cancelled
info → Partial
```

不等待 remote cancellation。

---

### 3.20.3 Completion wins

如果：

```text
normal completion
timeout
Runtime shutdown
```

先 terminalize：

```text
Cancel() → false
```

已有结果不被覆盖。

---

### 3.20.4 Native cancellation

如果 native Middleware 支持：

```text
gRPC TryCancel
native request cancel
```

RoComm 可以发起：

```text
best-effort native cancellation
```

但公共契约只保证：

```text
local Call cancelled
```

不提供：

```text
cancelled_remote
```

通用 flag。

---

## 3.21 Logging

Logging 是 RoComm Core 正式基础组件。

领域：

```text
rocomm::logging
```

不是：

```text
RuntimeConfig 中几个临时 logging 字段
```

---

### 3.21.1 Logging 目标

必须支持：

```text
Runtime-local configuration
category-based filtering
多个 sink
同步 / 异步模式
bounded async queue
明确 overflow policy
source location
flush
custom sink
Middleware / Core 共用
无 exception propagation
```

Logging：

> 不改变通信 operation 的 Status 语义。

---

### 3.21.2 LogLevel

```cpp
enum class LogLevel : std::uint8_t {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
    Off,
};
```

---

### 3.21.3 SourceLocation

C++17 不使用：

```cpp
std::source_location
```

定义：

```cpp
struct SourceLocation {
    const char* file = nullptr;
    const char* function = nullptr;
    std::uint32_t line = 0;
};
```

可由：

```text
__FILE__
__func__
__LINE__
```

构造。

---

### 3.21.4 LogRecord

概念：

```cpp
struct LogRecord {
    Timestamp timestamp;
    LogLevel level;

    std::string category;
    std::string message;

    SourceLocation source;
    std::thread::id thread_id;
};
```

异步 logging 必须拥有：

```text
category
message
```

storage。

不得异步保存 caller-owned：

```text
string_view
format arg reference
```

---

### 3.21.5 LogSink

```cpp
class LogSink {
public:
    virtual ~LogSink() = default;

    virtual void
    Write(const LogRecord&) noexcept = 0;

    virtual void
    Flush() noexcept = 0;
};
```

Sink 不允许 exception 穿过 Logging boundary。

---

### 3.21.6 Built-in sinks

V1 至少提供：

```text
ConsoleSink
FileSink
RotatingFileSink
NullSink
```

RotatingFileSink 至少支持：

```text
maximum file size
maximum retained file count
```

具体 rotation 算法可以调优。

---

### 3.21.7 LoggingMode

```cpp
enum class LoggingMode {
    Synchronous,
    Asynchronous,
};
```

---

### 3.21.8 LogOverflowPolicy

```cpp
enum class LogOverflowPolicy {
    DropNewest,
    DropOldest,
    Block,
};
```

默认：

```text
DropNewest
```

避免 logging queue 对通信路径产生无界阻塞。

`Block` 必须由用户显式选择。

---

### 3.21.9 LoggingConfig

概念：

```cpp
class LoggingConfig {
public:
    LoggingConfig&
    SetDefaultLevel(
        LogLevel level);

    LoggingConfig&
    SetCategoryLevel(
        std::string category,
        LogLevel level);

    LoggingConfig&
    AddSink(
        std::shared_ptr<LogSink> sink);

    LoggingConfig&
    SetMode(
        LoggingMode mode);

    LoggingConfig&
    SetQueueCapacity(
        std::size_t capacity);

    LoggingConfig&
    SetOverflowPolicy(
        LogOverflowPolicy policy);

    LoggingConfig&
    SetFlushLevel(
        LogLevel level);
};
```

exact header 可在 Public Header Finalization 细调，但上述能力是 V1 contract。

---

### 3.21.10 Category hierarchy

正式 category 至少包含：

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

Category override 使用：

> **longest matching dot-prefix**

例如：

```text
default = Info
rocomm = Debug
rocomm.fastdds = Trace
```

则：

```text
rocomm.fastdds.discovery
→ Trace
```

---

### 3.21.11 Logger

概念：

```cpp
class Logger {
public:
    bool Enabled(
        LogLevel level) const noexcept;

    void Log(
        LogLevel level,
        std::string_view message,
        SourceLocation source = {}) const noexcept;

    void Flush() const noexcept;
};
```

Runtime：

```cpp
auto logger =
    runtime.GetLogger(
        "rocomm.my_component");
```

---

### 3.21.12 Runtime-local ownership

每个 RuntimeState 持有：

```text
LoggingContext
```

禁止使用：

```text
global mutable logger singleton
```

因此多个 Runtime 可分别使用：

```text
不同 level
不同 sink
不同输出文件
不同 test capture
```

---

### 3.21.13 Runtime Create 期间日志

`Runtime::Create()` 必须先建立：

```text
LoggingContext
```

然后：

```text
Middleware Initialize
Route validation
Executor startup
Type binding resolution
```

才能统一使用 Runtime logging。

LoggingConfig 无效：

```text
Runtime::Create()
→ failure
```

---

### 3.21.14 日志格式化位置

V1 冻结：

> **message formatting 发生在调用线程，并且只在 `Enabled(level)` 成功之后发生。**

Async：

```text
caller
  ↓
Enabled(level)
  ↓
format final message
  ↓
materialize owned LogRecord
  ↓
bounded queue
  ↓
logging worker
  ↓
LogSink
```

V1 不实现：

```text
queue raw format args
后台 type-erased formatting
```

避免：

```text
format arg lifetime
复杂 formatter ownership
额外 type-erasure layer
```

---

### 3.21.15 Hot-path logging

成功通信 hot path 不应无条件格式化：

```cpp
if (logger.Enabled(LogLevel::Debug)) {
    // formatting / message construction
}
```

disabled log path 应保持低成本。

---

### 3.21.16 Sink blocking

Synchronous mode：

```text
LogSink::Write()
```

允许阻塞当前调用线程。

这是用户主动选择 synchronous logging 的代价。

Asynchronous mode：

```text
Logger::Log()
```

不得直接执行 Sink I/O。

Sink I/O 只在 logging worker。

---

### 3.21.17 Custom Sink contract

自定义：

```text
Write()
Flush()
```

不得无限阻塞。

但 RoComm 无法安全强杀违反该 contract 的第三方 Sink。

Stop 等待 logging worker 超时：

```text
Stop() → Timeout
```

RoComm：

```text
停止等待
不 unsafe terminate sink thread
保留必要 backing state
```

直到 sink 返回。

这与 Runtime quiescence 模型一致。

---

### 3.21.18 Logging failure

Sink failure：

```text
不能改变 Publish Status
不能改变 AsyncCall Status
不能改变 ResponseSender::Send Status
不能抛 exception 越过 Logging boundary
```

Built-in sink 应维护：

```text
logging error counter
```

Error / Critical 可以尝试：

```text
emergency stderr fallback
```

但必须防：

```text
recursive logging
```

---

### 3.21.19 Logging 与 fail-fast

以下场景在 terminate 前 best-effort 记录：

```text
user callback throws
Handler throws
ResponseHandler throws
Future continuation throws
internal unrecoverable invariant failure
```

推荐：

```text
Critical
```

但：

> Logging failure 不能阻止最终 `std::terminate()`。

---

# 4. Middleware SPI、类型系统与能力扩展

## 4.1 Middleware 是独立 integration

```text
FastDdsMiddleware
CycloneDdsMiddleware
Ros2Middleware
ZenohMiddleware
GrpcMiddleware
```

Core 不建立：

```text
DdsMiddleware
├── FastDDS
├── CycloneDDS
└── Connext
```

这样的公共语义继承体系。

DDS family 可以共享内部 helper：

```text
QoS mapping
CDR helper
DDS request/reply helper
metadata conversion
```

原则：

> **share implementation, not Core semantics。**

---

## 4.2 ROS2 是独立 Middleware integration

```text
Ros2Middleware
      ↓
   rcl / rmw
      ↓
FastDDS / CycloneDDS / Zenoh / ...
```

因此：

```text
FastDdsMiddleware
```

与：

```text
Ros2Middleware
```

是两个独立 integration。

RoComm ROS2 integration 不通过：

```text
直接复用 FastDdsMiddleware
```

来模拟 ROS2 语义。

---

## 4.3 Middleware lifecycle semantic model

冻结 lifecycle：

```text
construct
Initialize
Start
Shutdown
```

V1 不再引入职责模糊的：

```text
MiddlewareOptions
```

作为第二套 generic Options abstraction。

Middleware-specific initialization config 已由：

```text
MiddlewareSpec
```

捕获。

---

## 4.4 MiddlewareInitContext

Initialize 只接收 Runtime 注入资源。

概念至少包括：

```text
MiddlewareId
Logger
必要 Runtime service references
```

例如：

```cpp
class MiddlewareInitContext {
public:
    MiddlewareId id() const noexcept;

    Logger GetLogger(
        std::string_view category) const;

    // other exact accessors:
    // Phase 0 Header Finalization
};
```

Middleware semantic interface：

```cpp
class Middleware {
public:
    virtual ~Middleware() = default;

    virtual std::string_view
    Name() const noexcept = 0;

    virtual Status
    Initialize(
        const MiddlewareInitContext&) = 0;

    virtual Status
    Start() = 0;

    virtual void
    Shutdown() noexcept = 0;
};
```

Exact C++ signature 在 Header Finalization 定稿。

---

## 4.5 Middleware Capability

Middleware 是否支持：

```text
Topic
Service
```

不通过 giant interface。

而通过：

```cpp
middleware.As<TopicInterface>();
middleware.As<ServiceInterface>();
```

因此合法：

```text
FastDDS
├── TopicInterface
└── ServiceInterface

Zenoh
├── TopicInterface
└── ServiceInterface

gRPC
└── ServiceInterface
```

能力不对称是正常状态。

---

## 4.6 TopicInterface / ServiceInterface

它们属于：

```text
Middleware SPI
```

不是普通 Application API。

目前冻结的是 semantic model：

```text
Topic create / destroy / publish
Subscriber receive ingress

Service client/server create/destroy
Request invocation
Response completion

owned frame semantics
opaque native-resource token
```

而不是带 `...` 的伪 exact signature。

Exact declaration 必须在：

> **Phase 0 Public Header Finalization**

完成。

---

## 4.7 SPI Handle

SPI 内允许：

```text
PublisherHandle
SubscriberHandle
ClientHandle
ServerHandle
```

这些是：

> **Middleware resource opaque token。**

不是：

```text
Publisher / Subscriber / Client / Server
```

的公共 umbrella handle。

必须具有：

```text
invalid/null state
stale generation detection 或等价保护
明确 destroy ownership
```

以下错误统一：

```text
invalid handle
stale generation
detected use-after-destroy
→ InvalidHandle
```

`MiddlewareError` 仅表示：

> handle 合法，但 native Middleware operation 执行失败。

---

## 4.8 MessageFrame

`transport::MessageFrame` 是：

> **logically immutable、caller-independent owned frame。**

概念：

```text
MessageFrame
├── TypeDescriptor
├── logical payload owner/reference
├── serialized payload cache
└── internal transport metadata
```

一旦进入 Middleware SPI：

```text
不再依赖 initiating caller lifetime
```

因此 Middleware 可以：

```text
在 SPI call 返回后继续异步发送
```

---

### 4.8.1 Logical immutability

MessageFrame 的 logical message：

```text
immutable
```

Middleware integration 不允许修改它所表达的应用层 message 语义。

---

### 4.8.2 Serialization cache

内部 serialized cache 可以：

```text
lazy populate
```

但必须：

```text
thread-safe
对外不可观察
不改变 logical message
```

这样 Topic fan-out 可以：

```text
共享同一个 MessageFrame backing
```

而不是每个 target 重复 materialize caller data。

---

## 4.9 Type opt-in

默认：

```cpp
template <typename T>
struct type_traits {
    static constexpr bool enabled = false;
};
```

应用类型：

```cpp
template <>
struct type_traits<RobotState> {
    static constexpr bool enabled = true;

    static constexpr std::string_view
        canonical_name =
            "robot.state.v1";

    inline static constexpr
        TypeDescriptor descriptor{
            StableHash64(canonical_name),
            canonical_name
        };

    static const TypeSupport&
    support() noexcept;
};
```

---

## 4.10 Unregistered type

例如：

```cpp
runtime.CreatePublisher<UnknownType>(...);
```

若：

```text
type_traits<UnknownType>::enabled == false
```

则：

```text
compile-time failure
```

至少产生：

```text
T is not registered as a RoComm type;
specialize type_traits<T>.
```

Service：

```text
Request
Response
```

都必须 compile-time opt-in。

---

## 4.11 不提供 public TypeRegistry registration API

V1 不提供：

```cpp
runtime.RegisterType<T>();
config.RegisterType<T>();
```

类型发现依赖：

```text
type_traits<T>
```

内部：

```text
rocomm::type::Registry
```

负责：

```text
MiddlewareTypeBinding cache
TypeSupport lookup cache
conversion lookup
```

不是 public registration subsystem。

---

## 4.12 MiddlewareTypeBinding resolution

Middleware Initialize 不要求提前知道：

```text
应用以后会创建哪些 T
```

流程：

```text
CreatePublisher<T>()
        ↓
type_traits<T>
        ↓
TypeDescriptor + TypeSupport
        ↓
resolved Middleware target
        ↓
resolve MiddlewareTypeBinding
        ↓
cache in rocomm::type::Registry
```

Service Request / Response 同理。

---

### 4.12.1 Binding failure

如果 target Middleware 无法为某类型建立 binding：

```text
Create*
→ Unsupported
```

并返回：

```text
target-specific Diagnostic
```

Topic fan-out：

```text
任一 resolved target binding failure
→ logical Create 整体 failure
→ rollback previously created native resources
```

---

## 4.13 Type / Serialization / Binding / Conversion 分离

严格区分：

```text
Application Type
Serialization
Middleware Native Binding
Type Conversion
```

Core primitive：

```text
TypeDescriptor
TypeSupport
Encoding
SerializedPayload
```

---

### 4.13.1 TypeDescriptor

```cpp
struct TypeDescriptor {
    std::uint64_t local_key;
    std::string_view canonical_name;
};
```

Core identity 不使用：

```cpp
std::type_index
```

---

### 4.13.2 不同身份层

以下不是同一个 identity：

```text
RoComm TypeDescriptor
DDS type name
ROS2 interface name
protobuf fully-qualified name
wire schema identifier
```

Middleware binding：

```text
(TypeDescriptor, MiddlewareId)
          ↓
MiddlewareTypeBinding
```

---

### 4.13.3 Serialization

```text
T ↔ bytes
```

---

### 4.13.4 Conversion

```text
Type A ↔ Type B
```

使用独立：

```text
TypeConverterRegistry
```

不能把：

```text
serialization
conversion
```

混成一个万能转换接口。

---

## 4.14 Core / SPI 不泄漏 native type

portable headers 不直接暴露：

```text
DDS DataWriter
DDS native QoS
rclcpp::Node
rclcpp::QoS
rmw native handle
grpc::Channel
grpc::ClientContext
Zenoh Session
```

这些只存在于：

```text
Middleware-specific Options
Middleware-specific Context
Capability
Info
native escape hatch
```

---

## 4.15 Dynamic runtime attribute update

```cpp
enum class UpdateResult {
    Applied,
    Unsupported,
    Immutable,
    RequiresRecreation,
};
```

`RequiresRecreation`：

> Runtime 不自动重建 Publisher / Subscriber / Client / Server。

由用户决定：

```text
是否重建
何时重建
如何迁移状态
```

如果未来 setter 本身还可能失败，应考虑：

```text
Result<UpdateResult>
```

而不是强行让 enum 承担 error detail。

Exact capability setter API 在对应 Capability 设计时定稿。

---

## 4.16 Fast DDS Architecture Pressure Test

Creation-time：

```text
DataWriterQos
DataReaderQos
history
reliability
durability
deadline
liveliness
resource limits
Fast DDS resource options
```

→ Options

Per-publish：

```text
source timestamp
related sample identity
WriteParams
```

→ PublishContext

Runtime：

```text
GetQos
SetQos
WaitForAcknowledgments
matching
liveliness
loan
```

→ DDS / FastDDS Capability

Native：

```text
DataWriter*
```

→ FastDDS native Capability

**Architecture pressure-test: PASS**

该 PASS 只表示：

> 当前架构可以表达这些语义。

不代表：

```text
integration 已实现
已编译
已 native interop 验收
```

---

## 4.17 ROS2 Architecture Pressure Test

Creation-time：

```text
QoS
intra-process
publisher/subscription options
allocator
network flow config
```

→ Options

Runtime：

```text
actual QoS
publisher/subscription count
wait-for-all-acked
loan
```

→ ROS2 Capability

Native：

```text
rcl / rmw handle
```

→ ROS2 native Capability

Service native schema：

```text
NativeCompatible
```

可以保持 native interoperability。

**Architecture pressure-test: PASS**

---

## 4.18 Zenoh Architecture Pressure Test

Creation-time：

```text
priority
congestion
reliability
publisher/session config
```

→ Options

Per-operation：

```text
timestamp
attachment
query target
parameters
priority
timeout
```

→ Context + View/Ref

Runtime：

```text
matching
liveliness
query state
```

→ Capability

native multi-reply：

```text
不进入 V1 common unary Service
```

**Architecture pressure-test: PASS**

---

## 4.19 gRPC Architecture Pressure Test

Creation-time：

```text
endpoint
credentials
compression
message limits
load balancing
channel args
```

→ ClientOptions / ServerOptions

Request operation：

```text
deadline
metadata
wait_for_ready
authority
compression
credentials
```

→ RequestContext

Response operation：

```text
response metadata
status mapping
```

→ ResponseContext / Service Error mapping

`grpc::ClientContext`：

```text
one-call runtime state
```

不能成为：

```text
Client::As<T>()
```

Capability。

gRPC native Status 可以支持 Portable error mapping。

**Architecture pressure-test: PASS**

---

# 5. 工程结构、实施阶段与 Public Header Finalization

## 5.1 Repository layout

```text
rocomm/
├── CMakeLists.txt
├── README.md
├── LICENSE
│
├── include/
│   └── rocomm/
│       ├── runtime.hpp
│       ├── runtime_config.hpp
│       ├── status.hpp
│       ├── result.hpp
│       ├── scope.hpp
│       ├── middleware_id.hpp
│       │
│       ├── topic/
│       │   ├── publisher.hpp
│       │   ├── subscriber.hpp
│       │   ├── options.hpp
│       │   ├── publish_context.hpp
│       │   └── message_info.hpp
│       │
│       ├── service/
│       │   ├── client.hpp
│       │   ├── server.hpp
│       │   ├── options.hpp
│       │   ├── request_context.hpp
│       │   ├── response_context.hpp
│       │   ├── response_sender.hpp
│       │   ├── client_call_info.hpp
│       │   ├── server_call_info.hpp
│       │   ├── call_completion.hpp
│       │   └── service_error_mode.hpp
│       │
│       ├── async/
│       │   ├── future.hpp
│       │   ├── promise.hpp
│       │   ├── cancellation.hpp
│       │   └── timer.hpp
│       │
│       ├── executor/
│       │   ├── executor.hpp
│       │   ├── inline_executor.hpp
│       │   ├── single_thread_executor.hpp
│       │   ├── thread_pool_executor.hpp
│       │   └── serial_executor.hpp
│       │
│       ├── logging/
│       │   ├── logger.hpp
│       │   ├── log_level.hpp
│       │   ├── log_record.hpp
│       │   ├── log_sink.hpp
│       │   ├── logging_config.hpp
│       │   ├── console_sink.hpp
│       │   ├── file_sink.hpp
│       │   ├── rotating_file_sink.hpp
│       │   └── null_sink.hpp
│       │
│       ├── capability/
│       │   ├── descriptor.hpp
│       │   ├── traits.hpp
│       │   ├── match_status.hpp
│       │   └── ...
│       │
│       ├── info/
│       │   ├── descriptor.hpp
│       │   └── traits.hpp
│       │
│       ├── type/
│       │   ├── descriptor.hpp
│       │   ├── traits.hpp
│       │   ├── type_support.hpp
│       │   ├── encoding.hpp
│       │   └── serialized_payload.hpp
│       │
│       └── middleware/
│           ├── middleware.hpp
│           └── middleware_init_context.hpp
│
├── src/
│   ├── runtime/
│   │   ├── runtime_state.hpp
│   │   ├── endpoint_state.hpp
│   │   └── endpoint_registry.hpp
│   │
│   ├── topic/
│   │   ├── publisher_state.hpp
│   │   └── subscriber_state.hpp
│   │
│   ├── service/
│   │   ├── client_state.hpp
│   │   ├── server_state.hpp
│   │   └── call_state.hpp
│   │
│   ├── execution/
│   │   ├── dispatch_envelope.hpp
│   │   └── dispatch_queue.hpp
│   │
│   ├── async/
│   ├── logging/
│   ├── transport/
│   │   └── message_frame.hpp
│   │
│   ├── type/
│   ├── middleware/
│   ├── capability/
│   └── info/
│
├── middleware/
│   ├── local/
│   ├── fastdds/
│   ├── ros2/
│   ├── zenoh/
│   └── grpc/
│
├── examples/
├── tests/
├── benchmarks/
└── docs/
```

不因为命名对称而强制创建：

```text
service::ResponseState
```

如果：

```text
CallState
```

已经能够完整承担 ResponseSender backing，则不新增无独立职责类型。

原则：

> **有独立职责才创建独立类型。**

---

## 5.2 内部领域 namespace

使用：

```text
rocomm::runtime
rocomm::topic
rocomm::service
rocomm::execution
rocomm::async
rocomm::logging
rocomm::transport
rocomm::type
rocomm::middleware
rocomm::capability
rocomm::info
```

不使用：

```text
rocomm::detail
rocomm::internal
rocomm::impl
```

作为通用垃圾桶。

原则：

> **namespace 回答“属于什么领域”，类型名回答“在领域中承担什么职责”。**

---

## 5.3 禁止 mechanism-only 一级 namespace

不建立：

```text
rocomm::registry
```

统一装所有 Registry。

应使用：

```text
rocomm::middleware::Registry
rocomm::type::Registry
rocomm::capability::Table
```

---

## 5.4 Endpoint 内部术语边界

允许：

```text
runtime::EndpointState
runtime::EndpointRegistry
```

因为它们有明确 Runtime 内部含义。

禁止扩散：

```text
topic::EndpointPublisher
service::EndpointClient
type::EndpointTraits
execution::EndpointDispatcher
```

进入具体领域后恢复具体名称：

```text
PublisherState
SubscriberState
ClientState
ServerState
CallState
```

---

## 5.5 Public / Private boundary

Public：

```text
include/rocomm/**
```

Private：

```text
src/<domain>/**
```

CMake：

```cmake
target_include_directories(rocomm_core
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)
```

Shared library 默认：

```text
hidden visibility
```

公共非模板 symbol：

```text
ROCOMM_API
```

显式 export。

---

## 5.6 内部类型不泄漏 public header

优先：

```cpp
class Runtime {
private:
    struct State;
    std::shared_ptr<State> state_;
};
```

而不是在 installed header 中暴露：

```cpp
namespace rocomm::runtime {
class RuntimeState;
}
```

namespace 不是 access-control mechanism。

真正边界由：

```text
installed header
include path
symbol visibility
```

控制。

---

## 5.7 CMake targets

对外 target alias：

```text
rocomm::core
rocomm::local
rocomm::fastdds
rocomm::ros2
rocomm::zenoh
rocomm::grpc
```

未来：

```text
rocomm::cyclonedds
```

内部 build target：

```text
rocomm_core
rocomm_local
rocomm_fastdds
...
```

例如：

```cmake
add_library(rocomm_core ...)

add_library(
    rocomm::core
    ALIAS rocomm_core)
```

---

## 5.8 CMake options

至少：

```text
ROCOMM_BUILD_TESTS
ROCOMM_BUILD_EXAMPLES
ROCOMM_BUILD_BENCHMARKS

ROCOMM_BUILD_LOCAL
ROCOMM_BUILD_FASTDDS
ROCOMM_BUILD_ROS2
ROCOMM_BUILD_ZENOH
ROCOMM_BUILD_GRPC

ROCOMM_ENABLE_ASAN
ROCOMM_ENABLE_TSAN
ROCOMM_ENABLE_UBSAN
```

Feature definitions：

```text
ROCOMM_HAS_FASTDDS
ROCOMM_HAS_ROS2
ROCOMM_HAS_ZENOH
ROCOMM_HAS_GRPC
```

---

## 5.9 LocalMiddleware

LocalMiddleware 必须是：

> **reference implementation**

不是简单 mock。

完整覆盖：

```text
Topic pub/sub

Unary Service

Portable Service error

normal completion
timeout
cancellation

MessageInfo
ClientCallInfo
ServerCallInfo

Simple Handler
ResponseHandler
ResponseSender
ResponseContext

Executor dispatch
Executor rejection

Runtime Stop
Create*/Stop concurrency

Logging integration

Type binding
```

LocalMiddleware 是第一套通过全部：

```text
Common Contract Tests
```

的实现。

---

## 5.10 Phase 0 已冻结语义

已经冻结：

```text
项目命名

Status
Diagnostic
Result

RuntimeConfig
Runtime::Create
Runtime::Stop semantic model

MiddlewareId
MiddlewareSpec

TopicRouteRule
ServiceRouteRule

Publisher
Subscriber
Client
Server

object_scope
operation_scope
object_scope_set
operation_scope_set

Options
PublishContext
RequestContext
ResponseContext

Call / Request / Response naming model

Capability
Info

StableHash64
canonical-name identity

MessageInfo
ClientCallInfo
ServerCallInfo

Topic fan-out

Service single-target routing

ServiceErrorMode

ResponseSender semantic model

Call cancellation semantics

RuntimeState
EndpointState
EndpointRegistry

Create/Stop linearization

Executor dispatch semantics
Executor rejection semantics

User exception fail-fast semantics

Logging semantic model

Type compile-time opt-in

MiddlewareTypeBinding resolution

MessageFrame ownership semantics

Middleware SPI semantic model

领域 namespace 规则
```

---

## 5.11 Phase 0 Public Header Finalization

以下仍必须一次性定稿。

它们是：

```text
exact C++ API 问题
```

而不是重新打开 Architecture。

---

### 5.11.1 Runtime Create* exact API

必须冻结：

```text
CreatePublisher<T>
CreateSubscriber<T>
CreateClient<Req, Resp>
CreateServer<Req, Resp>
```

exact signature。

需要回答：

```text
resource name 使用 std::string_view 还是其他类型
内部何时复制 name
空名字是否非法
name normalization 是否存在
Options 参数传值还是 const&
创建 callback / handler 的方式
返回 Result<...> exact shape
```

例如可能是：

```cpp
template <typename T>
Result<Publisher<T>>
CreatePublisher(
    std::string_view topic,
    PublisherOptions options = {});
```

但在 final header 前不把示例当作已经冻结签名。

---

### 5.11.2 Subscriber callback registration

需要冻结：

```text
callback creation-time 传入
还是
CreateSubscriber 后 SetCallback
```

并明确：

```text
是否允许 callback replacement
Running 后能否替换
replacement 与正在执行 callback 的并发语义
未设置 callback 时是否允许接收
SetCallback 返回 Status 还是 void
```

推荐设计应避免不必要的：

```text
已开始 receive
但没有 callback
```

中间状态。

---

### 5.11.3 Server Handler registration

必须冻结：

```text
Handler
ResponseHandler
```

的安装方式。

需要回答：

```text
creation-time 还是 post-create
两者是否 mutually exclusive
是否允许重复设置
是否允许 replacement
Call 正在执行时 replacement 的 snapshot semantics
registration failure 返回什么
未安装 handler 时是否允许 Server Running
```

推荐：

> 成功创建 Server 时就已经具有明确且唯一的处理策略。

避免：

```text
Server 已可接收请求
但 Handler 尚未安装
```

---

### 5.11.4 Future / Promise / Timer exact API

必须冻结：

```text
Future copy/move semantics
Ready()
Wait()
WaitFor()
Get()
Then()
Cancel()

Get 是否 consume
多次 Get 是否允许

Then exact template signature
continuation return mapping
Future flatten
continuation cancellation
continuation Executor rejection

Promise completion API
Promise set-once
broken Promise semantics

Timer creation
Timer callback/Future representation
Timer cancellation race
Timer shutdown behavior
```

---

### 5.11.5 Middleware SPI exact headers

必须定稿：

```text
Middleware
MiddlewareInitContext

TopicInterface
ServiceInterface

PublisherHandle
SubscriberHandle
ClientHandle
ServerHandle

MessageFrame passing form

Service Request frame
Service Response completion

receive callback
completion callback

TypeSupport
Encoding
SerializedPayload
MiddlewareTypeBinding
```

当前冻结的是：

> **SPI semantic model**

不是：

> exact C++ signatures。

---

## 5.12 Implementation Phases

### Phase 0 — Architecture + Exact Headers

完成：

```text
Architecture semantic freeze
Public Header Finalization
Middleware SPI exact headers
compile-fail contracts
```

---

### Phase 1 — Core Runtime

只实现 Core：

```text
Runtime
RuntimeConfig
RuntimeState

Status
Diagnostic
Result

Publisher / Subscriber facade internals
Client / Server facade internals

EndpointState
EndpointRegistry

scope machinery
trait diagnostics

Options
Context bounded storage

Capability lookup
Info lookup

routing

Executor
Future / Promise
Timer
Cancellation

Logging

Type infrastructure
Type Registry
Binding cache

MessageFrame
DispatchEnvelope

Middleware Registry / factory infrastructure
```

Phase 1 不提前绑定：

```text
FastDDS
ROS2
Zenoh
gRPC
```

---

### Phase 2 — LocalMiddleware

作为：

```text
reference implementation
contract validation layer
```

验证：

```text
Topic
Service
Portable error
timeout
cancellation
ResponseSender
Executor rejection
Runtime Stop
Logging
type binding
```

---

### Phase 3 — Fast DDS

验证：

```text
DDS Topic
QoS
metadata
loan
native capability
native interoperability
```

---

### Phase 4 — Zenoh

验证：

```text
rich Context
View/Ref
query semantics
multi-reply rejection
native interoperability
```

---

### Phase 5 — gRPC

验证：

```text
unary RPC
deadline
metadata
Portable status
ResponseContext
cancellation
native interoperability
```

---

### Phase 6 — ROS2

验证：

```text
rcl/rmw
QoS
native Service schema
NativeCompatible mode
ROS2 semantics 不污染 Core
native interoperability
```

---

### Phase 7 — Cyclone DDS

用于检测：

```text
是否存在 Fast DDS-specific architecture assumption
```

---

# 6. 验证标准、术语表与冻结边界

## 6.1 Status / Result Contract Tests

必须覆盖：

```text
success Status:
  code == Ok
  diagnostic == nullptr
  no Diagnostic allocation

failure Status:
  optional Diagnostic

Status copy:
  shares Diagnostic backing

Result<T> success
Result<T> failure

Result<void> success/failure

Result(Status::Ok):
  contract violation / rejected construction path

Result::value():
  success
  failed fail-fast
```

---

## 6.2 Scope Compile-fail Tests

必须验证：

```text
object_scope_set<>
→ fail

duplicate object scope
→ fail

operation scope inside object_scope_set
→ fail

operation_scope_set<>
→ fail

duplicate operation scope
→ fail

object scope inside operation_scope_set
→ fail
```

---

## 6.3 Trait Diagnostic Tests

必须覆盖：

```text
option_extension_traits<T>::enabled == false
→ unregistered error

enabled == true but missing scope
→ controlled RoComm diagnostic

wrong Options scope
→ controlled diagnostic

wrong Context scope
→ controlled diagnostic

malformed capability_traits
→ controlled diagnostic

malformed info_traits
→ controlled diagnostic

malformed type_traits
→ controlled diagnostic
```

---

## 6.4 Options Tests

至少：

```text
default construction
→ zero allocation

Set<T>
same-type last-write-wins

wrong scope compile-fail

explicit Middleware config consumption

unconsumed config
→ UnsupportedConfiguration

target without explicit config
→ Middleware default allowed
```

---

## 6.5 Context Tests

必须覆盖：

```text
default construction
copy
reuse

zero heap
no RTTI

common setters

Set<T>

same-type replacement
including storage already full

sticky invalid

all setters no-op after invalid

first failure wins

Reset

Validate

ContextExtensionDescriptor identity

hash collision full-name check

View/Ref materialization

operation boundary automatic validation
```

---

## 6.6 Publisher / Subscriber / Client / Server Ownership Tests

必须覆盖：

```text
create
destroy

move construction
move assignment

moved-from bool == false

moved-from live-required operation
→ InvalidHandle

moved-from valid-scope As<T>()
→ nullptr

capability pointer before move
→ remains valid after move

Runtime wrapper destruction
→ remaining object safe to destroy
```

---

## 6.7 Capability Tests

必须覆盖：

```text
explicit opt-in

object_scope_set validation

canonical-name uniqueness

StableHash64 deterministic

hash collision protected by full name

As<T>() noexcept

As<T>() no allocation

As<T>() no RTTI

As<T>() no Middleware I/O

wrong scope
→ compile-fail

valid scope unsupported instance
→ nullptr

membership frozen after Create
membership unchanged after Runtime Stop

capability operation after Stop
→ InvalidState / RuntimeStopped as defined by capability
```

---

## 6.8 Info Tests

必须覆盖：

```text
InfoKind validation

MessageInfo receive_timestamp

MessageInfo callback lifetime

message payload callback lifetime

MessageInfo As<T>() lifetime

ClientCallInfo Complete

ClientCallInfo Partial

Partial As<T>() may return nullptr

ServerCallInfo handler lifetime

no generic deep-copy assumption
```

---

## 6.9 Topic Create Tests

必须覆盖：

```text
route resolution

explicit targets

default topic targets

zero targets
→ UnsupportedConfiguration

type binding validation

Options consumption

native resource create rollback

logical all-or-nothing
```

---

## 6.10 Publish Tests

必须覆盖：

```text
single target

fan-out all success
→ Ok

partial success
→ PartialFailure

all failure
→ AggregateFailure

all resolved targets attempted

no rollback

Diagnostic target order
→ resolved order

failure Diagnostic includes successful attempted targets

success path:
  no Diagnostic
  no mandatory per-target vector
```

---

## 6.11 Subscriber Dispatch Tests

```text
Executor accepted
→ callback runs

Executor rejected
→ callback does not run
→ MessageInfo not exposed
→ executor_rejected counter++
→ log according to policy
```

---

## 6.12 Service Client Tests

必须覆盖：

```text
AsyncCall

AsyncCallWithInfo

RequestContext timeout
priority
explicit target

route precedence:
RequestContext
ClientOptions
Service route
Runtime default

Complete ClientCallInfo

Partial ClientCallInfo

normal response

logical error

timeout

cancel

Runtime Stop

Executor-driven server failure
→ ResourceExhausted
```

---

## 6.13 ResponseSender 状态矩阵

Phase 1 前必须形成正式 matrix。

至少三个 sender state：

```text
Attached
Consumed
MovedFrom
```

CallState 至少：

```text
Pending
Completed
Cancelled
Timeout
RuntimeStopped
```

操作：

```text
Send
Fail
destructor
move constructor
move assignment
operator bool
IsCancelled
```

至少验证：

```text
Attached + Pending + successful Send
→ Consumed

Attached + Pending + successful Fail
→ Consumed

Attached + Pending + invalid Context
→ remains Attached

Attached + Cancelled + Send
→ Cancelled
→ remains Attached

Attached + Timeout + Send
→ Timeout
→ remains Attached

Attached + RuntimeStopped + Fail
→ RuntimeStopped
→ remains Attached

Attached + Pending + destructor
→ InternalError

Attached + Cancelled + destructor
→ no-op

Consumed + Send
→ InvalidState

MovedFrom + destructor
→ no-op
```

Portable abandonment：

```text
Client Result
→ InternalError

ClientCallInfo
→ Partial
```

NativeCompatible abandonment：

```text
Server local state
→ InternalError

remote native peer:
  may only observe timeout / transport failure
```

---

## 6.14 ResponseSender Move Assignment Tests

必须验证：

```text
destination Attached + Pending
source Attached
move assignment
```

采用：

```text
release-before-acquire
```

旧 destination Call：

```text
abandonment semantics
```

然后 destination 接管 source。

source：

```text
MovedFrom
```

---

## 6.15 ServiceErrorMode Tests

Portable：

```text
success response
portable error
abandonment InternalError
Executor rejection ResourceExhausted
```

都必须可被 RoComm peer terminalize。

如果 target 不支持：

```text
Create*
→ UnsupportedConfiguration
```

NativeCompatible：

```text
native response success

Fail() when no error channel
→ Unsupported
→ sender remains Attached

failed Simple Handler
→ local Unsupported
→ no fake success response
```

---

## 6.16 User Exception Boundary Tests

分别测试：

```text
Subscriber callback throws
Handler throws
ResponseHandler throws
Future continuation throws
```

必须：

```text
best-effort Critical log
std::terminate()
```

不得：

```text
转换成 Status
静默吞掉
继续执行
```

---

## 6.17 RuntimeConfig Validation Tests

至少：

```text
duplicate Middleware logical name

invalid default target

cross-domain MiddlewareId

stale MiddlewareId

invalid Topic route target

invalid Service route target

invalid LoggingConfig

invalid shutdown timeout
```

---

## 6.18 Runtime Create Tests

```text
successful Create
→ Running

Logging setup failure
→ rollback

Middleware Initialize failure
→ rollback

Middleware Start failure
→ rollback

route validation failure
→ rollback

default target validation failure
→ rollback

no half-created Runtime escapes
```

---

## 6.19 RuntimeState / EndpointState Ownership Tests

必须验证：

```text
Runtime wrapper strong owns RuntimeState

EndpointState strong keeps RuntimeState alive

Runtime destructor
→ RuntimeState logical Stopped

remaining Publisher/Subscriber/Client/Server
→ safe destruction

EndpointRegistry
→ no strong ownership cycle
```

---

## 6.20 Runtime Concurrency Tests

必须覆盖：

```text
CreatePublisher vs Stop
CreateSubscriber vs Stop
CreateClient vs Stop
CreateServer vs Stop

Endpoint destruction vs Stop
concurrent Stop callers
```

结果：

```text
Create wins
→ full registration
→ Stop observes it

Stop wins
→ Create fails RuntimeStopped

no orphan object
```

---

## 6.21 Runtime Stop / Quiescence Tests

必须区分：

```text
Stop == Ok
Stop == Timeout
```

测试：

```text
queued callback
running callback
blocked callback
blocked Handler
blocked continuation
blocked custom LogSink
Endpoint destruction during Stop
```

验证：

```text
Stop == Ok
→ quiescent

Stop == Timeout
→ logical Stopped
→ may still have in-flight user code
→ no unsafe thread termination
→ backing remains alive
```

---

## 6.22 Logging Tests

必须覆盖：

```text
default level

category hierarchy

longest-prefix override

Runtime-local isolation

ConsoleSink
FileSink
RotatingFileSink
NullSink
custom Sink

Synchronous mode

Asynchronous mode

bounded queue

DropNewest

DropOldest

Block

caller-side formatting

async worker only performs Sink I/O

disabled log path

Flush

Runtime Stop logging flush

Stop timeout with blocked Sink

Sink failure does not change communication Status

Critical log before fail-fast
```

---

## 6.23 Type System Compile-fail Tests

```text
CreatePublisher<UnregisteredType>
→ fail

CreateSubscriber<UnregisteredType>
→ fail

CreateClient<UnknownReq, Resp>
→ fail

CreateClient<Req, UnknownResp>
→ fail

malformed type_traits
→ controlled diagnostic
```

---

## 6.24 Type Binding Tests

必须覆盖：

```text
lazy binding resolution

binding cache

same type + same Middleware cache reuse

unsupported target binding
→ Unsupported

Topic multi-target binding validation

binding failure during logical Create
→ rollback
```

---

## 6.25 MessageFrame Tests

至少：

```text
caller message destroyed after Publish returns
→ async Middleware still safe

fan-out shares logical frame safely

logical payload immutable

serialization cache lazy creation

serialization cache concurrent access

serialized cache does not change logical payload
```

---

## 6.26 SPI Handle Tests

```text
default/null handle
→ InvalidHandle

stale generation
→ InvalidHandle

use-after-destroy detected
→ InvalidHandle

valid handle + native failure
→ MiddlewareError
```

---

## 6.27 Native Interoperability

必须真实验证：

```text
RoComm FastDDS ↔ native FastDDS

RoComm ROS2 ↔ native ROS2

RoComm Zenoh ↔ native Zenoh

RoComm gRPC ↔ native gRPC
```

后续：

```text
RoComm CycloneDDS ↔ native CycloneDDS
```

不能使用：

```text
RoComm ↔ RoComm
```

自测代替 native interoperability。

---

## 6.28 Architecture Pressure-test 与实现验收区分

文档中的：

```text
Architecture pressure-test: PASS
```

只表示：

> 当前抽象能够表达该 Middleware 已知关键语义。

不表示：

```text
代码已实现
代码已编译
性能已验证
native interoperability 已通过
```

Phase 3～6 仍需实际验收。

---

## 6.29 性能验证

### Context

benchmark：

```text
default construction
copy

common setter

Set<T>

same-type replacement

Validate

Publish submission

AsyncCall submission

ResponseSender::Send
```

要求：

```text
zero heap
no RTTI
bounded storage
bounded local work
```

---

### Options

默认：

```text
O(1)
zero heap
```

普通 `Set<T>()` 允许 allocation。

---

### `As<T>()`

要求：

```text
no allocation
no RTTI
no exception
no Middleware I/O
fixed-size key lookup
collision full-name verify
```

---

### Publish

benchmark：

```text
single-target
fan-out

MessageFrame sharing

serialization cache

failure-only diagnostic allocation
```

成功路径不得强制构造：

```text
per-target Diagnostic vector
```

---

### Logging

benchmark：

```text
disabled log call
Enabled()

caller-side formatting

sync Write

async enqueue

queue overflow
```

要求：

```text
disabled path 不构造 final message storage

async queue bounded

默认配置不对通信 hot path 产生无界阻塞
```

除非用户显式选择：

```text
LogOverflowPolicy::Block
```

---

## 6.30 CI 分层

| 测试层 | 策略 |
|---|---|
| Core unit tests | 每次提交 |
| LocalMiddleware contract | 每次提交 |
| compile-fail tests | 每次提交 |
| basic sanitizer | protected branch |
| TSAN concurrency | 定期 / protected branch |
| FastDDS/ROS2/Zenoh/gRPC native interop | integration CI |
| full sanitizer matrix | scheduled / release candidate |
| benchmark | scheduled / release |

Compile-fail 推荐：

```text
CMake try_compile
```

或：

```text
独立 negative translation units
```

主要覆盖：

```text
wrong scope
invalid scope_set
unregistered type
malformed traits
```

---

## 6.31 API / Architecture Review Checklist

任何新增 API、trait 或内部 abstraction 进入 Core 前必须回答：

1. 它属于 Runtime、Middleware、Publisher、Subscriber、Client、Server，还是某次具体 Operation？
2. 如果属于 Service，它描述的是完整 `Call`，还是 `Request` / `Response` operation？
3. 是否真的需要适用于多个 scope？
4. 如果是 Options extension，它是否只有一个明确 `object_scope`？
5. 如果是 Context extension，它是否只有一个明确 `operation_scope`？
6. 如果是 Capability，它在哪些 `object_scope` 下语义成立？
7. 同一 Capability 跨 scope 时 interface contract 是否真正一致？
8. 是否因为多个类型共享行为，就无必要地创造了 umbrella type？
9. 是否重新引入 `EndpointKind`、`is_communication_object` 等 membership abstraction？
10. Generic programming 是否围绕需要的 capability/expression？
11. 是否引入无领域语义的 `detail/internal/impl` namespace？
12. 内部类型是否可以归入明确领域？
13. namespace 是否表达领域而不是实现机制？
14. 是否因为共享实现无必要地创建 Base class？
15. 是否可以使用 composition 代替 inheritance？
16. 内部类型是否无必要泄漏 installed public headers？
17. Topic multi-target 与 Service single-target 是否被错误统一？
18. View/Ref 是否在 initiating API 返回前完成 materialization？
19. async backing storage 由谁拥有？
20. cancellation terminal race 是否完整定义？
21. ResponseSender / CallState terminal race 是否完整定义？
22. fan-out 是否明确 non-transactional？
23. Create 与 Runtime Stop 是否具有明确线性化语义？
24. Stop timeout 后哪些 in-flight work 仍然可能存在？
25. 是否错误假设 Runtime Stop 可以强杀用户线程？
26. MiddlewareId 是否验证 runtime/config domain 与 generation？
27. Logging 是否改变通信 Status？
28. Logging 是否可能在默认模式下无界阻塞 hot path？
29. Log formatting 是否遵守 caller-side formatting contract？
30. 新应用类型是否通过 `type_traits<T>` compile-time opt-in？
31. SPI Handle stale use 是否统一映射 `InvalidHandle`？
32. MessageFrame 是否已经拥有 caller-independent lifetime？
33. MessageFrame lazy cache 是否保持 logical immutability？
34. NativeCompatible Service 是否清楚暴露业务 error wire limitation？
35. 新设计是否引入职责不明确的 `Base` / `Impl` / `Manager` 等泛化名称？
36. 如果建立新的 `Registry`，它是否真正属于对应领域，而不是 mechanism-only namespace？
37. 如果新增 operation capability，是否与 object capability 类型域隔离？
38. 如果新增 borrowed Info，是否明确 lifetime 且避免假装可 universal deep-copy？

---

## 6.32 术语表

| 术语 | 定义 |
|---|---|
| `RoComm` | Robotics Communication Runtime |
| `Topic` | 异步 typed message communication primitive |
| `Service` | unary Request/Response communication primitive |
| `Publisher` | 发布 Topic 数据的 Runtime-created public type |
| `Subscriber` | 接收 Topic 数据的 Runtime-created public type |
| `Client` | 发起 Service Call 的 Runtime-created public type |
| `Server` | 处理 Service Call 的 Runtime-created public type |
| `Call` | 一次完整 Request → terminal Response/Error 生命周期 |
| `Request operation` | Client 发起请求的一次 operation |
| `Response operation` | Server 产生 terminal Response/Error 的 operation |
| `RequestContext` | Request operation 的 per-operation config |
| `ResponseContext` | Response operation 的 per-operation config |
| `PublishContext` | Publish operation 的 per-operation config |
| `ResponseSender` | Server explicit response control 的 move-only auxiliary object |
| `ResponseHandler` | Server-side explicit-response handler |
| `CallState` | 整个 Service Call 生命周期内部状态 |
| `CallCompletion` | Client-side terminal result + CallInfo |
| `ClientCallInfo` | Client 侧 Call metadata |
| `ServerCallInfo` | Server 侧 Call metadata |
| `object_scope` | 四类 Runtime-created public type 的 compile-time applicability marker |
| `operation_scope` | Publish / Request / Response operation marker |
| `object_scope_set` | Object Capability 合法 scope set |
| `operation_scope_set` | Future Operation Capability scope set |
| `Options` | 创建期 configuration |
| `Context` | per-operation configuration |
| `Capability` | 已创建 object 的高级 Runtime 行为接口 |
| `Info` | 实际通信结果 metadata |
| `CapabilityDescriptor` | Capability canonical identity |
| `ContextExtensionDescriptor` | Context extension canonical identity |
| `InfoDescriptor` | Info extension canonical identity |
| `TypeDescriptor` | RoComm application type identity |
| `StableHash64` | FNV-1a 64-bit in-process lookup key |
| `RuntimeConfig` | Runtime 创建期总配置 |
| `MiddlewareId` | 带 Runtime/config domain 语义的 opaque Middleware ID |
| `MiddlewareSpec` | Middleware instance 创建描述 |
| `MiddlewareInitContext` | Runtime 注入给 Middleware Initialize 的服务上下文 |
| `RuntimeState` | Runtime 内部共享生命周期状态 |
| `EndpointState` | 四类公共对象共享的 Runtime internal state |
| `EndpointRegistry` | Runtime 对 live EndpointState 的注册/观察结构 |
| `MessageFrame` | logically immutable、caller-independent owned message frame |
| `DispatchEnvelope` | Executor dispatch 所需 owned callback envelope |
| `ServiceErrorMode` | Portable / NativeCompatible Service error wire semantics |
| `LoggingContext` | Runtime-local logging state |
| `Logger` | Runtime-bound logging frontend |
| `LogSink` | 日志输出接口 |
| `quiescent` | 所有已开始的 user invocation 均已退出的状态 |

---

## 6.33 正式命名禁止规则

公共 API 不允许建立：

```cpp
class Endpoint;
class EndpointBase;
class Communicator;
class CommunicationObject;

enum class EndpointKind;

template <typename T>
struct is_endpoint;

template <typename T>
struct is_communication_object;
```

但字面词 `Endpoint` 不是绝对禁词。

允许：

```text
runtime::EndpointState
runtime::EndpointRegistry
grpc::EndpointAddress
NativeHandle
```

禁止的是：

> 把 Publisher / Subscriber / Client / Server 再次统一成新的公共 umbrella abstraction。

---

## 6.34 通用内部 namespace 禁止规则

RoComm 不使用：

```text
detail
internal
impl
```

作为通用内部 namespace。

也不通过：

```text
_private
_internal
_impl
```

简单换词规避。

内部实现必须按照真实领域职责组织。

---

## 6.35 冻结状态

当前正式状态：

```text
Architecture / conceptual API model:
    FROZEN

Project naming:
    FROZEN

Topic / Service model:
    FROZEN

No-public-Endpoint model:
    FROZEN

Call / Request / Response naming model:
    FROZEN

Options / Context / As<T>() / Info model:
    FROZEN

Object / Operation scope model:
    FROZEN

Status / Diagnostic / Result semantics:
    FROZEN

Topic routing / fan-out semantics:
    FROZEN

Service routing / error semantics:
    FROZEN

ResponseSender semantic model:
    FROZEN

Runtime lifecycle / Stop / quiescence semantics:
    FROZEN

RuntimeState / EndpointState / Registry ownership:
    FROZEN

Create / Stop linearization:
    FROZEN

Executor semantic model:
    FROZEN

User exception contract:
    FROZEN

Logging semantic model:
    FROZEN

Type semantic model:
    FROZEN

MessageFrame ownership model:
    FROZEN

Middleware SPI semantic model:
    FROZEN

Phase 0 exact public C++ headers:
    FINALIZATION REQUIRED

Middleware SPI exact C++ signatures:
    FINALIZATION REQUIRED

Middleware-specific implementation:
    OPEN

Performance tuning constants:
    OPEN
```

---

## 6.36 仍未冻结的 exact-header 项目

剩余工作明确限制在以下四组，不重新打开总体 Architecture：

```text
1. Future / Promise / Timer exact public API

2. Runtime CreatePublisher / CreateSubscriber /
   CreateClient / CreateServer exact signatures

3. Subscriber callback 与 Server Handler /
   ResponseHandler registration exact API

4. Middleware SPI / TypeSupport / MessageFrame
   exact C++ declarations
```

这些属于：

> **Phase 0 Public Header Finalization**

完成后才正式进入 Phase 1 Core Runtime Implementation。

---

## 6.37 最终架构图

```text
                         Application
                              │
                            RoComm
                              │
              ┌───────────────┴───────────────┐
              │                               │
             Topic                          Service
              │                               │
       ┌──────┴──────┐                 ┌──────┴──────┐
       ▼             ▼                 ▼             ▼
 Publisher<T>   Subscriber<T>    Client<Req,Rsp>  Server<Req,Rsp>
```

Service：

```text
Call
├── Request operation
│   └── RequestContext
│
└── Response operation
    ├── ResponseContext
    ├── automatic Handler result
    └── explicit ResponseHandler
            ↓
       ResponseSender
```

Call lifecycle：

```text
AsyncCall
   ↓
CallState
   ↓
terminal result
   ↓
CallCompletion<Response>
├── Result<Response>
└── ClientCallInfo
```

Runtime：

```text
RuntimeConfig
     ↓
Runtime::Create()
     ↓
RuntimeState
 ├── Middleware instances
 ├── EndpointRegistry
 ├── default Executor
 ├── route tables
 ├── Type binding cache
 └── LoggingContext
```

Ownership：

```text
Publisher / Subscriber / Client / Server
                  ↓ strong
             specific state
                  ↓ strong
             EndpointState
                  ↓ strong
              RuntimeState
```

Registry：

```text
RuntimeState
    ↓ owns
EndpointRegistry
    ↓ weak/reference
EndpointState
```

Topic receive：

```text
Middleware ingress
      ↓
MessageFrame / typed payload
      +
Info backing
      ↓
DispatchEnvelope
      ↓
Executor
      ↓
user callback
```

Logging：

```text
call site
   ↓
Enabled(level)
   ↓
caller-side formatting
   ↓
owned LogRecord
   ↓
sync Sink
or
bounded async queue
   ↓
logging worker
   ↓
Sink
```

---

## 6.38 最终冻结结论

RoComm V1 正式遵循以下原则：

> **RoComm 是 Robotics Communication Runtime；异构机器人通信是第一定位，Runtime、异步执行、类型系统与日志是支撑基础。**

> **`Publisher`、`Subscriber`、`Client` 和 `Server` 是平行的 Runtime-created public types，不存在用户可见的 Endpoint / Communicator / CommunicationObject umbrella。**

> **共享契约不升级为共享概念，共享实现不上浮为公共继承层次。**

> **Service 使用 Call / Request / Response 三层模型：Call 是完整生命周期，Request 和 Response 是两个 operation 方向。**

> **Options / Context / As<T>() / Info 分别承担创建期配置、operation 配置、高级运行时能力和通信结果 metadata。**

> **Options extension 只属于一个 object scope；Context extension 只属于一个 operation scope；Object Capability 可以显式支持多个 object scope。**

> **`object_scope_set` 与 `operation_scope_set` 在类型系统中严格隔离，必须非空、无重复且只能包含对应 scope 类型。**

> **Context 采用 bounded、zero-heap、no-RTTI 模型，并使用 StableHash64 + canonical name 的 ContextExtensionDescriptor 完成 slot identity 和 same-type replacement。**

> **Capability scope misuse 在编译期失败；`As<T>() == nullptr` 只表示“Capability 合法，但当前实例没有实现”。**

> **Topic 可以解析到多个 Middleware target；Service 每次 Call 只解析一个 target。**

> **Topic Create 是 logical all-or-nothing；Topic Publish 是 non-transactional best-effort fan-out。**

> **Publish 聚合失败 Diagnostic 按 resolved-target order 报告所有实际尝试 target，同时保持全成功路径零 Diagnostic allocation。**

> **ResponseSender 只在成功提交 terminal Response/Error 时消费；validation failure 或 external Call terminal state 不自动消费 sender。**

> **Portable Service 模式提供完整 RoComm error contract；NativeCompatible 模式优先 native schema interoperability，不能表达的业务 error 不得伪装成成功 Response。**

> **Runtime 通过 `Result<Runtime> Runtime::Create(RuntimeConfig)` 创建，不通过异常处理初始化失败。**

> **Runtime Stop 区分 logical Stopped 与 quiescence；RoComm 永远不通过 unsafe thread termination 强杀正在执行的用户代码。**

> **Runtime wrapper 与 live EndpointState 可以共同保持 RuntimeState backing；EndpointRegistry 不建立反向 strong ownership。**

> **Create* 与 Runtime::Stop() 必须线性化：成功 Create 的对象一定被 Stop 看见，Stop 先进入 Stopping 则新 Create 失败。**

> **MiddlewareId 是带 Runtime/config domain 与 generation 语义的 opaque identifier，跨 Runtime 使用必须被检测并返回 InvalidArgument。**

> **MessageFrame 是 logically immutable、caller-independent owned frame，可安全支持异步 Middleware 与 fan-out；lazy serialization cache 必须线程安全且不改变 logical message。**

> **用户 callback、Handler、ResponseHandler 和 Future continuation 不允许异常穿越 RoComm invocation boundary；异常会 best-effort Critical log 后 fail-fast。**

> **Logging 是 Runtime-local Core component；formatting 在调用线程完成，异步 worker 只处理 bounded queue 后的 Sink I/O。**

> **RoComm 不使用 `detail/internal/impl` 作为通用内部 namespace；内部实现必须归属于真实领域。**

> **Middleware Architecture Pressure-test PASS 只表示架构可表达，不代表实现完成或 native interoperability 已通过。**

> **统一的是通信语义和 Runtime 使用模型，而不是强迫 Fast DDS、ROS2、Zenoh、gRPC 具有相同能力。**

当前唯一尚未关闭的 Phase 0 工作是：

```text
Future / Promise / Timer exact API

Runtime Create* exact API

Subscriber callback / Server Handler registration API

Middleware SPI / TypeSupport / MessageFrame exact headers
```

这些完成后，Phase 0 才正式关闭，并进入：

```text
Phase 1 Core Runtime Implementation
        ↓
Phase 2 LocalMiddleware Contract Validation
        ↓
Phase 3–6 Native Middleware Integration
```