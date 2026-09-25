# Glossary

Terms a reader meets in compiler diagnostics and elsewhere in this book.

**Auto-clone** — When a move-out-of-borrow or a use-after-move would
otherwise be a hard error, the compiler inserts a deep `<T>_clone()` call
and emits a warning. The programmer can then add an explicit `.clone()` or
restructure to avoid the copy.

**Bootstrap** — The chain that builds komp from nothing but a C compiler and
a seed. The seed is a released komp and kflatc as C, pinned by
`bootstrap/stage0.toml`; it can compile the current KFlat source to produce a
new binary, which then produces identical C output — the fixpoint.

**Box** — A unique-ownership heap pointer (`Box<T>`). Allocates on creation,
drops the pointee and frees on scope exit. Unlike `Ptr<T>`, the compiler
tracks ownership and inserts drops.

**Fixed-point / fixpoint** — A property of a self-hosting compiler: the C
output of stage N (compiled by the binary from stage N-1) is byte-identical
to the C output of stage N+1 (compiled by the binary from stage N). A
passing fixpoint proves the compiler can reproduce itself.

**Glue** — Compiler-generated helper functions for a struct or enum:
drop (`<T>_drop`), clone (`<T>_clone`), retain, and optionally
new/drop-inner. Synthesised based on the fields' types and whether the
type implements `Drop`.

**Interface (`.kfi`)** — A JSON file produced during separate compilation that
carries a crate's public declarations, type signatures, trait implementations,
and trait-object layouts and vtables, along with the compiler and runtime ABI
tags and a hash per dependency. Dependent crates read the `.kfi` instead of
re-parsing source.

**Monomorphization** — The process of creating a concrete copy of a generic
function, struct, or enum for each unique set of type arguments. `identity(42)`
produces `identity__int32`; `identity<String>` produces a separate function.
No type-erasure and no runtime overhead — one copy of the code per set of
type arguments. The alternative, paying an indirect call to compile the
function once, is a [trait object](lang/trait-objects.md).

**Poison** — The type the checker assigns to an expression it could not type,
usually because an earlier error already fired there. It absorbs further
operations silently, which is how one real mistake avoids producing a cascade
of follow-on errors. It renders as `<error>` in diagnostics.

**Projection** — The syntax `Type.AssocName` that resolves an associated
type to the concrete type chosen in an impl. `Widget.Out` becomes `int32`
at compile time. Monomorphization substitutes projections away, so they
have no runtime cost.

**Object safety** — Whether a trait can be used as a [trait
object](lang/trait-objects.md). It cannot if it declares an associated type,
a generic method, or a static method, or if `Self` appears anywhere but the
implicit receiver — each would need something from the vtable that a fixed
list of function pointers cannot carry.

**Self (upper-case)** — In a trait declaration, refers to the implementing
type. In `trait Equal { fun equals(other: Self): bool }`, `Self` is a
placeholder that each impl replaces with its concrete type.

**self (lower-case)** — The receiver of a method. Inside `impl Foo { fun
bar(): int32 { ... } }`, `self` is the value the method was called on.

**Shadowing** — Declaring a binding with the same name as one already in
scope. The new binding replaces the old one; internally, the compiler
renames them so that move-checking and codegen see distinct variables.

**Trait object (`&dyn Trait`)** — A borrow that carries a vtable alongside the
data address, so the implementation is chosen at run time rather than at
compile time. Two words wide; it allocates nothing and owns nothing. See
[trait objects](lang/trait-objects.md).

**Unity build** — A build mode (`komp build --unity`) that emits the
entire crate and all its dependencies as a single C file. Faster to compile
than separate compilation, but does not produce `.kfi` interfaces.

**vtable** — A table of function pointers used for dynamic dispatch. KFlat
emits one per (type, trait) pair, as a static constant, and uses it only for
[trait objects](lang/trait-objects.md). Dispatch through a trait *bound* is
static, so most programs have no vtables at all.
