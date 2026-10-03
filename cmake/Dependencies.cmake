# Third-party dependencies, fetched at configure time.
#
# To build offline, point FetchContent at local checkouts, e.g.
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
#   -DFETCHCONTENT_SOURCE_DIR_DOCTEST=/path/to/doctest

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

if(BOUNCE_BUILD_PLUGIN)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.9
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(BOUNCE_BUILD_TESTS)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.11
        GIT_SHALLOW    TRUE)
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(doctest)
endif()
