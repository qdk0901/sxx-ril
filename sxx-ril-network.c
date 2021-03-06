#include <stdio.h>
#include <telephony/ril.h>
#include <pthread.h>
#include <cutils/properties.h>

#include <assert.h>
#include "sxx-ril.h"

#define REPOLL_OPERATOR_SELECTED 30     /* 30 * 2 = 1M = ok? */
static const struct timeval TIMEVAL_OPERATOR_SELECT_POLL = { 2, 0 };

static int net_mode = 0;
static int sys_mode = 0;

int creg_value,  mode_value;

int getZmdsValue();

volatile int sig_ok = 0;
//------------------------private function -----------------------------

static int get_reg_stat(int registered,int  roaming)
{
	if(registered == 0){
		return 0; //0 - Not registered, MT is not currently searching a new operator to register
	}else if(registered == 4){
		return 2; // 2 - Not registered, but MT is currently searching  a new operator to register
	}else if(roaming == 1){
		return 5;// Registered, roaming
	}else{
		return 1;// Registered, home network
	}
}

/*

   0 - Unknown, 1 - GPRS, 2 - EDGE, 3 - UMTS,
 *                                  4 - IS95A, 5 - IS95B, 6 - 1xRTT,
 *                                  7 - EvDo Rev. 0, 8 - EvDo Rev. A,
 *                                  9 - HSDPA, 10 - HSUPA, 11 - HSPA,
 *                                  12 - EVDO Rev B


 <sys_mode>
 0   no service
 1   AMPS
 2   CDMA
 3   GSM/GPRS
 4   HDR\u6a21\u5f0f
 5   WCDMA\u6a21\u5f0f
 6   GPS\u6a21\u5f0f
 7   GSM/WCDMA
 8   CDMA/HDR HYBRID

 */

static int get_sys_mode( int sysmode)
{
	int map[] = {0,8,6,1,8,3,3,3,8};
	if( sysmode >= 9 || sysmode < 0){
		return 0;
	}
	return map[sysmode];
}

/** do post-AT+CFUN=1 initialization */
static void onRadioPowerOn()
{
	pollSIMState(NULL);
}

static int is_cdma_net( int sysmode){
	int ret = 0;
	switch(sysmode){
		case 0:
		case 1:
		case 6:
			ret = 1;
			break;
		case 8:
		case 3:
			ret = 0;
			break;
	}
	return ret;
}


/**
 * RIL_REQUEST_SET_NETWORK_SELECTION_MANUAL
 *
 * Manually select a specified network.
 *
 * The radio baseband/RIL implementation is expected to fall back to 
 * automatic selection mode if the manually selected network should go
 * out of range in the future.
 */
void requestSetNetworkSelectionManual(void *data, size_t datalen,
		RIL_Token t)
{
	_requestSetNetworkSelectionManualWCDMA(data, datalen, t);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_SET_NETWORK_SELECTION_MANUAL, requestSetNetworkSelectionManual)

/**
 * RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE
 *
 * Requests to set the preferred network type for searching and registering
 * (CS/PS domain, RAT, and operation mode).
 */
void requestSetPreferredNetworkType(void *data, size_t datalen,
		RIL_Token t)
{
	_requestSetPreferredNetworkTypeWCDMA(data, datalen, t);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE, requestSetPreferredNetworkType)

/**
 * RIL_REQUEST_SIGNAL_STRENGTH
 *
 * Requests current signal strength and bit error rate.
 *
 * Must succeed if radio is on.
 */
void requestSignalStrength(void *data, size_t datalen, RIL_Token t)
{
	_requestSignalStrengthWCDMA(data, datalen, t);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_SIGNAL_STRENGTH, requestSignalStrength)


void requestSetNetworkSelectionAutomatic(void *data, size_t datalen,
		RIL_Token t)
{
	int err = 0;

    
	err = at_send_command("AT+COPS=0", NULL);
	if (err < 0)
		goto error;

	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
	return;

error:
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	return;
}

REGISTER_DEFAULT_ITEM(RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC, requestSetNetworkSelectionAutomatic)


void _requestSetNetworkSelectionManualWCDMA(void *data, size_t datalen,
		RIL_Token t)
{
/* 
 * AT+COPS=[<mode>[,<format>[,<oper>[,<AcT>]]]]
 *    <mode>   = 4 = Manual (<oper> field shall be present and AcT optionally) with fallback to automatic if manual fails.
 *    <format> = 2 = Numeric <oper>, the number has structure:
 *                   (country code digit 3)(country code digit 2)(country code digit 1)
 *                   (network code digit 2)(network code digit 1) 
 */

	int err = 0;
	char *cmd = NULL;
	ATResponse *atresponse = NULL;
	const char *mccMnc = (const char *) data;

	/* Check inparameter. */
	if (mccMnc == NULL) {
		goto error;
	}
	/* Build and send command. */
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

void select_tech(int t)
{
	char *cmd;
	asprintf(&cmd, "AT+COPS=0,,,%d", t);
	at_send_command(cmd,NULL);
	free(cmd);
}

void _requestSetPreferredNetworkTypeWCDMA(void *data, size_t datalen,
		RIL_Token t)
{
	ATResponse *atresponse = NULL;
	int err = 0;
	int rat;
	int arg;
	char *cmd = NULL;
	RIL_Errno errno = RIL_E_GENERIC_FAILURE;

	rat = ((int *) data)[0];
	if(sState == RADIO_STATE_OFF){
		RIL_onRequestComplete(t,RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
		return;
	}

	switch (rat) {
		case 0:
		case 3:
		case 4:
		case 7:
			arg = 4;   // atuo
			break;
		case 1:
		case 5:
			arg = 13;	// GSM
			break;
		case 2:
		case 6:
			arg = 14;	// WCDMA
			break;

		default:
			RIL_onRequestComplete(t,RIL_E_MODE_NOT_SUPPORTED, NULL, 0);
			return;
	}

	int zv = getZmdsValue();
	if(zv == arg){
		RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
		return;
	}

	asprintf(&cmd, "AT+ZMDS=%d", arg);
	err = at_send_command(cmd, &atresponse);
	if (err < 0 || atresponse->success == 0){
		errno = RIL_E_GENERIC_FAILURE;
		goto error;
	}


	if(arg == 14)
		select_tech(2);
	else if(arg == 13)
		select_tech(0);
	else{
		at_send_command("AT+COPS=0,,,",NULL);
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

void reportSignalStrength(void *param)
{
	ATResponse *p_response = NULL;
	int err, tmp;
	char *line;
	RIL_SignalStrength_v6 resp;

	memset(&resp,0,sizeof(resp));

	resp.GW_SignalStrength.signalStrength = -1;
	resp.GW_SignalStrength.bitErrorRate = -1;
	resp.LTE_SignalStrength.signalStrength = -1;
	resp.LTE_SignalStrength.rsrp = -1;
	resp.LTE_SignalStrength.rsrq = -1;
	resp.LTE_SignalStrength.rssnr = -1;
	resp.LTE_SignalStrength.cqi = -1;


	err = at_send_command_singleline("AT+CSQ", "+CSQ:", &p_response);

	if (err < 0 || p_response->success == 0) 
	{
		goto error;
	}
	line = p_response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &(resp.GW_SignalStrength.signalStrength));
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &(resp.GW_SignalStrength.bitErrorRate));
	if (err < 0) goto error;

	at_response_free(p_response);
	p_response = NULL;

    if (param != NULL)
        resp.GW_SignalStrength.signalStrength = *(int*)param;

	resp.LTE_SignalStrength.signalStrength = -1;
	resp.LTE_SignalStrength.rsrp = -1;
	resp.LTE_SignalStrength.rsrq = -1;
	resp.LTE_SignalStrength.rssnr = -1;
	resp.LTE_SignalStrength.cqi = -1;

	RIL_onUnsolicitedResponse (
			RIL_UNSOL_SIGNAL_STRENGTH,
			& resp, sizeof(resp));
	return;

error:
	at_response_free(p_response);

}

void _requestSignalStrengthWCDMA(void *data, size_t datalen, RIL_Token t)
{
	ATResponse *p_response = NULL;
	int err;
	RIL_SignalStrength_v6 resp;
	char *line;

	memset(&resp,0,sizeof(resp));

	err = at_send_command_singleline("AT+CSQ", "+CSQ:", &p_response);

	if (err < 0 || p_response->success == 0) {
		RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
		goto error;
	}

	line = p_response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &(resp.GW_SignalStrength.signalStrength));
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &(resp.GW_SignalStrength.bitErrorRate));
	if (err < 0) goto error;

	resp.LTE_SignalStrength.signalStrength = -1;
	resp.LTE_SignalStrength.rsrp = -1;
	resp.LTE_SignalStrength.rsrq = -1;
	resp.LTE_SignalStrength.rssnr = -1;
	resp.LTE_SignalStrength.cqi = -1;

	RIL_onRequestComplete(t, RIL_E_SUCCESS, &resp, sizeof(resp));

	at_response_free(p_response);
	return;

error:
	LOGE("requestSignalStrength must never return an error when radio is on");
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	at_response_free(p_response);
}

/**
 * RIL_REQUEST_QUERY_AVAILABLE_NETWORKS
 *
 * Scans for available networks.
 ok
 */
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

	err = at_send_command_multiline("AT+COPS=?", "+COPS:", &atresponse);
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
REGISTER_DEFAULT_ITEM(RIL_REQUEST_QUERY_AVAILABLE_NETWORKS, requestQueryAvailableNetworks)


	/**
	 * RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE
	 *
	 * Query current network selectin mode.
	 ok
	 */
void requestQueryNetworkSelectionMode(
		void *data, size_t datalen, RIL_Token t)
{
	int err;
	ATResponse *p_response = NULL;
	int response = 0;
	char *line;


	err = at_send_command_singleline("AT+COPS?", "+COPS:", &p_response);

	if (err < 0 || p_response->success == 0) {
		goto error;
	}

	line = p_response->p_intermediates->line;

	err = at_tok_start(&line);

	if (err < 0) {
		goto error;
	}

	err = at_tok_nextint(&line, &response);

	if (err < 0) {
		goto error;
	}

	RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(int));
	at_response_free(p_response);
	return;
error:
	at_response_free(p_response);
	LOGE("requestQueryNetworkSelectionMode must never return error when radio is on");
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE, requestQueryNetworkSelectionMode)

int switch_signal_strength(int rssi,int evdo)
{
	int dbm;

	if(evdo){//evdo
		if(rssi >= 80 ) return 60;
		if(rssi == 60) return 75;
		if(rssi == 40) return 90;
		if(rssi == 20) return 105;
		return 125;
	}else{//cdma
		if(16<=rssi && rssi <= 31 ) return 75;
		if(8<=rssi && rssi < 16) return 85;
		if(4<=rssi && rssi< 8) return 95;
		if(1<=rssi && rssi < 4) return 100;
		return 125;
	}
}

/**
 * RIL_REQUEST_VOICE_REGISTRATION_STATE
 *
 * Request current registration state
 *
 * "data" is NULL
 * "response" is a "char **"
 * ((const char **)response)[0] is registration state 0-6,
 *              0 - Not registered, MT is not currently searching
 *                  a new operator to register
 *              1 - Registered, home network
 *              2 - Not registered, but MT is currently searching
 *                  a new operator to register
 *              3 - Registration denied
 *              4 - Unknown
 *              5 - Registered, roaming
 *             10 - Same as 0, but indicates that emergency calls
 *                  are enabled.
 *             12 - Same as 2, but indicates that emergency calls
 *                  are enabled.
 *             13 - Same as 3, but indicates that emergency calls
 *                  are enabled.
 *             14 - Same as 4, but indicates that emergency calls
 *                  are enabled.
 *
 * ((const char **)response)[1] is LAC if registered on a GSM/WCDMA system or
 *                              NULL if not.Valid LAC are 0x0000 - 0xffff
 * ((const char **)response)[2] is CID if registered on a * GSM/WCDMA or
 *                              NULL if not.
 *                                 Valid CID are 0x00000000 - 0xffffffff
 *                                    In GSM, CID is Cell ID (see TS 27.007)
 *                                            in 16 bits
 *                                    In UMTS, CID is UMTS Cell Identity
 *                                             (see TS 25.331) in 28 bits
 * ((const char **)response)[3] indicates the available radio technology 0-7,
 *                                  0 - Unknown, 1 - GPRS, 2 - EDGE, 3 - UMTS,
 *                                  4 - IS95A, 5 - IS95B, 6 - 1xRTT,
 *                                  7 - EvDo Rev. 0, 8 - EvDo Rev. A,
 *                                  9 - HSDPA, 10 - HSUPA, 11 - HSPA,
 *                                  12 - EVDO Rev B
 * ((const char **)response)[4] is Base Station ID if registered on a CDMA
 *                              system or NULL if not.  Base Station ID in
 *                              decimal format
 * ((const char **)response)[5] is Base Station latitude if registered on a
 *                              CDMA system or NULL if not. Base Station
 *                              latitude is a decimal number as specified in
 *                              3GPP2 C.S0005-A v6.0. It is represented in
 *                              units of 0.25 seconds and ranges from -1296000
 *                              to 1296000, both values inclusive (corresponding
 *                              to a range of -90� to +90�).
 * ((const char **)response)[6] is Base Station longitude if registered on a
 *                              CDMA system or NULL if not. Base Station
 *                              longitude is a decimal number as specified in
 *                              3GPP2 C.S0005-A v6.0. It is represented in
 *                              units of 0.25 seconds and ranges from -2592000
 *                              to 2592000, both values inclusive (corresponding
 *                              to a range of -180� to +180�).
 * ((const char **)response)[7] is concurrent services support indicator if
 *                              registered on a CDMA system 0-1.
 *                                   0 - Concurrent services not supported,
 *                                   1 - Concurrent services supported
 * ((const char **)response)[8] is System ID if registered on a CDMA system or
 *                              NULL if not. Valid System ID are 0 - 32767
 * ((const char **)response)[9] is Network ID if registered on a CDMA system or
 *                              NULL if not. Valid System ID are 0 - 65535
 * ((const char **)response)[10] is the TSB-58 Roaming Indicator if registered
 *                               on a CDMA or EVDO system or NULL if not. Valid values
 *                               are 0-255.
 * ((const char **)response)[11] indicates whether the current system is in the
 *                               PRL if registered on a CDMA or EVDO system or NULL if
 *                               not. 0=not in the PRL, 1=in the PRL
* ((const char **)response)[12] is the default Roaming Indicator from the PRL,
	*                               if registered on a CDMA or EVDO system or NULL if not.
	*                               Valid values are 0-255.
	* ((const char **)response)[13] if registration state is 3 (Registration
			*                               denied) this is an enumerated reason why
	*                               registration was denied.  See 3GPP TS 24.008,
	*                               10.5.3.6 and Annex G.
	*                                 0 - General
	*                                 1 - Authentication Failure
	*                                 2 - IMSI unknown in HLR
	*                                 3 - Illegal MS
	*                                 4 - Illegal ME
	*                                 5 - PLMN not allowed
	*                                 6 - Location area not allowed
	*                                 7 - Roaming not allowed
	*                                 8 - No Suitable Cells in this Location Area
	*                                 9 - Network failure
	*                                10 - Persistent location update reject
	*
	* Please note that registration state 4 ("unknown") is treated
	* as "out of service" in the Android telephony system
	*
	* Registration state 3 can be returned if Location Update Reject
	* (with cause 17 - Network Failure) is received repeatedly from the network,
	* to facilitate "managed roaming"
	*
	* Valid errors:
	*  SUCCESS
	*  RADIO_NOT_AVAILABLE
	*  GENERIC_FAILURE
	*/
void requestVoiceRegistrationState(void *data, size_t datalen, RIL_Token t)
{
	_requestRegistrationStateWCDMA(data, datalen, t);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_VOICE_REGISTRATION_STATE, requestVoiceRegistrationState)

	/**
	 * RIL_REQUEST_OPERATOR
	 *
	 * Request current operator ONS or EONS
	 *
	 * "data" is NULL
	 * "response" is a "const char **"
	 * ((const char **)response)[0] is long alpha ONS or EONS
	 *                                  or NULL if unregistered
	 *
	 * ((const char **)response)[1] is short alpha ONS or EONS
	 *                                  or NULL if unregistered
	 * ((const char **)response)[2] is 5 or 6 digit numeric code (MCC + MNC)
	 *                                  or NULL if unregistered
	 *
	 * Valid errors:
	 *  SUCCESS
	 *  RADIO_NOT_AVAILABLE
	 *  GENERIC_FAILURE
	 */
void requestOperator(void *data, size_t datalen, RIL_Token t)
{
	_requestOperatorWCDMA(data, datalen, t);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_OPERATOR, requestOperator)

	/**
	 * RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE
	 *
	 * Query the preferred network type (CS/PS domain, RAT, and operation mode)
	 * for searching and registering.
	 *
	 * "data" is NULL
	 *
	 * "response" is int *
	 * ((int *)response)[0] is == 0 for GSM/WCDMA (WCDMA preferred)
	 * ((int *)response)[0] is == 1 for GSM only
	 * ((int *)response)[0] is == 2 for WCDMA only
	 * ((int *)response)[0] is == 3 for GSM/WCDMA (auto mode, according to PRL)
	 * ((int *)response)[0] is == 4 for CDMA and EvDo (auto mode, according to PRL)
	 * ((int *)response)[0] is == 5 for CDMA only
	 * ((int *)response)[0] is == 6 for EvDo only
	 * ((int *)response)[0] is == 7 for GSM/WCDMA, CDMA, and EvDo (auto mode, according to PRL)
	 *
	 * Valid errors:
	 *  SUCCESS
	 *  RADIO_NOT_AVAILABLE
	 *  GENERIC_FAILURE
	 *
	 * See also: RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE
	 */
void requestGetPreferredNetworkType(void *data, size_t datalen,
		RIL_Token t)
{
	_requestGetPreferredNetworkTypeWCDMA(data, datalen, t);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE, requestGetPreferredNetworkType)


static int getNetworkType()
{
	int err;
	ATResponse *p_response = NULL;
	int type = 0;
	char *line;

	err = at_send_command_singleline("AT+COPS?", "+COPS:", &p_response);

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

void reportSignalStrength(void *param);

static int _requestRegistrationStateWCDMASub(void *data,int request, size_t datalen, RIL_Token t)
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
		assert(0);
		goto error;
	}

	err = at_send_command_singleline(cmd, prefix, &p_response);

	if (err != 0 ||p_response->success == 0 ) goto error;

	line = p_response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	/* Ok you have to be careful here
	 * The solicited version of the CREG response is
	 * +CREG: n, stat, [lac, cid]
	 * and the unsolicited version is
	 * +CREG: stat, [lac, cid]
	 * The <n> parameter is basically "is unsolicited creg on?"
	 * which it should always be
	 *
	 * Now we should normally get the solicited version here,
	 * but the unsolicited version could have snuck in
	 * so we have to handle both
	 *
	 * Also since the LAC and CID are only reported when registered,
	 * we can have 1, 2, 3, or 4 arguments here
	 *
	 * finally, a +CGREG: answer may have a fifth value that corresponds
	 * to the network type, as in;
	 *
	 *   +CGREG: n, stat [,lac, cid [,networkType]]
	 */

	/* count number of commas */
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

	if(response[0] != 1 && response[0] != 5 && response[0] != 3) goto error;

	//if(response[0] == 5) response[0] = 1;

	int type = getNetworkType();
	asprintf(&responseStr[0], "%d", response[0]);
	asprintf(&responseStr[1], "%x", response[1]);
	asprintf(&responseStr[2], "%x", response[2]);
	asprintf(&responseStr[3], "%d", type);
	creg_value=response[0];

	RIL_onRequestComplete(t, RIL_E_SUCCESS, responseStr, 4*sizeof(char*));
	at_response_free(p_response);

	for(i=0;i<4;i++){
		if(responseStr[i]) free(responseStr[i]);
	}

	return 0;
error:
	LOGE("requestRegistrationState must never return an error when radio is on");
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	at_response_free(p_response);
	for(i=0;i<4;i++){
		if(responseStr[i]) free(responseStr[i]);
	}
	return -1;
}

extern SIM_Status getSIMStatus(int falce);

void _requestRegistrationStateWCDMA(void *data, size_t datalen, RIL_Token t)
{
	int wait = 20,ret = -1;
	int sim_status = getSIMStatus(0);
	int is_lock = 0;
	if(sim_status == SIM_PIN || sim_status == SIM_PUK){
		is_lock = 1;
	}else if(sim_status == SIM_ABSENT) wait = 2;

	while(wait-- && ret == -1){
		ret = _requestRegistrationStateWCDMASub(data,RIL_REQUEST_VOICE_REGISTRATION_STATE,datalen,t);
		if(is_lock) break;
		LOGD("_requestRegistrationStateWCDMA wait = %d",wait);
		if(ret ) sleep(1);
	}
}

void requestDataRegistrationState(void *data, size_t datalen, RIL_Token t)
{	
	int wait = 20,ret = -1;
	int sim_status = getSIMStatus(0);
	int is_lock = 0;
	if(sim_status == SIM_PIN || sim_status == SIM_PUK){
		is_lock = 1;
	}else if(sim_status == SIM_ABSENT) 
		wait = 2;

	while(wait-- && ret == -1){
		ret = _requestRegistrationStateWCDMASub(data,RIL_REQUEST_DATA_REGISTRATION_STATE,datalen,t);
		if(is_lock) break;
		LOGD("requestGPRSRegistrationState wait = %d",wait);
		if(ret ) sleep(1);
	}
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_DATA_REGISTRATION_STATE, requestDataRegistrationState)

void _requestOperatorWCDMA(void *data, size_t datalen, RIL_Token t)
{
	int err;
	int i;
	int skip;
	ATLine *p_cur;
	char *response[3];

	memset(response, 0, sizeof(response));

	ATResponse *p_response = NULL;

	err = at_send_command_multiline(
			"AT+COPS=3,0;+COPS?;+COPS=3,1;+COPS?;+COPS=3,2;+COPS?",
			"+COPS:", &p_response);

	/* we expect 3 lines here:
	 * +COPS: 0,0,"T - Mobile"
	 * +COPS: 0,1,"TMO"
	 * +COPS: 0,2,"310170"
	 */

	if (err != 0) goto error;

	for (i = 0, p_cur = p_response->p_intermediates
			; p_cur != NULL
			; p_cur = p_cur->p_next, i++
	    ) {
		char *line = p_cur->line;

		err = at_tok_start(&line);
		if (err < 0) goto error;

		err = at_tok_nextint(&line, &skip);
		if (err < 0) goto error;

		// If we're unregistered, we may just get
		// a "+COPS: 0" response
		if (!at_tok_hasmore(&line)) {
			response[i] = NULL;
			continue;
		}

		err = at_tok_nextint(&line, &skip);
		if (err < 0) goto error;

		// a "+COPS: 0, n" response is also possible
		if (!at_tok_hasmore(&line)) {
			response[i] = NULL;
			continue;
		}

		err = at_tok_nextstr(&line, &(response[i]));
		if (err < 0) goto error;
	}

	if (i != 3) {
		/* expect 3 lines exactly */
		goto error;
	}

	RIL_onRequestComplete(t, RIL_E_SUCCESS, response, sizeof(response));
	at_response_free(p_response);

	return;
error:
	LOGE("requestOperator must not return error when radio is on");
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	at_response_free(p_response);
}


int getZmdsValue()
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
void _requestGetPreferredNetworkTypeWCDMA(void *data, size_t datalen,
		RIL_Token t)
{
	int err = 0;
	int response = 0;
	int type;

	type = getZmdsValue();

	if(type == 4 )
		response = 0; //auto
	else if(type == 13)
		response = 1; //gsm only
	else if(type == 14) 
		response = 2; //wcdma only

	RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(int));
	return;
}

void requestSetFacilityLock(void *data, size_t datalen, RIL_Token t)
{
	int err;
	ATResponse *atresponse = NULL;
	char *cmd = NULL;
	char *facility_string = NULL;
	int facility_mode = -1;
	char *facility_mode_str = NULL;
	char *facility_password = NULL;
	char *facility_class = NULL;
	int num_retries = -1;

	assert(datalen >= (4 * sizeof(char **)));

	facility_string = ((char **) data)[0];
	facility_mode_str = ((char **) data)[1];
	facility_password = ((char **) data)[2];
	facility_class = ((char **) data)[3];

	assert(*facility_mode_str == '0' || *facility_mode_str == '1');
	facility_mode = atoi(facility_mode_str);


	asprintf(&cmd, "AT+CLCK=\"%s\",%d,\"%s\",%s", facility_string,
			facility_mode, facility_password, facility_class);
	err = at_send_command(cmd, &atresponse);
	free(cmd);
	if (err < 0 || atresponse->success == 0) {
		goto error;
	}

	RIL_onRequestComplete(t, RIL_E_SUCCESS, &num_retries, sizeof(int *));
	at_response_free(atresponse);
	return;

error:
	at_response_free(atresponse);
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_SET_FACILITY_LOCK, requestSetFacilityLock)

	/**
	 * RIL_REQUEST_QUERY_FACILITY_LOCK
	 *
	 * Query the status of a facility lock state.
	 */
void requestQueryFacilityLock(void *data, size_t datalen, RIL_Token t)
{
	int err, rat, response;
	ATResponse *atresponse = NULL;
	char *cmd = NULL;
	char *line = NULL;
	char *facility_string = NULL;
	char *facility_password = NULL;
	char *facility_class = NULL;

	assert(datalen >= (3 * sizeof(char **)));

	facility_string = ((char **) data)[0];
	facility_password = ((char **) data)[1];
	facility_class = ((char **) data)[2];

	/* do not support FD string, force copy string SC */
	if(strcmp(facility_string, "FD") == 0){
		strcpy(facility_string, "SC");
	}

	asprintf(&cmd, "AT+CLCK=\"%s\",2,\"%s\",%s", facility_string,
			facility_password, facility_class);

	err = at_send_command_singleline(cmd, "+CLCK:", &atresponse);
	free(cmd);
	if (err < 0 || atresponse->success == 0) {
		goto error;
	}

	line = atresponse->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0)
		goto error;

	err = at_tok_nextint(&line, &response);
	if (err < 0)
		goto error;

	RIL_onRequestComplete(t, RIL_E_SUCCESS, &response, sizeof(int));

finally:
	at_response_free(atresponse);
	return;

error:
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	goto finally;
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_QUERY_FACILITY_LOCK, requestQueryFacilityLock)

	/* * "response" is const char **
	 * ((const char **)response)[0] is MDN if CDMA subscription is available
	 * ((const char **)response)[1] is a comma separated list of H_SID (Home SID) if
	 *                              CDMA subscription is available, in decimal format
	 * ((const char **)response)[2] is a comma separated list of H_NID (Home NID) if
	 *                              CDMA subscription is available, in decimal format
	 * ((const char **)response)[3] is MIN (10 digits, MIN2+MIN1) if CDMA subscription is available
	 * ((const char **)response)[4] is PRL version if CDMA subscription is available*/

void requestCDMASubScription(void *data, size_t datalen, RIL_Token t)
{
	char *response[5]={"23122","22322","23233","1132565870","1.0"};
	RIL_onRequestComplete(t, RIL_E_SUCCESS, response, sizeof(response));   
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_CDMA_SUBSCRIPTION, requestCDMASubScription)


static first_cfun = 1;
extern void wait_for_pppd_down();
static void requestRadioPower(void *data, size_t datalen, RIL_Token t)
{
	int onOff;
	int err=0;
	ATResponse *p_response = NULL;

	assert (datalen >= sizeof(int *));
	onOff = ((int *)data)[0];


	if (onOff == 0 && sState != RADIO_STATE_OFF) {

		if (first_cfun == 0) {
			stop_pppd();
			wait_for_pppd_down();

			err = at_send_command("AT+CFUN=0", &p_response);
			if (err < 0) 
				goto error;
		} else {
			first_cfun = 0;		
		}
		setRadioState(RADIO_STATE_OFF);

	} else if (onOff > 0 && sState == RADIO_STATE_OFF) {
		err = at_send_command("AT+CFUN=1", &p_response);
		
		if (err < 0|| p_response->success == 0) {
			// Some stacks return an error when there is no SIM,
			// but they really turn the RF portion on
			// So, if we get an error, let's check to see if it
			// turned on anyway

			if (getRadioPower() != 1) {
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
REGISTER_DEFAULT_ITEM(RIL_REQUEST_RADIO_POWER, requestRadioPower)

	/**
	 * RIL_REQUEST_SET_LOCATION_UPDATES
	 *
	 * Enables/disables network state change notifications due to changes in
	 * LAC and/or CID (basically, +CREG=2 vs. +CREG=1).  
	 *
	 * Note:  The RIL implementation should default to "updates enabled"
	 * when the screen is on and "updates disabled" when the screen is off.
	 *
	 * See also: RIL_REQUEST_SCREEN_STATE, RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED.
	 */
void requestSetLocationUpdates(void *data, size_t datalen, RIL_Token t)
{
	int enable = 0;
	int err = 0;
	char *cmd;
	ATResponse *atresponse = NULL;

	enable = ((int *) data)[0];
	assert(enable == 0 || enable == 1);

	asprintf(&cmd, "AT+CREG=%d", (enable == 0 ? 1 : 2));
	err = at_send_command(cmd, &atresponse);
	free(cmd);

	if (err < 0 || atresponse->success == 0)
		goto error;

	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

finally:
	at_response_free(atresponse);
	return;

error:
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	goto finally;
}


void setRadioState(RIL_RadioState newState)
{
	RIL_RadioState oldState;

	pthread_mutex_lock(&s_state_mutex);

	oldState = sState;

	if (s_closed > 0) {
		// If we're closed, the only reasonable state is
		// RADIO_STATE_UNAVAILABLE
		// This is here because things on the main thread
		// may attempt to change the radio state after the closed
		// event happened in another thread
		newState = RADIO_STATE_UNAVAILABLE;
	}

	if (sState != newState || s_closed > 0) {
		sState = newState;

		pthread_cond_broadcast (&s_state_cond);
	}

	pthread_mutex_unlock(&s_state_mutex);


	/* do these outside of the mutex */
	if (sState != oldState) {
		LOGD("RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED sState = %d,old = %d",sState,oldState);
		RIL_onUnsolicitedResponse (RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED,
				NULL, 0);

		if (sState == RADIO_STATE_SIM_READY) {
			onSIMReady();
		} else if (sState == RADIO_STATE_SIM_NOT_READY) {
			onRadioPowerOn();
		}
	}
}


/** returns 1 if on, 0 if off, and -1 on error */
int getRadioPower()
{
	ATResponse *p_response = NULL;
	int err;
	char *line;
	int ret;

	err = at_send_command_singleline("AT+CFUN?", "+CFUN:", &p_response);

	if (err < 0 || p_response->success == 0) {
		// assume radio is off
		goto error;
	}

	line = p_response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &ret);
	if (err < 0) goto error;

	at_response_free(p_response);

	return ret;

error:

	at_response_free(p_response);
	return -1;
}

static int onNetworkStateChanged(char* s, char* sms_pdu)
{
	int err;
	int reg_value;
	if (strStartsWith(s,"+CREG:") ||  strStartsWith(s,"+CGREG") ) {
		RIL_onUnsolicitedResponse (
				RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED,
				NULL, 0);
	}
	return UNSOLICITED_SUCCESSED;
}

REGISTER_DEFAULT_UNSOLICITED(CREG, "+CREG:", onNetworkStateChanged)
REGISTER_DEFAULT_UNSOLICITED(CGREG, "+CGREG:", onNetworkStateChanged)
REGISTER_DEFAULT_UNSOLICITED(MODE, "^MODE:", onNetworkStateChanged)
REGISTER_DEFAULT_UNSOLICITED(DSDORMANT, "^DSDORMANT:", onNetworkStateChanged)
