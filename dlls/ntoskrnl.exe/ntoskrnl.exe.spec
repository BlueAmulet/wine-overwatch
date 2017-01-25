@ stub ExAcquireFastMutexUnsafe
@ stub ExAcquireRundownProtection
@ stub ExAcquireRundownProtectionEx
@ stub ExInitializeRundownProtection
@ stub ExInterlockedAddLargeStatistic
@ stub ExInterlockedCompareExchange64
@ stub ExInterlockedFlushSList
@ stub ExInterlockedPopEntrySList
@ stub ExInterlockedPushEntrySList
@ stub ExReInitializeRundownProtection
@ stub ExReleaseFastMutexUnsafe
@ stub ExReleaseResourceLite
@ stub ExReleaseRundownProtection
@ stub ExReleaseRundownProtectionEx
@ stub ExRundownCompleted
@ stub ExWaitForRundownProtectionRelease
@ stub ExfAcquirePushLockExclusive
@ stub ExfAcquirePushLockShared
@ stub ExfInterlockedAddUlong
@ stub ExfInterlockedCompareExchange64
@ stub ExfInterlockedInsertHeadList
@ stub ExfInterlockedInsertTailList
@ stub ExfInterlockedPopEntryList
@ stub ExfInterlockedPushEntryList
@ stdcall -norelay ExfInterlockedRemoveHeadList(ptr ptr)
@ stub ExfReleasePushLock
@ stub Exfi386InterlockedDecrementLong
@ stub Exfi386InterlockedExchangeUlong
@ stub Exfi386InterlockedIncrementLong
@ stub HalExamineMBR
@ stdcall -norelay InterlockedCompareExchange(ptr long long) NTOSKRNL_InterlockedCompareExchange
@ stdcall -norelay InterlockedDecrement(ptr) NTOSKRNL_InterlockedDecrement
@ stdcall -norelay InterlockedExchange(ptr long) NTOSKRNL_InterlockedExchange
@ stdcall -norelay InterlockedExchangeAdd(ptr long) NTOSKRNL_InterlockedExchangeAdd
@ stdcall -norelay InterlockedIncrement(ptr) NTOSKRNL_InterlockedIncrement
@ stdcall -norelay InterlockedPopEntrySList(ptr) NTOSKRNL_InterlockedPopEntrySList
@ stdcall -norelay InterlockedPushEntrySList(ptr ptr) NTOSKRNL_InterlockedPushEntrySList
@ stub IoAssignDriveLetters
@ stub IoReadPartitionTable
@ stub IoSetPartitionInformation
@ stub IoWritePartitionTable
@ stdcall -norelay IofCallDriver(ptr ptr)
@ stdcall -norelay IofCompleteRequest(ptr long)
@ stub KeAcquireInStackQueuedSpinLockAtDpcLevel
@ stub KeReleaseInStackQueuedSpinLockFromDpcLevel
@ stub KeSetTimeUpdateNotifyRoutine
@ stub KefAcquireSpinLockAtDpcLevel
@ stub KefReleaseSpinLockFromDpcLevel
@ stub KiAcquireSpinLock
@ stub KiReleaseSpinLock
@ stdcall -norelay ObfDereferenceObject(ptr)
@ stdcall -norelay ObfReferenceObject(ptr)
@ stub RtlPrefetchMemoryNonTemporal
@ cdecl -i386 -norelay RtlUlongByteSwap() ntdll.RtlUlongByteSwap
@ cdecl -ret64 RtlUlonglongByteSwap(int64) ntdll.RtlUlonglongByteSwap
@ cdecl -i386 -norelay RtlUshortByteSwap() ntdll.RtlUshortByteSwap
@ stub WmiGetClock
@ stub Kei386EoiHelper
@ stub Kii386SpinOnSpinLock
@ stub CcCanIWrite
@ stub CcCopyRead
@ stub CcCopyWrite
@ stub CcDeferWrite
@ stub CcFastCopyRead
@ stub CcFastCopyWrite
@ stub CcFastMdlReadWait
@ stub CcFastReadNotPossible
@ stub CcFastReadWait
@ stub CcFlushCache
@ stub CcGetDirtyPages
@ stub CcGetFileObjectFromBcb
@ stub CcGetFileObjectFromSectionPtrs
@ stub CcGetFlushedValidData
@ stub CcGetLsnForFileObject
@ stub CcInitializeCacheMap
@ stub CcIsThereDirtyData
@ stub CcMapData
@ stub CcMdlRead
@ stub CcMdlReadComplete
@ stub CcMdlWriteAbort
@ stub CcMdlWriteComplete
@ stub CcPinMappedData
@ stub CcPinRead
@ stub CcPrepareMdlWrite
@ stub CcPreparePinWrite
@ stub CcPurgeCacheSection
@ stub CcRemapBcb
@ stub CcRepinBcb
@ stub CcScheduleReadAhead
@ stub CcSetAdditionalCacheAttributes
@ stub CcSetBcbOwnerPointer
@ stub CcSetDirtyPageThreshold
@ stub CcSetDirtyPinnedData
@ stub CcSetFileSizes
@ stub CcSetLogHandleForFile
@ stub CcSetReadAheadGranularity
@ stub CcUninitializeCacheMap
@ stub CcUnpinData
@ stub CcUnpinDataForThread
@ stub CcUnpinRepinnedBcb
@ stub CcWaitForCurrentLazyWriterActivity
@ stub CcZeroData
@ stdcall CmRegisterCallback(ptr ptr ptr)
@ stdcall CmUnRegisterCallback(int64)
@ stdcall DbgBreakPoint() ntdll.DbgBreakPoint
@ stub DbgBreakPointWithStatus
@ stub DbgLoadImageSymbols
@ varargs DbgPrint(str) ntdll.DbgPrint
@ varargs DbgPrintEx(long long str) ntdll.DbgPrintEx
@ stub DbgPrintReturnControlC
@ stub DbgPrompt
@ stub DbgQueryDebugFilterState
@ stub DbgSetDebugFilterState
@ stdcall ExAcquireResourceExclusiveLite(ptr long)
@ stub ExAcquireResourceSharedLite
@ stub ExAcquireSharedStarveExclusive
@ stub ExAcquireSharedWaitForExclusive
@ stub ExAllocateFromPagedLookasideList
@ stdcall ExAllocatePool(long long)
@ stdcall ExAllocatePoolWithQuota(long long)
@ stdcall ExAllocatePoolWithQuotaTag(long long long)
@ stdcall ExAllocatePoolWithTag(long long long)
@ stub ExAllocatePoolWithTagPriority
@ stub ExConvertExclusiveToSharedLite
@ stdcall ExCreateCallback(ptr ptr long long)
@ stdcall ExDeleteNPagedLookasideList(ptr)
@ stdcall ExDeletePagedLookasideList(ptr)
@ stdcall ExDeleteResourceLite(ptr)
@ stub ExDesktopObjectType
@ stub ExDisableResourceBoostLite
@ stub ExEnumHandleTable
@ stub ExEventObjectType
@ stub ExExtendZone
@ stdcall ExFreePool(ptr)
@ stdcall ExFreePoolWithTag(ptr long)
@ stub ExFreeToPagedLookasideList
@ stub ExGetCurrentProcessorCounts
@ stub ExGetCurrentProcessorCpuUsage
@ stub ExGetExclusiveWaiterCount
@ stub ExGetPreviousMode
@ stub ExGetSharedWaiterCount
@ stdcall ExInitializeNPagedLookasideList(ptr ptr ptr long long long long)
@ stdcall ExInitializePagedLookasideList(ptr ptr ptr long long long long)
@ stdcall ExInitializeResourceLite(ptr)
@ stdcall ExInitializeZone(ptr long ptr long)
@ stub ExInterlockedAddLargeInteger
@ stub ExInterlockedAddUlong
@ stub ExInterlockedDecrementLong
@ stub ExInterlockedExchangeUlong
@ stub ExInterlockedExtendZone
@ stub ExInterlockedIncrementLong
@ stub ExInterlockedInsertHeadList
@ stub ExInterlockedInsertTailList
@ stub ExInterlockedPopEntryList
@ stub ExInterlockedPushEntryList
@ stdcall ExInterlockedRemoveHeadList(ptr ptr)
@ stub ExIsProcessorFeaturePresent
@ stub ExIsResourceAcquiredExclusiveLite
@ stub ExIsResourceAcquiredSharedLite
@ stdcall ExLocalTimeToSystemTime(ptr ptr) ntdll.RtlLocalTimeToSystemTime
@ stub ExNotifyCallback
@ stub ExQueryPoolBlockSize
@ stub ExQueueWorkItem
@ stub ExRaiseAccessViolation
@ stub ExRaiseDatatypeMisalignment
@ stub ExRaiseException
@ stub ExRaiseHardError
@ stub ExRaiseStatus
@ stub ExRegisterCallback
@ stub ExReinitializeResourceLite
@ stdcall ExReleaseResourceForThreadLite(ptr long)
@ stub ExSemaphoreObjectType
@ stub ExSetResourceOwnerPointer
@ stub ExSetTimerResolution
@ stub ExSystemExceptionFilter
@ stdcall ExSystemTimeToLocalTime(ptr ptr) ntdll.RtlSystemTimeToLocalTime
@ stub ExUnregisterCallback
@ stub ExUuidCreate
@ stub ExVerifySuite
@ stub ExWindowStationObjectType
@ stub Exi386InterlockedDecrementLong
@ stub Exi386InterlockedExchangeUlong
@ stub Exi386InterlockedIncrementLong
@ stub FsRtlAcquireFileExclusive
@ stub FsRtlAddLargeMcbEntry
@ stub FsRtlAddMcbEntry
@ stub FsRtlAddToTunnelCache
@ stub FsRtlAllocateFileLock
@ stub FsRtlAllocatePool
@ stub FsRtlAllocatePoolWithQuota
@ stub FsRtlAllocatePoolWithQuotaTag
@ stub FsRtlAllocatePoolWithTag
@ stub FsRtlAllocateResource
@ stub FsRtlAreNamesEqual
@ stub FsRtlBalanceReads
@ stub FsRtlCheckLockForReadAccess
@ stub FsRtlCheckLockForWriteAccess
@ stub FsRtlCheckOplock
@ stub FsRtlCopyRead
@ stub FsRtlCopyWrite
@ stub FsRtlCurrentBatchOplock
@ stub FsRtlDeleteKeyFromTunnelCache
@ stub FsRtlDeleteTunnelCache
@ stub FsRtlDeregisterUncProvider
@ stub FsRtlDissectDbcs
@ stub FsRtlDissectName
@ stub FsRtlDoesDbcsContainWildCards
@ stub FsRtlDoesNameContainWildCards
@ stub FsRtlFastCheckLockForRead
@ stub FsRtlFastCheckLockForWrite
@ stub FsRtlFastUnlockAll
@ stub FsRtlFastUnlockAllByKey
@ stub FsRtlFastUnlockSingle
@ stub FsRtlFindInTunnelCache
@ stub FsRtlFreeFileLock
@ stub FsRtlGetFileSize
@ stub FsRtlGetNextFileLock
@ stub FsRtlGetNextLargeMcbEntry
@ stub FsRtlGetNextMcbEntry
@ stub FsRtlIncrementCcFastReadNoWait
@ stub FsRtlIncrementCcFastReadNotPossible
@ stub FsRtlIncrementCcFastReadResourceMiss
@ stub FsRtlIncrementCcFastReadWait
@ stub FsRtlInitializeFileLock
@ stub FsRtlInitializeLargeMcb
@ stub FsRtlInitializeMcb
@ stub FsRtlInitializeOplock
@ stub FsRtlInitializeTunnelCache
@ stub FsRtlInsertPerFileObjectContext
@ stub FsRtlInsertPerStreamContext
@ stub FsRtlIsDbcsInExpression
@ stub FsRtlIsFatDbcsLegal
@ stub FsRtlIsHpfsDbcsLegal
@ stub FsRtlIsNameInExpression
@ stub FsRtlIsNtstatusExpected
@ stub FsRtlIsPagingFile
@ stub FsRtlIsTotalDeviceFailure
@ stub FsRtlLegalAnsiCharacterArray
@ stub FsRtlLookupLargeMcbEntry
@ stub FsRtlLookupLastLargeMcbEntry
@ stub FsRtlLookupLastLargeMcbEntryAndIndex
@ stub FsRtlLookupLastMcbEntry
@ stub FsRtlLookupMcbEntry
@ stub FsRtlLookupPerFileObjectContext
@ stub FsRtlLookupPerStreamContextInternal
@ stub FsRtlMdlRead
@ stub FsRtlMdlReadComplete
@ stub FsRtlMdlReadCompleteDev
@ stub FsRtlMdlReadDev
@ stub FsRtlMdlWriteComplete
@ stub FsRtlMdlWriteCompleteDev
@ stub FsRtlNormalizeNtstatus
@ stub FsRtlNotifyChangeDirectory
@ stub FsRtlNotifyCleanup
@ stub FsRtlNotifyFilterChangeDirectory
@ stub FsRtlNotifyFilterReportChange
@ stub FsRtlNotifyFullChangeDirectory
@ stub FsRtlNotifyFullReportChange
@ stub FsRtlNotifyInitializeSync
@ stub FsRtlNotifyReportChange
@ stub FsRtlNotifyUninitializeSync
@ stub FsRtlNotifyVolumeEvent
@ stub FsRtlNumberOfRunsInLargeMcb
@ stub FsRtlNumberOfRunsInMcb
@ stub FsRtlOplockFsctrl
@ stub FsRtlOplockIsFastIoPossible
@ stub FsRtlPostPagingFileStackOverflow
@ stub FsRtlPostStackOverflow
@ stub FsRtlPrepareMdlWrite
@ stub FsRtlPrepareMdlWriteDev
@ stub FsRtlPrivateLock
@ stub FsRtlProcessFileLock
@ stub FsRtlRegisterFileSystemFilterCallbacks
@ stdcall FsRtlRegisterUncProvider(ptr ptr long)
@ stub FsRtlReleaseFile
@ stub FsRtlRemoveLargeMcbEntry
@ stub FsRtlRemoveMcbEntry
@ stub FsRtlRemovePerFileObjectContext
@ stub FsRtlRemovePerStreamContext
@ stub FsRtlResetLargeMcb
@ stub FsRtlSplitLargeMcb
@ stub FsRtlSyncVolumes
@ stub FsRtlTeardownPerStreamContexts
@ stub FsRtlTruncateLargeMcb
@ stub FsRtlTruncateMcb
@ stub FsRtlUninitializeFileLock
@ stub FsRtlUninitializeLargeMcb
@ stub FsRtlUninitializeMcb
@ stub FsRtlUninitializeOplock
@ stub HalDispatchTable
@ stub HalPrivateDispatchTable
@ stub HeadlessDispatch
@ stub InbvAcquireDisplayOwnership
@ stub InbvCheckDisplayOwnership
@ stub InbvDisplayString
@ stub InbvEnableBootDriver
@ stub InbvEnableDisplayString
@ stub InbvInstallDisplayStringFilter
@ stub InbvIsBootDriverInstalled
@ stub InbvNotifyDisplayOwnershipLost
@ stub InbvResetDisplay
@ stub InbvSetScrollRegion
@ stub InbvSetTextColor
@ stub InbvSolidColorFill
@ extern InitSafeBootMode
@ stdcall IoAcquireCancelSpinLock(ptr)
@ stdcall IoAcquireRemoveLockEx(ptr ptr ptr long long)
@ stub IoAcquireVpbSpinLock
@ stub IoAdapterObjectType
@ stub IoAllocateAdapterChannel
@ stub IoAllocateController
@ stdcall IoAllocateDriverObjectExtension(ptr ptr long ptr)
@ stdcall IoAllocateErrorLogEntry(ptr long)
@ stdcall IoAllocateIrp(long long)
@ stdcall IoAllocateMdl(ptr long long long ptr)
@ stdcall IoAllocateWorkItem(ptr)
@ stub IoAssignResources
@ stdcall IoAttachDevice(ptr ptr ptr)
@ stub IoAttachDeviceByPointer
@ stdcall IoAttachDeviceToDeviceStack(ptr ptr)
@ stub IoAttachDeviceToDeviceStackSafe
@ stub IoBuildAsynchronousFsdRequest
@ stdcall IoBuildDeviceIoControlRequest(long ptr ptr long ptr long long ptr ptr)
@ stub IoBuildPartialMdl
@ stdcall IoBuildSynchronousFsdRequest(long ptr ptr long ptr ptr ptr)
@ stdcall IoCallDriver(ptr ptr)
@ stub IoCancelFileOpen
@ stub IoCancelIrp
@ stub IoCheckDesiredAccess
@ stub IoCheckEaBufferValidity
@ stub IoCheckFunctionAccess
@ stub IoCheckQuerySetFileInformation
@ stub IoCheckQuerySetVolumeInformation
@ stub IoCheckQuotaBufferValidity
@ stub IoCheckShareAccess
@ stdcall IoCompleteRequest(ptr long)
@ stub IoConnectInterrupt
@ stub IoCreateController
@ stdcall IoCreateDevice(ptr long ptr long long long ptr)
@ stub IoCreateDisk
@ stdcall IoCreateDriver(ptr ptr)
@ stub IoCreateFile
@ stub IoCreateFileSpecifyDeviceObjectHint
@ stub IoCreateNotificationEvent
@ stub IoCreateStreamFileObject
@ stub IoCreateStreamFileObjectEx
@ stub IoCreateStreamFileObjectLite
@ stdcall IoCreateSymbolicLink(ptr ptr)
@ stdcall IoCreateSynchronizationEvent(ptr ptr)
@ stub IoCreateUnprotectedSymbolicLink
@ stdcall IoCsqInitialize(ptr ptr ptr ptr ptr ptr ptr)
@ stub IoCsqInsertIrp
@ stub IoCsqRemoveIrp
@ stub IoCsqRemoveNextIrp
@ stub IoDeleteController
@ stdcall IoDeleteDevice(ptr)
@ stdcall IoDeleteDriver(ptr)
@ stdcall IoDeleteSymbolicLink(ptr)
@ stub IoDetachDevice
@ stub IoDeviceHandlerObjectSize
@ stub IoDeviceHandlerObjectType
@ stub IoDeviceObjectType
@ stub IoDisconnectInterrupt
@ stub IoDriverObjectType
@ stub IoEnqueueIrp
@ stub IoEnumerateDeviceObjectList
@ stub IoFastQueryNetworkAttributes
@ stub IoFileObjectType
@ stub IoForwardAndCatchIrp
@ stub IoForwardIrpSynchronously
@ stub IoFreeController
@ stub IoFreeErrorLogEntry
@ stdcall IoFreeIrp(ptr)
@ stdcall IoFreeMdl(ptr)
@ stub IoFreeWorkItem
@ stdcall IoGetAttachedDevice(ptr)
@ stdcall IoGetAttachedDeviceReference(ptr)
@ stub IoGetBaseFileSystemDeviceObject
@ stub IoGetBootDiskInformation
@ stdcall IoGetConfigurationInformation()
@ stdcall IoGetCurrentProcess()
@ stub IoGetDeviceAttachmentBaseRef
@ stub IoGetDeviceInterfaceAlias
@ stdcall IoGetDeviceInterfaces(ptr ptr long ptr)
@ stdcall IoGetDeviceObjectPointer(ptr long ptr ptr)
@ stdcall IoGetDeviceProperty(ptr long long ptr ptr)
@ stub IoGetDeviceToVerify
@ stub IoGetDiskDeviceObject
@ stub IoGetDmaAdapter
@ stdcall IoGetDriverObjectExtension(ptr ptr)
@ stub IoGetFileObjectGenericMapping
@ stub IoGetInitialStack
@ stub IoGetLowerDeviceObject
@ stdcall IoGetRelatedDeviceObject(ptr)
@ stub IoGetRequestorProcess
@ stub IoGetRequestorProcessId
@ stub IoGetRequestorSessionId
@ stub IoGetStackLimits
@ stub IoGetTopLevelIrp
@ stdcall IoInitializeIrp(ptr long long)
@ stdcall IoInitializeRemoveLockEx(ptr long long long long)
@ stdcall IoInitializeTimer(ptr ptr ptr)
@ stdcall IoInvalidateDeviceRelations(ptr long)
@ stub IoInvalidateDeviceState
@ stub IoIsFileOriginRemote
@ stub IoIsOperationSynchronous
@ stub IoIsSystemThread
@ stub IoIsValidNameGraftingBuffer
@ stdcall IoIsWdmVersionAvailable(long long)
@ stub IoMakeAssociatedIrp
@ stub IoOpenDeviceInterfaceRegistryKey
@ stub IoOpenDeviceRegistryKey
@ stub IoPageRead
@ stub IoPnPDeliverServicePowerNotification
@ stdcall IoQueryDeviceDescription(ptr ptr ptr ptr ptr ptr ptr ptr)
@ stub IoQueryFileDosDeviceName
@ stub IoQueryFileInformation
@ stub IoQueryVolumeInformation
@ stub IoQueueThreadIrp
@ stub IoQueueWorkItem
@ stub IoRaiseHardError
@ stub IoRaiseInformationalHardError
@ stub IoReadDiskSignature
@ stub IoReadOperationCount
@ stub IoReadPartitionTableEx
@ stub IoReadTransferCount
@ stub IoRegisterBootDriverReinitialization
@ stub IoRegisterDeviceInterface
@ stdcall IoRegisterDriverReinitialization(ptr ptr ptr)
@ stdcall IoRegisterFileSystem(ptr)
@ stub IoRegisterFsRegistrationChange
@ stub IoRegisterLastChanceShutdownNotification
@ stdcall IoRegisterPlugPlayNotification(long long ptr ptr ptr ptr ptr)
@ stdcall IoRegisterShutdownNotification(ptr)
@ stdcall IoReleaseCancelSpinLock(ptr)
@ stub IoReleaseRemoveLockAndWaitEx
@ stub IoReleaseRemoveLockEx
@ stub IoReleaseVpbSpinLock
@ stub IoRemoveShareAccess
@ stub IoReportDetectedDevice
@ stub IoReportHalResourceUsage
@ stub IoReportResourceForDetection
@ stdcall IoReportResourceUsage(ptr ptr ptr long ptr ptr long long ptr)
@ stub IoReportTargetDeviceChange
@ stub IoReportTargetDeviceChangeAsynchronous
@ stub IoRequestDeviceEject
@ stub IoReuseIrp
@ stub IoSetCompletionRoutineEx
@ stub IoSetDeviceInterfaceState
@ stub IoSetDeviceToVerify
@ stub IoSetFileOrigin
@ stub IoSetHardErrorOrVerifyDevice
@ stub IoSetInformation
@ stub IoSetIoCompletion
@ stub IoSetPartitionInformationEx
@ stub IoSetShareAccess
@ stub IoSetStartIoAttributes
@ stub IoSetSystemPartition
@ stdcall IoSetThreadHardErrorMode(long)
@ stub IoSetTopLevelIrp
@ stdcall IoStartNextPacket(ptr long)
@ stub IoStartNextPacketByKey
@ stub IoStartPacket
@ stdcall IoStartTimer(ptr)
@ stub IoStatisticsLock
@ stub IoStopTimer
@ stub IoSynchronousInvalidateDeviceRelations
@ stub IoSynchronousPageWrite
@ stub IoThreadToProcess
@ stdcall IoUnregisterFileSystem(ptr)
@ stub IoUnregisterFsRegistrationChange
@ stub IoUnregisterPlugPlayNotification
@ stdcall IoUnregisterShutdownNotification(ptr)
@ stub IoUpdateShareAccess
@ stub IoValidateDeviceIoControlAccess
@ stub IoVerifyPartitionTable
@ stub IoVerifyVolume
@ stub IoVolumeDeviceToDosName
@ stub IoWMIAllocateInstanceIds
@ stub IoWMIDeviceObjectToInstanceName
@ stub IoWMIExecuteMethod
@ stub IoWMIHandleToInstanceName
@ stub IoWMIOpenBlock
@ stub IoWMIQueryAllData
@ stub IoWMIQueryAllDataMultiple
@ stub IoWMIQuerySingleInstance
@ stub IoWMIQuerySingleInstanceMultiple
@ stdcall IoWMIRegistrationControl(ptr long)
@ stub IoWMISetNotificationCallback
@ stub IoWMISetSingleInstance
@ stub IoWMISetSingleItem
@ stub IoWMISuggestInstanceName
@ stub IoWMIWriteEvent
@ stub IoWriteErrorLogEntry
@ stub IoWriteOperationCount
@ stub IoWritePartitionTableEx
@ stub IoWriteTransferCount
@ extern KdDebuggerEnabled
@ stub KdDebuggerNotPresent
@ stub KdDisableDebugger
@ stub KdEnableDebugger
@ stub KdEnteredDebugger
@ stub KdPollBreakIn
@ stub KdPowerTransition
@ stub Ke386CallBios
@ stdcall Ke386IoSetAccessProcess(ptr long)
@ stub Ke386QueryIoAccessMap
@ stdcall Ke386SetIoAccessMap(long ptr)
@ stub KeAcquireInterruptSpinLock
@ stub KeAcquireSpinLockAtDpcLevel
@ stub KeAddSystemServiceTable
@ stub KeAreApcsDisabled
@ stub KeAttachProcess
@ stub KeBugCheck
@ stub KeBugCheckEx
@ stub KeCancelTimer
@ stub KeCapturePersistentThreadState
@ stub KeClearEvent
@ stub KeConnectInterrupt
@ stub KeDcacheFlushCount
@ stdcall KeDelayExecutionThread(long long ptr)
@ stub KeDeregisterBugCheckCallback
@ stub KeDeregisterBugCheckReasonCallback
@ stub KeDetachProcess
@ stub KeDisconnectInterrupt
@ stdcall KeEnterCriticalRegion()
@ stub KeEnterKernelDebugger
@ stub KeFindConfigurationEntry
@ stub KeFindConfigurationNextEntry
@ stub KeFlushEntireTb
@ stub KeFlushQueuedDpcs
@ stdcall KeGetCurrentThread()
@ stub KeGetPreviousMode
@ stub KeGetRecommendedSharedDataAlignment
@ stub KeI386AbiosCall
@ stub KeI386AllocateGdtSelectors
@ stub KeI386Call16BitCStyleFunction
@ stub KeI386Call16BitFunction
@ stub KeI386FlatToGdtSelector
@ stub KeI386GetLid
@ stub KeI386MachineType
@ stub KeI386ReleaseGdtSelectors
@ stub KeI386ReleaseLid
@ stub KeI386SetGdtSelector
@ stub KeIcacheFlushCount
@ stub KeInitializeApc
@ stub KeInitializeDeviceQueue
@ stdcall KeInitializeDpc(ptr ptr ptr)
@ stdcall KeInitializeEvent(ptr long long)
@ stub KeInitializeInterrupt
@ stub KeInitializeMutant
@ stdcall KeInitializeMutex(ptr long)
@ stub KeInitializeQueue
@ stdcall KeInitializeSemaphore(ptr long long)
@ stdcall KeInitializeSpinLock(ptr)
@ stdcall KeInitializeTimer(ptr)
@ stdcall KeInitializeTimerEx(ptr long)
@ stub KeInsertByKeyDeviceQueue
@ stub KeInsertDeviceQueue
@ stub KeInsertHeadQueue
@ stdcall KeInsertQueue(ptr ptr)
@ stub KeInsertQueueApc
@ stub KeInsertQueueDpc
@ stub KeIsAttachedProcess
@ stub KeIsExecutingDpc
@ stdcall KeLeaveCriticalRegion()
@ stub KeLoaderBlock
@ stub KeNumberProcessors
@ stub KeProfileInterrupt
@ stub KeProfileInterruptWithSource
@ stub KePulseEvent
@ stdcall KeQueryActiveProcessors()
@ stdcall KeQueryInterruptTime()
@ stub KeQueryPriorityThread
@ stub KeQueryRuntimeThread
@ stdcall KeQuerySystemTime(ptr)
@ stdcall KeQueryTickCount(ptr)
@ stdcall KeQueryTimeIncrement()
@ stub KeRaiseUserException
@ stub KeReadStateEvent
@ stub KeReadStateMutant
@ stub KeReadStateMutex
@ stub KeReadStateQueue
@ stub KeReadStateSemaphore
@ stub KeReadStateTimer
@ stub KeRegisterBugCheckCallback
@ stub KeRegisterBugCheckReasonCallback
@ stub KeReleaseInterruptSpinLock
@ stub KeReleaseMutant
@ stdcall KeReleaseMutex(ptr long)
@ stdcall KeReleaseSemaphore(ptr long long long)
@ stub KeReleaseSpinLockFromDpcLevel
@ stub KeRemoveByKeyDeviceQueue
@ stub KeRemoveByKeyDeviceQueueIfBusy
@ stub KeRemoveDeviceQueue
@ stub KeRemoveEntryDeviceQueue
@ stub KeRemoveQueue
@ stub KeRemoveQueueDpc
@ stub KeRemoveSystemServiceTable
@ stdcall KeResetEvent(ptr)
@ stub KeRestoreFloatingPointState
@ stub KeRevertToUserAffinityThread
@ stub KeRundownQueue
@ stub KeSaveFloatingPointState
@ stub KeSaveStateForHibernate
@ extern KeServiceDescriptorTable
@ stub KeSetAffinityThread
@ stub KeSetBasePriorityThread
@ stub KeSetDmaIoCoherency
@ stdcall KeSetEvent(ptr long long)
@ stub KeSetEventBoostPriority
@ stub KeSetIdealProcessorThread
@ stub KeSetImportanceDpc
@ stub KeSetKernelStackSwapEnable
@ stdcall KeSetPriorityThread(ptr long)
@ stub KeSetProfileIrql
@ stdcall KeSetSystemAffinityThread(long)
@ stub KeSetTargetProcessorDpc
@ stub KeSetTimeIncrement
@ stub KeSetTimer
@ stdcall KeSetTimerEx(ptr int64 long ptr)
@ stub KeStackAttachProcess
@ stub KeSynchronizeExecution
@ stub KeTerminateThread
@ extern KeTickCount
@ stub KeUnstackDetachProcess
@ stub KeUpdateRunTime
@ stub KeUpdateSystemTime
@ stub KeUserModeCallback
@ stub KeWaitForMultipleObjects
@ stdcall KeWaitForMutexObject(ptr long long long ptr)
@ stdcall KeWaitForSingleObject(ptr long long long ptr)
@ stub KiBugCheckData
@ stub KiCoprocessorError
@ stub KiDeliverApc
@ stub KiDispatchInterrupt
@ stub KiEnableTimerWatchdog
@ stub KiIpiServiceRoutine
@ stub KiUnexpectedInterrupt
@ stdcall LdrAccessResource(long ptr ptr ptr) ntdll.LdrAccessResource
@ stub LdrEnumResources
@ stdcall LdrFindResourceDirectory_U(long ptr long ptr) ntdll.LdrFindResourceDirectory_U
@ stdcall LdrFindResource_U(long ptr long ptr) ntdll.LdrFindResource_U
@ stub LpcPortObjectType
@ stub LpcRequestPort
@ stub LpcRequestWaitReplyPort
@ stub LsaCallAuthenticationPackage
@ stub LsaDeregisterLogonProcess
@ stub LsaFreeReturnBuffer
@ stub LsaLogonUser
@ stub LsaLookupAuthenticationPackage
@ stub LsaRegisterLogonProcess
@ stub Mm64BitPhysicalAddress
@ stub MmAddPhysicalMemory
@ stub MmAddVerifierThunks
@ stub MmAdjustWorkingSetSize
@ stub MmAdvanceMdl
@ stdcall MmAllocateContiguousMemory(long int64)
@ stdcall MmAllocateContiguousMemorySpecifyCache(long int64 int64 int64 long)
@ stub MmAllocateMappingAddress
@ stdcall MmAllocateNonCachedMemory(long)
@ stdcall MmAllocatePagesForMdl(int64 int64 int64 long)
@ stub MmBuildMdlForNonPagedPool
@ stub MmCanFileBeTruncated
@ stub MmCommitSessionMappedView
@ stub MmCreateMdl
@ stub MmCreateSection
@ stub MmDisableModifiedWriteOfSection
@ stub MmFlushImageSection
@ stub MmForceSectionClosed
@ stub MmFreeContiguousMemory
@ stub MmFreeContiguousMemorySpecifyCache
@ stub MmFreeMappingAddress
@ stdcall MmFreeNonCachedMemory(ptr long)
@ stub MmFreePagesFromMdl
@ stub MmGetPhysicalAddress
@ stub MmGetPhysicalMemoryRanges
@ stdcall MmGetSystemRoutineAddress(ptr)
@ stub MmGetVirtualForPhysical
@ stub MmGrowKernelStack
@ stub MmHighestUserAddress
@ stdcall MmIsAddressValid(ptr)
@ stub MmIsDriverVerifying
@ stub MmIsNonPagedSystemAddressValid
@ stub MmIsRecursiveIoFault
@ stub MmIsThisAnNtAsSystem
@ stub MmIsVerifierEnabled
@ stub MmLockPagableDataSection
@ stub MmLockPagableImageSection
@ stdcall MmLockPagableSectionByHandle(ptr)
@ stdcall MmMapIoSpace(int64 long long)
@ stub MmMapLockedPages
@ stdcall MmMapLockedPagesSpecifyCache(ptr long long ptr long long)
@ stub MmMapLockedPagesWithReservedMapping
@ stub MmMapMemoryDumpMdl
@ stub MmMapUserAddressesToPage
@ stub MmMapVideoDisplay
@ stub MmMapViewInSessionSpace
@ stub MmMapViewInSystemSpace
@ stub MmMapViewOfSection
@ stub MmMarkPhysicalMemoryAsBad
@ stub MmMarkPhysicalMemoryAsGood
@ stdcall MmPageEntireDriver(ptr)
@ stub MmPrefetchPages
@ stdcall MmProbeAndLockPages(ptr long long)
@ stub MmProbeAndLockProcessPages
@ stub MmProbeAndLockSelectedPages
@ stub MmProtectMdlSystemAddress
@ stdcall MmQuerySystemSize()
@ stub MmRemovePhysicalMemory
@ stdcall MmResetDriverPaging(ptr)
@ stub MmSectionObjectType
@ stub MmSecureVirtualMemory
@ stub MmSetAddressRangeModified
@ stub MmSetBankedSection
@ stub MmSizeOfMdl
@ stub MmSystemRangeStart
@ stub MmTrimAllSystemPagableMemory
@ stdcall MmUnlockPagableImageSection(ptr)
@ stdcall MmUnlockPages(ptr)
@ stdcall MmUnmapIoSpace(ptr long)
@ stub MmUnmapLockedPages
@ stub MmUnmapReservedMapping
@ stub MmUnmapVideoDisplay
@ stub MmUnmapViewInSessionSpace
@ stub MmUnmapViewInSystemSpace
@ stub MmUnmapViewOfSection
@ stub MmUnsecureVirtualMemory
@ stub MmUserProbeAddress
@ extern NlsAnsiCodePage ntdll.NlsAnsiCodePage
@ stub NlsLeadByteInfo
@ extern NlsMbCodePageTag ntdll.NlsMbCodePageTag
@ extern NlsMbOemCodePageTag ntdll.NlsMbOemCodePageTag
@ stub NlsOemCodePage
@ stub NlsOemLeadByteInfo
@ stdcall NtAddAtom(ptr long ptr) ntdll.NtAddAtom
@ stdcall NtAdjustPrivilegesToken(long long long long long long) ntdll.NtAdjustPrivilegesToken
@ stdcall NtAllocateLocallyUniqueId(ptr) ntdll.NtAllocateLocallyUniqueId
@ stdcall NtAllocateUuids(ptr ptr ptr ptr) ntdll.NtAllocateUuids
@ stdcall NtAllocateVirtualMemory(long ptr ptr ptr long long) ntdll.NtAllocateVirtualMemory
@ stub NtBuildNumber
@ stdcall NtClose(long) ntdll.NtClose
@ stdcall NtConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr) ntdll.NtConnectPort
@ stdcall NtCreateEvent(long long long long long) ntdll.NtCreateEvent
@ stdcall NtCreateFile(ptr long ptr ptr ptr long long long long ptr long) ntdll.NtCreateFile
@ stdcall NtCreateSection(ptr long ptr ptr long long long) ntdll.NtCreateSection
@ stdcall NtDeleteAtom(long) ntdll.NtDeleteAtom
@ stdcall NtDeleteFile(ptr) ntdll.NtDeleteFile
@ stdcall NtDeviceIoControlFile(long long ptr ptr ptr long ptr long ptr long) ntdll.NtDeviceIoControlFile
@ stdcall NtDuplicateObject(long long long ptr long long long) ntdll.NtDuplicateObject
@ stdcall NtDuplicateToken(long long long long long long) ntdll.NtDuplicateToken
@ stdcall NtFindAtom(ptr long ptr) ntdll.NtFindAtom
@ stdcall NtFreeVirtualMemory(long ptr ptr long) ntdll.NtFreeVirtualMemory
@ stdcall NtFsControlFile(long long long long long long long long long long) ntdll.NtFsControlFile
@ stub NtGlobalFlag
@ stdcall NtLockFile(long long ptr ptr ptr ptr ptr ptr long long) ntdll.NtLockFile
@ stub NtMakePermanentObject
@ stdcall NtMapViewOfSection(long long ptr long long ptr ptr long long long) ntdll.NtMapViewOfSection
@ stdcall NtNotifyChangeDirectoryFile(long long ptr ptr ptr ptr long long long) ntdll.NtNotifyChangeDirectoryFile
@ stdcall NtOpenFile(ptr long ptr ptr long long) ntdll.NtOpenFile
@ stdcall NtOpenProcess(ptr long ptr ptr) ntdll.NtOpenProcess
@ stdcall NtOpenProcessToken(long long ptr) ntdll.NtOpenProcessToken
@ stdcall NtOpenProcessTokenEx(long long long ptr) ntdll.NtOpenProcessTokenEx
@ stdcall NtOpenThread(ptr long ptr ptr) ntdll.NtOpenThread
@ stdcall NtOpenThreadToken(long long long ptr) ntdll.NtOpenThreadToken
@ stdcall NtOpenThreadTokenEx(long long long long ptr) ntdll.NtOpenThreadTokenEx
@ stdcall NtQueryDirectoryFile(long long ptr ptr ptr ptr long long long ptr long) ntdll.NtQueryDirectoryFile
@ stdcall NtQueryEaFile(long ptr ptr long long ptr long ptr long) ntdll.NtQueryEaFile
@ stdcall NtQueryInformationAtom(long long ptr long ptr) ntdll.NtQueryInformationAtom
@ stdcall NtQueryInformationFile(long ptr ptr long long) ntdll.NtQueryInformationFile
@ stdcall NtQueryInformationProcess(long long ptr long ptr) ntdll.NtQueryInformationProcess
@ stdcall NtQueryInformationThread(long long ptr long ptr) ntdll.NtQueryInformationThread
@ stdcall NtQueryInformationToken(long long ptr long ptr) ntdll.NtQueryInformationToken
@ stub NtQueryQuotaInformationFile
@ stdcall NtQuerySecurityObject(long long long long long) ntdll.NtQuerySecurityObject
@ stdcall NtQuerySystemInformation(long long long long) ntdll.NtQuerySystemInformation
@ stdcall NtQueryVolumeInformationFile(long ptr ptr long long) ntdll.NtQueryVolumeInformationFile
@ stdcall NtReadFile(long long ptr ptr ptr ptr long ptr ptr) ntdll.NtReadFile
@ stub NtRequestPort
@ stdcall NtRequestWaitReplyPort(ptr ptr ptr) ntdll.NtRequestWaitReplyPort
@ stdcall NtSetEaFile(long ptr ptr long) ntdll.NtSetEaFile
@ stdcall NtSetEvent(long long) ntdll.NtSetEvent
@ stdcall NtSetInformationFile(long long long long long) ntdll.NtSetInformationFile
@ stdcall NtSetInformationProcess(long long long long) ntdll.NtSetInformationProcess
@ stdcall NtSetInformationThread(long long ptr long) ntdll.NtSetInformationThread
@ stub NtSetQuotaInformationFile
@ stdcall NtSetSecurityObject(long long ptr) ntdll.NtSetSecurityObject
@ stdcall NtSetVolumeInformationFile(long ptr ptr long long) ntdll.NtSetVolumeInformationFile
@ stdcall NtShutdownSystem(long) ntdll.NtShutdownSystem
@ stub NtTraceEvent
@ stdcall NtUnlockFile(long ptr ptr ptr ptr) ntdll.NtUnlockFile
@ stub NtVdmControl
@ stdcall NtWaitForSingleObject(long long long) ntdll.NtWaitForSingleObject
@ stdcall NtWriteFile(long long ptr ptr ptr ptr long ptr ptr) ntdll.NtWriteFile
@ stub ObAssignSecurity
@ stub ObCheckCreateObjectAccess
@ stub ObCheckObjectAccess
@ stub ObCloseHandle
@ stub ObCreateObject
@ stub ObCreateObjectType
@ stdcall ObDereferenceObject(ptr)
@ stub ObDereferenceSecurityDescriptor
@ stub ObFindHandleForObject
@ stub ObGetObjectSecurity
@ stub ObInsertObject
@ stub ObLogSecurityDescriptor
@ stub ObMakeTemporaryObject
@ stub ObOpenObjectByName
@ stub ObOpenObjectByPointer
@ stdcall ObQueryNameString(ptr ptr long ptr)
@ stub ObQueryObjectAuditingByHandle
@ stdcall ObReferenceObjectByHandle(long long ptr long ptr ptr)
@ stdcall ObReferenceObjectByName(ptr long ptr long ptr long ptr ptr)
@ stub ObReferenceObjectByPointer
@ stub ObReferenceSecurityDescriptor
@ stub ObReleaseObjectSecurity
@ stub ObSetHandleAttributes
@ stub ObSetSecurityDescriptorInfo
@ stub ObSetSecurityObjectByPointer
@ stub PfxFindPrefix
@ stub PfxInitialize
@ stub PfxInsertPrefix
@ stub PfxRemovePrefix
@ stub PoCallDriver
@ stub PoCancelDeviceNotify
@ stub PoQueueShutdownWorkItem
@ stub PoRegisterDeviceForIdleDetection
@ stub PoRegisterDeviceNotify
@ stub PoRegisterSystemState
@ stub PoRequestPowerIrp
@ stub PoRequestShutdownEvent
@ stub PoSetHiberRange
@ stdcall PoSetPowerState(ptr long long)
@ stub PoSetSystemState
@ stub PoShutdownBugCheck
@ stub PoStartNextPowerIrp
@ stub PoUnregisterSystemState
@ stdcall ProbeForRead(ptr long long)
@ stdcall ProbeForWrite(ptr long long)
@ stub PsAssignImpersonationToken
@ stub PsChargePoolQuota
@ stub PsChargeProcessNonPagedPoolQuota
@ stub PsChargeProcessPagedPoolQuota
@ stub PsChargeProcessPoolQuota
@ stub PsCreateSystemProcess
@ stdcall PsCreateSystemThread(ptr long ptr long ptr ptr ptr)
@ stub PsDereferenceImpersonationToken
@ stub PsDereferencePrimaryToken
@ stub PsDisableImpersonation
@ stub PsEstablishWin32Callouts
@ stub PsGetContextThread
@ stdcall PsGetCurrentProcess() IoGetCurrentProcess
@ stdcall PsGetCurrentProcessId()
@ stub PsGetCurrentProcessSessionId
@ stdcall PsGetCurrentThread() KeGetCurrentThread
@ stdcall PsGetCurrentThreadId()
@ stub PsGetCurrentThreadPreviousMode
@ stub PsGetCurrentThreadStackBase
@ stub PsGetCurrentThreadStackLimit
@ stub PsGetJobLock
@ stub PsGetJobSessionId
@ stub PsGetJobUIRestrictionsClass
@ stub PsGetProcessCreateTimeQuadPart
@ stub PsGetProcessDebugPort
@ stub PsGetProcessExitProcessCalled
@ stub PsGetProcessExitStatus
@ stub PsGetProcessExitTime
@ stub PsGetProcessId
@ stub PsGetProcessImageFileName
@ stub PsGetProcessInheritedFromUniqueProcessId
@ stub PsGetProcessJob
@ stub PsGetProcessPeb
@ stub PsGetProcessPriorityClass
@ stub PsGetProcessSectionBaseAddress
@ stub PsGetProcessSecurityPort
@ stub PsGetProcessSessionId
@ stub PsGetProcessWin32Process
@ stub PsGetProcessWin32WindowStation
@ stub PsGetThreadFreezeCount
@ stub PsGetThreadHardErrorsAreDisabled
@ stub PsGetThreadId
@ stub PsGetThreadProcess
@ stub PsGetThreadProcessId
@ stub PsGetThreadSessionId
@ stub PsGetThreadTeb
@ stub PsGetThreadWin32Thread
@ stdcall PsGetVersion(ptr ptr ptr ptr)
@ stdcall PsImpersonateClient(ptr ptr long long long)
@ stub PsInitialSystemProcess
@ stub PsIsProcessBeingDebugged
@ stub PsIsSystemThread
@ stub PsIsThreadImpersonating
@ stub PsIsThreadTerminating
@ stub PsJobType
@ stdcall PsLookupProcessByProcessId(ptr ptr)
@ stub PsLookupProcessThreadByCid
@ stub PsLookupThreadByThreadId
@ stub PsProcessType
@ stub PsReferenceImpersonationToken
@ stub PsReferencePrimaryToken
@ stdcall PsRemoveCreateThreadNotifyRoutine(ptr)
@ stdcall PsRemoveLoadImageNotifyRoutine(ptr)
@ stub PsRestoreImpersonation
@ stub PsReturnPoolQuota
@ stub PsReturnProcessNonPagedPoolQuota
@ stub PsReturnProcessPagedPoolQuota
@ stub PsRevertThreadToSelf
@ stub PsRevertToSelf
@ stub PsSetContextThread
@ stdcall PsSetCreateProcessNotifyRoutine(ptr long)
@ stdcall PsSetCreateThreadNotifyRoutine(ptr)
@ stub PsSetJobUIRestrictionsClass
@ stub PsSetLegoNotifyRoutine
@ stdcall PsSetLoadImageNotifyRoutine(ptr)
@ stub PsSetProcessPriorityByClass
@ stub PsSetProcessPriorityClass
@ stub PsSetProcessSecurityPort
@ stub PsSetProcessWin32Process
@ stub PsSetProcessWindowStation
@ stub PsSetThreadHardErrorsAreDisabled
@ stub PsSetThreadWin32Thread
@ stdcall PsTerminateSystemThread(long)
@ stub PsThreadType
@ stdcall READ_REGISTER_BUFFER_UCHAR(ptr ptr long)
@ stub READ_REGISTER_BUFFER_ULONG
@ stub READ_REGISTER_BUFFER_USHORT
@ stub READ_REGISTER_UCHAR
@ stub READ_REGISTER_ULONG
@ stub READ_REGISTER_USHORT
@ stdcall RtlAbsoluteToSelfRelativeSD(ptr ptr ptr) ntdll.RtlAbsoluteToSelfRelativeSD
@ stdcall RtlAddAccessAllowedAce(ptr long long ptr) ntdll.RtlAddAccessAllowedAce
@ stdcall RtlAddAccessAllowedAceEx(ptr long long long ptr) ntdll.RtlAddAccessAllowedAceEx
@ stdcall RtlAddAce(ptr long long ptr long) ntdll.RtlAddAce
@ stdcall RtlAddAtomToAtomTable(ptr wstr ptr) ntdll.RtlAddAtomToAtomTable
@ stub RtlAddRange
@ stdcall RtlAllocateHeap(long long long) ntdll.RtlAllocateHeap
@ stdcall RtlAnsiCharToUnicodeChar(ptr) ntdll.RtlAnsiCharToUnicodeChar
@ stdcall RtlAnsiStringToUnicodeSize(ptr) ntdll.RtlAnsiStringToUnicodeSize
@ stdcall RtlAnsiStringToUnicodeString(ptr ptr long) ntdll.RtlAnsiStringToUnicodeString
@ stdcall RtlAppendAsciizToString(ptr str) ntdll.RtlAppendAsciizToString
@ stdcall RtlAppendStringToString(ptr ptr) ntdll.RtlAppendStringToString
@ stdcall RtlAppendUnicodeStringToString(ptr ptr) ntdll.RtlAppendUnicodeStringToString
@ stdcall RtlAppendUnicodeToString(ptr wstr) ntdll.RtlAppendUnicodeToString
@ stdcall RtlAreAllAccessesGranted(long long) ntdll.RtlAreAllAccessesGranted
@ stdcall RtlAreAnyAccessesGranted(long long) ntdll.RtlAreAnyAccessesGranted
@ stdcall RtlAreBitsClear(ptr long long) ntdll.RtlAreBitsClear
@ stdcall RtlAreBitsSet(ptr long long) ntdll.RtlAreBitsSet
@ stdcall RtlAssert(ptr ptr long str) ntdll.RtlAssert
@ stdcall -norelay RtlCaptureContext(ptr) ntdll.RtlCaptureContext
@ stdcall RtlCaptureStackBackTrace(long long ptr ptr) ntdll.RtlCaptureStackBackTrace
@ stdcall RtlCharToInteger(ptr long ptr) ntdll.RtlCharToInteger
@ stdcall RtlCheckRegistryKey(long ptr) ntdll.RtlCheckRegistryKey
@ stdcall RtlClearAllBits(ptr) ntdll.RtlClearAllBits
@ stub RtlClearBit
@ stdcall RtlClearBits(ptr long long) ntdll.RtlClearBits
@ stdcall RtlCompareMemory(ptr ptr long) ntdll.RtlCompareMemory
@ stdcall RtlCompareMemoryUlong(ptr long long) ntdll.RtlCompareMemoryUlong
@ stdcall RtlCompareString(ptr ptr long) ntdll.RtlCompareString
@ stdcall RtlCompareUnicodeString(ptr ptr long) ntdll.RtlCompareUnicodeString
@ stdcall RtlCompressBuffer(long ptr long ptr long long ptr ptr) ntdll.RtlCompressBuffer
@ stub RtlCompressChunks
@ stdcall -arch=win32 -ret64 RtlConvertLongToLargeInteger(long) ntdll.RtlConvertLongToLargeInteger
@ stdcall RtlConvertSidToUnicodeString(ptr ptr long) ntdll.RtlConvertSidToUnicodeString
@ stdcall -arch=win32 -ret64 RtlConvertUlongToLargeInteger(long) ntdll.RtlConvertUlongToLargeInteger
@ stdcall RtlCopyLuid(ptr ptr) ntdll.RtlCopyLuid
@ stdcall -arch=x86_64 RtlCopyMemory(ptr ptr long) ntdll.RtlCopyMemory
@ stub RtlCopyRangeList
@ stdcall RtlCopySid(long ptr ptr) ntdll.RtlCopySid
@ stdcall RtlCopyString(ptr ptr) ntdll.RtlCopyString
@ stdcall RtlCopyUnicodeString(ptr ptr) ntdll.RtlCopyUnicodeString
@ stdcall RtlCreateAcl(ptr long long) ntdll.RtlCreateAcl
@ stdcall RtlCreateAtomTable(long ptr) ntdll.RtlCreateAtomTable
@ stdcall RtlCreateHeap(long ptr long long ptr ptr) ntdll.RtlCreateHeap
@ stub RtlCreateRegistryKey
@ stdcall RtlCreateSecurityDescriptor(ptr long) ntdll.RtlCreateSecurityDescriptor
@ stub RtlCreateSystemVolumeInformationFolder
@ stdcall RtlCreateUnicodeString(ptr wstr) ntdll.RtlCreateUnicodeString
@ stub RtlCustomCPToUnicodeN
@ stdcall RtlDecompressBuffer(long ptr long ptr long ptr) ntdll.RtlDecompressBuffer
@ stub RtlDecompressChunks
@ stdcall RtlDecompressFragment(long ptr long ptr long long ptr ptr) ntdll.RtlDecompressFragment
@ stub RtlDelete
@ stdcall RtlDeleteAce(ptr long) ntdll.RtlDeleteAce
@ stdcall RtlDeleteAtomFromAtomTable(ptr long) ntdll.RtlDeleteAtomFromAtomTable
@ stub RtlDeleteElementGenericTable
@ stub RtlDeleteElementGenericTableAvl
@ stub RtlDeleteNoSplay
@ stub RtlDeleteOwnersRanges
@ stub RtlDeleteRange
@ stdcall RtlDeleteRegistryValue(long ptr ptr) ntdll.RtlDeleteRegistryValue
@ stub RtlDescribeChunk
@ stdcall RtlDestroyAtomTable(ptr) ntdll.RtlDestroyAtomTable
@ stdcall RtlDestroyHeap(long) ntdll.RtlDestroyHeap
@ stdcall RtlDowncaseUnicodeString(ptr ptr long) ntdll.RtlDowncaseUnicodeString
@ stdcall RtlEmptyAtomTable(ptr long) ntdll.RtlEmptyAtomTable
@ stdcall -arch=win32 -ret64 RtlEnlargedIntegerMultiply(long long) ntdll.RtlEnlargedIntegerMultiply
@ stdcall -arch=win32 RtlEnlargedUnsignedDivide(int64 long ptr) ntdll.RtlEnlargedUnsignedDivide
@ stdcall -arch=win32 -ret64 RtlEnlargedUnsignedMultiply(long long) ntdll.RtlEnlargedUnsignedMultiply
@ stub RtlEnumerateGenericTable
@ stub RtlEnumerateGenericTableAvl
@ stub RtlEnumerateGenericTableLikeADirectory
@ stdcall RtlEnumerateGenericTableWithoutSplaying(ptr ptr) ntdll.RtlEnumerateGenericTableWithoutSplaying
@ stub RtlEnumerateGenericTableWithoutSplayingAvl
@ stdcall RtlEqualLuid(ptr ptr) ntdll.RtlEqualLuid
@ stdcall RtlEqualSid(long long) ntdll.RtlEqualSid
@ stdcall RtlEqualString(ptr ptr long) ntdll.RtlEqualString
@ stdcall RtlEqualUnicodeString(ptr ptr long) ntdll.RtlEqualUnicodeString
@ stdcall -arch=win32 -ret64 RtlExtendedIntegerMultiply(int64 long) ntdll.RtlExtendedIntegerMultiply
@ stdcall -arch=win32 -ret64 RtlExtendedLargeIntegerDivide(int64 long ptr) ntdll.RtlExtendedLargeIntegerDivide
@ stdcall -arch=win32 -ret64 RtlExtendedMagicDivide(int64 int64 long) ntdll.RtlExtendedMagicDivide
@ stdcall RtlFillMemory(ptr long long) ntdll.RtlFillMemory
@ stdcall RtlFillMemoryUlong(ptr long long) ntdll.RtlFillMemoryUlong
@ stdcall RtlFindClearBits(ptr long long) ntdll.RtlFindClearBits
@ stdcall RtlFindClearBitsAndSet(ptr long long) ntdll.RtlFindClearBitsAndSet
@ stdcall RtlFindClearRuns(ptr ptr long long) ntdll.RtlFindClearRuns
@ stub RtlFindFirstRunClear
@ stdcall RtlFindLastBackwardRunClear(ptr long ptr) ntdll.RtlFindLastBackwardRunClear
@ stdcall RtlFindLeastSignificantBit(int64) ntdll.RtlFindLeastSignificantBit
@ stdcall RtlFindLongestRunClear(ptr long) ntdll.RtlFindLongestRunClear
@ stdcall RtlFindMessage(long long long long ptr) ntdll.RtlFindMessage
@ stdcall RtlFindMostSignificantBit(int64) ntdll.RtlFindMostSignificantBit
@ stdcall RtlFindNextForwardRunClear(ptr long ptr) ntdll.RtlFindNextForwardRunClear
@ stub RtlFindRange
@ stdcall RtlFindSetBits(ptr long long) ntdll.RtlFindSetBits
@ stdcall RtlFindSetBitsAndClear(ptr long long) ntdll.RtlFindSetBitsAndClear
@ stub RtlFindUnicodePrefix
@ stdcall RtlFormatCurrentUserKeyPath(ptr) ntdll.RtlFormatCurrentUserKeyPath
@ stdcall RtlFreeAnsiString(long) ntdll.RtlFreeAnsiString
@ stdcall RtlFreeHeap(long long ptr) ntdll.RtlFreeHeap
@ stdcall RtlFreeOemString(ptr) ntdll.RtlFreeOemString
@ stub RtlFreeRangeList
@ stdcall RtlFreeUnicodeString(ptr) ntdll.RtlFreeUnicodeString
@ stdcall RtlGUIDFromString(ptr ptr) ntdll.RtlGUIDFromString
@ stub RtlGenerate8dot3Name
@ stdcall RtlGetAce(ptr long ptr) ntdll.RtlGetAce
@ stub RtlGetCallersAddress
@ stdcall RtlGetCompressionWorkSpaceSize(long ptr ptr) ntdll.RtlGetCompressionWorkSpaceSize
@ stdcall RtlGetDaclSecurityDescriptor(ptr ptr ptr ptr) ntdll.RtlGetDaclSecurityDescriptor
@ stub RtlGetDefaultCodePage
@ stub RtlGetElementGenericTable
@ stub RtlGetElementGenericTableAvl
@ stub RtlGetFirstRange
@ stdcall RtlGetGroupSecurityDescriptor(ptr ptr ptr) ntdll.RtlGetGroupSecurityDescriptor
@ stub RtlGetNextRange
@ stdcall RtlGetNtGlobalFlags() ntdll.RtlGetNtGlobalFlags
@ stdcall RtlGetOwnerSecurityDescriptor(ptr ptr ptr) ntdll.RtlGetOwnerSecurityDescriptor
@ stdcall RtlGetSaclSecurityDescriptor(ptr ptr ptr ptr) ntdll.RtlGetSaclSecurityDescriptor
@ stub RtlGetSetBootStatusData
@ stdcall RtlGetVersion(ptr) ntdll.RtlGetVersion
@ stdcall RtlHashUnicodeString(ptr long long ptr) ntdll.RtlHashUnicodeString
@ stdcall RtlImageDirectoryEntryToData(long long long ptr) ntdll.RtlImageDirectoryEntryToData
@ stdcall RtlImageNtHeader(long) ntdll.RtlImageNtHeader
@ stdcall RtlInitAnsiString(ptr str) ntdll.RtlInitAnsiString
@ stub RtlInitCodePageTable
@ stdcall RtlInitString(ptr str) ntdll.RtlInitString
@ stdcall RtlInitUnicodeString(ptr wstr) ntdll.RtlInitUnicodeString
@ stdcall RtlInitializeBitMap(ptr long long) ntdll.RtlInitializeBitMap
@ stdcall RtlInitializeGenericTable(ptr ptr ptr ptr ptr) ntdll.RtlInitializeGenericTable
@ stdcall RtlInitializeGenericTableAvl(ptr ptr ptr ptr ptr) ntdll.RtlInitializeGenericTableAvl
@ stub RtlInitializeRangeList
@ stdcall RtlInitializeSid(ptr ptr long) ntdll.RtlInitializeSid
@ stub RtlInitializeUnicodePrefix
@ stub RtlInsertElementGenericTable
@ stdcall RtlInsertElementGenericTableAvl(ptr ptr long ptr) ntdll.RtlInsertElementGenericTableAvl
@ stub RtlInsertElementGenericTableFull
@ stub RtlInsertElementGenericTableFullAvl
@ stub RtlInsertUnicodePrefix
@ stdcall RtlInt64ToUnicodeString(int64 long ptr) ntdll.RtlInt64ToUnicodeString
@ stdcall RtlIntegerToChar(long long long ptr) ntdll.RtlIntegerToChar
@ stub RtlIntegerToUnicode
@ stdcall RtlIntegerToUnicodeString(long long ptr) ntdll.RtlIntegerToUnicodeString
@ stub RtlInvertRangeList
@ stdcall RtlIpv4AddressToStringA(ptr ptr) ntdll.RtlIpv4AddressToStringA
@ stdcall RtlIpv4AddressToStringExA(ptr long ptr ptr) ntdll.RtlIpv4AddressToStringExA
@ stdcall RtlIpv4AddressToStringExW(ptr long ptr ptr) ntdll.RtlIpv4AddressToStringExW
@ stdcall RtlIpv4AddressToStringW(ptr ptr) ntdll.RtlIpv4AddressToStringW
@ stub RtlIpv4StringToAddressA
@ stub RtlIpv4StringToAddressExA
@ stdcall RtlIpv4StringToAddressExW(ptr ptr wstr ptr) ntdll.RtlIpv4StringToAddressExW
@ stub RtlIpv4StringToAddressW
@ stub RtlIpv6AddressToStringA
@ stub RtlIpv6AddressToStringExA
@ stub RtlIpv6AddressToStringExW
@ stub RtlIpv6AddressToStringW
@ stub RtlIpv6StringToAddressA
@ stub RtlIpv6StringToAddressExA
@ stub RtlIpv6StringToAddressExW
@ stub RtlIpv6StringToAddressW
@ stub RtlIsGenericTableEmpty
@ stub RtlIsGenericTableEmptyAvl
@ stdcall RtlIsNameLegalDOS8Dot3(ptr ptr ptr) ntdll.RtlIsNameLegalDOS8Dot3
@ stub RtlIsRangeAvailable
@ stub RtlIsValidOemCharacter
@ stdcall -arch=win32 -ret64 RtlLargeIntegerAdd(int64 int64) ntdll.RtlLargeIntegerAdd
@ stdcall -arch=win32 -ret64 RtlLargeIntegerArithmeticShift(int64 long) ntdll.RtlLargeIntegerArithmeticShift
@ stdcall -arch=win32 -ret64 RtlLargeIntegerDivide(int64 int64 ptr) ntdll.RtlLargeIntegerDivide
@ stdcall -arch=win32 -ret64 RtlLargeIntegerNegate(int64) ntdll.RtlLargeIntegerNegate
@ stdcall -arch=win32 -ret64 RtlLargeIntegerShiftLeft(int64 long) ntdll.RtlLargeIntegerShiftLeft
@ stdcall -arch=win32 -ret64 RtlLargeIntegerShiftRight(int64 long) ntdll.RtlLargeIntegerShiftRight
@ stdcall -arch=win32 -ret64 RtlLargeIntegerSubtract(int64 int64) ntdll.RtlLargeIntegerSubtract
@ stdcall RtlLengthRequiredSid(long) ntdll.RtlLengthRequiredSid
@ stdcall RtlLengthSecurityDescriptor(ptr) ntdll.RtlLengthSecurityDescriptor
@ stdcall RtlLengthSid(ptr) ntdll.RtlLengthSid
@ stub RtlLockBootStatusData
@ stdcall RtlLookupAtomInAtomTable(ptr wstr ptr) ntdll.RtlLookupAtomInAtomTable
@ stub RtlLookupElementGenericTable
@ stub RtlLookupElementGenericTableAvl
@ stub RtlLookupElementGenericTableFull
@ stub RtlLookupElementGenericTableFullAvl
@ stdcall RtlMapGenericMask(long ptr) ntdll.RtlMapGenericMask
@ stub RtlMapSecurityErrorToNtStatus
@ stub RtlMergeRangeLists
@ stdcall RtlMoveMemory(ptr ptr long) ntdll.RtlMoveMemory
@ stdcall RtlMultiByteToUnicodeN(ptr long ptr ptr long) ntdll.RtlMultiByteToUnicodeN
@ stdcall RtlMultiByteToUnicodeSize(ptr str long) ntdll.RtlMultiByteToUnicodeSize
@ stub RtlNextUnicodePrefix
@ stdcall RtlNtStatusToDosError(long) ntdll.RtlNtStatusToDosError
@ stdcall RtlNtStatusToDosErrorNoTeb(long) ntdll.RtlNtStatusToDosErrorNoTeb
@ stdcall RtlNumberGenericTableElements(ptr) ntdll.RtlNumberGenericTableElements
@ stub RtlNumberGenericTableElementsAvl
@ stdcall RtlNumberOfClearBits(ptr) ntdll.RtlNumberOfClearBits
@ stdcall RtlNumberOfSetBits(ptr) ntdll.RtlNumberOfSetBits
@ stub RtlOemStringToCountedUnicodeString
@ stdcall RtlOemStringToUnicodeSize(ptr) ntdll.RtlOemStringToUnicodeSize
@ stdcall RtlOemStringToUnicodeString(ptr ptr long) ntdll.RtlOemStringToUnicodeString
@ stdcall RtlOemToUnicodeN(ptr long ptr ptr long) ntdll.RtlOemToUnicodeN
@ stdcall RtlPinAtomInAtomTable(ptr long) ntdll.RtlPinAtomInAtomTable
@ stdcall RtlPrefixString(ptr ptr long) ntdll.RtlPrefixString
@ stdcall RtlPrefixUnicodeString(ptr ptr long) ntdll.RtlPrefixUnicodeString
@ stdcall RtlQueryAtomInAtomTable(ptr long ptr ptr ptr ptr) ntdll.RtlQueryAtomInAtomTable
@ stdcall RtlQueryRegistryValues(long ptr ptr ptr ptr) ntdll.RtlQueryRegistryValues
@ stdcall RtlQueryTimeZoneInformation(ptr) ntdll.RtlQueryTimeZoneInformation
@ stdcall -register RtlRaiseException(ptr) ntdll.RtlRaiseException
@ stdcall RtlRandom(ptr) ntdll.RtlRandom
@ stub RtlRandomEx
@ stub RtlRealPredecessor
@ stub RtlRealSuccessor
@ stub RtlRemoveUnicodePrefix
@ stub RtlReserveChunk
@ stdcall RtlSecondsSince1970ToTime(long ptr) ntdll.RtlSecondsSince1970ToTime
@ stdcall RtlSecondsSince1980ToTime(long ptr) ntdll.RtlSecondsSince1980ToTime
@ stub RtlSelfRelativeToAbsoluteSD2
@ stdcall RtlSelfRelativeToAbsoluteSD(ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr ptr) ntdll.RtlSelfRelativeToAbsoluteSD
@ stdcall RtlSetAllBits(ptr) ntdll.RtlSetAllBits
@ stub RtlSetBit
@ stdcall RtlSetBits(ptr long long) ntdll.RtlSetBits
@ stdcall RtlSetDaclSecurityDescriptor(ptr long ptr long) ntdll.RtlSetDaclSecurityDescriptor
@ stdcall RtlSetGroupSecurityDescriptor(ptr ptr long) ntdll.RtlSetGroupSecurityDescriptor
@ stdcall RtlSetOwnerSecurityDescriptor(ptr ptr long) ntdll.RtlSetOwnerSecurityDescriptor
@ stdcall RtlSetSaclSecurityDescriptor(ptr long ptr long) ntdll.RtlSetSaclSecurityDescriptor
@ stdcall RtlSetTimeZoneInformation(ptr) ntdll.RtlSetTimeZoneInformation
@ stdcall RtlSizeHeap(long long ptr) ntdll.RtlSizeHeap
@ stub RtlSplay
@ stdcall RtlStringFromGUID(ptr ptr) ntdll.RtlStringFromGUID
@ stdcall RtlSubAuthorityCountSid(ptr) ntdll.RtlSubAuthorityCountSid
@ stdcall RtlSubAuthoritySid(ptr long) ntdll.RtlSubAuthoritySid
@ stub RtlSubtreePredecessor
@ stub RtlSubtreeSuccessor
@ stub RtlTestBit
@ stdcall RtlTimeFieldsToTime(ptr ptr) ntdll.RtlTimeFieldsToTime
@ stdcall RtlTimeToElapsedTimeFields(long long) ntdll.RtlTimeToElapsedTimeFields
@ stdcall RtlTimeToSecondsSince1970(ptr ptr) ntdll.RtlTimeToSecondsSince1970
@ stdcall RtlTimeToSecondsSince1980(ptr ptr) ntdll.RtlTimeToSecondsSince1980
@ stdcall RtlTimeToTimeFields(long long) ntdll.RtlTimeToTimeFields
@ stub RtlTraceDatabaseAdd
@ stub RtlTraceDatabaseCreate
@ stub RtlTraceDatabaseDestroy
@ stub RtlTraceDatabaseEnumerate
@ stub RtlTraceDatabaseFind
@ stub RtlTraceDatabaseLock
@ stub RtlTraceDatabaseUnlock
@ stub RtlTraceDatabaseValidate
@ stdcall RtlUnicodeStringToAnsiSize(ptr) ntdll.RtlUnicodeStringToAnsiSize
@ stdcall RtlUnicodeStringToAnsiString(ptr ptr long) ntdll.RtlUnicodeStringToAnsiString
@ stub RtlUnicodeStringToCountedOemString
@ stdcall RtlUnicodeStringToInteger(ptr long ptr) ntdll.RtlUnicodeStringToInteger
@ stdcall RtlUnicodeStringToOemSize(ptr) ntdll.RtlUnicodeStringToOemSize
@ stdcall RtlUnicodeStringToOemString(ptr ptr long) ntdll.RtlUnicodeStringToOemString
@ stub RtlUnicodeToCustomCPN
@ stdcall RtlUnicodeToMultiByteN(ptr long ptr ptr long) ntdll.RtlUnicodeToMultiByteN
@ stdcall RtlUnicodeToMultiByteSize(ptr ptr long) ntdll.RtlUnicodeToMultiByteSize
@ stdcall RtlUnicodeToOemN(ptr long ptr ptr long) ntdll.RtlUnicodeToOemN
@ stub RtlUnlockBootStatusData
@ stdcall -register RtlUnwind(ptr ptr ptr ptr) ntdll.RtlUnwind
@ stdcall -arch=x86_64 RtlUnwindEx(ptr ptr ptr ptr ptr ptr) ntdll.RtlUnwindEx
@ stdcall RtlUpcaseUnicodeChar(long) ntdll.RtlUpcaseUnicodeChar
@ stdcall RtlUpcaseUnicodeString(ptr ptr long) ntdll.RtlUpcaseUnicodeString
@ stdcall RtlUpcaseUnicodeStringToAnsiString(ptr ptr long) ntdll.RtlUpcaseUnicodeStringToAnsiString
@ stdcall RtlUpcaseUnicodeStringToCountedOemString(ptr ptr long) ntdll.RtlUpcaseUnicodeStringToCountedOemString
@ stdcall RtlUpcaseUnicodeStringToOemString(ptr ptr long) ntdll.RtlUpcaseUnicodeStringToOemString
@ stub RtlUpcaseUnicodeToCustomCPN
@ stdcall RtlUpcaseUnicodeToMultiByteN(ptr long ptr ptr long) ntdll.RtlUpcaseUnicodeToMultiByteN
@ stdcall RtlUpcaseUnicodeToOemN(ptr long ptr ptr long) ntdll.RtlUpcaseUnicodeToOemN
@ stdcall RtlUpperChar(long) ntdll.RtlUpperChar
@ stdcall RtlUpperString(ptr ptr) ntdll.RtlUpperString
@ stdcall RtlValidRelativeSecurityDescriptor(ptr long long) ntdll.RtlValidRelativeSecurityDescriptor
@ stdcall RtlValidSecurityDescriptor(ptr) ntdll.RtlValidSecurityDescriptor
@ stdcall RtlValidSid(ptr) ntdll.RtlValidSid
@ stdcall RtlVerifyVersionInfo(ptr long int64) ntdll.RtlVerifyVersionInfo
@ stub RtlVolumeDeviceToDosName
@ stub RtlWalkFrameChain
@ stdcall RtlWriteRegistryValue(long ptr ptr long ptr long) ntdll.RtlWriteRegistryValue
@ stub RtlZeroHeap
@ stdcall RtlZeroMemory(ptr long) ntdll.RtlZeroMemory
@ stdcall RtlxAnsiStringToUnicodeSize(ptr) ntdll.RtlxAnsiStringToUnicodeSize
@ stdcall RtlxOemStringToUnicodeSize(ptr) ntdll.RtlxOemStringToUnicodeSize
@ stdcall RtlxUnicodeStringToAnsiSize(ptr) ntdll.RtlxUnicodeStringToAnsiSize
@ stdcall RtlxUnicodeStringToOemSize(ptr) ntdll.RtlxUnicodeStringToOemSize
@ stub SeAccessCheck
@ stub SeAppendPrivileges
@ stub SeAssignSecurity
@ stub SeAssignSecurityEx
@ stub SeAuditHardLinkCreation
@ stub SeAuditingFileEvents
@ stub SeAuditingFileEventsWithContext
@ stub SeAuditingFileOrGlobalEvents
@ stub SeAuditingHardLinkEvents
@ stub SeAuditingHardLinkEventsWithContext
@ stub SeCaptureSecurityDescriptor
@ stub SeCaptureSubjectContext
@ stub SeCloseObjectAuditAlarm
@ stub SeCreateAccessState
@ stub SeCreateClientSecurity
@ stub SeCreateClientSecurityFromSubjectContext
@ stub SeDeassignSecurity
@ stub SeDeleteAccessState
@ stub SeDeleteObjectAuditAlarm
@ stub SeExports
@ stub SeFilterToken
@ stub SeFreePrivileges
@ stub SeImpersonateClient
@ stub SeImpersonateClientEx
@ stub SeLockSubjectContext
@ stub SeMarkLogonSessionForTerminationNotification
@ stub SeOpenObjectAuditAlarm
@ stub SeOpenObjectForDeleteAuditAlarm
@ stub SePrivilegeCheck
@ stub SePrivilegeObjectAuditAlarm
@ stub SePublicDefaultDacl
@ stub SeQueryAuthenticationIdToken
@ stub SeQueryInformationToken
@ stub SeQuerySecurityDescriptorInfo
@ stub SeQuerySessionIdToken
@ stub SeRegisterLogonSessionTerminatedRoutine
@ stub SeReleaseSecurityDescriptor
@ stub SeReleaseSubjectContext
@ stub SeSetAccessStateGenericMapping
@ stub SeSetSecurityDescriptorInfo
@ stub SeSetSecurityDescriptorInfoEx
@ stub SeSinglePrivilegeCheck
@ stub SeSystemDefaultDacl
@ stub SeTokenImpersonationLevel
@ stub SeTokenIsAdmin
@ stub SeTokenIsRestricted
@ stub SeTokenIsWriteRestricted
@ stub SeTokenObjectType
@ stub SeTokenType
@ stub SeUnlockSubjectContext
@ stub SeUnregisterLogonSessionTerminatedRoutine
@ stub SeValidSecurityDescriptor
@ stdcall -ret64 VerSetConditionMask(int64 long long) ntdll.VerSetConditionMask
@ stub VfFailDeviceNode
@ stub VfFailDriver
@ stub VfFailSystemBIOS
@ stub VfIsVerificationEnabled
@ stub WRITE_REGISTER_BUFFER_UCHAR
@ stub WRITE_REGISTER_BUFFER_ULONG
@ stub WRITE_REGISTER_BUFFER_USHORT
@ stub WRITE_REGISTER_UCHAR
@ stub WRITE_REGISTER_ULONG
@ stub WRITE_REGISTER_USHORT
@ stub WmiFlushTrace
@ stub WmiQueryTrace
@ stub WmiQueryTraceInformation
@ stub WmiStartTrace
@ stub WmiStopTrace
@ stub WmiTraceMessage
@ stub WmiTraceMessageVa
@ stub WmiUpdateTrace
@ stub XIPDispatch
@ stdcall -private ZwAccessCheckAndAuditAlarm(ptr long ptr ptr ptr long ptr long ptr ptr ptr) ntdll.ZwAccessCheckAndAuditAlarm
@ stub ZwAddBootEntry
@ stdcall -private ZwAdjustPrivilegesToken(long long long long long long) ntdll.ZwAdjustPrivilegesToken
@ stdcall -private ZwAlertThread(long) ntdll.ZwAlertThread
@ stdcall -private ZwAllocateVirtualMemory(long ptr ptr ptr long long) ntdll.ZwAllocateVirtualMemory
@ stdcall -private ZwAssignProcessToJobObject(long long) ntdll.ZwAssignProcessToJobObject
@ stdcall -private ZwCancelIoFile(long ptr) ntdll.ZwCancelIoFile
@ stdcall -private ZwCancelTimer(long ptr) ntdll.ZwCancelTimer
@ stdcall -private ZwClearEvent(long) ntdll.ZwClearEvent
@ stdcall -private ZwClose(long) ntdll.ZwClose
@ stub ZwCloseObjectAuditAlarm
@ stdcall -private ZwConnectPort(ptr ptr ptr ptr ptr ptr ptr ptr) ntdll.ZwConnectPort
@ stdcall -private ZwCreateDirectoryObject(long long long) ntdll.ZwCreateDirectoryObject
@ stdcall -private ZwCreateEvent(long long long long long) ntdll.ZwCreateEvent
@ stdcall -private ZwCreateFile(ptr long ptr ptr ptr long long long long ptr long) ntdll.ZwCreateFile
@ stdcall -private ZwCreateJobObject(ptr long ptr) ntdll.ZwCreateJobObject
@ stdcall -private ZwCreateKey(ptr long ptr long ptr long long) ntdll.ZwCreateKey
@ stdcall -private ZwCreateSection(ptr long ptr ptr long long long) ntdll.ZwCreateSection
@ stdcall -private ZwCreateSymbolicLinkObject(ptr long ptr ptr) ntdll.ZwCreateSymbolicLinkObject
@ stdcall -private ZwCreateTimer(ptr long ptr long) ntdll.ZwCreateTimer
@ stub ZwDeleteBootEntry
@ stdcall -private ZwDeleteFile(ptr) ntdll.ZwDeleteFile
@ stdcall -private ZwDeleteKey(long) ntdll.ZwDeleteKey
@ stdcall -private ZwDeleteValueKey(long ptr) ntdll.ZwDeleteValueKey
@ stdcall -private ZwDeviceIoControlFile(long long ptr ptr ptr long ptr long ptr long) ntdll.ZwDeviceIoControlFile
@ stdcall -private ZwDisplayString(ptr) ntdll.ZwDisplayString
@ stdcall -private ZwDuplicateObject(long long long ptr long long long) ntdll.ZwDuplicateObject
@ stdcall -private ZwDuplicateToken(long long long long long long) ntdll.ZwDuplicateToken
@ stub ZwEnumerateBootEntries
@ stdcall -private ZwEnumerateKey(long long long ptr long ptr) ntdll.ZwEnumerateKey
@ stdcall -private ZwEnumerateValueKey(long long long ptr long ptr) ntdll.ZwEnumerateValueKey
@ stdcall -private ZwFlushInstructionCache(long ptr long) ntdll.ZwFlushInstructionCache
@ stdcall -private ZwFlushKey(long) ntdll.ZwFlushKey
@ stdcall -private ZwFlushVirtualMemory(long ptr ptr long) ntdll.ZwFlushVirtualMemory
@ stdcall -private ZwFreeVirtualMemory(long ptr ptr long) ntdll.ZwFreeVirtualMemory
@ stdcall -private ZwFsControlFile(long long long long long long long long long long) ntdll.ZwFsControlFile
@ stdcall -private ZwInitiatePowerAction(long long long long) ntdll.ZwInitiatePowerAction
@ stdcall -private ZwIsProcessInJob(long long) ntdll.ZwIsProcessInJob
@ stdcall ZwLoadDriver(ptr)
@ stdcall -private ZwLoadKey(ptr ptr) ntdll.ZwLoadKey
@ stdcall -private ZwMakeTemporaryObject(long) ntdll.ZwMakeTemporaryObject
@ stdcall -private ZwMapViewOfSection(long long ptr long long ptr ptr long long long) ntdll.ZwMapViewOfSection
@ stdcall -private ZwNotifyChangeKey(long long ptr ptr ptr long long ptr long long) ntdll.ZwNotifyChangeKey
@ stdcall -private ZwOpenDirectoryObject(long long long) ntdll.ZwOpenDirectoryObject
@ stdcall -private ZwOpenEvent(long long long) ntdll.ZwOpenEvent
@ stdcall -private ZwOpenFile(ptr long ptr ptr long long) ntdll.ZwOpenFile
@ stdcall -private ZwOpenJobObject(ptr long ptr) ntdll.ZwOpenJobObject
@ stdcall -private ZwOpenKey(ptr long ptr) ntdll.ZwOpenKey
@ stdcall -private ZwOpenProcess(ptr long ptr ptr) ntdll.ZwOpenProcess
@ stdcall -private ZwOpenProcessToken(long long ptr) ntdll.ZwOpenProcessToken
@ stdcall -private ZwOpenProcessTokenEx(long long long ptr) ntdll.ZwOpenProcessTokenEx
@ stdcall -private ZwOpenSection(ptr long ptr) ntdll.ZwOpenSection
@ stdcall -private ZwOpenSymbolicLinkObject(ptr long ptr) ntdll.ZwOpenSymbolicLinkObject
@ stdcall -private ZwOpenThread(ptr long ptr ptr) ntdll.ZwOpenThread
@ stdcall -private ZwOpenThreadToken(long long long ptr) ntdll.ZwOpenThreadToken
@ stdcall -private ZwOpenThreadTokenEx(long long long long ptr) ntdll.ZwOpenThreadTokenEx
@ stdcall -private ZwOpenTimer(ptr long ptr) ntdll.ZwOpenTimer
@ stdcall -private ZwPowerInformation(long ptr long ptr long) ntdll.ZwPowerInformation
@ stdcall -private ZwPulseEvent(long ptr) ntdll.ZwPulseEvent
@ stub ZwQueryBootEntryOrder
@ stub ZwQueryBootOptions
@ stdcall -private ZwQueryDefaultLocale(long ptr) ntdll.ZwQueryDefaultLocale
@ stdcall -private ZwQueryDefaultUILanguage(ptr) ntdll.ZwQueryDefaultUILanguage
@ stdcall -private ZwQueryDirectoryFile(long long ptr ptr ptr ptr long long long ptr long) ntdll.ZwQueryDirectoryFile
@ stdcall -private ZwQueryDirectoryObject(long ptr long long long ptr ptr) ntdll.ZwQueryDirectoryObject
@ stdcall -private ZwQueryEaFile(long ptr ptr long long ptr long ptr long) ntdll.ZwQueryEaFile
@ stdcall -private ZwQueryFullAttributesFile(ptr ptr) ntdll.ZwQueryFullAttributesFile
@ stdcall -private ZwQueryInformationFile(long ptr ptr long long) ntdll.ZwQueryInformationFile
@ stdcall -private ZwQueryInformationJobObject(long long ptr long ptr) ntdll.ZwQueryInformationJobObject
@ stdcall -private ZwQueryInformationProcess(long long ptr long ptr) ntdll.ZwQueryInformationProcess
@ stdcall -private ZwQueryInformationThread(long long ptr long ptr) ntdll.ZwQueryInformationThread
@ stdcall -private ZwQueryInformationToken(long long ptr long ptr) ntdll.ZwQueryInformationToken
@ stdcall -private ZwQueryInstallUILanguage(ptr) ntdll.ZwQueryInstallUILanguage
@ stdcall -private ZwQueryKey(long long ptr long ptr) ntdll.ZwQueryKey
@ stdcall -private ZwQueryObject(long long long long long) ntdll.ZwQueryObject
@ stdcall -private ZwQuerySection(long long ptr long ptr) ntdll.ZwQuerySection
@ stdcall -private ZwQuerySecurityObject(long long long long long) ntdll.ZwQuerySecurityObject
@ stdcall -private ZwQuerySymbolicLinkObject(long ptr ptr) ntdll.ZwQuerySymbolicLinkObject
@ stdcall -private ZwQuerySystemInformation(long long long long) ntdll.ZwQuerySystemInformation
@ stdcall -private ZwQueryValueKey(long ptr long ptr long ptr) ntdll.ZwQueryValueKey
@ stdcall -private ZwQueryVolumeInformationFile(long ptr ptr long long) ntdll.ZwQueryVolumeInformationFile
@ stdcall -private ZwReadFile(long long ptr ptr ptr ptr long ptr ptr) ntdll.ZwReadFile
@ stdcall -private ZwReplaceKey(ptr long ptr) ntdll.ZwReplaceKey
@ stdcall -private ZwRequestWaitReplyPort(ptr ptr ptr) ntdll.ZwRequestWaitReplyPort
@ stdcall -private ZwResetEvent(long ptr) ntdll.ZwResetEvent
@ stdcall -private ZwRestoreKey(long long long) ntdll.ZwRestoreKey
@ stdcall -private ZwSaveKey(long long) ntdll.ZwSaveKey
@ stub ZwSaveKeyEx
@ stub ZwSetBootEntryOrder
@ stub ZwSetBootOptions
@ stdcall -private ZwSetDefaultLocale(long long) ntdll.ZwSetDefaultLocale
@ stdcall -private ZwSetDefaultUILanguage(long) ntdll.ZwSetDefaultUILanguage
@ stdcall -private ZwSetEaFile(long ptr ptr long) ntdll.ZwSetEaFile
@ stdcall -private ZwSetEvent(long long) ntdll.ZwSetEvent
@ stdcall -private ZwSetInformationFile(long long long long long) ntdll.ZwSetInformationFile
@ stdcall -private ZwSetInformationJobObject(long long ptr long) ntdll.ZwSetInformationJobObject
@ stdcall -private ZwSetInformationObject(long long ptr long) ntdll.ZwSetInformationObject
@ stdcall -private ZwSetInformationProcess(long long long long) ntdll.ZwSetInformationProcess
@ stdcall -private ZwSetInformationThread(long long ptr long) ntdll.ZwSetInformationThread
@ stdcall -private ZwSetSecurityObject(long long ptr) ntdll.ZwSetSecurityObject
@ stdcall -private ZwSetSystemInformation(long ptr long) ntdll.ZwSetSystemInformation
@ stdcall -private ZwSetSystemTime(ptr ptr) ntdll.ZwSetSystemTime
@ stdcall -private ZwSetTimer(long ptr ptr ptr long long ptr) ntdll.ZwSetTimer
@ stdcall -private ZwSetValueKey(long long long long long long) ntdll.ZwSetValueKey
@ stdcall -private ZwSetVolumeInformationFile(long ptr ptr long long) ntdll.ZwSetVolumeInformationFile
@ stdcall -private ZwTerminateJobObject(long long) ntdll.ZwTerminateJobObject
@ stdcall -private ZwTerminateProcess(long long) ntdll.ZwTerminateProcess
@ stub ZwTranslateFilePath
@ stdcall ZwUnloadDriver(ptr)
@ stdcall -private ZwUnloadKey(long) ntdll.ZwUnloadKey
@ stdcall -private ZwUnmapViewOfSection(long ptr) ntdll.ZwUnmapViewOfSection
@ stdcall -private ZwWaitForMultipleObjects(long ptr long long ptr) ntdll.ZwWaitForMultipleObjects
@ stdcall -private ZwWaitForSingleObject(long long long) ntdll.ZwWaitForSingleObject
@ stdcall -private ZwWriteFile(long long ptr ptr ptr ptr long ptr ptr) ntdll.ZwWriteFile
@ stdcall -private ZwYieldExecution() ntdll.ZwYieldExecution
@ cdecl -private -arch=i386 _CIcos() msvcrt._CIcos
@ cdecl -private -arch=i386 _CIsin() msvcrt._CIsin
@ cdecl -private -arch=i386 _CIsqrt() msvcrt._CIsqrt
@ cdecl -private _abnormal_termination() msvcrt._abnormal_termination
@ stdcall -private -arch=i386 -ret64 _alldiv(int64 int64) ntdll._alldiv
@ stub _alldvrm
@ stdcall -private -arch=i386 -ret64 _allmul(int64 int64) ntdll._allmul
@ stdcall -private -arch=i386 -norelay _alloca_probe() ntdll._alloca_probe
@ stdcall -private -arch=i386 -ret64 _allrem(int64 int64) ntdll._allrem
@ stdcall -private -arch=i386 -ret64 _allshl(int64 long) ntdll._allshl
@ stdcall -private -arch=i386 -ret64 _allshr(int64 long) ntdll._allshr
@ stdcall -private -arch=i386 -ret64 _aulldiv(int64 int64) ntdll._aulldiv
@ stub _aulldvrm
@ stdcall -private -arch=i386 -ret64 _aullrem(int64 int64) ntdll._aullrem
@ stdcall -private -arch=i386 -ret64 _aullshr(int64 long) ntdll._aullshr
@ cdecl -private -arch=i386 _except_handler2(ptr ptr ptr ptr) msvcrt._except_handler2
@ cdecl -private -arch=i386 _except_handler3(ptr ptr ptr ptr) msvcrt._except_handler3
@ cdecl -private -arch=i386 _global_unwind2(ptr) msvcrt._global_unwind2
@ cdecl -private _itoa(long ptr long) msvcrt._itoa
@ cdecl -private _itow(long ptr long) msvcrt._itow
@ cdecl -private -arch=x86_64 _local_unwind(ptr ptr) msvcrt._local_unwind
@ cdecl -private -arch=i386 _local_unwind2(ptr long) msvcrt._local_unwind2
@ cdecl -private _purecall() msvcrt._purecall
@ varargs -private _snprintf(ptr long str) msvcrt._snprintf
@ varargs -private _snwprintf(ptr long wstr) msvcrt._snwprintf
@ cdecl -private _stricmp(str str) msvcrt._stricmp
@ cdecl -private _strlwr(str) msvcrt._strlwr
@ cdecl -private _strnicmp(str str long) msvcrt._strnicmp
@ cdecl -private _strnset(str long long) msvcrt._strnset
@ cdecl -private _strrev(str) msvcrt._strrev
@ cdecl -private _strset(str long) msvcrt._strset
@ cdecl -private _strupr(str) msvcrt._strupr
@ cdecl -private _vsnprintf(ptr long str ptr) msvcrt._vsnprintf
@ cdecl -private _vsnwprintf(ptr long wstr ptr) msvcrt._vsnwprintf
@ cdecl -private _wcsicmp(wstr wstr) msvcrt._wcsicmp
@ cdecl -private _wcslwr(wstr) msvcrt._wcslwr
@ cdecl -private _wcsnicmp(wstr wstr long) msvcrt._wcsnicmp
@ cdecl -private _wcsnset(wstr long long) msvcrt._wcsnset
@ cdecl -private _wcsrev(wstr) msvcrt._wcsrev
@ cdecl -private _wcsupr(wstr) msvcrt._wcsupr
@ cdecl -private atoi(str) msvcrt.atoi
@ cdecl -private atol(str) msvcrt.atol
@ cdecl -private isdigit(long) msvcrt.isdigit
@ cdecl -private islower(long) msvcrt.islower
@ cdecl -private isprint(long) msvcrt.isprint
@ cdecl -private isspace(long) msvcrt.isspace
@ cdecl -private isupper(long) msvcrt.isupper
@ cdecl -private isxdigit(long) msvcrt.isxdigit
@ cdecl -private mbstowcs(ptr str long) msvcrt.mbstowcs
@ cdecl -private mbtowc(ptr str long) msvcrt.mbtowc
@ cdecl -private memchr(ptr long long) msvcrt.memchr
@ cdecl -private memcpy(ptr ptr long) msvcrt.memcpy
@ cdecl -private memmove(ptr ptr long) msvcrt.memmove
@ cdecl -private memset(ptr long long) msvcrt.memset
@ cdecl -private qsort(ptr long long ptr) msvcrt.qsort
@ cdecl -private rand() msvcrt.rand
@ varargs -private sprintf(ptr str) msvcrt.sprintf
@ cdecl -private srand(long) msvcrt.srand
@ cdecl -private strcat(str str) msvcrt.strcat
@ cdecl -private strchr(str long) msvcrt.strchr
@ cdecl -private strcmp(str str) msvcrt.strcmp
@ cdecl -private strcpy(ptr str) msvcrt.strcpy
@ cdecl -private strlen(str) msvcrt.strlen
@ cdecl -private strncat(str str long) msvcrt.strncat
@ cdecl -private strncmp(str str long) msvcrt.strncmp
@ cdecl -private strncpy(ptr str long) msvcrt.strncpy
@ cdecl -private strrchr(str long) msvcrt.strrchr
@ cdecl -private strspn(str str) msvcrt.strspn
@ cdecl -private strstr(str str) msvcrt.strstr
@ varargs -private swprintf(ptr wstr) msvcrt.swprintf
@ cdecl -private tolower(long) msvcrt.tolower
@ cdecl -private toupper(long) msvcrt.toupper
@ cdecl -private towlower(long) msvcrt.towlower
@ cdecl -private towupper(long) msvcrt.towupper
@ stdcall vDbgPrintEx(long long str ptr) ntdll.vDbgPrintEx
@ stdcall vDbgPrintExWithPrefix(str long long str ptr) ntdll.vDbgPrintExWithPrefix
@ cdecl -private vsprintf(ptr str ptr) msvcrt.vsprintf
@ cdecl -private wcscat(wstr wstr) msvcrt.wcscat
@ cdecl -private wcschr(wstr long) msvcrt.wcschr
@ cdecl -private wcscmp(wstr wstr) msvcrt.wcscmp
@ cdecl -private wcscpy(ptr wstr) msvcrt.wcscpy
@ cdecl -private wcscspn(wstr wstr) msvcrt.wcscspn
@ cdecl -private wcslen(wstr) msvcrt.wcslen
@ cdecl -private wcsncat(wstr wstr long) msvcrt.wcsncat
@ cdecl -private wcsncmp(wstr wstr long) msvcrt.wcsncmp
@ cdecl -private wcsncpy(ptr wstr long) msvcrt.wcsncpy
@ cdecl -private wcsrchr(wstr long) msvcrt.wcsrchr
@ cdecl -private wcsspn(wstr wstr) msvcrt.wcsspn
@ cdecl -private wcsstr(wstr wstr) msvcrt.wcsstr
@ cdecl -private wcstombs(ptr ptr long) msvcrt.wcstombs
@ cdecl -private wctomb(ptr long) msvcrt.wctomb

################################################################
# Wine internal extensions
#
# All functions must be prefixed with '__wine_' (for internal functions)
# or 'wine_' (for user-visible functions) to avoid namespace conflicts.

@ cdecl wine_ntoskrnl_main_loop(long)
