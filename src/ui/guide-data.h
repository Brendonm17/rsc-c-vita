#ifndef _H_GUIDE_DATA
#define _H_GUIDE_DATA

// skill/quest guide tables, see guide-data.c

typedef struct GuideRow {
    unsigned char is_npc;   // 0 = item sprite, 1 = npc
    short id;
    const char *level;
    const char *detail;
    unsigned char custom_only; // needs custom sprites (config 52)
} GuideRow;

typedef struct GuideTab {
    const char *name;
    short row_start;
    short row_count;
    unsigned char custom_only;
} GuideTab;

typedef struct GuideSkill {
    const char *skill;
    short tab_start;
    short tab_count;
} GuideSkill;

extern const GuideRow guide_rows[];
extern const GuideTab guide_tabs[];
extern const GuideSkill guide_skills[];
extern const int guide_skill_count;

extern const char *quest_guide_whos[];
extern const char *quest_guide_wheres[];
extern const char *quest_guide_requirements[];
extern const short quest_guide_requirements_index[][2];
extern const char *quest_guide_rewards[];
extern const short quest_guide_rewards_index[][2];
extern const int quest_guide_count;

#endif
