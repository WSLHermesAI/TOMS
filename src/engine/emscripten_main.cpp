// emscripten_main.cpp — browser entry for Tower of the Sorcerer (Emscripten/WebGL2).
// Isolated from the Windows/Linux Vulkan build: only compiled under __EMSCRIPTEN__.
#include "game.h"
#include "renderer_webgl.h"

namespace toms { extern float g_uiScale; }   // defined in game_text_draw.cpp (B: mobile font scale)   // the web backend that owns kDesignW/setDesignSize (A: mobile design size)
#include <emscripten.h>
#include <emscripten/fetch.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui_web.h"   // M2: ImGui on the browser build (see imgui_web.h)

static Game* g_game = nullptr;
static int g_lastClickX = -1, g_lastClickY = -1;
// M2 dev windows -- the same toggles the desktop entry (main.cpp) has: F1 debug overlay, F2
// styling spike. Both are compiled on every backend now; they change nothing a player sees unless
// the key is pressed.
static bool g_showDebugOverlay = false;
static bool g_showStylingSpike = false;

// ---- Input wrappers exposed to the virtual gamepad (touch) UI ----
// They mirror keyCb's routing: inventory nav when inventory open, otherwise blocked
// while a combat/dialogue modal is active, else the gameplay action.
extern "C" {
EMSCRIPTEN_KEEPALIVE
void jsMove(int dx, int dy) {
    if (!g_game) return;
    if (g_game->titleOpen()) { g_game->titleMove(dx, dy); return; }   // title phase owns input
    if (g_game->inventoryOpen()) { g_game->invMoveSel(dx, dy); return; }
    if (g_game->modalActive()) return;
    g_game->movePlayer(dx, dy);
}
EMSCRIPTEN_KEEPALIVE
void jsInteract() {
    if (!g_game) return;
    if (g_game->titleOpen()) { g_game->titleConfirm(); return; }
    if (g_game->inventoryOpen()) { g_game->invUseSelected(); return; }
    if (g_game->modalActive()) return;
    g_game->interact();
}
// Called from the IDBFS sync-in callback below: slots read before IndexedDB finished loading
// would look empty, so the Continue list is re-read once the files are actually there.
EMSCRIPTEN_KEEPALIVE
void jsRefreshSlots() { if (g_game) g_game->refreshSlots(); }
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
    // M2: if the pointer is over an ImGui window (F1/F2 dev tools) the click belongs to ImGui --
    // otherwise dragging a slider would also move the player / fire a battle tap underneath it.
    if (toms::imgui_web::wantsMouse()) return;
    if (g_game) g_game->handleTouch((float)x, (float)y, phase);
}
// S3 dialogue/choice probes for the deploy harness: the dialogue box is drawn on the canvas, so a
// page driver cannot see which node it is on or whether a choice was recorded -- these assert on the
// real Game state instead. Same rationale as the battle probes below.
EMSCRIPTEN_KEEPALIVE
void jsTalk(const char* npc) { if (g_game && npc) g_game->startDialogue(npc); }
EMSCRIPTEN_KEEPALIVE
int jsDialogueInfo(int which) {
    if (!g_game) return -1;
    if (which == 0) return g_game->inDialogueFlag() ? 1 : 0;
    if (which == 1) return g_game->dialogueChoiceCount();
    if (which == 2) return g_game->dialogueSel();
    return -1;
}
EMSCRIPTEN_KEEPALIVE
void jsChoose(int idx) { if (g_game) g_game->chooseDialogue(idx); }
// The S3 probes: did a main-line choice get recorded on the RUN, and what do the counters say?
EMSCRIPTEN_KEEPALIVE
int jsChoiceMade(const char* choiceId, const char* optionId) {
    if (!g_game || !choiceId || !optionId) return -1;
    return g_game->runState().choiceMade(choiceId, optionId) ? 1 : 0;
}
EMSCRIPTEN_KEEPALIVE
int jsRunCounter(const char* name) {
    if (!g_game || !name) return -999;
    return g_game->runState().counter(name);
}
EMSCRIPTEN_KEEPALIVE
int jsRunInfo(int which) {
    if (!g_game) return -1;
    const toms::RunStoryState& r = g_game->runState();
    if (which == 0) return r.shardCount();
    if (which == 1) return (int)r.clearedFloors().size();
    if (which == 2) return r.deathsTotal();
    if (which == 3) return r.flag("flag_motive_know_self") ? 1 : 0;   // spot-check a choice flag
    return -1;
}
// S3.5 probes: which floor the run is on, how the counter is derived, and a way to jump to one.
EMSCRIPTEN_KEEPALIVE
int jsFloorInfo(int which) {
    if (!g_game) return -1;
    if (which == 0) return g_game->floorMode() ? 1 : 0;          // is the tower data loaded?
    if (which == 1) return g_game->floorTable().size();          // 70
    if (which == 2) return g_game->floorTable().seqOf(g_game->currentFloorId());   // 0 when not a floor
    if (which == 3) return g_game->themeAct();                       // S3.5 (e): the palette in use
    if (which == 4) return g_game->storyLineIndex();                 // S3.5 (c): 0 intro, 1.. ambient
    if (which == 5) return g_game->chapterCardVisible() ? 1 : 0;     // S3.5 (c): act card on screen
    return -1;
}
// Does the loaded stage's stair fields carry the tower's links? 0: st.up == nextFloor, 1: st.down ==
// prev floor, 2: both. This is the direct check that a floor's exit leads where the table says.
EMSCRIPTEN_KEEPALIVE
int jsFloorLinks() {
    if (!g_game) return -1;
    const std::string cur = g_game->currentFloorId();
    if (cur.empty()) return -1;
    const toms::FloorTable& t = g_game->floorTable();
    bool up = (g_game->stage().up == t.next(cur));
    bool down = (g_game->stage().down == t.prev(cur));
    return (up && down) ? 2 : (up ? 0 : (down ? 1 : 3));
}
EMSCRIPTEN_KEEPALIVE
int jsFloorNextIsMapped(const char* floorId) {
    if (!g_game || !floorId) return -1;
    const toms::FloorInfo* f = g_game->floorTable().find(floorId);
    if (!f) return -1;
    return f->nextFloor.empty() ? 0 : 1;
}
// Force a run save (the page then reads /save/slotN.json out of IDBFS to verify schemaVersion 3).
EMSCRIPTEN_KEEPALIVE
void jsSaveNow() { if (g_game) g_game->saveRunNow(); }
// Dev/verification only: jump the run to a floor (or a hand-authored stage id) so a page driver can
// check a boss floor, the counter, etc. without walking the whole maze.
EMSCRIPTEN_KEEPALIVE
void jsGoStage(const char* id) { if (g_game && id) g_game->loadStage(id); }

// Diagnostics for the browser build's battle input. The battle scene is driven by taps on canvas
// rects, which a page driver cannot observe through the DOM (nothing in the canvas reports state),
// so the deploy harness asserts on these instead: which==0 -> flag bits (0 active, 1 attack bar
// cooling, 2 defense bar cooling, 3 won, 4 shield banked), which==1 -> enemy HP, which==2 -> ms
// elapsed on the enemy's own attack clock, which==3 -> current Super Attack gauge charge.
EMSCRIPTEN_KEEPALIVE
int jsCombatInfo(int which) {
    if (!g_game) return -1;
    const CombatState& c = g_game->combat();
    if (which == 0) return (c.active?1:0) | (c.atkBar.cooling?2:0) | (c.defBar.cooling?4:0) | (c.won?8:0) | (c.shieldBanked?16:0);
    if (which == 1) return c.enemyHP;
    if (which == 2) return c.enemyClockMs;
    if (which == 3) return c.superCharge;
    return -1;
}
// More harness diagnostics: which==0/1 -> player x/y, so a JS-driven walk can prove a step really
// happened; jsDebugBattle() starts a fight with the nearest monster so the battle scene's tap
// input can be exercised without walking the maze first.
EMSCRIPTEN_KEEPALIVE
int jsPlayerInfo(int which) {
    if (!g_game) return -1;
    if (which == 0) return g_game->player().x;
    if (which == 1) return g_game->player().y;
    return -1;
}
EMSCRIPTEN_KEEPALIVE
int jsDebugBattle() {
    return (g_game && g_game->debugStartNearestBattle()) ? 1 : 0;
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
    // M2: ImGui runs on the web renderer too. ImGui must see NewFrame() before any ImGui::* call
    // (the dev windows below) and Render() after the game's own draws have been flushed, so the
    // frame is bracketed here. DisplaySize is the canvas drawing-buffer size, not the game's
    // 1024x768 design space: the game maps design->buffer inside its own shader, ImGui draws in
    // real pixels.
    int fbW = 0, fbH = 0;
    emscripten_get_canvas_element_size("#canvas", &fbW, &fbH);
    toms::imgui_web::beginFrame((float)fbW, (float)fbH);

    g_game->update(33);                  // ~30 fps tick (advances combat timer)
    if (toms::imgui_web::ready()) {
        // Same order as main.cpp: font scale first, then the dev windows, and
        // setStylingSpikeVisible(false) before drawStylingSpike() so the backdrop switches off the
        // frame after F2 is toggled off instead of staying stuck visible.
        g_game->applyUiSettings();
        if (g_showDebugOverlay) g_game->drawDebugOverlay();
        g_game->setStylingSpikeVisible(false);
        if (g_showStylingSpike) g_game->drawStylingSpike();
    }
    g_game->draw();
    toms::imgui_web::endFrame();
    // Mark the runtime ready AFTER the first successful frame so the touch/mouse
    // handlers can safely call jsGamepad (avoids the pre-init abort crash and is
    // reliable across Emscripten versions, unlike Module.runtimeInitialized).
    emscripten_run_script("window.__tomsReady=true;");
}

// keyboard handling for the inventory UI + movement.
static EM_BOOL keyCb(int eventType, const EmscriptenKeyboardEvent* e, void* userData) {
    (void)eventType; (void)userData;
    if (!g_game) return EM_FALSE;
    std::string k = e->key;
    // M2 dev windows (desktop parity: main.cpp binds F1/F2). Returning EM_TRUE also stops the
    // browser's own F1 = help default.
    if (k == "F1") { g_showDebugOverlay  = !g_showDebugOverlay;   return EM_TRUE; }
    if (k == "F2") { g_showStylingSpike  = !g_showStylingSpike;   return EM_TRUE; }
    // While an ImGui window is focused, it owns the keyboard (typing in a field or dragging a
    // slider must not also walk the player / fire battle taps). The title phase keeps its keys:
    // it is a full-screen boot screen the dev windows are not meant to be used over.
    if (toms::imgui_web::wantsKeyboard() && !g_game->titleOpen()) return EM_TRUE;
    // Title phase (Boot screen): it owns the keyboard while it is up — same bindings as the
    // desktop build's main.cpp (arrows/WASD to move, Enter/Space to confirm, Esc to go back).
    if (g_game->titleOpen()) {
        if (k == "ArrowUp"    || k == "w" || k == "W")      g_game->titleMove(0, -1);
        else if (k == "ArrowDown"  || k == "s" || k == "S") g_game->titleMove(0, 1);
        else if (k == "ArrowLeft"  || k == "a" || k == "A") g_game->titleMove(-1, 0);
        else if (k == "ArrowRight" || k == "d" || k == "D") g_game->titleMove(1, 0);
        else if (k == "Enter" || k == " ")                  g_game->titleConfirm();
        else if (k == "Escape")                             g_game->titleCancel();
        return EM_TRUE;   // swallow everything else while the title is up
    }
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
    // Stairs confirm (S3.5 finding): the floor-change prompt is raised by stepping onto a floor's
    // exit tile, and until now its Enter/Esc handling lived only in the desktop entry point
    // (main.cpp), even though the button itself is labelled "(Enter)". A keyboard-only player in
    // the browser was stuck at the prompt until they clicked the canvas. Same two bindings here.
    if (g_game->stairsConfirmOpen()) {
        if (k == "Enter" || k == " ") { g_game->confirmStageTransition(); return EM_TRUE; }
        if (k == "Escape")            { g_game->cancelStageTransition();  return EM_TRUE; }
        return EM_TRUE; // swallow all other keys while the prompt is up
    }
    // In-game menu (Save/Settings/Back to Title) -- the gear icon (tap/click) is the primary
    // way in on touch, but a keyboard attached to the browser gets the same bindings as the
    // title phase's own Menu/Settings above.
    if (g_game->inGameMenuOpen()) {
        if (k == "ArrowUp"   || k == "ArrowLeft")  { g_game->inGameMenuMove(-1); return EM_TRUE; }
        if (k == "ArrowDown" || k == "ArrowRight") { g_game->inGameMenuMove(1); return EM_TRUE; }
        if (k == "Enter" || k == " ") { g_game->inGameMenuActivate(); return EM_TRUE; }
        if (k == "Escape") { g_game->inGameMenuBack(); return EM_TRUE; }
        return EM_TRUE; // swallow all other keys while the menu is open
    }
    // Battle System v2: a keyboard attached to the browser gets the same tap bindings as the
    // desktop build (main.cpp) -- Attack/Defend/Super are independent instantaneous taps, each a
    // safe no-op while its bar is cooling / the gauge isn't full, so no state tracking is needed.
    // The primary input is still touch (see handleTouch's rect-based dispatch via jsGamepad, which
    // already supports genuinely concurrent multi-touch since each finger's pointerdown fires its
    // own independent call) -- this is parity for anyone with a keyboard, not the main path.
    if (g_game->combatActive()) {
        if (k == "Enter" || k == " ")      { g_game->battleTapAttack(); return EM_TRUE; }
        if (k == "f" || k == "F")          { g_game->battleTapDefense(); return EM_TRUE; }
        if (k == "g" || k == "G")          { g_game->battleTapSuper(); return EM_TRUE; }
        return EM_TRUE;
    }
    if (g_game->modalActive()) return EM_TRUE;
    if (k == "ArrowLeft"  || k == "a" || k == "A") g_game->movePlayer(-1, 0);
    else if (k == "ArrowRight" || k == "d" || k == "D") g_game->movePlayer(1, 0);
    else if (k == "ArrowUp"    || k == "w" || k == "W") g_game->movePlayer(0, -1);
    else if (k == "ArrowDown"  || k == "s" || k == "S") g_game->movePlayer(0, 1);
    else if (k == " " || k == "e" || k == "E") g_game->interact();
    else if (k == "Escape") g_game->openInGameMenu();
    return EM_TRUE;
}

// Injected browser UI. NOTE: EM_ASM does NOT support C++ raw-string (R"JS(...)JS")
// syntax — that form leaks `R"JS(` into the emitted JS and breaks it. Use a plain
// string literal instead. JS uses only single quotes to avoid C++ string escaping.
static const char* TOMS_WEB_UI =
"(function(){try{"
"var T=function(){var c=document.getElementById('canvas');if(!c)return;"
"var r=c.getBoundingClientRect();var top=(r.top>0)?r.top:0;"
"var ah=window.innerHeight-top-4,aw=window.innerWidth-4;"
"var d=[c.width||1024,c.height||768];"   /* the drawing buffer IS the design size (A) */
"var s=Math.min(aw/d[0],ah/d[1]);if(s<=0)s=0.1;"
"c.style.width=Math.floor(d[0]*s)+'px';c.style.height=Math.floor(d[1]*s)+'px';"
"c.style.display='block';c.style.margin='0 auto';};"
"var R=function(){var p=window.innerHeight>window.innerWidth;var o=document.getElementById('tomsRotate');"
"if(!o){o=document.createElement('div');o.id='tomsRotate';"
"o.style.cssText='position:fixed;left:0;top:0;right:0;bottom:0;background:#0b0e16;color:#f2e6c8;"
"display:flex;align-items:center;justify-content:center;text-align:center;padding:24px;z-index:99999;"
"font:600 20px/1.6 system-ui,-apple-system,sans-serif';"
"o.textContent='\u8acb\u628a\u88dd\u7f6e\u8f49\u70ba\u6a6b\u5411 · Rotate your device';"
"document.body.appendChild(o);}o.style.display=p?'flex':'none';};"
"window.addEventListener('resize',R);window.addEventListener('orientationchange',R);R();"
"window.addEventListener('resize',T);window.addEventListener('load',T);"
"window.addEventListener('fullscreenchange',T);window.addEventListener('webkitfullscreenchange',T);"
"requestAnimationFrame(T);T();"
"if(typeof Module!=='undefined'&&Module.requestFullscreen){Module.requestFullscreen=function(){"
"var d=document.documentElement;try{if(!document.fullscreenElement){"
"var p=(d.requestFullscreen||d.webkitRequestFullscreen||function(){}).call(d);if(p&&p.catch)p.catch(function(){});}"
"else{var q=(document.exitFullscreen||document.webkitExitFullscreen||function(){}).call(document);if(q&&q.catch)q.catch(function(){});}}catch(e){}};}"
"var ctrls=document.getElementById('controls');if(ctrls)ctrls.style.display='none';"
"var fsb=document.createElement('button');fsb.textContent='⛶';fsb.title='Fullscreen';"
"fsb.style.cssText='position:fixed;top:8px;right:8px;z-index:50;width:40px;height:40px;font:18px sans-serif;background:#222;color:#fff;border:1px solid #555;border-radius:6px';"
"fsb.onclick=function(){var d=document.documentElement;"
"if(!document.fullscreenElement){var fp=(d.requestFullscreen||d.webkitRequestFullscreen);if(fp){var fr=fp.call(d);if(fr&&fr.catch)fr.catch(function(){});}}"
"else{(document.exitFullscreen||document.webkitExitFullscreen)&&(document.exitFullscreen||document.webkitExitFullscreen).call(document);}};"
"document.body.appendChild(fsb);"
"var bpb=document.createElement('button');bpb.textContent='背包';bpb.title='Backpack';"
"bpb.style.cssText='position:fixed;top:8px;right:56px;z-index:50;width:54px;height:40px;font:16px sans-serif;background:#222;color:#fff;border:1px solid #555;border-radius:6px';"
"bpb.onclick=function(){try{if(typeof Module!=='undefined'&&Module.ccall)Module.ccall('jsInventory','null',[],[]);}catch(e){showErr('inventory button: '+e);}};"
"document.body.appendChild(bpb);"
"var cv=document.getElementById('canvas');if(cv)cv.style.touchAction='none';"
"window.__tomsReady=false;"
"function toBP(e){var r=cv.getBoundingClientRect();var w=(r.width>0)?r.width:1024;var h=(r.height>0)?r.height:768;var t=(e.changedTouches&&e.changedTouches[0])?e.changedTouches[0]:e;var bx=(t.clientX-r.left)/w*1024;var by=(t.clientY-r.top)/h*768;if(!isFinite(bx)||!isFinite(by))return null;return [bx,by];}"
"function gpCall(p,ph){try{if(typeof Module!=='undefined'&&Module.ccall&&window.__tomsReady)Module.ccall('jsGamepad','null',['number','number','number'],[ph,p[0],p[1]]);}catch(err){showErr('jsGamepad: '+err);}}"
"function showErr(m){try{console.error('[TOMS] '+m);}catch(e){}}"
"window.onerror=function(m,s,l,c,e){showErr(m+' @'+l+':'+c+(e&&e.stack?' | '+e.stack:''));return false;};"
"if(typeof Module!=='undefined'){Module.onAbort=function(what){showErr('ABORT: '+(what||'unknown')+' (open DevTools console for the C++ stack)');};}"
"function onDown(e){var p=toBP(e);if(!p)return;cv._p=p;gpCall(p,0);if(cv._rep)clearTimeout(cv._rep);if(cv._rep2)clearInterval(cv._rep2);cv._rep=null;cv._rep2=null;cv._rep=setTimeout(function(){cv._rep2=setInterval(function(){gpCall(cv._p||p,1);},150);},320);}"
"function onMove(e){var p=toBP(e);if(p)cv._p=p;}"
"function onUp(e){var p=toBP(e);if(p)gpCall(p,2);if(cv._rep){clearTimeout(cv._rep);cv._rep=null;}if(cv._rep2){clearInterval(cv._rep2);cv._rep2=null;}}"
"if(window.PointerEvent){"
"cv.addEventListener('pointerdown',function(e){e.preventDefault();onDown(e);},false);"
"cv.addEventListener('pointermove',function(e){onMove(e);},false);"
"cv.addEventListener('pointerup',function(e){e.preventDefault();onUp(e);},false);"
"}else{"
"cv.addEventListener('touchstart',function(e){e.preventDefault();onDown(e);},false);"
"cv.addEventListener('touchmove',function(e){e.preventDefault();onMove(e);},false);"
"cv.addEventListener('touchend',function(e){e.preventDefault();onUp(e);},false);"
"cv.addEventListener('touchcancel',function(e){if(cv._rep){clearInterval(cv._rep);cv._rep=null;}},false);"
"cv.addEventListener('mousedown',function(e){e.preventDefault();onDown(e);},false);"
"cv.addEventListener('mousemove',function(e){onMove(e);},false);"
"cv.addEventListener('mouseup',function(e){e.preventDefault();onUp(e);},false);"
"}"
"}catch(e){console.error('[TOMS_WEB_UI] init failed (non-fatal):', e && e.stack ? e.stack : e);"
"if(typeof showErr==='function')showErr('TOMS_WEB_UI init failed: '+(e&&e.message?e.message:e));}"
"})();";

int main() {
 try {
#ifndef WEBGPU
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2; attrs.minorVersion = 0;
    attrs.alpha = 0; attrs.preserveDrawingBuffer = 1;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("#canvas", &attrs);
    if (!ctx) { fprintf(stderr, "[web] failed to create WebGL2 context\n"); return 1; }
    emscripten_webgl_make_context_current(ctx);
#endif
    // ---- A+B (mobile): a smaller logical design + a bigger font scale on small screens --------
    // The design is what the game draws in; shrinking it makes every element occupy a larger share
    // of the same physical screen, which is the real fix for "everything is too small on a phone".
    // A phone in landscape is the target case (a 4:3 game on a 390 px-tall screen is inherently
    // small); 768x576 halves the logical pixels and 1.25 on the fonts lifts the text further.
    {
        const int cssW = EM_ASM_INT({ return window.innerWidth; });
        const int cssH = EM_ASM_INT({ return window.innerHeight; });
        if (cssW < 900 || cssH < 560) {
            WebGLRenderer::setDesignSize(768, 576);
            // B (the extra font scale) stays OFF until C lands: the dialogue and battle screens lay
            // their rows out on a fixed pixel pitch, so growing the glyphs 25% makes the speaker
            // line collide with the first choice row. A (the smaller design) already makes every
            // element 1.33x bigger, which is the safe part of the mobile fix.
            toms::g_uiScale = 1.0f;
            fprintf(stderr, "[web] small screen (%dx%d css) -> design 768x576, ui scale 1.25\n", cssW, cssH);
        }
    }
    emscripten_set_canvas_element_size("#canvas", (int)WebGLRenderer::kDesignW, (int)WebGLRenderer::kDesignH);

    // ---- Saves that survive a page reload ----
    // save_slots.h's defaultSaveDir() is /save in the browser. Mount IndexedDB-backed IDBFS
    // there so slot files persist; when IndexedDB is unavailable (e.g. private browsing) the
    // mount throws and the game keeps running with an in-memory /save — saves then last for the
    // session only, which is still better than not saving at all. The initial sync-in is async,
    // so the Continue list is re-read (jsRefreshSlots) once the files have actually arrived.
    EM_ASM({
        try {
            if (typeof FS === 'undefined') return;
            try { FS.mkdir('/save'); } catch (e) {}
            FS.mount(IDBFS, {}, '/save');
            Module.__tomsSyncfs = function(load) {
                try {
                    FS.syncfs(!!load, function(err) {
                        if (err) { console.warn('[TOMS] syncfs', err); return; }
                        if (load && typeof Module !== 'undefined' && Module.ccall) {
                            try { Module.ccall('jsRefreshSlots', 'null', [], []); } catch (e) {}
                        }
                    });
                } catch (e) { console.warn('[TOMS] syncfs failed', e); }
            };
            Module.__tomsSyncfs(true);
        } catch (e) { console.warn('[TOMS] IDBFS mount failed; saves are session-only:', e); }
    });

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

    // M2: ImGui on the browser build. Must come after the context is current (the OpenGL3 backend
    // creates its GL objects immediately) and before the first frame (loop() brackets on
    // beginFrame/endFrame). Failure is not fatal: without ImGui the dev windows simply stay off.
    if (!toms::imgui_web::init("#canvas"))
        fprintf(stderr, "[web] ImGui unavailable -- continuing without the dev windows\n");

    emscripten_set_main_loop(loop, 30, 0);
    return 0;
 } catch (const std::exception& e) {
    fprintf(stderr, "[web] FATAL init exception: %s\n", e.what());
    EM_ASM({ if (typeof showErr === 'function') showErr('init exception: ' + UTF8ToString($0)); }, e.what());
    return 2;
 } catch (...) {
    fprintf(stderr, "[web] FATAL init exception: unknown\n");
    return 2;
 }
}
