
#include "sxx-ril.h"
#define PHONE_BOOK_BUF_LEN  (420)
#define PHONE_BUF_LEN 128
typedef struct {
        int  index;
        char alpha[PHONE_BUF_LEN];
        int  code;
	    int  number_length;
        char number[PHONE_BUF_LEN];
}phone_book_t;

unsigned char g_pb_buffer[PHONE_BOOK_BUF_LEN];
static char g_alpha_buf[128];
static char g_number_buf[128];

static void retrive_alpha(char* alpha, int force)
{
	char tmp[64] = {0};
	int should_trim = 0;
	char* dst = tmp;
	char* src = alpha;

    if (!force) {
	    if (alpha[0] == '0' && alpha[1] == '0')
		    should_trim = 1;
	    else {
		    // UCS2 Magic
		    *dst++ = '8';
		    *dst++ = '0';	
	    }
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

static void write_alpha(char* alpha, int force)
{
	int i;
	char alpha_buf[64] = {0};
	strcpy(alpha_buf, alpha);
	retrive_alpha(alpha_buf, force);
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
	char tmp[64] = {0};
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
	char number_buf[64] = {0};
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

static int is_empty_alpha(char* alpha)
{
    int len = strlen(alpha);
    int i = 0;
    for (i = 0; i < len / 4; i++) {
        if (alpha[4 * i ] != '0' || alpha[4 * i + 1] != '0' || alpha[4 * i + 2] != '2' || alpha[4 * i + 3] != '0')
            return 0;
    }
    return 1;
}

static void copy_number_to_alpha(char* alpha, char* number, int len)
{
       
    char* dst = alpha;
    char* src = number;
    int i;
    
    for (i = 0; i < len; i++) {
        if (*src < '0' || *src > '9') {
            src++;
            i++;
            continue;
        }
        *dst++ = '3';
        *dst++ = *src++;
    }
    *dst = 0;
       
}

static void convert_phone_book(phone_book_t* pb, char* buffer)
{
    int force = 0;
    
    if (is_empty_alpha(pb->alpha)) {
        int len = strlen(pb->number);     
        if (len > 13)
            len = 13;       
        copy_number_to_alpha(pb->alpha, pb->number, len);
        force = 1;
    }
	set_data_buffer(buffer);
	set_data_pos(0);
	write_alpha(pb->alpha, force);;
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
	strcpy(pb->alpha, g_alpha_buf);
	strcpy(pb->number, g_number_buf);
}


static int get_phone_book(int index, phone_book_t* pb)
{
	int ret = -1, err;
	char* cmd = NULL;
	char* line = NULL;
    char* buffer = NULL;
    ATResponse *p_response = NULL;
	asprintf(&cmd,"AT+CPBR=%d", index);

	at_send_command("AT+CSCS=\"UCS2\"", NULL);
	err = at_send_command_singleline(cmd,"+CPBR:",&p_response);

	if (err < 0 || !p_response || p_response->success == 0)
		goto error;

    	if (p_response && p_response->p_intermediates) {
		line=p_response->p_intermediates->line;
		err=at_tok_start(&line);
		if (err < 0) 
			goto error;
		
		err=at_tok_nextint(&line,&(pb->index));
		if (err < 0)
			goto error;
		
		err=at_tok_nextstr(&line,&buffer);
		if (err < 0) 
			goto error;

        strcpy(pb->number, buffer);
		
		err=at_tok_nextint(&line,&(pb->code));
		if (err < 0)
			goto error;
		
		err=at_tok_nextstr(&line,&buffer);

		if (err < 0) 
			goto error;
        
        strcpy(pb->alpha, buffer);
	}
	
	ret = 0;
error:
	at_response_free(p_response);
	free(cmd);
	return ret;	
}

static int set_phone_book(int index, phone_book_t* pb)
{
	int ret = -1, err;
	char* cmd = NULL;
	char* line = NULL;
    ATResponse *p_response = NULL;
	asprintf(&cmd,"AT+CPBW=%d, \"%s\", %d, \"%s\"", index, pb->number, pb->code, pb->alpha);

	at_send_command("AT+CSCS=\"UCS2\"", NULL);
	err = at_send_command(cmd, &p_response);
	if (err < 0 || !p_response || p_response->success == 0)
		goto error;
	ret = 0;
error:
	at_response_free(p_response);
	free(cmd);
	return ret;	
}

int sim_io_hook(RIL_SIM_IO * pRilSimIo,RIL_SIM_IO_Response * pSr)
{
	phone_book_t pb;
	if (pRilSimIo->data == NULL) {
		if (pRilSimIo->command == 192 && pRilSimIo->fileid == 28474) {
                        if (pRilSimIo->p3 == 15) {
                                pSr->sw1 = 0x90;
                                pSr->sw2 = 0x00;
                                pSr->simResponse = g_pb_buffer;
                                memset(pSr->simResponse, 0, PHONE_BOOK_BUF_LEN);
                                strncpy(pSr->simResponse, "00001B586F3A040011F0220102011C", 30);
                                return 0;
                        }
		} else if (pRilSimIo->command == 178 && pRilSimIo->fileid == 28474) {
                       if (pRilSimIo->p3 == 28) {
                                pSr->sw1 = 0x90;
                                pSr->sw2 = 0x00;
				memset(g_pb_buffer, 0x0, 320);
        		memset(g_pb_buffer, 'F', 56);
                
                memset(&pb, 0, sizeof(phone_book_t));
				if (get_phone_book(pRilSimIo->p1, &pb) == 0)
					convert_phone_book(&pb, g_pb_buffer);
                                LOGD("%s", g_pb_buffer);
                                pSr->simResponse = g_pb_buffer;
                                return 0;
                        }
		}
	} else {
		// p1	index
		// p2	record mode
		// p3	data length(binary length) 	
 		if (pRilSimIo->command == 220 && pRilSimIo->fileid == 28474) {
			extract_phone_book(&pb, pRilSimIo->data, pRilSimIo->p3 * 2);
			LOGD("alpha: %s", pb.alpha);
			LOGD("number: %s", pb.number);
			LOGD("code: %d", pb.code);
			if (set_phone_book(pRilSimIo->p1, &pb) == 0) {
				pSr->sw1 = 0x90;
                                pSr->sw2 = 0x00;
				return 0;
			}
		}	
	}
	
	return -1;
}

