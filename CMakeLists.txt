PROJECT( kio_upnp_ms )
FIND_PACKAGE(KDE4 REQUIRED)
INCLUDE_DIRECTORIES( ${KDE4_INCLUDES} . build )

set(kio_upnp_ms_PART_SRCS
   kio_upnp_ms.cpp
   dbuscodec.cpp
   )

find_path( HUpnp_INCLUDE_DIRS HUpnp )
find_library( HUpnp_LIBS HUpnp )

kde4_add_plugin(kio_upnp_ms ${kio_upnp_ms_PART_SRCS})

include_directories( ${HUpnp_INCLUDE_DIRS} )
target_link_libraries(kio_upnp_ms ${KDE4_KIO_LIBS})
target_link_libraries(kio_upnp_ms ${HUpnp_LIBS})

install(TARGETS kio_upnp_ms  DESTINATION ${PLUGIN_INSTALL_DIR})

########### install files ###############

install(FILES kio_upnp_ms.protocol DESTINATION ${SERVICES_INSTALL_DIR})
