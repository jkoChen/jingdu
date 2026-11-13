cmake_minimum_required(VERSION 3.16)
project(jingdu_host LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(JD_SANITIZE "Address and undefined behavior sanitizers" ON)
if(JD_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()
set(F lib/FreeInkFont)
set(B lib/FreeInkBook)
file(GLOB FT_SOURCES ${F}/src/freetype/*.c)
add_library(jd_core STATIC
  src/learning/Learning.cpp
  src/reader/Chapters.cpp
  src/reader/GlyphCache.cpp
  src/reader/GlyphWindow.cpp
  src/reader/Epub.cpp
  src/reader/Reader.cpp
  src/reader/StreamCmap.cpp
  src/reader/Text.cpp
  src/storage/Bookshelf.cpp
  src/storage/Storage.cpp
  src/ui/App.cpp
  src/ui/Canvas.cpp
  src/ui/Controls.cpp
  src/ui/LearningPages.cpp
  src/ui/Lunar.cpp
  src/ui/ReadingPages.cpp
  src/ui/ReadingSession.cpp
  src/ui/SettingsPages.cpp
  src/ui/ViewData.cpp
  ${F}/src/FontAlloc.c
  ${F}/src/FtFont.cpp
  ${F}/src/Gpos.cpp
  ${F}/src/Gsub.cpp
  ${FT_SOURCES}
  ${B}/src/BookCatalog.cpp
  ${B}/src/css/Css.cpp
  ${B}/src/FreeInkBook.cpp
  ${B}/src/epub/XmlSax.cpp
  ${B}/src/epub/ZipCatalog.cpp
  ${B}/src/epub/PackageParsers.cpp
  ${B}/src/render/ImageRenderer.cpp
  ${B}/src/epub/ImageProbe.cpp
  ${B}/src/vendor/tjpgd_impl.c
  ${B}/src/vendor/pngle_impl.c
  ${B}/src/text/EntityFilter.cpp
  ${B}/src/vendor/expat_xmlparse.c
  ${B}/src/vendor/expat_xmlrole.c
  ${B}/src/vendor/expat_xmltok.c
  ${B}/src/vendor/miniz_impl.c
)
target_include_directories(jd_core PUBLIC include ${F}/include ${B}/include lib/ArduinoJson/src PRIVATE
 ${B}/third_party/expat ${B}/third_party/miniz ${B}/third_party/pngle ${B}/third_party/tjpgd ${F}/third_party/freetype/include ${F}/third_party/stb)
target_compile_definitions(jd_core PUBLIC JINGDU_SPLIT_INFLATE=1 FREEINK_BOOK_SMALL=1 FREEINK_FONT_STREAM_LOCA=1 FREEINK_FONT_ENABLE_NATIVE_HINTING=1 FREEINK_FONT_ENABLE_AUTOHINT=0)
if(NOT APPLE)
  find_package(OpenSSL REQUIRED)
  target_link_libraries(jd_core PUBLIC OpenSSL::Crypto)
endif()
add_executable(jingdu_test tests/system.cpp)
target_link_libraries(jingdu_test jd_core)
add_executable(jingdu_lan_test src/network/Lan.cpp tests/lan_runner.cpp)
target_compile_definitions(jingdu_lan_test PRIVATE JD_LAN_TEST=1)
target_include_directories(jingdu_lan_test PRIVATE tests)
target_link_libraries(jingdu_lan_test jd_core)
add_executable(jingdu_card_test tests/card.cpp)
target_link_libraries(jingdu_card_test jd_core)
add_executable(jingdu_study_font EXCLUDE_FROM_ALL scripts/study_font.cpp)
target_link_libraries(jingdu_study_font PRIVATE jd_core)
add_executable(jingdu_screen_test src/network/ScreenReceiver.cpp tests/screen_runner.cpp)
target_compile_definitions(jingdu_screen_test PRIVATE JD_LAN_TEST=1)
target_include_directories(jingdu_screen_test PRIVATE tests)
target_link_libraries(jingdu_screen_test PRIVATE jd_core)
add_executable(jingdu_screen_ui_test tests/screen_ui.cpp)
target_link_libraries(jingdu_screen_ui_test PRIVATE jd_core)
