
static unsigned char* g_buffer;
static int g_data_pos;
static int g_read_pos;

void set_data_buffer(unsigned char* buffer)
{
	g_buffer = buffer;
}

void set_data_pos(int pos)
{
	g_data_pos = pos;
}

void set_read_pos(int pos)
{
	g_read_pos = pos;	
}

void write_bits(unsigned char* bits, int len)
{
	int remain = len;
	while (remain > 0) {
		int dst_byte_index = g_data_pos >> 3;
		int dst_bit_index = g_data_pos & 0x07;
		int src_byte_index = (len - remain) >> 3;

		unsigned short* p = (unsigned short*)(g_buffer + dst_byte_index);
		unsigned short mask;
		unsigned short d = bits[src_byte_index];
		if (remain < 8) {
			mask = (1 << remain) - 1;
			g_data_pos += remain;
			remain -= remain;
		} else {
			mask = 0xFF;
			g_data_pos += 8;
			remain -= 8;
		}

		d <<= dst_bit_index;
		mask <<= dst_bit_index;
		*p &= ~mask;
		*p |= d;
	}
}

int read_bits(unsigned char* bits, int len)
{
	int remain = len;
	if (g_read_pos + remain > g_data_pos)
		return -1;

	while (remain > 0) {
		int src_byte_index = g_read_pos >> 3;
		int src_bit_index = g_read_pos & 0x07;
		int dst_byte_index = (len - remain) >> 3;
		
		unsigned short* p = (unsigned short*)(g_buffer + src_byte_index);
		unsigned short mask;
		if (remain < 8) {
			mask = (1 << remain) - 1;		
			g_read_pos += remain;
			remain -= remain;
		} else {
			mask = 0xFF;
			g_read_pos += 8;
			remain -= 8;
		}
		bits[dst_byte_index] = (*p >> src_bit_index) & mask;
	}
	return 0;
}

// len <= 8
void write_bits_8(unsigned char bits, int len)
{
	unsigned char d = bits;
	write_bits(&bits, len);
}

// len <= 16
void write_bits_16(unsigned short bits, int len)
{
	unsigned short d = bits;
	write_bits(&bits, len);
}

// len <= 32
void write_bits_32(unsigned int bits, int len)
{
	unsigned int d = bits;
	write_bits(&bits, len);
}

void dump_buffer()
{
	int ret;
	printf("bits:\n");
	set_read_pos(0);
	unsigned char d;
	while (read_bits(&d, 8) == 0) {
		printf("%02x ", d);
	}
	printf("\n");
}
void test_bit_op()
{
	int i;
	set_data_pos(0);
	for (i = 0; i < 16; i++) {
		write_bits_8(i, 7);
		write_bits_16(0, 9);
	}
	dump_buffer();
	set_data_pos(0);
	set_read_pos(0);
	
	unsigned char d;
	unsigned int rd;
	write_bits_32(0, 1);
	write_bits_32(0x555555, 24);
	read_bits(&rd, 25);
	printf("%08x\n", rd);
}

#define PHONE_BOOK_BUF_LEN  (420)

typedef struct {
        int  index;
        char *alpha;
        int  code;
	int  number_length;
        char *number;
}phone_book_t;

unsigned char g_pb_buffer[PHONE_BOOK_BUF_LEN];
static char g_alpha_buf[64];
static char g_number_buf[64];

static void retrive_alpha(char* alpha)
{
	char tmp[64];
	int should_trim = 0;
	char* dst = tmp;
	char* src = alpha;

	if (alpha[0] == '0' && alpha[1] == '0')
		should_trim = 1;
	else {
		// UCS2 Magic
		*dst++ = '8';
		*dst++ = '0';	
	}
	
	int len = strlen(alpha);
	int i;
	for (i = 0; i < len;) {
		if (should_trim) {
			i += 2;
			src += 2;
		}
		*dst++ = *src++;	
		*dst++ = *src++;
		i += 2;	
	}
	*dst = 0;
	strcpy(alpha, tmp);
}

static void write_alpha(char* alpha)
{
	int i;
	char alpha_buf[64];
	strcpy(alpha_buf, alpha);
	retrive_alpha(alpha_buf);

	int len = strlen(alpha_buf);
	for (i = 0; i < len; i++)
		write_bits_8(alpha_buf[i], 8);
}
static void write_number_length(int len)
{
	char c[3] = {0};
	sprintf(c, "%02X", len);
	write_bits(c, 2 * 8);
}

static void retrive_number(char* number)
{
	char tmp[64];
	char* dst = tmp;
	char* src = number;
	int i;
	int len = strlen(number);
	for (i = 0; i < len - 1; i += 2) {
		if (*src == '+') {
			src++;
			i++;
			continue;	
		}
		*(dst + 1) = *src;
		*dst = *(src + 1);
		src += 2;
		dst += 2;
	}
	if (i < len) {
		*(dst + 1) = *src;
		*dst = 'F';
		dst += 2;
		*dst = 0;	
	}
	strcpy(number, tmp);
}

static void write_number(char* number)
{
	int i, len;
	char number_buf[64];
	if (strlen(number) == 0)
		return;

	strcpy(number_buf, number);
	retrive_number(number_buf);
	len = strlen(number_buf);
	for (i = 0; i < len; i++)
		write_bits_8(number_buf[i], 8);
}

static void write_internat_code(int code)
{
	char c[3] = {0};
	sprintf(c, "%02X", code);
	write_bits(c, 2 * 8);
}

static void convert_phone_book(phone_book_t* pb, char* buffer)
{
	set_data_buffer(buffer);
	set_data_pos(0);
	write_alpha(pb->alpha);
	set_data_pos(28 * 8);
	write_number_length((strlen(pb->number) + 1) / 2 + 1);
	set_data_pos(30 * 8);	
	write_internat_code(pb->code);
	set_data_pos(32 * 8);
	write_number(pb->number);
}

static void read_alpha(char* alpha)
{
	read_bits(alpha, 28 * 8);

	char tmp[64];
	int is_ucs2 = 0;
	char* dst = tmp;
	char* src = alpha;

	if (alpha[0] == '8' && alpha[1] == '0') {
		src += 2;
		is_ucs2 = 1;
	}
	
	int len = strlen(src);
	int i;
	for (i = 0; i < len;) {
		if ((src[0] == 'F' && src[1] == 'F') ||
			(src[0] == 'f' && src[1] == 'f')) {
			break;
		}
		if (!is_ucs2) {
			*dst++ = '0';
			*dst++ = '0';
		}
		*dst++ = *src++;	
		*dst++ = *src++;
		i += 2;
	}
	*dst = 0;
	strcpy(alpha, tmp);
}

static void read_number_length(int* len)
{
	char c[16] = {0};
	read_bits(c, 2 * 8);
	sscanf(c, "%02x", len);
	*len = (*len - 1) * 2;
}

static void read_internate_code(int* code)
{
	char c[16] = {0};
	read_bits(c, 2 * 8);
	sscanf(c, "%02x", code);
}

static void read_number(char* number, int len)
{
	char tmp[64];
	char* dst = tmp;
	char* src = number;
	

	read_bits(number, len * 8);
	
	int l = strlen(number);
	int i;
	for (i = 0; i < l - 1; i += 2) {
		*(dst + 1) = *src;
		*dst = *(src + 1);
		src += 2;
		dst += 2;
	}
	if (*(dst - 1) == 'F' || *(dst - 1) == 'f')
		dst--;
	*dst = 0;
	strcpy(number, tmp);
}

static void extract_phone_book(phone_book_t* pb, char* buffer, int buffer_len)
{
	set_data_buffer(buffer);
	set_read_pos(0);
	set_data_pos(buffer_len * 8);
	read_alpha(g_alpha_buf);
	read_number_length(&pb->number_length);
	read_internate_code(&pb->code);
	read_number(g_number_buf, pb->number_length);
	pb->alpha = g_alpha_buf;
	pb->number = g_number_buf;
}

int main()
{
	phone_book_t pb;
	pb.alpha = "00420069006C006C0079002E006A007400680072";
	pb.code = 145;
	pb.number = "13528727035";

	memset(g_pb_buffer, 0x0, 320);
	memset(g_pb_buffer, 'F', 56);

	convert_phone_book(&pb, g_pb_buffer);
	printf("%s\n", g_pb_buffer);

	char* data = "7373ffffffffffffffffffffffff07813125787230f5ffffffffffff";
	int len = strlen(data);
	extract_phone_book(&pb, data, len);
	printf("alpha: %s\n", pb.alpha);
	printf("internate code: %d\n", pb.code);
	printf("number length: %d\n", pb.number_length);
	printf("number: %s\n", pb.number);
	return 0;
}
