#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>


#include "sxx-ril.h"


#define RIL_MAX_REQUEST 256
static modem_spec_t* g_modem = NULL;

typedef struct {
	int request;
	request_handler handler;
	int vid_pid;
}request_item_t;

request_item_t request_items[RIL_MAX_REQUEST];

void register_request(int request, request_handler handler, int vid_pid)
{
	int i;
	if (vid_pid != VID_PID_GENERIC && vid_pid != g_modem->vid_pid)
		return;
		
	for (i = 0; i < RIL_MAX_REQUEST; i++) {
		request_item_t* r = &request_items[i];
		if (r->request != 0 && r->request != request) 
			continue;

		if (r->request != 0 && r->vid_pid != VID_PID_GENERIC)
			break;

		if (r->request !=0 && vid_pid != VID_PID_GENERIC)
			LOGD("Override %s , %x", requestToString(request), vid_pid);
			
		//LOGD("register request handler: %s, %x, %x\n", requestToString(request), handler, vid_pid);
		r->request = request;
		r->handler = handler;
		r->vid_pid = vid_pid;

		break;

	}
}

#define RIL_MAX_UNSOLICITED 256

typedef struct {
	char* prefix;
	unsolicited_handler handler;
	int vid_pid;
}unsolicited_item_t;

unsolicited_item_t unsolicited_items[RIL_MAX_UNSOLICITED];

void register_unsolicited(char* prefix, unsolicited_handler handler, int vid_pid)
{
	int i;
	if (vid_pid != VID_PID_GENERIC && vid_pid != g_modem->vid_pid)
		return;
	
	for (i = 0; i < RIL_MAX_UNSOLICITED; i++) {
		unsolicited_item_t* u = &unsolicited_items[i];
		if (u->prefix != 0 && strcmp(u->prefix, prefix))
			continue;

		if (u->prefix != 0 && u->vid_pid != VID_PID_GENERIC)
			break;

		//LOGD("register unsolicited handler: %s, %x, %x\n", prefix, handler, vid_pid);
		u->prefix = prefix;
		u->handler = handler;
		u->vid_pid = vid_pid;

		break;

	}
}


#define MAX_SUPPORT_MODEM 10
static modem_spec_t modems[MAX_SUPPORT_MODEM];

void register_modem(modem_spec_t* modem)
{
	int n = sizeof(modems)/sizeof(modem_spec_t);
	int i;
	for (i = 0; i < n; i++) {
		if (modems[i].vid_pid != 0)
			continue;
		
		LOGD("###### Add support for %s #######", modem->name);
		memcpy(&modems[i], modem, sizeof(modem_spec_t));

		if (modem->bringup)
			modem->bringup();
		break;
	}

	if (i == n) {
		LOGE("no memory for new modem!!!!!!!!!!!!!!!!");
		exit(-1);
	}
}



struct usb_device_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;

	__le16 bcdUSB;
	__u8  bDeviceClass;
	__u8  bDeviceSubClass;
	__u8  bDeviceProtocol;
	__u8  bMaxPacketSize0;
	__le16 idVendor;
	__le16 idProduct;
	__le16 bcdDevice;
	__u8  iManufacturer;
	__u8  iProduct;
	__u8  iSerialNumber;
	__u8  bNumConfigurations;
} __attribute__ ((packed));

static inline int badname(const char *name)
{
	while(*name) {
		if(!isdigit(*name++)) return 1;
	}
	return 0;
}

static modem_spec_t* check_match(int id)
{
	int n = sizeof(modems)/sizeof(modem_spec_t);
	int i;
	for(i = 0; i < n; i++){
		if (modems[i].vid_pid == 0)
			break;
		if (modems[i].vid_pid == id)
			return &modems[i];
	}
	return NULL;
}

static modem_spec_t* usb_detect(const char *base)
{
	char busname[64], devname[64];
	char desc[1024];
	int n;
	modem_spec_t* hit = NULL;
	struct usb_device_descriptor *dev;

	DIR *busdir, *devdir;
	struct dirent *de;
	int fd;
	int writable;

	busdir = opendir(base);
	if (busdir == 0) {
		LOGD("Open %s error", base);
		return 0;
	}

	while((de = readdir(busdir)) && (hit == NULL)) {
		if(badname(de->d_name)) continue;

		sprintf(busname, "%s/%s", base, de->d_name);
		devdir = opendir(busname);
		if(devdir == 0) continue;

		while((de = readdir(devdir)) && (hit == NULL)) {
			if (badname(de->d_name)) 
				continue;
				
			sprintf(devname, "%s/%s", busname, de->d_name);

			writable = 1;
			if((fd = open(devname, O_RDWR)) < 0) {
				writable = 0;
				if((fd = open(devname, O_RDONLY)) < 0) {
					continue;
				}
			}
			n = read(fd, desc, sizeof(desc));
			dev = (void*) desc;
			
			hit = check_match((dev->idVendor<<16) | dev->idProduct);
			close(fd);
		}
		closedir(devdir);
	}
	closedir(busdir);
	return hit;
}

__attribute__((constructor(LEVEL_MODEM_DETECT)))
static void ril_detect()
{
	do
	{
		g_modem = usb_detect("/dev/bus/usb");
		if (!g_modem) {
			LOGE("cannot find 3G modem!");
		}
		sleep(1);
	}while(!g_modem);
}

char* get_at_port()
{
	if (!g_modem)
		return "/dev/ttyUSB0";

	return g_modem->at_port;
}

char* get_data_port()
{
	if (!g_modem)
		return "/dev/ttyUSB3";

	return g_modem->data_port;
}

char* get_chat_option()
{
	if (!g_modem || !g_modem->chat_option)
		return DEFAULT_CHAT_OPTION;
	
	return g_modem->chat_option;
}

void modem_periodic()
{
    if (g_modem->periodic)
        g_modem->periodic();
}

void modem_init_specific()
{
	g_modem->init();
} 


void process_request(int request, void *data, size_t datalen, RIL_Token t)
{
	int i;
	for (i = 0; i < RIL_MAX_REQUEST; i++) {
		request_item_t* r = &request_items[i];
		
		if (!r->handler)
			break;
		
		if (r->request != request) {
			continue;
		}

		LOGD("handle request : %s", requestToString(request));
		r->handler(data, datalen, t);
		break;		
	}
}

int process_unsolicited(char* s, char* sms_pdu)
{
	int i;
	for (i = 0; i < RIL_MAX_UNSOLICITED; i++) {
		unsolicited_item_t* u = &unsolicited_items[i];
		
		if (!u->prefix)
			break;
		
		if (!strStartsWith(s, u->prefix)) {
			continue;
		}

		return u->handler(s, sms_pdu);
	}
	return 0;
}

__attribute__((constructor(LEVEL_MANAGER_INIT)))
static void ril_manager_init()
{
	memset(unsolicited_items, 0, sizeof(unsolicited_items));
	memset(request_items, 0 , sizeof(request_items));
	memset(modems, 0, sizeof(modems));
}




