#include "sxx-ril.h"

extern void wait_for_pppd_down();
static int getNetworkType();
extern void reportSignalStrength(void *param);

typedef enum _AudioPath {
	SOUND_AUDIO_PATH_HANDSET,
	SOUND_AUDIO_PATH_HEADSET,
	SOUND_AUDIO_PATH_SPEAKER,
	SOUND_AUDIO_PATH_BLUETOOTH,
	SOUND_AUDIO_PATH_BLUETOOTH_NO_NR,
	SOUND_AUDIO_PATH_HEADPHONE
} AudioPath;

static void switchToChannel(int channel,int l)
{
	char* cmd;
	asprintf(&cmd, "AT+SPEAKER=%d",channel);
	at_send_command(cmd, NULL);
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
			channel = 1;
			break;
		case SOUND_AUDIO_PATH_SPEAKER:
		case SOUND_AUDIO_PATH_HEADSET:
			channel = 8;
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

static void requestSetVoiceVolume(int *data, size_t datalen, RIL_Token t)
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

static int first_cfun = 1;
static void requestRadioPower(void *data, size_t datalen, RIL_Token t)
{
	int onOff;
	int err=0;
	ATResponse *p_response = NULL;

	onOff = ((int *)data)[0];


	if (onOff == 0 && sState != RADIO_STATE_OFF) {
		stop_pppd();
		wait_for_pppd_down();

        if (!first_cfun) {
	        err = at_send_command("AT+CFUN=0", &p_response);

	        if (err < 0) 
		        goto error;
        }
		setRadioState(RADIO_STATE_OFF);

	} else if (onOff > 0 && sState == RADIO_STATE_OFF) {
		err = at_send_command("AT+CFUN=1", &p_response);
		
		if (err < 0|| p_response->success == 0) {
			// Some stacks return an error when there is no SIM,
			// but they really turn the RF portion on
			// So, if we get an error, let's check to see if it
			// turned on anyway

			if (getRadioPower() != 1 && getRadioPower() != 7) {
				goto error;
			}
		}
		setRadioState(RADIO_STATE_SIM_NOT_READY);
	}

	at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
	return;
error:
	at_response_free(p_response);
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}
REGISTER_REQUEST_ITEM(RIL_REQUEST_RADIO_POWER, requestRadioPower, 0x05c66000)

static int last_network_type = -1;

static void requestSetPreferredNetworkType(void *data, size_t datalen,
		RIL_Token t)
{
	ATResponse *atresponse = NULL;
	int err = 0;
	int pref;
	char *cmd = NULL;
	RIL_Errno errno = RIL_E_GENERIC_FAILURE;

	pref = ((int *) data)[0];
	if(sState == RADIO_STATE_OFF){
		RIL_onRequestComplete(t,RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
		return;
	}
    
    if (pref == 0)
        pref = 4;
    else if (pref == 1)
        pref = 13;
    else
        pref = 4;

    if (pref == 4)
        at_send_command("AT+PHPREF=4", NULL);

    last_network_type = getNetworkType();

	asprintf(&cmd, "AT+ZMDS=%d", pref);
	err = at_send_command(cmd, &atresponse);
	if (err < 0 || atresponse->success == 0){
		errno = RIL_E_GENERIC_FAILURE;
		goto error;
	}

	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

finally:
	free(cmd);
	at_response_free(atresponse);
	return;

error:
	RIL_onRequestComplete(t, errno, NULL, 0);
	goto finally;
}
REGISTER_REQUEST_ITEM(RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE, requestSetPreferredNetworkType, 0x05c66000)

static int getPrefValue()
{
	int err = 0;
	int type = 4;
	char *line;
	ATResponse *atresponse=NULL;

	err = at_send_command_singleline("AT+PHPREF?", "+PHPREF:", &atresponse);

	if (err < 0)
		goto error;

	if (!atresponse->p_intermediates || !atresponse->p_intermediates->line)
		goto error;

	line = atresponse->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0)
		goto error;

	err = at_tok_nextint(&line, &type);
	if (err < 0)
		goto error;

error:
	at_response_free(atresponse);
	return type;
}

static int getZmdsValue()
{
	int err = 0;
	int type = 4;
	char *line;
	ATResponse *atresponse=NULL;

	err = at_send_command_singleline("AT+ZMDS?", "+ZMDS:", &atresponse);

	if (err < 0)
		goto error;

	if (!atresponse->p_intermediates || !atresponse->p_intermediates->line)
		goto error;

	line = atresponse->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0)
		goto error;

	err = at_tok_nextint(&line, &type);
	if (err < 0)
		goto error;

error:
	at_response_free(atresponse);
	return type;
}

static void requestGetPreferredNetworkType(void *data, size_t datalen,
		RIL_Token t)
{
	int err = 0;
	int pref;

	pref = getZmdsValue();

	if(pref == 0 || pref == 14)
		pref = 0; // wcdma pref
	else if(pref == 13)
		pref = 1; //gsm only
    else
        pref = 0;


	RIL_onRequestComplete(t, RIL_E_SUCCESS, &pref, sizeof(int));
	return;
}
REGISTER_REQUEST_ITEM(RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE, requestGetPreferredNetworkType, 0x05c66000)

static void requestSetNetworkSelectionAutomatic(void *data, size_t datalen,
		RIL_Token t)
{
	int err = 0;
    char* cmd = NULL;

	err = at_send_command("AT+COPS=0", NULL);
	if (err < 0)
		goto error;
    
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
	return;

error:
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	return;
}

REGISTER_REQUEST_ITEM(RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC, requestSetNetworkSelectionAutomatic, 0x05c66000)

static void requestSetNetworkSelectionManual(void *data, size_t datalen,
		RIL_Token t)
{
	int err = 0;
	char *cmd = NULL;
	ATResponse *atresponse = NULL;
	const char *mccMnc = (const char *) data;

	if (mccMnc == NULL) {
		goto error;
	}

	asprintf(&cmd, "AT+COPS=4,2,\"%s\"", mccMnc);
	err = at_send_command(cmd, &atresponse);
	if (err < 0 || atresponse->success == 0)
		goto error;

	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
finally:

	at_response_free(atresponse);

	if (cmd != NULL)
		free(cmd);

	return;

error:
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	goto finally;
}

REGISTER_REQUEST_ITEM(RIL_REQUEST_SET_NETWORK_SELECTION_MANUAL, requestSetNetworkSelectionManual, 0x05c66000)

static int getNetworkTypeOld()
{
	int err;
	ATResponse *p_response = NULL;
	int type = 0;
	char *line;

    hide_info(1);
	err = at_send_command_singleline("AT+COPS?", "+COPS:", &p_response);
    hide_info(0);
	if (err < 0 || p_response->success == 0) {
		return type;
	}

	line = p_response->p_intermediates->line;

	err = at_tok_start(&line);

	if (err < 0) {
		goto out;
	}

	err = at_tok_nextint(&line, &type);
	err = at_tok_nextint(&line, &type);
	err = at_tok_nextint(&line, &type);
	err = at_tok_nextint(&line, &type);

	switch(type){
		case 0:
		case 1:
			type = 2;
			break;
		case 2:
			type = 3;
			break;
		default:
			type = 3;
			break;
	}
out:
	at_response_free(p_response);
	return type;	
}

static int getNetworkType()
{
	int err;
	ATResponse *p_response = NULL;
	int type = 0;
	char *line;

    hide_info(1);
   	err = at_send_command_singleline("AT+NETMODE?", "+NETMODE:", &p_response);
    hide_info(0);

	if (err < 0 || p_response->success == 0) {
		return type;
	}

	line = p_response->p_intermediates->line;

	err = at_tok_start(&line);

	if (err < 0) {
		goto out;
	}

	err = at_tok_nextint(&line, &type);

	switch(type){    
		case 0: 
            //unknown
            type = getNetworkTypeOld();
        break;
		case 1:
            //gprs
		case 2:
            //edge
        case 3:
            //utms
            break;
        case 8:
            //HSDPA
            type = 9;
            break;
        case 9:
            //HSUPA
            type = 10;
            break;
        case 10:
            //HSPA
            type = 11;
            break;
        default:
            type = 0;
        break;
            
	}
out:
	at_response_free(p_response);
	return type;	
}

static int requestRegistrationStateSub(void *data,int request, size_t datalen, RIL_Token t)
{
	int err;
	int response[4]={0,};
	char * responseStr[4]={NULL,};
	ATResponse *p_response = NULL;
	const char *cmd;
	const char *prefix;
	char *line, *p;
	int commas;
	int skip;
	int count = 3;
	int i = 0;


	if (request == RIL_REQUEST_VOICE_REGISTRATION_STATE) {
		cmd = "AT+CREG?";
		prefix = "+CREG:";
	} else if (request == RIL_REQUEST_DATA_REGISTRATION_STATE) {
		cmd = "AT+CGREG?";
		prefix = "+CGREG:";
	} else {
		goto error;
	}

	err = at_send_command_singleline(cmd, prefix, &p_response);

	if (err != 0 ||p_response->success == 0 ) goto error;

	line = p_response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	commas = 0;
	for (p = line ; *p != '\0' ;p++) {
		if (*p == ',') commas++;
	}


	switch (commas) {
		case 0: /* +CREG: <stat> */
			err = at_tok_nextint(&line, &response[0]);
			if (err < 0) goto error;
			break;

		case 1: /* +CREG: <n>, <stat> */
			err = at_tok_nextint(&line, &skip);
			if (err < 0) goto error;
			err = at_tok_nextint(&line, &response[0]);
			if (err < 0) goto error;
			break;

		case 2: /* +CREG: <stat>, <lac>, <cid> */
			err = at_tok_nextint(&line, &response[0]);
			if (err < 0) goto error;
			err = at_tok_nexthexint(&line, &response[1]);
			if (err < 0) goto error;
			err = at_tok_nexthexint(&line, &response[2]);
			if (err < 0) goto error;
			break;
		case 3: /* +CREG: <n>, <stat>, <lac>, <cid> */
			err = at_tok_nextint(&line, &skip);
			if (err < 0) goto error;
			err = at_tok_nextint(&line, &response[0]);
			if (err < 0) goto error;
			err = at_tok_nexthexint(&line, &response[1]);
			if (err < 0) goto error;
			err = at_tok_nexthexint(&line, &response[2]);
			if (err < 0) goto error;
			break;
			/* special case for CGREG, there is a fourth parameter
			 * that is the network type (unknown/gprs/edge/umts)
			 */
		case 4: /* +CGREG: <n>, <stat>, <lac>, <cid>, <networkType> */
			err = at_tok_nextint(&line, &skip);
			if (err < 0) goto error;
			err = at_tok_nextint(&line, &response[0]);
			if (err < 0) goto error;
			err = at_tok_nexthexint(&line, &response[1]);
			if (err < 0) goto error;
			err = at_tok_nexthexint(&line, &response[2]);
			if (err < 0) goto error;
			err = at_tok_nexthexint(&line, &response[3]);
			if (err < 0) goto error;
			count = 4;
			break;
		default:
			goto error;
	}

	//if(response[0] != 1 && response[0] != 5 && response[0] != 3) goto error;

	int type = getNetworkType();
	asprintf(&responseStr[0], "%d", response[0]);
	asprintf(&responseStr[1], "%x", response[1]);
	asprintf(&responseStr[2], "%x", response[2]);
	asprintf(&responseStr[3], "%d", type);

	RIL_onRequestComplete(t, RIL_E_SUCCESS, responseStr, 4*sizeof(char*));

    int signal = 0;
    reportSignalStrength(&signal);
    reportSignalStrength(NULL);
	at_response_free(p_response);

	for(i=0;i<4;i++){
		if(responseStr[i]) free(responseStr[i]);
	}

	return 0;
error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	LOGE("requestRegistrationState must never return an error when radio is on");
	at_response_free(p_response);
	for(i=0;i<4;i++){
		if(responseStr[i]) free(responseStr[i]);
	}
	return -1;
}

typedef struct 
{
    int request;
    RIL_Token t;
}poll_network_t;

static void* poll_network_thread(void* ptr)
{
    poll_network_t* pn = (poll_network_t*)ptr;
    int wait = 20,ret = -1;
    int is_lock = 0;
	while(wait-- && ret == -1){
		ret = requestRegistrationStateSub(NULL, pn->request, 0, pn->t);
		if(is_lock) break;
		LOGD("_requestRegistrationStateWCDMA wait = %d",wait);
		if(ret ) sleep(1);
	}
    if (wait == 0)
        RIL_onRequestComplete(pn->t, RIL_E_GENERIC_FAILURE, NULL, 0);
    free(pn);
    
    return NULL;
}

static void start_poll_network(int request, RIL_Token t)
{
	poll_network_t* pn = (poll_network_t*)malloc(sizeof(poll_network_t));
	memset(pn, 0, sizeof(poll_network_t));

    pn->request = request;
    pn->t = t;
	pthread_t tid;
	pthread_attr_t attr;
	pthread_attr_init (&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&tid, NULL, poll_network_thread, pn);      
}

static void requestVoiceRegistrationState(void *data, size_t datalen, RIL_Token t)
{
	int wait = 20,ret = -1;
    int is_lock = 0;
    /*
	int sim_status = getSIMStatus(0);	
	if(sim_status == SIM_PIN || sim_status == SIM_PUK){
		is_lock = 1;
	}else if(sim_status == SIM_ABSENT) wait = 2;
    */
    /*
	while(wait-- && ret == -1){
		ret = requestRegistrationStateSub(data,RIL_REQUEST_VOICE_REGISTRATION_STATE,datalen,t);
		if(is_lock) break;
		LOGD("_requestRegistrationStateWCDMA wait = %d",wait);
		if(ret ) sleep(1);
	}
    */
    
    requestRegistrationStateSub(data,RIL_REQUEST_VOICE_REGISTRATION_STATE,datalen,t);
    //start_poll_network(RIL_REQUEST_VOICE_REGISTRATION_STATE, t);
}
REGISTER_REQUEST_ITEM(RIL_REQUEST_VOICE_REGISTRATION_STATE, requestVoiceRegistrationState, 0x05c66000)

static void requestDataRegistrationState(void *data, size_t datalen, RIL_Token t)
{
	int wait = 20,ret = -1;
    int is_lock = 0;
    /*
	int sim_status = getSIMStatus(0);	
	if(sim_status == SIM_PIN || sim_status == SIM_PUK) {
		is_lock = 1;
	}else if(sim_status == SIM_ABSENT) 
		wait = 2;
    */
    /*
	while(wait-- && ret == -1) {
		ret = requestRegistrationStateSub(data,RIL_REQUEST_DATA_REGISTRATION_STATE,datalen,t);
		if(is_lock) break;
		LOGD("requestGPRSRegistrationState wait = %d",wait);
		if(ret ) sleep(1);
	}
    */

    requestRegistrationStateSub(data,RIL_REQUEST_DATA_REGISTRATION_STATE,datalen,t);
    //start_poll_network(RIL_REQUEST_DATA_REGISTRATION_STATE, t);
}
REGISTER_REQUEST_ITEM(RIL_REQUEST_DATA_REGISTRATION_STATE, requestDataRegistrationState, 0x05c66000)


static void requestQueryAvailableNetworks(void *data, size_t datalen, RIL_Token t)
{
	/* 
	 * AT+COPS=?
	 *   +COPS: [list of supported (<stat>,long alphanumeric <oper>
	 *           ,short alphanumeric <oper>,numeric <oper>[,<AcT>])s]
	 *          [,,(list of supported <mode>s),(list of supported <format>s)]
	 *
	 *   <stat>
	 *     0 = unknown
	 *     1 = available
	 *     2 = current
	 *     3 = forbidden 
	 */

	int err = 0;
	ATResponse *atresponse = NULL;
	const char *statusTable[] =
	{ "unknown", "available", "current", "forbidden" };
	char **responseArray = NULL;
	char *p;
	int n = 0;
	int i = 0;

    halt_pppd();
	err = at_send_command_multiline("AT+COPS=?", "+COPS:", &atresponse);
    restart_pppd();
	if (err < 0 || 
			atresponse->success == 0 || 
			atresponse->p_intermediates == NULL)
		goto error;

	p = atresponse->p_intermediates->line;
	while (*p != '\0') {
		if (*p == '(')
			n++;
		if(*p == ',' && *(p+1) == ',')
			break;
		p++;
	}

	/* Allocate array of strings, blocks of 4 strings. */
	responseArray = alloca(n * 4 * sizeof(char *));

	p = atresponse->p_intermediates->line;

	/* Loop and collect response information into the response array. */
	for (i = 0; i < n; i++) {
		int status = 0;
		char *line = NULL;
		char *s = NULL;
		char *longAlphaNumeric = NULL;
		char *shortAlphaNumeric = NULL;
		char *numeric = NULL;
		char *remaining = NULL;

		s = line = getFirstElementValue(p, "(", ")", &remaining);
		p = remaining;

		if (line == NULL) {
			LOGE("Null pointer while parsing COPS response. This should not happen.");
			break;
		}
		/* <stat> */
		err = at_tok_nextint(&line, &status);
		if (err < 0)
			goto error;

		/* long alphanumeric <oper> */
		err = at_tok_nextstr(&line, &longAlphaNumeric);
		if (err < 0)
			goto error;

		/* short alphanumeric <oper> */            
		err = at_tok_nextstr(&line, &shortAlphaNumeric);
		if (err < 0)
			goto error;

		/* numeric <oper> */
		err = at_tok_nextstr(&line, &numeric);
		if (err < 0)
			goto error;

		responseArray[i * 4 + 0] = alloca(strlen(longAlphaNumeric) + 1);
		strcpy(responseArray[i * 4 + 0], longAlphaNumeric);

		responseArray[i * 4 + 1] = alloca(strlen(shortAlphaNumeric) + 1);
		strcpy(responseArray[i * 4 + 1], shortAlphaNumeric);

		responseArray[i * 4 + 2] = alloca(strlen(numeric) + 1);
		strcpy(responseArray[i * 4 + 2], numeric);

		free(s);

		/* 
		 * Check if modem returned an empty string, and fill it with MNC/MMC 
		 * if that's the case.
		 */
		if (responseArray[i * 4 + 0] && strlen(responseArray[i * 4 + 0]) == 0) {
			responseArray[i * 4 + 0] = alloca(strlen(responseArray[i * 4 + 2])
					+ 1);
			strcpy(responseArray[i * 4 + 0], responseArray[i * 4 + 2]);
		}

		if (responseArray[i * 4 + 1] && strlen(responseArray[i * 4 + 1]) == 0) {
			responseArray[i * 4 + 1] = alloca(strlen(responseArray[i * 4 + 2])
					+ 1);
			strcpy(responseArray[i * 4 + 1], responseArray[i * 4 + 2]);
		}

		responseArray[i * 4 + 3] = alloca(strlen(statusTable[status]) + 1);
		sprintf(responseArray[i * 4 + 3], "%s", statusTable[status]);
	}

	RIL_onRequestComplete(t, RIL_E_SUCCESS, responseArray,
			i * 4 * sizeof(char *));

finally:
	at_response_free(atresponse);
	return;

error:
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	goto finally;
}
REGISTER_REQUEST_ITEM(RIL_REQUEST_QUERY_AVAILABLE_NETWORKS, requestQueryAvailableNetworks, 0x05c66000)

static int delay_count = 5;
static void init()
{
	while(getSIMStatus(2) == SIM_ABSENT && delay_count--) {
		LOGD("wait for sim ready");
		sleep(1);
	}

    at_send_command("AT+SPEAKER=8", NULL);
}

static void bringup_mh400b()
{
	system("echo 05c6 6000>/sys/bus/usb-serial/drivers/option1/new_id");
}

#define NETWORK_DELAY_CHECK 20
static int delay_counter = 20;
static int last_network_type2 = -5;
static void periodic_mh400b()
{
    delay_counter--;
    if (delay_counter == 0) {
	    delay_counter = NETWORK_DELAY_CHECK;
        
        int current = getNetworkType();
        if (last_network_type == current) {
        } else {
            last_network_type = current;
            RIL_onUnsolicitedResponse (
					        RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED,
					        NULL, 0);
        }
        if (last_network_type2 == current) {
        } else {
            if (last_network_type2 ++ > 0)
                last_network_type2 = current;
            RIL_onUnsolicitedResponse (
					        RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED,
					        NULL, 0);
        }
    }
}


modem_spec_t mh400b = 
{
	.name = "MH400B",
	.at_port = "/dev/ttyUSB1",
	.data_port = "/dev/ttyUSB0",
	.vid_pid = 0x05c66000,
	.init = init,
	.bringup = bringup_mh400b,
    .periodic = periodic_mh400b,
};

REGISTER_MODEM(MH400B,&mh400b)
