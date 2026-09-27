# 07 — Publish the web version to GitHub Pages

The web game is published as static files on the **`gh-pages` branch** of this repository.
GitHub Pages serves that branch at:

**https://wslhermesai.github.io/TOMS/**

(The URL is `https://<owner>.github.io/<repo>/`; for a fork it follows the fork's owner and name.)

## 1. One-time setup

On GitHub: **Settings → Pages → Build and deployment**:

- **Source:** *Deploy from a branch*
- **Branch:** `gh-pages`, folder **`/ (root)`**

This repository already has it (the old project deployed there). For a new fork or repository,
set it after the first publish has created the `gh-pages` branch.

You need **push access** to the repository, and git must be able to push from this machine (the
Git Credential Manager that comes with Git for Windows asks you to sign in once).

## 2. Publish (the normal way)

Double-click **`publish_web.bat`** in the repository root, or run it in a command prompt:

| Command | Does |
|---|---|
| `publish_web.bat` | builds the web release (`build_web.bat`), prepares the site, **asks**, then pushes |
| `publish_web.bat dryrun` | builds and prepares, shows what would change, pushes **nothing** |
| `publish_web.bat nobuild` | publishes the existing `dist\TOMS-web` without rebuilding |

What happens:

1. `build_web.bat` builds the web release and packages it into `dist\TOMS-web\` with
   **version-stamped file names**: `toms_game.<commit>-<time>.js/.wasm/.data` (the stamp is also
   in `version.txt`). Only `index.html` keeps its name.
2. `tools\publish_pages.ps1` checks out `gh-pages` into a temporary git worktree under `%TEMP%`,
   so your working copy and current branch are never touched.
3. In that worktree it deletes the old site, **except the previous version's stamped files**, then
   copies in the new package and a `.nojekyll` file (so GitHub serves the files as they are).
4. It lists the resulting site and the number of changed files, then asks:
   `Publish <stamp> to https://wslhermesai.github.io/TOMS/ now? (y/N)`.
5. On **y**: it commits `Deploy web build <stamp>` on `gh-pages` and pushes it. On anything else
   it stops and nothing is pushed. The temporary worktree is removed either way.
6. GitHub rebuilds the site in about **1–2 minutes** (GitHub → **Actions** shows the
   *pages build and deployment* run). Then open the URL.

Direct use of the script: `powershell -File tools\publish_pages.ps1 [-DryRun] [-Yes] [-Keep 1]`
(`-Yes` skips the question for automation; `-Keep` sets how many previous versions stay online).

## 3. Why version-stamped file names

GitHub Pages sends files with a cache time of about 10 minutes, and browsers keep them too. With
fixed names (`toms_game.data`), a returning player could get the **new** page with a cached **old**
`.data` or `.wasm`, which fails with a black screen until a hard reload. With stamped names every
release is a new URL, so the files of one release always match.

`index.html` itself can still be cached for a few minutes. It then points at the *previous*
release's files, which is why the publish keeps the previous version's stamped files online: the
cached page keeps working until it refreshes. Older versions are removed on the next publish.

## 4. Check it

- Open https://wslhermesai.github.io/TOMS/ (after 1–2 minutes). The title screen appears.
- F12 → Console: `bgfx ... ready: renderer=OpenGL ES 3.0` and `TOMS on bgfx`.
- A save survives a page reload (stored in the browser's IndexedDB, per player and per browser).
- The automated test also works against the live site:
  `node tools\web_smoke_test.mjs https://wslhermesai.github.io/TOMS/ out\pages_smoke`

## 5. Manual publish (without the script)

```bat
build_web.bat
git fetch origin gh-pages
git worktree add --detach ..\TOMS-pages FETCH_HEAD
cd ..\TOMS-pages
:: delete the old files (keep the previous toms_game.<stamp>.* if you want cached pages to keep working)
git rm -r -q .
xcopy /e /y /q ..\TOMS\dist\TOMS-web\* .
type nul > .nojekyll
git add -A
git commit -m "Deploy web build"
git push origin HEAD:refs/heads/gh-pages
cd ..\TOMS
git worktree remove --force ..\TOMS-pages
```

## 6. Undo a bad release

The site is just the `gh-pages` branch, so an older release is one commit back:

```bat
git fetch origin gh-pages
git log --oneline FETCH_HEAD -5            :: find the good "Deploy web build ..." commit
git push --force origin <good-commit>:refs/heads/gh-pages
```

(`--force` rewrites the published branch; do it only to roll the site back.)

## 7. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| 404 at the URL | Pages not enabled for `gh-pages`, or the first deploy is still running | §1; wait for the Actions run |
| Push rejected / asks for a password | no push access, or git is not signed in | sign in through Git Credential Manager; check repository permissions |
| Old version still shows | page cached (up to ~10 min) | wait, or Ctrl+F5 |
| Black page | see [06 §7](06_BUILD_WEB.md#7-troubleshooting); check `canvas.width` in the console | |
| `No packaged web build in dist\TOMS-web` | nothing built yet | run `build_web.bat` (or `publish_web.bat` without `nobuild`) |
| `Versioning: expected '...' exactly once` while packaging | a new Emscripten changed how the loader names its files | update the three replacements in `tools\package.ps1` |
