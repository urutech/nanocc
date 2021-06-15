#include <stdio.h>
#include <stdlib.h>
#ifdef _MSC_VER
#include <io.h>
#define ssize_t int
#else
#include <unistd.h>
#endif

// nanocc treats all # lines as comments
#include "nanocc-itf.h"
#define _sys_read  read
#define _sys_write write
#define _sys_exit  exit

// elf32 binary generator

enum P_FLAGS { PF_R = 4, PF_W = 2, PF_X = 1 };
enum SH_FLAGS { SHF_WRITE = 1, SHF_ALLOC = 2, SHF_EXECINSTR = 4 };
enum SH_TYPE { SHT_PROGBITS = 1, SHT_STRTAB = 3, SHT_NOBITS = 8 };

int gen_write_pad(int count);
void gen_backpatching(int string_base, int idata_base, int data_base);

int write_bytes(int count, int b1, int b2, int b3, int b4)
{
    char buffer[4];
    buffer[0] = b1;
    buffer[1] = b2;
    buffer[2] = b3;
    buffer[3] = b4;
    _sys_write(1, buffer, count);
    return count;
}

int write_elf_header(int e_entry, int e_shoff, char e_phnum, char e_shnum)
{
    char a[4];
    int null;

    null = 0;

    a[0] = 0; a[1] = 0; a[2] = 0; a[3] = 0;

    write_bytes(4, 0x7f, 'E', 'L', 'F');   // e_ident
    write_bytes(4, 0x01, 0x01, 0x01, 0);
    _sys_write(1, &null, 4);
    _sys_write(1, &null, 4);
    write_bytes(2, 0x02, 0x00, 0, 0);      // e_type
    write_bytes(2, 0x03, 0x00, 0, 0);      // e_machine
    write_bytes(4, 0x01, 0x00, 0x00, 0x00);// e_version
    _sys_write(1, &e_entry, 4);            // e_entry
    write_bytes(4, 0x34, 0x00, 0x00, 0x00);// e_phoff: offset to program header (= size of elf header)

    _sys_write(1, &e_shoff, 4);            // e_shoff
    _sys_write(1, &null, 4);               // e_flags
    write_bytes(2, 0x34, 0x00, 0, 0);      // e_ehsize ELF header size
    write_bytes(2, 0x20, 0x00, 0, 0);      // e_phentsize: size of one program header table entry
    a[0] = e_phnum;
    _sys_write(1, a, 2);                   // e_phnum
    write_bytes(2, 0x28, 0x00, 0, 0);      // e_shentsize: size of one section header table entry
    a[0] = e_shnum;
    _sys_write(1, a, 2);                   // e_shnum
    a[0] = e_shnum-1;                      // e_shstrndx is the last section
    _sys_write(1, a, 2);                   // e_shstrndx

    return 0x34;  // 52 bytes
}

int write_elf_ph(int p_offset, int p_vaddr, int p_filesz, int p_memsz, int p_flags, char p_align)
{
    char a[4];
    int null, one;

    null = 0;
    one = 1;
    a[0] = 0; a[1] = 0; a[2] = 0; a[3] = 0;

    _sys_write(1, &one, 4);                 // p_type: type of segment
    _sys_write(1, &p_offset, 4);            // p_offset
    _sys_write(1, &p_vaddr, 4);             // p_vaddr
    _sys_write(1, &null, 4);                // p_paddr (= 0)
    _sys_write(1, &p_filesz, 4);            // p_filesz
    _sys_write(1, &p_memsz, 4);             // p_memsz
    _sys_write(1, &p_flags, 4);             // p_flags
    a[0] = p_align;
    _sys_write(1, a, 4);                    // p_align

    return 0x20;
}

int write_elf_sh(int sh_name, int sh_type, int sh_flags, int sh_addr, int sh_offset, int sh_size, int sh_align)
{
    int null;

    null = 0;
    _sys_write(1, &sh_name, 4);            // sh_name
    _sys_write(1, &sh_type, 4);            // sh_type
    _sys_write(1, &sh_flags, 4);           // sh_flags
    _sys_write(1, &sh_addr, 4);            // sh_addr
    _sys_write(1, &sh_offset, 4);          // sh_offset
    _sys_write(1, &sh_size, 4);            // sh_size
    _sys_write(1, &null, 4);               // sh_link
    _sys_write(1, &null, 4);               // sh_info
    _sys_write(1, &sh_align, 4);           // sh_align
    _sys_write(1, &null, 4);  // sh_entsize

    return 4 * 10;
}

void gen_library(int emit_pos, char *symbol_name[], int *symbol_type, int *symbol_address, int symbol_count)
{
    int symidx;

    symidx = parse_lookup_symbol("_sys_exit");
    if (symidx > 0 && symbol_address[symidx] == 0) {
        int temp;

        symbol_address[symidx] = emit_pos;
        emit_pos += gen_emitbytes(4, 0x8b, 0x44, 0x24, 0x04);   // mov    eax,DWORD PTR [esp+0x4]
        emit_pos += gen_emitbytes(2, 0x89, 0xc3, 0, 0);         // mov    ebx,eax
        emit_pos += gen_emitbyte(0xb8);                         // mov    eax, 1
        temp = 1;
        emit_pos += gen_emitdword(temp);
        emit_pos += gen_emitbytes(2, 0xcd, 0x80, 0, 0);         // int    0x80      ; syscall
    }

    symidx = parse_lookup_symbol("_sys_write");
    if (symidx > 0 && symbol_address[symidx] == 0) {
        int temp;

        symbol_address[symidx] = emit_pos;

        emit_pos += gen_emitbytes(3, 0x55, 0x89, 0xe5, 0);          // push ebp  / mov ebp, esp
        emit_pos += gen_emitbytes(3, 0x52, 0x51, 0x53, 0);          // push edx / push ecx / push ebx
        emit_pos += gen_emitbytes(3, 0x8b, 0x5d, 0x08, 0);          // mov    ebx,DWORD PTR [ebp+8]
        emit_pos += gen_emitbytes(3, 0x8b, 0x4d, 0x0c, 0);          // mov    ecx,DWORD PTR [ebp+12]
        emit_pos += gen_emitbytes(3, 0x8b, 0x55, 0x10, 0);          // mov    edx,DWORD PTR [ebp+16]
        emit_pos += gen_emitbyte(0xb8);
        temp = 4;
        emit_pos += gen_emitdword(temp);                            // mov    eax, 4
        emit_pos += gen_emitbytes(2, 0xcd, 0x80, 0, 0);             // int    0x80      ; syscall
        emit_pos += gen_emitbytes(3, 0x5b, 0x59, 0x5a, 0);          // pop ebx / pop ecx / pop edx
        emit_pos += gen_emitbytes(4, 0x89, 0xec, 0x5d, 0xc3);       // mov esp, ebp   / pop ebp  /  ret
    }

    symidx = parse_lookup_symbol("_sys_read");
    if (symidx > 0 && symbol_address[symidx] == 0) {
        int temp;

        symbol_address[symidx] = emit_pos;

        emit_pos += gen_emitbytes(3, 0x55, 0x89, 0xe5, 0);          // push ebp  / mov ebp, esp
        emit_pos += gen_emitbytes(3, 0x52, 0x51, 0x53, 0);          // push edx / push ecx / push ebx
        emit_pos += gen_emitbytes(3, 0x8b, 0x5d, 0x08, 0);          // mov    ebx,DWORD PTR [ebp+8]
        emit_pos += gen_emitbytes(3, 0x8b, 0x4d, 0x0c, 0);          // mov    ecx,DWORD PTR [ebp+12]
        emit_pos += gen_emitbytes(3, 0x8b, 0x55, 0x10, 0);          // mov    edx,DWORD PTR [ebp+16]
        emit_pos += gen_emitbyte(0xb8);
        temp = 3;
        emit_pos += gen_emitdword(temp);                            // mov    eax, 3
        emit_pos += gen_emitbytes(2, 0xcd, 0x80, 0, 0);             // int    0x80      ; syscall
        emit_pos += gen_emitbytes(3, 0x5b, 0x59, 0x5a, 0);          // pop ebx / pop ecx / pop edx
        emit_pos += gen_emitbytes(4, 0x89, 0xec, 0x5d, 0xc3);       // mov esp, ebp   / pop ebp  /  ret
    }
}

void gen_write_binary(char *emit_buffer, int emit_pos, char *string_table_buffer, int string_table_size)
{
    int n;
    int e_shoff, code_size;
    int string_base, e_entry;

    e_entry = 0x400080;
    n = 0;
    code_size = emit_pos + string_table_size;
    e_shoff = 0x80 + code_size + 28;
    e_shoff += (16 - e_shoff%16);

    n += write_elf_header(e_entry, e_shoff, 2, 4);
    n += write_elf_ph(0x80, e_entry, code_size, 0x10000+0x80, PF_R+PF_X, 16);
    n += write_elf_ph(0x80, 0x413000, 0, 0x800000, PF_R+PF_W, 16);
    n += gen_write_pad(16-n%16);

    string_base = e_entry + code_size - string_table_size;
    gen_backpatching(string_base, 0, 0x413000);

    n += _sys_write(1, emit_buffer, emit_pos);
    n += _sys_write(1, string_table_buffer, string_table_size);
    n += _sys_write(1, "\0.shstrtab", 10);
    n += _sys_write(1, "\0.text", 6);
    n += _sys_write(1, "\0.bss", 5);
    n += _sys_write(1, "\0.data\0", 7);
    n += gen_write_pad(16-n%16);
    n += gen_write_pad(40);
    n += write_elf_sh(11, SHT_PROGBITS, SHF_EXECINSTR+SHF_ALLOC, e_entry, 0x80, code_size, 16);  // .text
    n += write_elf_sh(17, SHT_NOBITS, SHF_ALLOC+SHF_WRITE, 0x413000, 0x80, 0x800000, 16); // .bss
    n += write_elf_sh(1, SHT_STRTAB, 0, 0, 0x80+code_size, 28, 1); // .shstrtab
}
