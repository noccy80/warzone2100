/*
	This file is part of Warzone 2100.
	Copyright (C) 1999-2004  Eidos Interactive
	Copyright (C) 2005-2007  Warzone Resurrection Project

	Warzone 2100 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2100 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2100; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
/*
 * MultiOpt.c
 *
 * Alex Lee,97/98, Pumpkin Studios
 *
 * Routines for setting the game options and starting the init process.
 */
#include "lib/framework/frame.h"			// for everything
#include "map.h"
#include "game.h"			// for loading maps
#include "message.h"		// for clearing messages.
#include "winmain.h"
#include "display3d.h"		// for changing the viewpoint
#include "power.h"
#include "lib/widget/widget.h"
#include "lib/gamelib/gtime.h"
#include "lib/netplay/netplay.h"
#include "hci.h"
#include "configuration.h"			// lobby cfg.
#include "clparse.h"
#include "lib/ivis_common/piestate.h"

#include "component.h"
#include "console.h"
#include "multiplay.h"
#include "lib/sound/audio.h"
#include "multijoin.h"
#include "frontend.h"
#include "levels.h"
#include "multistat.h"
#include "multiint.h"
#include "multilimit.h"
#include "multigifts.h"
#include "aiexperience.h"	//for beacon messages
#include "multiint.h"

// ////////////////////////////////////////////////////////////////////////////
// External Variables

extern char	buildTime[8];

// ////////////////////////////////////////////////////////////////////////////
// ////////////////////////////////////////////////////////////////////////////

// send complete game info set!
// dpid == 0 for no new players.
void sendOptions(UDWORD dest, UDWORD play)
{
	NETMSG m;

	NetAdd(m,0,game);
	m.size = sizeof(game);

	NetAdd(m,m.size,player2dpid);			//add dpid array
	m.size += sizeof(player2dpid);

	NetAdd(m,m.size,ingame.JoiningInProgress);
	m.size += sizeof(ingame.JoiningInProgress);

	NetAdd(m,m.size,dest);
	m.size += sizeof(dest);

	NetAdd(m,m.size,play);
	m.size += sizeof(play);

	NetAdd(m,m.size,PlayerColour);
	m.size += sizeof(PlayerColour);

	NetAdd(m,m.size,alliances);
	m.size += sizeof(alliances);

	NetAdd(m,m.size,playerTeamGUI);
	m.size += sizeof(playerTeamGUI);

	NetAdd(m,m.size,ingame.numStructureLimits);
	m.size += sizeof(ingame.numStructureLimits);
	if(ingame.numStructureLimits)
	{
		memcpy(&(m.body[m.size]),ingame.pStructureLimits, ingame.numStructureLimits * (sizeof(UBYTE)+sizeof(UDWORD)) );
		m.size = (UWORD)(m.size + ingame.numStructureLimits * (sizeof(UBYTE)+sizeof(UDWORD)) );
	}

	//
	// now add the wdg files that are being used.
	//

	m.type = NET_OPTIONS;				// send it.
	NETbcast(&m,TRUE);

}

// ////////////////////////////////////////////////////////////////////////////
static BOOL checkGameWdg(const char *nm)
{
	LEVEL_DATASET *lev;

	//
	// now check the wdg files that are being used.
	//

	// game.map must be available in xxx list.

	lev = psLevels;
	while(lev)
	{
		if( strcmp(lev->pName, nm) == 0)
		{
			return TRUE;
		}
		lev=lev->psNext;
	}

	return FALSE;
}

// ////////////////////////////////////////////////////////////////////////////
// options for a game. (usually recvd in frontend)
void recvOptions(NETMSG *pMsg)
{
	UDWORD	pos=0,play,id;
	UDWORD	newPl;

	NetGet(pMsg,0,game);									// get details.
	pos += sizeof(game);
	if(strncmp((char*)game.version,buildTime,8) != 0)
	{

#ifndef DEBUG
		debug( LOG_ERROR, "Host is running a different version of Warzone2100." );
		abort();
#endif
	}
	if(ingame.numStructureLimits)							// free old limits.
	{
			ingame.numStructureLimits = 0;
			free(ingame.pStructureLimits);
			ingame.pStructureLimits = NULL;
	}

	NetGet(pMsg,pos,player2dpid);
	pos += sizeof(player2dpid);

	NetGet(pMsg,pos,ingame.JoiningInProgress);
	pos += sizeof(ingame.JoiningInProgress);

	NetGet(pMsg,pos,newPl);
	pos += sizeof(newPl);

	NetGet(pMsg,pos,play);
	pos += sizeof(play);

	NetGet(pMsg,pos,PlayerColour);
	pos += sizeof(PlayerColour);

	NetGet(pMsg,pos,alliances);
	pos += sizeof(alliances);

	NetGet(pMsg,pos,playerTeamGUI);
	pos += sizeof(playerTeamGUI);

	NetGet(pMsg,pos,ingame.numStructureLimits);
	pos += sizeof(ingame.numStructureLimits);
	if(ingame.numStructureLimits)
	{
		ingame.pStructureLimits = (UBYTE*)malloc(ingame.numStructureLimits*(sizeof(UDWORD)+sizeof(UBYTE)));	// malloc some room
		memcpy(ingame.pStructureLimits, &(pMsg->body[pos]) ,ingame.numStructureLimits*(sizeof(UDWORD)+sizeof(UBYTE)));
	}

	// process
	if(newPl != 0)
	{
		if(newPl == NetPlay.dpidPlayer)
		{
			// it's us thats new
			selectedPlayer = play;							// select player
			NETplayerInfo();							// get player info
			powerCalculated = FALSE;						// turn off any power requirements.
		}
		else
		{
			// someone else is joining.
			setupNewPlayer( newPl, play);
		}
	}


	// do the skirmish slider settings if they are up,
	for(id=0;id<MAX_PLAYERS;id++)
	{
		if(widgGetFromID(psWScreen,MULTIOP_SKSLIDE+id))
		{
			widgSetSliderPos(psWScreen,MULTIOP_SKSLIDE+id,game.skDiff[id]);
		}
	}

	if(!checkGameWdg(game.map) )
	{
		// request the map from the host. NET_REQUESTMAP
		{
			NETMSG m;
			NetAdd(m,0,NetPlay.dpidPlayer);
			m.type = NET_REQUESTMAP;
			m.size =4;
			NETbcast(&m,TRUE);
			addConsoleMessage("MAP REQUESTED!",DEFAULT_JUSTIFY);
		}
	}
	else
	{
		loadMapPreview();
	}

}






// ////////////////////////////////////////////////////////////////////////////
// Host Campaign.
BOOL hostCampaign(char *sGame, char *sPlayer)
{
	PLAYERSTATS playerStats;
	UDWORD		pl,numpl,i,j;

	debug(LOG_WZ, "Hosting campaign: '%s', player: '%s'", sGame, sPlayer);

	freeMessages();
	if(!NetPlay.bLobbyLaunched)
	{
		NEThostGame(sGame,sPlayer,game.type,0,0,0,game.maxPlayers); // temporary stuff
	}
	else
	{
		NETsetGameFlags(1,game.type);
		// 2 is average ping
		NETsetGameFlags(3,0);
		NETsetGameFlags(4,0);
	}

	for(i=0;i<MAX_PLAYERS;i++)
	{
		player2dpid[i] =0;
		(void)setPlayerName(i, "");			//Clear custom names (use default ones instead)
	}


	pl = rand()%game.maxPlayers;						//pick a player

	player2dpid[pl] = NetPlay.dpidPlayer;				// add ourselves to the array.
	selectedPlayer = pl;

	ingame.localJoiningInProgress = TRUE;
	ingame.JoiningInProgress[selectedPlayer] = TRUE;
	bMultiPlayer = TRUE;								// enable messages

	loadMultiStats(sPlayer,&playerStats);				// stats stuff
	setMultiStats(NetPlay.dpidPlayer,playerStats,FALSE);
	setMultiStats(NetPlay.dpidPlayer,playerStats,TRUE);

	if(!NetPlay.bComms)
	{
		NETplayerInfo();
		strcpy(NetPlay.players[0].name,sPlayer);
		numpl = 1;
	}
	else
	{
		numpl = NETplayerInfo();
	}

	// may be more than one player already. check and resolve!
	if(numpl >1)
	{
		for(j = 0;j<MAX_PLAYERS;j++)
		{
			if(NetPlay.players[j].dpid && (NetPlay.players[j].dpid != NetPlay.dpidPlayer))
			{
				for(i = 0;player2dpid[i] != 0;i++);
				player2dpid[i] = NetPlay.players[j].dpid;
			}
		}
	}

	return TRUE;
}

// ////////////////////////////////////////////////////////////////////////////
// Join Campaign

BOOL joinCampaign(UDWORD gameNumber, char *sPlayer)
{
	PLAYERSTATS	playerStats;

	if(!ingame.localJoiningInProgress)
	{
//		game.type = CAMPAIGN;
		if(!NetPlay.bLobbyLaunched)
		{
			NETjoinGame(gameNumber, sPlayer);	// join
		}
		ingame.localJoiningInProgress	= TRUE;

		loadMultiStats(sPlayer,&playerStats);
		setMultiStats(NetPlay.dpidPlayer,playerStats,FALSE);
		setMultiStats(NetPlay.dpidPlayer,playerStats,TRUE);
		return FALSE;
	}

	bMultiPlayer = TRUE;
	return TRUE;
}

// ////////////////////////////////////////////////////////////////////////////
// Lobby launched. fires the correct routine when the game was lobby launched.
BOOL LobbyLaunched(void)
{
	UDWORD i;
	PLAYERSTATS pl={0};

	// set the player info as soon as possible to avoid screwy scores appearing elsewhere.
	NETplayerInfo();
	NETfindGame();

	for (i = 0; i < MAX_PLAYERS && NetPlay.players[i].dpid != NetPlay.dpidPlayer; i++);

	if(!loadMultiStats(NetPlay.players[i].name, &pl) )
	{
		return FALSE; // cheating was detected, so fail.
	}

	setMultiStats(NetPlay.dpidPlayer, pl, FALSE);
	setMultiStats(NetPlay.dpidPlayer, pl, TRUE);

	// setup text boxes on multiplay screen.
	strcpy((char*) sPlayer, NetPlay.players[i].name);
	strcpy((char*) game.name, NetPlay.games[0].name);

	return TRUE;
}

// ////////////////////////////////////////////////////////////////////////////
// Init and shutdown routines
BOOL lobbyInitialise(void)
{

	if(!NETinit(TRUE))								// initialise, may change guid.
	{
		return FALSE;
	}

	if(NetPlay.bLobbyLaunched) // now check for lobby launching..
	{
		if(!LobbyLaunched())
		{
			return FALSE;
		}
	}
	return TRUE;
}

BOOL multiInitialise(void)
{
	// Perform multiplayer initialization here, on success return TRUE
	return TRUE;
}

// ////////////////////////////////////////////////////////////////////////////
// say goodbye to everyone else
BOOL sendLeavingMsg(void)
{
	NETMSG m;
        UBYTE bHost = (UBYTE)NetPlay.bHost;
	// send a leaving message, This resolves a problem with tcpip which
	// occasionally doesn't automatically notice a player leaving.
	NetAdd(m,0,player2dpid[selectedPlayer]);
	NetAdd(m,4,bHost);
	m.size = 5;
	m.type = NET_LEAVING ;
	NETbcast(&m,TRUE);

	return TRUE;
}

// ////////////////////////////////////////////////////////////////////////////
// called in Init.c to shutdown the whole netgame gubbins.
BOOL multiShutdown(void)
{
	debug(LOG_MAIN, "shutting down audio capture");

	debug(LOG_MAIN, "shutting down audio playback");

	// shut down netplay lib.
	debug(LOG_MAIN, "shutting down networking");
  	NETshutdown();

	debug(LOG_MAIN, "free game data (structure limits)");
	if(ingame.numStructureLimits)
	{
		ingame.numStructureLimits = 0;
		free(ingame.pStructureLimits);
		ingame.pStructureLimits = NULL;
	}

	return TRUE;
}

// ////////////////////////////////////////////////////////////////////////////
// copy tempates from one player to another.


BOOL addTemplate(UDWORD player, DROID_TEMPLATE *psNew)
{
	DROID_TEMPLATE  *psTempl = malloc(sizeof(DROID_TEMPLATE));

	if (psTempl == NULL)
	{
		debug(LOG_ERROR, "addTemplate: Out of memory");
		return FALSE;
	}
	memcpy(psTempl, psNew, sizeof(DROID_TEMPLATE));

	psTempl->pName = (char*)&psTempl->aName;
	strncpy(psTempl->aName, psNew->aName,DROID_MAXNAME);
	psTempl->pName[DROID_MAXNAME-1]=0;


	psTempl->psNext = apsDroidTemplates[player];
	apsDroidTemplates[player] = psTempl;

	return TRUE;
}

BOOL addTemplateSet(UDWORD from,UDWORD to)
{
	DROID_TEMPLATE	*psCurr;

	if(from == to)
	{
		return TRUE;
	}

	for(psCurr = apsDroidTemplates[from];psCurr;psCurr= psCurr->psNext)
	{
		addTemplate(to, psCurr);
	}

	return TRUE;
}

BOOL copyTemplateSet(UDWORD from,UDWORD to)
{
	DROID_TEMPLATE	*psTempl;

	if(from == to)
	{
		return TRUE;
	}

	while(apsDroidTemplates[to])				// clear the old template out.
	{
		psTempl = apsDroidTemplates[to]->psNext;
		free(apsDroidTemplates[to]);
		apsDroidTemplates[to] = psTempl;
	}

	return 	addTemplateSet(from,to);
}

// ////////////////////////////////////////////////////////////////////////////
// setup templates
BOOL multiTemplateSetup(void)
{
	UDWORD player, pcPlayer = 0;

	switch (game.type)
	{
	case CAMPAIGN:
		for(player=0;player<game.maxPlayers;player++)
		{
			copyTemplateSet(CAMPAIGNTEMPLATES,player);
		}
		break;

	case SKIRMISH:
		// create the pc player list in deathmatch set.
		addTemplateSet(CAMPAIGNTEMPLATES,DEATHMATCHTEMPLATES);
		addTemplateSet(6,DEATHMATCHTEMPLATES);
		addTemplateSet(2,DEATHMATCHTEMPLATES);

		//choose which way to do this.
		if(isHumanPlayer(CAMPAIGNTEMPLATES))
		{
			//pc first
			for(player=0;player<game.maxPlayers;player++)
			{
				if(!isHumanPlayer(player))
				{
					copyTemplateSet(DEATHMATCHTEMPLATES,player);
				}
			}
			//now players.
			for(player=0;player<game.maxPlayers;player++)
			{
				if(isHumanPlayer(player))
				{
					copyTemplateSet(CAMPAIGNTEMPLATES,player);
				}
			}
		}
		else
		{
			// ensure a copy of pc templates to a pc player.
			if(isHumanPlayer(DEATHMATCHTEMPLATES))
			{

				for(player=0;player<MAX_PLAYERS && isHumanPlayer(player);player++);
				if(!isHumanPlayer(player))
				{
					pcPlayer = player;
					copyTemplateSet(DEATHMATCHTEMPLATES,pcPlayer);
				}
			}
			else
			{
				pcPlayer = DEATHMATCHTEMPLATES;
			}
			//players first
			for(player=0;player<game.maxPlayers;player++)
			{
				if(isHumanPlayer(player))
				{
					copyTemplateSet(CAMPAIGNTEMPLATES,player);
				}
			}
			//now pc
			for(player=0;player<game.maxPlayers;player++)
			{
				if(!isHumanPlayer(player))
				{
					copyTemplateSet(pcPlayer,player);
				}
			}
		}
		break;

	default:
		break;
	}

	return TRUE;
}



// ////////////////////////////////////////////////////////////////////////////
// remove structures from map before campaign play.
static BOOL cleanMap(UDWORD player)
{
	DROID		*psD,*psD2;
	STRUCTURE	*psStruct;
	BOOL		firstFact,firstRes;

	bMultiPlayer = FALSE;

	firstFact = TRUE;
	firstRes = TRUE;

	// reverse so we always remove the last object. re-reverse afterwards.
//	reverseObjectList((BASE_OBJECT**)&apsStructLists[player]);


	switch(game.base)
	{
	case CAMP_CLEAN:									//clean map
		while(apsStructLists[player])					//strip away structures.
		{
			removeStruct(apsStructLists[player], TRUE);
		}
		psD = apsDroidLists[player];					// remove all but construction droids.
		while(psD)
		{
			psD2=psD->psNext;
			//if(psD->droidType != DROID_CONSTRUCT)
            if (!(psD->droidType == DROID_CONSTRUCT ||
                psD->droidType == DROID_CYBORG_CONSTRUCT))
			{
				killDroid(psD);
			}
			psD = psD2;
		}
		break;

	case CAMP_BASE:												//just structs, no walls
		psStruct = apsStructLists[player];
		while(psStruct)
		{
			if ( (psStruct->pStructureType->type == REF_WALL)
			   ||(psStruct->pStructureType->type == REF_WALLCORNER)
			   ||(psStruct->pStructureType->type == REF_DEFENSE)
			   ||(psStruct->pStructureType->type == REF_BLASTDOOR)
			   ||(psStruct->pStructureType->type == REF_CYBORG_FACTORY)
			   ||(psStruct->pStructureType->type == REF_COMMAND_CONTROL)
			   )
			{
				removeStruct(psStruct, TRUE);
				psStruct= apsStructLists[player];			//restart,(list may have changed).
			}

			else if( (psStruct->pStructureType->type == REF_FACTORY)
				   ||(psStruct->pStructureType->type == REF_RESEARCH)
				   ||(psStruct->pStructureType->type == REF_POWER_GEN))
			{
				if(psStruct->pStructureType->type == REF_FACTORY )
				{
					if(firstFact == TRUE)
					{
						firstFact = FALSE;
						removeStruct(psStruct, TRUE);
						psStruct= apsStructLists[player];
					}
					else	// don't delete, just rejig!
					{
						if(((FACTORY*)psStruct->pFunctionality)->capacity != 0)
						{
							((FACTORY*)psStruct->pFunctionality)->capacity = 0;
							((FACTORY*)psStruct->pFunctionality)->productionOutput = (UBYTE)((PRODUCTION_FUNCTION*)psStruct->pStructureType->asFuncList[0])->productionOutput;

							psStruct->sDisplay.imd	= psStruct->pStructureType->pIMD;
							psStruct->body			= (UWORD)(structureBody(psStruct));

						}
						psStruct				= psStruct->psNext;
					}
				}
				else if(psStruct->pStructureType->type == REF_RESEARCH)
				{
					if(firstRes == TRUE)
					{
						firstRes = FALSE;
						removeStruct(psStruct, TRUE);
						psStruct= apsStructLists[player];
					}
					else
					{
						if(((RESEARCH_FACILITY*)psStruct->pFunctionality)->capacity != 0)
						{	// downgrade research
							((RESEARCH_FACILITY*)psStruct->pFunctionality)->capacity = 0;
							((RESEARCH_FACILITY*)psStruct->pFunctionality)->researchPoints = ((RESEARCH_FUNCTION*)psStruct->pStructureType->asFuncList[0])->researchPoints;
							psStruct->sDisplay.imd	= psStruct->pStructureType->pIMD;
							psStruct->body			= (UWORD)(structureBody(psStruct));
						}
						psStruct=psStruct->psNext;
					}
				}
				else if(psStruct->pStructureType->type == REF_POWER_GEN)
				{
						if(((POWER_GEN*)psStruct->pFunctionality)->capacity != 0)
						{	// downgrade powergen.
							((POWER_GEN*)psStruct->pFunctionality)->capacity = 0;
							((POWER_GEN*)psStruct->pFunctionality)->power = ((POWER_GEN_FUNCTION*)psStruct->pStructureType->asFuncList[0])->powerOutput;
							((POWER_GEN*)psStruct->pFunctionality)->multiplier += ((POWER_GEN_FUNCTION*)psStruct->pStructureType->asFuncList[0])->powerMultiplier;

							psStruct->sDisplay.imd	= psStruct->pStructureType->pIMD;
							psStruct->body			= (UWORD)(structureBody(psStruct));
						}
						psStruct=psStruct->psNext;
				}
			}

			else
			{
				psStruct=psStruct->psNext;
			}
		}
		break;


	case CAMP_WALLS:												//everything.
		break;
	default:
		debug( LOG_ERROR, "Unknown Campaign Style" );
		abort();
		break;
	}

	// rerev list to get back to normal.
//	reverseObjectList((BASE_OBJECT**)&apsStructLists[player]);

	bMultiPlayer = TRUE;
	return TRUE;
}

// ////////////////////////////////////////////////////////////////////////////
// setup a campaign game
static BOOL campInit(void)
{
	UDWORD			player;
	UBYTE		newPlayerArray[MAX_PLAYERS];
	UDWORD		i,j,lastAI;
	SDWORD		newPlayerTeam[MAX_PLAYERS] = {-1,-1,-1,-1,-1,-1,-1,-1};

// if this is from a savegame, stop here!
	if((getSaveGameType() == GTYPE_SAVE_START)
	|| (getSaveGameType() == GTYPE_SAVE_MIDMISSION)	)
	{
		// these two lines are the biggest hack in the world.
		// the reticule seems to get detached from 'reticuleup'
		// this forces it back in sync...
		intRemoveReticule();
		intAddReticule();

		return TRUE;
	}

	//Convert skirmish GUI player ids to in-game ids
	if(game.type == SKIRMISH)
	{
		lastAI = 0;		//last used AI slot
		memset(newPlayerArray,1,MAX_PLAYERS * sizeof(newPlayerArray[0]));		//'1' for humans
		for(i=0;i<MAX_PLAYERS;i++)
		{
			if(game.skDiff[i] < UBYTE_MAX )		//slot with enabled or disabled AI
			{
				//find first unused slot
				for(j=lastAI;j<MAX_PLAYERS && isHumanPlayer(j);j++);	//skip humans

				ASSERT(j<MAX_PLAYERS,"campInit: couldn't find free slot while assigning AI %d , lastAI=%d", i, lastAI);

				newPlayerArray[j] = game.skDiff[i];		//copy over
				newPlayerTeam[j] = playerTeamGUI[i];

				//remove player if it was disabled in menus
				if(game.skDiff[i] == 0)
					clearPlayer(j,TRUE,FALSE);

				lastAI = j;
				lastAI++;
			}
			else if(game.skDiff[i] == UBYTE_MAX)	//human player
			{
				//find player net id
				for(j=0;(j < MAX_PLAYERS) && (player2dpid[j] != NetPlay.players[i].dpid);j++);

				ASSERT(j<MAX_PLAYERS,"campInit: couldn't find player id for GUI id %d", i);

				newPlayerTeam[j] = playerTeamGUI[i];
			}

		}

		memcpy(game.skDiff,newPlayerArray,MAX_PLAYERS  * sizeof(newPlayerArray[0]));
		memcpy(playerTeam,newPlayerTeam,MAX_PLAYERS * sizeof(newPlayerTeam[0]));
	}

	for(player = 0;player<game.maxPlayers;player++)			// clean up only to the player limit for this map..
	{
		if( (!isHumanPlayer(player)) && game.type != SKIRMISH)	// strip away unused players
		{
			clearPlayer(player,TRUE,TRUE);
		}

		cleanMap(player);
	}

	// optionally remove other computer players.
	// HACK: if actual number of players is 8, then drop baba player to avoid
	// exceeding player number (babas need a player, too!) - Per
	if ((game.type == CAMPAIGN && game.maxPlayers < 8) || game.type == SKIRMISH)
	{
		for(player=game.maxPlayers;player<MAX_PLAYERS;player++)
		{
			clearPlayer(player,TRUE,FALSE);
		}
	}

	// add free research gifts..
	if(NetPlay.bHost)
	{
		addOilDrum( NetPlay.playercount*2 );		// add some free power.
	}

	playerResponding();			// say howdy!

	return TRUE;
}

// ////////////////////////////////////////////////////////////////////////////
// say hi to everyone else....
void playerResponding(void)
{
	NETMSG	msg;

	ingame.startTime = gameTime;
	ingame.localJoiningInProgress = FALSE;				// no longer joining.
	ingame.JoiningInProgress[selectedPlayer] = FALSE;

	cameraToHome(selectedPlayer,FALSE);						// home the camera to the player.

	NetAdd(msg,0,selectedPlayer);						// tell the world we're here.
	msg.size = sizeof(UDWORD);
	msg.type = NET_PLAYERRESPONDING;
	NETbcast(&msg,TRUE);
}

// ////////////////////////////////////////////////////////////////////////////
//called when the game finally gets fired up.
BOOL multiGameInit(void)
{
	UDWORD player;

	for(player=0;player<MAX_PLAYERS;player++)
	{
		openchannels[player] =TRUE;								//open comms to this player.
	}

	campInit();

	InitializeAIExperience();
	msgStackReset();	//for multiplayer msgs, reset message stack


	return TRUE;
}

////////////////////////////////
// at the end of every game.
BOOL multiGameShutdown(void)
{
	PLAYERSTATS	st;

	sendLeavingMsg();							// say goodbye
	updateMultiStatsGames();					// update games played.

	st = getMultiStats(selectedPlayer,TRUE);	// save stats

	saveMultiStats(getPlayerName(selectedPlayer),getPlayerName(selectedPlayer),&st);

	NETclose();									// close game.

	if(ingame.numStructureLimits)
	{
		ingame.numStructureLimits = 0;
		free(ingame.pStructureLimits);
		ingame.pStructureLimits = NULL;
	}

	ingame.localJoiningInProgress   = FALSE;	// clean up
	ingame.localOptionsReceived		= FALSE;
	ingame.bHostSetup				= FALSE;	//dont attempt a host
	NetPlay.bLobbyLaunched			= FALSE;	//revert back to main menu, not multioptions.
	NetPlay.bHost					= FALSE;
	bMultiPlayer					= FALSE;	//back to single player mode
	selectedPlayer					= 0;		//back to use player 0 (single player friendly)

	return TRUE;
}
