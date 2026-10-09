# GarudaLang (GaLang)

Bahasa pemrograman baru yang sederhana, cepat, dan konkuren.

## Fitur

- **Strict Typing**: Semua variable harus punya type annotation eksplisit
- **Native Codegen**: Compile ke ELF64 x86-64 langsung
- **CLI Tool**: `gar build`, `gar run`, `gar check`
- **Modern Syntax**: `fn`, `let`, `match`, `Option<T>`
- **Cross-platform**: Build untuk Linux, macOS, Windows via GitHub Actions

## Quick Start

```bash
# Build
gar build hello.ga

# Run
gar run hello.ga

# Type check only
gar check hello.ga

# Format code
gar fmt hello.ga
```

## Contoh Program

### Hello World (`hello.ga`)
```ga
fn main() {
    print("Hello, Garurda!\n");
}
```

### Factorial (`factorial.ga`)
```ga
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
```

## Struktur Project

```
garurda/
├── src/              # Source code compiler
│   ├── main.c        # CLI driver
│   ├── lexer.c       # Tokenizer
│   ├── parser.c      # Recursive descent parser
│   ├── checker.c     # Type checker
│   ├── codegen.c     # Native code generator
│   └── runtime.c     # Runtime library
├── include/          # Header files
├── testdata/         # Test fixtures
└── Makefile
```

## Status

v0.1 - Initial release with:
- [x] Lexer
- [x] Parser
- [x] Type Checker
- [x] Code Generator (x86-64 ELF)
- [ ] Goroutine/Channel (planned)
- [ ] Multi-platform support (planned)
- [ ] GC + Escape Analysis (planned)

## Lisensi

MIT
