GARUDALANG (GaLang) — Design Proposal
======================================
A simple, fast, statically-typed, compiled language.
Last updated: 2026-10-09

────────────────────────────────────────────────────────
1. IDENTITY
────────────────────────────────────────────────────────
Name       : GarudaLang (GaLang)
Extension  : .ga
Keywords   : English
Style      : Classic C-Family + Strict Typing
Target     : Native machine code, multi-platform

────────────────────────────────────────────────────────
2. PHILOSOPHY
────────────────────────────────────────────────────────
GarudaLang is a simple, fast, statically-typed, compiled
language with built-in concurrency.

It takes inspiration from Go's simplicity, but chooses
"simplicity by design" instead of "simplicity by omission":
features that reduce boilerplate are included, as long as
they remain explicit and easy to understand.

Goals:
  - Simple like Go
  - Fast compilation
  - Native machine code output
  - Bootstrapable (can compile itself)
  - Strict typing as default
  - Lightweight concurrency built-in

────────────────────────────────────────────────────────
3. SYNTAX DECISIONS
────────────────────────────────────────────────────────
Function keyword : fn
Variable         : let x: T = value (strict typing)
Short decl       : := for inferred (optional)
Semicolon        : Required (explicit)
Block            : { }
Entrypoint       : fn main()
Logical ops      : and, or, not (not &&, ||, !)
File extension   : .ga

────────────────────────────────────────────────────────
4. TYPE SYSTEM
────────────────────────────────────────────────────────
Primitive:
  int       — 64-bit signed integer
  float64   — 64-bit floating point
  bool      — true / false
  string    — immutable UTF-8 text

Special:
  Option<T> — optional value (replaces nil)
  Result<T, E> — error handling
  enum      — sum type
  struct    — product type
  interface — nominal typing

Type checking:
  - All variables must have explicit type annotation
  - No implicit type inference (strict mode)
  - Type mismatch is compile error
  - Unused variable is compile error
  - Unreachable code warning

────────────────────────────────────────────────────────
5. VARIABLE DECLARATION
────────────────────────────────────────────────────────
Strict typing (default):
  let port: int = 7272;
  let name: string = "Garurda";
  const MAX: int = 100;

Short declaration (inferred):
  port := 7272;
  name := "Garurda";

────────────────────────────────────────────────────────
6. FUNCTIONS
────────────────────────────────────────────────────────
Declaration:
  fn add(a: int, b: int) -> int {
      return a + b;
  }

Void function:
  fn greet(name: string) {
      print("Hello, " + name + "\n");
  }

Multiple return:
  fn divmod(a: int, b: int) -> (int, int) {
      return a / b, a % b;
  }

────────────────────────────────────────────────────────
7. CONTROL FLOW
────────────────────────────────────────────────────────
if / else if / else:
  if age >= 17 {
      print("Adult");
  } else if age >= 13 {
      print("Teen");
  } else {
      print("Child");
  }

for loop (range-based):
  for i in 0..10 {
      print(i);
  }

while loop:
  while active {
      print("running...");
  }

────────────────────────────────────────────────────────
8. OPTION<T> (replaces nil)
────────────────────────────────────────────────────────
fn findUser(id: int) -> Option<User> {
    if id == 1 {
        return Some(User { id: 1, name: "Garurda" });
    }
    return None;
}

let user = findUser(42);
match user {
    Some(u) => print(u.name),
    None    => print("Not found")
}

────────────────────────────────────────────────────────
9. RESULT<T, E> (error handling)
────────────────────────────────────────────────────────
fn readFile(path: string) -> Result<string, Error> {
    // ...
}

let content = readFile("data.txt");
match content {
    Ok(text) => print(text),
    Err(e) => print(f"Error: {e}")
}

────────────────────────────────────────────────────────
10. ENUM + MATCH
────────────────────────────────────────────────────────
enum Color { Red, Green, Blue }

fn colorName(c: Color) -> string {
    match c {
        Red   => "Red",
        Green => "Green",
        Blue  => "Blue"
    }
}

match is exhaustive: compiler errors if not all cases handled.

────────────────────────────────────────────────────────
11. INTERFACES (nominal typing)
────────────────────────────────────────────────────────
interface Writer {
    write(data: []byte) -> (int, Error);
}

struct MyWriter { ... }
impl Writer for MyWriter {
    write(data: []byte) -> (int, Error) { ... }
}

────────────────────────────────────────────────────────
12. CONCURRENCY (core feature, v0.1)
────────────────────────────────────────────────────────
Spawn coroutine:
  spawn fn() {
      print("Hello from coroutine");
  }

Channel:
  let ch: chan<int> = make(chan<int>);
  ch <- 42;
  let val: int = <-ch;

Buffered channel:
  let ch: chan<int> = make(chan<int>, 10);

Select:
  select {
      msg1 <- ch1 => print(msg1),
      msg2 <- ch2 => print(msg2),
      default => print("No message")
  }

Runtime:
  - M:N scheduling (many coroutines on few OS threads)
  - Initial stack: 2KB per coroutine
  - Work stealing for load balancing
  - Preemptive scheduling (no starvation)
  - Context switch: < 500ns

See: concurrency-design.txt for full details.

────────────────────────────────────────────────────────
13. MEMORY MANAGEMENT
────────────────────────────────────────────────────────
Garbage Collection + Escape Analysis

GC:
  - Modern mark-sweep collector
  - Concurrent GC (background collection)
  - Generational GC (young/old generations)

Escape Analysis:
  - Compile-time analysis
  - Stack allocation for non-escaping objects
  - Heap allocation only for escaping objects
  - Reduces GC pressure significantly

────────────────────────────────────────────────────────
14. CLI TOOL
────────────────────────────────────────────────────────
Command: gar

  gar build main.ga        # compile to native binary
  gar build main.ga -o app # custom output name
  gar run main.ga          # compile + run
  gar check main.ga        # type check only (no output)
  gar fmt main.ga          # format source code
  gar repl                 # interactive REPL

Multi-platform:
  gar build --target linux/arm64 main.ga
  gar build --target windows/amd64 main.ga
  gar build --target darwin/arm64 main.ga

────────────────────────────────────────────────────────
15. PLANNED FEATURES
────────────────────────────────────────────────────────
String interpolation : f"Hello, {name}!"
Default parameters   : fn greet(name, greeting = "Hello")
Operator overloading : +, -, *, / for custom types
Comptime keyword     : compile-time evaluation
Drop trait           : RAII / deterministic cleanup
Generics             : planned
Sync primitives      : mutex, rwlock, condvar, waitgroup

────────────────────────────────────────────────────────
16. WHAT GARUDALANG FIXES FROM GO
────────────────────────────────────────────────────────
Problem in Go                  | Garurda Solution
-------------------------------|----------------------------------
Type inference too loose       | Strict typing with let
Implicit semicolons (bugs)     | Explicit ;
Structural typing (magic)      | Nominal typing (impl)
nil everywhere                 | Option<T>
Verbose error handling         | Result<T, E> + match
No enum / exhaustive match     | enum + match
No default params              | Default parameters
No string interpolation        | f"..." builtin
No operator overloading        | Custom operators
No compile-time eval           | comptime keyword
No RAII                        | Drop trait
GC pauses                      | Concurrent + generational GC

────────────────────────────────────────────────────────
17. IMPLEMENTATION ROADMAP
────────────────────────────────────────────────────────
Phase 1: Lexer + Parser + AST
Phase 2: Type Checker + Escape Analysis
Phase 3: C Transpiler (GCC backend)
Phase 4: Runtime + Concurrency (coroutines, channels, GC)
Phase 5: CLI Tool (gar build, gar run, gar check, gar fmt, repl)
Phase 6: Stdlib + Advanced Features
Phase 7: Native Codegen (ELF x86-64, multi-platform)
Phase 8: Self-hosting / Bootstrapping

────────────────────────────────────────────────────────
18. FULL EXAMPLE
────────────────────────────────────────────────────────
// factorial.ga

fn factorial(n: int) -> int {
    if n <= 1 {
        return 1;
    }
    return n * factorial(n - 1);
}

fn main() {
    let n: int = 5;
    print("Factorial: ");
    print(factorial(n));
    print("\n");
}

Build and run:
  gar build factorial.ga
  ./factorial
  # Factorial: 120

Concurrent example:
  fn worker(id: int, jobs: chan<int>, results: chan<int>) {
      for job in jobs {
          let result: int = process(job);
          results <- result;
      }
  }

  fn main() {
      let jobs: chan<int> = make(chan<int>, 100);
      let results: chan<int> = make(chan<int>, 100);

      spawn worker(1, jobs, results);
      spawn worker(2, jobs, results);
      spawn worker(3, jobs, results);

      for i in 0..10 {
          jobs <- i;
      }
      close(jobs);

      for i in 0..10 {
          print(<-results);
      }
  }

────────────────────────────────────────────────────────
END OF DESIGN DOCUMENT
