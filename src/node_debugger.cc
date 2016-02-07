#include "node_debugger.h"

#include "node.h"
#include "node_internals.h"
#include "base-object.h"
#include "base-object-inl.h"
#include "env.h"
#include "env-inl.h"
#include "libplatform/libplatform.h"
#include "v8-debug.h"
#include "v8-platform.h"

#include "platform/inspector_protocol/FrontendChannel.h"
#include "platform/v8_inspector/public/V8Inspector.h"

#include <string>

using namespace blink;

namespace node {

class NodeDebugger::ChannelImpl final : public protocol::FrontendChannel {
 public:
  ChannelImpl() {}
  virtual ~ChannelImpl() {}
 private:
  virtual void sendProtocolResponse(int sessionId, int callId, PassRefPtr<blink::JSONObject> message) override {
    sendMessageToFrontend(message);
  }
  virtual void sendProtocolNotification(PassRefPtr<blink::JSONObject> message) override {
    sendMessageToFrontend(message);
  }
  virtual void flush() override { }

  void sendMessageToFrontend(PassRefPtr<blink::JSONObject> message) {
    WTF::String message_string = message->toJSONString();
    std::string message_std(message_string.utf8().data(), message_string.length());
    node::NodeDebugger::instance()->SendToFrontend(message_std);
  }
};

class NodeDebugger::DispatchOnInspectorBackendTask : public v8::Task {
 public:
  explicit DispatchOnInspectorBackendTask(const std::string& message) : message_(message) {}
  void Run() override {
    String message = String::fromUTF8(message_.data(), message_.length());
    NodeDebugger::instance()->inspector_->dispatchMessageFromFrontend(message);
  }

 private:
  std::string message_;
};

class NodeDebugger::SendToFrontendTask : public v8::Task {
 public:
  explicit SendToFrontendTask(const std::string& message) : message_(message) {}
  void Run() override {
    v8::Isolate* isolate = NodeDebugger::instance()->server_env_->isolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Object> connection = v8::Local<v8::Object>::New(isolate, NodeDebugger::instance()->connection_);
    v8::Local<v8::Value> function_obj = connection->Get(context, v8::String::NewFromUtf8(isolate, "sendMessageToFrontend")).ToLocalChecked();
    ASSERT(function_obj->IsFunction());
    v8::Local<v8::Function> function = function_obj.As<v8::Function>();
    v8::Local<v8::Value> argv[] = { v8::String::NewFromUtf8(isolate, message_.data(), v8::String::kNormalString, message_.size()) };
    if (function->Call(context, connection, 1, argv).IsEmpty())
        fprintf(stderr, "SendToFrontendTask failed\n");
  }

 private:
  std::string message_;
};


NodeDebugger* NodeDebugger::instance_ = nullptr;

static double CurrentTime()
{
    return 0.0;
}

void NodeDebugger::init(v8::Platform* platform, v8::Isolate* isolate, v8::Local<v8::Context> context) {
  ASSERT(!instance_);
  WTF::Partitions::initialize(nullptr);
  WTF::initialize(CurrentTime, nullptr, nullptr);
  instance_ = new NodeDebugger(platform, isolate, context);
}

NodeDebugger* NodeDebugger::instance() {
  return instance_;
}

NodeDebugger::NodeDebugger(v8::Platform* platform, v8::Isolate* isolate, v8::Local<v8::Context> context)
    : platform_(platform)
    , main_thread_isolate_(isolate)
    , server_env_(nullptr)
    , server_message_loop_(v8::platform::CreateDefaultPlatform())
{
  Environment* env = Environment::GetCurrent(isolate->GetCurrentContext());
  uv_loop_t* loop = env->event_loop();
  int rc = uv_async_init(loop, &main_thread_async_, &NodeDebugger::Async);
  CHECK_EQ(0, rc);
  inspector_ = new blink::V8Inspector(isolate, context, platform);
  channel_ = new NodeDebugger::ChannelImpl();
  inspector_->connectFrontend(channel_);
}

NodeDebugger::~NodeDebugger() {
  delete channel_;
  delete inspector_;
  delete server_message_loop_;
}

void NodeDebugger::Connect(Environment* server_env, v8::Local<v8::Object> connection) {
  ASSERT(!server_env_);
  server_env_ = server_env;
  connection_.Reset(server_env->isolate(), connection);

  uv_loop_t* loop = server_env_->event_loop();
  int rc = uv_async_init(loop, &server_thread_async_, &NodeDebugger::Async);
  CHECK_EQ(0, rc);
}

void NodeDebugger::Disconnect() {
  fprintf(stderr, "NodeDebugger::Disconnect\n");
  server_env_ = nullptr;
  connection_.Reset();
}

void NodeDebugger::DispatchOnBackend(const std::string& message) {
  platform_->CallOnForegroundThread(main_thread_isolate_, new DispatchOnInspectorBackendTask(message));
  // Wake up main thread.
  uv_async_send(&main_thread_async_);
}

void NodeDebugger::PumpServerMessageLoop() {
  // Check if connected yet.
  if (!server_env_)
    return;
  v8::Isolate* isolate = server_env_->isolate();
  while (v8::platform::PumpMessageLoop(server_message_loop_, isolate)) {}
}

void NodeDebugger::SendToFrontend(const std::string& message) {
  server_message_loop_->CallOnForegroundThread(server_env_->isolate(), new SendToFrontendTask(message));
  // Wake up server thread.
  uv_async_send(&server_thread_async_);
}

void NodeDebugger::Async(uv_async_t* async)
{
}

static std::string toStdString(v8::Local<v8::String> value) {
  v8::String::Utf8Value utf8(value);
  return std::string(*utf8, utf8.length());
}

class NodeDebuggerContext {
 public:
  static void Init(Environment* env, v8::Local<v8::Object> exports) {
    env->SetMethod(exports, "connectToInspectorBackend", ConnectToInspectorBackend);
    env->SetMethod(exports, "dispatchOnInspectorBackend", DispatchOnInspectorBackend);
    env->SetMethod(exports, "disconnectFromInspectorBackend", DisconnectFromInspectorBackend);
  }

 private:
  static void ConnectToInspectorBackend(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    Environment* env = Environment::GetCurrent(context);

    v8::Local<v8::Object> connection = info[0].As<v8::Object>();
    NodeDebugger::instance()->Connect(env, connection);
  }

  static void DispatchOnInspectorBackend(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!info[0]->IsString()) {
      return;
    }
    v8::Local<v8::String> v8message = info[0].As<v8::String>();
    info.GetReturnValue().Set(v8message);
    std::string message = toStdString(v8message);
    NodeDebugger::instance()->DispatchOnBackend(message);
  }

  static void DisconnectFromInspectorBackend(const v8::FunctionCallbackInfo<v8::Value>& info) {
    NodeDebugger::instance()->Disconnect();
  }
};

void InitDebugger(v8::Local<v8::Object> exports,
                  v8::Local<v8::Value> unused,
                  v8::Local<v8::Context> context) {
  Environment* env = Environment::GetCurrent(context);
  NodeDebuggerContext::Init(env, exports);
}

}  // namespace node

NODE_MODULE_CONTEXT_AWARE_BUILTIN(node_debugger, node::InitDebugger);
