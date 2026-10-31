qt_add_executable(kreart MANUAL_FINALIZATION
    main.cpp
    ${CMAKE_SOURCE_DIR}/data/kreart.qrc
)

if(ANDROID)
    set_target_properties(kreart PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/data/android"
        QT_ANDROID_TARGET_SDK_VERSION "34"
        QT_ANDROID_SDK_BUILD_TOOLS_REVISION "34.0.0"
    )
endif()

target_link_libraries(kreart PRIVATE
    kreart-ui
)

if(COMMAND qt_finalize_executable)
    qt_finalize_executable(kreart)
endif()

if(ANDROID)
    add_custom_target(kreart_apk
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/android-build/libs/${CMAKE_ANDROID_ARCH_ABI}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:kreart>" "${CMAKE_CURRENT_BINARY_DIR}/android-build/libs/${CMAKE_ANDROID_ARCH_ABI}/libkreart_${CMAKE_ANDROID_ARCH_ABI}.so"
        COMMAND "${QT_HOST_PATH}/bin/androiddeployqt"
            --input "${CMAKE_CURRENT_BINARY_DIR}/android-kreart-deployment-settings.json"
            --output "${CMAKE_CURRENT_BINARY_DIR}/android-build"
            --apk "${CMAKE_CURRENT_BINARY_DIR}/android-build/kreart.apk"
            --android-platform android-34
        DEPENDS kreart
        COMMENT "Packaging Kreart Android APK"
    )
endif()

install(TARGETS kreart
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
