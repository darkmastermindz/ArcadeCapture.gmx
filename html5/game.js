// Arcade Capture - HTML5 port of the GameMaker Studio project.
// Two-player local capture-the-flag: Blue (WASD + Space) vs Red (Arrows + Enter).
(function () {
  "use strict";

  // ---------------------------------------------------------------------------
  // Constants
  // ---------------------------------------------------------------------------
  var ROOM_W = 1024;
  var ROOM_H = 768;
  var BORDER = 20;            // thickness of the green frame in bkg_play
  var TILE = 32;              // spr_wall size

  var PLAYER_SPEED = 260;     // px/s
  var CARRIER_SPEED = 220;    // px/s while holding the enemy flag
  var PLAYER_HALF = 20;       // collision half-size (sprite is 80x80, glow included)
  var MAX_HP = 5;             // spr_health* has 5 frames
  var FIRE_COOLDOWN = 0.3;
  var BULLET_SPEED = 620;
  var BULLET_RADIUS = 6;
  var RESPAWN_TIME = 2;
  var INVULN_TIME = 1.5;
  var FLAG_SCALE = 0.55;
  var FLAG_HALF = 30;         // pickup reach for a scaled flag
  var BASE_HALF = 57;         // spr_bluegoal is 114x114
  var WIN_SCORE = 3;

  var ASSET_ROOT = "../";

  // ---------------------------------------------------------------------------
  // Assets
  // ---------------------------------------------------------------------------
  var SPRITES = {
    spr_blue: 3, spr_red: 2, spr_bluegoal: 2,
    spr_bulletb: 2, spr_bulletr: 2,
    spr_flagb: 1, spr_flagr: 1,
    spr_healthb: 5, spr_healthr: 5,
    spr_wall: 1
  };
  var BACKGROUNDS = ["bkg_title", "bkg_play"];

  var img = {};
  var pending = 0;

  function loadImage(key, src) {
    var im = new Image();
    pending++;
    im.onload = im.onerror = function () { pending--; };
    im.src = src;
    img[key] = im;
  }

  Object.keys(SPRITES).forEach(function (name) {
    for (var i = 0; i < SPRITES[name]; i++) {
      loadImage(name + "_" + i, ASSET_ROOT + "sprites/images/" + name + "_" + i + ".png");
    }
  });
  BACKGROUNDS.forEach(function (name) {
    loadImage(name, ASSET_ROOT + "background/images/" + name + ".png");
  });

  function makeSound(name) {
    var a = new Audio(ASSET_ROOT + "sound/audio/" + name + ".mp3");
    a.loop = true;
    a.volume = 0.6;
    return a;
  }
  var snd = { intro: makeSound("snd_intro"), main: makeSound("snd_main") };
  var currentMusic = null;
  var muted = false;

  // Plays one track and stops the other (what obj_music was meant to do).
  function playMusic(track) {
    if (currentMusic && currentMusic !== track) {
      currentMusic.pause();
      currentMusic.currentTime = 0;
    }
    currentMusic = track;
    track.muted = muted;
    var p = track.play();
    if (p && p.catch) p.catch(function () { /* blocked until first input */ });
  }

  // ---------------------------------------------------------------------------
  // Input
  // ---------------------------------------------------------------------------
  var keys = {};
  var pressed = {};

  var CONTROLS = {
    blue: { up: ["KeyW"], down: ["KeyS"], left: ["KeyA"], right: ["KeyD"], fire: ["Space", "KeyF"] },
    red: { up: ["ArrowUp"], down: ["ArrowDown"], left: ["ArrowLeft"], right: ["ArrowRight"],
           fire: ["Enter", "NumpadEnter", "Numpad0", "Slash"] }
  };
  var GAME_KEYS = ["KeyW", "KeyA", "KeyS", "KeyD", "Space", "ArrowUp", "ArrowDown",
                   "ArrowLeft", "ArrowRight", "Enter", "Slash"];

  window.addEventListener("keydown", function (e) {
    if (GAME_KEYS.indexOf(e.code) !== -1) e.preventDefault();
    if (!keys[e.code]) pressed[e.code] = true;
    keys[e.code] = true;
    // Browsers block autoplay until the page gets input; retry on first key.
    if (currentMusic && currentMusic.paused) playMusic(currentMusic);
  });
  window.addEventListener("keyup", function (e) { keys[e.code] = false; });
  window.addEventListener("blur", function () { keys = {}; });

  function down(list) {
    for (var i = 0; i < list.length; i++) if (keys[list[i]]) return true;
    return false;
  }
  function hit(code) { return !!pressed[code]; }

  // ---------------------------------------------------------------------------
  // Level layout (mirrored left/right), in tiles of 32px
  // ---------------------------------------------------------------------------
  var walls = [];
  function addWall(tx, ty, tw, th) {
    walls.push({ x: tx * TILE, y: ty * TILE, w: tw * TILE, h: th * TILE });
    var mx = ROOM_W - (tx + tw) * TILE;
    if (mx !== tx * TILE) walls.push({ x: mx, y: ty * TILE, w: tw * TILE, h: th * TILE });
  }
  // Cover in front of each base
  addWall(7, 6, 1, 4);
  addWall(7, 14, 1, 4);
  // Mid-field blocks
  addWall(10, 3, 3, 1);
  addWall(10, 20, 3, 1);
  addWall(12, 10, 1, 4);
  // Center pillar
  addWall(15, 11, 2, 2);

  // ---------------------------------------------------------------------------
  // Entities
  // ---------------------------------------------------------------------------
  var BASES = {
    blue: { x: 90, y: ROOM_H / 2 },
    red: { x: ROOM_W - 90, y: ROOM_H / 2 }
  };

  function makePlayer(team) {
    var base = BASES[team];
    return {
      team: team,
      enemy: team === "blue" ? "red" : "blue",
      spawnX: base.x + (team === "blue" ? 90 : -90),
      spawnY: base.y,
      x: 0, y: 0,
      dirX: team === "blue" ? 1 : -1, dirY: 0,
      hp: MAX_HP,
      cooldown: 0,
      dead: false,
      respawn: 0,
      invuln: 0,
      score: 0
    };
  }

  function makeFlag(team) {
    var base = BASES[team];
    return { team: team, homeX: base.x, homeY: base.y, x: base.x, y: base.y, carrier: null };
  }

  var players, flags, bullets, effects, winner, banner;

  function spawn(p) {
    p.x = p.spawnX;
    p.y = p.spawnY;
    p.dirX = p.team === "blue" ? 1 : -1;
    p.dirY = 0;
    p.hp = MAX_HP;
    p.dead = false;
    p.invuln = INVULN_TIME;
    p.cooldown = 0;
  }

  function resetMatch() {
    players = { blue: makePlayer("blue"), red: makePlayer("red") };
    flags = { blue: makeFlag("blue"), red: makeFlag("red") };
    bullets = [];
    effects = [];
    winner = null;
    banner = { text: "FIRST TO " + WIN_SCORE + " CAPTURES", color: "#00ff00", t: 2 };
    spawn(players.blue);
    spawn(players.red);
  }

  // ---------------------------------------------------------------------------
  // Collision helpers
  // ---------------------------------------------------------------------------
  function rectsOverlap(ax, ay, aw, ah, bx, by, bw, bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
  }

  function boxBlocked(cx, cy, half) {
    var x = cx - half, y = cy - half, s = half * 2;
    if (x < BORDER || y < BORDER || x + s > ROOM_W - BORDER || y + s > ROOM_H - BORDER) return true;
    for (var i = 0; i < walls.length; i++) {
      var w = walls[i];
      if (rectsOverlap(x, y, s, s, w.x, w.y, w.w, w.h)) return true;
    }
    return false;
  }

  function near(ax, ay, bx, by, dist) {
    return Math.abs(ax - bx) < dist && Math.abs(ay - by) < dist;
  }

  // ---------------------------------------------------------------------------
  // Game logic
  // ---------------------------------------------------------------------------
  function dropFlag(p) {
    var f = flags[p.enemy];
    if (f.carrier === p) {
      f.carrier = null;
      f.x = p.x;
      f.y = p.y;
    }
  }

  function returnFlag(f) {
    f.carrier = null;
    f.x = f.homeX;
    f.y = f.homeY;
  }

  function flagHome(f) {
    return !f.carrier && f.x === f.homeX && f.y === f.homeY;
  }

  function burst(x, y, team) {
    for (var i = 0; i < 14; i++) {
      var a = Math.random() * Math.PI * 2;
      var s = 80 + Math.random() * 220;
      effects.push({ x: x, y: y, vx: Math.cos(a) * s, vy: Math.sin(a) * s, life: 0.5, max: 0.5, team: team });
    }
  }

  function updatePlayer(p, dt) {
    if (p.dead) {
      p.respawn -= dt;
      if (p.respawn <= 0) spawn(p);
      return;
    }
    if (p.invuln > 0) p.invuln -= dt;
    if (p.cooldown > 0) p.cooldown -= dt;

    var c = CONTROLS[p.team];
    var mx = (down(c.right) ? 1 : 0) - (down(c.left) ? 1 : 0);
    var my = (down(c.down) ? 1 : 0) - (down(c.up) ? 1 : 0);
    if (mx || my) {
      var len = Math.sqrt(mx * mx + my * my);
      mx /= len; my /= len;
      p.dirX = mx; p.dirY = my;
      var speed = flags[p.enemy].carrier === p ? CARRIER_SPEED : PLAYER_SPEED;
      // Move one axis at a time so players slide along walls.
      var nx = p.x + mx * speed * dt;
      if (!boxBlocked(nx, p.y, PLAYER_HALF)) p.x = nx;
      var ny = p.y + my * speed * dt;
      if (!boxBlocked(p.x, ny, PLAYER_HALF)) p.y = ny;
    }

    if (down(c.fire) && p.cooldown <= 0) {
      p.cooldown = FIRE_COOLDOWN;
      bullets.push({
        team: p.team,
        x: p.x + p.dirX * 30, y: p.y + p.dirY * 30,
        vx: p.dirX * BULLET_SPEED, vy: p.dirY * BULLET_SPEED,
        angle: Math.atan2(p.dirY, p.dirX)
      });
    }

    // Flags
    var enemyFlag = flags[p.enemy];
    var ownFlag = flags[p.team];
    if (!enemyFlag.carrier && near(p.x, p.y, enemyFlag.x, enemyFlag.y, PLAYER_HALF + FLAG_HALF)) {
      enemyFlag.carrier = p;
    }
    if (!ownFlag.carrier && !flagHome(ownFlag) &&
        near(p.x, p.y, ownFlag.x, ownFlag.y, PLAYER_HALF + FLAG_HALF)) {
      returnFlag(ownFlag);
      banner = { text: p.team.toUpperCase() + " FLAG RETURNED", color: teamColor(p.team), t: 1.5 };
    }
    var base = BASES[p.team];
    if (enemyFlag.carrier === p && flagHome(ownFlag) &&
        near(p.x, p.y, base.x, base.y, PLAYER_HALF + BASE_HALF)) {
      returnFlag(enemyFlag);
      p.score++;
      burst(base.x, base.y, p.team);
      if (p.score >= WIN_SCORE) {
        winner = p.team;
      } else {
        banner = { text: p.team.toUpperCase() + " CAPTURES!", color: teamColor(p.team), t: 2 };
      }
    }
  }

  function updateBullets(dt) {
    for (var i = bullets.length - 1; i >= 0; i--) {
      var b = bullets[i];
      b.x += b.vx * dt;
      b.y += b.vy * dt;
      var gone = boxBlocked(b.x, b.y, BULLET_RADIUS);
      if (!gone) {
        var target = players[b.team === "blue" ? "red" : "blue"];
        if (!target.dead && near(b.x, b.y, target.x, target.y, PLAYER_HALF + BULLET_RADIUS)) {
          gone = true;
          if (target.invuln <= 0) {
            target.hp--;
            burst(b.x, b.y, b.team);
            if (target.hp <= 0) {
              dropFlag(target);
              target.dead = true;
              target.respawn = RESPAWN_TIME;
              burst(target.x, target.y, target.team);
            }
          }
        }
      }
      if (gone) bullets.splice(i, 1);
    }
  }

  function updateEffects(dt) {
    for (var i = effects.length - 1; i >= 0; i--) {
      var e = effects[i];
      e.x += e.vx * dt;
      e.y += e.vy * dt;
      e.vx *= 0.9; e.vy *= 0.9;
      e.life -= dt;
      if (e.life <= 0) effects.splice(i, 1);
    }
  }

  function updateFlags() {
    ["blue", "red"].forEach(function (t) {
      var f = flags[t];
      if (f.carrier) { f.x = f.carrier.x; f.y = f.carrier.y - 34; }
    });
  }

  // ---------------------------------------------------------------------------
  // Rooms / states
  // ---------------------------------------------------------------------------
  var state = "title";   // title | play | paused | over
  var clock = 0;
  var overTime = 0;      // Enter is Red's fire key, so ignore it briefly after a win

  function goTitle() {
    state = "title";
    playMusic(snd.intro);
  }

  function goPlay() {
    resetMatch();
    state = "play";
    playMusic(snd.main);
  }

  function update(dt) {
    clock += dt;

    if (hit("KeyM")) {
      muted = !muted;
      snd.intro.muted = snd.main.muted = muted;
    }

    if (state === "title") {
      if (hit("Enter") || hit("NumpadEnter")) goPlay();
    } else if (state === "play") {
      if (hit("Escape") || hit("KeyP")) { state = "paused"; return; }
      updatePlayer(players.blue, dt);
      updatePlayer(players.red, dt);
      updateFlags();
      updateBullets(dt);
      updateEffects(dt);
      if (banner) { banner.t -= dt; if (banner.t <= 0) banner = null; }
      if (winner) { state = "over"; banner = null; overTime = 0; }
    } else if (state === "paused") {
      if (hit("Escape") || hit("KeyP")) state = "play";
      if (hit("KeyQ")) goTitle();
    } else if (state === "over") {
      updateEffects(dt);
      overTime += dt;
      if (overTime > 1 && (hit("Enter") || hit("NumpadEnter"))) goTitle();
    }
  }

  // ---------------------------------------------------------------------------
  // Drawing
  // ---------------------------------------------------------------------------
  var canvas = document.getElementById("game");
  var ctx = canvas.getContext("2d");
  var FONT = "'Courier New', Courier, monospace";

  function teamColor(t) { return t === "blue" ? "#22e6ff" : "#ff3030"; }

  function draw(name, x, y, sx, sy) {
    var im = img[name];
    if (!im || !im.complete || !im.naturalWidth) return;
    sx = sx || 1; sy = sy || sx;
    ctx.save();
    ctx.translate(x, y);
    ctx.scale(sx, sy);
    ctx.drawImage(im, -im.naturalWidth / 2, -im.naturalHeight / 2);
    ctx.restore();
  }

  function text(str, x, y, size, color, align) {
    ctx.font = "bold " + size + "px " + FONT;
    ctx.textAlign = align || "center";
    ctx.textBaseline = "middle";
    ctx.shadowColor = color;
    ctx.shadowBlur = 12;
    ctx.fillStyle = color;
    ctx.fillText(str, x, y);
    ctx.shadowBlur = 0;
  }

  function drawTitle() {
    ctx.drawImage(img.bkg_title, 0, 0);
    // Placement mirrors rm_title: flags left of the logo, players at 15% / 75%.
    draw("spr_flagr_0", 320 - 41, 160 + 55, -1, 1);
    draw("spr_flagb_0", 352 - 48, 160 + 72, -1.17, 1.29);
    var pulse = Math.floor(clock * 4) % 2;
    draw("spr_blue_" + pulse, ROOM_W * 0.15 + 40, ROOM_H * 0.65 + 40);
    draw(pulse ? "spr_red_1" : "spr_red_0", ROOM_W * 0.75 + 40, ROOM_H * 0.65 + 40);

    text("SPACE: fire", 108, 728, 14, "#00ff00");
    text("ENTER: fire", ROOM_W - 92, 728, 14, "#00ff00");
    text("Capture the enemy flag and bring it home. First to " + WIN_SCORE + " wins.",
         ROOM_W / 2, 600, 15, "#00aa00");
    if (Math.floor(clock * 2) % 2 === 0) {
      ctx.fillStyle = "rgba(0,255,0,0.15)";
      ctx.fillRect(400, 632, 224, 36);
    }
  }

  function drawBase(team) {
    var b = BASES[team];
    if (team === "blue") {
      draw("spr_bluegoal_" + (Math.floor(clock * 2) % 2), b.x, b.y);
      return;
    }
    // No red goal sprite exists in the project; draw a matching one.
    var s = 70, glow = 0.5 + 0.25 * Math.sin(clock * Math.PI * 2);
    ctx.save();
    ctx.shadowColor = "#ff3030";
    ctx.shadowBlur = 20;
    ctx.fillStyle = "rgba(255,60,60," + (0.35 + glow * 0.3) + ")";
    ctx.fillRect(b.x - s / 2, b.y - s / 2, s, s);
    ctx.lineWidth = 3;
    ctx.strokeStyle = "#802020";
    ctx.strokeRect(b.x - s / 2 + 3, b.y - s / 2 + 3, s - 6, s - 6);
    ctx.restore();
    text("R", b.x, b.y + 2, 46, "#802020");
  }

  function drawFlag(f) {
    var name = f.team === "blue" ? "spr_flagb_0" : "spr_flagr_0";
    var bob = f.carrier ? 0 : Math.sin(clock * 3) * 3;
    draw(name, f.x, f.y + bob, FLAG_SCALE);
  }

  function drawPlayer(p) {
    if (p.dead) {
      // Respawn ghost at the spawn point using the small-orb frame.
      ctx.globalAlpha = 0.3 + 0.2 * Math.sin(clock * 10);
      draw(p.team === "blue" ? "spr_blue_2" : "spr_red_0", p.spawnX, p.spawnY);
      ctx.globalAlpha = 1;
      return;
    }
    if (p.invuln > 0 && Math.floor(clock * 16) % 2) ctx.globalAlpha = 0.35;
    var f = Math.floor(clock * 6) % 2;
    draw(p.team === "blue" ? "spr_blue_" + f : "spr_red_1", p.x, p.y);
    ctx.globalAlpha = 1;
    // Aim indicator
    ctx.fillStyle = teamColor(p.team);
    ctx.beginPath();
    ctx.arc(p.x + p.dirX * 28, p.y + p.dirY * 28, 3, 0, Math.PI * 2);
    ctx.fill();
  }

  function drawBullet(b) {
    var name = (b.team === "blue" ? "spr_bulletb_" : "spr_bulletr_") + (Math.floor(clock * 12) % 2);
    ctx.save();
    ctx.translate(b.x, b.y);
    ctx.rotate(b.angle);
    ctx.scale(1.6, 1.6);
    draw(name, 0, 0);
    ctx.restore();
  }

  function drawHud() {
    var bp = players.blue, rp = players.red;
    var bhp = bp.dead ? 1 : bp.hp, rhp = rp.dead ? 1 : rp.hp;
    draw("spr_healthb_" + (MAX_HP - bhp), 60, 60);
    draw("spr_healthr_" + (MAX_HP - rhp), ROOM_W - 60, 60);
    text("P1  " + bp.score, 100, 60, 28, teamColor("blue"), "left");
    text(rp.score + "  P2", ROOM_W - 100, 60, 28, teamColor("red"), "right");
    if (bp.dead) text("respawn " + Math.ceil(bp.respawn), 100, 92, 14, teamColor("blue"), "left");
    if (rp.dead) text("respawn " + Math.ceil(rp.respawn), ROOM_W - 100, 92, 14, teamColor("red"), "right");
  }

  function drawPlay() {
    ctx.drawImage(img.bkg_play, 0, 0);
    for (var i = 0; i < walls.length; i++) {
      var w = walls[i];
      for (var x = w.x; x < w.x + w.w; x += TILE) {
        for (var y = w.y; y < w.y + w.h; y += TILE) ctx.drawImage(img.spr_wall_0, x, y);
      }
    }
    drawBase("blue");
    drawBase("red");
    if (!flags.blue.carrier) drawFlag(flags.blue);
    if (!flags.red.carrier) drawFlag(flags.red);
    drawPlayer(players.blue);
    drawPlayer(players.red);
    if (flags.blue.carrier) drawFlag(flags.blue);
    if (flags.red.carrier) drawFlag(flags.red);
    bullets.forEach(drawBullet);
    effects.forEach(function (e) {
      ctx.globalAlpha = e.life / e.max;
      ctx.fillStyle = teamColor(e.team);
      ctx.fillRect(e.x - 2, e.y - 2, 4, 4);
    });
    ctx.globalAlpha = 1;
    drawHud();
    if (banner) {
      ctx.globalAlpha = Math.min(1, banner.t * 2);
      text(banner.text, ROOM_W / 2, 140, 36, banner.color);
      ctx.globalAlpha = 1;
    }
  }

  function overlay(title, color, sub) {
    ctx.fillStyle = "rgba(0,0,0,0.65)";
    ctx.fillRect(0, 0, ROOM_W, ROOM_H);
    text(title, ROOM_W / 2, ROOM_H / 2 - 30, 64, color);
    text(sub, ROOM_W / 2, ROOM_H / 2 + 40, 20, "#00ff00");
  }

  function render() {
    ctx.clearRect(0, 0, ROOM_W, ROOM_H);
    if (state === "title") {
      drawTitle();
    } else {
      drawPlay();
      if (state === "paused") overlay("PAUSED", "#00ff00", "P / Esc: resume    Q: quit to title");
      if (state === "over") {
        overlay(winner.toUpperCase() + " WINS", teamColor(winner),
                players.blue.score + " - " + players.red.score + "    Press Enter");
      }
    }
    if (muted) text("MUTED (M)", ROOM_W / 2, ROOM_H - 36, 12, "#00aa00");
  }

  function drawLoading() {
    ctx.fillStyle = "#000";
    ctx.fillRect(0, 0, ROOM_W, ROOM_H);
    text("LOADING...", ROOM_W / 2, ROOM_H / 2, 28, "#00ff00");
  }

  // ---------------------------------------------------------------------------
  // Main loop
  // ---------------------------------------------------------------------------
  var last = 0;
  var started = false;

  function frame(t) {
    var dt = Math.min(0.05, (t - last) / 1000 || 0);
    last = t;
    if (pending > 0) {
      drawLoading();
    } else {
      if (!started) { started = true; goTitle(); }
      update(dt);
      render();
    }
    pressed = {};
    requestAnimationFrame(frame);
  }

  canvas.focus();
  requestAnimationFrame(frame);
})();
