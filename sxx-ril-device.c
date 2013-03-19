#include <stdio.h>
#include <telephony/ril.h>
#include <pthread.h>
#include <string.h>

#include <assert.h>
#include "sxx-ril.h"

#define RIL_IMEISV_VERSION 2


/**
 * RIL_REQUEST_GET_IMSI
 *
 * Get the SIM IMSI
 *
 * Only valid when radio state is "RADIO_STATE_SIM_READY"
 *
 * "data" is NULL
 * "response" is a const char * containing the IMSI
 *
 * Valid errors:
 *  SUCCESS
 *  RADIO_NOT_AVAILABLE (radio resetting)
 *  GENERIC_FAILURE
 */
void requestGetIMSI(void *data, size_t datalen, RIL_Token t)
{
	int err;
	ATResponse *atresponse = NULL;
	char * response = NULL;
	char* line = NULL;

	err = at_send_command_singleline("AT+CIMI", "", &atresponse);
	if (err < 0 || atresponse->success == 0) goto error;

	line = atresponse->p_intermediates->line;

	response = (char *)alloca(sizeof(char *));

	err = at_tok_nextstr(&line, &response);
	if (err < 0) goto error;

	/*
	 * If get '+CIMI:' command, force copy to response.
	 * '+CIMI:' will make no APN issue.
	 */
	if(strstr(response, "+CIMI:") != NULL){
		strcpy(response, response + 6);
	}

	RIL_onRequestComplete(t, RIL_E_SUCCESS, response, sizeof(char *));
	at_response_free(atresponse);

	return;

error:
	at_response_free(atresponse);
	LOGE("ERROR: requestGetIMSI failed\n");
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_GET_IMSI, requestGetIMSI)

	/* RIL_REQUEST_DEVICE_IDENTITY
	 *
	 * RIL_REQUEST_DEVICE_IDENTITY
	 *
	 * Request the device ESN / MEID / IMEI / IMEISV.
	 *
	 * The request is always allowed and contains GSM and CDMA device identity;
	 * it substitutes the deprecated requests RIL_REQUEST_GET_IMEI and
	 * RIL_REQUEST_GET_IMEISV.
	 *
	 * If a NULL value is returned for any of the device id, it means that error
	 * accessing the device.
	 *
	 * When CDMA subscription is changed the ESN/MEID may change.  The application
	 * layer should re-issue the request to update the device identity in this case.
	 *
	 * "response" is const char **
	 * ((const char **)response)[0] is IMEI if GSM subscription is available
	 * ((const char **)response)[1] is IMEISV if GSM subscription is available
	 * ((const char **)response)[2] is ESN if CDMA subscription is available
	 * ((const char **)response)[3] is MEID if CDMA subscription is available
	 *
	 * Valid errors:
	 *  SUCCESS
	 *  RADIO_NOT_AVAILABLE
	 *  GENERIC_FAILURE
	 */
void requestDeviceIdentity(void *data, size_t datalen, RIL_Token t)
{
	_requestDeviceIdentityWCDMA(data, datalen, t);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_DEVICE_IDENTITY, requestDeviceIdentity)

	/* Deprecated */
	/**
	 * RIL_REQUEST_GET_IMEI
	 *
	 * Get the device IMEI, including check digit.
	 */
void requestGetIMEI(void *data, size_t datalen, RIL_Token t)
{
	_requestGetIMEIWCDMA(data, datalen, t);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_GET_IMEI, requestGetIMEI)



void _requestDeviceIdentityWCDMA(void *data, size_t datalen, RIL_Token t)
{
	ATResponse *atresponse = NULL;
	char* response[4]={NULL,};
	int err,i=0;

	/* IMEI */ 
	err = at_send_command_numeric("AT+CGSN", &atresponse);

	if (err < 0 || atresponse->success == 0) {
		goto error;
	} else {
		asprintf(&response[0], "%s", atresponse->p_intermediates->line);
	}

	/* IMEISV */
	asprintf(&response[1], "%02d", RIL_IMEISV_VERSION);

	/* CDMA not supported */
	response[2] = NULL;
	response[3] = NULL;

	RIL_onRequestComplete(t, RIL_E_SUCCESS,
			&response,
			sizeof(response));

	for(i=0;i<4;i++){
		if(response[i]) free(response[i]);
	}

finally:
	at_response_free(atresponse);
	return;

error:
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	goto finally;
}

void _requestGetIMEIWCDMA(void *data, size_t datalen, RIL_Token t)
{
	ATResponse *atresponse = NULL;
	int err;

	err = at_send_command_numeric("AT+CGSN", &atresponse);

	if (err < 0 || atresponse->success == 0) {
		RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	} else {
		property_set("ril.imei",atresponse->p_intermediates->line);

		RIL_onRequestComplete(t, RIL_E_SUCCESS,
				atresponse->p_intermediates->line,
				sizeof(char *));
	}
	at_response_free(atresponse);
	return;
}

/* Deprecated */
/**
 * RIL_REQUEST_GET_IMEISV
 *
 * Get the device IMEISV, which should be two decimal digits.
 */

void requestGetIMEISV(void *data, size_t datalen, RIL_Token t)
{
	char *response = NULL;

	asprintf(&response, "%02d", RIL_IMEISV_VERSION);

	RIL_onRequestComplete(t, RIL_E_SUCCESS,
			response,
			sizeof(char *));

	if (response)
		free(response);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_GET_IMEISV, requestGetIMEISV)

void requestReportStkServiceIsRunning(void *data, size_t datalen, RIL_Token t)
{
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_REPORT_STK_SERVICE_IS_RUNNING, requestReportStkServiceIsRunning)

	/**
	 * RIL_REQUEST_BASEBAND_VERSION
	 *
	 * Return string value indicating baseband version, eg
	 * response from AT+CGMR.
	 */

void requestBasebandVersion(void *data, size_t datalen, RIL_Token t)
{
	int err;
	ATResponse *atresponse = NULL;
	char * response;
	char* line;

	err = at_send_command_singleline("AT+CGMM", "", &atresponse);
	if (err < 0 || 
			atresponse->success == 0 || 
			atresponse->p_intermediates == NULL) {
		goto error;
	}

	line = atresponse->p_intermediates->line;

	response = (char *)alloca(sizeof(char *));

	err = at_tok_nextstr(&line, &response);
	if (err < 0) goto error;

	RIL_onRequestComplete(t, RIL_E_SUCCESS, response, sizeof(char *));

finally:
	at_response_free(atresponse);
	return;

error:
	LOGE("Error in requestBasebandVersion()");
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	goto finally;
}

REGISTER_DEFAULT_ITEM(RIL_REQUEST_BASEBAND_VERSION, requestBasebandVersion)
