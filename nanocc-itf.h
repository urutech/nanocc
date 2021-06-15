#ifndef _NANOCC_ITF_H
#define _NANOCC_ITF_H

int _sys_read(int fd, char *buf, int count);
int _sys_write(int fd, char *s, int n);
int _sys_exit(int code);

int gen_write_pad(int count);
void gen_backpatching(int string_base, int idata_base, int data_base);
void gen_add_backpatch(int type, int offset);

int parse_lookup_symbol(char *name);

void gen_write_binary(char *emit_buffer, int emit_pos, char *string_table_buffer, int string_table_size);
void gen_library(int emit_pos, char *symbol_name[], int *symbol_type, int *symbol_address, int symbol_count);

int gen_emitbyte(int byte);
int gen_emitbytes(int count, int b1, int b2, int b3, int b4);
int gen_emitdword(int dword);

#endif
