# Implementing String Taint tracking in Chromium - V8 (II)
> Documentation is a love letter that you write to your future self. - Damian Conway

Commit SHA: 600241ea64

#### 6. Adjusting AST
```
 src/ast/ast-value-factory.cc                       |    2 +-
 src/ast/ast.cc                                     |    8 +
 src/ast/ast.h                                      |   14 +-
 src/base/logging.cc                                |    1 +

```

#### 7. Making changes to internal memory structures

```
 src/d8.cc                                          |    3 +-
 src/debug/debug-coverage.cc                        |    4 +-
 src/debug/liveedit.cc                              |    3 +-
 src/deoptimizer.h                                  |    2 +-
 src/execution.cc                                   |   17 +-
 src/execution.h                                    |    3 +-
 src/extensions/externalize-string-extension.cc     |   24 +-
 src/external-reference-table.cc                    |    9 +
 src/external-reference-table.h                     |    5 +-
 src/external-reference.cc                          |    6 +
 src/external-reference.h                           |    2 +
 src/flag-definitions.h                             |   49 +
 src/frames.cc                                      |   41 +
 src/frames.h                                       |   19 +
 src/globals.h                                      |   56 +
 src/heap/factory.cc                                |  137 +-
 src/heap/factory.h                                 |    1 +
 src/heap/heap-inl.h                                |    1 +
 src/heap/heap.cc                                   |    2 +-
 src/interpreter/bytecode-array-builder.cc          |    6 +-
 src/interpreter/bytecode-array-builder.h           |   19 +-
 src/interpreter/bytecode-array-writer.cc           |    3 +-
 src/interpreter/bytecode-generator.cc              |  119 +-
 src/interpreter/bytecode-generator.h               |    5 +
 src/interpreter/bytecode-source-info.h             |   34 +-
 src/isolate.cc                                     |   35 +-
 src/isolate.h                                      |    7 +
 src/json-parser.cc                                 |   25 +-
 src/json-parser.h                                  |    1 +
 src/json-stringifier.cc                            |   51 +-
 src/objects-inl.h                                  |   15 +-
 src/objects.cc                                     |  131 +-
 src/objects.h                                      |    9 +-
 src/objects/code.h                                 |    1 +
 src/objects/free-space-inl.h                       |    5 +
 src/objects/js-objects.h                           |    4 +-
 src/objects/name.h                                 |    8 +-
 src/objects/scope-info.cc                          |   55 +-
 src/objects/scope-info.h                           |    6 +
 src/objects/shared-function-info-inl.h             |    1 +
 src/objects/shared-function-info.h                 |    3 +
 src/objects/string-inl.h                           |   35 +
 src/objects/string-table.h                         |    3 +-
 src/objects/string.h                               |   22 +-
 src/parsing/parse-info.cc                          |    3 +
 src/parsing/parse-info.h                           |   11 +
 src/parsing/parser.h                               |    3 +
 src/roots.h                                        |    1 +
 src/runtime/runtime-compiler.cc                    |    6 +
 src/runtime/runtime-function.cc                    |    2 +-
 src/runtime/runtime-internal.cc                    |  241 ++
 src/runtime/runtime-regexp.cc                      |   25 +
 src/runtime/runtime-scopes.cc                      |   56 +-
 src/runtime/runtime-strings.cc                     |   74 +-
 src/runtime/runtime.h                              |   25 +-
 src/snapshot/code-serializer.cc                    |   11 +-
 src/snapshot/deserializer.cc                       |    6 +
 src/snapshot/deserializer.h                        |    1 +
 src/snapshot/natives.h                             |    3 +-
 src/snapshot/serializer.cc                         |    9 +
 src/snapshot/snapshot-source-sink.cc               |    1 +
 src/snapshot/snapshot-source-sink.h                |   14 +-
 src/source-position-table.cc                       |   10 +-
 src/source-position-table.h                        |   21 +-
 src/string-builder-inl.h                           |   45 +-
 src/string-builder.cc                              |   32 +-
```

#### X. Taint Tracking Logic

```
 src/taint_tracking-inl.h                           |  288 ++
 src/taint_tracking.h                               |  489 +++
 src/taint_tracking/ast_serialization.cc            | 4191 ++++++++++++++++++++
 src/taint_tracking/ast_serialization.h             |  700 ++++
 src/taint_tracking/capnp-diff.patch                |   27 +
 src/taint_tracking/log_listener.h                  |   18 +
 src/taint_tracking/object_versioner.cc             |  509 +++
 src/taint_tracking/object_versioner.h              |  179 +
 src/taint_tracking/protos/ast.capnp                |  765 ++++
 src/taint_tracking/protos/logrecord.capnp          |  351 ++
 src/taint_tracking/symbolic_state.cc               |  859 ++++
 src/taint_tracking/symbolic_state.h                |  391 ++
 src/taint_tracking/taint_tracking.cc               | 3080 ++++++++++++++
 src/taint_tracking/third_party/picosha2.h          |  395 ++
 src/unoptimized-compilation-info.cc                |    1 +
 src/unoptimized-compilation-info.h                 |    5 +
 src/uri.cc                                         |  157 +-
 src/v8.cc                                          |    2 +
 src/wasm/baseline/liftoff-compiler.cc              |   10 +-
 test/cctest/BUILD.gn                               |    1 +
 test/cctest/cctest.h                               |    3 +-
 test/cctest/heap/test-external-string-tracker.cc   |    3 +-
 test/cctest/heap/test-heap.cc                      |    6 +-
 .../bytecode_expectations/ContextVariables.golden  |    6 +-
 .../bytecode_expectations/CreateArguments.golden   |    8 +-
 .../bytecode_expectations/Delete.golden            |    6 +-
 .../bytecode_expectations/ForAwaitOf.golden        |   13 +-
 .../interpreter/bytecode_expectations/ForIn.golden |   20 +-
 .../interpreter/bytecode_expectations/ForOf.golden |   11 +-
 .../GlobalCountOperators.golden                    |    3 +-
 .../bytecode_expectations/GlobalDelete.golden      |    3 +-
 .../IIFEWithOneshotOpt.golden                      |   83 +-
 .../IIFEWithoutOneshotOpt.golden                   |   38 +-
 .../bytecode_expectations/LoadGlobal.golden        |  386 +-
 .../bytecode_expectations/LookupSlotInEval.golden  |    3 +-
 .../bytecode_expectations/Modules.golden           |    5 +-
 .../bytecode_expectations/PropertyCall.golden      |  412 +-
 .../bytecode_expectations/PropertyLoads.golden     |  404 +-
 .../bytecode_expectations/PropertyStores.golden    |  782 ++--
 .../bytecode_expectations/StandardForLoop.golden   |    6 +-
 .../bytecode_expectations/StoreGlobal.golden       |  773 ++--
 test/cctest/interpreter/source-position-matcher.cc |    3 +-
 test/cctest/parsing/test-scanner-streams.cc        |    2 +
 test/cctest/test-api.cc                            |   86 +-
 test/cctest/test-heap-profiler.cc                  |    3 +-
 test/cctest/test-log.cc                            |    3 +-
 test/cctest/test-parsing.cc                        |    3 +-
 test/cctest/test-regexp.cc                         |    3 +-
 test/cctest/test-serialize.cc                      |   16 +-
 test/cctest/test-strings.cc                        |   18 +-
 test/cctest/test-taint-tracking.cc                 | 2324 +++++++++++
 test/cctest/test-types.cc                          |    2 +-
```