# MP7 40-Round Magazine for Geronimo

A [GML](https://github.com/RemArrow/geronimo-mod-loader) plugin that gives Geronimo's MP7 a
40-round magazine. Every MP7 magazine, whether in the gun, in a pouch or on its own:

- **holds 40 rounds.** A full one is filled to 40, and reloads and pickups fill it to 40.
- **looks like a 40-rounder.** It reaches about 8 cm below the grip, where the game's 20-rounder
  sits flush.
- **is held where it should be.** The hand cups its baseplate, as it does on the 20, and the whole
  magazine can be grabbed, including the part below the 20's length.

Nothing in the game is replaced. The magazine is still the game's own: its handling, reloads and
the gun's feeding are untouched, and only its capacity and look change.

## Credits

**3D model and textures:** [HK MP7 SMG](https://www.cgtrader.com/free-3d-models/military/gun/smg-mp7)
by **Alpooo**, used under CGTrader's Royalty Free License (no AI). Only its magazine is used, built
into the plugin DLL encrypted, as that licence asks for use in software (§21A.3). The model may not
be extracted from the DLL, redistributed, or used to train AI.

Plugin by RemArrow. Not affiliated with or endorsed by Alpooo, CGTrader, Heckler & Koch or Dark
Matter Studios.

## How it works

1. **Which magazines.** The MP7 (`Firearm_SMG_MP7A2`) takes one magazine class,
   `Magazine_MP7A2_20rnd`. Each one is caught as it begins play, and again by a check four times a
   second, so magazines from every source (gun stations, pouches, ammo boxes) are covered.
2. **Capacity.** `AmmoCapacity` goes to 40. A magazine that is full when it appears is filled to
   40. That happens on the machine with authority over it, and `CurrentAmmo` replicates from there.
3. **Look.** The magazine's two components showing the 20-round mesh are hidden, along with the
   game's rounds, which are laid out for the 20's shape. The 40 hangs on the magazine's root, in the
   20's mesh space, so it goes wherever the magazine goes.
4. **Hand.** The magazine's `HandSocket` (a VRExpansion hand socket) sits 1.36 cm under the 20's
   baseplate. It is moved by (+1.90, 0, −7.23) cm, to the same place under the 40's baseplate. A
   query-only box covers the part of the 40 below the 20's collision, so hands find it there too. It
   follows the magazine's own collision (off while the game has it off) and never takes part in
   physics.

## Assets

`tools/prepare_mag.py` (Blender, headless) builds `Assets/` from the model's `.blend`. `Assets/`
and the model's files are never committed.

- **Fit:** the model is about 10% larger than the game's MP7: its grip is 3.15 cm wide where the
  game's is 2.84. So it is scaled by 0.90, which also makes its baseplate as wide as the game's
  grip. It is placed so it leaves the grip where the 20-rounder's baseplate begins (z = −8.0 in the
  20's mesh space), with the two magazine bodies centred on each other there.
- **Textures:** the model shares one 4K atlas across the whole gun, with the magazine's pieces
  spread over all of it (8% of its area). The magazine gets a UV layout of its own, and its colour,
  roughness, metallic and normal are baked onto it at 1024 (the normal in tangent space). Normal
  and ORM are pre-encoded for the game's sRGB import, and the normal's green is flipped for UE.

`tools/pack` encrypts the assets (AES-256-GCM, a new key every build) into the payload that
`src/MP7Mag40.rc` builds into the DLL (`src/payload.h`).

## Install

Needs [GML 2.2.0 or newer](https://github.com/RemArrow/geronimo-mod-loader/releases). Close the
game, download `MP7Mag40-<version>.zip` from
[Releases](https://github.com/RemArrow/geronimo-mp7-40rnd-mag/releases) and extract it into
`...\GERONIMO\Geronimo\Binaries\Win64\`. It adds `GML\plugins\MP7Mag40\`. To remove the plugin,
delete that folder.

## Build from source

The model isn't in this repo. Download it from
[CGTrader](https://www.cgtrader.com/free-3d-models/military/gun/smg-mp7). The script takes the
`Magazine` object from its `mp7.blend`, with the textures packed in it as they come. Then:

```bat
git clone --recursive https://github.com/RemArrow/geronimo-mp7-40rnd-mag
set MP7_BLEND=...\mp7.blend
blender.exe --background --python tools\prepare_mag.py
build.bat install    rem refuses while the game runs
```

Needs Visual Studio 2022 (C++ desktop workload) and Blender.

## Settings: `GML\config\com.remarrow.mp7mag40.cfg`

| Key | Default | |
|---|---|---|
| `[Magazine] FortyRounds` | true | MP7 magazines hold 40 and look like the 40-rounder |

## Testing

`tests/MP7Mag40Dev` is the in-game harness (built, never installed). It spawns an MP7, loads it the
way the game does, and logs the magazine's capacity, rounds, components and hand socket. A camera of
its own takes close-ups of the magazine in the gun and on its own, with a marker on the hand socket.
Then it fires the gun dry, one shot per cycle, counting the game's own `OnFired`.

Verified in game:
- a fresh magazine holds 40 of 40;
- the gun fired exactly **40** rounds from one (39 from the magazine plus the one the insert
  chambered), leaving the magazine at 0 of 40 and the chamber empty;
- a magazine spawned on its own is 40 of 40;
- the 40 sits in the grip as intended, and the hand socket is centred under its baseplate.

## Known limitations

- A dropped 40 still collides like the 20 in physics, so its lower part can sink into the floor.
- **Multiplayer:** the round count is decided by the host. Players without the plugin see the game's
  20-round magazine. Verified in a two-player test (`Utils\MPDev`, 2026-10-13): a client's MP7 came with a
  40-round magazine on both machines (40/40, the 40's mesh), and its shot, fired by the host, took a round
  from it.
- The model's magazine top carries a "30" marking from the original model.
