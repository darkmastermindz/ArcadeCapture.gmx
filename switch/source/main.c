// Arcade Capture - Nintendo Switch (homebrew) port, written against SDL2.
// The same source also builds on desktop (see Makefile.pc) for testing.
//
// Two-player local capture-the-flag: Blue vs Red. Grab the enemy flag and
// bring it back to your base while your own flag is home. First to 3 wins.

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>

#ifdef __SWITCH__
#include <switch.h>
#define ASSET_DIR "romfs:/"
#else
#ifndef ASSET_DIR
#define ASSET_DIR "../"
#endif
#endif

// ---------------------------------------------------------------------------
// Constants (kept in sync with html5/game.js)
// ---------------------------------------------------------------------------
#define ROOM_W 1024
#define ROOM_H 768
#define BORDER 20
#define TILE 32

#define PLAYER_SPEED 260.0f
#define CARRIER_SPEED 220.0f
#define PLAYER_HALF 20.0f
#define MAX_HP 5
#define FIRE_COOLDOWN 0.3f
#define BULLET_SPEED 620.0f
#define BULLET_RADIUS 6.0f
#define RESPAWN_TIME 2.0f
#define INVULN_TIME 1.5f
#define FLAG_SCALE 0.55f
#define FLAG_HALF 30.0f
#define BASE_HALF 57.0f
#define WIN_SCORE 3

#define STICK_DEADZONE 0.25f
#define MAX_BULLETS 64
#define MAX_EFFECTS 256
#define MAX_WALLS 16

enum { BLUE = 0, RED = 1 };

// ---------------------------------------------------------------------------
// Assets
// ---------------------------------------------------------------------------
typedef struct {
    const char *name;
    const char *path;
    SDL_Texture *tex;
    int w, h;
} Sprite;

static Sprite sprites[] = {
    {"spr_blue_0", "sprites/images/spr_blue_0.png"},
    {"spr_blue_1", "sprites/images/spr_blue_1.png"},
    {"spr_blue_2", "sprites/images/spr_blue_2.png"},
    {"spr_red_0", "sprites/images/spr_red_0.png"},
    {"spr_red_1", "sprites/images/spr_red_1.png"},
    {"spr_bluegoal_0", "sprites/images/spr_bluegoal_0.png"},
    {"spr_bluegoal_1", "sprites/images/spr_bluegoal_1.png"},
    {"spr_bulletb_0", "sprites/images/spr_bulletb_0.png"},
    {"spr_bulletb_1", "sprites/images/spr_bulletb_1.png"},
    {"spr_bulletr_0", "sprites/images/spr_bulletr_0.png"},
    {"spr_bulletr_1", "sprites/images/spr_bulletr_1.png"},
    {"spr_flagb_0", "sprites/images/spr_flagb_0.png"},
    {"spr_flagr_0", "sprites/images/spr_flagr_0.png"},
    {"spr_healthb_0", "sprites/images/spr_healthb_0.png"},
    {"spr_healthb_1", "sprites/images/spr_healthb_1.png"},
    {"spr_healthb_2", "sprites/images/spr_healthb_2.png"},
    {"spr_healthb_3", "sprites/images/spr_healthb_3.png"},
    {"spr_healthb_4", "sprites/images/spr_healthb_4.png"},
    {"spr_healthr_0", "sprites/images/spr_healthr_0.png"},
    {"spr_healthr_1", "sprites/images/spr_healthr_1.png"},
    {"spr_healthr_2", "sprites/images/spr_healthr_2.png"},
    {"spr_healthr_3", "sprites/images/spr_healthr_3.png"},
    {"spr_healthr_4", "sprites/images/spr_healthr_4.png"},
    {"spr_wall_0", "sprites/images/spr_wall_0.png"},
    {"bkg_title", "background/images/bkg_title.png"},
    {"bkg_play", "background/images/bkg_play.png"},
};
#define SPRITE_COUNT ((int)(sizeof(sprites) / sizeof(sprites[0])))

static SDL_Renderer *renderer;
static Mix_Music *mus_intro, *mus_main, *current_music;
static bool muted;

static Sprite *spr(const char *name) {
    for (int i = 0; i < SPRITE_COUNT; i++)
        if (strcmp(sprites[i].name, name) == 0) return &sprites[i];
    return NULL;
}

static Sprite *sprf(const char *prefix, int frame) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%s_%d", prefix, frame);
    return spr(buf);
}

static bool load_assets(void) {
    char path[256];
    for (int i = 0; i < SPRITE_COUNT; i++) {
        snprintf(path, sizeof(path), "%s%s", ASSET_DIR, sprites[i].path);
        sprites[i].tex = IMG_LoadTexture(renderer, path);
        if (!sprites[i].tex) {
            SDL_Log("Failed to load %s: %s", path, IMG_GetError());
            return false;
        }
        SDL_SetTextureBlendMode(sprites[i].tex, SDL_BLENDMODE_BLEND);
        SDL_QueryTexture(sprites[i].tex, NULL, NULL, &sprites[i].w, &sprites[i].h);
    }
    snprintf(path, sizeof(path), "%ssound/audio/snd_intro.mp3", ASSET_DIR);
    mus_intro = Mix_LoadMUS(path);
    snprintf(path, sizeof(path), "%ssound/audio/snd_main.mp3", ASSET_DIR);
    mus_main = Mix_LoadMUS(path);
    if (!mus_intro || !mus_main) SDL_Log("Music unavailable: %s", Mix_GetError());
    return true;
}

// Plays one track and stops the other (what obj_music was meant to do).
static void play_music(Mix_Music *m) {
    if (!m || m == current_music) return;
    current_music = m;
    Mix_PlayMusic(m, -1);
    Mix_VolumeMusic(muted ? 0 : MIX_MAX_VOLUME * 6 / 10);
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
typedef struct { Uint8 r, g, b; } Color;
static const Color GREEN = {0, 255, 0};
static const Color DIM_GREEN = {0, 170, 0};
static const Color TEAM_COLOR[2] = {{34, 230, 255}, {255, 48, 48}};

// Draws a sprite centred on (x, y). Negative scale mirrors.
static void draw_ex(Sprite *s, float x, float y, float sx, float sy, double angle, Uint8 alpha) {
    if (!s || !s->tex) return;
    SDL_RendererFlip flip = SDL_FLIP_NONE;
    if (sx < 0) { flip |= SDL_FLIP_HORIZONTAL; sx = -sx; }
    if (sy < 0) { flip |= SDL_FLIP_VERTICAL; sy = -sy; }
    SDL_FRect dst = {x - s->w * sx / 2, y - s->h * sy / 2, s->w * sx, s->h * sy};
    SDL_SetTextureAlphaMod(s->tex, alpha);
    SDL_RenderCopyExF(renderer, s->tex, NULL, &dst, angle, NULL, flip);
}

static void draw(Sprite *s, float x, float y, float scale) {
    draw_ex(s, x, y, scale, scale, 0, 255);
}

static void fill_rect(float x, float y, float w, float h, Color c, Uint8 a) {
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, a);
    SDL_FRect r = {x, y, w, h};
    SDL_RenderFillRectF(renderer, &r);
}

// 5x7 bitmap font so the port needs no font files.
static const struct { char ch; Uint8 rows[7]; } FONT[] = {
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}}, {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}}, {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}}, {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}}, {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}}, {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}}, {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}}, {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}}, {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}}, {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}}, {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}}, {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}}, {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}}, {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}}, {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}}, {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}}, {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}}, {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}}, {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}}, {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}}, {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}}, {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}}, {'\'', {0x0C, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}},
};

static const Uint8 *glyph(char c) {
    if (c >= 'a' && c <= 'z') c -= 32;
    for (size_t i = 0; i < sizeof(FONT) / sizeof(FONT[0]); i++)
        if (FONT[i].ch == c) return FONT[i].rows;
    return NULL;
}

typedef enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT } Align;

// Draws text vertically centred on y. `px` is the size of one font pixel.
static void text(const char *s, float x, float y, float px, Color c, Uint8 alpha, Align align) {
    float w = strlen(s) * 6 * px - px;
    if (align == ALIGN_CENTER) x -= w / 2;
    else if (align == ALIGN_RIGHT) x -= w;
    y -= 3.5f * px;
    for (; *s; s++, x += 6 * px) {
        const Uint8 *g = glyph(*s);
        if (!g) continue;
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (g[row] & (0x10 >> col)) fill_rect(x + col * px, y + row * px, px, px, c, alpha);
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
typedef struct {
    float mx, my;  // movement, -1..1
    bool fire;
} PlayerInput;

typedef struct {
    PlayerInput p[2];
    bool confirm, pause, back, quit, mute, setup;
    bool two_pads;  // Switch: each player has their own controller
} Input;

#ifdef __SWITCH__
static PadState pads[2];

static void input_init(void) {
    // Single Joy-Cons are held sideways, one per player.
    hidSetNpadJoyHoldType(HidNpadJoyHoldType_Horizontal);
    padConfigureInput(2, HidNpadStyleSet_NpadStandard);
    padInitialize(&pads[0], HidNpadIdType_No1, HidNpadIdType_Handheld);
    padInitialize(&pads[1], HidNpadIdType_No2);
}

static void stick_or_dpad(PadState *pad, u64 held, bool use_dpad, float *mx, float *my) {
    // Use whichever stick is pushed further; a single right Joy-Con only has stick 1.
    HidAnalogStickState l = padGetStickPos(pad, 0), r = padGetStickPos(pad, 1);
    HidAnalogStickState s = (abs(l.x) + abs(l.y) >= abs(r.x) + abs(r.y)) ? l : r;
    *mx = s.x / (float)JOYSTICK_MAX;
    *my = -s.y / (float)JOYSTICK_MAX;
    if (use_dpad) {
        if (held & HidNpadButton_Left) *mx = -1;
        if (held & HidNpadButton_Right) *mx = 1;
        if (held & HidNpadButton_Up) *my = -1;
        if (held & HidNpadButton_Down) *my = 1;
    }
}

// Opens the system controller applet so players can pair/split Joy-Cons.
static void controller_setup(void) {
    HidLaControllerSupportArg arg;
    hidLaCreateControllerSupportArg(&arg);
    arg.hdr.player_count_min = 1;
    arg.hdr.player_count_max = 2;
    arg.hdr.enable_single_mode = true;
    hidLaShowControllerSupport(NULL, &arg);
}

static void read_input(Input *in) {
    memset(in, 0, sizeof(*in));
    padUpdate(&pads[0]);
    padUpdate(&pads[1]);
    u64 held0 = padGetButtons(&pads[0]), down0 = padGetButtonsDown(&pads[0]);
    u64 held1 = padGetButtons(&pads[1]), down1 = padGetButtonsDown(&pads[1]);
    u64 down = down0 | down1;
    in->two_pads = padIsConnected(&pads[1]);

    const u64 FIRE = HidNpadButton_A | HidNpadButton_B | HidNpadButton_X | HidNpadButton_Y |
                     HidNpadButton_L | HidNpadButton_R | HidNpadButton_ZL | HidNpadButton_ZR |
                     HidNpadButton_AnySL | HidNpadButton_AnySR;

    if (in->two_pads) {
        // One controller each (e.g. two sideways Joy-Cons).
        for (int i = 0; i < 2; i++) {
            PadState *pad = &pads[i];
            u64 held = i ? held1 : held0;
            u32 style = padGetStyleSet(pad);
            bool single_left = (style & HidNpadStyleTag_NpadJoyLeft) && !(style & HidNpadStyleTag_NpadJoyDual);
            // A sideways left Joy-Con uses its arrow buttons as face buttons.
            stick_or_dpad(pad, held, !single_left, &in->p[i].mx, &in->p[i].my);
            in->p[i].fire = (held & FIRE) || (single_left && (held & (HidNpadButton_Left | HidNpadButton_Right |
                                                                       HidNpadButton_Up | HidNpadButton_Down)));
        }
    } else {
        // Shared controller: Blue = left stick/d-pad + L/ZL, Red = right stick/ABXY + R/ZR.
        HidAnalogStickState l = padGetStickPos(&pads[0], 0), r = padGetStickPos(&pads[0], 1);
        in->p[BLUE].mx = l.x / (float)JOYSTICK_MAX;
        in->p[BLUE].my = -l.y / (float)JOYSTICK_MAX;
        if (held0 & HidNpadButton_Left) in->p[BLUE].mx = -1;
        if (held0 & HidNpadButton_Right) in->p[BLUE].mx = 1;
        if (held0 & HidNpadButton_Up) in->p[BLUE].my = -1;
        if (held0 & HidNpadButton_Down) in->p[BLUE].my = 1;
        in->p[BLUE].fire = held0 & (HidNpadButton_L | HidNpadButton_ZL);

        in->p[RED].mx = r.x / (float)JOYSTICK_MAX;
        in->p[RED].my = -r.y / (float)JOYSTICK_MAX;
        if (held0 & HidNpadButton_Y) in->p[RED].mx = -1;
        if (held0 & HidNpadButton_A) in->p[RED].mx = 1;
        if (held0 & HidNpadButton_X) in->p[RED].my = -1;
        if (held0 & HidNpadButton_B) in->p[RED].my = 1;
        in->p[RED].fire = held0 & (HidNpadButton_R | HidNpadButton_ZR);
    }

    in->confirm = down & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_X | HidNpadButton_Y |
                          HidNpadButton_AnySL | HidNpadButton_AnySR);
    in->pause = down & HidNpadButton_Plus;
    in->back = down & HidNpadButton_Minus;
    in->setup = down & HidNpadButton_Minus;
    in->quit = false;
    in->mute = false;
}
#else
static const Uint8 *kb;
static Uint8 prev_kb[SDL_NUM_SCANCODES];

static void input_init(void) {}

static bool pressed(SDL_Scancode sc) { return kb[sc] && !prev_kb[sc]; }

static void read_input(Input *in) {
    memset(in, 0, sizeof(*in));
    kb = SDL_GetKeyboardState(NULL);
    in->p[BLUE].mx = (kb[SDL_SCANCODE_D] ? 1 : 0) - (kb[SDL_SCANCODE_A] ? 1 : 0);
    in->p[BLUE].my = (kb[SDL_SCANCODE_S] ? 1 : 0) - (kb[SDL_SCANCODE_W] ? 1 : 0);
    in->p[BLUE].fire = kb[SDL_SCANCODE_SPACE] || kb[SDL_SCANCODE_F];
    in->p[RED].mx = (kb[SDL_SCANCODE_RIGHT] ? 1 : 0) - (kb[SDL_SCANCODE_LEFT] ? 1 : 0);
    in->p[RED].my = (kb[SDL_SCANCODE_DOWN] ? 1 : 0) - (kb[SDL_SCANCODE_UP] ? 1 : 0);
    in->p[RED].fire = kb[SDL_SCANCODE_RETURN] || kb[SDL_SCANCODE_KP_ENTER] || kb[SDL_SCANCODE_SLASH];
    in->confirm = pressed(SDL_SCANCODE_RETURN) || pressed(SDL_SCANCODE_KP_ENTER);
    in->pause = pressed(SDL_SCANCODE_P) || pressed(SDL_SCANCODE_ESCAPE);
    in->back = pressed(SDL_SCANCODE_Q);
    in->quit = false;
    in->mute = pressed(SDL_SCANCODE_M);
    memcpy(prev_kb, kb, SDL_NUM_SCANCODES);
}
#endif

// ---------------------------------------------------------------------------
// World
// ---------------------------------------------------------------------------
typedef struct { float x, y, w, h; } Rect;

typedef struct Player {
    int team, enemy;
    float spawn_x, spawn_y, x, y, dir_x, dir_y;
    int hp, score;
    float cooldown, respawn, invuln;
    bool dead;
} Player;

typedef struct {
    int team;
    float home_x, home_y, x, y;
    Player *carrier;
} Flag;

typedef struct { int team; float x, y, vx, vy, angle; } Bullet;
typedef struct { int team; float x, y, vx, vy, life, max; } Effect;

static const float BASE_X[2] = {90, ROOM_W - 90};
static const float BASE_Y = ROOM_H / 2;

static Rect walls[MAX_WALLS];
static int wall_count;
static Player players[2];
static Flag flags[2];
static Bullet bullets[MAX_BULLETS];
static int bullet_count;
static Effect effects[MAX_EFFECTS];
static int effect_count;
static int winner = -1;

static char banner_text[48];
static Color banner_color;
static float banner_t;

typedef enum { ST_TITLE, ST_PLAY, ST_PAUSED, ST_OVER } State;
static State state = ST_TITLE;
static float clock_t_;
static float over_time;

static void set_banner(const char *t, Color c, float secs) {
    snprintf(banner_text, sizeof(banner_text), "%s", t);
    banner_color = c;
    banner_t = secs;
}

// Mirrored left/right, in tiles of 32px.
static void add_wall(int tx, int ty, int tw, int th) {
    walls[wall_count++] = (Rect){tx * TILE, ty * TILE, tw * TILE, th * TILE};
    float mx = ROOM_W - (tx + tw) * TILE;
    if (mx != tx * TILE) walls[wall_count++] = (Rect){mx, ty * TILE, tw * TILE, th * TILE};
}

static void build_level(void) {
    wall_count = 0;
    add_wall(7, 6, 1, 4);    // cover in front of each base
    add_wall(7, 14, 1, 4);
    add_wall(10, 3, 3, 1);   // mid-field blocks
    add_wall(10, 20, 3, 1);
    add_wall(12, 10, 1, 4);
    add_wall(15, 11, 2, 2);  // center pillar
}

static void spawn(Player *p) {
    p->x = p->spawn_x;
    p->y = p->spawn_y;
    p->dir_x = p->team == BLUE ? 1 : -1;
    p->dir_y = 0;
    p->hp = MAX_HP;
    p->dead = false;
    p->invuln = INVULN_TIME;
    p->cooldown = 0;
}

static void reset_match(void) {
    for (int t = 0; t < 2; t++) {
        Player *p = &players[t];
        memset(p, 0, sizeof(*p));
        p->team = t;
        p->enemy = 1 - t;
        p->spawn_x = BASE_X[t] + (t == BLUE ? 90 : -90);
        p->spawn_y = BASE_Y;
        spawn(p);
        flags[t] = (Flag){t, BASE_X[t], BASE_Y, BASE_X[t], BASE_Y, NULL};
    }
    bullet_count = effect_count = 0;
    winner = -1;
    char buf[48];
    snprintf(buf, sizeof(buf), "FIRST TO %d CAPTURES", WIN_SCORE);
    set_banner(buf, GREEN, 2);
}

static bool rects_overlap(float ax, float ay, float aw, float ah, Rect b) {
    return ax < b.x + b.w && ax + aw > b.x && ay < b.y + b.h && ay + ah > b.y;
}

static bool box_blocked(float cx, float cy, float half) {
    float x = cx - half, y = cy - half, s = half * 2;
    if (x < BORDER || y < BORDER || x + s > ROOM_W - BORDER || y + s > ROOM_H - BORDER) return true;
    for (int i = 0; i < wall_count; i++)
        if (rects_overlap(x, y, s, s, walls[i])) return true;
    return false;
}

static bool near(float ax, float ay, float bx, float by, float dist) {
    return fabsf(ax - bx) < dist && fabsf(ay - by) < dist;
}

static bool flag_home(const Flag *f) {
    return !f->carrier && f->x == f->home_x && f->y == f->home_y;
}

static void return_flag(Flag *f) {
    f->carrier = NULL;
    f->x = f->home_x;
    f->y = f->home_y;
}

static void burst(float x, float y, int team) {
    for (int i = 0; i < 14 && effect_count < MAX_EFFECTS; i++) {
        float a = (rand() / (float)RAND_MAX) * 6.2831853f;
        float s = 80 + (rand() / (float)RAND_MAX) * 220;
        effects[effect_count++] = (Effect){team, x, y, cosf(a) * s, sinf(a) * s, 0.5f, 0.5f};
    }
}

static const char *TEAM_NAME[2] = {"BLUE", "RED"};

static void update_player(Player *p, const PlayerInput *in, float dt) {
    if (p->dead) {
        p->respawn -= dt;
        if (p->respawn <= 0) spawn(p);
        return;
    }
    if (p->invuln > 0) p->invuln -= dt;
    if (p->cooldown > 0) p->cooldown -= dt;

    float mx = in->mx, my = in->my;
    float len = sqrtf(mx * mx + my * my);
    if (len > STICK_DEADZONE) {
        mx /= len;
        my /= len;
        p->dir_x = mx;
        p->dir_y = my;
        float speed = (flags[p->enemy].carrier == p ? CARRIER_SPEED : PLAYER_SPEED) * (len > 1 ? 1 : len);
        // Move one axis at a time so players slide along walls.
        float nx = p->x + mx * speed * dt;
        if (!box_blocked(nx, p->y, PLAYER_HALF)) p->x = nx;
        float ny = p->y + my * speed * dt;
        if (!box_blocked(p->x, ny, PLAYER_HALF)) p->y = ny;
    }

    if (in->fire && p->cooldown <= 0 && bullet_count < MAX_BULLETS) {
        p->cooldown = FIRE_COOLDOWN;
        bullets[bullet_count++] = (Bullet){p->team, p->x + p->dir_x * 30, p->y + p->dir_y * 30,
                                           p->dir_x * BULLET_SPEED, p->dir_y * BULLET_SPEED,
                                           atan2f(p->dir_y, p->dir_x) * 57.29578f};
    }

    Flag *enemy_flag = &flags[p->enemy], *own_flag = &flags[p->team];
    if (!enemy_flag->carrier && near(p->x, p->y, enemy_flag->x, enemy_flag->y, PLAYER_HALF + FLAG_HALF))
        enemy_flag->carrier = p;
    if (!own_flag->carrier && !flag_home(own_flag) &&
        near(p->x, p->y, own_flag->x, own_flag->y, PLAYER_HALF + FLAG_HALF)) {
        return_flag(own_flag);
        char buf[48];
        snprintf(buf, sizeof(buf), "%s FLAG RETURNED", TEAM_NAME[p->team]);
        set_banner(buf, TEAM_COLOR[p->team], 1.5f);
    }
    if (enemy_flag->carrier == p && flag_home(own_flag) &&
        near(p->x, p->y, BASE_X[p->team], BASE_Y, PLAYER_HALF + BASE_HALF)) {
        return_flag(enemy_flag);
        p->score++;
        burst(BASE_X[p->team], BASE_Y, p->team);
        if (p->score >= WIN_SCORE) {
            winner = p->team;
        } else {
            char buf[48];
            snprintf(buf, sizeof(buf), "%s CAPTURES!", TEAM_NAME[p->team]);
            set_banner(buf, TEAM_COLOR[p->team], 2);
        }
    }
}

static void update_bullets(float dt) {
    for (int i = bullet_count - 1; i >= 0; i--) {
        Bullet *b = &bullets[i];
        b->x += b->vx * dt;
        b->y += b->vy * dt;
        bool gone = box_blocked(b->x, b->y, BULLET_RADIUS);
        Player *t = &players[1 - b->team];
        if (!gone && !t->dead && near(b->x, b->y, t->x, t->y, PLAYER_HALF + BULLET_RADIUS)) {
            gone = true;
            if (t->invuln <= 0) {
                t->hp--;
                burst(b->x, b->y, b->team);
                if (t->hp <= 0) {
                    Flag *f = &flags[t->enemy];
                    if (f->carrier == t) { f->carrier = NULL; f->x = t->x; f->y = t->y; }
                    t->dead = true;
                    t->respawn = RESPAWN_TIME;
                    burst(t->x, t->y, t->team);
                }
            }
        }
        if (gone) bullets[i] = bullets[--bullet_count];
    }
}

static void update_effects(float dt) {
    for (int i = effect_count - 1; i >= 0; i--) {
        Effect *e = &effects[i];
        e->x += e->vx * dt;
        e->y += e->vy * dt;
        e->vx *= 0.9f;
        e->vy *= 0.9f;
        e->life -= dt;
        if (e->life <= 0) effects[i] = effects[--effect_count];
    }
}

static void go_title(void) {
    state = ST_TITLE;
    play_music(mus_intro);
}

static void go_play(void) {
    reset_match();
    state = ST_PLAY;
    play_music(mus_main);
}

// Returns false when the app should exit.
static bool update(const Input *in, float dt) {
    clock_t_ += dt;
    if (in->mute) {
        muted = !muted;
        Mix_VolumeMusic(muted ? 0 : MIX_MAX_VOLUME * 6 / 10);
    }
    switch (state) {
    case ST_TITLE:
        if (in->confirm) go_play();
#ifdef __SWITCH__
        if (in->setup) controller_setup();
        if (in->pause) return false;  // + exits, as is customary for homebrew
#endif
        break;
    case ST_PLAY:
        if (in->pause) { state = ST_PAUSED; break; }
        update_player(&players[BLUE], &in->p[BLUE], dt);
        update_player(&players[RED], &in->p[RED], dt);
        for (int t = 0; t < 2; t++)
            if (flags[t].carrier) { flags[t].x = flags[t].carrier->x; flags[t].y = flags[t].carrier->y - 34; }
        update_bullets(dt);
        update_effects(dt);
        if (banner_t > 0) banner_t -= dt;
        if (winner >= 0) { state = ST_OVER; banner_t = 0; over_time = 0; }
        break;
    case ST_PAUSED:
        if (in->pause) state = ST_PLAY;
        if (in->back) go_title();
        break;
    case ST_OVER:
        update_effects(dt);
        over_time += dt;
        // Fire buttons double as confirm, so ignore them briefly after the win.
        if (over_time > 1 && (in->confirm || in->pause)) go_title();
        break;
    }
    return !in->quit;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
static int blink(float rate) { return (int)floorf(clock_t_ * rate) % 2; }

static void draw_title(const Input *in) {
    draw(spr("bkg_title"), ROOM_W / 2, ROOM_H / 2, 1);
    // Placement mirrors rm_title: flags left of the logo, players at 15% / 75%.
    draw_ex(spr("spr_flagr_0"), 320 - 41, 160 + 55, -1, 1, 0, 255);
    draw_ex(spr("spr_flagb_0"), 352 - 48, 160 + 72, -1.17f, 1.29f, 0, 255);
    int pulse = blink(4);
    draw(sprf("spr_blue", pulse), ROOM_W * 0.15f + 40, ROOM_H * 0.65f + 40, 1);
    draw(sprf("spr_red", pulse), ROOM_W * 0.75f + 40, ROOM_H * 0.65f + 40, 1);

    char buf[96];
    snprintf(buf, sizeof(buf), "CAPTURE THE ENEMY FLAG AND BRING IT HOME. FIRST TO %d WINS.", WIN_SCORE);
    text(buf, ROOM_W / 2, 600, 2, DIM_GREEN, 255, ALIGN_CENTER);
#ifdef __SWITCH__
    // Cover the keyboard hints baked into bkg_title with controller hints.
    fill_rect(40, 650, 140, 80, (Color){0, 0, 0}, 255);
    fill_rect(870, 650, 120, 80, (Color){0, 0, 0}, 255);
    fill_rect(400, 632, 224, 36, (Color){0, 0, 0}, 255);
    text("PRESS A", ROOM_W / 2, 650, 4, GREEN, 255, ALIGN_CENTER);
    if (in->two_pads) {
        text("P1: STICK + ANY BUTTON", 40, 715, 2, TEAM_COLOR[BLUE], 255, ALIGN_LEFT);
        text("P2: STICK + ANY BUTTON", ROOM_W - 40, 715, 2, TEAM_COLOR[RED], 255, ALIGN_RIGHT);
    } else {
        text("P1: L-STICK/D-PAD  FIRE L/ZL", 40, 715, 2, TEAM_COLOR[BLUE], 255, ALIGN_LEFT);
        text("P2: R-STICK/ABXY  FIRE R/ZR", ROOM_W - 40, 715, 2, TEAM_COLOR[RED], 255, ALIGN_RIGHT);
    }
    text("-: CONTROLLERS   +: EXIT", ROOM_W / 2, 740, 2, DIM_GREEN, 255, ALIGN_CENTER);
#else
    (void)in;
    text("SPACE: FIRE", 108, 728, 2, GREEN, 255, ALIGN_CENTER);
    text("ENTER: FIRE", ROOM_W - 92, 728, 2, GREEN, 255, ALIGN_CENTER);
#endif
    if (!blink(2)) fill_rect(400, 632, 224, 36, GREEN, 38);
}

static void draw_base(int team) {
    float x = BASE_X[team], y = BASE_Y;
    if (team == BLUE) {
        draw(sprf("spr_bluegoal", blink(2)), x, y, 1);
        return;
    }
    // No red goal sprite exists in the project; draw a matching one.
    float s = 70, glow = 0.5f + 0.25f * sinf(clock_t_ * 6.2831853f);
    fill_rect(x - s / 2 - 6, y - s / 2 - 6, s + 12, s + 12, TEAM_COLOR[RED], 40);
    fill_rect(x - s / 2, y - s / 2, s, s, (Color){255, 60, 60}, (Uint8)(255 * (0.35f + glow * 0.3f)));
    SDL_SetRenderDrawColor(renderer, 128, 32, 32, 255);
    for (int i = 3; i < 6; i++) {
        SDL_FRect r = {x - s / 2 + i, y - s / 2 + i, s - 2 * i, s - 2 * i};
        SDL_RenderDrawRectF(renderer, &r);
    }
    text("R", x, y, 6, (Color){128, 32, 32}, 255, ALIGN_CENTER);
}

static void draw_flag(const Flag *f) {
    float bob = f->carrier ? 0 : sinf(clock_t_ * 3) * 3;
    draw(spr(f->team == BLUE ? "spr_flagb_0" : "spr_flagr_0"), f->x, f->y + bob, FLAG_SCALE);
}

static void draw_player(const Player *p) {
    if (p->dead) {
        // Respawn ghost at the spawn point using the small-orb frame.
        Uint8 a = (Uint8)(255 * (0.3f + 0.2f * sinf(clock_t_ * 10)));
        draw_ex(spr(p->team == BLUE ? "spr_blue_2" : "spr_red_0"), p->spawn_x, p->spawn_y, 1, 1, 0, a);
        return;
    }
    Uint8 a = (p->invuln > 0 && blink(16)) ? 90 : 255;
    Sprite *s = p->team == BLUE ? sprf("spr_blue", blink(6)) : spr("spr_red_1");
    draw_ex(s, p->x, p->y, 1, 1, 0, a);
    // Aim indicator
    Color c = TEAM_COLOR[p->team];
    fill_rect(p->x + p->dir_x * 28 - 3, p->y + p->dir_y * 28 - 3, 6, 6, c, 255);
}

static void draw_hud(void) {
    for (int t = 0; t < 2; t++) {
        const Player *p = &players[t];
        int hp = p->dead ? 1 : p->hp;
        float hx = t == BLUE ? 60 : ROOM_W - 60;
        float tx = t == BLUE ? 100 : ROOM_W - 100;
        Align al = t == BLUE ? ALIGN_LEFT : ALIGN_RIGHT;
        draw(sprf(t == BLUE ? "spr_healthb" : "spr_healthr", MAX_HP - hp), hx, 60, 1);
        char buf[32];
        if (t == BLUE) snprintf(buf, sizeof(buf), "P1  %d", p->score);
        else snprintf(buf, sizeof(buf), "%d  P2", p->score);
        text(buf, tx, 60, 4, TEAM_COLOR[t], 255, al);
        if (p->dead) {
            snprintf(buf, sizeof(buf), "RESPAWN %d", (int)ceilf(p->respawn));
            text(buf, tx, 92, 2, TEAM_COLOR[t], 255, al);
        }
    }
}

static void draw_play(void) {
    draw(spr("bkg_play"), ROOM_W / 2, ROOM_H / 2, 1);
    Sprite *wall = spr("spr_wall_0");
    for (int i = 0; i < wall_count; i++)
        for (float x = walls[i].x; x < walls[i].x + walls[i].w; x += TILE)
            for (float y = walls[i].y; y < walls[i].y + walls[i].h; y += TILE)
                draw(wall, x + TILE / 2, y + TILE / 2, 1);
    draw_base(BLUE);
    draw_base(RED);
    for (int t = 0; t < 2; t++) if (!flags[t].carrier) draw_flag(&flags[t]);
    draw_player(&players[BLUE]);
    draw_player(&players[RED]);
    for (int t = 0; t < 2; t++) if (flags[t].carrier) draw_flag(&flags[t]);
    for (int i = 0; i < bullet_count; i++) {
        const Bullet *b = &bullets[i];
        draw_ex(sprf(b->team == BLUE ? "spr_bulletb" : "spr_bulletr", blink(12)), b->x, b->y, 1.6f, 1.6f,
                b->angle, 255);
    }
    for (int i = 0; i < effect_count; i++) {
        const Effect *e = &effects[i];
        fill_rect(e->x - 2, e->y - 2, 4, 4, TEAM_COLOR[e->team], (Uint8)(255 * e->life / e->max));
    }
    draw_hud();
    if (banner_t > 0) {
        float a = banner_t * 2 > 1 ? 1 : banner_t * 2;
        text(banner_text, ROOM_W / 2, 140, 5, banner_color, (Uint8)(255 * a), ALIGN_CENTER);
    }
}

static void overlay(const char *title, Color c, const char *sub) {
    fill_rect(0, 0, ROOM_W, ROOM_H, (Color){0, 0, 0}, 166);
    text(title, ROOM_W / 2, ROOM_H / 2 - 30, 9, c, 255, ALIGN_CENTER);
    text(sub, ROOM_W / 2, ROOM_H / 2 + 40, 3, GREEN, 255, ALIGN_CENTER);
}

static void render(const Input *in) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    if (state == ST_TITLE) {
        draw_title(in);
    } else {
        draw_play();
#ifdef __SWITCH__
        if (state == ST_PAUSED) overlay("PAUSED", GREEN, "+: RESUME    -: QUIT TO TITLE");
        const char *again = "PRESS A";
#else
        if (state == ST_PAUSED) overlay("PAUSED", GREEN, "P / ESC: RESUME    Q: QUIT TO TITLE");
        const char *again = "PRESS ENTER";
#endif
        if (state == ST_OVER) {
            char title[32], sub[48];
            snprintf(title, sizeof(title), "%s WINS", TEAM_NAME[winner]);
            snprintf(sub, sizeof(sub), "%d - %d    %s", players[BLUE].score, players[RED].score, again);
            overlay(title, TEAM_COLOR[winner], sub);
        }
    }
    if (muted) text("MUTED (M)", ROOM_W / 2, ROOM_H - 36, 2, DIM_GREEN, 255, ALIGN_CENTER);
    SDL_RenderPresent(renderer);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
#ifdef __SWITCH__
    romfsInit();
    int win_w = 1280, win_h = 720;
#else
    int win_w = ROOM_W, win_h = ROOM_H;
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    IMG_Init(IMG_INIT_PNG);
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) == 0) Mix_Init(MIX_INIT_MP3);
    else SDL_Log("Audio unavailable: %s", Mix_GetError());

    SDL_Window *window = SDL_CreateWindow("Arcade Capture", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          win_w, win_h, SDL_WINDOW_RESIZABLE);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!window || !renderer) {
        SDL_Log("Window/renderer creation failed: %s", SDL_GetError());
        return 1;
    }
    // The game is authored at 1024x768 (4:3); SDL letterboxes it onto the 16:9 screen.
    SDL_RenderSetLogicalSize(renderer, ROOM_W, ROOM_H);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    if (!load_assets()) return 1;
    input_init();
    build_level();
    go_title();

    Uint64 freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
    bool running = true;
    Input in;
#ifdef __SWITCH__
    while (running && appletMainLoop()) {
#else
    while (running) {
#endif
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
            if (ev.type == SDL_QUIT) running = false;

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last) / freq;
        last = now;
        if (dt > 0.05f) dt = 0.05f;

        read_input(&in);
        if (!update(&in, dt)) running = false;
        render(&in);
    }

    Mix_HaltMusic();
    if (mus_intro) Mix_FreeMusic(mus_intro);
    if (mus_main) Mix_FreeMusic(mus_main);
    for (int i = 0; i < SPRITE_COUNT; i++) if (sprites[i].tex) SDL_DestroyTexture(sprites[i].tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    Mix_CloseAudio();
    Mix_Quit();
    IMG_Quit();
    SDL_Quit();
#ifdef __SWITCH__
    romfsExit();
#endif
    return 0;
}
