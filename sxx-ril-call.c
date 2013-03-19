#include <stdio.h>
#include <telephony/ril.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include "sxx-ril.h"

/* Last call fail cause, obtained by *ECAV. */
static int s_lastCallFailCause = CALL_FAIL_ERROR_UNSPECIFIED;

static const struct timeval TIMEVAL_CALLSTATEPOLL = {0,500000};


static int clccStateToRILState(int state, RIL_CallState *p_state)
{
	switch(state) {
		case 0: *p_state = RIL_CALL_ACTIVE;   return 0;
		case 1: *p_state = RIL_CALL_HOLDING;  return 0;
		case 2: *p_state = RIL_CALL_DIALING;  return 0;
		case 3: *p_state = RIL_CALL_ALERTING; return 0;
		case 4: *p_state = RIL_CALL_INCOMING; return 0;
		case 5: *p_state = RIL_CALL_WAITING;  return 0;
		default: return -1;
	}
}

static int callFromCLCCLine(char *line, RIL_Call *p_call)
{
	int err;
	int state;
	int mode;

	err = at_tok_start(&line);
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &(p_call->index));
	if (err < 0) goto error;

	err = at_tok_nextbool(&line, &(p_call->isMT));
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &state);
	if (err < 0) goto error;

	err = clccStateToRILState(state, &(p_call->state));
	if (err < 0) goto error;

	err = at_tok_nextint(&line, &mode);
	if (err < 0) goto error;

	p_call->isVoice = (mode == 0);
	
	// ignore data call mode
	if (mode == 1)
		goto error;

	err = at_tok_nextbool(&line, &(p_call->isMpty));
	if (err < 0) goto error;

	if (at_tok_hasmore(&line)) {
		err = at_tok_nextstr(&line, &(p_call->number));

		/* tolerate null here */
		if (err < 0) return 0;

		err = at_tok_nextint(&line, &p_call->toa);
		if (err < 0) goto error;
	}else{
		goto error;    
	}

	p_call->uusInfo = NULL;

	return 0;

error:
	LOGE("invalid CLCC line\n");
	return -1;
}
/**
 * RIL_REQUEST_HANGUP_WAITING_OR_BACKGROUND
 *
 * Hang up waiting or held (like AT+CHLD=0)
 ok
 */   
void requestHangupWaitingOrBackground(void *data, size_t datalen,
		RIL_Token t)
{
	int *p_line;
	ATResponse *p_response = NULL;

	int ret;
	char *cmd;

	p_line = (int *)data;

	asprintf(&cmd, "AT+CHLD=0");

	ret = at_send_command(cmd, &p_response);

	free(cmd);

	if (ret < 0 || p_response->success == 0) {
		ret = at_send_command("AT+CHUP", NULL);
		if (ret < 0 || p_response->success == 0) 
		{
			LOGD("requestHangup--faild");
			RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
			return ;
		}
	}
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_HANGUP_WAITING_OR_BACKGROUND, requestHangupWaitingOrBackground)

	/**
	 * RIL_REQUEST_HANGUP_FOREGROUND_RESUME_BACKGROUND
	 *
	 * Hang up waiting or held (like AT+CHLD=1)
	 ok
	 */
void requestHangupForegroundResumeBackground(void *data, size_t datalen,
		RIL_Token t)
{
	int *p_line;
	ATResponse *p_response = NULL;

	int ret;
	char *cmd;

	asprintf(&cmd, "AT+CHLD=1");

	ret = at_send_command(cmd, &p_response);

	free(cmd);

	if (ret < 0 || p_response->success == 0) {
		ret = at_send_command("AT+CHUP", NULL);
		if (ret < 0 || p_response->success == 0) 
		{
			LOGD("requestHangup--faild");
			RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
			return ;
		}
	}

	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_HANGUP_FOREGROUND_RESUME_BACKGROUND, requestHangupForegroundResumeBackground)

	/**
	 * RIL_REQUEST_SWITCH_WAITING_OR_HOLDING_AND_ACTIVE
	 *
	 * Switch waiting or holding call and active call (like AT+CHLD=2)
	 ok
	 */
void requestSwitchWaitingOrHoldingAndActive(void *data, size_t datalen,
		RIL_Token t)
{
	// 3GPP 22.030 6.5.5
	// "Places all active calls (if any exist) on hold and accepts
	//  the other (held or waiting) call."
	at_send_command("AT+CHLD=2", NULL);


	/* success or failure is ignored by the upper layer here.
	   it will call GET_CURRENT_CALLS and determine success that way */
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_SWITCH_WAITING_OR_HOLDING_AND_ACTIVE, requestSwitchWaitingOrHoldingAndActive)

	/**
	 * RIL_REQUEST_CONFERENCE
	 *
	 * Conference holding and active (like AT+CHLD=3)
	 ok
	 */
void requestConference(void *data, size_t datalen, RIL_Token t)
{
	// 3GPP 22.030 6.5.5
	// "Adds a held call to the conversation"
	at_send_command("AT+CHLD=3", NULL);

	/* success or failure is ignored by the upper layer here.
	   it will call GET_CURRENT_CALLS and determine success that way */
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_CONFERENCE, requestConference)

	/**
	 * RIL_REQUEST_SEPARATE_CONNECTION
	 *
	 * Separate a party from a multiparty call placing the multiparty call
	 * (less the specified party) on hold and leaving the specified party 
	 * as the only other member of the current (active) call
	 *
	 * Like AT+CHLD=2x
	 *
	 * See TS 22.084 1.3.8.2 (iii)
	 * TS 22.030 6.5.5 "Entering "2X followed by send"
	 * TS 27.007 "AT+CHLD=2x"
	 ok
	 */
void requestSeparateConnection(void *data, size_t datalen, RIL_Token t)
{
	int *p_line;

	int ret;
	char *cmd;

	p_line = (int *)data;

	asprintf(&cmd, "AT+CHLD=2%d", p_line[0]);

	ret = at_send_command(cmd, NULL);

	free(cmd);
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_SEPARATE_CONNECTION, requestSeparateConnection)

	/**
	 * RIL_REQUEST_EXPLICIT_CALL_TRANSFER
	 *
	 * Connects the two calls and disconnects the subscriber from both calls.
	 ok
	 */
void requestExplicitCallTransfer(void *data, size_t datalen, RIL_Token t)
{
	at_send_command("AT+CHLD=4", NULL);
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_EXPLICIT_CALL_TRANSFER, requestExplicitCallTransfer)

	/**
	 * RIL_REQUEST_UDUB
	 *
	 * Send UDUB (user determined used busy) to ringing or 
	 * waiting call answer (RIL_BasicRequest r).
	 ok
	 */
void requestUDUB(void *data, size_t datalen, RIL_Token t)
{
	/* user determined user busy */
	/* sometimes used: ATH */
	at_send_command("AT+CHLD=0", NULL);

	/* success or failure is ignored by the upper layer here.
	   it will call GET_CURRENT_CALLS and determine success that way */
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_UDUB, requestUDUB)

	/**
	 * RIL_REQUEST_SET_MUTE
	 *
	 * Turn on or off uplink (microphone) mute.
	 *
	 * Will only be sent while voice call is active.
	 * Will always be reset to "disable mute" when a new voice call is initiated.
	 ok
	 */
void requestSetMute(void *data, size_t datalen, RIL_Token t)
{
	int err;
	char *cmd;

	assert (datalen >= sizeof(int *));

	asprintf(&cmd, "AT+CMUT=%d", ((int*)data)[0]);

	err = at_send_command(cmd, NULL);
	free(cmd);

	if (err != 0) goto error;
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

	return;

error:
	LOGE("ERROR: requestSetMute failed");
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);

}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_SET_MUTE, requestSetMute)

	/**
	 * RIL_REQUEST_GET_MUTE
	 *
	 * Queries the current state of the uplink mute setting.
	 ok
	 */
void requestGetMute(void *data, size_t datalen, RIL_Token t)
{
	RIL_onRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_GET_MUTE, requestGetMute)

	/**
	 * RIL_REQUEST_LAST_CALL_FAIL_CAUSE
	 *
	 * Requests the failure cause code for the most recently terminated call.
	 *
	 * See also: RIL_REQUEST_LAST_PDP_FAIL_CAUSE
	 ok
	 */
void requestLastCallFailCause(void *data, size_t datalen, RIL_Token t)
{
	RIL_onRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_LAST_CALL_FAIL_CAUSE, requestLastCallFailCause)

static void sendCallStateChanged(void *param)
{
	RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED,
			NULL, 0);
}

/**
 * RIL_REQUEST_GET_CURRENT_CALLS 
 *
 * Requests current call list.
 ok
 */
void requestGetCurrentCalls(void *data, size_t datalen, RIL_Token t)
{
	int err;
	ATResponse *p_response;
	ATLine *p_cur;
	int countCalls;
	int countValidCalls;
	RIL_Call *p_calls;
	RIL_Call **pp_calls;
	int i;
	int needRepoll = 0;

	err = at_send_command_multiline ("AT+CLCC", "+CLCC:", &p_response);

	if (err != 0 || p_response->success == 0) {
		RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL,0);
		return;
	}

	//
	for (countCalls = 0, p_cur = p_response->p_intermediates
			; p_cur != NULL
			; p_cur = p_cur->p_next
	    ) {
		countCalls++;
	}

	//yes, there's an array of pointers and then an array of structures 

	pp_calls = (RIL_Call **)alloca(countCalls * sizeof(RIL_Call *));
	p_calls = (RIL_Call *)alloca(countCalls * sizeof(RIL_Call));
	memset (p_calls, 0, countCalls * sizeof(RIL_Call));

	// init the pointer array 
	for(i = 0; i < countCalls ; i++) {
		pp_calls[i] = &(p_calls[i]);
	}

	for (countValidCalls = 0, p_cur = p_response->p_intermediates
			; p_cur != NULL
			; p_cur = p_cur->p_next
	    ) {
		err = callFromCLCCLine(p_cur->line, p_calls + countValidCalls);

		if (err != 0) {
			continue;
		}


		if (p_calls[countValidCalls].state != RIL_CALL_ACTIVE
				&& p_calls[countValidCalls].state != RIL_CALL_HOLDING
		   ) {
			needRepoll = 1;
		}

		countValidCalls++;
	}

	RIL_onRequestComplete(t, RIL_E_SUCCESS, pp_calls,
			countValidCalls * sizeof (RIL_Call *));

	at_response_free(p_response);

#ifdef POLL_CALL_STATE
	if (countValidCalls) {  // We don't seem to get a "NO CARRIER" message from
		// smd, so we're forced to poll until the call ends.
		RIL_requestTimedCallback (sendCallStateChanged, NULL, &TIMEVAL_CALLSTATEPOLL);
	}
#else
	if (needRepoll) {
		RIL_requestTimedCallback (sendCallStateChanged, NULL, &TIMEVAL_CALLSTATEPOLL);
	}
#endif

	return;
error:
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL,0);
	at_response_free(p_response);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_GET_CURRENT_CALLS, requestGetCurrentCalls)


	/** 
	 * RIL_REQUEST_DIAL
	 *
	 * Initiate voice call.
	 ok
	 */
void requestDial(void *data, size_t datalen, RIL_Token t)
{
	RIL_Dial *p_dial;
	char *cmd;
	const char *clir;
	int ret;

	p_dial = (RIL_Dial *)data;

	switch (p_dial->clir) {
		case 1: clir = "I"; break;  /*invocation*/
		case 2: clir = "i"; break;  /*suppression*/
		default:
		case 0: clir = ""; break;   /*subscription default*/
	}

	asprintf(&cmd, "ATD%s;", p_dial->address);   
	ret = at_send_command(cmd, NULL);

	free(cmd);

	/* success or failure is ignored by the upper layer here.
	   it will call GET_CURRENT_CALLS and determine success that way */
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_DIAL, requestDial)

	/**
	 * RIL_REQUEST_ANSWER
	 *
	 * Answer incoming call.
	 *
	 * Will not be called for WAITING calls.
	 * RIL_REQUEST_SWITCH_WAITING_OR_HOLDING_AND_ACTIVE will be used in this case
	 * instead.
	 ok
	 */
void requestAnswer(void *data, size_t datalen, RIL_Token t)
{
	ATResponse *atresponse = NULL;
	int err;

	err = at_send_command("ATA", &atresponse);

	if (err < 0 || atresponse->success == 0)
		goto error;

	/* Success or failure is ignored by the upper layer here,
	   it will call GET_CURRENT_CALLS and determine success that way. */
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);

finally:
	at_response_free(atresponse);
	return;

error:
	RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
	goto finally;
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_ANSWER, requestAnswer)

	/**
	 * RIL_REQUEST_HANGUP
	 *
	 * Hang up a specific line (like AT+CHLD=1x).
	 ok
	 */
void requestHangup(void *data, size_t datalen, RIL_Token t)
{
	int *p_line;
	ATResponse *p_response = NULL;

	int ret;
	char *cmd;

	p_line = (int *)data;


	ret = at_send_command("AT+CHLD=?", &p_response);

	// 3GPP 22.030 6.5.5
	// "Releases a specific active call X"
	asprintf(&cmd, "AT+CHLD=1%d", p_line[0]);

	ret = at_send_command(cmd, &p_response);

	free(cmd);

	if (ret < 0 || p_response->success == 0) {
		ret = at_send_command("AT+CHUP", NULL);
		if (ret < 0 || p_response->success == 0) 
		{
			LOGD("requestHangup--faild");
			RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
			return ;
		}
	}
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_HANGUP, requestHangup)

	/**
	 * RIL_REQUEST_DTMF
	 *
	 * Send a DTMF tone
	 *
	 * If the implementation is currently playing a tone requested via
	 * RIL_REQUEST_DTMF_START, that tone should be cancelled and the new tone
	 * should be played instead.
	 ok
	 */
void requestDTMF(void *data, size_t datalen, RIL_Token t)
{
	char c = ((char *)data)[0];
	char *cmd;
	asprintf(&cmd, "AT+VTS=%c", (int)c);
	at_send_command(cmd, NULL);
	free(cmd);
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_DTMF, requestDTMF)

	/**
	 * RIL_REQUEST_DTMF_START
	 *
	 * Start playing a DTMF tone. Continue playing DTMF tone until 
	 * RIL_REQUEST_DTMF_STOP is received .
	 *
	 * If a RIL_REQUEST_DTMF_START is received while a tone is currently playing,
	 * it should cancel the previous tone and play the new one.
	 *
	 * See also: RIL_REQUEST_DTMF, RIL_REQUEST_DTMF_STOP.
	 ok
	 */
void requestDTMFStart(void *data, size_t datalen, RIL_Token t)
{
	//RIL_onRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
	char c = ((char *)data)[0];
	char *cmd;
	asprintf(&cmd, "AT+VTS=%c", (int)c);
	at_send_command(cmd, NULL);
	free(cmd);
	RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);    
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_DTMF_START, requestDTMFStart)

	/**
	 * RIL_REQUEST_DTMF_STOP
	 *
	 * Stop playing a currently playing DTMF tone.
	 *
	 * See also: RIL_REQUEST_DTMF, RIL_REQUEST_DTMF_START.
	 ok
	 */
void requestDTMFStop(void *data, size_t datalen, RIL_Token t)
{
	RIL_onRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_DTMF_STOP, requestDTMFStop)


static int onCallStateChanged(char* s, char* sms_pdu)
{
	RIL_onUnsolicitedResponse (
			RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED,
			NULL, 0);
	return UNSOLICITED_SUCCESSED;
}
REGISTER_DEFAULT_UNSOLICITED(CRING, "+CRING:", onCallStateChanged)
REGISTER_DEFAULT_UNSOLICITED(RING, "RING", onCallStateChanged)
REGISTER_DEFAULT_UNSOLICITED(NOCARRIER, "NO CARRIER", onCallStateChanged)
REGISTER_DEFAULT_UNSOLICITED(CCWA, "+CCWA", onCallStateChanged)
REGISTER_DEFAULT_UNSOLICITED(CEND, "^CEND", onCallStateChanged)
REGISTER_DEFAULT_UNSOLICITED(ZCEND, "+ZCEND:", onCallStateChanged)

static void callConnectedJob(void *param)
{
	at_send_command("AT+CLVL=6", NULL);
	at_send_command("AT+SIDET=0,1",NULL);
	at_send_command("AT+ECHO=1",NULL);
}

static int onCallConnected(char* s, char* sms_pdu)
{
	RIL_requestTimedCallback (callConnectedJob, NULL, NULL);
	return UNSOLICITED_SUCCESSED;
}
REGISTER_DEFAULT_UNSOLICITED(ZCCNT, "+ZCCNT:",onCallConnected);
