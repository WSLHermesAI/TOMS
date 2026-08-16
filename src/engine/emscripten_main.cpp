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
EMSCRIPTEN_KEEPALIVE
void jsGamepad(int phase, int x, int y) {
    if (g_game) g_game->handleTouch((float)x, (float)y, phase);
}

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

// Injected browser UI. NOTE: EM_ASM does NOT support C++ raw-string (R"JS(...)JS")
// syntax — that form leaks `R"JS(` into the emitted JS and breaks it. Use a plain
// string literal instead. JS uses only single quotes to avoid C++ string escaping.
static const char* TOMS_WEB_UI =
"var T=function(){var c=document.getElementById('canvas');if(!c)return;"
"var r=c.getBoundingClientRect();var top=(r.top>0)?r.top:0;"
"var ah=window.innerHeight-top-4,aw=window.innerWidth-4;"
"var s=Math.min(aw/1024,ah/768);if(s<=0)s=0.1;"
"c.style.width=Math.floor(1024*s)+'px';c.style.height=Math.floor(768*s)+'px';"
"c.style.display='block';c.style.margin='0 auto';};"
"window.addEventListener('resize',T);window.addEventListener('load',T);"
"window.addEventListener('fullscreenchange',T);window.addEventListener('webkitfullscreenchange',T);"
"requestAnimationFrame(T);T();"
"if(typeof Module!=='undefined'&&Module.requestFullscreen){Module.requestFullscreen=function(){"
"var c=document.getElementById('canvas');try{if(!document.fullscreenElement){"
"(c.requestFullscreen||c.webkitRequestFullscreen||function(){}).call(c);}"
"else{(document.exitFullscreen||document.webkitExitFullscreen||function(){}).call(document);}}catch(e){}};}"
"var ctrls=document.getElementById('controls');if(ctrls)ctrls.style.display='none';"
"var fsb=document.createElement('button');fsb.textContent='⛶';fsb.title='Fullscreen';"
"fsb.style.cssText='position:fixed;top:8px;right:8px;z-index:50;width:40px;height:40px;font:18px sans-serif;background:#222;color:#fff;border:1px solid #555;border-radius:6px';"
"fsb.onclick=function(){var c=document.getElementById('canvas');"
"if(!document.fullscreenElement){(c.requestFullscreen||c.webkitRequestFullscreen)&&(c.requestFullscreen||c.webkitRequestFullscreen).call(c);}"
"else{(document.exitFullscreen||document.webkitExitFullscreen)&&(document.exitFullscreen||document.webkitExitFullscreen).call(document);}};"
"document.body.appendChild(fsb);"
"var cv=document.getElementById('canvas');if(cv)cv.style.touchAction='none';"
"function toBP(e){var r=cv.getBoundingClientRect();var t=(e.changedTouches&&e.changedTouches[0])?e.changedTouches[0]:e;var bx=(t.clientX-r.left)/r.width*1024;var by=(t.clientY-r.top)/r.height*768;return [bx,by];}"
"function gpCall(p,ph){if(typeof Module!=='undefined'&&Module.ccall)Module.ccall('jsGamepad','null',['number','number','number'],[ph,p[0],p[1]]);}"
"if('ontouchstart'in window||navigator.maxTouchPoints>0){"
"cv.addEventListener('touchstart',function(e){e.preventDefault();var p=toBP(e);cv._p=p;gpCall(p,0);if(cv._rep)clearInterval(cv._rep);cv._rep=setInterval(function(){gpCall(cv._p||p,1);},150);},false);"
"cv.addEventListener('touchmove',function(e){e.preventDefault();cv._p=toBP(e);},false);"
"cv.addEventListener('touchend',function(e){e.preventDefault();var p=toBP(e);gpCall(p,2);if(cv._rep){clearInterval(cv._rep);cv._rep=null;}},false);"
"cv.addEventListener('touchcancel',function(e){if(cv._rep){clearInterval(cv._rep);cv._rep=null;}},false);"
"}";

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

    emscripten_run_script(TOMS_WEB_UI);

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
