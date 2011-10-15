
void CanIf_RxIndication(
	Can_HwHandleType Hrh, 
	Can_IdType CanId, 
	uint8 CanDlc, 
	const uint8* CanSduPtr 
);

void CanIf_TxConfirmation(
    PduIdType CanTxPduId // L-PDU id
);

void CanIf_CancelTxConfirmation(
    const Can_PduType* PduInfoPtr
);

void CanIf_ControllerBusOff( 
    uint8 Controller
);