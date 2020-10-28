# CSA , Torque & builtins

## Introduction & Rational

Imagine you were implementing the V8 engine. Your main target is to be able to run the ECMAScript specification for Javascript. This requires read through the [comprehensive specification](https://github.com/tc39/proposals) and implement the required behaviour.

V8's optimizing compiler [Turbofan]() uses a combination of techniques in order to make long running code faster with help of  type information and optimizations. However, you would still require a good baseline performance for all the functions that ECMAScript defines. A bultin function from ECMAScript's prespective is a function from the ECMAScript spec's library.

You would want these functions to be fast, and historically many were written in assembly to ensure that they were fast, efficient (which they had to since user code basically built ontop of these primitive functionalities). The assembly had various special tweaks to it in order to utilize the platform's instruction set better. Initially V8 was supportted on only on one platform, with time that grew to a [large number](). As you would imagine, with the volatile nature of ECMAScript & complex design for V8, writing assembly based builtins for multiple platforms and keeping them updated isn't a trivial task. 

V8 team was soon looking for a abstraction / system which let's them write the bultins once and use then use them across the platforms, while retaining the architecture specific optimizations.


### Fits like an old sock

Okay Pandu, fine I get what builtins are. But I still quite don't get how it really fits into the bigger picture of the assets that V8 produces, codebase and the controlflow. Could you maybe, go a little deeper from the codeflow prespective? 

![](https://i.imgur.com/WayerLo.gif)

[Embedded Builtins](https://v8.dev/blog/embedded-builtins) provides some great context on the entire rational behind builtins.  Below is key parts based on my understanding of the document:
- The idea of builtins is similar to the way we have DLLs and .so files in Windows & UNIX respectively. The idea is to share JS code accross V8 Isolates. Imagine having the entire codebase which implements the ECMAScript standard over and over again for each instance of V8 Isolate. Not very efficient.
- This was historically hard to implement because:

> Generated builtin code was neither isolate- nor process-independent due to embedded pointers to isolate- and process-specific data. V8 had no concept of executing generated code located outside the managed heap.

If you're intrested on why and how bultins & snapshots function, highly recommend reading [Embedded Builtins](https://v8.dev/blog/embedded-builtins).

## Enter CodeStubAssembler

We know that V8's Turbofan uses an internal IR for low-level machine operations. The Turbofan backend already knows how to optimize these operations according to the traits of the target platform. What if we could use the Turbofan to directly generate the platform specific builtin code! CSA was built to do just that.
> Dubbed the CodeStubAssembler or CSA—that defines a portable assembly language built on top of TurboFan’s backend. The CSA adds an API to generate TurboFan machine-level IR directly without having to write and parse JavaScript or apply TurboFan’s JavaScript-specific optimizations - V8 Team

![](https://i.imgur.com/IiHyJAe.png)

As you can see in the image above, this bath can only be currenly used by bultins and some additional [Ignition]() based handlers which are created and managed by V8. The normal JS code still hits the Javascript frontend. 

CSA contains code to generate IR for low level operations like "load this object pointer from a given addr" and "mask this bit from this address", which allows the developer to write the builtins in near to assembly level glanularity, while remaining platform independent.

### Test Triving CSA

Well that's goodlooking and everything, but let's have a deeper hands on look at the CSA.

#### Example 1
Now let's do through the example from [CSA Embedding Post](https://v8.dev/docs/csa-builtins)from V8 Blog. 

##### Task 1: Declare the builtin

For the builtin to be picked by the `mksnapshot.exe`, we first need to declare it in the `src/builtins/builtins-definitions.h`. Here, you can have [multiple kinds](https://chromium.googlesource.com/external/github.com/v8/v8.wiki/+/30daab8a92562c331c93470f54877fa02b9422b5/CodeStubAssembler-Builtins.md) of builtins, each with specific usecases in mind, which are not at all apperent at first. Below are few starting points for each:
- `CPP`: Builtin in C++. Entered via BUILTIN_EXIT frame.
    - [starCTF's OOB V8](https://web.archive.org/web/20200907163615/https://faraz.faith/2019-12-13-starctf-oob-v8-indepth/) problem made use of it, to introduce a builtins to make an off by one, out of bounds access.
- `TFJ`: Builtin in Turbofan, with JS linkage (callable as Javascript function)
    - These builtins are concise, easy to read and often called as javascript functions, though not as fast.  
    - V8 Blog example: [Math42](https://v8.dev/docs/csa-builtins#declaring-mathis42) (discussed in this section)
- `TFS`: Builtin in TurboFan, with CodeStub linkage
    - These builtins are usually used to extract out commonly used code since these can be called from other builtins (multiple callers).
    - V8 Blog example: [Math42extension](https://v8.dev/docs/csa-builtins#defining-and-calling-a-builtin-with-stub-linkage)
- `TFC`: Builtin in Turbofan, with CodeStub linkage and custom descriptor.
    - In my experience, these are usually used for implementing operator overloads like [stringEqual](https://source.chromium.org/chromium/chromium/src/+/master:v8/src/builtins/builtins-definitions.h;l=111;drc=40ad911657e160af18dfc0ebe36c0ea3078fbf25) and [definition](https://source.chromium.org/chromium/chromium/src/+/master:v8/src/builtins/builtins-string-gen.cc;l=724;drc=74a9b9c4d8dbddabdbb2df4bceb9e0d4b4369898).
- `TFH`: Handlers in Turbofan, with CodeStub linkage.
- `BCH`: Bytecode Handlers, with bytecode dispatch linkage.
- `ASM`: Builtin in platform-dependent assembly.
    - Highly optimzied versions, usually used for builtins which are called very often, constructors etc.
    - Example:  
  
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

CSA is great! Provides us all the low level optimizations we want from builtins, while keeping arhitecture specific nitti-gritties away from us. However, looking at the CSA example we say ealier, it's very similar to assembly itself (with GOTOs). Secondly, we need to manually do a lot of checks for the node types, resulting in still more code. Looks like a place for some more abstraction. This is where torque comes in.
> V8 Torque: is a V8-specific domain-specific language that is translated to CodeStubAssembler. As such, it extends upon CodeStubAssembler and offers static typing as well as readable and expressive syntax.

Torque thus basically is a wrapper over CSA, which makes things easier (somewhat) for V8 dev writing the builtins. It's most useful when you're using other exisiting CSA/ASM builtins to do somthing more powerful.

![](https://i.imgur.com/jgO8e0O.png)

To summarize, your torque file is compiled to CSA code before it's integrated into the snapshot file.

![](https://v8.dev/_img/docs/torque/build-process.svg)

### Test Driving Torque


### References
1. https://v8.dev/docs/builtin-functions
1. https://v8.dev/blog/embedded-builtins
1. https://v8.dev/docs/torque