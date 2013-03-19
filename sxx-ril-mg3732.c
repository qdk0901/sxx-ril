#include "sxx-ril.h"

static void requestGetIMEI(void *data, size_t datalen, RIL_Token t)
{
	ATResponse *atresponse = NULL;
	int err;

	err = at_send_command_singleline("AT+CGSN", "+CGSN:",&atresponse);

	if (err < 0 || atresponse->success == 0) {
		RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	} else {
		property_set("ril.imei",atresponse->p_intermediates->line+6);

		RIL_onRequestComplete(t, RIL_E_SUCCESS,
				atresponse->p_intermediates->line+6,
				sizeof(char *));
	}
	at_response_free(atresponse);
	return;
}
REGISTER_REQUEST_ITEM(RIL_REQUEST_GET_IMEI, requestGetIMEI, 0x19d2ffeb)

static void requestSetNetworkSelectionAutomaticW218(void *data, size_t datalen,
		RIL_Token t)
{
	int err = 0;

	int zmds_save = getZmdsValue();

	err = at_send_command("AT+COPS=0", NULL);
	if (err < 0)
		goto error;

	char* cmd;
	asprintf(&cmd,"at+zmds=%d",zmds_save);
	at_send_command(cmd,NULL);
	free(cmd);

	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
	return;

error:
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	return;
}
REGISTER_REQUEST_ITEM(RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC, requestSetNetworkSelectionAutomaticW218, 0x19d2ffeb)

typedef enum _AudioPath {
	SOUND_AUDIO_PATH_HANDSET,
	SOUND_AUDIO_PATH_HEADSET,
	SOUND_AUDIO_PATH_SPEAKER,
	SOUND_AUDIO_PATH_BLUETOOTH,
	SOUND_AUDIO_PATH_BLUETOOTH_NO_NR,
	SOUND_AUDIO_PATH_HEADPHONE
} AudioPath;

static int getClvlValue()
{
	int err = 0;
	int clvl;
	char *line;
	ATResponse *atresponse=NULL;

	err = at_send_command_singleline("AT+CLVL?", "+CLVL:", &atresponse);

	if (err < 0 || !atresponse || !atresponse->p_intermediates)
		goto error;

	line = atresponse->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0)
		goto error;

	err = at_tok_nextint(&line, &clvl);
	if (err < 0)
		goto error;
error:
	at_response_free(atresponse);
	return clvl;
}

static void switchToChannel(int channel,int l)
{
	int clvl = getClvlValue();
	if(l >= 0) clvl = l;

	char* cmd;
	at_send_command("AT+CMUT=1",NULL);
	at_send_command("AT+CLVL=0", NULL);
	asprintf(&cmd, "AT+SPEAKER=%d",channel);
	at_send_command(cmd, NULL);
	free(cmd);

	asprintf(&cmd, "AT+CLVL=%d",clvl);
	at_send_command(cmd, NULL);
	at_send_command("AT+CMUT=0",NULL);
	free(cmd); 
}

static void requestSetAudioPath(int *data, size_t datalen, RIL_Token t)
{
	ATResponse *p_response = NULL;
	int err;	
	char *cmd;

	int path = data[0];
	int channel = 1;

	switch(path){
		case SOUND_AUDIO_PATH_HANDSET:
			channel = 0;
			break;
		case SOUND_AUDIO_PATH_SPEAKER:
		case SOUND_AUDIO_PATH_HEADSET:
			channel = 1;
			break;
		case SOUND_AUDIO_PATH_BLUETOOTH:
		case SOUND_AUDIO_PATH_BLUETOOTH_NO_NR:
			channel = 2;
			break;
	}
	switchToChannel(channel,-1);
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_SET_VOICE_PATH, requestSetAudioPath)

void requestSetVoiceVolume(int *data, size_t datalen, RIL_Token t)
{
	ATResponse *p_response = NULL;
	int err;	
	char *cmd;

	asprintf(&cmd, "AT+CLVL=%d", data[0]);
	err = at_send_command(cmd, NULL);	
	free(cmd);
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_SET_VOICE_VOLUME, requestSetVoiceVolume)

extern int need_network_fix;
static void init()
{
    need_network_fix = 1;
	at_send_command("AT+CEQREQ=", NULL);
	at_send_command("AT+CEQMIN=", NULL);        
	at_send_command("AT+CGEQREQ=", NULL);
	at_send_command("AT+CGEQMIN=", NULL);

	switchToChannel(1,0);
}

modem_spec_t mg3732 = 
{
	.name = "MG3732",
	.at_port = "/dev/ttyUSB244",//"/dev/ttyUSB0",
	.data_port = "/dev/ttyUSB247",//"/dev/ttyUSB3",
	.chat_option = NULL,
	.vid_pid = 0x19d2ffeb,
	.init = init,
	.bringup = NULL,
};

REGISTER_MODEM(MG3732,&mg3732)
