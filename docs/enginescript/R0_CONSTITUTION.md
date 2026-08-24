# EngineScript Constitution (Milestone R0)

**Language Name:** EngineScript  
**Source File Extension:** `.e2s`  
**Language Specification Version:** 1.0.0  
**Target Backend:** C++20 (Clang / MSVC / GCC)  

---

## 1. Goals and Non-Goals

### Goals
1. **Safety & Clarity:** Statically typed, object-oriented gameplay language transpiling directly to clean C++20.
2. **First-Class Engine Concepts:** Built-in decorators for Dependency Injection (`@inject`), ECS component binding (`@component`, `@optional`), and Editor Inspector reflection (`@expose`, `@range`).
3. **Structured Concurrency:** Native `async`/`await` powered by C++20 coroutines, bound to `TaskScope` tied to Entity and Scene lifecycles.
4. **Deterministic Hot Reload:** Dynamic `GameModule` hot reload with property snapshots, DI reinjection, and explicit fail-stop behavior without migrating raw pointers.
5. **Tooling First:** VSCode LSP server using the exact same frontend (lexer, parser, typechecker, diagnostics) as the build compiler.

### Non-Goals
1. Not a general-purpose systems programming replacement for C++.
2. No raw pointers, pointer arithmetic, or unchecked manual `new`/`delete`.
3. No untyped `any` or dynamic Lua-like tables.
4. No embedded in-engine script editor; all code is authored in external editors (e.g. VSCode) within the project's `Source/` directory.
5. No cross-module C++ exceptions.

---

## 2. Type System & Semantics

### 2.1 Primitives
- `Bool`: Boolean `true`/`false`.
- Integers: `Int8`, `Int16`, `Int32` (alias `Int`), `Int64`, `UInt8`, `UInt16`, `UInt32` (alias `UInt`), `UInt64`.
- Floating Point: `Float` (alias `Float32`), `Float64`.
- Core Value Types: `String`, `Vec2`, `Vec3`, `Vec4`, `Color`, `Duration` (e.g., `500.ms`, `2.seconds`).
- Engine Handles: `Entity`, `Asset<T>`.

### 2.2 Objects & Memory Ownership
- `struct`: Value semantics. Stack allocated or inlined in parent objects. Copyable.
- `class`: Reference semantics. Managed via engine reference-counted handle (`Ref<T>`).
- `Weak<T>`: Weak reference to break cycles.
- `behaviour`: Engine-owned gameplay script component attached to an `Entity`. Cannot be instantiated with `new`. Owned and managed by the Scene ECS and `GameModule`.
- ECS Components (`@component`): Validated typed entity handles.

### 2.3 Nullability & Error Propagation
- Nullable types are declared with `T?`.
- Non-nullable types cannot hold `null`.
- Optional chaining `obj?.prop` and null coalescing `a ?? b` are supported.
- Expected failures use `Result<T, E>`.

### 2.4 Collections
- `Array<T>`: Dynamic resizable sequence with deterministic ordering.
- `FixedArray<T, N>`: Fixed-size buffer.
- `Map<K, V>`: Deterministic ordered key-value map.
- `Set<T>`: Deterministic ordered set.

### 2.5 Generics
- Monomorphized ahead of time into native C++ templates.
- Generic functions, structs, and classes with type parameter constraints.
