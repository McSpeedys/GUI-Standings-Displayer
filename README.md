# LoL Intramural Standings

A small C++ desktop app (Dear ImGui + GLFW) for tracking and displaying tournament standings.
CMake downloads ImGui and GLFW automatically the first time you build you only need a compiler, CMake and Git.

## Build

**Windows (Visual Studio 2022 with "Desktop development with C++")**
```
cmake -S . -B build
cmake --build build --config Release
```
The app is at `build\Release\LolStandings.exe`.

**Windows (MinGW / MSYS2)**
```
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**macOS**: `brew install cmake`, then the same two commands as MinGW without `-G`.

**Linux**: install `libgl-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev`, then build the same way.

## Using it
- **Teams** tab: add teams (a team can only be removed once it has no recorded series).
- **Record** tab: pick the two teams, then click the winner's score (2-0 or 2-1). Every match is a best-of-3 series.
- **History** tab: every series, newest first; click **X** to undo a wrong result.
- **Settings** tab: tournament name, number of playoff spots (highlighted in gold), and text size for projecting.

### How standings are ranked
1. Series wins, then fewest series losses.
2. Teams with the same record are ranked by **head-to-head**: a mini-table of only the series played
   between those tied teams (series wins, then game differential in those series).
3. If that splits a 3+ way tie into a smaller tie, head-to-head is applied again among just the teams
   still tied (so if two teams remain level, whoever won their meeting goes higher).
4. If head-to-head can't separate them (e.g. they haven't played), overall game differential, then games won.

Teams placed above a tied team by head-to-head show **(H2H)** next to their name.

### Clinched / eliminated
Each team plays a fixed number of regular-season series (default 8, set in Settings); the **Left**
column shows how many remain, and the app won't record more than that for a team.
- **Blue name** = guaranteed a playoff spot, even if it loses every remaining series.
- **Red name** = guaranteed out, even if it wins every remaining series.

The app doesn't know the remaining schedule, but it does know every series has exactly one winner,
so the remaining series hand out a fixed number of wins. A team is colored only when the outcome
holds for every possible way those wins could be split. Ties are always assumed to go the wrong way,
so a team that's locked in only via a tiebreaker is colored once the season ends, when every team is
colored by its final position.

### Save file compatibility
Loads saves from every earlier version. First-version results (single winner, no score) count as a
series win with a 1-0 game score. New saves add one `G` line (series per team), which earlier
versions simply ignore, so older builds can still open them.

Everything saves automatically to `league_data.txt` in the folder you run the app from
copy that file to back up or move the league.
