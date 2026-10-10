# GarudaLang (GaLang)

Bahasa pemrograman statically-typed yang dikompilasi ke kode native x86-64.

## Fitur

- **Strict typing** — setiap variabel wajib punya anotasi tipe eksplisit
- **Native codegen** — menghasilkan assembly x86-64, lalu ELF64 lewat `as`/`ld`
- **Modul** — `import` dengan resolusi path, deduplikasi, dan deteksi siklus
- **CLI** — `gar build`, `gar run`, `gar check`

## Quick Start

```bash
make          # build ./bin/gar
make test     # jalankan test suite
make asmv FILE=testdata/loop.ga   # lihat assembly yang dihasilkan
```

```bash
gar build hello.ga       # compile
gar run hello.ga         # compile lalu jalankan
gar check hello.ga       # type check saja
GAR_VERBOSE=1 gar build hello.ga   # tampilkan modul yang dimuat
```

## Contoh Program

### Hello World

```ga
fn main() {
    print("Hello, Garurda!\n");
}
```

### Fungsi, rekursi, loop

```ga
fn factorial(n: int) -> int {
    if n <= 1 {
        return 1;
    }
    return n * factorial(n - 1);
}

fn main() {
    for i in 1..6 {
        print(factorial(i));
        print("\n");
    }
}
```

### Modul

`main.ga`

```ga
import "std/math.ga";

fn main() {
    print(add(2, 3));
}
```

`std/math.ga`

```ga
fn add(a: int, b: int) -> int {
    return a + b;
}
```

Path di-resolve relatif terhadap direktori file peng-import terlebih dahulu,
lalu relatif terhadap direktori file entry.

## Struktur Project

```
garurda/
├── src/
│   ├── main.c        # CLI driver
│   ├── lexer.c       # Tokenizer
│   ├── parser.c      # Recursive descent parser
│   ├── resolver.c    # Module resolver (import graph)
│   ├── checker.c     # Type checker
│   ├── codegen.c     # x86-64 code generator
│   └── runtime.c     # Runtime library
├── include/
├── testdata/         # Fixtures + .expected hasil yang harus dicocokkan
├── run_tests.sh      # Test runner
└── Makefile
```

## Pipeline

```
.ga  ->  Lexer -> Parser -> Module Resolver -> Type Checker
     ->  Codegen (x86-64 asm) -> as -> ld -> ELF64
```

## Status

v0.2

- [x] Lexer, parser, type checker
- [x] `if` / `while` / `for`, fungsi, rekursi, assignment
- [x] Codegen x86-64 (int, bool, string, perbandingan, aritmetika)
- [x] Modul: `import`, deduplikasi, deteksi siklus, deteksi duplikat
- [x] Test suite + ASAN/UBSAN bersih
- [ ] `float64` di codegen (sekarang dipotong ke integer)
- [ ] Operator logika `and` / `or` (parser belum memproduksinya)
- [ ] `match` + `enum`, `struct`, `impl`, `interface`
- [ ] `Option<T>` / `Result<T, E>`
- [ ] GASM sendiri (saat ini `as`/`ld`) dan GLINK
- [ ] Incremental compilation
- [ ] Backend Windows

## Lisensi

MIT
