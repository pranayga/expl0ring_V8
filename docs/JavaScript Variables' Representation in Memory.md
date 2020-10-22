# JavaScript Variables' Representation in Memory

## Introduction

So far in this series we have covered a significant number of topics related to understanding the V8 code. The last area we need to explore is the memory layout of data on the heap within a V8 isolate. We need to know how objects are stored, what metadata they include, and how to examine them in memory. We will also cover other structures, but understanding objects is key to understanding the basis of our exploitation primitives.

## Pointer Compression

Early in 2020, V8 made a huge leap in memory usage reduction with the introduction of [pointer compression](https://v8.dev/blog/pointer-compression). Essentially the change reduces 64-bit pointers to 32 bits by storing some base address and then making all pointers relative to that base. The article I just linked is really good, but I'll give the TLDR.

Every variable in JavaScript is stored as a pointer to an object (think C++ object, not JavaScript). You probably have noticed a lot of these while looking in Turbolizer. However, since integers get operated on a lot, and usually with relatively simple operations, we can store the integer directly in the pointer's spot. Previously, in 64-bit processes, this was represented as

```
            |----- 32 bits -----|----- 32 bits -----|
Pointer:    |________________address______________w1|
Smi:        |____int32_value____|0000000000000000000|
```

##### Note: Smi just means "small integer"

Whenever a pointer gets dereferenced, we can check if the LSB is a 0 or 1, and determine if it is really a pointer or an integer. The "w1" at the end of the pointer symbolizes the bits available to store metadata, since all pointer addresses are word-aligned, as well as the 1 that signifies this is a pointer. This space still exists when you reduce the pointer to 32 bits. However, there is a problem for integers. If they are represented with 32 bits, they may end in a 1 and look like a pointer. The fix was to make integers represented by 31 bits, and have the LSB always be 0. Thus the new layout:

```
                        |----- 32 bits -----|
Pointer:                |_____address_____w1|
Smi:                    |___int31_value____0|
```

When looking in memory, remember to subtract 1 from pointers and shift integers right by 1 bit to get the actual values!

## Objects

`objects/objects.h` shows us a list of all object types. Almost everything in JavaScript is an `Object`, and almost everything in V8 is a `HeapObject`. The exceptions are `SMI`s and `TaggedIndex`s, which we just talked about.

In JavaScript, objects have properties (and methods, but let's just consider those properties), and these properties can be created and modified at any point during the lifetime of a script. This dynamic nature means that storing an object in memory can be tricky, because we don't know how to properly allocate for an object when it is initialized. Let's have a discussion about efficiently storing objects.

Every object property has attributes, which are the value of the property, and three flags that indicate if the property is writeable, enumerable, and configurable. For example:

```
var animal1 = {
  type: "Bunny",
  name: "Flopsy",
  weight: 18,
  speak: function() { return "meow"; }
};

console.log(Object.getOwnPropertyNames(animal1));
// type,name,weight,speak

console.log(Object.values(animal1));
// Bunny,Flopsy,18,function() { return "meow"; }

console.log(JSON.stringify(Object.getOwnPropertyDescriptor(animal1, "type")));
// {"value":"Bunny","writable":true,"enumerable":true,"configurable":true}
```

Properties in JavaScript are treated like a dictionary, with the string name as the key. So basically, we need to store this dictionary somewhere. However, we want to increase speed and conserve memory, which will add a little more complexity. In our current understanding, to access an object property we need to look up the property name in the dictionary and then retrieve the value attribute. This wastes space when we have similar objects whose property names are the same, but with different values. Instead of having 2 dictionaries with all of the property names, it would be nice to just have 1 dictionary and store the actual values somewhere else.

### Shapes / Hidden Classes (general name) / Maps (V8 name)

This is accomplished by storing just the values together at the same memory location, and using a "map" to link properties to attributes using _offsets_ as the value that gets looked up. The map pointer gets stored in the object so it can find these offsets... 😕

Okay, even I'm lost, so here's an example: 

```
let obj1 = {};
let obj2 = {};

obj1.x = 5;
obj1.y = 6;
obj2.x = 7;
obj2.y = 8;
```
--------------------------------------------
```

// Naive case structure in memory
obj_1 = {"x": {"value": 5, ...}, "y": {"value": 6, ...}}
obj_2 = {"x": {"value": 7, ...}, "y": {"value": 8, ...}}

// Efficient case structure in memory
obj_case1_dict = {"x": {"offset": 0, ...}, "y": {"offset": 1, ...}}
obj1 = [5, 6, &obj_case1_dict] // not an accurate layout, but we'll fix this soon
obj2 = [7, 8, &obj_case1_dict] // not an accurate layout, but we'll fix this soon
// "..." here represents the enumerable, writable, and configurable flags
```

##### Note: Whether talking about shapes, hidden classes, types, structure, or maps; we are referring to the layout of an object's properties. Articles use names interchangeably because each JS engine uses a different name. V8 uses the term "Map."

You create 2 objects, obj1 and obj2. You give each object properties "x" and "y", to which you assign integer values. In the naive case, the JS engine would create a dictionary for each object. Those dictionaries would both have "x" and "y" as keys. Each key would have property attributes (again, the value and some flags). Looking at the "value" attribute would show the actual integer you stored in that property. Now, in the efficient case, the JS engine creates 1 dictionary that is shared between the objects. It still has "x" and "y" as keys, but the property attributes now hold an offset instead of the actual value. That offset can be used to find the location of the value relative to the object's location in memory. This becomes even more efficient as we create more objects with a similar shape.

The important takeaway here is that objects have pointers to a map/shape/whatever you want to call it, which is a dictionary for its properties. The property names are key values for the attributes that contain the offset within the object's memory structure where the actual property value can be found. Many objects can share the same map.

...well 1 more thing, sorry. The object doesn't exactly point to a map as I've been describing it. Instead it points to the end of a transition tree, which is a map entry in a chain of entries. Think of this chain exactly like the structure I've been describing, except with properties linked by pointers instead of being in contiguous memory like most dictionaries. It turns out that as properties are added to an object, a new entry is created with a pointer to the last, so maps aren't really dictionaries anymore, they're more like linked lists. This is a called a tree because multiple objects can share some properties, but may branch out as they add different properties. When a property access is made, this chain is walked backwards until the desired property is found. See this stolen image for some clarity:

![Transition Tree](https://i.imgur.com/nw5XMkc.png)

When we talk about maps, we are actually referring to this structure, which is easy to think about as more of a dictionary than a linked list. Regardless, memorizing the memory layout of maps is much less important than knowing that Objects contain a map pointer.

Maps also contain a lot of information about an object. It's not all important to know, but here's a nice little diagram from `objects/map.h`:

```
// All heap objects have a Map that describes their structure.
//  A Map contains information about:
//  - Size information about the object
//  - How to iterate over an object (for garbage collection)
//
// Map layout:
// +---------------+------------------------------------------------+
// |   _ Type _    | _ Description _                                |
// +---------------+------------------------------------------------+
// | TaggedPointer | map - Always a pointer to the MetaMap root     |
// +---------------+------------------------------------------------+
// | Int           | The first int field                            |
//  `---+----------+------------------------------------------------+
//      | Byte     | [instance_size]                                |
//      +----------+------------------------------------------------+
//      | Byte     | If Map for a primitive type:                   |
//      |          |   native context index for constructor fn      |
//      |          | If Map for an Object type:                     |
//      |          |   inobject properties start offset in words    |
//      +----------+------------------------------------------------+
//      | Byte     | [used_or_unused_instance_size_in_words]        |
//      |          | For JSObject in fast mode this byte encodes    |
//      |          | the size of the object that includes only      |
//      |          | the used property fields or the slack size     |
//      |          | in properties backing store.                   |
//      +----------+------------------------------------------------+
//      | Byte     | [visitor_id]                                   |
// +----+----------+------------------------------------------------+
// | Int           | The second int field                           |
//  `---+----------+------------------------------------------------+
//      | Short    | [instance_type]                                |
//      +----------+------------------------------------------------+
//      | Byte     | [bit_field]                                    |
//      |          |   - has_non_instance_prototype (bit 0)         |
//      |          |   - is_callable (bit 1)                        |
//      |          |   - has_named_interceptor (bit 2)              |
//      |          |   - has_indexed_interceptor (bit 3)            |
//      |          |   - is_undetectable (bit 4)                    |
//      |          |   - is_access_check_needed (bit 5)             |
//      |          |   - is_constructor (bit 6)                     |
//      |          |   - has_prototype_slot (bit 7)                 |
//      +----------+------------------------------------------------+
//      | Byte     | [bit_field2]                                   |
//      |          |   - new_target_is_base (bit 0)                 |
//      |          |   - is_immutable_proto (bit 1)                 |
//      |          |   - unused bit (bit 2)                         |
//      |          |   - elements_kind (bits 3..7)                  |
// +----+----------+------------------------------------------------+
// | Int           | [bit_field3]                                   |
// |               |   - enum_length (bit 0..9)                     |
// |               |   - number_of_own_descriptors (bit 10..19)     |
// |               |   - is_prototype_map (bit 20)                  |
// |               |   - is_dictionary_map (bit 21)                 |
// |               |   - owns_descriptors (bit 22)                  |
// |               |   - is_in_retained_map_list (bit 23)           |
// |               |   - is_deprecated (bit 24)                     |
// |               |   - is_unstable (bit 25)                       |
// |               |   - is_migration_target (bit 26)               |
// |               |   - is_extensible (bit 28)                     |
// |               |   - may_have_interesting_symbols (bit 28)      |
// |               |   - construction_counter (bit 29..31)          |
// |               |                                                |
// +****************************************************************+
// | Int           | On systems with 64bit pointer types, there     |
// |               | is an unused 32bits after bit_field3           |
// +****************************************************************+
// | TaggedPointer | [prototype]                                    |
// +---------------+------------------------------------------------+
// | TaggedPointer | [constructor_or_backpointer_or_native_context] |
// +---------------+------------------------------------------------+
// | TaggedPointer | [instance_descriptors]                         |
// +****************************************************************+
// ! TaggedPointer ! [layout_descriptors]                           !
// !               ! Field is only present if compile-time flag     !
// !               ! FLAG_unbox_double_fields is enabled            !
// !               ! (basically on 64 bit architectures)            !
// +****************************************************************+
// | TaggedPointer | [dependent_code]                               |
// +---------------+------------------------------------------------+
// | TaggedPointer | [prototype_validity_cell]                      |
// +---------------+------------------------------------------------+
// | TaggedPointer | If Map is a prototype map:                     |
// |               |   [prototype_info]                             |
// |               | Else:                                          |
// |               |   [raw_transitions]                            |
// +---------------+------------------------------------------------+
```

### Arrays

Arrays are just a specialized type of object. The `Array` "class" is a "subclass" of `Object` (class is in parentheses here because JavaScript doesn't really have classes, but I can't think of a better word...). For example, an array's length is just a property. It is stored in a dictionary with the key `"length"`, and the property attribute `value` is the actual length of the array. 

All items in the array are stored with a key that is the string representation of the index. Well, not exactly like an object. Arrays are unique in that all of the values within an array are writable, enumerable, and configurable (unless, of course you change them, but that's less relevant for exploitation). They also use numeric indices to map values, meaning that a large number of properties are just sequential numbers. Therefore, V8 stores array properties, such as the length, in a typical map; however, they also include a backing store. This backing store points to the elements that are actually in the array, which is more efficient than using the map.

##### Note: I've also seen other names used in place of "backing store." If you see something talking about a "store" or list of elements, it's probably referring to this.

As it turns out, arrays and objects are _so_ closely bound that their memory structure is the same. Objects that have properties with numeric keys will also have those values stored in a backing store.

![Memez](https://raw.githubusercontent.com/m4dSt4cks/m4dst4cks.github.io/master/public/img/array_meme.jpg)

Another important aspect to arrays in V8 is how their elements are stored. If we have a simple array like `arr1 = [1, 2, 3]` it makes sense to store that linearly in memory. However, if we do something like

```
arr1 = new Array(100);
arr1[0] = 0;
arr1[99] = 99;
```

then we have to decide whether to store this as 100 linear values, marking only slots 0 and 99 as valid, or to store only our 2 values with their index. We can also make an array like this:

```
arr1 = new Array(100);
arr1[0] = 0;
arr1[50] = "50";
arr1[99] = 99.9;
```

Now we have to remember even more about the array since the types of values are different. V8 describes arrays based on the "elements kinds." `objects/elements-kind.h` lists all of the different kinds of arrays. The type of array is relevant for the memory layout as well as the optimizations that can be performed. Much like our discussion on typing before, this is an area where a mismatch in assumptions at compile time vs. actual values at runtime has caused many Turbofan vulnerabilities. I will bring this concept up again in our case studies, but if you are interested in learning more about how V8 handles array typing you should look here: [blog](https://v8.dev/blog/elements-kinds) ([slides](https://slidr.io/mathiasbynens/v8-internals-for-javascript-developers#1)).

To summarize, Objects have a map pointer that shows how named properties are stored. The properties will most likely be stored out-of-line, so the map will actually point to offsets in a different memory region. Objects also have a backing store that contains the values for properties that are described by numeric names. The backing stored can also be stored away from an object, or potentially directly after the object in memory. We'll talk about the exact memory layout later on. In case anything hasn't made total sense so far, I recommend reading [this article](https://v8.dev/blog/fast-properties) as an additional source to understand how V8 deals with Objects (note that they use the term "hidden classes" to describe Maps).

### Inline caches

As you may have gathered, the time it takes to look up a property's value can be lengthy. V8 reduces this time by storing the result within the actual instructions of that lookup. [_picture back to the future meme here_] When object property lookup bytecode instructions are generated, they have room in the binary for data to be stored. At runtime, these slots will be overwritten with the value's offset (as defined by the Map) and the actual value of that property. The code for the IC is in the `src/ic` folder.

I wanted to mention inline caches because they affect some instructions and change the execution flow from what may be expected. I didn't think this topic was all that important for exploitation; however, [a recent bug](https://bugs.chromium.org/p/chromium/issues/detail?id=1104608) proved me wrong. Although there was not a corresponding exploit written, it was marked as a security issue.

### Inheritance and Prototypes

Another thing that I believe is important to understanding these concepts is to know a little about JavaScript and Object-Oriented Programming. We have been talking about objects in general, as well as the JavaScript Object. It can get confusing, so I wanted to mention that you should recognize that creating a new object will inherit the properties of its prototype. For example, arrays inherit the properties from the JavaScript Object, its prototype. They then extend Object by adding more properties that are specific to arrays. When you create an instance of an array, that has a prototype of the Array object, which you extend by adding elements. This creates a chain of prototypes that are searched when an object property lookup is made. This may be relevant if you are trying to explore the finer details of creating an object.

I'm sure I have JavaScript buffs cringing at my horrible explanation. I apologize if anything in this section is incorrect, JavaScript is not my strong suit and this was more an attempt for me to learn through writing. Please look through the JavaScript docs for the correct information.

## Exploring the Object Layout

So, I've done a lot of explaining to try and detail what a JavaScript Object looks like in C++ memory, but I haven't provided a diagram. I'll do that now, but just know it may change in the future. I'll only being doing a simple example with arrays, finding the layouts of more complex objects is an exercise left to the reader.

Viewing the structure of variables in V8's memory is very simple, and can even be done in the d8 REPL! For this, we will pass the `--allow-natives-syntax` flag to d8 and call the `%DebugPrint()` function. The only problem with this method is that we won't get a great idea of the exact memory layout. Thankfully, we can also use GDB to help out.

There is a useful file for debugging in the V8 install directory (`tools/gdbinit`). You can include this by placing it in your home directory as `.gdbinit`. You can also add the code inside to your existing `.gdbinit` file if you already have other modifications, or simply add this line to it:

```
source <path to v8>/tools/gdbinit
```

You can see all of the newly added commands by looking for lines that start with "define" in this file. For example, the `job` command will essentially run what `%DebugPrint` does when you pass it a `TaggedPtr`.

##### Note: I like to use pwndbg instead of GDB; however, it seems there is some sort of conflict between it and V8 for most of the commands. `job` will still work, but most others may require you to use vanilla GDB or another debugger.

Once you have your debugger of choice setup, you can try out a script like this one:

```
//////////////////// script.js ////////////////////
function testing() {
  var a = new Array(1.1, 1.2, 1.3);
  var b = new Array(2.1, 2.2, 3.3);
  var c = new Array(1, 2, 3);
  
  delim = "-".repeat(50);
  console.log(delim + delim);
  console.log(delim + " A " + delim);
  %DebugPrint(a);
  console.log(delim + " B " + delim);
  %DebugPrint(b);
  console.log(delim + " C " + delim);
  %DebugPrint(c);
  console.log(delim + delim);
  return 0;
}

testing();

while(1){} // <- "debugger;" doesn't work so I use this to attach

//////////////////// command line ////////////////////
gdb d8
r --allow-natives-syntax script.js
Ctrl+C
```

The calls to `%DebugPrint(obj)` will output the addresses we need, and you can get more details by using `%DebugPrintPtr(tagged_ptr)` or by calling your newly added built-ins from your debugger's terminal. I took the pointer from each object, subtracted 1, and used GDB to examine the memory, which provided me a layout like this:

```
+--------------------+    <- begin first allocation (a)
-       map ptr      -
+--------------------+
-   properties ptr   -
+--------------------+
-  backing store ptr -
+--------------------+
-   length of array  -
+--------------------+    <- begin second allocation (a's backing store)
-   length of store  -
+--------------------+
-        a[0]        -
+--------------------+
-        a[1]        -
+--------------------+
-        a[2]        -
+--------------------+    <- begin third allocation (b)
...
```

Notice how the arrays are laid out sequentially, with the elements stored in-between. This will be very important to our next post on building exploitation primitives.

##### Side note: Jeremy Fetiveau made a [tool](https://github.com/JeremyFetiveau/debugging-tools) that helps with this, although you may need to make updates for newer versions of V8. You can also use lldb, and there are some helpful files for that in the V8 source code. A few different approaches have been described in depth [here](https://joyeecheung.github.io/blog/2018/12/31/tips-and-tricks-node-core/).

## Where is the Code?

It's nice to be able to see the memory layout and theorize how allocations are being made. However, I wanted to find where the code actually allocates these structures. We are looking for the "New" keyword since V8 uses automatic memory management. The `src/objects` folder has several `js-[].cc` files which seem to be relevant, and contain references to regexps, proxies, etc.; which I know are different types of [Objects](https://www.w3schools.com/js/js_object_definition.asp). This led me to `js-objects.h` which had a definition for `class JSObject`. In `js-objects.cc` there is a definition `MaybeHandle<JSObject> JSObject::New(Handle<JSFunction> constructor, Handle<JSReceiver> new_target, Handle<AllocationSite> site)` which seems to be where the actual allocation happens. Unfortunately, most of the actual creation seems like magic to me, but this is where I believe we would look. However, my partner in this series mentioned that this process is also influenced by Torque, which  makes sense considering how many array operations it performs. His posts may be able to shed a little more light on this topic.
                                    
Taking a deeper dive into the code would be useful for understanding how different scenarios affect the heap layout. For example, what happens when we declare variables with `let` vs `var`. How do the allocations for `Holey` arrays differ from `packed` arrays, for both the object and backing store? For now, I will rely on runtime analysis to provide those answers. However, when it comes to reviewing the security of an application, an in-depth understanding of this code would really help. I at least hope I have provided a baseline to answer these questions and covered enough to get to the next post, which will involve building exploitation primitives.

## Conclusion

As always, no one explains these concepts better than the V8 team. If you have 15 minutes, I'd recommend watching [this video](https://youtu.be/5nmpokoRaZI?t=397) (starting at 6:30). If the presentation makes total sense, that's great! If not, I'd say look through this post one more time. It will be absolutely key for understanding our case studies. There were several more of their articles I linked throughout this post which will help nail down these concepts. I hope that I was able to combine all the knowledge they've shared into the linear flow of this series so that it is helpful for learning V8 exploitation.

### Glossary

Object - A "thing" in JavaScript (more formally, a collection of properties)

Object Property - Some characteristic of an object

Object Property Attribute/Descriptor - Information about the property; specifically, its value and enumerable/writable/configurable flags

Map - V8's name for a dictionary that holds offsets to where values are stored in an object

Transition Tree - How maps are actually stored in V8, with pointers between properties instead of being stored linearly in memory, this is what is usually meant when people talk about maps

Array - Another "thing" in JavaScript that is a type of Object, but contains values with numeric indices

Backing Store - The location in memory where an array's elements are stored

Elements - The values within an array, or better stated as object properties that have numeric names

Elements Kinds - The different ways that an array can store a group of elements based on their types and spacing

## References

[JavaScript engine fundamentals: Shapes and Inline Caches - Mathias Bynens](https://mathiasbynens.be/notes/shapes-ics)

[Inheritance and the prototype chain - MDN Web Docs](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Inheritance_and_the_prototype_chain)
