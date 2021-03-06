/*

Jabber Protocol Plugin for Miranda NG

Copyright (c) 2002-04  Santithorn Bunchua
Copyright (c) 2005-12  George Hazan
Copyright (c) 2007     Maxim Mluhov
Copyright (C) 2012-19 Miranda NG team

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "stdafx.h"
#include "jabber_iq.h"
#include "jabber_rc.h"
#include "version.h"

BOOL CJabberProto::OnIqRequestVersion(const TiXmlElement*, CJabberIqInfo *pInfo)
{
	if (!pInfo->GetFrom())
		return TRUE;

	if (!m_bAllowVersionRequests)
		return FALSE;

	XmlNodeIq iq("result", pInfo);
	TiXmlElement *query = iq << XQUERY(JABBER_FEAT_VERSION);
	query << XCHILD("name", "Miranda NG Jabber");
	query << XCHILD("version", szCoreVersion);

	if (m_bShowOSVersion) {
		wchar_t os[256] = { 0 };
		if (!GetOSDisplayString(os, _countof(os)))
			mir_wstrncpy(os, L"Microsoft Windows", _countof(os));
		query << XCHILD("os", T2Utf(os));
	}

	m_ThreadInfo->send(iq);
	return TRUE;
}

// last activity (XEP-0012) support
BOOL CJabberProto::OnIqRequestLastActivity(const TiXmlElement*, CJabberIqInfo *pInfo)
{
	m_ThreadInfo->send(
		XmlNodeIq("result", pInfo) << XQUERY(JABBER_FEAT_LAST_ACTIVITY)
		<< XATTRI("seconds", m_tmJabberIdleStartTime ? time(0) - m_tmJabberIdleStartTime : 0));
	return TRUE;
}

// XEP-0199: XMPP Ping support
BOOL CJabberProto::OnIqRequestPing(const TiXmlElement*, CJabberIqInfo *pInfo)
{
	m_ThreadInfo->send(XmlNodeIq("result", pInfo) << XATTR("from", m_ThreadInfo->fullJID));
	return TRUE;
}

// Returns the current GMT offset in seconds
int GetGMTOffset(void)
{
	TIME_ZONE_INFORMATION tzinfo;
	int nOffset = 0;

	DWORD dwResult = GetTimeZoneInformation(&tzinfo);

	switch (dwResult) {
	case TIME_ZONE_ID_STANDARD:
		nOffset = tzinfo.Bias + tzinfo.StandardBias;
		break;
	case TIME_ZONE_ID_DAYLIGHT:
		nOffset = tzinfo.Bias + tzinfo.DaylightBias;
		break;
	case TIME_ZONE_ID_UNKNOWN:
		nOffset = tzinfo.Bias;
		break;
	case TIME_ZONE_ID_INVALID:
	default:
		nOffset = 0;
		break;
	}

	return -nOffset;
}

// entity time (XEP-0202) support
BOOL CJabberProto::OnIqRequestTime(const TiXmlElement*, CJabberIqInfo *pInfo)
{
	wchar_t stime[100];
	wchar_t szTZ[10];

	TimeZone_PrintDateTime(UTC_TIME_HANDLE, L"I", stime, _countof(stime), 0);

	int nGmtOffset = GetGMTOffset();
	mir_snwprintf(szTZ, L"%+03d:%02d", nGmtOffset / 60, nGmtOffset % 60);

	XmlNodeIq iq("result", pInfo);
	TiXmlElement *timeNode = iq << XCHILDNS("time", JABBER_FEAT_ENTITY_TIME);
	timeNode << XCHILD("utc", T2Utf(stime)); timeNode << XCHILD("tzo", T2Utf(szTZ));
	
	const wchar_t *szTZName = TimeZone_GetName(nullptr);
	if (szTZName)
		timeNode << XCHILD("tz", T2Utf(szTZName));
	
	m_ThreadInfo->send(iq);
	return TRUE;
}

BOOL CJabberProto::OnIqProcessIqOldTime(const TiXmlElement*, CJabberIqInfo *pInfo)
{
	struct tm *gmt;
	time_t ltime;
	char stime[100], *dtime;

	_tzset();
	time(&ltime);
	gmt = gmtime(&ltime);
	mir_snprintf(stime, "%.4i%.2i%.2iT%.2i:%.2i:%.2i",
		gmt->tm_year + 1900, gmt->tm_mon + 1,
		gmt->tm_mday, gmt->tm_hour, gmt->tm_min, gmt->tm_sec);
	dtime = ctime(&ltime);
	dtime[24] = 0;

	XmlNodeIq iq("result", pInfo);
	TiXmlElement *queryNode = iq << XQUERY(JABBER_FEAT_ENTITY_TIME_OLD);
	queryNode << XCHILD("utc", stime);

	const wchar_t *szTZName = TimeZone_GetName(nullptr);
	if (szTZName)
		queryNode << XCHILD("tz", T2Utf(szTZName));

	queryNode << XCHILD("display", dtime);
	m_ThreadInfo->send(iq);
	return TRUE;
}

BOOL CJabberProto::OnIqRequestAvatar(const TiXmlElement*, CJabberIqInfo *pInfo)
{
	if (!m_bEnableAvatars)
		return TRUE;

	int pictureType = m_bAvatarType;
	if (pictureType == PA_FORMAT_UNKNOWN)
		return TRUE;

	const char *szMimeType = ProtoGetAvatarMimeType(pictureType);
	if (szMimeType == nullptr)
		return TRUE;

	wchar_t szFileName[MAX_PATH];
	GetAvatarFileName(0, szFileName, _countof(szFileName));

	FILE* in = _wfopen(szFileName, L"rb");
	if (in == nullptr)
		return TRUE;

	long bytes = _filelength(_fileno(in));
	ptrA buffer((char*)mir_alloc(bytes * 4 / 3 + bytes + 1000));
	if (buffer == nullptr) {
		fclose(in);
		return TRUE;
	}

	fread(buffer, bytes, 1, in);
	fclose(in);

	ptrA str(mir_base64_encode(buffer, bytes));
	m_ThreadInfo->send(XmlNodeIq("result", pInfo) << XQUERY(JABBER_FEAT_AVATAR) << XCHILD("query", str) << XATTR("mimetype", szMimeType));
	return TRUE;
}

BOOL CJabberProto::OnSiRequest(const TiXmlElement *node, CJabberIqInfo *pInfo)
{
	const char *szProfile = XmlGetAttr(pInfo->GetChildNode(), "profile");

	if (szProfile && !mir_strcmp(szProfile, JABBER_FEAT_SI_FT))
		FtHandleSiRequest(node);
	else {
		XmlNodeIq iq("error", pInfo);
		TiXmlElement *error = iq << XCHILD("error") << XATTRI("code", 400) << XATTR("type", "cancel");
		error << XCHILDNS("bad-request", "urn:ietf:params:xml:ns:xmpp-stanzas");
		error << XCHILD("bad-profile");
		m_ThreadInfo->send(iq);
	}
	return TRUE;
}

BOOL CJabberProto::OnRosterPushRequest(const TiXmlElement*, CJabberIqInfo *pInfo)
{
	auto *queryNode = pInfo->GetChildNode();

	// RFC 3921 #7.2 Business Rules
	if (pInfo->GetFrom()) {
		ptrA szFrom(JabberPrepareJid(pInfo->GetFrom()));
		if (!szFrom)
			return TRUE;

		ptrA szTo(JabberPrepareJid(m_ThreadInfo->fullJID));
		if (!szTo)
			return TRUE;

		char *pDelimiter = strchr(szFrom, '/');
		if (pDelimiter)
			*pDelimiter = 0;

		pDelimiter = strchr(szTo, '/');
		if (pDelimiter)
			*pDelimiter = 0;

		// invalid JID
		BOOL bRetVal = mir_strcmp(szFrom, szTo) == 0;
		if (!bRetVal) {
			debugLogA("<iq/> attempt to hack via roster push from %s", pInfo->GetFrom());
			return TRUE;
		}
	}

	debugLogA("<iq/> Got roster push");
	for (auto *itemNode : TiXmlFilter(queryNode, "item")) {
		const char *jid = XmlGetAttr(itemNode, "jid"), *str = XmlGetAttr(itemNode, "subscription");
		if (jid == nullptr || str == nullptr)
			continue;

		// we will not add new account when subscription=remove
		if (!mir_strcmp(str, "to") || !mir_strcmp(str, "both") || !mir_strcmp(str, "from") || !mir_strcmp(str, "none")) {
			const char *name = XmlGetAttr(itemNode, "name");
			ptrA nick((name != nullptr) ? mir_strdup(name) : JabberNickFromJID(jid));
			if (nick != nullptr) {
				MCONTACT hContact = HContactFromJID(jid, false);
				if (hContact == 0)
					hContact = DBCreateContact(jid, nick, false, false);
				else
					db_set_utf(hContact, m_szModuleName, "jid", jid);

				JABBER_LIST_ITEM *item = ListAdd(LIST_ROSTER, jid, hContact);
				replaceStr(item->nick, nick);
				item->bRealContact = true;

				replaceStr(item->group, XmlGetChildText(itemNode, "group"));

				if (name != nullptr) {
					ptrA tszNick(getUStringA(hContact, "Nick"));
					if (tszNick != nullptr) {
						if (mir_strcmp(nick, tszNick) != 0)
							db_set_utf(hContact, "CList", "MyHandle", nick);
						else
							db_unset(hContact, "CList", "MyHandle");
					}
					else db_set_utf(hContact, "CList", "MyHandle", nick);
				}
				else db_unset(hContact, "CList", "MyHandle");

				if (!m_bIgnoreRosterGroups) {
					if (item->group != nullptr) {
						Clist_GroupCreate(0, Utf2T(item->group));
						db_set_utf(hContact, "CList", "Group", item->group);
					}
					else db_unset(hContact, "CList", "Group");
				}
			}
		}

		if (auto *item = ListGetItemPtr(LIST_ROSTER, jid)) {
			if (!mir_strcmp(str, "both"))
				item->subscription = SUB_BOTH;
			else if (!mir_strcmp(str, "to"))
				item->subscription = SUB_TO;
			else if (!mir_strcmp(str, "from"))
				item->subscription = SUB_FROM;
			else
				item->subscription = SUB_NONE;

			debugLogA("Roster push for jid=%s (hContact=%d), set subscription to %s", jid, item->hContact, str);

			// subscription = remove is to remove from roster list
			// but we will just set the contact to offline and not actually
			// remove, so that history will be retained.
			if (!mir_strcmp(str, "remove")) {
				SetContactOfflineStatus(item->hContact);
				UpdateSubscriptionInfo(item->hContact, item);
			}
			else if (isChatRoom(item->hContact))
				db_unset(item->hContact, "CList", "Hidden");
			else
				UpdateSubscriptionInfo(item->hContact, item);
		}
	}

	UI_SAFE_NOTIFY(m_pDlgServiceDiscovery, WM_JABBER_TRANSPORT_REFRESH);
	RebuildInfoFrame();
	return TRUE;
}

BOOL CJabberProto::OnIqRequestOOB(const TiXmlElement*, CJabberIqInfo *pInfo)
{
	if (!pInfo->GetFrom() || !pInfo->GetHContact())
		return TRUE;

	const char *pszUrl = XmlGetChildText(pInfo->GetChildNode(), "url");
	if (!pszUrl)
		return TRUE;

	if (m_bBsOnlyIBB) {
		// reject
		XmlNodeIq iq("error", pInfo);
		TiXmlElement *e = XmlAddChildA(iq, "error", "File transfer refused"); e->SetAttribute("code", 406);
		m_ThreadInfo->send(iq);
		return TRUE;
	}

	filetransfer *ft = new filetransfer(this);
	ft->std.totalFiles = 1;
	ft->jid = mir_strdup(pInfo->GetFrom());
	ft->std.hContact = pInfo->GetHContact();
	ft->type = FT_OOB;
	ft->httpHostName = nullptr;
	ft->httpPort = 80;
	ft->httpPath = nullptr;

	// Parse the URL
	if (!mir_strncmpi(pszUrl, "http://", 7)) {
		const char *p = pszUrl + 7, *q;
		if ((q = strchr(p, '/')) != nullptr) {
			char text[1024];
			if (q - p < _countof(text)) {
				strncpy_s(text, p, q - p);
				text[q - p] = '\0';
				if (char *d = strchr(text, ':')) {
					ft->httpPort = (WORD)atoi(d + 1);
					*d = '\0';
				}
				ft->httpHostName = mir_strdup(text);
			}
			ft->httpPath = mir_strdup(q);
			mir_urlDecode(ft->httpPath);
		}
	}

	if (pInfo->GetIdStr())
		ft->szId = JabberId2string(pInfo->GetIqId());

	if (ft->httpHostName && ft->httpPath) {
		debugLogA("Host=%s Port=%d Path=%s", ft->httpHostName, ft->httpPort, ft->httpPath);

		const char *desc = XmlGetChildText(pInfo->GetChildNode(), "desc");
		debugLogA("description = %s", desc);

		const char *str2;
		if ((str2 = strrchr(ft->httpPath, '/')) != nullptr)
			str2++;
		else
			str2 = ft->httpPath;

		PROTORECVFILE pre;
		pre.timestamp = time(0);
		pre.descr.a = desc;
		pre.files.a = &str2;
		pre.fileCount = 1;
		pre.lParam = (LPARAM)ft;
		ProtoChainRecvFile(ft->std.hContact, &pre);
	}
	else { // reject
		XmlNodeIq iq("error", pInfo);
		TiXmlElement *e = XmlAddChildA(iq, "error", "File transfer refused"); e->SetAttribute("code", 406);
		m_ThreadInfo->send(iq);
		delete ft;
	}
	return TRUE;
}

BOOL CJabberProto::OnHandleDiscoInfoRequest(const TiXmlElement *iqNode, CJabberIqInfo *pInfo)
{
	if (!pInfo->GetChildNode())
		return TRUE;

	const char *szNode = XmlGetAttr(pInfo->GetChildNode(), "node");
	// caps hack
	if (m_clientCapsManager.HandleInfoRequest(iqNode, pInfo, szNode))
		return TRUE;

	// ad-hoc hack:
	if (szNode && m_adhocManager.HandleInfoRequest(iqNode, pInfo, szNode))
		return TRUE;

	// another request, send empty result
	m_ThreadInfo->send(
		XmlNodeIq("error", pInfo)
		<< XCHILD("error") << XATTRI("code", 404) << XATTR("type", "cancel")
		<< XCHILDNS("item-not-found", "urn:ietf:params:xml:ns:xmpp-stanzas"));
	return TRUE;
}

BOOL CJabberProto::OnHandleDiscoItemsRequest(const TiXmlElement *iqNode, CJabberIqInfo *pInfo)
{
	if (!pInfo->GetChildNode())
		return TRUE;

	// ad-hoc commands check:
	const char *szNode = XmlGetAttr(pInfo->GetChildNode(), "node");
	if (szNode && m_adhocManager.HandleItemsRequest(iqNode, pInfo, szNode))
		return TRUE;

	// another request, send empty result
	XmlNodeIq iq("result", pInfo);
	TiXmlElement *resultQuery = iq << XQUERY(JABBER_FEAT_DISCO_ITEMS);
	if (szNode)
		resultQuery->SetAttribute("node", szNode);

	if (!szNode && m_bEnableRemoteControl)
		resultQuery << XCHILD("item") << XATTR("jid", m_ThreadInfo->fullJID) << XATTR("node", JABBER_FEAT_COMMANDS) << XATTR("name", "Ad-hoc commands");

	m_ThreadInfo->send(iq);
	return TRUE;
}

BOOL CJabberProto::AddClistHttpAuthEvent(CJabberHttpAuthParams *pParams)
{
	char szService[256];
	mir_snprintf(szService, "%s%s", m_szModuleName, JS_HTTP_AUTH);

	CLISTEVENT cle = {};
	cle.hIcon = g_plugin.getIcon(IDI_HTTP_AUTH);
	cle.flags = CLEF_PROTOCOLGLOBAL | CLEF_UNICODE;
	cle.hDbEvent = -99;
	cle.lParam = (LPARAM)pParams;
	cle.pszService = szService;
	cle.szTooltip.w = TranslateT("Http authentication request received");
	g_clistApi.pfnAddEvent(&cle);
	return TRUE;
}

BOOL CJabberProto::OnIqHttpAuth(const TiXmlElement *node, CJabberIqInfo *pInfo)
{
	if (!m_bAcceptHttpAuth)
		return TRUE;

	if (!node || !pInfo->GetChildNode() || !pInfo->GetFrom() || !pInfo->GetIdStr())
		return TRUE;

	auto *pConfirm = XmlFirstChild(node, "confirm");
	if (!pConfirm)
		return TRUE;

	const char *szId = XmlGetAttr(pConfirm, "id");
	const char *szUrl = XmlGetAttr(pConfirm, "url");
	const char *szMethod = XmlGetAttr(pConfirm, "method");
	if (!szId || !szMethod || !szUrl)
		return TRUE;

	CJabberHttpAuthParams *pParams = (CJabberHttpAuthParams*)mir_calloc(sizeof(CJabberHttpAuthParams));
	if (pParams) {
		pParams->m_nType = CJabberHttpAuthParams::IQ;
		pParams->m_szFrom = mir_strdup(pInfo->GetFrom());
		pParams->m_szId = mir_strdup(szId);
		pParams->m_szMethod = mir_strdup(szMethod);
		pParams->m_szUrl = mir_strdup(szUrl);
		AddClistHttpAuthEvent(pParams);
	}
	return TRUE;
}
