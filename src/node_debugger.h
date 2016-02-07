#ifndef SRC_NODE_DEBUGGER_H_
#define SRC_NODE_DEBUGGER_H_

#include "node.h"
#include "uv.h"
#include "v8.h"
#include "v8-platform.h"

#include <string>

namespace blink {
class V8Inspector;
}

namespace node {

class NodeDebugger final {
 public:
  static void init(v8::Platform*, v8::Isolate*, v8::Local<v8::Context>);
  static NodeDebugger* instance();

  // Called on server thread.
  void Connect(Environment* server_env, v8::Local<v8::Object> connection);
  void Disconnect();
  void DispatchOnBackend(const std::string&);

  void PumpServerMessageLoop();

  // Called on the main (inspected) thread.
  void SendToFrontend(const std::string&);

 private:
  class DispatchOnInspectorBackendTask;
  class ConnectToInspectorBackendTask;
  class SendToFrontendTask;
  class ChannelImpl;
  NodeDebugger(v8::Platform*, v8::Isolate*, v8::Local<v8::Context>);
  ~NodeDebugger();

  static void Async(uv_async_t* async);

  v8::Platform* platform_;
  v8::Isolate* main_thread_isolate_;
  uv_async_t main_thread_async_;
  uv_async_t server_thread_async_;
  Environment* server_env_;
  v8::Global<v8::Object> connection_;
  // Abuse v8::Platform to post tasks to the server thread.
  v8::Platform* server_message_loop_;
  blink::V8Inspector* inspector_;
  ChannelImpl* channel_;

  static NodeDebugger* instance_;
};


}  // namespace node

#endif  // SRC_NODE_DEBUGGER_H_
