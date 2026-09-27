Tower of the Sorcerer (TOMS) - web version

Put every file of this folder into one folder on a web server (any static host: nginx, Apache,
IIS, GitHub Pages, Netlify, an S3 bucket, ...) and open that folder's URL (index.html).

- It must be served over http(s). Opening index.html from the disk (file://) does not work.
- .wasm must be served as "application/wasm" (most servers do; .htaccess / web.config set it for
  Apache / IIS).
- File names carry a version stamp (see version.txt), so browsers and caches never mix an old
  .data with a new page. When uploading a new version over an old one, keep the previous
  toms_game.<stamp>.* files for a while: a page cached for a few minutes still points at them.
  (tools\publish_pages.ps1 does this for GitHub Pages.)
- Saves are stored in the browser (IndexedDB) of each player.
- Local test: tools\serve_web.cmd in the source tree, or  python -m http.server 8099  in this folder.
