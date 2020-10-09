# Exploring V8 Engine - II (Control Flow & Memory Structures)

##### In this post, we will start our exploration of V8 engine and look under the hood at call sequences that are made in order to execute a simple `'hello' + 'world'` command. This post is a follow up to our [Exploring V8 Engine - I](/docs/v8_exploration_I.md) post.

## Setting up release.sample build (monolithic with debug)

In order to testout the V8 embedding, you need a compile your sample hello-world.cc file with the V8 source code. Doing this for each sample would be tedious. V8 provide you way to build the V8 source as a standlone binary which we can directly use while building any samples which embed V8. This makes sure that you don't have to recompile the V8 source each time you make a small change to your sample.

The [embedding V8](https://v8.dev/docs/embed) takes you through the process effortlessly. However, the `x64.release.sample` they build do not have debug symbols enabled (which we want). So, below is a slightly tweaked version of the `gn args out.gn/x64.release.sample` which includes all the extra debugging symbols we will require.

```gn
is_component_build = false
is_debug = true
target_cpu = "x64"
use_custom_libcxx = false
v8_monolithic = true
v8_use_external_startup_data = false
v8_enable_backtrace = true
v8_optimized_debug = false
```

Use the above config with `gn args out.gn/x64.release.sample` to enable all the symbols (similar to a debug build). Finally, while building your hello-world.cc example, add the `-g` flag to include your sample's source.

```bash
g++ -g -I. -Iinclude samples/hello-world.cc -o hello_world -lv8_monolith -Lout.gn/x64.release.sample/obj/ -pthread -std=c++0x -DV8_COMPRESS_POINTERS
```

This should now generate the hello_world executable which if you run:
```
Hello, World!
3 + 4 = 7
```

#### Tips on running GDB better
* when building with `is_debug` flag and a monolithic binary, the build process populates `v8/out.gn/x64.release.sample/obj` with the debugging symbols. Thus, run GDB from `v8/out.gn/x64.release.sample` directory (or set you paths), so that you pick up the debugging symbols
  * Good: 
  ![](https://i.imgur.com/bZKOnTL.png)
  * Bad:
  ![](https://i.imgur.com/lMeJjsO.png)
* Configure your `GDB` to pickup the `tools/gdbinit` file

Now lets move on and hook up our good old friend `GDB` and see what's what.

### Setting up GDB with GEF

I tried a couple of different debuggers:
- plain `GDB`
- plain `LLDB`
- `llnode` (a LLDB extension for node analysis)

While llnode may be regarded much better for V8 analysis, it's maintained to work with the latest LTS release of `NODEJS`, which may not be most up to date with V8 master. This may cause issues with extensions giving you [faulty data](https://joyeecheung.github.io/blog/2018/12/31/tips-and-tricks-node-core/).

IMO, GDB with [GEF](https://gef.readthedocs.io/en/master/) is the best way to go along with using `/tools/gdbinit` file.

Now that we are all setup, let's start taking a look at the source code and the control flow.

## hello-world.cc under a debugger

From our previous post, we understand the major ideas about `Isolates`,`isolate_scope()`, `handle_scope` and `Contexts`. To summarize, this is what the object structures would look like:

![](https://i.imgur.com/eBzCTr9.png)

With that picture in our head, let's keep going.

### Tracing under a debugger

Our trace starts at the block scope where we are trying to execute an one liner javascript code:
```cpp
...
    { 
      // Create a string containing the JavaScript source code.
      v8::Local<v8::String> source =
          v8::String::NewFromUtf8Literal(isolate, "'Hello' + ', World!'");

      // Compile the source code.
      v8::Local<v8::Script> script =
          v8::Script::Compile(context, source).ToLocalChecked();

      // Run the script to get the result.
      v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();

      // Convert the result to an UTF8 string and print it.
      v8::String::Utf8Value utf8(isolate, result);
      printf("%s\n", *utf8);
    }
...
```

The first just prepares the provided command into a `utf-8` encoded javascript type string object, placed inside the target V8 isolate. This will later be consumed by or compiler to generate the object code.

#### Compiling

`v8::Script::Compile(context, source)` is where the magic begins. If you trace out the call structure for this function call, [constructor in api.cc](https://source.chromium.org/chromium/chromium/src/+/master:v8/src/api/api.cc). Next, [compiler.cc](https://source.chromium.org/chromium/chromium/src/+/master:v8/src/codegen/compiler.cc;l=2678;drc=e5ea75ba76df2c306f03373a1ca6332898f5b37b) is hit where the function [CompileScriptOnMainThread](https://source.chromium.org/chromium/chromium/src/+/master:v8/src/codegen/compiler.cc;l=2439;drc=e5ea75ba76df2c306f03373a1ca6332898f5b37b) does all the work which is required to get the script compiled and ready to run.

![](https://i.imgur.com/SMLk6OW.png)

This includes the flow hitting [interpreter.cc](https://source.chromium.org/chromium/chromium/src/+/master:v8/src/interpreter/interpreter.cc) and getting interpret. One good way is to use `(gdb) rbreak filename.xx:.` to break on all the functions of the file. This is what the back trace looks like at that instance of time:

![](https://i.imgur.com/RFf7pZ7.png)

#### Running

Now that we have the code compiled and the env mostly setup, the only left out piece in the puzzel is, how does the code actually execute?

We see the run invocation on line 47. This line invokes
[v8::Script::Run](https://source.chromium.org/chromium/chromium/src/+/master:v8/src/api/api.cc;l=2146;drc=3499b8567ac2e8e6ac46c717df495e5ed91b3fc6)
which is reponsible for handling the execution of the code in the provided context.

![](https://i.imgur.com/LqmYdg9.png)

From the `v8::Script::Run`,
[Execution::Call](https://source.chromium.org/chromium/chromium/src/+/master:v8/src/execution/execution.cc;l=459;drc=2e96276c762073eb1e4de8a95bb944bd764f75fb)
is invoked which is responsible for setting up the remaining structures required to execute the interpret code and get back the result.
[Execution::Invoke](https://source.chromium.org/chromium/chromium/src/+/master:v8/src/execution/execution.cc;l=241;drc=8d739b8c2d6b98b5c77b1852923ab2ae1b9fdabf)
is eventually invoked which converts the interpret intermediate bytecode into the platform specific object code using
[GenerateCode](https://source.chromium.org/chromium/chromium/src/+/master:v8/src/execution/simulator.h;l=152;drc=8d739b8c2d6b98b5c77b1852923ab2ae1b9fdabf;bpv=0;bpt=1)
and triggers the invocation of the code.
Providing us the data in the stuctures we require.

### Reflecting on this article

The main target of this post was to explore what main files and functions are touched during the compilation and execution of a simple example. More discussion on what code sections go what are described in [this](https://m4dst4cks.github.io/blog/2020/09/18/V8-Exploitation-Series-Part-3) post.

The main idea to learn is, while tracing have an idea of what file the control might hit and then use `(gdb) rbreak filename:.` or a similar eq to trace on a higher level rather than tracing the control flow line by line.