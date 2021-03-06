#ifndef SXX_RIL_H
#define SXX_RIL_H

#include <telephony/ril.h>
#include <pthread.h>
#define LOG_TAG "RIL"
#include <utils/Log.h>

#include "atchannel.h"
#include "at_tok.h"
#include "misc.h"
#include "helper/gsm.h"
#include "helper/sms_gsm.h"
#include "helper/bit_op.h"

#define MAX_AT_RESPONSE (8 * 1024)
#define DEFAULT_CHAT_OPTION "/system/bin/chat -v ABORT 'BUSY' ABORT 'NO CARRIER' ABORT 'NO ANSWER' TIMEOUT 6 '' 'AT' '' 'ATDT*99# CONNECT'"

#ifdef RIL_SHLIB
extern const struct RIL_Env *s_rilenv;
extern int s_closed;

#define RIL_onRequestComplete(t, e, response, responselen) s_rilenv->OnRequestComplete(t,e, response, responselen)
#define RIL_onUnsolicitedResponse(a,b,c) s_rilenv->OnUnsolicitedResponse(a,b,c)
#define RIL_requestTimedCallback(a,b,c) s_rilenv->RequestTimedCallback(a,b,c)
#endif

#define LEVEL_MANAGER_INIT	101
#define LEVEL_MODEM_REG 	102
#define LEVEL_MODEM_DETECT 	103
#define LEVEL_REQUEST_REG 	104

typedef void (*request_handler)(void *data, size_t datalen, RIL_Token t);
extern void register_request(int request, request_handler handler, int vid_pid);

#define VID_PID_GENERIC (-1)

#define REGISTER_REQUEST_ITEM(request, handler, vid_pid) \
__attribute__((constructor(LEVEL_REQUEST_REG))) \
static void register_##request() {\
	register_request(request, handler, vid_pid);\
}

#define REGISTER_DEFAULT_ITEM(request, handler) \
	REGISTER_REQUEST_ITEM(request, handler, VID_PID_GENERIC)

#define UNSOLICITED_FAILED 1
#define UNSOLICITED_SUCCESSED 0
typedef int (*unsolicited_handler)(char* s, char* sms_pdu);
extern void register_unsolicited(char* prefix, unsolicited_handler handler, int vid_pid);
#define REGISTER_UNSOLICITED_ITEM(name, prefix, handler, vid_pid) \
__attribute__((constructor(LEVEL_REQUEST_REG))) \
static void register_##name() {\
	register_unsolicited(prefix, handler, vid_pid);\
}
#define REGISTER_DEFAULT_UNSOLICITED(name, prefix, handler) \
	REGISTER_UNSOLICITED_ITEM(name, prefix, handler, VID_PID_GENERIC)


typedef void (*init_handler)(void);
typedef void (*bringup_handler)(void);
typedef void (*periodic_handler)(void);
typedef struct {
	char* name;
	char* at_port;
	char* data_port;
	char* chat_option;
	int vid_pid;
	init_handler init;
	bringup_handler bringup;
    periodic_handler periodic;
}modem_spec_t;
void register_modem(modem_spec_t* modem);

#define REGISTER_MODEM(modem, spec) \
__attribute__((constructor(LEVEL_MODEM_REG))) \
void register_##modem() {\
	register_modem(spec);\
}

extern void process_request(int request, void *data, size_t datalen, RIL_Token t);
extern int process_unsolicited(char* s, char* sms_pdu);
extern char* get_at_port();
extern char* get_data_port();
extern char* get_chat_option();
extern void modem_init_specific();
extern void modem_periodic();
extern void hide_info(int hide);

extern RIL_RadioState sState ;
extern pthread_mutex_t s_state_mutex ;
extern pthread_cond_t s_state_cond ;


typedef enum {
    SIM_ABSENT = 0,
    SIM_NOT_READY = 1,
    SIM_READY = 2,
    SIM_PIN = 3,
    SIM_PUK = 4,
    SIM_NETWORK_PERSONALIZATION = 5
} SIM_Status; 

typedef struct
{
  RIL_CardState card_state;
  RIL_PinState  universal_pin_state;             /* applicable to USIM and CSIM: RIL_PINSTATE_xxx */
  int           gsm_umts_subscription_app_index; /* value < RIL_CARD_MAX_APPS */
  int           cdma_subscription_app_index;     /* value < RIL_CARD_MAX_APPS */
  int           num_applications;                /* value <= RIL_CARD_MAX_APPS */
  RIL_AppStatus applications[RIL_CARD_MAX_APPS];
} RIL_CardStatus;

typedef struct {
    int command;    /* one of the commands listed for TS 27.007 +CRSM*/
    int fileid;     /* EF id */
    char *path;     /* "pathid" from TS 27.007 +CRSM command.
                       Path is in hex asciii format eg "7f205f70"
                       Path must always be provided.
                     */
    int p1;
    int p2;
    int p3;
    char *data;     /* May be NULL*/
    char *pin2;     /* May be NULL*/
} RIL_SIM_IO;

extern int stop_pppd();

static const struct timeval TIMEVAL_3 = {3,0};
static const struct timeval TIMEVAL_10 = {10,0};

#endif
