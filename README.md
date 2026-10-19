## ArcadeCapture made in GameMaker Studio

## HTML5 version

`html5/` contains a browser port that reuses the original sprites, backgrounds and music.
Open `html5/index.html` in a browser (or visit `/html5/` on the GitHub Pages site).

Two-player local capture-the-flag: grab the enemy flag and bring it back to your base while your own flag is home. First to 3 captures wins.

| | Move | Fire |
|---|---|---|
| Player 1 (Blue) | W A S D | Space / F |
| Player 2 (Red) | Arrow keys | Enter / `/` |

Enter starts the game, P or Esc pauses, M mutes. Five hits destroy a player, who drops any carried flag and respawns at their base. Touching your own dropped flag returns it home.

## Nintendo Switch version

`switch/` contains a C/SDL2 homebrew port that builds a `.nro` with devkitPro. See [switch/README.md](switch/README.md) for build steps and controls.
