#pragma once

#include "SteamTypes.h"

class ISteamFriends
{
public:
	virtual const char* GetPersonaName() = 0;
	virtual void* SetPersonaName(const char* pchPersonaName) = 0;
	virtual int GetPersonaState() = 0;
	virtual int GetFriendCount(int iFriendFlags) = 0;
	virtual CSteamID GetFriendByIndex(int iFriend, int iFriendFlags) = 0;
	virtual int GetFriendRelationship(CSteamID steamIDFriend) = 0;
	virtual int GetFriendPersonaState(CSteamID steamIDFriend) = 0;
	virtual const char* GetFriendPersonaName(CSteamID steamIDFriend) = 0;
	virtual bool GetFriendGamePlayed(CSteamID steamIDFriend, void* pFriendGameInfo) = 0;
	virtual const char* GetFriendPersonaNameHistory(CSteamID steamIDFriend, int iPersonaName) = 0;
	virtual int GetFriendSteamLevel(CSteamID steamIDFriend) = 0;
	virtual const char* GetPlayerNickname(CSteamID steamIDPlayer) = 0;
	virtual int GetFriendsGroupCount() = 0;
	virtual int16 GetFriendsGroupIDByIndex(int iFG) = 0;
	virtual const char* GetFriendsGroupName(int16 friendsGroupID) = 0;
	virtual int GetFriendsGroupMembersCount(int16 friendsGroupID) = 0;
	virtual void GetFriendsGroupMembersList(int16 friendsGroupID, CSteamID* pOutSteamIDMembers, int nMembersCount) = 0;
	virtual bool HasFriend(CSteamID steamIDFriend, int iFriendFlags) = 0;
	virtual int GetClanCount() = 0;
	virtual CSteamID GetClanByIndex(int iClan) = 0;
	virtual const char* GetClanName(CSteamID steamIDClan) = 0;
	virtual const char* GetClanTag(CSteamID steamIDClan) = 0;
	virtual bool GetClanActivityCounts(CSteamID steamIDClan, int* pnOnline, int* pnInGame, int* pnChatting) = 0;
	virtual void* DownloadClanActivityCounts(CSteamID* psteamIDClans, int cClansToRequest) = 0;
	virtual int GetFriendCountFromSource(CSteamID steamIDSource) = 0;
	virtual CSteamID GetFriendFromSourceByIndex(CSteamID steamIDSource, int iFriend) = 0;
	virtual bool IsUserInSource(CSteamID steamIDUser, CSteamID steamIDSource) = 0;
	virtual void SetInGameVoiceSpeaking(CSteamID steamIDUser, bool bSpeaking) = 0;
	virtual void ActivateGameOverlay(const char* pchDialog) = 0;
	virtual void ActivateGameOverlayToUser(const char* pchDialog, CSteamID steamID) = 0;
	virtual void ActivateGameOverlayToWebPage(const char* pchURL, int eMode) = 0;
	virtual void ActivateGameOverlayToStore(uint32 nAppID, int eFlag) = 0;
	virtual void SetPlayedWith(CSteamID steamIDUserPlayedWith) = 0;
	virtual void ActivateGameOverlayInviteDialog(CSteamID steamIDLobby) = 0;
	virtual int GetSmallFriendAvatar(CSteamID steamIDFriend) = 0;
	virtual int GetMediumFriendAvatar(CSteamID steamIDFriend) = 0;
	virtual int GetLargeFriendAvatar(CSteamID steamIDFriend) = 0;
	virtual bool RequestUserInformation(CSteamID steamIDUser, bool bRequireNameOnly) = 0;
};
