// MIT License

// Copyright (c) 2017 Vadim Grigoruk @nesbox // grigoruk@gmail.com

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "studio/system.h"
#include "tools.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#if defined(CRT_SHADER_SUPPORT)

#include <SDL_gpu.h>
#define STUDIO_PIXEL_FORMAT GPU_FORMAT_RGBA

#else

#include <SDL.h>
#define STUDIO_PIXEL_FORMAT SDL_PIXELFORMAT_ABGR8888

#endif

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#define TEXTURE_SIZE (TIC80_FULLWIDTH)

#if defined(__TIC_WINDOWS__)
#include <windows.h>
#endif

#if defined(__TIC_ANDROID__)
#include <sys/stat.h>
#endif

#if defined(TOUCH_INPUT_SUPPORT)
#define TOUCH_TIMEOUT (10 * TIC80_FRAMERATE)
#endif

#define TIC_PACKAGE "com.nesbox.tic"

#define KBD_COLS 22
#define KBD_ROWS 17

enum 
{
    tic_key_board = tic_keys_count + 1,
    tic_touch_size,
};

typedef enum
{
    HandCursor,
    IBeamCursor,
    ArrowCursor
} CursorType;

static const SDL_SystemCursor SystemCursors[] = 
{
    [HandCursor] = SDL_SYSTEM_CURSOR_HAND,
    [IBeamCursor] = SDL_SYSTEM_CURSOR_IBEAM,
    [ArrowCursor] = SDL_SYSTEM_CURSOR_ARROW
};

#if defined(CRT_SHADER_SUPPORT)
#   define Renderer GPU_Target
#   define Texture GPU_Image
#   define Rect GPU_Rect
#   define destoryTexture GPU_FreeImage
#   define destoryRenderer(render) GPU_CloseCurrentRenderer()
#   define renderPresent GPU_Flip
#   define renderClear GPU_Clear
#   define renderCopy(render, tex, src, dst) GPU_BlitScale(tex, &src, render, dst.x, dst.y, dst.w / src.w, dst.h / src.h)
#else
#   define Renderer SDL_Renderer
#   define Texture SDL_Texture
#   define Rect SDL_Rect
#   define destoryTexture SDL_DestroyTexture
#   define destoryRenderer(render) SDL_DestroyRenderer(render)
#   define renderPresent SDL_RenderPresent
#   define renderClear SDL_RenderClear
#   define renderCopy(render, tex, src, dst) SDL_RenderCopy(render, tex, &src, &dst)
#endif

static struct
{
    Studio* studio;

    SDL_Window* window;

    struct
    {
        Renderer* renderer;
        Texture* texture;

#if defined(CRT_SHADER_SUPPORT)
        u32 shader;
        GPU_ShaderBlock block;
#endif
    } gpu;

    struct
    {
        SDL_Joystick* ports[TIC_GAMEPADS];

#if defined(TOUCH_INPUT_SUPPORT)
        struct
        {
            Texture* texture;
            void* pixels;
            tic80_gamepads joystick;

            struct
            {
                s32 size;
                SDL_Point axis;
                SDL_Point a;
                SDL_Point b;
                SDL_Point x;
                SDL_Point y;
            } button;

            s32 counter;
        }touch;
#endif

        tic80_gamepads joystick;

    } gamepad;

    struct
    {
        bool state[tic_keys_count];
        char text;

#if defined(TOUCH_INPUT_SUPPORT)
        struct
        {
            struct
            {
                s32 size;
                SDL_Point pos;
            } button;

            bool state[tic_touch_size];

            struct
            {
                Texture* up;
                Texture* down;
                void* upPixels;
                void* downPixels;
            } texture;

            bool useText;

        } touch;
#endif

    } keyboard;

    struct
    {
        Texture* texture;
        const u8* src;
        SDL_Cursor* cursors[COUNT_OF(SystemCursors)];
    } mouse;

    struct
    {
        SDL_AudioSpec       spec;
        SDL_AudioDeviceID   device;
    } audio;
} platform
#if defined(TOUCH_INPUT_SUPPORT)
= 
{
    .gamepad.touch.counter = TOUCH_TIMEOUT,
    .keyboard.touch.useText = false,
};
#endif
;

#if defined(CRT_SHADER_SUPPORT)
static inline bool crtMonitorEnabled()
{
    return platform.studio->config()->crtMonitor && platform.gpu.shader;
}
#endif

static void initSound()
{
    SDL_AudioSpec want =
    {
        .freq = TIC80_SAMPLERATE,
        .format = AUDIO_S16,
        .channels = TIC_STEREO_CHANNELS,
        .userdata = NULL,
    };

    platform.audio.device = SDL_OpenAudioDevice(NULL, 0, &want, &platform.audio.spec, 0);
}

static const u8* getSpritePtr(const tic_tile* tiles, s32 x, s32 y)
{
    enum { SheetCols = (TIC_SPRITESHEET_SIZE / TIC_SPRITESIZE) };
    return tiles[x / TIC_SPRITESIZE + y / TIC_SPRITESIZE * SheetCols].data;
}

static u8 getSpritePixel(const tic_tile* tiles, s32 x, s32 y)
{
    return tic_tool_peek4(getSpritePtr(tiles, x, y), (x % TIC_SPRITESIZE) + (y % TIC_SPRITESIZE) * TIC_SPRITESIZE);
}

static void setWindowIcon()
{
    enum{ Size = 64, TileSize = 16, ColorKey = 14, Cols = TileSize / TIC_SPRITESIZE, Scale = Size/TileSize};

    DEFER(u32* pixels = SDL_malloc(Size * Size * sizeof(u32)), SDL_free(pixels))
    {
        const u32* pal = tic_tool_palette_blit(&platform.studio->config()->cart->bank0.palette.scn, platform.studio->tic->screen_format);

        for(s32 j = 0, index = 0; j < Size; j++)
            for(s32 i = 0; i < Size; i++, index++)
            {
                u8 color = getSpritePixel(platform.studio->config()->cart->bank0.tiles.data, i/Scale, j/Scale);
                pixels[index] = color == ColorKey ? 0 : pal[color];
            }

        DEFER(SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(pixels, Size, Size,
            sizeof(s32) * BITS_IN_BYTE, Size * sizeof(s32),
            0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000), SDL_FreeSurface(surface))
        {
            SDL_SetWindowIcon(platform.window, surface);
        }
    }
}

static void updateTextureBytes(Texture* texture, const void* data, s32 width, s32 height)
{
#if defined(CRT_SHADER_SUPPORT)
    GPU_UpdateImageBytes(texture, NULL, (const u8*)data, width * sizeof(u32));
#else
    void* pixels = NULL;
    s32 pitch = 0;
    SDL_LockTexture(texture, NULL, &pixels, &pitch);
    SDL_memcpy(pixels, data, pitch * height);
    SDL_UnlockTexture(texture);
#endif
}

#if defined(TOUCH_INPUT_SUPPORT)

static void drawKeyboardLabels(s32 shift)
{
    tic_mem* tic = platform.studio->tic;

    typedef struct {const char* text; s32 x; s32 y; bool alt; const char* shift;} Label;
    static const Label Labels[] =
    {
        #include "kbdlabels.inl"
    };

    for(s32 i = 0; i < COUNT_OF(Labels); i++)
    {
        const Label* label = Labels + i;
        if(label->text)
            tic_api_print(tic, label->text, label->x, label->y + shift, tic_color_grey, true, 1, label->alt);

        if(label->shift)
            tic_api_print(tic, label->shift, label->x + 6, label->y + shift + 2, tic_color_light_grey, true, 1, label->alt);
    }
}

static void map2ram()
{
    tic_mem* tic = platform.studio->tic;
    memcpy(tic->ram.map.data, &platform.studio->config()->cart->bank0.map, sizeof tic->ram.map);
    memcpy(tic->ram.tiles.data, &platform.studio->config()->cart->bank0.tiles, sizeof tic->ram.tiles * TIC_SPRITE_BANKS);
}

static void initTouchKeyboardState(Texture** texture, void** pixels, bool down)
{
    enum{Cols=KBD_COLS, Rows=KBD_ROWS};

    tic_mem* tic = platform.studio->tic;

    tic_api_map(tic, down ? Cols : 0, 0, Cols, Rows, 0, 0, 0, 0, 1, NULL, NULL);
    drawKeyboardLabels(down ? 2 : 0);
    tic_core_blit(tic, tic->screen_format);
    *pixels = SDL_malloc(TIC80_FULLWIDTH * TIC80_FULLHEIGHT * sizeof(u32));
    memcpy(*pixels, tic->screen, TIC80_FULLWIDTH * TIC80_FULLHEIGHT * sizeof(u32));

#if defined(CRT_SHADER_SUPPORT)
    *texture = GPU_CreateImage(TIC80_FULLWIDTH, TIC80_FULLHEIGHT, STUDIO_PIXEL_FORMAT);
    GPU_SetAnchor(*texture, 0, 0);
    GPU_SetImageFilter(*texture, GPU_FILTER_NEAREST);
#else
    *texture = SDL_CreateTexture(platform.gpu.renderer, STUDIO_PIXEL_FORMAT, SDL_TEXTUREACCESS_STREAMING, TIC80_FULLWIDTH, TIC80_FULLHEIGHT);
#endif
    updateTextureBytes(*texture, *pixels, TIC80_FULLWIDTH, TIC80_FULLHEIGHT);
}

static void initTouchKeyboard()
{
    tic_mem* tic = platform.studio->tic;

    memcpy(tic->ram.vram.palette.data, platform.studio->config()->cart->bank0.palette.scn.data, sizeof(tic_palette));
    tic_api_cls(tic, 0);
    map2ram();

    initTouchKeyboardState(&platform.keyboard.touch.texture.up, &platform.keyboard.touch.texture.upPixels, false);
    initTouchKeyboardState(&platform.keyboard.touch.texture.down, &platform.keyboard.touch.texture.downPixels, true);

    memset(tic->ram.map.data, 0, sizeof tic->ram.map);
}

static void updateGamepadParts()
{
    s32 tileSize = TIC_SPRITESIZE;
    s32 offset = 0;
    SDL_Rect rect;

    const s32 JoySize = 3;
    SDL_GetWindowSize(platform.window, &rect.w, &rect.h);

    if(rect.w < rect.h)
    {
        tileSize = rect.w / 2 / JoySize;
        offset = (rect.h * 2 - JoySize * tileSize) / 3;
    }
    else
    {
        tileSize = rect.w / 5 / JoySize;
        offset = (rect.h - JoySize * tileSize) / 2;
    }

    platform.gamepad.touch.button.size = tileSize;
    platform.gamepad.touch.button.axis = (SDL_Point){0, offset};
    platform.gamepad.touch.button.a = (SDL_Point){rect.w - 2*tileSize, 2*tileSize + offset};
    platform.gamepad.touch.button.b = (SDL_Point){rect.w - 1*tileSize, 1*tileSize + offset};
    platform.gamepad.touch.button.x = (SDL_Point){rect.w - 3*tileSize, 1*tileSize + offset};
    platform.gamepad.touch.button.y = (SDL_Point){rect.w - 2*tileSize, 0*tileSize + offset};

    platform.keyboard.touch.button.size = rect.w < rect.h ? tileSize : 0;
    platform.keyboard.touch.button.pos = (SDL_Point){rect.w/2 - tileSize, rect.h - 2*tileSize};
}
#endif

#if defined(TOUCH_INPUT_SUPPORT)
static void initTouchGamepad()
{
    if(!platform.gamepad.touch.pixels)
    {
        tic_mem* tic = platform.studio->tic;
        const tic_bank* bank = &platform.studio->config()->cart->bank0;

        {
            memcpy(tic->ram.vram.palette.data, &bank->palette.scn, sizeof(tic_palette));
            memcpy(tic->ram.tiles.data, &bank->tiles, sizeof(tic_tiles));
            tic_api_spr(tic, 0, 0, 0, TIC_SPRITESHEET_COLS, TIC_SPRITESHEET_COLS, NULL, 0, 1, tic_no_flip, tic_no_rotate);
        }

        platform.gamepad.touch.pixels = SDL_malloc(TEXTURE_SIZE * TEXTURE_SIZE * sizeof(u32));

        tic_core_blit(tic, tic->screen_format);

        for(u32* pix = tic->screen, *end = pix + TIC80_FULLWIDTH * TIC80_FULLHEIGHT; pix != end; ++pix)
            if(*pix == tic_rgba(&bank->palette.scn.colors[0]))
                *pix = 0;

        memcpy(platform.gamepad.touch.pixels, tic->screen, TIC80_FULLWIDTH * TIC80_FULLHEIGHT * sizeof(u32));

        ZEROMEM(tic->ram.vram.palette);
        ZEROMEM(tic->ram.tiles);
    }

    if(!platform.gamepad.touch.texture)
    {
#if defined(CRT_SHADER_SUPPORT)
        platform.gamepad.touch.texture = GPU_CreateImage(TEXTURE_SIZE, TEXTURE_SIZE, STUDIO_PIXEL_FORMAT);
        GPU_SetAnchor(platform.gamepad.touch.texture, 0, 0);
        GPU_SetImageFilter(platform.gamepad.touch.texture, GPU_FILTER_NEAREST);
        GPU_SetRGBA(platform.gamepad.touch.texture, 0xff, 0xff, 0xff, platform.studio->config()->theme.gamepad.touch.alpha);
#else
        platform.gamepad.touch.texture = SDL_CreateTexture(platform.gpu.renderer, STUDIO_PIXEL_FORMAT, SDL_TEXTUREACCESS_STREAMING, TEXTURE_SIZE, TEXTURE_SIZE);
        SDL_SetTextureBlendMode(platform.gamepad.touch.texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(platform.gamepad.touch.texture, platform.studio->config()->theme.gamepad.touch.alpha);
#endif
        updateTextureBytes(platform.gamepad.touch.texture, platform.gamepad.touch.pixels, TEXTURE_SIZE, TEXTURE_SIZE);
    }

    updateGamepadParts();
}
#endif

static void initGPU()
{
#if defined(CRT_SHADER_SUPPORT)

    {
        s32 w = 0, h = 0;
        SDL_GetWindowSize(platform.window, &w, &h);

        GPU_SetInitWindow(SDL_GetWindowID(platform.window));

        platform.gpu.renderer = GPU_Init(w, h, GPU_INIT_DISABLE_VSYNC);

        GPU_SetWindowResolution(w, h);
        GPU_SetVirtualResolution(platform.gpu.renderer, w, h);
    }

    platform.gpu.texture = GPU_CreateImage(TIC80_FULLWIDTH, TIC80_FULLHEIGHT, STUDIO_PIXEL_FORMAT);
    GPU_SetAnchor(platform.gpu.texture, 0, 0);
    GPU_SetImageFilter(platform.gpu.texture, GPU_FILTER_NEAREST);

#else

    platform.gpu.renderer = SDL_CreateRenderer(platform.window, -1, SDL_RENDERER_ACCELERATED);
    platform.gpu.texture = SDL_CreateTexture(platform.gpu.renderer, STUDIO_PIXEL_FORMAT, SDL_TEXTUREACCESS_STREAMING, TIC80_FULLWIDTH, TIC80_FULLHEIGHT);

#endif

#if defined(TOUCH_INPUT_SUPPORT)
    initTouchGamepad();
    initTouchKeyboard();
#endif
}


static void destroyGPU()
{
    destoryTexture(platform.gpu.texture);

    if(platform.mouse.texture)
        destoryTexture(platform.mouse.texture);

#if defined(TOUCH_INPUT_SUPPORT)

    if(platform.gamepad.touch.texture)
        destoryTexture(platform.gamepad.touch.texture);

    if(platform.keyboard.touch.texture.up)
        destoryTexture(platform.keyboard.touch.texture.up);

    if(platform.keyboard.touch.texture.down)
        destoryTexture(platform.keyboard.touch.texture.down);

#endif

    destoryRenderer(platform.gpu.renderer);

#if defined(CRT_SHADER_SUPPORT)

    if(platform.gpu.shader)
    {
        GPU_FreeShaderProgram(platform.gpu.shader);
        platform.gpu.shader = 0;
    }

    platform.mouse.src = NULL;

    GPU_Quit();

#endif
}

static void calcTextureRect(SDL_Rect* rect)
{
    SDL_GetWindowSize(platform.window, &rect->w, &rect->h);

#if defined(CRT_SHADER_SUPPORT)
    if(crtMonitorEnabled())
    {
        enum{Width = TIC80_FULLWIDTH, Height = TIC80_FULLHEIGHT};

        if (rect->w * Height < rect->h * Width)
        {
            rect->x = 0;
            rect->y = 0;

            rect->h = Height * rect->w / Width;
        }
        else
        {
            s32 width = Width * rect->h / Height;

            rect->x = (rect->w - width) / 2;
            rect->y = 0;

            rect->w = width;
        }
    }
    else
#endif
    {
        enum{Width = TIC80_WIDTH, Height = TIC80_HEIGHT};

        if (rect->w * Height < rect->h * Width)
        {
            s32 discreteWidth = rect->w - rect->w % Width;
            s32 discreteHeight = Height * discreteWidth / Width;

            rect->x = (rect->w - discreteWidth) / 2;

            rect->y = rect->w > rect->h 
                ? (rect->h - discreteHeight) / 2 
                : TIC80_OFFSET_TOP*discreteWidth/Width;

            rect->w = discreteWidth;
            rect->h = discreteHeight;

        }
        else
        {
            s32 discreteHeight = rect->h - rect->h % Height;
            s32 discreteWidth = Width * discreteHeight / Height;

            rect->x = (rect->w - discreteWidth) / 2;
            rect->y = (rect->h - discreteHeight) / 2;

            rect->w = discreteWidth;
            rect->h = discreteHeight;
        }
    }
}

static void processMouse()
{
    tic_point pt;
    s32 mb = SDL_GetMouseState(&pt.x, &pt.y);

    tic_mem* tic = platform.studio->tic;
    tic80_mouse* mouse = &tic->ram.input.mouse;

    if(SDL_GetRelativeMouseMode())
    {
        SDL_GetRelativeMouseState(&pt.x, &pt.y);

        mouse->rx = pt.x;
        mouse->ry = pt.y;
    }
    else
    {

        {
            mouse->x = mouse->y = 0;

            SDL_Rect rect = {0, 0, 0, 0};
            calcTextureRect(&rect);

#if defined(CRT_SHADER_SUPPORT)
            if(crtMonitorEnabled())
            {
                if(rect.w) {
                    s32 temp_x = (pt.x - rect.x) * TIC80_FULLWIDTH / rect.w;
                    if (temp_x < 0) temp_x = 0; else if (temp_x >= TIC80_FULLWIDTH) temp_x = TIC80_FULLWIDTH-1; // clip: 0 to TIC80_FULLWIDTH-1
                    mouse->x = temp_x;
                }
                if(rect.h) {
                    s32 temp_y = (pt.y - rect.y) * TIC80_FULLHEIGHT / rect.h;
                    if (temp_y < 0) temp_y = 0; else if (temp_y >= TIC80_FULLHEIGHT) temp_y = TIC80_FULLHEIGHT-1; // clip: 0 to TIC80_FULLHEIGHT-1
                    mouse->y = temp_y;
                }
            }
            else
#endif
            {
                if (rect.w) {
                    s32 temp_x = (pt.x - rect.x) * TIC80_WIDTH / rect.w + TIC80_OFFSET_LEFT;
                    if (temp_x < 0) temp_x = 0; else if (temp_x >= TIC80_FULLWIDTH) temp_x = TIC80_FULLWIDTH-1; // clip: 0 to TIC80_FULLWIDTH-1
                    mouse->x = temp_x;
                }
                if (rect.h) {
                    s32 temp_y = (pt.y - rect.y) * TIC80_HEIGHT / rect.h + TIC80_OFFSET_TOP;
                    if (temp_y < 0) temp_y = 0; else if (temp_y >= TIC80_FULLHEIGHT) temp_y = TIC80_FULLHEIGHT-1; // clip: 0 to TIC80_FULLHEIGHT-1
                    mouse->y = temp_y;
                }
            }
        }
    }

    {        
        mouse->left = mb & SDL_BUTTON_LMASK ? 1 : 0;
        mouse->middle = mb & SDL_BUTTON_MMASK ? 1 : 0;
        mouse->right = mb & SDL_BUTTON_RMASK ? 1 : 0;
    }
}

static void processKeyboard()
{
    tic_mem* tic = platform.studio->tic;
    tic80_input* input = &tic->ram.input;

    {
        SDL_Keymod mod = SDL_GetModState();

        platform.keyboard.state[tic_key_shift] = mod & KMOD_SHIFT;
        platform.keyboard.state[tic_key_ctrl] = mod & (KMOD_CTRL | KMOD_GUI);
        platform.keyboard.state[tic_key_alt] = mod & KMOD_LALT;
        platform.keyboard.state[tic_key_capslock] = mod & KMOD_CAPS;

        // it's weird, but system sends CTRL when you press RALT
        if(mod & KMOD_RALT)
            platform.keyboard.state[tic_key_ctrl] = false;
    }   

    for(s32 i = 0, c = 0; i < COUNT_OF(platform.keyboard.state) && c < TIC80_KEY_BUFFER; i++)
        if(platform.keyboard.state[i] 
#if defined(TOUCH_INPUT_SUPPORT)
            || platform.keyboard.touch.state[i]
#endif
            )
            input->keyboard.keys[c++] = i;

#if defined(TOUCH_INPUT_SUPPORT)
    if(platform.keyboard.touch.state[tic_key_board])
    {
        *input->keyboard.keys = tic_key_board;
        if(!SDL_IsTextInputActive())
            SDL_StartTextInput();
    }
#endif
}

#if defined(TOUCH_INPUT_SUPPORT)

static bool checkTouch(const SDL_Rect* rect, s32* x, s32* y)
{
    s32 devices = SDL_GetNumTouchDevices();
    s32 width = 0, height = 0;
    SDL_GetWindowSize(platform.window, &width, &height);

    for (s32 i = 0; i < devices; i++)
    {
        SDL_TouchID id = SDL_GetTouchDevice(i);

        {
            s32 fingers = SDL_GetNumTouchFingers(id);

            for (s32 f = 0; f < fingers; f++)
            {
                SDL_Finger* finger = SDL_GetTouchFinger(id, f);

                if (finger && finger->pressure > 0.0f)
                {
                    SDL_Point point = { (s32)(finger->x * width), (s32)(finger->y * height) };
                    if (SDL_PointInRect(&point, rect))
                    {
                        *x = point.x;
                        *y = point.y;
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

static bool isGamepadVisible()
{
    return platform.studio->tic->input.gamepad;
}

static bool isKbdVisible()
{
    if(!platform.studio->tic->input.keyboard)
        return false;

    s32 w, h;
    SDL_GetWindowSize(platform.window, &w, &h);

    SDL_Rect rect;
    calcTextureRect(&rect);

    float scale = (float)w / (KBD_COLS*TIC_SPRITESIZE);

    return h - KBD_ROWS*TIC_SPRITESIZE*scale - (rect.h + rect.y*2) >= 0 && !SDL_IsTextInputActive();
}

static const tic_key KbdLayout[] = 
{
    #include "kbdlayout.inl"
};

static void processTouchKeyboardButton(SDL_Point pt)
{
    enum{Cols = KBD_COLS, Rows = KBD_ROWS};

    s32 w, h;
    SDL_GetWindowSize(platform.window, &w, &h);

    SDL_Rect kbd = {0, h - Rows * w / Cols, w, Rows * w / Cols};

    if(SDL_PointInRect(&pt, &kbd))
    {
        tic_point pos = {(pt.x - kbd.x) * Cols / w, (pt.y - kbd.y) * Cols / w};
        platform.keyboard.touch.state[KbdLayout[pos.x + pos.y * Cols]] = true;
        platform.keyboard.touch.useText = true;
    }
}

static void processTouchKeyboard()
{
    if(!isKbdVisible()) return;

    {
        SDL_Point pt;
        if(SDL_GetMouseState(&pt.x, &pt.y) & SDL_BUTTON_LMASK)
            processTouchKeyboardButton(pt);
    }

    s32 w, h;
    SDL_GetWindowSize(platform.window, &w, &h);

    s32 devices = SDL_GetNumTouchDevices();
    for (s32 i = 0; i < devices; i++)
    {
        SDL_TouchID id = SDL_GetTouchDevice(i);
        s32 fingers = SDL_GetNumTouchFingers(id);

        for (s32 f = 0; f < fingers; f++)
        {
            SDL_Finger* finger = SDL_GetTouchFinger(id, f);

            if (finger && finger->pressure > 0.0f)
            {
                SDL_Point pt = {finger->x * w, finger->y * h};
                processTouchKeyboardButton(pt);
            }
        }
    }
}

static void processTouchGamepad()
{
    if(!platform.gamepad.touch.counter)
        return;

    platform.gamepad.touch.counter--;

    const s32 size = platform.gamepad.touch.button.size;
    s32 x = 0, y = 0;

    tic80_gamepad* joystick = &platform.gamepad.touch.joystick.first;

    {
        SDL_Rect axis = {platform.gamepad.touch.button.axis.x, platform.gamepad.touch.button.axis.y, size*3, size*3};

        if(checkTouch(&axis, &x, &y))
        {
            x -= axis.x;
            y -= axis.y;

            s32 xt = x / size;
            s32 yt = y / size;

            if(yt == 0) joystick->up = true;
            else if(yt == 2) joystick->down = true;

            if(xt == 0) joystick->left = true;
            else if(xt == 2) joystick->right = true;

            if(xt == 1 && yt == 1)
            {
                xt = (x - size)/(size/3);
                yt = (y - size)/(size/3);

                if(yt == 0) joystick->up = true;
                else if(yt == 2) joystick->down = true;

                if(xt == 0) joystick->left = true;
                else if(xt == 2) joystick->right = true;
            }
        }
    }

    {
        SDL_Rect a = {platform.gamepad.touch.button.a.x, platform.gamepad.touch.button.a.y, size, size};
        if(checkTouch(&a, &x, &y)) joystick->a = true;
    }

    {
        SDL_Rect b = {platform.gamepad.touch.button.b.x, platform.gamepad.touch.button.b.y, size, size};
        if(checkTouch(&b, &x, &y)) joystick->b = true;
    }

    {
        SDL_Rect xb = {platform.gamepad.touch.button.x.x, platform.gamepad.touch.button.x.y, size, size};
        if(checkTouch(&xb, &x, &y)) joystick->x = true;
    }

    {
        SDL_Rect yb = {platform.gamepad.touch.button.y.x, platform.gamepad.touch.button.y.y, size, size};
        if(checkTouch(&yb, &x, &y)) joystick->y = true;
    }
}

#endif

static s32 getAxisMask(SDL_Joystick* joystick)
{
    s32 mask = 0;

    s32 axesCount = SDL_JoystickNumAxes(joystick);

    for (s32 a = 0; a < axesCount; a++)
    {
        s32 axe = SDL_JoystickGetAxis(joystick, a);

        if (axe)
        {
            if (a == 0)
            {
                if (axe > 16384) mask |= SDL_HAT_RIGHT;
                else if(axe < -16384) mask |= SDL_HAT_LEFT;
            }
            else if (a == 1)
            {
                if (axe > 16384) mask |= SDL_HAT_DOWN;
                else if (axe < -16384) mask |= SDL_HAT_UP;
            }
        }
    }

    return mask;
}

static s32 getJoystickHatMask(s32 hat)
{
    tic80_gamepads gamepad;
    gamepad.data = 0;

    gamepad.first.up = hat & SDL_HAT_UP;
    gamepad.first.down = hat & SDL_HAT_DOWN;
    gamepad.first.left = hat & SDL_HAT_LEFT;
    gamepad.first.right = hat & SDL_HAT_RIGHT;

    return gamepad.data;
}

static void processJoysticks()
{
    tic_mem* tic = platform.studio->tic;
    platform.gamepad.joystick.data = 0;
    s32 index = 0;

    for(s32 i = 0; i < COUNT_OF(platform.gamepad.ports); i++)
    {
        SDL_Joystick* joystick = platform.gamepad.ports[i];

        if(joystick && SDL_JoystickGetAttached(joystick))
        {
            tic80_gamepad* gamepad = NULL;

            switch(index)
            {
            case 0: gamepad = &platform.gamepad.joystick.first; break;
            case 1: gamepad = &platform.gamepad.joystick.second; break;
            case 2: gamepad = &platform.gamepad.joystick.third; break;
            case 3: gamepad = &platform.gamepad.joystick.fourth; break;
            }

            if(gamepad)
            {
                gamepad->data |= getJoystickHatMask(getAxisMask(joystick));

                for (s32 h = 0; h < SDL_JoystickNumHats(joystick); h++)
                    gamepad->data |= getJoystickHatMask(SDL_JoystickGetHat(joystick, h));

                s32 numButtons = SDL_JoystickNumButtons(joystick);
                if(numButtons >= 2)
                {
                    gamepad->a = SDL_JoystickGetButton(joystick, 0);
                    gamepad->b = SDL_JoystickGetButton(joystick, 1);

                    if(numButtons >= 4)
                    {
                        gamepad->x = SDL_JoystickGetButton(joystick, 2);
                        gamepad->y = SDL_JoystickGetButton(joystick, 3);

                        if(numButtons >= 8)
                        {
                            // !TODO: We have to find a better way to handle gamepad MENU button
                            // atm we show game menu for only Pause Menu button on XBox one controller
                            // issue #1220
                            s32 back = SDL_JoystickGetButton(joystick, 7);

                            if(back)
                            {
                                tic80_input* input = &tic->ram.input;
                                input->keyboard.keys[0] = tic_key_escape;
                            }
                        }
                    }
                }

                index++;
            }
        }
    }
}

static void processGamepad()
{
    processJoysticks();
    
    {
        tic_mem* tic = platform.studio->tic;
        tic80_input* input = &tic->ram.input;

        input->gamepads.data = 0;

#if defined(TOUCH_INPUT_SUPPORT)
        input->gamepads.data |= platform.gamepad.touch.joystick.data;
#endif        
        input->gamepads.data |= platform.gamepad.joystick.data;
    }
}

#if defined(TOUCH_INPUT_SUPPORT)
static void processTouchInput()
{
    s32 devices = SDL_GetNumTouchDevices();
    
    for (s32 i = 0; i < devices; i++)
        if(SDL_GetNumTouchFingers(SDL_GetTouchDevice(i)) > 0)
        {
            platform.gamepad.touch.counter = TOUCH_TIMEOUT;
            break;
        }

    if(isGamepadVisible())
        processTouchGamepad();
    else
        processTouchKeyboard();
}
#endif

static void handleKeydown(SDL_Keycode keycode, bool down, bool* state)
{
    static const u32 KeyboardCodes[tic_keys_count] = 
    {
        #include "keycodes.inl"
    };

    for(tic_key i = 0; i < COUNT_OF(KeyboardCodes); i++)
    {
        if(KeyboardCodes[i] == keycode)
        {
            state[i] = down;
            break;
        }
    }

#if defined(__TIC_ANDROID__)
    if(keycode == SDLK_AC_BACK)
        state[tic_key_escape] = down;
#endif
}

void tic_sys_poll()
{
    tic_mem* tic = platform.studio->tic;
    tic80_input* input = &tic->ram.input;

    if((bool)input->mouse.relative != (bool)SDL_GetRelativeMouseMode())
        SDL_SetRelativeMouseMode(input->mouse.relative ? SDL_TRUE : SDL_FALSE);

    SDL_memset(input, 0, sizeof(tic80_input));

    // keep relative mode enabled
    input->mouse.relative = SDL_GetRelativeMouseMode() ? 1 : 0;

#if defined(TOUCH_INPUT_SUPPORT)
    ZEROMEM(platform.gamepad.touch.joystick);
    ZEROMEM(platform.keyboard.touch.state);
#endif

#if defined(__TIC_ANDROID__)
    // SDL2 doesn't send SDL_KEYUP for backspace button on Android sometimes
    platform.keyboard.state[tic_key_backspace] = false;
#endif

    SDL_Event event;

// workaround for issue #1332 to not process keyboard when window gained focus
#if defined(__LINUX__)
    static s32 lockInput = 0;

    if(lockInput)
        lockInput--;
#endif

    // Workaround for freeze on fullscreen under macOS #819
    SDL_PumpEvents();
    while(SDL_PollEvent(&event))
    {
        switch(event.type)
        {
        case SDL_MOUSEWHEEL:
            {
                input->mouse.scrollx = event.wheel.x;
                input->mouse.scrolly = event.wheel.y;
            }
            break;
        case SDL_JOYDEVICEADDED:
            {
                s32 id = event.jdevice.which;

                if (id < TIC_GAMEPADS)
                {
                    if(platform.gamepad.ports[id])
                        SDL_JoystickClose(platform.gamepad.ports[id]);

                    platform.gamepad.ports[id] = SDL_JoystickOpen(id);
                }
            }
            break;

        case SDL_JOYDEVICEREMOVED:
            {
                s32 id = event.jdevice.which;

                if (id < TIC_GAMEPADS && platform.gamepad.ports[id])
                {
                    SDL_JoystickClose(platform.gamepad.ports[id]);
                    platform.gamepad.ports[id] = NULL;
                }
            }
            break;
        case SDL_WINDOWEVENT:
            switch(event.window.event)
            {
            case SDL_WINDOWEVENT_SIZE_CHANGED:
                {

#if defined(CRT_SHADER_SUPPORT)
                    s32 w = 0, h = 0;
                    SDL_GetWindowSize(platform.window, &w, &h);
                    GPU_SetWindowResolution(w, h);
                    GPU_SetVirtualResolution(platform.gpu.renderer, w, h);
#endif

#if defined(TOUCH_INPUT_SUPPORT)
                    updateGamepadParts();
#endif                    
                }
                break;
#if defined(__LINUX__)
            case SDL_WINDOWEVENT_FOCUS_GAINED: 
                // lock input for 10 ticks
                lockInput = 10;
                break;
#endif
            }
            break;

        case SDL_KEYDOWN:

#if defined(TOUCH_INPUT_SUPPORT)
            platform.keyboard.touch.useText = false;
            handleKeydown(event.key.keysym.sym, true, platform.keyboard.touch.state);

            if(event.key.keysym.sym != SDLK_AC_BACK)
                if(!SDL_IsTextInputActive())
                    SDL_StartTextInput();
#endif

            handleKeydown(event.key.keysym.sym, true, platform.keyboard.state);
            break;
        case SDL_KEYUP:
            handleKeydown(event.key.keysym.sym, false, platform.keyboard.state);
            break;
        case SDL_TEXTINPUT:
            if(strlen(event.text.text) == 1)
                platform.keyboard.text = event.text.text[0];
            break;
        case SDL_QUIT:
            platform.studio->exit();
            break;
        default:
            break;
        }
    }

#if defined(__LINUX__)
    if(lockInput)
        return;
#endif    

    processMouse();

#if defined(TOUCH_INPUT_SUPPORT)
    processTouchInput();
#endif

    processKeyboard();
    processGamepad();
}

bool tic_sys_keyboard_text(char* text)
{
#if defined(TOUCH_INPUT_SUPPORT)
    if(platform.keyboard.touch.useText)
        return false;
#endif

    *text = platform.keyboard.text;
    return true;
}

static void blitSound()
{
    tic_mem* tic = platform.studio->tic;    
    SDL_QueueAudio(platform.audio.device, tic->samples.buffer, tic->samples.size);
}

#if defined(TOUCH_INPUT_SUPPORT)

static void renderKeyboard()
{
    if(!isKbdVisible()) return;

    SDL_Rect rect;
    SDL_GetWindowSize(platform.window, &rect.w, &rect.h);

    Rect src = {TIC80_OFFSET_LEFT, TIC80_OFFSET_TOP, KBD_COLS*TIC_SPRITESIZE, KBD_ROWS*TIC_SPRITESIZE};
    Rect dst = {0, rect.h - src.h * rect.w / src.w, rect.w, src.h * rect.w / src.w};

    renderCopy(platform.gpu.renderer, platform.keyboard.touch.texture.up, src, dst);

    const tic80_input* input = &platform.studio->tic->ram.input;

    enum{Cols=KBD_COLS, Rows=KBD_ROWS};

    for(s32 i = 0; i < COUNT_OF(input->keyboard.keys); i++)
    {
        tic_key key = input->keyboard.keys[i];

        if(key > tic_key_unknown)
        {
            for(s32 k = 0; k < COUNT_OF(KbdLayout); k++)
            {
                if(key == KbdLayout[k])
                {
                    Rect src2 = 
                    {
                        (k % Cols) * TIC_SPRITESIZE + TIC80_OFFSET_LEFT, 
                        (k / Cols) * TIC_SPRITESIZE + TIC80_OFFSET_TOP, 
                        TIC_SPRITESIZE, 
                        TIC_SPRITESIZE,
                    };

                    Rect dst2 = 
                    {
                        (src2.x - TIC80_OFFSET_LEFT) * rect.w/src.w, 
                        (src2.y - TIC80_OFFSET_TOP) * rect.w/src.w + dst.y, 
                        TIC_SPRITESIZE * rect.w/src.w, 
                        TIC_SPRITESIZE * rect.w/src.w,
                    };

                    renderCopy(platform.gpu.renderer, platform.keyboard.touch.texture.down, src2, dst2);
                }
            }
        }
    }
}

static void renderGamepad()
{
    if(!platform.gamepad.touch.counter)
        return;

    const s32 tileSize = platform.gamepad.touch.button.size;
    const SDL_Point axis = platform.gamepad.touch.button.axis;
    typedef struct { bool press; s32 x; s32 y;} Tile;
    const tic80_input* input = &platform.studio->tic->ram.input;
    const Tile Tiles[] =
    {
        {input->gamepads.first.up,     axis.x + 1*tileSize, axis.y + 0*tileSize},
        {input->gamepads.first.down,   axis.x + 1*tileSize, axis.y + 2*tileSize},
        {input->gamepads.first.left,   axis.x + 0*tileSize, axis.y + 1*tileSize},
        {input->gamepads.first.right,  axis.x + 2*tileSize, axis.y + 1*tileSize},

        {input->gamepads.first.a,      platform.gamepad.touch.button.a.x, platform.gamepad.touch.button.a.y},
        {input->gamepads.first.b,      platform.gamepad.touch.button.b.x, platform.gamepad.touch.button.b.y},
        {input->gamepads.first.x,      platform.gamepad.touch.button.x.x, platform.gamepad.touch.button.x.y},
        {input->gamepads.first.y,      platform.gamepad.touch.button.y.x, platform.gamepad.touch.button.y.y},
    };

    enum { Left = TIC80_MARGIN_LEFT + 8 * TIC_SPRITESIZE};

    for(s32 i = 0; i < COUNT_OF(Tiles); i++)
    {
        const Tile* tile = Tiles + i;

#if defined(CRT_SHADER_SUPPORT)
        GPU_Rect src = { (float)i * TIC_SPRITESIZE + Left, (float)(tile->press ? TIC_SPRITESIZE : 0) + TIC80_MARGIN_TOP, (float)TIC_SPRITESIZE, (float)TIC_SPRITESIZE};
        GPU_Rect dest = { (float)tile->x, (float)tile->y, (float)tileSize, (float)tileSize};

        GPU_BlitScale(platform.gamepad.touch.texture, &src, platform.gpu.renderer, dest.x, dest.y,
            (float)dest.w / TIC_SPRITESIZE, (float)dest.h / TIC_SPRITESIZE);
#else
        SDL_Rect src = {i * TIC_SPRITESIZE + Left, (tile->press ? TIC_SPRITESIZE : 0) + TIC80_MARGIN_TOP, TIC_SPRITESIZE, TIC_SPRITESIZE};
        SDL_Rect dest = {tile->x, tile->y, tileSize, tileSize};
        SDL_RenderCopy(platform.gpu.renderer, platform.gamepad.touch.texture, &src, &dest);
#endif
    }
}

#endif

static void blitCursor(const u8* in)
{

    if(!platform.mouse.texture)
    {
#if defined(CRT_SHADER_SUPPORT)
        platform.mouse.texture = GPU_CreateImage(TIC_SPRITESIZE, TIC_SPRITESIZE, STUDIO_PIXEL_FORMAT);
        GPU_SetAnchor(platform.mouse.texture, 0, 0);
        GPU_SetImageFilter(platform.mouse.texture, GPU_FILTER_NEAREST);
#else
        platform.mouse.texture = SDL_CreateTexture(platform.gpu.renderer, STUDIO_PIXEL_FORMAT, SDL_TEXTUREACCESS_STREAMING, TIC_SPRITESIZE, TIC_SPRITESIZE);
        SDL_SetTextureBlendMode(platform.mouse.texture, SDL_BLENDMODE_BLEND);
#endif
    }

    if(platform.mouse.src != in)
    {
        platform.mouse.src = in;

        const u8* end = in + sizeof(tic_tile);
        const u32* pal = tic_tool_palette_blit(&platform.studio->tic->ram.vram.palette, platform.studio->tic->screen_format);
        static u32 data[TIC_SPRITESIZE*TIC_SPRITESIZE];
        u32* out = data;

        while(in != end)
        {
            u8 low = *in & 0x0f;
            u8 hi = (*in & 0xf0) >> TIC_PALETTE_BPP;
            *out++ = low ? *(pal + low) : 0;
            *out++ = hi ? *(pal + hi) : 0;

            in++;
        }

        updateTextureBytes(platform.mouse.texture, data, TIC_SPRITESIZE, TIC_SPRITESIZE);
    }

    SDL_Rect rect = {0, 0, 0, 0};
    calcTextureRect(&rect);
    s32 scale = rect.w / TIC80_WIDTH;

    s32 mx, my;
    SDL_GetMouseState(&mx, &my);

    if(platform.studio->config()->theme.cursor.pixelPerfect)
    {
        mx -= (mx - rect.x) % scale;
        my -= (my - rect.y) % scale;
    }

    if(SDL_GetWindowFlags(platform.window) & SDL_WINDOW_MOUSE_FOCUS)
    {
#if defined(CRT_SHADER_SUPPORT)
        GPU_BlitScale(platform.mouse.texture, NULL, platform.gpu.renderer, (float)mx, (float)my, (float)scale, (float)scale);
#else
        SDL_Rect rect = {mx, my, TIC_SPRITESIZE * scale, TIC_SPRITESIZE * scale};
        SDL_RenderCopy(platform.gpu.renderer, platform.mouse.texture, NULL, &rect);
#endif
    }
}

static void renderCursor()
{
    if(!platform.studio->tic->input.mouse)
    {
        SDL_ShowCursor(SDL_DISABLE);
        return;
    }

    if(SDL_GetRelativeMouseMode())
        return;

    if(platform.studio->tic->ram.vram.vars.cursor.system)
    {
        const StudioConfig* config = platform.studio->config();
        const tic_tiles* tiles = &config->cart->bank0.tiles;

        switch(platform.studio->tic->ram.vram.vars.cursor.sprite)
        {
        case tic_cursor_hand: 
            {
                if(config->theme.cursor.hand >= 0)
                {
                    SDL_ShowCursor(SDL_DISABLE);
                    blitCursor(tiles->data[config->theme.cursor.hand].data);
                }
                else
                {
                    SDL_ShowCursor(SDL_ENABLE);
                    SDL_SetCursor(platform.mouse.cursors[HandCursor]);
                }
            }
            break;
        case tic_cursor_ibeam:
            {
                if(config->theme.cursor.ibeam >= 0)
                {
                    SDL_ShowCursor(SDL_DISABLE);
                    blitCursor(tiles->data[config->theme.cursor.ibeam].data);
                }
                else
                {
                    SDL_ShowCursor(SDL_ENABLE);
                    SDL_SetCursor(platform.mouse.cursors[IBeamCursor]);
                }
            }
            break;
        default:
            {
                if(config->theme.cursor.arrow >= 0)
                {
                    SDL_ShowCursor(SDL_DISABLE);
                    blitCursor(tiles->data[config->theme.cursor.arrow].data);
                }
                else
                {
                    SDL_ShowCursor(SDL_ENABLE);
                    SDL_SetCursor(platform.mouse.cursors[ArrowCursor]);
                }
            }
        }
    }
    else
    {
        SDL_ShowCursor(SDL_DISABLE);
        blitCursor(platform.studio->tic->ram.sprites.data[platform.studio->tic->ram.vram.vars.cursor.sprite].data);
    }
}

static const char* getAppFolder()
{
    static char appFolder[TICNAME_MAX];

#if defined(__EMSCRIPTEN__)

        strcpy(appFolder, "/" TIC_PACKAGE "/" TIC_NAME "/");

#elif defined(__TIC_ANDROID__)

        strcpy(appFolder, SDL_AndroidGetExternalStoragePath());
        const char AppFolder[] = "/" TIC_NAME "/";
        strcat(appFolder, AppFolder);
        mkdir(appFolder, 0700);

#else

        char* path = SDL_GetPrefPath(TIC_PACKAGE, TIC_NAME);
        strcpy(appFolder, path);
        SDL_free(path);

#endif

    return appFolder;
}

void tic_sys_clipboard_set(const char* text)
{
    SDL_SetClipboardText(text);
}

bool tic_sys_clipboard_has()
{
    return SDL_HasClipboardText();
}

char* tic_sys_clipboard_get()
{
    return SDL_GetClipboardText();
}

void tic_sys_clipboard_free(const char* text)
{
    SDL_free((void*)text);
}

u64 tic_sys_counter_get()
{
    return SDL_GetPerformanceCounter();
}

u64 tic_sys_freq_get()
{
    return SDL_GetPerformanceFrequency();
}

void tic_sys_fullscreen()
{
#if defined(CRT_SHADER_SUPPORT)
    GPU_SetFullscreen(GPU_GetFullscreen() ? false : true, true);
#else
    SDL_SetWindowFullscreen(platform.window, 
        SDL_GetWindowFlags(platform.window) & SDL_WINDOW_FULLSCREEN_DESKTOP ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
#endif
}

void tic_sys_message(const char* title, const char* message)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, title, message, NULL);
}

void tic_sys_title(const char* title)
{
    if(platform.window)
        SDL_SetWindowTitle(platform.window, title);
}

#if defined(__WINDOWS__) || defined(__LINUX__) || defined(__MACOSX__)

void tic_sys_open_path(const char* path)
{
    char command[TICNAME_MAX];

#if defined(__WINDOWS__)

    sprintf(command, "explorer \"%s\"", path);

    wchar_t wcommand[TICNAME_MAX];
    MultiByteToWideChar(CP_UTF8, 0, command, TICNAME_MAX, wcommand, TICNAME_MAX);

    _wsystem(wcommand);

#elif defined(__LINUX__)

    sprintf(command, "xdg-open \"%s\"", path);
    if(system(command)){}

#elif defined(__MACOSX__)

    sprintf(command, "open \"%s\"", path);
    system(command);

#endif
}

#else

void tic_sys_open_path(const char* path) {}

#endif

void tic_sys_preseed()
{
#if defined(__MACOSX__)
    srandom(time(NULL));
    random();
#else
    srand((u32)time(NULL));
    rand();
#endif
}

#if defined(CRT_SHADER_SUPPORT)

static void loadCrtShader()
{
    const char* vertextShader = platform.studio->config()->shader.vertex;
    const char* pixelShader = platform.studio->config()->shader.pixel;

    if(!vertextShader)
        printf("Error: vertex shader is empty.\n");

    if(!pixelShader)
        printf("Error: pixel shader is empty.\n");

    u32 vertex = GPU_CompileShader(GPU_VERTEX_SHADER, vertextShader);
    
    if(!vertex)
    {
        printf("Failed to load vertex shader: %s\n", GPU_GetShaderMessage());
        return;
    }

    u32 pixel = GPU_CompileShader(GPU_PIXEL_SHADER, pixelShader);
    
    if(!pixel)
    {
        printf("Failed to load pixel shader: %s\n", GPU_GetShaderMessage());
        return;
    }
    
    if(platform.gpu.shader)
        GPU_FreeShaderProgram(platform.gpu.shader);

    platform.gpu.shader = GPU_LinkShaders(vertex, pixel);
    
    if(platform.gpu.shader)
    {
        platform.gpu.block = GPU_LoadShaderBlock(platform.gpu.shader, "gpu_Vertex", "gpu_TexCoord", "gpu_Color", "gpu_ModelViewProjectionMatrix");
        GPU_ActivateShaderProgram(platform.gpu.shader, &platform.gpu.block);
    }
    else
    {
        printf("Failed to link shader program: %s\n", GPU_GetShaderMessage());
    }
}
#endif

void tic_sys_update_config()
{
#if defined(TOUCH_INPUT_SUPPORT)
    if(platform.gpu.renderer)
        initTouchGamepad();
#endif
}

static void gpuTick()
{
    tic_mem* tic = platform.studio->tic;

    tic_sys_poll();

    if(platform.studio->quit)
    {
#if defined __EMSCRIPTEN__
        emscripten_cancel_main_loop();
#endif
        return;
    }

    platform.studio->tick();
    renderClear(platform.gpu.renderer);
    updateTextureBytes(platform.gpu.texture, tic->screen, TIC80_FULLWIDTH, TIC80_FULLHEIGHT);

#if defined(CRT_SHADER_SUPPORT)

    if(platform.studio->config()->crtMonitor)
    {
        if(platform.gpu.shader == 0)
            loadCrtShader();

        SDL_Rect rect = {0, 0, 0, 0};
        calcTextureRect(&rect);

        GPU_ActivateShaderProgram(platform.gpu.shader, &platform.gpu.block);

        GPU_SetUniformf(GPU_GetUniformLocation(platform.gpu.shader, "trg_x"), (float)rect.x);
        GPU_SetUniformf(GPU_GetUniformLocation(platform.gpu.shader, "trg_y"), (float)rect.y);
        GPU_SetUniformf(GPU_GetUniformLocation(platform.gpu.shader, "trg_w"), (float)rect.w);
        GPU_SetUniformf(GPU_GetUniformLocation(platform.gpu.shader, "trg_h"), (float)rect.h);

        {
            s32 w, h;
            SDL_GetWindowSize(platform.window, &w, &h);
            GPU_SetUniformf(GPU_GetUniformLocation(platform.gpu.shader, "scr_w"), (float)w);
            GPU_SetUniformf(GPU_GetUniformLocation(platform.gpu.shader, "scr_h"), (float)h);
        }

        GPU_BlitScale(platform.gpu.texture, NULL, platform.gpu.renderer, (float)rect.x, (float)rect.y,
            (float)rect.w / TIC80_FULLWIDTH, (float)rect.h / TIC80_FULLHEIGHT);
        GPU_DeactivateShaderProgram();
    }
    else

#endif

    {
        SDL_Rect rect = {0, 0, 0, 0};
        calcTextureRect(&rect);

        enum {Header = TIC80_OFFSET_TOP, Top = TIC80_OFFSET_TOP, Left = TIC80_OFFSET_LEFT};

        s32 width = 0;
        SDL_GetWindowSize(platform.window, &width, NULL);

        struct RectIt {Rect src; Rect dst;} Rects[] = 
        {
            {{0, 0, TIC80_FULLWIDTH, Header}, {0, 0, width, rect.y}},
            {{0, TIC80_FULLHEIGHT - Header, TIC80_FULLWIDTH, Header}, {0, rect.y + rect.h, width, rect.y}},
            {{0, Header, Left, TIC80_HEIGHT}, {0, rect.y, width, rect.h}},
            {{Left, Top, TIC80_WIDTH, TIC80_HEIGHT}, {rect.x, rect.y, rect.w, rect.h}},
        };

        FOR(struct RectIt*, it, Rects)
            renderCopy(platform.gpu.renderer, platform.gpu.texture, it->src, it->dst);
    }

    renderCursor();

#   if defined(TOUCH_INPUT_SUPPORT)

    if(isGamepadVisible())
        renderGamepad();
    else
        renderKeyboard();
#   endif

    renderPresent(platform.gpu.renderer);
    blitSound();

    platform.keyboard.text = '\0';
}

#if defined(__EMSCRIPTEN__)

static void emsGpuTick()
{
    static double nextTick = -1.0;

    if(nextTick < 0.0)
        nextTick = emscripten_get_now();

    nextTick += 1000.0/TIC80_FRAMERATE;
    gpuTick();

    EM_ASM(
    {
        if(FS.syncFSRequests == 0 && Module.syncFSRequests)
        {
            Module.syncFSRequests = 0;
            FS.syncfs(false,function(){});
        }
    });

    double delay = nextTick - emscripten_get_now();

    if(delay < 0.0)
    {
        nextTick -= delay;
    }
    else
        emscripten_set_main_loop_timing(EM_TIMING_SETTIMEOUT, delay);
}

#endif

static void createMouseCursors()
{
    for(s32 i = 0; i < COUNT_OF(platform.mouse.cursors); i++)
        platform.mouse.cursors[i] = SDL_CreateSystemCursor(SystemCursors[i]);
}

static s32 start(s32 argc, char **argv, const char* folder)
{
    platform.studio = studioInit(argc, argv, TIC80_SAMPLERATE, folder);

    SCOPE(platform.studio->close())
    {
        if (platform.studio->config()->cli)
        {
            while (!platform.studio->quit)
                platform.studio->tick();
        }
        else
        {
            SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK);

            initSound();

            {
                const s32 Width = TIC80_FULLWIDTH * platform.studio->config()->uiScale;
                const s32 Height = TIC80_FULLHEIGHT * platform.studio->config()->uiScale;

                platform.window = SDL_CreateWindow( TIC_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                    Width, Height, SDL_WINDOW_SHOWN 
                        | SDL_WINDOW_RESIZABLE
#if defined(CRT_SHADER_SUPPORT)
                        | SDL_WINDOW_OPENGL
#endif            
#if !defined(__EMSCRIPTEN__) && !defined(__MACOSX__)
                        | SDL_WINDOW_ALLOW_HIGHDPI
#endif
                        );

                setWindowIcon();
                createMouseCursors();

                initGPU();

                if(platform.studio->config()->goFullscreen)
                    tic_sys_fullscreen();
            }

            SDL_PauseAudioDevice(platform.audio.device, 0);

#if defined(__EMSCRIPTEN__)
            emscripten_set_main_loop(emsGpuTick, 0, 1);
#else
            {
                u64 nextTick = SDL_GetPerformanceCounter();
                const u64 Delta = SDL_GetPerformanceFrequency() / TIC80_FRAMERATE;

                while (!platform.studio->quit)
                {
                    nextTick += Delta;
                
                    gpuTick();

                    {
                        s64 delay = nextTick - SDL_GetPerformanceCounter();

                        if(delay < 0)
                            nextTick -= delay;
                        else
                            SDL_Delay((u32)(delay * 1000 / SDL_GetPerformanceFrequency()));
                    }
                }
            }

#endif

#if defined(TOUCH_INPUT_SUPPORT)
            if(SDL_IsTextInputActive())
                SDL_StopTextInput();
#endif

            {
                destroyGPU();

#if defined(TOUCH_INPUT_SUPPORT)
                if(platform.gamepad.touch.pixels)
                    SDL_free(platform.gamepad.touch.pixels);

                if(platform.keyboard.touch.texture.upPixels)
                    SDL_free(platform.keyboard.touch.texture.upPixels);

                if(platform.keyboard.touch.texture.downPixels)
                    SDL_free(platform.keyboard.touch.texture.downPixels);
#endif    

                SDL_DestroyWindow(platform.window);
                SDL_CloseAudioDevice(platform.audio.device);

                for(s32 i = 0; i < COUNT_OF(platform.mouse.cursors); i++)
                    SDL_FreeCursor(platform.mouse.cursors[i]);
            }

        }
    }

    return 0;
}

#if defined(__EMSCRIPTEN__)

static void checkPreloadedFile()
{
    EM_ASM_(
    {
        if(Module.filePreloaded)
        {
            _emscripten_cancel_main_loop();
            var start = Module.startArg;
            dynCall('iiii', $0, [start.argc, start.argv, start.folder]);
        }
    }, start);
}

static s32 emsStart(s32 argc, char **argv, const char* folder)
{
    if (argc >= 2)
    {
        s32 pos = strlen(argv[1]) - strlen(".tic");
        if (pos >= 0 && strcmp(&argv[1][pos], ".tic") == 0)
        {
            const char* url = argv[1];

            {
                static char path[TICNAME_MAX];
                strcpy(path, folder);
                strcat(path, argv[1]);
                argv[1] = path;
            }

            EM_ASM_(
            {
                var start = {};
                start.argc = $0;
                start.argv = $1;
                start.folder = $2;

                Module.startArg = start;

                var file = PATH_FS.resolve(UTF8ToString($4));
                var dir = PATH.dirname(file);

                Module.filePreloaded = false;

                FS.createPreloadedFile(dir, PATH.basename(file), UTF8ToString($3), true, true, 
                    function()
                    {
                        Module.filePreloaded = true;
                    },
                    function(){}, false, false, function()
                    {
                        try{FS.unlink(file);}catch(e){}
                        FS.mkdirTree(dir);
                    });

            }, argc, argv, folder, url, argv[1]);

            emscripten_set_main_loop(checkPreloadedFile, 0, 0);

            return 0;
        }
    }

    return start(argc, argv, folder);
}

#endif

s32 main(s32 argc, char **argv)
{
#if defined(__TIC_WINDOWS__)
    {
        CONSOLE_SCREEN_BUFFER_INFO info;
        if(GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info) && !info.dwCursorPosition.X && !info.dwCursorPosition.Y)
            FreeConsole();
    }
#endif

    const char* folder = getAppFolder();

#if defined(__EMSCRIPTEN__)

    EM_ASM_
    (
        {
            Module.syncFSRequests = 0;

            var dir = UTF8ToString($0);
           
            FS.mkdirTree(dir);

            FS.mount(IDBFS, {}, dir);
            FS.syncfs(true, function(e)
            {
                dynCall('iiii', $1, [$2, $3, $0]);
            });

         }, folder, emsStart, argc, argv
    );

#else

    return start(argc, argv, folder);
    
#endif
}

// workaround to build app on Raspbian
#if defined(__RPI__)

#include <fcntl.h>
int fcntl64(int fd, int cmd)
{
    return fcntl(fd, cmd);
}

#endif