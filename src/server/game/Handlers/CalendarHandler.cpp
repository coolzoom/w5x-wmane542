/*
 * Copyright (C) 2008-2013 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/*
----- Opcodes Not Used yet -----

SMSG_CALENDAR_CLEAR_PENDING_ACTION SendCalendarClearPendingAction()
SMSG_CALENDAR_RAID_LOCKOUT_UPDATED SendCalendarRaidLockoutUpdated(InstanceSave const* save)

----- Opcodes without Sniffs -----
SMSG_CALENDAR_FILTER_GUILD              [ for (... uint32(count) { packguid(???), uint8(???) } ]
SMSG_CALENDAR_ARENA_TEAM                [ for (... uint32(count) { packguid(???), uint8(???) } ]
CMSG_CALENDAR_EVENT_INVITE_NOTES        [ packguid(Invitee), uint64(inviteId), string(Text), Boolean(Unk) ]
SMSG_CALENDAR_EVENT_INVITE_NOTES        [ uint32(unk1), uint32(unk2), uint32(unk3), uint32(unk4), uint32(unk5) ]
SMSG_CALENDAR_EVENT_INVITE_NOTES_ALERT  [ uint64(inviteId), string(Text) ]
SMSG_CALENDAR_EVENT_INVITE_STATUS_ALERT [ Structure unkown ]

*/

#include "Common.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "InstanceSaveMgr.h"
#include "Log.h"
#include "Opcodes.h"
#include "Player.h"
#include "CalendarMgr.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "DatabaseEnv.h"

void WorldSession::HandleCalendarGetCalendar(WorldPacket &recvData)
{
    uint64 guid = _player->GetGUID();
    TC_LOG_DEBUG("network", "CMSG_CALENDAR_GET_CALENDAR [" UI64FMTD "]", guid);

    time_t curTime = time_t(time(NULL));

    ByteBuffer eventsBuffer;
    ByteBuffer invitesBuffer;
    ByteBuffer holidaysBuffer;
    ByteBuffer raidLockoutBuffer;
    ByteBuffer raidResetBuffer;

    WorldPacket data(SMSG_CALENDAR_SEND_CALENDAR);

    CalendarEventIdList const& events = sCalendarMgr->GetPlayerEvents(guid);

    uint32 eventCount = 0;
    size_t eventCountPos = data.bitwpos();
    data.WriteBits(eventCount, 19);

    for (auto const &calendarEventId : events)
    {
        if (CalendarEvent* calendarEvent = sCalendarMgr->GetEvent(calendarEventId))
        {
            ObjectGuid creatorGuid = calendarEvent->GetCreatorGUID();
            ObjectGuid guildGuid = MAKE_NEW_GUID(calendarEvent->GetGuildId(), 0, calendarEvent->GetGuildId() ? uint32(HIGHGUID_GUILD) : 0);

            data.WriteBits(calendarEvent->GetTitle().size(), 8);
            data.WriteBitSeq<1>(creatorGuid);
            data.WriteBitSeq<7>(guildGuid);
            data.WriteBitSeq<0>(creatorGuid);
            data.WriteBitSeq<3, 6, 5>(guildGuid);
            data.WriteBitSeq<7, 2, 6>(creatorGuid);
            data.WriteBitSeq<0, 1, 2, 4>(guildGuid);
            data.WriteBitSeq<4, 3, 5>(creatorGuid);

            eventsBuffer.WriteByteSeq<6, 7>(guildGuid);
            eventsBuffer << uint32(calendarEvent->GetTime());
            eventsBuffer.WriteByteSeq<1>(creatorGuid);
            eventsBuffer.WriteByteSeq<2>(guildGuid);
            eventsBuffer.WriteByteSeq<6, 4, 7>(creatorGuid);
            eventsBuffer.WriteByteSeq<3, 5>(guildGuid);
            eventsBuffer << uint32(calendarEvent->GetDungeonId());
            eventsBuffer.WriteByteSeq<1, 0>(guildGuid);
            eventsBuffer << uint64(calendarEvent->GetEventId());
            eventsBuffer.WriteByteSeq<5>(creatorGuid);
            eventsBuffer.WriteByteSeq<4>(guildGuid);
            eventsBuffer.WriteByteSeq<3, 0>(creatorGuid);
            eventsBuffer.WriteString(calendarEvent->GetTitle());
            eventsBuffer.WriteByteSeq<2>(creatorGuid);
            eventsBuffer << uint32(calendarEvent->GetFlags());
            eventsBuffer << uint8(calendarEvent->GetType());

            eventCount++;
        }
    }

    CalendarInviteIdList const& invites = sCalendarMgr->GetPlayerInvites(guid);

    uint32 inviteCount = 0;
    size_t inviteCountPos = data.bitwpos();
    data.WriteBits(inviteCount, 19);

    for (auto const &calendarInviteId : invites)
    {
        CalendarInvite* invite = sCalendarMgr->GetInvite(calendarInviteId);
        CalendarEvent* calendarEvent = invite ? sCalendarMgr->GetEvent(invite->GetEventId()) : NULL;

        if (calendarEvent)
        {
            ObjectGuid creatorGuid = invite->GetSenderGUID();

            data.WriteBitSeq<1, 6, 2, 3, 5, 7, 4, 0>(creatorGuid);

            invitesBuffer.WriteByteSeq<7>(creatorGuid);
            invitesBuffer << uint8(invite->GetStatus());
            invitesBuffer << uint8(invite->GetRank());
            invitesBuffer.WriteByteSeq<6, 3>(creatorGuid);
            invitesBuffer << uint64(invite->GetEventId());
            invitesBuffer.WriteByteSeq<2, 5, 1>(creatorGuid);
            invitesBuffer << uint8(calendarEvent ? calendarEvent->GetGuildId() : 0);
            invitesBuffer.WriteByteSeq<4, 0>(creatorGuid);
            invitesBuffer << uint64(invite->GetInviteId());

            inviteCount++;
        }
    }

    uint32 raidResetCount = 0;
    size_t raidResetCountPos = data.bitwpos();
    data.WriteBits(raidResetCount, 20);

    data.WriteBits(0 /*sHolidaysStore.GetNumRows()*/, 16);
    /*for (uint32 i = 0; i < sHolidaysStore.GetNumRows(); i++)
    {
        data.WriteBits(0, 6);   // Name length

        for (uint8 j = 0; j < 51; j++)
            holidaysBuffer << uint32(0);
    }*/

    uint32 raidLockoutCount = 0;
    size_t raidLockoutCountPos = data.bitwpos();
    data.WriteBits(raidLockoutCount, 20);

    for (uint8 i = 0; i < MAX_DIFFICULTY; i++)
    {
        for (auto const &raidLockout : _player->m_boundInstances[i])
        {
            if (raidLockout.second.perm)
            {
                InstanceSave* save = raidLockout.second.save;
                ObjectGuid instanceGuid = MAKE_NEW_GUID(save->GetInstanceId(), 0, HIGHGUID_INSTANCE_SAVE);

                data.WriteBitSeq<2, 7, 1, 6, 3, 5, 4, 0>(instanceGuid);

                raidLockoutBuffer << uint32(save->GetDifficulty());
                raidLockoutBuffer.WriteByteSeq<1>(instanceGuid);
                raidLockoutBuffer << uint32(save->GetMapId());
                raidLockoutBuffer.WriteByteSeq<4, 3, 7>(instanceGuid);
                raidLockoutBuffer << uint32(save->GetResetTime());
                raidLockoutBuffer.WriteByteSeq<6, 5, 0, 2>(instanceGuid);

                raidLockoutCount++;
            }
        }
    }

    std::set<uint32> raidCheck;
    for (auto const &raidReset : sInstanceSaveMgr->GetResetTimeMap())
    {
        uint32 mapId = PAIR32_LOPART(raidReset.first);

        MapEntry const* mapEntry = sMapStore.LookupEntry(mapId);
        if (!mapEntry || !mapEntry->IsRaid())
            continue;

        if (raidCheck.find(mapId) != raidCheck.end())
            continue;

        raidResetBuffer << uint32(mapId);
        raidResetBuffer << uint32(raidReset.second - 1800);
        raidResetBuffer << uint32(7 * DAY);

        raidCheck.insert(mapId);
        raidResetCount++;
    }

    data.FlushBits();

    data.PutBits(eventCountPos, eventCount, 19);
    data.PutBits(inviteCountPos, inviteCount, 19);
    data.PutBits(raidResetCountPos, raidResetCount, 20);
    data.PutBits(raidLockoutCountPos, raidLockoutCount, 20);
    data.PutBits(eventCountPos, eventCount, 19);

    data.append(raidLockoutBuffer);
    data.append(raidResetBuffer);
    data.append(holidaysBuffer);
    data.append(eventsBuffer);
    data << uint32(1135753200);                             // RaidOrigin
    data << uint32(secsToTimeBitFields(curTime));           // ServerTime
    data.append(invitesBuffer);
    data << uint32(time(NULL));                             // ServerNow

    SendPacket(&data);

    TC_LOG_DEBUG("network", "SMSG_CALENDAR_SEND_CALENDAR [" UI64FMTD "]", guid);
}

void WorldSession::HandleCalendarGetEvent(WorldPacket& recvData)
{
    uint64 eventId;
    recvData >> eventId;

    TC_LOG_DEBUG("network", "CMSG_CALENDAR_GET_EVENT. Event: ["
        UI64FMTD "] Event [" UI64FMTD "]", _player->GetGUID(), eventId);

    if (CalendarEvent* calendarEvent = sCalendarMgr->GetEvent(eventId))
        SendCalendarEvent(*calendarEvent, CALENDAR_SENDTYPE_GET);
}

void WorldSession::HandleCalendarGuildFilter(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "CMSG_CALENDAR_GUILD_FILTER [" UI64FMTD "]", _player->GetGUID());

    int32 unk1, unk2, unk3;
    recvData >> unk1;
    recvData >> unk2;
    recvData >> unk3;

    TC_LOG_DEBUG("network", "Calendar: CMSG_CALENDAR_GUILD_FILTER - unk1: %d unk2: %d unk3: %d", unk1, unk2, unk3);
}

void WorldSession::HandleCalendarArenaTeam(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "CMSG_CALENDAR_ARENA_TEAM [" UI64FMTD "]", _player->GetGUID());

    int32 unk1;
    recvData >> unk1;

    TC_LOG_DEBUG("network", "Calendar: CMSG_CALENDAR_ARENA_TEAM - unk1: %d", unk1);
}

void WorldSession::HandleCalendarAddEvent(WorldPacket& recvData)
{
    uint64 guid = _player->GetGUID();
    std::string title;
    std::string description;
    uint8 type;
    bool repeatable;
    uint32 maxInvites;
    int32 dungeonId;
    uint32 eventPackedTime;
    uint32 unkPackedTime;
    uint32 flags;
    uint64 inviteId = 0;
    uint64 invitee = 0;
    uint8 status;
    uint8 rank;

    recvData >> title >> description >> type >> repeatable >> maxInvites;
    recvData >> dungeonId >> eventPackedTime >> unkPackedTime >> flags;

    if (!(flags & CALENDAR_FLAG_WITHOUT_INVITES))
    {
        uint32 inviteCount;
        recvData >> inviteCount;
        recvData.readPackGUID(invitee);
        recvData >> status >> rank;

        if (inviteCount != 1 || invitee != guid)
        {
            TC_LOG_ERROR("network", "HandleCalendarAddEvent: [" UI64FMTD
                 "]: More than one invite (%d) or Invitee  [" UI64FMTD
                 "] differs", guid, inviteCount, invitee);
            return;
        }

        inviteId = sCalendarMgr->GetFreeInviteId();
    }
    else
    {
        inviteId = 0;
        status = CALENDAR_STATUS_NO_OWNER;
        rank = CALENDAR_RANK_PLAYER;
    }

    TC_LOG_DEBUG("network", "CMSG_CALENDAR_ADD_EVENT: [" UI64FMTD "] "
        "Title %s, Description %s, type %u, Repeatable %u, MaxInvites %u, "
        "Dungeon ID %d, Time %u, Time2 %u, Flags %u, Invitee [" UI64FMTD "] "
        "Status %d, Rank %d", guid, title.c_str(), description.c_str(),
        type, repeatable, maxInvites, dungeonId, eventPackedTime,
        unkPackedTime, flags, invitee, status, rank);

    CalendarAction action;

    action.SetAction(CALENDAR_ACTION_ADD_EVENT);
    action.SetPlayer(_player);
    action.Event.SetEventId(sCalendarMgr->GetFreeEventId());
    action.Event.SetCreatorGUID(guid);
    action.Event.SetType((CalendarEventType) type);
    action.Event.SetFlags(flags);
    action.Event.SetTime(eventPackedTime);
    action.Event.SetTimeZoneTime(unkPackedTime);
    action.Event.SetRepeatable(repeatable);
    action.Event.SetMaxInvites(maxInvites);
    action.Event.SetDungeonId(dungeonId);
    action.Event.SetGuildId((flags & CALENDAR_FLAG_GUILD_ONLY) ? GetPlayer()->GetGuildId() : 0);
    action.Event.SetTitle(title);
    action.Event.SetDescription(description);
    action.Event.AddInvite(inviteId);
    action.Invite.SetEventId(action.Event.GetEventId());
    action.Invite.SetInviteId(inviteId);
    action.Invite.SetInvitee(invitee);
    action.Invite.SetStatus((CalendarInviteStatus) status);
    action.Invite.SetRank((CalendarModerationRank) rank);
    action.Invite.SetSenderGUID(guid);

    sCalendarMgr->AddAction(action);
}

void WorldSession::HandleCalendarUpdateEvent(WorldPacket& recvData)
{
    uint64 guid = _player->GetGUID();
    uint64 eventId;
    uint64 inviteId;
    std::string title;
    std::string description;
    uint8 type;
    bool repeatable;
    uint32 maxInvites;
    int32 dungeonId;
    uint32 eventPackedTime;
    uint32 timeZoneTime;
    uint32 flags;

    recvData >> eventId >> inviteId >> title >> description >> type;
    recvData >> repeatable >> maxInvites >> dungeonId;
    recvData  >> eventPackedTime >> timeZoneTime >> flags;

    TC_LOG_DEBUG("network", "CMSG_CALENDAR_UPDATE_EVENT [" UI64FMTD "] EventId [" UI64FMTD
        "], InviteId [" UI64FMTD "] Title %s, Description %s, type %u "
        "Repeatable %u, MaxInvites %u, Dungeon ID %d, Time %u "
        "Time2 %u, Flags %u", guid, eventId, inviteId, title.c_str(),
        description.c_str(), type, repeatable, maxInvites, dungeonId,
        eventPackedTime, timeZoneTime, flags);

    CalendarAction action;
    action.SetAction(CALENDAR_ACTION_MODIFY_EVENT);
    action.SetPlayer(_player);
    action.SetInviteId(inviteId);
    action.Event.SetEventId(eventId);
    action.Event.SetType((CalendarEventType) type);
    action.Event.SetFlags((CalendarFlags) flags);
    action.Event.SetTime(eventPackedTime);
    action.Event.SetTimeZoneTime(timeZoneTime);
    action.Event.SetRepeatable(repeatable);
    action.Event.SetDungeonId(dungeonId);
    action.Event.SetTitle(title);
    action.Event.SetDescription(description);
    action.Event.SetMaxInvites(maxInvites);

    sCalendarMgr->AddAction(action);
}

void WorldSession::HandleCalendarRemoveEvent(WorldPacket& recvData)
{
    uint64 guid = _player->GetGUID();
    uint64 eventId;
    uint64 inviteId;
    uint32 flags;

    recvData >> eventId >> inviteId >> flags;
    TC_LOG_DEBUG("network", "CMSG_CALENDAR_REMOVE_EVENT [" UI64FMTD "], EventId [" UI64FMTD
        "] inviteId [" UI64FMTD "] Flags?: %u", guid, eventId, inviteId, flags);

    CalendarAction action;
    action.SetAction(CALENDAR_ACTION_REMOVE_EVENT);
    action.SetPlayer(_player);
    action.SetInviteId(inviteId);
    action.Event.SetEventId(eventId);
    action.Event.SetFlags((CalendarFlags) flags);

    sCalendarMgr->AddAction(action);
}

void WorldSession::HandleCalendarCopyEvent(WorldPacket& recvData)
{
    uint64 guid = _player->GetGUID();
    uint64 eventId;
    uint64 inviteId;
    uint32 time;

    recvData >> eventId >> inviteId >> time;
    TC_LOG_DEBUG("network", "CMSG_CALENDAR_COPY_EVENT [" UI64FMTD "], EventId [" UI64FMTD
        "] inviteId [" UI64FMTD "] Time: %u", guid, eventId, inviteId, time);

    CalendarAction action;
    action.SetAction(CALENDAR_ACTION_COPY_EVENT);
    action.SetPlayer(_player);
    action.SetInviteId(inviteId);
    action.Event.SetEventId(eventId);
    action.Event.SetTime(time);

    sCalendarMgr->AddAction(action);
}

void WorldSession::HandleCalendarEventInvite(WorldPacket& recvData)
{
    time_t now = time(NULL);
    if (now - timeLastCalendarInvCommand < 5)
        return;
    else
       timeLastCalendarInvCommand = now;

    uint64 guid = _player->GetGUID();
    uint64 eventId;
    uint64 inviteId;
    std::string name;
    uint8 status;
    uint8 rank;
    uint64 invitee = 0;
    uint32 team = 0;

    recvData >> eventId >> inviteId >> name >> status >> rank;
    if (Player* player = sObjectAccessor->FindPlayerByName(name))
    {
        invitee = player->GetGUID();
        team = player->GetTeam();
    }
    else
    {
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_GUID_RACE_ACC_BY_NAME);
        stmt->setString(0, name);
        if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
        {
            Field* fields = result->Fetch();
            invitee = MAKE_NEW_GUID(fields[0].GetUInt32(), 0, HIGHGUID_PLAYER);
            team = Player::TeamForRace(fields[1].GetUInt8());
        }
    }

    TC_LOG_DEBUG("network", "CMSG_CALENDAR_EVENT_INVITE [" UI64FMTD "], EventId ["
        UI64FMTD "] InviteId [" UI64FMTD "] Name %s ([" UI64FMTD "]), status %u, "
        "Rank %u", guid, eventId, inviteId, name.c_str(), invitee, status, rank);

    if (!invitee)
    {
        SendCalendarCommandResult(CALENDAR_ERROR_PLAYER_NOT_FOUND);
        return;
    }

    if (_player->GetTeam() != team && !sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_CALENDAR))
    {
        SendCalendarCommandResult(CALENDAR_ERROR_NOT_ALLIED);
        return;
    }

    // TODO: Check ignore, even if offline (db query)

    CalendarAction action;
    action.SetAction(CALENDAR_ACTION_ADD_EVENT_INVITE);
    action.SetPlayer(_player);
    action.SetInviteId(inviteId);
    action.Invite.SetEventId(eventId);
    action.Invite.SetInviteId(sCalendarMgr->GetFreeInviteId());
    action.Invite.SetSenderGUID(_player->GetGUID());
    action.Invite.SetInvitee(invitee);
    action.Invite.SetRank((CalendarModerationRank) rank);
    action.Invite.SetStatus((CalendarInviteStatus) status);

    sCalendarMgr->AddAction(action);
}

void WorldSession::HandleCalendarEventSignup(WorldPacket& recvData)
{
    uint64 guid = _player->GetGUID();
    uint64 eventId;
    uint8 status;

    recvData >> eventId >> status;
    TC_LOG_DEBUG("network", "CMSG_CALENDAR_EVENT_SIGNUP [" UI64FMTD "] EventId ["
        UI64FMTD "] Status %u", guid, eventId, status);

    CalendarAction action;
    action.SetAction(CALENDAR_ACTION_SIGNUP_TO_EVENT);
    action.SetPlayer(_player);
    action.SetExtraData(GetPlayer()->GetGuildId());
    action.Event.SetEventId(eventId);
    action.Invite.SetStatus((CalendarInviteStatus) status);
    sCalendarMgr->AddAction(action);
}

void WorldSession::HandleCalendarEventRsvp(WorldPacket& recvData)
{
    uint64 guid = _player->GetGUID();
    uint64 eventId;
    uint64 inviteId;
    uint8 status;

    recvData >> eventId >> inviteId >> status;
    TC_LOG_DEBUG("network", "CMSG_CALENDAR_EVENT_RSVP [" UI64FMTD"] EventId ["
        UI64FMTD "], InviteId [" UI64FMTD "], status %u", guid, eventId,
        inviteId, status);

    CalendarAction action;
    action.SetAction(CALENDAR_ACTION_MODIFY_EVENT_INVITE);
    action.SetPlayer(_player);
    action.SetInviteId(inviteId);
    action.Invite.SetInviteId(inviteId);
    action.Invite.SetEventId(eventId);
    action.Invite.SetStatus((CalendarInviteStatus) status);

    sCalendarMgr->AddAction(action);
}

void WorldSession::HandleCalendarEventRemoveInvite(WorldPacket& recvData)
{
    uint64 guid = _player->GetGUID();
    uint64 invitee;
    uint64 eventId;
    uint64 owninviteId;
    uint64 inviteId;

    recvData.readPackGUID(invitee);
    recvData >> inviteId >> owninviteId >> eventId;

    TC_LOG_DEBUG("network", "CMSG_CALENDAR_EVENT_REMOVE_INVITE ["
        UI64FMTD "] EventId [" UI64FMTD "], OwnInviteId ["
        UI64FMTD "], Invitee ([" UI64FMTD "] id: [" UI64FMTD "])",
        guid, eventId, owninviteId, invitee, inviteId);

    CalendarAction action;
    action.SetAction(CALENDAR_ACTION_REMOVE_EVENT_INVITE);
    action.SetPlayer(_player);
    action.SetInviteId(owninviteId);
    action.Invite.SetInviteId(inviteId);
    action.Invite.SetEventId(eventId);
    action.Invite.SetInvitee(invitee);

    sCalendarMgr->AddAction(action);
}

void WorldSession::HandleCalendarEventStatus(WorldPacket& recvData)
{
    uint64 guid = _player->GetGUID();
    uint64 invitee;
    uint64 eventId;
    uint64 inviteId;
    uint64 owninviteId;
    uint8 status;

    recvData.readPackGUID(invitee);
    recvData >> eventId >>  inviteId >> owninviteId >> status;
    TC_LOG_DEBUG("network", "CMSG_CALENDAR_EVENT_STATUS [" UI64FMTD"] EventId ["
        UI64FMTD "] OwnInviteId [" UI64FMTD "], Invitee ([" UI64FMTD "] id: ["
        UI64FMTD "], status %u", guid, eventId, owninviteId, invitee, inviteId, status);

    CalendarAction action;
    action.SetAction(CALENDAR_ACTION_MODIFY_EVENT_INVITE);
    action.SetPlayer(_player);
    action.SetInviteId(owninviteId);
    action.Invite.SetInviteId(inviteId);
    action.Invite.SetEventId(eventId);
    action.Invite.SetInvitee(invitee);
    action.Invite.SetStatus((CalendarInviteStatus) status);

    sCalendarMgr->AddAction(action);
}

void WorldSession::HandleCalendarEventModeratorStatus(WorldPacket& recvData)
{
    uint64 guid = _player->GetGUID();
    uint64 invitee;
    uint64 eventId;
    uint64 inviteId;
    uint64 owninviteId;
    uint8 status;

    recvData.readPackGUID(invitee);
    recvData >> eventId >>  inviteId >> owninviteId >> status;
    TC_LOG_DEBUG("network", "CMSG_CALENDAR_EVENT_MODERATOR_STATUS [" UI64FMTD "] EventId ["
        UI64FMTD "] OwnInviteId [" UI64FMTD "], Invitee ([" UI64FMTD "] id: ["
        UI64FMTD "], status %u", guid, eventId, owninviteId, invitee, inviteId, status);

    CalendarAction action;
    action.SetAction(CALENDAR_ACTION_MODIFY_MODERATOR_EVENT_INVITE);
    action.SetPlayer(_player);
    action.SetInviteId(owninviteId);
    action.Invite.SetInviteId(inviteId);
    action.Invite.SetEventId(eventId);
    action.Invite.SetInvitee(invitee);
    action.Invite.SetStatus((CalendarInviteStatus) status);

    sCalendarMgr->AddAction(action);
}

void WorldSession::HandleCalendarComplain(WorldPacket& recvData)
{
    uint64 guid = _player->GetGUID();
    uint64 eventId;
    uint64 complainGUID;

    recvData >> eventId >> complainGUID;
    TC_LOG_DEBUG("network", "CMSG_CALENDAR_COMPLAIN [" UI64FMTD "] EventId ["
        UI64FMTD "] guid [" UI64FMTD "]", guid, eventId, complainGUID);
}

void WorldSession::HandleCalendarGetNumPending(WorldPacket& /*recvData*/)
{
    uint64 guid = _player->GetGUID();
    uint32 pending = sCalendarMgr->GetPlayerNumPending(guid);

    TC_LOG_DEBUG("network", "CMSG_CALENDAR_GET_NUM_PENDING: [" UI64FMTD
        "] Pending: %u", guid, pending);

    WorldPacket data(SMSG_CALENDAR_SEND_NUM_PENDING, 4);
    data << uint32(pending);
    SendPacket(&data);
}

// ----------------------------------- SEND ------------------------------------

void WorldSession::SendCalendarEvent(CalendarEvent const& calendarEvent, CalendarSendEventType sendEventType)
{
    uint64 eventId = calendarEvent.GetEventId();

    TC_LOG_DEBUG("network", "SMSG_CALENDAR_SEND_EVENT [" UI64FMTD "] EventId ["
        UI64FMTD "] SendType %u", _player->GetGUID(), eventId, sendEventType);

    WorldPacket data(SMSG_CALENDAR_SEND_EVENT);
    data << uint8(sendEventType);
    data.appendPackGUID(calendarEvent.GetCreatorGUID());
    data << uint64(eventId);
    data << calendarEvent.GetTitle().c_str();
    data << calendarEvent.GetDescription().c_str();
    data << uint8(calendarEvent.GetType());
    data << uint8(calendarEvent.GetRepeatable());
    data << uint32(calendarEvent.GetMaxInvites());
    data << int32(calendarEvent.GetDungeonId());
    data << uint32(calendarEvent.GetFlags());
    data << uint32(calendarEvent.GetTime());
    data << uint32(calendarEvent.GetTimeZoneTime());
    //data << uint32(calendarEvent.GetGuildId());
    data << uint64(0);
    CalendarInviteIdList const& invites = calendarEvent.GetInviteIdList();
    data << uint32(invites.size());
    for (CalendarInviteIdList::const_iterator it = invites.begin(); it != invites.end(); ++it)
    {
        if (CalendarInvite* invite = sCalendarMgr->GetInvite(*it))
        {
            uint64 guid = invite->GetInvitee();
            Player* player = ObjectAccessor::FindPlayer(guid);
            uint8 level = player ? player->getLevel() : Player::GetLevelFromDB(guid);

            data.appendPackGUID(guid);
            data << uint8(level);
            data << uint8(invite->GetStatus());
            data << uint8(invite->GetRank());
            data << uint8(calendarEvent.GetGuildId() != 0);
            data << uint64(invite->GetInviteId());
            data << uint32(invite->GetStatusTime());
            data << invite->GetText().c_str();
        }
        else
        {
            data.appendPackGUID(_player->GetGUID());
            data << uint8(0) << uint8(0) << uint8(0) << uint8(0)
                << uint64(0) << uint32(0) << uint8(0);

            TC_LOG_ERROR("network", "SendCalendarEvent: No Invite found with id [" UI64FMTD "]", *it);
        }
    }
    SendPacket(&data);
}

void WorldSession::SendCalendarEventInvite(CalendarInvite const& invite, bool pending)
{
    uint64 guid = _player->GetGUID();
    uint64 eventId = invite.GetEventId();
    uint64 inviteId = invite.GetInviteId();
    uint64 invitee = invite.GetInvitee();
    uint8 status = invite.GetStatus();
    uint32 statusTime = invite.GetStatusTime();
    Player* player = ObjectAccessor::FindPlayer(invitee);
    uint8 level = player ? player->getLevel() : Player::GetLevelFromDB(invitee);

    TC_LOG_DEBUG("network", "SMSG_CALENDAR_EVENT_INVITE [" UI64FMTD "] EventId ["
        UI64FMTD "] InviteId [" UI64FMTD "] Invitee [" UI64FMTD "] "
        " Level %u, Status %u, StatusTime %u" , guid, eventId, inviteId,
        invitee, level, status, statusTime);

    WorldPacket data(SMSG_CALENDAR_EVENT_INVITE, 8 + 8 + 8 + 1 + 1 + 1 + (statusTime ? 4 : 0) + 1);
    data.appendPackGUID(invitee);
    data << uint64(eventId);
    data << uint64(inviteId);
    data << uint8(level);
    data << uint8(status);
    if (statusTime)
        data << uint8(1) << uint32(statusTime);
    else
        data << uint8(0);
    data << uint8(pending);

    SendPacket(&data);
}

void WorldSession::SendCalendarEventInviteAlert(CalendarEvent const& calendarEvent, CalendarInvite const& invite)
{
    uint64 guid = _player->GetGUID();
    uint64 eventId = calendarEvent.GetEventId();
    uint64 inviteId = invite.GetInviteId();

    TC_LOG_DEBUG("network", "SMSG_CALENDAR_EVENT_INVITE_ALERT [" UI64FMTD "] EventId ["
        UI64FMTD "] InviteId [" UI64FMTD "]", guid, eventId, inviteId);

    WorldPacket data(SMSG_CALENDAR_EVENT_INVITE_ALERT);
    data << uint64(eventId);
    data << calendarEvent.GetTitle().c_str();
    data << uint32(calendarEvent.GetTime());
    data << uint32(calendarEvent.GetFlags());
    data << uint32(calendarEvent.GetType());
    data << uint32(calendarEvent.GetDungeonId());
    data << uint64(inviteId);
    data << uint8(invite.GetStatus());
    data << uint8(invite.GetRank());
    data.appendPackGUID(calendarEvent.GetCreatorGUID());
    data.appendPackGUID(invite.GetSenderGUID());
    SendPacket(&data);
}

void WorldSession::SendCalendarEventUpdateAlert(CalendarEvent const& calendarEvent, CalendarSendEventType sendEventType)
{
    uint64 guid = _player->GetGUID();
    uint64 eventId = calendarEvent.GetEventId();

    TC_LOG_DEBUG("network", "SMSG_CALENDAR_EVENT_UPDATED_ALERT ["
        UI64FMTD "] EventId [" UI64FMTD "]", guid, eventId);


    WorldPacket data(SMSG_CALENDAR_EVENT_UPDATED_ALERT, 1 + 8 + 4 + 4 + 4 + 1 + 4 +
        calendarEvent.GetTitle().size() + calendarEvent.GetDescription().size() + 1 + 4 + 4);
    data << uint8(sendEventType);
    data << uint64(eventId);
    data << uint32(calendarEvent.GetTime());
    data << uint32(calendarEvent.GetFlags());
    data << uint32(calendarEvent.GetTime());
    data << uint8(calendarEvent.GetType());
    data << uint32(calendarEvent.GetDungeonId());
    data << calendarEvent.GetTitle().c_str();
    data << calendarEvent.GetDescription().c_str();
    data << uint8(calendarEvent.GetRepeatable());
    data << uint32(calendarEvent.GetMaxInvites());
    data << uint32(0); // FIXME
    SendPacket(&data);
}

void WorldSession::SendCalendarEventRemovedAlert(CalendarEvent const& calendarEvent)
{
    uint64 guid = _player->GetGUID();
    uint64 eventId = calendarEvent.GetEventId();
    uint32 eventTime = (calendarEvent.GetTime());

    TC_LOG_DEBUG("network", "SMSG_CALENDAR_EVENT_REMOVED_ALERT [" UI64FMTD "] EventId ["
        UI64FMTD "] Time %u", guid, eventId, eventTime);

    WorldPacket data(SMSG_CALENDAR_EVENT_REMOVED_ALERT, 1 + 8 + 1);
    data << uint8(1); // FIXME: If true does not SignalEvent(EVENT_CALENDAR_ACTION_PENDING)
    data << uint64(eventId);
    data << uint32(eventTime);
    SendPacket(&data);
}

void WorldSession::SendCalendarEventStatus(CalendarEvent const& calendarEvent, CalendarInvite const& invite)
{
    uint64 guid = _player->GetGUID();
    uint64 eventId = calendarEvent.GetEventId();
    uint64 inviteId = invite.GetInviteId();
    uint64 invitee = invite.GetInvitee();
    uint32 eventTime = (calendarEvent.GetTime());
    uint32 flags = calendarEvent.GetFlags();
    uint8 status = invite.GetStatus();
    uint8 rank = invite.GetRank();
    uint32 statusTime = secsToTimeBitFields(invite.GetStatusTime());


    TC_LOG_DEBUG("network", "SMSG_CALENDAR_EVENT_STATUS [" UI64FMTD "] EventId ["
        UI64FMTD "] InviteId [" UI64FMTD "] Invitee [" UI64FMTD "] Time %u "
        "Flags %u, Status %u, Rank %u, StatusTime %u",
        guid, eventId, inviteId, invitee, eventTime, flags, status, rank,
        statusTime);

    WorldPacket data(SMSG_CALENDAR_EVENT_STATUS, 8 + 8 + 4 + 4 + 1 + 1 + 4);
    data.appendPackGUID(invitee);
    data << uint64(eventId);
    data << uint32(eventTime);
    data << uint32(flags);
    data << uint8(status);
    data << uint8(rank);
    data << uint32(statusTime);
    SendPacket(&data);
}

void WorldSession::SendCalendarEventModeratorStatusAlert(CalendarInvite const& invite)
{
    uint64 guid = _player->GetGUID();
    uint64 eventId = invite.GetEventId();
    uint64 invitee = invite.GetInvitee();
    uint8 status = invite.GetStatus();


    TC_LOG_DEBUG("network", "SMSG_CALENDAR_EVENT_MODERATOR_STATUS_ALERT [" UI64FMTD
        "] Invitee [" UI64FMTD "] EventId [" UI64FMTD "] Status %u ", guid,
        invitee, eventId, status);


    WorldPacket data(SMSG_CALENDAR_EVENT_MODERATOR_STATUS_ALERT, 8 + 8 + 1 + 1);
    data.appendPackGUID(invitee);
    data << uint64(eventId);
    data << uint8(status);
    data << uint8(1); // FIXME
    SendPacket(&data);
}

void WorldSession::SendCalendarEventInviteRemoveAlert(CalendarEvent const& calendarEvent, CalendarInviteStatus status)
{
    uint64 guid = _player->GetGUID();
    uint64 eventId = calendarEvent.GetEventId();
    uint32 eventTime = (calendarEvent.GetTime());
    uint32 flags = calendarEvent.GetFlags();

    TC_LOG_DEBUG("network", "SMSG_CALENDAR_EVENT_INVITE_REMOVED_ALERT ["
        UI64FMTD "] EventId [" UI64FMTD "] Time %u, Flags %u, Status %u",
        guid, eventId, eventTime, flags, status);

    WorldPacket data(SMSG_CALENDAR_EVENT_INVITE_REMOVED_ALERT, 8 + 4 + 4 + 1);
    data << uint64(eventId);
    data << uint32(eventTime);
    data << uint32(flags);
    data << uint8(status);
    SendPacket(&data);
}

void WorldSession::SendCalendarEventInviteRemove(CalendarInvite const& invite, uint32 flags)
{
    uint64 guid = _player->GetGUID();
    uint64 eventId = invite.GetEventId();
    uint64 invitee = invite.GetInvitee();

    TC_LOG_DEBUG("network", "SMSG_CALENDAR_EVENT_INVITE_REMOVED ["
        UI64FMTD "] Invitee [" UI64FMTD "] EventId [" UI64FMTD
        "] Flags %u", guid, invitee, eventId, flags);

    WorldPacket data(SMSG_CALENDAR_EVENT_INVITE_REMOVED, 8 + 4 + 4 + 1);
    data.appendPackGUID(invitee);
    data << uint32(eventId);
    data << uint32(flags);
    data << uint8(1); // FIXME
    SendPacket(&data);
}

void WorldSession::SendCalendarClearPendingAction()
{
    uint64 guid = _player->GetGUID();
    TC_LOG_DEBUG("network", "SMSG_CALENDAR_CLEAR_PENDING_ACTION [" UI64FMTD "]", guid);

    WorldPacket data(SMSG_CALENDAR_CLEAR_PENDING_ACTION, 0);
    SendPacket(&data);
}

void WorldSession::SendCalendarCommandResult(CalendarError err, char const* param /*= NULL*/)
{
    uint64 guid = _player->GetGUID();
    TC_LOG_DEBUG("network", "SMSG_CALENDAR_COMMAND_RESULT [" UI64FMTD "] Value: %u", guid, err);

    WorldPacket data(SMSG_CALENDAR_COMMAND_RESULT, 0);
    data << uint32(0);
    data << uint8(0);
    switch (err)
    {
        case CALENDAR_ERROR_OTHER_INVITES_EXCEEDED:
        case CALENDAR_ERROR_ALREADY_INVITED_TO_EVENT_S:
        case CALENDAR_ERROR_IGNORING_YOU_S:
            data << param;
            break;
        default:
            data << uint8(0);
            break;
    }

    data << uint32(err);

    SendPacket(&data);
}

void WorldSession::SendCalendarRaidLockout(InstanceSave const* save, bool add)
{
    TC_LOG_DEBUG("network", "%s", add ? "SMSG_CALENDAR_RAID_LOCKOUT_ADDED" : "SMSG_CALENDAR_RAID_LOCKOUT_REMOVED");
    time_t currTime = time(NULL);

    WorldPacket data(SMSG_CALENDAR_RAID_LOCKOUT_REMOVED, (add ? 4 : 0) + 4 + 4 + 4 + 8);
    if (add)
    {
        data.SetOpcode(SMSG_CALENDAR_RAID_LOCKOUT_ADDED);
        data << uint32(secsToTimeBitFields(currTime));
    }

    data << uint32(save->GetMapId());
    data << uint32(save->GetDifficulty());
    data << uint32(save->GetResetTime() - currTime);
    data << uint64(save->GetInstanceId());
    SendPacket(&data);
}

void WorldSession::SendCalendarRaidLockoutUpdated(InstanceSave const* save)
{
    if (!save)
        return;

    uint64 guid = _player->GetGUID();
    TC_LOG_DEBUG("network", "SMSG_CALENDAR_RAID_LOCKOUT_UPDATED [" UI64FMTD
        "] Map: %u, Difficulty %u", guid, save->GetMapId(), save->GetDifficulty());

    time_t cur_time = time_t(time(NULL));

    WorldPacket data(SMSG_CALENDAR_RAID_LOCKOUT_UPDATED, 4 + 4 + 4 + 4 + 8);
    data << secsToTimeBitFields(cur_time);
    data << uint32(save->GetMapId());
    data << uint32(save->GetDifficulty());
    data << uint32(0); // Amount of seconds that has changed to the reset time
    data << uint32(save->GetResetTime() - cur_time);
    SendPacket(&data);
}
