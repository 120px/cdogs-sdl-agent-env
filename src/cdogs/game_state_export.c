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

json_t *GameStateToJSON(const int tick)
{
	json_t *root = json_new_object();

	json_t *actorArr = json_new_array();
	CA_FOREACH(const TActor, a, gActors)
		if (!a->isInUse)
			continue;
		json_t *actor = json_new_object();
		AddIntPair(actor, "uid", a->uid);
		AddIntPair(actor, "health", a->health);
		// add more fields as needed
		json_insert_child(actorArr, actor);
	CA_FOREACH_END()
	json_insert_pair_into_object(root, "actors", actorArr);
	AddIntPair(root, "tick", tick);

	WriteSnapshotToJsonFile(root);

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