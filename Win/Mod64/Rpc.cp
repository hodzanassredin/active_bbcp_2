MODULE WinRpc ["RPCRT4.dll"];
(**
	project	= "BlackBox"
	organization	= "www.oberon.ch"
	contributors	= "Oberon microsystems"
	version	= "System/Rsrc/About"
	copyright	= "System/Rsrc/About"
	license	= "Docu/BB-License"
	changes	= ""
	issues	= ""

**)

	IMPORT SYSTEM, WinOle, COM, WinApi;

	CONST (* macros *)
		RPC_C_BINDING_INFINITE_TIMEOUT* = 10;
		RPC_C_BINDING_MIN_TIMEOUT* = 0;
		RPC_C_BINDING_DEFAULT_TIMEOUT* = 5;
		RPC_C_BINDING_MAX_TIMEOUT* = 9;
		RPC_C_CANCEL_INFINITE_TIMEOUT* = -1;
		RPC_C_LISTEN_MAX_CALLS_DEFAULT* = 1234;
		RPC_C_PROTSEQ_MAX_REQS_DEFAULT* = 10;
		RPC_C_STATS_CALLS_IN* = 0;
		RPC_C_STATS_CALLS_OUT* = 1;
		RPC_C_STATS_PKTS_IN* = 2;
		RPC_C_STATS_PKTS_OUT* = 3;
		RPC_C_AUTHN_LEVEL_DEFAULT* = 0;
		RPC_C_AUTHN_LEVEL_NONE* = 1;
		RPC_C_AUTHN_LEVEL_CONNECT* = 2;
		RPC_C_AUTHN_LEVEL_CALL* = 3;
		RPC_C_AUTHN_LEVEL_PKT* = 4;
		RPC_C_AUTHN_LEVEL_PKT_INTEGRITY* = 5;
		RPC_C_AUTHN_LEVEL_PKT_PRIVACY* = 6;
		RPC_C_IMP_LEVEL_ANONYMOUS* = 1;
		RPC_C_IMP_LEVEL_IDENTIFY* = 2;
		RPC_C_IMP_LEVEL_IMPERSONATE* = 3;
		RPC_C_IMP_LEVEL_DELEGATE* = 4;
		RPC_C_PROTECT_LEVEL_DEFAULT* = 0;
		RPC_C_PROTECT_LEVEL_NONE* = 1;
		RPC_C_PROTECT_LEVEL_CONNECT* = 2;
		RPC_C_PROTECT_LEVEL_CALL* = 3;
		RPC_C_PROTECT_LEVEL_PKT* = 4;
		RPC_C_PROTECT_LEVEL_PKT_INTEGRITY* = 5;
		RPC_C_PROTECT_LEVEL_PKT_PRIVACY* = 6;
		RPC_C_AUTHN_NONE* = 0;
		RPC_C_AUTHN_DCE_PRIVATE* = 1;
		RPC_C_AUTHN_DCE_PUBLIC* = 2;
		RPC_C_AUTHN_DEC_PUBLIC* = 4;
		RPC_C_AUTHN_WINNT* = 10;
		RPC_C_AUTHN_DEFAULT* = -1;
		SEC_WINNT_AUTH_IDENTITY_ANSI* = 1;
		SEC_WINNT_AUTH_IDENTITY_UNICODE* = 2;
		RPC_C_AUTHZ_NONE* = 0;
		RPC_C_AUTHZ_NAME* = 1;
		RPC_C_AUTHZ_DCE* = 2;
		DCE_C_ERROR_STRING_LEN* = 256;
		RPC_C_EP_ALL_ELTS* = 0;
		RPC_C_EP_MATCH_BY_IF* = 1;
		RPC_C_EP_MATCH_BY_OBJ* = 2;
		RPC_C_EP_MATCH_BY_BOTH* = 3;
		RPC_C_VERS_ALL* = 1;
		RPC_C_VERS_COMPATIBLE* = 2;
		RPC_C_VERS_EXACT* = 3;
		RPC_C_VERS_MAJOR_ONLY* = 4;
		RPC_C_VERS_UPTO* = 5;
		RPC_C_MGMT_INQ_IF_IDS* = 0;
		RPC_C_MGMT_INQ_PRINC_NAME* = 1;
		RPC_C_MGMT_INQ_STATS* = 2;
		RPC_C_MGMT_IS_SERVER_LISTEN* = 3;
		RPC_C_MGMT_STOP_SERVER_LISTEN* = 4;
		RPC_C_PARM_MAX_PACKET_LENGTH* = 1;
		RPC_C_PARM_BUFFER_LENGTH* = 2;
		RPC_IF_AUTOLISTEN* = 1;
		RPC_IF_OLE* = 2;
		RPC_NCA_FLAGS_DEFAULT* = {};
		RPC_NCA_FLAGS_IDEMPOTENT* = {0};
		RPC_NCA_FLAGS_BROADCAST* = {1};
		RPC_NCA_FLAGS_MAYBE* = {2};
		RPCFLG_ASYNCHRONOUS* = {30};
		RPCFLG_INPUT_SYNCHRONOUS* = {29};
		RPC_FLAGS_VALID_BIT* = {15};
		TRANSPORT_TYPE_CN* = 1;
		TRANSPORT_TYPE_DG* = 2;
		TRANSPORT_TYPE_LPC* = 4;
		TRANSPORT_TYPE_WMSG* = 8;
		RPC_C_NS_SYNTAX_DEFAULT* = 0;
		RPC_C_NS_SYNTAX_DCE* = 3;
		RPC_C_PROFILE_DEFAULT_ELT* = 0;
		RPC_C_PROFILE_ALL_ELT* = 1;
		RPC_C_PROFILE_MATCH_BY_IF* = 2;
		RPC_C_PROFILE_MATCH_BY_MBR* = 3;
		RPC_C_PROFILE_MATCH_BY_BOTH* = 4;
		RPC_C_NS_DEFAULT_EXP_AGE* = -1;
		RPC_S_OK* = 0;
		RPC_S_INVALID_ARG* = 87;
		RPC_S_OUT_OF_MEMORY* = 14;
		RPC_S_OUT_OF_THREADS* = 164;
		RPC_S_INVALID_LEVEL* = 87;
		RPC_S_BUFFER_TOO_SMALL* = 122;
		RPC_S_INVALID_SECURITY_DESC* = 1338;
		RPC_S_ACCESS_DENIED* = 5;
		RPC_S_SERVER_OUT_OF_MEMORY* = 1130;
		RPC_X_NO_MEMORY* = 14;
		RPC_X_INVALID_BOUND* = 1734;
		RPC_X_INVALID_TAG* = 1733;
		RPC_X_ENUM_VALUE_TOO_LARGE* = 1781;
		RPC_X_SS_CONTEXT_MISMATCH* = 6;
		RPC_X_INVALID_BUFFER* = 1784;
		NDR_CHAR_REP_MASK* = 15;
		NDR_INT_REP_MASK* = 240;
		NDR_FLOAT_REP_MASK* = 65280;
		NDR_LITTLE_ENDIAN* = 16;
		NDR_BIG_ENDIAN* = 0;
		NDR_IEEE_FLOAT* = 0;
		NDR_VAX_FLOAT* = 256;
		NDR_ASCII_CHAR* = 0;
		NDR_EBCDIC_CHAR* = 1;
		NDR_LOCAL_DATA_REPRESENTATION* = 16;
		NDR_LOCAL_ENDIAN* = 16;
		cbNDRContext* = 20;
		USER_MARSHAL_FC_BYTE* = 1;
		USER_MARSHAL_FC_CHAR* = 2;
		USER_MARSHAL_FC_SMALL* = 3;
		USER_MARSHAL_FC_USMALL* = 4;
		USER_MARSHAL_FC_WCHAR* = 5;
		USER_MARSHAL_FC_SHORT* = 6;
		USER_MARSHAL_FC_USHORT* = 7;
		USER_MARSHAL_FC_LONG* = 8;
		USER_MARSHAL_FC_ULONG* = 9;
		USER_MARSHAL_FC_FLOAT* = 10;
		USER_MARSHAL_FC_HYPER* = 11;
		USER_MARSHAL_FC_DOUBLE* = 12;

	CONST (* enumerations *)
		XLAT_SERVER* = 1;
		XLAT_CLIENT* = 2;
		STUB_UNMARSHAL* = 0;
		STUB_CALL_SERVER* = 1;
		STUB_MARSHAL* = 2;
		STUB_CALL_SERVER_NO_HRESULT* = 3;
		PROXY_CALCSIZE* = 0;
		PROXY_GETBUFFER* = 1;
		PROXY_MARSHAL* = 2;
		PROXY_SENDRECEIVE* = 3;
		PROXY_UNMARSHAL* = 4;

	TYPE
		RPC_STATUS* = INTEGER;
		RPC_BINDING_VECTOR* = RECORD [align8]
			Count*: INTEGER;
			BindingH*: ARRAY [untagged] 1 OF WinApi.PtrVoid;
		END;
		PtrRPC_BINDING_VECTOR* = POINTER TO RPC_BINDING_VECTOR;
		rpc_binding_vector_t* = RPC_BINDING_VECTOR; (*m*)
		Ptrrpc_binding_vector_t* = PtrRPC_BINDING_VECTOR;
		UUID_VECTOR* = RECORD [align8]
			Count*: INTEGER;
			Uuid*: ARRAY [untagged] 1 OF POINTER TO (*?*) ARRAY [untagged] OF COM.GUID;
		END;
		PtrUUID_VECTOR* = POINTER TO UUID_VECTOR;
		uuid_vector_t* = UUID_VECTOR; (*m*)
		Ptruuid_vector_t* = PtrUUID_VECTOR;
		RPC_IF_ID* = RECORD [align8]
			Uuid*: COM.GUID;
			VersMajor*: SHORTINT;
			VersMinor*: SHORTINT;
		END;
		PtrRPC_IF_ID* = POINTER TO RPC_IF_ID;
		RPC_PROTSEQ_VECTORA* = RECORD [align8]
			Count*: INTEGER;
			Protseq*: ARRAY [untagged] 1 OF WinApi.PtrSTR;
		END;
		PtrRPC_PROTSEQ_VECTORA* = POINTER TO RPC_PROTSEQ_VECTORA;
		RPC_PROTSEQ_VECTORW* = RECORD [align8]
			Count*: INTEGER;
			Protseq*: ARRAY [untagged] 1 OF POINTER TO (*?*) ARRAY [untagged] OF SHORTINT;
		END;
		PtrRPC_PROTSEQ_VECTORW* = POINTER TO RPC_PROTSEQ_VECTORW;
		RPC_PROTSEQ_VECTOR* = RPC_PROTSEQ_VECTORA; (*m*)
		PtrRPC_PROTSEQ_VECTOR* = PtrRPC_PROTSEQ_VECTORA;
		RPC_OBJECT_INQ_FN* = PROCEDURE(VAR [nil] ObjectUuid: COM.GUID; VAR [nil] TypeUuid: COM.GUID; VAR [nil] Status: RPC_STATUS);
		RPC_IF_CALLBACK_FN* = PROCEDURE(InterfaceUuid: WinApi.PtrVoid; Context: WinApi.PtrVoid): RPC_STATUS;
		RPC_STATS_VECTOR* = RECORD [align8]
			Count*: INTEGER;
			Stats*: ARRAY [untagged] 1 OF INTEGER;
		END;
		PtrRPC_STATS_VECTOR* = POINTER TO RPC_STATS_VECTOR;
		RPC_IF_ID_VECTOR* = RECORD [align8]
			Count*: INTEGER;
			IfId*: ARRAY [untagged] 1 OF PtrRPC_IF_ID;
		END;
		PtrRPC_IF_ID_VECTOR* = POINTER TO RPC_IF_ID_VECTOR;
		SEC_WINNT_AUTH_IDENTITY_W* = RECORD [align8]
			User*: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT;
			UserLength*: INTEGER;
			Domain*: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT;
			DomainLength*: INTEGER;
			Password*: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT;
			PasswordLength*: INTEGER;
			Flags*: SET;
		END;
		PtrSEC_WINNT_AUTH_IDENTITY_W* = POINTER TO SEC_WINNT_AUTH_IDENTITY_W;
		SEC_WINNT_AUTH_IDENTITY_A* = RECORD [align8]
			User*: WinApi.PtrSTR;
			UserLength*: INTEGER;
			Domain*: WinApi.PtrSTR;
			DomainLength*: INTEGER;
			Password*: WinApi.PtrSTR;
			PasswordLength*: INTEGER;
			Flags*: SET;
		END;
		PtrSEC_WINNT_AUTH_IDENTITY_A* = POINTER TO SEC_WINNT_AUTH_IDENTITY_A;
		SEC_WINNT_AUTH_IDENTITY* = SEC_WINNT_AUTH_IDENTITY_A; (*m*)
		PtrSEC_WINNT_AUTH_IDENTITY* = PtrSEC_WINNT_AUTH_IDENTITY_A;
		_SEC_WINNT_AUTH_IDENTITY* = SEC_WINNT_AUTH_IDENTITY_A; (*m*)
		Ptr_SEC_WINNT_AUTH_IDENTITY* = PtrSEC_WINNT_AUTH_IDENTITY_A;
		RPC_AUTH_KEY_RETRIEVAL_FN* = PROCEDURE [ms64] (Arg: WinApi.PtrVoid; VAR [nil] ServerPrincName: SHORTINT; KeyVer: INTEGER; VAR [nil] Key: WinApi.PtrVoid; VAR [nil] Status: RPC_STATUS);
		RPC_CLIENT_INFORMATION1* = RECORD [align8]
			UserName*: WinApi.PtrSTR;
			ComputerName*: WinApi.PtrSTR;
			Privilege*: SHORTINT;
			AuthFlags*: INTEGER;
		END;
		PtrRPC_CLIENT_INFORMATION1* = POINTER TO RPC_CLIENT_INFORMATION1;
		RPC_MGMT_AUTHORIZATION_FN* = PROCEDURE [ms64] (ClientBinding: WinApi.PtrVoid; RequestedMgmtOperation: INTEGER; VAR [nil] Status: RPC_STATUS): INTEGER;
		RPC_VERSION* = RECORD [align8]
			MajorVersion*: SHORTINT;
			MinorVersion*: SHORTINT;
		END;
		PtrRPC_VERSION* = POINTER TO RPC_VERSION;
		RPC_SYNTAX_IDENTIFIER* = RECORD [align8]
			SyntaxGUID*: COM.GUID;
			SyntaxVersion*: RPC_VERSION;
		END;
		PtrRPC_SYNTAX_IDENTIFIER* = POINTER TO RPC_SYNTAX_IDENTIFIER;
		RPC_MESSAGE* = RECORD [align8]
			Handle*: WinApi.PtrVoid;
			DataRepresentation*: INTEGER;
			Buffer*: WinApi.PtrVoid;
			BufferLength*: INTEGER;
			ProcNum*: INTEGER;
			TransferSyntax*: PtrRPC_SYNTAX_IDENTIFIER;
			RpcInterfaceInformation*: WinApi.PtrVoid;
			ReservedForRuntime*: WinApi.PtrVoid;
			ManagerEpv*: WinApi.PtrVoid;
			ImportContext*: WinApi.PtrVoid;
			RpcFlags*: INTEGER;
		END;
		PtrRPC_MESSAGE* = POINTER TO RPC_MESSAGE;
		RPC_FORWARD_FUNCTION* = PROCEDURE(VAR [nil] InterfaceId: COM.GUID; VAR [nil] InterfaceVersion: RPC_VERSION; VAR [nil] ObjectId: COM.GUID; Rpcpro: WinApi.PtrSTR; VAR [nil] ppDestEndpoint: WinApi.PtrVoid): RPC_STATUS;
		RPC_DISPATCH_FUNCTION* = PROCEDURE [ms64] (VAR [nil] Message: RPC_MESSAGE);
		RPC_DISPATCH_TABLE* = RECORD [align8]
			DispatchTableCount*: INTEGER;
			DispatchTable*: POINTER TO (*?*) ARRAY [untagged] OF RPC_DISPATCH_FUNCTION;
			Reserved*: INTEGER;
		END;
		PtrRPC_DISPATCH_TABLE* = POINTER TO RPC_DISPATCH_TABLE;
		RPC_PROTSEQ_ENDPOINT* = RECORD [align8]
			RpcProtocolSequence*: WinApi.PtrSTR;
			Endpoint*: WinApi.PtrSTR;
		END;
		PtrRPC_PROTSEQ_ENDPOINT* = POINTER TO RPC_PROTSEQ_ENDPOINT;
		RPC_SERVER_INTERFACE* = RECORD [align8]
			Length*: INTEGER;
			InterfaceId*: RPC_SYNTAX_IDENTIFIER;
			TransferSyntax*: RPC_SYNTAX_IDENTIFIER;
			DispatchTable*: PtrRPC_DISPATCH_TABLE;
			RpcProtseqEndpointCount*: INTEGER;
			RpcProtseqEndpoint*: PtrRPC_PROTSEQ_ENDPOINT;
			DefaultManagerEpv*: WinApi.PtrVoid;
			InterpreterInfo*: WinApi.PtrVoid;
		END;
		PtrRPC_SERVER_INTERFACE* = POINTER TO RPC_SERVER_INTERFACE;
		RPC_CLIENT_INTERFACE* = RECORD [align8]
			Length*: INTEGER;
			InterfaceId*: RPC_SYNTAX_IDENTIFIER;
			TransferSyntax*: RPC_SYNTAX_IDENTIFIER;
			DispatchTable*: PtrRPC_DISPATCH_TABLE;
			RpcProtseqEndpointCount*: INTEGER;
			RpcProtseqEndpoint*: PtrRPC_PROTSEQ_ENDPOINT;
			Reserved*: INTEGER;
			InterpreterInfo*: WinApi.PtrVoid;
		END;
		PtrRPC_CLIENT_INTERFACE* = POINTER TO RPC_CLIENT_INTERFACE;
		RPC_RUNDOWN* = PROCEDURE [ms64] (AssociationContext: WinApi.PtrVoid);
		RPC_TRANSFER_SYNTAX* = RECORD [align8]
			Uuid*: COM.GUID;
			VersMajor*: SHORTINT;
			VersMinor*: SHORTINT;
		END;
		PtrRPC_TRANSFER_SYNTAX* = POINTER TO RPC_TRANSFER_SYNTAX;
		RPC_IMPORT_CONTEXT_P* = RECORD [align8]
			LookupContext*: WinApi.PtrVoid;
			ProposedHandle*: WinApi.PtrVoid;
			Bindings*: PtrRPC_BINDING_VECTOR;
		END;
		PtrRPC_IMPORT_CONTEXT_P* = POINTER TO RPC_IMPORT_CONTEXT_P;
		boolean* = SHORTCHAR;
		NDR_SCONTEXT* = POINTER TO RECORD [align8]
			pad*: ARRAY [untagged] 2 OF WinApi.PtrVoid;
			userContext*: WinApi.PtrVoid;
		END;
		NDR_RUNDOWN* = PROCEDURE [ms64] (context: WinApi.PtrVoid);
		SCONTEXT_QUEUE* = RECORD [align8]
			NumberOfObjects*: INTEGER;
			ArrayOfObjects*: POINTER TO (*?*) ARRAY [untagged] OF NDR_SCONTEXT;
		END;
		PtrSCONTEXT_QUEUE* = POINTER TO SCONTEXT_QUEUE;
		error_status_t* = INTEGER;
		RPC_LENGTH* = INTEGER;
		PtrMIDL_STUB_MESSAGE* = POINTER TO MIDL_STUB_MESSAGE;
		EXPR_EVAL* = PROCEDURE [ms64] (p0: PtrMIDL_STUB_MESSAGE);
		ARRAY_INFO* = RECORD [align8]
			Dimension*: INTEGER;
			BufferConformanceMark*: POINTER TO (*?*) ARRAY [untagged] OF INTEGER;
			BufferVarianceMark*: POINTER TO (*?*) ARRAY [untagged] OF INTEGER;
			MaxCountArray*: POINTER TO (*?*) ARRAY [untagged] OF INTEGER;
			OffsetArray*: POINTER TO (*?*) ARRAY [untagged] OF INTEGER;
			ActualCountArray*: POINTER TO (*?*) ARRAY [untagged] OF INTEGER;
		END;
		PtrARRAY_INFO* = POINTER TO ARRAY_INFO;
		PtrMIDL_STUB_DESC* = POINTER TO MIDL_STUB_DESC;
		PtrFULL_PTR_XLAT_TABLES* = POINTER TO FULL_PTR_XLAT_TABLES;
		MIDL_STUB_MESSAGE* = RECORD [align8]
			RpcMsg*: PtrRPC_MESSAGE;
			Buffer*: WinApi.PtrSTR;
			BufferStart*: WinApi.PtrSTR;
			BufferEnd*: WinApi.PtrSTR;
			BufferMark*: WinApi.PtrSTR;
			BufferLength*: INTEGER;
			MemorySize*: INTEGER;
			Memory*: WinApi.PtrSTR;
			IsClient*, Pad*: SHORTCHAR; uFlags2*: SHORTINT;
			ReuseBuffer*: INTEGER;
			AllocAllNodesMemory*: WinApi.PtrSTR;
			AllocAllNodesMemoryEnd*: WinApi.PtrSTR;
			IgnoreEmbeddedPointers*: INTEGER;
			PointerBufferMark*: WinApi.PtrSTR;
			fBufferValid*: SHORTCHAR;
			uFlags*: SHORTCHAR; UniquePtrCount*: SHORTINT;
			MaxCount*: WinApi.PtrVoid;
			Offset*: INTEGER;
			ActualCount*: INTEGER;
			pfnAllocate*: PROCEDURE [ms64] (p0: WinApi.PtrVoid): WinApi.PtrVoid;
			pfnFree*: PROCEDURE [ms64] (p0: WinApi.PtrVoid);
			StackTop*: WinApi.PtrSTR;
			pPresentedType*: WinApi.PtrSTR;
			pTransmitType*: WinApi.PtrSTR;
			SavedHandle*: WinApi.PtrVoid;
			StubDesc*: PtrMIDL_STUB_DESC;
			FullPtrXlatTables*: PtrFULL_PTR_XLAT_TABLES;
			FullPtrRefId*: INTEGER;
			PointerLength*: INTEGER;
			fBits0*: SET;
			(* fInDontFree*: LONGINT; (1 bits) *)
			(* fDontCallFreeInst*: LONGINT; (1 bits) *)
			(* fInOnlyParam*: LONGINT; (1 bits) *)
			(* fHasReturn*: LONGINT; (1 bits) *)
			dwDestContext*: INTEGER;
			pvDestContext*: WinApi.PtrVoid;
			SavedContextHandles*: POINTER TO (*?*) ARRAY [untagged] OF NDR_SCONTEXT;
			ParamNumber*: INTEGER;
			pRpcChannelBuffer*: WinOle.IRpcChannelBuffer;
			pArrayInfo*: PtrARRAY_INFO;
			SizePtrCountArray*: POINTER TO (*?*) ARRAY [untagged] OF INTEGER;
			SizePtrOffsetArray*: POINTER TO (*?*) ARRAY [untagged] OF INTEGER;
			SizePtrLengthArray*: POINTER TO (*?*) ARRAY [untagged] OF INTEGER;
			pArgQueue*: WinApi.PtrVoid;
			dwStubPhase*: INTEGER;
			LowStackMark*, pAsyncMsg*, pCorrInfo*, pCorrMemory*, pMemoryList*, pCSInfo*: WinApi.PtrVoid;
			ConformanceMark*, VarianceMark*, Unused*, pContext*, pUserMarshalList*: WinApi.PtrVoid;
			Reserved51_2*, Reserved51_3*, Reserved51_4*, Reserved51_5*: WinApi.PtrVoid;
		END;
		GENERIC_BINDING_ROUTINE* = PROCEDURE [ms64] (p0: WinApi.PtrVoid): WinApi.PtrVoid;
		GENERIC_UNBIND_ROUTINE* = PROCEDURE [ms64] (p0: WinApi.PtrVoid; p1: WinApi.PtrSTR);
		GENERIC_BINDING_ROUTINE_PAIR* = RECORD [align8]
			pfnBind*: GENERIC_BINDING_ROUTINE;
			pfnUnbind*: GENERIC_UNBIND_ROUTINE;
		END;
		PtrGENERIC_BINDING_ROUTINE_PAIR* = POINTER TO GENERIC_BINDING_ROUTINE_PAIR;
		GENERIC_BINDING_INFO* = RECORD [align8]
			pObj*: WinApi.PtrVoid;
			Size*: INTEGER;
			pfnBind*: GENERIC_BINDING_ROUTINE;
			pfnUnbind*: GENERIC_UNBIND_ROUTINE;
		END;
		PtrGENERIC_BINDING_INFO* = POINTER TO GENERIC_BINDING_INFO;
		XMIT_HELPER_ROUTINE* = PROCEDURE [ms64] (VAR [nil] p0: MIDL_STUB_MESSAGE);
		XMIT_ROUTINE_QUINTUPLE* = RECORD [align8]
			pfnTranslateToXmit*: XMIT_HELPER_ROUTINE;
			pfnTranslateFromXmit*: XMIT_HELPER_ROUTINE;
			pfnFreeXmit*: XMIT_HELPER_ROUTINE;
			pfnFreeInst*: XMIT_HELPER_ROUTINE;
		END;
		PtrXMIT_ROUTINE_QUINTUPLE* = POINTER TO XMIT_ROUTINE_QUINTUPLE;
		USER_MARSHAL_SIZING_ROUTINE* = PROCEDURE [ms64] (VAR [nil] p0: INTEGER; p1: INTEGER; p2: WinApi.PtrVoid): INTEGER;
		USER_MARSHAL_MARSHALLING_ROUTINE* = PROCEDURE [ms64] (VAR [nil] p0: INTEGER; p1: WinApi.PtrSTR; p2: WinApi.PtrVoid): WinApi.PtrSTR;
		USER_MARSHAL_UNMARSHALLING_ROUTINE* = PROCEDURE [ms64] (VAR [nil] p0: INTEGER; p1: WinApi.PtrSTR; p2: WinApi.PtrVoid): WinApi.PtrSTR;
		USER_MARSHAL_FREEING_ROUTINE* = PROCEDURE [ms64] (VAR [nil] p0: INTEGER; p1: WinApi.PtrVoid);
		USER_MARSHAL_ROUTINE_QUADRUPLE* = RECORD [align8]
			pfnBufferSize*: USER_MARSHAL_SIZING_ROUTINE;
			pfnMarshall*: USER_MARSHAL_MARSHALLING_ROUTINE;
			pfnUnmarshall*: USER_MARSHAL_UNMARSHALLING_ROUTINE;
			pfnFree*: USER_MARSHAL_FREEING_ROUTINE;
		END;
		PtrUSER_MARSHAL_ROUTINE_QUADRUPLE* = POINTER TO USER_MARSHAL_ROUTINE_QUADRUPLE;
		USER_MARSHAL_CB* = RECORD [align8]
			Flags*: SET;
			pStubMsg*: PtrMIDL_STUB_MESSAGE;
			pReserve*: WinApi.PtrSTR;
		END;
		MALLOC_FREE_STRUCT* = RECORD [align8]
			pfnAllocate*: PROCEDURE [ms64] (p0: WinApi.PtrVoid): WinApi.PtrVoid;
			pfnFree*: PROCEDURE [ms64] (p0: WinApi.PtrVoid);
		END;
		PtrMALLOC_FREE_STRUCT* = POINTER TO MALLOC_FREE_STRUCT;
		COMM_FAULT_OFFSETS* = RECORD [align8]
			CommOffset*: SHORTINT;
			FaultOffset*: SHORTINT;
		END;
		PtrCOMM_FAULT_OFFSETS* = POINTER TO COMM_FAULT_OFFSETS;
		MIDL_STUB_DESC* = RECORD [align8]
			RpcInterfaceInformation*: WinApi.PtrVoid;
			pfnAllocate*: PROCEDURE [ms64] (p0: WinApi.PtrVoid): WinApi.PtrVoid;
			pfnFree*: PROCEDURE [ms64] (p0: WinApi.PtrVoid);
			IMPLICIT_HANDLE_INFO*: RECORD [union]
				pAutoHandle*: POINTER TO (*?*) ARRAY [untagged] OF WinApi.PtrVoid;
				pPrimitiveHandle*: POINTER TO (*?*) ARRAY [untagged] OF WinApi.PtrVoid;
				pGenericBindingInfo*: PtrGENERIC_BINDING_INFO;
			END;
			apfnNdrRundownRoutines*: POINTER TO (*?*) ARRAY [untagged] OF NDR_RUNDOWN;
			aGenericBindingRoutinePairs*: PtrGENERIC_BINDING_ROUTINE_PAIR;
			apfnExprEval*: POINTER TO (*?*) ARRAY [untagged] OF EXPR_EVAL;
			aXmitQuintuple*: PtrXMIT_ROUTINE_QUINTUPLE;
			pFormatTypes*: WinApi.PtrSTR;
			fCheckBounds*: INTEGER;
			Version*: INTEGER;
			pMallocFreeStruct*: PtrMALLOC_FREE_STRUCT;
			MIDLVersion*: INTEGER;
			CommFaultOffsets*: PtrCOMM_FAULT_OFFSETS;
			aUserMarshalQuadruple*: PtrUSER_MARSHAL_ROUTINE_QUADRUPLE;
			Reserved1*: WinApi.PtrVoid;
			Reserved2*: WinApi.PtrVoid;
			Reserved3*: WinApi.PtrVoid;
			Reserved4*: WinApi.PtrVoid;
			Reserved5*: WinApi.PtrVoid;
		END;
		STUB_THUNK* = PROCEDURE [ms64] (VAR [nil] p0: MIDL_STUB_MESSAGE);
		SERVER_ROUTINE* = PROCEDURE [ms64] (): INTEGER;
		MIDL_SERVER_INFO* = RECORD [align8]
			pStubDesc*: PtrMIDL_STUB_DESC;
			DispatchTable*: POINTER TO (*?*) ARRAY [untagged] OF SERVER_ROUTINE;
			ProcString*: WinApi.PtrSTR;
			FmtStringOffset*: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT;
			ThunkTable*: POINTER TO (*?*) ARRAY [untagged] OF STUB_THUNK;
		END;
		PtrMIDL_SERVER_INFO* = POINTER TO MIDL_SERVER_INFO;
		MIDL_STUBLESS_PROXY_INFO* = RECORD [align8]
			pStubDesc*: PtrMIDL_STUB_DESC;
			ProcFormatString*: WinApi.PtrSTR;
			FormatStringOffset*: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT;
		END;
		PtrMIDL_STUBLESS_PROXY_INFO* = POINTER TO MIDL_STUBLESS_PROXY_INFO;
		CLIENT_CALL_RETURN* = RECORD [union]
			Pointer*: WinApi.PtrVoid;
			Simple*: INTEGER;
		END;
		XLAT_SIDE* = INTEGER;
		PtrFULL_PTR_TO_REFID_ELEMENT* = POINTER TO FULL_PTR_TO_REFID_ELEMENT;
		FULL_PTR_TO_REFID_ELEMENT* = RECORD [align8]
			Next*: PtrFULL_PTR_TO_REFID_ELEMENT;
			Pointer*: WinApi.PtrVoid;
			RefId*: INTEGER;
			State*: SHORTCHAR;
		END;
		FULL_PTR_XLAT_TABLES* = RECORD [align8]
			RefIdToPointer*: RECORD [align8]
				XlatTable*: POINTER TO (*?*) ARRAY [untagged] OF WinApi.PtrVoid;
				StateTable*: WinApi.PtrSTR;
				NumberOfEntries*: INTEGER;
			END;
			PointerToRefId*: RECORD [align8]
				XlatTable*: POINTER TO (*?*) ARRAY [untagged] OF PtrFULL_PTR_TO_REFID_ELEMENT;
				NumberOfBuckets*: INTEGER;
				HashMask*: INTEGER;
			END;
			NextRefId*: INTEGER;
			XlatSide*: XLAT_SIDE;
		END;
		STUB_PHASE* = INTEGER;
		PROXY_PHASE* = INTEGER;
		RPC_CLIENT_ALLOC* = PROCEDURE(Size: INTEGER): WinApi.PtrVoid;
		RPC_CLIENT_FREE* = PROCEDURE(Ptr: WinApi.PtrVoid);

	PROCEDURE [ms64] RpcBindingCopy* (SourceBinding: WinApi.PtrVoid; VAR [nil] DestinationBinding: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcBindingCopy;*)

	PROCEDURE [ms64] RpcBindingFree* (VAR [nil] Binding: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcBindingFree;*)

	PROCEDURE [ms64] RpcBindingFromStringBindingA* (StringBinding: WinApi.PtrSTR; VAR [nil] Binding: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcBindingFromStringBindingA;*)

	PROCEDURE [ms64] RpcBindingFromStringBindingW* (VAR [nil] StringBinding: SHORTINT; VAR [nil] Binding: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcBindingFromStringBindingW;*)

	PROCEDURE [ms64] RpcBindingFromStringBinding* ["RpcBindingFromStringBindingA"] (StringBinding: WinApi.PtrSTR; VAR [nil] Binding: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcBindingFromStringBinding;*)

	PROCEDURE [ms64] RpcBindingInqObject* (Binding: WinApi.PtrVoid; VAR [nil] ObjectUuid: COM.GUID): RPC_STATUS;
	(*END RpcBindingInqObject;*)

	PROCEDURE [ms64] RpcBindingReset* (Binding: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcBindingReset;*)

	PROCEDURE [ms64] RpcBindingSetObject* (Binding: WinApi.PtrVoid; VAR [nil] ObjectUuid: COM.GUID): RPC_STATUS;
	(*END RpcBindingSetObject;*)

	PROCEDURE [ms64] RpcMgmtInqDefaultProtectLevel* (AuthnSvc: INTEGER; VAR [nil] AuthnLevel: INTEGER): RPC_STATUS;
	(*END RpcMgmtInqDefaultProtectLevel;*)

	PROCEDURE [ms64] RpcBindingToStringBindingA* (Binding: WinApi.PtrVoid; VAR [nil] StringBinding: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcBindingToStringBindingA;*)

	PROCEDURE [ms64] RpcBindingToStringBindingW* (Binding: WinApi.PtrVoid; VAR [nil] StringBinding: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT): RPC_STATUS;
	(*END RpcBindingToStringBindingW;*)

	PROCEDURE [ms64] RpcBindingToStringBinding* ["RpcBindingToStringBindingA"] (Binding: WinApi.PtrVoid; VAR [nil] StringBinding: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcBindingToStringBinding;*)

	PROCEDURE [ms64] RpcBindingVectorFree* (VAR [nil] BindingVector: PtrRPC_BINDING_VECTOR): RPC_STATUS;
	(*END RpcBindingVectorFree;*)

	PROCEDURE [ms64] RpcStringBindingComposeA* (ObjUuid: WinApi.PtrSTR; Protseq: WinApi.PtrSTR; NetworkAddr: WinApi.PtrSTR; Endpoint: WinApi.PtrSTR; Options: WinApi.PtrSTR; VAR [nil] StringBinding: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcStringBindingComposeA;*)

	PROCEDURE [ms64] RpcStringBindingComposeW* (VAR [nil] ObjUuid: SHORTINT; VAR [nil] Protseq: SHORTINT; VAR [nil] NetworkAddr: SHORTINT; VAR [nil] Endpoint: SHORTINT; VAR [nil] Options: SHORTINT; VAR [nil] StringBinding: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT): RPC_STATUS;
	(*END RpcStringBindingComposeW;*)

	PROCEDURE [ms64] RpcStringBindingCompose* ["RpcStringBindingComposeA"] (ObjUuid: WinApi.PtrSTR; Protseq: WinApi.PtrSTR; NetworkAddr: WinApi.PtrSTR; Endpoint: WinApi.PtrSTR; Options: WinApi.PtrSTR; VAR [nil] StringBinding: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcStringBindingCompose;*)

	PROCEDURE [ms64] RpcStringBindingParseA* (StringBinding: WinApi.PtrSTR; VAR [nil] ObjUuid: WinApi.PtrSTR; VAR [nil] Protseq: WinApi.PtrSTR; VAR [nil] NetworkAddr: WinApi.PtrSTR; VAR [nil] Endpoint: WinApi.PtrSTR; VAR [nil] NetworkOptions: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcStringBindingParseA;*)

	PROCEDURE [ms64] RpcStringBindingParseW* (VAR [nil] StringBinding: SHORTINT; VAR [nil] ObjUuid: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT; VAR [nil] Protseq: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT; VAR [nil] NetworkAddr: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT; VAR [nil] Endpoint: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT; VAR [nil] NetworkOptions: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT): RPC_STATUS;
	(*END RpcStringBindingParseW;*)

	PROCEDURE [ms64] RpcStringBindingParse* ["RpcStringBindingParseA"] (StringBinding: WinApi.PtrSTR; VAR [nil] ObjUuid: WinApi.PtrSTR; VAR [nil] Protseq: WinApi.PtrSTR; VAR [nil] NetworkAddr: WinApi.PtrSTR; VAR [nil] Endpoint: WinApi.PtrSTR; VAR [nil] NetworkOptions: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcStringBindingParse;*)

	PROCEDURE [ms64] RpcStringFreeA* (VAR [nil] String: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcStringFreeA;*)

	PROCEDURE [ms64] RpcStringFreeW* (VAR [nil] String: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT): RPC_STATUS;
	(*END RpcStringFreeW;*)

	PROCEDURE [ms64] RpcStringFree* ["RpcStringFreeA"] (VAR [nil] String: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcStringFree;*)

	PROCEDURE [ms64] RpcIfInqId* (RpcIfHandle: WinApi.PtrVoid; VAR [nil] RpcIfId: RPC_IF_ID): RPC_STATUS;
	(*END RpcIfInqId;*)

	PROCEDURE [ms64] RpcNetworkIsProtseqValidA* (Protseq: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNetworkIsProtseqValidA;*)

	PROCEDURE [ms64] RpcNetworkIsProtseqValidW* (VAR [nil] Protseq: SHORTINT): RPC_STATUS;
	(*END RpcNetworkIsProtseqValidW;*)

	PROCEDURE [ms64] RpcNetworkIsProtseqValid* ["RpcNetworkIsProtseqValidA"] (Protseq: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNetworkIsProtseqValid;*)

	PROCEDURE [ms64] RpcMgmtInqComTimeout* (Binding: WinApi.PtrVoid; VAR [nil] Timeout: INTEGER): RPC_STATUS;
	(*END RpcMgmtInqComTimeout;*)

	PROCEDURE [ms64] RpcMgmtSetComTimeout* (Binding: WinApi.PtrVoid; Timeout: INTEGER): RPC_STATUS;
	(*END RpcMgmtSetComTimeout;*)

	PROCEDURE [ms64] RpcMgmtSetCancelTimeout* (Timeout: INTEGER): RPC_STATUS;
	(*END RpcMgmtSetCancelTimeout;*)

	PROCEDURE [ms64] RpcNetworkInqProtseqsA* (VAR [nil] ProtseqVector: PtrRPC_PROTSEQ_VECTORA): RPC_STATUS;
	(*END RpcNetworkInqProtseqsA;*)

	PROCEDURE [ms64] RpcNetworkInqProtseqsW* (VAR [nil] ProtseqVector: PtrRPC_PROTSEQ_VECTORW): RPC_STATUS;
	(*END RpcNetworkInqProtseqsW;*)

	PROCEDURE [ms64] RpcNetworkInqProtseqs* ["RpcNetworkInqProtseqsA"] (VAR [nil] ProtseqVector: PtrRPC_PROTSEQ_VECTORA): RPC_STATUS;
	(*END RpcNetworkInqProtseqs;*)

	PROCEDURE [ms64] RpcObjectInqType* (VAR [nil] ObjUuid: COM.GUID; VAR [nil] TypeUuid: COM.GUID): RPC_STATUS;
	(*END RpcObjectInqType;*)

	PROCEDURE [ms64] RpcObjectSetInqFn* (InquiryFn: RPC_OBJECT_INQ_FN): RPC_STATUS;
	(*END RpcObjectSetInqFn;*)

	PROCEDURE [ms64] RpcObjectSetType* (VAR [nil] ObjUuid: COM.GUID; VAR [nil] TypeUuid: COM.GUID): RPC_STATUS;
	(*END RpcObjectSetType;*)

	PROCEDURE [ms64] RpcProtseqVectorFreeA* (VAR [nil] ProtseqVector: PtrRPC_PROTSEQ_VECTORA): RPC_STATUS;
	(*END RpcProtseqVectorFreeA;*)

	PROCEDURE [ms64] RpcProtseqVectorFreeW* (VAR [nil] ProtseqVector: PtrRPC_PROTSEQ_VECTORW): RPC_STATUS;
	(*END RpcProtseqVectorFreeW;*)

	PROCEDURE [ms64] RpcProtseqVectorFree* ["RpcProtseqVectorFreeA"] (VAR [nil] ProtseqVector: PtrRPC_PROTSEQ_VECTORA): RPC_STATUS;
	(*END RpcProtseqVectorFree;*)

	PROCEDURE [ms64] RpcServerInqBindings* (VAR [nil] BindingVector: PtrRPC_BINDING_VECTOR): RPC_STATUS;
	(*END RpcServerInqBindings;*)

	PROCEDURE [ms64] RpcServerInqIf* (IfSpec: WinApi.PtrVoid; VAR [nil] MgrTypeUuid: COM.GUID; VAR [nil] MgrEpv: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcServerInqIf;*)

	PROCEDURE [ms64] RpcServerListen* (MinimumCallThreads: INTEGER; MaxCalls: INTEGER; DontWait: INTEGER): RPC_STATUS;
	(*END RpcServerListen;*)

	PROCEDURE [ms64] RpcServerRegisterIf* (IfSpec: WinApi.PtrVoid; VAR [nil] MgrTypeUuid: COM.GUID; MgrEpv: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcServerRegisterIf;*)

	PROCEDURE [ms64] RpcServerUnregisterIf* (IfSpec: WinApi.PtrVoid; VAR [nil] MgrTypeUuid: COM.GUID; WaitForCallsToComplete: INTEGER): RPC_STATUS;
	(*END RpcServerUnregisterIf;*)

	PROCEDURE [ms64] RpcServerUseAllProtseqs* (MaxCalls: INTEGER; SecurityDescriptor: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcServerUseAllProtseqs;*)

	PROCEDURE [ms64] RpcServerUseAllProtseqsIf* (MaxCalls: INTEGER; IfSpec: WinApi.PtrVoid; SecurityDescriptor: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcServerUseAllProtseqsIf;*)

	PROCEDURE [ms64] RpcServerUseProtseqA* (Protseq: WinApi.PtrSTR; MaxCalls: INTEGER; SecurityDescriptor: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcServerUseProtseqA;*)

	PROCEDURE [ms64] RpcServerUseProtseqW* (VAR [nil] Protseq: SHORTINT; MaxCalls: INTEGER; SecurityDescriptor: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcServerUseProtseqW;*)

	PROCEDURE [ms64] RpcServerUseProtseq* ["RpcServerUseProtseqA"] (Protseq: WinApi.PtrSTR; MaxCalls: INTEGER; SecurityDescriptor: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcServerUseProtseq;*)

	PROCEDURE [ms64] RpcServerUseProtseqEpA* (Protseq: WinApi.PtrSTR; MaxCalls: INTEGER; Endpoint: WinApi.PtrSTR; SecurityDescriptor: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcServerUseProtseqEpA;*)

	PROCEDURE [ms64] RpcServerUseProtseqEpW* (VAR [nil] Protseq: SHORTINT; MaxCalls: INTEGER; VAR [nil] Endpoint: SHORTINT; SecurityDescriptor: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcServerUseProtseqEpW;*)

	PROCEDURE [ms64] RpcServerUseProtseqEp* ["RpcServerUseProtseqEpA"] (Protseq: WinApi.PtrSTR; MaxCalls: INTEGER; Endpoint: WinApi.PtrSTR; SecurityDescriptor: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcServerUseProtseqEp;*)

	PROCEDURE [ms64] RpcServerUseProtseqIfA* (Protseq: WinApi.PtrSTR; MaxCalls: INTEGER; IfSpec: WinApi.PtrVoid; SecurityDescriptor: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcServerUseProtseqIfA;*)

	PROCEDURE [ms64] RpcServerUseProtseqIfW* (VAR [nil] Protseq: SHORTINT; MaxCalls: INTEGER; IfSpec: WinApi.PtrVoid; SecurityDescriptor: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcServerUseProtseqIfW;*)

	PROCEDURE [ms64] RpcServerUseProtseqIf* ["RpcServerUseProtseqIfA"] (Protseq: WinApi.PtrSTR; MaxCalls: INTEGER; IfSpec: WinApi.PtrVoid; SecurityDescriptor: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcServerUseProtseqIf;*)

	PROCEDURE [ms64] RpcMgmtStatsVectorFree* (VAR [nil] StatsVector: PtrRPC_STATS_VECTOR): RPC_STATUS;
	(*END RpcMgmtStatsVectorFree;*)

	PROCEDURE [ms64] RpcMgmtInqStats* (Binding: WinApi.PtrVoid; VAR [nil] Statistics: PtrRPC_STATS_VECTOR): RPC_STATUS;
	(*END RpcMgmtInqStats;*)

	PROCEDURE [ms64] RpcMgmtIsServerListening* (Binding: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcMgmtIsServerListening;*)

	PROCEDURE [ms64] RpcMgmtStopServerListening* (Binding: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcMgmtStopServerListening;*)

	PROCEDURE [ms64] RpcMgmtWaitServerListen* (): RPC_STATUS;
	(*END RpcMgmtWaitServerListen;*)

	PROCEDURE [ms64] RpcMgmtSetServerStackSize* (ThreadStackSize: INTEGER): RPC_STATUS;
	(*END RpcMgmtSetServerStackSize;*)

	PROCEDURE [ms64] RpcSsDontSerializeContext* ();
	(*END RpcSsDontSerializeContext;*)

	PROCEDURE [ms64] RpcMgmtEnableIdleCleanup* (): RPC_STATUS;
	(*END RpcMgmtEnableIdleCleanup;*)

	PROCEDURE [ms64] RpcMgmtInqIfIds* (Binding: WinApi.PtrVoid; VAR [nil] IfIdVector: PtrRPC_IF_ID_VECTOR): RPC_STATUS;
	(*END RpcMgmtInqIfIds;*)

	PROCEDURE [ms64] RpcIfIdVectorFree* (VAR [nil] IfIdVector: PtrRPC_IF_ID_VECTOR): RPC_STATUS;
	(*END RpcIfIdVectorFree;*)

	PROCEDURE [ms64] RpcMgmtInqServerPrincNameA* (Binding: WinApi.PtrVoid; AuthnSvc: INTEGER; VAR [nil] ServerPrincName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcMgmtInqServerPrincNameA;*)

	PROCEDURE [ms64] RpcMgmtInqServerPrincNameW* (Binding: WinApi.PtrVoid; AuthnSvc: INTEGER; VAR [nil] ServerPrincName: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT): RPC_STATUS;
	(*END RpcMgmtInqServerPrincNameW;*)

	PROCEDURE [ms64] RpcMgmtInqServerPrincName* ["RpcMgmtInqServerPrincNameA"] (Binding: WinApi.PtrVoid; AuthnSvc: INTEGER; VAR [nil] ServerPrincName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcMgmtInqServerPrincName;*)

	PROCEDURE [ms64] RpcServerInqDefaultPrincNameA* (AuthnSvc: INTEGER; VAR [nil] PrincName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcServerInqDefaultPrincNameA;*)

	PROCEDURE [ms64] RpcServerInqDefaultPrincNameW* (AuthnSvc: INTEGER; VAR [nil] PrincName: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT): RPC_STATUS;
	(*END RpcServerInqDefaultPrincNameW;*)

	PROCEDURE [ms64] RpcServerInqDefaultPrincName* ["RpcServerInqDefaultPrincNameA"] (AuthnSvc: INTEGER; VAR [nil] PrincName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcServerInqDefaultPrincName;*)

	PROCEDURE [ms64] RpcEpResolveBinding* (Binding: WinApi.PtrVoid; IfSpec: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcEpResolveBinding;*)

	PROCEDURE [ms64] RpcNsBindingInqEntryNameA* (Binding: WinApi.PtrVoid; EntryNameSyntax: INTEGER; VAR [nil] EntryName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsBindingInqEntryNameA;*)

	PROCEDURE [ms64] RpcNsBindingInqEntryNameW* (Binding: WinApi.PtrVoid; EntryNameSyntax: INTEGER; VAR [nil] EntryName: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT): RPC_STATUS;
	(*END RpcNsBindingInqEntryNameW;*)

	PROCEDURE [ms64] RpcNsBindingInqEntryName* ["RpcNsBindingInqEntryNameA"] (Binding: WinApi.PtrVoid; EntryNameSyntax: INTEGER; VAR [nil] EntryName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsBindingInqEntryName;*)

	PROCEDURE [ms64] RpcBindingInqAuthClientA* (ClientBinding: WinApi.PtrVoid; VAR [nil] Privs: WinApi.PtrVoid; VAR [nil] ServerPrincName: WinApi.PtrSTR; VAR [nil] AuthnLevel: INTEGER; VAR [nil] AuthnSvc: INTEGER; VAR [nil] AuthzSvc: INTEGER): RPC_STATUS;
	(*END RpcBindingInqAuthClientA;*)

	PROCEDURE [ms64] RpcBindingInqAuthClientW* (ClientBinding: WinApi.PtrVoid; VAR [nil] Privs: WinApi.PtrVoid; VAR [nil] ServerPrincName: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT; VAR [nil] AuthnLevel: INTEGER; VAR [nil] AuthnSvc: INTEGER; VAR [nil] AuthzSvc: INTEGER): RPC_STATUS;
	(*END RpcBindingInqAuthClientW;*)

	PROCEDURE [ms64] RpcBindingInqAuthInfoA* (Binding: WinApi.PtrVoid; VAR [nil] ServerPrincName: WinApi.PtrSTR; VAR [nil] AuthnLevel: INTEGER; VAR [nil] AuthnSvc: INTEGER; VAR [nil] AuthIdentity: WinApi.PtrVoid; VAR [nil] AuthzSvc: INTEGER): RPC_STATUS;
	(*END RpcBindingInqAuthInfoA;*)

	PROCEDURE [ms64] RpcBindingInqAuthInfoW* (Binding: WinApi.PtrVoid; VAR [nil] ServerPrincName: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT; VAR [nil] AuthnLevel: INTEGER; VAR [nil] AuthnSvc: INTEGER; VAR [nil] AuthIdentity: WinApi.PtrVoid; VAR [nil] AuthzSvc: INTEGER): RPC_STATUS;
	(*END RpcBindingInqAuthInfoW;*)

	PROCEDURE [ms64] RpcBindingSetAuthInfoA* (Binding: WinApi.PtrVoid; ServerPrincName: WinApi.PtrSTR; AuthnLevel: INTEGER; AuthnSvc: INTEGER; AuthIdentity: WinApi.PtrVoid; AuthzSvc: INTEGER): RPC_STATUS;
	(*END RpcBindingSetAuthInfoA;*)

	PROCEDURE [ms64] RpcBindingSetAuthInfoW* (Binding: WinApi.PtrVoid; VAR [nil] ServerPrincName: SHORTINT; AuthnLevel: INTEGER; AuthnSvc: INTEGER; AuthIdentity: WinApi.PtrVoid; AuthzSvc: INTEGER): RPC_STATUS;
	(*END RpcBindingSetAuthInfoW;*)

	PROCEDURE [ms64] RpcServerRegisterAuthInfoA* (ServerPrincName: WinApi.PtrSTR; AuthnSvc: INTEGER; GetKeyFn: RPC_AUTH_KEY_RETRIEVAL_FN; Arg: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcServerRegisterAuthInfoA;*)

	PROCEDURE [ms64] RpcServerRegisterAuthInfoW* (VAR [nil] ServerPrincName: SHORTINT; AuthnSvc: INTEGER; GetKeyFn: RPC_AUTH_KEY_RETRIEVAL_FN; Arg: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcServerRegisterAuthInfoW;*)

	PROCEDURE [ms64] RpcBindingInqAuthClient* ["RpcBindingInqAuthClientA"] (ClientBinding: WinApi.PtrVoid; VAR [nil] Privs: WinApi.PtrVoid; VAR [nil] ServerPrincName: WinApi.PtrSTR; VAR [nil] AuthnLevel: INTEGER; VAR [nil] AuthnSvc: INTEGER; VAR [nil] AuthzSvc: INTEGER): RPC_STATUS;
	(*END RpcBindingInqAuthClient;*)

	PROCEDURE [ms64] RpcBindingInqAuthInfo* ["RpcBindingInqAuthInfoA"] (Binding: WinApi.PtrVoid; VAR [nil] ServerPrincName: WinApi.PtrSTR; VAR [nil] AuthnLevel: INTEGER; VAR [nil] AuthnSvc: INTEGER; VAR [nil] AuthIdentity: WinApi.PtrVoid; VAR [nil] AuthzSvc: INTEGER): RPC_STATUS;
	(*END RpcBindingInqAuthInfo;*)

	PROCEDURE [ms64] RpcBindingSetAuthInfo* ["RpcBindingSetAuthInfoA"] (Binding: WinApi.PtrVoid; ServerPrincName: WinApi.PtrSTR; AuthnLevel: INTEGER; AuthnSvc: INTEGER; AuthIdentity: WinApi.PtrVoid; AuthzSvc: INTEGER): RPC_STATUS;
	(*END RpcBindingSetAuthInfo;*)

	PROCEDURE [ms64] RpcServerRegisterAuthInfo* ["RpcServerRegisterAuthInfoA"] (ServerPrincName: WinApi.PtrSTR; AuthnSvc: INTEGER; GetKeyFn: RPC_AUTH_KEY_RETRIEVAL_FN; Arg: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcServerRegisterAuthInfo;*)

	PROCEDURE [ms64] RpcBindingServerFromClient* (ClientBinding: WinApi.PtrVoid; VAR [nil] ServerBinding: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcBindingServerFromClient;*)

	PROCEDURE [ms64] RpcRaiseException* (exception: RPC_STATUS);
	(*END RpcRaiseException;*)

	PROCEDURE [ms64] RpcTestCancel* (): RPC_STATUS;
	(*END RpcTestCancel;*)

	PROCEDURE [ms64] RpcCancelThread* (Thread: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcCancelThread;*)

	PROCEDURE [ms64] UuidCreate* (VAR [nil] Uuid: COM.GUID): RPC_STATUS;
	(*END UuidCreate;*)

	PROCEDURE [ms64] UuidToStringA* (VAR [nil] Uuid: COM.GUID; VAR [nil] StringUuid: WinApi.PtrSTR): RPC_STATUS;
	(*END UuidToStringA;*)

	PROCEDURE [ms64] UuidFromStringA* (StringUuid: WinApi.PtrSTR; VAR [nil] Uuid: COM.GUID): RPC_STATUS;
	(*END UuidFromStringA;*)

	PROCEDURE [ms64] UuidToStringW* (VAR [nil] Uuid: COM.GUID; VAR [nil] StringUuid: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT): RPC_STATUS;
	(*END UuidToStringW;*)

	PROCEDURE [ms64] UuidFromStringW* (VAR [nil] StringUuid: SHORTINT; VAR [nil] Uuid: COM.GUID): RPC_STATUS;
	(*END UuidFromStringW;*)

	PROCEDURE [ms64] UuidFromString* ["UuidFromStringA"] (StringUuid: WinApi.PtrSTR; VAR [nil] Uuid: COM.GUID): RPC_STATUS;
	(*END UuidFromString;*)

	PROCEDURE [ms64] UuidToString* ["UuidToStringA"] (VAR [nil] Uuid: COM.GUID; VAR [nil] StringUuid: WinApi.PtrSTR): RPC_STATUS;
	(*END UuidToString;*)

	PROCEDURE [ms64] UuidCompare* (VAR [nil] Uuid1: COM.GUID; VAR [nil] Uuid2: COM.GUID; VAR [nil] Status: RPC_STATUS): INTEGER;
	(*END UuidCompare;*)

	PROCEDURE [ms64] UuidCreateNil* (VAR [nil] NilUuid: COM.GUID): RPC_STATUS;
	(*END UuidCreateNil;*)

	PROCEDURE [ms64] UuidEqual* (VAR [nil] Uuid1: COM.GUID; VAR [nil] Uuid2: COM.GUID; VAR [nil] Status: RPC_STATUS): INTEGER;
	(*END UuidEqual;*)

	PROCEDURE [ms64] UuidHash* (VAR [nil] Uuid: COM.GUID; VAR [nil] Status: RPC_STATUS): SHORTINT;
	(*END UuidHash;*)

	PROCEDURE [ms64] UuidIsNil* (VAR [nil] Uuid: COM.GUID; VAR [nil] Status: RPC_STATUS): INTEGER;
	(*END UuidIsNil;*)

	PROCEDURE [ms64] RpcEpRegisterNoReplaceA* (IfSpec: WinApi.PtrVoid; VAR [nil] BindingVector: RPC_BINDING_VECTOR; VAR [nil] UuidVector: UUID_VECTOR; Annotation: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcEpRegisterNoReplaceA;*)

	PROCEDURE [ms64] RpcEpRegisterNoReplaceW* (IfSpec: WinApi.PtrVoid; VAR [nil] BindingVector: RPC_BINDING_VECTOR; VAR [nil] UuidVector: UUID_VECTOR; VAR [nil] Annotation: SHORTINT): RPC_STATUS;
	(*END RpcEpRegisterNoReplaceW;*)

	PROCEDURE [ms64] RpcEpRegisterA* (IfSpec: WinApi.PtrVoid; VAR [nil] BindingVector: RPC_BINDING_VECTOR; VAR [nil] UuidVector: UUID_VECTOR; Annotation: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcEpRegisterA;*)

	PROCEDURE [ms64] RpcEpRegisterW* (IfSpec: WinApi.PtrVoid; VAR [nil] BindingVector: RPC_BINDING_VECTOR; VAR [nil] UuidVector: UUID_VECTOR; VAR [nil] Annotation: SHORTINT): RPC_STATUS;
	(*END RpcEpRegisterW;*)

	PROCEDURE [ms64] RpcEpRegisterNoReplace* ["RpcEpRegisterNoReplaceA"] (IfSpec: WinApi.PtrVoid; VAR [nil] BindingVector: RPC_BINDING_VECTOR; VAR [nil] UuidVector: UUID_VECTOR; Annotation: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcEpRegisterNoReplace;*)

	PROCEDURE [ms64] RpcEpRegister* ["RpcEpRegisterA"] (IfSpec: WinApi.PtrVoid; VAR [nil] BindingVector: RPC_BINDING_VECTOR; VAR [nil] UuidVector: UUID_VECTOR; Annotation: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcEpRegister;*)

	PROCEDURE [ms64] RpcEpUnregister* (IfSpec: WinApi.PtrVoid; VAR [nil] BindingVector: RPC_BINDING_VECTOR; VAR [nil] UuidVector: UUID_VECTOR): RPC_STATUS;
	(*END RpcEpUnregister;*)

	PROCEDURE [ms64] DceErrorInqTextA* (RpcStatus: RPC_STATUS; ErrorText: WinApi.PtrSTR): RPC_STATUS;
	(*END DceErrorInqTextA;*)

	PROCEDURE [ms64] DceErrorInqTextW* (RpcStatus: RPC_STATUS; VAR [nil] ErrorText: SHORTINT): RPC_STATUS;
	(*END DceErrorInqTextW;*)

	PROCEDURE [ms64] DceErrorInqText* ["DceErrorInqTextA"] (RpcStatus: RPC_STATUS; ErrorText: WinApi.PtrSTR): RPC_STATUS;
	(*END DceErrorInqText;*)

	PROCEDURE [ms64] RpcMgmtEpEltInqBegin* (EpBinding: WinApi.PtrVoid; InquiryType: INTEGER; VAR [nil] IfId: RPC_IF_ID; VersOption: INTEGER; VAR [nil] ObjectUuid: COM.GUID; VAR [nil] InquiryContext: POINTER TO (*?*) ARRAY [untagged] OF WinApi.PtrVoid): RPC_STATUS;
	(*END RpcMgmtEpEltInqBegin;*)

	PROCEDURE [ms64] RpcMgmtEpEltInqDone* (VAR [nil] InquiryContext: POINTER TO (*?*) ARRAY [untagged] OF WinApi.PtrVoid): RPC_STATUS;
	(*END RpcMgmtEpEltInqDone;*)

	PROCEDURE [ms64] RpcMgmtEpEltInqNextA* (VAR [nil] InquiryContext: WinApi.PtrVoid; VAR [nil] IfId: RPC_IF_ID; VAR [nil] Binding: WinApi.PtrVoid; VAR [nil] ObjectUuid: COM.GUID; VAR [nil] Annotation: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcMgmtEpEltInqNextA;*)

	PROCEDURE [ms64] RpcMgmtEpEltInqNextW* (VAR [nil] InquiryContext: WinApi.PtrVoid; VAR [nil] IfId: RPC_IF_ID; VAR [nil] Binding: WinApi.PtrVoid; VAR [nil] ObjectUuid: COM.GUID; VAR [nil] Annotation: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT): RPC_STATUS;
	(*END RpcMgmtEpEltInqNextW;*)

	PROCEDURE [ms64] RpcMgmtEpEltInqNext* ["RpcMgmtEpEltInqNextA"] (VAR [nil] InquiryContext: WinApi.PtrVoid; VAR [nil] IfId: RPC_IF_ID; VAR [nil] Binding: WinApi.PtrVoid; VAR [nil] ObjectUuid: COM.GUID; VAR [nil] Annotation: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcMgmtEpEltInqNext;*)

	PROCEDURE [ms64] RpcMgmtEpUnregister* (EpBinding: WinApi.PtrVoid; VAR [nil] IfId: RPC_IF_ID; Binding: WinApi.PtrVoid; VAR [nil] ObjectUuid: COM.GUID): RPC_STATUS;
	(*END RpcMgmtEpUnregister;*)

	PROCEDURE [ms64] RpcMgmtSetAuthorizationFn* (AuthorizationFn: RPC_MGMT_AUTHORIZATION_FN): RPC_STATUS;
	(*END RpcMgmtSetAuthorizationFn;*)

	PROCEDURE [ms64] RpcMgmtInqParameter* (Parameter: INTEGER; VAR [nil] Value: INTEGER): RPC_STATUS;
	(*END RpcMgmtInqParameter;*)

	PROCEDURE [ms64] RpcMgmtSetParameter* (Parameter: INTEGER; Value: INTEGER): RPC_STATUS;
	(*END RpcMgmtSetParameter;*)

	PROCEDURE [ms64] RpcMgmtBindingInqParameter* (Handle: WinApi.PtrVoid; Parameter: INTEGER; VAR [nil] Value: INTEGER): RPC_STATUS;
	(*END RpcMgmtBindingInqParameter;*)

	PROCEDURE [ms64] RpcMgmtBindingSetParameter* (Handle: WinApi.PtrVoid; Parameter: INTEGER; Value: INTEGER): RPC_STATUS;
	(*END RpcMgmtBindingSetParameter;*)

	PROCEDURE [ms64] I_RpcGetBuffer* (VAR [nil] Message: RPC_MESSAGE): RPC_STATUS;
	(*END I_RpcGetBuffer;*)

	PROCEDURE [ms64] I_RpcSendReceive* (VAR [nil] Message: RPC_MESSAGE): RPC_STATUS;
	(*END I_RpcSendReceive;*)

	PROCEDURE [ms64] I_RpcFreeBuffer* (VAR [nil] Message: RPC_MESSAGE): RPC_STATUS;
	(*END I_RpcFreeBuffer;*)

	PROCEDURE [ms64] I_RpcRequestMutex* (VAR [nil] Mutex: WinApi.PtrVoid);
	(*END I_RpcRequestMutex;*)

	PROCEDURE [ms64] I_RpcClearMutex* (Mutex: WinApi.PtrVoid);
	(*END I_RpcClearMutex;*)

	PROCEDURE [ms64] I_RpcDeleteMutex* (Mutex: WinApi.PtrVoid);
	(*END I_RpcDeleteMutex;*)

	PROCEDURE [ms64] I_RpcAllocate* (Size: INTEGER): WinApi.PtrVoid;
	(*END I_RpcAllocate;*)

	PROCEDURE [ms64] I_RpcFree* (Object: WinApi.PtrVoid);
	(*END I_RpcFree;*)

	PROCEDURE [ms64] I_RpcPauseExecution* (Milliseconds: INTEGER);
	(*END I_RpcPauseExecution;*)

	PROCEDURE [ms64] I_RpcMonitorAssociation* (Handle: WinApi.PtrVoid; RundownRoutine: RPC_RUNDOWN; Context: WinApi.PtrVoid): RPC_STATUS;
	(*END I_RpcMonitorAssociation;*)

	PROCEDURE [ms64] I_RpcStopMonitorAssociation* (Handle: WinApi.PtrVoid): RPC_STATUS;
	(*END I_RpcStopMonitorAssociation;*)

	PROCEDURE [ms64] I_RpcGetCurrentCallHandle* (): WinApi.PtrVoid;
	(*END I_RpcGetCurrentCallHandle;*)

	PROCEDURE [ms64] I_RpcGetAssociationContext* (VAR [nil] AssociationContext: WinApi.PtrVoid): RPC_STATUS;
	(*END I_RpcGetAssociationContext;*)

	PROCEDURE [ms64] I_RpcSetAssociationContext* (AssociationContext: WinApi.PtrVoid): RPC_STATUS;
	(*END I_RpcSetAssociationContext;*)

	PROCEDURE [ms64] I_RpcNsBindingSetEntryName* (Binding: WinApi.PtrVoid; EntryNameSyntax: INTEGER; VAR [nil] EntryName: SHORTINT): RPC_STATUS;
	(*END I_RpcNsBindingSetEntryName;*)

	PROCEDURE [ms64] I_RpcBindingInqDynamicEndpoint* (Binding: WinApi.PtrVoid; VAR [nil] DynamicEndpoint: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT): RPC_STATUS;
	(*END I_RpcBindingInqDynamicEndpoint;*)

	PROCEDURE [ms64] I_RpcBindingInqTransportType* (Binding: WinApi.PtrVoid; VAR [nil] Type: INTEGER): RPC_STATUS;
	(*END I_RpcBindingInqTransportType;*)

	PROCEDURE [ms64] I_RpcIfInqTransferSyntaxes* (RpcIfHandle: WinApi.PtrVoid; VAR [nil] TransferSyntaxes: RPC_TRANSFER_SYNTAX; TransferSyntaxSize: INTEGER; VAR [nil] TransferSyntaxCount: INTEGER): RPC_STATUS;
	(*END I_RpcIfInqTransferSyntaxes;*)

	PROCEDURE [ms64] I_UuidCreate* (VAR [nil] Uuid: COM.GUID): RPC_STATUS;
	(*END I_UuidCreate;*)

	PROCEDURE [ms64] I_RpcBindingCopy* (SourceBinding: WinApi.PtrVoid; VAR [nil] DestinationBinding: WinApi.PtrVoid): RPC_STATUS;
	(*END I_RpcBindingCopy;*)

	PROCEDURE [ms64] I_RpcBindingIsClientLocal* (BindingHandle: WinApi.PtrVoid; VAR [nil] ClientLocalFlag: INTEGER): RPC_STATUS;
	(*END I_RpcBindingIsClientLocal;*)

	PROCEDURE [ms64] I_RpcSsDontSerializeContext* ();
	(*END I_RpcSsDontSerializeContext;*)

	PROCEDURE [ms64] I_RpcLaunchDatagramReceiveThread* (pAddress: WinApi.PtrVoid): RPC_STATUS;
	(*END I_RpcLaunchDatagramReceiveThread;*)

	PROCEDURE [ms64] I_RpcServerRegisterForwardFunction* (pForwardFunction: RPC_FORWARD_FUNCTION): RPC_STATUS;
	(*END I_RpcServerRegisterForwardFunction;*)

	PROCEDURE [ms64] I_RpcConnectionInqSockBuffSize* (VAR [nil] RecvBuffSize: INTEGER; VAR [nil] SendBuffSize: INTEGER): RPC_STATUS;
	(*END I_RpcConnectionInqSockBuffSize;*)

	PROCEDURE [ms64] I_RpcConnectionSetSockBuffSize* (RecvBuffSize: INTEGER; SendBuffSize: INTEGER): RPC_STATUS;
	(*END I_RpcConnectionSetSockBuffSize;*)

	PROCEDURE [ms64] I_RpcServerInqTransportType* (VAR [nil] Type: INTEGER): RPC_STATUS;
	(*END I_RpcServerInqTransportType;*)

	PROCEDURE [ms64] RpcNsBindingExportA* ["RPCNS4.dll", ""] (EntryNameSyntax: INTEGER; EntryName: WinApi.PtrSTR; IfSpec: WinApi.PtrVoid; VAR [nil] BindingVec: RPC_BINDING_VECTOR; VAR [nil] ObjectUuidVec: UUID_VECTOR): RPC_STATUS;
	(*END RpcNsBindingExportA;*)

	PROCEDURE [ms64] RpcNsBindingUnexportA* ["RPCNS4.dll", ""] (EntryNameSyntax: INTEGER; EntryName: WinApi.PtrSTR; IfSpec: WinApi.PtrVoid; VAR [nil] ObjectUuidVec: UUID_VECTOR): RPC_STATUS;
	(*END RpcNsBindingUnexportA;*)

	PROCEDURE [ms64] RpcNsBindingExportW* ["RPCNS4.dll", ""] (EntryNameSyntax: INTEGER; VAR [nil] EntryName: SHORTINT; IfSpec: WinApi.PtrVoid; VAR [nil] BindingVec: RPC_BINDING_VECTOR; VAR [nil] ObjectUuidVec: UUID_VECTOR): RPC_STATUS;
	(*END RpcNsBindingExportW;*)

	PROCEDURE [ms64] RpcNsBindingUnexportW* ["RPCNS4.dll", ""] (EntryNameSyntax: INTEGER; VAR [nil] EntryName: SHORTINT; IfSpec: WinApi.PtrVoid; VAR [nil] ObjectUuidVec: UUID_VECTOR): RPC_STATUS;
	(*END RpcNsBindingUnexportW;*)

	PROCEDURE [ms64] RpcNsBindingLookupBeginA* ["RPCNS4.dll", ""] (EntryNameSyntax: INTEGER; EntryName: WinApi.PtrSTR; IfSpec: WinApi.PtrVoid; VAR [nil] ObjUuid: COM.GUID; BindingMaxCount: INTEGER; VAR [nil] LookupContext: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsBindingLookupBeginA;*)

	PROCEDURE [ms64] RpcNsBindingLookupBeginW* ["RPCNS4.dll", ""] (EntryNameSyntax: INTEGER; VAR [nil] EntryName: SHORTINT; IfSpec: WinApi.PtrVoid; VAR [nil] ObjUuid: COM.GUID; BindingMaxCount: INTEGER; VAR [nil] LookupContext: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsBindingLookupBeginW;*)

	PROCEDURE [ms64] RpcNsBindingLookupNext* ["RPCNS4.dll", ""] (LookupContext: WinApi.PtrVoid; VAR [nil] BindingVec: PtrRPC_BINDING_VECTOR): RPC_STATUS;
	(*END RpcNsBindingLookupNext;*)

	PROCEDURE [ms64] RpcNsBindingLookupDone* ["RPCNS4.dll", ""] (VAR [nil] LookupContext: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsBindingLookupDone;*)

	PROCEDURE [ms64] RpcNsGroupDeleteA* ["RPCNS4.dll", ""] (GroupNameSyntax: INTEGER; GroupName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsGroupDeleteA;*)

	PROCEDURE [ms64] RpcNsGroupMbrAddA* ["RPCNS4.dll", ""] (GroupNameSyntax: INTEGER; GroupName: WinApi.PtrSTR; MemberNameSyntax: INTEGER; MemberName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsGroupMbrAddA;*)

	PROCEDURE [ms64] RpcNsGroupMbrRemoveA* ["RPCNS4.dll", ""] (GroupNameSyntax: INTEGER; GroupName: WinApi.PtrSTR; MemberNameSyntax: INTEGER; MemberName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsGroupMbrRemoveA;*)

	PROCEDURE [ms64] RpcNsGroupMbrInqBeginA* ["RPCNS4.dll", ""] (GroupNameSyntax: INTEGER; GroupName: WinApi.PtrSTR; MemberNameSyntax: INTEGER; VAR [nil] InquiryContext: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsGroupMbrInqBeginA;*)

	PROCEDURE [ms64] RpcNsGroupMbrInqNextA* ["RPCNS4.dll", ""] (InquiryContext: WinApi.PtrVoid; VAR [nil] MemberName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsGroupMbrInqNextA;*)

	PROCEDURE [ms64] RpcNsGroupDeleteW* ["RPCNS4.dll", ""] (GroupNameSyntax: INTEGER; VAR [nil] GroupName: SHORTINT): RPC_STATUS;
	(*END RpcNsGroupDeleteW;*)

	PROCEDURE [ms64] RpcNsGroupMbrAddW* ["RPCNS4.dll", ""] (GroupNameSyntax: INTEGER; VAR [nil] GroupName: SHORTINT; MemberNameSyntax: INTEGER; VAR [nil] MemberName: SHORTINT): RPC_STATUS;
	(*END RpcNsGroupMbrAddW;*)

	PROCEDURE [ms64] RpcNsGroupMbrRemoveW* ["RPCNS4.dll", ""] (GroupNameSyntax: INTEGER; VAR [nil] GroupName: SHORTINT; MemberNameSyntax: INTEGER; VAR [nil] MemberName: SHORTINT): RPC_STATUS;
	(*END RpcNsGroupMbrRemoveW;*)

	PROCEDURE [ms64] RpcNsGroupMbrInqBeginW* ["RPCNS4.dll", ""] (GroupNameSyntax: INTEGER; VAR [nil] GroupName: SHORTINT; MemberNameSyntax: INTEGER; VAR [nil] InquiryContext: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsGroupMbrInqBeginW;*)

	PROCEDURE [ms64] RpcNsGroupMbrInqNextW* ["RPCNS4.dll", ""] (InquiryContext: WinApi.PtrVoid; VAR [nil] MemberName: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT): RPC_STATUS;
	(*END RpcNsGroupMbrInqNextW;*)

	PROCEDURE [ms64] RpcNsGroupMbrInqDone* ["RPCNS4.dll", ""] (VAR [nil] InquiryContext: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsGroupMbrInqDone;*)

	PROCEDURE [ms64] RpcNsProfileDeleteA* ["RPCNS4.dll", ""] (ProfileNameSyntax: INTEGER; ProfileName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsProfileDeleteA;*)

	PROCEDURE [ms64] RpcNsProfileEltAddA* ["RPCNS4.dll", ""] (ProfileNameSyntax: INTEGER; ProfileName: WinApi.PtrSTR; VAR [nil] IfId: RPC_IF_ID; MemberNameSyntax: INTEGER; MemberName: WinApi.PtrSTR; Priority: INTEGER; Annotation: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsProfileEltAddA;*)

	PROCEDURE [ms64] RpcNsProfileEltRemoveA* ["RPCNS4.dll", ""] (ProfileNameSyntax: INTEGER; ProfileName: WinApi.PtrSTR; VAR [nil] IfId: RPC_IF_ID; MemberNameSyntax: INTEGER; MemberName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsProfileEltRemoveA;*)

	PROCEDURE [ms64] RpcNsProfileEltInqBeginA* ["RPCNS4.dll", ""] (ProfileNameSyntax: INTEGER; ProfileName: WinApi.PtrSTR; InquiryType: INTEGER; VAR [nil] IfId: RPC_IF_ID; VersOption: INTEGER; MemberNameSyntax: INTEGER; MemberName: WinApi.PtrSTR; VAR [nil] InquiryContext: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsProfileEltInqBeginA;*)

	PROCEDURE [ms64] RpcNsProfileEltInqNextA* ["RPCNS4.dll", ""] (InquiryContext: WinApi.PtrVoid; VAR [nil] IfId: RPC_IF_ID; VAR [nil] MemberName: WinApi.PtrSTR; VAR [nil] Priority: INTEGER; VAR [nil] Annotation: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsProfileEltInqNextA;*)

	PROCEDURE [ms64] RpcNsProfileDeleteW* ["RPCNS4.dll", ""] (ProfileNameSyntax: INTEGER; VAR [nil] ProfileName: SHORTINT): RPC_STATUS;
	(*END RpcNsProfileDeleteW;*)

	PROCEDURE [ms64] RpcNsProfileEltAddW* ["RPCNS4.dll", ""] (ProfileNameSyntax: INTEGER; VAR [nil] ProfileName: SHORTINT; VAR [nil] IfId: RPC_IF_ID; MemberNameSyntax: INTEGER; VAR [nil] MemberName: SHORTINT; Priority: INTEGER; VAR [nil] Annotation: SHORTINT): RPC_STATUS;
	(*END RpcNsProfileEltAddW;*)

	PROCEDURE [ms64] RpcNsProfileEltRemoveW* ["RPCNS4.dll", ""] (ProfileNameSyntax: INTEGER; VAR [nil] ProfileName: SHORTINT; VAR [nil] IfId: RPC_IF_ID; MemberNameSyntax: INTEGER; VAR [nil] MemberName: SHORTINT): RPC_STATUS;
	(*END RpcNsProfileEltRemoveW;*)

	PROCEDURE [ms64] RpcNsProfileEltInqBeginW* ["RPCNS4.dll", ""] (ProfileNameSyntax: INTEGER; VAR [nil] ProfileName: SHORTINT; InquiryType: INTEGER; VAR [nil] IfId: RPC_IF_ID; VersOption: INTEGER; MemberNameSyntax: INTEGER; VAR [nil] MemberName: SHORTINT; VAR [nil] InquiryContext: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsProfileEltInqBeginW;*)

	PROCEDURE [ms64] RpcNsProfileEltInqNextW* ["RPCNS4.dll", ""] (InquiryContext: WinApi.PtrVoid; VAR [nil] IfId: RPC_IF_ID; VAR [nil] MemberName: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT; VAR [nil] Priority: INTEGER; VAR [nil] Annotation: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT): RPC_STATUS;
	(*END RpcNsProfileEltInqNextW;*)

	PROCEDURE [ms64] RpcNsProfileEltInqDone* ["RPCNS4.dll", ""] (VAR [nil] InquiryContext: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsProfileEltInqDone;*)

	PROCEDURE [ms64] RpcNsEntryObjectInqBeginA* ["RPCNS4.dll", ""] (EntryNameSyntax: INTEGER; EntryName: WinApi.PtrSTR; VAR [nil] InquiryContext: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsEntryObjectInqBeginA;*)

	PROCEDURE [ms64] RpcNsEntryObjectInqBeginW* ["RPCNS4.dll", ""] (EntryNameSyntax: INTEGER; VAR [nil] EntryName: SHORTINT; VAR [nil] InquiryContext: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsEntryObjectInqBeginW;*)

	PROCEDURE [ms64] RpcNsEntryObjectInqNext* ["RPCNS4.dll", ""] (InquiryContext: WinApi.PtrVoid; VAR [nil] ObjUuid: COM.GUID): RPC_STATUS;
	(*END RpcNsEntryObjectInqNext;*)

	PROCEDURE [ms64] RpcNsEntryObjectInqDone* ["RPCNS4.dll", ""] (VAR [nil] InquiryContext: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsEntryObjectInqDone;*)

	PROCEDURE [ms64] RpcNsEntryExpandNameA* ["RPCNS4.dll", ""] (EntryNameSyntax: INTEGER; EntryName: WinApi.PtrSTR; VAR [nil] ExpandedName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsEntryExpandNameA;*)

	PROCEDURE [ms64] RpcNsMgmtBindingUnexportA* ["RPCNS4.dll", ""] (EntryNameSyntax: INTEGER; EntryName: WinApi.PtrSTR; VAR [nil] IfId: RPC_IF_ID; VersOption: INTEGER; VAR [nil] ObjectUuidVec: UUID_VECTOR): RPC_STATUS;
	(*END RpcNsMgmtBindingUnexportA;*)

	PROCEDURE [ms64] RpcNsMgmtEntryCreateA* ["RPCNS4.dll", ""] (EntryNameSyntax: INTEGER; EntryName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsMgmtEntryCreateA;*)

	PROCEDURE [ms64] RpcNsMgmtEntryDeleteA* ["RPCNS4.dll", ""] (EntryNameSyntax: INTEGER; EntryName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsMgmtEntryDeleteA;*)

	PROCEDURE [ms64] RpcNsMgmtEntryInqIfIdsA* ["RPCNS4.dll", ""] (EntryNameSyntax: INTEGER; EntryName: WinApi.PtrSTR; VAR [nil] IfIdVec: PtrRPC_IF_ID_VECTOR): RPC_STATUS;
	(*END RpcNsMgmtEntryInqIfIdsA;*)

	PROCEDURE [ms64] RpcNsMgmtHandleSetExpAge* ["RPCNS4.dll", ""] (NsHandle: WinApi.PtrVoid; ExpirationAge: INTEGER): RPC_STATUS;
	(*END RpcNsMgmtHandleSetExpAge;*)

	PROCEDURE [ms64] RpcNsMgmtInqExpAge* ["RPCNS4.dll", ""] (VAR [nil] ExpirationAge: INTEGER): RPC_STATUS;
	(*END RpcNsMgmtInqExpAge;*)

	PROCEDURE [ms64] RpcNsMgmtSetExpAge* ["RPCNS4.dll", ""] (ExpirationAge: INTEGER): RPC_STATUS;
	(*END RpcNsMgmtSetExpAge;*)

	PROCEDURE [ms64] RpcNsEntryExpandNameW* ["RPCNS4.dll", ""] (EntryNameSyntax: INTEGER; VAR [nil] EntryName: SHORTINT; VAR [nil] ExpandedName: POINTER TO (*?*) ARRAY [untagged] OF SHORTINT): RPC_STATUS;
	(*END RpcNsEntryExpandNameW;*)

	PROCEDURE [ms64] RpcNsMgmtBindingUnexportW* ["RPCNS4.dll", ""] (EntryNameSyntax: INTEGER; VAR [nil] EntryName: SHORTINT; VAR [nil] IfId: RPC_IF_ID; VersOption: INTEGER; VAR [nil] ObjectUuidVec: UUID_VECTOR): RPC_STATUS;
	(*END RpcNsMgmtBindingUnexportW;*)

	PROCEDURE [ms64] RpcNsMgmtEntryCreateW* ["RPCNS4.dll", ""] (EntryNameSyntax: INTEGER; VAR [nil] EntryName: SHORTINT): RPC_STATUS;
	(*END RpcNsMgmtEntryCreateW;*)

	PROCEDURE [ms64] RpcNsMgmtEntryDeleteW* ["RPCNS4.dll", ""] (EntryNameSyntax: INTEGER; VAR [nil] EntryName: SHORTINT): RPC_STATUS;
	(*END RpcNsMgmtEntryDeleteW;*)

	PROCEDURE [ms64] RpcNsMgmtEntryInqIfIdsW* ["RPCNS4.dll", ""] (EntryNameSyntax: INTEGER; VAR [nil] EntryName: SHORTINT; VAR [nil] IfIdVec: PtrRPC_IF_ID_VECTOR): RPC_STATUS;
	(*END RpcNsMgmtEntryInqIfIdsW;*)

	PROCEDURE [ms64] RpcNsBindingImportBeginA* ["RPCNS4.dll", ""] (EntryNameSyntax: INTEGER; EntryName: WinApi.PtrSTR; IfSpec: WinApi.PtrVoid; VAR [nil] ObjUuid: COM.GUID; VAR [nil] ImportContext: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsBindingImportBeginA;*)

	PROCEDURE [ms64] RpcNsBindingImportBeginW* ["RPCNS4.dll", ""] (EntryNameSyntax: INTEGER; VAR [nil] EntryName: SHORTINT; IfSpec: WinApi.PtrVoid; VAR [nil] ObjUuid: COM.GUID; VAR [nil] ImportContext: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsBindingImportBeginW;*)

	PROCEDURE [ms64] RpcNsBindingImportNext* ["RPCNS4.dll", ""] (ImportContext: WinApi.PtrVoid; VAR [nil] Binding: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsBindingImportNext;*)

	PROCEDURE [ms64] RpcNsBindingImportDone* ["RPCNS4.dll", ""] (VAR [nil] ImportContext: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsBindingImportDone;*)

	PROCEDURE [ms64] RpcNsBindingSelect* ["RPCNS4.dll", ""] (VAR [nil] BindingVec: RPC_BINDING_VECTOR; VAR [nil] Binding: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsBindingSelect;*)

	PROCEDURE [ms64] RpcNsBindingLookupBegin* ["RPCNS4.dll", "RpcNsBindingLookupBeginA"] (EntryNameSyntax: INTEGER; EntryName: WinApi.PtrSTR; IfSpec: WinApi.PtrVoid; VAR [nil] ObjUuid: COM.GUID; BindingMaxCount: INTEGER; VAR [nil] LookupContext: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsBindingLookupBegin;*)

	PROCEDURE [ms64] RpcNsBindingImportBegin* ["RPCNS4.dll", "RpcNsBindingImportBeginA"] (EntryNameSyntax: INTEGER; EntryName: WinApi.PtrSTR; IfSpec: WinApi.PtrVoid; VAR [nil] ObjUuid: COM.GUID; VAR [nil] ImportContext: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsBindingImportBegin;*)

	PROCEDURE [ms64] RpcNsBindingExport* ["RPCNS4.dll", "RpcNsBindingExportA"] (EntryNameSyntax: INTEGER; EntryName: WinApi.PtrSTR; IfSpec: WinApi.PtrVoid; VAR [nil] BindingVec: RPC_BINDING_VECTOR; VAR [nil] ObjectUuidVec: UUID_VECTOR): RPC_STATUS;
	(*END RpcNsBindingExport;*)

	PROCEDURE [ms64] RpcNsBindingUnexport* ["RPCNS4.dll", "RpcNsBindingUnexportA"] (EntryNameSyntax: INTEGER; EntryName: WinApi.PtrSTR; IfSpec: WinApi.PtrVoid; VAR [nil] ObjectUuidVec: UUID_VECTOR): RPC_STATUS;
	(*END RpcNsBindingUnexport;*)

	PROCEDURE [ms64] RpcNsGroupDelete* ["RPCNS4.dll", "RpcNsGroupDeleteA"] (GroupNameSyntax: INTEGER; GroupName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsGroupDelete;*)

	PROCEDURE [ms64] RpcNsGroupMbrAdd* ["RPCNS4.dll", "RpcNsGroupMbrAddA"] (GroupNameSyntax: INTEGER; GroupName: WinApi.PtrSTR; MemberNameSyntax: INTEGER; MemberName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsGroupMbrAdd;*)

	PROCEDURE [ms64] RpcNsGroupMbrRemove* ["RPCNS4.dll", "RpcNsGroupMbrRemoveA"] (GroupNameSyntax: INTEGER; GroupName: WinApi.PtrSTR; MemberNameSyntax: INTEGER; MemberName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsGroupMbrRemove;*)

	PROCEDURE [ms64] RpcNsGroupMbrInqBegin* ["RPCNS4.dll", "RpcNsGroupMbrInqBeginA"] (GroupNameSyntax: INTEGER; GroupName: WinApi.PtrSTR; MemberNameSyntax: INTEGER; VAR [nil] InquiryContext: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsGroupMbrInqBegin;*)

	PROCEDURE [ms64] RpcNsGroupMbrInqNext* ["RPCNS4.dll", "RpcNsGroupMbrInqNextA"] (InquiryContext: WinApi.PtrVoid; VAR [nil] MemberName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsGroupMbrInqNext;*)

	PROCEDURE [ms64] RpcNsEntryExpandName* ["RPCNS4.dll", "RpcNsEntryExpandNameA"] (EntryNameSyntax: INTEGER; EntryName: WinApi.PtrSTR; VAR [nil] ExpandedName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsEntryExpandName;*)

	PROCEDURE [ms64] RpcNsEntryObjectInqBegin* ["RPCNS4.dll", "RpcNsEntryObjectInqBeginA"] (EntryNameSyntax: INTEGER; EntryName: WinApi.PtrSTR; VAR [nil] InquiryContext: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsEntryObjectInqBegin;*)

	PROCEDURE [ms64] RpcNsMgmtBindingUnexport* ["RPCNS4.dll", "RpcNsMgmtBindingUnexportA"] (EntryNameSyntax: INTEGER; EntryName: WinApi.PtrSTR; VAR [nil] IfId: RPC_IF_ID; VersOption: INTEGER; VAR [nil] ObjectUuidVec: UUID_VECTOR): RPC_STATUS;
	(*END RpcNsMgmtBindingUnexport;*)

	PROCEDURE [ms64] RpcNsMgmtEntryCreate* ["RPCNS4.dll", "RpcNsMgmtEntryCreateA"] (EntryNameSyntax: INTEGER; EntryName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsMgmtEntryCreate;*)

	PROCEDURE [ms64] RpcNsMgmtEntryDelete* ["RPCNS4.dll", "RpcNsMgmtEntryDeleteA"] (EntryNameSyntax: INTEGER; EntryName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsMgmtEntryDelete;*)

	PROCEDURE [ms64] RpcNsMgmtEntryInqIfIds* ["RPCNS4.dll", "RpcNsMgmtEntryInqIfIdsA"] (EntryNameSyntax: INTEGER; EntryName: WinApi.PtrSTR; VAR [nil] IfIdVec: PtrRPC_IF_ID_VECTOR): RPC_STATUS;
	(*END RpcNsMgmtEntryInqIfIds;*)

	PROCEDURE [ms64] RpcNsProfileDelete* ["RPCNS4.dll", "RpcNsProfileDeleteA"] (ProfileNameSyntax: INTEGER; ProfileName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsProfileDelete;*)

	PROCEDURE [ms64] RpcNsProfileEltAdd* ["RPCNS4.dll", "RpcNsProfileEltAddA"] (ProfileNameSyntax: INTEGER; ProfileName: WinApi.PtrSTR; VAR [nil] IfId: RPC_IF_ID; MemberNameSyntax: INTEGER; MemberName: WinApi.PtrSTR; Priority: INTEGER; Annotation: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsProfileEltAdd;*)

	PROCEDURE [ms64] RpcNsProfileEltRemove* ["RPCNS4.dll", "RpcNsProfileEltRemoveA"] (ProfileNameSyntax: INTEGER; ProfileName: WinApi.PtrSTR; VAR [nil] IfId: RPC_IF_ID; MemberNameSyntax: INTEGER; MemberName: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsProfileEltRemove;*)

	PROCEDURE [ms64] RpcNsProfileEltInqBegin* ["RPCNS4.dll", "RpcNsProfileEltInqBeginA"] (ProfileNameSyntax: INTEGER; ProfileName: WinApi.PtrSTR; InquiryType: INTEGER; VAR [nil] IfId: RPC_IF_ID; VersOption: INTEGER; MemberNameSyntax: INTEGER; MemberName: WinApi.PtrSTR; VAR [nil] InquiryContext: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcNsProfileEltInqBegin;*)

	PROCEDURE [ms64] RpcNsProfileEltInqNext* ["RPCNS4.dll", "RpcNsProfileEltInqNextA"] (InquiryContext: WinApi.PtrVoid; VAR [nil] IfId: RPC_IF_ID; VAR [nil] MemberName: WinApi.PtrSTR; VAR [nil] Priority: INTEGER; VAR [nil] Annotation: WinApi.PtrSTR): RPC_STATUS;
	(*END RpcNsProfileEltInqNext;*)

	PROCEDURE [ms64] RpcImpersonateClient* (BindingHandle: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcImpersonateClient;*)

	PROCEDURE [ms64] RpcRevertToSelfEx* (BindingHandle: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcRevertToSelfEx;*)

	PROCEDURE [ms64] RpcRevertToSelf* (): RPC_STATUS;
	(*END RpcRevertToSelf;*)

	PROCEDURE [ms64] I_RpcMapWin32Status* (Status: RPC_STATUS): INTEGER;
	(*END I_RpcMapWin32Status;*)

	PROCEDURE [ms64] I_RpcNsGetBuffer* ["RPCNS4.dll", ""] (VAR [nil] Message: RPC_MESSAGE): RPC_STATUS;
	(*END I_RpcNsGetBuffer;*)

	PROCEDURE [ms64] I_RpcNsSendReceive* ["RPCNS4.dll", ""] (VAR [nil] Message: RPC_MESSAGE; VAR [nil] Handle: WinApi.PtrVoid): RPC_STATUS;
	(*END I_RpcNsSendReceive;*)

	PROCEDURE [ms64] I_RpcNsRaiseException* ["RPCNS4.dll", ""] (VAR [nil] Message: RPC_MESSAGE; Status: RPC_STATUS);
	(*END I_RpcNsRaiseException;*)

	PROCEDURE [ms64] I_RpcReBindBuffer* ["RPCNS4.dll", ""] (VAR [nil] Message: RPC_MESSAGE): RPC_STATUS;
	(*END I_RpcReBindBuffer;*)

	PROCEDURE [ms64] NDRCContextBinding* (CContext: WinApi.PtrVoid): WinApi.PtrVoid;
	(*END NDRCContextBinding;*)

	PROCEDURE [ms64] NDRCContextMarshall* (CContext: WinApi.PtrVoid; pBuff: WinApi.PtrVoid);
	(*END NDRCContextMarshall;*)

	PROCEDURE [ms64] NDRCContextUnmarshall* (VAR [nil] pCContext: WinApi.PtrVoid; hBinding: WinApi.PtrVoid; pBuff: WinApi.PtrVoid; DataRepresentation: INTEGER);
	(*END NDRCContextUnmarshall;*)

	PROCEDURE [ms64] NDRSContextMarshall* (CContext: NDR_SCONTEXT; pBuff: WinApi.PtrVoid; userRunDownIn: NDR_RUNDOWN);
	(*END NDRSContextMarshall;*)

	PROCEDURE [ms64] NDRSContextUnmarshall* (pBuff: WinApi.PtrVoid; DataRepresentation: INTEGER): NDR_SCONTEXT;
	(*END NDRSContextUnmarshall;*)

	PROCEDURE [ms64] RpcSsDestroyClientContext* (VAR [nil] ContextHandle: WinApi.PtrVoid);
	(*END RpcSsDestroyClientContext;*)

	PROCEDURE [ms64] NDRcopy* (pTarget: WinApi.PtrVoid; pSource: WinApi.PtrVoid; size: INTEGER);
	(*END NDRcopy;*)

	PROCEDURE [ms64] MIDL_wchar_strlen* (s: WinApi.PtrWSTR): INTEGER;
	(*END MIDL_wchar_strlen;*)

	PROCEDURE [ms64] MIDL_wchar_strcpy* (t: WinApi.PtrVoid; s: WinApi.PtrWSTR);
	(*END MIDL_wchar_strcpy;*)

	PROCEDURE [ms64] char_from_ndr* (VAR [nil] SourceMessage: RPC_MESSAGE; Target: WinApi.PtrSTR);
	(*END char_from_ndr;*)

	PROCEDURE [ms64] char_array_from_ndr* (VAR [nil] SourceMessage: RPC_MESSAGE; LowerIndex: INTEGER; UpperIndex: INTEGER; Target: WinApi.PtrSTR);
	(*END char_array_from_ndr;*)

	PROCEDURE [ms64] short_from_ndr* (VAR [nil] source: RPC_MESSAGE; VAR [nil] target: SHORTINT);
	(*END short_from_ndr;*)

	PROCEDURE [ms64] short_array_from_ndr* (VAR [nil] SourceMessage: RPC_MESSAGE; LowerIndex: INTEGER; UpperIndex: INTEGER; VAR [nil] Target: SHORTINT);
	(*END short_array_from_ndr;*)

	PROCEDURE [ms64] short_from_ndr_temp* (VAR [nil] source: WinApi.PtrSTR; VAR [nil] target: SHORTINT; format: INTEGER);
	(*END short_from_ndr_temp;*)

	PROCEDURE [ms64] long_from_ndr* (VAR [nil] source: RPC_MESSAGE; VAR [nil] target: INTEGER);
	(*END long_from_ndr;*)

	PROCEDURE [ms64] long_array_from_ndr* (VAR [nil] SourceMessage: RPC_MESSAGE; LowerIndex: INTEGER; UpperIndex: INTEGER; VAR [nil] Target: INTEGER);
	(*END long_array_from_ndr;*)

	PROCEDURE [ms64] long_from_ndr_temp* (VAR [nil] source: WinApi.PtrSTR; VAR [nil] target: INTEGER; format: INTEGER);
	(*END long_from_ndr_temp;*)

	PROCEDURE [ms64] enum_from_ndr* (VAR [nil] SourceMessage: RPC_MESSAGE; VAR [nil] Target: INTEGER);
	(*END enum_from_ndr;*)

	PROCEDURE [ms64] float_from_ndr* (VAR [nil] SourceMessage: RPC_MESSAGE; Target: WinApi.PtrVoid);
	(*END float_from_ndr;*)

	PROCEDURE [ms64] float_array_from_ndr* (VAR [nil] SourceMessage: RPC_MESSAGE; LowerIndex: INTEGER; UpperIndex: INTEGER; Target: WinApi.PtrVoid);
	(*END float_array_from_ndr;*)

	PROCEDURE [ms64] double_from_ndr* (VAR [nil] SourceMessage: RPC_MESSAGE; Target: WinApi.PtrVoid);
	(*END double_from_ndr;*)

	PROCEDURE [ms64] double_array_from_ndr* (VAR [nil] SourceMessage: RPC_MESSAGE; LowerIndex: INTEGER; UpperIndex: INTEGER; Target: WinApi.PtrVoid);
	(*END double_array_from_ndr;*)

	PROCEDURE [ms64] data_from_ndr* (VAR [nil] source: RPC_MESSAGE; target: WinApi.PtrVoid; format: WinApi.PtrSTR; MscPak: SHORTCHAR);
	(*END data_from_ndr;*)

	PROCEDURE [ms64] data_into_ndr* (source: WinApi.PtrVoid; VAR [nil] target: RPC_MESSAGE; format: WinApi.PtrSTR; MscPak: SHORTCHAR);
	(*END data_into_ndr;*)

	PROCEDURE [ms64] tree_into_ndr* (source: WinApi.PtrVoid; VAR [nil] target: RPC_MESSAGE; format: WinApi.PtrSTR; MscPak: SHORTCHAR);
	(*END tree_into_ndr;*)

	PROCEDURE [ms64] data_size_ndr* (source: WinApi.PtrVoid; VAR [nil] target: RPC_MESSAGE; format: WinApi.PtrSTR; MscPak: SHORTCHAR);
	(*END data_size_ndr;*)

	PROCEDURE [ms64] tree_size_ndr* (source: WinApi.PtrVoid; VAR [nil] target: RPC_MESSAGE; format: WinApi.PtrSTR; MscPak: SHORTCHAR);
	(*END tree_size_ndr;*)

	PROCEDURE [ms64] tree_peek_ndr* (VAR [nil] source: RPC_MESSAGE; VAR [nil] buffer: WinApi.PtrSTR; format: WinApi.PtrSTR; MscPak: SHORTCHAR);
	(*END tree_peek_ndr;*)

	PROCEDURE [ms64] NdrSimpleTypeMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; FormatChar: SHORTCHAR);
	(*END NdrSimpleTypeMarshall;*)

	PROCEDURE [ms64] NdrPointerMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR): WinApi.PtrSTR;
	(*END NdrPointerMarshall;*)

	PROCEDURE [ms64] NdrSimpleStructMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR): WinApi.PtrSTR;
	(*END NdrSimpleStructMarshall;*)

	PROCEDURE [ms64] NdrConformantStructMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR): WinApi.PtrSTR;
	(*END NdrConformantStructMarshall;*)

	PROCEDURE [ms64] NdrConformantVaryingStructMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR): WinApi.PtrSTR;
	(*END NdrConformantVaryingStructMarshall;*)

	PROCEDURE [ms64] NdrHardStructMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR): WinApi.PtrSTR;
	(*END NdrHardStructMarshall;*)

	PROCEDURE [ms64] NdrComplexStructMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR): WinApi.PtrSTR;
	(*END NdrComplexStructMarshall;*)

	PROCEDURE [ms64] NdrFixedArrayMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR): WinApi.PtrSTR;
	(*END NdrFixedArrayMarshall;*)

	PROCEDURE [ms64] NdrConformantArrayMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR): WinApi.PtrSTR;
	(*END NdrConformantArrayMarshall;*)

	PROCEDURE [ms64] NdrConformantVaryingArrayMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR): WinApi.PtrSTR;
	(*END NdrConformantVaryingArrayMarshall;*)

	PROCEDURE [ms64] NdrVaryingArrayMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR): WinApi.PtrSTR;
	(*END NdrVaryingArrayMarshall;*)

	PROCEDURE [ms64] NdrComplexArrayMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR): WinApi.PtrSTR;
	(*END NdrComplexArrayMarshall;*)

	PROCEDURE [ms64] NdrNonConformantStringMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR): WinApi.PtrSTR;
	(*END NdrNonConformantStringMarshall;*)

	PROCEDURE [ms64] NdrConformantStringMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR): WinApi.PtrSTR;
	(*END NdrConformantStringMarshall;*)

	PROCEDURE [ms64] NdrEncapsulatedUnionMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR): WinApi.PtrSTR;
	(*END NdrEncapsulatedUnionMarshall;*)

	PROCEDURE [ms64] NdrNonEncapsulatedUnionMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR): WinApi.PtrSTR;
	(*END NdrNonEncapsulatedUnionMarshall;*)

	PROCEDURE [ms64] NdrByteCountPointerMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR): WinApi.PtrSTR;
	(*END NdrByteCountPointerMarshall;*)

	PROCEDURE [ms64] NdrXmitOrRepAsMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR): WinApi.PtrSTR;
	(*END NdrXmitOrRepAsMarshall;*)

	PROCEDURE [ms64] NdrUserMarshalMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR): WinApi.PtrSTR;
	(*END NdrUserMarshalMarshall;*)

	PROCEDURE [ms64] NdrInterfacePointerMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR): WinApi.PtrSTR;
	(*END NdrInterfacePointerMarshall;*)

	PROCEDURE [ms64] NdrClientContextMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; ContextHandle: WinApi.PtrVoid; fCheck: INTEGER);
	(*END NdrClientContextMarshall;*)

	PROCEDURE [ms64] NdrServerContextMarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; ContextHandle: NDR_SCONTEXT; RundownRoutine: NDR_RUNDOWN);
	(*END NdrServerContextMarshall;*)

	PROCEDURE [ms64] NdrSimpleTypeUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; FormatChar: SHORTCHAR);
	(*END NdrSimpleTypeUnmarshall;*)

	PROCEDURE [ms64] NdrPointerUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] ppMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR; fMustAlloc: SHORTCHAR): WinApi.PtrSTR;
	(*END NdrPointerUnmarshall;*)

	PROCEDURE [ms64] NdrSimpleStructUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] ppMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR; fMustAlloc: SHORTCHAR): WinApi.PtrSTR;
	(*END NdrSimpleStructUnmarshall;*)

	PROCEDURE [ms64] NdrConformantStructUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] ppMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR; fMustAlloc: SHORTCHAR): WinApi.PtrSTR;
	(*END NdrConformantStructUnmarshall;*)

	PROCEDURE [ms64] NdrConformantVaryingStructUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] ppMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR; fMustAlloc: SHORTCHAR): WinApi.PtrSTR;
	(*END NdrConformantVaryingStructUnmarshall;*)

	PROCEDURE [ms64] NdrHardStructUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] ppMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR; fMustAlloc: SHORTCHAR): WinApi.PtrSTR;
	(*END NdrHardStructUnmarshall;*)

	PROCEDURE [ms64] NdrComplexStructUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] ppMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR; fMustAlloc: SHORTCHAR): WinApi.PtrSTR;
	(*END NdrComplexStructUnmarshall;*)

	PROCEDURE [ms64] NdrFixedArrayUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] ppMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR; fMustAlloc: SHORTCHAR): WinApi.PtrSTR;
	(*END NdrFixedArrayUnmarshall;*)

	PROCEDURE [ms64] NdrConformantArrayUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] ppMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR; fMustAlloc: SHORTCHAR): WinApi.PtrSTR;
	(*END NdrConformantArrayUnmarshall;*)

	PROCEDURE [ms64] NdrConformantVaryingArrayUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] ppMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR; fMustAlloc: SHORTCHAR): WinApi.PtrSTR;
	(*END NdrConformantVaryingArrayUnmarshall;*)

	PROCEDURE [ms64] NdrVaryingArrayUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] ppMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR; fMustAlloc: SHORTCHAR): WinApi.PtrSTR;
	(*END NdrVaryingArrayUnmarshall;*)

	PROCEDURE [ms64] NdrComplexArrayUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] ppMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR; fMustAlloc: SHORTCHAR): WinApi.PtrSTR;
	(*END NdrComplexArrayUnmarshall;*)

	PROCEDURE [ms64] NdrNonConformantStringUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] ppMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR; fMustAlloc: SHORTCHAR): WinApi.PtrSTR;
	(*END NdrNonConformantStringUnmarshall;*)

	PROCEDURE [ms64] NdrConformantStringUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] ppMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR; fMustAlloc: SHORTCHAR): WinApi.PtrSTR;
	(*END NdrConformantStringUnmarshall;*)

	PROCEDURE [ms64] NdrEncapsulatedUnionUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] ppMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR; fMustAlloc: SHORTCHAR): WinApi.PtrSTR;
	(*END NdrEncapsulatedUnionUnmarshall;*)

	PROCEDURE [ms64] NdrNonEncapsulatedUnionUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] ppMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR; fMustAlloc: SHORTCHAR): WinApi.PtrSTR;
	(*END NdrNonEncapsulatedUnionUnmarshall;*)

	PROCEDURE [ms64] NdrByteCountPointerUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] ppMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR; fMustAlloc: SHORTCHAR): WinApi.PtrSTR;
	(*END NdrByteCountPointerUnmarshall;*)

	PROCEDURE [ms64] NdrXmitOrRepAsUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] ppMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR; fMustAlloc: SHORTCHAR): WinApi.PtrSTR;
	(*END NdrXmitOrRepAsUnmarshall;*)

	PROCEDURE [ms64] NdrUserMarshalUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] ppMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR; fMustAlloc: SHORTCHAR): WinApi.PtrSTR;
	(*END NdrUserMarshalUnmarshall;*)

	PROCEDURE [ms64] NdrInterfacePointerUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] ppMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR; fMustAlloc: SHORTCHAR): WinApi.PtrSTR;
	(*END NdrInterfacePointerUnmarshall;*)

	PROCEDURE [ms64] NdrClientContextUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] pContextHandle: WinApi.PtrVoid; BindHandle: WinApi.PtrVoid);
	(*END NdrClientContextUnmarshall;*)

	PROCEDURE [ms64] NdrServerContextUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE): NDR_SCONTEXT;
	(*END NdrServerContextUnmarshall;*)

	PROCEDURE [ms64] NdrPointerBufferSize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrPointerBufferSize;*)

	PROCEDURE [ms64] NdrSimpleStructBufferSize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrSimpleStructBufferSize;*)

	PROCEDURE [ms64] NdrConformantStructBufferSize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrConformantStructBufferSize;*)

	PROCEDURE [ms64] NdrConformantVaryingStructBufferSize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrConformantVaryingStructBufferSize;*)

	PROCEDURE [ms64] NdrHardStructBufferSize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrHardStructBufferSize;*)

	PROCEDURE [ms64] NdrComplexStructBufferSize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrComplexStructBufferSize;*)

	PROCEDURE [ms64] NdrFixedArrayBufferSize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrFixedArrayBufferSize;*)

	PROCEDURE [ms64] NdrConformantArrayBufferSize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrConformantArrayBufferSize;*)

	PROCEDURE [ms64] NdrConformantVaryingArrayBufferSize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrConformantVaryingArrayBufferSize;*)

	PROCEDURE [ms64] NdrVaryingArrayBufferSize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrVaryingArrayBufferSize;*)

	PROCEDURE [ms64] NdrComplexArrayBufferSize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrComplexArrayBufferSize;*)

	PROCEDURE [ms64] NdrConformantStringBufferSize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrConformantStringBufferSize;*)

	PROCEDURE [ms64] NdrNonConformantStringBufferSize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrNonConformantStringBufferSize;*)

	PROCEDURE [ms64] NdrEncapsulatedUnionBufferSize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrEncapsulatedUnionBufferSize;*)

	PROCEDURE [ms64] NdrNonEncapsulatedUnionBufferSize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrNonEncapsulatedUnionBufferSize;*)

	PROCEDURE [ms64] NdrByteCountPointerBufferSize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrByteCountPointerBufferSize;*)

	PROCEDURE [ms64] NdrXmitOrRepAsBufferSize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrXmitOrRepAsBufferSize;*)

	PROCEDURE [ms64] NdrUserMarshalBufferSize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrUserMarshalBufferSize;*)

	PROCEDURE [ms64] NdrInterfacePointerBufferSize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrInterfacePointerBufferSize;*)

	PROCEDURE [ms64] NdrContextHandleSize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrContextHandleSize;*)

	PROCEDURE [ms64] NdrPointerMemorySize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR): INTEGER;
	(*END NdrPointerMemorySize;*)

	PROCEDURE [ms64] NdrSimpleStructMemorySize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR): INTEGER;
	(*END NdrSimpleStructMemorySize;*)

	PROCEDURE [ms64] NdrConformantStructMemorySize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR): INTEGER;
	(*END NdrConformantStructMemorySize;*)

	PROCEDURE [ms64] NdrConformantVaryingStructMemorySize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR): INTEGER;
	(*END NdrConformantVaryingStructMemorySize;*)

	PROCEDURE [ms64] NdrHardStructMemorySize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR): INTEGER;
	(*END NdrHardStructMemorySize;*)

	PROCEDURE [ms64] NdrComplexStructMemorySize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR): INTEGER;
	(*END NdrComplexStructMemorySize;*)

	PROCEDURE [ms64] NdrFixedArrayMemorySize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR): INTEGER;
	(*END NdrFixedArrayMemorySize;*)

	PROCEDURE [ms64] NdrConformantArrayMemorySize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR): INTEGER;
	(*END NdrConformantArrayMemorySize;*)

	PROCEDURE [ms64] NdrConformantVaryingArrayMemorySize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR): INTEGER;
	(*END NdrConformantVaryingArrayMemorySize;*)

	PROCEDURE [ms64] NdrVaryingArrayMemorySize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR): INTEGER;
	(*END NdrVaryingArrayMemorySize;*)

	PROCEDURE [ms64] NdrComplexArrayMemorySize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR): INTEGER;
	(*END NdrComplexArrayMemorySize;*)

	PROCEDURE [ms64] NdrConformantStringMemorySize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR): INTEGER;
	(*END NdrConformantStringMemorySize;*)

	PROCEDURE [ms64] NdrNonConformantStringMemorySize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR): INTEGER;
	(*END NdrNonConformantStringMemorySize;*)

	PROCEDURE [ms64] NdrEncapsulatedUnionMemorySize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR): INTEGER;
	(*END NdrEncapsulatedUnionMemorySize;*)

	PROCEDURE [ms64] NdrNonEncapsulatedUnionMemorySize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR): INTEGER;
	(*END NdrNonEncapsulatedUnionMemorySize;*)

	PROCEDURE [ms64] NdrXmitOrRepAsMemorySize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR): INTEGER;
	(*END NdrXmitOrRepAsMemorySize;*)

	PROCEDURE [ms64] NdrUserMarshalMemorySize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR): INTEGER;
	(*END NdrUserMarshalMemorySize;*)

	PROCEDURE [ms64] NdrInterfacePointerMemorySize* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR): INTEGER;
	(*END NdrInterfacePointerMemorySize;*)

	PROCEDURE [ms64] NdrPointerFree* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrPointerFree;*)

	PROCEDURE [ms64] NdrSimpleStructFree* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrSimpleStructFree;*)

	PROCEDURE [ms64] NdrConformantStructFree* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrConformantStructFree;*)

	PROCEDURE [ms64] NdrConformantVaryingStructFree* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrConformantVaryingStructFree;*)

	PROCEDURE [ms64] NdrHardStructFree* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrHardStructFree;*)

	PROCEDURE [ms64] NdrComplexStructFree* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrComplexStructFree;*)

	PROCEDURE [ms64] NdrFixedArrayFree* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrFixedArrayFree;*)

	PROCEDURE [ms64] NdrConformantArrayFree* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrConformantArrayFree;*)

	PROCEDURE [ms64] NdrConformantVaryingArrayFree* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrConformantVaryingArrayFree;*)

	PROCEDURE [ms64] NdrVaryingArrayFree* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrVaryingArrayFree;*)

	PROCEDURE [ms64] NdrComplexArrayFree* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrComplexArrayFree;*)

	PROCEDURE [ms64] NdrEncapsulatedUnionFree* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrEncapsulatedUnionFree;*)

	PROCEDURE [ms64] NdrNonEncapsulatedUnionFree* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrNonEncapsulatedUnionFree;*)

	PROCEDURE [ms64] NdrByteCountPointerFree* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrByteCountPointerFree;*)

	PROCEDURE [ms64] NdrXmitOrRepAsFree* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrXmitOrRepAsFree;*)

	PROCEDURE [ms64] NdrUserMarshalFree* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrUserMarshalFree;*)

	PROCEDURE [ms64] NdrInterfacePointerFree* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pMemory: WinApi.PtrSTR; pFormat: WinApi.PtrSTR);
	(*END NdrInterfacePointerFree;*)

	PROCEDURE [ms64] NdrConvert2* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR; NumberParams: INTEGER);
	(*END NdrConvert2;*)

	PROCEDURE [ms64] NdrConvert* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR);
	(*END NdrConvert;*)

	PROCEDURE [ms64] NdrUserMarshalSimpleTypeConvert* (VAR [nil] pFlags: INTEGER; pBuffer: WinApi.PtrSTR; FormatChar: SHORTCHAR): WinApi.PtrSTR;
	(*END NdrUserMarshalSimpleTypeConvert;*)

	PROCEDURE [ms64] NdrClientInitializeNew* (VAR [nil] pRpcMsg: RPC_MESSAGE; VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] pStubDescriptor: MIDL_STUB_DESC; ProcNum: INTEGER);
	(*END NdrClientInitializeNew;*)

	PROCEDURE [ms64] NdrServerInitializeNew* (VAR [nil] pRpcMsg: RPC_MESSAGE; VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] pStubDescriptor: MIDL_STUB_DESC): WinApi.PtrSTR;
	(*END NdrServerInitializeNew;*)

	PROCEDURE [ms64] NdrClientInitialize* (VAR [nil] pRpcMsg: RPC_MESSAGE; VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] pStubDescriptor: MIDL_STUB_DESC; ProcNum: INTEGER);
	(*END NdrClientInitialize;*)

	PROCEDURE [ms64] NdrServerInitialize* (VAR [nil] pRpcMsg: RPC_MESSAGE; VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] pStubDescriptor: MIDL_STUB_DESC): WinApi.PtrSTR;
	(*END NdrServerInitialize;*)

	PROCEDURE [ms64] NdrServerInitializeUnmarshall* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] pStubDescriptor: MIDL_STUB_DESC; VAR [nil] pRpcMsg: RPC_MESSAGE): WinApi.PtrSTR;
	(*END NdrServerInitializeUnmarshall;*)

	PROCEDURE [ms64] NdrServerInitializeMarshall* (VAR [nil] pRpcMsg: RPC_MESSAGE; VAR [nil] pStubMsg: MIDL_STUB_MESSAGE);
	(*END NdrServerInitializeMarshall;*)

	PROCEDURE [ms64] NdrGetBuffer* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; BufferLength: INTEGER; Handle: WinApi.PtrVoid): WinApi.PtrSTR;
	(*END NdrGetBuffer;*)

	PROCEDURE [ms64] NdrNsGetBuffer* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; BufferLength: INTEGER; Handle: WinApi.PtrVoid): WinApi.PtrSTR;
	(*END NdrNsGetBuffer;*)

	PROCEDURE [ms64] NdrSendReceive* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pBufferEnd: WinApi.PtrSTR): WinApi.PtrSTR;
	(*END NdrSendReceive;*)

	PROCEDURE [ms64] NdrNsSendReceive* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pBufferEnd: WinApi.PtrSTR; VAR [nil] pAutoHandle: WinApi.PtrVoid): WinApi.PtrSTR;
	(*END NdrNsSendReceive;*)

	PROCEDURE [ms64] NdrFreeBuffer* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE);
	(*END NdrFreeBuffer;*)

	PROCEDURE [ms64] NdrClientCall2* (VAR [nil] pStubDescriptor: MIDL_STUB_DESC; pFormat: WinApi.PtrSTR): LONGINT;
	(*END NdrClientCall2;*)

	PROCEDURE [ms64] NdrClientCall* (VAR [nil] pStubDescriptor: MIDL_STUB_DESC; pFormat: WinApi.PtrSTR): LONGINT;
	(*END NdrClientCall;*)

	PROCEDURE [ms64] NdrStubCall2* (pThis: WinOle.IRpcStubBuffer; pChannel: WinOle.IRpcChannelBuffer; VAR [nil] pRpcMsg: RPC_MESSAGE; VAR [nil] pdwStubPhase: INTEGER): INTEGER;
	(*END NdrStubCall2;*)

	PROCEDURE [ms64] NdrServerCall2* (VAR [nil] pRpcMsg: RPC_MESSAGE);
	(*END NdrServerCall2;*)

	PROCEDURE [ms64] NdrStubCall* (pThis: WinOle.IRpcStubBuffer; pChannel: WinOle.IRpcChannelBuffer; VAR [nil] pRpcMsg: RPC_MESSAGE; VAR [nil] pdwStubPhase: INTEGER): INTEGER;
	(*END NdrStubCall;*)

	PROCEDURE [ms64] NdrServerCall* (VAR [nil] pRpcMsg: RPC_MESSAGE);
	(*END NdrServerCall;*)

	PROCEDURE [ms64] NdrServerUnmarshall* (pChannel: WinOle.IRpcChannelBuffer; VAR [nil] pRpcMsg: RPC_MESSAGE; VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] pStubDescriptor: MIDL_STUB_DESC; pFormat: WinApi.PtrSTR; pParamList: WinApi.PtrVoid): INTEGER;
	(*END NdrServerUnmarshall;*)

	PROCEDURE [ms64] NdrServerMarshall* (pThis: WinOle.IRpcStubBuffer; pChannel: WinOle.IRpcChannelBuffer; VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR);
	(*END NdrServerMarshall;*)

	PROCEDURE [ms64] NdrMapCommAndFaultStatus* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; VAR [nil] pCommStatus: INTEGER; VAR [nil] pFaultStatus: INTEGER; Status: RPC_STATUS): RPC_STATUS;
	(*END NdrMapCommAndFaultStatus;*)

	PROCEDURE [ms64] RpcSsAllocate* (Size: INTEGER): WinApi.PtrVoid;
	(*END RpcSsAllocate;*)

	PROCEDURE [ms64] RpcSsDisableAllocate* ();
	(*END RpcSsDisableAllocate;*)

	PROCEDURE [ms64] RpcSsEnableAllocate* ();
	(*END RpcSsEnableAllocate;*)

	PROCEDURE [ms64] RpcSsFree* (NodeToFree: WinApi.PtrVoid);
	(*END RpcSsFree;*)

	PROCEDURE [ms64] RpcSsGetThreadHandle* (): WinApi.PtrVoid;
	(*END RpcSsGetThreadHandle;*)

	PROCEDURE [ms64] RpcSsSetClientAllocFree* (ClientAlloc: RPC_CLIENT_ALLOC; ClientFree: RPC_CLIENT_FREE);
	(*END RpcSsSetClientAllocFree;*)

	PROCEDURE [ms64] RpcSsSetThreadHandle* (Id: WinApi.PtrVoid);
	(*END RpcSsSetThreadHandle;*)

	PROCEDURE [ms64] RpcSsSwapClientAllocFree* (ClientAlloc: RPC_CLIENT_ALLOC; ClientFree: RPC_CLIENT_FREE; VAR [nil] OldClientAlloc: RPC_CLIENT_ALLOC; VAR [nil] OldClientFree: RPC_CLIENT_FREE);
	(*END RpcSsSwapClientAllocFree;*)

	PROCEDURE [ms64] RpcSmAllocate* (Size: INTEGER; VAR [nil] pStatus: RPC_STATUS): WinApi.PtrVoid;
	(*END RpcSmAllocate;*)

	PROCEDURE [ms64] RpcSmClientFree* (pNodeToFree: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcSmClientFree;*)

	PROCEDURE [ms64] RpcSmDestroyClientContext* (VAR [nil] ContextHandle: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcSmDestroyClientContext;*)

	PROCEDURE [ms64] RpcSmDisableAllocate* (): RPC_STATUS;
	(*END RpcSmDisableAllocate;*)

	PROCEDURE [ms64] RpcSmEnableAllocate* (): RPC_STATUS;
	(*END RpcSmEnableAllocate;*)

	PROCEDURE [ms64] RpcSmFree* (NodeToFree: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcSmFree;*)

	PROCEDURE [ms64] RpcSmGetThreadHandle* (VAR [nil] pStatus: RPC_STATUS): WinApi.PtrVoid;
	(*END RpcSmGetThreadHandle;*)

	PROCEDURE [ms64] RpcSmSetClientAllocFree* (ClientAlloc: RPC_CLIENT_ALLOC; ClientFree: RPC_CLIENT_FREE): RPC_STATUS;
	(*END RpcSmSetClientAllocFree;*)

	PROCEDURE [ms64] RpcSmSetThreadHandle* (Id: WinApi.PtrVoid): RPC_STATUS;
	(*END RpcSmSetThreadHandle;*)

	PROCEDURE [ms64] RpcSmSwapClientAllocFree* (ClientAlloc: RPC_CLIENT_ALLOC; ClientFree: RPC_CLIENT_FREE; VAR [nil] OldClientAlloc: RPC_CLIENT_ALLOC; VAR [nil] OldClientFree: RPC_CLIENT_FREE): RPC_STATUS;
	(*END RpcSmSwapClientAllocFree;*)

	PROCEDURE [ms64] NdrRpcSsEnableAllocate* (VAR [nil] pMessage: MIDL_STUB_MESSAGE);
	(*END NdrRpcSsEnableAllocate;*)

	PROCEDURE [ms64] NdrRpcSsDisableAllocate* (VAR [nil] pMessage: MIDL_STUB_MESSAGE);
	(*END NdrRpcSsDisableAllocate;*)

	PROCEDURE [ms64] NdrRpcSmSetClientToOsf* (VAR [nil] pMessage: MIDL_STUB_MESSAGE);
	(*END NdrRpcSmSetClientToOsf;*)

	PROCEDURE [ms64] NdrRpcSmClientAllocate* (Size: INTEGER): WinApi.PtrVoid;
	(*END NdrRpcSmClientAllocate;*)

	PROCEDURE [ms64] NdrRpcSmClientFree* (NodeToFree: WinApi.PtrVoid);
	(*END NdrRpcSmClientFree;*)

	PROCEDURE [ms64] NdrRpcSsDefaultAllocate* (Size: INTEGER): WinApi.PtrVoid;
	(*END NdrRpcSsDefaultAllocate;*)

	PROCEDURE [ms64] NdrRpcSsDefaultFree* (NodeToFree: WinApi.PtrVoid);
	(*END NdrRpcSsDefaultFree;*)

	PROCEDURE [ms64] NdrFullPointerXlatInit* (NumberOfPointers: INTEGER; XlatSide: XLAT_SIDE): PtrFULL_PTR_XLAT_TABLES;
	(*END NdrFullPointerXlatInit;*)

	PROCEDURE [ms64] NdrFullPointerXlatFree* (VAR [nil] pXlatTables: FULL_PTR_XLAT_TABLES);
	(*END NdrFullPointerXlatFree;*)

	PROCEDURE [ms64] NdrFullPointerQueryPointer* (VAR [nil] pXlatTables: FULL_PTR_XLAT_TABLES; pPointer: WinApi.PtrVoid; QueryType: SHORTCHAR; VAR [nil] pRefId: INTEGER): INTEGER;
	(*END NdrFullPointerQueryPointer;*)

	PROCEDURE [ms64] NdrFullPointerQueryRefId* (VAR [nil] pXlatTables: FULL_PTR_XLAT_TABLES; RefId: INTEGER; QueryType: SHORTCHAR; VAR [nil] ppPointer: WinApi.PtrVoid): INTEGER;
	(*END NdrFullPointerQueryRefId;*)

	PROCEDURE [ms64] NdrFullPointerInsertRefId* (VAR [nil] pXlatTables: FULL_PTR_XLAT_TABLES; RefId: INTEGER; pPointer: WinApi.PtrVoid);
	(*END NdrFullPointerInsertRefId;*)

	PROCEDURE [ms64] NdrFullPointerFree* (VAR [nil] pXlatTables: FULL_PTR_XLAT_TABLES; Pointer: WinApi.PtrVoid): INTEGER;
	(*END NdrFullPointerFree;*)

	PROCEDURE [ms64] NdrAllocate* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; Len: INTEGER): WinApi.PtrVoid;
	(*END NdrAllocate;*)

	PROCEDURE [ms64] NdrClearOutParameters* (VAR [nil] pStubMsg: MIDL_STUB_MESSAGE; pFormat: WinApi.PtrSTR; ArgAddr: WinApi.PtrVoid);
	(*END NdrClearOutParameters;*)

	PROCEDURE [ms64] NdrOleAllocate* (Size: INTEGER): WinApi.PtrVoid;
	(*END NdrOleAllocate;*)

	PROCEDURE [ms64] NdrOleFree* (NodeToFree: WinApi.PtrVoid);
	(*END NdrOleFree;*)

END WinRpc.
