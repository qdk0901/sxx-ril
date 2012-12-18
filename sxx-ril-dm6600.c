#include "sxx-ril.h"


static void init()
{
	while(getSIMStatus(2) == SIM_ABSENT) {
		LOGD("wait for sim ready");
		sleep(1);
	}
    
	at_send_command("AT+CEQREQ=", NULL);
	at_send_command("AT+CEQMIN=", NULL);        
	at_send_command("AT+CGEQREQ=", NULL);
	at_send_command("AT+CGEQMIN=", NULL);
}

modem_spec_t dm6600 = 
{
	.name = "DM6600",
	.at_port = "/dev/ttyUSB2",
	.data_port = "/dev/ttyUSB4",
	.vid_pid = 0x05c6910e,
	.init = init,
};

REGISTER_MODEM(DM6600,&dm6600)
