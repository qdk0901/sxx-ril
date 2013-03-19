#include "sxx-ril.h"


static void init()
{

}

void bringup_dm6600()
{
	system("echo 05c6 1003>/sys/bus/usb-serial/drivers/option1/new_id");
}


modem_spec_t dm6600 = 
{
	.name = "DM6600",
	.at_port = "/dev/ttyUSB2",
	.data_port = "/dev/ttyUSB3",
	.vid_pid = 0x05c61003,
	.init = init,
	.bringup = bringup_dm6600,
};

REGISTER_MODEM(DM6600,&dm6600)
