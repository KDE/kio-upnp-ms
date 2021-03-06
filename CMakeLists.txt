PROJECT( kio_upnp_ms )

set( CMAKE_MODULE_PATH  ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules )

FIND_PACKAGE(KDE4 REQUIRED)
set(KIO_UPNP_MS_MAJOR_VERSION "1")
set(KIO_UPNP_MS_MINOR_VERSION "0")
set(KIO_UPNP_MS_PATCH_VERSION "0")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR})
find_package(HUpnp REQUIRED)

if ( HUPNP_FOUND AND ((HUPNP_VERSION_MAJOR EQUAL 0 AND HUPNP_VERSION_MINOR EQUAL 9) OR (HUPNP_VERSION_MAJOR EQUAL 1)))
else()
    message(FATAL_ERROR "The UPnP MediaServer kioslave requires the HUpnp library version>=0.9.0 -- http://www.herqq.org")
endif()

INCLUDE_DIRECTORIES( ${KDE4_INCLUDES}
    ${CMAKE_SOURCE_DIR} build . )

set(kio_upnp_ms_PART_SRCS
   kio_upnp_ms.cpp
   didlparser.cpp
   didlobjects.cpp
   controlpointthread.cpp
   objectcache.cpp
   persistentaction.cpp
   )

kde4_add_plugin(kio_upnp_ms ${kio_upnp_ms_PART_SRCS})

include_directories( ${HUPNP_INCLUDE_DIR} )
target_link_libraries(kio_upnp_ms ${KDE4_KIO_LIBS})
target_link_libraries(kio_upnp_ms ${HUPNP_LIBS})

install(TARGETS kio_upnp_ms  DESTINATION ${PLUGIN_INSTALL_DIR})

########### install files ###############

install(FILES kio_upnp_ms.protocol DESTINATION ${SERVICES_INSTALL_DIR})

# so other programs can access the types
install(FILES upnp-ms-types.h DESTINATION
    ${INCLUDE_INSTALL_DIR}/kio COMPONENT devel)
 
########### test programs ##############
if(KDE4_BUILD_TESTS)
    KDE4_ADD_EXECUTABLE(upnpmstest tests/upnpmstest.cpp)

    TARGET_LINK_LIBRARIES(upnpmstest ${KDE4_KDEUI_LIBS}
        ${KDE4_KPARTS_LIBS} )

    KDE4_ADD_EXECUTABLE(stattest tests/stattest.cpp)

    TARGET_LINK_LIBRARIES(stattest ${KDE4_KDEUI_LIBS}
        ${KDE4_KPARTS_LIBS} )

    KDE4_ADD_EXECUTABLE(recursive_upnp tests/recursive_upnp.cpp)

    TARGET_LINK_LIBRARIES(recursive_upnp ${KDE4_KDEUI_LIBS} ${KDE4_KPARTS_LIBS})

    install(TARGETS upnpmstest  DESTINATION ${BIN_INSTALL_DIR})
    install(TARGETS stattest  DESTINATION ${BIN_INSTALL_DIR})
    install(TARGETS recursive_upnp DESTINATION ${BIN_INSTALL_DIR})
endif()
