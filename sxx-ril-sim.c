#include <telephony/ril.h>
#include <assert.h>
#include <stdio.h>


#include "sxx-ril.h"

static const struct timeval TIMEVAL_SIMPOLL = { 1, 0 };
static const struct timeval TIMEVAL_SIMRESET = { 60, 0 };

/** do post- SIM ready initialization */
void onSIMReady()
{
    at_send_command_singleline("AT+CSMS=1", "+CSMS:", NULL); 
    at_send_command("AT+CNMI=1,1,2,0,0", NULL);
}


void onSimStateChanged(const char *s)
{
    int err, state;
    char *tok;
    char *line = tok = strdup(s);

    RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_SIM_STATUS_CHANGED, NULL, 0);


    /* 
     * Now, find out if we went to poweroff-state. If so, enqueue some loop
     * to try to reset the SIM for a minute or so to try to recover.
     */
    err = at_tok_start(&line);
    if (err < 0)
        goto error;

    err = at_tok_nextint(&line, &state);
    if (err < 0)
        goto error;

finally:
    free(tok);
    return;

error:
    LOGE("ERROR in onSimStateChanged!");
    goto finally;
}

/** Returns one of SIM_*. Returns SIM_NOT_READY on error. */
SIM_Status getSIMStatus(int falce)
{
   ATResponse *p_response = NULL;
    int err;
    int ret;
    char *cpinLine;
    char *cpinResult;

    if ( falce==0 && (sState == RADIO_STATE_OFF || sState == RADIO_STATE_UNAVAILABLE)) {
        ret = SIM_NOT_READY;
        goto done;
    }

    err = at_send_command_singleline("AT+CPIN?", "+CPIN:", &p_response);

    if (err != 0) {
        //ret = SIM_NOT_READY;
        ret = SIM_ABSENT;
        goto done;
    }

    switch (at_get_cme_error(p_response)) {
        case CME_SUCCESS:
            break;

        case CME_SIM_NOT_INSERTED:
            ret = SIM_ABSENT;
            goto done;

        default:
            //ret = SIM_NOT_READY;
            ret = SIM_ABSENT;
            goto done;
    }

    /* CPIN? has succeeded, now look at the result */

    cpinLine = p_response->p_intermediates->line;
    err = at_tok_start (&cpinLine);

    if (err < 0) {
        ret = SIM_NOT_READY;
        goto done;
    }

    err = at_tok_nextstr(&cpinLine, &cpinResult);

    if (err < 0) {
        ret = SIM_NOT_READY;
        goto done;
    }

    if (0 == strcmp (cpinResult, "SIM PIN")) {
        ret = SIM_PIN;
        goto done;
    } else if (0 == strcmp (cpinResult, "SIM PUK")) {
        ret = SIM_PUK;
        goto done;
    } else if (0 == strcmp (cpinResult, "PH-NET PIN")) {
        return SIM_NETWORK_PERSONALIZATION;
    } else if (0 != strcmp (cpinResult, "READY"))  {
        /* we're treating unsupported lock types as "sim absent" */
        ret = SIM_ABSENT;
        goto done;
    }

    at_response_free(p_response);
    p_response = NULL;
    cpinResult = NULL;

    ret = SIM_READY;

done:
    at_response_free(p_response);
    return ret;
}

/**
 * Get the current card status.
 *
 * This must be freed using freeCardStatus.
 * @return: On success returns RIL_E_SUCCESS.
 */
static int getCardStatus(RIL_CardStatus **pp_card_status) 
{
     static RIL_AppStatus app_status[] = {
        // SIM_ABSENT = 0
        { RIL_APPTYPE_UNKNOWN, RIL_APPSTATE_UNKNOWN, RIL_PERSOSUBSTATE_UNKNOWN,
          NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
        // SIM_NOT_READY = 1
        { RIL_APPTYPE_SIM, RIL_APPSTATE_DETECTED, RIL_PERSOSUBSTATE_UNKNOWN,
          NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
        // SIM_READY = 2
        { RIL_APPTYPE_SIM, RIL_APPSTATE_READY, RIL_PERSOSUBSTATE_READY,
          NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
        // SIM_PIN = 3
        { RIL_APPTYPE_SIM, RIL_APPSTATE_PIN, RIL_PERSOSUBSTATE_UNKNOWN,
          NULL, NULL, 0, RIL_PINSTATE_ENABLED_NOT_VERIFIED, RIL_PINSTATE_UNKNOWN },
        // SIM_PUK = 4
        { RIL_APPTYPE_SIM, RIL_APPSTATE_PUK, RIL_PERSOSUBSTATE_UNKNOWN,
          NULL, NULL, 0, RIL_PINSTATE_ENABLED_BLOCKED, RIL_PINSTATE_UNKNOWN },
        // SIM_NETWORK_PERSONALIZATION = 5
        { RIL_APPTYPE_SIM, RIL_APPSTATE_SUBSCRIPTION_PERSO, RIL_PERSOSUBSTATE_SIM_NETWORK,
          NULL, NULL, 0, RIL_PINSTATE_ENABLED_NOT_VERIFIED, RIL_PINSTATE_UNKNOWN }
    };

    RIL_CardState card_state;
    int num_apps;

    int sim_status = getSIMStatus(0);
    if (sim_status == SIM_ABSENT) {
        card_state = RIL_CARDSTATE_ABSENT;
        num_apps = 0;
    } else {
        card_state = RIL_CARDSTATE_PRESENT;
        num_apps = 1;
    }

    // Allocate and initialize base card status.
    RIL_CardStatus *p_card_status = malloc(sizeof(RIL_CardStatus));
    p_card_status->card_state = card_state;
    p_card_status->universal_pin_state = RIL_PINSTATE_UNKNOWN;
    p_card_status->gsm_umts_subscription_app_index = RIL_CARD_MAX_APPS;
    p_card_status->cdma_subscription_app_index = RIL_CARD_MAX_APPS;
    p_card_status->num_applications = num_apps;

    // Initialize application status
    int i;
    for (i = 0; i < RIL_CARD_MAX_APPS; i++) {
            p_card_status->applications[i] = app_status[SIM_ABSENT];
    }

    // Pickup the appropriate application status
    // that reflects sim_status for gsm.
    if (num_apps != 0) {
        // Only support one app, gsm
        p_card_status->num_applications = 1;
        p_card_status->gsm_umts_subscription_app_index = 0;
	 			p_card_status->cdma_subscription_app_index = 0;
            p_card_status->applications[0] = app_status[sim_status];
    }
    *pp_card_status = p_card_status;
     return RIL_E_SUCCESS;
}

/**
 * Free the card status returned by getCardStatus.
 */
static void freeCardStatus(RIL_CardStatus *p_card_status) {
   if(p_card_status == NULL ) return ;
    free(p_card_status);
}

/**
 * SIM ready means any commands that access the SIM will work, including:
 *  AT+CPIN, AT+CSMS, AT+CNMI, AT+CRSM
 *  (all SMS-related commands).
 */
void pollSIMState(void *param)
{
    if ((sState != RADIO_STATE_SIM_NOT_READY ) 
            && (sState != RADIO_STATE_RUIM_NOT_READY ))
    {
        // no longer valid to poll
        return;
    }

    switch(getSIMStatus(0)) {
        case SIM_ABSENT:
        case SIM_PIN:
        case SIM_PUK:
        case SIM_NETWORK_PERSONALIZATION:
        default:
                setRadioState(RADIO_STATE_SIM_LOCKED_OR_ABSENT);
        return;

        case SIM_NOT_READY:
            RIL_requestTimedCallback (pollSIMState, NULL, &TIMEVAL_SIMPOLL);
        return;

        case SIM_READY:
                setRadioState(RADIO_STATE_SIM_READY);
        return;
    }
}

/** 
 * RIL_REQUEST_GET_SIM_STATUS
 *
 * Requests status of the SIM interface and the SIM card.
 * 
 * Valid errors:
 *  Must never fail. 
 */

void requestGetSimStatus(void *data, size_t datalen, RIL_Token t)
{
    RIL_CardStatus *p_card_status;
    char *p_buffer;
    int buffer_size;

    int result = getCardStatus(&p_card_status);
    if (result == RIL_E_SUCCESS) {
           p_buffer = (char *)p_card_status;
           buffer_size = sizeof(*p_card_status);
    } else {
             p_buffer = NULL;
             buffer_size = 0;
     }
    RIL_onRequestComplete(t, result, p_buffer, buffer_size);
    freeCardStatus(p_card_status);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_GET_SIM_STATUS, requestGetSimStatus)

typedef struct{
        int  index;
        char *name;
        int  internate_code;
        char *number;
}PhoneBook;

#define MAX_CUST_BUF_LEN  (420)
static char custBuf[MAX_CUST_BUF_LEN]={0};

static void convertPhoneBookNumber(char * pDstBuf,char * pSrcBuf)
{
        int i;
        int iTotalLen=strlen(pSrcBuf);
        int iLen=(iTotalLen/2)*2;
        
        for(i=0;i<iLen;)
        {
		if (*pSrcBuf == '+') {
			i++;
			pSrcBuf++;
			continue;
		}
                *(pDstBuf+1)=*pSrcBuf;
                *pDstBuf=*(pSrcBuf+1);
                pDstBuf=pDstBuf+2;
                pSrcBuf=pSrcBuf+2;
                i=i+2; 
        }
        if(iLen<iTotalLen)
                *(pDstBuf+1)=*pSrcBuf;

}

static void convertPhoneBookInternateCode(char * pDstBuf,int iInternateCode)
{
        if(iInternateCode==0x81)
                *pDstBuf='8';
        if(iInternateCode==0x91)
                *pDstBuf='9';
        pDstBuf++;
        *pDstBuf='1';
}

static void convertPhoneBookNumberLen(char * pDstBuf,char * pSrcBuf)
{
        int iLen=strlen(pSrcBuf);
        iLen=(iLen+1)/2+1;
        *pDstBuf='0';
        pDstBuf++;
        switch(iLen)
        {
                default:
                case 0x0: *pDstBuf='0';break;
                case 0x1: *pDstBuf='1';break;
                case 0x2: *pDstBuf='2';break;
                case 0x3: *pDstBuf='3';break;
                case 0x4: *pDstBuf='4';break;
                case 0x5: *pDstBuf='5';break;
                case 0x6: *pDstBuf='6';break;
                case 0x7: *pDstBuf='7';break;
                case 0x8: *pDstBuf='8';break;
                case 0x9: *pDstBuf='9';break;
                case 0xA: *pDstBuf='A';break;
                case 0xB: *pDstBuf='B';break;
                case 0xC: *pDstBuf='C';break;
                case 0xD: *pDstBuf='D';break;
                case 0xE: *pDstBuf='E';break;
                case 0xF: *pDstBuf='F';break;
        }
}


static void convertPhoneBookName(char * pDstBuf,char * pSrcBuf)
{
        int iLen=strlen(pSrcBuf);
        int i;
        for(i=0;i<iLen;i++)
                *pDstBuf++=*pSrcBuf++;
}

static int is_blank(char* str)
{
	int len = strlen(str), i;
	for(i = 0; i<len - 4; i += 4){
		if(str[i] != '0' || str[i+1] != '0' || str[i+2] != '2' || str[i+3] != '0')
			return 0;
	}
	return 1;
}
static char alpha[250];
static char* number2alpha(char* num)
{
	int len = strlen(num), i;
	char* p = alpha;
	for(i = 0; i < len && i < 28; i++){
		sprintf(p,"%02x",num[i]);
		p+=2;
	}
	*p = 0;
	return alpha;
}
static int  parsePhoneBook(ATResponse * p_response,char * pcustBuf)
{
        int err;
        char * line=NULL;
        PhoneBook stPhoneBook; 
        memset(pcustBuf,0x0,320);
        memset(pcustBuf,'F',56);
        memset(&stPhoneBook,0,sizeof(PhoneBook));
        if (p_response && p_response->p_intermediates) {
                line=p_response->p_intermediates->line;
                err=at_tok_start(&line);
                if(err<0) goto error;

                err=at_tok_nextint(&line,&(stPhoneBook.index));
                if(err<0) goto error;

                err=at_tok_nextstr(&line,&(stPhoneBook.number));
                if(err<0) goto error;

                err=at_tok_nextint(&line,&(stPhoneBook.internate_code));
                if(err<0) goto error;

                err=at_tok_nextstr(&line,&(stPhoneBook.name));
                if(err<0) goto error;


		
		if(!is_blank(stPhoneBook.name))	{
			//magic
			*pcustBuf = '8';
			pcustBuf++;
			*pcustBuf = '0';
			pcustBuf++;
               		convertPhoneBookName(pcustBuf,stPhoneBook.name);
		} else {
			convertPhoneBookName(pcustBuf,number2alpha(stPhoneBook.number));
		}
	
                convertPhoneBookNumberLen(pcustBuf+28,stPhoneBook.number);
                convertPhoneBookInternateCode(pcustBuf+30,stPhoneBook.internate_code);
                convertPhoneBookNumber(pcustBuf+32,stPhoneBook.number);
                
                LOGD("%s", pcustBuf);
                return 0x00;
        }

error:
        return -1;
}


static int  custRequestSIM_IO(RIL_SIM_IO * pRilSimIo,RIL_SIM_IO_Response * pSr)
{
        //static char custBuf[320]={0};
        char * cmd=NULL;
        ATResponse *p_response = NULL; 
        if (pRilSimIo->data == NULL && (pRilSimIo->fileid == 28474 || pRilSimIo->fileid == 28474)) {
                //get response
                if (pRilSimIo->command == 192) {
                        //get record size
                        if (pRilSimIo->p3 == 15) {  
                                pSr->sw1=0x90;
                                pSr->sw2=0x00;
                                pSr->simResponse=custBuf;
                                memset(pSr->simResponse,0,MAX_CUST_BUF_LEN);
                                strncpy(pSr->simResponse,"00001B586F3A040011F0220102011C",30);
                                return 0x00;
                        }
                }
                if (pRilSimIo->command == 178) {
                        if(pRilSimIo->p3 == 28) {
                                pSr->sw1=0x90;
                                pSr->sw2=0x00;
                                pSr->simResponse=custBuf;
                                memset(pSr->simResponse,0,MAX_CUST_BUF_LEN);
                                asprintf(&cmd,"AT+CPBR=%d",pRilSimIo->p1);
                                
				at_send_command("AT+CSCS=\"UCS2\"", NULL);
                                at_send_command_singleline(cmd,"+CPBR:",&p_response);
                                parsePhoneBook(p_response,custBuf);
                                at_response_free(p_response);
                                free(cmd);
                                return 0x00;
                        }
                }
        }
        return 0x01;
}

/**
 * RIL_REQUEST_SIM_IO
 *
 * Request SIM I/O operation.
 * This is similar to the TS 27.007 "restricted SIM" operation
 * where it assumes all of the EF selection will be done by the
 * callee.
 ok
 */
void  requestSimIo(void *data, size_t datalen, RIL_Token t)
{
    ATResponse *p_response = NULL;
    RIL_SIM_IO_Response sr;
    int err;
    char *cmd = NULL;
    RIL_SIM_IO *p_args;
    char *line;
    memset(&sr, 0, sizeof(sr));


    p_args = (RIL_SIM_IO *)data;

    /* FIXME handle pin2 */

    if (p_args->data == NULL) {
        asprintf(&cmd, "AT+CRSM=%d,%d,%d,%d,%d",
                    p_args->command, p_args->fileid,
                    p_args->p1, p_args->p2, p_args->p3);
    } else {
        asprintf(&cmd, "AT+CRSM=%d,%d,%d,%d,%d,%s",
                    p_args->command, p_args->fileid,
                    p_args->p1, p_args->p2, p_args->p3, p_args->data);
    }
    
    #if 1
    err=custRequestSIM_IO(p_args,&sr);
    if(err==0x00)
        goto CUST_OK;        
	#endif

    err = at_send_command_singleline(cmd, "+CRSM:", &p_response);

    if (err < 0 || p_response->success == 0) {
        goto error;
    }

    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &(sr.sw1));
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &(sr.sw2));
    if (err < 0) goto error;

    if (at_tok_hasmore(&line)) {
        err = at_tok_nextstr(&line, &(sr.simResponse));
        if (err < 0) goto error;
    }
    
CUST_OK:
    RIL_onRequestComplete(t, RIL_E_SUCCESS, &sr, sizeof(sr));
    at_response_free(p_response);
    free(cmd);

    return;
error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);
    free(cmd);

}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_SIM_IO, requestSimIo)

/**
 * Enter SIM PIN, might be PIN, PIN2, PUK, PUK2, etc.
 *
 * Data can hold pointers to one or two strings, depending on what we
 * want to enter. (PUK requires new PIN, etc.).
 *
 * FIXME: Do we need to return remaining tries left on error as well?
 *        Also applies to the rest of the requests that got the retries
 *        in later commits to ril.h.
 ok
 */
void  requestEnterSimPin(void*  data, size_t  datalen, RIL_Token  t)
{
    ATResponse   *p_response = NULL;
    int           err;
    char*         cmd = NULL;
    const char**  strings = (const char**)data;;

    int is_lock = 0;
    int sim_status = getSIMStatus(0);
    if(sim_status == SIM_PIN || sim_status == SIM_PUK){
        is_lock = 1;
    }
    
    if ( datalen == sizeof(char*) || (datalen == 2*sizeof(char*) && strings[1] == NULL)) {
        asprintf(&cmd, "AT+CPIN=%s", strings[0]);
    } else if ( datalen == 2*sizeof(char*) ) {
        asprintf(&cmd, "AT+CPIN=%s,%s", strings[0], strings[1]);
    } else
        goto error;

    err = at_send_command(cmd, &p_response);
    free(cmd);

    sleep(2);
    if (err < 0 || p_response->success == 0) {
error:
        RIL_onRequestComplete(t, RIL_E_PASSWORD_INCORRECT, NULL, 0);
    } else {
        RIL_onRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
        if(is_lock){
            setRadioState (RADIO_STATE_OFF);
            setRadioState (RADIO_STATE_SIM_READY);
        }
    }
    at_response_free(p_response);
}
REGISTER_DEFAULT_ITEM(RIL_REQUEST_ENTER_SIM_PIN,requestEnterSimPin)
REGISTER_DEFAULT_ITEM(RIL_REQUEST_ENTER_SIM_PUK,requestEnterSimPin)
REGISTER_DEFAULT_ITEM(RIL_REQUEST_ENTER_SIM_PIN2,requestEnterSimPin)
REGISTER_DEFAULT_ITEM(RIL_REQUEST_ENTER_SIM_PUK2,requestEnterSimPin)
REGISTER_DEFAULT_ITEM(RIL_REQUEST_CHANGE_SIM_PIN,requestEnterSimPin)
REGISTER_DEFAULT_ITEM(RIL_REQUEST_CHANGE_SIM_PIN2,requestEnterSimPin)
