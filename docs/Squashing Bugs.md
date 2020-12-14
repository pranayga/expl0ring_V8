# Squashing Bugs

## Introduction

There are several topics related to V8 security that I have not yet discussed. This post will cover some areas that are related to bug hunting, such as security mechanisms already employed by V8 and Chrome. The intent is to provide some more resources for people interested in V8 security research.

## Studying N-Days

Besides reading through the codebase, many times the best way to get involved is through practice. Studying past vulnerabilities can help lead to discovering new ones. And when one bug is found, there's a decent chance that similar vulnerabilities exist. Below are some ways to find disclosed bugs in V8 that can help with finding future ones.

### Bug Trackers

While V8 has its own [list of bugs](https://bugs.chromium.org/p/v8/issues/list), many are also posted within the larger Chromium [tracking list](https://bugs.chromium.org/p/chromium/issues/list). This is where anyone can report bugs to Google, which every takes totally seriously and never posts any joke bugs. 

![Serious Security Bug](https://raw.githubusercontent.com/m4dSt4cks/m4dst4cks.github.io/master/public/img/security_bug.PNG)

Thankfully, there are several filters to locate security-related bugs. Combining the filters isn't always intuitive, but one that I usually go with is `Type="Bug-Security" component=Blink>JavaScript>Compiler`, and this is what you'll [see](https://bugs.chromium.org/p/chromium/issues/list?q=Type%3D%22Bug-Security%22%20component%3DBlink%3EJavaScript%3ECompiler&can=1). There are usually dropdowns for all the different kinds of filters; however, I have found that when you want to combine filters it is best to do so manually by copying the strings produced by using a filter individually (like in the example I just gave). An important thing to note here is that, if a bug has some security impact, then it will be restricted until 14 weeks after it has been closed. 

Bug reports are very helpful for getting started in V8 security research. Many times, these reports contain PoCs, explanations of the code base, and a discussion about how the bug was caused and how it will be fixed. When new exploitation techniques are discovered, they are usually discussed in the most detail in one of these reports.

### Chrome Releases

If you don't have time to regularly check the bug trackers, the official [Chrome Releases](https://chromereleases.googleblog.com/) page details all of the major vulnerabilities that were patched over the past 6 weeks. This is generally easier to search for bugs in V8 because it contains a concise list with short descriptions for each vulnerability. However, it only publishes bugs once they have been patched, and at larger time intervals.

### Git Changelog

Meanwhile, if you have a lot of time to search for the latest patches, you should look through the open-source V8 [log](https://chromium.googlesource.com/v8/v8.git/+log) or the [GitHub mirror](https://github.com/v8/v8/commits/master). From your terminal you can run `git log origin/master` to look through the master branch log, even if you are on an old commit locally. Most of the code changes are non security-related. However, commits often have a bug ID embedded in the commit message. This means that you can cross-reference IDs from the bug tracker and the changelog to see if a bug is a security issue or not. The interesting part about this is that some bugs are found, and patched, publicly before applications, like Chrome, implement these patches. "In effect, there’s a window of opportunity for attackers ranging from a couple days to weeks in which the vulnerability details are practically public yet most of the users are vulnerable and cannot obtain a patch." [-Exodus Blog](https://blog.exodusintel.com/2019/04/03/a-window-of-opportunity/)

## Posts

The V8 team manages a [twitter account](https://twitter.com/v8js) as well as a [blog](https://v8.dev/blog). Both of these post information about changes in V8, usually from a development perspective, but changes often mean new features that need to be looked into. These blogs often add a lot of explanation that the code does not readily provide.

While it is not V8-focused, the [Google Project Zero Blog](https://googleprojectzero.blogspot.com/) often does research on browser vulnerabilities. They have written [a](https://googleprojectzero.blogspot.com/2019/05/trashing-flow-of-data.html) [few](https://googleprojectzero.blogspot.com/2019/04/virtually-unlimited-memory-escaping.html) [articles](https://googleprojectzero.blogspot.com/2020/02/escaping-chrome-sandbox-with-ridl.html) on Chrome and V8. These go into great depth on browser exploitation and they will likely release similar posts in the future.

## Fuzzing

When you have a piece of software as large and complex as a JavaScript engine, you need to implement fuzzing to cover the huge number of potential cases that may crash the system. The V8 team offers several resources for anyone to implement pre-made fuzzers or introduce new ones.

[ClusterFuzz](https://blog.chromium.org/2012/04/fuzzing-for-security.html) is Chrome's main fuzzer, and it is has different components that test specific features, like [JavaScript](https://github.com/v8/v8/tree/master/tools/clusterfuzz/js_fuzzer). Even bugs that are discovered through manual code review are fed into ClusterFuzz to assess the range of affected Chrome versions and help with patching. It is already well integrated into V8, and can take advantage of address sanitization (one of Google many [sanitizers](https://github.com/google/sanitizers)).

Some fuzzing products from outside Google have been introduced as well, such as [Fuzzilli](https://saelo.github.io/papers/thesis.pdf) from Samuel Groß. While it was not built specifically for V8, you can see how it was ported [in the GPZ repo](https://github.com/googleprojectzero/fuzzilli). It has already found bugs in V8, such as [1072171](https://sensepost.com/blog/2020/the-hunt-for-chromium-issue-1072171/). Another recently introduced fuzzer, [DIE](https://github.com/sslab-gatech/DIE), from Georgia Tech's lab, has also discovered multiple vulnerabilities. Both have enough documentation to get started with relatively little work. There is also a [very good walkthrough](https://fuzzinglabs.com/fuzzing-javascript-wasm-dharma-chrome-v8/) from Patrick Ventuzelo on fuzzing Liftoff using Dharma. 

Finally, the [Chromium blog](https://chromium.googlesource.com/chromium/src/+/master/testing/libfuzzer/getting_started.md) has a helpful guide for getting started with fuzzing and creating new testing methods. Specifically [this guide](https://chromium.googlesource.com/v8/v8/+/refs/heads/master/test/fuzzer/README.md) outlines the process for introducing a new fuzzer to V8, much like how they ported a new WebAssembly fuzzer.

## Checks

When you look through the V8 code, you will notice a lot of CHECK and DCHECK macros. CHECK statements assert conditions that would have security implications if not met. These will force the browser to crash in order to prevent more serious consequences. DCHECK statements are only present in the debug build, and are meant to validate pre-conditions and post-conditions that should always be true. These statements can help fuzzers locate errors more closely to the source. For example, a type confusion may not cause a crash. Even if it does, it may take several hours to track down where the actual mistake is located in the code. However, DCHECKS can find where an assumption was broken and narrow the search scope.

## Conclusion

There are a wide variety of security mechanisms built into V8; including fuzzing, sanitization, manual code review, compartmentalization, and a [recently increased](https://security.googleblog.com/2020/12/announcing-bonus-rewards-for-v8-exploits.html) bug bounty program. The [Security Brag Sheet](https://www.chromium.org/Home/chromium-security/brag-sheet) has a list of protections put in place by the Chrome team for the entire browser. Some of these are specific to V8, but many are general techniques to improve software. Anyone who is interested in improving V8 security should look into these areas. This post hopefully introduced some aspects of bug tracking and hunting that will help with starting points for additional bug discovery.

## References

[A window of opportunity: exploiting a Chrome 1day vulnerability by Exodus Intelligence](https://blog.exodusintel.com/2019/04/03/a-window-of-opportunity/)

[Security Brag Sheet](https://www.chromium.org/Home/chromium-security/brag-sheet)

[Chromium Blog: Fuzzing for Security](https://blog.chromium.org/2012/04/fuzzing-for-security.html)

[Chromium C++ style guide](https://chromium.googlesource.com/chromium/src/+/HEAD/styleguide/c++/c++.md#check_dcheck_and-notreached)

[GTAC 2013: AddressSanitizer, ThreadSanitizer and MemorySanitizer -- Dynamic Testing Tools for C++](https://www.youtube.com/watch?v=Q2C2lP8_tNE)
