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

Seems like a lot of NOs. So what can that nano-c language do? Well, obviously it is possible to write a compiler for nano-c in nano-c. And the limitations are bearable. For example is the absence of structs much less of a deal than one would think. Instead of ˋsymbol[symidx].nameˋ it is ˋsymbol_name[symidx]ˋ. So I'm using as many array variable as a struct has members. Not having the possibility to declare a variable and initialize it with a value at the same time is a disciplinary effort. Instead of ˋint x = 0;ˋ it is ˋint x; x = 0;ˋ. The reason for not implementing structs, multi dimensional arrays and initialization actually are related to each other. Initialization of global variables requires quite some code. This is especially true to match the initialization in curly brackets to combinations of structs in struct of arrays ... So I left the work to do that once and for all when the full type system is in place. Talking about the type system. That is one int describing the type of a variable. The lower 8 bits are reserved for the base type (int, char or void) and the remaining 24 bits are flags indicating ARRAY and/or POINTER.
This is possible because only arrays of base type or pointer are allowed but not pointer to arrays. Therefore if both flags are set (ARRAY and POINTER) then it is an array of pointers. And when only one of the two flags are set it's an array of a base type or a pointer to a base type.



