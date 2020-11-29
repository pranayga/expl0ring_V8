# Implementing String Taint tracking in Chromium - V8 (III)
> Documentation is a love letter that you write to your future self. - Damian Conway

Commit SHA: 600241ea64

## Chrome Diff breakdown
Let's start taking a look ny breaking down the changes in the Chrome source code into major chunks. While ther are a lot of gluecode in smaller files, we'll track our way from the bigger changes to the smaller ones.
Blink [basics](https://docs.google.com/document/d/1aitSOucL0VHZa9Z2vbRJSyAIsAz24kX8LFByQ5xQnUg/edit#heading=h.v5plba74lfde).

#### 1. Basic Setup
```
chrome/BUILD.gn                                    |   5 +
```
Linking information on the `capnp_lib` has been added in the build process. This is used for taint data commiunication between the V8 engine and blink's backend as far as I can currently understand. 

**TODO**: Probably a good idea to get @michael's view on it.

```
 base/debug/stack_trace_posix.cc                    |   1 +
 base/logging.cc                                    |   1 
```
Some minor changes related to logging and not letting the program crash on debug.

#### 2. Importing in the new V8_taint_class into blink
```
 .../static_v8_external_one_byte_string_resource.h  |   3 +-
 net/proxy_resolution/proxy_resolver_v8.cc          |   6 +-

```
This change seems to implement `multiple inheritence` on the `StaticV8ExternalOneByteStringResource` which was initially derived from the internal structure in the V8's `public v8::String::ExternalOneByteStringResource` and `public v8::String::TaintTrackingStringBufferImpl` was added to the mix.

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
Proxy Resolver seems to have very similar changes. Wherever we have a class being derived from `v8::String::ExternalOneByteStringResource`, we replace it with a multiple inheritence configuration. Probably to add our taint keeping structures.


**TODO**: Reorder this once you understand the significance and inner working of this class.

#### 3. V8 - Message event

```
 .../core/v8/custom/v8_message_event_custom.cc      |  32 +++
```
This significant change seem to add some modifications to the V8MessageEvent in order to continue the taint propogation. Some major blink control flow is documented in [basics of blink](https://docs.google.com/document/d/1aitSOucL0VHZa9Z2vbRJSyAIsAz24kX8LFByQ5xQnUg/edit#heading=h.v5plba74lfde).

**TODO**: However, I am still quite unsure on how exactly the message pasing the the V8 embedding work. I am going to contact Michael for some pointers and come back after looking into V8 some more.

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

#### 5 - ... Pending Stuff

```
 .../renderer/bindings/core/v8/local_window_proxy.h |   2 +
 .../renderer/bindings/core/v8/script_controller.cc |   4 +
 .../renderer/bindings/core/v8/script_controller.h  |   1 +
 .../renderer/bindings/core/v8/v8_code_cache.cc     |   1 +
 third_party/blink/renderer/core/dom/document.cc    |  32 ++-
 third_party/blink/renderer/core/dom/element.cc     |  14 ++
 .../blink/renderer/core/dom/events/event_target.cc |   6 +
 .../blink/renderer/core/dom/events/event_target.h  |   3 +
 .../renderer/core/dom/events/event_target.idl      |   2 +
 third_party/blink/renderer/core/dom/node.cc        |  15 ++
 third_party/blink/renderer/core/dom/node.h         |   2 +
 .../blink/renderer/core/events/message_event.cc    |  11 +
 .../blink/renderer/core/events/message_event.h     |   9 +
 .../blink/renderer/core/events/message_event.idl   |   2 +-
 .../blink/renderer/core/frame/local_frame.h        |   1 +
 third_party/blink/renderer/core/frame/location.cc  |  60 +++++-
 .../core/frame/window_or_worker_global_scope.cc    |  14 ++
 .../renderer/core/html/html_anchor_element.cc      |   4 +
 .../blink/renderer/core/html/html_embed_element.cc |   4 +
 .../renderer/core/html/html_iframe_element.cc      |   1 +
 .../blink/renderer/core/html/html_image_element.cc |   2 +
 .../renderer/core/html/html_script_element.cc      |   7 +
 .../renderer/core/html/parser/atomic_html_token.h  |   1 +
 .../core/html/parser/atomic_html_token_test.cc     |   8 +-
 .../core/html/parser/compact_html_token_test.cc    |   4 +-
 .../core/html/parser/html_entity_parser.cc         |  73 ++++---
 .../renderer/core/html/parser/html_entity_parser.h |  15 +-
 .../blink/renderer/core/html/parser/html_token.h   |  57 +++--
 .../renderer/core/html/parser/html_tokenizer.cc    | 229 ++++++++++++---------
 .../renderer/core/html/parser/html_tokenizer.h     |   6 +-
 .../core/html/parser/input_stream_preprocessor.h   |  11 +
 .../core/html/parser/markup_tokenizer_inlines.h    |   3 +
 .../blink/renderer/core/html/parser/xss_auditor.cc |   2 +-
 third_party/blink/renderer/core/page/frame_tree.cc |   2 +
 .../service_worker/thread_safe_script_container.cc |   4 +-
 .../blink/renderer/modules/storage/storage_area.cc |  11 +-
 .../renderer/platform/bindings/script_state.cc     |  43 ++++
 .../renderer/platform/bindings/script_state.h      |   5 +
 .../renderer/platform/bindings/string_resource.cc  |  33 ++-
 .../renderer/platform/bindings/string_resource.h   |  25 ++-
 .../platform/loader/fetch/cached_metadata.cc       |   2 +
 .../platform/loader/fetch/resource_loader.cc       |   1 +
 .../renderer/platform/text/segmented_string.cc     |   8 +-
 .../renderer/platform/text/segmented_string.h      |  20 +-
 third_party/blink/renderer/platform/wtf/BUILD.gn   |   2 +
 .../renderer/platform/wtf/text/string_impl.cc      |  13 +-
 .../blink/renderer/platform/wtf/text/string_impl.h |   6 +-
 .../renderer/platform/wtf/text/taint_tracking.cc   |  86 ++++++++
 .../renderer/platform/wtf/text/taint_tracking.h    | 100 +++++++++
 tools/v8_context_snapshot/BUILD.gn                 |   7 +
 url/gurl.cc                                        |  11 +
 url/url_canon_etc.cc                               |   1 +
 url/url_canon_internal.cc                          |   3 +
 66 files changed, 887 insertions(+), 190 deletions(-)
 create mode 100644 third_party/blink/renderer/platform/wtf/text/taint_tracking.cc
 create mode 100644 third_party/blink/renderer/platform/wtf/text/taint_tracking.h
```