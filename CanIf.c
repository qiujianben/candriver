
#include "CanIf.h"
#include "CanIf_Cbk.h"
#include "Det.h"
#include "Dem.h"
///todo implement support for MemMap.h
//#include "MemMap.h"
#if defined(USE_PDUR)
#include "PduR_CanIf.h"
#endif

#if defined(USE_SCHM)
#include "SchM_CanIf.h"
#endif

typedef struct {
#if CANIF_PUBLIC_SETDYNAMICTXID_API
	dynCanId[CANIF_NUMBER_OF_DYNAMIC_CANTXPDUIDS];
#endif
	struct {
		nextInQueue; // ptr or L-PDU id?
//		canid; // sl� upp det fr�n lpdu ist�llet?
		uint8 data[8];
		uint8 dlc; // -1 betyder tom buffer
#if CANIF_PUBLIC_READTXPDU_NOTIFY_STATUS_API
		bool txConfirmed;
#endif
	} txLpdu[CANIF_NUM_TX_LPDU_ID];
	struct {
		nextInQueue; // ptr or L-PDU id?
//		canid; // sl� upp det fr�n lpdu ist�llet?
#if CANIF_PUBLIC_READRXPDU_DATA_API
		uint8 data[8];
		uint8 dlc;
#endif
#if CANIF_PUBLIC_READRXPDU_NOTIFY_STATUS_API
		bool rxInd;
#endif
	} rxLpdu[CANIF_NUM_RX_LPDU_ID];
} CanIf_LPduDataType;

// f�r buffered tx
typedef struct {
	
	struct {
		nextInQueue; // ptr or L-PDU id?
	}hth[CANIF_NUM_TX_LPDU_ID];
} CanIf_CanDriverUnitType;

typedef struct {
	uint8 controllerMode; // CanIf_ControllerModeType
	uint8 pduMode;// CanIf_PduGetModeType
#if CANIF_PUBLIC_TXCONFIRM_POLLING_SUPPORT
	uint8 transmitConfirmedSinceLastStart;
#endif	
} CanIf_CanControllerType;

static CanIf_LPduDataType lPduData;
static CanIf_CanDriverUnitType driverUnit;
static const CanIf_ConfigType* CanIf_ConfigPtr;
static CanIf_CanControllerType controllerData[CANIF_CHANNEL_CNT];

void CanIf_Init(const CanIf_ConfigType* ConfigPtr);

Std_ReturnType CanIf_SetControllerMode(uint8 controllerId, CanIf_ControllerModeType controllerMode) {
	Can_StateTransitionType transition;
	switch(controllerMode) {
	case CANIF_CS_STOPPED:
		if(controllerData[controllerId].controllerMode == CANIF_CS_SLEEP) {
			transition = CAN_T_WAKEUP;
		} else {
			transition = CAN_T_STOP;
		}
		break;
	case CANIF_CS_STARTED:
		transition = CAN_T_START;
		break;
	case CANIF_CS_SLEEP:
		transition = CAN_T_SLEEP;
		break;
	default:
		transition = CAN_T_STOP;
	}

	if(Can_SetControllerMode(controllerId, transition) == CAN_NOT_OK) {
		return E_NOT_OK;
	} else {
		return E_OK;
	}
}

Std_ReturnType CanIf_GetControllerMode(uint8 controllerId, CanIf_ControllerModeType *controllerModePtr) {
	*controllerModePtr = controllerData[controllerId].controllerMode;
	///todo should this function return NOK if new mode has been requested but not accepted?
	return E_OK;
}
		
Std_ReturnType CanIf_Transmit(PduIdType canTxPduId,	const PduInfoType *pduInfoPtr) {
	// verifiera att channel �r online
	if(controllerData[CanIf_ConfigPtr->txLpduCfg[canTxPduId].controller].controllerMode != CANIF_CS_STARTED) {
		// channel not started, return
		return ERR_NOT_OK;
	}
	Can_HwHandleType hth = CanIf_ConfigPtr->txLpduCfg[canTxPduId].hth;
	uint8 controller = CanIf_ConfigPtr->txLpduCfg[canTxPduId].controller;
	Can_IdType canId = ((canTxPduId < CANIF_NUM_DYNAMIC_TX_LPDU_ID)? lPduData.dynCanId[canTxPduId] : CanIf_ConfigPtr->txLpduCfg[canTxPduId].id)
	Can_PduType canPdu = {
		.sdu = pduInfoPtr->SduDataPtr,
		.id = canId,
		.swPduHandle = canTxPduId,
		.length = pduInfoPtr->SduLength
	};
#if buffered transmit
	bool lock = LockSave();
	if(lPduData.txLpdu[canTxPduId].dlc == -1) {
		// pdu buffer not empty and therefore scheduled to be sent. overwrite data and return
		lPduData.txLpdu[canTxPduId].dlc = pduInfoPtr->SduLength;
		memcpy(lPduData.txLpdu[canTxPduId].data, pduInfoPtr->SduDataPtr, pduInfoPtr->SduLength);
	} else if(Can_Write(hth, &canPdu) == CAN_BUSY) {
		// write data to buffer
		lPduData.txLpdu[canTxPduId].dlc = pduInfoPtr->SduLength;
		memcpy(lPduData.txLpdu[canTxPduId].data, pduInfoPtr->SduDataPtr, pduInfoPtr->SduLength);
		// add txlpdu to transmission queue in prio order
		PduId *pduIdPtr = &driverUnit.hth[hth].nextInQueue;
		///todo ensure that high id means high prio
		while(*pduIdPtr != -1 &&
			((*pduIdPtr < CANIF_NUM_DYNAMIC_TX_LPDU_ID)? lPduData.dynCanId[*pduIdPtr] : CanIf_ConfigPtr->txLpduCfg[*pduIdPtr].id) >= canId) {
			*pduIdPtr = &lPduData.txLpdu[canTxPduId].nextInQueue;
		}
		// add pdu tp queue
		lPduData.txLpdu[canTxPduId].nextInQueue = *pduIdPtr;
		*pduIdPtr = canTxPduId;
	}
	LockRestore(lock);
#else
	Can_Write(hth, &canPdu);
#endif
	///todo shoud this funtion return E_NOT_OK if not buffered transmission and Can_Write returns not ok?
	return E_OK;
}

#if CANIF_CANCEL_TRANSMIT_SUPPORT
// sevice id 0x18
Std_ReturnType CanIf_CancelTransmit(PduIdType CanTxPduId) {
	// dummy function with no functionality needed since it may be callbed by PduR
	VALIDATE(CanTxPduId < CANIF_NUM_TX_LPDU_ID, 0x18, CANIF_E_INVALID_TXPDUID);
	return STD_OK;
}
#endif

#if ( CANIF_PUBLIC_READRXPDU_DATA_API == STD_ON )
// service id 6
Std_ReturnType CanIf_ReadRxPduData(PduIdType canRxPduId, PduInfoType *pduInfoPtr) {
	VALIDATE(CanIf_ConfigPtr != 0, 6, CANIF_E_UNINIT);
	VALIDATE(pduInfoPtr != 0, 6, CANIF_E_PARAM_POINTER);
	VALIDATE(canRxPduId < CANIF_NUM_RX_LPDU_ID, 6, CANIF_E_INVALID_RXPDUID);
	if(controllerData[CanIf_ConfigPtr->rxLpduCfg[canRxPduId].controller].controllerMode != CANIF_CS_STARTED) {
		// Controller is not in started mode, return error
		return E_NOT_OK;
	}
	if(controllerData[CanIf_ConfigPtr->rxLpduCfg[canRxPduId].controller].pduMode & CANIF_GET_RX_ONLINE) {
		// Controller is not in receive mode online, return error
		return E_NOT_OK;
	}
	// copy data
	bool lock = LockSave();
	uint8 dlc = lPduData.rxLpdu[canRxPduId].dlc;
	pduInfoPtr->SduLength = dlc;
	memcpy(pduInfoPtr->SduDataPtr, lPduData.rxLpdu[canRxPduId].data, dlc);
	LockRestore(lock);
	return E_OK;
}
#endif

#if ( CANIF_PUBLIC_READTXPDU_NOTIFY_STATUS_API == STD_ON )
// service id 7
CanIf_NotifStatusType CanIf_ReadTxNotifStatus(PduIdType canTxPduId) {
	VALIDATE_NO_RV(CanIf_ConfigPtr != 0, 7, CANIF_E_UNINIT);
	VALIDATE_NO_RV(canTxPduId < CANIF_NUM_TX_LPDU_ID, 7, CANIF_E_INVALID_TXPDUID);
	CanIf_NotifStatusType retVal = lPduData.txLpdu[canTxPduId].txConfirmed;
	if(retVal != CANIF_NO_NOTIFICATION) {
		lPduData.txLpdu[canTxPduId].txConfirmed = CANIF_NO_NOTIFICATION;
	}
	return retVal;
}
#endif

#if ( CANIF_PUBLIC_READRXPDU_NOTIFY_STATUS_API == STD_ON )
// service id 8
CanIf_NotifStatusType CanIf_ReadRxNotifStatus(PduIdType CanRxPduId) {
	VALIDATE_NO_RV(CanIf_ConfigPtr != 0, 8, CANIF_E_UNINIT);
	VALIDATE_NO_RV(canRxPduId < CANIF_NUM_TX_LPDU_ID, 8, CANIF_E_INVALID_TXPDUID);
	CanIf_NotifStatusType retVal = lPduData.rxLpdu[canRxPduId].rxInd;
	if(retVal != CANIF_NO_NOTIFICATION) {
		lPduData.rxLpdu[canRxPduId].rxInd = CANIF_NO_NOTIFICATION;
	}
	return retVal;
}
#endif

// service id 9
Std_ReturnType CanIf_SetPduMode(uint8 controllerId, CanIf_PduSetModeType pduModeRequest) {
	VALIDATE(CanIf_ConfigPtr != 0, 9, CANIF_E_UNINIT);
	VALIDATE(controllerId < CANIF_CHANNEL_CNT, 9, CANIF_E_PARAM_CONTROLLERID);
	// set controller pdu mode
	controllerData[controllerId].pduMode = controllerData[controllerId].pduMode & ~(pduModeRequest >> 8) | pduModeRequest & 0xff;
	return E_OK;
}

// service id 10
Std_ReturnType CanIf_GetPduMode(uint8 controllerId, CanIf_PduGetModeType* pduModePtr) {
	VALIDATE(CanIf_ConfigPtr != 0, 10, CANIF_E_UNINIT);
	VALIDATE(pduModePtr != 0, 10, CANIF_E_PARAM_POINTER);
	VALIDATE(controllerId < CANIF_CHANNEL_CNT, 10, CANIF_E_PARAM_CONTROLLERID);
	// return controller pdu mode
	*pduModePtr = controllerData[controllerId].pduMode;
}
#if ( CANIF_VERSION_INFO_API == STD_ON )
#define CanIf_GetVersionInfo(_vi) STD_GET_VERSION_INFO(_vi,CANIF)
#endif

#if CANIF_PUBLIC_SETDYNAMICTXID_API
// service id 12
void CanIf_SetDynamicTxId(PduIdType canTxPduId, Can_IdType canId) {
	VALIDATE_NO_RV(CanIf_ConfigPtr != 0, 12, CANIF_E_UNINIT);
	VALIDATE_NO_RV((canId & 0x80000000) && canId < 0xA0000000 || canId < 0x800, 12, CANIF_E_PARAM_CANID);
	VALIDATE_NO_RV(canTxPduId < CANIF_NUM_DYNAMIC_TX_LPDU_ID, 12, CANIF_E_INVALID_TXPDUID);
	// change the tx id
	lPduData.dynCanId[canTxPduId] = canId;
}
#endif

Std_ReturnType CanIf_SetTrcvMode( uint8 TransceiverId, CanTrcv_TrcvModeType TransceiverMode ) {
	///todo implement
}
Std_ReturnType CanIf_GetTrcvMode( uint8 TransceiverId, CanTrcv_TrcvModeType* TransceiverModePtr ) {
	///todo implement
}
Std_ReturnType CanIf_GetTrcvWakeupReason( uint8 TransceiverId, CanTrcv_TrcvWakeupReasonType* TrcvWuReasonPtr ) {
	///todo implement
}
Std_ReturnType CanIf_SetTrcvWakeupMode( uint8 TransceiverId, CanTrcv_TrcvWakeupModeType TrcvWakeupMode ) {
	///todo implement
}

#if CANIF_CTRL_WAKEUP_SUPPORT || CANIF_TRCV_WAKEUP_SUPPORT
Std_ReturnType CanIf_CheckWakeup( EcuM_WakeupSourceType WakeupSource ) {
	///todo implement
}
#if CANIF_PUBLIC_WAKEUP_CHECK_VALIDATION_API
Std_ReturnType CanIf_CheckValidation( EcuM_WakeupSourceType WakeupSource ) {
	///todo implement
}
#endif
#endif

#if CANIF_PUBLIC_TXCONFIRM_POLLING_SUPPORT
// service id 0x19
CanIf_NotifStatusType CanIf_GetTxConfirmationState(uint8 controllerId) {
	VALIDATE(CanIf_ConfigPtr != 0, 0x19, CANIF_E_UNINIT);
	VALIDATE(controllerId < CANIF_CHANNEL_CNT, 0x19, CANIF_E_PARAM_CONTROLLERID);
	return controllerData[controllerId].transmitConfirmedSinceLastStart;
}
#endif


void CanIf_TxConfirmation(PduIdType canTxPduId) { // L-PDU id
#if buffered transmit
#if CANIF_PUBLIC_TXCONFIRM_POLLING_SUPPORT
	controllerData[CanIf_ConfigPtr->txLpduCfg[canTxPduId].controller].transmitConfirmedSinceLastStart = CANIF_TX_RX_NOTIFICATION;
#endif
	hth = CanIf_ConfigPtr->txLpduCfg[canTxPduId].controller].hth;
	bool lock = LockSave();
	Can_HwHandleType lpdu = driverUnit.hth[hth].nextInQueue;
	if(lpdu != -1) {
		// send next
		Can_IdType canId = ((lpdu < CANIF_NUM_DYNAMIC_TX_LPDU_ID)? lPduData.dynCanId[lpdu] : CanIf_ConfigPtr->txLpduCfg[lpdu].id)
		Can_PduType canPdu = {
			.sdu = lPduData.txLpdu[lpdu].data,
			.id = canId,
			.swPduHandle = lpdu,
			.length = lPduData.txLpdu[lpdu].dlc
		};
		// send data to CAN controller, ignore return value since it cannot be busy, send confirm is just called
		Can_Write(hth, &canPdu);
		// set dlc to -1 to indicate empty buffer
		lPduData.txLpdu[lpdu].dlc = -1;
		// update queue head
		driverUnit.hth[hth].nextInQueue = lPduData.txLpdu[lpdu].nextInQueue;
	}
	LockRestore(lock);
#endif
#if CANIF_PUBLIC_READTXPDU_NOTIFY_STATUS_API
	// set tx ind flag
	lPduData.txLpdu[canTxPduId].txConfirmed = CANIF_TX_RX_NOTIFICATION;
#endif
	// call eventual callback
	if(CanIf_ConfigPtr->txLpduCfg[canTxPduId].callback) {
		(*CanIf_ConfigPtr->txLpduCfg[canTxPduId].callback)(CanIf_ConfigPtr->txLpduCfg[canTxPduId].i_PduId);
	}
}

extern const CanIf_HrHConfigType canIfHrhCfg[];
void CanIf_RxIndication(Can_HwHandleType hrh, Can_IdType canId, uint8 canDlc, const uint8* canSduPtr) {
	// function callbed by CAN controller. Different names for different driver units
	CanIf_Arc_RxIndication(hrh, canId, canDlc, canSduPtr, canIfHrhCfg);
}

static void RxLPduReceived(PduId lpdu, Can_IdType canId, uint8 canDlc, const uint8* canSduPtr) {
	// store in buffer
#if CANIF_PUBLIC_READRXPDU_DATA_API
	bool lock = LockSave();
	lPduData.rxLpdu[lpdu].dlc = canDlc;
	memcpy(lPduData.rxLpdu[lpdu].data, canSduPtr, canDlc);
	LockRestore(lock);
#endif
#if CANIF_PUBLIC_READRXPDU_NOTIFY_STATUS_API
	// set indicator
	lPduData.rxLpdu[lpdu].rxInd;
#endif
	// call eventual callback
	if(CanIf_ConfigPtr->rxLpduCfg[lpdu].callback) {
		(*CanIf_ConfigPtr->rxLpduCfg[lpdu].callback)(canDlc, canSduPtr, CanIf_ConfigPtr->rxLpduCfg[lpdu].i_PduId);
}

// called by CanIf_RxIndication with info about correct hrhConfig set for CanDriverUnit
void CanIf_Arc_RxIndication(Can_HwHandleType hrh, Can_IdType canId, uint8 canDlc, const uint8* canSduPtr, const CanIf_HrHConfigType *hrhCfgPtr) {
	PduIdType firstPduId = hrhCfgPtr[hrh].l_pduIdFirst;
	int numPdus = hrhCfgPtr[hrh].numLpdus;
	if(numPdus == 0) {
		// no filtering, lpdu id found
		RxLPduReceived(firstPduId, canId, canDlc, canSduPtr);
	} else {
		while(numPdus > 1) {
			if(CanIf_ConfigPtr->rxLpduCfg[firstPduId + numPdus / 2].id >= canId) {
				firstPduId += numPdus / 2;
				numPdus = numPdus / 2 + numPdus % 2;
			} else {
				numPdus = numPdus / 2;
			}
		}
		if(CanIf_ConfigPtr->rxLpduCfg[firstPduId].id == canId) {
			// lpdu id found handle message
			RxLPduReceived(firstPduId, canId, canDlc, canSduPtr);
		}
	}
}

void CanIf_CancelTxConfirmation(const Can_PduType* pduInfoPtr) {
#if buffered transmit
	bool lock = LockSave();
	if(lPduData.txLpdu[pduInfoPtr->swPduHandle].dlc != -1) {
		// pdu buffer not empty: throw old data and return
	} else {
		// write data to buffer
		lPduData.txLpdu[pduInfoPtr->swPduHandle].dlc = pduInfoPtr->length;
		memcpy(lPduData.txLpdu[pduInfoPtr->swPduHandle].data, pduInfoPtr->sdu, pduInfoPtr->length);
		// add txlpdu to transmission queue in prio order
		PduId *pduIdPtr = &driverUnit.hth[hth].nextInQueue;
		///todo ensure that high id means high prio
		while(*pduIdPtr != -1 &&
			((*pduIdPtr < CANIF_NUM_DYNAMIC_TX_LPDU_ID)? lPduData.dynCanId[*pduIdPtr] : CanIf_ConfigPtr->txLpduCfg[*pduIdPtr].id) >= canId) {
			*pduIdPtr = &lPduData.txLpdu[pduInfoPtr->swPduHandle].nextInQueue;
		}
		// add pdu to queue
		lPduData.txLpdu[pduInfoPtr->swPduHandle].nextInQueue = *pduIdPtr;
		*pduIdPtr = pduInfoPtr->swPduHandle;
	}
	LockRestore(lock);
	hth = CanIf_ConfigPtr->txLpduCfg[pduInfoPtr->swPduHandle].hth;
	bool lock = LockSave();
	Can_HwHandleType lpdu = driverUnit.hth[hth].nextInQueue;
	if(lpdu != -1) {
		// send next
		Can_IdType canId = ((lpdu < CANIF_NUM_DYNAMIC_TX_LPDU_ID)? lPduData.dynCanId[lpdu] : CanIf_ConfigPtr->txLpduCfg[lpdu].id)
		Can_PduType canPdu = {
			.sdu = lPduData.txLpdu[lpdu].data,
			.id = canId,
			.swPduHandle = lpdu,
			.length = lPduData.txLpdu[lpdu].dlc
		};
		// send data to CAN controller, ignore return value since it cannot be busy, send confirm is just called
		Can_Write(hth, &canPdu);
		// set dlc to -1 to indicate empty buffer
		lPduData.txLpdu[lpdu].dlc = -1;
		// update queue head
		driverUnit.hth[hth].nextInQueue = lPduData.txLpdu[lpdu].nextInQueue;
	}
	LockRestore(lock);
#else
	// do nothing
#endif
}

void CanIf_ControllerBusOff(uint8 Controller) {
	// store the new mode
	controllerData[controller].controllerMode = CANIF_CS_STOPPED;
	// reset all pending tx requests
	for(PduIdType i = 0; i < CANIF_NUM_TX_LPDU_ID; i++) {
		if(CanIf_ConfigPtr->txLpduCfg[i].controller == controller) {
			driverUnit.hth[CanIf_ConfigPtr->txLpduCfg[i].hth].nextInQueue = -1;
		}
	}
	// call ev callback
	if(CanIf_ConfigPtr->controller[controller].busOffFunction) {
		(*CanIf_ConfigPtr->controller[controller].busOffFunction)(controller);
}

void CanIf_ControllerModeIndication(uint8 controller, CanIf_ControllerModeType controllerMode) {
	// store the new mode
	controllerData[controller].controllerMode = controllerMode;
	if(controllerMode == CANIF_CS_STOPPED) {
		// stopped mode reached, disable all pending tx requests
		for(PduIdType i = 0; i < CANIF_NUM_TX_LPDU_ID; i++) {
			if(CanIf_ConfigPtr->txLpduCfg[i].controller == controller) {
				driverUnit.hth[CanIf_ConfigPtr->txLpduCfg[i].hth].nextInQueue = -1;
			}
		}
	}
#if CANIF_PUBLIC_TXCONFIRM_POLLING_SUPPORT
	 else if(controllerMode == CANIF_CS_STARTED) {
		controllerData[controller].transmitConfirmedSinceLastStart = CANIF_NO_NOTIFICATION;
	}
#endif
	// call ev callback
	if(CanIf_ConfigPtr->controller[controller].modeIndFunction) {
		(*CanIf_ConfigPtr->controller[controller].modeIndFunction)(controller, controllerMode);
}

void CanIf_TrcvModeIndication(uint8 Transceiver, CanTrcv_TrcvModeType TransceiverMode) {
}