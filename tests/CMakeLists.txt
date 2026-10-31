enable_testing()

# kreart-core-test
add_executable(kreart-core-test
    core_test.cpp
)
target_link_libraries(kreart-core-test PRIVATE
    kreart-core
    kreart-io
    kreart-brush
    Qt6::Core
)
add_test(NAME kreart-core-test COMMAND kreart-core-test)

# kreart-resources-test
add_executable(kreart-resources-test
    resources_test.cpp
)
target_compile_definitions(kreart-resources-test PRIVATE
    KREART_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_link_libraries(kreart-resources-test PRIVATE
    kreart-resources
    kreart-brush
    Qt6::Core
)
add_test(NAME kreart-resources-test COMMAND kreart-resources-test)

# kreart-color-history-test
add_executable(kreart-color-history-test
    color_history_test.cpp
)
target_link_libraries(kreart-color-history-test PRIVATE
    kreart-ui
    Qt6::Core
)
add_test(NAME kreart-color-history-test COMMAND kreart-color-history-test)

# kreart-ui-test
add_executable(kreart-ui-test
    ui_test.cpp
    ${CMAKE_SOURCE_DIR}/data/kreart.qrc
)
target_link_libraries(kreart-ui-test PRIVATE
    kreart-ui
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Svg
)
add_test(NAME kreart-ui-test COMMAND kreart-ui-test)

# kreart-mobile-ui-test
add_executable(kreart-mobile-ui-test
    mobile_ui_test.cpp
    ${CMAKE_SOURCE_DIR}/data/kreart.qrc
)
target_link_libraries(kreart-mobile-ui-test PRIVATE
    kreart-ui
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Svg
    Qt6::Test
)
add_test(NAME kreart-mobile-ui-test COMMAND kreart-mobile-ui-test)
