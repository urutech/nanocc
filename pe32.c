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

// pe32 binary generator

enum PE32_SIZES {
    SECTION_SIZE = 40,
    IMPORT_TABLE_SIZE = 40,
    HEADER_SIZE = 512,
    IMAGE_BASE = 0x400000,
    TEXT_SEG   = 0x001000,
};

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

int write_opt_header(int SizeOfCode, int SizeOfInitializedData, int AddressOfEntryPoint, int BaseOfData, int ImageBase, int SizeOfImage)
{
    int n, SectionAlignment, FileAlignment, SizeOfHeaders;
    int SizeOfStackReserve, SizeOfStackCommit;
    int SizeOfHeapReserve, SizeOfHeapCommit;
    int import_table_size;

    n = 0;
    SectionAlignment   = 0x1000;
    FileAlignment      = 0x1000;
    SizeOfHeaders      = HEADER_SIZE;
    SizeOfStackReserve = 0x100000;
    SizeOfStackCommit  = 0x1000;
    SizeOfHeapReserve  = 0x100000;
    SizeOfHeapCommit   = 0x1000;
    import_table_size  = IMPORT_TABLE_SIZE;

    n += write_bytes(2, 0x0b, 0x01, 0, 0);          // Magic (PE32)
    n += write_bytes(2, 0x08, 0x00, 0, 0);          // LinkerVersion UNUSED
    n += _sys_write(1, &SizeOfCode, 4);             // SizeOfCode
    n += _sys_write(1, &SizeOfInitializedData, 4);  // SizeOfInitializedData
    n += gen_write_pad(4);                          // SizeOfUninitializedData UNUSED
    n += _sys_write(1, &AddressOfEntryPoint, 4);    // AddressOfEntryPoint
    n += _sys_write(1, &AddressOfEntryPoint, 4);    // BaseOfCode UNUSED
    n += _sys_write(1, &BaseOfData, 4);             // BaseOfData UNUSED
    n += _sys_write(1, &ImageBase, 4);              // ImageBase
    n += _sys_write(1, &SectionAlignment, 4);       // SectionAlignment
    n += _sys_write(1, &FileAlignment, 4);          // FileAlignment
    n += write_bytes(4, 0x04, 0x00, 0x00, 0x00);    // OperatingSystemVersion UNUSED
    n += write_bytes(4, 0x00, 0x00, 0x00, 0x00);    // ImageVersion  UNUSED
    n += write_bytes(4, 0x04, 0x00, 0x00, 0x00);    // SubsystemVersion
    n += write_bytes(4, 0x00, 0x00, 0x00, 0x00);    // Win32VersionValue UNUSED
    n += _sys_write(1, &SizeOfImage, 4);            // SizeOfImage  (incl 8MB bss)
    n += _sys_write(1, &SizeOfHeaders, 4);          // SizeOfHeaders
    n += write_bytes(4, 0x00, 0x00, 0x00, 0x00);    // CheckSum UNUSED
    n += write_bytes(2, 0x03, 0x00, 0x00, 0x00);    // Subsystem (02:Win32 GUI, 03:Console)
    n += write_bytes(2, 0x00, 0x00, 0x00, 0x00);    // DllCharacteristics UNUSED
    n += _sys_write(1, &SizeOfStackReserve, 4);     // SizeOfStackReserve
    n += _sys_write(1, &SizeOfStackCommit, 4);      // SizeOfStackCommit
    n += _sys_write(1, &SizeOfHeapReserve, 4);      // SizeOfHeapReserve
    n += _sys_write(1, &SizeOfHeapCommit, 4);       // SizeOfHeapCommit
    n += write_bytes(4, 0x00, 0x00, 0x00, 0x00);    // LoaderFlags  UNUSED
    n += write_bytes(4, 0x10, 0x00, 0x00, 0x00);    // NumberOfRvaAndSizes UNUSED
    n += gen_write_pad(8);                          // no export table
    n += _sys_write(1, &BaseOfData, 4);             // import table
    n += _sys_write(1, &import_table_size, 4);      // import_table_size
    n += gen_write_pad(112);                        // no other entries in the data directory

    return n;
}

int write_section(char *name,
    int VirtualSize, int VirtualAddress, int SizeOfRawData, int PointerToRawData,
    int Characteristics)
{
    int n;
    n = 0;

    n += _sys_write(1, name, 8);                 // name
    n += _sys_write(1, &VirtualSize, 4);         // VirtualSize
    n += _sys_write(1, &VirtualAddress, 4);      // VirtualAddress
    n += _sys_write(1, &SizeOfRawData, 4);       // SizeOfRawData
    n += _sys_write(1, &PointerToRawData, 4);    // PointerToRawData
    n += write_bytes(4, 0x00, 0x00, 0x00, 0x00); // PointerToRelocations UNUSED
    n += write_bytes(4, 0x00, 0x00, 0x00, 0x00); // PointerToLinenumbers UNUSED
    n += write_bytes(2, 0x00, 0x00, 0x00, 0x00); // NumberOfRelocations UNUSED
    n += write_bytes(2, 0x00, 0x00, 0x00, 0x00); // NumberOfLinenumbers UNUSED
    n += _sys_write(1, &Characteristics, 4);     // Characteristics

    return n;
}

void gen_library(int emit_pos, char *symbol_name[], int *symbol_type, int *symbol_address, int symbol_count)
{
    int symidx;

    symidx = parse_lookup_symbol("_sys_exit");
    if (symidx > 0 && symbol_address[symidx] == 0) {
        int temp;

        symbol_address[symidx] = emit_pos;
        emit_pos += gen_emitbytes(2, 0xff, 0x25, 0, 0);  // jmp    DWORD PTR ds:0x2028
        temp = 0x28; //0x402028;
		gen_add_backpatch(0x2800, emit_pos);
        emit_pos += gen_emitdword(temp);
    }

    symidx = parse_lookup_symbol("_sys_write");
    if (symidx > 0 && symbol_address[symidx] == 0) {
        int temp;

        symbol_address[symidx] = emit_pos;

        emit_pos += gen_emitbyte(0x55);                    // push   ebp
        emit_pos += gen_emitbytes(2, 0x89, 0xe5, 0, 0);    // mov    ebp,esp
        emit_pos += gen_emitbytes(3, 0x83, 0xec, 0x08, 0); // sub    esp,0x8
        emit_pos += gen_emitbytes(2, 0x6a, 0x00, 0, 0);    // push   0x0
        emit_pos += gen_emitbytes(3, 0x8d, 0x45, 0xfc, 0); // lea    eax,[ebp-0x4]
        emit_pos += gen_emitbyte(0x50);                    // push   eax
        emit_pos += gen_emitbytes(3, 0x8b, 0x45, 0x10, 0); // mov    eax,DWORD PTR [ebp+0x10]
        emit_pos += gen_emitbyte(0x50);                    // push   eax
        emit_pos += gen_emitbytes(3, 0x8b, 0x45, 0x0c, 0); // mov    eax,DWORD PTR [ebp+0x0c]
        emit_pos += gen_emitbyte(0x50);                    // push   eax

        // calc the GetSdtHandle input (-11 for stdout and -12 for stderr)
        emit_pos += gen_emitbyte(0xb8);                    // mov    eax, -10
        temp = -10;
        emit_pos += gen_emitdword(temp);
        emit_pos += gen_emitbytes(3, 0x2b, 0x45, 0x08, 0); // sub    eax,DWORD PTR [ebp+0x08]
        emit_pos += gen_emitbyte(0x50);                    // push   eax

        emit_pos += gen_emitbytes(2, 0xff, 0x15, 0, 0);    // call   DWORD PTR ds:0x202c   GetStdHandle
        temp = 0x2c;
        gen_add_backpatch(0x2800, emit_pos);
        emit_pos += gen_emitdword(temp);
        emit_pos += gen_emitbyte(0x50);                    // push   eax
        emit_pos += gen_emitbytes(2, 0xff, 0x15, 0, 0);    // call   DWORD PTR ds:0x2030   WriteFile
        temp = 0x30;
        gen_add_backpatch(0x2800, emit_pos);
        emit_pos += gen_emitdword(temp);
        emit_pos += gen_emitbytes(2, 0x09, 0xc0, 0, 0);    // or     eax,eax
        emit_pos += gen_emitbytes(2, 0x74, 0x03, 0, 0);    // je     +3
        emit_pos += gen_emitbytes(3, 0x8b, 0x45, 0xfc, 0); // mov    eax,DWORD PTR [ebp-4]
        emit_pos += gen_emitbytes(2, 0x89, 0xec, 0, 0);    // mov    esp,ebp
        emit_pos += gen_emitbyte(0x5d);                    // pop    ebp
        emit_pos += gen_emitbyte(0xc3);                    // ret
    }

    symidx = parse_lookup_symbol("_sys_read");
    if (symidx > 0 && symbol_address[symidx] == 0) {
        int temp;

        symbol_address[symidx] = emit_pos;

        emit_pos += gen_emitbyte(0x55);                    // push   ebp
        emit_pos += gen_emitbytes(2, 0x89, 0xe5, 0, 0);    // mov    ebp,esp
        emit_pos += gen_emitbytes(3, 0x83, 0xec, 0x08, 0); // sub    esp,0x8
        emit_pos += gen_emitbytes(2, 0x6a, 0x00, 0, 0);    // push   0x0
        emit_pos += gen_emitbytes(3, 0x8d, 0x45, 0xfc, 0); // lea    eax,[ebp-0x4]
        emit_pos += gen_emitbyte(0x50);                    // push   eax
        emit_pos += gen_emitbytes(3, 0x8b, 0x45, 0x10, 0); // mov    eax,DWORD PTR [ebp+0x10]
        emit_pos += gen_emitbyte(0x50);                    // push   eax
        emit_pos += gen_emitbytes(3, 0x8b, 0x45, 0x0c, 0); // mov    eax,DWORD PTR [ebp+0x0c]
        emit_pos += gen_emitbyte(0x50);                    // push   eax
        emit_pos += gen_emitbytes(2, 0x6a, 0xf6, 0, 0);    // push   -10
        emit_pos += gen_emitbytes(2, 0xff, 0x15, 0, 0);    // call   DWORD PTR ds:0x202c
        temp = 0x2c;
        gen_add_backpatch(0x2800, emit_pos);
        emit_pos += gen_emitdword(temp);
        emit_pos += gen_emitbyte(0x50);                    // push   eax
        emit_pos += gen_emitbytes(2, 0xff, 0x15, 0, 0);    // call   DWORD PTR ds:0x2034
        temp = 0x34;
        gen_add_backpatch(0x2800, emit_pos);
        emit_pos += gen_emitdword(temp);
        emit_pos += gen_emitbytes(2, 0x09, 0xc0, 0, 0);    // or     eax,eax
        emit_pos += gen_emitbytes(2, 0x74, 0x03, 0, 0);    // je     +3
        emit_pos += gen_emitbytes(3, 0x8b, 0x45, 0xfc, 0); // mov    eax,DWORD PTR [ebp-4]
        emit_pos += gen_emitbytes(2, 0x89, 0xec, 0, 0);    // mov    esp,ebp
        emit_pos += gen_emitbyte(0x5d);                    // pop    ebp
        emit_pos += gen_emitbyte(0xc3);                    // ret
    }
}

void gen_write_binary(char *emit_buffer, int emit_pos, char *string_table_buffer, int string_table_size)
{
    int n, e_lfanew, SizeOfOptionalHeader;
    int code_size, padded_code_size, data_size, offset, string_base;
    int idata_start, padded_data_size;

    n = 0;
    SizeOfOptionalHeader = 224;
    code_size = emit_pos + string_table_size;
    padded_code_size = code_size + (0x1000 - code_size % 0x1000);
    data_size = 0x200;
    padded_data_size = data_size + (0x1000 - data_size % 0x1000);

    n += write_bytes(2, 'M', 'Z', 0, 0);    // "MZ" e_magic
    n += gen_write_pad(26);
    n += gen_write_pad(32);
    e_lfanew = n + 4;
    n += _sys_write(1, &e_lfanew, 4);             // e_lfanew
    n += write_bytes(4, 'P', 'E', 0, 0);          // "PE"
    n += write_bytes(2, 0x4c, 0x01, 0, 0);        // Machine (Intel 386)
    n += write_bytes(2, 0x03, 0x00, 0, 0);        // NumberOfSections
    n += write_bytes(4, 0x5D, 0xBE, 0x45, 0x45);  // TimeDateStamp UNUSED
    n += gen_write_pad(4);                        // PointerToSymbolTable UNUSED
    n += gen_write_pad(4);                        // NumberOfSymbols UNUSED

    n += write_bytes(2, SizeOfOptionalHeader & 0xFF, (SizeOfOptionalHeader & 0xFF00) >> 8, 0, 0);  // SizeOfOptionalHeader
    n += write_bytes(2, 0x02, 0x01, 0, 0);       // Characteristics (no relocations, executable, 32 bit)

    n += write_opt_header(
            padded_code_size,  /* SizeOfCode */
            TEXT_SEG+padded_data_size+0x800000,          /* SizeOfInitializedData */
            TEXT_SEG,          /* AddressOfEntryPoint */
            TEXT_SEG+padded_code_size,         /* BaseOfData */
            IMAGE_BASE,        /* ImageBase */
            TEXT_SEG+padded_code_size+padded_data_size+0x800000);         /* SizeOfImage */

    n += write_section(
            ".text\0\0\0",    /* name */
            code_size,        /* VirtualSize */
            TEXT_SEG,         /* VirtualAddress */
            padded_code_size, /* SizeOfRawData */
            HEADER_SIZE,      /* PointerToRawData */
            0x60000020);      /* Characteristics */

    n += write_section(
            ".idata\0\0",   /* name */
            data_size,      /* VirtualSize */
            TEXT_SEG+padded_code_size,      /* VirtualAddress */
            padded_data_size,              /* SizeOfRawData */
            HEADER_SIZE+code_size+(512 - code_size%512),  /* PointerToRawData */
            0xC0000040);    /* Characteristics */

    n += write_section(
            ".data\0\0\0",  /* name */
            0x800000,       /* VirtualSize */
            TEXT_SEG+padded_code_size+padded_data_size,  /* VirtualAddress */
            0,              /* SizeOfRawData */
            0,              /* PointerToRawData */
            0xC0000040);    /* Characteristics */

    n += gen_write_pad(512 - n%512);  // align

    string_base = IMAGE_BASE + TEXT_SEG + code_size - string_table_size;
    gen_backpatching(string_base, IMAGE_BASE+TEXT_SEG+padded_code_size, IMAGE_BASE+TEXT_SEG+padded_code_size+padded_data_size);
    n += _sys_write(1, emit_buffer, emit_pos);
    n += _sys_write(1, string_table_buffer, string_table_size);

    n += gen_write_pad(512 - n%512);  // align

    idata_start = n;

    // import table
    /* offs   0 */ n += write_bytes(4, 0x00, 0x00, 0x00, 0x00); // [UNUSED] read-only IAT
    /* offs   4 */ n += write_bytes(4, 0x00, 0x00, 0x00, 0x00); // [UNUSED] timestamp
    /* offs   8 */ n += write_bytes(4, 0x00, 0x00, 0x00, 0x00); // [UNUSED] forwarder chain

    offset = TEXT_SEG+padded_code_size + 64;
    /* offs  12 */ n += _sys_write(1, &offset, 4);              // kernel32 library name

    offset = TEXT_SEG+padded_code_size + IMPORT_TABLE_SIZE;
    /* offs  16 */ n += _sys_write(1, &offset, 4);              // kernel32 IAT  pointer
    /* offs  20 */ n += gen_write_pad(20);                      // terminator (empty item)

    // kernel32 IAT
    offset = TEXT_SEG+padded_code_size + 78;
    /* offs  40 */ n += _sys_write(1, &offset, 4);              // pointer to ExitProcess
    offset = TEXT_SEG+padded_code_size + 94;
    /* offs  44 */ n += _sys_write(1, &offset, 4);              // pointer to GetStdHandle
    offset = TEXT_SEG+padded_code_size + 110;
    /* offs  48 */ n += _sys_write(1, &offset, 4);              // pointer to WriteFile
    offset = TEXT_SEG+padded_code_size + 124;
    /* offs  52 */ n += _sys_write(1, &offset, 4);              // pointer to ReadFile
    /* offs  56 */ n += write_bytes(4, 0x00, 0x00, 0x00, 0x00); // end of IAT

    /* offs  60 */ n += gen_write_pad(4 - n%4);      // align to 4 byte boundary
    /* offs  64 */ n += _sys_write(1, "kernel32.dll", 13);

    /* offs  77 */ n += gen_write_pad(2 - n%2);      // align to 2 byte boundary
    /* offs  78 */ n += _sys_write(1, "\0\0ExitProcess", 14);

    /* offs  92 */ n += gen_write_pad(2 - n%2);      // align to 2 byte boundary
    /* offs  94 */ n += _sys_write(1, "\0\0GetStdHandle", 15);

    /* offs 109 */ n += gen_write_pad(2 - n%2);      // align to 2 byte boundary
    /* offs 110 */ n += _sys_write(1, "\0\0WriteFile", 12);

    /* offs 122 */ n += gen_write_pad(2 - n%2);      // align to 2 byte boundary
    /* offs 124 */ n += _sys_write(1, "\0\0ReadFile", 11);

    /* offs 135 */ n += gen_write_pad(512 - n%512);  // align to 512 byte boundary
    /* offs 512 */
}
