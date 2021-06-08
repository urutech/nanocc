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
&nbsp;&nbsp;&nbsp;&nbsp;           | 'enum' IDENTIFIER? '{' IDENTIFIER ('=' NUMBER)?  (',' IDENTIFIER ('=' NUMBER)?)* '}' IDENTIFIER? ';'

fundecl ::= type '*'? IDENTIFIER '(' args ')' (stmt | ';')

args ::= 'void' | type '*' ? IDENTIFER ('['']')? (',' type '*'? IDENTIFER ('['']')?)*

stmt ::= 'if'  '(' expr ')' stmt ('else' stmt)?\
&nbsp;&nbsp;&nbsp;&nbsp;       | while  '(' expr ')' stmt\
&nbsp;&nbsp;&nbsp;&nbsp;       | do stmt while  '(' expr ')' ';'\
&nbsp;&nbsp;&nbsp;&nbsp;       | '{' vardecl* stmt* '}'\
&nbsp;&nbsp;&nbsp;&nbsp;       | 'return'? expr? ';'\
&nbsp;&nbsp;&nbsp;&nbsp;       | IDENTIFER ':' stmt\
&nbsp;&nbsp;&nbsp;&nbsp;       | 'continue' ';'\
&nbsp;&nbsp;&nbsp;&nbsp;       | 'break' ';'

type ::= 'int' | 'char' | 'void'

expr ::= expr binop expr\
&nbsp;&nbsp;&nbsp;&nbsp;        | unop expr\
&nbsp;&nbsp;&nbsp;&nbsp;        | '(' expr ')'\
&nbsp;&nbsp;&nbsp;&nbsp;        | IDENTIFIER ('++'|'--')?\
&nbsp;&nbsp;&nbsp;&nbsp;        | IDENTIFIER '(' expr (',' expr)* ')'\

binop ::= '=' | '<' | '>=' | '==' | '!=' | '+' | '-' | '*' | '/' | '||' | '&&' | '|' | '&' | '^' | '<<' | '>>'

unop ::= '++' | '--' | '*' | '&' | '-' | '+' | '!' | '~'


Obviously this language would have some major restrictions.
* No other data types besides int, char and void. No unsigned, short, struct or union.
* Only pointer to basic types (int, char, void) or array of pointers
* No multi dimensional array
* No for loops
* No switch statement
* No static keyword and therefore also no local variables with global storage class
* no keywords such as register, volatile and the like
*
