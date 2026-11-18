# Outil hors plugin : compile le processeur + la fenetre de Backdraft sous Linux, teste le moteur et exporte des captures PNG
cmake_minimum_required(VERSION 3.22)
project(BackdraftRender VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)
add_subdirectory(${JUCE_DIR} JUCE)
set(W ${CMAKE_CURRENT_SOURCE_DIR}/../..)
file(STRINGS ${W}/CMakeLists.txt BD_PRJ REGEX "^project\\(Backdraft VERSION")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" BD_VER "${BD_PRJ}")
juce_add_binary_data(BackdraftAssets SOURCES ${W}/logo/NineHandsLogoWhite.svg
  ${W}/fonts/Poppins-Light.ttf ${W}/fonts/Poppins-Regular.ttf ${W}/fonts/Poppins-Medium.ttf ${W}/fonts/Poppins-Bold.ttf
  ${W}/assets/knob_shadow.png ${W}/assets/knob_crown.png ${W}/assets/knob_cap.png ${W}/assets/knob_marker.png ${W}/assets/knob_highlight.png)
juce_add_console_app(BackdraftRender PRODUCT_NAME "BackdraftRender")
target_sources(BackdraftRender PRIVATE main.cpp ${W}/PluginProcessor.cpp ${W}/PluginEditor.cpp ${W}/WindowMinSize.cpp)
target_include_directories(BackdraftRender PRIVATE ${W})
target_compile_definitions(BackdraftRender PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_ALSA=0 JUCE_JACK=0
  JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XSHM=0 JucePlugin_Name="Backdraft" JucePlugin_VersionString="${BD_VER}"
  JucePlugin_Build_VST3=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(BackdraftRender PRIVATE BackdraftAssets juce::juce_audio_utils juce::juce_audio_formats juce::juce_recommended_config_flags)
