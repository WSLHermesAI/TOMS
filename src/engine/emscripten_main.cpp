// emscripten_main.cpp — browser entry for Tower of the Sorcerer (Emscripten/WebGL2).
// Isolated from the Windows/Linux Vulkan build: only compiled under __EMSCRIPTEN__.
#include "game.h"
#include <emscripten.h>
#include <emscripten/fetch.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>
#include <cstdio>
#include <string>
#include <vector>

static Game* g_game = nullptr;
static int g_lastClickX = -1, g_lastClickY = -1;

// ---- Input wrappers exposed to the virtual gamepad (touch) UI ----
// They mirror keyCb's routing: inventory nav when inventory open, otherwise blocked
// while a combat/dialogue modal is active, else the gameplay action.
extern "C" {
EMSCRIPTEN_KEEPALIVE
void jsMove(int dx, int dy) {
    if (!g_game) return;
    if (g_game->inventoryOpen()) { g_game->invMoveSel(dx, dy); return; }
    if (g_game->modalActive()) return;
    g_game->movePlayer(dx, dy);
}
EMSCRIPTEN_KEEPALIVE
void jsInteract() {
    if (!g_game) return;
    if (g_game->inventoryOpen()) { g_game->invUseSelected(); return; }
    if (g_game->modalActive()) return;
    g_game->interact();
}
EMSCRIPTEN_KEEPALIVE
void jsInventory() { if (g_game) g_game->toggleInventory(); }
EMSCRIPTEN_KEEPALIVE
void jsInvDrop() { if (g_game && g_game->inventoryOpen()) g_game->invDropSelected(); }
EMSCRIPTEN_KEEPALIVE
int jsInvOpen() { return (g_game && g_game->inventoryOpen()) ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE
int jsModalActive() { return (g_game && g_game->modalActive()) ? 1 : 0; }

// Download a SINGLE file from a URL (e.g. a remote stage JSON) into the FS.
EMSCRIPTEN_KEEPALIVE
void downloadFile(const char* url, const char* dest) {
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.destinationPath = dest;
    attr.userData = nullptr;
    emscripten_fetch(&attr, url);
}
}

// Async fetch of a packed asset bundle (single file) into the FS.
static void fetchBundle(const char* url, const char* dest) {
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_PERSIST_FILE;
    attr.destinationPath = dest;
    emscripten_fetch(&attr, url);
}

static void loop() {
    if (!g_game) return;
    g_game->update(33);                  // ~30 fps tick (advances combat timer)
    g_game->draw();
}

// keyboard handling for the inventory UI + movement.
static EM_BOOL keyCb(int eventType, const EmscriptenKeyboardEvent* e, void* userData) {
    (void)eventType; (void)userData;
    if (!g_game) return EM_FALSE;
    std::string k = e->key;
    if (k == "i" || k == "I") { g_game->toggleInventory(); return EM_TRUE; }
    if (g_game->inventoryOpen()) {
        if (k == "ArrowLeft")  { g_game->invMoveSel(-1, 0); return EM_TRUE; }
        if (k == "ArrowRight") { g_game->invMoveSel( 1, 0); return EM_TRUE; }
        if (k == "ArrowUp")    { g_game->invMoveSel( 0,-1); return EM_TRUE; }
        if (k == "ArrowDown")  { g_game->invMoveSel( 0, 1); return EM_TRUE; }
        if (k == "Enter")      { g_game->invUseSelected(); return EM_TRUE; }
        if (k == "d" || k == "D") { g_game->invDropSelected(); return EM_TRUE; }
        return EM_TRUE; // swallow all other keys while inventory is open
    }
    if (g_game->modalActive()) return EM_TRUE;
    if (k == "ArrowLeft"  || k == "a" || k == "A") g_game->movePlayer(-1, 0);
    else if (k == "ArrowRight" || k == "d" || k == "D") g_game->movePlayer(1, 0);
    else if (k == "ArrowUp"    || k == "w" || k == "W") g_game->movePlayer(0, -1);
    else if (k == "ArrowDown"  || k == "s" || k == "S") g_game->movePlayer(0, 1);
    else if (k == " " || k == "e" || k == "E") g_game->interact();
    return EM_TRUE;
}

int main() {
#ifndef WEBGPU
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2; attrs.minorVersion = 0;
    attrs.alpha = 0; attrs.preserveDrawingBuffer = 1;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("#canvas", &attrs);
    if (!ctx) { fprintf(stderr, "[web] failed to create WebGL2 context\n"); return 1; }
    emscripten_webgl_make_context_current(ctx);
#endif
    emscripten_set_canvas_element_size("#canvas", 1024, 768);

    // Inject UI: responsive canvas fit, CSS-only fullscreen (no drawing-buffer resize,
    // which caused the black screen), and a touch virtual gamepad on mobile.
    EM_ASM(R"JS(
      function tomsFit(){
        var c=document.getElementById('canvas'); if(!c) return;
        var r=c.getBoundingClientRect(); var top=(r.top>0)?r.top:0;
        var availH=window.innerHeight-top-4, availW=window.innerWidth-4;
        var s=Math.min(availW/1024, availH/768); if(s<=0) s=0.1;
        c.style.width=Math.floor(1024*s)+'px'; c.style.height=Math.floor(768*s)+'px';
        c.style.display='block'; c.style.margin='0 auto';
      }
      window.addEventListener('resize', tomsFit);
      window.addEventListener('load', tomsFit);
      window.addEventListener('fullscreenchange', tomsFit);
      window.addEventListener('webkitfullscreenchange', tomsFit);
      requestAnimationFrame(tomsFit); tomsFit();

      // CSS-only fullscreen: keep the drawing buffer at 1024x768 so the game keeps
      // rendering the full frame (no black surround). Override Emscripten's button.
      if (typeof Module !== 'undefined' && Module.requestFullscreen) {
        Module.requestFullscreen = function(){
          var c=document.getElementById('canvas');
          try { if(!document.fullscreenElement){ (c.requestFullscreen||c.webkitRequestFullscreen||function(){}).call(c); }
                else { (document.exitFullscreen||document.webkitExitFullscreen||function(){}).call(document); } }
          catch(e){}
        };
      }
      // hide Emscripten's generated control bar (its Fullscreen button resizes the buffer)
      var ctrls=document.getElementById('controls'); if(ctrls) ctrls.style.display='none';
      // our own fullscreen toggle (top-right)
      var fsb=document.createElement('button');
      fsb.textContent='⛶'; fsb.title='Fullscreen';
      fsb.style.cssText='position:fixed;top:8px;right:8px;z-index:50;width:40px;height:40px;font:18px sans-serif;background:#222;color:#fff;border:1px solid #555;border-radius:6px';
      fsb.onclick=function(){ var c=document.getElementById('canvas');
        if(!document.fullscreenElement){ (c.requestFullscreen||c.webkitRequestFullscreen)&&(c.requestFullscreen||c.webkitRequestFullscreen).call(c); }
        else { (document.exitFullscreen||document.webkitExitFullscreen)&&(document.exitFullscreen||document.webkitExitFullscreen).call(document); } };
      document.body.appendChild(fsb);

      // ---- virtual gamepad (touch devices only) ----
      if ('ontouchstart' in window || navigator.maxTouchPoints>0) {
        function call(name,a,b){ if(typeof Module!=='undefined'&&Module.ccall) Module.ccall(name,'null',['number','number'], a!==undefined?[a,b]:[]); }
        function mk(label,style,fn){
          var b=document.createElement('button'); b.textContent=label;
          b.style.cssText='position:fixed;z-index:41;pointer-events:auto;opacity:.7;border:2px solid #999;border-radius:50%;background:rgba(20,20,30,.92);color:#fff;font:bold 22px sans-serif;width:64px;height:64px;user-select:none;-webkit-user-select:none;touch-action:none;'+style;
          var rep=null, fire=function(e){ if(e) e.preventDefault(); fn(); };
          b.addEventListener('touchstart',function(e){ e.preventDefault(); fire(); rep=setInterval(fire,150); },false);
          b.addEventListener('touchend',function(e){ e.preventDefault(); if(rep) clearInterval(rep); rep=null; },false);
          b.addEventListener('touchcancel',function(e){ if(rep) clearInterval(rep); rep=null; },false);
          document.body.appendChild(b);
        }
        // D-pad (plus layout, left side)
        mk('▲','left:88px;bottom:248px;', function(){ call('jsMove',0,-1); });
        mk('▼','left:88px;bottom:120px;', function(){ call('jsMove',0, 1); });
        mk('◀','left:24px;bottom:184px;', function(){ call('jsMove',-1,0); });
        mk('▶','left:152px;bottom:184px;', function(){ call('jsMove', 1,0); });
        // action buttons (right side)
        mk('A','right:24px;bottom:104px;width:72px;height:72px;background:rgba(40,90,40,.92);', function(){ call('jsInteract'); });
        mk('B','right:108px;bottom:48px;width:64px;height:64px;background:rgba(90,40,40,.92);', function(){ call('jsInvDrop'); });
        mk('I','right:24px;bottom:24px;width:56px;height:56px;background:rgba(40,40,90,.92);', function(){ call('jsInventory'); });
      }
    )JS");

    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, 1, keyCb);

    g_game = new Game();
    if (!g_game->loadAssets("assets")) {
        delete g_game; g_game = new Game();
        if (!g_game->loadAssets("./assets")) {
            fprintf(stderr, "[web] asset load failed\n");
            return 1;
        }
    }
    g_game->loadStage("stage01");

    emscripten_set_main_loop(loop, 30, 0);
    return 0;
}
