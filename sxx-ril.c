#include <telephony/ril.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <alloca.h>
#include <getopt.h>
#include <sys/socket.h>
#include <cutils/sockets.h>
#include <termios.h>

#include "sxx-ril.h"

RIL_RadioState sState = RADIO_STATE_UNAVAILABLE;
pthread_mutex_t s_state_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t s_state_cond = PTHREAD_COND_INITIALIZER;
extern SIM_Status getSIMStatus(int falce);

static void onRequest (int request, void *data, size_t datalen, RIL_Token t);
static RIL_RadioState currentState();
static int onSupports (int requestCode);
static void onCancel (RIL_Token t);
static const char *getVersion();


/*** Static Variables ***/
static const RIL_RadioFunctions s_callbacks = {
	6,//RIL_VERSION
	onRequest,
	currentState,
	onSupports,
	onCancel,
	getVersion
};

const struct RIL_Env *s_rilenv;

static int 	s_port = -1;
const char 	* s_device_path = NULL;
static int	s_device_socket = 0;

/* trigger change to this with s_state_cond */
int s_closed = 0;

static int sFD;     /* file desc of AT channel */
static char sATBuffer[MAX_AT_RESPONSE+1];
static char *sATBufferCur = NULL;

static const struct timeval TIMEVAL_0 = {0,0};


static void sendCallStateChanged(void *param)
{
	RIL_onUnsolicitedResponse (
			RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED,
			NULL, 0);
}

static bool requestStateFilter(int request, RIL_Token t)
{
	/*
	 * These commands will not accept RADIO_NOT_AVAILABLE and cannot be executed
	 * before we are in SIM_STATE_READY so we just return GENERIC_FAILURE if
	 * not in SIM_STATE_READY.
	 */
	if (sState != RADIO_STATE_SIM_READY
			&& (request == RIL_REQUEST_WRITE_SMS_TO_SIM ||
				request == RIL_REQUEST_DELETE_SMS_ON_SIM)) {
		RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
		return true;
	}


	/* Ignore all requsts while is radio_state_unavailable */
	if (sState == RADIO_STATE_UNAVAILABLE) {
		/*
		 * The following command(s) must never fail. Return static state for
		 * these command(s) while in RADIO_STATE_UNAVAILABLE.
		 */
		if (request == RIL_REQUEST_GET_SIM_STATUS) {
			SIM_Status simStatus = getSIMStatus(2);
			
			RIL_onRequestComplete(t, RIL_REQUEST_GET_SIM_STATUS,
					(char *) &simStatus,
					sizeof(simStatus));
		}
		/*
		 * The following command must never fail. Return static state for this
		 * command while in RADIO_STATE_UNAVAILABLE.
		 */
		else if (request == RIL_REQUEST_SCREEN_STATE) {
			RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
		}
		/* Ignore all other requests when RADIO_STATE_UNAVAILABLE */
		else {
			RIL_onRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
		}
		return true;
	}

	/*
	 * Ignore all non-power requests when RADIO_STATE_OFF
	 * (except RIL_REQUEST_RADIO_POWER and
	 * RIL_REQUEST_GET_SIM_STATUS and a few more).
	 * This is according to reference RIL implementation.
	 * Note that returning RIL_E_RADIO_NOT_AVAILABLE for all ignored requests
	 * causes Android Telephony to enter state RADIO_NOT_AVAILABLE and block
	 * all communication with the RIL.
	 */
	if (sState == RADIO_STATE_OFF
			&& !(request == RIL_REQUEST_RADIO_POWER ||
				request == RIL_REQUEST_STK_GET_PROFILE ||
				request == RIL_REQUEST_STK_SET_PROFILE ||
				request == RIL_REQUEST_REPORT_STK_SERVICE_IS_RUNNING ||
				request == RIL_REQUEST_GET_SIM_STATUS ||
				request == RIL_REQUEST_GET_IMEISV ||
				request == RIL_REQUEST_GET_IMEI ||
				request == RIL_REQUEST_DEVICE_IDENTITY ||
				request == RIL_REQUEST_BASEBAND_VERSION ||
				request == RIL_REQUEST_SCREEN_STATE)) {
		RIL_onRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
		return true;
	}

	/*
	 * Ignore all non-power requests when RADIO_STATE_OFF
	 * and RADIO_STATE_SIM_NOT_READY (except RIL_REQUEST_RADIO_POWER
	 * and a few more).
	 */
	if ((sState == RADIO_STATE_OFF || sState == RADIO_STATE_SIM_NOT_READY)
			&& !(request == RIL_REQUEST_RADIO_POWER ||
				request == RIL_REQUEST_REPORT_STK_SERVICE_IS_RUNNING ||
				request == RIL_REQUEST_GET_SIM_STATUS ||
				request == RIL_REQUEST_GET_IMEISV ||
				request == RIL_REQUEST_GET_IMEI ||
				request == RIL_REQUEST_DEVICE_IDENTITY ||
				request == RIL_REQUEST_BASEBAND_VERSION ||
				request == RIL_REQUEST_SCREEN_STATE)) {
		RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
		return true;
	}

	/*
	 * Don't allow radio operations when sim is absent or locked!
	 * DIAL, GET_CURRENT_CALLS, HANGUP and LAST_CALL_FAIL_CAUSE are
	 * required to handle emergency calls.
	 */
	if (sState == RADIO_STATE_SIM_LOCKED_OR_ABSENT
			&& !(request == RIL_REQUEST_ENTER_SIM_PIN ||
				request == RIL_REQUEST_ENTER_SIM_PUK ||
				request == RIL_REQUEST_ENTER_SIM_PIN2 ||
				request == RIL_REQUEST_ENTER_SIM_PUK2 ||
				request == RIL_REQUEST_ENTER_NETWORK_DEPERSONALIZATION ||
				request == RIL_REQUEST_GET_SIM_STATUS ||
				request == RIL_REQUEST_RADIO_POWER ||
				request == RIL_REQUEST_GET_IMEISV ||
				request == RIL_REQUEST_GET_IMEI ||
				request == RIL_REQUEST_BASEBAND_VERSION ||
				request == RIL_REQUEST_DIAL ||
				request == RIL_REQUEST_GET_CURRENT_CALLS ||
				request == RIL_REQUEST_HANGUP ||
				request == RIL_REQUEST_HANGUP_FOREGROUND_RESUME_BACKGROUND ||
				request == RIL_REQUEST_SET_TTY_MODE ||
				request == RIL_REQUEST_QUERY_TTY_MODE ||
				request == RIL_REQUEST_DTMF ||
				request == RIL_REQUEST_DTMF_START ||
				request == RIL_REQUEST_DTMF_STOP ||
				request == RIL_REQUEST_LAST_CALL_FAIL_CAUSE ||
				request == RIL_REQUEST_SCREEN_STATE)) {
					RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
					return true;
				}

	return false;
}


	static void
onRequest (int request, void *data, size_t datalen, RIL_Token t)
{
	ATResponse *p_response;
	int err;
	if (requestStateFilter(request, t))
		goto finally;

	process_request(request, data, datalen, t);
finally:
	return;
}


	static RIL_RadioState
currentState()
{
	return sState;
}


	static int
onSupports (int requestCode)
{
	//@@@ todo

	return 1;
}

static void onCancel (RIL_Token t)
{
	//@@@todo

}

static const char * getVersion(void)
{
	return "sxx ril version 2.0";
}

void modem_init_generic()
{
	ATResponse *p_response = NULL;
	int err;
    
	at_send_command("AT+CREG=1", NULL);

	at_send_command("AT+CGREG=1", NULL);

	//at_send_command("AT+CGATT=1", NULL);

	at_send_command("AT+CCWA=1", NULL);

	at_send_command("AT+CMOD=0", NULL);

	at_send_command("AT+CMUT=0", NULL);

	at_send_command("AT+CSSN=0,1", NULL);

	at_send_command("AT+COLP=0", NULL);

	//at_send_command("AT+CSCS=\"UCS2\"", NULL);

	at_send_command("AT+CSMP =,,0,8", NULL);

	at_send_command("AT+CUSD=1", NULL);

	at_send_command("AT+CGEREP=1,0", NULL);

	err = at_send_command_numeric("AT+CPMS=\"SM\",\"MT\",\"SM\"", &p_response);
	if (err < 0 || p_response->success == 0) {
		LOGE("can not send AT+CPMS=\"SM\",\"MT\",\"SM\"\n");
	}

	at_send_command("AT+CMGF=0", NULL);

	//at_send_command("AT+CMGD=,4",NULL);

	at_send_command("AT+CSCA?", NULL);

	at_send_command("AT+CMEE=1", NULL);	
    
    setRadioState(RADIO_STATE_SIM_NOT_READY);
}

static void initializeCallback(void *param)
{
	property_set("ro.ril.ecclist", "119,122,120,112,911");
	setRadioState (RADIO_STATE_SIM_NOT_READY);
    int err;
	at_send_command("ATE", NULL);

	at_handshake();
	modem_init_specific();
	modem_init_generic();

}

static void waitForClose()
{
	pthread_mutex_lock(&s_state_mutex);

	while (s_closed == 0) {
		pthread_cond_wait(&s_state_cond, &s_state_mutex);
	}

	pthread_mutex_unlock(&s_state_mutex);
}

static int onTimeChanged(char* s, char* sms_pdu)
{
	char *line = NULL;
	int err;
	char *response;

	line = strdup(s);
	at_tok_start(&line);

	err = at_tok_nextstr(&line, &response);

	if (err != 0) {
		LOGE("invalid NITZ line %s\n", s);
	} else {
		RIL_onUnsolicitedResponse (
				RIL_UNSOL_NITZ_TIME_RECEIVED,
				response, strlen(response));
	}
	free(line);

	return UNSOLICITED_SUCCESSED;
}
REGISTER_DEFAULT_UNSOLICITED(CTZV, "%CTZV:", onTimeChanged)

/**
 * Called by atchannel when an unsolicited line appears
 * This is called on atchannel's reader thread. AT commands may
 * not be issued here
 */




static void onUnsolicited (const char *s, const char *sms_pdu)
{
	if (sState == RADIO_STATE_UNAVAILABLE) {
		return;
	}

	process_unsolicited(s, sms_pdu);
}

static void onATReaderClosed()
{
	LOGI("AT channel closed\n");
	at_close();
	s_closed = 1;

	setRadioState (RADIO_STATE_UNAVAILABLE);
}

static void onATTimeout()
{
	LOGI("AT channel timeout; closing\n");
	at_close();

	s_closed = 1;

	setRadioState (RADIO_STATE_UNAVAILABLE);
    exit(1);
}

static void usage(char *s)
{
#ifdef RIL_SHLIB
	fprintf(stderr, "reference-ril requires: -p <tcp port> or -d /dev/tty_device\n");
#else
	fprintf(stderr, "usage: %s [-p <tcp port>] [-d /dev/tty_device]\n", s);
	exit(-1);
#endif
}

extern int init_pppd_manager();
static void * mainLoop(void *param)
{
	int fd,mod_fd = -1;
	int ret;
	char dev_name[64]={0,};

	AT_DUMP("== ", "entering mainLoop()", -1 );
	at_set_on_reader_closed(onATReaderClosed);
	at_set_on_timeout(onATTimeout);
	init_pppd_manager();
	for (;;) {
		fd = -1;
		while  (fd < 0) {
			
			fd = open (s_device_path, O_RDWR);
			if ( fd >= 0 ) {
				struct termios  ios;
				tcgetattr( fd, &ios );
				ios.c_lflag = 0;  /* disable ECHO, ICANON, etc... */
				tcsetattr( fd, TCSANOW, &ios );

/*
fcntl(fd, F_SETFL, 0);
struct termios t;
tcgetattr( fd,&t);
t.c_cflag &= ~(CSIZE | CSTOPB | PARENB );	
t.c_cflag |= (CREAD | CLOCAL | CS8 |CRTSCTS);
//在调试接收短信遇到不能接收问题，把串口模式设置为 raw mode后可以接收
t.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);//raw mode
t.c_iflag &= ~( ICRNL | INLCR |IGNCR);
t.c_iflag &= ~(IXON | IXOFF );	
t.c_oflag &= ~OPOST;
cfsetispeed(&t, B115200);
cfsetospeed(&t, B115200);
tcsetattr( fd, TCSANOW, &t);
*/
			}

			if (fd < 0) {
				LOGD ("sxx : opening AT interface: %s. retrying...", s_device_path);
				sleep(1);
			}  
		}

		LOGD ("open AT interface success! \n");
        
        //sleep(8);
		s_closed = 0;
		ret = at_open(fd,onUnsolicited);

		if (ret < 0) {
			LOGE ("AT error %d on at_open\n", ret);
			return 0;
		}

		RIL_requestTimedCallback(initializeCallback, NULL, &TIMEVAL_0);

		waitForClose();
		
		stop_pppd();
		LOGI("Re-opening after close");
	}
}

#ifdef RIL_SHLIB

pthread_t s_tid_mainloop;

const RIL_RadioFunctions *RIL_Init(const struct RIL_Env *env, int argc, char **argv)
{
	int ret;
	int fd = -1;
	int opt;
	pthread_attr_t attr;

	s_rilenv = env;

	while ( -1 != (opt = getopt(argc, argv, "p:d:s:"))) {
		switch (opt) {
			case 'p':
				s_port = atoi(optarg);
				if (s_port == 0) {
					usage(argv[0]);
					return NULL;
				}
				LOGI("Opening loopback port %d\n", s_port);
				break;

			case 'd':
				s_device_path = optarg;
				break;

			case 's':
				s_device_path   = optarg;
				s_device_socket = 1;
				LOGI("Opening socket %s\n", s_device_path);
				break;

			default:
				usage(argv[0]);
				return NULL;
		}
	}

	s_device_path = get_at_port();

	LOGD("Opening tty device %s\n", s_device_path);

	if (s_port < 0 && s_device_path == NULL) {
		usage(argv[0]);
		return NULL;
	}

	pthread_attr_init (&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	ret = pthread_create(&s_tid_mainloop, &attr, mainLoop, NULL);

	return &s_callbacks;
}
#else /* RIL_SHLIB */
int main (int argc, char **argv)
{
	int ret;
	int fd = -1;
	int opt;

	while ( -1 != (opt = getopt(argc, argv, "p:d:"))) {
		switch (opt) {
			case 'p':
				s_port = atoi(optarg);
				if (s_port == 0) {
					usage(argv[0]);
				}
				LOGI("Opening loopback port %d\n", s_port);
				break;

			case 'd':
				s_device_path = optarg;
				break;

			case 's':
				s_device_path   = optarg;
				s_device_socket = 1;
				LOGI("Opening socket %s\n", s_device_path);
				break;

			default:
				usage(argv[0]);
		}
	}

	if (s_port < 0 && s_device_path == NULL) {
		usage(argv[0]);
	}

	s_device_path = get_at_port();

	LOGI("Opening tty device %s\n", s_device_path);

	RIL_register(&s_callbacks);

	mainLoop(NULL);

	return 0;
}

#endif /* RIL_SHLIB */
