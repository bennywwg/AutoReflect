# AutoReflect
Automatic C++ reflection code generation using clang

To test the functionality of this program, set up the test repository, which will download this one as a submodule:

https://github.com/bennywwg/AutoReflectTest

## How it works:
AutoReflect uses clang to generate an abstract syntax tree of a C++ source file, parses that AST, then generates reflection code based off of class fields. The primary use is for automatically serializing and deserializing structs, which is a capability that has largely eluded C++ despite being present in almost every other language.

Here's an example of an incredibly useful function, `Log`:
```
template<typename T>
void Log(T const& Val) {
    Serializer Ser;
    SerializeFields(Ser, Val);
    std::cout << Ser.Data << std::endl;
}
```

Any serializable class can be passed into this function, and it will be formatted and printed to stdout!

Class supporting auto serialization are defined like this:
```
namespace AutoReflect {
    class Person {
    public:
        int Age;
        std::string Name;
    };
}
```
A reference to a `Person` can now be passed into the functions `Serialize` and `Deserialize`, which will produce or consume an `nlohmann::json`.
```
void main(int argc, char** argv) {
  Person p { 45, "Robert" };
  
  Log(p); // {"Age":45,"Name":"Robert"}
}
```

## Features:
- Nested classes and namespaces are fully supported, with one caveat listed below
- As template are a first class feature in C++, and also work in AutoReflect!
```
namespace AutoReflect {
    template<typename T>
    class Vec {
    public:
        T x;
        T y;
        T z;
    };
}

void main(int argc, char** argv) {
  Vec<int> v = { 1, 2, 3 };
  
  Log(v); // {"x":1,"y":2,"z":3}
}
```

## Limitations
Pointer, const, and reference types are untested, but they certainly won't produce good results. Don't use them as fields in `AutoReflect` classes.

Reflection definitions are generated in `{YourFileName}.generated.inl`. All your reflectable classsed must be declared before that file is included, which somewhat limits the flexibility of code layout.

Only classes are supported, not structs. Limited support for STL containers is provided.

Nested templates are not supported, and are impossible to implement in a decent way. The reason for this is that the library relies heavily on template inference. Inferring template parameters on nested classes is simply not supported. For example:
```
template<int A> struct Outer { template<int B> struct Inner { }; };

template<int A, int B>
void Foo(Outer<A>::Inner<B>) { ... }
```

Is not supported in C++, and therefore AutoReflect cannot do anything useful with these types of classes.

## Requirements:
- Linux-like environment with modern C++ compiler
- [cmake](https://cmake.org/)
- [clang](https://clang.llvm.org/)
