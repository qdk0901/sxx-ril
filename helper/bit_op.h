#ifndef _BIT_OP_H_
#define _BIT_OP_H_
void set_data_buffer(unsigned char* buffer);
void set_data_pos(int pos);
void reset_read_pos();
void write_bits(unsigned char* bits, int len);
int read_bits(unsigned char* bits, int len);
void write_bits_8(unsigned char bits, int len);
void write_bits_16(unsigned short bits, int len);
void write_bits_32(unsigned int bits, int len);
void set_data_pos(int pos);
#endif
