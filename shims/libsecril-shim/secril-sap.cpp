#define LOG_TAG "sec-sap"
#define RIL_SHLIB

#include "secril-sap.h"

#include <string.h>
#include <utils/Log.h>

// --------------------------------------------------------------------------
// Constants

static const int CMD_CONNECT = 1;
static const int CMD_SIM_POWER = 4;
static const int CMD_ATR = 5;

static const int MSGID_CONNECT = 0;
static const int MSGID_DISCONNECT = 2;

static const int MSGID_SIM_OFF = 0x09;
static const int MSGID_SIM_ON = 0x0B;
static const int MSGID_SIM_RESET = 0x0D;

static const int MSGID_ATR = 0x07;

// --------------------------------------------------------------------------
// Local types

typedef struct
{
    int cmd;
    int msgId;
    RIL_Token t;
    void* bytes;
    size_t byteslen;
} RequestParam;

typedef struct _ResponseData
{
    uint64_t magic;
    void* param;
    void (*callback)(void* param, RIL_Errno e);
    void (*callbackData)(void* param, RIL_Errno e, int resultCode, void* data, size_t datalen);
} ResponseData;

// --------------------------------------------------------------------------
// Local data

static  RIL_RadioFunctions const *s_radioFunctions;
static const struct RIL_Env *s_env;
static const uint64_t RESPONSE_DATA_MAGIC = UINT64_C(0x534150524553504E);

// --------------------------------------------------------------------------
// Declarations

static void OnRequestComplete(RIL_Token t, RIL_Errno e, void *response, size_t responselen);

static void SendSapRequest(int cmd, int msgId, RIL_Token t);
static void SendSapApduRequest(void* data, size_t datalen, RIL_Token t);
static RIL_Token GetToken(void (*callback)(void* param, RIL_Errno e), void* param);
static RIL_Token GetTokenData(void (*callbackData)(void* param, RIL_Errno e, int resultCode,
                              void* data, size_t datalen), void* param);

// --------------------------------------------------------------------------
// Public functions

struct RIL_Env* GetEnv(const struct RIL_Env *env)
{
	static struct RIL_Env rilEnv;

    s_env = env;
    memcpy(&rilEnv, env, sizeof(struct RIL_Env));
    rilEnv.OnRequestComplete = OnRequestComplete;
    return &rilEnv;
}

void SetRadioFunctions(const RIL_RadioFunctions* radioFunctions)
{
    s_radioFunctions = radioFunctions;
}

void SecSapConnect(void* param, void (*callback)(void* param, RIL_Errno e))
{
    RIL_Token t = GetToken(callback, param);
    if (t == NULL) callback(param, RIL_E_NO_MEMORY);
    else SendSapRequest(CMD_CONNECT, MSGID_CONNECT, t);
}

void SecSapDisconnect(void* param, void (*callback)(void* param, RIL_Errno e))
{
    RIL_Token t = GetToken(callback, param);
    if (t == NULL) callback(param, RIL_E_NO_MEMORY);
    else SendSapRequest(CMD_CONNECT, MSGID_DISCONNECT, t);
}

void SecSapResetSim(void* param, void (*callback)(void* param, RIL_Errno e))
{
    RIL_Token t = GetToken(callback, param);
    if (t == NULL) callback(param, RIL_E_NO_MEMORY);
    else SendSapRequest(CMD_SIM_POWER, MSGID_SIM_RESET, t);
}

void SecSapApdu(void* param, void* apdu, size_t apdusize, void (*callback)(void* param, RIL_Errno e,
                int resultCode, void* apdu, size_t apdulen))
{
    RIL_Token t = GetTokenData(callback, param);
    if (t == NULL) callback(param, RIL_E_NO_MEMORY, 6, NULL, 0);
    else SendSapApduRequest(apdu, apdusize, t);
}

void SecSapTransferAtr(void* param, void (*callback)(void* param, RIL_Errno e, int resultCode,
                       void* apdu, size_t apdulen))
{
    RIL_Token t = GetTokenData(callback, param);
    if (t == NULL) callback(param, RIL_E_NO_MEMORY, 6, NULL, 0);
    else SendSapRequest(CMD_ATR, MSGID_ATR, t);
}

void SecSapSimPower(void* param, bool on, void (*callback)(void* param, RIL_Errno e))
{
    RIL_Token t = GetToken(callback, param);
    if (t == NULL) callback(param, RIL_E_NO_MEMORY);
    else SendSapRequest(CMD_SIM_POWER, on ? MSGID_SIM_ON : MSGID_SIM_OFF, t);
}

// --------------------------------------------------------------------------
// Handle requests

static RIL_Token GetToken(void (*callback)(void* param, RIL_Errno e), void* param)
{
    ResponseData* response = (ResponseData*)calloc(1, sizeof(*response));
    if (response == NULL) return NULL;
    response->magic = RESPONSE_DATA_MAGIC;
    response->param = param;
    response->callback = callback;
    return response;
}

static RIL_Token GetTokenData(void (*callbackData)(void* param, RIL_Errno e, int resultCode,
                              void* data, size_t datalen), void* param)
{
    ResponseData* response = (ResponseData*)calloc(1, sizeof(*response));
    if (response == NULL) return NULL;
    response->magic = RESPONSE_DATA_MAGIC;
    response->param = param;
    response->callbackData = callbackData;
    return response;
}

static void SapRequestTimedCallback(void *param)
{
    RequestParam* data = (RequestParam*)param;

    RLOGD("..... SapRequestTimedCallback() cmd = %d, msgId = %d, t = %p",
          data->cmd, data->msgId, data->t);

    unsigned char request[5] = { 0x14, 0xFF, 0x00, 0x05, 0xFF };
    request[1] = data->cmd;
    request[4] = data->msgId;
    s_radioFunctions->onRequest(RIL_REQUEST_OEM_HOOK_RAW, request, sizeof(request), data->t);
    free(data);
}

static void SendSapRequest(int cmd, int msgId, RIL_Token t)
{
    RLOGD("..... sendSapRequest(cmd = %d, msgId = %d, t = %p)", cmd, msgId, t);

    if (t == NULL) return;
    RequestParam* data = (RequestParam*)calloc(1, sizeof(*data));
    if (data == NULL) {
        ResponseData* response = (ResponseData*)t;
        if (response->callback != NULL)
            response->callback(response->param, RIL_E_NO_MEMORY);
        else
            response->callbackData(response->param, RIL_E_NO_MEMORY, 6, NULL, 0);
        free(response);
        return;
    }
    data->cmd = cmd;
    data->msgId = msgId;
    data->t = t;
    s_env->RequestTimedCallback(SapRequestTimedCallback, data, NULL);
}

static void SapApduRequestTimedCallback(void *param)
{
    RequestParam* data = (RequestParam*)param;

    RLOGD("..... SapApduRequestTimedCallback() bytes = %p, byteslen = %zu, t = %p",
          data->bytes, data->byteslen, data->t);

    unsigned char* request = (unsigned char*)malloc(data->byteslen+6);
    if (request == NULL) {
        ResponseData* response = (ResponseData*)data->t;
        response->callbackData(response->param, RIL_E_NO_MEMORY, 6, NULL, 0);
        free(response);
        free(data->bytes);
        free(data);
        return;
    }
    request[0] = 0x14;
    request[1] = 0x06;
    request[2] = (data->byteslen+6) >> 8;
    request[3] = (data->byteslen+6) & 0xFF;
    request[4] = data->byteslen & 0xFF;
    request[5] = data->byteslen >> 8;
    memcpy(request+6, data->bytes, data->byteslen);
    s_radioFunctions->onRequest(RIL_REQUEST_OEM_HOOK_RAW, request, data->byteslen+6, data->t);

    free(request);
    free(data->bytes);
    free(data);
}

static void SendSapApduRequest(void* bytes, size_t byteslen, RIL_Token t)
{
    RLOGD("..... sendSapApduRequest(bytes = %p, byteslen = %zu, t = %p)", bytes, byteslen, t);

    if (t == NULL || bytes == NULL || byteslen == 0 || byteslen > UINT16_MAX) {
        if (t != NULL) {
            ResponseData* response = (ResponseData*)t;
            response->callbackData(response->param, RIL_E_INVALID_ARGUMENTS, 6, NULL, 0);
            free(response);
        }
        return;
    }
    RequestParam* data = (RequestParam*)calloc(1, sizeof(*data));
    if (data == NULL) {
        ResponseData* response = (ResponseData*)t;
        response->callbackData(response->param, RIL_E_NO_MEMORY, 6, NULL, 0);
        free(response);
        return;
    }
    data->cmd = 6;
    data->t = t;
    data->bytes = malloc(byteslen);
    if (data->bytes == NULL) {
        ResponseData* response = (ResponseData*)t;
        response->callbackData(response->param, RIL_E_NO_MEMORY, 6, NULL, 0);
        free(response);
        free(data);
        return;
    }
    memcpy(data->bytes, bytes, byteslen);
    data->byteslen = byteslen;
    s_env->RequestTimedCallback(SapApduRequestTimedCallback, data, NULL);
}

// --------------------------------------------------------------------------
// Handle response

static void OnRequestComplete(RIL_Token t, RIL_Errno e, void *response, size_t responselen)
{
    ResponseData* req = (ResponseData*)t;
    if (req != NULL && req->magic == RESPONSE_DATA_MAGIC)
    {
        RLOGD("-----    OnRequestComplete(%p,%d,%p,%zu)", t, e, response, responselen);

        if (req->callback != NULL)
        {
            RLOGD("        Callback(%p,%d)", req, e);
            req->callback(req->param, e);
        }
        else
        {
            void* bytes = NULL;
            size_t len = 0;
            int resultCode = 6; // Error, data not available
            if (response != NULL && responselen >= 7)
            {
                resultCode = ((unsigned char*)response)[4];
                len = ((unsigned char*)response)[5] |
                        ((size_t)((unsigned char*)response)[6] << 8);
                if (len <= responselen - 7) bytes = (char*)response + 7;
                else {
                    len = 0;
                    resultCode = 6;
                    e = RIL_E_INVALID_RESPONSE;
                }
            }

            RLOGD("        Callback(%p,%d,%d,%p,%zu)", req, e, resultCode, bytes, len);
            req->callbackData(req->param, e, resultCode, bytes, len);
        }
        req->magic = 0;
        free(req);
    }
    else
    {
        s_env->OnRequestComplete(t, e, response, responselen);
    }
}
