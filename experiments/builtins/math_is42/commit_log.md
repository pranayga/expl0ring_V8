### Intro
Adapted from: https://v8.dev/blog/csa
Date: 19th Oct 2020

### Use the follwing commit for this patch
commit 7a8d20a79f9d5ce6fe589477b09327f3e90bf0e0 (HEAD)
Author: yangguo <yangguo@chromium.org>
Date:   Mon Apr 10 22:46:46 2017 -0700

    [snapshot] encode resource before serializing.
    
    Before serializing an external string for a native source, we replace
    its resource field with the type and index of the native source. Upon
    deserialization, we restore the resource.
    
    This change also removes the native source caches with a more straight-
    forward mechanism to find the resource type and index.
    
    R=ulan@chromium.org
    
    Review-Url: https://codereview.chromium.org/2807023003
    Cr-Commit-Position: refs/heads/master@{#44545}

### Commit Diff
```diff
diff --git a/src/bootstrapper.cc b/src/bootstrapper.cc
index 4d50b56f5d..eeb6cf9899 100644
--- a/src/bootstrapper.cc
+++ b/src/bootstrapper.cc
@@ -2512,6 +2512,8 @@ void Genesis::InitializeGlobal(Handle<JSGlobalObject> global_object,
     SimpleInstallFunction(math, "cos", Builtins::kMathCos, 1, true);
     SimpleInstallFunction(math, "cosh", Builtins::kMathCosh, 1, true);
     SimpleInstallFunction(math, "exp", Builtins::kMathExp, 1, true);
+    /* Pandu's Function */
+    SimpleInstallFunction(math, "is42", Builtins::kMathIs42, 1, true);
     Handle<JSFunction> math_floor =
         SimpleInstallFunction(math, "floor", Builtins::kMathFloor, 1, true);
     native_context()->set_math_floor(*math_floor);
diff --git a/src/builtins/builtins-definitions.h b/src/builtins/builtins-definitions.h
index 53fd43ad53..c502dd5304 100644
--- a/src/builtins/builtins-definitions.h
+++ b/src/builtins/builtins-definitions.h
@@ -583,6 +583,8 @@ namespace internal {
   /* ES6 #sec-math.trunc */                                                    \
   TFJ(MathTrunc, 1, kX)                                                        \
                                                                                \
+  /* Pandu */                                                                  \
+  TFJ(MathIs42, 1, kX)                                                         \
   /* Number */                                                                 \
   /* ES6 section 20.1.1.1 Number ( [ value ] ) for the [[Call]] case */        \
   ASM(NumberConstructor)                                                       \
diff --git a/src/builtins/builtins-math-gen.cc b/src/builtins/builtins-math-gen.cc
index e5c8489301..4dde57f044 100644
--- a/src/builtins/builtins-math-gen.cc
+++ b/src/builtins/builtins-math-gen.cc
@@ -534,5 +534,64 @@ TF_BUILTIN(MathMin, MathBuiltinsAssembler) {
   MathMaxMin(context, argc, &CodeStubAssembler::Float64Min, V8_INFINITY);
 }
 
+// Pandu Addition
+TF_BUILTIN(MathIs42, MathBuiltinsAssembler) {
+    // Load the current function context (an implicit argument for every stub)
+    // and the X argument. Note that we can refer to parameters by the names 
+    // defined in the builtin declartion
+    Node* const context = Parameter(Descriptor::kContext);
+    Node* const x = Parameter(Descriptor::kX);
+
+    // At this point, x can be basically anything - a SMI, a heapNumber,
+    // Undefined, or any other arbitrary JS object. Let's call the ToNUmber
+    // builtin to covert x to a number we can use.
+    // CallBuiltin can be used to conveniently call any CSA builtin
+    Node* const number = CallBuiltin(Builtins::kToNumber, context, x);
+
+    // Create a CSA variable to store the resulting value. The type of the
+    // Variable is kTagged since we will only be storing tagged pointers in it.
+    VARIABLE(var_result, MachineRepresentation::kTagged);
+
+    // We need to define a couple of labels which will be used as jmp targets
+    Label if_issmi(this), if_isheapnumber(this), out(this);
+
+    // ToNumber always returns a Number. We need to distinguish between Smis
+    // and heap numbers - here, we check whether number is a a Smi and conditionally
+    // jmp to the corresponding labels
+    Branch(TaggedIsSmi(number), &if_issmi, &if_isheapnumber);
+
+    // Binding a label begins generation of code for it
+    BIND(&if_issmi);
+    {
+        // SelectBooleanConstant returns the JS true/false values depending on
+        // whether the passed condition was true/false. The result is bound to
+        // our var_result variable, and we then unconditionally jmp to the out
+        // label.
+        var_result.Bind(SelectBooleanConstant(SmiEqual(number, SmiConstant(42))));
+        Goto(&out);
+    }
+
+    BIND(&if_isheapnumber);
+    {
+        // ToNumber can only return a Smi or a heapNumber. Just to make sure
+        // we add an assertion here that verifies number is actually a heap number.
+        CSA_ASSERT(this, IsHeapNumber(number));
+        // Heap numbers wrap a floating point value. We need to explicitly
+        // extract this value, perform a floating point comparison, and again
+        // bind var_result based on the outcome
+        Node* const value = LoadHeapNumberValue(number);
+        Node* const is_42 = Float64Equal(value, Float64Constant(42));
+        var_result.Bind(SelectBooleanConstant(is_42));
+        Goto(&out);
+    }
+
+    BIND(&out);
+    {
+        Node* const result = var_result.value();
+        CSA_ASSERT(this, IsBoolean(result));
+        Return(result);
+    }
+}
+
 }  // namespace internal
 }  // namespace v8
```
