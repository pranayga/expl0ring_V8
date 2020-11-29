# Tweaking / Adding a new Object type to V8
> Documentation is a love letter that you write to your future self. - Damian Conway

Commit SHA: 600241ea64
```
 src/frames.cc                                      |   41 +
 src/frames.h                                       |   19 +
 src/globals.h                                      |   56 +
 src/heap/factory.cc                                |  137 +-
 src/heap/factory.h                                 |    1 +
 src/heap/heap-inl.h                                |    1 +
 src/heap/heap.cc                                   |    2 +-
 ...
 src/objects.cc                                     |  131 +-
 src/objects.h                                      |    9 +-
 ...
 src/objects/string.h                               |   22 +-
```
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
 
diff --git a/src/globals.h b/src/globals.h
index 49f5d83dd5..6459076c4f 100644
--- a/src/globals.h
+++ b/src/globals.h
@@ -20,6 +20,62 @@
 
 #define V8_INFINITY std::numeric_limits<double>::infinity()
 
+namespace tainttracking {
+enum FrameType {
+  // From the JIT compiler called from JavaScript
+  JS,
+  JS_CALL_NEW,
+  JS_CALL_RUNTIME,
+
+  // Special case for the native context
+  TOP_LEVEL,
+
+  // Special case for property getters/setters
+  SETTER_ACCESSOR,
+  GETTER_ACCESSOR,
+
+  // Special case for implicit calls to toString in the left or right hand of a
+  // plus operation.
+  TO_STRING_CONVERT_PLUS_LEFT,
+  TO_STRING_CONVERT_PLUS_RIGHT,
+
+  // Special case for Runtime::Call
+  RUNTIME_CALL,
+
+  // Special cases of builtins in builtins-x64.cc
+  BUILTIN_CALL_OR_APPLY,
+
+  BUILTIN_REFLECT_APPLY,
+  BUILTIN_REFLECT_CONSTRUCT,
+  BUILTIN_APPLY,
+  BUILTIN_CALL,
+  BUILTIN_CONSTRUCT,
+  BUILTIN_CALL_FUNCTION,
+  BUILTIN_CALL_BOUND_FUNCTION,
+  BUILTIN_CONSTRUCT_FUNCTION,
+  BUILTIN_FUNCTION_PROTOTYPE_CALL,
+  BUILTIN_FUNCTION_PROTOTYPE_APPLY,
+  BUILTIN_JS_TRAMPOLINE,
+  BUILTIN_INVOKE_FUNCTION_CODE,
+
+  // The following types need literal arguments
+
+  // A call to Execution::Call that is not instrumented
+  UNKNOWN_CAPI,
+
+  // Execution::New
+  UNKNOWN_CAPI_NEW,
+
+  // A call to Builtins::InvokeApiFunction
+  UNKNOWN_EXTERNAL,
+
+
+  FIRST_NEEDS_LITERAL = UNKNOWN_CAPI,
+  FIRST_NEEDS_AUTO_EXIT = BUILTIN_REFLECT_APPLY,
+  LAST_NEEDS_AUTO_EXIT = BUILTIN_INVOKE_FUNCTION_CODE,
+};
+}
+
 namespace v8 {
 
 namespace base {
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
@@ -1526,6 +1624,14 @@ Handle<Context> Factory::NewCatchContext(Handle<Context> previous,
   STATIC_ASSERT(Context::MIN_CONTEXT_SLOTS == Context::THROWN_OBJECT_INDEX);
   // TODO(ishell): Take the details from CatchContext class.
   int variadic_part_length = Context::MIN_CONTEXT_SLOTS + 1;
+
+  bool is_rewrite_enabled =
+    tainttracking::TaintTracker::FromIsolate(isolate())->
+    IsRewriteAstEnabled();
+  if (is_rewrite_enabled) {
+    variadic_part_length++;
+  }
+
   Handle<Context> context = NewContext(RootIndex::kCatchContextMap,
                                        Context::SizeFor(variadic_part_length),
                                        variadic_part_length, NOT_TENURED);
@@ -1534,6 +1640,12 @@ Handle<Context> Factory::NewCatchContext(Handle<Context> previous,
   context->set_extension(*the_hole_value());
   context->set_native_context(previous->native_context());
   context->set(Context::THROWN_OBJECT_INDEX, *thrown_object);
+
+  if (is_rewrite_enabled) {
+    // Extra slot for the symbolic argument of the thrown exception
+    tainttracking::RuntimeOnCatch(isolate(), thrown_object, context);
+  }
+
   return context;
 }
 
@@ -3690,6 +3802,9 @@ Handle<SharedFunctionInfo> Factory::NewSharedFunctionInfo(
 #ifdef VERIFY_HEAP
   share->SharedFunctionInfoVerify(isolate());
 #endif
+
+  share->set_taint_node_label(*undefined_value(), SKIP_WRITE_BARRIER);
+
   return share;
 }
 
diff --git a/src/heap/factory.h b/src/heap/factory.h
index abdca3807a..e4b5af3f72 100644
--- a/src/heap/factory.h
+++ b/src/heap/factory.h
@@ -328,6 +328,7 @@ class V8_EXPORT_PRIVATE Factory {
   // Allocates and partially initializes an one-byte or two-byte String. The
   // characters of the string are uninitialized. Currently used in regexp code
   // only, where they are pretenured.
+  // XXXstroucki also in json parser
   V8_WARN_UNUSED_RESULT MaybeHandle<SeqOneByteString> NewRawOneByteString(
       int length, PretenureFlag pretenure = NOT_TENURED);
   V8_WARN_UNUSED_RESULT MaybeHandle<SeqTwoByteString> NewRawTwoByteString(
diff --git a/src/heap/heap-inl.h b/src/heap/heap-inl.h
index c5f441d8fe..c92cbc7f12 100644
--- a/src/heap/heap-inl.h
+++ b/src/heap/heap-inl.h
@@ -83,6 +83,7 @@ void Heap::account_external_memory_concurrently_freed() {
   external_memory_concurrently_freed_ = 0;
 }
 
+// XXXstroucki interning strings
 RootsTable& Heap::roots_table() { return isolate()->roots_table(); }
 
 #define ROOT_ACCESSOR(Type, name, CamelName)                           \
diff --git a/src/heap/heap.cc b/src/heap/heap.cc
index ed1bb91037..80f56844ef 100644
--- a/src/heap/heap.cc
+++ b/src/heap/heap.cc
@@ -856,7 +856,6 @@ void Heap::GarbageCollectionEpilogue() {
   if (Heap::ShouldZapGarbage() || FLAG_clear_free_memory) {
     ZapFromSpace();
   }
-
 #ifdef VERIFY_HEAP
   if (FLAG_verify_heap) {
     Verify();
@@ -4277,6 +4276,7 @@ HeapObject Heap::EnsureImmovableCode(HeapObject heap_object, int object_size) {
 HeapObject Heap::AllocateRawWithLightRetry(int size, AllocationSpace space,
                                            AllocationAlignment alignment) {
   HeapObject result;
+// XXXstroucki this causes extra space to get allocated
   AllocationResult alloc = AllocateRaw(size, space, alignment);
   if (alloc.To(&result)) {
     DCHECK(result != ReadOnlyRoots(this).exception());
```

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
@@ -1736,6 +1737,9 @@ Maybe<bool> Object::SetPropertyWithAccessor(LookupIterator* it,
           Nothing<bool>());
     }
 
+    tainttracking::RuntimePrepareSymbolicStackFrame(
+        isolate, tainttracking::FrameType::SETTER_ACCESSOR);
+
     // The actual type of setter callback is either
     // v8::AccessorNameSetterCallback or
     // i::Accesors::AccessorNameBooleanSetterCallback, depending on whether the
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
@@ -1799,7 +1813,7 @@ MaybeHandle<Object> Object::GetPropertyWithDefinedGetter(
     return MaybeHandle<Object>();
   }
 
-  return Execution::Call(isolate, getter, receiver, 0, nullptr);
+  return Execution::Call(isolate, getter, receiver, 0, nullptr, tainttracking::FrameType::GETTER_ACCESSOR);
 }
 
 
@@ -1811,7 +1825,8 @@ Maybe<bool> Object::SetPropertyWithDefinedSetter(Handle<Object> receiver,
 
   Handle<Object> argv[] = { value };
   RETURN_ON_EXCEPTION_VALUE(isolate, Execution::Call(isolate, setter, receiver,
-                                                     arraysize(argv), argv),
+                                                     arraysize(argv), argv,
+                                                     tainttracking::FrameType::SETTER_ACCESSOR),
                             Nothing<bool>());
   return Just(true);
 }
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
@@ -2791,6 +2813,7 @@ bool String::MakeExternal(v8::String::ExternalOneByteStringResource* resource) {
 
   // Byte size of the external String object.
   int new_size = this->SizeFromMap(new_map);
+  DCHECK_GE(size, new_size);
   heap->CreateFillerObjectAt(this->address() + new_size, size - new_size,
                              ClearRecordedSlots::kNo);
   if (has_pointers) {
@@ -8873,7 +8896,8 @@ Handle<Object> JSObject::FastPropertyAt(Handle<JSObject> object,
 
 // static
 MaybeHandle<Object> JSReceiver::ToPrimitive(Handle<JSReceiver> receiver,
-                                            ToPrimitiveHint hint) {
+                                            ToPrimitiveHint hint,
+                                            tainttracking::FrameType frame_type) {
   Isolate* const isolate = receiver->GetIsolate();
   Handle<Object> exotic_to_prim;
   ASSIGN_RETURN_ON_EXCEPTION(
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
@@ -12463,6 +12490,7 @@ Handle<String> SeqString::Truncate(Handle<SeqString> string, int new_length) {
   int old_length = string->length();
   if (old_length <= new_length) return string;
 
+  bool one_byte = string->IsSeqOneByteString();
   if (string->IsSeqOneByteString()) {
     old_size = SeqOneByteString::SizeFor(old_length);
     new_size = SeqOneByteString::SizeFor(new_length);
@@ -12472,6 +12500,19 @@ Handle<String> SeqString::Truncate(Handle<SeqString> string, int new_length) {
     new_size = SeqTwoByteString::SizeFor(new_length);
   }
 
+  // XXXstroucki what is the point of this? getting the taint then replacing it?
+  byte taint_data[new_length];
+  // initialize this to some invalid value to catch uninitialized use
+  {for (int foo=0; foo < new_length; foo++) taint_data[foo]=0xff;}
+
+  if (one_byte) {
+    DisallowHeapAllocation no_gc;
+    tainttracking::CopyOut(SeqOneByteString::cast(*string), taint_data, 0, new_length);
+  } else {
+    DisallowHeapAllocation no_gc;
+    tainttracking::CopyOut(SeqTwoByteString::cast(*string), taint_data, 0, new_length);
+  }
+
   int delta = old_size - new_size;
 
   Address start_of_string = string->address();
@@ -12487,6 +12528,14 @@ Handle<String> SeqString::Truncate(Handle<SeqString> string, int new_length) {
   // for the left-over space to avoid races with the sweeper thread.
   string->synchronized_set_length(new_length);
 
+  if (one_byte) {
+    DisallowHeapAllocation no_gc;
+    tainttracking::CopyIn(SeqOneByteString::cast(*string), taint_data, 0, new_length);
+  } else {
+    DisallowHeapAllocation no_gc;
+    tainttracking::CopyIn(SeqTwoByteString::cast(*string), taint_data, 0, new_length);
+  }
+
   return string;
 }
 
@@ -14479,6 +14528,13 @@ void SharedFunctionInfo::InitFromFunctionLiteral(
             lit->function_literal_id());
     shared_info->set_uncompiled_data(*data);
   }
+
+  std::unique_ptr<uint8_t[]> taint = nullptr;
+  tainttracking::V8NodeLabelSerializer ser;
+  if (ser.Serialize(taint, lit->GetTaintTrackingLabel())) {
+    Handle<String> obj = isolate->factory()->NewStringFromOneByte(VectorOf(taint.get(), sizeof(taint.get()))).ToHandleChecked();
+    shared_info->set_taint_node_label(*obj);
+  }
 }
 
 void SharedFunctionInfo::SetExpectedNofPropertiesFromEstimate(
@@ -14760,6 +14816,24 @@ int AbstractCode::SourcePosition(int offset) {
   return position;
 }
 
+int AbstractCode::SourcePositionWithTaintTrackingAstIndex(
+    int offset, int* out_ast_index) {
+  DCHECK_NOT_NULL(out_ast_index);
+  int position = 0;
+  int ast_index =
+    v8::internal::StackFrame::TaintStackFrameInfo::SOURCE_POS_DEFAULT;
+  // Subtract one because the current PC is one instruction after the call site.
+  if (IsCode()) offset--;
+  for (SourcePositionTableIterator iterator(source_position_table());
+       !iterator.done() && iterator.code_offset() <= offset;
+       iterator.Advance()) {
+    position = iterator.source_position().ScriptOffset();
+    ast_index = iterator.ast_taint_tracking_index();
+  }
+  *out_ast_index = ast_index;
+  return position;
+}
+
 int AbstractCode::SourceStatementPosition(int offset) {
   // First find the closest position.
   int position = SourcePosition(offset);
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
@@ -17402,6 +17487,8 @@ void MakeStringThin(String string, String internalized, Isolate* isolate) {
 
 }  // namespace
 
+static int exitpoint = 0;
+
 // static
 Handle<String> StringTable::LookupString(Isolate* isolate,
                                          Handle<String> string) {
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
 
@@ -17452,6 +17562,7 @@ Handle<String> StringTable::LookupKey(Isolate* isolate, StringTableKey* key) {
   table = StringTable::EnsureCapacity(isolate, table, 1);
   isolate->heap()->SetRootStringTable(*table);
 
+exitpoint=2;
   return AddKeyNoResize(isolate, key);
 }
 
diff --git a/src/objects.h b/src/objects.h
index 72d3511c6f..f1f24387a7 100644
--- a/src/objects.h
+++ b/src/objects.h
@@ -182,6 +182,12 @@
 //  Smi:        [31 bit signed int] 0
 //  HeapObject: [32 bit direct pointer] (4 byte aligned) | 01
 
+namespace tainttracking {
+  inline int SizeForTaint(int length) {
+    return length * v8::internal::kCharSize;
+  }
+};
+
 namespace v8 {
 namespace internal {
 
@@ -692,7 +698,8 @@ class Object {
 
   // ES6 section 7.1.1 ToPrimitive
   V8_WARN_UNUSED_RESULT static inline MaybeHandle<Object> ToPrimitive(
-      Handle<Object> input, ToPrimitiveHint hint = ToPrimitiveHint::kDefault);
+      Handle<Object> input, ToPrimitiveHint hint = ToPrimitiveHint::kDefault,
+      tainttracking::FrameType frame_type = tainttracking::FrameType::UNKNOWN_CAPI);
 
   // ES6 section 7.1.3 ToNumber
   V8_WARN_UNUSED_RESULT static inline MaybeHandle<Object> ToNumber(
diff --git a/src/objects/code.h b/src/objects/code.h
index 6239ef9a0b..5212b0877c 100644
```

```
 src/runtime/runtime-internal.cc                    |  241 ++
 src/runtime/runtime-regexp.cc                      |   25 +
 src/runtime/runtime-scopes.cc                      |   56 +-
 src/runtime/runtime-strings.cc                     |   74 +-
```

```diff
diff --git a/src/runtime/runtime-internal.cc b/src/runtime/runtime-internal.cc
index 60791aadc7..c6fcb7abb1 100644
--- a/src/runtime/runtime-internal.cc
+++ b/src/runtime/runtime-internal.cc
@@ -674,5 +674,246 @@ RUNTIME_FUNCTION(Runtime_GetInitializerFunction) {
   Handle<Object> initializer = JSReceiver::GetDataProperty(constructor, key);
   return *initializer;
 }
+
+RUNTIME_FUNCTION(Runtime_TaintTrackingHook) {
+  HandleScope scope(isolate);
+
+  DCHECK_EQ(3, args.length());
+  CONVERT_ARG_HANDLE_CHECKED(Object, target, 0);
+  CONVERT_ARG_HANDLE_CHECKED(Object, label, 1);
+  CONVERT_ARG_HANDLE_CHECKED(Smi, checktype, 2);
+
+  tainttracking::RuntimeHook(
+      isolate,
+      target,
+      label,
+      checktype->value());
+  return *target;
+}
+
+RUNTIME_FUNCTION(Runtime_TaintTrackingLoadVariable) {
+  HandleScope scope(isolate);
+
+  DCHECK_EQ(4, args.length());
+  CONVERT_ARG_HANDLE_CHECKED(Object, target, 0);
+  CONVERT_ARG_HANDLE_CHECKED(Object, proxy_label, 1);
+  CONVERT_ARG_HANDLE_CHECKED(Object, past_assignment_label_or_idx, 2);
+  CONVERT_ARG_HANDLE_CHECKED(Smi, checktype, 3);
+
+  tainttracking::RuntimeHookVariableLoad(
+      isolate,
+      target,
+      proxy_label,
+      past_assignment_label_or_idx,
+      checktype->value());
+  return *target;
+}
+
+RUNTIME_FUNCTION(Runtime_TaintTrackingStoreVariable) {
+  HandleScope scope(isolate);
+
+  DCHECK(args.length() == 3 || args.length() == 4);
+  CONVERT_ARG_HANDLE_CHECKED(Object, concrete, 0);
+  CONVERT_ARG_HANDLE_CHECKED(Object, label, 1);
+  CONVERT_ARG_HANDLE_CHECKED(Smi, checktype, 2);
+  Handle<Object> idx_or_holder (
+      Smi::FromInt(tainttracking::NO_VARIABLE_INDEX), isolate);
+  if (args.length() == 4) {
+    idx_or_holder = args.at<Object>(3);
+  }
+
+  Handle<Object> ret = tainttracking::RuntimeHookVariableStore(
+      isolate,
+      concrete,
+      label,
+      static_cast<tainttracking::CheckType>(checktype->value()),
+      idx_or_holder);
+  return *ret;
+}
+
+RUNTIME_FUNCTION(Runtime_TaintTrackingStoreContextVariable) {
+  HandleScope scope(isolate);
+
+  DCHECK_EQ(4, args.length());
+  CONVERT_ARG_HANDLE_CHECKED(Object, concrete, 0);
+  CONVERT_ARG_HANDLE_CHECKED(Object, label, 1);
+  CONVERT_ARG_HANDLE_CHECKED(Context, context, 2);
+  CONVERT_ARG_HANDLE_CHECKED(Smi, index, 3);
+
+  tainttracking::RuntimeHookVariableContextStore(
+      isolate, concrete, label, context, index);
+  return ReadOnlyRoots(isolate).undefined_value();
+}
+
+RUNTIME_FUNCTION(Runtime_TaintTrackingExitStackFrame) {
+  HandleScope scope(isolate);
+  DCHECK_EQ(0, args.length());
+  tainttracking::RuntimeExitSymbolicStackFrame(isolate);
+  return ReadOnlyRoots(isolate).undefined_value();
+}
+
+RUNTIME_FUNCTION(Runtime_TaintTrackingPrepareFrame) {
+  HandleScope scope(isolate);
+  DCHECK_EQ(1, args.length());
+  CONVERT_ARG_HANDLE_CHECKED(Smi, frame_type, 0);
+  tainttracking::RuntimePrepareSymbolicStackFrame(
+      isolate,
+      static_cast<tainttracking::FrameType>(frame_type->value()));
+  return ReadOnlyRoots(isolate).undefined_value();
+}
+
+RUNTIME_FUNCTION(Runtime_TaintTrackingEnterFrame) {
+  HandleScope scope(isolate);
+  DCHECK_EQ(0, args.length());
+  tainttracking::RuntimeEnterSymbolicStackFrame(isolate);
+  return ReadOnlyRoots(isolate).undefined_value();
+}
+
+RUNTIME_FUNCTION(Runtime_TaintTrackingAddArgumentToFrame) {
+  HandleScope scope(isolate);
+  DCHECK_EQ(1, args.length());
+  CONVERT_ARG_HANDLE_CHECKED(Object, label, 0);
+  tainttracking::RuntimeAddArgumentToStackFrame(isolate, label);
+  return ReadOnlyRoots(isolate).undefined_value();
+}
+
+
+RUNTIME_FUNCTION(Runtime_TaintTrackingSetReturnValue) {
+  HandleScope scope(isolate);
+  DCHECK(args.length() == 1 || args.length() == 2);
+  CONVERT_ARG_HANDLE_CHECKED(Object, value, 0);
+  MaybeHandle<Object> label;
+  if (args.length() > 1) {
+    label = args.at<Object>(1);
+  }
+  tainttracking::RuntimeSetReturnValue(isolate, value, label);
+  return ReadOnlyRoots(isolate).undefined_value();
+}
+
+RUNTIME_FUNCTION(Runtime_TaintTrackingEnterTry) {
+  HandleScope scope(isolate);
+  DCHECK_EQ(args.length(), 1);
+  CONVERT_ARG_HANDLE_CHECKED(Object, label, 0);
+  tainttracking::RuntimeEnterTry(isolate, label);
+  return ReadOnlyRoots(isolate).undefined_value();
+}
+
+RUNTIME_FUNCTION(Runtime_TaintTrackingExitTry) {
+  HandleScope scope(isolate);
+  DCHECK_EQ(args.length(), 1);
+  CONVERT_ARG_HANDLE_CHECKED(Object, label, 0);
+  tainttracking::RuntimeExitTry(isolate, label);
+  return ReadOnlyRoots(isolate).undefined_value();
+}
+
+RUNTIME_FUNCTION(Runtime_TaintTrackingExitFinally) {
+  HandleScope scope(isolate);
+  DCHECK_EQ(args.length(), 0);
+  tainttracking::RuntimeOnExitFinally(isolate);
+  return ReadOnlyRoots(isolate).undefined_value();
+}
+
+RUNTIME_FUNCTION(Runtime_TaintTrackingAddReceiver) {
+  HandleScope scope(isolate);
+  DCHECK_EQ(2, args.length());
+  CONVERT_ARG_HANDLE_CHECKED(Object, value, 0);
+  CONVERT_ARG_HANDLE_CHECKED(Object, label, 1);
+  tainttracking::RuntimeSetReceiver(isolate, value, label);
+  return ReadOnlyRoots(isolate).undefined_value();
+}
+
+RUNTIME_FUNCTION(Runtime_TaintTrackingAddLiteralReceiver) {
+  HandleScope scope(isolate);
+  DCHECK_EQ(1, args.length());
+  CONVERT_ARG_HANDLE_CHECKED(Object, value, 0);
+  tainttracking::RuntimeSetLiteralReceiver(isolate, value);
+  return ReadOnlyRoots(isolate).undefined_value();
+}
+
+
+
+RUNTIME_FUNCTION(Runtime_TaintTrackingPrepareApply) {
+  HandleScope scope(isolate);
+  DCHECK_EQ(5, args.length());
+
+  CONVERT_ARG_HANDLE_CHECKED(Object, arguments_list, 0);
+  CONVERT_ARG_HANDLE_CHECKED(Object, target_fn, 1);
+  CONVERT_ARG_HANDLE_CHECKED(Object, new_target, 2);
+  CONVERT_ARG_HANDLE_CHECKED(Object, this_argument, 3);
+  CONVERT_ARG_HANDLE_CHECKED(Smi, frame_type, 4);
+
+  return tainttracking::RuntimePrepareApplyFrame(
+      isolate,
+      arguments_list,
+      target_fn,
+      new_target,
+      this_argument,
+      static_cast<tainttracking::FrameType>(frame_type->value()));
+}
+
+RUNTIME_FUNCTION(Runtime_TaintTrackingPrepareCall) {
+  HandleScope scope(isolate);
+  static const int OTHER_ARGS = 2;
+
+  DCHECK_LE(OTHER_ARGS, args.length());
+
+  CONVERT_ARG_HANDLE_CHECKED(Object, target_fn, 0);
+  CONVERT_ARG_HANDLE_CHECKED(Smi, caller_frame_type, 1);
+
+  Handle<FixedArray> arg_list = isolate->factory()->NewFixedArray(
+      args.length() - OTHER_ARGS);
+  for (int i = OTHER_ARGS; i < args.length(); i++) {
+    arg_list->set(i - OTHER_ARGS, *(args.at<Object>(i)));
+  }
+
+  return tainttracking::RuntimePrepareCallFrame(
+      isolate,
+      target_fn,
+      static_cast<tainttracking::FrameType>(caller_frame_type->value()),
+      arg_list);
+}
+
+RUNTIME_FUNCTION(Runtime_TaintTrackingPrepareCallOrConstruct) {
+  HandleScope scope(isolate);
+
+  static const int OTHER_ARGS = 2;
+
+  DCHECK_LE(OTHER_ARGS, args.length());
+
+  CONVERT_ARG_HANDLE_CHECKED(Object, target_fn, 0);
+  CONVERT_ARG_HANDLE_CHECKED(Object, new_target, 1);
+
+  Handle<FixedArray> arg_list = isolate->factory()->NewFixedArray(
+      args.length() - OTHER_ARGS);
+  for (int i = OTHER_ARGS; i < args.length(); i++) {
+    arg_list->set(i - OTHER_ARGS, *(args.at<Object>(i)));
+  }
+
+  return tainttracking::RuntimePrepareCallOrConstructFrame(
+      isolate, target_fn, new_target, arg_list);
+}
+
+RUNTIME_FUNCTION(Runtime_TaintTrackingCheckMessageOrigin) {
+  HandleScope scope (isolate);
+  CONVERT_ARG_HANDLE_CHECKED(Object, left, 0);
+  CONVERT_ARG_HANDLE_CHECKED(Object, right, 1);
+  CONVERT_ARG_HANDLE_CHECKED(Smi, token, 2);
+
+  tainttracking::RuntimeCheckMessageOrigin(
+      isolate, left, right, static_cast<Token::Value>(token->value()));
+
+  return ReadOnlyRoots(isolate).undefined_value();
+}
+
+RUNTIME_FUNCTION(Runtime_TaintTrackingParameterToContextStorage) {
+  HandleScope scope (isolate);
+  CONVERT_ARG_HANDLE_CHECKED(Smi, param_index, 0);
+  CONVERT_ARG_HANDLE_CHECKED(Smi, ctx_slot_index, 1);
+  CONVERT_ARG_HANDLE_CHECKED(Context, context, 2);
+
+  tainttracking::RuntimeParameterToContextStorage(
+      isolate, param_index->value(), ctx_slot_index->value(), context);
+  return ReadOnlyRoots(isolate).undefined_value();
+}
 }  // namespace internal
 }  // namespace v8
diff --git a/src/runtime/runtime-regexp.cc b/src/runtime/runtime-regexp.cc
index f472da7478..66314c8209 100644
--- a/src/runtime/runtime-regexp.cc
+++ b/src/runtime/runtime-regexp.cc
@@ -569,12 +569,17 @@ V8_WARN_UNUSED_RESULT static Object StringReplaceGlobalAtomRegExpWithString(
   ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, untyped_res, maybe_res);
   Handle<ResultSeqString> result = Handle<ResultSeqString>::cast(untyped_res);
 
+  // TODO: log symbolic
   DisallowHeapAllocation no_gc;
+  tainttracking::TaintData* data =
+    tainttracking::GetWriteableStringTaintData(*result);
   for (int index : *indices) {
     // Copy non-matched subject content.
     if (subject_pos < index) {
       String::WriteToFlat(*subject, result->GetChars(no_gc) + result_pos,
                           subject_pos, index);
+      tainttracking::FlattenTaintData(*subject, data + result_pos,
+        subject_pos, index - subject_pos);
       result_pos += index - subject_pos;
     }
 
@@ -582,6 +587,8 @@ V8_WARN_UNUSED_RESULT static Object StringReplaceGlobalAtomRegExpWithString(
     if (replacement_len > 0) {
       String::WriteToFlat(*replacement, result->GetChars(no_gc) + result_pos, 0,
                           replacement_len);
+      tainttracking::FlattenTaintData(*replacement, data + result_pos,
+        0, replacement_len);
       result_pos += replacement_len;
     }
 
@@ -591,12 +598,17 @@ V8_WARN_UNUSED_RESULT static Object StringReplaceGlobalAtomRegExpWithString(
   if (subject_pos < subject_len) {
     String::WriteToFlat(*subject, result->GetChars(no_gc) + result_pos,
                         subject_pos, subject_len);
+    tainttracking::FlattenTaintData(*subject, data + result_pos,
+      subject_pos, subject_len - subject_pos);
   }
 
   int32_t match_indices[] = {indices->back(), indices->back() + pattern_len};
   RegExpImpl::SetLastMatchInfo(isolate, last_match_info, subject, 0,
                                match_indices);
 
+  tainttracking::OnNewReplaceRegexpWithString(*subject, *result,
+    *pattern_regexp, *replacement);
+
   TruncateRegexpIndicesList(isolate);
 
   return *result;
@@ -739,6 +751,8 @@ V8_WARN_UNUSED_RESULT static Object StringReplaceGlobalRegExpWithEmptyString(
   int prev = 0;
   int position = 0;
 
+  tainttracking::TaintData taint_data[new_length];
+
   DisallowHeapAllocation no_gc;
   do {
     start = current_match[0];
@@ -747,6 +761,8 @@ V8_WARN_UNUSED_RESULT static Object StringReplaceGlobalRegExpWithEmptyString(
       // Add substring subject[prev;start] to answer string.
       String::WriteToFlat(*subject, answer->GetChars(no_gc) + position, prev,
                           start);
+      tainttracking::FlattenTaintData(*subject, &taint_data[position], prev,
+        start - prev);
       position += start - prev;
     }
     prev = end;
@@ -763,6 +779,8 @@ V8_WARN_UNUSED_RESULT static Object StringReplaceGlobalRegExpWithEmptyString(
     // Add substring subject[prev;length] to answer string.
     String::WriteToFlat(*subject, answer->GetChars(no_gc) + position, prev,
                         subject_length);
+      tainttracking::FlattenTaintData(*subject, &taint_data[position], prev,
+        subject_length - prev);
     position += subject_length - prev;
   }
 
@@ -774,6 +792,10 @@ V8_WARN_UNUSED_RESULT static Object StringReplaceGlobalRegExpWithEmptyString(
   int delta = allocated_string_size - string_size;
 
   answer->set_length(position);
+
+  tainttracking::CopyIn(*Handle<ResultSeqString>::cast(answer), taint_data,
+    0, position);
+
   if (delta == 0) return *answer;
 
   Address end_of_string = answer->address() + string_size;
@@ -788,6 +810,9 @@ V8_WARN_UNUSED_RESULT static Object StringReplaceGlobalRegExpWithEmptyString(
   if (!heap->IsLargeObject(*answer)) {
     heap->CreateFillerObjectAt(end_of_string, delta, ClearRecordedSlots::kNo);
   }
+
+  tainttracking::OnNewReplaceRegexpWithString(*subject, *answer, *regexp,
+    *isolate->factory()->empty_string());
   return *answer;
 }
 
diff --git a/src/runtime/runtime-scopes.cc b/src/runtime/runtime-scopes.cc
index d29722fb62..a61fb73e66 100644
--- a/src/runtime/runtime-scopes.cc
+++ b/src/runtime/runtime-scopes.cc
@@ -903,9 +903,11 @@ RUNTIME_FUNCTION_RETURN_PAIR(Runtime_LoadLookupSlotForCall) {
 
 
 namespace {
+static const int UNINITIALIZED = -2;
 
 MaybeHandle<Object> StoreLookupSlot(
     Isolate* isolate, Handle<String> name, Handle<Object> value,
+    MaybeHandle<Object> label,
     LanguageMode language_mode,
     ContextLookupFlags context_lookup_flags = FOLLOW_CHAINS) {
   Handle<Context> context(isolate->context(), isolate);
@@ -915,9 +917,18 @@ MaybeHandle<Object> StoreLookupSlot(
   InitializationFlag flag;
   VariableMode mode;
   bool is_sloppy_function_name;
-  Handle<Object> holder =
+  int symbolic_index = UNINITIALIZED;
+  Handle<Object> holder;
+  if (!label.is_null()) {
+    holder =
+      context->Lookup(name, context_lookup_flags, &index, &attributes, &flag,
+                      &mode, &is_sloppy_function_name, &symbolic_index);
+  } else {
+    holder = 
       context->Lookup(name, context_lookup_flags, &index, &attributes, &flag,
                       &mode, &is_sloppy_function_name);
+  }
+
   if (holder.is_null()) {
     // In case of JSProxy, an exception might have been thrown.
     if (isolate->has_pending_exception()) return MaybeHandle<Object>();
@@ -939,7 +950,14 @@ MaybeHandle<Object> StoreLookupSlot(
                       Object);
     }
     if ((attributes & READ_ONLY) == 0) {
-      Handle<Context>::cast(holder)->set(index, *value);
+      Handle<Context> holder_as_ctx = Handle<Context>::cast(holder);
+      holder_as_ctx->set(index, *value);
+      Handle<Object> next_label;
+      if (label.ToHandle(&next_label)) {
+        if (symbolic_index != UNINITIALIZED) {
+          holder_as_ctx->set(symbolic_index, *next_label);
+        }
+      }
     } else if (!is_sloppy_function_name || is_strict(language_mode)) {
       THROW_NEW_ERROR(
           isolate, NewTypeError(MessageTemplate::kConstAssign, name), Object);
@@ -974,11 +992,20 @@ MaybeHandle<Object> StoreLookupSlot(
 
 RUNTIME_FUNCTION(Runtime_StoreLookupSlot_Sloppy) {
   HandleScope scope(isolate);
-  DCHECK_EQ(2, args.length());
+  DCHECK(2 == args.length() || 3 == args.length());
   CONVERT_ARG_HANDLE_CHECKED(String, name, 0);
   CONVERT_ARG_HANDLE_CHECKED(Object, value, 1);
-  RETURN_RESULT_OR_FAILURE(
-      isolate, StoreLookupSlot(isolate, name, value, LanguageMode::kSloppy));
+
+  if (args.length() == 3) {
+    CONVERT_ARG_HANDLE_CHECKED(Object, label, 2);
+    RETURN_RESULT_OR_FAILURE(
+      isolate, StoreLookupSlot(isolate, name, value, label,
+      LanguageMode::kSloppy));
+  } else {
+    RETURN_RESULT_OR_FAILURE(
+      isolate, StoreLookupSlot(isolate, name, value, MaybeHandle<Object>(),
+      LanguageMode::kSloppy));
+  }
 }
 
 // Store into a dynamic context for sloppy-mode block-scoped function hoisting
@@ -992,17 +1019,26 @@ RUNTIME_FUNCTION(Runtime_StoreLookupSlot_SloppyHoisting) {
   const ContextLookupFlags lookup_flags = static_cast<ContextLookupFlags>(
       FOLLOW_CONTEXT_CHAIN | STOP_AT_DECLARATION_SCOPE | SKIP_WITH_CONTEXT);
   RETURN_RESULT_OR_FAILURE(
-      isolate, StoreLookupSlot(isolate, name, value, LanguageMode::kSloppy,
-                               lookup_flags));
+      isolate, StoreLookupSlot(isolate, name, value, MaybeHandle<Object>(),
+      LanguageMode::kSloppy, lookup_flags));
 }
 
 RUNTIME_FUNCTION(Runtime_StoreLookupSlot_Strict) {
   HandleScope scope(isolate);
-  DCHECK_EQ(2, args.length());
+  DCHECK(2 == args.length() || 3 == args.length());
   CONVERT_ARG_HANDLE_CHECKED(String, name, 0);
   CONVERT_ARG_HANDLE_CHECKED(Object, value, 1);
-  RETURN_RESULT_OR_FAILURE(
-      isolate, StoreLookupSlot(isolate, name, value, LanguageMode::kStrict));
+
+  if (args.length() == 3) {
+    CONVERT_ARG_HANDLE_CHECKED(Object, label, 2);
+    RETURN_RESULT_OR_FAILURE(
+      isolate, StoreLookupSlot(isolate, name, value, label,
+      LanguageMode::kStrict));
+  } else {
+    RETURN_RESULT_OR_FAILURE(
+      isolate, StoreLookupSlot(isolate, name, value, MaybeHandle<Object>(),
+      LanguageMode::kStrict));
+  }
 }
 
 }  // namespace internal
diff --git a/src/runtime/runtime-strings.cc b/src/runtime/runtime-strings.cc
index ec527b7db8..049a656530 100644
--- a/src/runtime/runtime-strings.cc
+++ b/src/runtime/runtime-strings.cc
@@ -14,6 +14,7 @@
 #include "src/runtime/runtime-utils.h"
 #include "src/string-builder-inl.h"
 #include "src/string-search.h"
+#include "src/taint_tracking.h"
 
 namespace v8 {
 namespace internal {
@@ -330,7 +331,9 @@ RUNTIME_FUNCTION(Runtime_StringBuilderConcat) {
     DisallowHeapAllocation no_gc;
     StringBuilderConcatHelper(*special, answer->GetChars(no_gc),
                               FixedArray::cast(array->elements()),
-                              array_length);
+                              array_length,
+      tainttracking::GetWriteableStringTaintData(*answer));
+    tainttracking::OnJoinManyStrings(*answer, *array);
     return *answer;
   } else {
     Handle<SeqTwoByteString> answer;
@@ -339,7 +342,9 @@ RUNTIME_FUNCTION(Runtime_StringBuilderConcat) {
     DisallowHeapAllocation no_gc;
     StringBuilderConcatHelper(*special, answer->GetChars(no_gc),
                               FixedArray::cast(array->elements()),
-                              array_length);
+                              array_length,
+      tainttracking::GetWriteableStringTaintData(*answer));
+    tainttracking::OnJoinManyStrings(*answer, *array);
     return *answer;
   }
 }
@@ -398,6 +403,11 @@ RUNTIME_FUNCTION(Runtime_StringBuilderJoin) {
   DisallowHeapAllocation no_gc;
 
   uc16* sink = answer->GetChars(no_gc);
+
+  tainttracking::TaintData* taint_sink =
+    tainttracking::GetWriteableStringTaintData(*answer);
+  // TODO: log symbolic
+
 #ifdef DEBUG
   uc16* end = sink + length;
 #endif
@@ -408,70 +418,92 @@ RUNTIME_FUNCTION(Runtime_StringBuilderJoin) {
 
   int first_length = first->length();
   String::WriteToFlat(first, sink, 0, first_length);
+  tainttracking::FlattenTaintData(first, taint_sink, 0, first_length);
   sink += first_length;
+  taint_sink += first_length;
 
   for (int i = 1; i < array_length; i++) {
     DCHECK(sink + separator_length <= end);
     String::WriteToFlat(separator_raw, sink, 0, separator_length);
+    tainttracking::FlattenTaintData(separator_raw, taint_sink, 0, separator_length);
     sink += separator_length;
+    taint_sink += separator_length;
 
     CHECK(fixed_array->get(i)->IsString());
     String element = String::cast(fixed_array->get(i));
     int element_length = element->length();
     DCHECK(sink + element_length <= end);
     String::WriteToFlat(element, sink, 0, element_length);
+    tainttracking::FlattenTaintData(element, taint_sink, 0, element_length);
     sink += element_length;
+    taint_sink += element_length;
   }
   DCHECK(sink == end);
 
   // Use %_FastOneByteArrayJoin instead.
   DCHECK(!answer->IsOneByteRepresentation());
+  tainttracking::OnJoinManyStrings(*answer, *array);
   return *answer;
 }
 
-template <typename sinkchar>
-static void WriteRepeatToFlat(String src, Vector<sinkchar> buffer, int cursor,
+// XXXstroucki upstream for some reason passes buffer as Vector<bla>
+template <typename ResultString, typename sinkchar>
+static void WriteRepeatToFlat(String src, ResultString buffer, int cursor,
                               int repeat, int length) {
   if (repeat == 0) return;
 
-  sinkchar* start = &buffer[cursor];
+  DisallowHeapAllocation no_gc;
+  sinkchar* start = buffer->GetChars(no_gc)+cursor;
   String::WriteToFlat<sinkchar>(src, start, 0, length);
 
+  tainttracking::TaintData* start_taint =
+    tainttracking::GetWriteableStringTaintData(buffer) + cursor;
+  tainttracking::FlattenTaintData(src, start_taint, 0, length);
+
   int done = 1;
   sinkchar* next = start + length;
 
+  tainttracking::TaintData* next_taint = start_taint + length;
+
   while (done < repeat) {
     int block = Min(done, repeat - done);
     int block_chars = block * length;
     CopyChars(next, start, block_chars);
+    CopyChars(next_taint, start_taint, block_chars);
     next += block_chars;
     done += block;
   }
 }
 
 // TODO(pwong): Remove once TypedArray.prototype.join() is ported to Torque.
-template <typename Char>
+template <typename ResultString, typename Char>
 static void JoinSparseArrayWithSeparator(FixedArray elements,
                                          int elements_length,
                                          uint32_t array_length,
                                          String separator,
-                                         Vector<Char> buffer) {
+                                         Handle<ResultString> buffer) {
   DisallowHeapAllocation no_gc;
   int previous_separator_position = 0;
   int separator_length = separator->length();
   DCHECK_LT(0, separator_length);
   int cursor = 0;
+
+  tainttracking::TaintData* buffer_taint =
+    tainttracking::GetWriteableStringTaintData(*buffer);
+
   for (int i = 0; i < elements_length; i += 2) {
     int position = NumberToInt32(elements->get(i));
     String string = String::cast(elements->get(i + 1));
     int string_length = string->length();
     if (string->length() > 0) {
       int repeat = position - previous_separator_position;
-      WriteRepeatToFlat<Char>(separator, buffer, cursor, repeat,
+      WriteRepeatToFlat<ResultString, Char>(separator, *buffer, cursor, repeat,
                               separator_length);
       cursor += repeat * separator_length;
       previous_separator_position = position;
-      String::WriteToFlat<Char>(string, &buffer[cursor], 0, string_length);
+      String::WriteToFlat<Char>(string, buffer->GetChars(no_gc) + cursor, 0, string_length);
+      tainttracking::FlattenTaintData(
+          string, buffer_taint + cursor, 0, string_length);
       cursor += string->length();
     }
   }
@@ -481,9 +513,9 @@ static void JoinSparseArrayWithSeparator(FixedArray elements,
   // otherwise the total string length would have been too large.
   DCHECK_LE(array_length, 0x7FFFFFFF);  // Is int32_t.
   int repeat = last_array_index - previous_separator_position;
-  WriteRepeatToFlat<Char>(separator, buffer, cursor, repeat, separator_length);
+  WriteRepeatToFlat<ResultString, Char>(separator, *buffer, cursor, repeat, separator_length);
   cursor += repeat * separator_length;
-  DCHECK(cursor <= buffer.length());
+  DCHECK(cursor <= buffer->length());
 }
 
 // TODO(pwong): Remove once TypedArray.prototype.join() is ported to Torque.
@@ -555,20 +587,28 @@ RUNTIME_FUNCTION(Runtime_SparseJoinWithSeparator) {
                                           ->NewRawOneByteString(string_length)
                                           .ToHandleChecked();
     DisallowHeapAllocation no_gc;
-    JoinSparseArrayWithSeparator<uint8_t>(
+    JoinSparseArrayWithSeparator<SeqOneByteString, uint8_t>(
         FixedArray::cast(elements_array->elements()), elements_length,
         array_length, *separator,
-        Vector<uint8_t>(result->GetChars(no_gc), string_length));
+        result);
+    {
+      DisallowHeapAllocation no_gc;
+      tainttracking::OnJoinManyStrings(*result, *elements_array);
+    }
     return *result;
   } else {
     Handle<SeqTwoByteString> result = isolate->factory()
                                           ->NewRawTwoByteString(string_length)
                                           .ToHandleChecked();
     DisallowHeapAllocation no_gc;
-    JoinSparseArrayWithSeparator<uc16>(
+    JoinSparseArrayWithSeparator<SeqTwoByteString, uc16>(
         FixedArray::cast(elements_array->elements()), elements_length,
         array_length, *separator,
-        Vector<uc16>(result->GetChars(no_gc), string_length));
+        result);
+    {
+      DisallowHeapAllocation no_gc;
+      tainttracking::OnJoinManyStrings(*result, *elements_array);
+    }
     return *result;
   }
 }
@@ -638,8 +678,8 @@ RUNTIME_FUNCTION(Runtime_StringToArray) {
     elements = isolate->factory()->NewFixedArray(length);
   }
   for (int i = position; i < length; ++i) {
-    Handle<Object> str =
-        isolate->factory()->LookupSingleCharacterStringFromCode(s->Get(i));
+    // Use the substring command so that the taint gets propagated
+    Handle<Object> str = isolate->factory()->NewSubString(s, i, i+1);
     elements->set(i, *str);
   }
```