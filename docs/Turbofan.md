# Turbofan

## Introduction

In this long-awaited post we will cover V8's compiler. We are going to look at the general design, its implementation in code, debugging tools, and more. As I have mentioned before, this is the component in V8 that faces the most scrutiny. Although there is so much more to V8 exploitation, this is the area that is generally talked about.

Turbofan is given information from Ignition or Liftoff about the source code, and then converts that into highly-optimized, architecture-specific machine code. Remember, this process only takes place once a function or other section of code becomes "hot," or is run multiple times. V8 then uses on-stack replacement to execute the optimized code at runtime.

##### Note: In this post I'll be talking about compiling JavaScript, ignoring WebAssembly for the time being.

## Compiler design

Turbofan operates on a [Sea of Nodes](https://darksi.de/d.sea-of-nodes/) structure, which is well known in compiler theory, and slightly modified for V8's purposes. At a very high level, the compiler takes the Abstract Syntax Tree (AST), which has already been generated by Ignition, and performs a series of optimizations to reduce the graph and turn it into machine code. These nodes can combine, split, or change names in the tree during the optimization phases. Each phase uses a specific [visitor class](https://en.wikipedia.org/wiki/Visitor_pattern) to go through the nodes and apply node-specific changes. We're going to start our analysis of optimization by talking about this Intermediate Representation (IR) between the JavaScript source and finalized JIT code.

## Deoptimization

I know we haven't even really got to optimization yet, but some of the first things mentioned in the talk on IR are the calls to deopt, so we need to take a quick detour. While the decision to optimize is made outside of Turbofan, deoptimization is something that gets called from within the JIT code that Turbofan produces. In V8, there are two kinds of deoptimization, eager and lazy. 

Eager deopt is when "the code currently being executed needs to be deoptimized." ([source](https://docs.google.com/presentation/d/1AVu1wiz6Deyz1MDlhzOWZDRn6g_iFkcqsGce1F23i-M/edit#slide=id.g261a17b7b7_0_359)) In the code below, the function `f()` would deopt if it was called again with an argument that was not a number.

```
function f(x) { 
    return x + 1; 
}

// optimize for integer argument
for (i = 0; i < 1000000; i++) { 
    f(i);
}

// now deoptimize when called with string as argument
f("1");
```

In this case, calling `f()` with a string invalidates the assumption that the parameter is an integer. V8 validates assumptions by placing checks within the JIT code, which we will see in the IR section. If these checks fail, we will jump back to the interpreter immediately, which is why this is _eager_. 

Lazy deopt is when "the code currently being executed invalidates some other code." ([source](https://docs.google.com/presentation/d/1AVu1wiz6Deyz1MDlhzOWZDRn6g_iFkcqsGce1F23i-M/edit#slide=id.g261a17b7b7_0_359)) So essentially, if our optimized code affects some _other_ optimized code, we need to deoptimize that other code because we may have broken some of its assumptions. V8 used to trace the effects tree, deoptimizing as it went along. However, it now replaces the code of other functions with calls to deoptimize. Since the deoptimization will happen later, whenever that other code is run, this is called _lazy_. The work for lazy deopt can be found in the outbrief from this [internship on laziness](https://docs.google.com/presentation/d/1AVu1wiz6Deyz1MDlhzOWZDRn6g_iFkcqsGce1F23i-M/edit).

We may need to bail out to the interpreter if something goes wrong in our function, and you can see all of the reasons in `src/deoptimize-reason.h`. Using d8 we can see which functions have deoptimized using the `--trace-deopt` flag.

For once, I finally summarized the main points of something without just linking the reference. That being said, if you want a full presentation about deoptimization from the Google team, that can be found [here](https://docs.google.com/presentation/d/1Z6oCocRASCfTqGq1GCo1jbULDGS-w-nzxkbVF7Up0u0/edit).

## Intermediate Representation

Since Turbofan optimizes in multiple rounds, it makes sense to have some intermediate structure to slowly optimize. There are two great presentations that should serve as initial learning points and references for later on: [Turbofan IR](https://docs.google.com/presentation/d/1Z9iIHojKDrXvZ27gRX51UxHD-bKf1QcPzSijntpMJBM/edit) and [Turbofan JIT Design](https://docs.google.com/presentation/d/1sOEF4MlF7LeO7uq-uThJSulJlTh--wgLeaVibsbb3tc/edit). If you understand both of these then that will make further posts much easier to understand. We will learn how to look at the graphs for our own JavaScript code later on in this post.

## Typing

Once we get all of our JavaScript put into a tree, we can start to include information about each node in order to better collapse it. Some of this information may include side-effect or control flow relevance, as well as the type of node and range of possible values. Typing is the process of determining the possible, well... _type_ of a node, and it's possible values. For example, a node may represent an integer with a possible range from 5-10, as would be the case for `x` in the code below.

```
var x = 5;
if (y == 10) {
    x = 10;
}
```

This can be helpful information for optimization, like in this example:

```
var x = 5;
if (y == 10) {
    x = 10;
}

if (x < 5) {
    // unreachable, "if" statement can be eliminated
}
```

The compiler can eliminate the "if" statement code block since it knows that comparing an integer with a possible range of 5-10 will never be less than 5. However, it's also important to remember that not all code in a JavaScript file is being optimized. The same assumptions could not be made in this case:

```
function example(x, y) {
    if (y == 10) {
        x = 10;
    }

    if (x < 5) {
        // compiler wouldn't know that this is unreachable because it is only optimizing within the function scope
    }
}

// optimize
for (i = 0; i < 1000000; i++) { 
    example(i, 0);
}

// "if" statement would be unnecessary in this case
var z1 = 5;
var z2 = 10;
example(z1, z2);
```

Typing is one of the most important aspects of the compiler to understand because many exploits have been developed from taking advantage of what the compiler assumes about the code vs. what actually happens at runtime. This concept gets a little confusing, but we will go into great depth as part of our case studies (and there are several examples already available in our resources section).

The `types.h` file is incredibly useful for understanding how V8 accomplishes typing. Not only does it include all of the different "types" that exists, but it also has plenty of comments to make sense of things. Essentially there is a bit-array that describes the type of every node. The bit flags are combined to show what all possible types that a node may be. Take a special note of how NaN and MinusZero have their own values, and how many different representations exist for numbers. When we look at the IR tree, we will see how information about the type of node and possible value ranges are tracked.

### Arrays

One thing that is not immediately obvious from looking at `types.h` is that there are actually different types of arrays. That is because we want to store array values efficiently, and there are different optimizations for different kinds of arrays. For example, take a look at this code:

```
let arr1 = [0, 1, 2];
let arr2 = new Array(1000);

arr2[0] = 0;
arr2[999] = 999;
```

For `arr1` in the example above, it makes sense to allocate 3 blocks of contiguous memory to hold our values. However, for `arr2`, we may just wish to store 2 values with their indices, 0 and 999. This is basically the difference between "packed" and "holey" arrays. There's a lot more to say about array typing, and we will cover that in my next post.

### Operations

When nodes are collapsed, they may have their values merged. Here is a quick recap of terms that appear a lot in the code:

**union** - Combine all possible values of both inputs.

**intersect** - Combine only the matching values of both inputs.

**phi** - Compilers need some way to track variables along different possible execution paths. This requires assigning different intermediate identifiers for the same variable ([explanation](https://en.wikipedia.org/wiki/Static_single_assignment_form)). When the execution paths merge, we combine the possible values of the variable from both paths.

**is** - `node.Is(arg)` A node "is" the argument it is given if it is a _subset_ of the argument passed to the function.

**maybe** - `node.Maybe(arg)` A node "maybe" is the argument if the argument is a subset of all the types of the node.

##### See [Saelo's presentaiton](https://docs.google.com/presentation/d/1DJcWByz11jLoQyNhmOvkZSrkgcVhllIlCHmal1tGzaw/edit#slide=id.g52a72d9904_2_148)

## Debugging Tools

### d8 flags

Invoking d8 with the `--help` flag outputs a ginourmous list of options. While there are options available for enabling/disabling many of V8's features, we will focus on the `--trace-turbo` flag, which will output a json file for us to view the IR of optimized functions in our script. The `--trace-turbo-filter` option let's us specify the function to trace, which can get rid of annoying extra files if your for loop gets optimized or something like that. Another quick note, `--print-opt-code --print-code-verbose --code-comments` can be used to get some extra information about the machine code that Turbofan produces. If you just want to see when a function gets optimized, and the reason, you can use `--trace-opt`.

There are 3 ways that I have seen to optimize functions for exploit dev testing. There is no "right" way, but I want to mention this because when you play around with POCs of previous bugs it's important to know that not all paths to optimization will immediately present bugs in the same way. As I mentioned in a [previous post](https://m4dst4cks.github.io/blog/2020/09/18/V8-Exploitation-Series-Part-3) from this series, the `--allow-natives-syntax` flag lets us use certain built-in functions that V8 understands.

> 1. A for loop with enough iterations to trigger optimization

```
function test() {
    // some code here to optimize
}

for (var i=0; i < 1000000; i++) {
    test();
}

// ./d8 test.js --allow-natives-syntax --trace-turbo --trace-turbo-path turbo_out --trace-turbo-filter test
```

> 2. A call to the native function OptimizeFunctionOnNextCall

```
function test() {
    // some code here to optimize
}

%OptimizeFunctionOnNextCall(test);
test();

// ./d8 test.js --allow-natives-syntax --trace-turbo --trace-turbo-path turbo_out --trace-turbo-filter test
```

> 3. Using native functions PrepareFunctionForOptimization and then OptimizeFunctionOnNextCall

```
function test() {
    // some code here to optimize
}

%PrepareFunctionForOptimization(test);
test();
%OptimizeFunctionOnNextCall(test);
test();

// ./d8 test.js --allow-natives-syntax --trace-turbo --trace-turbo-path turbo_out --trace-turbo-filter test
```

### Turbolizer

Now that we have our json file, we can view it through our browser using Turbolizer, which is included in the V8 repo. You can access a public version [here](https://v8.github.io/tools/head/turbolizer/index.html). 

Here are the instructions if you want to set it up locally: 

1. Go to `tools/turbolizer` in your V8 install directory
2. `npm i`
3. You may also be forced to run `npm audit fix`
4. `npm run-script build`

Running locally:

1. Anytime you want to use turbolizer, you just need to have a web server for this directory, an easy way to do this is by running `python3 -m http.server`
2. Go to port 8000 (or whatever port you defined) in your browser and upload your json file (in the output directory you specified) using the cloud icon in the upper right corner

Turbolizer is great because it allows you to look through the IR at each optimization phase. This can be used to verify whether or not the compiler is correctly optimizing your code. Many bugs require looking at these graphs to see where Turbofan may be making a flawed assumption.

### Bytecode

You can print out the original Ignition bytecode with the `--print-bytecode` flag.

### Practice

There is no better way to understand the IR than by actually looking at it in Turbolizer. Create some simple scripts that optimize some function, perhaps trying out the three different techniques above, and see how the nodes are changed during different optimization phases. Some of the node names won't make much sense, but that's okay. We're going to go in-depth on tracing the optimization path.

## Where is the Code?

In my last post I said it was a good idea to get comfortable with the code base. That will come in handy now as you look at the optimization phases as they are actually carried out in C++. It will also be useful when any of this information changes in the future.

I want to highlight the current go-to article for learning about Turbofan exploitation. [Jeremy Fetiveau](https://twitter.com/__x86) has written a lot on this topic, and you can find his intro article [here](https://doar-e.github.io/blog/2019/01/28/introduction-to-turbofan). He gives a list of some of the important locations within `src/compiler` and talks about the optimization phases. This article is great for getting a high-level overview with some detailed information. I definitely recommend starting there, and then looking at this [blog post by Sakura](https://eternalsakura13.com/2018/09/05/pipeline/), which goes into very fine detail about the optimization pipeline. I am trying to avoid getting into too many specifics since the code base will change, but I found much of Sakura's analysis to still be accurate. However, all of this information will probably take a while to understand, and it's probably better to practice by looking at a past bug to get some familiarity before trying completely understand all aspects of the timeline. 

My personal process for understanding is to look in Turbolizer and flip through the different optimization phases. I then try to map the phase name with the relevant file in `src/compiler`, or look at the references I mentioned, and then search the file for the names of the nodes that I am looking at. Generally, nodes have a specific function for each phase that is able to perform the specific optimization. Some phases cannot reduce some nodes, and they will be left unchanged. Sometimes phases simply change the name of a node. Figuring out transitions takes time, but using both Turbolizer and the source code helps. As I mentioned in a previous post, you can use VSCode to quickly find references between functions to get a better understanding of the control flow as well. However, many times, bug reports come with details of where the vulnerable code is and you can just skip there directly. However, having a better understanding of the overall process makes understanding reports (and finding your own bugs!) much easier. 

### Nodes and Operators

`node.h` has a lot of comments that detail the `Node` struct, which is central to understanding optimizations. This struct contains and ID, the type information, adjacent nodes, and several other details that comprise a node in the IR. It is important to know where to look for the members of this struct because some names are not immediately intuitive. Some are more guessable, for example, `node->InputAt(0)` refers to an adjacent node that provides input to the current node (Although I often see this wrapped as `Operand(node, i)`).

Some more information from the comments:

> // A Node is the basic primitive of graphs. Nodes are chained together by
> // input/use chains but by default otherwise contain only an identifying number
> // which specific applications of graphs and nodes can use to index auxiliary
> // out-of-line data, especially transient data.
> //
> // In addition Nodes only contain a mutable Operator that may change during
> // compilation, e.g. during lowering passes. Other information that needs to be
> // associated with Nodes during compilation must be stored out-of-line indexed
> // by the Node's id.

`operator.h` contains the definition for, you guessed it, the `Operator` struct. Nodes that "operate" on other values (think "+") also contain information about what dependencies they create, how many values they accept, etc. This is accessed through `node->op()`.

Some more information from the comments:

> // An operator represents description of the "computation" of a node in the
> // compiler IR. A computation takes values (i.e. data) as input and produces
> // zero or more values as output. The side-effects of a computation must be
> // captured by additional control and data dependencies which are part of the
> // IR graph.
> // Operators are immutable and describe the statically-known parts of a
> // computation. Thus they can be safely shared by many different nodes in the
> // IR graph, or even globally between graphs. Operators can have "static
> // parameters" which are compile-time constant parameters to the operator, such
> // as the name for a named field access, the ID of a runtime function, etc.
> // Static parameters are private to the operator and only semantically
> // meaningful to the operator itself.

## Conclusion

There are still many questions to be answered, like "how do we find bugs in this huge code base?", "how do you get practice with understanding this?", "is it Turbofan or TurboFan?". Thankfully there are more places with information on it than most other parts of V8. My goal was to include a lot of baseline information and references for reading up on Turbofan before jumping into exploitation.

Also, [Tsuro's article on V8 exploitation](https://docs.google.com/presentation/d/1DJcWByz11jLoQyNhmOvkZSrkgcVhllIlCHmal1tGzaw/edit) is a great presentation that summarizes most of what I talked about and has some exaples of exlpoitation.

## References

[V8 Turbofan docs](https://v8.dev/docs/turbofan)

[V8 elements kinds docs](https://v8.dev/blog/elements-kinds)

[An internship on laziness](https://docs.google.com/presentation/d/1AVu1wiz6Deyz1MDlhzOWZDRn6g_iFkcqsGce1F23i-M/edit)

[A guided tour through Chrome's javascript compiler](https://docs.google.com/presentation/d/1DJcWByz11jLoQyNhmOvkZSrkgcVhllIlCHmal1tGzaw/edit)