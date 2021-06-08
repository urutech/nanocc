# nanocc

**nanocc is a compiler for a subset of C generatiing i386 (32 bit) elf executables**

## Why?

Bootstrapping has caught my interest. I was thinking: what is a suitable subset of C to implement a C compiler in?
It would need to be small enough to even think about it to code that in assembly language. I was not actually interested in
coding it in assembly but it should be considered doable. While I was thinking about that I came to the following conclusion.

## nano-c as a subset of C

nano-c should be a true subset of C. This means that a valid nano-c program should also be a valid C program. This would allow me to implement
the first version of the nano-c compiler in C. Rewriting it to nano-c should be a trivial task (it's the same). And after nano-c can compile itself,
the route to bootstrapping is pretty much paved. This is the nano=c-c language I came up with:


program ::= (vardecl | fundecl)*

vardecl ::= type '*'? IDENTIFIER ('[' NUMBER ']')? (',' '*'? IDENTIFIER ('[' NUMBER ']')?)* ';'\
          | 'enum' IDENTIFIER? '{' IDENTIFIER ('=' NUMBER)?  (',' IDENTIFIER ('=' NUMBER)?)* '}' IDENTIFIER? ';'

fundecl ::= type '*'? IDENTIFIER '(' args ')' (stmt | ';')

args ::= 'void' | type '*'? IDENTIFER ('['']')? (',' type '*'? IDENTIFER ('['']')?)*

stmt ::= 'if'  '(' expr ')' stmt ('else' stmt)?\
>       | while  '(' expr ')' stmt\
>       | do stmt while  '(' expr ')' ';'\
>       | '{' vardecl* stmt* '}'\
>       | 'return'? expr? ';'\
>       | IDENTIFER ':' stmt\
>       | 'continue' ';'\
>       | 'break' ';'

type ::= 'int' | 'char' | 'void'

expr ::= expr binop expr\
       | unop expr\
       | '(' expr ')'\
       | IDENTIFIER ('++'|'--')?\
       | IDENTIFIER '(' expr (',' expr)* ')'\

binop ::= '=' | '<' | '>=' | '==' | '!=' | '+' | '-' | '*' | '/' | '||' | '&&' | '|' | '&' | '^' | '<<' | '>>'

unop ::= '++' | '--' | '*' | '&' | '-' | '+' | '!' | '~'
