#include "sxx-ril.h"

static void init()
{

}

static void bringup_mw100()
{
	system("echo 19f5 9013>/sys/bus/usb-serial/drivers/option1/new_id");
}
modem_spec_t mw100 = 
{
	.name = "MW100",
	.at_port = "/dev/ttyUSB2",
	.data_port = "/dev/ttyUSB3",
	.chat_option = NULL,
	.vid_pid = 0x19f59013,
	.init = init,
	.bringup = bringup_mw100,
};

REGISTER_MODEM(MW100,&mw100)
