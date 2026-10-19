#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#pragma clang diagnostic ignored "-Wformat"
#include "secril-shim.h"
#include "secril-sap.h"

#include <algorithm>
#include <atomic>
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>

#define ATOI_NULL_HANDLED(x) (x ? atoi(x) : 0)

static int parseHexCellId(const char *value) {
	if (value == NULL || *value == '\0') return -1;
	char *end = NULL;
	long parsed = strtol(value, &end, 16);
	return end == value || *end != '\0' || parsed < 0 || parsed > INT_MAX
			? -1 : static_cast<int>(parsed);
}

/* A copy of the original RIL function table. */
static const RIL_RadioFunctions *origRilFunctions;

/*
 * Some vendor RILs wait for the modem link in RIL_Init. Keep that wait away
 * from rild's main thread so the radio service can be registered immediately.
 */
static std::atomic<bool> vendorRilReady(false);

/* A copy of the ril environment passed to RIL_Init. */
static const struct RIL_Env *rilEnv;

/* Response data for RIL_REQUEST_VOICE_REGISTRATION_STATE */
static const size_t VOICE_REGSTATE_COUNT = 15;
static const size_t VOICE_REGSTATE_SIZE = VOICE_REGSTATE_COUNT * sizeof(char *);
static char *voiceRegStateResponse[VOICE_REGSTATE_COUNT];

/* Store voice radio technology */
static int voiceRadioTechnology = -1;

/*
 * Samsung's XMM6260/6262/6360 voice-registration reply omits the RAT. Keep
 * the last RAT actually reported by data registration so the compatibility
 * response does not claim UMTS while the modem is registered on EDGE.
 */
static int registeredDataRadioTechnology = RADIO_TECH_UNKNOWN;
static bool voiceRegistrationActive = false;
static bool dataRegistrationActive = false;

/* Store cdma subscription source */
static int cdmaSubscriptionSource = -1;

/* Store sim ruim status */
int simRuimStatus = -1;

/* Per-request state for translating DEVICE_IDENTITY into the two v7 queries. */
static const uint64_t IDENTITY_REQUEST_MAGIC = UINT64_C(0x5345434944454E54);
typedef struct {
	uint64_t magic;
	RIL_Token originalToken;
	int stage;
	char imei[16];
	char imeisv[3];
} IdentityRequest;

#ifndef SAMSUNG_NEXT_GEN_MODEM
/*
 * The XMM6260/6262/6360 RIL can remain indefinitely in registration state 0
 * after coverage is lost or the radio is cycled.  Android first queries the
 * selection mode and, when the blob reports automatic, deliberately does not
 * send SET_NETWORK_SELECTION_AUTOMATIC again.  The old CP therefore never
 * restarts its network search.
 *
 * Keep the recovery entirely inside the legacy compatibility shim.  It sends
 * the real request to libsec-ril.so; it never manufactures registration,
 * operator or signal data.  Newer XMM7260/XMM7450-class RILs are excluded and
 * retain their native behaviour.
 */
static const uint64_t NETWORK_RECOVERY_MAGIC = UINT64_C(0x5345434E45545243);
static const unsigned int NETWORK_RECOVERY_MAX_ATTEMPTS = 4;

typedef struct {
	uint64_t magic;
	unsigned int generation;
} NetworkRecoveryRequest;

typedef struct {
	unsigned int generation;
} NetworkRecoveryTimer;

static pthread_mutex_t networkRecoveryMutex = PTHREAD_MUTEX_INITIALIZER;
static int networkRecoveryVoiceState = -1;
static int networkRecoveryDataState = -1;
static bool networkRecoveryRadioOn = false;
static bool networkRecoverySimReady = false;
static bool networkRecoverySelectionKnown = false;
static bool networkRecoverySelectionAutomatic = false;
static bool networkRecoveryCallActive = false;
static bool networkRecoveryEverRegistered = false;
static bool networkRecoveryTimerPending = false;
static bool networkRecoveryRequestInFlight = false;
static unsigned int networkRecoveryAttempts = 0;
static unsigned int networkRecoveryGeneration = 1;

static void scheduleNetworkRecovery(void);
static void networkRecoveryTimerCallback(void *param);
#endif

/* Response data for RIL_REQUEST_GET_CELL_INFO_LIST */
static int lteTac = -1;
static int lteCi = -1;
static int wcdmaLac = -1;
static int wcdmaCid = -1;
static int gsmLac = -1;
static int gsmCid = -1;

static void onRequestDeviceIdentity(int request, RIL_Token t);
static void requestImeisvDeferred(void *param);

static RIL_Dial dial;

static void onRequestDial(int request, void *data, RIL_Token t) {
	RIL_UUS_Info uusInfo;

	dial.address = ((RIL_Dial *) data)->address;
	dial.clir = ((RIL_Dial *) data)->clir;
	dial.uusInfo = ((RIL_Dial *) data)->uusInfo;

	if (dial.uusInfo == NULL) {
		uusInfo.uusType = (RIL_UUS_Type) 0;
		uusInfo.uusDcs = (RIL_UUS_DCS) 0;
		uusInfo.uusData = NULL;
		uusInfo.uusLength = 0;
		dial.uusInfo = &uusInfo;
	}

	origRilFunctions->onRequest(request, &dial, sizeof(dial), t);
}


static int
decodeVoiceRadioTechnology (RIL_RadioState radioState) {
    switch (radioState) {
        case RADIO_STATE_SIM_NOT_READY:
        [[fallthrough]];
        case RADIO_STATE_SIM_LOCKED_OR_ABSENT:
        [[fallthrough]];
        case RADIO_STATE_SIM_READY:
            return RADIO_TECH_UMTS;

        case RADIO_STATE_RUIM_NOT_READY:
        [[fallthrough]];
        case RADIO_STATE_RUIM_READY:
        [[fallthrough]];
        case RADIO_STATE_RUIM_LOCKED_OR_ABSENT:
        [[fallthrough]];
        case RADIO_STATE_NV_NOT_READY:
        [[fallthrough]];
        case RADIO_STATE_NV_READY:
            return RADIO_TECH_1xRTT;

        default:
            RLOGD("decodeVoiceRadioTechnology: Invoked with incorrect RadioState");
            return -1;
    }
}

static void OnRequestGetCellInfoList(int request, void *data, size_t datalen, RIL_Token t) {
	RLOGI("%s: got request %s (data:%p datalen:%d)\n", __FUNCTION__,
		requestToString(request),
		data, datalen);

	RIL_CellInfo_v12 CellInfoLte = {};
	RIL_CellInfo_v12 cellInfoWCDMA = {};
	RIL_CellInfo_v12 cellInfoGSM = {};
	RIL_CellInfo_v12 cellInfoList[3] = {};

	CellInfoLte.cellInfoType = RIL_CELL_INFO_TYPE_LTE;
	CellInfoLte.registered = voiceRegistrationActive || dataRegistrationActive;
	CellInfoLte.CellInfo.lte.cellIdentityLte.mcc = -1;
	CellInfoLte.CellInfo.lte.cellIdentityLte.mnc = -1;
	CellInfoLte.CellInfo.lte.cellIdentityLte.ci = -1;
	CellInfoLte.CellInfo.lte.cellIdentityLte.pci = -1;
	CellInfoLte.CellInfo.lte.cellIdentityLte.tac = lteTac;
	CellInfoLte.CellInfo.lte.cellIdentityLte.ci = lteCi;
	CellInfoLte.CellInfo.lte.signalStrengthLte.signalStrength = 99;
	CellInfoLte.CellInfo.lte.signalStrengthLte.rsrp = INT_MAX;
	CellInfoLte.CellInfo.lte.signalStrengthLte.rsrq = INT_MAX;
	CellInfoLte.CellInfo.lte.signalStrengthLte.rssnr = INT_MAX;
	CellInfoLte.CellInfo.lte.signalStrengthLte.cqi = INT_MAX;
	CellInfoLte.CellInfo.lte.signalStrengthLte.timingAdvance = INT_MAX;

	cellInfoWCDMA.cellInfoType = RIL_CELL_INFO_TYPE_WCDMA;
	cellInfoWCDMA.registered = voiceRegistrationActive || dataRegistrationActive;
	cellInfoWCDMA.CellInfo.wcdma.cellIdentityWcdma.mcc = -1;
	cellInfoWCDMA.CellInfo.wcdma.cellIdentityWcdma.mnc = -1;
	cellInfoWCDMA.CellInfo.wcdma.cellIdentityWcdma.psc = -1;
	cellInfoWCDMA.CellInfo.wcdma.cellIdentityWcdma.lac = wcdmaLac;
	cellInfoWCDMA.CellInfo.wcdma.cellIdentityWcdma.cid = wcdmaCid;
	cellInfoWCDMA.CellInfo.wcdma.signalStrengthWcdma.signalStrength = 99;
	cellInfoWCDMA.CellInfo.wcdma.signalStrengthWcdma.bitErrorRate = 99;

	cellInfoGSM.cellInfoType = RIL_CELL_INFO_TYPE_GSM;
	cellInfoGSM.registered = voiceRegistrationActive || dataRegistrationActive;
	cellInfoGSM.CellInfo.gsm.cellIdentityGsm.mcc = -1;
	cellInfoGSM.CellInfo.gsm.cellIdentityGsm.mnc = -1;
	cellInfoGSM.CellInfo.gsm.cellIdentityGsm.lac = gsmLac;
	cellInfoGSM.CellInfo.gsm.cellIdentityGsm.cid = gsmCid;
	cellInfoGSM.CellInfo.gsm.signalStrengthGsm.signalStrength = 99;
	cellInfoGSM.CellInfo.gsm.signalStrengthGsm.bitErrorRate = 99;
	cellInfoGSM.CellInfo.gsm.signalStrengthGsm.timingAdvance = INT_MAX;

	if (cellInfoGSM.CellInfo.gsm.cellIdentityGsm.lac > -1 &&
	    cellInfoGSM.CellInfo.gsm.cellIdentityGsm.cid > -1) {
		cellInfoList[0] = cellInfoGSM;
		rilEnv->OnRequestComplete(t, RIL_E_SUCCESS, cellInfoList, sizeof(cellInfoList[0]));
	} else if (cellInfoWCDMA.CellInfo.wcdma.cellIdentityWcdma.lac > -1 &&
               	    cellInfoWCDMA.CellInfo.wcdma.cellIdentityWcdma.cid > -1)
        {
		rilEnv->OnRequestComplete(t, RIL_E_SUCCESS, &cellInfoWCDMA, sizeof(cellInfoWCDMA));
	    }
	else {
		if (CellInfoLte.CellInfo.lte.cellIdentityLte.tac > -1 &&
				CellInfoLte.CellInfo.lte.cellIdentityLte.ci > -1) {
			rilEnv->OnRequestComplete(t, RIL_E_SUCCESS, &CellInfoLte, sizeof(CellInfoLte));
		} else {
			rilEnv->OnRequestComplete(t, RIL_E_SUCCESS, NULL, 0);
		}
	    }
}

static void onRequestVoiceRadioTech(int request, void *data, size_t datalen, RIL_Token t) {
	RLOGI("%s: got request %s (data:%p datalen:%d)\n", __FUNCTION__,
		requestToString(request),
		data, datalen);
        RIL_RadioState radioState = origRilFunctions->onStateRequest();

	voiceRadioTechnology = decodeVoiceRadioTechnology(radioState);
	if (voiceRadioTechnology < 0) {
		rilEnv->OnRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
		return;
	}
	rilEnv->OnRequestComplete(t, RIL_E_SUCCESS, &voiceRadioTechnology, sizeof(voiceRadioTechnology));
}

static int
decodeCdmaSubscriptionSource (RIL_RadioState radioState) {
    switch (radioState) {
        case RADIO_STATE_SIM_NOT_READY:
        [[fallthrough]];
        case RADIO_STATE_SIM_LOCKED_OR_ABSENT:
        [[fallthrough]];
        case RADIO_STATE_SIM_READY:
        [[fallthrough]];
        case RADIO_STATE_RUIM_NOT_READY:
        [[fallthrough]];
        case RADIO_STATE_RUIM_READY:
        [[fallthrough]];
        case RADIO_STATE_RUIM_LOCKED_OR_ABSENT:
            return CDMA_SUBSCRIPTION_SOURCE_RUIM_SIM;

        case RADIO_STATE_NV_NOT_READY:
        [[fallthrough]];
        case RADIO_STATE_NV_READY:
            return CDMA_SUBSCRIPTION_SOURCE_NV;

        default:
            RLOGD("decodeCdmaSubscriptionSource: Invoked with incorrect RadioState");
            return -1;
    }
}

static void onRequestCdmaGetSubscriptionSource(int request, void *data, size_t datalen, RIL_Token t) {
	RLOGI("%s: got request %s (data:%p datalen:%d)\n", __FUNCTION__,
		requestToString(request),
		data, datalen);
        RIL_RadioState radioState = (RIL_RadioState)origRilFunctions->onStateRequest();

	cdmaSubscriptionSource = decodeCdmaSubscriptionSource(radioState);
	if (cdmaSubscriptionSource < 0) {
		rilEnv->OnRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
		return;
	}
	rilEnv->OnRequestComplete(t, RIL_E_SUCCESS, &cdmaSubscriptionSource, sizeof(cdmaSubscriptionSource));
}

static void onRequestDeviceIdentity(int request, RIL_Token t) {
	ALOGI("DEVICE_IDENTITY bridge: querying Samsung v7 IMEI/IMEISV");
	IdentityRequest *identity = (IdentityRequest *)calloc(1, sizeof(*identity));
	if (identity == NULL) {
		rilEnv->OnRequestComplete(t, RIL_E_NO_MEMORY, NULL, 0);
		return;
	}
	identity->magic = IDENTITY_REQUEST_MAGIC;
	identity->originalToken = t;
	identity->stage = 1;
	origRilFunctions->onRequest(RIL_REQUEST_GET_IMEI, NULL, 0, identity);
}

static void requestImeisvDeferred(void *param) {
	IdentityRequest *identity = (IdentityRequest *)param;
	if (identity == NULL || identity->magic != IDENTITY_REQUEST_MAGIC ||
			identity->stage != 2) {
		RLOGE("%s: stale or invalid DEVICE_IDENTITY state", __func__);
		return;
	}

	/*
	 * Samsung's request processor invokes OnRequestComplete while it still
	 * owns its request-queue lock. Calling the blob's onRequest() directly
	 * from that callback deadlocks the processor on XMM6262 (and potentially
	 * other generations). Run the cached IMEISV query from libril's timed
	 * callback thread, after the GET_IMEI handler has returned and released
	 * the vendor lock.
	 */
	RLOGI("%s: querying cached modem IMEISV outside vendor callback", __func__);
	origRilFunctions->onRequest(RIL_REQUEST_GET_IMEISV, NULL, 0, identity);
}

#ifndef SAMSUNG_NEXT_GEN_MODEM
static bool networkRecoveryConditionsLocked(void) {
	return networkRecoveryRadioOn && networkRecoverySimReady &&
			networkRecoverySelectionKnown && networkRecoverySelectionAutomatic &&
			!networkRecoveryCallActive && !networkRecoveryRequestInFlight &&
			networkRecoveryVoiceState == RIL_NOT_REG_AND_NOT_SEARCHING &&
			networkRecoveryDataState == RIL_NOT_REG_AND_NOT_SEARCHING &&
			networkRecoveryAttempts < NETWORK_RECOVERY_MAX_ATTEMPTS;
}

static void invalidateNetworkRecoveryLocked(bool resetAttempts) {
	++networkRecoveryGeneration;
	networkRecoveryTimerPending = false;
	networkRecoveryRequestInFlight = false;
	if (resetAttempts) networkRecoveryAttempts = 0;
}

static unsigned int networkRecoveryDelayLocked(void) {
	if (networkRecoveryAttempts == 0)
		return networkRecoveryEverRegistered ? 15 : 60;
	static const unsigned int retryDelays[] = { 45, 120, 300 };
	unsigned int index = networkRecoveryAttempts - 1;
	if (index >= sizeof(retryDelays) / sizeof(retryDelays[0]))
		index = sizeof(retryDelays) / sizeof(retryDelays[0]) - 1;
	return retryDelays[index];
}

static void scheduleNetworkRecovery(void) {
	unsigned int generation;
	unsigned int delaySeconds;
	unsigned int nextAttempt;

	pthread_mutex_lock(&networkRecoveryMutex);
	if (!networkRecoveryConditionsLocked() || networkRecoveryTimerPending) {
		pthread_mutex_unlock(&networkRecoveryMutex);
		return;
	}
	generation = networkRecoveryGeneration;
	delaySeconds = networkRecoveryDelayLocked();
	nextAttempt = networkRecoveryAttempts + 1;
	networkRecoveryTimerPending = true;
	pthread_mutex_unlock(&networkRecoveryMutex);

	NetworkRecoveryTimer *timer =
			(NetworkRecoveryTimer *)calloc(1, sizeof(*timer));
	if (timer == NULL) {
		pthread_mutex_lock(&networkRecoveryMutex);
		if (generation == networkRecoveryGeneration)
			networkRecoveryTimerPending = false;
		pthread_mutex_unlock(&networkRecoveryMutex);
		RLOGE("network recovery: unable to allocate timer state");
		return;
	}
	timer->generation = generation;
	struct timeval delay = { static_cast<time_t>(delaySeconds), 0 };
	RLOGI("network recovery: scheduling automatic reselection in %u seconds "
			"(attempt %u/%u)", delaySeconds, nextAttempt,
			NETWORK_RECOVERY_MAX_ATTEMPTS);
	rilEnv->RequestTimedCallback(networkRecoveryTimerCallback, timer, &delay);
}

static void networkRecoveryTimerCallback(void *param) {
	NetworkRecoveryTimer *timer = (NetworkRecoveryTimer *)param;
	if (timer == NULL) return;
	const unsigned int generation = timer->generation;
	free(timer);

	NetworkRecoveryRequest *request =
			(NetworkRecoveryRequest *)calloc(1, sizeof(*request));
	if (request == NULL) {
		pthread_mutex_lock(&networkRecoveryMutex);
		if (generation == networkRecoveryGeneration)
			networkRecoveryTimerPending = false;
		pthread_mutex_unlock(&networkRecoveryMutex);
		RLOGE("network recovery: unable to allocate request state");
		return;
	}

	bool issueRequest = false;
	unsigned int attempt = 0;
	pthread_mutex_lock(&networkRecoveryMutex);
	if (generation == networkRecoveryGeneration) {
		networkRecoveryTimerPending = false;
		if (networkRecoveryConditionsLocked()) {
			networkRecoveryRequestInFlight = true;
			attempt = ++networkRecoveryAttempts;
			request->magic = NETWORK_RECOVERY_MAGIC;
			request->generation = generation;
			issueRequest = true;
		}
	}
	pthread_mutex_unlock(&networkRecoveryMutex);

	if (!issueRequest) {
		free(request);
		return;
	}

	/* Run outside the vendor completion callback to avoid its request lock. */
	RLOGW("network recovery: requesting real automatic network reselection "
			"from libsec-ril.so (attempt %u/%u)", attempt,
			NETWORK_RECOVERY_MAX_ATTEMPTS);
	origRilFunctions->onRequest(RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC,
			NULL, 0, request);
}

static bool onRequestCompleteNetworkRecovery(RIL_Token t, RIL_Errno e) {
	NetworkRecoveryRequest *request = (NetworkRecoveryRequest *)t;
	if (request == NULL || request->magic != NETWORK_RECOVERY_MAGIC) return false;
	const unsigned int generation = request->generation;
	request->magic = 0;
	free(request);

	bool current;
	pthread_mutex_lock(&networkRecoveryMutex);
	current = generation == networkRecoveryGeneration;
	if (current) networkRecoveryRequestInFlight = false;
	pthread_mutex_unlock(&networkRecoveryMutex);

	RLOGI("network recovery: automatic reselection completed with error %d%s",
			e, current ? "" : " (stale generation)");
	if (current) scheduleNetworkRecovery();
	return true;
}

static void updateNetworkRecoveryRegistration(bool voice, int state,
		bool validResponse) {
	bool registered = validResponse &&
			(state == RIL_REG_HOME || state == RIL_REG_ROAMING);
	pthread_mutex_lock(&networkRecoveryMutex);
	if (voice)
		networkRecoveryVoiceState = validResponse ? state : -1;
	else
		networkRecoveryDataState = validResponse ? state : -1;
	if (registered) {
		networkRecoveryEverRegistered = true;
		invalidateNetworkRecoveryLocked(true);
	}
	pthread_mutex_unlock(&networkRecoveryMutex);
	if (!registered) scheduleNetworkRecovery();
}

static void updateNetworkRecoveryRadioState(RIL_RadioState state) {
	const bool radioOn = state > RADIO_STATE_OFF;
	const bool simReady = state == RADIO_STATE_SIM_READY ||
			(state == RADIO_STATE_ON && simRuimStatus == RADIO_STATE_SIM_READY);
	pthread_mutex_lock(&networkRecoveryMutex);
	if (!radioOn || !simReady) {
		invalidateNetworkRecoveryLocked(true);
		networkRecoveryVoiceState = -1;
		networkRecoveryDataState = -1;
	}
	networkRecoveryRadioOn = radioOn;
	networkRecoverySimReady = simReady;
	pthread_mutex_unlock(&networkRecoveryMutex);
	if (radioOn && simReady) scheduleNetworkRecovery();
}

static void updateNetworkRecoverySelectionMode(bool known, bool automatic,
		bool resetAttempts) {
	pthread_mutex_lock(&networkRecoveryMutex);
	networkRecoverySelectionKnown = known;
	networkRecoverySelectionAutomatic = known && automatic;
	if (!known || !automatic)
		invalidateNetworkRecoveryLocked(resetAttempts);
	else if (resetAttempts)
		invalidateNetworkRecoveryLocked(true);
	pthread_mutex_unlock(&networkRecoveryMutex);
	if (known && automatic) scheduleNetworkRecovery();
}

static void updateNetworkRecoveryCallState(bool active) {
	pthread_mutex_lock(&networkRecoveryMutex);
	networkRecoveryCallActive = active;
	if (active) invalidateNetworkRecoveryLocked(false);
	pthread_mutex_unlock(&networkRecoveryMutex);
	if (!active) scheduleNetworkRecovery();
}
#endif

static bool onRequestEnterSimPin(int request, void *data, size_t datalen, RIL_Token t) {
	int length = (int)datalen/ sizeof(char *);
	if (length == 2) {
		char **field = (char **) data;
		char *pin = field[0];
		if (pin == NULL) {
			RLOGE("%s: got empty PIN for %s", __FUNCTION__, requestToString(request));
			rilEnv->OnRequestComplete(t, RIL_E_INVALID_ARGUMENTS, NULL, 0);
			return true;
		}
	}
	return false;
}

static void onRequestUnsupportedRequest(int request, RIL_Token t) {
	RLOGE("%s:\t\t<<< REQUEST-COMPLETE: %s: (token:%p): Request not send to RIL. Sending REQUEST_NOT_SUPPORTED back to libril.\n",
		__FUNCTION__,
		requestToString(request),
		t);
	rilEnv->OnRequestComplete(t, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
}


static bool is3gpp2(int radioTech) {
    switch (radioTech) {
        case RADIO_TECH_IS95A:
        [[fallthrough]];
        case RADIO_TECH_IS95B:
        [[fallthrough]];
        case RADIO_TECH_1xRTT:
        [[fallthrough]];
        case RADIO_TECH_EVDO_0:
        [[fallthrough]];
        case RADIO_TECH_EVDO_A:
        [[fallthrough]];
        case RADIO_TECH_EVDO_B:
        [[fallthrough]];
        case RADIO_TECH_EHRPD:
            return true;
        default:
            return false;
    }
}

static int
decodeSimStatus (RIL_RadioState radioState) {
   switch (radioState) {
       case RADIO_STATE_SIM_NOT_READY:
           RLOGE("%s: radioState=RADIO_STATE_SIM_NOT_READY", __func__);
           return -1;
       case RADIO_STATE_RUIM_NOT_READY:
           RLOGE("%s: radioState=RADIO_STATE_RUIM_NOT_READY", __func__);
           return -1;
       case RADIO_STATE_NV_NOT_READY:
           RLOGE("%s: radioState=RADIO_STATE_NV_NOT_READY", __func__);
           return -1;
       case RADIO_STATE_NV_READY:
           RLOGE("%s: radioState=RADIO_STATE_NV_READY", __func__);
           return -1;
       case RADIO_STATE_SIM_LOCKED_OR_ABSENT:
           RLOGE("%s: radioState=RADIO_STATE_SIM_LOCKED_OR_ABSENT", __func__);
           return radioState;
       case RADIO_STATE_SIM_READY:
           RLOGE("%s: radioState=RADIO_STATE_SIM_READY", __func__);
           return radioState;
       case RADIO_STATE_RUIM_READY:
           RLOGE("%s: radioState=RADIO_STATE_RUIM_READY", __func__);
           return radioState;
       case RADIO_STATE_RUIM_LOCKED_OR_ABSENT:
           RLOGE("%s: radioState=RADIO_STATE_RUIM_LOCKED_OR_ABSENT", __func__);
           return radioState;
       default:
           RLOGD("decodeSimStatus: Invoked with incorrect RadioState");
           return -1;
   }
}

static RIL_RadioState
processRadioState(RIL_RadioState newRadioState) {
#ifndef SAMSUNG_NEXT_GEN_MODEM
    const RIL_RadioState vendorRadioState = newRadioState;
#endif
    if((newRadioState > RADIO_STATE_UNAVAILABLE) && (newRadioState < RADIO_STATE_ON)) {
        int newVoiceRadioTech;
        int newCdmaSubscriptionSource;
        int newSimStatus;

        /* This is old RIL. Decode Subscription source and Voice Radio Technology
           from Radio State and send change notifications if there has been a change */
        newVoiceRadioTech = decodeVoiceRadioTechnology(newRadioState);
        if(newVoiceRadioTech != voiceRadioTechnology) {
            voiceRadioTechnology = newVoiceRadioTech;
            rilEnv->OnUnsolicitedResponse(RIL_UNSOL_VOICE_RADIO_TECH_CHANGED,
                &voiceRadioTechnology, sizeof(voiceRadioTechnology));
        }
        if(is3gpp2(newVoiceRadioTech)) {
            newCdmaSubscriptionSource = decodeCdmaSubscriptionSource(newRadioState);
            if(newCdmaSubscriptionSource != cdmaSubscriptionSource) {
                cdmaSubscriptionSource = newCdmaSubscriptionSource;
                rilEnv->OnUnsolicitedResponse(RIL_UNSOL_CDMA_SUBSCRIPTION_SOURCE_CHANGED,
                        &cdmaSubscriptionSource, sizeof(cdmaSubscriptionSource));
            }
        }
        newSimStatus = decodeSimStatus(newRadioState);
        RLOGE("%s: newSimStatus=%d", __func__, newSimStatus);
        if(newSimStatus != simRuimStatus) {
            simRuimStatus = newSimStatus;
            rilEnv->OnUnsolicitedResponse(RIL_UNSOL_RESPONSE_SIM_STATUS_CHANGED, NULL, 0);
        }

        /* Send RADIO_ON to telephony */
        newRadioState = RADIO_STATE_ON;
    }

#ifndef SAMSUNG_NEXT_GEN_MODEM
    updateNetworkRecoveryRadioState(vendorRadioState);
#endif
    return newRadioState;
}

static bool onRequestGetRadioCapability(RIL_Token t)
{
	RIL_RadioCapability rc[1] =
	{
		{ /* rc[0] */
			RIL_RADIO_CAPABILITY_VERSION, /* version */
			0, /* session */
			RC_PHASE_CONFIGURED, /* phase */
#ifndef SAMSUNG_NEXT_GEN_MODEM
			RAF_GSM | RAF_GPRS | RAF_EDGE | RAF_HSUPA | RAF_HSDPA | RAF_HSPA | RAF_HSPAP | RAF_UMTS, /* rat */
#else
			RAF_GSM | RAF_GPRS | RAF_EDGE | RAF_HSUPA | RAF_HSDPA | RAF_HSPA | RAF_HSPAP | RAF_UMTS | RAF_LTE, /* rat */
#endif
			{ /* logicalModemUuid */
				0,
			},
			RC_STATUS_SUCCESS /* status */
		}
	};
	rilEnv->OnRequestComplete(t, RIL_E_SUCCESS, rc, sizeof(rc));
	return true;
}

static RIL_RadioState onStateRequestShim() {
	if (!vendorRilReady.load(std::memory_order_acquire))
		return RADIO_STATE_UNAVAILABLE;

    RIL_RadioState radioState = RADIO_STATE_OFF;
    RIL_RadioState newRadioState = RADIO_STATE_OFF;

    radioState = origRilFunctions->onStateRequest();
    newRadioState = processRadioState(radioState);

    RLOGI("%s: RIL legacy radiostate converted from %d to %d\n", __FUNCTION__, radioState, newRadioState);
    return newRadioState;
}

static void onRequestShim(int request, void *data, size_t datalen, RIL_Token t)
{
	if (!vendorRilReady.load(std::memory_order_acquire)) {
		rilEnv->OnRequestComplete(t, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
		return;
	}

	RLOGD("%s:\t\t\t\t\t>>> REQUEST\t\t\t: %s: data:%p datalen:%d token:%p\n", __FUNCTION__, requestToString(request), data, datalen, t);

#ifndef SAMSUNG_NEXT_GEN_MODEM
	/* Track only facts supplied by Android/the modem; never synthesize service. */
	switch (request) {
		case RIL_REQUEST_RADIO_POWER:
			if (data != NULL && datalen >= sizeof(int)) {
				const bool on = *(const int *)data != 0;
				updateNetworkRecoveryRadioState(on
						? (simRuimStatus == RADIO_STATE_SIM_READY
								? RADIO_STATE_SIM_READY : RADIO_STATE_SIM_NOT_READY)
						: RADIO_STATE_OFF);
			}
			break;
		case RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC:
			updateNetworkRecoverySelectionMode(true, true, true);
			break;
		case RIL_REQUEST_SET_NETWORK_SELECTION_MANUAL:
			updateNetworkRecoverySelectionMode(true, false, true);
			break;
		default:
			break;
	}
#endif

	switch (request) {
                /* Our RIL doesn't support this, so we implement this ourself */
                case RIL_REQUEST_GET_CELL_INFO_LIST:
			OnRequestGetCellInfoList(request, data, datalen, t);
			return;
                /* Our RIL doesn't support this, so we implement this ourself */
                case RIL_REQUEST_VOICE_RADIO_TECH:
			onRequestVoiceRadioTech(request, data, datalen, t);
			return;
                /* Our RIL doesn't support this, so we implement this ourself */
                case RIL_REQUEST_CDMA_GET_SUBSCRIPTION_SOURCE:
			onRequestCdmaGetSubscriptionSource(request, data, datalen, t);
			return;
		/* RIL_REQUEST_GET_IMEI and RIL_REQUEST_GET_IMEISV is deprecated */
		case RIL_REQUEST_DEVICE_IDENTITY:
			onRequestDeviceIdentity(request, t);
			return;
		/* The Samsung RIL crashes if uusInfo is NULL... */
		case RIL_REQUEST_DIAL:
			if (datalen == sizeof(RIL_Dial) && data != NULL) {
				onRequestDial(request, data, t);
				return;
			}
			break;

		/* Necessary; RILJ may fake this for us if we reply not supported, but we can just implement it. */
		case RIL_REQUEST_GET_RADIO_CAPABILITY:
			onRequestGetRadioCapability(t);
			return;
		/* The Samsung RIL doesn't support RIL_REQUEST_SEND_SMS_EXPECT_MORE, reply with RIL_REQUEST_SEND_SMS instead */
		case RIL_REQUEST_SEND_SMS_EXPECT_MORE:
			origRilFunctions->onRequest(RIL_REQUEST_SEND_SMS, data, datalen, t);
			return;
		case RIL_REQUEST_ENTER_SIM_PIN:
			if (onRequestEnterSimPin(request, data, datalen, t))
				return;
			origRilFunctions->onRequest(request, data, datalen, t);
			return;
		case RIL_REQUEST_SET_SIM_CARD_POWER:
			/*
			 * Do not translate the standard card-power/UICC APIs to Samsung's
			 * vendor SET_SIM_POWER. On XMM6262 that command controls the legacy
			 * CP/SIM power path, is fire-and-forget, and can leave the modem in
			 * BOOTING (0x3) across a warm reboot. A prompt unsupported response
			 * is both API-correct and safer than claiming success or powering
			 * down the whole modem.
			 */
			onRequestUnsupportedRequest(request, t);
			return;
		/* The following requests were introduced post-4.3. */
		case RIL_REQUEST_IMS_REGISTRATION_STATE:
			/*
			 * This deprecated query is not implemented by the circuit-switched
			 * XMM RIL and otherwise remains pending forever. Android has no IMS
			 * service on these devices, so return the API-defined unsupported
			 * error instead of inventing an IMS registration result.
			 */
			onRequestUnsupportedRequest(request, t);
			return;
#ifndef SAMSUNG_NEXT_GEN_MODEM
		case RIL_REQUEST_SET_INITIAL_ATTACH_APN:
			/*
			 * The v7 XMM6260/6262/6360 blob predates this request. It
			 * silently queues request 111 without ever completing it, which
			 * leaves Android's RIL wakelock/request list occupied for a full
			 * timeout. Legacy packet data receives its APN in SETUP_DATA_CALL,
			 * so rejecting this optional pre-programming request is correct
			 * and does not remove the data-call APN.
			 *
			 * Do not apply this to XMM7260/XMM7450-class blobs: those are
			 * selected with SAMSUNG_NEXT_GEN_MODEM and may implement request
			 * 111 themselves.
			 */
			onRequestUnsupportedRequest(request, t);
			return;
		case RIL_REQUEST_GET_ACTIVITY_INFO:
			/*
			 * This request was added long after the v7 XMM626x blob. Passing it
			 * through blocks the single vendor request queue for roughly two
			 * seconds, after which the old shim returned an all-zero success
			 * payload that Android rejects as invalid anyway. Fail immediately
			 * so service-state, SIM and call requests are not delayed. Newer
			 * Samsung modems retain their native implementation.
			 */
			onRequestUnsupportedRequest(request, t);
			return;
#endif
		case RIL_REQUEST_SIM_TRANSMIT_APDU_BASIC:
		[[fallthrough]];
		case RIL_REQUEST_SIM_OPEN_CHANNEL: /* !!! */
		[[fallthrough]];
		case RIL_REQUEST_SIM_CLOSE_CHANNEL:
		[[fallthrough]];
		case RIL_REQUEST_SIM_TRANSMIT_APDU_CHANNEL:
		[[fallthrough]];
		case RIL_REQUEST_NV_READ_ITEM:
		[[fallthrough]];
		case RIL_REQUEST_NV_WRITE_ITEM:
		[[fallthrough]];
		case RIL_REQUEST_NV_WRITE_CDMA_PRL:
		[[fallthrough]];
		case RIL_REQUEST_NV_RESET_CONFIG:
		[[fallthrough]];
		case RIL_REQUEST_SET_UICC_SUBSCRIPTION:
		[[fallthrough]];
		case RIL_REQUEST_ALLOW_DATA:
		[[fallthrough]];
		case RIL_REQUEST_GET_HARDWARE_CONFIG:
		[[fallthrough]];
		case RIL_REQUEST_SIM_AUTHENTICATION:
		[[fallthrough]];
		case RIL_REQUEST_GET_DC_RT_INFO:
		[[fallthrough]];
		case RIL_REQUEST_SET_DC_RT_INFO_RATE:
		[[fallthrough]];
		case RIL_REQUEST_SET_DATA_PROFILE:
		[[fallthrough]];
		case RIL_REQUEST_SHUTDOWN: /* TODO: Is there something we can do for RIL_REQUEST_SHUTDOWN ? */
		[[fallthrough]];
		case RIL_REQUEST_SET_RADIO_CAPABILITY:
		[[fallthrough]];
		case RIL_REQUEST_START_LCE:
		[[fallthrough]];
		case RIL_REQUEST_STOP_LCE:
		[[fallthrough]];
		case RIL_REQUEST_PULL_LCEDATA:
			onRequestUnsupportedRequest(request, t);
			return;
	}

	origRilFunctions->onRequest(request, data, datalen, t);
}

static void onCompleteRequestGetSimStatus(RIL_Token t, RIL_Errno e, void *response) {
	/* While at it, upgrade the response to RIL_CardStatus_v6 */
	const RIL_CardStatus_v5_samsung *p_cur =
			static_cast<const RIL_CardStatus_v5_samsung *>(response);
	RIL_CardStatus_v6 v6response = {};

	if (p_cur->num_applications < 0 ||
			p_cur->num_applications > RIL_CARD_MAX_APPS ||
			p_cur->gsm_umts_subscription_app_index < -1 ||
			p_cur->gsm_umts_subscription_app_index >= p_cur->num_applications ||
			p_cur->cdma_subscription_app_index < -1 ||
			p_cur->cdma_subscription_app_index >= p_cur->num_applications) {
		RLOGE("GET_SIM_STATUS bridge: invalid Samsung metadata "
				"(card=%d gsm=%d cdma=%d apps=%d)",
				p_cur->card_state, p_cur->gsm_umts_subscription_app_index,
				p_cur->cdma_subscription_app_index, p_cur->num_applications);
		rilEnv->OnRequestComplete(t,
				e == RIL_E_SUCCESS ? RIL_E_INVALID_RESPONSE : e, NULL, 0);
		return;
	}

	v6response.card_state = p_cur->card_state;
	v6response.universal_pin_state = p_cur->universal_pin_state;
	v6response.gsm_umts_subscription_app_index = p_cur->gsm_umts_subscription_app_index;
	v6response.cdma_subscription_app_index = p_cur->cdma_subscription_app_index;
	v6response.ims_subscription_app_index = -1;
	v6response.num_applications = p_cur->num_applications;

	for (int i = 0; i < p_cur->num_applications; ++i)
		memcpy(&v6response.applications[i], &p_cur->applications[i], sizeof(RIL_AppStatus));

	RLOGI("GET_SIM_STATUS bridge: card=%d upin=%d gsm=%d cdma=%d apps=%d",
			v6response.card_state, v6response.universal_pin_state,
			v6response.gsm_umts_subscription_app_index,
			v6response.cdma_subscription_app_index,
			v6response.num_applications);

	/* Send the fixed response to libril */
	rilEnv->OnRequestComplete(t, e, &v6response, sizeof(RIL_CardStatus_v6));
}

static void onRequestCompleteVoiceRegistrationState(RIL_Token t, RIL_Errno e, void *response, size_t responselen) {
	char **resp = (char **) response;
	static char radioTech[12];
	const size_t count = std::min(responselen / sizeof(char *), VOICE_REGSTATE_COUNT);
	memset(voiceRegStateResponse, 0, VOICE_REGSTATE_SIZE);
	for (size_t index = 0; index < count; index++) {
		voiceRegStateResponse[index] = resp[index];
		switch (index) {
			case 1: {
				wcdmaLac = parseHexCellId(voiceRegStateResponse[index]);
				break;
			}
			case 2: {
				wcdmaCid = parseHexCellId(voiceRegStateResponse[index]);
				break;
			}
			default:
				break;
		}
	}
	const int voiceState = e == RIL_E_SUCCESS && count > 0 &&
			voiceRegStateResponse[0] != NULL
			? atoi(voiceRegStateResponse[0]) : RIL_NOT_REG_AND_NOT_SEARCHING;
	voiceRegistrationActive =
			voiceState == RIL_REG_HOME || voiceState == RIL_REG_ROAMING;
	/*
	 * The XMM6260/6262/6360 libsec-ril response contains only state/LAC/CID
	 * (three pointers). Filling RAT inside the copy loop meant index 3 was
	 * never reached. Do not apply this fallback to LTE-generation blobs: keep
	 * their modem-supplied RAT so XMM7260/XMM7450 and peers remain accurate.
	 */
#ifndef SAMSUNG_NEXT_GEN_MODEM
	if (e == RIL_E_SUCCESS && count == 3) {
		int rat = RADIO_TECH_UNKNOWN;
		if (voiceRegistrationActive) {
			// The circuit- and packet-switched domains share the same access
			// technology on these single-stack XMM modems.
			rat = registeredDataRadioTechnology;
			if (rat == RADIO_TECH_UNKNOWN && voiceRadioTechnology > RADIO_TECH_UNKNOWN)
				rat = voiceRadioTechnology;
		}
		snprintf(radioTech, sizeof(radioTech), "%d", rat);
		voiceRegStateResponse[3] = radioTech;
	}
#endif
	RLOGD("VOICE_REG bridge: state=%s rat=%s fields=%zu",
			voiceRegStateResponse[0] != NULL ? voiceRegStateResponse[0] : "<null>",
			voiceRegStateResponse[3] != NULL ? voiceRegStateResponse[3] : "<null>",
			count);
	rilEnv->OnRequestComplete(t, e, voiceRegStateResponse, VOICE_REGSTATE_SIZE);
}

static void onRequestCompleteDataRegistrationState(RIL_Token t, RIL_Errno e, void *response, size_t responselen) {
	char **resp = (char **) response;
	int length = response != NULL ? (int)responselen / sizeof(char *) : 0;
	if (e == RIL_E_SUCCESS && length >= 4 && resp[0] != NULL && resp[3] != NULL) {
		const int state = atoi(resp[0]);
		const int rat = atoi(resp[3]);
		dataRegistrationActive = state == RIL_REG_HOME || state == RIL_REG_ROAMING;
		registeredDataRadioTechnology =
				dataRegistrationActive &&
				rat >= RADIO_TECH_GPRS && rat <= RADIO_TECH_LTE_CA
				? rat : RADIO_TECH_UNKNOWN;
	} else {
		dataRegistrationActive = false;
		registeredDataRadioTechnology = RADIO_TECH_UNKNOWN;
	}
#ifndef SAMSUNG_NEXT_GEN_MODEM
	updateNetworkRecoveryRegistration(false,
			length > 0 && resp[0] != NULL ? atoi(resp[0])
					: RIL_NOT_REG_AND_NOT_SEARCHING,
			e == RIL_E_SUCCESS && length > 0 && resp[0] != NULL);
#endif
	RLOGD("DATA_REG bridge: state=%s rat=%s fields=%d",
			length > 0 && resp[0] != NULL ? resp[0] : "<null>",
			length > 3 && resp[3] != NULL ? resp[3] : "<null>", length);

	if (length >= 4 && resp[1] != NULL && resp[2] != NULL && resp[3] != NULL) {
		gsmLac = gsmCid = -1;
		wcdmaLac = wcdmaCid = -1;
		lteTac = lteCi = -1;
		/* Gather Cellidentity data for RIL_REQUEST_GET_CELL_INFO_LIST */
		switch (atoi(resp[3])) {
			case RADIO_TECH_GPRS:
			case RADIO_TECH_EDGE:
			case RADIO_TECH_GSM: {
				RLOGI("%s: RIL_CELL_INFO_TYPE_GSM: lac:%s cid:%s \n",
					__FUNCTION__,
					resp[1],
					resp[2]);
				gsmLac = parseHexCellId(resp[1]);
				gsmCid = parseHexCellId(resp[2]);
				break;
			}
			case RADIO_TECH_UMTS:
			case RADIO_TECH_HSDPA:
			case RADIO_TECH_HSUPA:
			case RADIO_TECH_HSPA:
			case RADIO_TECH_HSPAP: {
				RLOGI("%s: RIL_CELL_INFO_TYPE_WCDMA: lac:%s cid:%s \n",
					__FUNCTION__,
					resp[1],
					resp[2]);
				wcdmaLac = parseHexCellId(resp[1]);
				wcdmaCid = parseHexCellId(resp[2]);
				break;
			}
			case RADIO_TECH_LTE:
			case RADIO_TECH_LTE_CA: {
				RLOGI("%s: RIL_CELL_INFO_TYPE_LTE: tac:%s ci:%s \n",
					__FUNCTION__,
					resp[1],
					resp[2]);
				lteTac = parseHexCellId(resp[1]);
				lteCi = parseHexCellId(resp[2]);

				break;
			}
			default:
				break;
		}
	}
	rilEnv->OnRequestComplete(t, e, response, responselen);
}

static bool onRequestCompleteDeviceIdentity(RIL_Token t, RIL_Errno e,
		void *response, size_t responselen) {
	IdentityRequest *identity = (IdentityRequest *)t;
	if (identity == NULL || identity->magic != IDENTITY_REQUEST_MAGIC) return false;
	auto failIdentityRequest = [identity](RIL_Errno error) {
		RIL_Token originalToken = identity->originalToken;
		identity->magic = 0;
		free(identity);
		rilEnv->OnRequestComplete(originalToken, error, NULL, 0);
	};
	auto isDecimal = [](const char *value, size_t length) {
		for (size_t i = 0; i < length; ++i) {
			if (value[i] < '0' || value[i] > '9') return false;
		}
		return true;
	};
	auto completeIdentityRequest = [identity](bool hasImeisv) {
		char empty[] = "";
		char *deviceIdentityResponse[] = {
			identity->imei, hasImeisv ? identity->imeisv : empty, empty, empty
		};
		RIL_Token originalToken = identity->originalToken;
		RLOGI("%s: completing DEVICE_IDENTITY with modem IMEI (%zu digits), "
				"IMEISV %s", __func__, strlen(identity->imei),
				hasImeisv ? "present" : "unavailable");
		rilEnv->OnRequestComplete(originalToken, RIL_E_SUCCESS,
				deviceIdentityResponse, sizeof(deviceIdentityResponse));
		identity->magic = 0;
		free(identity);
	};

	if (e != RIL_E_SUCCESS || response == NULL || responselen == 0) {
		/*
		 * IMEISV is optional device-identity metadata.  Do not discard a real,
		 * already validated IMEI when an older Samsung blob does not implement
		 * the separate GET_IMEISV request (seen across XMM modem generations).
		 */
		if (identity->stage == 2 && identity->imei[0] != '\0') {
			RLOGW("%s: modem IMEISV query failed (%d); preserving valid IMEI",
					__func__, e);
			completeIdentityRequest(false);
			return true;
		}
		failIdentityRequest(e == RIL_E_SUCCESS ? RIL_E_INVALID_RESPONSE : e);
		return true;
	}

	const char *value = (const char *)response;
	size_t length = strnlen(value, responselen);
	if (identity->stage == 1) {
		/*
		 * The n8000 XMM6262 modem sends a 17-digit ME serial to libsec-ril,
		 * but that blob exposes it through the v7 ABI as a 15-digit GET_IMEI
		 * response and a cached 2-digit GET_IMEISV response.  Other Samsung
		 * blobs may expose the combined value. Accept both real formats, but
		 * never manufacture missing identity digits.
		 */
		if (length == 17 && isDecimal(value, length)) {
			memcpy(identity->imei, value, 15);
			identity->imei[15] = '\0';
			memcpy(identity->imeisv, value + 15, 2);
			identity->imeisv[2] = '\0';
			RLOGI("%s: accepted combined modem IMEI/IMEISV response", __func__);
			completeIdentityRequest(true);
			return true;
		}
		if (length != 15 || !isDecimal(value, length)) {
			RLOGE("%s: invalid IMEI response length %zu", __func__, length);
			failIdentityRequest(RIL_E_INVALID_RESPONSE);
			return true;
		}
		memcpy(identity->imei, value, length);
		identity->imei[length] = '\0';
		RLOGI("%s: accepted 15-digit modem IMEI; deferring cached IMEISV query", __func__);
		identity->stage = 2;
		static const struct timeval imeisvDelay = { 0, 10000 };
		rilEnv->RequestTimedCallback(requestImeisvDeferred, identity, &imeisvDelay);
		return true;
	}

	if (identity->stage != 2) {
		failIdentityRequest(RIL_E_INVALID_RESPONSE);
		return true;
	}

	if (length == 2 && isDecimal(value, length)) {
		memcpy(identity->imeisv, value, 2);
	} else if (length == 17 && isDecimal(value, length) &&
			memcmp(identity->imei, value, 15) == 0) {
		/* Some modem generations return the combined serial for both queries. */
		memcpy(identity->imeisv, value + 15, 2);
	} else {
		RLOGW("%s: invalid modem IMEISV response length %zu; preserving valid IMEI",
				__func__, length);
		completeIdentityRequest(false);
		return true;
	}
	identity->imeisv[2] = '\0';
	completeIdentityRequest(true);
	return true;
}

static void fixupDataCallList(void *response, size_t responselen) {
	RIL_Data_Call_Response_v6 *p_cur = (RIL_Data_Call_Response_v6 *) response;
	int num = responselen / sizeof(RIL_Data_Call_Response_v6);

	int i;
	for (i = 0; i < num; ++i)
		p_cur[i].gateways = p_cur[i].addresses;
}

#ifndef SAMSUNG_NEXT_GEN_MODEM
static void fixupSignalStrength(void *response, size_t responselen) {
	int gsmSignalStrength;

	RIL_SignalStrength_v10 *p_cur = ((RIL_SignalStrength_v10 *) response);

	gsmSignalStrength = p_cur->GW_SignalStrength.signalStrength & 0xFF;

	if (gsmSignalStrength < 0 ||
		(gsmSignalStrength > 31 && p_cur->GW_SignalStrength.signalStrength != 99)) {
		gsmSignalStrength = p_cur->CDMA_SignalStrength.dbm;
	}

	/* Fix GSM signal strength */
	p_cur->GW_SignalStrength.signalStrength = gsmSignalStrength;

	/* We don't support LTE - values should be set to INT_MAX */
	if (responselen >= sizeof(RIL_SignalStrength_v10)) {
		p_cur->LTE_SignalStrength.signalStrength = 99;
		p_cur->LTE_SignalStrength.cqi = INT_MAX;
		p_cur->LTE_SignalStrength.rsrp = INT_MAX;
		p_cur->LTE_SignalStrength.rsrq = INT_MAX;
		p_cur->LTE_SignalStrength.rssnr = INT_MAX;
		p_cur->LTE_SignalStrength.timingAdvance = INT_MAX;
	}

	/* The XMM626x blob leaves unrelated families filled with union garbage.
	 * Android 16 considers one stray value enough to make LTE/CDMA the primary
	 * signal and hides the valid GSM bars. Scrub only families unsupported by
	 * this legacy modem; SAMSUNG_NEXT_GEN_MODEM keeps its real LTE values. */
	p_cur->CDMA_SignalStrength.dbm = INT_MAX;
	p_cur->CDMA_SignalStrength.ecio = INT_MAX;
	p_cur->EVDO_SignalStrength.dbm = INT_MAX;
	p_cur->EVDO_SignalStrength.ecio = INT_MAX;
	p_cur->EVDO_SignalStrength.signalNoiseRatio = INT_MAX;
	if (responselen >= sizeof(RIL_SignalStrength_v10))
		p_cur->TD_SCDMA_SignalStrength.rscp = INT_MAX;
}
#else
static void fixupSignalStrength(void *response, size_t responselen) {
    int gsmSignalStrength;
	int lteSignalStrength;

    RIL_SignalStrength_v10 *p_cur = ((RIL_SignalStrength_v10 *) response);

    gsmSignalStrength = p_cur->GW_SignalStrength.signalStrength & 0xFF;
    lteSignalStrength = responselen >= sizeof(RIL_SignalStrength_v10)
			? p_cur->LTE_SignalStrength.signalStrength & 0xFF : 99;

	if (gsmSignalStrength < 0 ||
		(gsmSignalStrength > 31 && p_cur->GW_SignalStrength.signalStrength != 99)) {
		gsmSignalStrength = p_cur->CDMA_SignalStrength.dbm;
	}

    if (responselen >= sizeof(RIL_SignalStrength_v10) && (lteSignalStrength < 0 ||
		(lteSignalStrength > 31 && p_cur->LTE_SignalStrength.signalStrength != 99))) {
    	lteSignalStrength = p_cur->LTE_SignalStrength.rsrp;
    }

	/* Fix GSM signal strength */
	p_cur->GW_SignalStrength.signalStrength = gsmSignalStrength;

    /* Fix LTE signal strength */
    if (responselen >= sizeof(RIL_SignalStrength_v10))
		p_cur->LTE_SignalStrength.signalStrength = lteSignalStrength;
}
#endif


static void onRequestCompleteShim(RIL_Token t, RIL_Errno e, void *response, size_t responselen) {
	int request;
	RequestInfo *pRI;
#ifndef SAMSUNG_NEXT_GEN_MODEM
	if (onRequestCompleteNetworkRecovery(t, e)) return;
#endif
	if (onRequestCompleteDeviceIdentity(t, e, response, responselen)) return;

	pRI = (RequestInfo *)t;

	/* If pRI is null, this entire function is useless. */
	if (pRI == NULL)
		goto null_token_exit;

	/* If pCI is null, this entire function is useless. */
	if (pRI->pCI == NULL)
		goto null_token_exit;

	request = pRI->pCI->requestNumber;

	RLOGD("%s:\t\t\t<<< REQUEST-COMPLETE: %s: response:%p responselen:%d token:%p error:%d\n", __FUNCTION__, requestToString(request), response, responselen, t, e);

	switch (request) {
		case RIL_REQUEST_GET_CURRENT_CALLS:
#ifndef SAMSUNG_NEXT_GEN_MODEM
			if (e == RIL_E_SUCCESS)
				updateNetworkRecoveryCallState(response != NULL &&
						responselen >= sizeof(RIL_Call *));
#endif
			break;
		case RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE:
#ifndef SAMSUNG_NEXT_GEN_MODEM
			if (e == RIL_E_SUCCESS && response != NULL &&
					responselen >= sizeof(int)) {
				const int mode = *(const int *)response;
				if (mode == 0 || mode == 1)
					updateNetworkRecoverySelectionMode(true, mode == 0, false);
				else
					RLOGW("network recovery: invalid selection mode %d", mode);
			}
#endif
			break;
                case RIL_REQUEST_VOICE_REGISTRATION_STATE:
                        /* libsecril expects responselen of 60 (bytes) */
                        /* numstrings (15 * sizeof(char *) = 60) */
#ifndef SAMSUNG_NEXT_GEN_MODEM
			if (response != NULL && responselen >= sizeof(char *)) {
				char **registration = (char **)response;
				updateNetworkRecoveryRegistration(true,
						registration[0] != NULL ? atoi(registration[0])
								: RIL_NOT_REG_AND_NOT_SEARCHING,
						e == RIL_E_SUCCESS && registration[0] != NULL);
			} else {
				updateNetworkRecoveryRegistration(true,
						RIL_NOT_REG_AND_NOT_SEARCHING, false);
			}
#endif
			if (response != NULL && responselen < VOICE_REGSTATE_SIZE) {
				onRequestCompleteVoiceRegistrationState(t, e, response, responselen);
				return;
			}
			break;
                case RIL_REQUEST_DATA_REGISTRATION_STATE:
			onRequestCompleteDataRegistrationState(t, e, response, responselen);
			return;
		case RIL_REQUEST_GET_SIM_STATUS:
			/* Remove unused extra elements from RIL_AppStatus */
			if (response != NULL && responselen == sizeof(RIL_CardStatus_v5_samsung)) {
				onCompleteRequestGetSimStatus(t, e, response);
				return;
			}
			break;
		case RIL_REQUEST_LAST_CALL_FAIL_CAUSE:
			/* Remove extra element (ignored on pre-M, now crashing the framework) */
			if (responselen > sizeof(int)) {
				rilEnv->OnRequestComplete(t, e, response, sizeof(int));
				return;
			}
			break;
		case RIL_REQUEST_DATA_CALL_LIST:
		[[fallthrough]];
		case RIL_REQUEST_SETUP_DATA_CALL:
			/* According to the Samsung RIL, the addresses are the gateways?
			 * This fixes mobile data. */
			if (response != NULL && responselen != 0 && (responselen % sizeof(RIL_Data_Call_Response_v6) == 0)) {
				fixupDataCallList(response, responselen);
				rilEnv->OnRequestComplete(t, e, response, responselen);
				return;
			}
			break;
		case RIL_REQUEST_SIGNAL_STRENGTH:
			/* The Samsung RIL reports the signal strength in a strange way... */
			if (response != NULL && responselen >= sizeof(RIL_SignalStrength_v5)) {
				fixupSignalStrength(response, responselen);
				rilEnv->OnRequestComplete(t, e, response, responselen);
				return;
			}
			break;
	}

null_token_exit:
	rilEnv->OnRequestComplete(t, e, response, responselen);
}

static void onUnsolicitedResponseShim(int unsolResponse, const void *data, size_t datalen)
{
	switch (unsolResponse) {
		case RIL_UNSOL_DATA_CALL_LIST_CHANGED:
			/* According to the Samsung RIL, the addresses are the gateways?
			 * This fixes mobile data. */
			if (data != NULL && datalen != 0 && (datalen % sizeof(RIL_Data_Call_Response_v6) == 0))
				fixupDataCallList((void*) data, datalen);
			break;
		case RIL_UNSOL_SIGNAL_STRENGTH:
			/* The Samsung RIL reports the signal strength in a strange way... */
			if (data != NULL && datalen >= sizeof(RIL_SignalStrength_v5))
				fixupSignalStrength((void*) data, datalen);
			break;
	}

	rilEnv->OnUnsolicitedResponse(unsolResponse, data, datalen);
}

static void patchMem(void *libHandle) {
	/*
	 * MAX_TIMEOUT is used for a call to pthread_cond_timedwait_relative_np.
	 * The issue is bionic has switched to using absolute timeouts instead of
	 * relative timeouts, and a maximum time value can cause an overflow in
	 * the function converting relative to absolute timespecs if unpatched.
	 *
	 * By patching this to 0x01FFFFFF from 0x7FFFFFFF, the timeout should
	 * expire in about a year rather than 68 years, and the RIL should be good
	 * up until the year 2036 or so.
	 */
	uint32_t *MAX_TIMEOUT;

	MAX_TIMEOUT = (uint32_t *)dlsym(libHandle, "MAX_TIMEOUT");
	if (CC_UNLIKELY(!MAX_TIMEOUT)) {
		RLOGE("%s: MAX_TIMEOUT could not be found!", __FUNCTION__);
		return;
	}
	RLOGD("%s: MAX_TIMEOUT found at %p!", __FUNCTION__, MAX_TIMEOUT);
	RLOGD("%s: MAX_TIMEOUT is currently 0x%" PRIX32, __FUNCTION__, *MAX_TIMEOUT);
	if (CC_LIKELY(*MAX_TIMEOUT == 0x7FFFFFFF)) {
		*MAX_TIMEOUT = 0x01FFFFFF;
		RLOGI("%s: MAX_TIMEOUT was changed to 0x0%" PRIX32, __FUNCTION__, *MAX_TIMEOUT);
	} else {
		RLOGW("%s: MAX_TIMEOUT was not 0x7FFFFFFF; leaving alone", __FUNCTION__);
	}

}

typedef const RIL_RadioFunctions* (*VendorRilInit)(
		const struct RIL_Env *, int, char **);

typedef struct {
	VendorRilInit init;
	const struct RIL_Env *env;
	int argc;
	char **argv;
	void *handle;
} VendorRilInitContext;

static int supportsShim(int requestCode) {
	if (!vendorRilReady.load(std::memory_order_acquire) ||
			origRilFunctions->supports == NULL)
		return 0;
	return origRilFunctions->supports(requestCode);
}

static void onCancelShim(RIL_Token t) {
	if (vendorRilReady.load(std::memory_order_acquire) &&
			origRilFunctions->onCancel != NULL)
		origRilFunctions->onCancel(t);
}

static const char *getVersionShim(void) {
	if (!vendorRilReady.load(std::memory_order_acquire) ||
			origRilFunctions->getVersion == NULL)
		return "Samsung RIL initializing";
	return origRilFunctions->getVersion();
}

static void *initializeVendorRil(void *param) {
	VendorRilInitContext *context =
			static_cast<VendorRilInitContext *>(param);
	const RIL_RadioFunctions *functions = context->init(
			GetEnv(context->env), context->argc, context->argv);

	if (CC_UNLIKELY(functions == NULL)) {
		RLOGE("%s: the original RIL_Init failed", __FUNCTION__);
		dlclose(context->handle);
		free(context);
		return NULL;
	}

	origRilFunctions = functions;
	SetRadioFunctions(functions);
	vendorRilReady.store(true, std::memory_order_release);
	free(context);

	RLOGI("%s: vendor RIL is ready", __FUNCTION__);
	rilEnv->OnUnsolicitedResponse(
			RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED, NULL, 0);
	return NULL;
}

const RIL_RadioFunctions* RIL_Init(const struct RIL_Env *env, int argc, char **argv)
{
	RIL_RadioFunctions const* (*origRilInit)(const struct RIL_Env *env, int argc, char **argv);
	static RIL_RadioFunctions shimmedFunctions;
	static struct RIL_Env shimmedEnv;
	void *origRil;
	VendorRilInitContext *context;
	pthread_t initThread;
	int error;

	/* Shim the RIL_Env passed to the real RIL, saving a copy of the original */
	ALOGI("loaded; forwarding radio requests to " RIL_LIB_PATH);
	rilEnv = env;
	shimmedEnv = *env;
	shimmedEnv.OnRequestComplete = onRequestCompleteShim;
	shimmedEnv.OnUnsolicitedResponse = onUnsolicitedResponseShim;

	/* Open and Init the original RIL. */

	origRil = dlopen(RIL_LIB_PATH, RTLD_GLOBAL);
	if (CC_UNLIKELY(!origRil)) {
		RLOGE("%s: failed to load '" RIL_LIB_PATH  "': %s\n", __FUNCTION__, dlerror());
		return NULL;
	}

	origRilInit = (const RIL_RadioFunctions *(*)(const struct RIL_Env *, int, char **))(dlsym(origRil, "RIL_Init"));
	if (CC_UNLIKELY(!origRilInit)) {
		RLOGE("%s: couldn't find original RIL_Init!\n", __FUNCTION__);
		goto fail_after_dlopen;
	}

	RLOGE("%s: RIL_Init = %x, origRil = %x", __func__, origRilInit, origRil);

	// Fix RIL issues by patching memory
	patchMem(origRil);

	//remove "-c" command line as Samsung's RIL does not understand it - it just barfs instead
	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "-c") && i != argc -1) {	//found it
			memcpy(argv + i, argv + i + 2, sizeof(char*[argc - i - 2]));
			argc -= 2;
		}
	}

	context = static_cast<VendorRilInitContext *>(
			calloc(1, sizeof(*context)));
	if (CC_UNLIKELY(context == NULL)) {
		RLOGE("%s: failed to allocate vendor RIL init context", __FUNCTION__);
		goto fail_after_dlopen;
	}

	context->init = origRilInit;
	context->env = &shimmedEnv;
	context->argc = argc;
	context->argv = argv;
	context->handle = origRil;

	shimmedFunctions = {};
	shimmedFunctions.version = RIL_VERSION;
	shimmedFunctions.onRequest = onRequestShim;
	shimmedFunctions.onStateRequest = onStateRequestShim;
	shimmedFunctions.supports = supportsShim;
	shimmedFunctions.onCancel = onCancelShim;
	shimmedFunctions.getVersion = getVersionShim;

	error = pthread_create(&initThread, NULL, initializeVendorRil, context);
	if (CC_UNLIKELY(error != 0)) {
		RLOGE("%s: failed to start vendor RIL init thread: %d",
				__FUNCTION__, error);
		free(context);
		goto fail_after_dlopen;
	}
	pthread_detach(initThread);
	return &shimmedFunctions;

fail_after_dlopen:
	dlclose(origRil);
	return NULL;
}
