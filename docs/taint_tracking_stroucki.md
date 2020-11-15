# Implementing String Taint tracking in Chromium
> Documentation is a love letter that you write to your future self. - Damian Conway

Commit SHA: 600241ea64

## Introduction
This post discusses the internals the changes which were made into the V8 & Blink source code in order to implement taint-tracking for String class.

We shall first discuss an overall picture of what we want to be able to do and how it fits in to the major sub-components of V8 & blink. Next, we will bisect the changes in depth in order to understand them better.

## Overall Picture

**TODO**: Add the initial diagram and picture once you have the understanding of the major pieces and keep updating them as you move forward. 

## V8 Diff breakdown

Now let's first take a look at the changes which have been made in the V8 Javscript engine source code.

#### 1. Build

```
 BUILD.gn                                           |   66 +
 gni/v8.gni                                         |   11 +
```

The `BUILD.gn` while mainly have more includes in order to include the `capnp` Message passing code & `taint_tracking/` source to be included in the build proccess based on the build arguments which are set during `gn args` command. This includes:
```build
v8_capnp_include_dir = "/path-to/capnproto-install/include"
v8_capnp_lib_dir = "/path-to/capnproto-install/lib"
v8_capnp_bin_dir = "/path-to/capnproto-install/bin"

use_jumbo_build = true
enable_nacl=false

#is_component_build = true

# Set this to 0 to disable symbols
#symbol_level = 1
#enable_nacl = false
#remove_webcore_debug_symbols = false
is_debug = true

Include the capnp binaries in your path:
export PATH="$PATH:/path-to/capnproto-install/bin"
```

Which can be found in the `TAINT_TRACKING_README`. You might wanna learn how to effectiely use gn, [this](https://www.chromium.org/developers/gn-build-configuration) might help.

#### 2. Adding a new internal V8 class
```
 include/v8-internal.h                              |    4 +-
 include/v8.h                                       |  196 +-
```
These files make changes to the soul `objects/string.h` import into `v8.h` so that the exported `String` class can support taint data propogation.

This requires notable functions, classes and interfaces which appear throughout the codebase.
- Namespace: `tainttracking`
- dataType **TaintData**:
    - enum `TaintType`
        - This enum store the kind of Taint and the source of taint in a bit encoded form
    - enum `TaintSinkLabel`
        - The Label for the end distination for the tainted string data
- Functions (Most of them are defined in `src/api.cc`):
    - `WriteTaint()`
    - `GetTaintInfo()`
    - `LogIfBufferTainted()`
    - `LogIfTainted()`
    - `SetTaint()`
    - `SetTaintInfo()`
    - `NewUniqueId()`
- Class: 
    - `TaintTrackingBase`
        - Implements an virtual class which defines two purely virtual functions `GetTaintChars()` & `InitTaintChars()`. These are to be imeplemented by the derived class in order to keep track & update taint.
    - `TaintTrackingStringBufferImpl`
        - This class extends `TaintTrackingBase` and implements the Two purley virutal functions defined by `TaintTrackingBase`.
        - It makes use of a new private data member  `private: mutable std::unique_ptr<TaintData> taint_data_;`
    - Extending `V8_EXPORT String : public Name` with a TaintVisitor class which we will see is defined in `/src/taint_tracking/taint_tracking.cc` later.

```diff
diff --git a/include/v8.h b/include/v8.h
index dd81ef81e3..9a685e2ee6 100644
--- a/include/v8.h
+++ b/include/v8.h
@@ -29,6 +29,10 @@
 // We reserve the V8_* prefix for macros defined in V8 public API and
 // assume there are no name conflicts with the embedder's code.
 
+namespace tainttracking {
+class TaintVisitor;
+} // ns tainttracking
+
 /**
  * The v8 JavaScript engine.
  */
@@ -2513,9 +2517,17 @@ enum class NewStringType {
  */
 class V8_EXPORT String : public Name {
  public:
+  // from objects/string.h:
+  // Maximal string length.
+  // The max length is different on 32 and 64 bit platforms. Max length for a
+  // 32-bit platform is ~268.4M chars. On 64-bit platforms, max length is
+  // ~1.073B chars. The limit on 64-bit is so that SeqTwoByteString::kMaxSize
+  // can fit in a 32bit int: 2^31 - 1 is the max positive int, minus one bit as
+  // each char needs two bytes, subtract 24 bytes for the string header size.
+  // XXXstroucki billy had -18, original was -16
   static constexpr int kMaxLength = internal::kApiTaggedSize == 4
-                                        ? (1 << 28) - 16
-                                        : internal::kSmiMaxValue / 2 - 24;
+                                        ? (1 << 28) - 18
+                                        : internal::kSmiMaxValue / 4 - 24;
 
   enum Encoding {
     UNKNOWN_ENCODING = 0x1,
@@ -2609,7 +2621,170 @@ class V8_EXPORT String : public Name {
    */
   bool IsExternalOneByte() const;
 
-  class V8_EXPORT ExternalStringResourceBase {  // NOLINT
+  typedef uint8_t TaintData;
+
+
+  // A taint type stores a single byte of taint information about a single
+  // character of string data. The most significant three bits are used for the
+  // encoding and the last significant 5 bits are used for the taint type.
+  //
+  // 0 0 0      0 0 0 0 0
+  // \___/      \_______/
+  //   |            |
+  // encoding    taint type
+  //
+  // Must be kept in sync with
+  // ../../third_party/WebKit/Source/wtf/text/TaintTracking.h
+  enum TaintType {
+    UNTAINTED = 0,
+    TAINTED = 1,
+    COOKIE = 2,
+    MESSAGE = 3,
+    URL = 4,
+    URL_HASH = 5,
+    URL_PROTOCOL = 6,
+    URL_HOST = 7,
+    URL_HOSTNAME = 8,
+    URL_ORIGIN = 9,
+    URL_PORT = 10,
+    URL_PATHNAME = 11,
+    URL_SEARCH = 12,
+    DOM = 13,
+    REFERRER = 14,
+    WINDOWNAME = 15,
+    STORAGE = 16,
+    NETWORK = 17,
+    MULTIPLE_TAINTS = 18,       // Used when combining multiple bytes with
+                                // different taints.
+    MESSAGE_ORIGIN = 19,
+
+    // This must be less than the value of URL_ENCODED
+    MAX_TAINT_TYPE = 20,
+
+    // Encoding types
+    URL_ENCODED = 32,            // 1 << 5
+    URL_COMPONENT_ENCODED = 64,  // 2 << 5
+    ESCAPE_ENCODED = 96,         // 3 << 5
+    MULTIPLE_ENCODINGS = 128,    // 4 << 5
+    URL_DECODED = 160,           // 5 << 5
+    URL_COMPONENT_DECODED = 192, // 6 << 5
+    ESCAPE_DECODED = 224,        // 7 << 5
+
+    NO_ENCODING = 0,            // Must use the encoding mask to compare to no
+                                // encoding.
+
+    // Masks
+    TAINT_TYPE_MASK = 31,       // 1 << 5 - 1 (all ones in lower 5 bits)
+    ENCODING_TYPE_MASK = 224   // 7 << 5 (all ones in top 3 bits)
+  };
+
+  enum TaintSinkLabel {
+    URL_SINK,
+    EMBED_SRC_SINK,
+    IFRAME_SRC_SINK,
+    ANCHOR_SRC_SINK,
+    IMG_SRC_SINK,
+    SCRIPT_SRC_URL_SINK,
+
+    JAVASCRIPT,
+    JAVASCRIPT_EVENT_HANDLER_ATTRIBUTE,
+    JAVASCRIPT_SET_TIMEOUT,
+    JAVASCRIPT_SET_INTERVAL,
+
+    HTML,
+    MESSAGE_DATA,
+    COOKIE_SINK,
+    STORAGE_SINK,
+    ORIGIN,
+    DOM_URL,
+    JAVASCRIPT_URL,
+    ELEMENT,
+    CSS,
+    CSS_STYLE_ATTRIBUTE,
+    LOCATION_ASSIGNMENT
+  };
+
+  void WriteTaint(TaintData* buffer,
+                  int start = 0,
+                  int length = -1) const;
+
+  int64_t GetTaintInfo() const;
+
+  template <typename Char>
+  static int64_t LogIfBufferTainted(TaintData* buffer,
+                                    Char* stringdata,
+                                    size_t length,
+
+                                    // Should be retrieved from the function
+                                    // arguments.
+                                    int symbolic_data,
+                                    v8::Isolate* isolate,
+                                    TaintSinkLabel label);
+
+  // Returns -1 if not tainted. Otherwise returns the message ID of the logged
+  // message.
+  int64_t LogIfTainted(TaintSinkLabel label,
+
+                       // Should be the index of the function arguments
+                       int symbolic_data);
+  static void SetTaint(v8::Local<v8::Value> val,
+                       v8::Isolate* isolate,
+                       TaintType type);
+  static void SetTaintInfo(v8::Local<v8::Value> val,
+                           int64_t info);
+
+  static int64_t NewUniqueId(v8::Isolate* isolate);
+
+  class V8_EXPORT TaintTrackingBase {
+  public:
+
+    virtual ~TaintTrackingBase() {};
+
+    /**
+     * Return the taint information. May return NULL if untainted.
+     */
+    virtual TaintData* GetTaintChars() const = 0;
+
+    /**
+     * Return the taint information for writing. May not return NULL.
+     */
+    virtual TaintData* InitTaintChars(size_t) const = 0;
+  };
+
+  // Inherit from this class to get an implementation of the taint tracking
+  // interface that stores a malloc-ed pointer on Set.
+  class V8_EXPORT TaintTrackingStringBufferImpl :
+    public virtual TaintTrackingBase {
+  public:
+
+    TaintTrackingStringBufferImpl() : taint_data_(nullptr) {}
+
+    // XXXstroucki I hope the data stays live long enough...
+    virtual TaintData* GetTaintChars() const {
+      return taint_data_.get();
+    }
+
+    virtual TaintData* InitTaintChars(size_t length) const {
+      TaintData* answer = taint_data_.get();
+      if (!taint_data_) {
+        answer = new TaintData[length];
+        taint_data_.reset(answer);
+        return answer;
+      } else {
+        return answer;
+      }
+    }
+
+    virtual void SetTaintChars(TaintData* buffer) {
+      taint_data_.reset(buffer);
+    }
+
+  private:
+    mutable std::unique_ptr<TaintData> taint_data_;
+  };
+
+  class V8_EXPORT ExternalStringResourceBase
+    : public virtual TaintTrackingBase {  // NOLINT
    public:
     virtual ~ExternalStringResourceBase() = default;
 
@@ -2661,6 +2836,7 @@ class V8_EXPORT String : public Name {
     friend class internal::Heap;
     friend class v8::String;
     friend class internal::ScopedExternalStringLock;
+    friend class tainttracking::TaintVisitor;
   };
 
   /**
@@ -2901,6 +3077,14 @@ class V8_EXPORT String : public Name {
   static void CheckCast(v8::Value* obj);
 };
 
+inline String::TaintType operator|(const String::TaintType& a, const String::TaintType& b) {
+  return static_cast<String::TaintType>(static_cast<int>(a) | static_cast<int>(b));
+}
+
+class V8_EXPORT TaintTracking {
+  public:
+    static void LogInitializeNavigate(v8::Local<v8::String> url);
+};
 
 /**
  * A JavaScript symbol (ECMA-262 edition 6)
@@ -6343,7 +6527,8 @@ class V8_EXPORT AccessorSignature : public Data {
 // --- Extensions ---
 V8_DEPRECATED("Implementation detail", class)
 V8_EXPORT ExternalOneByteStringResourceImpl
-    : public String::ExternalOneByteStringResource {
+    : public String::ExternalOneByteStringResource,
+    public String::TaintTrackingStringBufferImpl {
  public:
   ExternalOneByteStringResourceImpl() : data_(nullptr), length_(0) {}
   ExternalOneByteStringResourceImpl(const char* data, size_t length)
@@ -9083,6 +9268,9 @@ class V8_EXPORT Context {
   /** Returns the security token of this context.*/
   Local<Value> GetSecurityToken();
 
+  /** Used by tainttracking logging to identify different execution contexts **/
+  void SetTaintTrackingContextId(Local<Value> token);
+
   /**
    * Enter this context.  After entering a context, all code compiled
    * and run is compiled and run in this context.  If another context
```

#### 3. Additing Embedding API for taint
```
 src/api.cc                                         |  120 +-
```
The `src/api.cc` defines functions which are normally called from the process which embeds the V8 Isolate. Normally, the Embedder only has access to `V8.h` and `api.cc` primitives.

Tweaks below allow the chromium backened to interface with the V8 backened in order to execute the javascript context. 

```diff
diff --git a/src/api.cc b/src/api.cc
index 634ad37115..27346704d6 100644
--- a/src/api.cc
+++ b/src/api.cc
@@ -915,7 +915,8 @@ void RegisteredExtension::UnregisterAll() {
 }
 
 namespace {
-class ExtensionResource : public String::ExternalOneByteStringResource {
+class ExtensionResource : public String::ExternalOneByteStringResource,
+  public String::TaintTrackingStringBufferImpl {
  public:
   ExtensionResource() : data_(nullptr), length_(0) {}
   ExtensionResource(const char* data, size_t length)
@@ -2352,6 +2353,7 @@ MaybeLocal<UnboundScript> ScriptCompiler::CompileUnboundInternal(
                      InternalEscapableScope);
 
   i::ScriptData* script_data = nullptr;
+// XXXstroucki can check for problematic data here
   if (options == kConsumeCodeCache) {
     DCHECK(source->cached_data);
     // ScriptData takes care of pointer-aligning the data.
@@ -5471,6 +5473,114 @@ int String::Write(Isolate* isolate, uint16_t* buffer, int start, int length,
 }
```

Here, WriteTaint is defined which allows the embedder to write new taint data to the V8 string object. Each cracter has it's only taint data.
This function digs out the handler to the string and then calls out taint-tracking logic in ` src/taint_tracking.h` to mark the taints appropriately.
```diff
+void String::WriteTaint(uint8_t* buffer,
+                        int start,
+                        int length) const {
+  i::Handle<i::String> thisstr = Utils::OpenHandle(this);
+  i::Isolate* isolate;
+  //isolate = i::Heap::FromWritableHeapObject(*thisstr)->isolate();
+  isolate = i::Isolate::Current();
+  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(isolate);
+  {
+    i::DisallowHeapAllocation no_gc;
+    if (length < 0) {
+      length = thisstr->length();
+    }
+    tainttracking::FlattenTaintData(*thisstr, buffer, start, length);
+  }
+}
```
Similarly, most of the following definitions wrap the call to the actual logic in the `src/taint_tracking.h` to update the taints appropriately. These are thus basically trampoline functions.
```diff
+void String::SetTaintInfo(v8::Local<v8::Value> val,
+                          int64_t info) {
+  i::Handle<i::Object> i_val = Utils::OpenHandle(*val);
+  if (i_val->IsHeapObject()) {
+    i::Handle<i::HeapObject> as_heap = i::Handle<i::HeapObject>::cast(i_val);
+    i::Isolate* i_isolate;
+    //i::Isolate::FromWritableHeapObject(*as_heap, &i_isolate);
+    i_isolate = i::Isolate::Current();
+    ENTER_V8_NO_SCRIPT_NO_EXCEPTION(i_isolate);
+    tainttracking::SetTaintInfo(as_heap, info);
+  }
+}
+
+int64_t String::GetTaintInfo() const {
+  i::Handle<i::String> thisstr = Utils::OpenHandle(this);
+  i::Isolate* isolate;
+  //i::Isolate::FromWritableHeapObject(*thisstr, &isolate);
+  isolate = i::Isolate::Current();
+  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(isolate);
+  return thisstr->taint_info();
+}
+
+
+// static
+int64_t String::NewUniqueId(v8::Isolate* isolate) {
+  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
+  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(i_isolate);
+  int64_t* item = tainttracking::TaintTracker::FromIsolate(i_isolate)->
+    symbolic_elem_counter();
+  int64_t ret = (*item = *item + 1);
+  DCHECK_NE(ret, 0);
+  return ret;
+}
+
+int64_t String::LogIfTainted(TaintSinkLabel label, int symbolic_data) {
+  i::Handle<i::String> thisstr = Utils::OpenHandle(this);
+  i::Isolate* isolate;
+  i::Isolate::FromWritableHeapObject(*thisstr, &isolate);
+  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(isolate);
+  return tainttracking::LogIfTainted(thisstr, label, symbolic_data);
+}
+
+// static
+void String::SetTaint(v8::Local<v8::Value> val,
+                      v8::Isolate* isolate,
+                      TaintType type) {
+  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(reinterpret_cast<i::Isolate*>(isolate));
+  i::Handle<i::Object> obj = Utils::OpenHandle(*val);
+  tainttracking::SetTaint(obj, type);
+}
+
+// static
+template <typename Char>
+int64_t String::LogIfBufferTainted(TaintData* buffer,
+                                   Char* stringdata,
+                                   size_t length,
+
+                                   int symbolic_data,
+                                   v8::Isolate* isolate,
+                                   TaintSinkLabel label) {
+  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
+  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(i_isolate);
+  return tainttracking::LogIfBufferTainted(
+      buffer, stringdata, length, symbolic_data, i_isolate, label);
+}
+
+#define DECLARE_LOG_IF_TAINTED(type)               \
+template int64_t String::LogIfBufferTainted<type>( \
+    TaintData* buffer,                             \
+    type* stringdata,                              \
+    size_t length,                                 \
+    int symbolic_data,                             \
+    v8::Isolate* isolate,                          \
+    TaintSinkLabel label);
+
+DECLARE_LOG_IF_TAINTED(uint8_t);
+DECLARE_LOG_IF_TAINTED(uint16_t);
+DECLARE_LOG_IF_TAINTED(const uint8_t);
+DECLARE_LOG_IF_TAINTED(const uint16_t);
+#undef DECLARE_LOG_IF_TAINTED
+
+// static
+void TaintTracking::LogInitializeNavigate(v8::Local<v8::String> url) {
+  i::Handle<i::String> urlstr = Utils::OpenHandle(*url);
+  i::Isolate* i_isolate;
+  i::Isolate::FromWritableHeapObject(*urlstr, &i_isolate);
+  ENTER_V8_NO_SCRIPT_NO_EXCEPTION(i_isolate);
+  return tainttracking::LogInitializeNavigate(Utils::OpenHandle(*url));
+}
+
+
 bool v8::String::IsExternal() const {
   i::Handle<i::String> str = Utils::OpenHandle(this);
   return i::StringShape(*str).IsExternalTwoByte();
@@ -6087,6 +6197,13 @@ void v8::Context::SetSecurityToken(Local<Value> token) {
 }
 
 
+void v8::Context::SetTaintTrackingContextId(Local<Value> token) {
+  i::Handle<i::Context> env = Utils::OpenHandle(this);
+  i::Handle<i::Object> token_handle = Utils::OpenHandle(*token);
+  env->set_taint_tracking_context_id(*token_handle);
+}
+
+
 void v8::Context::UseDefaultSecurityToken() {
   i::Handle<i::Context> env = Utils::OpenHandle(this);
   env->set_security_token(env->global_object());
@@ -10660,6 +10777,7 @@ void InvokeFunctionCallback(const v8::FunctionCallbackInfo<v8::Value>& info,
   Address callback_address = reinterpret_cast<Address>(callback);
   VMState<EXTERNAL> state(isolate);
   ExternalCallbackScope call_scope(isolate, callback_address);
+  DCHECK(tainttracking::SymbolicMatchesFunctionArgs(info));
   callback(info);
 }
 
diff --git a/src/assembler.cc b/src/assembler.cc
index 98182514aa..f820cf6a75 100644
```
#### 4. Mobifying the Internal assembler
```
 src/assembler.cc                                   |    5 +
 src/assembler.h                                    |   11 +-
```
The `/src/assembler.cc` file defines the aseemblers usually used by CSA for code generation.
```diff
diff --git a/src/assembler.cc b/src/assembler.cc
index 98182514aa..f820cf6a75 100644
--- a/src/assembler.cc
+++ b/src/assembler.cc
@@ -105,6 +105,11 @@ AssemblerBase::AssemblerBase(const AssemblerOptions& options, void* buffer,
   buffer_ = static_cast<byte*>(buffer);
   buffer_size_ = buffer_size;
   pc_ = buffer_;
+/*
+fprintf(stderr, "pc and buffer set to %p\n", pc_);
+static size_t counter=1234567890;strouckitest_=counter;counter++;
+USE(strouckitest_);
+*/
 }
 
 AssemblerBase::~AssemblerBase() {
```
Here it looks like a counter of somesort was added with an easy to identify initialized value.

**TODO:** Ask @stroucki about the significance
```diff
diff --git a/src/assembler.h b/src/assembler.h
index a870c0b638..0987c508f3 100644
--- a/src/assembler.h
+++ b/src/assembler.h
@@ -220,7 +220,12 @@ class V8_EXPORT_PRIVATE AssemblerBase : public Malloced {
   // cross-snapshotting.
   static void QuietNaN(HeapObject nan) {}
 
-  int pc_offset() const { return static_cast<int>(pc_ - buffer_); }
+// XXXstroucki negs?
+// XXXstroucki difference of longs to int?
+  int pc_offset() const { auto pc = pc_; auto buffer = buffer_;USE(pc);USE(buffer);
+//fprintf(stderr, "pc: %lu buffer: %lu\n", (long)pc, (long)buffer);
+if ((pc_ - buffer_) < 0) __builtin_trap();
+return static_cast<int>(pc_ - buffer_); }
 
   // This function is called when code generation is aborted, so that
   // the assembler could clean up internal data structures.
@@ -239,6 +244,7 @@ class V8_EXPORT_PRIVATE AssemblerBase : public Malloced {
   // Record an inline code comment that can be used by a disassembler.
   // Use --code-comments to enable.
   void RecordComment(const char* msg) {
+pc_offset();
     if (FLAG_code_comments) {
       code_comments_writer_.Add(pc_offset(), std::string(msg));
     }
@@ -263,6 +269,9 @@ class V8_EXPORT_PRIVATE AssemblerBase : public Malloced {
   // The program counter, which points into the buffer above and moves forward.
   // TODO(jkummerow): This should probably have type {Address}.
   byte* pc_;
+public:
+  //volatile size_t strouckitest_;
+protected:
 
   void set_constant_pool_available(bool available) {
     if (FLAG_enable_embedded_constant_pool) {
```

#### 5. Adding builtins

```
 src/bootstrapper.cc                                |   18 +
 src/builtins/builtins-api.cc                       |   28 +-
 src/builtins/builtins-constructor-gen.cc           |    6 +
 src/builtins/builtins-definitions.h                |   11 +-
 src/builtins/builtins-function.cc                  |   10 +
 src/builtins/builtins-global.cc                    |   36 +
```
The builtins are the heart and soul of the changes from a javascript file's prespective. There are two ways for taint to get generated/propogated:
- due to builtins getting called from the embedded V8 calls
    - Usually done for blink based javascript DOM calls
- due to plain javscript interactions
    - happens due to javscript calls inside V8

the builtins are triggered for all the javascript code compilation. The builtins define the operation on internal objects based on the ECMAScript specification.

```diff
diff --git a/src/bootstrapper.cc b/src/bootstrapper.cc
index e970f5d2f3..e30ecd5860 100644
--- a/src/bootstrapper.cc
+++ b/src/bootstrapper.cc
@@ -2101,6 +2101,17 @@ void Genesis::InitializeGlobal(Handle<JSGlobalObject> global_object,
     SimpleInstallFunction(isolate_, prototype, "valueOf",
                           Builtins::kStringPrototypeValueOf, 0, true);
 
+    SimpleInstallFunction(isolate_, prototype, "__taintCount__",
+      Builtins::kStringPrototypeTaintCount, 0, true);
+    SimpleInstallFunction(isolate_, prototype, "__getAddress__",
+      Builtins::kStringPrototypeGetAddress, 0, true);
+    SimpleInstallFunction(isolate_, prototype, "__setTaint__",
+                          Builtins::kStringPrototypeSetTaint, 1, true);
+    SimpleInstallFunction(isolate_, prototype, "__getTaint__",
+                          Builtins::kStringPrototypeGetTaint, 0, true);
+    SimpleInstallFunction(isolate_, prototype, "__checkTaint__",
+                          Builtins::kStringPrototypeCheckTaint, 1, true);
+
     InstallFunctionAtSymbol(isolate_, prototype, factory->iterator_symbol(),
                             "[Symbol.iterator]",
                             Builtins::kStringPrototypeIterator, 0, true,
@@ -4934,6 +4945,13 @@ bool Genesis::InstallNatives() {
                                Builtins::kGlobalIsNaN, 1, true,
                                BuiltinFunctionId::kGlobalIsNaN);
 
+  SimpleInstallFunction(isolate(), global_object, "__printToTaintLog__",
+                        Builtins::kGlobalPrintToTaintLog, 2, false);
+  SimpleInstallFunction(isolate(), global_object, "__taintConstants__",
+                        Builtins::kGlobalTaintConstants, 0, false);
+  SimpleInstallFunction(isolate(), global_object, "__setTaint__",
+                        Builtins::kGlobalSetTaint, 2, false);
+
   // Install Array builtin functions.
   {
     Handle<JSFunction> array_constructor(native_context()->array_function(),
diff --git a/src/builtins/builtins-api.cc b/src/builtins/builtins-api.cc
index e1c76c0fd9..1fc4663902 100644
```
The `bootstrapper.cc` is the file which at build time generates the interpreter code for all the builtins as described [here](https://v8.dev/blog/embedded-builtins). You may install a buitin as a function, getter, setter etc.

In the above snippet, we are installing multiple builtins which would allow us to get and set taint data. These probably will be defined in the `/builtins/builtins-string*` files which we will discuss about in `section 5(i)`.

Lastly, we said above that these are functions which we can call. It's fair to assume that not normal script would be calling it itself. Rather, these allow us to see the operations. Thus, we still have a missing link on how exactly does the taint get transferred during the interaction between the strings. You would agree that in order to do it, we would have to change primitve string operations like creation, append etc. These are looked at mainly in the `builtins-x64.cc` since these operation are implemented in platform dependent way, with the goal of making them snappy.

##### 5(i): String CSA
```
 src/builtins/builtins-string-gen.cc                |   33 +-
 src/builtins/builtins-string.cc                    |   64 +
 src/builtins/builtins.h                            |    3 +-
 src/builtins/x64/builtins-x64.cc                   |  227 +-
```

##### 5(ii): Making CSA play well
```
 src/code-stub-assembler.cc                         |  223 +-
 src/code-stub-assembler.h                          |   13 +-
 src/compiler.cc                                    |   12 +-
 src/compiler/backend/code-generator.cc             |    7 +-
 src/compiler/bytecode-graph-builder.cc             |    3 +-
 src/compiler/code-assembler.cc                     |    2 +-
 src/compiler/effect-control-linearizer.cc          |   15 +
 src/compiler/schedule.cc                           |    2 +-
 src/contexts.cc                                    |   15 +-
 src/contexts.h                                     |    5 +-
```
#### 6. Adjusting AST
```
 src/ast/ast-value-factory.cc                       |    2 +-
 src/ast/ast.cc                                     |    8 +
 src/ast/ast.h                                      |   14 +-
 src/base/logging.cc                                |    1 +

```
```
 src/d8.cc                                          |    3 +-
 src/debug/debug-coverage.cc                        |    4 +-
 src/debug/liveedit.cc                              |    3 +-
 s
 rc/deoptimizer.h                                  |    2 +-
 src/execution.cc                                   |   17 +-
 src/execution.h                                    |    3 +-
 src/extensions/externalize-string-extension.cc     |   24 +-
 src/external-reference-table.cc                    |    9 +
 src/external-reference-table.h                     |    5 +-
 src/external-reference.cc                          |    6 +
 src/external-reference.h                           |    2 +
 src/flag-definitions.h                             |   49 +
 src/frames.cc                                      |   41 +
 src/frames.h                                       |   19 +
 src/globals.h                                      |   56 +
 src/heap/factory.cc                                |  137 +-
 src/heap/factory.h                                 |    1 +
 src/heap/heap-inl.h                                |    1 +
 src/heap/heap.cc                                   |    2 +-
 src/interpreter/bytecode-array-builder.cc          |    6 +-
 src/interpreter/bytecode-array-builder.h           |   19 +-
 src/interpreter/bytecode-array-writer.cc           |    3 +-
 src/interpreter/bytecode-generator.cc              |  119 +-
 src/interpreter/bytecode-generator.h               |    5 +
 src/interpreter/bytecode-source-info.h             |   34 +-
 src/isolate.cc                                     |   35 +-
 src/isolate.h                                      |    7 +
 src/json-parser.cc                                 |   25 +-
 src/json-parser.h                                  |    1 +
 src/json-stringifier.cc                            |   51 +-
 src/objects-inl.h                                  |   15 +-
 src/objects.cc                                     |  131 +-
 src/objects.h                                      |    9 +-
 src/objects/code.h                                 |    1 +
 src/objects/free-space-inl.h                       |    5 +
 src/objects/js-objects.h                           |    4 +-
 src/objects/name.h                                 |    8 +-
 src/objects/scope-info.cc                          |   55 +-
 src/objects/scope-info.h                           |    6 +
 src/objects/shared-function-info-inl.h             |    1 +
 src/objects/shared-function-info.h                 |    3 +
 src/objects/string-inl.h                           |   35 +
 src/objects/string-table.h                         |    3 +-
 src/objects/string.h                               |   22 +-
 src/parsing/parse-info.cc                          |    3 +
 src/parsing/parse-info.h                           |   11 +
 src/parsing/parser.h                               |    3 +
 src/roots.h                                        |    1 +
 src/runtime/runtime-compiler.cc                    |    6 +
 src/runtime/runtime-function.cc                    |    2 +-
 src/runtime/runtime-internal.cc                    |  241 ++
 src/runtime/runtime-regexp.cc                      |   25 +
 src/runtime/runtime-scopes.cc                      |   56 +-
 src/runtime/runtime-strings.cc                     |   74 +-
 src/runtime/runtime.h                              |   25 +-
 src/snapshot/code-serializer.cc                    |   11 +-
 src/snapshot/deserializer.cc                       |    6 +
 src/snapshot/deserializer.h                        |    1 +
 src/snapshot/natives.h                             |    3 +-
 src/snapshot/serializer.cc                         |    9 +
 src/snapshot/snapshot-source-sink.cc               |    1 +
 src/snapshot/snapshot-source-sink.h                |   14 +-
 src/source-position-table.cc                       |   10 +-
 src/source-position-table.h                        |   21 +-
 src/string-builder-inl.h                           |   45 +-
 src/string-builder.cc                              |   32 +-
```

#### X. Taint Tracking Logic

```
 src/taint_tracking-inl.h                           |  288 ++
 src/taint_tracking.h                               |  489 +++
 src/taint_tracking/ast_serialization.cc            | 4191 ++++++++++++++++++++
 src/taint_tracking/ast_serialization.h             |  700 ++++
 src/taint_tracking/capnp-diff.patch                |   27 +
 src/taint_tracking/log_listener.h                  |   18 +
 src/taint_tracking/object_versioner.cc             |  509 +++
 src/taint_tracking/object_versioner.h              |  179 +
 src/taint_tracking/protos/ast.capnp                |  765 ++++
 src/taint_tracking/protos/logrecord.capnp          |  351 ++
 src/taint_tracking/symbolic_state.cc               |  859 ++++
 src/taint_tracking/symbolic_state.h                |  391 ++
 src/taint_tracking/taint_tracking.cc               | 3080 ++++++++++++++
 src/taint_tracking/third_party/picosha2.h          |  395 ++
 src/unoptimized-compilation-info.cc                |    1 +
 src/unoptimized-compilation-info.h                 |    5 +
 src/uri.cc                                         |  157 +-
 src/v8.cc                                          |    2 +
 src/wasm/baseline/liftoff-compiler.cc              |   10 +-
 test/cctest/BUILD.gn                               |    1 +
 test/cctest/cctest.h                               |    3 +-
 test/cctest/heap/test-external-string-tracker.cc   |    3 +-
 test/cctest/heap/test-heap.cc                      |    6 +-
 .../bytecode_expectations/ContextVariables.golden  |    6 +-
 .../bytecode_expectations/CreateArguments.golden   |    8 +-
 .../bytecode_expectations/Delete.golden            |    6 +-
 .../bytecode_expectations/ForAwaitOf.golden        |   13 +-
 .../interpreter/bytecode_expectations/ForIn.golden |   20 +-
 .../interpreter/bytecode_expectations/ForOf.golden |   11 +-
 .../GlobalCountOperators.golden                    |    3 +-
 .../bytecode_expectations/GlobalDelete.golden      |    3 +-
 .../IIFEWithOneshotOpt.golden                      |   83 +-
 .../IIFEWithoutOneshotOpt.golden                   |   38 +-
 .../bytecode_expectations/LoadGlobal.golden        |  386 +-
 .../bytecode_expectations/LookupSlotInEval.golden  |    3 +-
 .../bytecode_expectations/Modules.golden           |    5 +-
 .../bytecode_expectations/PropertyCall.golden      |  412 +-
 .../bytecode_expectations/PropertyLoads.golden     |  404 +-
 .../bytecode_expectations/PropertyStores.golden    |  782 ++--
 .../bytecode_expectations/StandardForLoop.golden   |    6 +-
 .../bytecode_expectations/StoreGlobal.golden       |  773 ++--
 test/cctest/interpreter/source-position-matcher.cc |    3 +-
 test/cctest/parsing/test-scanner-streams.cc        |    2 +
 test/cctest/test-api.cc                            |   86 +-
 test/cctest/test-heap-profiler.cc                  |    3 +-
 test/cctest/test-log.cc                            |    3 +-
 test/cctest/test-parsing.cc                        |    3 +-
 test/cctest/test-regexp.cc                         |    3 +-
 test/cctest/test-serialize.cc                      |   16 +-
 test/cctest/test-strings.cc                        |   18 +-
 test/cctest/test-taint-tracking.cc                 | 2324 +++++++++++
 test/cctest/test-types.cc                          |    2 +-
 150 files changed, 19365 insertions(+), 1249 deletions(-)
 create mode 100644 src/taint_tracking-inl.h
 create mode 100644 src/taint_tracking.h
 create mode 100644 src/taint_tracking/ast_serialization.cc
 create mode 100644 src/taint_tracking/ast_serialization.h
 create mode 100644 src/taint_tracking/capnp-diff.patch
 create mode 100644 src/taint_tracking/log_listener.h
 create mode 100644 src/taint_tracking/object_versioner.cc
 create mode 100644 src/taint_tracking/object_versioner.h
 create mode 100644 src/taint_tracking/protos/ast.capnp
 create mode 100644 src/taint_tracking/protos/logrecord.capnp
 create mode 100644 src/taint_tracking/symbolic_state.cc
 create mode 100644 src/taint_tracking/symbolic_state.h
 create mode 100644 src/taint_tracking/taint_tracking.cc
 create mode 100644 src/taint_tracking/third_party/picosha2.h
 create mode 100644 test/cctest/test-taint-tracking.cc
```

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


-------------------

Things to ask Michael:
- How does the IPC between the blink and V8 work
    - especially from the protobuf derivative that was added
- How does the controlflow work
    - from: the functions defined in the `api.cc`
    - to: functions defined in `src/taint_tracking/...`