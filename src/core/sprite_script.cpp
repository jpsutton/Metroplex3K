#include "core/sprite_script.h"

namespace m3k {
namespace {

int labelIndex(const CsScript& sc, int id) {
  for (size_t i = 0; i < sc.ops.size(); i++)
    if (sc.ops[i].kind == ScriptOp::Label && sc.ops[i].a == id) return int(i);
  return 0;   // no such label: the start of the script
}

// Check the armed date window after an entry: jump when the date is outside [start, end).
void checkWindow(const CsScript& sc, ScriptRun& run, int dateMD) {
  if (!run.armed) return;
  const bool outside = dateMD < run.startMD || dateMD >= run.endMD;
  if (!outside) return;
  run.step = run.windowAbs ? run.windowTarget : labelIndex(sc, run.windowTarget);
  run.armed = false;
}

// Execute one non-label entry. `freeze`: waits take no time (used for the static initial frame).
void execOne(const CsScript& sc, ScriptRun& run, int64_t nowMs, int j, AnimRng& rng, int dateMD, bool freeze) {
  const int n = int(sc.ops.size());
  if (!n) return;
  if (run.step < 0 || run.step >= n) run.step = 0;   // a Goto / date-window target outside the script restarts it
  for (int guard = 0; guard < n + 1 && sc.ops[size_t(run.step)].kind == ScriptOp::Label; guard++) {
    run.step++;
    if (run.step >= n) run.step = 0;
  }
  const ScriptOp& op = sc.ops[size_t(run.step)];
  switch (op.kind) {
    case ScriptOp::Draw:
    case ScriptOp::DrawAlpha:
      run.frame = op.a; run.alpha = op.kind == ScriptOp::DrawAlpha;
      if (op.hasOffset) { run.dx = op.dx; run.dy = op.dy; }
      run.step++;
      break;
    case ScriptOp::Wait:
      if (!freeze) run.nextMs = nowMs + op.a + int64_t(j) * ((op.a >> 6) + (op.a >> 5));
      run.step++;
      break;
    case ScriptOp::WaitRandom:
      if (!freeze) run.nextMs = nowMs + op.a + int64_t(rng.next() % uint32_t(op.b > 0 ? op.b : 1));
      run.step++;
      break;
    case ScriptOp::Goto: run.step = op.a; break;
    case ScriptOp::RandomSkip: run.step += 1 + int(rng.next() % uint32_t(op.a > 0 ? op.a : 1)); break;
    case ScriptOp::GotoWindowTarget: run.step = labelIndex(sc, run.windowLabel); break;
    case ScriptOp::DateWindow:
      run.armed = true; run.startMD = op.startMD; run.endMD = op.endMD;
      run.windowTarget = op.target; run.windowAbs = op.absTarget;
      if (!op.absTarget) run.windowLabel = op.target;
      run.step++;
      break;
    case ScriptOp::Label: run.step++; break;
  }
  checkWindow(sc, run, dateMD);
}

}  // namespace

void stepScript(const CsScript& sc, ScriptRun& run, int64_t nowMs, int j, AnimRng& rng, int dateMD) {
  if (run.nextMs > nowMs) return;
  execOne(sc, run, nowMs, j, rng, dateMD, false);
}

void initialScriptFrame(const CsScript& sc, ScriptRun& run, AnimRng& rng, int dateMD) {
  run = ScriptRun();
  for (int i = 0; i < 4 * int(sc.ops.size()) + 8 && run.frame < 0; i++) execOne(sc, run, 0, 0, rng, dateMD, true);
  // Leave the run at step 0 / time 0 so a started animation begins from the top, showing this frame until it draws.
  const int f = run.frame; const bool a = run.alpha; const int dx = run.dx, dy = run.dy;
  run = ScriptRun();
  run.frame = f; run.alpha = a; run.dx = dx; run.dy = dy;
}

}  // namespace m3k
