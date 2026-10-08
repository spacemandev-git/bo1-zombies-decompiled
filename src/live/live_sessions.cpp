#include "live_sessions.h"
#include <cstring>
#include <qcommon/msg_mp.h>
#include <client/cl_main.h>
#include "live_win.h"
#include <client/client.h>
#include "live_sessions_win.h"
#include <server_mp/sv_main_mp.h> // mod (coop): svs, to find the host by its loopback address
#include <qcommon/common.h>

unsigned __int8 s_qosPayload[8];

int __cdecl Session_BuildQoSPayload()
{
    msg_t qosMessage; // [esp+Ch] [ebp-4Ch] BYREF
    bool isMigrating; // [esp+3Fh] [ebp-19h]
    unsigned __int8 qos_payload[8]; // [esp+40h] [ebp-18h] BYREF
    int maxLocalPlayersAllowed; // [esp+4Ch] [ebp-Ch]
    bool allowGuests; // [esp+53h] [ebp-5h]
    connstate_t cstate; // [esp+54h] [ebp-4h]

    maxLocalPlayersAllowed = 1;
    allowGuests = 1;
    memset(qos_payload, 0, sizeof(qos_payload));
    MSG_Init(&qosMessage, qos_payload, 8);
    MSG_WriteBits(&qosMessage, 1044, 0x10u);
    MSG_WriteBit1(&qosMessage);
    isMigrating = 0;
    MSG_WriteBits(&qosMessage, 0, 1u);
    MSG_WriteBits(&qosMessage, 1, 6u);
    MSG_WriteBits(&qosMessage, maxLocalPlayersAllowed, 3u);
    if ( allowGuests )
        MSG_WriteBit1(&qosMessage);
    else
        MSG_WriteBit0(&qosMessage);
    if ( xblive_matchEndingSoon->current.enabled )
        MSG_WriteBit1(&qosMessage);
    else
        MSG_WriteBit0(&qosMessage);
    cstate = CL_GetLocalClientConnectionState(0);
    if ( cstate == CA_CHALLENGING )
        MSG_WriteBit1(&qosMessage);
    else
        MSG_WriteBit0(&qosMessage);
    if ( qosMessage.cursize != 4
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\live\\live_sessions.cpp",
                    196,
                    0,
                    "%s",
                    "qosMessage.cursize == QOS_CORE_PAYLOAD_SIZE") )
    {
        __debugbreak();
    }
    if ( qosMessage.overflowed
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\live\\live_sessions.cpp",
                    202,
                    0,
                    "%s",
                    "!qosMessage.overflowed") )
    {
        __debugbreak();
    }
    if ( !memcmp(qos_payload, s_qosPayload, 8u) )
        return 0;
    *(unsigned int *)s_qosPayload = *(unsigned int *)qos_payload;
    *(unsigned int *)&s_qosPayload[4] = *(unsigned int *)&qos_payload[4];
    return qosMessage.cursize;
}

unsigned __int8 *__cdecl Session_GetQosPayloadBuffer()
{
    return s_qosPayload;
}

bool __cdecl Session_IsHost(SessionData_s *session, int clientNum)
{
    if (IsDedicatedServer())
        return false;
    if (Demo_IsEnabled())
        return clientNum == 1;
    // mod (coop): the host is the listen server's own (loopback) client, which can have any client number (bo1_slot);
    // retail always gave the host client 0
    if (clientNum >= 0 && clientNum < com_maxclients->current.integer && svs.clients
        && svs.clients[clientNum].header.state >= CS_CONNECTED)
        return svs.clients[clientNum].header.netchan.remoteAddress.type == NA_LOOPBACK;
    return clientNum == 0;
}

int __cdecl Session_HostNum(SessionData_s *session)
{
    if ( !session
        && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\live\\live_sessions.cpp", 340, 0, "%s", "session") )
    {
        __debugbreak();
    }
    // mod (coop): the loopback client's number (see Session_IsHost); 0 as in retail when there is none
    for (int i = 0; svs.clients && i < com_maxclients->current.integer; ++i)
    {
        if (svs.clients[i].header.state >= CS_CONNECTED && svs.clients[i].header.netchan.remoteAddress.type == NA_LOOPBACK)
            return i;
    }
    return 0;
}

