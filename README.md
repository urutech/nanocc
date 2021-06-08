# nanocc

**nanocc is a compiler for a subset of C generatiing i386 (32 bit) elf executables**

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

vardecl ::= type '*'? IDENTIFIER ('[' expr ']')? (',' '*'? IDENTIFIER ('[' expr ']')?)* ';'
           | 'enum' IDENTIFIER? '{' IDENTIFIER ('=' expr)?  (',' IDENTIFIER ('=' expr)?)* '}' IDENTIFIER? ';'

fundecl ::= type '*'? IDENTIFIER '(' args ')' (stmtblock | ';')

args ::= 'void' | type '*'? IDENTIFER ('[' ']')? (',' type '*'? IDENTIFER ('[' ']')?)*

stmtblock ::= '{' vardecl* stmt* '}'

stmt ::= 'if'  '(' expr ')' stmt ('else' stmt)?
       | 'while'  '(' expr ')' stmt
       | 'do' stmt 'while'  '(' expr ')' ';'
       | stmtblock
       | 'return'? expr? ';'
       | 'continue' ';'
       | 'break' ';'

type ::= 'int' | 'char' | 'void'

expr ::= "Normal C expressions"
```


Obviously this language would have some major restrictions:
* No other data types besides int, char and void. No unsigned, short, float, double, struct or union.
* Only pointer to basic types (int, char, void) or array of pointers
* enums can only be used as names for constants, not as types
* No multi dimensional array
* No for-loops
* No switch statement
* No static keyword and therefore also no local variables with global storage class
* No keywords such as register, volatile and the like
* No goto and labels
* No initialization not for global and nor for local variables
* No sizeof
* No error text but error numbers and the compiler stops after the first error
* No preprocessor

Seems like a lot of NOs. So what can that nano-c language do? Well, obviously it is possible to write a compiler for nano-c in nano-c. And the limitations are bearable. For example is the absence of structs much less of a deal than one would think. Instead of ˋsymbol[symidx].nameˋ it is ˋsymbol_name[symidx]ˋ. So I'm using as many array variable as a struct has members.

Not having the possibility to declare a variable and initialize it with a value at the same time is a disciplinary effort. Instead of ˋint x = 0;ˋ it is ˋint x; x = 0;ˋ. The reason for not implementing structs, multi dimensional arrays and initialization actually are related to each other. Initialization of variables requires quite some code. This is especially true to match the initialization in curly brackets to combinations of structs in struct of arrays ... So I left the work to do that once and for all when the full type system is in place. This is also the reason to not implement sizeof.

Talking about the type system. That is one int describing the type of a variable. The lower 8 bits are reserved for the base type (int, char or void) and the remaining 24 bits are flags indicating ARRAY and/or POINTER.
This is possible because only arrays of base type or pointer are allowed but not pointer to arrays. Therefore if both flags are set (ARRAY and POINTER) then it is an array of pointers. And when only one of the two flags are set it's an array of a base type or a pointer to a base type, respectively.

I've sort of rediscovered enums during the implmenetation of nanocc. Enums are easy to implement and they cover a lot of the functionality that many programmers leave to the preprocessor: constant values used throughout the code. ˋenum { MAX_SYMBOLS = 1024 };ˋ is a very good substitute for ˋ#define MAX_SYMBOLS 1024ˋ. By the way: all hash lines (starting with #) are treated like single line comments.

The reason not to support goto and labels is, beside the fact that they are unneeded to implement a compiler, that labels require to implement the parser with a lookahead of 2. I need to read the label and can only decide after reading the following token (':' or something else) if it really was a label or just an identifier. In order to keep parsing simple, I definitly wanted the nano-c grammar to be LL(1) to realize it as a recursive descent parser with only one lookahead token.

The expression parser is implemented as an operator precedence parser based on the shunting yard algorithm. That is the reason why the above listed grammer does only say "normal C expression". I think that the expression parser is pretty complete incl. recursive funtion calls, pointer and array arithmetic and pre- and postfix operations. What is not implemented is the comma operator (except in function arguments, of course), because that operaor is seldomly used besides in for loops. And that is also the reason to not implement for loops as well - and while and do/while are covering all loop types required.

##Implementation design of nanocc

nannocc is a nano-c compiler written in nano-c, that can compile itself. Of course any C compiler will be able to compile nanocc as well, since nano-c is a subset of C. The implementation is in the file nanocc.c. It's a prgram that reads from stdin and writes to stdout. This is considered to be the easiest approach that does not have to deal with opening and closing files, printing error, if the file is not found and so on.

nanocc directly outputs binary code for the i386 32-bit processor in an elf executable format. Therefore is is only able to generate executables from single source files (like nanocc.c). The code it generates is not optimized at all. In fact it is brain dead stupid code that resembles a stack machine. Every expression is realized like a stack machine would do it. ˋa = b + cˋ is compiled into something like ˋb c + a =ˋ with every single instruction on the way popping the operands of the stack and pushing the result back on the stack. For me ease of implementation and correct operation had mush higher priority than optimization.

The compilers symbol table is nothing else than a few arrays (symbol_name, symbol_type, ...) with the index into those arrays being the symbol id throughout the compiler, usually called symidx. Adding a symbol to the symbol table incements the global symbol_count variable and searching a symbol visits all symbols from last (symbol_count-1) to first (0) and compare the symbol name. Deleting a symbol is not possible, instead it is realized as overwriting symbol_name[symidx] with 0 so that it can not be found anymore. This turned out to be very effective when dealing with variable scope in nested stmtblocks. At the end of the stmtblock: all local variables are the ones whos index is equal or higher then the symbol_count at the beginning of the stmtblock. Deleting them is enough to realize nested scopes.
