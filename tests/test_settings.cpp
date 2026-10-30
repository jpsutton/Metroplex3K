#include <map>
#include "core/cheats.h"
#include "core/settings.h"
#include "testutil.h"
using namespace m3k;

int main() {
  // languages
  CHECK(std::string(languageFolder(1)) == "English" && std::string(languageFolder(20)) == "Korean" && !languageFolder(21));
  CHECK(languageFromName("german") == 4 && languageFromName("ENGLISH-UK") == 2 && languageFromName("english-us") == 1);
  CHECK(languageFromName("9") == 9 && languageFromName("44") == 0 && languageFromName("klingon") == 0 && languageFromName("") == 0);
  CHECK(languageFromLangId(0x0409) == 1 && languageFromLangId(0x0809) == 2 && languageFromLangId(0x0C09) == 2);
  CHECK(languageFromLangId(0x040C) == 3 && languageFromLangId(0x0407) == 4 && languageFromLangId(0x0410) == 5);
  CHECK(languageFromLangId(0x040A) == 6 && languageFromLangId(0x0C0A) == 6 && languageFromLangId(0x500A) == 6);
  CHECK(languageFromLangId(0x080A) == 41 && languageFromLangId(0x0413) == 7 && languageFromLangId(0x041D) == 9);
  CHECK(languageFromLangId(0x0401) == 22 && languageFromLangId(0x4001) == 22 && languageFromLangId(0x0804) == 17);
  CHECK(languageFromLangId(0x0404) == 18 && languageFromLangId(0x042A) == 43 && languageFromLangId(0x1009) == 1);
  CHECK(languageFromLangId(0x0000) == 1 && languageFromLangId(0xFFFF) == 1 && languageFromLangId(0x0439) == 21);
  for (int id = 0; id < 65536; id++) CHECK(languageFromLangId(uint16_t(id)) >= 1);
  {
    LanguageInputs in;
    in.installed = [](int id) { return id == 1 || id == 2 || id == 3 || id == 4 || id == 5 || id == 7 || id == 9; };
    in.registryDword = 1036;
    CHECK(chooseLanguage(in) == 3);
    in.registryDword = 2057; CHECK(chooseLanguage(in) == 2);
    in.registryDword = 4; CHECK(chooseLanguage(in) == 4);
    in.registryDword = 0x0415;  // Polish, not installed -> cfg
    in.cfgLanguage = 9; CHECK(chooseLanguage(in) == 9);
    in.cmdLine = "german"; CHECK(chooseLanguage(in) == 4);
    in.cmdLine = "japanese"; CHECK(chooseLanguage(in) == 9);  // not installed -> ignored
    in.cmdLine = ""; in.registryDword.reset(); in.registryString = "Dutch"; in.cfgLanguage = 5;
    CHECK(chooseLanguage(in) == 7);
    in.registryString.reset();
    CHECK(chooseLanguage(in) == 5);
    in.cfgLanguage = -1; in.defaultLocale = "italian"; CHECK(chooseLanguage(in) == 5);
    in.defaultLocale = "polish"; CHECK(chooseLanguage(in) == 1);
    in.ctrlShiftHeld = true; in.cfgLanguage = 3; in.registryDword = 1036; CHECK(chooseLanguage(in) == 1);
    LanguageInputs none; none.installed = [](int) { return false; };
    CHECK(chooseLanguage(none) == 1);
    CountryInputs c;
    CHECK(chooseCountry(c) == 54);
    c.registryDword = 17; CHECK(chooseCountry(c) == 17);
    c.registryDword = 57; CHECK(chooseCountry(c) == 54);
    c.registryDword.reset(); c.registryString = "31"; CHECK(chooseCountry(c) == 31);
    c.cmdLine = "6"; CHECK(chooseCountry(c) == 6);
    CHECK(paradeNameInstance(54) == 0x260 && paradeNameInstance(6) == 0x258 && paradeNameInstance(1) == 0);
  }
  // command line
  {
    auto t = tokenizeCommandLine("  -r1024x768 \"-dMy Device\"\t-f\n-p 300 128");
    CHECK(t.size() == 6 && t[1] == "-dMy Device" && t[5] == "128");
    CommandLine c = parseCommandLine("-r1024x768 -dDISPLAY1 -f -lGerman -c19 -x1 -intro:off -sound:OFF -music:on -cheats:abc /w");
    CHECK(c.hasResolution && c.width == 1024 && c.height == 768 && c.device == "DISPLAY1");
    CHECK(c.fullscreen && !*c.fullscreen);  // last wins
    CHECK(c.language == "German" && c.country == "19" && c.cheatsEnabled && c.cheatsValue == "abc");
    CHECK(!c.intro && !c.sound && c.music && !c.restart && c.ignored.size() == 1);
    CommandLine d = parseCommandLine("\\R640x480 -R800X600 -intro:ofxx -restart:on -dFoo");
    CHECK(!d.hasResolution && d.device.empty() && d.restart && !d.intro);
    CommandLine e = parseCommandLine("-p500 64 7 6 file.sc3 -UpdaterApp:\"C:\\up dater\"");
    CHECK(e.profile && e.profile->cpuMHz == 500 && e.profile->memoryMB == 64 && e.profile->cpuGen == 7 && e.profile->memClass == 6);
    CHECK(e.positional.size() == 1 && e.positional[0] == "file.sc3" && e.updaterApp == "C:\\up dater");
    CommandLine f = parseCommandLine("-p 5000 4 99 1");
    CHECK(f.profile->cpuMHz == 1000 && f.profile->memoryMB == 16 && f.profile->cpuGen == 10 && f.profile->memClass == 4);
    CommandLine g = parseCommandLine("-p");
    CHECK(g.profile && g.profile->cpuMHz == 200 && g.profile->memoryMB == 128 && g.profile->cpuGen == 6 && g.profile->memClass == 5);
    const char* av[] = {"prog", "-w", "-r640x480"};
    CommandLine h = parseCommandLine(3, av);
    CHECK(h.width == 640 && h.fullscreen && !*h.fullscreen);
    CommandLine dflt = parseCommandLine("");
    CHECK(dflt.intro && dflt.sound && dflt.music && !dflt.fullscreen && !dflt.hasResolution);
  }
  // display
  {
    CHECK(displayModes().size() == 3);
    CHECK(uiSegmentName({640, 480}) == "00640480_MainUI.IXF" && uiSegmentName({800, 600}) == "00800600_MainUI.IXF" &&
          uiSegmentName({1024, 768}) == "01024768_MainUI.IXF");
    CHECK(machineClass(4, 500) == 1 && machineClass(5, 189) == 2 && machineClass(5, 190) == 3 && machineClass(6, 259) == 4 &&
          machineClass(6, 260) == 5 && machineClass(8, 299) == 5 && machineClass(8, 300) == 6 && machineClass(10, 1) == 7);
    CHECK(memoryClass(128) == 7 && memoryClass(100) == 6 && memoryClass(1) == 0);
    CHECK(roundMemoryMB(uint64_t(127) << 20) == 128 && roundMemoryMB(uint64_t(128) << 20) == 128);
    MachineProfile p = profileFromMeasurement(6, 50, uint64_t(65) << 20);
    CHECK(p.cpuMHz == 180 && p.memoryMB == 68 && p.memClass == 6);
    CommandLine none;
    CHECK(chooseDisplayMode(3, 0, 0, none).w == 640 && chooseDisplayMode(4, 0, 0, none).w == 800);
    CHECK(chooseDisplayMode(3, 1024, 768, none).h == 768);
    CommandLine r = parseCommandLine("-r800x600");
    CHECK(chooseDisplayMode(3, 1024, 768, r).w == 800);
    CHECK(defaultDetail(1).l1 == 1 && defaultDetail(2).l1 == 2 && defaultDetail(2).l3 == 0 && defaultDetail(3).l3 == 1 &&
          defaultDetail(4).l2 == 2 && defaultDetail(7).l1 == 3);
  }
  // SC3.cfg
  {
    GeneralSettings g;
    CHECK(g.deviceName().empty() && g.language == -1 && g.musicVolume == 1024 && g.flagF && !g.flagA);
    g.string1 = "user"; g.string2 = "co"; g.language = 4; g.width = 800; g.height = 600; g.windowed = true;
    g.setDeviceName("Primary"); g.sfxVolume = 5000; g.detail = defaultDetail(4); g.value = 77;
    Bytes b = g.serialize();
    CHECK(b.size() == 4 + 4 + 4 + 2 + 0x5F);
    // offset check relative to S = 14
    CHECK(rd32(&b[14]) == 4 && rd32(&b[14 + 5]) == 800 && b[14 + 0x0D] == 1 && b[14 + 0x0F] == 'P' && b[14 + 0x30] == 1);
    CHECK(rd32(&b[14 + 0x4E]) == 2 && rd32(&b[14 + 0x5B]) == 77);
    GeneralSettings h;
    CHECK(GeneralSettings::parse(b, h) && h.deviceName() == "Primary" && h.string1 == "user" && h.language == 4 && h.value == 77);
    h.repairVolumes();
    CHECK(h.sfxVolume == 700 && h.musicVolume == 1024);
    GeneralSettings t;
    CHECK(!GeneralSettings::parse(Bytes(b.begin(), b.end() - 1), t));
    Sc3Cfg cfg;
    cfg.general = g; cfg.hasGeneral = true;
    cfg.setIdFlags(2, {{7, true}, {9, false}});
    cfg.setIdFlags(13, {{1, true}}, 42);
    Bytes r3(12 + 128, 0); cfg.blocks[3] = r3;
    cfg.blocks[4] = Bytes{1};
    Bytes ixf = cfg.serialize();
    Sc3Cfg back = Sc3Cfg::parse(ixf);
    CHECK(back.hasGeneral && back.general.width == 800 && back.blocks.size() == 4 && back.warnings.empty());
    auto f2 = back.idFlags(2);
    CHECK(f2.size() == 2 && f2[0].id == 7 && f2[0].flag && !f2[1].flag);
    CHECK(back.idFlags(13).size() == 1 && rd32(&back.blocks[13][0]) == 42);
    CHECK(back.serialize() == ixf);
    // bad record is skipped, others survive
    IxfWriter w;
    CfgRecordId i1 = cfgRecordId(1), i4 = cfgRecordId(4), i7 = cfgRecordId(7);
    w.add({i1.type, kCfgGroup, i1.instance}, Bytes{1, 2});
    w.add({i4.type, kCfgGroup, i4.instance}, Bytes{1});
    w.add({i7.type, kCfgGroup, i7.instance}, Bytes{1, 2, 3});
    Sc3Cfg partial = Sc3Cfg::parse(w.build());
    CHECK(!partial.hasGeneral && partial.blocks.size() == 1 && partial.blocks.count(4) && partial.warnings.size() == 2);
    CHECK(Sc3Cfg::parse(Bytes{1, 2, 3, 4}).warnings.size() == 1);
    CHECK(cfgRecordId(1).type == 0xA352F1B5 && cfgRecordId(15).instance == 0xBA3512FD);
    CHECK(validateCfgBlock(14, Bytes(32, 0)) && !validateCfgBlock(14, Bytes(31, 0)) && validateCfgBlock(15, Bytes{1, 0, 0, 0, 'x'}) &&
          !validateCfgBlock(15, Bytes{9, 0, 0, 0, 'x'}) && validateCfgBlock(10, Bytes(44, 0)) && validateCfgBlock(11, Bytes(28, 0)));
  }
  // preferences
  {
    Preferences p = Preferences::parse("# c\nwindowed = 0\nui_scale=3\n;x\nname=Bob Smith\nbad line\nvsync=yes\n");
    CHECK(!p.getBool("windowed", true) && p.getInt("ui_scale", 0) == 3 && p.get("name") == "Bob Smith" && p.getBool("vsync", false));
    CHECK(p.getInt("name", 9) == 9 && p.getInt("missing", 5) == 5 && !p.has("bad line"));
    OwnPrefs o = OwnPrefs::fromPreferences(Preferences());
    CHECK(o.windowed && o.uiScale == 0 && o.vsync && o.gameDir.empty());
    o.windowed = false; o.gameDir = "/games/sc3k";
    Preferences q; o.toPreferences(q);
    Preferences r = Preferences::parse(q.serialize());
    OwnPrefs o2 = OwnPrefs::fromPreferences(r);
    CHECK(!o2.windowed && o2.gameDir == "/games/sc3k");
    fs::path tmp = fs::temp_directory_path() / "m3k_prefs_test" / "p.cfg";
    CHECK(r.save(tmp));
    Preferences s; CHECK(s.load(tmp) && s.get("game_dir") == "/games/sc3k");
    CHECK(!s.load(tmp.parent_path() / "nope.cfg"));
    fs::remove_all(tmp.parent_path());
    CHECK(!defaultPrefsDir().empty());
  }
  // LANGID table of spec 05 2.4: every row, and everything else maps to 1.
  {
    std::map<int, int> want;
    auto row = [&](std::initializer_list<int> ids, int lang) { for (int i : ids) want[i] = lang; };
    row({0x0809, 0x0C09, 0x1409, 0x1809, 0x1C09, 0x2009, 0x2409, 0x2809, 0x2C09, 0x3009, 0x3409}, 2);
    row({0x040C, 0x080C, 0x0C0C, 0x100C, 0x140C, 0x180C}, 3);
    row({0x0407, 0x0807, 0x0C07, 0x1007, 0x1407}, 4);
    row({0x0410, 0x0810}, 5);
    for (int hi = 0x04; hi <= 0x50; hi += 0x04) if (hi != 0x08) want[(hi << 8) | 0x0A] = 6;
    row({0x080A}, 41); row({0x0413, 0x0813}, 7); row({0x0406}, 8); row({0x041D, 0x081D}, 9); row({0x0414, 0x0814}, 10);
    row({0x040B}, 11); row({0x040D}, 12); row({0x0419}, 13); row({0x0416, 0x0816}, 14); row({0x0411}, 15); row({0x0415}, 16);
    row({0x0804, 0x1004, 0x1404}, 17); row({0x0404, 0x0C04}, 18); row({0x041E}, 19); row({0x0412, 0x0812}, 20);
    row({0x0439}, 21);
    for (int hi = 0x04; hi <= 0x40; hi += 0x04) want[(hi << 8) | 0x01] = 22;
    row({0x0402}, 23); row({0x0422}, 25); row({0x0405}, 26); row({0x0408}, 27); row({0x040E}, 28); row({0x040F}, 29);
    row({0x0418}, 30); row({0x041B}, 32); row({0x041C}, 33); row({0x041F}, 34); row({0x042A}, 43);
    for (int id = 0; id < 65536; id++) {
      auto it = want.find(id);
      CHECK(languageFromLangId(uint16_t(id)) == (it == want.end() ? 1 : it->second));
    }
    CHECK(langIdFromPosixLocale("de_DE.UTF-8") == 0x0407 && langIdFromPosixLocale("en_GB") == 0x0809 &&
          langIdFromPosixLocale("en_US.utf8") == 0x0409 && langIdFromPosixLocale("zh_TW") == 0x0404 &&
          langIdFromPosixLocale("sv_SE@euro") == 0x041D && langIdFromPosixLocale("tlh") == 0);
  }
  // registry-equivalent values from a Wine / Windows .reg text
  {
    const std::string reg =
        "WINE REGISTRY Version 2\n\n[Software\\\\Other] 1\n\"Language\"=dword:00000001\n\n"
        "[Software\\\\Wow6432Node\\\\Electronic Arts\\\\Maxis\\\\SimCity 3000 Unlimited] 1600000000\n"
        "#time=1d\n\"InstalledPath\"=\"C:\\\\GOG\\\\SC3U\\\\Apps\"\n\"Language\"=dword:00000409\n"
        "\"Country\"=dword:00000036\n\"ergc\"=\"abc\\\"d\"\n\"SKU\"=dword:00000002\n\n[Software\\\\Next]\n\"Language\"=dword:00000007\n";
    RegistryValues v = parseRegFile(reg);
    CHECK(v.found && v.languageDword && *v.languageDword == 0x409 && v.countryDword && *v.countryDword == 54);
    CHECK(v.installedPath == "C:\\GOG\\SC3U\\Apps" && v.ergc == "abc\"d" && v.sku && *v.sku == 2 && !v.languageString);
    RegistryValues w = parseRegFile("Windows Registry Editor Version 5.00\r\n\r\n[HKEY_LOCAL_MACHINE\\SOFTWARE\\Electronic Arts\\Maxis\\SimCity 3000 Unlimited]\r\n\"Language\"=\"German\"\r\n");
    CHECK(w.found && !w.languageDword && w.languageString && *w.languageString == "German");
    LanguageInputs li; CountryInputs ci;
    li.installed = [](int id) { return id == 1 || id == 4; };
    w.applyTo(li, ci);
    CHECK(chooseLanguage(li) == 4 && chooseCountry(ci) == 54);
    v.applyTo(li, ci);
    CHECK(chooseLanguage(li) == 1 && chooseCountry(ci) == 54);
    CHECK(!parseRegFile("[Software\\\\Other]\n\"Language\"=dword:00000004\n").found);
  }
  // cheats
  {
    CHECK(findCheatByPhrase("i am weak")->id == 0x62E8630B && findCheatById(0xE37FB7B8)->phrase == std::string("***"));
    CHECK(findCheatByPhrase("broccoli")->id == 0x2352E8C7 && !findCheatById(0));
    std::set<uint32_t> idset;
    for (const auto& c : cheatTable()) if (c.id) CHECK(idset.insert(c.id).second);
    CHECK(idset.size() == 21 + 23);
    CheatMatch m = matchCheat("simon says hello there");
    CHECK(m && std::string(m.info->phrase) == "simon says" && m.arg == "hello there" && tickerMessageFor(m) == 0x341);
    CHECK(std::string(matchCheat("simcity").info->phrase) == "simcity" && std::string(matchCheat("sim").info->phrase) == "sim");
    CHECK(tickerMessageFor(matchCheat("MAXIS")) == 0x32B && tickerMessageFor(matchCheat("maxis")) == 0x32C);
    CHECK(tickerMessageFor(matchCheat("help")) == 0x338 && tickerMessageFor(matchCheat("HELP")) == 0x337);
    CHECK(!matchCheat("nonsense") && !matchCheat(""));
    CHECK(matchCheat("traffic lights on").arg == "on");
    CheatRegistry reg;
    CheatResult r = reg.submit("I am weak");
    CHECK(r.stateOn && r.newlyLogged && reg.active(0x62E8630B) && reg.active("I am weak"));
    r = reg.submit("i am weak");
    CHECK(!r.stateOn && !r.newlyLogged && !reg.active(0x62E8630B));
    r = reg.submit("I am weak");
    CHECK(r.stateOn && !r.newlyLogged && reg.log().size() == 1);   // logged at most once
    r = reg.submit("power to the masses");
    CHECK(r.newlyLogged && reg.log().size() == 2);
    r = reg.submit("fund");
    CHECK(r.tickerMsg == 0x326 && !r.newlyLogged && reg.log().size() == 2);  // ticker never logged
    r = reg.submit("tuneup");
    CHECK(reg.log().size() == 2);
    r = reg.submit("nerdz rool");
    CHECK(r.stateOn && reg.active("nerdz rool") && reg.log().size() == 2);  // no known ID: not logged
    reg.resetSession();
    CHECK(!reg.active("nerdz rool") && !reg.active(0x62E8630B) && reg.log().size() == 2);
    // records
    Bytes b = serializeCheatLog({1, 2, 0xC2FAA2F5});
    CHECK(b.size() == 8 + 4 + 12 && b[2] == 2 && rd32(&b[4]) == 0xDEADBEEF);
    std::vector<uint32_t> ids;
    CHECK(parseCheatLog(b, ids) && ids.size() == 3 && ids[2] == 0xC2FAA2F5);
    Bytes bad = b; bad[4] = 0;
    CHECK(!parseCheatLog(bad, ids) && ids.empty());
    Bytes big = serializeCheatLog(std::vector<uint32_t>(2000, 7));
    CHECK(parseCheatLog(big, ids) && ids.size() == 1024);
    uint32_t cv = 0;
    CHECK(parseCheatCount(serializeCheatCount(13), cv) && cv == 13);
    IxfWriter w; w.add({1, 2, 3}, Bytes{4});
    w.add(kCheatCountKey, serializeCheatCount(5));
    Bytes city = w.build();
    CHECK(!readCheatLog(city, ids));
    Bytes c2 = writeCheatLog(city, {0x62E8630B});
    CHECK(readCheatLog(c2, ids) && ids.size() == 1);
    IxfFile f; CHECK(f.openMemory(c2) && f.has({1, 2, 3}) && f.has(kCheatCountKey));
    Bytes c3 = writeCheatLog(c2, {});
    CHECK(!readCheatLog(c3, ids));
    IxfFile f3; CHECK(f3.openMemory(c3) && f3.has(kCheatCountKey));
  }
  TEST_MAIN_END();
}
