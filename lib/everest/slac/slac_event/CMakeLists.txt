add_library(slac_event)
add_library(slac::event ALIAS slac_event)
ev_register_library_target(slac_event)

target_include_directories(slac_event
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_sources(slac_event
    PRIVATE
    src/slac_event.cpp
    src/slac_socket.cpp
    src/slac.cpp

    #fsm
    src/fsm/misc.cpp
    src/fsm/context.cpp
    src/fsm/states/others.cpp
    src/fsm/states/matching.cpp
    src/fsm/states/matching_handle_slac.cpp

)

target_link_libraries(slac_event
    PUBLIC
        everest::io
        everest::tls
        fsm::fsm
)
