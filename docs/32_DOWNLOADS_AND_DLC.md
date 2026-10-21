# 32 — Downloads: shared test scenarios and DLC packs (plan)

The owner's two uses for downloading (2026-10-21):
1. **Test saves shared between developers.** A developer saves a moment of the game as a test, uploads it, and
   another developer downloads it and plays or tests the same moment.
2. **DLC packs.** New content without a new build: a new weapon (its item, its play settings, its art), new events,
   dialogues, monsters.

Both need the same download layer (section 3), and every download is checked against a file list of sizes and hashes
(section 4). Nothing is built yet. Section 7 lists the decisions the owner must make first: where the files live, and
who can upload.

## 1. Shared test scenarios

A test save is a **scenario** ([31](31_TEST_SCENARIOS.md)): a folder with `scenario.json` and a `save.json`. Scenarios
in `tests/scenarios/` are already shared through git. The cloud is for moments a developer catches **while
playing**, shared at once without a commit.

**Saving a moment:** a debug key (proposal: **F9**) in a development build writes the moment as a scenario folder:
- `save.json`: the run now (the save format);
- `scenario.json`: the scene now (the floor, or the battle with this enemy, the dialogue, the store ...), plus a
  `"_comment"` the developer types;
- `shot.png`: a screenshot.

It goes to `save/scenarios/<date>_<name>/`.

**Sharing it:**

```
tools\scenarios.cmd upload save/scenarios/2026-10-21_golem_bug    # to the shared storage
tools\scenarios.cmd list                                          # what is there (name, who, when, comment)
tools\scenarios.cmd get 2026-10-21_golem_bug                      # into tests/scenarios/_cloud/ (git-ignored)
toms_game --scenario=tests/scenarios/_cloud/2026-10-21_golem_bug  # play it
```

- **Uploading is a developer tool, never the shipped game.** The upload key is the developer's own, kept out of the
  repository and out of every build.
- **A downloaded scenario is not a test.** Scenarios in `_cloud/` are not ctest tests. To make one a permanent test,
  copy its folder into `tests/scenarios/` and commit it; it is then `scenario.<name>` like the others.
- **The storage keeps an index** (`index.json`: name, uploader, date, game version, comment), so `list` needs one
  download.
- **A scenario from an older game version** still loads: saves migrate ([28](28_SAVE_SYSTEM.md)). `list` shows the
  version.

## 2. DLC packs

A **pack** is data only, no code: new entries for the game's data files plus their art. A new weapon is an
`items.json` entry, a `weapon_play.json` entry (its play settings, [30](30_GAMEPLAY_PLAN.md) §5.6), its sprite and its
text. A new event is an event, its dialogue, its text. The game already reads all of these from data, so a pack
needs no new code per weapon or event.

```
packs/weapon_thunder_spear/
  pack.json
  data/items.json            only the new entries
  data/weapon_play.json
  data/text.json
  media/atlas/pack.atlas     the pack's sprites (+ its page PNG)
```

`pack.json`:

```json
{
  "id": "weapon_thunder_spear",
  "version": 3,
  "minGameVersion": "0.9",
  "name": { "en": "Thunder Spear", "zh_TW": "雷霆之槍" },
  "requires": [],
  "files": { "data/items.json": "sha256:…", "media/atlas/pack.atlas": "sha256:…" }
}
```

**How the game uses a pack:**

1. **Installed** packs live in the player's save folder: `save/packs/<id>/<version>/`. They are never written into
   the game's own `assets/`.
2. **At start**, every installed, enabled pack is checked:
   - each file's hash matches `pack.json`;
   - the game is at least `minGameVersion`;
   - every pack it `requires` is there.

   A pack that fails is skipped, and the player is told why.
3. **Merging:** each data file is the game's own file plus every pack's entries. A pack **adds** ids. An id the game
   or another pack already has is an error, and the pack is skipped, unless the pack lists it in `"overrides"`
   (whether that is allowed is one of the owner's decisions, Q24).
4. **The resource manager** looks for media in the packs first, then in the game. Pack sprites have their own atlas,
   so the game's atlas is never rebuilt.
5. **Saves record the packs they used** (id and version). A save whose packs are missing still loads. Its pack items
   and events are kept in the file and come back when the pack is installed again ([28](28_SAVE_SYSTEM.md): unknown
   fields kept).
6. **A tool checks a pack before it ships:**
   - every id is new;
   - every sprite is in its atlas;
   - every text key is in all six languages;
   - the weapon has a `weapon_play` entry;
   - the hashes are filled in.

   A pack is tested like the game: a scenario folder that uses it (the arena with the new weapon).

## 3. The download layer

One way to download, for both uses and every platform:

```cpp
// C++17, callback style (proposal): `done` runs on the main thread, and never if `owner` is gone
res.fetch("packs/weapon_thunder_spear/3/pack.json", owner, [this](toms::FetchResult r) {
    if (r.ok) readPack(r.bytes); else showError(r.error);
});
```

- **The owner rule is the same as for timers** ([safe_timer.h](../src/core/engine/safe_timer.h)). A download finishing
  after the screen that asked for it is gone does nothing.
- **Per platform:**
  - the web uses `emscripten_fetch` (the browser downloads in the background);
  - desktop and Android do the HTTP request on a `job_system` worker.

  The result is handed to the main thread, where GPU uploads must happen.
- **Robustness:** progress for a progress bar, a retry with a growing wait, resuming a cut-off download for large
  files, and the size and hash checks of section 4 before anything is used.
- **Cache:** a file whose hash is already on disk is not downloaded again.
- **`async` / `await`:** C++20 coroutines can sit on top of `fetch` later (`co_await res.fetch(...)`). That needs the
  project moved from C++17 to C++20, which is a global build change. The callback version works first and does not
  wait for that.

## 4. Checking that a download is right: the file list

Every download (a pack, a shared scenario, a game update) comes with a **file list**: each file's path, size and
SHA-256 hash. The game, or the tool, trusts nothing it has not checked against that list.

### 4.1 The file list

```json
{
  "id": "weapon_thunder_spear",
  "version": 3,
  "created": "2026-10-21T10:00:00Z",
  "total": 1834211,
  "files": [
    { "path": "data/items.json",        "size": 2210,    "sha256": "9f2c…e41a" },
    { "path": "data/weapon_play.json",  "size": 412,     "sha256": "07bd…19c3" },
    { "path": "media/atlas/pack.atlas", "size": 1806,    "sha256": "c1aa…7f02" },
    { "path": "media/atlas/pack.png",   "size": 1829783, "sha256": "5e90…aa61" }
  ]
}
```

- For a pack, the list is the `files` part of `pack.json` (section 2). A shared scenario has its own `files.json`.
  The storage's `index.json` gives each list's own size and hash.
- **Why both a size and a hash:**
  - the size is free to check, catches a cut-off or empty download at once, and tells how far a resumed download got;
  - the hash catches everything else: a damaged byte, a wrong file served, a file changed on the server.

  SHA-256, not a CRC: a CRC finds accidents but not a file changed on purpose.
- **Paths** use `/`, are relative, and never contain `..`. Every path in the list must be unique. A list breaking
  these rules is refused as a whole.
- **Made by a tool, never by hand:** `tools/make_file_list.py <folder>` writes the list. The pack check tool and
  `scenarios.cmd upload` run it, so a list always matches what is uploaded.

### 4.2 Downloading one file

1. **Download to `<name>.part`**, never over the real file. A cut-off download never leaves a half-written file the
   game could read.
2. **Resume:** after a cut, if `<name>.part` exists and is shorter than the listed size, ask the server only for the
   rest (an HTTP `Range` request). A `.part` longer than the listed size is deleted and the file starts over.
3. **Size check:** when the download ends, the `.part` must be exactly the listed size. If not, retry.
4. **Hash check:** the SHA-256 of the `.part` must equal the listed hash. If not, delete it and retry. A resumed file
   whose hash is wrong is downloaded again from the start, not resumed.
5. **Move into place:** rename `<name>.part` to `<name>` (one atomic rename on the same disk). Only now can the game
   use it.
6. **Retries:** up to 3 times, waiting 1 s, 4 s, then 10 s. After that the file is marked failed, and so is the whole
   pack or scenario.

### 4.3 Downloading a set (a pack, a scenario)

1. **Get the file list first.** It is small. Check it against `index.json` (its size and hash) and, for a sold pack,
   its signature (section 5).
2. **Compare it with what is on disk:**
   - a file already there with the same size and hash is kept, not downloaded;
   - only new or changed files are downloaded.

   A new version of a 50 MB pack that changed one text file downloads one small file.
3. **Download the needed files**, a few at a time (at most 4 at once), each as in 4.2. Progress shows bytes done out
   of the bytes still needed.
4. **The set is complete** only when every file in the list is in place and checked. A `complete` marker
   (`.complete`, holding the list's hash) is written last.
   - The game uses a pack only when its marker is there and matches the list.
   - A set missing its marker (the download was stopped) is finished next time, not used half-done.
5. **Files on disk that are not in the list** (left from an older version) are deleted once the new set is complete.

### 4.4 Checking what is already installed

- **At every start (quick):**
  - every file in the list exists and has its listed size;
  - the `.complete` marker matches the list.

  This costs no hashing, so start-up stays fast.
- **In full** (every hash) when:
  - the quick check fails;
  - a file failed to load (a broken image, a JSON that does not parse);
  - the player chooses **Repair** for a pack;
  - once a week in the background.
- **A file that fails** is downloaded again (as in 4.2). Until then the pack is turned off and the player is told
  which pack and why. The game never runs with a half-checked pack.

### 4.5 Uploading (developers)

`scenarios.cmd upload` and the pack upload follow the same steps:
1. Make the file list, upload the files, then upload the list last.
2. Read the list back, and check every uploaded file's size against the server's report.
3. Only then add the entry to `index.json`.

A set whose upload was cut off is never in the index, so nobody downloads it.

### 4.6 Tests

- **Unit tests** against a local test server, with no network:
  - a normal download;
  - a cut-off download that resumes;
  - a wrong size;
  - a wrong hash, with a retry, then failure;
  - a file list with `..` or a duplicate path;
  - a set stopped halfway and finished on the next run;
  - a changed file in a new version, the only one downloaded;
  - a stale file removed.
- **The test server** can serve wrong bytes, a short file, or stop in the middle, so every failure path runs.
- **`tools/make_file_list.py --check <folder>`** checks a folder against its list. It also runs on the sample pack
  in the tests.

## 5. Safety

- A pack contains **data only**. The game never runs code from a pack.
- Every file is checked against the hash in `pack.json`. For packs sold to players, `pack.json` itself is signed, and
  the game holds the public key.
- Upload credentials are never in the game or the repository. Developers upload with their own key through the tool.
- A pack cannot write outside its folder. Paths with `..` or absolute paths are refused.

## 6. Order of work (proposal)

| Step | What | Needs |
|---|---|---|
| 1 | `fetch` (desktop worker + web `emscripten_fetch`), owner-guarded, with progress and retries; the file list and its checks (section 4: `.part`, resume, size, hash, the set's `.complete` marker); `tools/make_file_list.py`; unit tests against a local test server | — |
| 2 | F9 "save this moment as a scenario" in development builds | — |
| 3 | `tools\scenarios.cmd` upload / list / get, against the chosen storage | Q22, Q23 |
| 4 | Packs: `pack.json`, merging data files, media from packs first, saves recording packs, the pack check tool, one sample pack (a weapon) with its arena scenario | Q24 |
| 5 | Getting packs to players: the in-game list, download, enable / disable | Q25 |

## 7. Decisions for the owner

| # | Question | Default if no answer |
|---|---|---|
| Q22 | Where do shared test scenarios live? Options: a cloud bucket (Cloudflare R2, Amazon S3, Azure Blob), Google Drive, or your own server | a cloud bucket (R2: no download fees) |
| Q23 | Who can upload test scenarios: every developer with their own key, or one shared developer key? | each developer their own key |
| Q24 | May a DLC pack **change** existing content (a stronger version of an existing weapon, an edited event), or only **add** new things? | add only |
| Q25 | How do players get DLC: inside the game from our own storage, or through the stores' own DLC (Steam DLC, Google Play asset packs)? Are packs paid? | from our own storage, free packs first; store DLC later |
