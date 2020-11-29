# Implementing String Taint tracking in Chromium - V8 (I)
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
```
The builtins are the heart and soul of the changes from a javascript file's prespective. There are two ways for taint to get generated/propogated:
- due to builtins getting called from the embedded V8 calls
    - Usually done for blink based javascript DOM calls
- due to plain javscript interactions
    - happens due to javscript calls inside V8

the builtins are triggered for all the javascript code compilation. The builtins define the operation on internal objects based on the ECMAScript specification.

**Before you go any further**, the following subsections will make a whole lot of sense if you have played around with bultins before. Highly recommend follow thought this [guide](https://v8.dev/docs/csa-builtins) and getting a feel for builtins.

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

```diff
diff --git a/src/builtins/builtins-api.cc b/src/builtins/builtins-api.cc
index e1c76c0fd9..1fc4663902 100644
--- a/src/builtins/builtins-api.cc
+++ b/src/builtins/builtins-api.cc
@@ -166,7 +166,8 @@ MaybeHandle<Object> Builtins::InvokeApiFunction(Isolate* isolate,
                                                 Handle<HeapObject> function,
                                                 Handle<Object> receiver,
                                                 int argc, Handle<Object> args[],
-                                                Handle<HeapObject> new_target) {
+                                                Handle<HeapObject> new_target,
+                                                tainttracking::FrameType frametype) {
   RuntimeCallTimerScope timer(isolate,
                               RuntimeCallCounterId::kInvokeApiFunction);
   DCHECK(function->IsFunctionTemplateInfo() ||
@@ -202,6 +203,12 @@ MaybeHandle<Object> Builtins::InvokeApiFunction(Isolate* isolate,
     }
   }
 
+  tainttracking::RuntimePrepareSymbolicStackFrame(isolate, frametype);
+  for (int i = 0; i < argc; i++) {
+    tainttracking::RuntimeAddLiteralArgumentToStackFrame(isolate, args[i]);
+  }
+  tainttracking::RuntimeEnterSymbolicStackFrame(isolate);
+
   Handle<FunctionTemplateInfo> fun_data =
       function->IsFunctionTemplateInfo()
           ? Handle<FunctionTemplateInfo>::cast(function)
@@ -240,6 +247,9 @@ MaybeHandle<Object> Builtins::InvokeApiFunction(Isolate* isolate,
                                           fun_data, receiver, arguments);
     }
   }
+
+  tainttracking::RuntimeExitSymbolicStackFrame(isolate);
+
   if (argv != small_argv) delete[] argv;
   return result;
 }
@@ -266,6 +276,19 @@ V8_WARN_UNUSED_RESULT static Object HandleApiCallAsFunctionOrConstructor(
     new_target = ReadOnlyRoots(isolate).undefined_value();
   }
 
+// XXXstroucki this will fail CallAsFunction due to sealed HandleScope
+// also test-api/MicrotaskContextShouldBeNativeContext
+// also test-api/SetCallAsFunctionHandlerConstructor
+/*
+  tainttracking::RuntimePrepareSymbolicStackFrame(
+      isolate, tainttracking::FrameType::UNKNOWN_EXTERNAL);
+  for (int i = 0; i < args.length(); i++) {
+    tainttracking::RuntimeAddLiteralArgumentToStackFrame(
+        isolate, handle(args[i], isolate));
+  }
+  tainttracking::RuntimeEnterSymbolicStackFrame(isolate);
+*/
+
   // Get the invocation callback from the function descriptor that was
   // used to create the called object.
   DCHECK(obj->map()->is_callable());
@@ -291,6 +314,9 @@ V8_WARN_UNUSED_RESULT static Object HandleApiCallAsFunctionOrConstructor(
       result = *result_handle;
     }
   }
+
+  tainttracking::RuntimeExitSymbolicStackFrame(isolate);
+
   // Check for exceptions and return result.
   RETURN_FAILURE_IF_SCHEDULED_EXCEPTION(isolate);
   return result;
diff --git a/src/builtins/builtins-constructor-gen.cc b/src/builtins/builtins-constructor-gen.cc
index 08a03c4485..cc751a206f 100644
--- a/src/builtins/builtins-constructor-gen.cc
+++ b/src/builtins/builtins-constructor-gen.cc
@@ -249,6 +249,12 @@ Node* ConstructorBuiltinsAssembler::EmitFastNewFunctionContext(
   StoreMapNoWriteBarrier(function_context, context_type);
   TNode<IntPtrT> min_context_slots = IntPtrConstant(Context::MIN_CONTEXT_SLOTS);
   // TODO(ishell): for now, length also includes MIN_CONTEXT_SLOTS.
+
+  #ifdef V8_TAINT_TRACKING_INCLUDE_CONCOLIC
+  // Adding space for taint info
+  slots = assembler->Word32Shl(slots, assembler->Int32Constant(1));
+  #endif
+
   TNode<IntPtrT> length = IntPtrAdd(slots, min_context_slots);
   StoreObjectFieldNoWriteBarrier(function_context, Context::kLengthOffset,
                                  SmiTag(length));
diff --git a/src/builtins/builtins-definitions.h b/src/builtins/builtins-definitions.h
index 14a0d3ef11..b3bf0fc22d 100644
--- a/src/builtins/builtins-definitions.h
+++ b/src/builtins/builtins-definitions.h
@@ -639,7 +639,10 @@ namespace internal {
   TFJ(GlobalIsFinite, 1, kReceiver, kNumber)                                   \
   /* ES6 #sec-isnan-number */                                                  \
   TFJ(GlobalIsNaN, 1, kReceiver, kNumber)                                      \
-                                                                               \
+  /* TaintData */                                                            \
+  CPP(GlobalPrintToTaintLog)                                                 \
+  CPP(GlobalTaintConstants)                                                  \
+  CPP(GlobalSetTaint)                                                        \
   /* JSON */                                                                   \
   CPP(JsonParse)                                                               \
   CPP(JsonStringify)                                                           \
@@ -1133,6 +1136,12 @@ namespace internal {
   TFJ(StringPrototypeTrimEnd, SharedFunctionInfo::kDontAdaptArgumentsSentinel) \
   TFJ(StringPrototypeTrimStart,                                                \
       SharedFunctionInfo::kDontAdaptArgumentsSentinel)                         \
+  /* taint stuff */                                                          \
+  CPP(StringPrototypeTaintCount) \
+  CPP(StringPrototypeGetAddress) \
+  CPP(StringPrototypeSetTaint)                                               \
+  CPP(StringPrototypeGetTaint)                                               \
+  CPP(StringPrototypeCheckTaint)                                             \
   /* ES6 #sec-string.prototype.valueof */                                      \
   TFJ(StringPrototypeValueOf, 0, kReceiver)                                    \
   /* ES6 #sec-string.raw */                                                    \
diff --git a/src/builtins/builtins-function.cc b/src/builtins/builtins-function.cc
index cd68b261cc..6259bf8d8f 100644
--- a/src/builtins/builtins-function.cc
+++ b/src/builtins/builtins-function.cc
@@ -50,6 +50,11 @@ MaybeHandle<Object> CreateDynamicFunction(Isolate* isolate,
         ASSIGN_RETURN_ON_EXCEPTION(
             isolate, param, Object::ToString(isolate, args.at(i)), Object);
         param = String::Flatten(isolate, param);
+
+        tainttracking::LogIfTainted(Handle<String>::cast(param),
+                                    tainttracking::TaintSinkLabel::JAVASCRIPT,
+                                    i - 1);
+
         builder.AppendString(param);
       }
     }
@@ -60,6 +65,11 @@ MaybeHandle<Object> CreateDynamicFunction(Isolate* isolate,
       Handle<String> body;
       ASSIGN_RETURN_ON_EXCEPTION(
           isolate, body, Object::ToString(isolate, args.at(argc)), Object);
+
+      tainttracking::LogIfTainted(Handle<String>::cast(body),
+                                  tainttracking::TaintSinkLabel::JAVASCRIPT,
+                                  argc - 1);
+
       builder.AppendString(body);
     }
     builder.AppendCString("\n})");
```

##### 5(i): Global Builtins
```
 src/builtins/builtins-global.cc                    |   36 +
```
..........................
```diff
diff --git a/src/builtins/builtins-global.cc b/src/builtins/builtins-global.cc
index 83820de135..c6c9c35b54 100644
--- a/src/builtins/builtins-global.cc
+++ b/src/builtins/builtins-global.cc
@@ -8,6 +8,7 @@
 #include "src/compiler.h"
 #include "src/counters.h"
 #include "src/objects-inl.h"
+#include "src/taint_tracking.h"
 #include "src/uri.h"
 
 namespace v8 {
@@ -87,6 +88,11 @@ BUILTIN(GlobalEval) {
   Handle<JSFunction> target = args.target();
   Handle<JSObject> target_global_proxy(target->global_proxy(), isolate);
   if (!x->IsString()) return *x;
+
+  tainttracking::LogIfTainted(Handle<String>::cast(x),
+                              tainttracking::TaintSinkLabel::JAVASCRIPT,
+                              0);
+
   if (!Builtins::AllowDynamicFunction(isolate, target, target_global_proxy)) {
     isolate->CountUsage(v8::Isolate::kFunctionConstructorReturnedUndefined);
     return ReadOnlyRoots(isolate).undefined_value();
@@ -102,5 +108,35 @@ BUILTIN(GlobalEval) {
       Execution::Call(isolate, function, target_global_proxy, 0, nullptr));
 }
 
+BUILTIN(GlobalPrintToTaintLog) {
+  HandleScope scope(isolate);
+  Handle<String> string;
+  Handle<String> extra;
+  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
+      isolate, string,
+      Object::ToString(isolate, args.atOrUndefined(isolate, 1)));
+  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
+      isolate, extra,
+      Object::ToString(isolate, args.atOrUndefined(isolate, 2)));
+  tainttracking::JSTaintLog(string, extra);
+  return ReadOnlyRoots(isolate).undefined_value();
+}
+
+BUILTIN(GlobalTaintConstants) {
+  HandleScope scope(isolate);
+  return *tainttracking::JSTaintConstants(isolate);
+}
+
+BUILTIN(GlobalSetTaint) {
+  HandleScope scope(isolate);
+  uint32_t taint_value;
+  if (args.atOrUndefined(isolate, 2)->ToUint32(&taint_value)) {
+    tainttracking::SetTaint(
+        args.atOrUndefined(isolate, 1),
+        static_cast<tainttracking::TaintType>(taint_value));
+  }
+  return ReadOnlyRoots(isolate).undefined_value();
+}
+
 }  // namespace internal
 }  // namespace v8
```

##### 5(ii): String CSA
```
 src/builtins/builtins-string-gen.cc                |   33 +-
 src/builtins/builtins-string.cc                    |   64 +
 src/builtins/builtins.h                            |    3 +-
 src/builtins/x64/builtins-x64.cc                   |  227 +-
```

```diff
diff --git a/src/builtins/builtins-string-gen.cc b/src/builtins/builtins-string-gen.cc
index 085ffcfafa..25205049c5 100644
--- a/src/builtins/builtins-string-gen.cc
+++ b/src/builtins/builtins-string-gen.cc
@@ -623,10 +623,13 @@ TF_BUILTIN(StringFromCharCode, CodeStubAssembler) {
   }
 
   Node* code16 = nullptr;
+  TNode<IntPtrT> one_byte_taint_start;
   BIND(&if_notoneargument);
   {
     Label two_byte(this);
     // Assume that the resulting string contains only one-byte characters.
+    TNode<IntPtrT> taint_start = IntPtrAdd(ChangeInt32ToIntPtr(argc), ChangeInt32ToIntPtr(Int32Constant(SeqOneByteString::kHeaderSize - kHeapObjectTag)));
+    one_byte_taint_start = taint_start;
     Node* one_byte_result = AllocateSeqOneByteString(context, Unsigned(argc));
 
     TVARIABLE(IntPtrT, var_max_index);
@@ -637,7 +640,7 @@ TF_BUILTIN(StringFromCharCode, CodeStubAssembler) {
     // in 8 bits.
     CodeStubAssembler::VariableList vars({&var_max_index}, zone());
     arguments.ForEach(vars, [this, context, &two_byte, &var_max_index, &code16,
-                             one_byte_result](Node* arg) {
+                             one_byte_result, taint_start](Node* arg) {
       Node* code32 = TruncateTaggedToWord32(context, arg);
       code16 = Word32And(code32, Int32Constant(String::kMaxUtf16CodeUnit));
 
@@ -652,6 +655,11 @@ TF_BUILTIN(StringFromCharCode, CodeStubAssembler) {
           SeqOneByteString::kHeaderSize - kHeapObjectTag);
       StoreNoWriteBarrier(MachineRepresentation::kWord8, one_byte_result,
                           offset, code16);
+      // Init taint with 0's
+      StoreNoWriteBarrier(
+        MachineRepresentation::kWord8, one_byte_result,
+        IntPtrAdd(taint_start, offset),
+        Int32Constant(0));
       var_max_index = IntPtrAdd(var_max_index.value(), IntPtrConstant(1));
     });
     arguments.PopAndReturn(one_byte_result);
@@ -663,10 +671,13 @@ TF_BUILTIN(StringFromCharCode, CodeStubAssembler) {
     // string.
     Node* two_byte_result = AllocateSeqTwoByteString(context, Unsigned(argc));
 
+    Node* length_double = WordShl(ChangeUint32ToWord(Unsigned(argc)), 1);
+    Node* ctaint_start = IntPtrAdd(IntPtrConstant(SeqTwoByteString::kHeaderSize - kHeapObjectTag), length_double);
+
     // Copy the characters that have already been put in the 8-bit string into
     // their corresponding positions in the new 16-bit string.
     TNode<IntPtrT> zero = IntPtrConstant(0);
-    CopyStringCharacters(one_byte_result, two_byte_result, zero, zero,
+    CopyStringCharacters(one_byte_result, two_byte_result, one_byte_taint_start, zero, zero,
                          var_max_index.value(), String::ONE_BYTE_ENCODING,
                          String::TWO_BYTE_ENCODING);
 
@@ -677,6 +688,15 @@ TF_BUILTIN(StringFromCharCode, CodeStubAssembler) {
                                SeqTwoByteString::kHeaderSize - kHeapObjectTag);
     StoreNoWriteBarrier(MachineRepresentation::kWord16, two_byte_result,
                         max_index_offset, code16);
+
+     // Init taint with 0's TODO: does this work?
+    StoreNoWriteBarrier(
+      MachineRepresentation::kWord8, two_byte_result,
+      IntPtrAdd(
+        ctaint_start,
+        max_index_offset),
+      Int32Constant(0));
+
     var_max_index = IntPtrAdd(var_max_index.value(), IntPtrConstant(1));
 
     // Resume copying the passed-in arguments from the same place where the
@@ -684,7 +704,7 @@ TF_BUILTIN(StringFromCharCode, CodeStubAssembler) {
     // using a 16-bit representation.
     arguments.ForEach(
         vars,
-        [this, context, two_byte_result, &var_max_index](Node* arg) {
+        [this, context, two_byte_result, ctaint_start, max_index_offset, &var_max_index](Node* arg) {
           Node* code32 = TruncateTaggedToWord32(context, arg);
           Node* code16 =
               Word32And(code32, Int32Constant(String::kMaxUtf16CodeUnit));
@@ -695,6 +715,13 @@ TF_BUILTIN(StringFromCharCode, CodeStubAssembler) {
               SeqTwoByteString::kHeaderSize - kHeapObjectTag);
           StoreNoWriteBarrier(MachineRepresentation::kWord16, two_byte_result,
                               offset, code16);
+          // Init taint with 0's TODO: does this work?
+          StoreNoWriteBarrier(
+            MachineRepresentation::kWord8, two_byte_result,
+            IntPtrAdd(
+              ctaint_start,
+              max_index_offset),
+            Int32Constant(0));
           var_max_index = IntPtrAdd(var_max_index.value(), IntPtrConstant(1));
         },
         var_max_index.value());
diff --git a/src/builtins/builtins-string.cc b/src/builtins/builtins-string.cc
index d656c8769c..0b98a2ecb1 100644
--- a/src/builtins/builtins-string.cc
+++ b/src/builtins/builtins-string.cc
@@ -436,6 +436,7 @@ V8_WARN_UNUSED_RESULT static Object ConvertCaseHelper(
     current = next;
   }
   if (has_changed_character) {
+    tainttracking::OnConvertCase(string, result);
     return result;
   } else {
     // If we didn't actually change anything in doing the conversion
@@ -473,6 +474,8 @@ V8_WARN_UNUSED_RESULT static Object ConvertCase(
         reinterpret_cast<char*>(result->GetChars(no_gc)),
         reinterpret_cast<const char*>(flat_content.ToOneByteVector().start()),
         length, &has_changed_character);
+    // XXXstroucki not care about has_changed_character?
+    tainttracking::OnConvertCase(*s, *result);
     // If not ASCII, we discard the result and take the 2 byte path.
     if (index_to_first_unprocessed == length)
       return has_changed_character ? *result : *s;
@@ -592,5 +595,66 @@ BUILTIN(StringRaw) {
   RETURN_RESULT_OR_FAILURE(isolate, result_builder.Finish());
 }
 
+BUILTIN(StringPrototypeTaintCount) {
+  HandleScope scope(isolate);
+  TO_THIS_STRING(string, "String.prototype.__taintCount__");
+exit(0);
+  return *(isolate->factory()->undefined_value());
+}
+
+BUILTIN(StringPrototypeGetAddress) {
+  HandleScope scope(isolate);
+  TO_THIS_STRING(string, "String.prototype.__getAddress__");
+
+static volatile int foo = 0;while (foo){;};
+  char address[19];
+  snprintf(address, 19, "%p", string->GetAddress());
+  Handle<String> answer =
+      isolate->factory()->NewStringFromAsciiChecked(address);
+  return *answer;
+}
+
+BUILTIN(StringPrototypeSetTaint) {
+  HandleScope scope(isolate);
+  TO_THIS_STRING(string, "String.prototype.__setTaint__");
+  uint32_t taint_value;
+  Handle<Object> taint_arg = args.atOrUndefined(isolate, 1);
+  if (taint_arg->ToUint32(&taint_value)) {
+    tainttracking::SetTaintString(
+        string, static_cast<tainttracking::TaintType>(taint_value));
+    return *(isolate->factory()->undefined_value());
+  } else if (taint_arg->IsJSArrayBuffer()) {
+    JSArrayBuffer taint_data = JSArrayBuffer::cast(*taint_arg);
+    int len = (int)taint_data->byte_length(); // XXXstroucki dumb
+    if (!len) {
+      THROW_NEW_ERROR_RETURN_FAILURE(
+        isolate, NewTypeError(MessageTemplate::kInvalidArgument, taint_arg));
+    }
+    if (len != string->length()) {
+      THROW_NEW_ERROR_RETURN_FAILURE(
+        isolate, NewTypeError(MessageTemplate::kInvalidArgument, taint_arg));
+    }
+    tainttracking::JSSetTaintBuffer(string, handle(taint_data, isolate));
+    return *(isolate->factory()->undefined_value());
+  }
+  THROW_NEW_ERROR_RETURN_FAILURE(
+      isolate, NewTypeError(MessageTemplate::kInvalidArgument, taint_arg));
+}
+
+BUILTIN(StringPrototypeGetTaint) {
+  HandleScope scope(isolate);
+  TO_THIS_STRING(string, "String.prototype.__getTaint__");
+  return *tainttracking::JSGetTaintStatus(string, isolate);
+}
+
+BUILTIN(StringPrototypeCheckTaint) {
+  HandleScope scope(isolate);
+  TO_THIS_STRING(string, "String.prototype.__checkTaint__");
+  return *(tainttracking::JSCheckTaintMaybeLog(
+                   string,
+                   args.atOrUndefined(isolate, 1),
+                   0));
+}
+
 }  // namespace internal
 }  // namespace v8
```
##### 5(iii): assembly CSA builtins
```
 src/builtins/builtins.h                            |    3 +-
 src/builtins/x64/builtins-x64.cc                   |  227 +-
```
```diff
diff --git a/src/builtins/builtins.h b/src/builtins/builtins.h
index 7ea440e004..8fbdad3a27 100644
--- a/src/builtins/builtins.h
+++ b/src/builtins/builtins.h
@@ -154,7 +154,8 @@ class Builtins {
   V8_WARN_UNUSED_RESULT static MaybeHandle<Object> InvokeApiFunction(
       Isolate* isolate, bool is_construct, Handle<HeapObject> function,
       Handle<Object> receiver, int argc, Handle<Object> args[],
-      Handle<HeapObject> new_target);
+      Handle<HeapObject> new_target,
+      tainttracking::FrameType frametype = tainttracking::FrameType::UNKNOWN_EXTERNAL);
 
   enum ExitFrameType { EXIT, BUILTIN_EXIT };
 
diff --git a/src/builtins/x64/builtins-x64.cc b/src/builtins/x64/builtins-x64.cc
index 0f0ca80311..2e5ba21887 100644
--- a/src/builtins/x64/builtins-x64.cc
+++ b/src/builtins/x64/builtins-x64.cc
@@ -20,6 +20,7 @@
 #include "src/objects/js-generator.h"
 #include "src/objects/smi.h"
 #include "src/register-configuration.h"
+#include "src/taint_tracking.h"
 #include "src/wasm/wasm-linkage.h"
 #include "src/wasm/wasm-objects.h"
 
@@ -75,6 +76,205 @@ static void GenerateTailCallToReturnedCode(MacroAssembler* masm,
 
 namespace {
 
+inline void GenerateTaintTrackingPrepareApply(
+    MacroAssembler* masm, tainttracking::FrameType caller_frame_type) {
+  #ifdef V8_TAINT_TRACKING_INCLUDE_CONCOLIC
+
+  // ----------- S t a t e -------------
+  //  -- rax    : argumentsList
+  //  -- rdi    : target
+  //  -- rdx    : new.target (checked to be constructor or undefined)
+  //  -- rsp[0] : return address.
+  //  -- rsp[8] : thisArgument
+  // -----------------------------------
+
+
+  __ movp(rcx, Operand(rsp, kPointerSize));
+
+  // Store caller save registers
+  __ Push(rax);
+  __ Push(rdx);
+  __ Push(rdi);
+
+  {
+    FrameScope scope(masm, StackFrame::INTERNAL);
+
+    // Push arguments to runtime call
+    __ Push(rax); // argumentsList
+    __ Push(rdi); // target
+    __ Push(rdx); // new.target
+    __ Push(rcx); // thisArgument
+
+    __ Push(Smi::FromInt(static_cast<int>(caller_frame_type)));
+
+    __ CallRuntime(Runtime::kTaintTrackingPrepareApply, 5);
+  }
+
+  // Restore caller save registers
+  __ Pop(rdi);
+  __ Pop(rdx);
+  __ Pop(rax);
+
+  #endif
+}
+
+#ifdef V8_TAINT_TRACKING_INCLUDE_CONCOLIC
+inline void GeneratePushArgumentLoop(MacroAssembler* masm, Register scratch) {
+  // ----------- S t a t e -------------
+  //  -- rax                 : the number of arguments (not including the receiv
+er)
+  //
+  //  -- rsp[0]              : return address
+  //  -- rsp[8]              : last argument
+  //  -- ...
+  //  -- rsp[8 * argc]       : first argument
+  //  -- rsp[8 * (argc + 1)] : receiver
+  // -----------------------------------
+
+
+  // Loop through all the arguments starting from rsp[8 * argc] and going to
+  // rsp[8].
+  Label loop, done;
+  __ testp(rax, rax);
+  __ j(zero, &done, Label::kNear);
+  __ movp(scratch, rax);
+  __ bind(&loop);
+
+  // Push rsp[scratch * kPointerSize]
+  __ Push(Operand(r11, scratch, times_pointer_size, 0));
+  __ decp(scratch);
+  __ j(not_zero, &loop);              // While non-zero.
+  __ bind(&done);
+}
+#endif
+
+inline void GenerateTaintTrackingPrepareCall(
+    MacroAssembler* masm, tainttracking::FrameType caller_frame_type) {
+  #ifdef V8_TAINT_TRACKING_INCLUDE_CONCOLIC
+
+  // ----------- S t a t e -------------
+  //  -- rax                 : argc, the number of arguments (not including the
+receiver)
+  //  -- rdi                 : the target to call (can be any Object)
+  //
+  //  -- rsp[0]              : return address
+  //  -- rsp[8]              : last argument
+  //  -- ...
+  //  -- rsp[8 * argc]       : first argument
+  //  -- rsp[8 * (argc + 1)] : receiver
+  // -----------------------------------
+
+
+  {
+    __ movp(r11, rsp);
+    FrameScope scope(masm, StackFrame::INTERNAL);
+
+    // Store caller save registers
+    __ Push(rax);
+    __ Push(rdi);
+    __ Push(rdx);
+    __ Push(rbx);
+
+    // Push arguments to prepare call
+    __ Push(rdi);
+    __ Push(Smi::FromInt(static_cast<int>(caller_frame_type)));
+    __ Push(Operand(r11, rax, times_pointer_size, kPointerSize));
+    GeneratePushArgumentLoop(masm, rcx);
+
+    // Add to rax the number of additional arguments
+    __ addp(rax, Immediate(3));
+
+    __ CallRuntime(Runtime::kTaintTrackingPrepareCall);
+
+    // Restore caller save registers
+    __ Pop(rbx);
+    __ Pop(rdx);
+    __ Pop(rdi);
+    __ Pop(rax);
+  }
+
+  #endif
+}
+
+inline void GenerateTaintTrackingPrepareCallOrConstruct(MacroAssembler* masm) {
+  #ifdef V8_TAINT_TRACKING_INCLUDE_CONCOLIC
+
+  // ----------- S t a t e -------------
+  //  -- rax        : the number of arguments (not including the receiver)
+  //  -- rdi        : the target to call (can be any Object)
+  //  -- rdx        : new.target - If undefined, then prepare for call,
+  //                  otherwise for construct
+  //  -- rsp        : return address
+  //  -- rsp[8 * n] : arguments to call
+  // -----------------------------------
+
+
+  // Store caller save registers
+
+  {
+    __ movp(r11, rsp);
+    FrameScope scope(masm, StackFrame::INTERNAL);
+
+    __ Push(rax);
+    __ Push(rdx);
+    __ Push(rdi);
+
+
+    // Push arguments to prepare call
+    __ Push(rdi);                 // the target to call
+    __ Push(rdx);                 // if undefined, then prepare for call
+                                  // otherwise for a construct call
+
+    // Push arguments from stack frame
+    GeneratePushArgumentLoop(masm, rcx);
+
+    // Add 2 to rax to reflect that the runtime call has 2 more arguments than
+    // the expected number because of the pushed rdi and rdx value
+    __ addp(rax, Immediate(2));
+
+    // Make the call
+    __ CallRuntime(Runtime::kTaintTrackingPrepareCallOrConstruct);
+
+
+    // Restore caller save registers in opposite order
+    __ Pop(rdi);
+    __ Pop(rdx);
+    __ Pop(rax);
+  }
+
+  #endif
+}
+
+inline void GenerateTaintTrackingSetReceiver(MacroAssembler* masm) {
+  #ifdef V8_TAINT_TRACKING_INCLUDE_CONCOLIC
+
+  // State:
+  //
+  // rbx -- new receiver
+
+  FrameScope scope (masm, StackFrame::INTERNAL);
+
+  // Save caller registers
+  __ Push(rax);
+  __ Push(rdx);
+  __ Push(rdi);
+  __ Push(rbx);
+
+  // Push rbx and make the call
+  __ Push(rbx);
+  __ CallRuntime(Runtime::kTaintTrackingAddLiteralReceiver);
+
+  // Restore caller registers
+  __ Pop(rbx);
+  __ Pop(rdi);
+  __ Pop(rdx);
+  __ Pop(rax);
+
+  #endif
+}
+
+
+
 void Generate_JSBuiltinsConstructStubHelper(MacroAssembler* masm) {
   // ----------- S t a t e -------------
   //  -- rax: number of arguments
@@ -95,6 +295,8 @@ void Generate_JSBuiltinsConstructStubHelper(MacroAssembler* masm) {
     // The receiver for the builtin/api call.
     __ PushRoot(RootIndex::kTheHoleValue);
 
+    GenerateTaintTrackingSetReceiver(masm);
+
     // Set up pointer to last argument.
     __ leap(rbx, Operand(rbp, StandardFrameConstants::kCallerSPOffset));
 
@@ -650,6 +852,13 @@ static void Generate_JSEntryTrampolineHelper(MacroAssembler* masm,
     __ cmpp(rcx, rax);
     __ j(not_equal, &loop, Label::kNear);
 
+    if (is_construct) {
+      // TODO: prepare for construct
+    } else {
+      // GenerateTaintTrackingPrepareCall(
+      //     masm, tainttracking::FrameType::BUILTIN_JS_TRAMPOLINE);
+    }
+
     // Invoke the builtin code.
     Handle<Code> builtin = is_construct
                                ? BUILTIN_CODE(masm->isolate(), Construct)
@@ -1666,8 +1875,10 @@ void Builtins::Generate_FunctionPrototypeApply(MacroAssembler* masm) {
 
   // 3. Tail call with no arguments if argArray is null or undefined.
   Label no_arguments;
-  __ JumpIfRoot(rbx, RootIndex::kNullValue, &no_arguments, Label::kNear);
-  __ JumpIfRoot(rbx, RootIndex::kUndefinedValue, &no_arguments, Label::kNear);
+  // billy: Removing the kNear tag to these two function calls because the
+  // GenerateTaintTrackingPrepareApply hook causes the tag to be not near
+  __ JumpIfRoot(rbx, RootIndex::kNullValue, &no_arguments);
+  __ JumpIfRoot(rbx, RootIndex::kUndefinedValue, &no_arguments);
 
   // 4a. Apply the receiver to the given argArray.
   __ Jump(BUILTIN_CODE(masm->isolate(), CallWithArrayLike),
@@ -1679,6 +1890,7 @@ void Builtins::Generate_FunctionPrototypeApply(MacroAssembler* masm) {
   __ bind(&no_arguments);
   {
     __ Set(rax, 0);
+    GenerateTaintTrackingPrepareApply(masm, tainttracking::FrameType::BUILTIN_FUNCTION_PROTOTYPE_APPLY);
     __ Jump(masm->isolate()->builtins()->Call(), RelocInfo::CODE_TARGET);
   }
 }
@@ -1729,6 +1941,9 @@ void Builtins::Generate_FunctionPrototypeCall(MacroAssembler* masm) {
     __ decp(rax);  // One fewer argument (first argument is new receiver).
   }
 
+  GenerateTaintTrackingPrepareCall(
+      masm, tainttracking::FrameType::BUILTIN_FUNCTION_PROTOTYPE_CALL);
+
   // 4. Call the callable.
   // Since we did not create a frame for Function.prototype.call() yet,
   // we use a normal Call builtin here.
@@ -1780,6 +1995,9 @@ void Builtins::Generate_ReflectApply(MacroAssembler* masm) {
   // since that's the first thing the Call/CallWithArrayLike builtins
   // will do.
 
+  GenerateTaintTrackingPrepareApply(
+      masm, tainttracking::FrameType::BUILTIN_REFLECT_APPLY);
+
   // 3. Apply the target to the given argumentsList.
   __ Jump(BUILTIN_CODE(masm->isolate(), CallWithArrayLike),
           RelocInfo::CODE_TARGET);
@@ -1837,6 +2055,9 @@ void Builtins::Generate_ReflectConstruct(MacroAssembler* masm) {
   // since that's the second thing the Construct/ConstructWithArrayLike
   // builtins will do.
 
+  GenerateTaintTrackingPrepareApply(
+      masm, tainttracking::FrameType::BUILTIN_REFLECT_CONSTRUCT);
+
   // 4. Construct the target with the given new.target and argumentsList.
   __ Jump(BUILTIN_CODE(masm->isolate(), ConstructWithArrayLike),
           RelocInfo::CODE_TARGET);
@@ -2077,6 +2298,8 @@ void Builtins::Generate_CallOrConstructVarargs(MacroAssembler* masm,
     __ addq(rax, r9);
   }
 
+  GenerateTaintTrackingPrepareCallOrConstruct(masm);
+
   // Tail-call to the actual Call or Construct builtin.
   __ Jump(code, RelocInfo::CODE_TARGET);
```
##### 5(iii): Making CSA play well
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

```diff
diff --git a/src/code-stub-assembler.cc b/src/code-stub-assembler.cc
index 8411a7c5da..4980eda073 100644
--- a/src/code-stub-assembler.cc
+++ b/src/code-stub-assembler.cc
@@ -3098,6 +3098,23 @@ TNode<UintPtrT> CodeStubAssembler::LoadBigIntDigit(TNode<BigInt> bigint,
       MachineType::UintPtr()));
 }
 
+void CodeStubAssembler::IncrementAndStoreTaintInstanceCounter(Node* result) {
+  // tainttracking::InstanceCounter* counter =
+  //   tainttracking::TaintTracker::FromIsolate(isolate())->
+  //   symbolic_elem_counter();
+  // Node* address = ExternalConstant(ExternalReference(counter));
+  // Node* value = Load(MachineType::Int64(), address);
+  StoreObjectFieldNoWriteBarrier(result, Name::kTaintInfoOffset,
+                                 Int64Constant(Name::DEFAULT_TAINT_INFO),
+                                 MachineRepresentation::kWord64);
+  // if (Is64()) {
+  //   value = IntPtrAdd(value, Int64Constant(1));
+  // } else {
+  //   DCHECK(false);
+  // }
+  // StoreNoWriteBarrier(MachineRepresentation::kWord64, address, value);
+}
+
 TNode<String> CodeStubAssembler::AllocateSeqOneByteString(
     uint32_t length, AllocationFlags flags) {
   Comment("AllocateSeqOneByteString");
@@ -3105,6 +3122,9 @@ TNode<String> CodeStubAssembler::AllocateSeqOneByteString(
     return CAST(LoadRoot(RootIndex::kempty_string));
   }
   Node* result = Allocate(SeqOneByteString::SizeFor(length), flags);
+  Node* taint_start = IntPtrAdd(
+      IntPtrConstant(SeqOneByteString::kHeaderSize - kHeapObjectTag),
+      IntPtrConstant(length));
   DCHECK(RootsTable::IsImmortalImmovable(RootIndex::kOneByteStringMap));
   StoreMapNoWriteBarrier(result, RootIndex::kOneByteStringMap);
   StoreObjectFieldNoWriteBarrier(result, SeqOneByteString::kLengthOffset,
@@ -3113,6 +3133,16 @@ TNode<String> CodeStubAssembler::AllocateSeqOneByteString(
   StoreObjectFieldNoWriteBarrier(result, SeqOneByteString::kHashFieldOffset,
                                  Int32Constant(String::kEmptyHashField),
                                  MachineRepresentation::kWord32);
+
+  IncrementAndStoreTaintInstanceCounter(result);
+  for (int i = 0; i < (int)length; i++) {
+    StoreNoWriteBarrier(
+        MachineRepresentation::kWord8,
+        result,
+        IntPtrAdd(taint_start, IntPtrConstant(i)),
+        Int32Constant(0));
+  }
+
   return CAST(result);
 }
 
@@ -3127,6 +3157,9 @@ TNode<String> CodeStubAssembler::AllocateSeqOneByteString(
   Comment("AllocateSeqOneByteString");
   CSA_SLOW_ASSERT(this, IsZeroOrContext(context));
   VARIABLE(var_result, MachineRepresentation::kTagged);
+  VARIABLE(var_offset, MachineType::PointerRepresentation());
+  Label loop(this, &var_offset), done_loop(this);
+  var_offset.Bind(IntPtrConstant(0));
 
   // Compute the SeqOneByteString size and check if it fits into new space.
   Label if_lengthiszero(this), if_sizeissmall(this),
@@ -3134,7 +3167,9 @@ TNode<String> CodeStubAssembler::AllocateSeqOneByteString(
   GotoIf(Word32Equal(length, Uint32Constant(0)), &if_lengthiszero);
 
   Node* raw_size = GetArrayAllocationSize(
-      Signed(ChangeUint32ToWord(length)), UINT8_ELEMENTS, INTPTR_PARAMETERS,
+    // taint needs 2*length
+      Signed(WordShl(ChangeUint32ToWord(length), 1)),
+      UINT8_ELEMENTS, INTPTR_PARAMETERS,
       SeqOneByteString::kHeaderSize + kObjectAlignmentMask);
   TNode<WordT> size = WordAnd(raw_size, IntPtrConstant(~kObjectAlignmentMask));
   Branch(IntPtrLessThanOrEqual(size, IntPtrConstant(kMaxRegularHeapObjectSize)),
@@ -3152,7 +3187,9 @@ TNode<String> CodeStubAssembler::AllocateSeqOneByteString(
     StoreObjectFieldNoWriteBarrier(result, SeqOneByteString::kHashFieldOffset,
                                    Int32Constant(String::kEmptyHashField),
                                    MachineRepresentation::kWord32);
+    IncrementAndStoreTaintInstanceCounter(result);
     var_result.Bind(result);
+
     Goto(&if_join);
   }
 
@@ -3172,6 +3209,30 @@ TNode<String> CodeStubAssembler::AllocateSeqOneByteString(
   }
 
   BIND(&if_join);
+  {
+    Goto(&loop);
+  }
+
+  BIND(&loop);
+  {
+    Node* taint_start = IntPtrAdd(
+        IntPtrConstant(SeqOneByteString::kHeaderSize - kHeapObjectTag),
+        ChangeInt32ToIntPtr(Signed(length)));
+    Node* result = var_result.value();
+    // clear taint
+    Node* offset = var_offset.value();
+    GotoIf(WordEqual(offset, ChangeUint32ToWord(length)), &done_loop);
+    StoreNoWriteBarrier(
+      MachineRepresentation::kWord8,
+      result,
+      IntPtrAdd(taint_start, offset),
+      Int32Constant(0));
+    var_offset.Bind(IntPtrAdd(offset, IntPtrConstant(1)));
+    Goto(&loop);
+  }
+
+  BIND(&done_loop);
+
   return CAST(var_result.value());
 }
 
@@ -3182,6 +3243,9 @@ TNode<String> CodeStubAssembler::AllocateSeqTwoByteString(
     return CAST(LoadRoot(RootIndex::kempty_string));
   }
   Node* result = Allocate(SeqTwoByteString::SizeFor(length), flags);
+  Node* taint_start = IntPtrAdd(
+      IntPtrConstant(SeqTwoByteString::kHeaderSize - kHeapObjectTag),
+      IntPtrConstant(length * kShortSize));
   DCHECK(RootsTable::IsImmortalImmovable(RootIndex::kStringMap));
   StoreMapNoWriteBarrier(result, RootIndex::kStringMap);
   StoreObjectFieldNoWriteBarrier(result, SeqTwoByteString::kLengthOffset,
@@ -3190,6 +3254,15 @@ TNode<String> CodeStubAssembler::AllocateSeqTwoByteString(
   StoreObjectFieldNoWriteBarrier(result, SeqTwoByteString::kHashFieldOffset,
                                  Int32Constant(String::kEmptyHashField),
                                  MachineRepresentation::kWord32);
+  IncrementAndStoreTaintInstanceCounter(result);
+  // XXXstroucki size adjust?
+  for (int i = 0; i < (int)length; i++) {
+    StoreNoWriteBarrier(
+        MachineRepresentation::kWord8,
+        result,
+        IntPtrAdd(taint_start, IntPtrConstant(i)),
+        Int32Constant(0));
+  }
   return CAST(result);
 }
 
@@ -3198,6 +3271,9 @@ TNode<String> CodeStubAssembler::AllocateSeqTwoByteString(
   CSA_SLOW_ASSERT(this, IsZeroOrContext(context));
   Comment("AllocateSeqTwoByteString");
   VARIABLE(var_result, MachineRepresentation::kTagged);
+  VARIABLE(var_offset, MachineType::PointerRepresentation());
+  Label loop(this, &var_offset), done_loop(this);
+  var_offset.Bind(IntPtrConstant(0));
 
   // Compute the SeqTwoByteString size and check if it fits into new space.
   Label if_lengthiszero(this), if_sizeissmall(this),
@@ -3205,7 +3281,9 @@ TNode<String> CodeStubAssembler::AllocateSeqTwoByteString(
   GotoIf(Word32Equal(length, Uint32Constant(0)), &if_lengthiszero);
 
   Node* raw_size = GetArrayAllocationSize(
-      Signed(ChangeUint32ToWord(length)), UINT16_ELEMENTS, INTPTR_PARAMETERS,
+      // taint needs total length + 2*length
+      Signed(IntPtrAdd(WordShl(ChangeUint32ToWord(length), 1), ChangeUint32ToWord(length))),
+      UINT8_ELEMENTS, INTPTR_PARAMETERS, // XXXstroucki was uint16, but we need taint space as uint8
       SeqOneByteString::kHeaderSize + kObjectAlignmentMask);
   TNode<WordT> size = WordAnd(raw_size, IntPtrConstant(~kObjectAlignmentMask));
   Branch(IntPtrLessThanOrEqual(size, IntPtrConstant(kMaxRegularHeapObjectSize)),
@@ -3223,7 +3301,10 @@ TNode<String> CodeStubAssembler::AllocateSeqTwoByteString(
     StoreObjectFieldNoWriteBarrier(result, SeqTwoByteString::kHashFieldOffset,
                                    Int32Constant(String::kEmptyHashField),
                                    MachineRepresentation::kWord32);
+
     var_result.Bind(result);
+
+    IncrementAndStoreTaintInstanceCounter(result);
     Goto(&if_join);
   }
 
@@ -3233,16 +3314,43 @@ TNode<String> CodeStubAssembler::AllocateSeqTwoByteString(
     Node* result = CallRuntime(Runtime::kAllocateSeqTwoByteString, context,
                                ChangeUint32ToTagged(length));
     var_result.Bind(result);
+    IncrementAndStoreTaintInstanceCounter(result);
     Goto(&if_join);
   }
 
   BIND(&if_lengthiszero);
   {
-    var_result.Bind(LoadRoot(RootIndex::kempty_string));
+    Node* result = LoadRoot(RootIndex::kempty_string);
+    var_result.Bind(result);
+    IncrementAndStoreTaintInstanceCounter(result);
     Goto(&if_join);
   }
 
   BIND(&if_join);
+  {
+    Goto(&loop);
+  }
+
+  BIND(&loop);
+  {
+    Node* taint_start = IntPtrAdd(
+      IntPtrConstant(SeqTwoByteString::kHeaderSize - kHeapObjectTag),
+      WordShl(ChangeUint32ToWord(length), 1));
+    Node* result = var_result.value();
+    // clear taint
+    Node* offset = var_offset.value();
+    GotoIf(WordEqual(offset, ChangeUint32ToWord(length)), &done_loop);
+    StoreNoWriteBarrier(
+      MachineRepresentation::kWord8,
+      result,
+      IntPtrAdd(taint_start, offset),
+      Int32Constant(0));
+    var_offset.Bind(IntPtrAdd(offset, IntPtrConstant(1)));
+    Goto(&loop);
+  }
+
+  BIND(&done_loop);
+
   return CAST(var_result.value());
 }
 
@@ -5076,6 +5184,7 @@ void CodeStubAssembler::CopyPropertyArrayValues(Node* from_array,
 }
 
 void CodeStubAssembler::CopyStringCharacters(Node* from_string, Node* to_string,
+                                             TNode<IntPtrT> from_taint,
                                              TNode<IntPtrT> from_index,
                                              TNode<IntPtrT> to_index,
                                              TNode<IntPtrT> character_count,
@@ -5097,8 +5206,12 @@ void CodeStubAssembler::CopyStringCharacters(Node* from_string, Node* to_string,
   int header_size = SeqOneByteString::kHeaderSize - kHeapObjectTag;
   Node* from_offset = ElementOffsetFromIndex(from_index, from_kind,
                                              INTPTR_PARAMETERS, header_size);
+  TNode<IntPtrT> const to_length = IntPtrSub(to_index, from_index);
   Node* to_offset =
       ElementOffsetFromIndex(to_index, to_kind, INTPTR_PARAMETERS, header_size);
+  Node* to_taint = IntPtrAdd(
+      IntPtrConstant(header_size),
+      IntPtrMul(to_length, IntPtrConstant((to_kind == UINT16_ELEMENTS) ? kShortSize : 1)));
   Node* byte_count =
       ElementOffsetFromIndex(character_count, from_kind, INTPTR_PARAMETERS);
   Node* limit_offset = IntPtrAdd(from_offset, byte_count);
@@ -5112,7 +5225,9 @@ void CodeStubAssembler::CopyStringCharacters(Node* from_string, Node* to_string,
   int to_increment = 1 << ElementsKindToShiftSize(to_kind);
 
   VARIABLE(current_to_offset, MachineType::PointerRepresentation(), to_offset);
-  VariableList vars({&current_to_offset}, zone());
+  VARIABLE(current_from_taint, MachineType::PointerRepresentation(), from_taint);
+  VARIABLE(current_to_taint, MachineType::PointerRepresentation(), to_taint);
+  VariableList vars({&current_to_offset, &current_from_taint, &current_to_taint}, zone());
   int to_index_constant = 0, from_index_constant = 0;
   bool index_same = (from_encoding == to_encoding) &&
                     (from_index == to_index ||
@@ -5120,12 +5235,18 @@ void CodeStubAssembler::CopyStringCharacters(Node* from_string, Node* to_string,
                       ToInt32Constant(to_index, to_index_constant) &&
                       from_index_constant == to_index_constant));
   BuildFastLoop(vars, from_offset, limit_offset,
-                [this, from_string, to_string, &current_to_offset, to_increment,
+                [this, from_string, from_taint, to_string, to_taint, &current_to_taint, &current_from_taint, &current_to_offset, to_increment,
                  type, rep, index_same](Node* offset) {
                   Node* value = Load(type, from_string, offset);
+                  Node* taint = Load(MachineType::Uint8(), from_taint, current_from_taint.value());
                   StoreNoWriteBarrier(
                       rep, to_string,
                       index_same ? offset : current_to_offset.value(), value);
+                  StoreNoWriteBarrier(
+                    MachineRepresentation::kWord8, to_taint,
+                    current_to_taint.value(), taint);
+                  Increment(&current_to_taint, 1);
+                  Increment(&current_from_taint, 1);
                   if (!index_same) {
                     Increment(&current_to_offset, to_increment);
                   }
@@ -6719,6 +6840,15 @@ TNode<String> CodeStubAssembler::StringFromSingleCharCode(TNode<Int32T> code) {
       StoreNoWriteBarrier(
           MachineRepresentation::kWord8, result,
           IntPtrConstant(SeqOneByteString::kHeaderSize - kHeapObjectTag), code);
+
+      // Init taint
+      StoreNoWriteBarrier(
+        MachineRepresentation::kWord8, result,
+        IntPtrConstant(SeqOneByteString::kHeaderSize -
+                       kHeapObjectTag +
+                       kCharSize),
+        Int32Constant(0));
+
       StoreFixedArrayElement(cache, code_index, result);
       var_result.Bind(result);
       Goto(&if_done);
@@ -6739,6 +6869,15 @@ TNode<String> CodeStubAssembler::StringFromSingleCharCode(TNode<Int32T> code) {
     StoreNoWriteBarrier(
         MachineRepresentation::kWord16, result,
         IntPtrConstant(SeqTwoByteString::kHeaderSize - kHeapObjectTag), code);
+
+    // Init taint
+    StoreNoWriteBarrier(
+        MachineRepresentation::kWord8, result,
+        IntPtrConstant(SeqTwoByteString::kHeaderSize -
+                       kHeapObjectTag +
+                       kShortSize),
+        Int32Constant(0));
+
     var_result.Bind(result);
     Goto(&if_done);
   }
@@ -6754,7 +6893,7 @@ TNode<String> CodeStubAssembler::StringFromSingleCharCode(TNode<Int32T> code) {
 // |from_string| must be a sequential string.
 // 0 <= |from_index| <= |from_index| + |character_count| < from_string.length.
 TNode<String> CodeStubAssembler::AllocAndCopyStringCharacters(
-    Node* from, Node* from_instance_type, TNode<IntPtrT> from_index,
+    Node* from, TNode<IntPtrT> from_taint, Node* from_instance_type, TNode<IntPtrT> from_index,
     TNode<IntPtrT> character_count) {
   Label end(this), one_byte_sequential(this), two_byte_sequential(this);
   TVARIABLE(String, var_result);
@@ -6767,7 +6906,7 @@ TNode<String> CodeStubAssembler::AllocAndCopyStringCharacters(
   {
     TNode<String> result = AllocateSeqOneByteString(
         NoContextConstant(), Unsigned(TruncateIntPtrToInt32(character_count)));
-    CopyStringCharacters(from, result, from_index, IntPtrConstant(0),
+    CopyStringCharacters(from, result, from_taint, from_index, IntPtrConstant(0),
                          character_count, String::ONE_BYTE_ENCODING,
                          String::ONE_BYTE_ENCODING);
     var_result = result;
@@ -6779,7 +6918,7 @@ TNode<String> CodeStubAssembler::AllocAndCopyStringCharacters(
   {
     TNode<String> result = AllocateSeqTwoByteString(
         NoContextConstant(), Unsigned(TruncateIntPtrToInt32(character_count)));
-    CopyStringCharacters(from, result, from_index, IntPtrConstant(0),
+    CopyStringCharacters(from, result, from_taint, from_index, IntPtrConstant(0),
                          character_count, String::TWO_BYTE_ENCODING,
                          String::TWO_BYTE_ENCODING);
     var_result = result;
@@ -6793,6 +6932,29 @@ TNode<String> CodeStubAssembler::AllocAndCopyStringCharacters(
 TNode<String> CodeStubAssembler::SubString(TNode<String> string,
                                            TNode<IntPtrT> from,
                                            TNode<IntPtrT> to) {
+
+  Label end(this), runtime(this);
+  TVARIABLE(String, var_result);
+
+  TNode<IntPtrT> const substr_length = IntPtrSub(to, from);
+  TNode<IntPtrT> const string_length = LoadStringLengthAsWord(string);
+
+  // Begin dispatching based on substring length.
+  Label original_string_or_invalid_length(this);
+  GotoIf(UintPtrGreaterThanOrEqual(substr_length, string_length),
+         &original_string_or_invalid_length);
+
+  // A real substring (substr_length < string_length).
+  Label empty(this);
+  GotoIf(IntPtrEqual(substr_length, IntPtrConstant(0)), &empty);
+
+  Label single_char(this);
+  GotoIf(IntPtrEqual(substr_length, IntPtrConstant(1)), &single_char);
+
+// XXXstroucki use the runtime implementation
+  Goto(&runtime);
+// XXXstroucki make sure to handle the from_taint before reenabling this path
+/*
   TVARIABLE(String, var_result);
   ToDirectStringAssembler to_direct(state(), string);
   Label end(this), runtime(this);
@@ -6801,7 +6963,6 @@ TNode<String> CodeStubAssembler::SubString(TNode<String> string,
   TNode<IntPtrT> const string_length = LoadStringLengthAsWord(string);
 
   // Begin dispatching based on substring length.
-
   Label original_string_or_invalid_length(this);
   GotoIf(UintPtrGreaterThanOrEqual(substr_length, string_length),
          &original_string_or_invalid_length);
@@ -6828,6 +6989,7 @@ TNode<String> CodeStubAssembler::SubString(TNode<String> string,
       Label next(this);
 
       // Short slice.  Copy instead of slicing.
+      // XXXstroucki default is 13
       GotoIf(IntPtrLessThan(substr_length,
                             IntPtrConstant(SlicedString::kMinLength)),
              &next);
@@ -6858,13 +7020,13 @@ TNode<String> CodeStubAssembler::SubString(TNode<String> string,
       }
 
       BIND(&next);
-    }
+    } // string slices
 
     // The subject string can only be external or sequential string of either
     // encoding at this point.
     GotoIf(to_direct.is_external(), &external_string);
 
-    var_result = AllocAndCopyStringCharacters(direct_string, instance_type,
+    var_result = AllocAndCopyStringCharacters(direct_string, direct_taint, instance_type,
                                               offset, substr_length);
 
     Counters* counters = isolate()->counters();
@@ -6879,14 +7041,14 @@ TNode<String> CodeStubAssembler::SubString(TNode<String> string,
     Node* const fake_sequential_string = to_direct.PointerToString(&runtime);
 
     var_result = AllocAndCopyStringCharacters(
-        fake_sequential_string, instance_type, offset, substr_length);
+        fake_sequential_string, taint, instance_type, offset, substr_length);
 
     Counters* counters = isolate()->counters();
     IncrementCounter(counters->sub_string_native(), 1);
-
     Goto(&end);
   }
 
+*/
   BIND(&empty);
   {
     var_result = EmptyStringConstant();
@@ -6947,6 +7109,8 @@ ToDirectStringAssembler::ToDirectStringAssembler(
   var_is_external_.Bind(Int32Constant(0));
 }
 
+// XXXstroucki I guess this thing loops until the string is either
+// external or internal sequential
 TNode<String> ToDirectStringAssembler::TryToDirect(Label* if_bailout) {
   VariableList vars({&var_string_, &var_offset_, &var_instance_type_}, zone());
   Label dispatch(this, vars);
@@ -7035,7 +7199,7 @@ TNode<String> ToDirectStringAssembler::TryToDirect(Label* if_bailout) {
 
 TNode<RawPtrT> ToDirectStringAssembler::TryToSequential(
     StringPointerKind ptr_kind, Label* if_bailout) {
-  CHECK(ptr_kind == PTR_TO_DATA || ptr_kind == PTR_TO_STRING);
+  CHECK(ptr_kind == PTR_TO_DATA || ptr_kind == PTR_TO_STRING || ptr_kind == PTR_TO_TAINT);
 
   TVARIABLE(RawPtrT, var_result);
   Label out(this), if_issequential(this), if_isexternal(this, Label::kDeferred);
@@ -7050,10 +7214,24 @@ TNode<RawPtrT> ToDirectStringAssembler::TryToSequential(
       result = IntPtrAdd(result, IntPtrConstant(SeqOneByteString::kHeaderSize -
                                                 kHeapObjectTag));
     }
+    if (ptr_kind == PTR_TO_TAINT) {
+      TNode<IntPtrT> const len = LoadStringLengthAsWord(var_string_.value());
+      result = IntPtrAdd(result, IntPtrConstant(SeqOneByteString::kHeaderSize -
+                                                kHeapObjectTag));
+      Label two_byte_slice(this), seq_end(this);
+      Branch(IsOneByteStringInstanceType(var_instance_type_.value()),
+             &seq_end, &two_byte_slice);
+      BIND(&two_byte_slice);
+      result = IntPtrAdd(result, len);
+      Goto(&seq_end);
+      BIND(&seq_end);
+      result = IntPtrAdd(result, len);
+    }
     var_result = ReinterpretCast<RawPtrT>(result);
     Goto(&out);
   }
 
+  // XXXstroucki in external, the resource has the taint
   BIND(&if_isexternal);
   {
     GotoIf(IsUncachedExternalStringInstanceType(var_instance_type_.value()),
@@ -7066,6 +7244,9 @@ TNode<RawPtrT> ToDirectStringAssembler::TryToSequential(
       result = IntPtrSub(result, IntPtrConstant(SeqOneByteString::kHeaderSize -
                                                 kHeapObjectTag));
     }
+    if (ptr_kind == PTR_TO_TAINT) {
+      result = LoadObjectField<IntPtrT>(string, ExternalString::kResourceTaintOffset);
+    }
     var_result = ReinterpretCast<RawPtrT>(result);
     Goto(&out);
   }
@@ -7204,6 +7385,9 @@ TNode<String> CodeStubAssembler::StringAdd(Node* context, TNode<String> left,
     BIND(&non_cons);
 
     Comment("Full string concatenate");
+// XXXstroucki jump to runtime implementation
+GotoIf(IntPtrEqual(IntPtrConstant(0), IntPtrConstant(0)), &runtime);
+// XXXstroucki fix the from_taint before reenabling this path
     Node* left_instance_type = LoadInstanceType(var_left.value());
     Node* right_instance_type = LoadInstanceType(var_right.value());
     // Compute intersection and difference of instance types.
@@ -7227,10 +7411,11 @@ TNode<String> CodeStubAssembler::StringAdd(Node* context, TNode<String> left,
            &two_byte);
     // One-byte sequential string case
     result = AllocateSeqOneByteString(context, new_length);
-    CopyStringCharacters(var_left.value(), result.value(), IntPtrConstant(0),
+    TNode<IntPtrT> var_left_taint = IntPtrConstant(0);
+    CopyStringCharacters(var_left.value(), result.value(), var_left_taint, IntPtrConstant(0),
                          IntPtrConstant(0), word_left_length,
                          String::ONE_BYTE_ENCODING, String::ONE_BYTE_ENCODING);
-    CopyStringCharacters(var_right.value(), result.value(), IntPtrConstant(0),
+    CopyStringCharacters(var_right.value(), result.value(), var_left_taint, IntPtrConstant(0),
                          word_left_length, word_right_length,
                          String::ONE_BYTE_ENCODING, String::ONE_BYTE_ENCODING);
     Goto(&done_native);
@@ -7239,11 +7424,12 @@ TNode<String> CodeStubAssembler::StringAdd(Node* context, TNode<String> left,
     {
       // Two-byte sequential string case
       result = AllocateSeqTwoByteString(context, new_length);
-      CopyStringCharacters(var_left.value(), result.value(), IntPtrConstant(0),
+      TNode<IntPtrT> var_left_taint = IntPtrConstant(0);
+      CopyStringCharacters(var_left.value(), result.value(), var_left_taint, IntPtrConstant(0),
                            IntPtrConstant(0), word_left_length,
                            String::TWO_BYTE_ENCODING,
                            String::TWO_BYTE_ENCODING);
-      CopyStringCharacters(var_right.value(), result.value(), IntPtrConstant(0),
+      CopyStringCharacters(var_right.value(), result.value(), var_left_taint, IntPtrConstant(0),
                            word_left_length, word_right_length,
                            String::TWO_BYTE_ENCODING,
                            String::TWO_BYTE_ENCODING);
@@ -7317,6 +7503,7 @@ TNode<String> CodeStubAssembler::StringFromSingleCodePoint(
         MachineRepresentation::kWord32, value,
         IntPtrConstant(SeqTwoByteString::kHeaderSize - kHeapObjectTag),
         codepoint);
+// XXXstroucki taint?
     var_result.Bind(value);
     Goto(&return_result);
   }
```




-------------------

Things to ask Michael:
- How does the IPC between the blink and V8 work
    - especially from the protobuf derivative that was added
- How does the controlflow work
    - from: the functions defined in the `api.cc`
    - to: functions defined in `src/taint_tracking/...`