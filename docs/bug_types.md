# Types of Security Bugs in V8

### General Overview

Based on our previous experiences & John's blog post here, we know that many of the widely used vulnerabilities involve some kind bug which usually leads to an out-of-bounds Read/Write. Now this could be due to:
- Some kind of type confusion, leading to some [checks getting removed](https://abiondo.me/2019/01/02/exploiting-math-expm1-v8/#the-bug)
- Some edge case in the builtins which isn't handled correctly
- .... (other reasons still to discover)

### New Stuff
In this post, we will be looking into Javascript Compiler (V8) related major security issues: [older exploitable ones](https://github.com/tunz/js-vuln-db/tree/master/v8), newer security bugs, many of which are internally fixed [bugs.chromium.org>JS>Compiler](https://bugs.chromium.org/p/chromium/issues/list?q=component%3ABlink%3EJavaScript%3ECompiler%20Type%3D%22Bug-Security%22&can=1). The goal is to notice patterns and figure out leads which can lead to the required future work in the area.

>Note: If you want to look into a broader set of javascript issues in V8, you'll be better off looking at [bugs.chromium.org>JS](https://bugs.chromium.org/p/chromium/issues/list?q=component%3ABlink%3EJavaScript%20Type%3D%22Bug-Security%22&can=1)


### Places bugs usually exploit
- Some edge case in builtins which don't perform bound checks / don't handle the entire range of spec
- Internal Array conversion and length changes which may have missing length checks
- Typer optimizing out checks which are provided as per Javascript spec if it believes that a certain if will never return false

I worked on looking into various bugs that have arisen in the past. V8 is a compiler/Interpreter which is built on top of its compilers (IE CSA/Torque) & Memory management. A small mistake anywhere can make the error bubble up in the later stage of the process. Currently, in a Week, I wasn't able to figure out any higher-level techniques to invoke the security bugs which have been discovered in the past.

However, I feel that doing it from the Javascript level might not be feasible. This is for the following reasons:
- Exploitation of any of these bugs requires you to specifically trigger them. This might involve:
    - Calling a builtin which is vulnerable (builtin bugs)
    - Understanding the internal memory buffer usage of wrapper classes to invoke edge cases in the internal array classes (Internal OOB bugs)
    - figuring out a edge case in Javascript spec VS typer phases to get rid of a critical check (math.expm1)
- All of these bugs types aren't directly related
- Either of these bugs require you to understand the internals of the good chunk of the engine to trigger them effectively

Hence I conclude that rather than testing for bugs from the Javascript level, implementing some kind of runtime check to prevent exploitation would be a better idea. I don't have a lot of experience on what's possible here but maybe looking or addresses in strings or certain patterns which usually don't appear in code and appear a whole lot in memory.

Secondly, the best way forward would be to test the V8 by embedding the function we're trying to test separately, which would break it into chewable size chunks.

#### OLD Bugs
 
Here is a list of exploitable old bungs which I am looking at to find some kind of co-relation between.

1. [CVE-2018-6056](https://github.com/tunz/js-vuln-db/blob/master/v8/CVE-2018-6056.md)
2. [CVE-2018-6064](https://github.com/tunz/js-vuln-db/blob/master/v8/CVE-2018-6064.md)
3. [CVE-2018-6065](https://github.com/tunz/js-vuln-db/blob/master/v8/CVE-2018-6065.md)
4. [CVE-2018-6142](https://github.com/tunz/js-vuln-db/blob/master/v8/CVE-2018-6142.md)
5. [CVE-2018-6143](https://github.com/tunz/js-vuln-db/blob/master/v8/CVE-2018-6143.md)
6. [CVE-2018-16065](https://github.com/tunz/js-vuln-db/blob/master/v8/CVE-2018-16065.md)
7. [CVE-2018-17463](https://github.com/tunz/js-vuln-db/blob/master/v8/CVE-2018-17463.md)