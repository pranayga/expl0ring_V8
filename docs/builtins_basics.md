# CSA , Torque & builtins

## Introduction & Rational

Imagine you were implementing the V8 engine. Your main target is to be able to run the ECMAScript specification for Javascript. This requires read through the [comprehensive specification](https://github.com/tc39/proposals) and implement the required behaviour.

V8's optimizing compiler [Turbofan]() uses a combination of techniques in order to make long running code faster with help of  type information and optimizations. However, you would still require a good baseline performance for all the functions that ECMAScript defines. A bultin function from ECMAScript's prespective is a function from the ECMAScript spec's library.

You would want these functions to be fast, and historically many were written in assembly to ensure that they were fast, efficient (which they had to since user code basically built ontop of these primitive functionalities). The assembly had various special tweaks to it in order to utilize the platform's instruction set better. Initially V8 was supportted on only on one platform, with time that grew to a [large number](). As you would imagine, with the volatile nature of ECMAScript & complex design for V8, writing assembly based builtins for multiple platforms and keeping them updated isn't a trivial task. 

V8 team was soon looking for a abstraction / system which let's them write the bultins once and use then use them across the platforms, while retaining the architecture specific optimizations.


### Fits like my old sock

Okay Pandu, fine I get what builtins are. But I still quite don't get how it really fits into the bigger picture of the assets that V8 produces, codebase and the controlflow. Could you maybe, go a little deeper from the codeflow prespective? 

![](https://i.imgur.com/WayerLo.gif)

[Embedded Builtins](https://v8.dev/blog/embedded-builtins) provides some great context on the entire rational behind builtins. Below is key parts based on my understanding of the document


## Enter CodeStubAssembler

We know that V8's Turbofan uses an internal IR for low-level machine operations. The Turbofan backend already knows how to optimize these operations according to the traits of the target platform. What if we could use the Turbofan to directly generate the platform specific builtin code! CSA was built to do just that.
> Dubbed the CodeStubAssembler or CSA—that defines a portable assembly language built on top of TurboFan’s backend. The CSA adds an API to generate TurboFan machine-level IR directly without having to write and parse JavaScript or apply TurboFan’s JavaScript-specific optimizations - V8 Team

![](https://i.imgur.com/IiHyJAe.png)

As you can see in the image above, this bath can only be currenly used by bultins and some additional [Ignition]() based handlers which are created and managed by V8. The normal JS code still hits the Javascript frontend. 

CSA contains code to generate IR for low level operations like "load this object pointer from a given addr" and "mask this bit from this address", which allows the developer to write the builtins in near to assembly level glanularity, while remaining platform independent.

### Test Triving CSA

Here we will look into the code in much more detail than what the introduction post goes into.

#### Example 1
Now let's do through the example from [CSA Embedding Post](https://v8.dev/docs/csa-builtins)from V8 Blog. 

```diff
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
```

```diff
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
```

#### Example 2
Now let's do through the example from [CSA Introduction Post](https://v8.dev/blog/csa)



## A little more snug: Torque

![](https://i.imgur.com/jgO8e0O.png)

![](https://v8.dev/_img/docs/torque/build-process.svg)

### Test Driving Torque




### References
1. https://v8.dev/docs/builtin-functions
1. https://v8.dev/blog/embedded-builtins
1. https://v8.dev/docs/torque