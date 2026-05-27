#include "game_state_export.h"

#include <direct.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include "actors.h"
#include "gamedata.h"
#include "json_utils.h"
#include "mission.h"
#include "player.h"
#include "log.h"
#include "tcp_server.h"

static const char *OUTPUT_PATH = "live_game_snapshots";

static void SanitizeFilename(char *dst, const char *src, size_t maxLen)
{
	size_t i = 0;
	for (; *src && i < maxLen - 1; src++)
	{
		if (isalnum((unsigned char)*src) || *src == '_' || *src == '-')
		{
			dst[i++] = *src;
		}
		else if (*src == ' ')
		{
			dst[i++] = '_';
		}
	}
	dst[i] = '\0';
}

void EnsureOutputDir(void)
{
	_mkdir(OUTPUT_PATH);
}

// Death event log: tracks which actor UIDs were alive last tick
#define MAX_TRACKED_ACTORS 256
typedef struct { int uid; bool wasAlive; } ActorAliveState;
static ActorAliveState sAliveStates[MAX_TRACKED_ACTORS];
static int sAliveStateCount = 0;

void ResetDeathLog(void)
{
	sAliveStateCount = 0;
}

static bool WasAliveLastTick(const int uid)
{
	for (int i = 0; i < sAliveStateCount; i++)
		if (sAliveStates[i].uid == uid)
			return sAliveStates[i].wasAlive;
	return true; // assume alive if not tracked yet
}

static void UpdateAliveState(const int uid, const bool isAlive)
{
	for (int i = 0; i < sAliveStateCount; i++)
	{
		if (sAliveStates[i].uid == uid)
		{
			sAliveStates[i].wasAlive = isAlive;
			return;
		}
	}
	if (sAliveStateCount < MAX_TRACKED_ACTORS)
	{
		sAliveStates[sAliveStateCount].uid = uid;
		sAliveStates[sAliveStateCount].wasAlive = isAlive;
		sAliveStateCount++;
	}
}

json_t *MapGridToJson(void)
{
	json_t *root = json_new_object();
	AddStringPair(root, "type", "map_init");
	AddIntPair(root, "width", gMap.Size.x);
	AddIntPair(root, "height", gMap.Size.y);
	AddIntPair(root, "tile_height", TILE_HEIGHT);
	AddIntPair(root, "tile_width", TILE_WIDTH);
	AddIntPair(root, "num_explorable_tiles", gMap.NumExplorableTiles);

	json_t *rows = json_new_array();

	for (int y = 0; y < gMap.Size.y; y++)
	{
		json_t *row = json_new_array();
		for (int x = 0; x < gMap.Size.x; x++)
		{
			const Tile *t = MapGetTile(&gMap, svec2i(x, y));
			const char *type = "unknown";
			if (t->Class->Type == TILE_CLASS_WALL)
				type = "wall";
			else if (t->Class->Type == TILE_CLASS_FLOOR)
				type = t->Class->IsRoom ? "room" : "floor";
			else if (t->Class->Type == TILE_CLASS_DOOR)
				type = "door";
			json_insert_child(row, json_new_string(type));
		}
		json_insert_child(rows, row);
	}
	// Print map grid for debugging
	for (int y = 0; y < gMap.Size.y; y++)
	{
		for (int x = 0; x < gMap.Size.x; x++)
		{
			const Tile *t = MapGetTile(&gMap, svec2i(x, y));
			char c = '?';
			switch (t->Class->Type)
			{
			case TILE_CLASS_WALL:  c = '#'; break;
			case TILE_CLASS_DOOR:  c = '+'; break;
			case TILE_CLASS_FLOOR: c = t->Class->IsRoom ? '.' : ' '; break;
			default: break;
			}
			putchar(c);
		}
		putchar('\n');
	}

	json_insert_pair_into_object(root, "tiles", rows);
	TcpServerSendJSON(root);
	return root;
}

json_t *GameStateToJSON(const int tick)
{
	json_t *root = json_new_object();
	AddStringPair(root, "type", "game_state");

	json_t *actorArr = json_new_array();
	json_t *deathArr = json_new_array();
	CA_FOREACH(const TActor, a, gActors)
		if (!a->isInUse)
			continue;
		const bool isAlive = a->dead == 0;
		const bool justDied = WasAliveLastTick(a->uid) && !isAlive;
		UpdateAliveState(a->uid, isAlive);
		if (justDied)
		{
			json_t *death = json_new_object();
			AddIntPair(death, "uid", a->uid);
			AddIntPair(death, "PlayerUID", a->PlayerUID);
			if (a->PlayerUID != -1)
			{
				const PlayerData *p = PlayerDataGetByUID(a->PlayerUID);
				if (p) AddStringPair(death, "playerName", p->name);
			}
			json_insert_child(deathArr, death);
		}
		json_t *actor = json_new_object();
		// JSON: Identification
		AddIntPair(actor, "uid", a->uid);
		AddIntPair(actor, "charId", a->charId);
		AddIntPair(actor, "PlayerUID", a->PlayerUID);
		AddIntPair(actor, "pilotUID", a->pilotUID);
		// JSON: Player stats
		AddIntPair(actor, "health", a->health);
		AddIntPair(actor, "dead", a->dead);
		
		//JSON: Guns
		json_t *gunsArr = json_new_array();
		for (int gi = 0; gi < MAX_WEAPONS; gi++)
		{
			const Weapon *w = &a->guns[gi];
			if (w->Gun == NULL)
				continue;
			json_t *gun = json_new_object();
			AddStringPair(gun, "name", w->Gun->name);
			json_insert_child(gunsArr, gun);
		}
		json_insert_pair_into_object(actor, "guns", gunsArr);
		AddIntArray(actor, "ammo", &a->ammo);
		AddIntPair(actor, "gun_current", a->gunIndex);
		AddIntPair(actor, "gun_last", a->lastGunIdx);

		// JSON: Position and movement
		AddVec2Pair(actor, "pos", a->Pos);
		AddVec2Pair(actor, "move_vel", a->MoveVel);
		AddIntPair(actor, "direction", a->direction);
		AddFloatPair(actor, "direction_radians", a->DrawRadians);

		// JSON: Status effects
		AddIntPair(actor, "flamed", a->flamed);
		AddIntPair(actor, "poisoned", a->poisoned);
		AddIntPair(actor, "petrified", a->petrified);
		AddIntPair(actor, "confused", a->confused);

		AddIntPair(actor, "flags", a->flags);
		AddIntPair(actor, "accumulated_damage", a->accumulatedDamage);
		AddIntPair(actor, "damage_cooldown_ticks", a->damageCooldownTicks);

		// JSON: PlayerData (human players only)
		if (a->PlayerUID != -1)
		{
			const PlayerData *p = PlayerDataGetByUID(a->PlayerUID);
			if (p)
			{
				json_t *player = json_new_object();
				AddStringPair(player, "name", p->name);
				AddIntPair(player, "lives", p->Lives);
				AddIntPair(player, "score", p->Stats.Score);
				AddIntPair(player, "kills", (int)p->Stats.Kills);
				AddIntPair(player, "suicides", (int)p->Stats.Suicides);
				AddIntPair(player, "friendlies", (int)p->Stats.Friendlies);
				json_insert_pair_into_object(actor, "player", player);
			}
		}

		// add more fields as needed
		//LOG(LM_MAIN, LL_INFO, "actor uid=%d pos=(%.1f,%.1f) dir=%d rads=%.1f",
		//	a->uid, a->Pos.x, a->Pos.y, (int)a->direction, a->DrawRadians);
		json_insert_child(actorArr, actor);
	CA_FOREACH_END()
	json_insert_pair_into_object(root, "actors", actorArr);
	json_insert_pair_into_object(root, "deaths", deathArr);
	AddIntPair(root, "tick", tick);

	//WriteSnapshotToJsonFile(root);
	TcpServerSendJSON(root);

	return root;
}

void WriteSnapshotToJsonFile(json_t *root)
{
	// Build date string: YYYYMMDD
	char dateBuf[16];
	time_t now = time(NULL);
	struct tm *t = localtime(&now);
	strftime(dateBuf, sizeof dateBuf, "%Y%m%d", t);

	// Sanitize map title for use in a filename
	char mapBuf[64] = "unnamed";
	if (gMission.missionData && gMission.missionData->Title)
	{
		SanitizeFilename(mapBuf, gMission.missionData->Title, sizeof mapBuf);
	}

	// Compose filename: live_game_snapshots/YYYYMMDD_mapname.json
	char filename[192];
	snprintf(filename, sizeof filename, "%s/%s_%s.json", OUTPUT_PATH, dateBuf, mapBuf);

	FILE *fp = fopen(filename, "a");
	if (!fp)
	{
		perror("Failed to open output file");
		return;
	}
	json_stream_output(fp, root);
	fputc('\n', fp);
	fclose(fp);
}