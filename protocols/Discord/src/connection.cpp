/*
Copyright © 2016-19 Miranda NG team

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "stdafx.h"

void CDiscordProto::ExecuteRequest(AsyncHttpRequest *pReq)
{
	CMStringA str;

	pReq->szUrl = pReq->m_szUrl.GetBuffer();
	if (!pReq->m_szParam.IsEmpty()) {
		if (pReq->requestType == REQUEST_GET) {
			str.Format("%s?%s", pReq->m_szUrl.c_str(), pReq->m_szParam.c_str());
			pReq->szUrl = str.GetBuffer();
		}
		else {
			pReq->pData = mir_strdup(pReq->m_szParam);
			pReq->dataLength = pReq->m_szParam.GetLength();
		}
	}

	if (pReq->m_bMainSite) {
		pReq->flags |= NLHRF_PERSISTENT;
		pReq->nlc = m_hAPIConnection;
		if (m_szAccessCookie)
			pReq->AddHeader("Cookie", m_szAccessCookie);
	}

	debugLogA("Executing request #%d:\n%s", pReq->m_iReqNum, pReq->szUrl);
	NETLIBHTTPREQUEST *reply = Netlib_HttpTransaction(m_hNetlibUser, pReq);
	if (reply != nullptr) {
		if (pReq->m_pFunc != nullptr)
			(this->*(pReq->m_pFunc))(reply, pReq);

		if (pReq->m_bMainSite)
			m_hAPIConnection = reply->nlc;

		Netlib_FreeHttpRequest(reply);
	}
	else {
		debugLogA("Request %d failed", pReq->m_iReqNum);

		if (pReq->m_bMainSite) {
			if (IsStatusConnecting(m_iStatus))
				ConnectionFailed(LOGINERR_NONETWORK);
			m_hAPIConnection = nullptr;
		}
	}
	delete pReq;
}

void CDiscordProto::OnLoggedIn()
{
	debugLogA("CDiscordProto::OnLoggedIn");
	m_bOnline = true;
	SetServerStatus(m_iDesiredStatus);

	if (m_szGateway.IsEmpty())
		Push(new AsyncHttpRequest(this, REQUEST_GET, "/gateway", &CDiscordProto::OnReceiveGateway));
	else
		ForkThread(&CDiscordProto::GatewayThread, nullptr);
}

static void __stdcall sttKillTimer(void *param)
{
	KillTimer(g_hwndHeartbeat, (UINT_PTR)param);
}

void CDiscordProto::OnLoggedOut()
{
	debugLogA("CDiscordProto::OnLoggedOut");
	m_bOnline = false;
	m_bTerminated = true;
	m_iGatewaySeq = 0;

	CallFunctionAsync(sttKillTimer, this);

	ProtoBroadcastAck(0, ACKTYPE_STATUS, ACKRESULT_SUCCESS, (HANDLE)m_iStatus, ID_STATUS_OFFLINE);
	m_iStatus = m_iDesiredStatus = ID_STATUS_OFFLINE;

	setAllContactStatuses(ID_STATUS_OFFLINE, false);
}

void CDiscordProto::ShutdownSession()
{
	if (m_bTerminated)
		return;

	debugLogA("CDiscordProto::ShutdownSession");

	// shutdown all resources
	if (m_hWorkerThread)
		SetEvent(m_evRequestsQueue);
	if (m_hGatewayConnection)
		Netlib_Shutdown(m_hGatewayConnection);
	if (m_hAPIConnection)
		Netlib_Shutdown(m_hAPIConnection);

	OnLoggedOut();
}

void CDiscordProto::ConnectionFailed(int iReason)
{
	debugLogA("CDiscordProto::ConnectionFailed -> reason %d", iReason);
	delSetting("AccessToken");

	ProtoBroadcastAck(0, ACKTYPE_LOGIN, ACKRESULT_FAILED, nullptr, iReason);
	ShutdownSession();
}
