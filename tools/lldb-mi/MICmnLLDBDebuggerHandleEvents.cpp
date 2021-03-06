//===-- MICmnLLDBDebuggerHandleEvents.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Third party headers:
#include "lldb/API/SBAddress.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBBreakpoint.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBTarget.h"
#include "lldb/API/SBThread.h"
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBCommandReturnObject.h"
#include "lldb/API/SBUnixSignals.h"
#ifdef _WIN32
#include <io.h> // For the ::_access()
#else
#include <unistd.h> // For the ::access()
#endif              // _WIN32

// In-house headers:
#include "MICmnLLDBDebuggerHandleEvents.h"
#include "MICmnResources.h"
#include "MICmnLog.h"
#include "MICmnLLDBDebugger.h"
#include "MICmnLLDBDebugSessionInfo.h"
#include "MICmnMIResultRecord.h"
#include "MICmnMIValueConst.h"
#include "MICmnMIValueList.h"
#include "MICmnMIOutOfBandRecord.h"
#include "MICmnStreamStdout.h"
#include "MICmnStreamStderr.h"
#include "MIUtilDebug.h"
#include "MIDriver.h"
#include "Platform.h" // for PATH_MAX

//++ ------------------------------------------------------------------------------------
// Details: CMICmnLLDBDebuggerHandleEvents constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnLLDBDebuggerHandleEvents::CMICmnLLDBDebuggerHandleEvents(void)
{
}

//++ ------------------------------------------------------------------------------------
// Details: CMICmnLLDBDebuggerHandleEvents destructor.
// Type:    Overridable.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMICmnLLDBDebuggerHandleEvents::~CMICmnLLDBDebuggerHandleEvents(void)
{
    Shutdown();
}

//++ ------------------------------------------------------------------------------------
// Details: Initialize resources for *this broardcaster object.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::Initialize(void)
{
    m_clientUsageRefCnt++;

    if (m_bInitialized)
        return MIstatus::success;

    m_bInitialized = MIstatus::success;
    m_bSignalsInitialized = false;
    m_SIGINT = 0;
    m_SIGSTOP = 0;
    m_SIGSEGV = 0;
    m_SIGTRAP = 0;

    return m_bInitialized;
}

//++ ------------------------------------------------------------------------------------
// Details: Release resources for *this broardcaster object.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::Shutdown(void)
{
    if (--m_clientUsageRefCnt > 0)
        return MIstatus::success;

    if (!m_bInitialized)
        return MIstatus::success;

    m_bInitialized = false;

    return MIstatus::success;
}

//++ ------------------------------------------------------------------------------------
// Details: Interpret the event object to asscertain the action to take or information to
//          to form and put in a MI Out-of-band record object which is given to stdout.
// Type:    Method.
// Args:    vEvent          - (R) An LLDB broadcast event.
//          vrbHandledEvent - (W) True - event handled, false = not handled.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleEvent(const lldb::SBEvent &vEvent, bool &vrbHandledEvent)
{
    bool bOk = MIstatus::success;
    vrbHandledEvent = false;

    if (lldb::SBProcess::EventIsProcessEvent(vEvent))
    {
        vrbHandledEvent = true;
        bOk = HandleEventSBProcess(vEvent);
    }
    else if (lldb::SBBreakpoint::EventIsBreakpointEvent(vEvent))
    {
        vrbHandledEvent = true;
        bOk = HandleEventSBBreakPoint(vEvent);
    }
    else if (lldb::SBThread::EventIsThreadEvent(vEvent))
    {
        vrbHandledEvent = true;
        bOk = HandleEventSBThread(vEvent);
    }
    else if (lldb::SBTarget::EventIsTargetEvent(vEvent))
    {
        vrbHandledEvent = true;
        bOk = HandleEventSBTarget(vEvent);
    }
    else if (lldb::SBCommandInterpreter::EventIsCommandInterpreterEvent(vEvent))
    {
        vrbHandledEvent = true;
        bOk = HandleEventSBCommandInterpreter(vEvent);
    }

    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Handle a LLDB SBProcess event.
// Type:    Method.
// Args:    vEvent          - (R) An LLDB broadcast event.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleEventSBProcess(const lldb::SBEvent &vEvent)
{
    bool bOk = MIstatus::success;

    const MIchar *pEventType = "";
    const MIuint nEventType = vEvent.GetType();
    switch (nEventType)
    {
        case lldb::SBProcess::eBroadcastBitInterrupt:
            pEventType = "eBroadcastBitInterrupt";
            break;
        case lldb::SBProcess::eBroadcastBitProfileData:
            pEventType = "eBroadcastBitProfileData";
            break;
        case lldb::SBProcess::eBroadcastBitStateChanged:
            pEventType = "eBroadcastBitStateChanged";
            bOk = HandleProcessEventBroadcastBitStateChanged(vEvent);
            break;
        case lldb::SBProcess::eBroadcastBitSTDERR:
            pEventType = "eBroadcastBitSTDERR";
            bOk = GetProcessStderr();
            break;
        case lldb::SBProcess::eBroadcastBitSTDOUT:
            pEventType = "eBroadcastBitSTDOUT";
            bOk = GetProcessStdout();
            break;
        default:
        {
            const CMIUtilString msg(CMIUtilString::Format(MIRSRC(IDS_LLDBOUTOFBAND_ERR_UNKNOWN_EVENT), "SBProcess", (MIuint)nEventType));
            SetErrorDescription(msg);
            return MIstatus::failure;
        }
    }
    m_pLog->WriteLog(CMIUtilString::Format("##### An SB Process event occurred: %s", pEventType));

    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Handle a LLDB SBBreakpoint event.
// Type:    Method.
// Args:    vEvent  - (R) An LLDB broadcast event.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleEventSBBreakPoint(const lldb::SBEvent &vEvent)
{
    bool bOk = MIstatus::success;

    const MIchar *pEventType = "";
    const lldb::BreakpointEventType eEvent = lldb::SBBreakpoint::GetBreakpointEventTypeFromEvent(vEvent);
    switch (eEvent)
    {
        case lldb::eBreakpointEventTypeThreadChanged:
            pEventType = "eBreakpointEventTypeThreadChanged";
            break;
        case lldb::eBreakpointEventTypeLocationsRemoved:
            pEventType = "eBreakpointEventTypeLocationsRemoved";
            break;
        case lldb::eBreakpointEventTypeInvalidType:
            pEventType = "eBreakpointEventTypeInvalidType";
            break;
        case lldb::eBreakpointEventTypeLocationsAdded:
            pEventType = "eBreakpointEventTypeLocationsAdded";
            bOk = HandleEventSBBreakpointLocationsAdded(vEvent);
            break;
        case lldb::eBreakpointEventTypeAdded:
            pEventType = "eBreakpointEventTypeAdded";
            bOk = HandleEventSBBreakpointAdded(vEvent);
            break;
        case lldb::eBreakpointEventTypeRemoved:
            pEventType = "eBreakpointEventTypeRemoved";
            bOk = HandleEventSBBreakpointCmn(vEvent);
            break;
        case lldb::eBreakpointEventTypeLocationsResolved:
            pEventType = "eBreakpointEventTypeLocationsResolved";
            break;
        case lldb::eBreakpointEventTypeEnabled:
            pEventType = "eBreakpointEventTypeEnabled";
            bOk = HandleEventSBBreakpointCmn(vEvent);
            break;
        case lldb::eBreakpointEventTypeDisabled:
            pEventType = "eBreakpointEventTypeDisabled";
            bOk = HandleEventSBBreakpointCmn(vEvent);
            break;
        case lldb::eBreakpointEventTypeCommandChanged:
            pEventType = "eBreakpointEventTypeCommandChanged";
            bOk = HandleEventSBBreakpointCmn(vEvent);
            break;
        case lldb::eBreakpointEventTypeConditionChanged:
            pEventType = "eBreakpointEventTypeConditionChanged";
            bOk = HandleEventSBBreakpointCmn(vEvent);
            break;
        case lldb::eBreakpointEventTypeIgnoreChanged:
            pEventType = "eBreakpointEventTypeIgnoreChanged";
            bOk = HandleEventSBBreakpointCmn(vEvent);
            break;
    }
    m_pLog->WriteLog(CMIUtilString::Format("##### An SB Breakpoint event occurred: %s", pEventType));

    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Handle a LLDB SBBreakpoint event.
// Type:    Method.
// Args:    vEvent  - (R) An LLDB broadcast event.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleEventSBBreakpointLocationsAdded(const lldb::SBEvent &vEvent)
{
    const MIuint nLoc = lldb::SBBreakpoint::GetNumBreakpointLocationsFromEvent(vEvent);
    if (nLoc == 0)
        return MIstatus::success;

    lldb::SBBreakpoint brkPt = lldb::SBBreakpoint::GetBreakpointFromEvent(vEvent);
    const CMIUtilString plural((nLoc == 1) ? "" : "s");
    const CMIUtilString msg(CMIUtilString::Format("%d location%s added to breakpoint %d", nLoc, plural.c_str(), brkPt.GetID()));

    return TextToStdout(msg);
}

//++ ------------------------------------------------------------------------------------
// Details: Handle a LLDB SBBreakpoint event.
// Type:    Method.
// Args:    vEvent  - (R) An LLDB broadcast event.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleEventSBBreakpointCmn(const lldb::SBEvent &vEvent)
{
    lldb::SBBreakpoint brkPt = lldb::SBBreakpoint::GetBreakpointFromEvent(vEvent);
    if (!brkPt.IsValid())
        return MIstatus::success;

    CMICmnLLDBDebugSessionInfo &rSessionInfo(CMICmnLLDBDebugSessionInfo::Instance());
    CMICmnLLDBDebugSessionInfo::SBrkPtInfo sBrkPtInfo;
    if (!rSessionInfo.GetBrkPtInfo(brkPt, sBrkPtInfo))
    {
        SetErrorDescription(
            CMIUtilString::Format(MIRSRC(IDS_LLDBOUTOFBAND_ERR_BRKPT_INFO_GET), "HandleEventSBBreakpointCmn()", brkPt.GetID()));
        return MIstatus::failure;
    }

    // CODETAG_LLDB_BREAKPOINT_CREATION
    // This is in a worker thread
    // Add more breakpoint information or overwrite existing information
    CMICmnLLDBDebugSessionInfo::SBrkPtInfo sBrkPtInfoRec;
    if (!rSessionInfo.RecordBrkPtInfoGet(brkPt.GetID(), sBrkPtInfoRec))
    {
        SetErrorDescription(
            CMIUtilString::Format(MIRSRC(IDS_LLDBOUTOFBAND_ERR_BRKPT_NOTFOUND), "HandleEventSBBreakpointCmn()", brkPt.GetID()));
        return MIstatus::failure;
    }
    sBrkPtInfo.m_bDisp = sBrkPtInfoRec.m_bDisp;
    sBrkPtInfo.m_bEnabled = brkPt.IsEnabled();
    sBrkPtInfo.m_bHaveArgOptionThreadGrp = false;
    sBrkPtInfo.m_strOptThrdGrp = "";
    sBrkPtInfo.m_nTimes = brkPt.GetHitCount();
    sBrkPtInfo.m_strOrigLoc = sBrkPtInfoRec.m_strOrigLoc;
    sBrkPtInfo.m_nIgnore = sBrkPtInfoRec.m_nIgnore;
    sBrkPtInfo.m_bPending = sBrkPtInfoRec.m_bPending;
    sBrkPtInfo.m_bCondition = sBrkPtInfoRec.m_bCondition;
    sBrkPtInfo.m_strCondition = sBrkPtInfoRec.m_strCondition;
    sBrkPtInfo.m_bBrkPtThreadId = sBrkPtInfoRec.m_bBrkPtThreadId;
    sBrkPtInfo.m_nBrkPtThreadId = sBrkPtInfoRec.m_nBrkPtThreadId;

    // MI print "=breakpoint-modified,bkpt={number=\"%d\",type=\"breakpoint\",disp=\"%s\",enabled=\"%c\",addr=\"0x%016" PRIx64 "\",
    // func=\"%s\",file=\"%s\",fullname=\"%s/%s\",line=\"%d\",times=\"%d\",original-location=\"%s\"}"
    CMICmnMIValueTuple miValueTuple;
    if (!rSessionInfo.MIResponseFormBrkPtInfo(sBrkPtInfo, miValueTuple))
    {
        SetErrorDescription(CMIUtilString::Format(MIRSRC(IDS_LLDBOUTOFBAND_ERR_FORM_MI_RESPONSE), "HandleEventSBBreakpointCmn()"));
        return MIstatus::failure;
    }

    const CMICmnMIValueResult miValueResultC("bkpt", miValueTuple);
    const CMICmnMIOutOfBandRecord miOutOfBandRecord(CMICmnMIOutOfBandRecord::eOutOfBand_BreakPointModified, miValueResultC);
    const bool bOk = MiOutOfBandRecordToStdout(miOutOfBandRecord);

    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Handle a LLDB SBBreakpoint added event.
//          Add more breakpoint information or overwrite existing information.
//          Normally a break point session info objects exists by now when an MI command
//          was issued to insert a break so the retrieval would normally always succeed
//          however should a user type "b main" into a console then LLDB will create a
//          breakpoint directly, hence no MI command, hence no previous record of the
//          breakpoint so RecordBrkPtInfoGet() will fail. We still get the event though
//          so need to create a breakpoint info object here and send appropriate MI
//          response.
// Type:    Method.
// Args:    vEvent  - (R) An LLDB broadcast event.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleEventSBBreakpointAdded(const lldb::SBEvent &vEvent)
{
    lldb::SBBreakpoint brkPt = lldb::SBBreakpoint::GetBreakpointFromEvent(vEvent);
    if (!brkPt.IsValid())
        return MIstatus::success;

    CMICmnLLDBDebugSessionInfo &rSessionInfo(CMICmnLLDBDebugSessionInfo::Instance());
    CMICmnLLDBDebugSessionInfo::SBrkPtInfo sBrkPtInfo;
    if (!rSessionInfo.GetBrkPtInfo(brkPt, sBrkPtInfo))
    {
        SetErrorDescription(
            CMIUtilString::Format(MIRSRC(IDS_LLDBOUTOFBAND_ERR_BRKPT_INFO_GET), "HandleEventSBBreakpointAdded()", brkPt.GetID()));
        return MIstatus::failure;
    }

    // CODETAG_LLDB_BREAKPOINT_CREATION
    // This is in a worker thread
    CMICmnLLDBDebugSessionInfo::SBrkPtInfo sBrkPtInfoRec;
    const bool bBrkPtExistAlready = rSessionInfo.RecordBrkPtInfoGet(brkPt.GetID(), sBrkPtInfoRec);
    if (bBrkPtExistAlready)
    {
        // Update breakpoint information object
        sBrkPtInfo.m_bDisp = sBrkPtInfoRec.m_bDisp;
        sBrkPtInfo.m_bEnabled = brkPt.IsEnabled();
        sBrkPtInfo.m_bHaveArgOptionThreadGrp = false;
        sBrkPtInfo.m_strOptThrdGrp.clear();
        sBrkPtInfo.m_nTimes = brkPt.GetHitCount();
        sBrkPtInfo.m_strOrigLoc = sBrkPtInfoRec.m_strOrigLoc;
        sBrkPtInfo.m_nIgnore = sBrkPtInfoRec.m_nIgnore;
        sBrkPtInfo.m_bPending = sBrkPtInfoRec.m_bPending;
        sBrkPtInfo.m_bCondition = sBrkPtInfoRec.m_bCondition;
        sBrkPtInfo.m_strCondition = sBrkPtInfoRec.m_strCondition;
        sBrkPtInfo.m_bBrkPtThreadId = sBrkPtInfoRec.m_bBrkPtThreadId;
        sBrkPtInfo.m_nBrkPtThreadId = sBrkPtInfoRec.m_nBrkPtThreadId;
    }
    else
    {
        // Create a breakpoint information object
        sBrkPtInfo.m_bDisp = brkPt.IsOneShot();
        sBrkPtInfo.m_bEnabled = brkPt.IsEnabled();
        sBrkPtInfo.m_bHaveArgOptionThreadGrp = false;
        sBrkPtInfo.m_strOptThrdGrp.clear();
        sBrkPtInfo.m_strOrigLoc = CMIUtilString::Format("%s:%d", sBrkPtInfo.m_fileName.c_str(), sBrkPtInfo.m_nLine);
        sBrkPtInfo.m_nIgnore = brkPt.GetIgnoreCount();
        sBrkPtInfo.m_bPending = false;
        const MIchar *pStrCondition = brkPt.GetCondition();
        sBrkPtInfo.m_bCondition = (pStrCondition != nullptr) ? true : false;
        sBrkPtInfo.m_strCondition = (pStrCondition != nullptr) ? pStrCondition : "??";
        sBrkPtInfo.m_bBrkPtThreadId = (brkPt.GetThreadID() != 0) ? true : false;
        sBrkPtInfo.m_nBrkPtThreadId = brkPt.GetThreadID();
    }

    CMICmnMIValueTuple miValueTuple;
    if (!rSessionInfo.MIResponseFormBrkPtInfo(sBrkPtInfo, miValueTuple))
    {
        SetErrorDescription(CMIUtilString::Format(MIRSRC(IDS_LLDBOUTOFBAND_ERR_FORM_MI_RESPONSE), "HandleEventSBBreakpointAdded()"));
        return MIstatus::failure;
    }

    bool bOk = MIstatus::success;
    if (bBrkPtExistAlready)
    {
        // MI print
        // "=breakpoint-modified,bkpt={number=\"%d\",type=\"breakpoint\",disp=\"%s\",enabled=\"%c\",addr=\"0x%016" PRIx64 "\",func=\"%s\",file=\"%s\",fullname=\"%s/%s\",line=\"%d\",times=\"%d\",original-location=\"%s\"}"
        const CMICmnMIValueResult miValueResult("bkpt", miValueTuple);
        const CMICmnMIOutOfBandRecord miOutOfBandRecord(CMICmnMIOutOfBandRecord::eOutOfBand_BreakPointModified, miValueResult);
        bOk = MiOutOfBandRecordToStdout(miOutOfBandRecord);
    }
    else
    {
        // CODETAG_LLDB_BRKPT_ID_MAX
        if (brkPt.GetID() > (lldb::break_id_t)rSessionInfo.m_nBrkPointCntMax)
        {
            SetErrorDescription(CMIUtilString::Format(MIRSRC(IDS_CMD_ERR_BRKPT_CNT_EXCEEDED), "HandleEventSBBreakpointAdded()",
                                                      rSessionInfo.m_nBrkPointCntMax, sBrkPtInfo.m_id));
            return MIstatus::failure;
        }
        if (!rSessionInfo.RecordBrkPtInfo(brkPt.GetID(), sBrkPtInfo))
        {
            SetErrorDescription(
                CMIUtilString::Format(MIRSRC(IDS_LLDBOUTOFBAND_ERR_BRKPT_INFO_SET), "HandleEventSBBreakpointAdded()", sBrkPtInfo.m_id));
            return MIstatus::failure;
        }

        // MI print
        // "=breakpoint-created,bkpt={number=\"%d\",type=\"breakpoint\",disp=\"%s\",enabled=\"%c\",addr=\"0x%016" PRIx64 "\",func=\"%s\",file=\"%s\",fullname=\"%s/%s\",line=\"%d\",times=\"%d\",original-location=\"%s\"}"
        const CMICmnMIValueResult miValueResult("bkpt", miValueTuple);
        const CMICmnMIOutOfBandRecord miOutOfBandRecord(CMICmnMIOutOfBandRecord::eOutOfBand_BreakPointCreated, miValueResult);
        bOk = MiOutOfBandRecordToStdout(miOutOfBandRecord);
    }

    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Handle a LLDB SBThread event.
// Type:    Method.
// Args:    vEvent  - (R) An LLDB broadcast event.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleEventSBThread(const lldb::SBEvent &vEvent)
{
    if (!ChkForStateChanges())
        return MIstatus::failure;

    bool bOk = MIstatus::success;
    const MIchar *pEventType = "";
    const MIuint nEventType = vEvent.GetType();
    switch (nEventType)
    {
        case lldb::SBThread::eBroadcastBitStackChanged:
            pEventType = "eBroadcastBitStackChanged";
            bOk = HandleEventSBThreadBitStackChanged(vEvent);
            break;
        case lldb::SBThread::eBroadcastBitThreadSuspended:
            pEventType = "eBroadcastBitThreadSuspended";
            bOk = HandleEventSBThreadSuspended(vEvent);
            break;
        case lldb::SBThread::eBroadcastBitThreadResumed:
            pEventType = "eBroadcastBitThreadResumed";
            break;
        case lldb::SBThread::eBroadcastBitSelectedFrameChanged:
            pEventType = "eBroadcastBitSelectedFrameChanged";
            break;
        case lldb::SBThread::eBroadcastBitThreadSelected:
            pEventType = "eBroadcastBitThreadSelected";
            break;
        default:
        {
            const CMIUtilString msg(CMIUtilString::Format(MIRSRC(IDS_LLDBOUTOFBAND_ERR_UNKNOWN_EVENT), "SBThread", (MIuint)nEventType));
            SetErrorDescription(msg);
            return MIstatus::failure;
        }
    }
    m_pLog->WriteLog(CMIUtilString::Format("##### An SBThread event occurred: %s", pEventType));

    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Handle a LLDB SBThread event.
// Type:    Method.
// Args:    vEvent  - (R) An LLDB broadcast event.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleEventSBThreadSuspended(const lldb::SBEvent &vEvent)
{
    lldb::SBThread thread = lldb::SBThread::GetThreadFromEvent(vEvent);
    if (!thread.IsValid())
        return MIstatus::success;

    const lldb::StopReason eStopReason = thread.GetStopReason();
    if (eStopReason != lldb::eStopReasonSignal)
        return MIstatus::success;

    // MI print "@thread=%d,signal=%lld"
    const MIuint64 nId = thread.GetStopReasonDataAtIndex(0);
    const CMIUtilString strThread(CMIUtilString::Format("%d", thread.GetThreadID()));
    const CMICmnMIValueConst miValueConst(strThread);
    const CMICmnMIValueResult miValueResult("thread", miValueConst);
    CMICmnMIOutOfBandRecord miOutOfBandRecord(CMICmnMIOutOfBandRecord::eOutOfBand_Thread, miValueResult);
    const CMIUtilString strSignal(CMIUtilString::Format("%lld", nId));
    const CMICmnMIValueConst miValueConst2(strSignal);
    const CMICmnMIValueResult miValueResult2("signal", miValueConst2);
    bool bOk = miOutOfBandRecord.Add(miValueResult2);
    bOk = bOk && MiOutOfBandRecordToStdout(miOutOfBandRecord);

    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Handle a LLDB SBThread event.
// Type:    Method.
// Args:    vEvent  - (R) An LLDB broadcast event.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleEventSBThreadBitStackChanged(const lldb::SBEvent &vEvent)
{
    lldb::SBThread thread = lldb::SBThread::GetThreadFromEvent(vEvent);
    if (!thread.IsValid())
        return MIstatus::success;

    lldb::SBStream streamOut;
    const bool bOk = thread.GetStatus(streamOut);
    return bOk && TextToStdout(streamOut.GetData());
}

//++ ------------------------------------------------------------------------------------
// Details: Handle a LLDB SBTarget event.
// Type:    Method.
// Args:    vEvent  - (R) An LLDB broadcast event.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleEventSBTarget(const lldb::SBEvent &vEvent)
{
    if (!ChkForStateChanges())
        return MIstatus::failure;

    bool bOk = MIstatus::success;
    const MIchar *pEventType = "";
    const MIuint nEventType = vEvent.GetType();
    switch (nEventType)
    {
        case lldb::SBTarget::eBroadcastBitBreakpointChanged:
            pEventType = "eBroadcastBitBreakpointChanged";
            break;
        case lldb::SBTarget::eBroadcastBitModulesLoaded:
            pEventType = "eBroadcastBitModulesLoaded";
            bOk = HandleTargetEventBroadcastBitModulesLoaded(vEvent);
            break;
        case lldb::SBTarget::eBroadcastBitModulesUnloaded:
            pEventType = "eBroadcastBitModulesUnloaded";
            bOk = HandleTargetEventBroadcastBitModulesUnloaded(vEvent);
            break;
        case lldb::SBTarget::eBroadcastBitWatchpointChanged:
            pEventType = "eBroadcastBitWatchpointChanged";
            break;
        case lldb::SBTarget::eBroadcastBitSymbolsLoaded:
            pEventType = "eBroadcastBitSymbolsLoaded";
            break;
        default:
        {
            const CMIUtilString msg(CMIUtilString::Format(MIRSRC(IDS_LLDBOUTOFBAND_ERR_UNKNOWN_EVENT), "SBTarget", (MIuint)nEventType));
            SetErrorDescription(msg);
            return MIstatus::failure;
        }
    }
    m_pLog->WriteLog(CMIUtilString::Format("##### An SBTarget event occurred: %s", pEventType));

    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Print to stdout "=shlibs-added,shlib-info=[key=\"value\"...]"
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleTargetEventBroadcastBitModulesLoaded(const lldb::SBEvent &vEvent)
{
    static MIuint s_nModulesLoadedNumber(0);
    const MIuint nSize(lldb::SBTarget::GetNumModulesFromEvent(vEvent));
    bool bOk = MIstatus::success;
    for (MIuint nIndex(0); bOk && (nIndex < nSize); ++nIndex)
    {
        const lldb::SBModule sbModule = lldb::SBTarget::GetModuleAtIndexFromEvent(nIndex, vEvent);
        CMICmnMIValueList miValueList(true);
        bOk = MiHelpGetModuleInfo(sbModule, ++s_nModulesLoadedNumber, miValueList);
        const CMICmnMIValueResult miValueResult("shlib-info", miValueList);
        const CMICmnMIOutOfBandRecord miOutOfBandRecord(CMICmnMIOutOfBandRecord::eOutOfBand_TargetModulesLoaded, miValueResult);
        bOk = bOk && MiOutOfBandRecordToStdout(miOutOfBandRecord);
    }

    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Print to stdout "=shlibs-removed,shlib-info=[key=\"value\"...]"
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleTargetEventBroadcastBitModulesUnloaded(const lldb::SBEvent &vEvent)
{
    static MIuint s_nModulesUnloadedNumber(0);
    const MIuint nSize(lldb::SBTarget::GetNumModulesFromEvent(vEvent));
    bool bOk = MIstatus::success;
    for (MIuint nIndex(0); bOk && (nIndex < nSize); ++nIndex)
    {
        const lldb::SBModule sbModule = lldb::SBTarget::GetModuleAtIndexFromEvent(nIndex, vEvent);
        CMICmnMIValueList miValueList(true);
        bOk = MiHelpGetModuleInfo(sbModule, ++s_nModulesUnloadedNumber, miValueList);
        const CMICmnMIValueResult miValueResult("shlib-info", miValueList);
        const CMICmnMIOutOfBandRecord miOutOfBandRecord(CMICmnMIOutOfBandRecord::eOutOfBand_TargetModulesUnloaded, miValueResult);
        bOk = bOk && MiOutOfBandRecordToStdout(miOutOfBandRecord);
    }

    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Build module information for shlib-info "[num=\"%ld\",name=\"%s\",dyld-addr=\"%#lx\",reason=\"dyld\",path=\"%s\",loaded_addr=\"%#lx\",dsym-objpath=\"%s\"]"
// Type:    Method.
// Args:    vwrMiValueList    - (W) MI value list object.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::MiHelpGetModuleInfo(const lldb::SBModule &vModule, const MIuint nModuleNum,
                                                    CMICmnMIValueList &vwrMiValueList)
{
    bool bOk = MIstatus::success;

    // Build "num" field
    const CMIUtilString strNum(CMIUtilString::Format("%ld", nModuleNum));
    const CMICmnMIValueConst miValueConst(strNum);
    const CMICmnMIValueResult miValueResult("num", miValueConst);
    bOk = bOk && vwrMiValueList.Add(miValueResult);
    // Build "name" field
    const CMICmnMIValueConst miValueConst2(vModule.GetPlatformFileSpec().GetFilename());
    const CMICmnMIValueResult miValueResult2("name", miValueConst2);
    bOk = bOk && vwrMiValueList.Add(miValueResult2);
    // Build "dyld-addr" field
    const lldb::SBAddress sbAddress(vModule.GetObjectFileHeaderAddress());
    const CMICmnLLDBDebugSessionInfo &rSessionInfo(CMICmnLLDBDebugSessionInfo::Instance());
    const lldb::addr_t nLoadAddress(sbAddress.GetLoadAddress(rSessionInfo.GetTarget()));
    const CMIUtilString strDyldAddr(CMIUtilString::Format("%#lx", nLoadAddress));
    const CMICmnMIValueConst miValueConst3(nLoadAddress != LLDB_INVALID_ADDRESS ? strDyldAddr : "-");
    const CMICmnMIValueResult miValueResult3("dyld-addr", miValueConst3);
    bOk = bOk && vwrMiValueList.Add(miValueResult3);
    // Build "reason" field
    const CMICmnMIValueConst miValueConst4("dyld");
    const CMICmnMIValueResult miValueResult4("reason", miValueConst4);
    bOk = bOk && vwrMiValueList.Add(miValueResult4);
    // Build "path" field
    char path[PATH_MAX];
    vModule.GetPlatformFileSpec().GetPath(path, sizeof(path));
    const CMIUtilString strPlatformPath(path);
    const CMICmnMIValueConst miValueConst5(strPlatformPath);
    const CMICmnMIValueResult miValueResult5("path", miValueConst5);
    bOk = bOk && vwrMiValueList.Add(miValueResult5);
    // Build "loaded_addr" field
    const CMIUtilString strLoadedAddr(CMIUtilString::Format("%#lx", nLoadAddress));
    const CMICmnMIValueConst miValueConst6(nLoadAddress != LLDB_INVALID_ADDRESS ? strDyldAddr : "-");
    const CMICmnMIValueResult miValueResult6("loaded_addr", miValueConst6);
    bOk = bOk && vwrMiValueList.Add(miValueResult6);
    // Build "dsym-objpath" field
    vModule.GetSymbolFileSpec().GetPath(path, sizeof(path));
    const CMIUtilString strSymbolFilePath(path);
    if (!CMIUtilString::Compare(strPlatformPath, strSymbolFilePath))
    {
        const CMICmnMIValueConst miValueConst7(strSymbolFilePath);
        const CMICmnMIValueResult miValueResult7("dsym-objpath", miValueConst7);
        bOk = bOk && vwrMiValueList.Add(miValueResult7);
    }

    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Handle a LLDB SBCommandInterpreter event.
// Type:    Method.
// Args:    vEvent  - (R) An LLDB command interpreter event.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleEventSBCommandInterpreter(const lldb::SBEvent &vEvent)
{
    // This function is not used
    // *** This function is under development

    const MIchar *pEventType = "";
    const MIuint nEventType = vEvent.GetType();
    switch (nEventType)
    {
        case lldb::SBCommandInterpreter::eBroadcastBitThreadShouldExit:
            pEventType = "eBroadcastBitThreadShouldExit";
        // ToDo: IOR: Reminder to maybe handle this here
        // const MIuint nEventType = event.GetType();
        // if (nEventType & lldb::SBCommandInterpreter::eBroadcastBitThreadShouldExit)
        //{
        //  m_pClientDriver->SetExitApplicationFlag();
        //  vrbYesExit = true;
        //  return MIstatus::success;
        //}     break;
        case lldb::SBCommandInterpreter::eBroadcastBitResetPrompt:
            pEventType = "eBroadcastBitResetPrompt";
            break;
        case lldb::SBCommandInterpreter::eBroadcastBitQuitCommandReceived:
        {
            pEventType = "eBroadcastBitQuitCommandReceived";
            const bool bForceExit = true;
            CMICmnLLDBDebugger::Instance().GetDriver().SetExitApplicationFlag(bForceExit);
            break;
        }
        case lldb::SBCommandInterpreter::eBroadcastBitAsynchronousOutputData:
            pEventType = "eBroadcastBitAsynchronousOutputData";
            break;
        case lldb::SBCommandInterpreter::eBroadcastBitAsynchronousErrorData:
            pEventType = "eBroadcastBitAsynchronousErrorData";
            break;
        default:
        {
            const CMIUtilString msg(
                CMIUtilString::Format(MIRSRC(IDS_LLDBOUTOFBAND_ERR_UNKNOWN_EVENT), "SBCommandInterpreter", (MIuint)nEventType));
            SetErrorDescription(msg);
            return MIstatus::failure;
        }
    }
    m_pLog->WriteLog(CMIUtilString::Format("##### An SBCommandInterpreter event occurred: %s", pEventType));

    return MIstatus::success;
}

//++ ------------------------------------------------------------------------------------
// Details: Handle SBProcess event eBroadcastBitStateChanged.
// Type:    Method.
// Args:    vEvent          - (R) An LLDB event object.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleProcessEventBroadcastBitStateChanged(const lldb::SBEvent &vEvent)
{
    if (lldb::SBProcess::GetRestartedFromEvent (vEvent))
        return true;
    bool bOk = ChkForStateChanges();
    bOk = bOk && GetProcessStdout();
    bOk = bOk && GetProcessStderr();
    if (!bOk)
        return MIstatus::failure;

    // Something changed in the process; get the event and report the process's current
    // status and location
    const lldb::StateType eEventState = lldb::SBProcess::GetStateFromEvent(vEvent);
    if (eEventState == lldb::eStateInvalid)
        return MIstatus::success;

    lldb::SBProcess process = lldb::SBProcess::GetProcessFromEvent(vEvent);
    if (!process.IsValid())
    {
        const CMIUtilString msg(CMIUtilString::Format(MIRSRC(IDS_LLDBOUTOFBAND_ERR_PROCESS_INVALID), "SBProcess",
                                                      "HandleProcessEventBroadcastBitStateChanged()"));
        SetErrorDescription(msg);
        return MIstatus::failure;
    }

    bool bShouldBrk = true;
    const MIchar *pEventType = "";
    switch (eEventState)
    {
        case lldb::eStateUnloaded:
            pEventType = "eStateUnloaded";
            break;
        case lldb::eStateConnected:
            pEventType = "eStateConnected";
            break;
        case lldb::eStateAttaching:
            pEventType = "eStateAttaching";
            break;
        case lldb::eStateLaunching:
            pEventType = "eStateLaunching";
            break;
        case lldb::eStateStopped:
            pEventType = "eStateStopped";
            bOk = HandleProcessEventStateStopped(bShouldBrk);
            if (bShouldBrk)
                break;
        case lldb::eStateCrashed:
        case lldb::eStateSuspended:
            pEventType = "eStateSuspended";
            bOk = HandleProcessEventStateSuspended(vEvent);
            break;
        case lldb::eStateRunning:
            pEventType = "eStateRunning";
            bOk = HandleProcessEventStateRunning();
            break;
        case lldb::eStateStepping:
            pEventType = "eStateStepping";
            break;
        case lldb::eStateDetached:
            pEventType = "eStateDetached";
            break;
        case lldb::eStateExited:
            // Don't exit from lldb-mi here. We should be able to re-run target.
            pEventType = "eStateExited";
            bOk = HandleProcessEventStateExited();
            break;
        default:
        {
            const CMIUtilString msg(CMIUtilString::Format(MIRSRC(IDS_LLDBOUTOFBAND_ERR_UNKNOWN_EVENT), "SBProcess BroadcastBitStateChanged",
                                                          (MIuint)eEventState));
            SetErrorDescription(msg);
            return MIstatus::failure;
        }
    }

    // ToDo: Remove when finished coding application
    m_pLog->WriteLog(CMIUtilString::Format("##### An SB Process event BroadcastBitStateChanged occurred: %s", pEventType));

    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Asynchronous event handler for LLDB Process state suspended.
// Type:    Method.
// Args:    vEvent  - (R) An LLDB event object.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleProcessEventStateSuspended(const lldb::SBEvent &vEvent)
{
    // Make sure the program hasn't been auto-restarted:
    if (lldb::SBProcess::GetRestartedFromEvent(vEvent))
        return MIstatus::success;

    bool bOk = MIstatus::success;
    lldb::SBDebugger &rDebugger = CMICmnLLDBDebugSessionInfo::Instance().GetDebugger();
    lldb::SBProcess sbProcess = CMICmnLLDBDebugSessionInfo::Instance().GetProcess();
    lldb::SBTarget target = sbProcess.GetTarget();
    if (rDebugger.GetSelectedTarget() == target)
    {
        if (!UpdateSelectedThread())
            return MIstatus::failure;

        lldb::SBCommandReturnObject result;
        const lldb::ReturnStatus status = rDebugger.GetCommandInterpreter().HandleCommand("process status", result, false);
        MIunused(status);
        bOk = TextToStderr(result.GetError());
        bOk = bOk && TextToStdout(result.GetOutput());
    }
    else
    {
        lldb::SBStream streamOut;
        const MIuint nTargetIndex = rDebugger.GetIndexOfTarget(target);
        if (nTargetIndex != UINT_MAX)
            streamOut.Printf("Target %d: (", nTargetIndex);
        else
            streamOut.Printf("Target <unknown index>: (");
        target.GetDescription(streamOut, lldb::eDescriptionLevelBrief);
        streamOut.Printf(") stopped.\n");
        bOk = TextToStdout(streamOut.GetData());
    }

    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Print to stdout MI formatted text to indicate process stopped.
// Type:    Method.
// Args:    vwrbShouldBrk   - (W) True = Yes break, false = do not.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleProcessEventStateStopped(bool &vwrbShouldBrk)
{
    if (!UpdateSelectedThread())
        return MIstatus::failure;

    const MIchar *pEventType = "";
    bool bOk = MIstatus::success;
    lldb::SBProcess sbProcess = CMICmnLLDBDebugSessionInfo::Instance().GetProcess();
    const lldb::StopReason eStoppedReason = sbProcess.GetSelectedThread().GetStopReason();
    switch (eStoppedReason)
    {
        case lldb::eStopReasonInvalid:
            pEventType = "eStopReasonInvalid";
            vwrbShouldBrk = false;
            break;
        case lldb::eStopReasonNone:
            pEventType = "eStopReasonNone";
            break;
        case lldb::eStopReasonTrace:
            pEventType = "eStopReasonTrace";
            bOk = HandleProcessEventStopReasonTrace();
            break;
        case lldb::eStopReasonBreakpoint:
            pEventType = "eStopReasonBreakpoint";
            bOk = HandleProcessEventStopReasonBreakpoint();
            break;
        case lldb::eStopReasonWatchpoint:
            pEventType = "eStopReasonWatchpoint";
            break;
        case lldb::eStopReasonSignal:
            pEventType = "eStopReasonSignal";
            bOk = HandleProcessEventStopSignal(vwrbShouldBrk);
            break;
        case lldb::eStopReasonException:
            pEventType = "eStopReasonException";
            bOk = HandleProcessEventStopException();
            break;
        case lldb::eStopReasonExec:
            pEventType = "eStopReasonExec";
            break;
        case lldb::eStopReasonPlanComplete:
            pEventType = "eStopReasonPlanComplete";
            bOk = HandleProcessEventStopReasonTrace();
            break;
        case lldb::eStopReasonThreadExiting:
            pEventType = "eStopReasonThreadExiting";
            break;
        case lldb::eStopReasonInstrumentation:
            pEventType = "eStopReasonInstrumentation";
            break;
    }

    // ToDo: Remove when finished coding application
    m_pLog->WriteLog(CMIUtilString::Format("##### An SB Process event stop state occurred: %s", pEventType));

    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Asynchronous event handler for LLDB Process stop signal.
// Type:    Method.
// Args:    vwrbShouldBrk   - (W) True = Yes break, false = do not.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleProcessEventStopSignal(bool &vwrbShouldBrk)
{
    bool bOk = MIstatus::success;

    InitializeSignals ();
    lldb::SBProcess sbProcess = CMICmnLLDBDebugSessionInfo::Instance().GetProcess();
    const MIuint64 nStopReason = sbProcess.GetSelectedThread().GetStopReasonDataAtIndex(0);
    if (nStopReason == m_SIGINT || nStopReason == m_SIGSTOP)
    {
        // MI print "*stopped,reason=\"signal-received\",signal-name=\"SIGNINT\",signal-meaning=\"Interrupt\",frame={%s}"
        const CMICmnMIValueConst miValueConst("signal-received");
        const CMICmnMIValueResult miValueResult("reason", miValueConst);
        CMICmnMIOutOfBandRecord miOutOfBandRecord(CMICmnMIOutOfBandRecord::eOutOfBand_Stopped, miValueResult);
        const CMICmnMIValueConst miValueConst2("SIGINT");
        const CMICmnMIValueResult miValueResult2("signal-name", miValueConst2);
        bOk = miOutOfBandRecord.Add(miValueResult2);
        const CMICmnMIValueConst miValueConst3("Interrupt");
        const CMICmnMIValueResult miValueResult3("signal-meaning", miValueConst3);
        bOk = bOk && miOutOfBandRecord.Add(miValueResult3);
        CMICmnMIValueTuple miValueTuple;
        bOk = bOk && MiHelpGetCurrentThreadFrame(miValueTuple);
        const CMICmnMIValueResult miValueResult5("frame", miValueTuple);
        bOk = bOk && miOutOfBandRecord.Add(miValueResult5);
        const CMIUtilString strThreadId(CMIUtilString::Format("%d", sbProcess.GetSelectedThread().GetIndexID()));
        const CMICmnMIValueConst miValueConst6(strThreadId);
        const CMICmnMIValueResult miValueResult6("thread-id", miValueConst6);
        bOk = bOk && miOutOfBandRecord.Add(miValueResult6);
        const CMICmnMIValueConst miValueConst7("all");
        const CMICmnMIValueResult miValueResult7("stopped-threads", miValueConst7);
        bOk = bOk && miOutOfBandRecord.Add(miValueResult7);
        bOk = bOk && MiOutOfBandRecordToStdout(miOutOfBandRecord);
        bOk = bOk && TextToStdout("(gdb)");
    }
    else if (nStopReason == m_SIGSEGV)
    {
        // MI print "*stopped,reason=\"signal-received\",signal-name=\"SIGSEGV\",signal-meaning=\"Segmentation
        // fault\",thread-id=\"%d\",frame={%s}"
        const CMICmnMIValueConst miValueConst("signal-received");
        const CMICmnMIValueResult miValueResult("reason", miValueConst);
        CMICmnMIOutOfBandRecord miOutOfBandRecord(CMICmnMIOutOfBandRecord::eOutOfBand_Stopped, miValueResult);
        const CMICmnMIValueConst miValueConst2("SIGSEGV");
        const CMICmnMIValueResult miValueResult2("signal-name", miValueConst2);
        bOk = miOutOfBandRecord.Add(miValueResult2);
        const CMICmnMIValueConst miValueConst3("Segmentation fault");
        const CMICmnMIValueResult miValueResult3("signal-meaning", miValueConst3);
        bOk = bOk && miOutOfBandRecord.Add(miValueResult3);
        const CMIUtilString strThreadId(CMIUtilString::Format("%d", sbProcess.GetSelectedThread().GetIndexID()));
        const CMICmnMIValueConst miValueConst4(strThreadId);
        const CMICmnMIValueResult miValueResult4("thread-id", miValueConst4);
        bOk = bOk && miOutOfBandRecord.Add(miValueResult4);
        CMICmnMIValueTuple miValueTuple;
        bOk = bOk && MiHelpGetCurrentThreadFrame(miValueTuple);
        const CMICmnMIValueResult miValueResult5("frame", miValueTuple);
        bOk = bOk && miOutOfBandRecord.Add(miValueResult5);
        bOk = bOk && MiOutOfBandRecordToStdout(miOutOfBandRecord);
        // Note no "(gdb)" output here
    }
    else if (nStopReason == m_SIGTRAP)
    {
        lldb::SBThread thread = sbProcess.GetSelectedThread();
        const MIuint nFrames = thread.GetNumFrames();
        if (nFrames > 0)
        {
            lldb::SBFrame frame = thread.GetFrameAtIndex(0);
            const char *pFnName = frame.GetFunctionName();
            if (pFnName != nullptr)
            {
                const CMIUtilString fnName = CMIUtilString(pFnName);
                static const CMIUtilString threadCloneFn = CMIUtilString("__pthread_clone");

                if (CMIUtilString::Compare(threadCloneFn, fnName))
                {
                    if (sbProcess.IsValid())
                    {
                        sbProcess.Continue();
                        vwrbShouldBrk = true;
                    }
                }
            }
        }
    }
    else
    {
        // MI print "*stopped,reason=\"signal-received\",signal=\"%lld\",thread-id=\"%d\",stopped-threads=\"all\""
        const CMICmnMIValueConst miValueConst("signal-received");
        const CMICmnMIValueResult miValueResult("reason", miValueConst);
        CMICmnMIOutOfBandRecord miOutOfBandRecord(CMICmnMIOutOfBandRecord::eOutOfBand_Stopped, miValueResult);
        const CMIUtilString strReason(CMIUtilString::Format("%lld", nStopReason));
        const CMICmnMIValueConst miValueConst2(strReason);
        const CMICmnMIValueResult miValueResult2("signal", miValueConst2);
        bOk = miOutOfBandRecord.Add(miValueResult2);
        const CMIUtilString strThreadId(CMIUtilString::Format("%d", sbProcess.GetSelectedThread().GetIndexID()));
        const CMICmnMIValueConst miValueConst3(strThreadId);
        const CMICmnMIValueResult miValueResult3("thread-id", miValueConst3);
        bOk = bOk && miOutOfBandRecord.Add(miValueResult3);
        const CMICmnMIValueConst miValueConst4("all");
        const CMICmnMIValueResult miValueResult4("stopped-threads", miValueConst4);
        bOk = bOk && miOutOfBandRecord.Add(miValueResult4);
        bOk = bOk && MiOutOfBandRecordToStdout(miOutOfBandRecord);
        bOk = bOk && TextToStdout("(gdb)");
    }
    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Asynchronous event handler for LLDB Process stop exception.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functional succeeded.
//          MIstatus::failure - Functional failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleProcessEventStopException(void)
{
    const lldb::SBProcess sbProcess = CMICmnLLDBDebugSessionInfo::Instance().GetProcess();
    lldb::SBThread sbThread = sbProcess.GetSelectedThread();
    const size_t nStopDescriptionLen = sbThread.GetStopDescription(nullptr, 0);
    std::unique_ptr<char[]> apStopDescription(new char[nStopDescriptionLen]);
    sbThread.GetStopDescription(apStopDescription.get(), nStopDescriptionLen);

    // MI print "*stopped,reason=\"exception-received\",exception=\"%s\",thread-id=\"%d\",stopped-threads=\"all\""
    const CMICmnMIValueConst miValueConst("exception-received");
    const CMICmnMIValueResult miValueResult("reason", miValueConst);
    CMICmnMIOutOfBandRecord miOutOfBandRecord(CMICmnMIOutOfBandRecord::eOutOfBand_Stopped, miValueResult);
    const CMIUtilString strReason(apStopDescription.get());
    const CMICmnMIValueConst miValueConst2(strReason);
    const CMICmnMIValueResult miValueResult2("exception", miValueConst2);
    bool bOk = miOutOfBandRecord.Add(miValueResult2);
    const CMIUtilString strThreadId(CMIUtilString::Format("%d", sbThread.GetIndexID()));
    const CMICmnMIValueConst miValueConst3(strThreadId);
    const CMICmnMIValueResult miValueResult3("thread-id", miValueConst3);
    bOk = bOk && miOutOfBandRecord.Add(miValueResult3);
    const CMICmnMIValueConst miValueConst4("all");
    const CMICmnMIValueResult miValueResult4("stopped-threads", miValueConst4);
    bOk = bOk && miOutOfBandRecord.Add(miValueResult4);
    bOk = bOk && MiOutOfBandRecordToStdout(miOutOfBandRecord);
    bOk = bOk && TextToStdout("(gdb)");

    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Form partial MI response in a MI value tuple object.
// Type:    Method.
// Args:    vwrMiValueTuple   - (W) MI value tuple object.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::MiHelpGetCurrentThreadFrame(CMICmnMIValueTuple &vwrMiValueTuple)
{
    CMIUtilString strThreadFrame;
    lldb::SBProcess sbProcess = CMICmnLLDBDebugSessionInfo::Instance().GetProcess();
    lldb::SBThread thread = sbProcess.GetSelectedThread();
    const MIuint nFrame = thread.GetNumFrames();
    if (nFrame == 0)
    {
        // MI print "addr=\"??\",func=\"??\",file=\"??\",fullname=\"??\",line=\"??\""
        const CMICmnMIValueConst miValueConst("??");
        const CMICmnMIValueResult miValueResult("addr", miValueConst);
        CMICmnMIValueTuple miValueTuple(miValueResult);
        const CMICmnMIValueResult miValueResult2("func", miValueConst);
        miValueTuple.Add(miValueResult2);
        const CMICmnMIValueResult miValueResult4("file", miValueConst);
        miValueTuple.Add(miValueResult4);
        const CMICmnMIValueResult miValueResult5("fullname", miValueConst);
        miValueTuple.Add(miValueResult5);
        const CMICmnMIValueResult miValueResult6("line", miValueConst);
        miValueTuple.Add(miValueResult6);

        vwrMiValueTuple = miValueTuple;

        return MIstatus::success;
    }

    CMICmnMIValueTuple miValueTuple;
    if (!CMICmnLLDBDebugSessionInfo::Instance().MIResponseFormFrameInfo(thread, 0, miValueTuple))
    {
        SetErrorDescription(CMIUtilString::Format(MIRSRC(IDS_LLDBOUTOFBAND_ERR_FORM_MI_RESPONSE), "MiHelpGetCurrentThreadFrame()"));
        return MIstatus::failure;
    }

    vwrMiValueTuple = miValueTuple;

    return MIstatus::success;
}

//++ ------------------------------------------------------------------------------------
// Details: Asynchronous event handler for LLDB Process stop reason breakpoint.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleProcessEventStopReasonBreakpoint(void)
{
    // CODETAG_DEBUG_SESSION_RUNNING_PROG_RECEIVED_SIGINT_PAUSE_PROGRAM
    if (!CMIDriver::Instance().SetDriverStateRunningNotDebugging())
    {
        const CMIUtilString &rErrMsg(CMIDriver::Instance().GetErrorDescription());
        SetErrorDescription(CMIUtilString::Format(MIRSRC(IDS_LLDBOUTOFBAND_ERR_SETNEWDRIVERSTATE),
                                                  "HandleProcessEventStopReasonBreakpoint()", rErrMsg.c_str()));
        return MIstatus::failure;
    }

    lldb::SBProcess sbProcess = CMICmnLLDBDebugSessionInfo::Instance().GetProcess();
    const MIuint64 brkPtId = sbProcess.GetSelectedThread().GetStopReasonDataAtIndex(0);
    lldb::SBBreakpoint brkPt = CMICmnLLDBDebugSessionInfo::Instance().GetTarget().GetBreakpointAtIndex((MIuint)brkPtId);

    return MiStoppedAtBreakPoint(brkPtId, brkPt);
}

//++ ------------------------------------------------------------------------------------
// Details: Form the MI Out-of-band response for stopped reason on hitting a break point.
// Type:    Method.
// Args:    vBrkPtId    - (R) The LLDB break point's ID
//          vBrkPt      - (R) THe LLDB break point object.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::MiStoppedAtBreakPoint(const MIuint64 vBrkPtId, const lldb::SBBreakpoint &vBrkPt)
{
    bool bOk = MIstatus::success;

    lldb::SBProcess sbProcess = CMICmnLLDBDebugSessionInfo::Instance().GetProcess();
    lldb::SBThread thread = sbProcess.GetSelectedThread();
    const MIuint nFrame = thread.GetNumFrames();
    if (nFrame == 0)
    {
        // MI print "*stopped,reason=\"breakpoint-hit\",disp=\"del\",bkptno=\"%d\",frame={},thread-id=\"%d\",stopped-threads=\"all\""
        const CMICmnMIValueConst miValueConst("breakpoint-hit");
        const CMICmnMIValueResult miValueResult("reason", miValueConst);
        CMICmnMIOutOfBandRecord miOutOfBandRecord(CMICmnMIOutOfBandRecord::eOutOfBand_Stopped, miValueResult);
        const CMICmnMIValueConst miValueConst2("del");
        const CMICmnMIValueResult miValueResult2("disp", miValueConst2);
        bOk = miOutOfBandRecord.Add(miValueResult2);
        const CMIUtilString strBkp(CMIUtilString::Format("%d", vBrkPtId));
        const CMICmnMIValueConst miValueConst3(strBkp);
        CMICmnMIValueResult miValueResult3("bkptno", miValueConst3);
        bOk = bOk && miOutOfBandRecord.Add(miValueResult3);
        const CMICmnMIValueConst miValueConst4("{}");
        const CMICmnMIValueResult miValueResult4("frame", miValueConst4);
        bOk = bOk && miOutOfBandRecord.Add(miValueResult4);
        const CMIUtilString strThreadId(CMIUtilString::Format("%d", vBrkPt.GetThreadIndex()));
        const CMICmnMIValueConst miValueConst5(strThreadId);
        const CMICmnMIValueResult miValueResult5("thread-id", miValueConst5);
        bOk = bOk && miOutOfBandRecord.Add(miValueResult5);
        const CMICmnMIValueConst miValueConst6("all");
        const CMICmnMIValueResult miValueResult6("stopped-threads", miValueConst6);
        bOk = bOk && miOutOfBandRecord.Add(miValueResult6);
        bOk = bOk && MiOutOfBandRecordToStdout(miOutOfBandRecord);
        bOk = bOk && TextToStdout("(gdb)");
        return bOk;
    }

    CMICmnLLDBDebugSessionInfo &rSession = CMICmnLLDBDebugSessionInfo::Instance();

    lldb::SBFrame frame = thread.GetFrameAtIndex(0);
    lldb::addr_t pc = 0;
    CMIUtilString fnName;
    CMIUtilString fileName;
    CMIUtilString path;
    MIuint nLine = 0;
    if (!rSession.GetFrameInfo(frame, pc, fnName, fileName, path, nLine))
    {
        SetErrorDescription(CMIUtilString::Format(MIRSRC(IDS_LLDBOUTOFBAND_ERR_FRAME_INFO_GET), "MiStoppedAtBreakPoint()"));
        return MIstatus::failure;
    }

    // MI print
    // "*stopped,reason=\"breakpoint-hit\",disp=\"del\",bkptno=\"%d\",frame={addr=\"0x%016" PRIx64 "\",func=\"%s\",args=[],file=\"%s\",fullname=\"%s\",line=\"%d\"},thread-id=\"%d\",stopped-threads=\"all\""
    const CMICmnMIValueConst miValueConst("breakpoint-hit");
    const CMICmnMIValueResult miValueResult("reason", miValueConst);
    CMICmnMIOutOfBandRecord miOutOfBandRecord(CMICmnMIOutOfBandRecord::eOutOfBand_Stopped, miValueResult);
    const CMICmnMIValueConst miValueConstA("del");
    const CMICmnMIValueResult miValueResultA("disp", miValueConstA);
    bOk = miOutOfBandRecord.Add(miValueResultA);
    const CMIUtilString strBkp(CMIUtilString::Format("%d", vBrkPtId));
    const CMICmnMIValueConst miValueConstB(strBkp);
    CMICmnMIValueResult miValueResultB("bkptno", miValueConstB);
    bOk = bOk && miOutOfBandRecord.Add(miValueResultB);

    // frame={addr=\"0x%016" PRIx64 "\",func=\"%s\",args=[],file=\"%s\",fullname=\"%s\",line=\"%d\"}
    if (bOk)
    {
        CMICmnMIValueList miValueList(true);
        const MIuint maskVarTypes = CMICmnLLDBDebugSessionInfo::eVariableType_Arguments;
        bOk = rSession.MIResponseFormVariableInfo2(frame, maskVarTypes, CMICmnLLDBDebugSessionInfo::eVariableInfoFormat_AllValues, miValueList);

        CMICmnMIValueTuple miValueTuple;
        bOk = bOk && rSession.MIResponseFormFrameInfo2(pc, miValueList.GetString(), fnName, fileName, path, nLine, miValueTuple);
        const CMICmnMIValueResult miValueResult8("frame", miValueTuple);
        bOk = bOk && miOutOfBandRecord.Add(miValueResult8);
    }

    // Add to MI thread-id=\"%d\",stopped-threads=\"all\"
    if (bOk)
    {
        const CMIUtilString strThreadId(CMIUtilString::Format("%d", thread.GetIndexID()));
        const CMICmnMIValueConst miValueConst8(strThreadId);
        const CMICmnMIValueResult miValueResult8("thread-id", miValueConst8);
        bOk = miOutOfBandRecord.Add(miValueResult8);
    }
    if (bOk)
    {
        const CMICmnMIValueConst miValueConst9("all");
        const CMICmnMIValueResult miValueResult9("stopped-threads", miValueConst9);
        bOk = miOutOfBandRecord.Add(miValueResult9);
        bOk = MiOutOfBandRecordToStdout(miOutOfBandRecord);
        bOk = bOk && TextToStdout("(gdb)");
    }

    return MIstatus::success;
}

//++ ------------------------------------------------------------------------------------
// Details: Asynchronous event handler for LLDB Process stop reason trace.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleProcessEventStopReasonTrace(void)
{
    bool bOk = true;
    lldb::SBProcess sbProcess = CMICmnLLDBDebugSessionInfo::Instance().GetProcess();
    lldb::SBThread thread = sbProcess.GetSelectedThread();
    const MIuint nFrame = thread.GetNumFrames();
    if (nFrame == 0)
    {
        // MI print "*stopped,reason=\"trace\",stopped-threads=\"all\""
        const CMICmnMIValueConst miValueConst("trace");
        const CMICmnMIValueResult miValueResult("reason", miValueConst);
        CMICmnMIOutOfBandRecord miOutOfBandRecord(CMICmnMIOutOfBandRecord::eOutOfBand_Stopped, miValueResult);
        const CMICmnMIValueConst miValueConst2("all");
        const CMICmnMIValueResult miValueResult2("stopped-threads", miValueConst2);
        bOk = miOutOfBandRecord.Add(miValueResult2);
        bOk = MiOutOfBandRecordToStdout(miOutOfBandRecord);
        bOk = bOk && TextToStdout("(gdb)");
        return bOk;
    }

    CMICmnLLDBDebugSessionInfo &rSession = CMICmnLLDBDebugSessionInfo::Instance();

    // MI print
    // "*stopped,reason=\"end-stepping-range\",frame={addr=\"0x%016" PRIx64 "\",func=\"%s\",args=[\"%s\"],file=\"%s\",fullname=\"%s\",line=\"%d\"},thread-id=\"%d\",stopped-threads=\"all\""
    lldb::SBFrame frame = thread.GetFrameAtIndex(0);
    lldb::addr_t pc = 0;
    CMIUtilString fnName;
    CMIUtilString fileName;
    CMIUtilString path;
    MIuint nLine = 0;
    if (!rSession.GetFrameInfo(frame, pc, fnName, fileName, path, nLine))
    {
        SetErrorDescription(CMIUtilString::Format(MIRSRC(IDS_LLDBOUTOFBAND_ERR_FRAME_INFO_GET), "HandleProcessEventStopReasonTrace()"));
        return MIstatus::failure;
    }

    // Function args
    CMICmnMIValueList miValueList(true);
    const MIuint maskVarTypes = CMICmnLLDBDebugSessionInfo::eVariableType_Arguments;
    if (!rSession.MIResponseFormVariableInfo2(frame, maskVarTypes, CMICmnLLDBDebugSessionInfo::eVariableInfoFormat_AllValues, miValueList))
        return MIstatus::failure;
    CMICmnMIValueTuple miValueTuple;
    if (!rSession.MIResponseFormFrameInfo2(pc, miValueList.GetString(), fnName, fileName, path, nLine, miValueTuple))
        return MIstatus::failure;

    const CMICmnMIValueConst miValueConst("end-stepping-range");
    const CMICmnMIValueResult miValueResult("reason", miValueConst);
    CMICmnMIOutOfBandRecord miOutOfBandRecord(CMICmnMIOutOfBandRecord::eOutOfBand_Stopped, miValueResult);
    const CMICmnMIValueResult miValueResult2("frame", miValueTuple);
    bOk = miOutOfBandRecord.Add(miValueResult2);

    // Add to MI thread-id=\"%d\",stopped-threads=\"all\"
    if (bOk)
    {
        const CMIUtilString strThreadId(CMIUtilString::Format("%d", thread.GetIndexID()));
        const CMICmnMIValueConst miValueConst8(strThreadId);
        const CMICmnMIValueResult miValueResult8("thread-id", miValueConst8);
        bOk = miOutOfBandRecord.Add(miValueResult8);
    }
    if (bOk)
    {
        const CMICmnMIValueConst miValueConst9("all");
        const CMICmnMIValueResult miValueResult9("stopped-threads", miValueConst9);
        bOk = miOutOfBandRecord.Add(miValueResult9);
        bOk = MiOutOfBandRecordToStdout(miOutOfBandRecord);
        bOk = bOk && TextToStdout("(gdb)");
    }

    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Asynchronous function update selected thread.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::UpdateSelectedThread(void)
{
    lldb::SBProcess process = CMICmnLLDBDebugSessionInfo::Instance().GetDebugger().GetSelectedTarget().GetProcess();
    if (!process.IsValid())
        return MIstatus::success;

    lldb::SBThread currentThread = process.GetSelectedThread();
    lldb::SBThread thread;
    const lldb::StopReason eCurrentThreadStoppedReason = currentThread.GetStopReason();
    if (!currentThread.IsValid() || (eCurrentThreadStoppedReason == lldb::eStopReasonInvalid) ||
        (eCurrentThreadStoppedReason == lldb::eStopReasonNone))
    {
        // Prefer a thread that has just completed its plan over another thread as current thread
        lldb::SBThread planThread;
        lldb::SBThread otherThread;
        const size_t nThread = process.GetNumThreads();
        for (MIuint i = 0; i < nThread; i++)
        {
            //  GetThreadAtIndex() uses a base 0 index
            //  GetThreadByIndexID() uses a base 1 index
            thread = process.GetThreadAtIndex(i);
            const lldb::StopReason eThreadStopReason = thread.GetStopReason();
            switch (eThreadStopReason)
            {
                case lldb::eStopReasonTrace:
                case lldb::eStopReasonBreakpoint:
                case lldb::eStopReasonWatchpoint:
                case lldb::eStopReasonSignal:
                case lldb::eStopReasonException:
                    if (!otherThread.IsValid())
                        otherThread = thread;
                    break;
                case lldb::eStopReasonPlanComplete:
                    if (!planThread.IsValid())
                        planThread = thread;
                    break;
                case lldb::eStopReasonInvalid:
                case lldb::eStopReasonNone:
                default:
                    break;
            }
        }
        if (planThread.IsValid())
            process.SetSelectedThread(planThread);
        else if (otherThread.IsValid())
            process.SetSelectedThread(otherThread);
        else
        {
            if (currentThread.IsValid())
                thread = currentThread;
            else
                thread = process.GetThreadAtIndex(0);

            if (thread.IsValid())
                process.SetSelectedThread(thread);
        }
    } // if( !currentThread.IsValid() || (eCurrentThreadStoppedReason == lldb::eStopReasonInvalid) || (eCurrentThreadStoppedReason ==
      // lldb::eStopReasonNone) )

    return MIstatus::success;
}

//++ ------------------------------------------------------------------------------------
// Details: Print to stdout "*running,thread-id=\"all\"", "(gdb)".
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleProcessEventStateRunning(void)
{
    CMICmnMIValueConst miValueConst("all");
    CMICmnMIValueResult miValueResult("thread-id", miValueConst);
    CMICmnMIOutOfBandRecord miOutOfBandRecord(CMICmnMIOutOfBandRecord::eOutOfBand_Running, miValueResult);
    bool bOk = MiOutOfBandRecordToStdout(miOutOfBandRecord);
    bOk = bOk && TextToStdout("(gdb)");

    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Print to stdout "=thread-exited,id=\"%ld\",group-id=\"i1\"",
//                          "=thread-group-exited,id=\"i1\",exit-code=\"0\""),
//                          "*stopped,reason=\"exited-normally\"",
//                          "(gdb)"
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::HandleProcessEventStateExited(void)
{
    const CMIUtilString strId(CMIUtilString::Format("%ld", 1));
    CMICmnMIValueConst miValueConst(strId);
    CMICmnMIValueResult miValueResult("id", miValueConst);
    CMICmnMIOutOfBandRecord miOutOfBandRecord(CMICmnMIOutOfBandRecord::eOutOfBand_ThreadExited, miValueResult);
    CMICmnMIValueConst miValueConst2("i1");
    CMICmnMIValueResult miValueResult2("group-id", miValueConst2);
    bool bOk = miOutOfBandRecord.Add(miValueResult2);
    bOk = bOk && MiOutOfBandRecordToStdout(miOutOfBandRecord);
    if (bOk)
    {
        CMICmnMIValueConst miValueConst3("i1");
        CMICmnMIValueResult miValueResult3("id", miValueConst3);
        CMICmnMIOutOfBandRecord miOutOfBandRecord2(CMICmnMIOutOfBandRecord::eOutOfBand_ThreadGroupExited, miValueResult3);
        CMICmnMIValueConst miValueConst2("0");
        CMICmnMIValueResult miValueResult2("exit-code", miValueConst2);
        bOk = miOutOfBandRecord2.Add(miValueResult2);
        bOk = bOk && MiOutOfBandRecordToStdout(miOutOfBandRecord2);
    }
    if (bOk)
    {
        CMICmnMIValueConst miValueConst4("exited-normally");
        CMICmnMIValueResult miValueResult4("reason", miValueConst4);
        CMICmnMIOutOfBandRecord miOutOfBandRecord3(CMICmnMIOutOfBandRecord::eOutOfBand_Stopped, miValueResult4);
        bOk = MiOutOfBandRecordToStdout(miOutOfBandRecord3);
    }
    bOk = bOk && TextToStdout("(gdb)");

    return bOk;
}

//++ ------------------------------------------------------------------------------------
// Details: Drain all stdout so we don't see any output come after we print our prompts.
//          The process has stuff waiting for stdout; get it and write it out to the
//          appropriate place.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::GetProcessStdout(void)
{
    CMIUtilString text;
    std::unique_ptr<char[]> apStdoutBuffer(new char[1024]);
    lldb::SBProcess process = CMICmnLLDBDebugSessionInfo::Instance().GetDebugger().GetSelectedTarget().GetProcess();
    while (1)
    {
        const size_t nBytes = process.GetSTDOUT(apStdoutBuffer.get(), 1024);
        if (nBytes == 0)
            break;

        text.append(apStdoutBuffer.get(), nBytes);
    }

    if (text.empty())
        return MIstatus::success;

    const bool bEscapeQuotes(true);
    const CMIUtilString t(CMIUtilString::Format("~\"%s\"", text.Escape(bEscapeQuotes).c_str()));
    return TextToStdout(t);
}

//++ ------------------------------------------------------------------------------------
// Details: Drain all stderr so we don't see any output come after we print our prompts.
//          The process has stuff waiting for stderr; get it and write it out to the
//          appropriate place.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::GetProcessStderr(void)
{
    CMIUtilString text;
    std::unique_ptr<char[]> apStderrBuffer(new char[1024]);
    lldb::SBProcess process = CMICmnLLDBDebugSessionInfo::Instance().GetDebugger().GetSelectedTarget().GetProcess();
    while (1)
    {
        const size_t nBytes = process.GetSTDERR(apStderrBuffer.get(), 1024);
        if (nBytes == 0)
            break;

        text.append(apStderrBuffer.get(), nBytes);
    }

    if (text.empty())
        return MIstatus::success;

    const bool bEscapeQuotes(true);
    const CMIUtilString t(CMIUtilString::Format("~\"%s\"", text.Escape(bEscapeQuotes).c_str()));
    return TextToStdout(t);
}

//++ ------------------------------------------------------------------------------------
// Details: Asynchronous event function check for state changes.
// Type:    Method.
// Args:    None.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::ChkForStateChanges(void)
{
    CMICmnLLDBDebugSessionInfo &rSessionInfo(CMICmnLLDBDebugSessionInfo::Instance());
    lldb::SBProcess sbProcess = rSessionInfo.GetProcess();
    if (!sbProcess.IsValid())
        return MIstatus::success;

    // Check for created threads
    const MIuint nThread = sbProcess.GetNumThreads();
    for (MIuint i = 0; i < nThread; i++)
    {
        //  GetThreadAtIndex() uses a base 0 index
        //  GetThreadByIndexID() uses a base 1 index
        lldb::SBThread thread = sbProcess.GetThreadAtIndex(i);
        if (!thread.IsValid())
            continue;

        const MIuint threadIndexID = thread.GetIndexID();
        const bool bFound = std::find(rSessionInfo.m_vecActiveThreadId.cbegin(), rSessionInfo.m_vecActiveThreadId.cend(), threadIndexID) != rSessionInfo.m_vecActiveThreadId.end();
        if (!bFound)
        {
            rSessionInfo.m_vecActiveThreadId.push_back(threadIndexID);

            // Form MI "=thread-created,id=\"%d\",group-id=\"i1\""
            const CMIUtilString strValue(CMIUtilString::Format("%d", threadIndexID));
            const CMICmnMIValueConst miValueConst(strValue);
            const CMICmnMIValueResult miValueResult("id", miValueConst);
            CMICmnMIOutOfBandRecord miOutOfBand(CMICmnMIOutOfBandRecord::eOutOfBand_ThreadCreated, miValueResult);
            const CMICmnMIValueConst miValueConst2("i1");
            const CMICmnMIValueResult miValueResult2("group-id", miValueConst2);
            bool bOk = miOutOfBand.Add(miValueResult2);
            bOk = bOk && MiOutOfBandRecordToStdout(miOutOfBand);
            if (!bOk)
                return MIstatus::failure;
        }
    }

    lldb::SBThread currentThread = sbProcess.GetSelectedThread();
    if (currentThread.IsValid())
    {
        const MIuint currentThreadIndexID = currentThread.GetIndexID();
        if (rSessionInfo.m_currentSelectedThread != currentThreadIndexID)
        {
            rSessionInfo.m_currentSelectedThread = currentThreadIndexID;

            // Form MI "=thread-selected,id=\"%d\""
            const CMIUtilString strValue(CMIUtilString::Format("%d", currentThreadIndexID));
            const CMICmnMIValueConst miValueConst(strValue);
            const CMICmnMIValueResult miValueResult("id", miValueConst);
            CMICmnMIOutOfBandRecord miOutOfBand(CMICmnMIOutOfBandRecord::eOutOfBand_ThreadSelected, miValueResult);
            if (!MiOutOfBandRecordToStdout(miOutOfBand))
                return MIstatus::failure;
        }
    }

    // Check for invalid (removed) threads
    CMICmnLLDBDebugSessionInfo::VecActiveThreadId_t::iterator it = rSessionInfo.m_vecActiveThreadId.begin();
    while (it != rSessionInfo.m_vecActiveThreadId.end())
    {
        const MIuint threadIndexID = *it;
        lldb::SBThread thread = sbProcess.GetThreadByIndexID(threadIndexID);
        if (!thread.IsValid())
        {
            // Form MI "=thread-exited,id=\"%ld\",group-id=\"i1\""
            const CMIUtilString strValue(CMIUtilString::Format("%ld", threadIndexID));
            const CMICmnMIValueConst miValueConst(strValue);
            const CMICmnMIValueResult miValueResult("id", miValueConst);
            CMICmnMIOutOfBandRecord miOutOfBand(CMICmnMIOutOfBandRecord::eOutOfBand_ThreadExited, miValueResult);
            const CMICmnMIValueConst miValueConst2("i1");
            const CMICmnMIValueResult miValueResult2("group-id", miValueConst2);
            bool bOk = miOutOfBand.Add(miValueResult2);
            bOk = bOk && MiOutOfBandRecordToStdout(miOutOfBand);
            if (!bOk)
                return MIstatus::failure;

            // Remove current thread from cache and get next
            it = rSessionInfo.m_vecActiveThreadId.erase(it);
        }
        else
            // Next
            ++it;
    }

    return TextToStdout("(gdb)");
}

//++ ------------------------------------------------------------------------------------
// Details: Take a fully formed MI result record and send to the stdout stream.
//          Also output to the MI Log file.
// Type:    Method.
// Args:    vrMiResultRecord  - (R) MI result record object.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::MiResultRecordToStdout(const CMICmnMIResultRecord &vrMiResultRecord)
{
    return TextToStdout(vrMiResultRecord.GetString());
}

//++ ------------------------------------------------------------------------------------
// Details: Take a fully formed MI Out-of-band record and send to the stdout stream.
//          Also output to the MI Log file.
// Type:    Method.
// Args:    vrMiOutOfBandRecord - (R) MI Out-of-band record object.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::MiOutOfBandRecordToStdout(const CMICmnMIOutOfBandRecord &vrMiOutOfBandRecord)
{
    return TextToStdout(vrMiOutOfBandRecord.GetString());
}

//++ ------------------------------------------------------------------------------------
// Details: Take a text data and send to the stdout stream. Also output to the MI Log
//          file.
// Type:    Method.
// Args:    vrTxt   - (R) Text.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::TextToStdout(const CMIUtilString &vrTxt)
{
    return CMICmnStreamStdout::TextToStdout(vrTxt);
}

//++ ------------------------------------------------------------------------------------
// Details: Take a text data and send to the stderr stream. Also output to the MI Log
//          file.
// Type:    Method.
// Args:    vrTxt   - (R) Text.
// Return:  MIstatus::success - Functionality succeeded.
//          MIstatus::failure - Functionality failed.
// Throws:  None.
//--
bool
CMICmnLLDBDebuggerHandleEvents::TextToStderr(const CMIUtilString &vrTxt)
{
    return CMICmnStreamStderr::TextToStderr(vrTxt);
}

//++ ------------------------------------------------------------------------------------
// Details: Initialize the member variables with the signal values in this process
//          file.
// Type:    Method.
// Args:    None
// Return:  Noen
// Throws:  None.
//--
void
CMICmnLLDBDebuggerHandleEvents::InitializeSignals()
{
    if (!m_bSignalsInitialized)
    {
        lldb::SBProcess sbProcess = CMICmnLLDBDebugSessionInfo::Instance().GetProcess();
        if (sbProcess.IsValid())
        {
            lldb::SBUnixSignals unix_signals = sbProcess.GetUnixSignals();
            m_SIGINT = unix_signals.GetSignalNumberFromName("SIGINT");
            m_SIGSTOP = unix_signals.GetSignalNumberFromName("SIGSTOP");
            m_SIGSEGV = unix_signals.GetSignalNumberFromName("SIGSEGV");
            m_SIGTRAP = unix_signals.GetSignalNumberFromName("SIGTRAP");
            m_bSignalsInitialized = true;
        }
    }
}
