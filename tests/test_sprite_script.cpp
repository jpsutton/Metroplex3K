// Sprite-script VM (spec 25 7.1): robustness against targets outside the script (original-written saves reach odd scripts).
#include "core/sprite_script.h"
#include "testutil.h"
using namespace m3k;

static ScriptOp op(ScriptOp::Kind k, int a = 0) { ScriptOp o; o.kind = k; o.a = a; return o; }

int main() {
  AnimRng rng(7);
  {  // Goto to a negative or too-large index restarts the script instead of reading outside it
    CsScript sc; sc.ops = {op(ScriptOp::Draw, 3), op(ScriptOp::Goto, -5), op(ScriptOp::Goto, 99)};
    ScriptRun run;
    for (int i = 0; i < 20; i++) stepScript(sc, run, int64_t(i) * 100, 0, rng, 101);
    CHECK(run.frame == 3);   // reached the Draw again after every out-of-range jump, no out-of-bounds read
  }
  {  // an absolute date-window target outside the script
    CsScript sc; sc.ops = {op(ScriptOp::Draw, 1), op(ScriptOp::Wait, 10)};
    ScriptOp w; w.kind = ScriptOp::DateWindow; w.startMD = 1201; w.endMD = 1231; w.target = -3; w.absTarget = true;
    sc.ops.insert(sc.ops.begin(), w);
    ScriptRun run;
    for (int i = 0; i < 20; i++) stepScript(sc, run, int64_t(i) * 100, 0, rng, 601);   // June: outside the window, every jump lands outside the script
    ScriptRun in; for (int i = 0; i < 6; i++) stepScript(sc, in, int64_t(i) * 100, 0, rng, 1215);   // inside the window: runs normally
    CHECK(in.frame == 1);
  }
  {  // an empty script does nothing
    CsScript sc; ScriptRun run;
    stepScript(sc, run, 0, 0, rng, 101);
    initialScriptFrame(sc, run, rng, 101);
    CHECK(run.frame == -1);
  }
  TEST_MAIN_END();
}
