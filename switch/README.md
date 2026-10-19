# Arcade Capture — Nintendo Switch port

A C/SDL2 port of Arcade Capture built with the open-source [devkitPro](https://devkitpro.org/) toolchain
(libnx). It produces a homebrew `.nro` that runs from the Homebrew Menu. It reuses the sprites, backgrounds and
music from the repository root, which are packed into the `.nro`'s RomFS at build time.

> This is a **homebrew** build. Releasing on the Nintendo eShop requires joining the Nintendo Developer
> Portal and using Nintendo's official SDK (or GameMaker's licensed Switch export), which can't be done
> from an open repository.

## Build

With Docker (no local toolchain needed), from the repository root:

```sh
docker run --rm -v "$PWD":/src -w /src/switch devkitpro/devkita64 make
```

Or with devkitPro installed (`dkp-pacman -S switch-dev switch-sdl2 switch-sdl2_image switch-sdl2_mixer`):

```sh
cd switch && make
```

Either way the output is `switch/ArcadeCapture.nro`. Copy it to `/switch/` on the SD card and launch it from
the Homebrew Menu (launch the Homebrew Menu by holding R while opening a game, rather than from the Album applet,
so the game gets full memory).

### Desktop build (for testing)

The same source builds on Linux/macOS with SDL2, SDL2_image and SDL2_mixer installed:

```sh
cd switch && make -f Makefile.pc && ./arcadecapture
```

It uses the keyboard controls from the HTML5 version.

## Controls

**One controller** (handheld, Joy-Con grip or Pro Controller), shared by both players:

| | Move | Fire |
|---|---|---|
| Player 1 (Blue) | Left stick / D-pad | L / ZL |
| Player 2 (Red) | Right stick / A B X Y | R / ZR |

**Two controllers** (for example two Joy-Cons held sideways): each player moves with the stick and fires with
any face, shoulder or SL/SR button. On the title screen press **−** to open the system controller screen and
split or pair Joy-Cons.

| Button | Title | In game | Paused |
|---|---|---|---|
| A | Start | — | — |
| + | Exit to Homebrew Menu | Pause | Resume |
| − | Controller setup | — | Quit to title |

## Layout

| Path | Purpose |
|---|---|
| `source/main.c` | The whole game: rendering, input, gameplay (mirrors `html5/game.js`) |
| `Makefile` | libnx build; stages `../sprites`, `../background` and `../sound` into `romfs/` |
| `Makefile.pc` | Desktop build |
| `icon.jpg` | 256×256 Homebrew Menu icon |
