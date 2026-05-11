#pragma once

namespace fb
{
#ifdef CYPRESS_BFN
    enum SecureReason
    {
        SecureReason_MismatchingContent = 126688420,
        SecureReason_VirtualServerExpired = 161719482,
        SecureReason_KickedOutServerFull = 166979642,
        SecureReason_WrongProtocolVersion = 185410182,
        SecureReason_KickedByAdmin = 209082678,
        SecureReason_Ok = 271367885,
        SecureReason_ESportsTeamFull = 327593467,
        SecureReason_MissingContent = 369647972,
        SecureReason_WrongPassword = 404977237,
        SecureReason_ESportsMatchEnding = 490728169,
        SecureReason_ESportsMatchWalkover = 537879353,
        SecureReason_WrongTitleVersion = 544941936,
        SecureReason_KickedOut = 588146540,
        SecureReason_DuplicateConnection = 633501800,
        SecureReason_ConnectFailed = 671806548,
        SecureReason_ConfigurationNotAllowed = 878665044,
        SecureReason_ServerReclaimed = 1179286160,
        SecureReason_KickedFromQueue = 1273769029,
        SecureReason_ESportsMatchWarmupTimedOut = 1488582677,
        SecureReason_InactivityInSocialHub = 1563215904,
        SecureReason_GenericError = 1607365024,
        SecureReason_NotVerified = 1688587866,
        SecureReason_KickedViaFairFight = 2024888468,
        SecureReason_PersistenceDownloadFailed = 2037929289,
        SecureReason_VirtualServerRecreate = 2070965438,
        SecureReason_TrialExcluded = -2147021015,
        SecureReason_ServerMaintenance = -2094710223,
        SecureReason_InteractivityTimeout = -1814382885,
        SecureReason_Inactivity = -1776838237,
        SecureReason_EAC_Violation = -1693383952,
        SecureReason_InvalidSpectateJoin = -1627030565,
        SecureReason_MalformedPacket = -1594863018,
        SecureReason_RankRestricted = -1537661598,
        SecureReason_KickedViaPunkBuster = -1455013253,
        SecureReason_ESportsMatchStarting = -1340840634,
        SecureReason_AcceptFailed = -1328758582,
        SecureReason_TimedOut = -1248028522,
        SecureReason_NotInESportsRosters = -1165517799,
        SecureReason_InactivityInGameServer = -935921745,
        SecureReason_Banned = -919204789,
        SecureReason_KickedCommanderOnLeave = -917904984,
        SecureReason_NoSpectateSlotAvailable = -913624236,
        SecureReason_NoReply = -845770278,
        SecureReason_EAC_AuthFailed = -812067782,
        SecureReason_TeamKills = -770347611,
        SecureReason_SendFail = -579632873,
        SecureReason_ESportsMatchAborted = -575324723,
        SecureReason_NotAllowedToSpectate = -521965794,
        SecureReason_InactivityInSpawn = -458219809,
        SecureReason_KickedCommanderAfterMutiny = -380081852,
        SecureReason_EAC_Banned = -369534509,
        SecureReason_ServerFull = -290965793,
        SecureReason_KickedOutDemoOver = -283096383,
        SecureReason_ConnectionHandshaking = -93304723
    };

    static const char* SecureReason_toString(SecureReason reason)
    {
        switch (reason) {
        case SecureReason_Ok: return "Ok";
        case SecureReason_WrongProtocolVersion: return "Wrong Protocol Version";
        case SecureReason_WrongTitleVersion: return "Wrong Title Version";
        case SecureReason_ServerFull: return "Server Full";
        case SecureReason_KickedOut: return "Kicked Out";
        case SecureReason_Banned: return "Banned";
        case SecureReason_GenericError: return "Generic Error";
        case SecureReason_WrongPassword: return "Wrong Password";
        case SecureReason_KickedOutDemoOver: return "Kicked Out (Demo Over)";
        case SecureReason_RankRestricted: return "Rank Restricted";
        case SecureReason_ConfigurationNotAllowed: return "Configuration Not Allowed";
        case SecureReason_ServerReclaimed: return "Server Reclaimed";
        case SecureReason_MissingContent: return "Missing Content";
        case SecureReason_NotVerified: return "Not Verified";
        case SecureReason_TimedOut: return "Timed Out";
        case SecureReason_ConnectFailed: return "Connect Failed";
        case SecureReason_NoReply: return "No Reply";
        case SecureReason_AcceptFailed: return "Accept Failed";
        case SecureReason_MismatchingContent: return "Mismatching Content";
        case SecureReason_InteractivityTimeout: return "Interactivity Timeout";
        case SecureReason_KickedFromQueue: return "Kicked From Queue";
        case SecureReason_TeamKills: return "Team Kills";
        case SecureReason_KickedByAdmin: return "Kicked By Admin";
        case SecureReason_KickedViaPunkBuster: return "Kicked Via PunkBuster";
        case SecureReason_KickedOutServerFull: return "Kicked Out (Server Full)";
        case SecureReason_ESportsMatchStarting: return "ESports Match Starting";
        case SecureReason_NotInESportsRosters: return "Not In ESports Rosters";
        case SecureReason_ESportsMatchEnding: return "ESportsMatchEnding";
        case SecureReason_VirtualServerExpired: return "Virtual Server Expired";
        case SecureReason_VirtualServerRecreate: return "Virtual Server Recreate";
        case SecureReason_ESportsTeamFull: return "ESports Team Full";
        case SecureReason_ESportsMatchAborted: return "ESports Match Aborted";
        case SecureReason_ESportsMatchWalkover: return "ESports Match Walkover";
        case SecureReason_ESportsMatchWarmupTimedOut: return "ESports Match Warmup Timed Out";
        case SecureReason_NotAllowedToSpectate: return "Not Allowed To Spectate";
        case SecureReason_NoSpectateSlotAvailable: return "No Spectate Slot Available";
        case SecureReason_InvalidSpectateJoin: return "Invalid Spectate Join";
        case SecureReason_KickedViaFairFight: return "Kicked Via FairFight";
        case SecureReason_KickedCommanderOnLeave: return "Kicked Commander On Leave";
        case SecureReason_KickedCommanderAfterMutiny: return "Kicked Commander After Mutiny";
        case SecureReason_ServerMaintenance: return "Server Maintenance";
        case SecureReason_PersistenceDownloadFailed: return "Persistence Download Failed";
        default: return "Unknown reason";
        }
    }
#else
    enum SecureReason {
        SecureReason_Ok,
        SecureReason_WrongProtocolVersion,
        SecureReason_WrongTitleVersion,
        SecureReason_ServerFull,
        SecureReason_KickedOut,
        SecureReason_Banned,
        SecureReason_GenericError,
        SecureReason_WrongPassword,
        SecureReason_KickedOutDemoOver,
        SecureReason_RankRestricted,
        SecureReason_ConfigurationNotAllowed,
        SecureReason_ServerReclaimed,
        SecureReason_MissingContent,
        SecureReason_NotVerified,
        SecureReason_TimedOut,
        SecureReason_ConnectFailed,
        SecureReason_NoReply,
        SecureReason_AcceptFailed,
        SecureReason_MismatchingContent,
        SecureReason_InteractivityTimeout,
        SecureReason_KickedFromQueue,
        SecureReason_TeamKills,
        SecureReason_KickedByAdmin,
        SecureReason_KickedViaPunkBuster,
        SecureReason_KickedOutServerFull,
        SecureReason_ESportsMatchStarting,
        SecureReason_NotInESportsRosters,
        SecureReason_ESportsMatchEnding,
        SecureReason_VirtualServerExpired,
        SecureReason_VirtualServerRecreate,
        SecureReason_ESportsTeamFull,
        SecureReason_ESportsMatchAborted,
        SecureReason_ESportsMatchWalkover,
        SecureReason_ESportsMatchWarmupTimedOut,
        SecureReason_NotAllowedToSpectate,
        SecureReason_NoSpectateSlotAvailable,
        SecureReason_InvalidSpectateJoin,
        SecureReason_KickedViaFairFight,
        SecureReason_KickedCommanderOnLeave,
        SecureReason_KickedCommanderAfterMutiny,
        SecureReason_ServerMaintenance,
        SecureReason_PlayerRemoveTimedOut,
        SecureReason_PlayerRemovePoorQuality,
        SecureReason_PlayerRemovedConnLost,
        SecureReason_PlayerRemovedBlazeserverConnection,
        SecureReason_PlayerRemovedMigrationFail,
        SecureReason_PlayerRemovedGameDestroyed,
        SecureReason_PlayerRemovedQueueFailed,
        SecureReason_PlayerRemovedExternalSession,
        SecureReason_HostDisbandedGroup,
        SecureReason_PersistenceDownloadFailed,
        SecureReason_ClientInactivity,
        SecureReason_TrialExpired,
        SecureReason_TrialUpgraded,
        SecureReason_Count
    };

    static const char* SecureReason_toStringArray[] = {
    "Ok",
    "WrongProtocolVersion",
    "WrongTitleVersion",
    "ServerFull",
    "KickedOut",
    "Banned",
    "GenericError",
    "WrongPassword",
    "KickedOutDemoOver",
    "RankRestricted",
    "ConfigurationNotAllowed",
    "ServerReclaimed",
    "MissingContent",
    "NotVerified",
    "TimedOut",
    "ConnectFailed",
    "NoReply",
    "AcceptFailed",
    "MismatchingContent",
    "InteractivityTimeout",
    "KickedFromQueue",
    "TeamKills",
    "KickedByAdmin",
    "KickedViaPunkBuster",
    "KickedOutServerFull",
    "ESportsMatchStarting",
    "NotInESportsRosters",
    "ESportsMatchEnding",
    "VirtualServerExpired",
    "VirtualServerRecreate",
    "ESportsTeamFull",
    "ESportsMatchAborted",
    "ESportsMatchWalkover",
    "ESportsMatchWarmupTimedOut",
    "NotAllowedToSpectate",
    "NoSpectateSlotAvailable",
    "InvalidSpectateJoin",
    "KickedViaFairFight",
    "KickedCommanderOnLeave",
    "KickedCommanderAfterMutiny",
    "ServerMaintenance",
    "PlayerRemoveTimedOut",
    "PlayerRemovePoorQuality",
    "PlayerRemovedConnLost",
    "PlayerRemovedBlazeserverConnection",
    "PlayerRemovedMigrationFail",
    "PlayerRemovedGameDestroyed",
    "PlayerRemovedQueueFailed",
    "PlayerRemovedExternalSession",
    "HostDisbandedGroup",
    "PersistenceDownloadFailed",
    "ClientInactivity",
    "TrialExpired",
    "TrialUpgraded"
    };

    static const char* SecureReason_toString(SecureReason reason)
    {
        if (reason >= SecureReason_Ok && reason < SecureReason_Count)
            return SecureReason_toStringArray[reason];

        return "Unknown reason";
    }
#endif
}