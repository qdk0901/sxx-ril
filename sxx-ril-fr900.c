#include "sxx-ril.h"

static void requestSendSMS(void *data, size_t datalen, RIL_Token t)
{
	int err;
	const char *smsc;
	const char *pdu;
	int tpLayerLength;
	char *cmd1, *cmd2;
	RIL_SMS_Response response;
	ATResponse *p_response = NULL;

	smsc = ((const char **)data)[0];
	pdu = ((const char **)data)[1];

	tpLayerLength = strlen(pdu)/2;

	// "NULL for default SMSC"
	if (smsc == NULL) {
		smsc= "00";
	}

    at_send_command("AT+CSCS=\"IRA\"", NULL);

	asprintf(&cmd1, "AT+CMGS=%d", tpLayerLength);
	asprintf(&cmd2, "%s%s", smsc, pdu);

	err = at_send_command_sms(cmd1, cmd2, "+CMGS:", &p_response);

	if (err != 0 || p_response->success == 0) goto error;

	memset(&response, 0, sizeof(response));

	/* FIXME fill in messageRef and ackPDU */

	RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(response));
	at_response_free(p_response);

	return;
error:
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	at_response_free(p_response);
}
REGISTER_REQUEST_ITEM(RIL_REQUEST_SEND_SMS, requestSendSMS, 0x12d11001)

static void init()
{

}

void bringup_fr900()
{
	system("echo 12d1 1001>/sys/bus/usb-serial/drivers/option1/new_id");
}


modem_spec_t fr900 = 
{
	.name = "FR900",
	.at_port = "/dev/ttyUSB2",
	.data_port = "/dev/ttyUSB0",
	.vid_pid = 0x12d11001,
	.init = init,
	.bringup = bringup_fr900,
};

REGISTER_MODEM(FR900,&fr900)
