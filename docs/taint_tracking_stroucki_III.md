# Implementing String Taint tracking in Chromium - V8 (III)
> Documentation is a love letter that you write to your future self. - Damian Conway

Commit SHA: 600241ea64

## Chrome Diff breakdown
Let's start taking a look by breaking down the changes in the Chrome source code into major chunks. While there is a lot of glue code in smaller files, we'll track our way from the bigger changes to the smaller ones.
Blink [basics](https://docs.google.com/document/d/1aitSOucL0VHZa9Z2vbRJSyAIsAz24kX8LFByQ5xQnUg/edit#heading=h.v5plba74lfde).

#### 1. Basic Setup
```
chrome/BUILD.gn                                    |   5 +
```
Linking information on the `capnp_lib` has been added in the build process. This is used for taint data communication between the V8 engine and blink's backend as far as I can currently understand. 

```
 base/debug/stack_trace_posix.cc                    |   1 +
 base/logging.cc                                    |   1 
```
Some minor changes related to logging and not letting the program crash on debugging.

#### 2. Importing in the new V8_taint_class into blink
```
 .../static_v8_external_one_byte_string_resource.h  |   3 +-
 net/proxy_resolution/proxy_resolver_v8.cc          |   6 +-

```
This change seems to implement `multiple inheritances` on the `StaticV8ExternalOneByteStringResource` which was initially derived from the internal structure in the V8's `public v8::String::ExternalOneByteStringResource` and `public v8::String::TaintTrackingStringBufferImpl` was added to the mix.

```diff
diff --git a/extensions/renderer/static_v8_external_one_byte_string_resource.h b/extensions/renderer/static_v8_external_one_byte_string_resource.h
index 3f569585a7b7..0b69ea4cb7fb 100644
--- a/extensions/renderer/static_v8_external_one_byte_string_resource.h
+++ b/extensions/renderer/static_v8_external_one_byte_string_resource.h
@@ -17,7 +17,8 @@ namespace extensions {
 // wraps a buffer. The buffer must outlive the v8 runtime instance this resource
 // is used in.
 class StaticV8ExternalOneByteStringResource
-    : public v8::String::ExternalOneByteStringResource {
+    : public v8::String::ExternalOneByteStringResource,
+     public v8::String::TaintTrackingStringBufferImpl {
  public:
   explicit StaticV8ExternalOneByteStringResource(
       const base::StringPiece& buffer);
--- a/net/proxy_resolution/proxy_resolver_v8.cc
+++ b/net/proxy_resolution/proxy_resolver_v8.cc
@@ -91,7 +91,8 @@ const char kPacUtilityResourceName[] = "proxy-pac-utility-script.js";
 // External string wrapper so V8 can access the UTF16 string wrapped by
 // PacFileData.
 class V8ExternalStringFromScriptData
-    : public v8::String::ExternalStringResource {
+    : public v8::String::ExternalStringResource,
+      public v8::String::TaintTrackingStringBufferImpl {
  public:
   explicit V8ExternalStringFromScriptData(
       const scoped_refptr<PacFileData>& script_data)
@@ -110,7 +111,8 @@ class V8ExternalStringFromScriptData
 
 // External string wrapper so V8 can access a string literal.
 class V8ExternalASCIILiteral
-    : public v8::String::ExternalOneByteStringResource {
+    : public v8::String::ExternalOneByteStringResource,
+      public v8::String::TaintTrackingStringBufferImpl {
  public:
   // |ascii| must be a NULL-terminated C string, and must remain valid
   // throughout this object's lifetime.
```
----
Proxy Resolver seems to have very similar changes. Wherever we have a class being derived from `v8::String::ExternalOneByteStringResource`, we replace it with a multiple inheritance configuration. Probably to add our taint keeping structures.

#### 3. V8 - Message event

```
 .../core/v8/custom/v8_message_event_custom.cc      |  32 +++
```
This significant change seems to add some modifications to the V8MessageEvent to continue the taint propagation. Some major blink control flow is documented in [basics of blink](https://docs.google.com/document/d/1aitSOucL0VHZa9Z2vbRJSyAIsAz24kX8LFByQ5xQnUg/edit#heading=h.v5plba74lfde).

This piece of code helps is explicitly propagate taint as required. In Blink, it's often called to make sure that the taint propagation takes place for objects started between the Blink V8 space.

**TODO**: However, I am still quite unsure of how exactly the message passing the V8 embedding work. I am going to contact Michael for some pointers and come back after looking into V8 some more. However, This is more of chromium and V8 cross-compatibility at this point, hence a little `out of scope`.

```diff
diff --git a/third_party/blink/renderer/bindings/core/v8/custom/v8_message_event_custom.cc b/third_party/blink/renderer/bindings/core/v8/custom/v8_message_event_custom.cc
index 864991f2a2f2..184a9fbb39cb 100644
--- a/third_party/blink/renderer/bindings/core/v8/custom/v8_message_event_custom.cc
+++ b/third_party/blink/renderer/bindings/core/v8/custom/v8_message_event_custom.cc
@@ -98,6 +98,38 @@ void V8MessageEvent::DataAttributeGetterCustom(
   // Store the result as a private value so this callback returns the cached
   // result in future invocations.
   private_cached_data.Set(info.Holder(), result);
+
+  v8::String::SetTaint(result, info.GetIsolate(), v8::String::MESSAGE);
+
+  int64_t taint_info = event->TaintTrackingInfo();
+  if (taint_info == -1) {
+    taint_info = v8::String::NewUniqueId(info.GetIsolate());
+    event->SetTaintTrackingInfo(taint_info);
+  }
+  v8::String::SetTaintInfo(result, taint_info);
+  V8SetReturnValue(info, result);
+}
+
+void V8MessageEvent::OriginAttributeGetterCustom(const v8::FunctionCallbackInfo<v8::Value>& info) {
+  MessageEvent* event = V8MessageEvent::ToImpl(info.Holder());
+  auto* isolate = info.GetIsolate();
+  v8::Local<v8::String> result = V8String(isolate, event->origin());
+
+  int64_t taint_info = event->TaintTrackingInfo();
+  if (taint_info == -1) {
+    taint_info = v8::String::NewUniqueId(isolate);
+    event->SetTaintTrackingInfo(taint_info);
+  }
+  DCHECK_NE(taint_info, 0);
+  DCHECK_NE(taint_info, -1);
+
+  if (result->GetTaintInfo() == taint_info) {
+    V8SetReturnValue(info, result);
+    return;
+  }
+
+  v8::String::SetTaint(result, isolate, v8::String::MESSAGE_ORIGIN);
+  v8::String::SetTaintInfo(result, taint_info);
   V8SetReturnValue(info, result);
 }
```

#### 4. Local Window Proxy
```
 .../bindings/core/v8/local_window_proxy.cc         |  22 ++
```
The `local window proxy` seems to be a way to link the window object for the rendered webpage with the V8 backend. This object would have members like URLs and the `GET` parameters which is what makes it interesting. 

In the code below, 
1. On each ContextCreate() we update the TrackingId so that we can update it in the V8's tracking system. This also enables us to track multiple taints for different windows running in different Isolates.
2. The definition of the `UpdateTaintTrackingContextId` here. In a nutshell, we get the frame and then use the V8's hook to update the information in the linked V8 Isolate's Context.

```diff
diff --git a/third_party/blink/renderer/bindings/core/v8/local_window_proxy.cc b/third_party/blink/renderer/bindings/core/v8/local_window_proxy.cc
index 9c3e848f01ae..0cc09f097ef7 100644
--- a/third_party/blink/renderer/bindings/core/v8/local_window_proxy.cc
+++ b/third_party/blink/renderer/bindings/core/v8/local_window_proxy.cc
@@ -44,6 +44,7 @@
 #include "third_party/blink/renderer/core/frame/local_dom_window.h"
 #include "third_party/blink/renderer/core/frame/local_frame.h"
 #include "third_party/blink/renderer/core/frame/local_frame_client.h"
+#include "third_party/blink/renderer/core/frame/location.h"
 #include "third_party/blink/renderer/core/html/document_name_collection.h"
 #include "third_party/blink/renderer/core/html/html_iframe_element.h"
 #include "third_party/blink/renderer/core/inspector/inspector_task_runner.h"
@@ -201,6 +202,12 @@ void LocalWindowProxy::Initialize() {
   if (World().IsMainWorld()) {
     GetFrame()->Loader().DispatchDidClearWindowObjectInMainWorld();
   }
+
+  UpdateTaintTrackingContextId();
+
+  // log this for the taint tracking instrumentation
+  const Frame* frame = GetFrame();
+  v8::TaintTracking::LogInitializeNavigate(V8String(GetIsolate(), frame->IsLocalFrame() && ToLocalFrame(frame)->GetDocument() ? ToLocalFrame(frame)->GetDocument()->baseURI() : KURL()));
 }
 
 void LocalWindowProxy::CreateContext() {
@@ -446,6 +453,7 @@ void LocalWindowProxy::UpdateDocument() {
   }
 
   UpdateDocumentInternal();
+  UpdateTaintTrackingContextId();
 }
 
 void LocalWindowProxy::UpdateDocumentInternal() {
@@ -567,6 +575,20 @@ void LocalWindowProxy::UpdateSecurityOrigin(const SecurityOrigin* origin) {
   SetSecurityToken(origin);
 }
 
+void LocalWindowProxy::UpdateTaintTrackingContextId() {
+  // update the taint tracking id for the window
+  v8::HandleScope scope(GetIsolate());
+  if (GetFrame() && script_state_) {
+    if (GetFrame()->IsLocalFrame()) {
+      script_state_->GetContext()->SetTaintTrackingContextId(
+        V8String(GetIsolate(), GetFrame()->DomWindow()->location()->toString()));
+    } else {
+      script_state_->GetContext()->SetTaintTrackingContextId(
+        V8String(GetIsolate(), "NON_LOCAL_FRAME"));
+    }
+  }
+}
+
 LocalWindowProxy::LocalWindowProxy(v8::Isolate* isolate,
                                    LocalFrame& frame,
                                    scoped_refptr<DOMWrapperWorld> world)
```

#### 5 An Over View Changes in Blink & Rendering family

Interfacing between the V8 engine and Chromium is no easy task. This is even bigger of a fact because there are a lot of key ideas to notice before such functionality can cohesively function.

While looking at all the blink related changes kind of go out of scope, here's a brief look at the major components which were changed and how those changes fit in:

- `third_party/blink/renderer/core/dom/*`: These changes mainly seem to add invocations to the taint-tracking code to propagate & Set taint data through these C++ functions which make use of the V8 String classes.
- `blink/renderer/core/HTML/parser/*`: Contains the actual HTML parser that the browser uses to generate the Tree structure from the provided HTML. A lot of changes here seem to augment existing helper functions with calls to taint tracking API + Create some storage space to store the taint data.
- `/renderer/platform/bindings/*`: This is a very interesting piece. If you take a look at the main header file:
```C++
// ScriptState is an abstraction class that holds all information about script
// execution (e.g., v8::Isolate, v8::Context, DOMWrapperWorld, ExecutionContext
// etc). If you need any info about the script execution, you're expected to
// pass around ScriptState in the code base. ScriptState is in a 1:1
// relationship with v8::Context.
```

This is where a lot of wrappers that blink uses are defined. These wrappers basically forward the incoming requests regarding taint tracking is to figure out how to handle them by looking at the V8 context state.
- `/renderer/platform/wtf/text/*`: The WTF framework provides a wrapper over the basic container classes to the blink and other Chromium components. These hooks mainly allow us to call our own initializer with any basic string initializers that are called throughout the codebase.
```
 third_party/blink/renderer/core/dom/document.cc    |  32 ++-
 third_party/blink/renderer/core/dom/element.cc     |  14 ++
 third_party/blink/renderer/core/dom/node.cc        |  15 ++
 third_party/blink/renderer/core/dom/node.h         |   2 +
 .../blink/renderer/core/events/message_event.cc    |  11 +
 third_party/blink/renderer/core/frame/location.cc  |  60 +++++-
 .../core/frame/window_or_worker_global_scope.cc    |  14 ++
 .../core/html/parser/html_entity_parser.cc         |  73 ++++---
 .../renderer/core/html/parser/html_entity_parser.h |  15 +-
 .../blink/renderer/core/html/parser/html_token.h   |  57 +++--
 .../renderer/core/html/parser/html_tokenizer.cc    | 229 ++++++++++++---------
 .../renderer/platform/bindings/script_state.cc     |  43 ++++
 .../renderer/platform/bindings/string_resource.cc  |  33 ++-
 .../renderer/platform/bindings/string_resource.h   |  25 ++-
 .../renderer/platform/text/segmented_string.h      |  20 +-
 .../renderer/platform/wtf/text/taint_tracking.cc   |  86 ++++++++
 .../renderer/platform/wtf/text/taint_tracking.h    | 100 +++++++++
 url/gurl.cc                                        |  11 +
 ........
 66 files changed, 887 insertions(+), 190 deletions(-)
 create mode 100644 third_party/blink/renderer/platform/wtf/text/taint_tracking.cc
 create mode 100644 third_party/blink/renderer/platform/wtf/text/taint_tracking.h
```

That's all we will we covering for the time being! Plan to update this post in the future with more in-depth details on the code once we understand them better!