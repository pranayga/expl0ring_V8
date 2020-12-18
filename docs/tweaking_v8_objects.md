# Tweaking / Adding a new Object type to V8
> Documentation is a love letter that you write to your future self. - Damian Conway

Commit SHA: 600241ea64

In this post, we shall take a look at the process which one might follow in order to modify / add to existing objects structures to store and propogate taint data.

Let's start by taking a look at the changes which were made to the Objects and then proceed with how this integrates with the test of the system. Remember that our goal is to include tainting information along with any String class instance that we create.

### Frames
```
 src/frames.cc                                      |   41 +
 src/frames.h                                       |   19 +
```

Frames are part of the execution framework inside V8. Frames help maintain stack frames for JSFunctions while they are executed and processed by the V8 engine. A stack contains parameters like the arguments passed to the JSFunction, parameters, context etc. Each Frame is marked by a Frame id slot to uniquely identify it. More details are described in`src/execution/frame-constants.h`  for the newer V8 revisions.

In the code below, we augemtn the `Print()s` so that we can retrive the taint data for the current iframe. This allows us to print the taint data for objects more easily. This function is more of a helper function to help use debug better.

```diff
diff --git a/src/frames.cc b/src/frames.cc
index 93a4c7aca1..350049ce90 100644
--- a/src/frames.cc
+++ b/src/frames.cc
@@ -415,6 +415,10 @@ Code GetContainingCode(Isolate* isolate, Address pc) {
 }
 }  // namespace
 
+StackFrame::TaintStackFrameInfo StackFrame::InfoForTaintLog() {
+  return TaintStackFrameInfo();
+}
+
 Code StackFrame::LookupCode() const {
   Code result = GetContainingCode(isolate(), pc());
   DCHECK_GE(pc(), result->InstructionStart());
@@ -1971,6 +1975,43 @@ void PrintFunctionSource(StringStream* accumulator, SharedFunctionInfo shared,
 
 }  // namespace
 
+StackFrame::TaintStackFrameInfo JavaScriptFrame::InfoForTaintLog() {
+  DisallowHeapAllocation no_gc;
+  JSFunction function = this->function();
+  SharedFunctionInfo shared = function->shared();
+  Object script_obj = shared->script();
+  //ScopeInfo scope_info = shared->scope_info();
+  //Code code = function->code();
+
+  TaintStackFrameInfo answer;
+  auto isolate = shared->GetIsolate();
+  answer.shared_info = Handle<SharedFunctionInfo>(shared, isolate);
+  if (script_obj->IsScript()) {
+    Script script = Script::cast(script_obj);
+    answer.script = Handle<Script>(script, isolate);
+
+    // XXXstroucki In 8340a86 Code::FUNCTION went away
+    //Address pc = this->pc();
+    if (is_interpreted()) {
+      const InterpretedFrame* iframe =
+        reinterpret_cast<const InterpretedFrame*>(this);
+      BytecodeArray bytecodes = iframe->GetBytecodeArray();
+      int offset = iframe->GetBytecodeOffset();
+      answer.position = AbstractCode::cast(bytecodes)->
+        SourcePositionWithTaintTrackingAstIndex(
+            offset, &answer.ast_taint_tracking_index);
+      answer.lineNumber = script->GetLineNumber(answer.position) + 1;
+    } else {
+      answer.position = shared->StartPosition();
+      answer.lineNumber = script->GetLineNumber(answer.position) + 1;
+      answer.ast_taint_tracking_index = TaintStackFrameInfo::NO_SOURCE_INFO;
+    }
+
+    return answer;
+  } else {
+    return answer;
+  }
+}
 
 void JavaScriptFrame::Print(StringStream* accumulator,
                             PrintMode mode,
diff --git a/src/frames.h b/src/frames.h
index 6fabe4fddb..30d08e1058 100644
--- a/src/frames.h
+++ b/src/frames.h
@@ -269,6 +269,23 @@ class StackFrame {
   virtual void Print(StringStream* accumulator, PrintMode mode,
                      int index) const;
 
+  struct TaintStackFrameInfo {
+    static constexpr int NO_AST_INDEX = -1;
+    static constexpr int NO_SOURCE_INFO = -2;
+    static constexpr int SOURCE_POS_DEFAULT = -3;
+    static constexpr int UNINSTRUMENTED = -4;
+
+    MaybeHandle<Script> script = MaybeHandle<Script>();
+    MaybeHandle<SharedFunctionInfo> shared_info = MaybeHandle<SharedFunctionInfo
+>();
+    int lineNumber = -1;
+    int position = -1;
+    int ast_taint_tracking_index = NO_AST_INDEX;
+  };
+
+
+  virtual TaintStackFrameInfo InfoForTaintLog();
+
   Isolate* isolate() const { return isolate_; }
 
   void operator=(const StackFrame& original) = delete;
@@ -717,6 +734,8 @@ class JavaScriptFrame : public StandardFrame {
   void Print(StringStream* accumulator, PrintMode mode,
              int index) const override;
 
+  TaintStackFrameInfo InfoForTaintLog() override;
+
   // Determine the code for the frame.
   Code unchecked_code() const override;
```

### Objects

```
 src/objects.cc                                     |  131 +-
 src/objects.h                                      |    9 +-
 ...
 src/objects/string.h                               |   22 +-
```
`Objects.h` or now `src/objects/objects.h` is a central location for all the predefined Objects that V8 internally uses to implement all the basic classes in V8. The Maps for these classes are predefined and these form the basic building blocks for any object that the user creates. 
Javascript is a weakly typed language. On top of that, it's much more premissive than `C++`, IE you can mix strings and Int inside an array (which are different types). In order to accomodate for this, as well as have fast execution with the bound checking, V8 has multiple kinds of Arrays, Maps etc.

All Objects are derived fom what's called HeapObject (with the only other kind being a SMI). HeapObjects are stored on the V8 Heap and garbage collected. Whenever a piece of JS code needs to create a new Object, it can do so with the help of `Factory` specific to the isolate. That's where V8's Objects are created. Below is a small scippet from the Object inheritence structure in V8:
```
//
// Most object types in the V8 JavaScript are described in this file.
//
// Inheritance hierarchy:
// - Object
//   - Smi          (immediate small integer)
//   - TaggedIndex  (properly sign-extended immediate small integer)
//   - HeapObject   (superclass for everything allocated in the heap)
//     - JSReceiver  (suitable for property access)
//       - JSObject
//         - JSArray
//         - JSArrayBuffer
......
//     - PrimitiveHeapObject
//       - BigInt
//       - HeapNumber
//       - Name
//         - String
//           - SeqString
//             - SeqOneByteString
//             - SeqTwoByteString
//           - SlicedString
//           - ConsString
//           - ThinString
//           - ExternalString
//             - ExternalOneByteString
//             - ExternalTwoByteString
//           - InternalizedString
//             - SeqInternalizedString
//               - SeqOneByteInternalizedString
//               - SeqTwoByteInternalizedString
//             - ConsInternalizedString
//             - ExternalInternalizedString
//               - ExternalOneByteInternalizedString
//               - ExternalTwoByteInternalizedString

```

Quite of lot of String right :D. Our Foucus here is on the String class. Specifically because we want to modify String class to include taint data which we can modify as and when different Strings interact. Since other String classes inherity from the base `String` class, they should also automatically inherit those modifications. Now let's take a look at the major changes from the `diff`.

----
#### `Objects.cc`

Let's take a look at the changes to Objects.cc which are quite numerous.

1. `GetPropertyWithAccessor`: Here while invoking the API Fucntion, we need to include the taint tracking frame' ACCESSOR

```diff
diff --git a/src/objects.cc b/src/objects.cc
index 6a297b93be..bf55e52c89 100644
--- a/src/objects.cc
+++ b/src/objects.cc
@@ -106,6 +106,7 @@
 #include "src/string-builder-inl.h"
 #include "src/string-search.h"
 #include "src/string-stream.h"
+#include "src/taint_tracking.h"
 #include "src/unicode-decoder.h"
 #include "src/unicode-inl.h"
 #include "src/utils-inl.h"
@@ -1657,7 +1658,7 @@ MaybeHandle<Object> Object::GetPropertyWithAccessor(LookupIterator* it) {
     isolate->set_context(*holder->GetCreationContext());
     return Builtins::InvokeApiFunction(
         isolate, false, Handle<FunctionTemplateInfo>::cast(getter), receiver, 0,
-        nullptr, isolate->factory()->undefined_value());
+        nullptr, isolate->factory()->undefined_value(), tainttracking::FrameType::GETTER_ACCESSOR);
   } else if (getter->IsCallable()) {
     // TODO(rossberg): nicer would be to cast to some JSCallable here...
     return Object::GetPropertyWithDefinedGetter(
```
2. `SetPropertyWithAccessor`: We seem to set the stack frame's setter function with traint tracking's Frame function which we control.
```diff
@@ -1736,6 +1737,9 @@ Maybe<bool> Object::SetPropertyWithAccessor(LookupIterator* it,
           Nothing<bool>());
     }
 
+    tainttracking::RuntimePrepareSymbolicStackFrame(
+        isolate, tainttracking::FrameType::SETTER_ACCESSOR);
+
     // The actual type of setter callback is either
     // v8::AccessorNameSetterCallback or
     // i::Accesors::AccessorNameBooleanSetterCallback, depending on whether the
```
3. `SetPropertyWithAccessor`: We seem to add an argument literal to the tainting stack frame. post which we set the runtime reciever to our own taintracking version defined in the `Frames.cc`
```diff
@@ -1744,12 +1748,21 @@ Maybe<bool> Object::SetPropertyWithAccessor(LookupIterator* it,
     // its Call method.
     PropertyCallbackArguments args(isolate, info->data(), *receiver, *holder,
                                    should_throw);
+
+    tainttracking::RuntimeAddLiteralArgumentToStackFrame(isolate, value);
+    tainttracking::RuntimeSetReceiver(
+        isolate, holder, handle(ReadOnlyRoots(isolate).undefined_value(), isolate));
+    tainttracking::RuntimeEnterSymbolicStackFrame(isolate);
+
     Handle<Object> result = args.CallAccessorSetter(info, name, value);
     // In the case of AccessorNameSetterCallback, we know that the result value
     // cannot have been set, so the result of Call will be null.  In the case of
     // AccessorNameBooleanSetterCallback, the result will either be null
     // (signalling an exception) or a boolean Oddball.
     RETURN_VALUE_IF_SCHEDULED_EXCEPTION(isolate, Nothing<bool>());
+
+    tainttracking::RuntimeExitSymbolicStackFrame(isolate);
+
     if (result.is_null()) return Just(true);
     DCHECK(result->BooleanValue(isolate) || should_throw == kDontThrow);
     return Just(result->BooleanValue(isolate));
```
4. `SetPropertyWithAccessor`: We seem to change the accessor again here.
```diff
@@ -1765,7 +1778,8 @@ Maybe<bool> Object::SetPropertyWithAccessor(LookupIterator* it,
         isolate, Builtins::InvokeApiFunction(
                      isolate, false, Handle<FunctionTemplateInfo>::cast(setter),
                      receiver, arraysize(argv), argv,
-                     isolate->factory()->undefined_value()),
+                     isolate->factory()->undefined_value(),
+                     tainttracking::FrameType::SETTER_ACCESSOR),
         Nothing<bool>());
     return Just(true);
   } else if (setter->IsCallable()) {
```
5. `GetPropertyWithDefinedGetter`: We tweak the wrapper to use out version of GETTER defined in the `frames.cc`.
```diff
@@ -1799,7 +1813,7 @@ MaybeHandle<Object> Object::GetPropertyWithDefinedGetter(
     return MaybeHandle<Object>();
   }
 
-  return Execution::Call(isolate, getter, receiver, 0, nullptr);
+  return Execution::Call(isolate, getter, receiver, 0, nullptr, tainttracking::FrameType::GETTER_ACCESSOR);
 }
 
```
6. `SetPropertyWithDefinedSetter`: Here we seem to change the defined setter with we set the property to our own version.
```diff
@@ -1811,7 +1825,8 @@ Maybe<bool> Object::SetPropertyWithDefinedSetter(Handle<Object> receiver,
 
   Handle<Object> argv[] = { value };
   RETURN_ON_EXCEPTION_VALUE(isolate, Execution::Call(isolate, setter, receiver,
-                                                     arraysize(argv), argv),
+                                                     arraysize(argv), argv,
+                                                     tainttracking::FrameType::SETTER_ACCESSOR),
                             Nothing<bool>());
   return Just(true);
 }
```
7. `SlowFlatten`: In this function, while flattening the string, we make sure to copy over the taint data from the src's taint data to the dest's taint data.
```diff
@@ -2638,6 +2653,7 @@ Handle<String> String::SlowFlatten(Isolate* isolate, Handle<ConsString> cons,
     DisallowHeapAllocation no_gc;
     WriteToFlat(*cons, flat->GetChars(no_gc), 0, length);
     result = flat;
+    tainttracking::OnNewSubStringCopy(*cons, *result, 0, length);
   } else {
     Handle<SeqTwoByteString> flat = isolate->factory()->NewRawTwoByteString(
         length, tenure).ToHandleChecked();

@@ -2665,8 +2681,11 @@ bool String::MakeExternal(v8::String::ExternalStringResource* resource) {
     DCHECK(static_cast<size_t>(this->length()) == resource->length());
     ScopedVector<uc16> smart_chars(this->length());
     String::WriteToFlat(*this, smart_chars.start(), 0, this->length());
+    ScopedVector<uint8_t> smart_taint(this->length());
     DCHECK_EQ(0, memcmp(smart_chars.start(), resource->data(),
                         resource->length() * sizeof(smart_chars[0])));
+    tainttracking::FlattenTaintData(*this, smart_taint.start(),
+      0, this->length());
   }
 #endif  // DEBUG
   int size = this->Size();  // Byte size of the original string.

@@ -2717,6 +2736,7 @@ bool String::MakeExternal(v8::String::ExternalStringResource* resource) {
 
   // Byte size of the external String object.
   int new_size = this->SizeFromMap(new_map);
+  DCHECK_GE(size, new_size);
   heap->CreateFillerObjectAt(this->address() + new_size, size - new_size,
                              ClearRecordedSlots::kNo);
   if (has_pointers) {
```
8. `MakeExternal`: This function is responsible for chaning the scope of a string. Thus here, we make sure to propogate the taint if the type of the object is `String`
```diff
@@ -2752,8 +2772,10 @@ bool String::MakeExternal(v8::String::ExternalOneByteStringResource* resource) {
     }
     ScopedVector<char> smart_chars(this->length());
     String::WriteToFlat(*this, smart_chars.start(), 0, this->length());
+    ScopedVector<uint8_t> smart_taint(this->length()); // XXXstroucki seemed necessary
     DCHECK_EQ(0, memcmp(smart_chars.start(), resource->data(),
                         resource->length() * sizeof(smart_chars[0])));
+    tainttracking::FlattenTaintData(*this, smart_taint.start(), 0, this->length());
   }
 #endif  // DEBUG
   int size = this->Size();  // Byte size of the original string.
```
9. `ToPrimitive`: This function is responisble to extract the primitive from an object. On faliure, it should return and preserve the taint data. 
```diff
@@ -8884,9 +8908,12 @@ MaybeHandle<Object> JSReceiver::ToPrimitive(Handle<JSReceiver> receiver,
     Handle<Object> hint_string =
         isolate->factory()->ToPrimitiveHintString(hint);
     Handle<Object> result;
+
+    // TODO: mark with the tainttracking frame type, and add a way to
+    // prepare for the frame type in the ast_serialization code
     ASSIGN_RETURN_ON_EXCEPTION(
         isolate, result,
-        Execution::Call(isolate, exotic_to_prim, receiver, 1, &hint_string),
+        Execution::Call(isolate, exotic_to_prim, receiver, 1, &hint_string, frame_type),
         Object);
     if (result->IsPrimitive()) return result;
     THROW_NEW_ERROR(isolate,
```
##### Major String class:
10: `StringClass`: Here we take a look at the changes made to the string calss itself. We start by changing the internal representation of how the string class is managed. This involves changing the way it's created; that is it now uses two bytes to store each byte of data. One for the character and the other for the taint data.
```diff
@@ -17346,18 +17420,29 @@ namespace {
 
 template <class StringClass>
 void MigrateExternalStringResource(Isolate* isolate, String from, String to) {
-  StringClass cast_from = StringClass::cast(from);
+  // XXXstroucki protect against crash where internalised representation
+  // is of one byte and external string is two byte
+  void *from_resource = nullptr;
+  if (from->IsExternalTwoByteString()) {
+    ExternalTwoByteString cast_from = ExternalTwoByteString::cast(from);
+    from_resource = (void *)cast_from->resource();
+  } else {
+    ExternalOneByteString cast_from = ExternalOneByteString::cast(from);
+    from_resource = (void *)cast_from->resource();
+  }
+    
   StringClass cast_to = StringClass::cast(to);
   const typename StringClass::Resource* to_resource = cast_to->resource();
   if (to_resource == nullptr) {
     // |to| is a just-created internalized copy of |from|. Migrate the resource.
+    StringClass cast_from = StringClass::cast(from);
     cast_to->SetResource(isolate, cast_from->resource());
     // Zap |from|'s resource pointer to reflect the fact that |from| has
     // relinquished ownership of its resource.
     isolate->heap()->UpdateExternalString(
         from, ExternalString::cast(from)->ExternalPayloadSize(), 0);
     cast_from->SetResource(isolate, nullptr);
-  } else if (to_resource != cast_from->resource()) {
+  } else if ((const void *)to_resource != from_resource) {
     // |to| already existed and has its own resource. Finalize |from|.
     isolate->heap()->FinalizeExternalString(from);
   }

```
11. `LookupString`: This also changes the way we look from the string from our string table. We accomodate the difference in structure to avoid taint bytes from getting compared.
```diff
@@ -17409,7 +17496,26 @@ Handle<String> StringTable::LookupString(Isolate* isolate,
   if (string->IsInternalizedString()) return string;
 
   InternalizedStringKey key(string);
-  Handle<String> result = LookupKey(isolate, &key);
+/*
+if (string->length() == 3 && string->IsExternalTwoByteString()) {
+  Handle<ExternalTwoByteString> bla = Handle<ExternalTwoByteString>::cast(string);
+  char* foo = (char *)bla->GetChars();
+  if (foo[0] == 'k' && foo[2] == 'e' && foo[4] == 'y') {
+    static volatile int bar = 1;while (bar);
+  }
+}
+*/
+if (string->IsExternalTwoByteString()) {
+    //static volatile int bar = 1;while (bar);
+}
+  Handle<String> result = LookupKey(isolate, &key, string);
+/*
+if (string->IsExternalTwoByteString()) {
+  USE(exitpoint);
+  // need to compare
+  //DCHECK(result->IsExternalTwoByteString());
+}
+*/
 
   if (FLAG_thin_strings) {
     if (!string->IsInternalizedString()) {

@@ -17438,12 +17544,16 @@ Handle<String> StringTable::LookupString(Isolate* isolate,
 }
 
 // static
-Handle<String> StringTable::LookupKey(Isolate* isolate, StringTableKey* key) {
+Handle<String> StringTable::LookupKey(Isolate* isolate, StringTableKey* key, Handle<String> string) {
   Handle<StringTable> table = isolate->factory()->string_table();
+if (!string->is_null() && string->IsExternalTwoByteString()) {
+  //static volatile int foo = 1;while (foo);
+}
   int entry = table->FindEntry(isolate, key);
 
   // String already in table.
   if (entry != kNotFound) {
+exitpoint=1;
     return handle(String::cast(table->KeyAt(entry)), isolate);
   }
 
```

### Factory

```
 src/heap/factory.cc                                |  137 +-
 src/heap/factory.h                                 |    1 +
 src/heap/heap-inl.h                                |    1 +
 src/heap/heap.cc                                   |    2 +-
```

V8 factory is exactly what you'd expect; It generates V8 heap objects and updates the required entires to enable garbage collection when they are no longer in use. The major changes in this sections are tweaks which allow us to propogate the taint from source string when a new string is getting created.

Below we also see a pattern of these lines:
```diff
+  DisallowHeapAllocation no_gc;
+  tainttracking::OnNewExternalString(*external_string);
+tainttracking::DebugCheckTaint(*external_string);
```
This basically tells the engine no garbage collect the greations. Then we create a new external_string with a corresponding tainting info.

```diff
diff --git a/src/heap/factory.cc b/src/heap/factory.cc
index 0f7e638a35..ce73a004e5 100644
--- a/src/heap/factory.cc
+++ b/src/heap/factory.cc
@@ -40,6 +40,7 @@
 #include "src/objects/scope-info.h"
 #include "src/objects/stack-frame-info-inl.h"
 #include "src/objects/struct-inl.h"
+#include "src/taint_tracking.h"
 #include "src/unicode-cache.h"
 #include "src/unicode-decoder.h"
 
@@ -627,7 +628,8 @@ Handle<String> Factory::InternalizeTwoByteString(Vector<const uc16> string) {
 
 template <class StringTableKey>
 Handle<String> Factory::InternalizeStringWithKey(StringTableKey* key) {
-  return StringTable::LookupKey(isolate(), key);
+  Handle<String> result = StringTable::LookupKey(isolate(), key, isolate()->factory()->empty_string());
+  return result;
 }
 
 MaybeHandle<String> Factory::NewStringFromOneByte(Vector<const uint8_t> string,
@@ -635,7 +637,9 @@ MaybeHandle<String> Factory::NewStringFromOneByte(Vector<const uint8_t> string,
   DCHECK_NE(pretenure, TENURED_READ_ONLY);
   int length = string.length();
   if (length == 0) return empty_string();
-  if (length == 1) return LookupSingleCharacterStringFromCode(string[0]);
+  if (length == 1 && tainttracking::kInternalizedStringsEnabled) {
+    return LookupSingleCharacterStringFromCode(string[0]);
+  }
   Handle<SeqOneByteString> result;
   ASSIGN_RETURN_ON_EXCEPTION(isolate(), result,
                              NewRawOneByteString(string.length(), pretenure),
@@ -645,6 +649,8 @@ MaybeHandle<String> Factory::NewStringFromOneByte(Vector<const uint8_t> string,
   // Copy the characters into the new object.
   CopyChars(SeqOneByteString::cast(*result)->GetChars(no_gc), string.start(),
             length);
+  tainttracking::OnNewStringLiteral(*result);
+tainttracking::DebugCheckTaint(*result);
   return result;
 }
 
@@ -685,6 +691,8 @@ MaybeHandle<String> Factory::NewStringFromUtf8(Vector<const char> string,
 
   // Now write the remainder.
   decoder->WriteUtf16(data, utf16_length, non_ascii);
+  tainttracking::OnNewStringLiteral(*result);
+tainttracking::DebugCheckTaint(*result);
   return result;
 }
 
@@ -740,6 +748,7 @@ MaybeHandle<String> Factory::NewStringFromUtf8SubString(
 
   // Now write the remainder.
   decoder->WriteUtf16(data, utf16_length, non_ascii);
+tainttracking::DebugCheckTaint(*result);
   return result;
 }
 
@@ -749,12 +758,16 @@ MaybeHandle<String> Factory::NewStringFromTwoByte(const uc16* string,
   DCHECK_NE(pretenure, TENURED_READ_ONLY);
   if (length == 0) return empty_string();
   if (String::IsOneByte(string, length)) {
-    if (length == 1) return LookupSingleCharacterStringFromCode(string[0]);
+    if (length == 1 && tainttracking::kInternalizedStringsEnabled) {
+      return LookupSingleCharacterStringFromCode(string[0]);
+    }
     Handle<SeqOneByteString> result;
     ASSIGN_RETURN_ON_EXCEPTION(isolate(), result,
                                NewRawOneByteString(length, pretenure), String);
     DisallowHeapAllocation no_gc;
     CopyChars(result->GetChars(no_gc), string, length);
+    tainttracking::OnNewStringLiteral(*result);
+tainttracking::DebugCheckTaint(*result);
     return result;
   } else {
     Handle<SeqTwoByteString> result;
@@ -762,6 +775,8 @@ MaybeHandle<String> Factory::NewStringFromTwoByte(const uc16* string,
                                NewRawTwoByteString(length, pretenure), String);
     DisallowHeapAllocation no_gc;
     CopyChars(result->GetChars(no_gc), string, length);
+    tainttracking::OnNewStringLiteral(*result);
+tainttracking::DebugCheckTaint(*result);
     return result;
   }
 }
@@ -841,6 +856,9 @@ Handle<SeqOneByteString> Factory::AllocateRawOneByteInternalizedString(
   answer->set_length(length);
   answer->set_hash_field(hash_field);
   DCHECK_EQ(size, answer->Size());
+
+  tainttracking::InitTaintData(*answer);
+
   return answer;
 }
 
@@ -861,6 +879,7 @@ Handle<String> Factory::AllocateTwoByteInternalizedString(
   // Fill in the characters.
   MemCopy(answer->GetChars(no_gc), str.start(), str.length() * kUC16Size);
 
+  tainttracking::InitTaintData(*answer);
   return answer;
 }
 
@@ -899,6 +918,9 @@ Handle<String> Factory::AllocateInternalizedStringImpl(T t, int chars,
     WriteTwoByteData(t, SeqTwoByteString::cast(*answer)->GetChars(no_gc),
                      chars);
   }
+
+  tainttracking::InitTaintData(SeqString::cast(*answer));
+
   return answer;
 }
 
@@ -910,6 +932,8 @@ Handle<String> Factory::NewInternalizedStringFromUtf8(Vector<const char> str,
         AllocateRawOneByteInternalizedString(str.length(), hash_field);
     DisallowHeapAllocation no_allocation;
     MemCopy(result->GetChars(no_allocation), str.start(), str.length());
+    tainttracking::OnNewStringLiteral(*result);
+tainttracking::DebugCheckTaint(*result);
     return result;
   }
   return AllocateInternalizedStringImpl<false>(str, chars, hash_field);
@@ -921,6 +945,8 @@ Handle<String> Factory::NewOneByteInternalizedString(Vector<const uint8_t> str,
       AllocateRawOneByteInternalizedString(str.length(), hash_field);
   DisallowHeapAllocation no_allocation;
   MemCopy(result->GetChars(no_allocation), str.start(), str.length());
+  tainttracking::OnNewStringLiteral(*result);
+tainttracking::DebugCheckTaint(*result);
   return result;
 }
 
@@ -932,21 +958,33 @@ Handle<String> Factory::NewOneByteInternalizedSubString(
   DisallowHeapAllocation no_allocation;
   MemCopy(result->GetChars(no_allocation),
           string->GetChars(no_allocation) + offset, length);
+  tainttracking::OnNewSubStringCopy(
+    *string, SeqOneByteString::cast(*result), offset, length);
+tainttracking::DebugCheckTaint(*result);
   return result;
 }
 
 Handle<String> Factory::NewTwoByteInternalizedString(Vector<const uc16> str,
                                                      uint32_t hash_field) {
-  return AllocateTwoByteInternalizedString(str, hash_field);
+  Handle<String> answer = AllocateTwoByteInternalizedString(str, hash_field);
+  tainttracking::OnNewStringLiteral(*answer);
+tainttracking::DebugCheckTaint(*answer);
+  return answer;
 }
 
 Handle<String> Factory::NewInternalizedStringImpl(Handle<String> string,
                                                   int chars,
                                                   uint32_t hash_field) {
+  Handle<String> answer;
   if (IsOneByte(string)) {
-    return AllocateInternalizedStringImpl<true>(string, chars, hash_field);
+    answer = AllocateInternalizedStringImpl<true>(string, chars, hash_field);
+  } else {
+    answer = AllocateInternalizedStringImpl<false>(string, chars, hash_field);
   }
-  return AllocateInternalizedStringImpl<false>(string, chars, hash_field);
+  tainttracking::OnNewSubStringCopy(
+    *string, SeqString::cast(*answer), 0, chars);
+tainttracking::DebugCheckTaint(*answer);
+  return answer;
 }
 
 namespace {
@@ -1017,6 +1055,9 @@ MaybeHandle<SeqOneByteString> Factory::NewRawOneByteString(
   string->set_length(length);
   string->set_hash_field(String::kEmptyHashField);
   DCHECK_EQ(size, string->Size());
+
+  tainttracking::InitTaintData(*string);
+tainttracking::DebugCheckTaint(*string);
   return string;
 }
 
@@ -1035,6 +1076,12 @@ MaybeHandle<SeqTwoByteString> Factory::NewRawTwoByteString(
   string->set_length(length);
   string->set_hash_field(String::kEmptyHashField);
   DCHECK_EQ(size, string->Size());
+DisallowHeapAllocation no_gc;
+uint8_t* dest = (uint8_t*)string->GetChars(no_gc);
+memset(dest, 0, 3*length); //XXXstroucki init for test
+
+  tainttracking::InitTaintData(*string);
+tainttracking::DebugCheckTaint(*string);
   return string;
 }
 
@@ -1058,6 +1105,8 @@ Handle<String> Factory::LookupSingleCharacterStringFromCode(uint32_t code) {
 
   Handle<SeqTwoByteString> result = NewRawTwoByteString(1).ToHandleChecked();
   result->SeqTwoByteStringSet(0, static_cast<uint16_t>(code));
+  tainttracking::OnNewStringLiteral(*result);
+tainttracking::DebugCheckTaint(*result);
   return result;
 }
 
@@ -1072,7 +1121,8 @@ static inline Handle<String> MakeOrFindTwoCharacterString(Isolate* isolate,
                                                           uint16_t c2) {
   // Numeric strings have a different hash algorithm not known by
   // LookupTwoCharsStringIfExists, so we skip this step for such strings.
-  if (!Between(c1, '0', '9') || !Between(c2, '0', '9')) {
+  if ((!Between(c1, '0', '9') || !Between(c2, '0', '9')) &&
+    tainttracking::kInternalizedStringsEnabled)  {
     Handle<String> result;
     if (StringTable::LookupTwoCharsStringIfExists(isolate, c1, c2)
             .ToHandle(&result)) {
@@ -1082,7 +1132,8 @@ static inline Handle<String> MakeOrFindTwoCharacterString(Isolate* isolate,
 
   // Now we know the length is 2, we might as well make use of that fact
   // when building the new string.
-  if (static_cast<unsigned>(c1 | c2) <= String::kMaxOneByteCharCodeU) {
+  if (static_cast<unsigned>(c1 | c2) <= String::kMaxOneByteCharCodeU &&
+    tainttracking::kInternalizedStringsEnabled) {
     // We can do this.
     DCHECK(base::bits::IsPowerOfTwo(String::kMaxOneByteCharCodeU +
                                     1));  // because of this.
@@ -1092,6 +1143,8 @@ static inline Handle<String> MakeOrFindTwoCharacterString(Isolate* isolate,
     uint8_t* dest = str->GetChars(no_allocation);
     dest[0] = static_cast<uint8_t>(c1);
     dest[1] = static_cast<uint8_t>(c2);
+    tainttracking::OnNewStringLiteral(*str);
+tainttracking::DebugCheckTaint(*str);
     return str;
   } else {
     Handle<SeqTwoByteString> str =
@@ -1100,6 +1153,8 @@ static inline Handle<String> MakeOrFindTwoCharacterString(Isolate* isolate,
     uc16* dest = str->GetChars(no_allocation);
     dest[0] = c1;
     dest[1] = c2;
+    tainttracking::OnNewStringLiteral(*str);
+tainttracking::DebugCheckTaint(*str);
     return str;
   }
 }
@@ -1112,6 +1167,8 @@ Handle<String> ConcatStringContent(Handle<StringType> result,
   SinkChar* sink = result->GetChars(pointer_stays_valid);
   String::WriteToFlat(*first, sink, 0, first->length());
   String::WriteToFlat(*second, sink + first->length(), 0, second->length());
+  tainttracking::OnNewConcatStringCopy(*result, *first, *second);
+tainttracking::DebugCheckTaint(*result);
   return result;
 }
 
@@ -1130,7 +1187,7 @@ MaybeHandle<String> Factory::NewConsString(Handle<String> left,
 
   int length = left_length + right_length;
 
-  if (length == 2) {
+  if (length == 2 && tainttracking::kInternalizedStringsEnabled) {
     uint16_t c1 = left->Get(0);
     uint16_t c2 = right->Get(0);
     return MakeOrFindTwoCharacterString(isolate(), c1, c2);
@@ -1181,6 +1238,10 @@ MaybeHandle<String> Factory::NewConsString(Handle<String> left,
                 ? Handle<ExternalOneByteString>::cast(right)->GetChars()
                 : Handle<SeqOneByteString>::cast(right)->GetChars(no_gc);
       for (int i = 0; i < right_length; i++) *dest++ = src[i];
+      tainttracking::OnNewConcatStringCopy(*result, *left, *right);
+tainttracking::DebugCheckTaint(*left);
+tainttracking::DebugCheckTaint(*right);
+//tainttracking::DebugCheckTaint(*result);
       return result;
     }
 
@@ -1192,6 +1253,7 @@ MaybeHandle<String> Factory::NewConsString(Handle<String> left,
                      right);
   }
 
+  // TODO: log symbolic
   bool one_byte = (is_one_byte || is_one_byte_data_in_two_byte_string);
   return NewConsString(left, right, length, one_byte);
 }
@@ -1215,6 +1277,8 @@ Handle<String> Factory::NewConsString(Handle<String> left, Handle<String> right,
   result->set_length(length);
   result->set_first(isolate(), *left, mode);
   result->set_second(isolate(), *right, mode);
+  tainttracking::OnNewConsString(*result, *left, *right);
+//tainttracking::DebugCheckTaint(*result);
   return result;
 }
 
@@ -1244,10 +1308,10 @@ Handle<String> Factory::NewProperSubString(Handle<String> str, int begin,
 
   int length = end - begin;
   if (length <= 0) return empty_string();
-  if (length == 1) {
+  if (length == 1 && tainttracking::kInternalizedStringsEnabled) {
     return LookupSingleCharacterStringFromCode(str->Get(begin));
   }
-  if (length == 2) {
+  if (length == 2 && tainttracking::kInternalizedStringsEnabled) {
     // Optimization for 2-byte strings often used as keys in a decompression
     // dictionary.  Check whether we already have the string in the string
     // table to prevent creation of many unnecessary strings.
@@ -1263,6 +1327,8 @@ Handle<String> Factory::NewProperSubString(Handle<String> str, int begin,
       DisallowHeapAllocation no_gc;
       uint8_t* dest = result->GetChars(no_gc);
       String::WriteToFlat(*str, dest, begin, end);
+      tainttracking::OnNewSubStringCopy(*str, *result, begin, length);
+tainttracking::DebugCheckTaint(*result);
       return result;
     } else {
       Handle<SeqTwoByteString> result =
@@ -1270,6 +1336,8 @@ Handle<String> Factory::NewProperSubString(Handle<String> str, int begin,
       DisallowHeapAllocation no_gc;
       uc16* dest = result->GetChars(no_gc);
       String::WriteToFlat(*str, dest, begin, end);
+      tainttracking::OnNewSubStringCopy(*str, *result, begin, length);
+tainttracking::DebugCheckTaint(*result);
       return result;
     }
   }
@@ -1297,6 +1365,11 @@ Handle<String> Factory::NewProperSubString(Handle<String> str, int begin,
   slice->set_length(length);
   slice->set_parent(isolate(), *str);
   slice->set_offset(offset);
+  {
+    DisallowHeapAllocation no_gc;
+    tainttracking::OnNewSlicedString(*slice, *str, offset, length);
+//tainttracking::DebugCheckTaint(*slice);
+  }
   return slice;
 }
 
@@ -1321,6 +1394,10 @@ MaybeHandle<String> Factory::NewExternalStringFromOneByte(
   external_string->SetResource(isolate(), resource);
   isolate()->heap()->RegisterExternalString(*external_string);
 
+  DisallowHeapAllocation no_gc;
+  tainttracking::OnNewExternalString(*external_string);
+tainttracking::DebugCheckTaint(*external_string);
+
   return external_string;
 }
 
@@ -1332,6 +1409,7 @@ MaybeHandle<String> Factory::NewExternalStringFromTwoByte(
   }
   if (length == 0) return empty_string();
 
+// XXXstroucki Future commit 683cf from 2019-03-04 removed this
   // For small strings we check whether the resource contains only
   // one byte characters.  If yes, we use a different string map.
   static const size_t kOneByteCheckLengthLimit = 32;
@@ -1346,6 +1424,16 @@ MaybeHandle<String> Factory::NewExternalStringFromTwoByte(
     map = is_one_byte ? external_string_with_one_byte_data_map()
                       : external_string_map();
   }
+// XXXstroucki replacing with
+/*
+  Handle<Map> map;
+  if (!resource->IsCacheable()) {
+    map = uncached_external_string_map();
+  } else {
+    map = external_string_map();
+  }
+*/
+
   Handle<ExternalTwoByteString> external_string(
       ExternalTwoByteString::cast(New(map, TENURED)), isolate());
   external_string->set_length(static_cast<int>(length));
@@ -1353,6 +1441,10 @@ MaybeHandle<String> Factory::NewExternalStringFromTwoByte(
   external_string->SetResource(isolate(), resource);
   isolate()->heap()->RegisterExternalString(*external_string);
 
+  DisallowHeapAllocation no_gc;
+  tainttracking::OnNewExternalString(*external_string);
+tainttracking::DebugCheckTaint(*external_string);
+
   return external_string;
 }
 
@@ -1369,6 +1461,10 @@ Handle<ExternalOneByteString> Factory::NewNativeSourceString(
   external_string->SetResource(isolate(), resource);
   isolate()->heap()->RegisterExternalString(*external_string);
 
+  DisallowHeapAllocation no_gc;
+  tainttracking::OnNewExternalString(*external_string);
+tainttracking::DebugCheckTaint(*external_string);
+
   return external_string;
 }
 
@@ -1376,6 +1472,8 @@ Handle<JSStringIterator> Factory::NewJSStringIterator(Handle<String> string) {
   Handle<Map> map(isolate()->native_context()->initial_string_iterator_map(),
                   isolate());
   Handle<String> flat_string = String::Flatten(isolate(), string);
+tainttracking::DebugCheckTaint(*string);
+tainttracking::DebugCheckTaint(*flat_string);
   Handle<JSStringIterator> iterator =
       Handle<JSStringIterator>::cast(NewJSObjectFromMap(map));
   iterator->set_string(*flat_string);
 
```

### Runtime

The `Runtime` relates to the runtime JS properties that the Javascript functions can define. 
> NOTE: I do not understand this part well enought yet. Leaving out for future update

```
 src/runtime/runtime-internal.cc                    |  241 ++
 src/runtime/runtime-regexp.cc                      |   25 +
 src/runtime/runtime-scopes.cc                      |   56 +-
 src/runtime/runtime-strings.cc                     |   74 +-
```
