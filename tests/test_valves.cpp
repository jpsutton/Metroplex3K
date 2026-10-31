#include "core/growth.h"
#include "core/ini.h"
#include "core/records.h"
#include "core/valves.h"
#include "testutil.h"
using namespace m3k;
using namespace m3k::growth;

int main() {
  // RNG test vectors (spec 14 section 8.1)
  { SimRand r(1); CHECK(r.rand(100) == 38); SimRand q(1); CHECK(q.next() == 0x41C6); CHECK(q.next() == 0x59D4967E);
    CrtRand c; CHECK(c.next() == 41); CHECK(c.next() == 18467); }

  // Valve update: clamp, cap (spec 15 3.2 example: capPct 20 -> 2000 - 400 = 1600)
  { Valve v; v.id = 3000; v.Dacc = 30000; v.Sacc = 7690; v.capPct = 20; v.update();
    CHECK(v.Dlast == 30000 && v.Slast == 7690 && v.V == 1600 && v.N == 800);
    v.Dacc = 0; v.Sacc = 5000; v.capPct = 0; v.update(); CHECK(v.V == -2000 && v.N == -1000);
    v.Dacc = 1000; v.Sacc = 0; v.f1 = -0.125f; v.f2 = -0.125f; v.update(); CHECK(v.V == 500);  // -0.25 * 2000 = -500
    // smooth: step = 4000*7/100 = 280
    Valve s; s.smooth = true; s.Dacc = 1000; s.update(); CHECK(s.V == 280);
    s.Dacc = 1000; s.update(); CHECK(s.V == 560);
    Valve z; z.smooth = true; z.V = -200; z.Dacc = 300; z.update(); CHECK(z.V == 80);
    Valve z2; z2.smooth = true; z2.V = -500; z2.Dacc = 100; z2.update(); CHECK(z2.V == 0);
    Valve z3; z3.smooth = true; z3.V = -300; z3.Dacc = 100; z3.update(); CHECK(z3.V == 0);
    Valve z4; z4.smooth = true; z4.V = 100; z4.Dacc = -1000; z4.update(); CHECK(z4.V == -180);
    Valve z5; z5.smooth = true; z5.V = 200; z5.Dacc = -100; z5.update(); CHECK(z5.V == -80);  // delta -300 <= -step; target<0, V>0, V-step=-80 not >0
    Valve z6; z6.smooth = true; z6.V = 500; z6.Dacc = -100; z6.update(); CHECK(z6.V == 0);
    Valve z7; z7.smooth = true; z7.V = 300; z7.Dacc = -100; z7.update(); CHECK(z7.V == 0);
    Valve big; big.id = 3001; big.setLimits(INT32_MAX, INT32_MIN); big.Dacc = 22310; big.update(); CHECK(big.V == 22310 && big.N == 0);
  }
  // Valve record block: 41 bytes, round trip
  { Valve v; v.id = 8001; v.Slast = -5; v.Dlast = 7; v.Sacc = 1; v.Dacc = 2; v.f1 = 0.5f; v.f2 = -0.25f; v.capPct = 33; v.V = 99; v.Sbase = 70000; v.Dbase = 3;
    Bytes b; v.write(b); CHECK(b.size() == 41); Reader r(b); Valve w = Valve::read(r);
    CHECK(w.id == 8001 && w.Slast == -5 && w.f1 == 0.5f && w.f2 == -0.25f && w.capPct == 33 && w.V == 99 && w.Sbase == 70000 && w.Dbase == 3); }

  // Curve interpolation incl. the decreasing-segment quirk
  { std::vector<CurvePoint> p = {{0, 10}, {10, 110}, {20, 60}};
    CHECK(curveValue(p, 0) == 10); CHECK(curveValue(p, 5000) == 60);
    CHECK(curveValue(p, 15000) == 110 + 5000 * 50 / 10000);  // abs() makes the decreasing segment rise
    CHECK(curveValue(p, 99999) == 60); CHECK(curveValue({{3, 7}}, 5) == 7); }

  // Tuning parsers
  ValveTuning t;
  { IniFile ini = IniFile::fromText("[AgentValveEffects]\n3100=3000:10:0+7000:0:0 comment\n5100=5000:5:0+7000:0:8\n8100=8000:4:0+7000:0:6\n"
      "[ConnectionValveEffects]\n0=5001:100:0\n[OrdinanceEffects]\n0x1234=3000,-100\n");
    t.parseAgentValveEffects(ini); t.parseConnectionEffects(ini); t.parseOrdinanceEffects(ini);
    CHECK(t.agentEffects.at(3100).size() == 2 && t.agentEffects.at(3100)[0].supply == 10);
    CHECK(t.agentEffects.at(5100)[1].valve == 7000 && t.agentEffects.at(5100)[1].demand == 8);
    CHECK(t.connectionEffects.at(0)[0].valve == 5001);
    CHECK(t.ordinanceEffects.at(0x1234)[0].permille == -100); }
  { IniFile ini = IniFile::fromText("[WorldLayer]\nJobToResFactor=0\nExternalJobDemand=40\nEconomicPhases=1:1:1 10:3:1\n[ListOfWorldValveMapping]\n3000=2:-50 3:0 0:0\n");
    t.parseWorldLayer(ini);
    CHECK(t.jobToResFactor == 1 && t.externalJobDemand == 40);
    CHECK(t.phaseA.size() == 3 && t.phaseA[0].x == 0 && t.phaseA[0].y == 1);  // (0 -> 1:1) added
    CHECK(t.worldCycles.at(3000).size() == 2); }
  t.parseTaxation("3000,-7000,7,0;1000,-5000,10,10000");
  { std::array<float, 23> e; CHECK(t.taxEffects(0, e)); CHECK(e[0] > 0.2999f && e[0] < 0.3001f && e[7] == 0 && e[22] < -0.6999f && e[22] > -0.7001f);
    std::array<float, 23> m; CHECK(t.taxEffects(5000, m)); CHECK(m[0] > 0.0f && m[0] < 0.3f);
    std::array<float, 23> l; CHECK(t.taxEffects(20000, l)); CHECK(l[0] > 0.0999f && l[0] < 0.1001f);
    // original defect: weight = pop / (hi - lo): at pop = 15000 with lo = 10000? (lo=10000) -> 15000/... see taxEffects
  }

  // World cycle + monthly update
  ValveLayer vl; vl.init(16, 16, t);
  CHECK(vl.v5001.Max == INT32_MAX && vl.v3000.Max == 2000 && vl.v7000.Min == -2000);
  vl.setWorldModifiers(1900, 1); CHECK(vl.v3000.f1 == -0.5f);
  vl.setWorldModifiers(1900, 3); CHECK(vl.v3000.f1 == 0.0f);
  vl.setWorldModifiers(1900, 6); CHECK(vl.v3000.f1 == -0.5f);  // position 5 mod 5 == 0
  {
    ValveCityInput in; in.year = 1900; in.month = 3; in.workforceFraction = 0.5; in.population = 0;
    ValveBuilding r; r.agentType = 3100; r.x = 2; r.z = 2; r.sizeX = 3; r.sizeZ = 3; in.buildings.push_back(r);
    ValveBuilding cb; cb.agentType = 5100; cb.x = 8; cb.z = 8; cb.sizeX = 2; cb.sizeZ = 2; in.buildings.push_back(cb);
    ValveBuilding ib; ib.agentType = 8100; ib.x = 12; ib.z = 12; ib.operating = false; in.buildings.push_back(ib);  // contributes nothing
    in.connectionCount[0] = 3;
    vl.monthlyUpdate(in);
    CHECK(vl.v3000.Slast == 90);              // 9 tiles * 10
    CHECK(vl.v7000.Slast == 45);              // workforce round(90 * 0.5)
    CHECK(vl.v5001.Slast == 300);             // 3 road connections * 100
    CHECK(vl.v7000.Dlast == 32 && vl.v3000.Dlast == 32);  // commerce 4 tiles * 8 jobs, JobToResFactor 1
    CHECK(vl.v5000.Slast == 20);
    CHECK(vl.v5000.Dlast == 23 && vl.v8000.Dlast == 22);  // 45 split 1:1: 22 + 22, remainder to commerce
    CHECK(vl.resUnits == 90 && vl.comUnits == 20 && vl.indUnits == 0);
    CHECK(vl.effectMap[size_t(2) * 16 + 2] == 10 && vl.effectMap[size_t(8) * 16 + 8] == 5 + 8);
    CHECK(vl.v7000.Sacc == 40);               // external workforce of the new job cycle
    CHECK(vl.reading(3000) == vl.v3000.N);
  }

  // Records
  { Bytes r = vl.writeResidential(); CHECK(r.size() == 1994); Bytes rm = vl.writeResidential(true); CHECK(rm.size() == 2046);
    Bytes c = vl.writeCommercial(); CHECK(c.size() == 94); CHECK(vl.writeCommercial(true).size() == 144);
    Bytes i = vl.writeIndustrial(); CHECK(i.size() == 94); CHECK(vl.writeIndustrial(true).size() == 144);
    ValveLayer o; o.init(16, 16, ValveTuning());
    CHECK(o.parseResidential(rm.data(), rm.size())); CHECK(o.v3000.Slast == vl.v3000.Slast && o.residentialState.size() == 1900);
    CHECK(o.writeResidential() == r);
    CHECK(o.parseCommercial(c.data(), c.size()) && o.writeCommercial() == c);
    CHECK(o.parseIndustrial(i.data(), i.size()) && o.writeIndustrial() == i);
    Bytes cm = vl.writeCommercial(true); CHECK(o.parseCommercial(cm.data(), cm.size()) && o.v5001.Slast == vl.v5001.Slast);
    Bytes bad = c; bad[0] = 9; CHECK(!o.parseCommercial(bad.data(), bad.size()));
    CHECK(!o.parseCommercial(c.data(), 20));
    Bytes rc = vl.writeRci(); CHECK(rc.size() == 8 + 2 * 256 + 24);
    CHECK(o.parseRci(rc.data(), rc.size()) && o.effectMap == vl.effectMap && o.resUnits == 90 && o.writeRci() == rc);
    Bytes w = vl.writeWorld(); CHECK(w.size() == 725); CHECK(o.parseWorld(w.data(), w.size()) && o.writeWorld() == w);
    Bytes dw = defaultWorldRecord(); CHECK(dw.size() == 725); CHECK(rd32(dw.data() + 0x10) == 150);
  }
  // Ordinance effect: Dacc += trunc(Dlast * f / 1000)
  { ValveLayer o; o.init(16, 16, t); o.v3000.Dlast = 1000;
    ValveCityInput in; in.enactedOrdinances.insert(0x1234); in.year = 1900; in.month = 3;
    o.monthlyUpdate(in); CHECK(o.v3000.Dlast == -100); }

  // Spec 15 s3.2 smoothing test vectors (Max 2000, Min -2000, step 280)
  { Valve v; v.smooth = true; v.V = 1540; v.Sacc = 4000;   // target -2000
    v.update(); CHECK(v.V == 0);
    for (int i = 0; i < 8; i++) { v.Sacc = 4000; v.update(); }
    CHECK(v.V == -2000);
    Valve a; a.smooth = true; a.V = -1000; a.Dacc = 1600; a.update(); CHECK(a.V == 0); a.Dacc = 1600; a.update(); CHECK(a.V == 280);
    Valve b; b.smooth = true; b.V = 1500; b.Dacc = 1700; b.update(); CHECK(b.V == 1700);
    Valve c; c.smooth = true; c.V = 200; c.Sacc = 2000; c.update(); CHECK(c.V == -80);
  }
  // Every valve is smoothed (spec 15 s3.2) [STATIC]
  { ValveLayer o; o.init(16, 16, ValveTuning());
    for (int id : {3000, 3001, 5000, 5001, 7000, 8000, 8001}) CHECK(o.valve(id).smooth); }
  // Cap rule (spec 15 s3.2): Madison test vectors and the D = 0 / S < D cases
  { Valve a; a.Slast = 739500; a.Dlast = 150984; CHECK(capFromAmenity(a) == 20);
    a.Slast = 282000; a.Dlast = 33668; CHECK(capFromAmenity(a) == 11);
    a.Slast = 503200; a.Dlast = 120493; CHECK(capFromAmenity(a) == 23);
    a.Slast = 25000; a.Dlast = 71566; CHECK(capFromAmenity(a) == 100);
    a.Slast = 25000; a.Dlast = 0; CHECK(capFromAmenity(a) == 0); }
  // The cap is set after the update from the cycle just closed, and takes effect next month.
  { ValveLayer o; o.init(16, 16, ValveTuning());
    o.v3001.Sacc = 1000; o.v3001.Dacc = 500; o.v3000.capPct = 0; o.v3000.Dacc = 5000;
    ValveCityInput in; in.year = 1900; in.month = 3;
    o.monthlyUpdate(in);
    CHECK(o.v3000.capPct == 50);
    CHECK(o.v3000.Dlast == 5000 && o.v3000.V == 280);   // smoothed toward 2000; cap 0 still in force this month
  }
  // Industrial ordinance demand cut (spec 15 s3.2): Berlin 8000 Dlast 113,594, effect 3 -> Dacc -3,407
  { ValveTuning ti; ti.powerConservationOrdEffect = 3; ti.waterConservationOrdEffect = 3;
    ValveLayer o; o.init(16, 16, ti); o.v8000.Dacc = 113594;
    ValveCityInput in; in.year = 1900; in.month = 3; in.enactedOrdinances.insert(0x82B9999B);
    o.monthlyUpdate(in);
    CHECK(o.v8000.Dlast == 113594);
    ValveLayer n; n.init(16, 16, ti); n.v8000.Dacc = 113594;
    ValveCityInput in0; in0.year = 1900; in0.month = 3;
    n.monthlyUpdate(in0);
    CHECK((o.v8000.Dacc - n.v8000.Dacc) == -3407);
    ValveLayer q; q.init(16, 16, ti); q.v8000.Dacc = 113594;
    ValveCityInput in2 = in; in2.enactedOrdinances.insert(0x02BF1DFA);
    q.monthlyUpdate(in2);
    CHECK((q.v8000.Dacc - o.v8000.Dacc) == -3407);
  }
  // Nearest ancestor entry (spec 15 s3.3)
  { ValveTuning ti; ti.agentEffects[3000] = {{3000, 7, 0}}; ti.agentEffects[3100] = {{3000, 5, 0}};
    ti.parent[3110] = 3100; ti.parent[3100] = 3000; ti.parent[3999] = 3000;
    CHECK(ti.effectsFor(3110) && (*ti.effectsFor(3110))[0].supply == 5);
    CHECK((*ti.effectsFor(3999))[0].supply == 7);
    CHECK(ti.effectsFor(4242) == nullptr); }
  // Unemployment (spec 20 s6): Berlin and Craterville vectors via K and J
  { CHECK(unemploymentPercent(513952, 1.0, 425561) == 8);
    CHECK(unemploymentPercent(140724, 1.0, 126521) == 5);
    CHECK(unemploymentPercent(0, 0.6, 0) == 0);
    CHECK(unemploymentPercent(1000, 0.5, 600) == 0);
    CHECK(unemploymentPercent(1000, 0.5, 0) == 100); }
  TEST_MAIN_END();
}
