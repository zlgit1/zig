/*
 * Copyright (c) 2015 Andrew Kelley
 *
 * This file is part of zig, which is MIT licensed.
 * See http://opensource.org/licenses/MIT
 */

#include "list.hpp"
#include "buffer.hpp"
#include "os.hpp"

#include <stdio.h>
#include <stdarg.h>

struct TestSourceFile {
    const char *relative_path;
    const char *source_code;
};

struct TestCase {
    const char *case_name;
    const char *output;
    ZigList<TestSourceFile> source_files;
    ZigList<const char *> compile_errors;
    ZigList<const char *> compiler_args;
    ZigList<const char *> program_args;
};

static ZigList<TestCase*> test_cases = {0};
static const char *tmp_source_path = ".tmp_source.zig";
static const char *tmp_exe_path = "./.tmp_exe";
static const char *zig_exe = "./zig";

static void add_source_file(TestCase *test_case, const char *path, const char *source) {
    test_case->source_files.add_one();
    test_case->source_files.last().relative_path = path;
    test_case->source_files.last().source_code = source;
}

static TestCase *add_simple_case(const char *case_name, const char *source, const char *output) {
    TestCase *test_case = allocate<TestCase>(1);
    test_case->case_name = case_name;
    test_case->output = output;

    test_case->source_files.resize(1);
    test_case->source_files.at(0).relative_path = tmp_source_path;
    test_case->source_files.at(0).source_code = source;

    test_case->compiler_args.append("build");
    test_case->compiler_args.append(tmp_source_path);
    test_case->compiler_args.append("--export");
    test_case->compiler_args.append("exe");
    test_case->compiler_args.append("--name");
    test_case->compiler_args.append("test");
    test_case->compiler_args.append("--output");
    test_case->compiler_args.append(tmp_exe_path);
    test_case->compiler_args.append("--release");
    test_case->compiler_args.append("--strip");
    //test_case->compiler_args.append("--verbose");
    test_case->compiler_args.append("--color");
    test_case->compiler_args.append("on");

    test_cases.append(test_case);

    return test_case;
}

static TestCase *add_compile_fail_case(const char *case_name, const char *source, int count, ...) {
    va_list ap;
    va_start(ap, count);

    TestCase *test_case = allocate<TestCase>(1);
    test_case->case_name = case_name;
    test_case->source_files.resize(1);
    test_case->source_files.at(0).relative_path = tmp_source_path;
    test_case->source_files.at(0).source_code = source;

    for (int i = 0; i < count; i += 1) {
        const char *arg = va_arg(ap, const char *);
        test_case->compile_errors.append(arg);
    }

    test_case->compiler_args.append("build");
    test_case->compiler_args.append(tmp_source_path);
    test_case->compiler_args.append("--output");
    test_case->compiler_args.append(tmp_exe_path);
    test_case->compiler_args.append("--release");
    test_case->compiler_args.append("--strip");
    //test_case->compiler_args.append("--verbose");

    test_cases.append(test_case);

    va_end(ap);

    return test_case;
}

static void add_compiling_test_cases(void) {
    add_simple_case("hello world with libc", R"SOURCE(
        #link("c")
        extern {
            fn puts(s: &const u8) -> i32;
        }

        export fn main(argc: i32, argv: &&u8, env: &&u8) -> i32 {
            puts(c"Hello, world!");
            return 0;
        }
    )SOURCE", "Hello, world!\n");

    add_simple_case("function call", R"SOURCE(
        use "std.zig";

        fn empty_function_1() {}
        fn empty_function_2() { return; }

        pub fn main(argc: isize, argv: &&u8, env: &&u8) -> i32 {
            empty_function_1();
            empty_function_2();
            this_is_a_function();
        }

        fn this_is_a_function() -> unreachable {
            print_str("OK\n");
            exit(0);
        }
    )SOURCE", "OK\n");

    add_simple_case("comments", R"SOURCE(
        use "std.zig";

        /**
         * multi line doc comment
         */
        fn another_function() {}

        /// this is a documentation comment
        /// doc comment line 2
        pub fn main(argc: isize, argv: &&u8, env: &&u8) -> i32 {
            print_str(/* mid-line comment /* nested */ */ "OK\n");
            return 0;
        }
    )SOURCE", "OK\n");

    {
        TestCase *tc = add_simple_case("multiple files with private function", R"SOURCE(
            use "libc.zig";
            use "foo.zig";

            export fn main(argc: i32, argv: &&u8, env: &&u8) -> i32 {
                private_function();
            }

            fn private_function() -> unreachable {
                print_text();
                exit(0);
            }
        )SOURCE", "OK\n");

        add_source_file(tc, "libc.zig", R"SOURCE(
            #link("c")
            extern {
                pub fn puts(s: &const u8) -> i32;
                pub fn exit(code: i32) -> unreachable;
            }
        )SOURCE");

        add_source_file(tc, "foo.zig", R"SOURCE(
            use "libc.zig";

            // purposefully conflicting function with main source file
            // but it's private so it should be OK
            fn private_function() {
                puts(c"OK");
            }

            pub fn print_text() {
                private_function();
            }
        )SOURCE");
    }

    add_simple_case("if statements", R"SOURCE(
        use "std.zig";

        pub fn main(argc: isize, argv: &&u8, env: &&u8) -> i32 {
            if (1 != 0) {
                print_str("1 is true\n");
            } else {
                print_str("1 is false\n");
            }
            if (0 != 0) {
                print_str("0 is true\n");
            } else if (1 - 1 != 0) {
                print_str("1 - 1 is true\n");
            }
            if (!(0 != 0)) {
                print_str("!0 is true\n");
            }
            return 0;
        }
    )SOURCE", "1 is true\n!0 is true\n");

    add_simple_case("params", R"SOURCE(
        use "std.zig";

        fn add(a: i32, b: i32) -> i32 {
            a + b
        }

        pub fn main(argc: isize, argv: &&u8, env: &&u8) -> i32 {
            if (add(22, 11) == 33) {
                print_str("pass\n");
            }
            return 0;
        }
    )SOURCE", "pass\n");

    add_simple_case("goto", R"SOURCE(
        use "std.zig";

        fn loop(a : i32) {
            if (a == 0) {
                goto done;
            }
            print_str("loop\n");
            loop(a - 1);

        done:
            return;
        }

        pub fn main(argc: isize, argv: &&u8, env: &&u8) -> i32 {
            loop(3);
            return 0;
        }
    )SOURCE", "loop\nloop\nloop\n");

    add_simple_case("local variables", R"SOURCE(
use "std.zig";

pub fn main(argc: isize, argv: &&u8, env: &&u8) -> i32 {
    const a : i32 = 1;
    const b = 2 as i32;
    if (a + b == 3) {
        print_str("OK\n");
    }
    return 0;
}
    )SOURCE", "OK\n");

    add_simple_case("bool literals", R"SOURCE(
use "std.zig";

pub fn main(argc: isize, argv: &&u8, env: &&u8) -> i32 {
    if (true)   { print_str("OK 1\n"); }
    if (false)  { print_str("BAD 1\n"); }
    if (!true)  { print_str("BAD 2\n"); }
    if (!false) { print_str("OK 2\n"); }
    return 0;
}
    )SOURCE", "OK 1\nOK 2\n");

    add_simple_case("separate block scopes", R"SOURCE(
use "std.zig";

pub fn main(argc: isize, argv: &&u8, env: &&u8) -> i32 {
    if (true) {
        const no_conflict : i32 = 5;
        if (no_conflict == 5) { print_str("OK 1\n"); }
    }

    const c = {
        const no_conflict = 10 as i32;
        no_conflict
    };
    if (c == 10) { print_str("OK 2\n"); }
    return 0;
}
    )SOURCE", "OK 1\nOK 2\n");

    add_simple_case("void parameters", R"SOURCE(
use "std.zig";

pub fn main(argc: isize, argv: &&u8, env: &&u8) -> i32 {
    void_fun(1, void, 2);
    return 0;
}

fn void_fun(a : i32, b : void, c : i32) {
    const v = b;
    const vv : void = if (a == 1) {v} else {};
    if (a + c == 3) { print_str("OK\n"); }
    return vv;
}
    )SOURCE", "OK\n");

    add_simple_case("mutable local variables", R"SOURCE(
use "std.zig";

pub fn main(argc: isize, argv: &&u8, env: &&u8) -> i32 {
    var zero : i32;
    if (zero == 0) { print_str("zero\n"); }

    var i = 0 as i32;
loop_start:
    if (i == 3) {
        goto done;
    }
    print_str("loop\n");
    i = i + 1;
    goto loop_start;
done:
    return 0;
}
    )SOURCE", "zero\nloop\nloop\nloop\n");

    add_simple_case("arrays", R"SOURCE(
use "std.zig";

pub fn main(argc: isize, argv: &&u8, env: &&u8) -> i32 {
    var array : [i32; 5];

    var i : i32 = 0;
loop_start:
    if (i == 5) {
        goto loop_end;
    }
    array[i] = i + 1;
    i = array[i];
    goto loop_start;

loop_end:

    i = 0;
    var accumulator = 0 as i32;
loop_2_start:
    if (i == 5) {
        goto loop_2_end;
    }

    accumulator = accumulator + array[i];

    i = i + 1;
    goto loop_2_start;
loop_2_end:

    if (accumulator == 15) {
        print_str("OK\n");
    }

    return 0;
}
    )SOURCE", "OK\n");


    add_simple_case("hello world without libc", R"SOURCE(
use "std.zig";

export fn main(argc : isize, argv : &&u8, env : &&u8) -> i32 {
    print_str("Hello, world!\n");
    return 0;
}
    )SOURCE", "Hello, world!\n");


    add_simple_case("a + b + c", R"SOURCE(
use "std.zig";

export fn main(argc : isize, argv : &&u8, env : &&u8) -> i32 {
    if (false || false || false) { print_str("BAD 1\n"); }
    if (true && true && false)   { print_str("BAD 2\n"); }
    if (1 | 2 | 4 != 7)          { print_str("BAD 3\n"); }
    if (3 ^ 6 ^ 8 != 13)         { print_str("BAD 4\n"); }
    if (7 & 14 & 28 != 4)        { print_str("BAD 5\n"); }
    if (9  << 1 << 2 != 9  << 3) { print_str("BAD 6\n"); }
    if (90 >> 1 >> 2 != 90 >> 3) { print_str("BAD 7\n"); }
    if (100 - 1 + 1000 != 1099)  { print_str("BAD 8\n"); }
    if (5 * 4 / 2 % 3 != 1)      { print_str("BAD 9\n"); }
    if (5 as i32 as i32 != 5)    { print_str("BAD 10\n"); }
    if (!!false)                 { print_str("BAD 11\n"); }
    if (7 != --7)                { print_str("BAD 12\n"); }

    print_str("OK\n");
    return 0;
}
    )SOURCE", "OK\n");

    add_simple_case("short circuit", R"SOURCE(
use "std.zig";

export fn main(argc : isize, argv : &&u8, env : &&u8) -> i32 {
    if (true || { print_str("BAD 1\n"); false }) {
      print_str("OK 1\n");
    }
    if (false || { print_str("OK 2\n"); false }) {
      print_str("BAD 2\n");
    }

    if (true && { print_str("OK 3\n"); false }) {
      print_str("BAD 3\n");
    }
    if (false && { print_str("BAD 4\n"); false }) {
    } else {
      print_str("OK 4\n");
    }

    return 0;
}
    )SOURCE", "OK 1\nOK 2\nOK 3\nOK 4\n");

    add_simple_case("modify operators", R"SOURCE(
use "std.zig";

export fn main(argc : isize, argv : &&u8, env : &&u8) -> i32 {
    var i : i32 = 0;
    i += 5;  if (i != 5)  { print_str("BAD +=\n"); }
    i -= 2;  if (i != 3)  { print_str("BAD -=\n"); }
    i *= 20; if (i != 60) { print_str("BAD *=\n"); }
    i /= 3;  if (i != 20) { print_str("BAD /=\n"); }
    i %= 11; if (i != 9)  { print_str("BAD %=\n"); }
    i <<= 1; if (i != 18) { print_str("BAD <<=\n"); }
    i >>= 2; if (i != 4)  { print_str("BAD >>=\n"); }
    i = 6;
    i &= 5;  if (i != 4)  { print_str("BAD &=\n"); }
    i ^= 6;  if (i != 2)  { print_str("BAD ^=\n"); }
    i = 6;
    i |= 3;  if (i != 7)  { print_str("BAD |=\n"); }

    print_str("OK\n");
    return 0;
}
    )SOURCE", "OK\n");

    add_simple_case("number literals", R"SOURCE(
#link("c")
extern {
    fn printf(__format: &const u8, ...) -> i32;
}

export fn main(argc : isize, argv : &&u8, env : &&u8) -> i32 {
    printf(c"\n");

    printf(c"0: %llu\n",
             0 as u64);
    printf(c"320402575052271: %llu\n",
             320402575052271 as u64);
    printf(c"0x01236789abcdef: %llu\n",
             0x01236789abcdef as u64);
    printf(c"0xffffffffffffffff: %llu\n",
             0xffffffffffffffff as u64);
    printf(c"0x000000ffffffffffffffff: %llu\n",
             0x000000ffffffffffffffff as u64);
    printf(c"0o1777777777777777777777: %llu\n",
             0o1777777777777777777777 as u64);
    printf(c"0o0000001777777777777777777777: %llu\n",
             0o0000001777777777777777777777 as u64);
    printf(c"0b1111111111111111111111111111111111111111111111111111111111111111: %llu\n",
             0b1111111111111111111111111111111111111111111111111111111111111111 as u64);
    printf(c"0b0000001111111111111111111111111111111111111111111111111111111111111111: %llu\n",
             0b0000001111111111111111111111111111111111111111111111111111111111111111 as u64);

    printf(c"\n");

    printf(c"0.0: %a\n",
             0.0 as f64);
    printf(c"0e0: %a\n",
             0e0 as f64);
    printf(c"0.0e0: %a\n",
             0.0e0 as f64);
    printf(c"000000000000000000000000000000000000000000000000000000000.0e0: %a\n",
             000000000000000000000000000000000000000000000000000000000.0e0 as f64);
    printf(c"0.000000000000000000000000000000000000000000000000000000000e0: %a\n",
             0.000000000000000000000000000000000000000000000000000000000e0 as f64);
    printf(c"0.0e000000000000000000000000000000000000000000000000000000000: %a\n",
             0.0e000000000000000000000000000000000000000000000000000000000 as f64);
    printf(c"1.0: %a\n",
             1.0 as f64);
    printf(c"10.0: %a\n",
             10.0 as f64);
    printf(c"10.5: %a\n",
             10.5 as f64);
    printf(c"10.5e5: %a\n",
             10.5e5 as f64);
    printf(c"10.5e+5: %a\n",
             10.5e+5 as f64);
    printf(c"50.0e-2: %a\n",
             50.0e-2 as f64);
    printf(c"50e-2: %a\n",
             50e-2 as f64);

    printf(c"\n");

    printf(c"0x1.0: %a\n",
             0x1.0 as f64);
    printf(c"0x10.0: %a\n",
             0x10.0 as f64);
    printf(c"0x100.0: %a\n",
             0x100.0 as f64);
    printf(c"0x103.0: %a\n",
             0x103.0 as f64);
    printf(c"0x103.7: %a\n",
             0x103.7 as f64);
    printf(c"0x103.70: %a\n",
             0x103.70 as f64);
    printf(c"0x103.70p4: %a\n",
             0x103.70p4 as f64);
    printf(c"0x103.70p5: %a\n",
             0x103.70p5 as f64);
    printf(c"0x103.70p+5: %a\n",
             0x103.70p+5 as f64);
    printf(c"0x103.70p-5: %a\n",
             0x103.70p-5 as f64);

    printf(c"\n");

    printf(c"0b10100.00010e0: %a\n",
             0b10100.00010e0 as f64);
    printf(c"0o10700.00010e0: %a\n",
             0o10700.00010e0 as f64);

    return 0;
}
    )SOURCE", R"OUTPUT(
0: 0
320402575052271: 320402575052271
0x01236789abcdef: 320402575052271
0xffffffffffffffff: 18446744073709551615
0x000000ffffffffffffffff: 18446744073709551615
0o1777777777777777777777: 18446744073709551615
0o0000001777777777777777777777: 18446744073709551615
0b1111111111111111111111111111111111111111111111111111111111111111: 18446744073709551615
0b0000001111111111111111111111111111111111111111111111111111111111111111: 18446744073709551615

0.0: 0x0p+0
0e0: 0x0p+0
0.0e0: 0x0p+0
000000000000000000000000000000000000000000000000000000000.0e0: 0x0p+0
0.000000000000000000000000000000000000000000000000000000000e0: 0x0p+0
0.0e000000000000000000000000000000000000000000000000000000000: 0x0p+0
1.0: 0x1p+0
10.0: 0x1.4p+3
10.5: 0x1.5p+3
10.5e5: 0x1.0059p+20
10.5e+5: 0x1.0059p+20
50.0e-2: 0x1p-1
50e-2: 0x1p-1

0x1.0: 0x1p+0
0x10.0: 0x1p+4
0x100.0: 0x1p+8
0x103.0: 0x1.03p+8
0x103.7: 0x1.037p+8
0x103.70: 0x1.037p+8
0x103.70p4: 0x1.037p+12
0x103.70p5: 0x1.037p+13
0x103.70p+5: 0x1.037p+13
0x103.70p-5: 0x1.037p+3

0b10100.00010e0: 0x1.41p+4
0o10700.00010e0: 0x1.1c0001p+12
)OUTPUT");

    add_simple_case("structs", R"SOURCE(
use "std.zig";

export fn main(argc : isize, argv : &&u8, env : &&u8) -> i32 {
    var foo : Foo;
    foo.a += 1;
    foo.b = foo.a == 1;
    test_foo(foo);
    test_mutation(&foo);
    if (foo.c != 100) {
        print_str("BAD\n");
    }
    test_point_to_self();
    test_byval_assign();
    test_initializer();
    print_str("OK\n");
    return 0;
}
struct Foo {
    a : i32,
    b : bool,
    c : f32,
}
fn test_foo(foo : Foo) {
    if (!foo.b) {
        print_str("BAD\n");
    }
}
fn test_mutation(foo : &Foo) {
    foo.c = 100;
}
struct Node {
    val: Val,
    next: &Node,
}

struct Val {
    x: i32,
}
fn test_point_to_self() {
    var root : Node;
    root.val.x = 1;

    var node : Node;
    node.next = &root;
    node.val.x = 2;

    root.next = &node;

    if (node.next.next.next.val.x != 1) {
        print_str("BAD\n");
    }
}
fn test_byval_assign() {
    var foo1 : Foo;
    var foo2 : Foo;

    foo1.a = 1234;

    if (foo2.a != 0) { print_str("BAD\n"); }

    foo2 = foo1;

    if (foo2.a != 1234) { print_str("BAD - byval assignment failed\n"); }
}
fn test_initializer() {
    const val = Val { .x = 42 };
    if (val.x != 42) { print_str("BAD\n"); }
}
    )SOURCE", "OK\n");

    add_simple_case("global variables", R"SOURCE(
use "std.zig";

const g1 : i32 = 1233 + 1;
var g2 : i32;

export fn main(argc : isize, argv : &&u8, env : &&u8) -> i32 {
    if (g2 != 0) { print_str("BAD\n"); }
    g2 = g1;
    if (g2 != 1234) { print_str("BAD\n"); }
    print_str("OK\n");
    return 0;
}
    )SOURCE", "OK\n");

    add_simple_case("while loop", R"SOURCE(
use "std.zig";
export fn main(argc : isize, argv : &&u8, env : &&u8) -> i32 {
    var i : i32 = 0;
    while (i < 4) {
        print_str("loop\n");
        i += 1;
    }
    return 0;
}
    )SOURCE", "loop\nloop\nloop\nloop\n");

    add_simple_case("continue and break", R"SOURCE(
use "std.zig";
export fn main(argc : isize, argv : &&u8, env : &&u8) -> i32 {
    var i : i32 = 0;
    while (true) {
        print_str("loop\n");
        i += 1;
        if (i < 4) {
            continue;
        }
        break;
    }
    return 0;
}
    )SOURCE", "loop\nloop\nloop\nloop\n");

    add_simple_case("maybe type", R"SOURCE(
use "std.zig";
export fn main(argc : isize, argv : &&u8, env : &&u8) -> i32 {
    const x : ?bool = true;

    if (const y ?= x) {
        if (y) {
            print_str("x is true\n");
        } else {
            print_str("x is false\n");
        }
    } else {
        print_str("x is none\n");
    }
    return 0;
}
    )SOURCE", "x is true\n");
}

////////////////////////////////////////////////////////////////////////////////////

static void add_compile_failure_test_cases(void) {
    add_compile_fail_case("multiple function definitions", R"SOURCE(
fn a() {}
fn a() {}
    )SOURCE", 1, ".tmp_source.zig:3:1: error: redefinition of 'a'");

    add_compile_fail_case("bad directive", R"SOURCE(
#bogus1("")
extern {
    fn b();
}
#bogus2("")
fn a() {}
    )SOURCE", 2, ".tmp_source.zig:2:1: error: invalid directive: 'bogus1'",
                 ".tmp_source.zig:6:1: error: invalid directive: 'bogus2'");

    add_compile_fail_case("unreachable with return", R"SOURCE(
fn a() -> unreachable {return;}
    )SOURCE", 1, ".tmp_source.zig:2:24: error: expected type 'unreachable', got 'void'");

    add_compile_fail_case("control reaches end of non-void function", R"SOURCE(
fn a() -> i32 {}
    )SOURCE", 1, ".tmp_source.zig:2:15: error: expected type 'i32', got 'void'");

    add_compile_fail_case("undefined function call", R"SOURCE(
fn a() {
    b();
}
    )SOURCE", 1, ".tmp_source.zig:3:5: error: undefined function: 'b'");

    add_compile_fail_case("wrong number of arguments", R"SOURCE(
fn a() {
    b(1);
}
fn b(a: i32, b: i32, c: i32) { }
    )SOURCE", 1, ".tmp_source.zig:3:6: error: wrong number of arguments. Expected 3, got 1.");

    add_compile_fail_case("invalid type", R"SOURCE(
fn a() -> bogus {}
    )SOURCE", 1, ".tmp_source.zig:2:11: error: invalid type name: 'bogus'");

    add_compile_fail_case("pointer to unreachable", R"SOURCE(
fn a() -> &unreachable {}
    )SOURCE", 1, ".tmp_source.zig:2:11: error: pointer to unreachable not allowed");

    add_compile_fail_case("unreachable code", R"SOURCE(
fn a() {
    return;
    b();
}

fn b() {}
    )SOURCE", 1, ".tmp_source.zig:4:5: error: unreachable code");

    add_compile_fail_case("bad version string", R"SOURCE(
#version("aoeu")
export executable "test";
    )SOURCE", 1, ".tmp_source.zig:2:1: error: invalid version string");

    add_compile_fail_case("bad import", R"SOURCE(
use "bogus-does-not-exist.zig";
    )SOURCE", 1, ".tmp_source.zig:2:1: error: unable to find 'bogus-does-not-exist.zig'");

    add_compile_fail_case("undeclared identifier", R"SOURCE(
fn a() {
    b +
    c
}
    )SOURCE", 2,
            ".tmp_source.zig:3:5: error: use of undeclared identifier 'b'",
            ".tmp_source.zig:4:5: error: use of undeclared identifier 'c'");

    add_compile_fail_case("goto cause unreachable code", R"SOURCE(
fn a() {
    goto done;
    b();
done:
    return;
}
fn b() {}
    )SOURCE", 1, ".tmp_source.zig:4:5: error: unreachable code");

    add_compile_fail_case("parameter redeclaration", R"SOURCE(
fn f(a : i32, a : i32) {
}
    )SOURCE", 1, ".tmp_source.zig:2:1: error: redeclaration of parameter 'a'");

    add_compile_fail_case("local variable redeclaration", R"SOURCE(
fn f() {
    const a : i32 = 0;
    const a = 0;
}
    )SOURCE", 1, ".tmp_source.zig:4:5: error: redeclaration of variable 'a'");

    add_compile_fail_case("local variable redeclares parameter", R"SOURCE(
fn f(a : i32) {
    const a = 0;
}
    )SOURCE", 1, ".tmp_source.zig:3:5: error: redeclaration of variable 'a'");

    add_compile_fail_case("variable has wrong type", R"SOURCE(
fn f() -> i32 {
    const a = c"a";
    a
}
    )SOURCE", 1, ".tmp_source.zig:2:15: error: expected type 'i32', got '&const u8'");

    add_compile_fail_case("if condition is bool, not int", R"SOURCE(
fn f() {
    if (0) {}
}
    )SOURCE", 1, ".tmp_source.zig:3:9: error: expected type 'bool', got '(u8 literal)'");

    add_compile_fail_case("assign unreachable", R"SOURCE(
fn f() {
    const a = return;
}
    )SOURCE", 1, ".tmp_source.zig:3:5: error: variable initialization is unreachable");

    add_compile_fail_case("unreachable variable", R"SOURCE(
fn f() {
    const a : unreachable = return;
}
    )SOURCE", 1, ".tmp_source.zig:3:15: error: variable of type 'unreachable' not allowed");

    add_compile_fail_case("unreachable parameter", R"SOURCE(
fn f(a : unreachable) {}
    )SOURCE", 1, ".tmp_source.zig:2:10: error: parameter of type 'unreachable' not allowed");

    add_compile_fail_case("exporting a void parameter", R"SOURCE(
export fn f(a : void) {}
    )SOURCE", 1, ".tmp_source.zig:2:17: error: parameter of type 'void' not allowed on exported functions");

    add_compile_fail_case("unused label", R"SOURCE(
fn f() {
a_label:
}
    )SOURCE", 1, ".tmp_source.zig:3:1: error: label 'a_label' defined but not used");

    add_compile_fail_case("bad assignment target", R"SOURCE(
fn f() {
    3 = 3;
}
    )SOURCE", 1, ".tmp_source.zig:3:5: error: assignment target must be variable, field, or array element");

    add_compile_fail_case("assign to constant variable", R"SOURCE(
fn f() {
    const a = 3;
    a = 4;
}
    )SOURCE", 1, ".tmp_source.zig:4:5: error: cannot assign to constant");

    add_compile_fail_case("use of undeclared identifier", R"SOURCE(
fn f() {
    b = 3;
}
    )SOURCE", 1, ".tmp_source.zig:3:5: error: use of undeclared identifier 'b'");

    add_compile_fail_case("const is a statement, not an expression", R"SOURCE(
fn f() {
    (const a = 0);
}
    )SOURCE", 1, ".tmp_source.zig:3:6: error: invalid token: 'const'");

    add_compile_fail_case("array access errors", R"SOURCE(
fn f() {
    var bad : bool;
    i[i] = i[i];
    bad[bad] = bad[bad];
}
    )SOURCE", 8, ".tmp_source.zig:4:5: error: use of undeclared identifier 'i'",
                 ".tmp_source.zig:4:7: error: use of undeclared identifier 'i'",
                 ".tmp_source.zig:4:12: error: use of undeclared identifier 'i'",
                 ".tmp_source.zig:4:14: error: use of undeclared identifier 'i'",
                 ".tmp_source.zig:5:8: error: array access of non-array",
                 ".tmp_source.zig:5:8: error: array subscripts must be integers",
                 ".tmp_source.zig:5:19: error: array access of non-array",
                 ".tmp_source.zig:5:19: error: array subscripts must be integers");

    add_compile_fail_case("variadic functions only allowed in extern", R"SOURCE(
fn f(...) {}
    )SOURCE", 1, ".tmp_source.zig:2:1: error: variadic arguments only allowed in extern functions");

    add_compile_fail_case("write to const global variable", R"SOURCE(
const x : i32 = 99;
fn f() {
    x = 1;
}
    )SOURCE", 1, ".tmp_source.zig:4:5: error: cannot assign to constant");


    add_compile_fail_case("missing else clause", R"SOURCE(
fn f() {
    const x : i32 = if (true) { 1 };
    const y = if (true) { 1 as i32 };
}
    )SOURCE", 2, ".tmp_source.zig:3:21: error: expected type 'i32', got 'void'",
                 ".tmp_source.zig:4:15: error: incompatible types: 'i32' and 'void'");

    add_compile_fail_case("direct struct loop", R"SOURCE(
struct A { a : A, }
    )SOURCE", 1, ".tmp_source.zig:2:1: error: struct has infinite size");

    add_compile_fail_case("indirect struct loop", R"SOURCE(
struct A { b : B, }
struct B { c : C, }
struct C { a : A, }
    )SOURCE", 1, ".tmp_source.zig:2:1: error: struct has infinite size");

    add_compile_fail_case("invalid struct field", R"SOURCE(
struct A { x : i32, }
fn f() {
    var a : A;
    a.foo = 1;
    const y = a.bar;
}
    )SOURCE", 2,
            ".tmp_source.zig:5:6: error: no member named 'foo' in 'A'",
            ".tmp_source.zig:6:16: error: no member named 'bar' in 'A'");

    add_compile_fail_case("redefinition of struct", R"SOURCE(
struct A { x : i32, }
struct A { y : i32, }
    )SOURCE", 1, ".tmp_source.zig:3:1: error: redefinition of 'A'");

    add_compile_fail_case("byvalue struct on exported functions", R"SOURCE(
struct A { x : i32, }
export fn f(a : A) {}
    )SOURCE", 1, ".tmp_source.zig:3:13: error: byvalue struct parameters not yet supported on exported functions");

    add_compile_fail_case("duplicate field in struct value expression", R"SOURCE(
struct A {
    x : i32,
    y : i32,
    z : i32,
}
fn f() {
    const a = A {
        .z = 1,
        .y = 2,
        .x = 3,
        .z = 4,
    };
}
    )SOURCE", 1, ".tmp_source.zig:12:9: error: duplicate field");

    add_compile_fail_case("missing field in struct value expression", R"SOURCE(
struct A {
    x : i32,
    y : i32,
    z : i32,
}
fn f() {
    const a = A {
        .z = 4,
        .y = 2,
    };
}
    )SOURCE", 1, ".tmp_source.zig:8:15: error: missing field: 'x'");

    add_compile_fail_case("invalid field in struct value expression", R"SOURCE(
struct A {
    x : i32,
    y : i32,
    z : i32,
}
fn f() {
    const a = A {
        .z = 4,
        .y = 2,
        .foo = 42,
    };
}
    )SOURCE", 1, ".tmp_source.zig:11:9: error: no member named 'foo' in 'A'");

    add_compile_fail_case("invalid break expression", R"SOURCE(
fn f() {
    break;
}
    )SOURCE", 1, ".tmp_source.zig:3:5: error: 'break' expression not in loop");

    add_compile_fail_case("invalid continue expression", R"SOURCE(
fn f() {
    continue;
}
    )SOURCE", 1, ".tmp_source.zig:3:5: error: 'continue' expression not in loop");

    add_compile_fail_case("invalid maybe type", R"SOURCE(
fn f() {
    if (const x ?= true) { }
}
    )SOURCE", 1, ".tmp_source.zig:3:20: error: expected maybe type");
}

static void print_compiler_invocation(TestCase *test_case) {
    printf("%s", zig_exe);
    for (int i = 0; i < test_case->compiler_args.length; i += 1) {
        printf(" %s", test_case->compiler_args.at(i));
    }
    printf("\n");
}

static void run_test(TestCase *test_case) {
    for (int i = 0; i < test_case->source_files.length; i += 1) {
        TestSourceFile *test_source = &test_case->source_files.at(i);
        os_write_file(
                buf_create_from_str(test_source->relative_path),
                buf_create_from_str(test_source->source_code));
    }

    Buf zig_stderr = BUF_INIT;
    Buf zig_stdout = BUF_INIT;
    int return_code;
    os_exec_process(zig_exe, test_case->compiler_args, &return_code, &zig_stderr, &zig_stdout);

    if (test_case->compile_errors.length) {
        if (return_code) {
            for (int i = 0; i < test_case->compile_errors.length; i += 1) {
                const char *err_text = test_case->compile_errors.at(i);
                if (!strstr(buf_ptr(&zig_stderr), err_text)) {
                    printf("\n");
                    printf("========= Expected this compile error: =========\n");
                    printf("%s\n", err_text);
                    printf("================================================\n");
                    print_compiler_invocation(test_case);
                    printf("%s\n", buf_ptr(&zig_stderr));
                    exit(1);
                }
            }
            return; // success
        } else {
            printf("\nCompile failed with return code 0 (Expected failure):\n");
            print_compiler_invocation(test_case);
            printf("%s\n", buf_ptr(&zig_stderr));
            exit(1);
        }
    }

    if (return_code != 0) {
        printf("\nCompile failed with return code %d:\n", return_code);
        print_compiler_invocation(test_case);
        printf("%s\n", buf_ptr(&zig_stderr));
        exit(1);
    }

    Buf program_stderr = BUF_INIT;
    Buf program_stdout = BUF_INIT;
    os_exec_process(tmp_exe_path, test_case->program_args, &return_code, &program_stderr, &program_stdout);

    if (return_code != 0) {
        printf("\nProgram exited with return code %d:\n", return_code);
        print_compiler_invocation(test_case);
        printf("%s", tmp_exe_path);
        for (int i = 0; i < test_case->program_args.length; i += 1) {
            printf(" %s", test_case->program_args.at(i));
        }
        printf("\n");
        printf("%s\n", buf_ptr(&program_stderr));
        exit(1);
    }

    if (!buf_eql_str(&program_stdout, test_case->output)) {
        printf("\n");
        print_compiler_invocation(test_case);
        printf("%s", tmp_exe_path);
        for (int i = 0; i < test_case->program_args.length; i += 1) {
            printf(" %s", test_case->program_args.at(i));
        }
        printf("\n");
        printf("==== Test failed. Expected output: ====\n");
        printf("%s\n", test_case->output);
        printf("========= Actual output: ==============\n");
        printf("%s\n", buf_ptr(&program_stdout));
        printf("=======================================\n");
        exit(1);
    }

    for (int i = 0; i < test_case->source_files.length; i += 1) {
        TestSourceFile *test_source = &test_case->source_files.at(i);
        remove(test_source->relative_path);
    }
}

static void run_all_tests(bool reverse) {
    if (reverse) {
        for (int i = test_cases.length - 1; i >= 0; i -= 1) {
            TestCase *test_case = test_cases.at(i);
            printf("Test %d/%d %s...", i + 1, test_cases.length, test_case->case_name);
            run_test(test_case);
            printf("OK\n");
        }
    } else {
        for (int i = 0; i < test_cases.length; i += 1) {
            TestCase *test_case = test_cases.at(i);
            printf("Test %d/%d %s...", i + 1, test_cases.length, test_case->case_name);
            run_test(test_case);
            printf("OK\n");
        }
    }
    printf("%d tests passed.\n", test_cases.length);
}

static void cleanup(void) {
    remove(tmp_source_path);
    remove(tmp_exe_path);
}

static int usage(const char *arg0) {
    fprintf(stderr, "Usage: %s [--reverse]\n", arg0);
    return 1;
}

int main(int argc, char **argv) {
    bool reverse = false;
    for (int i = 1; i < argc; i += 1) {
        if (strcmp(argv[i], "--reverse") == 0) {
            reverse = true;
        } else {
            return usage(argv[0]);
        }
    }
    add_compiling_test_cases();
    add_compile_failure_test_cases();
    run_all_tests(reverse);
    cleanup();
}
