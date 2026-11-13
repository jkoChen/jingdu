#pragma once
#include "Reader.h"
#include "Learning.h"
#include "Platform.h"
#include <functional>
namespace jd {
enum class View {
  Lock,
  Home,
  Shelf,
  Files,
  File,
  Book,
  Menu,
  Tools,
  Layout,
  Toc,
  Marks,
  Settings,
  Wifi,
  Password,
  Service,
  Learn,
  Courses,
  Catalog,
  Passage,
  Quiz,
  Feedback,
  Chunks,
  Dicts,
  Words,
  Definition,
  Review,
  Fonts,
  Jump,
  Vocab,
  Recall,
  Choice,
  RemoveMark,
  Message,
  Clock,
  FontPrep,
  Refresh,
  AutoLock,
  CardInfo,
  ClockEdit,
  AutoClock,
  Power,
  ConfirmLongSleep,
  ConfirmRestart,
  ScreenConnect,
  ScreenCanvas,
  ScreenInfo,
  ScreenKey,
  About
};
enum class PowerAction { None, ClockSleep, LongSleep, Restart };
class App {
  struct Hit {
    int x, y, w, h;
    std::function<void()> run;
  };
  Device& dev;
  std::vector<Hit> hits;
  struct Route {
    View view;
    unsigned offset, text;
  };
  std::vector<Route> stack;
  std::vector<unsigned> directoryPages;
  bool fileAdded = false;
  unsigned detailChoice = UINT_MAX, studyPageCount = 1;
  bool studying() const;
  void compact(int slot, int top, const Str&, std::function<void()>, bool primary = false);
  bool studyPager(unsigned pages);
  void pageCount(unsigned current, unsigned count);
  void brightnessControl();
  Str dir = "/", selected, ssid, password, course, dict, word, definition, fontPath, notice, lastBook,
      originDir;
  bool confirmStop = false;
  bool fromShelf = false, pickBook = false, showPass = false, joining = false;
  int keyboard = 0;
  unsigned offset = 0, article = 0, segment = 0, articles = 0, segments = 0, chunk = 0, textPage = 0,
           chosen = UINT_MAX, markIndex = 0;
  bool reveal = false;
  Lesson lesson;
  bool studyFontTried = false;
  void releaseStudy();
  Str answer;
  View previous = View::Home;
  int light = 4, cadence = 10, grip = 0, idleMins = 10;
  bool chapterPercent = false;
  bool pageAck = false, waitingResume = false, uncommitted = false;
  unsigned resumePercent = 0;
  uint32_t lastCommit = 0;
  bool committed = false;
  Anchor visible{};
  Str visiblePath;
  Anchor before{};
  bool hasBefore = false;
  void hit(int, int, int, int, std::function<void()>);
  void row(int, const Str&, std::function<void()>, const Str& detail = {});
  void btn(int, int, int, int, const Str&, std::function<void()>);
  void entry(int y, const Str& label, const Str& value, std::function<void()> action, const Str& detail = {});
  void option(int y, const Str& label, bool current, std::function<void()> action);
  std::function<void()> nextListPage;
  void menuLine(int y, const Str& label, const char* icon, std::function<void()> action);
  void stepper(int y, const Str& label, const Str& value, int current, int low, int high,
               std::function<void(int)> change);
  void primary(int x, int y, int w, const Str& label, std::function<void()> action);
  void header(const Str&, bool back = true);
  void pageControl(bool next, std::function<void()> action);
  void turnList(bool prev, bool next, unsigned total = 0, bool centerFree = true);
  void back();
  void go(View);
  enum class Recovery { Back, Card, Font, Shelf, Changed };
  Recovery recovery = Recovery::Back;
  void msg(const Str&, Recovery action = Recovery::Back);
  void openBook(const Str&);
  void addBook();
  FileIdentity shelfCheck;
  Str checkingShelf, visiblePending;
  void checkShelf(bool idle);
  void storeSettings();
  void position(bool force = false);
  void identityReady();
  Str markPath() const;
  bool mergeDraftMarks();
  Str resumeId, draftToken;
  bool resolving = false, versionChoice = false;
  Str fontDir = "/fonts";
  bool fontChosen = false;
  enum class FontStage { Open = 1, Warm, Render, Done };
  FontStage fontStage = FontStage::Open;
  Str pendingFont;
  bool prepReady = false, prepPainted = false;
  unsigned prepPercent = 0, warmIndex = 0;
  uint32_t phaseStarted = 0, lastFontFrame = 0;
  void startFont(const Str&);
  void setStage(FontStage);
  void prepareFont();
  void fontProgress();
  void finishFont();
  void failFont(const Str&);
  struct tm editTime{};
  int clockField = 0;
  void clockPage();
  void startClock();
  void fontRows();
  bool pendingBookDirectory = false;
  void startBookPicker(bool adding);
  void fileRows();
  void screenPage();
  void endScreen();
  void abortReading(const Str& reason);
  void reading();
  void learning();
  unsigned meaningPages(const Str& text, int top);
  void settings();
  void confirmPower(PowerAction);
  void lock();
  Str markIndexPath;
  std::unique_ptr<Anchor[]> markAnchors;
  unsigned markCount = 0;
  void invalidateMarks();
  bool marked();
  void noteMark(bool add);
  void enterLesson(bool restore);
  void choose(unsigned);
  void saveLesson();
  unsigned paragraph(const Str&, int, int, int, int, unsigned skip = 0, bool study = false);

 public:
  Canvas canvas;
  Reader reader;
  View view = View::Home;
  bool dirty = true, forceFull = true;
  PowerAction powerAction = PowerAction::None;
  bool sleepRequested() const {
    return powerAction != PowerAction::None;
  }
  unsigned idleMinutes() const {
    return dev.casting ? 0 : idleMins;
  }
  unsigned fullEvery() const {
    return cadence;
  }
  explicit App(Device& d) : dev(d) {}
  void begin(bool sleeping = false, bool resume = false);
  void tap(int x, int y, int releaseX, int releaseY, unsigned heldStillMs = 0);
  bool jumpFromReader(Anchor target);
  void single();
  void sleep();
  void background(bool idle = false);
  void paint();
  void displayed();
  bool inUse(const Str&) const;
  bool preparingFont() const {
    return view == View::FontPrep;
  }
  unsigned fontStageNumber() const {
    return unsigned(fontStage);
  }
  unsigned fontPercent() const {
    return prepPercent;
  }
  void networkChanged();
  void removed();
  void minute() {
    if (view == View::Lock) {
      dirty = true;
    }
  }
};
}  // namespace jd
