#define LOG_TAG "sec-sap"
#define RIL_SHLIB

#include "sap-functions.h"
#include <pthread.h>
#include "secril-sap.h"

// --------------------------------------------------------------------------
// Connect

typedef struct
{
    RIL_Token t;
    void (*connectRsp)(RIL_Token t, RIL_Errno e, RIL_SIM_SAP_CONNECT_RSP rsp);
    void (*statusInd)(RIL_Token t, RIL_Errno e, RIL_SIM_SAP_STATUS_IND rsp);
} SapConnectParam;

static void SecSapConnectRsp1(void* param, RIL_Errno e);
static void SecSapConnectRsp2(void* param, RIL_Errno e);

void SapConnect(RIL_Token t, RIL_SIM_SAP_CONNECT_REQ req,
                void (*connectRsp)(RIL_Token t, RIL_Errno e, RIL_SIM_SAP_CONNECT_RSP rsp),
                void (*statusInd)(RIL_Token t, RIL_Errno e, RIL_SIM_SAP_STATUS_IND rsp))
{
    SapConnectParam* param = (SapConnectParam*)calloc(1, sizeof(*param));
    if (param == NULL) {
        RIL_SIM_SAP_CONNECT_RSP rsp = RIL_SIM_SAP_CONNECT_RSP_init_zero;
        rsp.response = RIL_SIM_SAP_CONNECT_RSP_Response_RIL_E_SAP_CONNECT_FAILURE;
        connectRsp(t, RIL_E_NO_MEMORY, rsp);
        return;
    }
    param->t = t;
    param->connectRsp = connectRsp;
    param->statusInd = statusInd;
    SecSapConnect(param, SecSapConnectRsp1);
}

static void SecSapConnectRsp1(void* param, RIL_Errno e)
{
    SapConnectParam* connectParam = (SapConnectParam*)param;

    RIL_SIM_SAP_CONNECT_RSP rsp = RIL_SIM_SAP_CONNECT_RSP_init_zero;
    rsp.response = (e == RIL_E_SUCCESS)
        ? RIL_SIM_SAP_CONNECT_RSP_Response_RIL_E_SUCCESS
        : RIL_SIM_SAP_CONNECT_RSP_Response_RIL_E_SAP_CONNECT_FAILURE;
    rsp.has_max_message_size = false;

    connectParam->connectRsp(connectParam->t, e, rsp);

    if (e == RIL_E_SUCCESS)
    {
        SecSapResetSim(param, SecSapConnectRsp2);
    }
    else free(connectParam);
}

static void SecSapConnectRsp2(void* param, RIL_Errno e)
{
    SapConnectParam* connectParam = (SapConnectParam*)param;

    RIL_SIM_SAP_STATUS_IND rsp = RIL_SIM_SAP_STATUS_IND_init_zero;
    rsp.statusChange = RIL_SIM_SAP_STATUS_IND_Status_RIL_SIM_STATUS_CARD_RESET;

    connectParam->statusInd(connectParam->t, e, rsp);
    free(connectParam);
}

// --------------------------------------------------------------------------
// Disconnect

typedef struct
{
    RIL_Token t;
    void (*rsp)(RIL_Token t, RIL_Errno e, RIL_SIM_SAP_DISCONNECT_RSP rsp);
} SapDisconnectParam;

static void SapDisconnectRsp1(void* param, RIL_Errno e);
static void SapDisconnectRsp2(void* param, RIL_Errno e);

void SapDisconnect(RIL_Token t, RIL_SIM_SAP_DISCONNECT_REQ req,
                   void (*rsp)(RIL_Token t, RIL_Errno e, RIL_SIM_SAP_DISCONNECT_RSP rsp))
{
    SapDisconnectParam* param = (SapDisconnectParam*)calloc(1, sizeof(*param));
    if (param == NULL) {
        RIL_SIM_SAP_DISCONNECT_RSP response = RIL_SIM_SAP_DISCONNECT_RSP_init_zero;
        rsp(t, RIL_E_NO_MEMORY, response);
        return;
    }
    param->t = t;
    param->rsp = rsp;
    SecSapResetSim(param, SapDisconnectRsp1);
}

static void SapDisconnectRsp1(void* param, RIL_Errno e)
{
    SecSapDisconnect(param, SapDisconnectRsp2);
}

static void SapDisconnectRsp2(void* param, RIL_Errno e)
{
    // response from SecSapDisconnect
    SapDisconnectParam* disconnectParam = (SapDisconnectParam*)param;

    RIL_SIM_SAP_DISCONNECT_RSP rsp = RIL_SIM_SAP_DISCONNECT_RSP_init_zero;

    disconnectParam->rsp(disconnectParam->t, e, rsp);
    free(disconnectParam);
}

// --------------------------------------------------------------------------
// ATR

typedef struct
{
    RIL_Token t;
    void (*rsp)(RIL_Token t, RIL_Errno e, RIL_SIM_SAP_TRANSFER_ATR_RSP rsp);
} SapTransferAtrParam;

static void SapTransferAtrRsp(void* param, RIL_Errno e, int resultCode, void* atr, size_t atrlen);

void SapTransferAtr(RIL_Token t, RIL_SIM_SAP_TRANSFER_ATR_REQ req,
                    void (*rsp)(RIL_Token t, RIL_Errno e, RIL_SIM_SAP_TRANSFER_ATR_RSP rsp))
{
    SapTransferAtrParam* param = (SapTransferAtrParam*)calloc(1, sizeof(*param));
    if (param == NULL) {
        RIL_SIM_SAP_TRANSFER_ATR_RSP response = RIL_SIM_SAP_TRANSFER_ATR_RSP_init_zero;
        rsp(t, RIL_E_NO_MEMORY, response);
        return;
    }
    param->t = t;
    param->rsp = rsp;
    SecSapTransferAtr(param, SapTransferAtrRsp);
}

static void SapTransferAtrRsp(void* param, RIL_Errno e, int resultCode, void* atr, size_t atrlen)
{
    SapTransferAtrParam* atrParam = (SapTransferAtrParam*)param;

    RIL_SIM_SAP_TRANSFER_ATR_RSP rsp = RIL_SIM_SAP_TRANSFER_ATR_RSP_init_zero;
    rsp.response = (RIL_SIM_SAP_TRANSFER_ATR_RSP_Response)resultCode;
    if (atr != NULL && atrlen != 0) {
        rsp.atr = (pb_bytes_array_t*) malloc(PB_BYTES_ARRAY_T_ALLOCSIZE(atrlen));
        if (rsp.atr == NULL) e = RIL_E_NO_MEMORY;
        else {
            rsp.atr->size = atrlen;
            memcpy(rsp.atr->bytes, atr, atrlen);
        }
    }

    atrParam->rsp(atrParam->t, e, rsp);

    free(rsp.atr);
    free(atrParam);
}

// --------------------------------------------------------------------------
// APDU

typedef struct
{
    RIL_Token t;
    void (*rsp)(RIL_Token t, RIL_Errno e, RIL_SIM_SAP_APDU_RSP rsp);
} SapApduParam;

static void SapApduRsp(void* param, RIL_Errno e, int resultCode, void* apdu, size_t apdulen);

void SapApdu(RIL_Token t, RIL_SIM_SAP_APDU_REQ req,
             void (*rsp)(RIL_Token t, RIL_Errno e, RIL_SIM_SAP_APDU_RSP rsp))
{
    SapApduParam* param = (SapApduParam*)calloc(1, sizeof(*param));
    if (param == NULL || req.command == NULL) {
        RIL_SIM_SAP_APDU_RSP response = RIL_SIM_SAP_APDU_RSP_init_zero;
        if (param != NULL) free(param);
        rsp(t, param == NULL ? RIL_E_NO_MEMORY : RIL_E_INVALID_ARGUMENTS, response);
        return;
    }
    param->t = t;
    param->rsp = rsp;
    SecSapApdu(param, req.command->bytes, req.command->size, SapApduRsp);
}

static void SapApduRsp(void* param, RIL_Errno e, int resultCode, void* apdu, size_t apdulen)
{
    SapApduParam* apduParam = (SapApduParam*)param;

    RIL_SIM_SAP_APDU_RSP rsp = RIL_SIM_SAP_APDU_RSP_init_zero;
    rsp.type = RIL_SIM_SAP_APDU_RSP_Type_RIL_TYPE_APDU;
    rsp.response = (RIL_SIM_SAP_APDU_RSP_Response)resultCode;
    if (apdu != NULL && apdulen != 0) {
        rsp.apduResponse = (pb_bytes_array_t*) malloc(PB_BYTES_ARRAY_T_ALLOCSIZE(apdulen));
        if (rsp.apduResponse == NULL) e = RIL_E_NO_MEMORY;
        else {
            rsp.apduResponse->size = apdulen;
            memcpy(rsp.apduResponse->bytes, apdu, apdulen);
        }
    }

    apduParam->rsp(apduParam->t, e, rsp);

    free(rsp.apduResponse);
    free(apduParam);
}

// --------------------------------------------------------------------------
// SIM Power

typedef struct
{
    RIL_Token t;
    void (*rsp)(RIL_Token t, RIL_Errno e, RIL_SIM_SAP_POWER_RSP rsp);
} SapSimPowerParam;

static void SapSimPowerRsp(void* param, RIL_Errno e);

void SapSimPower(RIL_Token t, RIL_SIM_SAP_POWER_REQ req,
                 void (*rsp)(RIL_Token t, RIL_Errno e, RIL_SIM_SAP_POWER_RSP rsp))
{
    SapSimPowerParam* param = (SapSimPowerParam*)calloc(1, sizeof(*param));
    if (param == NULL) {
        RIL_SIM_SAP_POWER_RSP response = RIL_SIM_SAP_POWER_RSP_init_zero;
        rsp(t, RIL_E_NO_MEMORY, response);
        return;
    }
    param->t = t;
    param->rsp = rsp;
    SecSapSimPower(param, req.state, SapSimPowerRsp);
}

static void SapSimPowerRsp(void* param, RIL_Errno e)
{
    SapSimPowerParam* simPowerParam = (SapSimPowerParam*)param;

    RIL_SIM_SAP_POWER_RSP rsp = RIL_SIM_SAP_POWER_RSP_init_zero;
    rsp.response = e == RIL_E_SUCCESS
            ? RIL_SIM_SAP_POWER_RSP_Response_RIL_E_SUCCESS
            : RIL_SIM_SAP_POWER_RSP_Response_RIL_E_GENERIC_FAILURE;

    simPowerParam->rsp(simPowerParam->t, e, rsp);
    free(simPowerParam);
}

// --------------------------------------------------------------------------
// Reset SIM

typedef struct
{
    RIL_Token t;
    void (*rsp)(RIL_Token t, RIL_Errno e, RIL_SIM_SAP_RESET_SIM_RSP rsp);
} SapResetSimParam;

static void SapResetSimRsp(void* param, RIL_Errno e);

void SapResetSim(RIL_Token t, RIL_SIM_SAP_RESET_SIM_REQ req,
                 void (*rsp)(RIL_Token t, RIL_Errno e, RIL_SIM_SAP_RESET_SIM_RSP rsp))
{
    SapResetSimParam* param = (SapResetSimParam*)calloc(1, sizeof(*param));
    if (param == NULL) {
        RIL_SIM_SAP_RESET_SIM_RSP response = RIL_SIM_SAP_RESET_SIM_RSP_init_zero;
        rsp(t, RIL_E_NO_MEMORY, response);
        return;
    }
    param->t = t;
    param->rsp = rsp;
    SecSapResetSim(param, SapResetSimRsp);
}

static void SapResetSimRsp(void* param, RIL_Errno e)
{
    SapResetSimParam* resetSimParam = (SapResetSimParam*)param;

    RIL_SIM_SAP_RESET_SIM_RSP rsp = RIL_SIM_SAP_RESET_SIM_RSP_init_zero;
    rsp.response = e == RIL_E_SUCCESS
            ? RIL_SIM_SAP_RESET_SIM_RSP_Response_RIL_E_SUCCESS
            : RIL_SIM_SAP_RESET_SIM_RSP_Response_RIL_E_GENERIC_FAILURE;

    resetSimParam->rsp(resetSimParam->t, e, rsp);
    free(resetSimParam);
}
