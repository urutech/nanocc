# nanocc

**nanocc is a compiler for a subset of C generating i386 (32 bit) elf executables**

## Why?

Bootstrapping has caught my interest. I was thinking: what is a suitable subset of C to implement a C compiler in?
It would need to be small enough to even think about it to code that in assembly language. I was not actually interested in
coding it in assembly but it should be considered doable. While I was thinking about that I came to the following conclusion.

## nano-c as a subset of C

nano-c should be a true subset of C. This means that a valid nano-c program should also be a valid C program. This would allow me to implement
the first version of the nano-c compiler in C. Rewriting it to nano-c should be a trivial task (it's the same). And after nano-c can compile itself,
the route to bootstrapping is pretty much paved. This is the nano-c language I came up with:


```
program ::= (vardecl | fundecl)*

vardecl ::= type IDENTIFIER ('[' expr ']')? (',' type IDENTIFIER ('[' expr ']')?)* ';'
           | 'enum' IDENTIFIER? '{' IDENTIFIER ('=' expr)?  (',' IDENTIFIER ('=' expr)?)* '}' IDENTIFIER? ';'

fundecl ::= type IDENTIFIER '(' args ')' (stmtblock | ';')

args ::= 'void' | type IDENTIFER ('[' ']')? (',' type IDENTIFER ('[' ']')?)*

stmtblock ::= '{' vardecl* stmt* '}'

stmt ::= 'if'  '(' expr ')' stmt ('else' stmt)?
       | 'while'  '(' expr ')' stmt
       | 'do' stmt 'while'  '(' expr ')' ';'
       | stmtblock
       | 'goto' IDENTIFIER ';'
       | IDENTIFER ':' stmt
       | 'return'? expr? ';'
       | 'continue' ';'
       | 'break' ';'

type ::= ('int' | 'char' | 'void') '*'?

expr ::= "Normal C expression"
```


Obviously this language has some major restrictions:
* No other data types besides int, char and void. No unsigned, short, float, double, struct or union.
* Only pointer to basic types (int, char, void) or array of pointers
* Enums can only be used as names for constants, not as types
* No multi dimensional array
* No for-loops
* No switch statement
* No static keyword and therefore also no local variables with global storage class
* No keywords such as register, volatile and the like
* No initialization not for global and nor for local variables
* No sizeof
* No error text but error numbers and the compiler stops after the first error
* No preprocessor
* No type casts
* No function pointers

Seems like a lot of NOs. So what can that nano-c language do? Well, obviously it is possible to write a compiler for nano-c in nano-c. And the limitations are bearable. For example is the absence of structs much less of a deal than one would think. Instead of `symbol[symidx].name` it is `symbol_name[symidx]`. So I'm using as many array variables as the respective struct has members.

Not having the possibility to declare a variable and initialize it with a value at the same time is a disciplinary effort. Instead of `int x = 0;` it is `int x; x = 0;`. The reason for not implementing structs, multi dimensional arrays and initialization actually are related to each other. Initialization of variables requires quite some code. This is especially true to match the initialization in curly brackets to combinations of structs in struct of arrays ... So I left the work to do that once and for all when the full type system is in place. This is also the reason to not implement sizeof nor type casts.

Talking about the type system. That is one int describing the type of a variable. The lower 8 bits are reserved for the base type (int, char or void) and the remaining 24 bits are flags indicating (among other things) ARRAY and/or POINTER.
This is possible because only arrays of base type or pointer are allowed but not pointer to arrays. Therefore, if both flags are set (ARRAY and POINTER) then it is an array of pointers. And when only one of the two flags are set it's an array of a base type or a pointer to a base type, respectively.

I've sort of rediscovered enums during the implementation of nanocc. Enums are easy to implement and they cover a lot of the functionality that many programmers leave to the preprocessor: constant values used throughout the code. `enum { MAX_SYMBOLS = 1024 };` is a very good substitute for `#define MAX_SYMBOLS 1024`. By the way: all hash lines (starting with #) are treated as single line comments.

The language is LL(1) with one exception: labeled statements (used for goto). In order to keep parsing simple, I definitly wanted the nano-c grammar to be LL(1) to realize it as a recursive descent parser with only one lookahead token. Remembering the colon (':') after the identifier then makes it LL(2).

Another exception of LL(1) is the expression parser, which is implemented as an operator precedence parser based on the shunting yard algorithm. That is the reason why the above listed grammar does only say "normal C expression". I think that the expression parser is pretty complete including recursive function calls, pointer and array arithmetic and pre- and postfix operations. What is not implemented is the comma operator (except in function arguments, of course), because that operator is seldomly used besides in for loops. And that is also the reason to not implement for loops as well - and while and do/while are covering all loop types required. The expression parser does not work well in case of syntactically wrong expressions, and I did not test it much.

## Implementation design of nanocc

nannocc is a nano-c compiler written in nano-c, that can compile itself. Of course any C compiler will be able to compile nanocc as well, since nano-c is a subset of C. The implementation is in the file nanocc.c. It's a program that reads from stdin and writes to stdout. This is considered to be the easiest approach that does not have to deal with opening and closing files, printing an error, if the file is not found and so on.

nanocc directly outputs binary code for the i386 32-bit processor in an elf executable format. Therefore it is only able to generate executables from single source files (like nanocc.c). The generated code is not optimized at all. In fact it is brain dead stupid code that resembles a stack machine. Every expression is realized like a stack machine would do it. `a = b + c` is compiled into something like `b c + a =` with every single instruction on the way popping the operands of the stack and pushing the result back on the stack. Ease of implementation and correct operation had much higher priority than optimization, for me.

The compiler's symbol table is nothing else than a few arrays (symbol_name, symbol_type, ...) with the index into those arrays being the symbol id throughout compilation, usually called symidx. Adding a symbol to the symbol table increments the global symbol_count variable and searching a symbol visits all symbols from last (symbol_count-1) to first (0) and compares the symbol name. Usually this is a hash table implementation, but for reasons of simplicity here it is a flat array. Deleting a symbol is not possible, instead it is realized as overwriting symbol_name[symidx] with 0 so that it can not be found anymore. This turned out to be very effective when dealing with variable scope in nested stmtblocks. Since searching works from bottom to top: the innermost (last added) symbol is found first. And at the end of the stmtblock: all local variables, the ones whos index is equal or higher than the symbol_count at the beginning of the stmtblock, getting their name assigned to 0.

Function calls work like usual: parameters are pushed on the stack from right to left and the stack frame uses EBP register with offsets +8 and above for parameters and with negative offsets for local variables.

The generated executables consist of two segments: .text and .bss. Since we don't support initialized data, no .data segment is needed. The .bss segment is fixed to 8 MB in size, which is big enough by far to hold all global variables of the compiler (and most other programs). Strings are part of .text segment (directly after the generated code).

When you look into the source of nanocc.c, you will notice that most functions are either called lex_xxx, parse_xxx or gen_xxxx. The prefix lex, parse or gen denote what part of the compiler that function belongs to: the lexical analysis, the parser or the code generator.

## Essence of C
In a way nano-c resembles the essence of C. Everything that is really typical in C like pointer arithmetic x[a] = *(x + a) with a being multiplied by the sizeof(*x) and the ++/-- pre- and postfix operators are identical in nano-c. When you take a look at the nanocc.c source file, it actually reads and smells like C. A couple of things are essential for C programs, that are missing in nano-c: Initialization is more than syntactic sugar, because it reduces code by having initialized data. Also function pointers really are required to implement certain things. So there is some more work to be done to turn nanocc into a real C compiler. To learn how to write a compiler, compiler bootstrapping and as the basis for other projects this might be anyway useful for some people.

## How to use

Like you would expect, calling make will build nanocc. After the make step, you will find a 32-bit executable in your working directory. The make step involves compiling of nanocc.c and elf32.c and linking the resulting .o files to the nanocc executable.

```
make
ls
elf32.c elf32.o Makefile nanocc.c nanocc.o nanocc pe32.c README.md
```
It was said earlier that nanocc.c is a single source file and now we are compiling two files (nanocc.c and elf32.c)? In order to add a little flexibility, and to show how cross compilation can be done, the executable file format generating part is put into a different file. But it can be concatenated to a single file. The following shows that:

```
cat nanocc.c elf32.c | ./nanocc > nanocc_elfx86_elfx86 && chmod +x nanocc_elfx86_elfx86
cat nanocc.c elf32.c | ./nanocc_elfx86_elfx86 > nanocc_elfx86_elfx86-2 && chmod +x nanocc_elfx86_elfx86-2
diff nanocc_elfx86_elfx86 nanocc_elfx86_elfx86-2
cat nanocc.c pe32.c | ./nanocc_elfx86_elfx86 > nanocc_elfx86_pex86 && chmod +x nanocc_elfx86_pex86
cat nanocc.c pe32.c | ./nanocc_elfx86_pex86 > nanocc_pex86_pex86.exe
```

The above commands do the following:

First we use nanocc to compile itself. The naming convention used throughout this paragraph is that *nanocc_rr_gg* denotes a compiler that executes on *rr* and generates *gg*. So, the first thing we need is a compiler that runs on x86 (32bit) linux (elfx86) that also generates code for the same platform. According to that naming convention, we want to have nanocc_elfx86_elfx86

```
cat nanocc.c elf32.c | ./nanocc > nanocc_elfx86_elfx86 && chmod +x nanocc_elfx86_elfx86
```

The `&& chmod +x nanocc_elfx86_elfx86` is only to mark the file as executable.
Let's see if the new compiler can also generate itself:

```
cat nanocc.c elf32.c | ./nanocc_elfx86_elfx86 > nanocc_elfx86_elfx86-2 && chmod +x nanocc_elfx86_elfx86-2
diff nanocc_elfx86_elfx86 nanocc_elfx86_elfx86-2
```

Expectation is that diff will not show any differences between the two. If diff does not show any differences, then nanocc was successfully able to reproduce itself. Obviously nanocc (result of the make proccess based on gcc) will not be identical to nanocc_elfx86_elfx86, because it was compiled by a different compiler.

## Bootstrapping and cross compiling

Now the above mentioned flexibility helps to generate a different executable format. I have implemented an alternative to elfx86.c for windows: **pe32.c**. PE stands for portable executable format and is the Microsoft format for exe files on windows. We can build a compiler that creates exe files with the following command:

```
cat nanocc.c pe32.c | ./nanocc_elfx86_elfx86 > nanocc_elfx86_pex86 && chmod +x nanocc_elfx86_pex86
```

We feed the same compiler source (nanocc.c) together with the different executable format generator into nanocc_elfx86_elfx86. And since nanocc_elfx86_elfx86 can only generate code for elfx86 the resulting compiler will also only run on elfx86, but it will produce pe32 formatted output. That's why the new compiler we have generated is called nanocc_elfx86_pex86. We still run on linux but we have a cross compiler at hand that generates windows exe files. We use that:

```
cat nanocc.c pe32.c | ./nanocc_elfx86_pex86 > nanocc_pex86_pex86.exe
```

The resulting program is a nano-c compiler running on windows and it is called nanocc_pex86_pex86.exe. To test it, we of course need to run a Windows machine and test it there. Out of one set of source files we have created essentially three versions of the nano-c compiler:
* nanocc_elfx86_elfx86: running on linux creating linux executables
* nanocc_elfx86_pex86: running on linux creating Windows executables
* nanocc_pex86_pex86.exe: running on Windows creating Windows executables
